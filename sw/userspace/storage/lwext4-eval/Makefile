# lwext4 big-endian qualification rig.
#
# lwext4 is now vendored at ../../../../third_party/lwext4 with the three
# big-endian defect patches applied in tree, so this rig no longer points at an
# external checkout and no longer applies patches. What it does now is keep the
# vendored tree honest: it builds the same port, the same build profile and the
# same bounded allocator the MC68030 service ships, as a Linux user-mode m68k
# binary, and runs the storage suite's own mount test under qemu-m68k against a
# real mke2fs image that e2fsck then judges.
#
# This is the only place big-endian and LP32 are both visible. That is also why
# the allocator class table is measured here and not on the host: an m68k
# struct carrying a pointer is smaller than the host's and lands in a different
# size class, so host peaks describe a different workload.
#
# Needs on Beast: m68k-linux-gnu-gcc, a user-mode qemu-m68k, mke2fs, e2fsck.

LWEXT4 ?= ../../../../third_party/lwext4
USERSPACE := ../..
STORAGE := $(USERSPACE)/storage
ALLOC_DIR ?= $(USERSPACE)/alloc
QEMU_M68K ?= qemu-m68k
IMAGE ?= build/probe.img
IMAGE_MB ?= 16
BLOCK_SIZE ?= 4096
MKE2FS ?= mke2fs
E2FSCK ?= e2fsck

CROSS ?= m68k-linux-gnu-
HOST_CC ?= gcc

# The frozen mkfs profile, kept identical to sw/userspace/storage/Makefile.
MKFS_FEATURES := ^64bit,^casefold,^extent,^ext_attr,^metadata_csum_seed

# ext4_mkfs.c and ext4_mbr.c are vendored but never built; see
# third_party/lwext4/ASTRA_VENDOR.md.
LWEXT4_SOURCES := $(filter-out %/ext4_mkfs.c %/ext4_mbr.c, \
	$(wildcard $(LWEXT4)/src/*.c))

CPPFLAGS := -I$(LWEXT4)/include -I$(STORAGE)/port/include \
	-I$(STORAGE)/include -I$(USERSPACE)/../include \
	-I$(USERSPACE)/runtime/include -I$(ALLOC_DIR)/include \
	-DCONFIG_USE_DEFAULT_CFG=0
COMMON_FLAGS := -std=gnu11 -O2 -g -fno-strict-aliasing
TARGET_FLAGS := -static -m68030
SIZE_FLAGS := -std=gnu11 -Os -m68030 -msoft-float -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections

# Exactly the sources the service links, plus the mount test that drives them.
PORT_SOURCES := $(STORAGE)/src/block_device.c $(STORAGE)/src/memory_block.c \
	$(STORAGE)/src/ext4_port.c $(STORAGE)/src/ext4_alloc.c \
	$(STORAGE)/src/ext4_xattr_stub.c $(ALLOC_DIR)/src/alloc.c
MOUNT_TEST := $(STORAGE)/tests/test_ext4_mount.c
MOUNT_INPUTS := $(MOUNT_TEST) $(PORT_SOURCES) $(LWEXT4_SOURCES)

all: build/mount-m68k build/mount-host

# Big-endian, 32-bit, run under qemu-m68k. This is the run that counts.
build/mount-m68k: $(MOUNT_INPUTS)
	@mkdir -p $(@D)
	$(CROSS)gcc $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $^ -o $@

# The little-endian reference over the same sources.
build/mount-host: $(MOUNT_INPUTS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $^ -o $@

# The control that separates a big-endian defect from a 32-bit or m68k ABI one:
# LP32 and m68k are unchanged, only the endian conversions are switched off.
#
# It is a diagnostic, not a gate, and it is deliberately not wired into any
# target that runs it. Against a real mke2fs image it MUST fail to mount with
# ENOTSUP, because it reads a little-endian superblock as native big-endian.
# That failure is the control working. Reach for it when the big-endian build
# misbehaves and the question is whether the cause is endianness at all: a
# defect that reproduces here as well is not an endian defect.
build/mount-m68k-le: $(MOUNT_INPUTS)
	@mkdir -p $(@D)
	$(CROSS)gcc $(CPPFLAGS) -DASTRA_FORCE_LE $(COMMON_FLAGS) \
		$(TARGET_FLAGS) $^ -o $@

# Always rebuilt, never cached: the test writes the image back so e2fsck can
# judge it, so a failed run leaves a half-populated volume that would otherwise
# be inherited by the next one.
image:
	@mkdir -p $(dir $(IMAGE))
	dd if=/dev/zero of=$(IMAGE) bs=1M count=$(IMAGE_MB) status=none
	$(MKE2FS) -q -t ext4 -b $(BLOCK_SIZE) -I 256 -O $(MKFS_FEATURES) -F \
		$(IMAGE)

# Format offline with mke2fs, populate big-endian through the Astra port, then
# verify with the tool that shares no code with lwext4.
interop: build/mount-m68k image
	$(QEMU_M68K) build/mount-m68k $(IMAGE)
	$(E2FSCK) -fn $(IMAGE)

# Re-reading a volume the big-endian build already wrote proves the other
# direction: the second run mounts what the first one left and re-verifies
# every file before touching anything.
reread: build/mount-m68k image
	$(QEMU_M68K) build/mount-m68k $(IMAGE)
	$(QEMU_M68K) build/mount-m68k $(IMAGE) verify-only
	$(E2FSCK) -fn $(IMAGE)

# The allocator class table is measured from this run, not from a host one.
measure: build/mount-m68k image
	$(QEMU_M68K) build/mount-m68k $(IMAGE)

size:
	@mkdir -p build/size
	@for f in $(LWEXT4_SOURCES); do \
		$(CROSS)gcc $(CPPFLAGS) $(SIZE_FLAGS) -c $$f \
			-o build/size/`basename $$f .c`.o; done
	@echo "lwext4 BSD-3 profile (no extents, no xattr, no mkfs, no mbr):"
	@$(CROSS)size -t build/size/*.o | tail -1

clean:
	rm -rf build

.PHONY: all image interop reread measure size clean
