include ../../../mk/m68k-cross.mk
include ../../../mk/astra-shared-library.mk

CC := $(CROSS)gcc
READELF := $(CROSS)readelf
HOST_CC ?= cc
COMPILER := ../compiler
RUNTIME := ../runtime
VFS := ../vfs
NDK := ../../../ndk
CPPFLAGS := -I$(NDK)/include -I../../include
FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_SHARED_PIC_FLAGS)
LIBRARY := build/m68k/libraries/dos.library.1
OBJECT := build/m68k/dos_library.o
MAP := dos.library.exports
FILESYSTEM_LIBRARY := $(VFS)/build/m68k/libraries/filesystem.library.15
RUNTIME_LIBRARY := $(RUNTIME)/build/m68k/libraries/runtime.library.1
COMPILER_LIBRARY := $(COMPILER)/build/m68k/libraries/compiler.library.2

all: $(LIBRARY) library-contract
library: $(LIBRARY)

$(OBJECT): src/dos_library.c $(NDK)/include/astra/dos_library.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

shared-libraries-ready:
	$(MAKE) -C $(VFS) library
	$(MAKE) -C $(RUNTIME) library
	$(MAKE) -C $(COMPILER) library

$(LIBRARY): $(OBJECT) $(MAP) $(ASTRA_SHARED_LINK_INPUTS) | shared-libraries-ready
	@mkdir -p $(@D)
	$(CC) $(TARGET_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,dos.library.1 -Wl,--version-script=$(MAP) \
		-o $@ $(OBJECT) \
		-L$(dir $(FILESYSTEM_LIBRARY)) -L$(dir $(RUNTIME_LIBRARY)) \
		-L$(dir $(COMPILER_LIBRARY)) \
		-Wl,--no-as-needed -Wl,-l:filesystem.library.15 \
		-Wl,-l:runtime.library.1 -Wl,-l:compiler.library.2 \
		$(ASTRA_SHARED_LINK_END)

library-contract: $(LIBRARY)
	@python3 ../../../tools/check_shared_library_layout.py $(LIBRARY)
	@python3 ../../../tools/check_shared_symbol_ownership.py --readelf $(READELF) \
		$(LIBRARY) $(FILESYSTEM_LIBRARY) $(RUNTIME_LIBRARY) $(COMPILER_LIBRARY)
	@python3 ../../../tools/check_shared_symbol_ownership.py --readelf $(READELF) \
		--all-libraries $(LIBRARY) $(FILESYSTEM_LIBRARY)
	@python3 ../../../tools/check_library_exports.py $(MAP) \
		$(NDK)/include/astra/dos_library.h
	@python3 ../../../tools/check_elf_exports.py --readelf $(READELF) $(MAP) $(LIBRARY)
	@python3 ../../../tools/library_info.py $(LIBRARY) | \
		grep -q '^dos.library.1 1.0.0 ABI 1.0$$'
	@$(READELF) -VW $(LIBRARY) | grep -q ASTRA_DOS_1.0
	$(call ASTRA_CHECK_DSO,$(LIBRARY))

build/host/test_dos_disk: src/dos_library.c tests/test_dos_disk.c \
		$(NDK)/include/astra/dos_library.h
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(FLAGS) -O2 $< tests/test_dos_disk.c -o $@

test: build/host/test_dos_disk
	./build/host/test_dos_disk

sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(FLAGS) -O2 -fsanitize=address,undefined \
		-fno-omit-frame-pointer src/dos_library.c tests/test_dos_disk.c \
		-o build/host/test_dos_disk_sanitize
	./build/host/test_dos_disk_sanitize

analyze:
	@mkdir -p build/analyzer
	$(HOST_CC) $(CPPFLAGS) $(FLAGS) -O2 -fanalyzer -c src/dos_library.c \
		-o build/analyzer/dos_library.o

clean:
	rm -rf build

-include $(OBJECT:.o=.d)
.PHONY: all library library-contract shared-libraries-ready test sanitize analyze clean
