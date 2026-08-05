# lwext4 big-endian evaluation rig.
#
# lwext4 is not vendored. Point LWEXT4_DIR at a checkout of
# https://github.com/gkostka/lwext4 (evaluated at
# 58bcf89a121b72d4fb66334f1693d3b30e4cb9c5), apply patches/, then build the
# host and MC68030 probes.
#
# The m68k probe is a Linux user-mode binary run under qemu-m68k. It exists to
# qualify lwext4's big-endian behaviour and its allocation and size budgets
# before any decision to run it inside an Astra service.

LWEXT4_DIR ?= $(CURDIR)/lwext4
ALLOC_DIR ?= $(CURDIR)/../../alloc
QEMU_M68K ?= qemu-m68k
IMAGE ?= build/probe.img
IMAGE_MB ?= 32
BLOCK_SIZE ?= 4096

CROSS ?= m68k-linux-gnu-
HOST_CC ?= gcc

CPPFLAGS := -Iinclude -I$(LWEXT4_DIR)/include -I$(LWEXT4_DIR)/blockdev/linux \
	-DCONFIG_USE_DEFAULT_CONFIG=0
COMMON_FLAGS := -std=gnu11 -O0 -g -fno-strict-aliasing
TARGET_FLAGS := -static -m68030
SIZE_FLAGS := -std=gnu11 -Os -m68030 -msoft-float -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -DCONFIG_DEBUG_PRINTF=0 \
	-DCONFIG_DEBUG_ASSERT=0

FULL_SOURCES := $(wildcard $(LWEXT4_DIR)/src/*.c)
BSD_SOURCES := $(filter-out %/ext4_extent.c %/ext4_xattr.c,$(FULL_SOURCES))
BACKEND := $(LWEXT4_DIR)/blockdev/linux/file_dev.c

all: build/probe-host build/probe-m68k

build/probe-host: src/probe.c $(FULL_SOURCES) $(BACKEND)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $^ -o $@

build/probe-m68k: src/probe.c $(FULL_SOURCES) $(BACKEND)
	@mkdir -p $(@D)
	$(CROSS)gcc $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $^ -o $@

# GPL-free profile: ext4_extent.c and ext4_xattr.c are the only GPLv2 files.
build/probe-m68k-bsd: src/probe.c src/xattr_stub.c $(BSD_SOURCES) $(BACKEND)
	@mkdir -p $(@D)
	$(CROSS)gcc $(CPPFLAGS) -DASTRA_PROFILE_BSD $(COMMON_FLAGS) \
		$(TARGET_FLAGS) $^ -o $@

build/probe-m68k-alloc: src/probe.c src/alloc_track.c $(FULL_SOURCES) $(BACKEND)
	@mkdir -p $(@D)
	$(CROSS)gcc $(CPPFLAGS) -DCONFIG_USE_USER_MALLOC=1 -DASTRA_TRACK_ALLOC \
		$(COMMON_FLAGS) $(TARGET_FLAGS) $^ -o $@

# Same workload carried by the real bounded allocator instead of the host heap.
build/probe-m68k-astra-alloc: src/probe.c src/port_alloc.c $(ALLOC_DIR)/src/alloc.c \
		$(FULL_SOURCES) $(BACKEND)
	@mkdir -p $(@D)
	$(CROSS)gcc $(CPPFLAGS) -I$(ALLOC_DIR)/include \
		-DCONFIG_USE_USER_MALLOC=1 -DASTRA_TRACK_ALLOC \
		$(COMMON_FLAGS) $(TARGET_FLAGS) $^ -o $@

patch:
	for p in patches/*.patch; do patch -d $(LWEXT4_DIR) -p1 -N < $$p || true; done

$(IMAGE):
	@mkdir -p $(@D)
	dd if=/dev/zero of=$@ bs=1M count=$(IMAGE_MB) status=none
	mke2fs -q -t ext4 -b $(BLOCK_SIZE) -I 256 -O ^64bit,^casefold -F $@

# Format offline with mke2fs, populate big-endian, then verify with the tools
# that do not share lwext4's code: e2fsck and the Linux ext4 driver.
interop: build/probe-m68k $(IMAGE)
	$(QEMU_M68K) build/probe-m68k $(IMAGE) populate
	e2fsck -fn $(IMAGE)

alloc: build/probe-m68k-alloc $(IMAGE)
	$(QEMU_M68K) build/probe-m68k-alloc $(IMAGE) populate

# Qualifies libastraalloc under lwext4's real allocation pattern, big-endian.
astra-alloc: build/probe-m68k-astra-alloc $(IMAGE)
	$(QEMU_M68K) build/probe-m68k-astra-alloc $(IMAGE) populate

size:
	@mkdir -p build/size/full build/size/bsd
	@for f in $(FULL_SOURCES); do \
		$(CROSS)gcc $(CPPFLAGS) $(SIZE_FLAGS) -c $$f \
			-o build/size/full/`basename $$f .c`.o; done
	@for f in $(BSD_SOURCES); do \
		$(CROSS)gcc $(CPPFLAGS) -DASTRA_PROFILE_BSD $(SIZE_FLAGS) -c $$f \
			-o build/size/bsd/`basename $$f .c`.o; done
	@$(CROSS)gcc $(CPPFLAGS) -DASTRA_PROFILE_BSD $(SIZE_FLAGS) \
		-c src/xattr_stub.c -o build/size/bsd/xattr_stub.o
	@echo "full profile (extents + xattr, includes GPLv2 files):"
	@$(CROSS)size -t build/size/full/*.o | tail -1
	@echo "BSD-only profile (no extents, no xattr):"
	@$(CROSS)size -t build/size/bsd/*.o | tail -1

clean:
	rm -rf build

.PHONY: all patch interop alloc astra-alloc size clean
