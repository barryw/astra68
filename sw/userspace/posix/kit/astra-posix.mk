# Shared MC68030 compile and link contract for unmodified POSIX programs.
ASTRA_POSIX_KIT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ASTRA_ROOT ?= $(abspath $(ASTRA_POSIX_KIT_DIR)/../../../..)
PICOLIBC ?= $(HOME)/picolibc-astra
include $(ASTRA_ROOT)/mk/m68k-cross.mk

ASTRA_POSIX_RUNTIME := $(ASTRA_ROOT)/sw/userspace/runtime
ASTRA_POSIX_STREAMS := $(ASTRA_ROOT)/sw/userspace/streams
ASTRA_POSIX_VFS := $(ASTRA_ROOT)/sw/userspace/vfs
ASTRA_POSIX_LAYER := $(ASTRA_ROOT)/sw/userspace/posix
ASTRA_POSIX_NDK := $(ASTRA_ROOT)/ndk

ASTRA_POSIX_PROJECT_CPPFLAGS := \
	-I$(ASTRA_ROOT)/sw/include \
	-I$(ASTRA_POSIX_RUNTIME)/include \
	-I$(ASTRA_POSIX_STREAMS)/include \
	-I$(ASTRA_POSIX_VFS)/include \
	-I$(ASTRA_POSIX_LAYER)/include \
	-I$(ASTRA_POSIX_NDK)/include
ASTRA_POSIX_CPPFLAGS := $(ASTRA_POSIX_PROJECT_CPPFLAGS) \
	-isystem $(PICOLIBC)/include
# The Astra compiler finds its configured sysroot after libstdc++ so that
# C++ wrappers using include_next reach the C header they wrap.
ASTRA_POSIX_CXXCPPFLAGS := $(ASTRA_POSIX_PROJECT_CPPFLAGS)
ASTRA_POSIX_CFLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections
ASTRA_POSIX_CXXFLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os \
	-ffunction-sections -fdata-sections
ASTRA_POSIX_LDFLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections \
	-T $(ASTRA_POSIX_RUNTIME)/astra_user.ld
ASTRA_POSIX_CRT0 := $(ASTRA_POSIX_RUNTIME)/build/m68k/crt0-hosted.o
ASTRA_POSIX_PROGRAM_SOURCE := $(ASTRA_POSIX_KIT_DIR)/program.c
ASTRA_POSIX_LIBS := -Wl,--start-group \
	$(ASTRA_POSIX_LAYER)/build/m68k/libastraposix.a \
	$(ASTRA_POSIX_STREAMS)/build/m68k/libastrastreams.a \
	$(ASTRA_POSIX_VFS)/build/m68k/libastravfs.a \
	$(ASTRA_POSIX_RUNTIME)/build/m68k/libastrart.a \
	$(PICOLIBC)/lib/libc.a $(PICOLIBC)/lib/libm.a -lgcc \
	-Wl,--end-group
