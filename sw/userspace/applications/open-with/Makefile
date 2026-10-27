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
TARGET := build/m68k/open-with.elf
IMAGE := build/m68k/open-with
OBJECT := build/m68k/main.o
LIBRARIES := $(ASTRA_INTERFACE_LIBRARY) $(ASTRA_CONFIG_LIBRARY) \
	$(ASTRA_FILESYSTEM_LIBRARY) $(ASTRA_GRAPHICS_LIBRARY) \
	$(ASTRA_FONT_LIBRARY) $(ASTRA_SYSTEM_LIBRARY) \
	$(ASTRA_RUNTIME_LIBRARY) $(ASTRA_COMPILER_LIBRARY)
include ../../dynamic-program.mk

$(OBJECT): main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) -MMD -MP -c $< -o $@

test: all

sanitize analyze:
	@true

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)
.PHONY: test sanitize analyze clean
