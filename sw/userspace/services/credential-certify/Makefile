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
TARGET := build/m68k/credential-certify.elf
IMAGE := build/m68k/credential-certify
OBJECT := build/m68k/main.o
LIBRARIES := $(ASTRA_CONFIG_LIBRARY) $(ASTRA_RUNTIME_LIBRARY) \
	$(ASTRA_COMPILER_LIBRARY) $(ASTRA_FILESYSTEM_LIBRARY)
include ../../dynamic-program.mk

$(OBJECT): main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) -MMD -MP -c $< -o $@

test sanitize: all
analyze: $(TARGET)
clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)
.PHONY: test sanitize analyze clean
