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
TARGET_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic \
	$(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC $(ASTRA_DEBUG_FLAGS)
TARGET := build/m68k/pcm-certify.elf
IMAGE := build/m68k/pcm-certify
OBJECT := build/m68k/main.o
LIBRARIES := $(ASTRA_PCM_LIBRARY) $(ASTRA_RUNTIME_LIBRARY) \
	$(ASTRA_COMPILER_LIBRARY)
include ../../dynamic-program.mk

$(OBJECT): main.c $(NDK)/include/astra/pcm.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

test sanitize analyze: $(TARGET)

clean:
	rm -rf build

-include $(OBJECT:.o=.d)
.PHONY: test sanitize analyze clean
