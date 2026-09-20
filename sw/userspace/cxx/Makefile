include ../../../mk/m68k-cross.mk
include ../../../mk/astra-shared-library.mk

CC := $(CROSS)gcc
CXX := $(CROSS)g++
READELF := $(CROSS)readelf
POSIX := ../posix
RUNTIME := ../runtime
COMPILER := ../compiler
STREAMS := ../streams
VFS := ../vfs
LIBSTDCPP := $(shell $(CXX) -print-file-name=libstdc++_shared.a)
LIBRARY := build/m68k/libraries/cxx.library.1
LIBRARY_IDENTITY := build/m68k/cxx_library.o
LIBRARY_MAP := build/m68k/cxx.library.exports
LIBRARY_UNDEFINED := build/m68k/cxx.library.undefined
LIBRARY_CONTRACT_STAMP := build/m68k/cxx.library.inputs
EXTRA_EXPORTS := cxx.extra.exports
CONTRACT_TOOL := ../../../tools/generate_archive_contract.py
SYMBOL_OWNERSHIP_CHECK := ../../../tools/check_shared_symbol_ownership.py
LIBC_LIBRARY := $(POSIX)/build/m68k/libraries/libc.library.1
RUNTIME_LIBRARY := $(RUNTIME)/build/m68k/libraries/runtime.library.1
COMPILER_LIBRARY := $(COMPILER)/build/m68k/libraries/compiler.library.1
UNWIND_LIBRARY := $(COMPILER)/build/m68k/libraries/unwind.library.1
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
LIBRARY_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS) \
	$(ASTRA_SHARED_PIC_FLAGS)

all: cxx-library-contract

library: $(LIBRARY)

$(LIBRARY_IDENTITY): src/cxx_library.c
	@mkdir -p $(@D)
	$(CC) -I../../include $(COMMON_FLAGS) $(LIBRARY_FLAGS) -MMD -MP \
		-c $< -o $@

runtime-library-ready:
	$(MAKE) -C $(RUNTIME) library

libc-library-ready: runtime-library-ready
	$(MAKE) -C $(POSIX) library

unwind-library-ready: libc-library-ready
	$(MAKE) -C $(COMPILER) unwind-library

# Cross-directory products are built by their owning makefiles.  The concrete
# products remain normal prerequisites so their changes invalidate the ABI
# map; the phony readiness barrier must be order-only or every invocation
# regenerates the map and relinks the C++ library.
$(LIBC_LIBRARY) $(RUNTIME_LIBRARY) $(COMPILER_LIBRARY) \
		$(UNWIND_LIBRARY): | unwind-library-ready
	@test -f $@

$(LIBRARY_CONTRACT_STAMP): $(LIBSTDCPP) $(EXTRA_EXPORTS) $(CONTRACT_TOOL) \
		$(LIBC_LIBRARY) $(RUNTIME_LIBRARY) $(COMPILER_LIBRARY) \
		$(UNWIND_LIBRARY) $(ASTRA_TOOLCHAIN_STAMP) \
		| unwind-library-ready
	$(call ASTRA_UPDATE_CONTENT_STAMP,$(LIBSTDCPP) $(EXTRA_EXPORTS) \
		$(CONTRACT_TOOL) $(LIBC_LIBRARY) $(RUNTIME_LIBRARY) \
		$(COMPILER_LIBRARY) $(UNWIND_LIBRARY))

$(LIBRARY_MAP) $(LIBRARY_UNDEFINED) &: $(LIBRARY_CONTRACT_STAMP)
	@mkdir -p build/m68k
	python3 $(CONTRACT_TOOL) --readelf $(READELF) \
		--version ASTRA_CXX_1_0 --exports-out $(LIBRARY_MAP) \
		--undefined-out $(LIBRARY_UNDEFINED) --extra $(EXTRA_EXPORTS) \
		--dependency $(LIBC_LIBRARY) --dependency $(RUNTIME_LIBRARY) \
		--dependency $(COMPILER_LIBRARY) --dependency $(UNWIND_LIBRARY) \
		$(LIBSTDCPP)
	$(call ASTRA_MARK_CONTENT_OUTPUTS,$(LIBRARY_CONTRACT_STAMP),\
		$(LIBRARY_MAP) $(LIBRARY_UNDEFINED))

$(LIBRARY): $(LIBRARY_IDENTITY) $(LIBRARY_MAP) $(LIBRARY_UNDEFINED) \
		$(LIBSTDCPP) $(ASTRA_SHARED_LINK_INPUTS) \
		| unwind-library-ready
	@mkdir -p $(@D)
	$(CXX) $(ASTRA_TARGET_ABI_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,cxx.library.1 -Wl,--version-script=$(LIBRARY_MAP) \
		-Wl,@$(LIBRARY_UNDEFINED) -o $@ $(LIBRARY_IDENTITY) \
		-L$(dir $(LIBC_LIBRARY)) -L$(dir $(RUNTIME_LIBRARY)) \
		-L$(dir $(COMPILER_LIBRARY)) \
		-Wl,-rpath-link,$(abspath $(STREAMS)/build/m68k/libraries) \
		-Wl,-rpath-link,$(abspath $(VFS)/build/m68k/libraries) \
		-Wl,--no-as-needed -Wl,-l:libc.library.1 \
		-Wl,-l:runtime.library.1 -Wl,-l:compiler.library.1 \
		-Wl,-l:unwind.library.1 -Wl,--start-group \
		$(LIBSTDCPP) -Wl,--end-group $(ASTRA_SHARED_LINK_END)

cxx-library-contract: $(LIBRARY)
	@python3 ../../../tools/check_shared_library_layout.py $(LIBRARY)
	@python3 $(SYMBOL_OWNERSHIP_CHECK) --readelf $(READELF) \
		$(LIBRARY) $(LIBC_LIBRARY) $(RUNTIME_LIBRARY) \
		$(COMPILER_LIBRARY) $(UNWIND_LIBRARY)
	@python3 ../../../tools/check_elf_exports.py --readelf $(READELF) \
		$(LIBRARY_MAP) $(LIBRARY)
	@python3 ../../../tools/library_info.py $< | \
		grep -q '^cxx.library.1 1.0.0 ABI 1.0$$'
	@$(READELF) -dW $< | grep -Fq 'Library soname: [cxx.library.1]'
	@test "$$($(READELF) -dW $< | grep -Fc 'Shared library: [libc.library.1]')" = 1
	@test "$$($(READELF) -dW $< | grep -Fc 'Shared library: [runtime.library.1]')" = 1
	@test "$$($(READELF) -dW $< | grep -Fc 'Shared library: [compiler.library.1]')" = 1
	@test "$$($(READELF) -dW $< | grep -Fc 'Shared library: [unwind.library.1]')" = 1
	@test "$$($(READELF) -dW $< | awk '/NEEDED/{n++} END{print n+0}')" = 4
	@$(READELF) -dW $< | grep -q 'BIND_NOW'
	@! $(READELF) -dW $< | grep -q 'TEXTREL'
	@$(READELF) -lW $< | grep -q ' GNU_RELRO '
	@$(READELF) -VW $< | grep -q 'ASTRA_CXX_1.0'
	@echo "Astra C++ shared library: PASS"

test sanitize analyze:
	@:

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)

.PHONY: all library test sanitize analyze clean runtime-library-ready \
	libc-library-ready unwind-library-ready cxx-library-contract
