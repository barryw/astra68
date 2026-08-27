include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
RUNTIME := ../../runtime
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
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
LINK_FLAGS := -nostdlib -static -Wl,--build-id=none -Wl,--gc-sections \
	-Wl,-z,max-page-size=0x1000 -T $(RUNTIME)/astra_user.ld
TARGET := build/m68k/ntpd.elf
IMAGE := build/m68k/ntpd
OBJECT := build/m68k/main.o
RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart.a
STREAMS_LIB := $(STREAMS)/build/m68k/libastrastreams.a
VFS_LIB := $(VFS)/build/m68k/libastravfs.a
POSIX_LIB := $(POSIX)/build/m68k/libastraposix.a
NTP_LIB := $(NTP)/build/m68k/libastrantp.a
PICOLIBC_LIB := $(PICOLIBC)/lib/libc.a
CRT0 := $(RUNTIME)/build/m68k/crt0.o
ASTRA_PROGRAM_OWNER_DIRS := $(RUNTIME) $(STREAMS) $(VFS) $(POSIX) $(NTP)
include ../../program.mk

$(OBJECT): main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

$(TARGET): $(OBJECT) $(CRT0) $(NTP_LIB) $(POSIX_LIB) $(VFS_LIB) \
		$(STREAMS_LIB) $(RUNTIME_LIB) $(PICOLIBC_LIB) \
		$(RUNTIME)/astra_user.ld
	$(CC) $(TARGET_FLAGS) $(LINK_FLAGS) -o $@ $(CRT0) $(OBJECT) \
		-Wl,--start-group $(NTP_LIB) $(POSIX_LIB) $(VFS_LIB) \
		$(STREAMS_LIB) $(RUNTIME_LIB) $(PICOLIBC_LIB) -lgcc -Wl,--end-group

$(IMAGE): $(TARGET)
	$(OBJCOPY) --strip-all --remove-section=.astra_events $< $@

size: $(IMAGE)
	$(SIZE) $(IMAGE)

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
