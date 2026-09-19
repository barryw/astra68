# Relocatable MC68040 compile and dynamic-link contract for POSIX programs.
include $(abspath $(dir $(lastword $(MAKEFILE_LIST))))/astra-native.mk

# Consumers rebuild when an included compile/link contract changes, not only
# when their top-level Makefile or source changes.
ASTRA_POSIX_POLICY_INPUTS ?= $(filter %.mk,$(MAKEFILE_LIST))

ASTRA_POSIX_CPPFLAGS ?= $(ASTRA_CPPFLAGS) -I$(ASTRA_NDK_ROOT)/include/posix
ASTRA_POSIX_CXXCPPFLAGS ?= $(ASTRA_POSIX_CPPFLAGS)
ASTRA_POSIX_CFLAGS ?= $(ASTRA_CFLAGS)
ASTRA_POSIX_CXXFLAGS ?= $(ASTRA_CXXFLAGS)
ASTRA_POSIX_LDFLAGS ?= $(ASTRA_LDFLAGS) \
	-Wl,--undefined=astra_posix_entry_contract
ASTRA_POSIX_CRT0 ?= $(ASTRA_CRT0)
ASTRA_POSIX_PROGRAM_SOURCE ?= \
	$(ASTRA_NDK_ROOT)/src/astra-posix-program.c
ASTRA_CXX_CRTBEGIN ?= $(shell $(ASTRA_CXX) -print-file-name=crtbeginS.o)
ASTRA_CXX_CRTEND ?= $(shell $(ASTRA_CXX) -print-file-name=crtendS.o)
ASTRA_STATIC_CXX_CRTBEGIN ?= \
	$(shell $(ASTRA_CXX) -print-file-name=crtbegin.o)
ASTRA_STATIC_CXX_CRTEND ?= \
	$(shell $(ASTRA_CXX) -print-file-name=crtend.o)
ASTRA_POSIX_LIBS ?= -Wl,-l:libc.library.1 \
	-Wl,-l:runtime.library.1 -Wl,-l:compiler.library.1
ASTRA_POSIX_CXX_LIBS ?= -Wl,-l:cxx.library.1 \
	-Wl,-l:libc.library.1 -Wl,-l:runtime.library.1 \
	-Wl,-l:unwind.library.1 -Wl,-l:compiler.library.1

# Static POSIX images are an explicit self-contained mode rather than the
# default. They must opt in at the link call site; Astra itself uses that mode
# only where the loader or backing filesystem may be unavailable.
ASTRA_POSIX_STATIC_LDFLAGS ?= $(ASTRA_STATIC_LDFLAGS) \
	-Wl,--undefined=astra_posix_entry_contract
ASTRA_POSIX_STATIC_CRT0 ?= $(ASTRA_STATIC_HOSTED_CRT0)
ASTRA_POSIX_STATIC_LIBS ?= -Wl,--start-group \
	$(ASTRA_LIB_DIR)/libastraposix.a \
	$(ASTRA_LIB_DIR)/libastrastreams.a \
	$(ASTRA_LIB_DIR)/libastravfs.a \
	$(ASTRA_LIB_DIR)/libastranetwork.a \
	$(ASTRA_LIB_DIR)/libastra.a \
	$(ASTRA_LIB_DIR)/libastrart.a -lc -lm -lgcc -Wl,--end-group
ASTRA_POSIX_STATIC_CXX_LIBS ?= -Wl,--start-group -lstdc++ \
	$(ASTRA_LIB_DIR)/libastraposix.a \
	$(ASTRA_LIB_DIR)/libastrastreams.a \
	$(ASTRA_LIB_DIR)/libastravfs.a \
	$(ASTRA_LIB_DIR)/libastranetwork.a \
	$(ASTRA_LIB_DIR)/libastra.a \
	$(ASTRA_LIB_DIR)/libastrart.a -lc -lm -lgcc -Wl,--end-group
