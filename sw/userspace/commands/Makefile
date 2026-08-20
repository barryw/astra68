include ../../../mk/m68k-cross.mk

# The programs that live in COMMANDS:.
#
# Each one is a directory with a source file of the same name, linked exactly
# the way the supervisor is -- same linker script, same crt0, same runtime --
# because "the initial image is a program like any other" has to be true in the
# build before it can be true anywhere else.
#
# There is no host half. A command is an m68k image and nothing about it runs
# on this host; what is testable about them is tested where the logic lives,
# and what is not is proved by the terminal gate running them on the machine.

CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
RUNTIME := ../runtime
STREAMS := ../streams
VFS := ../vfs
POSIX := ../posix
NDK := ../../../ndk
# picolibc, built out of tree: see third_party/picolibc/ASTRA_VENDOR.md.
PICOLIBC ?= $(HOME)/picolibc-astra
CPPFLAGS := -I../../include -I$(RUNTIME)/include -I$(STREAMS)/include \
	-I$(VFS)/include -I$(POSIX)/include -I$(NDK)/include \
	-isystem $(PICOLIBC)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
LINK_FLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections -T $(RUNTIME)/astra_user.ld

RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart.a
STREAMS_LIB := $(STREAMS)/build/m68k/libastrastreams.a
VFS_LIB := $(VFS)/build/m68k/libastravfs.a
POSIX_LIB := $(POSIX)/build/m68k/libastraposix.a
PICOLIBC_LIB := $(PICOLIBC)/lib/libc.a
RUNTIME_CRT0 := $(RUNTIME)/build/m68k/crt0.o

COMMANDS := status events devices which hello ls ps cat mkdir rm posix \
            heapbench date
# What the gate installs on the volume: stripped, the way the ROM's own image
# is, because a command carries no more DWARF onto a card than the supervisor
# carries into ROM. The unstripped ELF stays beside it for a debugger.
IMAGES := $(patsubst %,build/m68k/%,$(COMMANDS))

all: libraries
	$(MAKE) $(IMAGES) size

# The same two-invocation shape as the supervisor, and for the same reason: make
# stats the archives before the sub-makes that build them, so a rebuilt library
# would otherwise leave the previous image in place.
libraries:
	$(MAKE) -C $(RUNTIME) all
	$(MAKE) -C $(STREAMS) all
	$(MAKE) -C $(VFS) all
	$(MAKE) -C $(POSIX) all

# .SECONDEXPANSION so a command's source is <name>/<name>.c rather than a
# second list to keep in step with the first.
.SECONDEXPANSION:
build/m68k/%: $$*/$$*.c $(RUNTIME_LIB) $(STREAMS_LIB) $(VFS_LIB) \
		$(POSIX_LIB) $(RUNTIME_CRT0) $(RUNTIME)/astra_user.ld
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) \
		$(LINK_FLAGS) -o $@.elf $(RUNTIME_CRT0) $< \
		-Wl,--start-group $(VFS_LIB) $(STREAMS_LIB) $(POSIX_LIB) \
		$(RUNTIME_LIB) $(PICOLIBC_LIB) -Wl,--end-group -lgcc
	$(OBJCOPY) --strip-debug --remove-section=.astra_events $@.elf $@

size: $(IMAGES)
	$(SIZE) $(IMAGES)

# Header dependencies: a stale object compiled against an older ABI
# constant is indistinguishable from a working one until it runs.
-include $(wildcard build/m68k/*.d)

clean:
	rm -rf build

# Nothing to run on this host, and the targets have to exist so the userspace
# Makefile's loop does not stop here.
test sanitize analyze:
	@:

.PHONY: all libraries size test sanitize analyze clean
