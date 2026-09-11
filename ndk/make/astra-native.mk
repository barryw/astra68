# Relocatable MC68040 compile and link contract for Astra-native programs.
ASTRA_NDK_ROOT ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
ASTRA_CROSS ?= m68k-astra-
ASTRA_CC ?= $(ASTRA_CROSS)gcc
ASTRA_CXX ?= $(ASTRA_CROSS)g++

ASTRA_CPPFLAGS := -I$(ASTRA_NDK_ROOT)/include
ASTRA_ABI_FLAGS := -m68040 -msoft-float -ffixed-a4 -D__astra__=1
ASTRA_CFLAGS := $(ASTRA_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections
ASTRA_CXXFLAGS := $(ASTRA_ABI_FLAGS) -Os \
	-ffunction-sections -fdata-sections
ASTRA_LDFLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections \
	-T $(ASTRA_NDK_ROOT)/lib/m68040/astra_user.ld
ASTRA_CRT0 := $(ASTRA_NDK_ROOT)/lib/m68040/crt0.o
ASTRA_HOSTED_CRT0 := $(ASTRA_NDK_ROOT)/lib/m68040/crt0-hosted.o

ASTRA_LIB_DIR := $(ASTRA_NDK_ROOT)/lib/m68040
ASTRA_NATIVE_LIBS := -Wl,--start-group \
	$(ASTRA_LIB_DIR)/libastragraphics.a \
	$(ASTRA_LIB_DIR)/libastrastreams.a \
	$(ASTRA_LIB_DIR)/libastravfs.a \
	$(ASTRA_LIB_DIR)/libastranetwork.a \
	$(ASTRA_LIB_DIR)/libastraevents.a \
	$(ASTRA_LIB_DIR)/libastra.a \
	$(ASTRA_LIB_DIR)/libastrart.a -lgcc -Wl,--end-group
