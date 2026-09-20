include ../../../mk/m68k-cross.mk
include ../../../mk/astra-shared-library.mk

CC := $(CROSS)gcc
READELF := $(CROSS)readelf
HOST_CC ?= cc
ANALYZER_CC ?= gcc
RUNTIME := ../runtime
COMPILER := ../compiler
GRAPHICS := ../graphics
NDK := ../../../ndk
CPPFLAGS := -Iinclude -I$(GRAPHICS)/include -I$(RUNTIME)/include \
	-I../../include -I$(NDK)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
HOST_FLAGS := -O2
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
LIBRARY_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_SHARED_PIC_FLAGS)
INTERFACE_TARGET := build/m68k/libraries/interface.library.5
INTERFACE_OBJECTS := build/m68k/interface_library.o build/m68k/control.o \
	build/m68k/text_surface.o build/m68k/undo.o build/m68k/text_model.o \
	build/m68k/scroll.o
INTERFACE_LIBRARY_MAP := interface.library.exports
NDK_HOST_UTF8 := $(NDK)/build/host/utf8.o
GRAPHICS_LIBRARY := $(GRAPHICS)/build/m68k/libraries/graphics.library.2
FONT_LIBRARY := $(GRAPHICS)/build/m68k/libraries/font.library.2
SYSTEM_LIBRARY := $(NDK)/build/m68k/libraries/system.library.2
RUNTIME_LIBRARY := $(RUNTIME)/build/m68k/libraries/runtime.library.1
COMPILER_LIBRARY := $(COMPILER)/build/m68k/libraries/compiler.library.1
SYMBOL_OWNERSHIP_CHECK := ../../../tools/check_shared_symbol_ownership.py
INTERFACE_HOST_TEST := build/host/test_interface
TEXT_SURFACE_HOST_TEST := build/host/test_text_surface
UNDO_HOST_TEST := build/host/test_undo
TEXT_MODEL_HOST_TEST := build/host/test_text_model
SCROLL_HOST_TEST := build/host/test_scroll
GRAPHICS_FONT_HEADERS := $(GRAPHICS)/build/generated/astra_font8x8.inc \
	$(GRAPHICS)/build/generated/astra_ui_font.inc \
	$(GRAPHICS)/build/generated/astra_mono_font.inc
GRAPHICS_FONT_INPUTS := $(GRAPHICS)/fonts/astra-workbench.afnt \
	$(GRAPHICS)/fonts/astra-mono.afnt ../../../assets/fonts/astra_8x16.hex \
	../../../tools/fonts/afnt.py $(NDK)/include/astra/font_library.h

all: $(INTERFACE_TARGET) library-contract

interface-library: $(INTERFACE_TARGET)

$(GRAPHICS_FONT_HEADERS): $(GRAPHICS_FONT_INPUTS)
	$(MAKE) -C $(GRAPHICS) all

build/m68k/interface_library.o: src/interface_library.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(GRAPHICS)/build/generated $(COMMON_FLAGS) \
		$(LIBRARY_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/control.o: src/control.c src/control_internal.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(GRAPHICS)/build/generated $(COMMON_FLAGS) \
		$(LIBRARY_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/text_surface.o: src/text_surface.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(GRAPHICS)/build/generated $(COMMON_FLAGS) \
		$(LIBRARY_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/undo.o: src/undo.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

build/m68k/text_model.o: src/text_model.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

build/m68k/scroll.o: src/scroll.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

$(INTERFACE_OBJECTS): Makefile

shared-libraries-ready:
	$(MAKE) -C $(GRAPHICS) graphics-library font-library
	$(MAKE) -C $(NDK) library
	$(MAKE) -C $(COMPILER) library

$(INTERFACE_TARGET): $(INTERFACE_OBJECTS) $(INTERFACE_LIBRARY_MAP) \
		$(ASTRA_SHARED_LINK_INPUTS) \
		| shared-libraries-ready
	@mkdir -p $(@D)
	$(CC) $(LIBRARY_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,interface.library.5 \
		-Wl,--version-script=$(INTERFACE_LIBRARY_MAP) \
		-o $@ $(INTERFACE_OBJECTS) \
		-L$(dir $(GRAPHICS_LIBRARY)) -L$(dir $(SYSTEM_LIBRARY)) \
		-L$(dir $(RUNTIME_LIBRARY)) -L$(dir $(COMPILER_LIBRARY)) \
		-Wl,--no-as-needed -Wl,-l:graphics.library.2 \
		-Wl,-l:font.library.2 -Wl,-l:system.library.2 \
		-Wl,-l:runtime.library.1 -Wl,-l:compiler.library.1 \
		$(ASTRA_SHARED_LINK_END)

library-contract: $(INTERFACE_TARGET)
	@python3 ../../../tools/check_shared_library_layout.py $(INTERFACE_TARGET)
	@python3 ../../../tools/check_elf_exports.py --readelf $(READELF) \
		$(INTERFACE_LIBRARY_MAP) $(INTERFACE_TARGET)
	@python3 $(SYMBOL_OWNERSHIP_CHECK) --readelf $(READELF) \
		$(INTERFACE_TARGET) $(GRAPHICS_LIBRARY) $(FONT_LIBRARY) \
		$(SYSTEM_LIBRARY) $(RUNTIME_LIBRARY) $(COMPILER_LIBRARY)
	@python3 ../../../tools/library_info.py $(INTERFACE_TARGET) | \
		grep -q '^interface.library.5 5.1.0 ABI 5.1$$'
	@test "$$($(READELF) -dW $(INTERFACE_TARGET) | \
		grep -Fc 'Shared library: [graphics.library.2]')" = 1
	@test "$$($(READELF) -dW $(INTERFACE_TARGET) | \
		grep -Fc 'Shared library: [font.library.2]')" = 1
	@test "$$($(READELF) -dW $(INTERFACE_TARGET) | \
		grep -Fc 'Shared library: [system.library.2]')" = 1
	@test "$$($(READELF) -dW $(INTERFACE_TARGET) | \
		grep -Fc 'Shared library: [runtime.library.1]')" = 1
	@test "$$($(READELF) -dW $(INTERFACE_TARGET) | \
		grep -Fc 'Shared library: [compiler.library.1]')" = 1
	@test "$$($(READELF) -dW $(INTERFACE_TARGET) | \
		awk '/NEEDED/{n++} END{print n+0}')" = 5
	$(call ASTRA_CHECK_DSO,$(INTERFACE_TARGET))

$(INTERFACE_HOST_TEST): src/interface_library.c src/control.c src/text_surface.c \
		src/text_model.c src/scroll.c $(NDK_HOST_UTF8) tests/test_interface.c \
		tests/test_layout.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -DASTRA_INTERFACE_TEST \
		-DASTRA_NDK_TEST \
		-ffunction-sections -Wl,--gc-sections $^ -o $@

$(TEXT_SURFACE_HOST_TEST): src/text_surface.c tests/test_text_surface.c \
		$(NDK_HOST_UTF8)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/text_surface.c $(NDK_HOST_UTF8) tests/test_text_surface.c -o $@

$(UNDO_HOST_TEST): src/undo.c tests/test_undo.c $(NDK_HOST_UTF8)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/undo.c $(NDK_HOST_UTF8) tests/test_undo.c -o $@

$(TEXT_MODEL_HOST_TEST): src/text_model.c tests/test_text_model.c \
		$(NDK_HOST_UTF8)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/text_model.c $(NDK_HOST_UTF8) tests/test_text_model.c -o $@

$(SCROLL_HOST_TEST): src/scroll.c tests/test_scroll.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $^ -o $@

$(NDK_HOST_UTF8):
	$(MAKE) -C $(NDK) build/host/utf8.o

test: $(INTERFACE_HOST_TEST) $(TEXT_SURFACE_HOST_TEST) \
		$(UNDO_HOST_TEST) $(TEXT_MODEL_HOST_TEST) $(SCROLL_HOST_TEST)
	./$(INTERFACE_HOST_TEST)
	./$(TEXT_SURFACE_HOST_TEST)
	./$(UNDO_HOST_TEST)
	./$(TEXT_MODEL_HOST_TEST)
	./$(SCROLL_HOST_TEST)

sanitize: $(NDK_HOST_UTF8)
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) -DASTRA_INTERFACE_TEST -DASTRA_NDK_TEST \
		-ffunction-sections -Wl,--gc-sections src/interface_library.c \
		src/control.c src/text_surface.c src/text_model.c src/scroll.c \
		$(NDK_HOST_UTF8) \
		tests/test_interface.c tests/test_layout.c \
		-o build/host/test_interface_sanitize
	./build/host/test_interface_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/text_surface.c $(NDK_HOST_UTF8) \
		tests/test_text_surface.c -o build/host/test_text_surface_sanitize
	./build/host/test_text_surface_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/undo.c $(NDK_HOST_UTF8) tests/test_undo.c \
		-o build/host/test_undo_sanitize
	./build/host/test_undo_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/text_model.c $(NDK_HOST_UTF8) \
		tests/test_text_model.c -o build/host/test_text_model_sanitize
	./build/host/test_text_model_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/scroll.c tests/test_scroll.c \
		-o build/host/test_scroll_sanitize
	./build/host/test_scroll_sanitize

analyze:
	@mkdir -p build/analyzer
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/interface_library.c -o build/analyzer/interface_library.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/control.c -o build/analyzer/control.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/text_surface.c -o build/analyzer/text_surface.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/undo.c -o build/analyzer/undo.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/text_model.c -o build/analyzer/text_model.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/scroll.c -o build/analyzer/scroll.o

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)

.PHONY: all interface-library test sanitize analyze library-contract clean \
	shared-libraries-ready
