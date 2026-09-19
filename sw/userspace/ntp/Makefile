include ../../../mk/m68k-cross.mk
include ../../../mk/astra-shared-library.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
READELF := $(CROSS)readelf
HOST_CC ?= cc
PICOLIBC ?= $(HOME)/picolibc-astra
RUNTIME := ../runtime
COMPILER := ../compiler
POSIX := ../posix
STREAMS := ../streams
VFS := ../vfs
NDK := ../../../ndk
CPPFLAGS := -I$(NDK)/include -Iinclude -I../../include -I../posix/include \
	-I$(RUNTIME)/include \
	-isystem $(PICOLIBC)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
LIBRARY_FLAGS := $(TARGET_FLAGS) $(ASTRA_SHARED_PIC_FLAGS)
SOURCES := src/ntp_core.c src/ntp_client.c src/ntp_control.c
OBJECTS := $(patsubst src/%.c,build/m68k/%.o,$(SOURCES))
LIB := build/m68k/libastrantp.a
LIBRARY := build/m68k/libraries/ntp.library.1
LIBRARY_IDENTITY := build/m68k/dynamic/ntp_library.o
LIBRARY_OBJECTS := $(patsubst src/%.c,build/m68k/dynamic/%.o,$(SOURCES))
LIBRARY_MAP := ntp.library.exports
LIBC_LIBRARY := $(POSIX)/build/m68k/libraries/libc.library.1
RUNTIME_LIBRARY := $(RUNTIME)/build/m68k/libraries/runtime.library.1
COMPILER_LIBRARY := $(COMPILER)/build/m68k/libraries/compiler.library.1
SYMBOL_OWNERSHIP_CHECK := ../../../tools/check_shared_symbol_ownership.py
HOST_TEST := build/host/test_ntp_core

all: $(LIB) ntp-library-contract

library: $(LIBRARY)

$(LIB): $(OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

build/m68k/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/m68k/dynamic/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) -MMD -MP \
		-c $< -o $@

$(LIBRARY_IDENTITY): src/ntp_library.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) -MMD -MP \
		-c $< -o $@

libc-library-ready:
	$(MAKE) -C $(POSIX) library

$(LIBRARY): $(LIBRARY_IDENTITY) $(LIBRARY_OBJECTS) $(LIBRARY_MAP) \
		$(ASTRA_SHARED_LINK_INPUTS) | libc-library-ready
	@mkdir -p $(@D)
	$(CC) $(ASTRA_TARGET_ABI_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,ntp.library.1 -Wl,--version-script=$(LIBRARY_MAP) \
		-o $@ $(LIBRARY_IDENTITY) $(LIBRARY_OBJECTS) \
		-L$(dir $(LIBC_LIBRARY)) -L$(dir $(RUNTIME_LIBRARY)) \
		-L$(dir $(COMPILER_LIBRARY)) \
		-Wl,-rpath-link,$(abspath $(STREAMS)/build/m68k/libraries) \
		-Wl,-rpath-link,$(abspath $(VFS)/build/m68k/libraries) \
		-Wl,--no-as-needed -Wl,-l:libc.library.1 \
		-Wl,-l:runtime.library.1 -Wl,-l:compiler.library.1

ntp-library-contract: $(LIBRARY)
	@python3 ../../../tools/check_shared_library_layout.py $(LIBRARY)
	@python3 $(SYMBOL_OWNERSHIP_CHECK) --readelf $(READELF) \
		$(LIBRARY) $(LIBC_LIBRARY) $(RUNTIME_LIBRARY) $(COMPILER_LIBRARY)
	@python3 ../../../tools/check_library_exports.py $(LIBRARY_MAP) \
		../../include/astra/ntp.h include/astra/ntp_core.h
	@python3 ../../../tools/check_elf_exports.py --readelf $(READELF) \
		$(LIBRARY_MAP) $(LIBRARY)
	@python3 ../../../tools/library_info.py $< | \
		grep -q '^ntp.library.1 1.0.0 ABI 1.0$$'
	@$(READELF) -dW $< | grep -Fq 'Library soname: [ntp.library.1]'
	@test "$$($(READELF) -dW $< | grep -Fc 'Shared library: [libc.library.1]')" = 1
	@test "$$($(READELF) -dW $< | grep -Fc 'Shared library: [runtime.library.1]')" = 1
	@test "$$($(READELF) -dW $< | grep -Fc 'Shared library: [compiler.library.1]')" = 1
	@test "$$($(READELF) -dW $< | awk '/NEEDED/{n++} END{print n+0}')" = 3
	@$(READELF) -dW $< | grep -q 'BIND_NOW'
	@! $(READELF) -dW $< | grep -q 'TEXTREL'
	@$(READELF) -lW $< | grep -q ' GNU_RELRO '
	@$(READELF) -VW $< | grep -q 'ASTRA_NTP_1.0'
	@echo "astra NTP shared library: PASS"

$(HOST_TEST): src/ntp_core.c tests/test_ntp_core.c
	@mkdir -p $(@D)
	$(HOST_CC) -Iinclude $(COMMON_FLAGS) -O2 $^ -o $@

test: $(HOST_TEST)
	./$(HOST_TEST)

sanitize:
	@mkdir -p build/host
	$(HOST_CC) -Iinclude $(COMMON_FLAGS) -O2 \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		src/ntp_core.c tests/test_ntp_core.c \
		-o build/host/test_ntp_core_sanitize
	./build/host/test_ntp_core_sanitize

analyze:
	@mkdir -p build/analyzer
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c src/ntp_core.c -o build/analyzer/ntp_core.o
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c src/ntp_client.c -o build/analyzer/ntp_client.o

clean:
	rm -rf build

-include $(OBJECTS:.o=.d) $(wildcard build/m68k/dynamic/*.d)

.PHONY: all library test sanitize analyze clean libc-library-ready \
	ntp-library-contract
