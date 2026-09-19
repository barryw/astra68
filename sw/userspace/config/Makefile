include ../../../mk/m68k-cross.mk
include ../../../mk/astra-shared-library.mk

CC := $(CROSS)gcc
READELF := $(CROSS)readelf
HOST_CC ?= cc
ANALYZER_CC ?= gcc
RUNTIME := ../runtime
COMPILER := ../compiler
VFS := ../vfs
NDK := ../../../ndk
CPPFLAGS := -Iinclude -I$(NDK)/include -I../../include \
	-I$(RUNTIME)/include -I$(VFS)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_SHARED_PIC_FLAGS)
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

LIBRARY := build/m68k/libraries/config.library.1
LIBRARY_OBJECTS := build/m68k/config_library.o build/m68k/config_document.o
LIBRARY_MAP := config.library.exports
SYSTEM_LIBRARY := $(NDK)/build/m68k/libraries/system.library.1
RUNTIME_LIBRARY := $(RUNTIME)/build/m68k/libraries/runtime.library.1
COMPILER_LIBRARY := $(COMPILER)/build/m68k/libraries/compiler.library.1
FILESYSTEM_LIBRARY := $(VFS)/build/m68k/libraries/filesystem.library.2
SYMBOL_OWNERSHIP_CHECK := ../../../tools/check_shared_symbol_ownership.py
HOST_TEST := build/host/test_config_document

all: $(LIBRARY) library-contract

library: $(LIBRARY)

build/m68k/config_library.o: src/config_library.c \
		$(NDK)/include/astra/config_library.h include/astra/config_document.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/m68k/config_document.o: src/config_document.c \
		include/astra/config_document.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

shared-libraries-ready:
	$(MAKE) -C $(NDK) library
	$(MAKE) -C $(VFS) library
	$(MAKE) -C $(COMPILER) library

$(LIBRARY): $(LIBRARY_OBJECTS) $(LIBRARY_MAP) $(ASTRA_SHARED_LINK_INPUTS) \
		| shared-libraries-ready
	@mkdir -p $(@D)
	$(CC) $(TARGET_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,config.library.1 \
		-Wl,--version-script=$(LIBRARY_MAP) -o $@ $(LIBRARY_OBJECTS) \
		-L$(dir $(SYSTEM_LIBRARY)) -L$(dir $(RUNTIME_LIBRARY)) \
		-L$(dir $(COMPILER_LIBRARY)) -L$(dir $(FILESYSTEM_LIBRARY)) \
		-Wl,--no-as-needed -Wl,-l:system.library.1 \
		-Wl,-l:runtime.library.1 -Wl,-l:compiler.library.1 \
		-Wl,-l:filesystem.library.2

library-contract: $(LIBRARY)
	@python3 ../../../tools/check_shared_library_layout.py $(LIBRARY)
	@python3 $(SYMBOL_OWNERSHIP_CHECK) --readelf $(READELF) \
		$(LIBRARY) $(SYSTEM_LIBRARY) $(RUNTIME_LIBRARY) \
		$(COMPILER_LIBRARY) $(FILESYSTEM_LIBRARY)
	@python3 ../../../tools/check_library_exports.py $(LIBRARY_MAP) \
		$(NDK)/include/astra/config_library.h
	@python3 ../../../tools/check_elf_exports.py --readelf $(READELF) \
		$(LIBRARY_MAP) $(LIBRARY)
	@python3 ../../../tools/library_info.py $(LIBRARY) | \
		grep -q '^config.library.1 1.0.0 ABI 1.0$$'
	@test "$$($(READELF) -dW $< | grep -c 'NEEDED.*system.library.1')" = 1
	@test "$$($(READELF) -dW $< | grep -c 'NEEDED.*runtime.library.1')" = 1
	@test "$$($(READELF) -dW $< | grep -c 'NEEDED.*compiler.library.1')" = 1
	@test "$$($(READELF) -dW $< | grep -c 'NEEDED.*filesystem.library.2')" = 1
	@test "$$($(READELF) -dW $< | awk '/NEEDED/{n++} END{print n+0}')" = 4
	@$(READELF) -VW $< | grep -q 'ASTRA_CONFIG_1.0'
	$(call ASTRA_CHECK_DSO,$(LIBRARY))

$(HOST_TEST): src/config_document.c $(NDK)/src/config_scope.c \
		$(NDK)/src/manifest.c \
		$(NDK)/src/utf8.c \
		tests/test_config_document.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O2 $^ -o $@

test: $(HOST_TEST)
	./$(HOST_TEST)

sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O2 $(SANITIZER_FLAGS) \
		src/config_document.c $(NDK)/src/config_scope.c \
		$(NDK)/src/manifest.c $(NDK)/src/utf8.c \
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

-include $(LIBRARY_OBJECTS:.o=.d)

.PHONY: all library test sanitize analyze clean library-contract shared-libraries-ready
