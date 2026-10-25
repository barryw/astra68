include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
READELF := $(CROSS)readelf
RUNTIME := ../../runtime
include $(RUNTIME)/astra-link.mk
include ../../libraries.mk
NDK := ../../../../ndk
CPPFLAGS := -I$(NDK)/include -I../../../include -I$(RUNTIME)/include
FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic \
	$(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC $(ASTRA_DEBUG_FLAGS)
TARGET := build/m68k/disk-setup.elf
IMAGE := build/m68k/disk-setup
OBJECT := build/m68k/main.o
LIBRARIES := $(ASTRA_INTERFACE_LIBRARY) $(ASTRA_DOS_LIBRARY) $(ASTRA_FILESYSTEM_LIBRARY) \
    $(ASTRA_GRAPHICS_LIBRARY) $(ASTRA_FONT_LIBRARY) $(ASTRA_SYSTEM_LIBRARY) \
	$(ASTRA_RUNTIME_LIBRARY) $(ASTRA_COMPILER_LIBRARY)
include ../../dynamic-program.mk

$(OBJECT): main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) -MMD -MP -c $< -o $@

test: all
	@$(READELF) --dyn-syms -W $(TARGET) | grep -q 'astra_volume_snapshot_ancestor@ASTRA_FILESYSTEM_15.0'
	@$(READELF) --dyn-syms -W $(TARGET) | grep -q 'astra_dos_disk_snapshot_open@ASTRA_DOS_1.0'
	@$(READELF) --dyn-syms -W $(TARGET) | grep -q 'astra_dos_disk_submit@ASTRA_DOS_1.0'

sanitize analyze:
	@true

clean:
	rm -rf build

.PHONY: test sanitize analyze clean
