include ../../../mk/m68k-cross.mk
include ../../../mk/astra-shared-library.mk

CC := $(CROSS)gcc
AR := $(CROSS)gcc-ar
RANLIB := $(CROSS)gcc-ranlib
READELF := $(CROSS)readelf
RUNTIME := ../runtime
STREAMS := ../streams
VFS := ../vfs
POSIX := ../posix
NETWORK := ../network
COMPILER := ../compiler
NDK := ../../../ndk
PICOLIBC ?= $(HOME)/picolibc-astra
NCURSES_SOURCE ?= $(HOME)/ncurses-astra-6.6
NCURSES_PREFIX ?= $(HOME)/ncurses-astra
NCURSES_BUILD ?= $(HOME)/ncurses-astra-build

ARCHIVE := $(NCURSES_PREFIX)/lib/libtinfow.a
ARCHIVE_BUILD := $(NCURSES_BUILD)/lib/libtinfow.a
NCURSES_INPUT_STAMP := build/m68k/ncurses.inputs
LIBRARY := build/m68k/libraries/terminfo.library.6
LIBRARY_IDENTITY := build/m68k/terminfo_library.o
LIBRARY_MAP := build/m68k/terminfo.library.exports
LIBRARY_UNDEFINED := build/m68k/terminfo.library.undefined
LIBRARY_CONTRACT_STAMP := build/m68k/terminfo.library.inputs
EXTRA_EXPORTS := terminfo.extra.exports
CONFIG_PROGRAM := build/m68k/configure-program.o
CONTRACT_TOOL := ../../../tools/generate_archive_contract.py
SYMBOL_OWNERSHIP_CHECK := ../../../tools/check_shared_symbol_ownership.py

RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart.a
STREAMS_LIB := $(STREAMS)/build/m68k/libastrastreams.a
VFS_LIB := $(VFS)/build/m68k/libastravfs.a
POSIX_LIB := $(POSIX)/build/m68k/libastraposix.a
NETWORK_LIB := $(NETWORK)/build/m68k/libastranetwork.a
PICOLIBC_LIB := $(PICOLIBC)/lib/libc.a
PICOLIBC_MATH := $(PICOLIBC)/lib/libm.a
RUNTIME_CRT0 := $(RUNTIME)/build/m68k/crt0-hosted.o
LIBC_LIBRARY := $(POSIX)/build/m68k/libraries/libc.library.1
RUNTIME_LIBRARY := $(RUNTIME)/build/m68k/libraries/runtime.library.1
COMPILER_LIBRARY := $(COMPILER)/build/m68k/libraries/compiler.library.1

PROJECT_CPPFLAGS := -I../../include -I$(RUNTIME)/include \
	-I$(STREAMS)/include -I$(VFS)/include -I$(POSIX)/include \
	-I$(NDK)/include
CPPFLAGS := $(PROJECT_CPPFLAGS) -isystem $(PICOLIBC)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -O3 -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
NCURSES_CFLAGS := $(TARGET_FLAGS) $(ASTRA_SHARED_PIC_FLAGS)
NCURSES_CPPFLAGS := -nostdinc $(addprefix -I,$(abspath ../../include \
	$(RUNTIME)/include $(STREAMS)/include $(VFS)/include $(POSIX)/include \
	$(NDK)/include)) -I$(NCURSES_PREFIX)/include \
	-I$(NCURSES_PREFIX)/include/ncursesw -isystem $(PICOLIBC)/include \
	-isystem $(shell $(CC) -print-file-name=include) \
	-D_GNU_SOURCE -D_DEFAULT_SOURCE -DNDEBUG
CONFIG_LDFLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections \
	-Wl,--undefined=astra_posix_entry_contract \
	-Wl,-L$(abspath $(RUNTIME)) \
	-T $(abspath $(RUNTIME)/astra_static_user.ld) \
	$(abspath $(RUNTIME_CRT0)) $(abspath $(CONFIG_PROGRAM))
CONFIG_LIBS := -Wl,--start-group $(abspath $(VFS_LIB)) \
	$(abspath $(STREAMS_LIB)) $(abspath $(POSIX_LIB)) \
	$(abspath $(NETWORK_LIB)) \
	$(abspath $(RUNTIME_LIB)) $(abspath $(PICOLIBC_LIB)) \
	$(abspath $(PICOLIBC_MATH)) -lgcc -Wl,--end-group

all: library-contract

library: $(LIBRARY)

$(CONFIG_PROGRAM): $(POSIX)/kit/program.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP \
		-DASTRA_POSIX_PROGRAM_NAME='"ncurses-configure"' \
		-DASTRA_POSIX_PROGRAM_MAJOR=6 -DASTRA_POSIX_PROGRAM_MINOR=6 \
		-DASTRA_POSIX_PROGRAM_PATCH=0 \
		-DASTRA_POSIX_PROGRAM_AUTHOR='"ncurses contributors"' \
		-DASTRA_POSIX_PROGRAM_COPYRIGHT='"Copyright ncurses contributors"' \
		-c $< -o $@

dependencies-ready:
	$(MAKE) -C $(RUNTIME) build/m68k/libastrart.a \
		build/m68k/crt0-hosted.o
	$(MAKE) -C $(STREAMS) build/m68k/libastrastreams.a
	$(MAKE) -C $(VFS) build/m68k/libastravfs.a
	$(MAKE) -C $(NETWORK) build/m68k/libastranetwork.a
	$(MAKE) -C $(POSIX) build/m68k/libastraposix.a
	$(MAKE) -C $(POSIX) library

$(NCURSES_INPUT_STAMP): $(PICOLIBC)/include/sys/termios.h \
		$(PICOLIBC)/include/sys/ioctl.h $(POSIX_LIB) $(RUNTIME_LIB) \
		$(STREAMS_LIB) $(VFS_LIB) $(NETWORK_LIB) $(RUNTIME_CRT0) \
		$(CONFIG_PROGRAM) \
		$(ASTRA_TOOLCHAIN_STAMP) Makefile $(NCURSES_SOURCE)/configure \
		| dependencies-ready
	$(call ASTRA_UPDATE_CONTENT_STAMP,$(PICOLIBC)/include/sys/termios.h \
		$(PICOLIBC)/include/sys/ioctl.h $(POSIX_LIB) $(RUNTIME_LIB) \
		$(STREAMS_LIB) $(VFS_LIB) $(NETWORK_LIB) $(RUNTIME_CRT0) \
		$(CONFIG_PROGRAM) \
		$(ASTRA_TOOLCHAIN_STAMP) Makefile $(NCURSES_SOURCE)/configure)

$(ARCHIVE): $(NCURSES_INPUT_STAMP)
	rm -rf $(NCURSES_BUILD)
	mkdir -p $(NCURSES_BUILD)
	cd $(NCURSES_BUILD) && \
		CC='$(CC)' AR='$(AR)' RANLIB='$(RANLIB)' \
		CFLAGS='$(NCURSES_CFLAGS)' CPPFLAGS='$(NCURSES_CPPFLAGS)' \
		LDFLAGS='$(CONFIG_LDFLAGS)' LIBS='$(CONFIG_LIBS)' \
		$(NCURSES_SOURCE)/configure \
		--build=x86_64-pc-linux-gnu --host=m68k-unknown-elf \
		--prefix=$(NCURSES_PREFIX) \
		--with-default-terminfo-dir=/LIBS/terminfo \
		--with-terminfo-dirs=/LIBS/terminfo --with-termlib \
		--enable-widec --without-shared --with-normal --without-cxx \
		--without-cxx-binding --without-ada --without-progs \
		--without-tests --without-manpages --without-debug \
		--without-profile --disable-db-install --disable-home-terminfo
	$(MAKE) -C $(NCURSES_BUILD)/include install.libs
	$(MAKE) -C $(NCURSES_BUILD)/ncurses ../lib/libtinfow.a
	mkdir -p $(dir $(ARCHIVE))
	install -m 644 $(ARCHIVE_BUILD) $(ARCHIVE)

$(LIBRARY_IDENTITY): src/terminfo_library.c
	@mkdir -p $(@D)
	$(CC) -I../../include $(COMMON_FLAGS) $(TARGET_FLAGS) \
		$(ASTRA_SHARED_PIC_FLAGS) -MMD -MP -c $< -o $@

$(LIBRARY_CONTRACT_STAMP): $(ARCHIVE) $(EXTRA_EXPORTS) $(CONTRACT_TOOL) \
		$(LIBC_LIBRARY) $(RUNTIME_LIBRARY) $(COMPILER_LIBRARY) \
		$(ASTRA_TOOLCHAIN_STAMP) | dependencies-ready
	$(call ASTRA_UPDATE_CONTENT_STAMP,$(ARCHIVE) $(EXTRA_EXPORTS) \
		$(CONTRACT_TOOL) $(LIBC_LIBRARY) $(RUNTIME_LIBRARY) \
		$(COMPILER_LIBRARY))

$(LIBRARY_MAP) $(LIBRARY_UNDEFINED) &: $(LIBRARY_CONTRACT_STAMP)
	@mkdir -p build/m68k
	python3 $(CONTRACT_TOOL) --readelf $(READELF) \
		--version ASTRA_TERMINFO_6_0 --exports-out $(LIBRARY_MAP) \
		--undefined-out $(LIBRARY_UNDEFINED) --extra $(EXTRA_EXPORTS) \
		--dependency $(LIBC_LIBRARY) --dependency $(RUNTIME_LIBRARY) \
		--dependency $(COMPILER_LIBRARY) $(ARCHIVE)
	$(call ASTRA_MARK_CONTENT_OUTPUTS,$(LIBRARY_CONTRACT_STAMP),\
		$(LIBRARY_MAP) $(LIBRARY_UNDEFINED))

$(LIBRARY): $(LIBRARY_IDENTITY) $(LIBRARY_MAP) $(LIBRARY_UNDEFINED) \
		$(ARCHIVE) $(ASTRA_SHARED_LINK_INPUTS) | dependencies-ready
	@mkdir -p $(@D)
	$(CC) $(ASTRA_TARGET_ABI_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,terminfo.library.6 \
		-Wl,--version-script=$(LIBRARY_MAP) \
		-Wl,@$(LIBRARY_UNDEFINED) -o $@ $(LIBRARY_IDENTITY) \
		-L$(dir $(LIBC_LIBRARY)) -L$(dir $(RUNTIME_LIBRARY)) \
		-L$(dir $(COMPILER_LIBRARY)) \
		-Wl,-rpath-link,$(abspath $(STREAMS)/build/m68k/libraries) \
		-Wl,-rpath-link,$(abspath $(VFS)/build/m68k/libraries) \
		-Wl,-rpath-link,$(abspath $(NDK)/build/m68k/libraries) \
		-Wl,--no-as-needed -Wl,-l:libc.library.1 \
		-Wl,-l:runtime.library.1 -Wl,-l:compiler.library.1 \
		-Wl,--start-group $(ARCHIVE) -Wl,--end-group

library-contract: $(LIBRARY)
	@python3 ../../../tools/check_shared_library_layout.py $(LIBRARY)
	@python3 $(SYMBOL_OWNERSHIP_CHECK) --readelf $(READELF) \
		$(LIBRARY) $(LIBC_LIBRARY) $(RUNTIME_LIBRARY) $(COMPILER_LIBRARY)
	@python3 ../../../tools/check_elf_exports.py --readelf $(READELF) \
		$(LIBRARY_MAP) $(LIBRARY)
	@python3 ../../../tools/library_info.py $(LIBRARY) | \
		grep -q '^terminfo.library.6 6.6.0 ABI 6.0$$'
	@$(READELF) -dW $(LIBRARY) | grep -Fq 'Library soname: [terminfo.library.6]'
	@test "$$($(READELF) -dW $(LIBRARY) | grep -Fc 'Shared library: [libc.library.1]')" = 1
	@test "$$($(READELF) -dW $(LIBRARY) | grep -Fc 'Shared library: [runtime.library.1]')" = 1
	@test "$$($(READELF) -dW $(LIBRARY) | grep -Fc 'Shared library: [compiler.library.1]')" = 1
	@! $(READELF) -dW $(LIBRARY) | grep -q 'TEXTREL'
	@$(READELF) -lW $(LIBRARY) | grep -q ' GNU_RELRO '
	@echo "Astra terminfo shared library: PASS"

test sanitize analyze:
	@:

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)

.PHONY: all library library-contract dependencies-ready test sanitize analyze clean
