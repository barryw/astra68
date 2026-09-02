include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
AR := $(CROSS)gcc-ar
SIZE := $(CROSS)size
HOST_CC ?= cc
ANALYZER_CC ?= gcc
RUNTIME := ../runtime
LWEXT4 := ../../../third_party/lwext4
MKE2FS ?= mke2fs
E2FSCK ?= e2fsck
CPPFLAGS := -Iinclude -I../../include -I$(RUNTIME)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -O2 -flto -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
HOST_FLAGS := -O2 -fno-builtin
# Host tests need the same rebuild discipline as the target build: a test binary
# silently linked against an older header reports a result it did not earn, and
# that is not hypothetical — it happened during the partition work.
#
# The target build uses depfiles. The host rules compile and link in one step
# from many sources, which clang refuses to combine with -MMD and a single -o,
# so they take every header as a prerequisite instead. Coarser than a depfile
# and it rebuilds more than it must; host tests are cheap and a wrong pass is
# not.
HOST_HEADERS := $(wildcard include/astra/*.h ../../include/astra/*.h \
	$(RUNTIME)/include/astra/*.h $(ALLOC)/include/astra/*.h \
	port/include/generated/*.h tests/*.h)
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
THREAD_SANITIZER_FLAGS := -fsanitize=thread -fno-omit-frame-pointer -g
SOURCES := src/block_device.c src/memory_block.c src/mbr.c
# The lease backend calls syscalls, so it is target-only.
OBJECTS := build/m68k/block_device.o build/m68k/memory_block.o \
	build/m68k/mbr.o build/m68k/lease_block.o
TARGET := build/m68k/libastrablock.a
HOST_TEST := build/host/test_block_device

# ---------------------------------------------------------------------------
# lwext4
#
# ext4_mkfs.c and ext4_mbr.c are vendored but not built: nothing in the core
# calls either, and ext4_mkfs mis-accounts free blocks when the last block
# group is short, which is exactly the 4 KiB profile Astra uses. Astra formats
# offline with mke2fs. See third_party/lwext4/ASTRA_VENDOR.md.
LWEXT4_SOURCES := $(filter-out %/ext4_mkfs.c %/ext4_mbr.c, \
	$(wildcard $(LWEXT4)/src/*.c))
LWEXT4_OBJECTS := $(patsubst $(LWEXT4)/src/%.c,build/m68k/lwext4/%.o, \
	$(LWEXT4_SOURCES))

# port/include supplies generated/ext4_config.h, which lwext4 includes when
# CONFIG_USE_DEFAULT_CFG is 0. The name is CFG, not CONFIG; spelling it wrong
# still works by accident because the guard reads an undefined macro as 0.
EXT4_CPPFLAGS := -I$(LWEXT4)/include -Iport/include -DCONFIG_USE_DEFAULT_CFG=0
# Vendored code is not held to the Astra warning contract. Turning -Werror
# -pedantic on third-party sources converts an upstream bump into an editing
# job and pressures the next porter to patch upstream for style.
EXT4_VENDOR_FLAGS := -std=gnu11 -Wall

PORT_SOURCES := src/ext4_port.c src/ext4_alloc.c src/ext4_xattr_stub.c \
	src/ext4_time.c src/ext4_clock_machine.c
PORT_OBJECTS := build/m68k/ext4_port.o build/m68k/ext4_alloc.o \
	build/m68k/ext4_xattr_stub.o build/m68k/ext4_time.o \
	build/m68k/ext4_clock_machine.o
ANALYZER_SOURCES := $(SOURCES) src/lease_block.c $(PORT_SOURCES)
ANALYZER_OBJECTS := $(patsubst src/%.c,build/analyzer/%.o,$(ANALYZER_SOURCES))
EXT4_TARGET := build/m68k/libastraext4.a

# Freestanding libc shims, target only: the host has a real C library and must
# keep using it, so these never reach a host compile.
FREESTANDING := -I$(RUNTIME)/freestanding

ALLOC := ../alloc
ALLOC_CPPFLAGS := -I$(ALLOC)/include

all: $(TARGET) $(EXT4_TARGET) size

$(TARGET): $(OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

$(EXT4_TARGET): $(LWEXT4_OBJECTS) $(PORT_OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

build/m68k/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

# The port speaks both dialects: Astra headers and lwext4 headers.
build/m68k/ext4_port.o: src/ext4_port.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(EXT4_CPPFLAGS) $(FREESTANDING) $(COMMON_FLAGS) \
		$(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/ext4_alloc.o: src/ext4_alloc.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ALLOC_CPPFLAGS) $(EXT4_CPPFLAGS) $(FREESTANDING) \
		$(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/ext4_time.o: src/ext4_time.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(EXT4_CPPFLAGS) $(FREESTANDING) $(COMMON_FLAGS) \
		$(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

# The machine's clock: the runtime, not lwext4, so it needs the runtime headers
# and never reaches a host build.
build/m68k/ext4_clock_machine.o: src/ext4_clock_machine.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(RUNTIME)/include $(COMMON_FLAGS) \
		$(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/ext4_xattr_stub.o: src/ext4_xattr_stub.c
	@mkdir -p $(@D)
	$(CC) $(EXT4_CPPFLAGS) $(FREESTANDING) $(EXT4_VENDOR_FLAGS) \
		$(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/lwext4/%.o: $(LWEXT4)/src/%.c
	@mkdir -p $(@D)
	$(CC) $(EXT4_CPPFLAGS) $(FREESTANDING) $(EXT4_VENDOR_FLAGS) \
		$(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

$(HOST_TEST): $(SOURCES) tests/test_block_device.c $(HOST_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SOURCES) tests/test_block_device.c -o $@

LEASE_TEST_SOURCES := src/lease_block.c tests/test_lease_block.c

build/host/test_lease_block: $(LEASE_TEST_SOURCES) $(HOST_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(LEASE_TEST_SOURCES) -o $@

# The port's own logic: transfer splitting, the errno map, lock re-entry and
# init validation. It drives the ext4_blockdev callbacks directly, so it needs
# lwext4's headers but none of its code, and it runs everywhere.
PORT_TEST_SOURCES := src/block_device.c src/memory_block.c src/ext4_port.c \
	tests/test_ext4_port.c

build/host/test_ext4_port: $(PORT_TEST_SOURCES) $(HOST_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(EXT4_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(PORT_TEST_SOURCES) -o $@

# The partition table reader, which decides which window a volume is confined
# to. It touches no lwext4 header: a partition table is not a filesystem.
MBR_TEST_SOURCES := src/block_device.c src/memory_block.c src/mbr.c \
	tests/test_mbr.c

build/host/test_mbr: $(MBR_TEST_SOURCES) $(HOST_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(MBR_TEST_SOURCES) -o $@

test: $(HOST_TEST) build/host/test_lease_block build/host/test_ext4_port \
		build/host/test_mbr
	./$(HOST_TEST)
	./build/host/test_lease_block
	./build/host/test_ext4_port
	./build/host/test_mbr

# ---------------------------------------------------------------------------
# Whole-library mount test.
#
# Split from `test` because it needs mke2fs to build the volume. A gate that
# silently skips when a tool is missing reports success it did not earn, so
# this is a target you have to ask for, and it fails loudly when it cannot run.
EXT4_MOUNT_TEST_SOURCES := src/block_device.c src/memory_block.c src/mbr.c \
	src/ext4_port.c src/ext4_alloc.c src/ext4_xattr_stub.c src/ext4_time.c \
	$(ALLOC)/src/alloc.c $(LWEXT4_SOURCES) tests/test_ext4_mount.c \
	tests/file_block.c

build/host/test_ext4_mount: $(EXT4_MOUNT_TEST_SOURCES) $(HOST_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(ALLOC_CPPFLAGS) $(EXT4_CPPFLAGS) \
		$(EXT4_VENDOR_FLAGS) $(HOST_FLAGS) -pthread \
		$(EXT4_MOUNT_TEST_SOURCES) \
		-o $@

build/host/test_ext4_mount_sanitize: $(EXT4_MOUNT_TEST_SOURCES) $(HOST_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(ALLOC_CPPFLAGS) $(EXT4_CPPFLAGS) \
		$(EXT4_VENDOR_FLAGS) $(SANITIZER_FLAGS) -pthread \
		$(EXT4_MOUNT_TEST_SOURCES) -o $@

build/host/test_ext4_mount_tsan: $(EXT4_MOUNT_TEST_SOURCES) $(HOST_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(ALLOC_CPPFLAGS) $(EXT4_CPPFLAGS) \
		$(EXT4_VENDOR_FLAGS) $(THREAD_SANITIZER_FLAGS) -pthread \
		$(EXT4_MOUNT_TEST_SOURCES) -o $@

# The frozen profile: 4 KiB blocks, 256-byte inodes, no 64bit, no casefold,
# and no extents or xattr because those two implementations are GPLv2 and are
# not vendored.
#
# metadata_csum_seed is subtracted because lwext4's supported-incompat set does
# not contain it and mount returns ENOTSUP; recent mke2fs builds enable it by
# default. metadata_csum itself is kept, and is exactly what patch 0002 exists
# to make work on big-endian.
#
# The subtractive form leaves this exposed to a future mke2fs adding another
# default incompat feature. That failure is loud — mount returns ENOTSUP and
# this target fails — which is the behaviour to want from a frozen profile.
EXT4_MKFS_FEATURES := ^64bit,^casefold,^extent,^ext_attr,^metadata_csum_seed

# The journal is pinned rather than left to mke2fs, which derives it from the
# volume size: 128 MiB for a 20 GB card.
#
# Two reasons, and the second is the larger one. Memory: lwext4's live
# descriptor count tracks journal size and saturates at 16 MiB, so a default
# journal costs 1,767 of the 33..64-byte descriptors against 855 for this one,
# at any volume size. Recovery: replay after an unclean shutdown is
# proportional to outstanding journal content, and this machine is a 12.5 MHz
# 68030 behind an SD card, so a 128 MiB journal bounds worst-case boot delay
# 32 times worse than a 4 MiB one.
#
# Astra's write workload is small; 4 MiB is many transactions' worth. The class
# table is deliberately NOT shrunk to match, so a card formatted elsewhere with
# a default journal still mounts. See src/ext4_alloc.c.
EXT4_MKFS_JOURNAL := -J size=4

format-volume:
	@test -n "$(VOLUME)"
	@test -n "$(VOLUME_BYTES)"
	truncate -s "$(VOLUME_BYTES)" "$(VOLUME)"
	$(MKE2FS) -q -t ext4 -b 4096 -I 256 -O $(EXT4_MKFS_FEATURES) \
		$(EXT4_MKFS_JOURNAL) -F "$(VOLUME)"

# The volume is rebuilt on every run rather than cached. The test writes the
# image back so e2fsck can judge it, which means a failed run leaves a
# half-populated volume behind; inheriting that would make the next run's
# result depend on the previous one's failure.
ext4-test: build/host/test_ext4_mount
	@mkdir -p build/host
	dd if=/dev/zero of=build/host/ext4-test.img bs=1048576 count=16 \
		status=none
	$(MKE2FS) -q -t ext4 -b 4096 -I 256 -O $(EXT4_MKFS_FEATURES) \
		$(EXT4_MKFS_JOURNAL) -F build/host/ext4-test.img
	./build/host/test_ext4_mount build/host/ext4-test.img
	$(E2FSCK) -fn build/host/ext4-test.img
	./build/host/test_ext4_mount build/host/ext4-test.img verify-only
	@echo "--- partitioned: the volume must not touch the boot partition"
	dd if=/dev/zero of=build/host/ext4-part.img bs=1048576 count=16 status=none
	$(MKE2FS) -q -t ext4 -b 4096 -I 256 -O $(EXT4_MKFS_FEATURES) \
		$(EXT4_MKFS_JOURNAL) -F build/host/ext4-part.img
	./build/host/test_ext4_mount build/host/ext4-part.img partitioned
	$(E2FSCK) -fn build/host/ext4-part.img
	@echo "--- full: a volume with no space left must say so"
	dd if=/dev/zero of=build/host/ext4-full.img bs=1048576 count=16 status=none
	$(MKE2FS) -q -t ext4 -b 4096 -I 256 -O $(EXT4_MKFS_FEATURES) \
		$(EXT4_MKFS_JOURNAL) -F build/host/ext4-full.img
	./build/host/test_ext4_mount build/host/ext4-full.img full
	$(E2FSCK) -fn build/host/ext4-full.img
	@echo "--- power cut: only explicit barriers survive, journal must recover"
	dd if=/dev/zero of=build/host/ext4-powercut.img bs=1048576 count=16 \
		status=none
	$(MKE2FS) -q -t ext4 -b 4096 -I 256 -O $(EXT4_MKFS_FEATURES) \
		$(EXT4_MKFS_JOURNAL) -F build/host/ext4-powercut.img
	@for pass in 1 2 3; do \
		echo "power-cut pass $$pass"; \
		./build/host/test_ext4_mount build/host/ext4-powercut.img \
			powercut-write || exit 1; \
		./build/host/test_ext4_mount build/host/ext4-powercut.img \
			powercut-recover || exit 1; \
		$(E2FSCK) -fn build/host/ext4-powercut.img || exit 1; \
	done
	@echo "--- journal I/O failure: abort read-only and recover cleanly"
	dd if=/dev/zero of=build/host/ext4-journal-failure.img bs=1048576 \
		count=16 status=none
	$(MKE2FS) -q -t ext4 -b 4096 -I 256 -O $(EXT4_MKFS_FEATURES) \
		$(EXT4_MKFS_JOURNAL) -F build/host/ext4-journal-failure.img
	./build/host/test_ext4_mount build/host/ext4-journal-failure.img \
		journal-failure-write
	./build/host/test_ext4_mount build/host/ext4-journal-failure.img \
		journal-failure-recover
	$(E2FSCK) -fn build/host/ext4-journal-failure.img
	@echo "--- stale journal: historical transactions must never replay"
	dd if=/dev/zero of=build/host/ext4-stale.img bs=1048576 count=16 \
		status=none
	$(MKE2FS) -q -t ext4 -b 4096 -I 256 -O $(EXT4_MKFS_FEATURES) \
		$(EXT4_MKFS_JOURNAL) -F build/host/ext4-stale.img
	./build/host/test_ext4_mount build/host/ext4-stale.img stale-setup
	./build/host/test_ext4_mount build/host/ext4-stale.img stale-history
	./build/host/test_ext4_mount build/host/ext4-stale.img stale-delete
	./build/host/test_ext4_mount build/host/ext4-stale.img stale-recover
	$(E2FSCK) -fn build/host/ext4-stale.img
	@echo "--- journal pressure: capacity commits before an operation can fail"
	dd if=/dev/zero of=build/host/ext4-pressure.img bs=1048576 count=16 \
		status=none
	$(MKE2FS) -q -t ext4 -b 4096 -I 256 -O $(EXT4_MKFS_FEATURES) \
		$(EXT4_MKFS_JOURNAL) -F build/host/ext4-pressure.img
	./build/host/test_ext4_mount build/host/ext4-pressure.img journal-pressure
	$(E2FSCK) -fn build/host/ext4-pressure.img

ext4-sanitize: build/host/test_ext4_mount_sanitize
	dd if=/dev/zero of=build/host/ext4-sanitize.img bs=1048576 count=16 \
		status=none
	$(MKE2FS) -q -t ext4 -b 4096 -I 256 -O $(EXT4_MKFS_FEATURES) \
		$(EXT4_MKFS_JOURNAL) -F build/host/ext4-sanitize.img
	./build/host/test_ext4_mount_sanitize build/host/ext4-sanitize.img
	$(E2FSCK) -fn build/host/ext4-sanitize.img

# Ubuntu's TSan runtime cannot reserve its shadow with normal ASLR on Beast.
ext4-tsan: build/host/test_ext4_mount_tsan
	dd if=/dev/zero of=build/host/ext4-tsan.img bs=1048576 count=16 \
		status=none
	$(MKE2FS) -q -t ext4 -b 4096 -I 256 -O $(EXT4_MKFS_FEATURES) \
		$(EXT4_MKFS_JOURNAL) -F build/host/ext4-tsan.img
	setarch x86_64 -R ./build/host/test_ext4_mount_tsan \
		build/host/ext4-tsan.img
	$(E2FSCK) -fn build/host/ext4-tsan.img

sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) $(LEASE_TEST_SOURCES) \
		-o build/host/test_lease_block_sanitize
	./build/host/test_lease_block_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) $(SOURCES) tests/test_block_device.c \
		-o build/host/test_block_device_sanitize
	./build/host/test_block_device_sanitize
	$(HOST_CC) $(CPPFLAGS) $(EXT4_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) $(PORT_TEST_SOURCES) \
		-o build/host/test_ext4_port_sanitize
	./build/host/test_ext4_port_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) $(MBR_TEST_SOURCES) \
		-o build/host/test_mbr_sanitize
	./build/host/test_mbr_sanitize

analyze: $(ANALYZER_OBJECTS)

build/analyzer/ext4_port.o build/analyzer/ext4_time.o: \
	ANALYZER_CPPFLAGS := $(EXT4_CPPFLAGS) $(FREESTANDING)
build/analyzer/ext4_alloc.o: ANALYZER_CPPFLAGS := \
	$(ALLOC_CPPFLAGS) $(EXT4_CPPFLAGS) $(FREESTANDING)
build/analyzer/ext4_xattr_stub.o: ANALYZER_CPPFLAGS := \
	$(EXT4_CPPFLAGS) $(FREESTANDING)
build/analyzer/ext4_xattr_stub.o: ANALYZER_CFLAGS := \
	$(EXT4_VENDOR_FLAGS) -Werror
build/analyzer/%.o: ANALYZER_CFLAGS ?= $(COMMON_FLAGS)

build/analyzer/%.o: src/%.c
	@mkdir -p $(@D)
	$(ANALYZER_CC) $(CPPFLAGS) $(ANALYZER_CPPFLAGS) $(ANALYZER_CFLAGS) \
		$(HOST_FLAGS) -fanalyzer -c $< -o $@

# ---------------------------------------------------------------------------
# Freestanding link proof.
#
# "lwext4 needs no C library" is a claim that only a link can settle. This
# links the whole filesystem stack into a real MC68030 image under the same
# -nostdlib contract a service uses, so any libc symbol that crept in through a
# vendored source or a port shim is an error here rather than a discovery made
# while bringing up hardware.
LINK_FLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -T $(RUNTIME)/astra_user.ld

linkcheck: $(EXT4_TARGET) $(TARGET)
	$(MAKE) -C $(RUNTIME) all
	$(MAKE) -C $(ALLOC) all
	@mkdir -p build/m68k
	$(CC) $(CPPFLAGS) $(ALLOC_CPPFLAGS) $(EXT4_CPPFLAGS) $(FREESTANDING) \
		$(COMMON_FLAGS) $(TARGET_FLAGS) -c tests/linkcheck.c \
		-o build/m68k/linkcheck.o
	$(CC) $(TARGET_FLAGS) $(LINK_FLAGS) -o build/m68k/linkcheck.elf \
		$(RUNTIME)/build/m68k/crt0.o build/m68k/linkcheck.o \
		$(EXT4_TARGET) $(TARGET) $(ALLOC)/build/m68k/libastraalloc.a \
		$(RUNTIME)/build/m68k/libastrart.a -lgcc
	@echo "freestanding link: OK"
	@$(SIZE) build/m68k/linkcheck.elf

size: $(OBJECTS) $(PORT_OBJECTS) $(LWEXT4_OBJECTS)
	$(SIZE) $(OBJECTS) $(PORT_OBJECTS)
	@echo "lwext4 (BSD-3 profile, no extents, no xattr, no mkfs, no mbr):"
	@$(SIZE) -t $(LWEXT4_OBJECTS) | tail -1

# Header dependencies: a stale object compiled against an older ABI
# constant is indistinguishable from a working one until it runs.
-include $(wildcard build/m68k/*.d build/m68k/*/*.d)

clean:
	rm -rf build

.PHONY: all test format-volume ext4-test ext4-sanitize ext4-tsan linkcheck sanitize \
	analyze size clean
