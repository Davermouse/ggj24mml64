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
    GAME_STATE_STARTING_LEVEL = 2,
    GAME_STATE_PLAYING = 3,
    GAME_STATE_GAME_OVER = 4,
} GameState;

typedef enum {
    ITEM_BRICK,
    ITEM_TAX,
    ITEM_CHEESE,
    ITEM_BEANS,
    ITEM_QUESTION = 100,
} ItemType;

typedef enum {
    STICK_SPRITE_NEUTRAL = 1,
    STICK_SPRITE_UP = 2,
    STICK_SPRITE_DOWN = 3,
} StickStatus;

ItemType levelProgression[] = {
    ITEM_CHEESE,
    ITEM_TAX,
    ITEM_BRICK,
    ITEM_BEANS
};

static int progressionLength = 4;

static int8_t stick_range = 80;
static bool debug = false;

sprite_t *attention_ray;
sprite_t *question_mark;
sprite_t *cheese;
sprite_t *beans;
sprite_t *brick;
sprite_t *tax;

GameState gameStatus = GAME_STATE_ATTRACT;
int state_start = 0;
int curr_time_ms = 0;

char title_buffer[50];
char subtitle_buffer[50];

ItemType currentItemType = ITEM_TAX;

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

cpBody *itemBody;

static cpSpace *space;

static cpCollisionType RAY = 1;
static cpCollisionType PLAYER = 2;
static cpCollisionType NONE = 3;

float laughometer_level = 1.0f;
float laughometer_change = 0.0f;

// The current
int level = 0;
int sub_level = 0;
int level_change_time = 0;
int game_start_time = 0;
int last_fire_time = 0;
int fire_time = 0;
int high_score = 0;
int score = 0;
bool item_funny = false;

float lung_scale = 1.0f;
bool lung_visible = true;
bool lung_ghost_visible = true;

float base_eye_scale = 0.6f;
float eye_scale = 1.0f;
float item_scale = 0.8f;
float eye_angle = 0.0f;

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

const cpFloat ray_width = 50.0f;
const cpFloat ray_height = 20.0f;

void play_laugh() {
    wav64_play(&sinister_laugh, CHANNEL_SFX1);

    float freq = sinister_laugh.wave.frequency * (((rand() % 200) + 100.0f) / 200);

    debugf("Sample freq original: %f updated: %f\n",sinister_laugh.wave.frequency, freq);
    mixer_ch_set_freq(CHANNEL_SFX1, freq);
}

void spawn_ray(cpFloat speed) {
    if (last_fire_time + 1000000 > curr_time_ms) {
        return;
    }

    last_fire_time = curr_time_ms;

    cpFloat mass = 1.0f;

    cpBody *rayBody = cpBodyNewKinematic();
    cpSpaceAddBody(space, rayBody);

    cpBodySetPosition(rayBody, cpv(eyes_pos.x, eyes_pos.y));
    cpBodySetAngle(rayBody, eye_angle);

    cpFloat rotSpeed = rand() % 2 == 0 ? 1.0f : -1.0f;
    cpBodySetVelocity(rayBody, cpv(speed * cosf(eye_angle), speed * sinf(eye_angle + M_PI)));
    cpBodySetAngularVelocity(rayBody, rotSpeed);

    cpShape *rayShape = cpSpaceAddShape(space, cpBoxShapeNew(rayBody, ray_width, ray_height, 0));
    cpShapeSetFriction(rayShape, 0.0);
    cpShapeSetElasticity(rayShape, 0.1f);
    cpShapeSetCollisionType(rayShape, RAY);
    cpShapeSetMass(rayShape, 5.0f);

    if (debug) {
        debugf("Added shape %p\n", rayShape);
        debugf("Added body %p\n", rayBody);
    }
}

void init() {
    lung_pos = cpv(100, 175);
    mouth_pos = cpv(220,175);
    eyes_pos = cpv(400, 180);

    laughometer_pos = cpv(320, 35);

    stick_pos = cpv(245, 275);
    dpad_pos = cpv(110, 303);
    cpad_pos = cpv(550, 290);

    item_pos = cpv(550, 150);
}

static void changeShapeCollision(cpBody* body, cpShape *shape, void* data) {
    cpShapeSetCollisionType(shape, NONE);
}

static void postRayCollide(cpSpace *space, cpBody *ray, void *unused)
{  
    // A body can have multiple shapes, so we need to iterate through and delete
    cpBodySetType(ray, CP_BODY_TYPE_DYNAMIC);
    cpBodySetMass(ray, 5.0f);

    cpBodyEachShape(ray, (cpBodyShapeIteratorFunc)changeShapeCollision, NULL);
}

static void removeShape(cpBody* body, cpShape* shape, cpSpace *space) {
  debugf("Remove shape %p\n", shape);
  cpSpaceRemoveShape(space, shape);
  cpShapeFree(shape);
}

static void postStepRemoveBody(cpSpace *space, cpBody *body, void *unused)
{  
  // A body can have multiple shapes, so we need to iterate through and delete
  cpBodyEachShape(body, (cpBodyShapeIteratorFunc)removeShape, space);

  cpBodySetType(body, CP_BODY_TYPE_KINEMATIC);
  debugf("Remove body %p\n", body);
  cpSpaceRemoveBody(space, body);

  cpBodyFree(body);
}

static cpBool handlePlayerRayCollision(cpArbiter *arb, cpSpace *space, void *data){
    debugf("Collide!\n");

    cpBody *a, *b; cpArbiterGetBodies(arb, &a, &b);

    cpBody *ray = a == itemBody ? b : a;

    if (item_funny) {
        cpSpaceAddPostStepCallback(
            space, (cpPostStepFunc)postStepRemoveBody, ray, NULL);
        
    } else {
        cpSpaceAddPostStepCallback(
            space, (cpPostStepFunc)postRayCollide, ray, NULL
        );
    }

    if (gameStatus == GAME_STATE_PLAYING) {
        if (item_funny) {
            laughometer_level += 0.1f;
        } else {
            laughometer_level -= 0.05f;
        }
    } else {
        play_laugh();
    }

    return cpTrue;
}

static cpSpace* init_space() {
    cpSpace *space = cpSpaceNew();
    cpSpaceSetGravity(space, cpv(0, 100));

    // This doesn't seem to work, so I've forcible disabled sleep in the cpSpaceStep code
    cpSpaceSetSleepTimeThreshold(space, INFINITY);

    itemBody = cpBodyNewKinematic();
    cpSpaceAddBody(space, itemBody);

    cpBodySetPosition(itemBody, item_pos);

    cpShape *itemShape = cpSpaceAddShape(space, cpBoxShapeNew(itemBody, 80, 60, 0));
    cpShapeSetCollisionType(itemShape, PLAYER);
    cpShapeSetMass(itemShape, 5.0f);

    cpCollisionHandler *handler = cpSpaceAddCollisionHandler(space, PLAYER, RAY);
    handler->beginFunc = (cpCollisionBeginFunc)handlePlayerRayCollision;

    return space;
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

    lung_target_scale = 1.0f;
    mouth_target = M_PI / 8;

    currentItemType = ITEM_QUESTION;

    cpBodySetPosition(itemBody, cpv(550, 150));
    cpBodySetVelocity(itemBody, cpv(0, 0));
    cpBodySetAngle(itemBody, 0);
    cpBodySetType(itemBody, CP_BODY_TYPE_KINEMATIC);
}

void start_starting() {
    update_game_state(GAME_STATE_STARTING_LEVEL);

    sprintf(title_buffer, "Starting level %i", level + 1);

    title_text = title_buffer;

    switch (rand() % 3) {
        case 0:
            subtitle_text = "Is this funny?";
        break;
        case 1:
            subtitle_text = "Does this make you laugh?";
        break;
        case 2:
            subtitle_text = "Is this funny enough for you?";
        break;
    }

    subtitle_visible = true;

    laughometer_level = 1.0f;
    laughometer_change = 0.0f;

    if (level < progressionLength) {
        currentItemType = levelProgression[level];
    } else {
        currentItemType = rand() % 4;
    }

    item_funny = currentItemType == ITEM_CHEESE || currentItemType == ITEM_BEANS;

    item_pos = cpv(550, 220);
    cpBodySetPosition(itemBody, item_pos);

    setup_speeds();
}

void new_game() {
    level = 0;
    score = 0;
    game_start_time = curr_time_ms;

    start_starting();
}

void next_level() {
    level += 1;

    start_starting();
}

void start_game() {
    update_game_state(GAME_STATE_PLAYING);

    title_text = "Start!";

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

    cpBodySetType(itemBody, CP_BODY_TYPE_DYNAMIC);
    cpBodySetMass(itemBody, 10.0f);
    cpBodySetAngularVelocity(itemBody,10 * ( -1.0f + (rand() % 2000) / 1000.0f));

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
    eye_angle = -M_PI / 8 + (M_PI_4 * fm_sinf_approx(curr_time_ms * 5.0f / (6000000.0f), 5));

    item_pos.y = 180.0f + 80.0f * fm_sinf_approx(curr_time_ms / (2000000.0f), 5);
    cpBodySetPosition(itemBody, item_pos);

    if (fire_time < curr_time_ms) {
        spawn_ray(80.0f);

        fire_time = curr_time_ms + cpfmax(500000, 500000 + rand() % 2000000);
    }

    subtitle_visible = curr_time_ms % 1000000 > 500000;
    if (pressed.start) {
        new_game();
    }

    if (pressed.a) {
        spawn_ray(100.0f);
    }
}

void update_starting() {
    if (curr_time_ms - state_start > 5000000) {
        start_game();
    }
}

void update_playing() {
    joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);

    bool eye_correct = false;

    mouth_angle = M_PI / 8 + (inputs.stick_y / 85.0f) * M_PI / 8;

    float mouth_wiggle = cpfmax(0.03f, 0.1f - (sub_level * 0.01f));

    if (mouth_target > mouth_angle + mouth_wiggle) {
        stickStatus = STICK_SPRITE_UP;
    } else if (mouth_target < mouth_angle - mouth_wiggle) {
        stickStatus = STICK_SPRITE_DOWN;
    }  else {
        stickStatus = STICK_SPRITE_NEUTRAL;
    }

    bool mouth_correct = stickStatus == STICK_SPRITE_NEUTRAL;

    mouth_target = M_PI * (1 + 0.75f * fm_sinf_approx(curr_time_ms * mouth_target_speed / (1000000), 5)) / 8;

    float lung_wiggle = cpfmax(0.03f, 0.1f - (sub_level * 0.01f));
    if (lung_target_scale > lung_scale + lung_wiggle) {
        dpadStatus = STICK_SPRITE_UP;
    } else if (lung_target_scale < lung_scale - lung_wiggle) {
        dpadStatus = STICK_SPRITE_DOWN;
    } else {
        dpadStatus = STICK_SPRITE_NEUTRAL;
    }

    bool lung_correct = dpadStatus == STICK_SPRITE_NEUTRAL;

    if (item_funny) {
        eye_angle = -M_PI / 8 + (M_PI_4 * fm_sinf_approx(curr_time_ms * (sub_level + 1) * 2.5f / (6000000.0f), 5));
    } else {
        cpVect dItem = cpvsub(eyes_pos, cpBodyGetPosition(itemBody));

        eye_angle = -cpvtoangle(dItem) + M_PI;
    }

    if (fire_time < curr_time_ms) {
        spawn_ray(50.0f + sub_level * 20.0f);

        fire_time = curr_time_ms + 500000 + rand() % 5000000;
    }

    laughometer_change += 0.001f;

    float punishment = -0.0001f -0.0003f * sub_level;

    if (!lung_correct) {
        laughometer_change += punishment;
    }

    if (!mouth_correct) {
        laughometer_change += punishment;
    }

    if (!lung_correct) {
        title_text = "Breath harder!";
    } else if (!mouth_correct) {
        title_text = "Flap more!";
    } else {
        title_text = "";
    }

    laughometer_change = cpfclamp(laughometer_change, -0.01f, 0.01f);

    laughometer_level += laughometer_change;

    laughometer_level = cpfclamp(laughometer_level, 0, 2.0f);

    joypad_buttons_t pressed  = joypad_get_buttons_pressed(JOYPAD_PORT_1);

    if ((laughometer_level <= 0.0f && !debug) || (debug && pressed.b)) {
        score = (curr_time_ms - game_start_time) / 1000000;
        start_game_over();

        play_laugh();
    }

    if (curr_time_ms > level_change_time || (debug && pressed.a)) {
        sub_level++;
        debugf("Advance to sub level %i\n", sub_level);
        level_change_time = 8 * 1000000 + curr_time_ms;

        setup_speeds();

        play_laugh();

        if (sub_level > 3) {
            next_level();
        }
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
    float itemSpeed = 2.0f + sub_level * 0.04f;

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

    cpBodySetPosition(itemBody, item_pos);
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

// Remove any bodies that have fallen off the screen
static void updateBody(cpBody *body, void* data) {
    cpVect pos = cpBodyGetPosition(body);

    if (body == itemBody) return;

    if (pos.y > 700 || pos.x < -50 || pos.x > 700) {
        cpSpaceAddPostStepCallback(
            space, (cpPostStepFunc)postStepRemoveBody, body, NULL
        );
    }
}

void update() {
    joypad_poll();

    curr_time_ms = TIMER_MICROS(timer_ticks());
    cpSpaceStep(space, 0.03);

    switch (gameStatus) {
        case GAME_STATE_ATTRACT:
            update_attract();
            break;
        case GAME_STATE_PLAYING:
            update_playing();
            break;
        case GAME_STATE_STARTING_LEVEL:
            update_starting();
            break;
        case GAME_STATE_GAME_OVER:
            update_game_over();
            break;
    }

    joypad_buttons_t pressed  = joypad_get_buttons_pressed(JOYPAD_PORT_1);

    if (pressed.r) {        
        debugf("Lung: %f actual %f target, mouth: %f action %f target, eye: %f angle %f\n", lung_scale, lung_target_scale, mouth_angle, mouth_target, eye_scale, eye_angle);
    }

    if (pressed.l) {
        debug = !debug;

        debugf("Debug: %i\n", debug);
        xm64player_stop(&xm);
    }

    cpSpaceEachBody(space, (cpSpaceBodyIteratorFunc)updateBody, NULL);
}

static void drawBody(cpBody *body, surface_t *disp) {
    cpVect pos = cpBodyGetPosition(body);
    cpVect rot = cpBodyGetRotation(body);
    float theta = atan2f(rot.y, -rot.x);

    if (body != itemBody) {
        rdpq_sprite_blit(attention_ray, pos.x, pos.y,  &(rdpq_blitparms_t){
            .theta = theta,
            .cx = ray_width / 2,
            .cy = ray_height / 2,               
        });
    } else {
        switch (currentItemType) {
            case ITEM_BRICK:
                rdpq_sprite_blit(
                            brick,
                            pos.x,
                            pos.y,
                            &(rdpq_blitparms_t){
                                .theta = -theta,
                                .cx = brick->width / 2,
                                .cy = brick->height / 2,
                            }
                        );
            break;
            case ITEM_CHEESE:
             rdpq_sprite_blit(
                    cheese,
                    pos.x,
                    pos.y,
                    &(rdpq_blitparms_t){
                        .cx = cheese->width / 2,
                        .cy = cheese->height / 2,
                        .scale_x = item_scale,
                        .scale_y = item_scale,
                        .theta = theta + M_PI,
                    }
                );
            break;
            case ITEM_BEANS:
                rdpq_sprite_blit(
                    beans,
                    pos.x,
                    pos.y,
                    &(rdpq_blitparms_t){
                        .theta = theta + M_PI,
                        .cx = beans->width / 2,
                        .cy = beans->height / 2,
                    }
                );
            break;
             case ITEM_TAX:
                rdpq_sprite_blit(
                    tax,
                    pos.x,
                    pos.y,
                    &(rdpq_blitparms_t){
                        .theta = theta + M_PI,
                        .cx = tax->width / 2,
                        .cy = tax->height / 2,
                    }
                );
            break;
               case ITEM_QUESTION:
                rdpq_sprite_blit(
                    question_mark,
                    pos.x,
                    pos.y,
                    &(rdpq_blitparms_t){
                        .theta = theta + M_PI,
                        .cx = question_mark->width / 2,
                        .cy = question_mark->height / 2,
                    }
                );
            break;
            default:
            break;
        }
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

    brick = sprite_load("rom:/brick.sprite");
    cheese = sprite_load("rom://cheese.sprite");
    beans = sprite_load("rom://beans_scaled.sprite");
    tax = sprite_load("rom://tax.sprite");
    question_mark = sprite_load("rom://question_mark.sprite");

    attention_ray = sprite_load("rom://attention-ray.sprite");

    wav64_open(&sinister_laugh, "rom://sinister_laugh.wav64");

    xm64player_open(&xm, "rom://circus_clowns.xm64");
    xm64player_play(&xm, CHANNEL_MUSIC);

    init();

    space = init_space();

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

/*
        rdpq_sprite_blit(
            c_pad,
            cpad_pos.x,
            cpad_pos.y,
             &(rdpq_blitparms_t){
                .cx = c_pad->width / 2,
                .cy = c_pad->height / 2,
            }
        );
*/
        cpSpaceEachBody(
            space,
            (cpSpaceBodyIteratorFunc)drawBody,
            disp
        );

        rdpq_sprite_blit(
            eye,
            eyes_pos.x,
            eyes_pos.y,
              &(rdpq_blitparms_t){
                .scale_x = base_eye_scale * eye_scale,
                .scale_y = base_eye_scale * eye_scale,
                .cx = eye->width / 2,
                .cy = eye->height / 2,
                .theta = eye_angle,
            }
        );
      
        if (debug) {
        }

        mixer_try_play();

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