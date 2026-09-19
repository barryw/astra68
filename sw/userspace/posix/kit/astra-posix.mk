# Source-tree locations for the canonical POSIX startup kit shipped in the NDK.
ASTRA_POSIX_KIT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ASTRA_ROOT ?= $(abspath $(ASTRA_POSIX_KIT_DIR)/../../../..)
include $(ASTRA_ROOT)/mk/m68k-cross.mk

ASTRA_POSIX_RUNTIME := $(ASTRA_ROOT)/sw/userspace/runtime
ASTRA_POSIX_COMPILER := $(ASTRA_ROOT)/sw/userspace/compiler
ASTRA_POSIX_STREAMS := $(ASTRA_ROOT)/sw/userspace/streams
ASTRA_POSIX_VFS := $(ASTRA_ROOT)/sw/userspace/vfs
ASTRA_POSIX_NETWORK := $(ASTRA_ROOT)/sw/userspace/network
ASTRA_POSIX_LAYER := $(ASTRA_ROOT)/sw/userspace/posix
ASTRA_POSIX_NDK := $(ASTRA_ROOT)/ndk
RUNTIME := $(ASTRA_POSIX_RUNTIME)
include $(RUNTIME)/astra-link.mk
include $(ASTRA_ROOT)/sw/userspace/libraries.mk

ASTRA_NDK_ROOT := $(ASTRA_POSIX_NDK)
ASTRA_CROSS := $(CROSS)
ASTRA_CC := $(CROSS)gcc
ASTRA_CXX := $(CROSS)g++
ASTRA_READELF := $(CROSS)readelf
ASTRA_ABI_FLAGS := $(ASTRA_TARGET_ABI_FLAGS)
ASTRA_CPPFLAGS := -I$(ASTRA_ROOT)/sw/include \
	-I$(ASTRA_POSIX_RUNTIME)/include \
	-I$(ASTRA_POSIX_STREAMS)/include \
	-I$(ASTRA_POSIX_VFS)/include \
	-I$(ASTRA_POSIX_LAYER)/include \
	-I$(ASTRA_POSIX_NDK)/include
ASTRA_POSIX_CPPFLAGS := $(ASTRA_CPPFLAGS) -isystem $(PICOLIBC)/include
# The Astra compiler finds its configured sysroot after libstdc++ so that
# C++ wrappers using include_next reach the C header they wrap.
ASTRA_POSIX_CXXCPPFLAGS := $(ASTRA_CPPFLAGS)
ASTRA_LINKER_SCRIPT := $(ASTRA_POSIX_RUNTIME)/astra_user.ld
ASTRA_STATIC_LINKER_SCRIPT := $(ASTRA_POSIX_RUNTIME)/astra_static_user.ld
ASTRA_LIBRARY_SEARCH_FLAGS := -Wl,-L$(ASTRA_POSIX_RUNTIME) \
	$(ASTRA_SHARED_LIBRARY_SEARCH_FLAGS)
ASTRA_CRT0 := $(ASTRA_POSIX_RUNTIME)/build/m68k/crt0-dynamic.o
ASTRA_STATIC_CRT0 := $(ASTRA_POSIX_RUNTIME)/build/m68k/crt0.o
ASTRA_STATIC_HOSTED_CRT0 := \
	$(ASTRA_POSIX_RUNTIME)/build/m68k/crt0-hosted.o
ASTRA_POSIX_PROGRAM_SOURCE := $(ASTRA_POSIX_KIT_DIR)/program.c
ASTRA_DYNAMIC_EXECUTABLE_CHECK := \
	$(ASTRA_ROOT)/tools/check_dynamic_executable.py
ASTRA_POSIX_STATIC_LIBS := -Wl,--start-group \
	$(ASTRA_POSIX_LAYER)/build/m68k/libastraposix.a \
	$(ASTRA_POSIX_STREAMS)/build/m68k/libastrastreams.a \
	$(ASTRA_POSIX_VFS)/build/m68k/libastravfs.a \
	$(ASTRA_POSIX_NETWORK)/build/m68k/libastranetwork.a \
	$(ASTRA_POSIX_RUNTIME)/build/m68k/libastrart.a \
	$(PICOLIBC)/lib/libc.a $(PICOLIBC)/lib/libm.a -lgcc \
	-Wl,--end-group
ASTRA_POSIX_STATIC_CXX_LIBS := -Wl,--start-group -lstdc++ \
	$(ASTRA_POSIX_LAYER)/build/m68k/libastraposix.a \
	$(ASTRA_POSIX_STREAMS)/build/m68k/libastrastreams.a \
	$(ASTRA_POSIX_VFS)/build/m68k/libastravfs.a \
	$(ASTRA_POSIX_NETWORK)/build/m68k/libastranetwork.a \
	$(ASTRA_POSIX_RUNTIME)/build/m68k/libastrart.a \
	$(PICOLIBC)/lib/libc.a $(PICOLIBC)/lib/libm.a -lgcc \
	-Wl,--end-group

include $(ASTRA_POSIX_NDK)/make/astra-posix.mk
