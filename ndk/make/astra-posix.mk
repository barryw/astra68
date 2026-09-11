# Relocatable MC68040 compile and link contract for POSIX programs.
include $(abspath $(dir $(lastword $(MAKEFILE_LIST))))/astra-native.mk

ASTRA_POSIX_CPPFLAGS := $(ASTRA_CPPFLAGS) -I$(ASTRA_NDK_ROOT)/include/posix
ASTRA_POSIX_CFLAGS := $(ASTRA_CFLAGS)
ASTRA_POSIX_CXXFLAGS := $(ASTRA_CXXFLAGS)
ASTRA_POSIX_LDFLAGS := $(ASTRA_LDFLAGS) \
	-Wl,--undefined=astra_posix_entry_contract
ASTRA_POSIX_CRT0 := $(ASTRA_HOSTED_CRT0)
ASTRA_POSIX_PROGRAM_SOURCE := \
	$(ASTRA_NDK_ROOT)/src/astra-posix-program.c
ASTRA_CXX_CRTBEGIN := $(shell $(ASTRA_CXX) -print-file-name=crtbegin.o)
ASTRA_CXX_CRTEND := $(shell $(ASTRA_CXX) -print-file-name=crtend.o)
ASTRA_POSIX_LIBS := -Wl,--start-group \
	$(ASTRA_LIB_DIR)/libastraposix.a \
	$(ASTRA_LIB_DIR)/libastrastreams.a \
	$(ASTRA_LIB_DIR)/libastravfs.a \
	$(ASTRA_LIB_DIR)/libastra.a \
	$(ASTRA_LIB_DIR)/libastrart.a -lc -lm -lgcc -Wl,--end-group
ASTRA_POSIX_CXX_LIBS := -Wl,--start-group -lstdc++ \
	$(ASTRA_LIB_DIR)/libastraposix.a \
	$(ASTRA_LIB_DIR)/libastrastreams.a \
	$(ASTRA_LIB_DIR)/libastravfs.a \
	$(ASTRA_LIB_DIR)/libastra.a \
	$(ASTRA_LIB_DIR)/libastrart.a -lc -lm -lgcc -Wl,--end-group
