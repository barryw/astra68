# Relocatable MC68040 compile and dynamic-link contract for Astra programs.
ASTRA_NDK_ROOT ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
ASTRA_CROSS ?= m68k-astra-
ASTRA_CC ?= $(ASTRA_CROSS)gcc
ASTRA_CXX ?= $(ASTRA_CROSS)g++
ASTRA_READELF ?= $(ASTRA_CROSS)readelf

ASTRA_CPPFLAGS ?= -I$(ASTRA_NDK_ROOT)/include
ASTRA_ABI_FLAGS ?= -m68040 -msoft-float -ffixed-a4 -D__astra__=1
ASTRA_CFLAGS ?= $(ASTRA_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC -ftls-model=initial-exec
ASTRA_CXXFLAGS ?= $(ASTRA_ABI_FLAGS) -Os \
	-ffunction-sections -fdata-sections -fPIC -ftls-model=initial-exec

ASTRA_LIB_DIR ?= $(ASTRA_NDK_ROOT)/lib/m68040
ASTRA_LINKER_SCRIPT ?= $(ASTRA_LIB_DIR)/astra_user.ld
ASTRA_STATIC_LINKER_SCRIPT ?= $(ASTRA_LIB_DIR)/astra_static_user.ld
ASTRA_LIBRARY_SEARCH_FLAGS ?= -L$(ASTRA_LIB_DIR) \
	-Wl,-rpath-link,$(ASTRA_LIB_DIR)
ASTRA_LDFLAGS ?= -nostdlib -pie -Wl,-Bdynamic -Wl,--no-as-needed \
	-Wl,-z,now -Wl,-z,relro -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections \
	-T $(ASTRA_LINKER_SCRIPT) $(ASTRA_LIBRARY_SEARCH_FLAGS)
ASTRA_CRT0 ?= $(ASTRA_LIB_DIR)/crt0-dynamic.o
ASTRA_NATIVE_LIBS ?= -Wl,-l:runtime.library.1 \
	-Wl,-l:compiler.library.1
ASTRA_DYNAMIC_EXECUTABLE_CHECK ?= \
	$(ASTRA_NDK_ROOT)/tools/check_dynamic_executable.py

# Fully self-contained images are an explicit opt-in. The normal NDK variables
# above always produce dynamically linked programs. Astra's Supervisor and
# Storage images use the static mode because they bootstrap the loader and
# system volume; developers retain it whenever self-containment is intentional.
ASTRA_STATIC_LDFLAGS ?= -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections \
	-Wl,-L$(dir $(ASTRA_STATIC_LINKER_SCRIPT)) \
	-T $(ASTRA_STATIC_LINKER_SCRIPT)
ASTRA_STATIC_CRT0 ?= $(ASTRA_LIB_DIR)/crt0.o
ASTRA_STATIC_HOSTED_CRT0 ?= $(ASTRA_LIB_DIR)/crt0-hosted.o
ASTRA_STATIC_NATIVE_LIBS ?= -Wl,--start-group \
	$(ASTRA_LIB_DIR)/libastragraphics.a \
	$(ASTRA_LIB_DIR)/libastrastreams.a \
	$(ASTRA_LIB_DIR)/libastravfs.a \
	$(ASTRA_LIB_DIR)/libastranetwork.a \
	$(ASTRA_LIB_DIR)/libastraevents.a \
	$(ASTRA_LIB_DIR)/libastra.a \
	$(ASTRA_LIB_DIR)/libastrart.a -lgcc -Wl,--end-group
