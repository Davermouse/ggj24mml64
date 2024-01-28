#include <libdragon.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/gl_integration.h>
#include <rspq_profile.h>
#include <math.h>
#include <unistd.h>

#include <chipmunk/chipmunk.h>

// Mixer channel allocation
#define CHANNEL_SFX1    0
#define CHANNEL_SFX2    1
#define CHANNEL_MUSIC   2

enum {
    FONT_IHATCS = 1,
    FONT_IHATCS_SMALL = 2,
};

typedef enum {
    GAME_STATE_ATTRACT = 1,
    GAME_STATE_STARTING = 2,
    GAME_STATE_PLAYING = 3,
    GAME_STATE_GAME_OVER = 4,
} GameState;

typedef enum {
    ITEM_BRICK,
    ITEM_CAT,
    ITEM_CHEESE,
    ITEM_BEANS
} ItemType;

typedef enum {
    STICK_SPRITE_NEUTRAL = 1,
    STICK_SPRITE_UP = 2,
    STICK_SPRITE_DOWN = 3,
} StickStatus;

ItemType levelProgression[] = {
    ITEM_CAT,
    ITEM_BRICK,
    ITEM_CHEESE,
    ITEM_BEANS
};

static int8_t stick_range = 80;
static bool debug = false;

GameState gameStatus = GAME_STATE_ATTRACT;
int state_start = 0;
int curr_time_ms = 0;

char subtitle_buffer[50];

ItemType currentItemType = ITEM_CHEESE;

StickStatus stickStatus = STICK_SPRITE_NEUTRAL;
StickStatus dpadStatus = STICK_SPRITE_NEUTRAL;

cpVect lung_pos;
cpVect mouth_pos;
cpVect stick_pos;
cpVect eyes_pos;
cpVect dpad_pos;
cpVect cpad_pos;
cpVect laughometer_pos;

cpVect item_pos;

float laughometer_level = 1.0f;
float laughometer_change = 0.0f;

int level = 0;
int level_change_time = 0;
int high_score = 0;
int score = 0;

float lung_scale = 1.0f;
bool lung_visible = true;
bool lung_ghost_visible = true;

float base_eye_scale = 0.8f;
float eye_scale = 1.0f;

float lung_target_speed = 1.0f;
float lung_target_scale = 1.0f;
float lung_breath_speed = 1.0f;

float target_speed = 1.0f;
float current_speed = 1.0f;

float mouth_angle = M_PI_4;
float mouth_target = M_PI / 8;
float mouth_target_speed = 1.0f;
bool mouth_ghost_visible = false;

char* title_text = "Make me laugh!";
char *subtitle_text = "Press start!";
bool title_visible = true;
bool subtitle_visible = true;

xm64player_t xm;

wav64_t sinister_laugh;

static const float FRAME_FACTOR = 1.0f/30;
static const float GRAVITY = 9.8;

void init() {
    lung_pos = cpv(100, 175);
    mouth_pos = cpv(220,175);
    eyes_pos = cpv(420, 180);

    laughometer_pos = cpv(320, 35);

    stick_pos = cpv(245, 275);
    dpad_pos = cpv(110, 303);
    cpad_pos = cpv(550, 290);

    item_pos = cpv(550, 150);
}

void play_laugh() {
    wav64_play(&sinister_laugh, CHANNEL_SFX1);

    float freq = sinister_laugh.wave.frequency * (((rand() % 200) + 100.0f) / 200);

    debugf("Sample freq original: %f updated: %f\n",sinister_laugh.wave.frequency, freq);
    mixer_ch_set_freq(CHANNEL_SFX1, freq);
}

void setup_speeds() {
    mouth_target_speed = 0.8f + level * 0.15f;
    lung_breath_speed = 0.8f + level * 0.15f;
}

void update_game_state(GameState new_state) {
    debugf("Change state from %i to %i at %i\n",
        gameStatus,
        new_state,
        curr_time_ms);

    state_start = curr_time_ms;
    gameStatus = new_state;
}

void start_attract() {
    update_game_state(GAME_STATE_ATTRACT);

    title_text = "Make Me Laugh!";
    subtitle_text = "Press start!";

    score = 0;

    lung_ghost_visible = false;
    mouth_ghost_visible = false;
}

void start_starting() {
    update_game_state(GAME_STATE_STARTING);
    subtitle_text = "";

    laughometer_level = 1.0f;
    laughometer_change = 0.0f;

    level = 0;
    score = 0;

    setup_speeds();
}

void start_game() {
    update_game_state(GAME_STATE_PLAYING);

    lung_ghost_visible = true;
    mouth_ghost_visible = true;

    level_change_time = 10 * 1000000 + curr_time_ms;

    play_laugh();
}

void start_game_over() {
    update_game_state(GAME_STATE_GAME_OVER);
    
    debugf("Gameover with score of %i\n", score);

    title_text = "Game over!";

    if (score > high_score) {
        high_score = score;
        sprintf(subtitle_buffer, "You got a new high score of %d seconds!", score);
    } else {
        sprintf(subtitle_buffer, "You survived %d seconds", score);
    }

    subtitle_visible = true;
    subtitle_text = subtitle_buffer;
}

void update_attract() {
    joypad_buttons_t pressed  = joypad_get_buttons_pressed(JOYPAD_PORT_1);

    laughometer_level = 1.0f + fm_sinf_approx(
        curr_time_ms * 4.0f / (5000000 * lung_breath_speed), 5
    );

    laughometer_level = cpfclamp(laughometer_level, 0, 2.0f);

    lung_scale = 0.95f + 0.1f * fm_sinf_approx(curr_time_ms / (1000000 * lung_breath_speed), 5);
    mouth_angle = M_PI / 8 + (M_PI / 10) * (fm_sinf_approx(curr_time_ms / (2000000 * 1.0f), 5));

    eye_scale = 0.9f + (0.2f * fm_sinf_approx(curr_time_ms / (3000000.0f), 5));

    subtitle_visible = curr_time_ms % 1000000 > 500000;
    if (pressed.start) {
        start_starting();
    }
}

void update_starting() {
    if (curr_time_ms - state_start > 1000000) {
        start_game();
    }
}

void update_playing() {
    joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);

    bool eye_correct = false;

    mouth_angle = M_PI / 8 + (inputs.stick_y / 85.0f) * M_PI / 8;

    float mouth_wiggle = cpfmax(0.03f, 0.1f - (level * 0.01f));

    if (mouth_target > mouth_angle + mouth_wiggle) {
        stickStatus = STICK_SPRITE_UP;
    } else if (mouth_target < mouth_angle - mouth_wiggle) {
        stickStatus = STICK_SPRITE_DOWN;
    }  else {
        stickStatus = STICK_SPRITE_NEUTRAL;
    }

    bool mouth_correct = stickStatus == STICK_SPRITE_NEUTRAL;

    mouth_target = M_PI * (1 + 0.75f * fm_sinf_approx(curr_time_ms * mouth_target_speed / (1000000), 5)) / 8;

    float lung_wiggle = cpfmax(0.03f, 0.1f - (level * 0.01f));
    if (lung_target_scale > lung_scale + lung_wiggle) {
        dpadStatus = STICK_SPRITE_UP;
    } else if (lung_target_scale < lung_scale - lung_wiggle) {
        dpadStatus = STICK_SPRITE_DOWN;
    } else {
        dpadStatus = STICK_SPRITE_NEUTRAL;
    }

    bool lung_correct = dpadStatus == STICK_SPRITE_NEUTRAL;

    laughometer_change += 0.001f;

    float punishment = -0.0005f * level;

    if (!lung_correct) {
        laughometer_change += punishment;
    }

    if (!mouth_correct) {
        laughometer_change += punishment;
    }

    if (!lung_correct) {
        title_text = "Breath harder!";
    } else if (!mouth_correct) {
        title_text = "Mouth harder!";
    } else {
        title_text = "";
    }

    laughometer_change = cpfclamp(laughometer_change, -0.01f, 0.01f);

    laughometer_level += laughometer_change;

    laughometer_level = cpfclamp(laughometer_level, 0, 2.0f);

    if (curr_time_ms > level_change_time) {
        level++;
        debugf("Advance to level %i\n", level);
        level_change_time = 10 * 1000000 + curr_time_ms;

        setup_speeds();

        play_laugh();
    }

    if (laughometer_level <= 0.0f && !debug) {
        score = (curr_time_ms - state_start) / 1000000;
        start_game_over();

        play_laugh();
    }

    joypad_buttons_t pressed  = joypad_get_buttons_pressed(JOYPAD_PORT_1);

    if (pressed.z) {

    }

    joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);

    lung_target_scale = 0.95f + 0.1f * fm_sinf_approx(curr_time_ms * lung_target_speed / (1000000), 5);

    float lungSpeed = 0.005f + level * 0.001f;
    if (held.d_up) {
        lung_scale += lungSpeed;
    }

    if (held.d_down) {
        lung_scale -= lungSpeed;
    }

    lung_scale = cpfclamp(lung_scale, 0.8f, 1.2f);
    float itemSpeed = 2.0f;

    if (held.c_up) {
        item_pos.y -= itemSpeed;
    }
    if (held.c_down) {
        item_pos.y += itemSpeed;
    }
    if (held.c_left) {
        item_pos.x -= itemSpeed;
    }
    if (held.c_right) {
        item_pos.x += itemSpeed;
    }

    if (pressed.l) {
        start_attract();
    }
}

void update_game_over() {
    lung_scale = 0.95f + 0.1f * fm_sinf_approx(curr_time_ms / (1000000 * lung_breath_speed), 5);
    mouth_angle = M_PI / 8 + (M_PI / 10) * (fm_sinf_approx(curr_time_ms / (2000000 * 1.0f), 5));

    eye_scale = 0.9f + (0.2f * fm_sinf_approx(curr_time_ms / (3000000.0f), 5));


    joypad_buttons_t pressed  = joypad_get_buttons_pressed(JOYPAD_PORT_1);

    if (pressed.start) {
        start_attract();
    }
}

void update() {
    joypad_poll();

    curr_time_ms = TIMER_MICROS(timer_ticks());

    switch (gameStatus) {
        case GAME_STATE_ATTRACT:
            update_attract();
            break;
        case GAME_STATE_PLAYING:
            update_playing();
            break;
        case GAME_STATE_STARTING:
            update_starting();
            break;
        case GAME_STATE_GAME_OVER:
            update_game_over();
            break;
    }

    joypad_buttons_t pressed  = joypad_get_buttons_pressed(JOYPAD_PORT_1);

    if (pressed.r) {        
        debugf("Lung: %f actual %f target, mouth: %f action %f target, eye: %f\n", lung_scale, lung_target_scale, mouth_angle, mouth_target, eye_scale);
    }

    if (pressed.l) {
        debug = !debug;

        debugf("Debug: %i\n", debug);
    }
}

int main() {
    debug_init_isviewer();
    debug_init_usblog();

    dfs_init(DFS_DEFAULT_LOCATION);

    debugf("Starting GGJ24");

    display_init(RESOLUTION_640x480, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_DISABLED);

    uint32_t seed;
    getentropy(&seed, 4);
    srand(seed);
    register_VI_handler((void(*)(void))rand);

    rdpq_init();
    joypad_init();
    timer_init();
    rdpq_debug_start();
    audio_init(44100, 4);
	mixer_init(16);  // Initialize up to 16 channels
    throttle_init(30, 0, 1);

    rdpq_font_t *ihat_cs_fnt = rdpq_font_load("rom://IHATCS.font64");
    rdpq_font_style(ihat_cs_fnt, 0, &(rdpq_fontstyle_t) {
        .color = RGBA32(0xff, 0xff, 0xff, 0xff)
    });
    rdpq_text_register_font(FONT_IHATCS, ihat_cs_fnt);

    rdpq_font_t *ihatcs_small_fnt = rdpq_font_load("rom://IHATCS_small.font64");
    rdpq_font_style(ihatcs_small_fnt, 1, &(rdpq_fontstyle_t) {
        .color = RGBA32(0xff, 0xff, 0xff, 0xff)
    });
    rdpq_text_register_font(FONT_IHATCS_SMALL, ihatcs_small_fnt);

    sprite_t *brick = sprite_load("rom:/brick.sprite");

    sprite_t *laughometer_base = sprite_load("rom://laughometer-base.sprite");
    sprite_t *laughometer_bar = sprite_load("rom://laughometer-bar.sprite");

    sprite_t *lungs = sprite_load("rom://lung_indexed.sprite");
    sprite_t *lungs_semi = sprite_load("rom://lung_semi.sprite");

    sprite_t *mouth_lower = sprite_load("rom://mouth_lower.sprite");
    sprite_t *mouth_upper = sprite_load("rom://mouth_upper.sprite");
    sprite_t *mouth_lower_semi = sprite_load("rom://mouth_lower_semi.sprite");
    sprite_t *mouth_upper_semi = sprite_load("rom://mouth_upper_semi.sprite");

    sprite_t *dpad_normal = sprite_load("rom://dpad_normal.sprite");
    sprite_t *dpad_up = sprite_load("rom://dpad_up.sprite");

    sprite_t *stick_neutral = sprite_load("rom://stick_neutral_indexed.sprite");
    sprite_t *stick_up = sprite_load("rom://stick_up_indexed.sprite");

    sprite_t *c_pad = sprite_load("rom://c_pad.sprite");

    sprite_t *eye = sprite_load("rom://eye_v2.sprite");

    sprite_t *cheese = sprite_load("rom://cheese.sprite");
    sprite_t *beans = sprite_load("rom://beans_scaled.sprite");

    wav64_open(&sinister_laugh, "rom://sinister_laugh.wav64");

    xm64player_open(&xm, "rom://circus_clowns.xm64");
    xm64player_play(&xm, CHANNEL_MUSIC);

    init();

    start_attract();

    while (1)
    {
        surface_t *disp = display_get();

        graphics_fill_screen( disp, graphics_make_color(0, 0, 0, 255));

        rdpq_attach(disp, NULL);

        if (title_visible) {
            rdpq_text_print(&(rdpq_textparms_t){
                .align = ALIGN_CENTER,
                .valign = VALIGN_TOP,
                .width = 640,
                .height = 60,
                .wrap = WRAP_WORD,
            }, FONT_IHATCS, 0, 370, title_text);
        }

        if (subtitle_visible) {
            rdpq_text_print(&(rdpq_textparms_t){
                .align = ALIGN_CENTER,
                .valign = VALIGN_TOP,
                .width = 640,
                .height = 40,
                .wrap = WRAP_WORD,
            }, FONT_IHATCS_SMALL, 0, 420, subtitle_text);
        }

        rdpq_set_mode_standard();
        rdpq_mode_alphacompare(128);

        rdpq_sprite_blit(
            laughometer_bar,
            laughometer_pos.x,
            laughometer_pos.y,
            &(rdpq_blitparms_t){
                .cx = laughometer_base->width / 2,
                .cy = laughometer_base->height / 2,
                .width = 207 + (laughometer_level / 2.0f) * 335
            }
        );

        rdpq_sprite_blit(
            laughometer_base,
            laughometer_pos.x,
            laughometer_pos.y,
            &(rdpq_blitparms_t){
                .cx = laughometer_base->width / 2,
                .cy = laughometer_base->height / 2,
            }
        );

        if (lung_visible) {
            rdpq_sprite_blit(
                lungs,
                lung_pos.x,
                lung_pos.y,
                &(rdpq_blitparms_t){
                    .scale_x = lung_scale,
                    .scale_y = lung_scale,
                    .cx = lungs->width / 2,
                    .cy = lungs->height / 2,
                }
            );
        }
        
        if (lung_ghost_visible) {
            rdpq_sprite_blit(
                lungs_semi,
                lung_pos.x,
                lung_pos.y,
                &(rdpq_blitparms_t){
                    .scale_x = lung_target_scale,
                    .scale_y = lung_target_scale,
                    .cx = lungs->width / 2,
                    .cy = lungs->height / 2,
                }
            );
        }

        rdpq_sprite_blit(
            mouth_upper,
            mouth_pos.x,
            mouth_pos.y - 2,
            &(rdpq_blitparms_t){
                .theta = mouth_angle,
                .cx = 15,
                .cy = 30,
                
            }
        );

        rdpq_sprite_blit(
            mouth_lower,
            mouth_pos.x,
            mouth_pos.y,
            &(rdpq_blitparms_t){
                .theta = -mouth_angle,
                .cx = 15,
                .cy = 10,
            }
        );

        if (mouth_ghost_visible) {
            rdpq_sprite_blit(
                mouth_upper_semi,
                mouth_pos.x,
                mouth_pos.y - 2,
                &(rdpq_blitparms_t){
                    .theta = mouth_target,
                    .cx = 15,
                    .cy = 30,        
                }
            );

            rdpq_sprite_blit(
                mouth_lower_semi,
                mouth_pos.x,
                mouth_pos.y,
                &(rdpq_blitparms_t){
                    .theta = -mouth_target,
                    .cx = 15,
                    .cy = 10,
                }
            );
        }

        rdpq_sprite_blit(
            eye,
            eyes_pos.x,
            eyes_pos.y,
              &(rdpq_blitparms_t){
                .scale_x = base_eye_scale * eye_scale,
                .scale_y = base_eye_scale * eye_scale,
                .cx = eye->width / 2,\
                .cy = eye->height / 2,
            }
        );

        if (dpadStatus == STICK_SPRITE_UP) {
            rdpq_sprite_blit(
                dpad_up,
                dpad_pos.x,
                dpad_pos.y,
                &(rdpq_blitparms_t){
                    .cx = 25,
                    .cy = 25
                }
            );
        } else if (dpadStatus == STICK_SPRITE_DOWN) {
            rdpq_sprite_blit(
                dpad_up,
                dpad_pos.x,
                dpad_pos.y,
                &(rdpq_blitparms_t){
                    .cx = 25,
                    .cy = 25,
                    .flip_y = true
                }
            );
        } else {
             rdpq_sprite_blit(
                dpad_normal,
                dpad_pos.x,
                dpad_pos.y,
                &(rdpq_blitparms_t){
                    .cx = 25,
                    .cy = 25,
                    .flip_y = true
                }
            );
        }

        switch (stickStatus)
        {
            case STICK_SPRITE_NEUTRAL:
                rdpq_sprite_blit(
                    stick_neutral,
                    stick_pos.x,
                    stick_pos.y,
                    &(rdpq_blitparms_t){
                    }
                );
            break;

            case STICK_SPRITE_UP:
                rdpq_sprite_blit(
                    stick_up,
                    stick_pos.x,
                    stick_pos.y,
                    &(rdpq_blitparms_t){
                    }
                );
            break;

             case STICK_SPRITE_DOWN:
                rdpq_sprite_blit(
                    stick_up,
                    stick_pos.x,
                    stick_pos.y + 15,
                    &(rdpq_blitparms_t){
                        .flip_y = true
                    }
                );
            break;
        }

        rdpq_sprite_blit(
            c_pad,
            cpad_pos.x,
            cpad_pos.y,
             &(rdpq_blitparms_t){
                .cx = c_pad->width / 2,
                .cy = c_pad->height / 2,
            }
        );

        switch (currentItemType) {
            case ITEM_BRICK:
                rdpq_sprite_blit(
                            brick,
                            item_pos.x,
                            item_pos.y,
                            &(rdpq_blitparms_t){
                                .cx = brick->width / 2,
                                .cy = brick->height / 2,
                            }
                        );
            break;
            case ITEM_CHEESE:
             rdpq_sprite_blit(
                    cheese,
                    item_pos.x,
                    item_pos.y,
                    &(rdpq_blitparms_t){
                        .cx = cheese->width / 2,
                        .cy = cheese->height / 2,
                    }
                );
            break;
            case ITEM_BEANS:
                rdpq_sprite_blit(
                    beans,
                    item_pos.x,
                    item_pos.y,
                    &(rdpq_blitparms_t){
                        .cx = beans->width / 2,
                        .cy = beans->height / 2,
                    }
                );
            break;
            default:
            break;
        }

      
        if (debug) {
        }

        rdpq_detach_wait();
        
        display_show(disp);

        update();

        // Check whether one audio buffer is ready, otherwise wait for next
		// frame to perform mixing.
		mixer_try_play();

        if (!throttle_wait()) {
        //    debugf("Throttle warning %ld\n", throttle_frame_length());
        };
    }
}