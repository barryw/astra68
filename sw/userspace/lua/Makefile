include ../../../mk/m68k-cross.mk
include ../../../mk/astra-shared-library.mk

CC := $(CROSS)gcc
AR := $(CROSS)gcc-ar
READELF := $(CROSS)readelf
LUA := ../../../third_party/lua/src
POSIX := ../posix
RUNTIME := ../runtime
COMPILER := ../compiler
STREAMS := ../streams
VFS := ../vfs
PICOLIBC ?= $(HOME)/picolibc-astra
CPPFLAGS := -I$(LUA) -I../../include -isystem $(PICOLIBC)/include
COMMON_FLAGS := -std=gnu11 -Wall -Wextra -Werror
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -O3 -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
LIBRARY_FLAGS := $(TARGET_FLAGS) $(ASTRA_SHARED_PIC_FLAGS)
SOURCES := $(filter-out $(LUA)/lua.c $(LUA)/luac.c,$(wildcard $(LUA)/*.c))
OBJECTS := $(patsubst $(LUA)/%.c,build/m68k/static/%.o,$(SOURCES))
LIBRARY_OBJECTS := $(patsubst $(LUA)/%.c,build/m68k/dynamic/%.o,$(SOURCES))
LIB := build/m68k/liblua.a
LIBRARY := build/m68k/libraries/lua.library.5
LIBRARY_IDENTITY := build/m68k/dynamic/lua_library.o
LIBRARY_MAP := build/m68k/lua.library.exports
EXPORT_TOOL := ../../../tools/generate_header_exports.py
PUBLIC_HEADERS := $(LUA)/lua.h $(LUA)/lauxlib.h $(LUA)/lualib.h
LIBC_LIBRARY := $(POSIX)/build/m68k/libraries/libc.library.1
RUNTIME_LIBRARY := $(RUNTIME)/build/m68k/libraries/runtime.library.1
COMPILER_LIBRARY := $(COMPILER)/build/m68k/libraries/compiler.library.1
STREAMS_LIBRARY := $(STREAMS)/build/m68k/libraries/streams.library.1
FILESYSTEM_LIBRARY := $(VFS)/build/m68k/libraries/filesystem.library.2
SYMBOL_OWNERSHIP_CHECK := ../../../tools/check_shared_symbol_ownership.py

all: $(LIB) lua-library-contract

library: $(LIBRARY)

$(LIB): $(OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

build/m68k/static/%.o: $(LUA)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/m68k/dynamic/%.o: $(LUA)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) -MMD -MP -c $< -o $@

$(LIBRARY_IDENTITY): src/lua_library.c
	@mkdir -p $(@D)
	$(CC) -I../../include $(COMMON_FLAGS) $(LIBRARY_FLAGS) -MMD -MP \
		-c $< -o $@

$(LIBRARY_MAP): $(PUBLIC_HEADERS) $(EXPORT_TOOL)
	@mkdir -p $(@D)
	python3 $(EXPORT_TOOL) --version ASTRA_LUA_5_5 --output $@ \
		--macro LUA_API --macro LUALIB_API --macro LUAMOD_API \
		--extra lua_ident $(PUBLIC_HEADERS)

libc-library-ready:
	$(MAKE) -C $(POSIX) library

$(LIBRARY): $(LIBRARY_IDENTITY) $(LIBRARY_OBJECTS) $(LIBRARY_MAP) \
		$(ASTRA_SHARED_LINK_INPUTS) | libc-library-ready
	@mkdir -p $(@D)
	$(CC) $(ASTRA_TARGET_ABI_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,lua.library.5 -Wl,--version-script=$(LIBRARY_MAP) \
		-o $@ $(LIBRARY_IDENTITY) $(LIBRARY_OBJECTS) \
		-L$(dir $(LIBC_LIBRARY)) \
		-L$(dir $(COMPILER_LIBRARY)) \
		-Wl,-rpath-link,$(abspath $(STREAMS)/build/m68k/libraries) \
		-Wl,-rpath-link,$(abspath $(VFS)/build/m68k/libraries) \
		-Wl,-rpath-link,$(abspath $(RUNTIME)/build/m68k/libraries) \
		-L$(dir $(RUNTIME_LIBRARY)) -Wl,--no-as-needed \
		-Wl,-l:libc.library.1 -Wl,-l:runtime.library.1 \
		-Wl,-l:compiler.library.1 $(ASTRA_SHARED_LINK_END)

lua-library-contract: $(LIBRARY)
	@python3 ../../../tools/check_shared_library_layout.py $(LIBRARY)
	@python3 $(SYMBOL_OWNERSHIP_CHECK) --readelf $(READELF) \
		$(LIBRARY) $(LIBC_LIBRARY) $(RUNTIME_LIBRARY) $(COMPILER_LIBRARY)
	@python3 ../../../tools/check_library_exports.py $(LIBRARY_MAP) \
		$(PUBLIC_HEADERS)
	@python3 ../../../tools/check_elf_exports.py --readelf $(READELF) \
		$(LIBRARY_MAP) $(LIBRARY)
	@python3 ../../../tools/library_info.py $< | \
		grep -q '^lua.library.5 5.5.1 ABI 5.5$$'
	@$(READELF) -dW $< | grep -Fq 'Library soname: [lua.library.5]'
	@test "$$($(READELF) -dW $< | grep -Fc 'Shared library: [libc.library.1]')" = 1
	@test "$$($(READELF) -dW $< | grep -Fc 'Shared library: [runtime.library.1]')" = 1
	@test "$$($(READELF) -dW $< | grep -Fc 'Shared library: [compiler.library.1]')" = 1
	@test "$$($(READELF) -dW $< | awk '/NEEDED/{n++} END{print n+0}')" = 3
	@$(READELF) -dW $< | grep -q 'BIND_NOW'
	@! $(READELF) -dW $< | grep -q 'TEXTREL'
	@$(READELF) -lW $< | grep -q ' GNU_RELRO '
	@$(READELF) -VW $< | grep -q 'ASTRA_LUA_5_5'
	@echo "Astra Lua shared library: PASS"

test:
	PYTHONPATH=../../.. python3 -m pytest -q \
		../../../tools/tests/test_generate_header_exports.py

sanitize analyze:
	@:

clean:
	rm -rf build

-include $(wildcard build/m68k/static/*.d build/m68k/dynamic/*.d)

.PHONY: all library test sanitize analyze clean libc-library-ready \
	lua-library-contract
