include ../../../mk/m68k-cross.mk
include ../../../mk/astra-shared-library.mk

# GCC emits calls to these routines whenever an MC68040 instruction cannot
# implement a C/C++ operation directly. They are process-wide machinery, not
# private implementation details of libc or libstdc++, so one Astra shared
# library owns them for every dynamically linked component. The unwinder is a
# separate upper layer because it legitimately needs libc; keeping it out of
# the builtins library prevents a compiler -> libc -> compiler cycle.

CC := $(CROSS)gcc
READELF := $(CROSS)readelf
# The static archive deliberately hides its definitions. Astra's toolchain
# also installs GCC's standard -DSHARED, default-visible PIC object sets as
# archives so they can be linked under Astra's own DSO layout and identity.
BUILTINS_ARCHIVE := $(shell $(CC) -print-file-name=libgcc_builtins_shared.a)
UNWIND_ARCHIVE := $(shell $(CC) -print-file-name=libgcc_unwind_shared.a)
LIBRARY := build/m68k/libraries/compiler.library.1
IDENTITY := build/m68k/compiler_library.o
EXPORTS := build/m68k/compiler.library.exports
UNDEFINED := build/m68k/compiler.library.undefined
CONTRACT_STAMP := build/m68k/compiler.library.inputs
EXTRA_EXPORTS := compiler.extra.exports
UNWIND_LIBRARY := build/m68k/libraries/unwind.library.1
UNWIND_IDENTITY := build/m68k/unwind_library.o
UNWIND_EXPORTS := build/m68k/unwind.library.exports
UNWIND_UNDEFINED := build/m68k/unwind.library.undefined
UNWIND_CONTRACT_STAMP := build/m68k/unwind.library.inputs
UNWIND_EXTRA_EXPORTS := unwind.extra.exports
CONTRACT_TOOL := ../../../tools/generate_archive_contract.py
SYMBOL_OWNERSHIP_CHECK := ../../../tools/check_shared_symbol_ownership.py
POSIX := ../posix
RUNTIME := ../runtime
LIBC_LIBRARY := $(POSIX)/build/m68k/libraries/libc.library.1
RUNTIME_LIBRARY := $(RUNTIME)/build/m68k/libraries/runtime.library.1
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
LIBRARY_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS) \
	$(ASTRA_SHARED_PIC_FLAGS)

all: compiler-library-contract

library: $(LIBRARY)

unwind-library: $(UNWIND_LIBRARY)

$(IDENTITY): src/compiler_library.c
	@mkdir -p $(@D)
	$(CC) -I../../include $(COMMON_FLAGS) $(LIBRARY_FLAGS) -MMD -MP \
		-c $< -o $@

$(UNWIND_IDENTITY): src/unwind_library.c
	@mkdir -p $(@D)
	$(CC) -I../../include $(COMMON_FLAGS) $(LIBRARY_FLAGS) -MMD -MP \
		-c $< -o $@

$(CONTRACT_STAMP): $(BUILTINS_ARCHIVE) $(EXTRA_EXPORTS) $(CONTRACT_TOOL) \
		$(ASTRA_TOOLCHAIN_STAMP)
	$(call ASTRA_UPDATE_CONTENT_STAMP,$(BUILTINS_ARCHIVE) \
		$(EXTRA_EXPORTS) $(CONTRACT_TOOL))

$(EXPORTS) $(UNDEFINED) &: $(CONTRACT_STAMP)
	@mkdir -p build/m68k
	python3 $(CONTRACT_TOOL) --readelf $(READELF) \
		--version ASTRA_COMPILER_1_0 --exports-out $(EXPORTS) \
		--undefined-out $(UNDEFINED) --extra $(EXTRA_EXPORTS) \
		$(BUILTINS_ARCHIVE)
	$(call ASTRA_MARK_CONTENT_OUTPUTS,$(CONTRACT_STAMP),$(EXPORTS) $(UNDEFINED))

$(LIBRARY): $(IDENTITY) $(EXPORTS) $(UNDEFINED) $(BUILTINS_ARCHIVE) \
		$(ASTRA_SHARED_LINK_INPUTS)
	@mkdir -p $(@D)
	$(CC) $(ASTRA_TARGET_ABI_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,compiler.library.1 \
		-Wl,--version-script=$(EXPORTS) -Wl,@$(UNDEFINED) \
		-o $@ $(IDENTITY) $(BUILTINS_ARCHIVE) $(ASTRA_SHARED_LINK_END)

compiler-library-contract: $(LIBRARY)
	@python3 ../../../tools/check_shared_library_layout.py $(LIBRARY)
	@python3 ../../../tools/check_elf_exports.py --readelf $(READELF) \
		$(EXPORTS) $(LIBRARY)
	@python3 ../../../tools/library_info.py $< | \
		grep -q '^compiler.library.1 1.0.0 ABI 1.0$$'
	@$(READELF) -dW $< | grep -Fq 'Library soname: [compiler.library.1]'
	@test "$$($(READELF) -dW $< | awk '/NEEDED/{n++} END{print n+0}')" = 0
	@$(READELF) -dW $< | grep -q 'BIND_NOW'
	@! $(READELF) -dW $< | grep -q 'TEXTREL'
	@$(READELF) -lW $< | grep -q ' GNU_RELRO '
	@$(READELF) -VW $< | grep -q 'ASTRA_COMPILER_1.0'
	@echo "Astra compiler runtime shared library: PASS"

libc-library-ready: library
	$(MAKE) -C $(POSIX) library

$(UNWIND_CONTRACT_STAMP): $(UNWIND_ARCHIVE) $(UNWIND_EXTRA_EXPORTS) \
		$(CONTRACT_TOOL) $(LIBRARY) $(RUNTIME_LIBRARY) $(LIBC_LIBRARY) \
		$(ASTRA_TOOLCHAIN_STAMP) | libc-library-ready
	$(call ASTRA_UPDATE_CONTENT_STAMP,$(UNWIND_ARCHIVE) \
		$(UNWIND_EXTRA_EXPORTS) $(CONTRACT_TOOL) $(LIBRARY) \
		$(RUNTIME_LIBRARY) $(LIBC_LIBRARY))

$(UNWIND_EXPORTS) $(UNWIND_UNDEFINED) &: $(UNWIND_CONTRACT_STAMP)
	@mkdir -p build/m68k
	python3 $(CONTRACT_TOOL) --readelf $(READELF) \
		--version ASTRA_UNWIND_1_0 --exports-out $(UNWIND_EXPORTS) \
		--undefined-out $(UNWIND_UNDEFINED) --extra $(UNWIND_EXTRA_EXPORTS) \
		--dependency $(LIBRARY) --dependency $(RUNTIME_LIBRARY) \
		--dependency $(LIBC_LIBRARY) \
		$(UNWIND_ARCHIVE)
	$(call ASTRA_MARK_CONTENT_OUTPUTS,$(UNWIND_CONTRACT_STAMP),\
		$(UNWIND_EXPORTS) $(UNWIND_UNDEFINED))

$(UNWIND_LIBRARY): $(UNWIND_IDENTITY) $(UNWIND_EXPORTS) \
		$(UNWIND_UNDEFINED) $(UNWIND_ARCHIVE) $(ASTRA_SHARED_LINK_INPUTS) \
		| libc-library-ready
	@mkdir -p $(@D)
	$(CC) $(ASTRA_TARGET_ABI_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,unwind.library.1 \
		-Wl,--version-script=$(UNWIND_EXPORTS) \
		-Wl,@$(UNWIND_UNDEFINED) -o $@ $(UNWIND_IDENTITY) \
		-L$(dir $(LIBC_LIBRARY)) -L$(dir $(RUNTIME_LIBRARY)) \
		-L$(dir $(LIBRARY)) \
		-Wl,--no-as-needed -Wl,-l:libc.library.1 \
		-Wl,-l:runtime.library.1 -Wl,-l:compiler.library.1 \
		$(UNWIND_ARCHIVE) $(ASTRA_SHARED_LINK_END)

unwind-library-contract: $(UNWIND_LIBRARY)
	@python3 ../../../tools/check_shared_library_layout.py $(UNWIND_LIBRARY)
	@python3 $(SYMBOL_OWNERSHIP_CHECK) --readelf $(READELF) \
		$(UNWIND_LIBRARY) $(LIBRARY) $(RUNTIME_LIBRARY) $(LIBC_LIBRARY)
	@python3 ../../../tools/check_elf_exports.py --readelf $(READELF) \
		$(UNWIND_EXPORTS) $(UNWIND_LIBRARY)
	@python3 ../../../tools/library_info.py $< | \
		grep -q '^unwind.library.1 1.0.0 ABI 1.0$$'
	@$(READELF) -dW $< | grep -Fq 'Library soname: [unwind.library.1]'
	@test "$$($(READELF) -dW $< | grep -Fc 'Shared library: [libc.library.1]')" = 1
	@test "$$($(READELF) -dW $< | grep -Fc 'Shared library: [runtime.library.1]')" = 1
	@test "$$($(READELF) -dW $< | grep -Fc 'Shared library: [compiler.library.1]')" = 1
	@test "$$($(READELF) -dW $< | awk '/NEEDED/{n++} END{print n+0}')" = 3
	@$(READELF) -dW $< | grep -q 'BIND_NOW'
	@! $(READELF) -dW $< | grep -q 'TEXTREL'
	@$(READELF) -lW $< | grep -q ' GNU_RELRO '
	@$(READELF) -VW $< | grep -q 'ASTRA_UNWIND_1.0'
	@echo "Astra unwinder shared library: PASS"

test sanitize analyze:
	@:

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)

.PHONY: all library unwind-library test sanitize analyze clean \
	compiler-library-contract libc-library-ready unwind-library-contract
