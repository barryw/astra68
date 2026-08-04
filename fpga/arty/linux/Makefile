CROSS_COMPILE ?= arm-linux-gnueabihf-
CC := $(CROSS_COMPILE)gcc
HOSTCC ?= cc
PYTHON ?= python3
OUT ?= ../../../build/arty-graphics/linux
BOOT_TARGET := $(OUT)/astra-graphics-boot
STATUS_TARGET := $(OUT)/astra-boot-status
SPRITE_TARGET := $(OUT)/astra-sprite-certify
RENDER_TARGET := $(OUT)/astra-render-certify
COPPER_TARGET := $(OUT)/astra-copper-certify
TARGETS := $(BOOT_TARGET) $(STATUS_TARGET) $(SPRITE_TARGET) $(RENDER_TARGET) \
	$(COPPER_TARGET)
COMMON_SOURCES := astra_graphics_hw.c astra_boot_text.c
COMMON_HEADERS := astra_graphics_hw.h astra_boot_text.h
PROTOCOL_SPEC := ../graphics/protocol/astra_render_v1.json
PROTOCOL_GENERATOR := ../graphics/protocol/generate_protocol.py
CFLAGS := -std=c11 -O2 -pipe -Wall -Wextra -Werror -Wformat=2 \
	-Wshadow -Wstrict-prototypes -D_FORTIFY_SOURCE=2
LDFLAGS := -static -Wl,--gc-sections

.PHONY: all clean analyze test-host

all: $(TARGETS)

astra_render_protocol.h: $(PROTOCOL_SPEC) $(PROTOCOL_GENERATOR)
	$(PYTHON) $(PROTOCOL_GENERATOR)

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
		astra_graphics_hw.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections \
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

analyze: astra_render_protocol.h
	$(CC) $(CFLAGS) -fanalyzer -fsyntax-only \
		astra_graphics_boot.c $(COMMON_SOURCES)
	$(CC) $(CFLAGS) -fanalyzer -fsyntax-only \
		astra_boot_status.c $(COMMON_SOURCES)
	$(CC) $(CFLAGS) -fanalyzer -fsyntax-only \
		astra_sprite_certify.c astra_graphics_hw.c
	$(CC) $(CFLAGS) -fanalyzer -fsyntax-only \
		astra_render_certify.c astra_graphics_hw.c
	$(CC) $(CFLAGS) -fanalyzer -fsyntax-only \
		astra_copper_certify.c astra_graphics_hw.c

test-host: $(OUT)/test-astra-boot-text
	$(OUT)/test-astra-boot-text

$(OUT)/test-astra-boot-text: test_astra_boot_text.c \
		$(COMMON_SOURCES) $(COMMON_HEADERS)
	mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) test_astra_boot_text.c $(COMMON_SOURCES) -o $@

clean:
	rm -f $(TARGETS) $(OUT)/test-astra-boot-text
