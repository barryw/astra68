CROSS_COMPILE ?= arm-linux-gnueabihf-
CC := $(CROSS_COMPILE)gcc
HOSTCC ?= cc
PYTHON ?= python3
PLATFORM ?= arty
ifeq ($(PLATFORM),arty)
PLATFORM_CPPFLAGS :=
else ifeq ($(PLATFORM),de25)
PLATFORM_CPPFLAGS := -DASTRA_GRAPHICS_ARENA_BASE_VALUE=0x40000000u \
	-DASTRA_GRAPHICS_ARENA_LIMIT_VALUE=0x80000000u \
	-DASTRA_CONTROL_BASE_VALUE=0x20100000u
else
$(error unsupported graphics platform: $(PLATFORM))
endif
OUT ?= ../../../build/$(PLATFORM)-graphics/linux
BOOT_TARGET := $(OUT)/astra-graphics-boot
STATUS_TARGET := $(OUT)/astra-boot-status
SPRITE_TARGET := $(OUT)/astra-sprite-certify
RENDER_TARGET := $(OUT)/astra-render-certify
COPPER_TARGET := $(OUT)/astra-copper-certify
AUDIO_TARGET := $(OUT)/astra-audio-certify
HDMI_LINK_TARGET := $(OUT)/astra-hdmi-link
TERMINAL_TARGET := $(OUT)/astra-terminal-display
TIME_SYNC_TARGET := $(OUT)/astra-time-sync
SPLASH_TARGET := $(OUT)/astra_boot_splash.rgb565
TARGETS := $(BOOT_TARGET) $(STATUS_TARGET) $(SPRITE_TARGET) $(RENDER_TARGET) \
	$(COPPER_TARGET) $(AUDIO_TARGET) $(TERMINAL_TARGET) \
	$(TIME_SYNC_TARGET) $(SPLASH_TARGET)
ifeq ($(PLATFORM),arty)
TARGETS += $(HDMI_LINK_TARGET)
endif
COMMON_SOURCES := astra_graphics_hw.c astra_boot_text.c
COMMON_HEADERS := astra_graphics_hw.h astra_boot_text.h
TERMINAL_FONT_SOURCE := ../graphics/post_fonts.hex
TERMINAL_FONT_HEADER := $(OUT)/astra_terminal_font.inc
SPLASH_PACKER := ../../../sw/boot/pack_arty_splash.py
SPLASH_SOURCE := ../../../sw/boot/assets/astra_boot_splash_1280x720_blank.png
ASTRA_INCLUDE := ../../../sw/include
NDK_INCLUDE := ../../../ndk/include
PROTOCOL_SPEC := ../graphics/protocol/astra_render_v1.json
PROTOCOL_GENERATOR := ../graphics/protocol/generate_protocol.py
CFLAGS := -std=c11 -O2 -pipe -Wall -Wextra -Werror -Wformat=2 \
	-Wshadow -Wstrict-prototypes -D_FORTIFY_SOURCE=2 $(PLATFORM_CPPFLAGS)
LDFLAGS := -static -Wl,--gc-sections

.PHONY: all clean analyze test-host

all: $(TARGETS)

astra_render_protocol.h: $(PROTOCOL_SPEC) $(PROTOCOL_GENERATOR)
	$(PYTHON) $(PROTOCOL_GENERATOR)

$(SPLASH_TARGET): $(SPLASH_SOURCE) $(SPLASH_PACKER)
	$(PYTHON) $(SPLASH_PACKER) --image $(SPLASH_SOURCE) --output $@

$(TIME_SYNC_TARGET): astra_time_sync.c
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $< $(LDFLAGS) -o $@

$(BOOT_TARGET): astra_graphics_boot.c $(COMMON_SOURCES) $(COMMON_HEADERS)
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections \
		astra_graphics_boot.c $(COMMON_SOURCES) \
		$(LDFLAGS) -o $@

$(STATUS_TARGET): astra_boot_status.c $(COMMON_SOURCES) $(COMMON_HEADERS)
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections \
		astra_boot_status.c $(COMMON_SOURCES) \
		$(LDFLAGS) -o $@

$(SPRITE_TARGET): astra_sprite_certify.c astra_graphics_hw.c \
		astra_graphics_hw.h $(NDK_INCLUDE)/astra/graphics.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -I$(NDK_INCLUDE) -ffunction-sections -fdata-sections \
		astra_sprite_certify.c astra_graphics_hw.c \
		$(LDFLAGS) -o $@

$(RENDER_TARGET): astra_render_certify.c astra_graphics_hw.c \
		astra_graphics_hw.h astra_render_protocol.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections \
		astra_render_certify.c astra_graphics_hw.c \
		$(LDFLAGS) -o $@

$(COPPER_TARGET): astra_copper_certify.c astra_graphics_hw.c \
		astra_graphics_hw.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections \
		astra_copper_certify.c astra_graphics_hw.c \
		$(LDFLAGS) -o $@

$(AUDIO_TARGET): astra_audio_certify.c astra_graphics_hw.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections \
		astra_audio_certify.c $(LDFLAGS) -o $@

$(HDMI_LINK_TARGET): astra_hdmi_link.c
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections \
		astra_hdmi_link.c $(LDFLAGS) -o $@

$(TERMINAL_FONT_HEADER): $(TERMINAL_FONT_SOURCE)
	mkdir -p $(OUT)
	{ printf 'static const uint8_t terminal_font[] = {\n'; \
	  awk '{ printf "0x%s,\n", $$1 }' $<; \
	  printf '};\n'; } > $@

$(TERMINAL_TARGET): astra_terminal_display.c $(COMMON_SOURCES) \
		$(COMMON_HEADERS) $(TERMINAL_FONT_HEADER) \
		astra_render_protocol.h \
		$(ASTRA_INCLUDE)/astra/display.h \
		$(ASTRA_INCLUDE)/astra/display_mailbox.h \
		$(ASTRA_INCLUDE)/astra/render_batch.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -I$(OUT) -I$(ASTRA_INCLUDE) \
		-ffunction-sections -fdata-sections \
		astra_terminal_display.c $(COMMON_SOURCES) \
		$(LDFLAGS) -o $@

analyze: astra_render_protocol.h $(TERMINAL_FONT_HEADER)
	$(CC) $(CFLAGS) -fanalyzer -fsyntax-only \
		astra_graphics_boot.c $(COMMON_SOURCES)
	$(CC) $(CFLAGS) -fanalyzer -fsyntax-only \
		astra_boot_status.c $(COMMON_SOURCES)
	$(CC) $(CFLAGS) -I$(NDK_INCLUDE) -fanalyzer -fsyntax-only \
		astra_sprite_certify.c astra_graphics_hw.c
	$(CC) $(CFLAGS) -fanalyzer -fsyntax-only \
		astra_render_certify.c astra_graphics_hw.c
	$(CC) $(CFLAGS) -fanalyzer -fsyntax-only \
		astra_copper_certify.c astra_graphics_hw.c
	$(CC) $(CFLAGS) -fanalyzer -fsyntax-only astra_audio_certify.c
	$(CC) $(CFLAGS) -fanalyzer -fsyntax-only astra_hdmi_link.c
	$(CC) $(CFLAGS) -fanalyzer -fsyntax-only astra_time_sync.c
	$(CC) $(CFLAGS) -I$(OUT) -I$(ASTRA_INCLUDE) -fanalyzer -fsyntax-only \
		astra_terminal_display.c $(COMMON_SOURCES)

test-host: $(OUT)/test-astra-boot-text $(OUT)/test-astra-terminal-display \
		$(OUT)/test-astra-audio $(OUT)/test-astra-hdmi-link \
		$(OUT)/test-astra-graphics-platform \
		$(OUT)/test-astra-graphics-platform-de25
	$(OUT)/test-astra-boot-text
	$(OUT)/test-astra-terminal-display --self-test
	$(OUT)/test-astra-audio --self-test
	$(OUT)/test-astra-hdmi-link --self-test
	$(OUT)/test-astra-graphics-platform
	$(OUT)/test-astra-graphics-platform-de25
	$(PYTHON) test-astra-chip-reset.py
	$(PYTHON) test-astra-terminal-start.py
	$(PYTHON) test-astra-firstboot.py
	$(PYTHON) test-configure-readonly-root.py

$(OUT)/test-astra-boot-text: test_astra_boot_text.c \
		$(COMMON_SOURCES) $(COMMON_HEADERS)
	mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) test_astra_boot_text.c $(COMMON_SOURCES) -o $@

$(OUT)/test-astra-terminal-display: astra_terminal_display.c \
		$(COMMON_SOURCES) $(COMMON_HEADERS) $(TERMINAL_FONT_HEADER) \
		astra_render_protocol.h \
		$(ASTRA_INCLUDE)/astra/display.h \
		$(ASTRA_INCLUDE)/astra/display_mailbox.h \
		$(ASTRA_INCLUDE)/astra/render_batch.h
	mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -I$(OUT) -I$(ASTRA_INCLUDE) astra_terminal_display.c \
		$(COMMON_SOURCES) -o $@

$(OUT)/test-astra-audio: astra_audio_certify.c
	mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) astra_audio_certify.c -o $@

$(OUT)/test-astra-hdmi-link: astra_hdmi_link.c
	mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) astra_hdmi_link.c -o $@

$(OUT)/test-astra-graphics-platform: test_astra_graphics_platform.c \
        astra_graphics_hw.c astra_graphics_hw.h
	mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) test_astra_graphics_platform.c \
		astra_graphics_hw.c -o $@

$(OUT)/test-astra-graphics-platform-de25: test_astra_graphics_platform.c \
        astra_graphics_hw.c astra_graphics_hw.h
	mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) \
		-DASTRA_GRAPHICS_ARENA_BASE_VALUE=0x40000000u \
		-DASTRA_GRAPHICS_ARENA_LIMIT_VALUE=0x80000000u \
		-DASTRA_CONTROL_BASE_VALUE=0x20100000u \
		-DEXPECT_ARENA_BASE=0x40000000u \
		-DEXPECT_ARENA_LIMIT=0x80000000u \
		-DEXPECT_CONTROL_BASE=0x20100000u \
		test_astra_graphics_platform.c astra_graphics_hw.c -o $@

clean:
	rm -f $(TARGETS) $(OUT)/test-astra-boot-text \
		$(OUT)/test-astra-terminal-display $(OUT)/test-astra-audio \
		$(OUT)/test-astra-hdmi-link \
		$(OUT)/test-astra-graphics-platform \
		$(OUT)/test-astra-graphics-platform-de25 \
		$(TERMINAL_FONT_HEADER)
