include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
HOST_CC ?= cc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
READELF := $(CROSS)readelf
RUNTIME := ../../runtime
include $(RUNTIME)/astra-link.mk
include ../../libraries.mk
GRAPHICS := ../../graphics
NDK := ../../../../ndk
CPPFLAGS := -I../../../../ndk/include -I../../../include -I$(RUNTIME)/include \
	-I$(GRAPHICS)/include -I../../../../fpga/arty/linux
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC $(ASTRA_DEBUG_FLAGS)
TARGET := build/m68k/display.elf
IMAGE := build/m68k/display
OBJECT := build/m68k/main.o
GRAPHICS_RENDERER_LIB := \
	$(GRAPHICS)/build/m68k/libastragraphics-renderer.a
GRAPHICS_FONT_HEADERS := $(GRAPHICS)/build/generated/astra_font8x8.inc \
	$(GRAPHICS)/build/generated/astra_ui_font.inc \
	$(GRAPHICS)/build/generated/astra_mono_font.inc
GRAPHICS_FONT_INPUTS := $(GRAPHICS)/fonts/astra-workbench.afnt \
	$(GRAPHICS)/fonts/astra-mono.afnt ../../../../assets/fonts/astra_8x16.hex \
	../../../../tools/fonts/afnt.py $(NDK)/include/astra/font_library.h
HOST_TEST := build/host/test_display
HOST_TEST_SANITIZE := build/host/test_display_sanitize
LIBRARIES := $(ASTRA_GRAPHICS_LIBRARY) $(ASTRA_FONT_LIBRARY) \
	$(ASTRA_SYSTEM_LIBRARY) $(ASTRA_RUNTIME_LIBRARY) \
	$(ASTRA_COMPILER_LIBRARY)
ASTRA_PROGRAM_PRIVATE_INPUTS := $(GRAPHICS_RENDERER_LIB)
ASTRA_PROGRAM_PRIVATE_OWNER_TARGETS := $(GRAPHICS):renderer
include ../../dynamic-program.mk

$(OBJECT): main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

$(GRAPHICS_FONT_HEADERS): $(GRAPHICS_FONT_INPUTS)
	$(MAKE) -C $(GRAPHICS) $(@:$(GRAPHICS)/%=%)

$(HOST_TEST): main.c tests/test_display.c $(GRAPHICS_FONT_HEADERS) \
		$(GRAPHICS)/src/surface.c $(GRAPHICS)/src/render_builder.c \
		$(NDK)/src/bundle.c $(NDK)/src/utf8.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) -I$(GRAPHICS)/build/generated \
		$(COMMON_FLAGS) -O2 -ffunction-sections -fdata-sections \
		-Wl,--gc-sections $(GRAPHICS)/src/surface.c \
		$(GRAPHICS)/src/render_builder.c $(NDK)/src/bundle.c \
		$(NDK)/src/utf8.c tests/test_display.c -o $@

test: $(HOST_TEST)
	./$(HOST_TEST)

$(HOST_TEST_SANITIZE): main.c tests/test_display.c $(GRAPHICS_FONT_HEADERS) \
		$(GRAPHICS)/src/surface.c $(GRAPHICS)/src/render_builder.c \
		$(NDK)/src/bundle.c $(NDK)/src/utf8.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) -I$(GRAPHICS)/build/generated \
		$(COMMON_FLAGS) -O1 -fsanitize=address,undefined \
		-fno-omit-frame-pointer -ffunction-sections -fdata-sections \
		-Wl,--gc-sections $(GRAPHICS)/src/surface.c \
		$(GRAPHICS)/src/render_builder.c $(NDK)/src/bundle.c \
		$(NDK)/src/utf8.c tests/test_display.c -o $@

sanitize: $(HOST_TEST_SANITIZE)
	./$(HOST_TEST_SANITIZE)

analyze:
	@mkdir -p build/analyzer
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c main.c -o build/analyzer/main.o

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)

.PHONY: all size test sanitize analyze clean
