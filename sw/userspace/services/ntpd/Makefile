include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
READELF := $(CROSS)readelf
RUNTIME := ../../runtime
include $(RUNTIME)/astra-link.mk
include ../../libraries.mk
STREAMS := ../../streams
VFS := ../../vfs
POSIX := ../../posix
NTP := ../../ntp
NDK := ../../../../ndk
PICOLIBC ?= $(HOME)/picolibc-astra
CPPFLAGS := -I../../../include -I$(RUNTIME)/include -I$(STREAMS)/include \
	-I$(VFS)/include -I$(POSIX)/include -I$(NTP)/include -I$(NDK)/include \
	-isystem $(PICOLIBC)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC $(ASTRA_DEBUG_FLAGS)
TARGET := build/m68k/ntpd.elf
IMAGE := build/m68k/ntpd
OBJECT := build/m68k/main.o
CONFIG_LIBRARY := $(ASTRA_CONFIG_LIBRARY)
NTP_LIBRARY := $(ASTRA_NTP_LIBRARY)
LIBC_LIBRARY := $(ASTRA_LIBC_LIBRARY)
RUNTIME_LIBRARY := $(ASTRA_RUNTIME_LIBRARY)
FILESYSTEM_LIBRARY := $(ASTRA_FILESYSTEM_LIBRARY)
COMPILER_LIBRARY := $(ASTRA_COMPILER_LIBRARY)
LIBRARIES := $(CONFIG_LIBRARY) $(NTP_LIBRARY) $(LIBC_LIBRARY) \
	$(RUNTIME_LIBRARY) $(FILESYSTEM_LIBRARY) $(COMPILER_LIBRARY)
include ../../dynamic-program.mk

$(OBJECT): main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

test sanitize:
	$(MAKE) -C $(NTP) $@

analyze:
	@mkdir -p build/analyzer
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c main.c -o build/analyzer/main.o

clean:
	rm -rf build

-include $(OBJECT:.o=.d)

.PHONY: size test sanitize analyze clean
