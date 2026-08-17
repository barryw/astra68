include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
HOST_CC ?= cc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
RUNTIME := ../../runtime
GRAPHICS := ../../graphics
NDK := ../../../../ndk
CPPFLAGS := -I../../../../ndk/include -I../../../include -I$(RUNTIME)/include \
	-I$(GRAPHICS)/include -I../../../../fpga/arty/linux
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
LINK_FLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections -T $(RUNTIME)/astra_user.ld
TARGET := build/m68k/display.elf
IMAGE := build/m68k/display
OBJECT := build/m68k/main.o
RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart.a
GRAPHICS_LIB := $(GRAPHICS)/build/m68k/libastragraphics.a
NDK_LIB := $(NDK)/build/m68k/libastra.a
GRAPHICS_FONT_HEADERS := $(GRAPHICS)/build/generated/astra_font8x8.inc \
	$(GRAPHICS)/build/generated/astra_ui_font.inc \
	$(GRAPHICS)/build/generated/astra_mono_font.inc
HOST_TEST := build/host/test_display
HOST_TEST_SANITIZE := build/host/test_display_sanitize
CRT0 := $(RUNTIME)/build/m68k/crt0.o

all: $(IMAGE) size

$(TARGET): $(OBJECT) $(CRT0) $(GRAPHICS_LIB) $(NDK_LIB) $(RUNTIME_LIB) \
	$(RUNTIME)/astra_user.ld
	@mkdir -p $(@D)
	$(CC) $(TARGET_FLAGS) $(LINK_FLAGS) -o $@ $(CRT0) $(OBJECT) \
		$(GRAPHICS_LIB) $(NDK_LIB) $(RUNTIME_LIB) -lgcc

$(NDK_LIB):
	$(MAKE) -C $(NDK) all

$(IMAGE): $(TARGET)
	$(OBJCOPY) --strip-all $< $@

$(OBJECT): main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

size: $(IMAGE)
	$(SIZE) $(IMAGE)

$(GRAPHICS_FONT_HEADERS):
	$(MAKE) -C $(GRAPHICS) $(@:$(GRAPHICS)/%=%)

$(HOST_TEST): main.c tests/test_display.c $(GRAPHICS_FONT_HEADERS) \
		$(GRAPHICS)/src/surface.c $(GRAPHICS)/src/render_builder.c \
		$(NDK)/src/bundle.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) -I$(GRAPHICS)/build/generated \
		$(COMMON_FLAGS) -O2 -ffunction-sections -fdata-sections \
		-Wl,--gc-sections $(GRAPHICS)/src/surface.c \
		$(GRAPHICS)/src/render_builder.c $(NDK)/src/bundle.c \
		tests/test_display.c -o $@

test: $(HOST_TEST)
	./$(HOST_TEST)

$(HOST_TEST_SANITIZE): main.c tests/test_display.c $(GRAPHICS_FONT_HEADERS) \
		$(GRAPHICS)/src/surface.c $(GRAPHICS)/src/render_builder.c \
		$(NDK)/src/bundle.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) -I$(GRAPHICS)/build/generated \
		$(COMMON_FLAGS) -O1 -fsanitize=address,undefined \
		-fno-omit-frame-pointer -ffunction-sections -fdata-sections \
		-Wl,--gc-sections $(GRAPHICS)/src/surface.c \
		$(GRAPHICS)/src/render_builder.c $(NDK)/src/bundle.c \
		tests/test_display.c -o $@

sanitize: $(HOST_TEST_SANITIZE)
	./$(HOST_TEST_SANITIZE)

analyze:
	@true

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)

.PHONY: all size test sanitize analyze clean
