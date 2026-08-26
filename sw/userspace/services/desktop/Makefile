include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
RUNTIME := ../../runtime
GRAPHICS := ../../graphics
VFS := ../../vfs
NDK := ../../../../ndk
CPPFLAGS := -I../../../../ndk/include -I../../../include -I$(RUNTIME)/include \
	-I$(GRAPHICS)/include -I$(VFS)/include

CPPFLAGS += -I../../interface/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
LINK_FLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections -T $(RUNTIME)/astra_user.ld
TARGET := build/m68k/desktop.elf
IMAGE := build/m68k/desktop
OBJECT := build/m68k/main.o
RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart.a
GRAPHICS_LIB := $(GRAPHICS)/build/m68k/libastragraphics.a
VFS_LIB := $(VFS)/build/m68k/libastravfs.a
NDK_LIB := $(NDK)/build/m68k/libastra.a
CRT0 := $(RUNTIME)/build/m68k/crt0.o
ASTRA_PROGRAM_OWNER_DIRS := $(RUNTIME) $(GRAPHICS) $(VFS) $(NDK)
include ../../program.mk

$(TARGET): $(OBJECT) $(CRT0) $(GRAPHICS_LIB) $(VFS_LIB) $(NDK_LIB) $(RUNTIME_LIB) \
	$(RUNTIME)/astra_user.ld
	@mkdir -p $(@D)
	$(CC) $(TARGET_FLAGS) $(LINK_FLAGS) -o $@ $(CRT0) $(OBJECT) \
		$(GRAPHICS_LIB) $(VFS_LIB) $(NDK_LIB) $(RUNTIME_LIB) -lgcc

$(NDK_LIB):
	$(MAKE) -C $(NDK) all

$(VFS_LIB):
	$(MAKE) -C $(VFS) all

$(IMAGE): $(TARGET)
	$(OBJCOPY) --strip-all $< $@

$(OBJECT): main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

size: $(IMAGE)
	$(SIZE) $(IMAGE)

test sanitize:
	@true

analyze:
	@mkdir -p build/analyzer
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c main.c -o build/analyzer/main.o

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)

.PHONY: all size test sanitize analyze clean
