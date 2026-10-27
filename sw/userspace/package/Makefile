include ../../../mk/m68k-cross.mk
include ../../../mk/astra-shared-library.mk

CC := $(CROSS)gcc
READELF := $(CROSS)readelf
NDK := ../../../ndk
ZSTD := ../../../third_party/zstd
COMMON := ../../common
FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
	$(ASTRA_SHARED_PIC_FLAGS)
CPPFLAGS := -I$(NDK)/include -I../../include -I$(ZSTD)
LIBRARY := build/m68k/libraries/package.library.1
OBJECTS := build/m68k/package.o build/m68k/sha256.o build/m68k/zstd.o
MAP := package.library.exports
SYSTEM := ../system/build/m68k/libraries/system.library.4
FILESYSTEM := ../vfs/build/m68k/libraries/filesystem.library.15
RUNTIME := ../runtime/build/m68k/libraries/runtime.library.1
COMPILER := ../compiler/build/m68k/libraries/compiler.library.2

all: $(LIBRARY) library-contract
library: $(LIBRARY)

build/m68k/package.o: src/package.c $(NDK)/include/astra/package_library.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) -MMD -MP -c $< -o $@

build/m68k/sha256.o: $(COMMON)/sha256.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) -MMD -MP -c $< -o $@

build/m68k/zstd.o: $(ZSTD)/zstd.c $(ZSTD)/zstd.h $(ZSTD)/zstd_errors.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(filter-out -Werror,$(FLAGS)) -w \
		-DZSTD_DISABLE_ASM=1 -MMD -MP -c $< -o $@

shared-libraries-ready:
	$(MAKE) -C ../system library
	$(MAKE) -C ../vfs library
	$(MAKE) -C ../runtime library
	$(MAKE) -C ../compiler library

$(LIBRARY): $(OBJECTS) $(MAP) $(ASTRA_SHARED_LINK_INPUTS) | shared-libraries-ready
	@mkdir -p $(@D)
	$(CC) $(FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,package.library.1 -Wl,--version-script=$(MAP) \
		-o $@ $(OBJECTS) \
		-L$(dir $(SYSTEM)) -L$(dir $(FILESYSTEM)) \
		-L$(dir $(RUNTIME)) -L$(dir $(COMPILER)) \
		-Wl,--no-as-needed -Wl,-l:system.library.4 \
		-Wl,-l:filesystem.library.15 -Wl,-l:runtime.library.1 \
		-Wl,-l:compiler.library.2 $(ASTRA_SHARED_LINK_END)

library-contract: $(LIBRARY)
	@python3 ../../../tools/check_shared_library_layout.py $(LIBRARY)
	@python3 ../../../tools/check_shared_symbol_ownership.py --readelf $(READELF) \
		$(LIBRARY) $(SYSTEM) $(FILESYSTEM) $(RUNTIME) $(COMPILER)
	@python3 ../../../tools/check_library_exports.py $(MAP) \
		$(NDK)/include/astra/package_library.h
	@python3 ../../../tools/check_elf_exports.py --readelf $(READELF) \
		$(MAP) $(LIBRARY)
	@python3 ../../../tools/library_info.py $(LIBRARY) | \
		grep -q '^package.library.1 1.0.0 ABI 1.0$$'
	$(call ASTRA_CHECK_DSO,$(LIBRARY))

build/host/test_paths: tests/test_paths.c src/package.c
	@mkdir -p $(@D)
	cc $(CPPFLAGS) -std=c11 -Wall -Wextra -Wno-unused-function \
		-ffunction-sections -fdata-sections -Wl,--gc-sections \
		tests/test_paths.c ../../common/utf8.c -o $@

test: library-contract build/host/test_paths
	./build/host/test_paths

clean:
	rm -rf build

-include $(OBJECTS:.o=.d)
.PHONY: all library library-contract shared-libraries-ready test clean
