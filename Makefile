all: ggj24.z64
.PHONY: all

BUILD_DIR=build
include $(N64_INST)/include/n64.mk

CFLAGS += -I.

OBJS := $(BUILD_DIR)/main.o $(patsubst %.c,$(BUILD_DIR)/%.o,$(wildcard chipmunk/*.c))

assets_png = $(wildcard assets/*.png)
assets_ttf = $(wildcard assets/*.ttf)
assets_wav = $(wildcard assets/*.wav)
assets_xm1 = $(wildcard assets/*.xm)

assets_conv = $(addprefix filesystem/,$(notdir $(assets_png:%.png=%.sprite))) \
			  $(addprefix filesystem/,$(notdir $(assets_ttf:%.ttf=%.font64))) \
			  $(addprefix filesystem/,$(notdir $(assets_wav:%.wav=%.wav64))) \
			  $(addprefix filesystem/,$(notdir $(assets_xm1:%.xm=%.xm64)))

CFLAGS += -I.
MKSPRITE_FLAGS ?=

filesystem/%.sprite: assets/%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@"
	@$(N64_MKSPRITE) -f RGBA16 --compress --verbose -o "$(dir $@)" "$<"

filesystem/%.font64: assets/%.ttf
	@mkdir -p $(dir $@)
	@echo "    [FONT] $@"
	@$(N64_MKFONT) $(MKFONT_FLAGS) -o filesystem "$<"

# Run audioconv on all WAV files under assets/
# We do this file by file, but we could even do it just once for the whole
# directory, because audioconv64 supports directory walking.
filesystem/%.wav64: assets/%.wav
	@mkdir -p $(dir $@)
	@echo "    [AUDIO] $@"
	@$(N64_AUDIOCONV) --wav-compress 3 -v -o filesystem $<

filesystem/%.xm64: assets/%.xm
	@mkdir -p $(dir $@)
	@echo "    [AUDIO] $@"
	@$(N64_AUDIOCONV) $(AUDIOCONV_FLAGS) -o filesystem "$<"

filesystem/IHATCS.font64: MKFONT_FLAGS+=--size 32
filesystem/IHATCS_small.font64: MKFONT_FLAGS+=--size 24

$(BUILD_DIR)/ggj24.dfs: $(assets_conv) 
$(BUILD_DIR)/ggj24.elf: $(OBJS)

ggj24.z64: N64_ROM_TITLE="GGJ24"
ggj24.z64: $(BUILD_DIR)/ggj24.dfs

clean:
	rm -rf $(BUILD_DIR) filesystem/ ggj24.z64

-include $(wildcard $(BUILD_DIR)/*.d)
