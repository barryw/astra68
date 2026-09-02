include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
READELF := $(CROSS)readelf
HOST_CC ?= cc
ANALYZER_CC ?= gcc
RUNTIME := ../runtime
VFS := ../vfs
NDK := ../../../ndk
CPPFLAGS := -Iinclude -I$(NDK)/include -I../../include \
	-I$(RUNTIME)/include -I$(VFS)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC -fvisibility=hidden
LINK_FLAGS := -nostdlib -shared -Wl,-Bsymbolic -Wl,--no-undefined \
	-Wl,--build-id=none -Wl,--gc-sections -Wl,--hash-style=sysv \
	-Wl,-z,max-page-size=0x1000 -T $(RUNTIME)/astra_library.ld
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

LIBRARY := build/m68k/libraries/config.library
LIBRARY_OBJECTS := build/m68k/config_library.o \
	build/m68k/config_document.o build/m68k/vfs_client.o \
	build/m68k/vfs_port_transport.o
NDK_PIC_LIB := $(NDK)/build/m68k/libastra-pic.a
RUNTIME_PIC_LIB := $(RUNTIME)/build/m68k/libastrart-pic.a
HOST_TEST := build/host/test_config_document

all: $(LIBRARY) library-contract

build/m68k/config_library.o: src/config_library.c \
		$(NDK)/include/astra/config_library.h include/astra/config_document.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/m68k/config_document.o: src/config_document.c \
		include/astra/config_document.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/m68k/vfs_client.o: $(VFS)/src/vfs_client.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/m68k/vfs_port_transport.o: $(VFS)/src/vfs_port_transport.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -DASTRA_VFS_EMBEDDED_THREAD_STATE=1 \
		$(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

$(NDK_PIC_LIB): FORCE
	$(MAKE) -C $(NDK) pic

$(RUNTIME_PIC_LIB): FORCE
	$(MAKE) -C $(RUNTIME) pic

$(LIBRARY): $(LIBRARY_OBJECTS) $(NDK_PIC_LIB) $(RUNTIME_PIC_LIB) \
		$(RUNTIME)/astra_library.ld
	@mkdir -p $(@D)
	$(CC) $(TARGET_FLAGS) $(LINK_FLAGS) -Wl,-soname,config.library.1 \
		-o $@ $(LIBRARY_OBJECTS) $(NDK_PIC_LIB) $(RUNTIME_PIC_LIB) -lgcc

library-contract: $(LIBRARY)
	@python3 ../../../tools/library_info.py $(LIBRARY) | \
		grep -q '^config.library 1.0.0 ABI 1.0$$'
	$(call ASTRA_CHECK_DSO,$(LIBRARY))

$(HOST_TEST): src/config_document.c $(NDK)/src/manifest.c \
		tests/test_config_document.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O2 $^ -o $@

test: $(HOST_TEST)
	./$(HOST_TEST)

sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O2 $(SANITIZER_FLAGS) \
		src/config_document.c $(NDK)/src/manifest.c \
		tests/test_config_document.c -o build/host/test_config_document_sanitize
	./build/host/test_config_document_sanitize

analyze:
	@mkdir -p build/analyzer
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O2 -fanalyzer \
		-c src/config_library.c -o build/analyzer/config_library.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O2 -fanalyzer \
		-c src/config_document.c -o build/analyzer/config_document.o

clean:
	rm -rf build

FORCE:

-include $(LIBRARY_OBJECTS:.o=.d)

.PHONY: all test sanitize analyze clean library-contract FORCE
