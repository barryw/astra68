include ../../../mk/m68k-cross.mk
include ../../../mk/astra-shared-library.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
READELF := $(CROSS)readelf
HOST_CC ?= cc
ANALYZER_CC ?= gcc
RUNTIME := ../runtime
COMPILER := ../compiler
NDK := ../../../ndk
CPPFLAGS := -I$(NDK)/include -Iinclude -I$(RUNTIME)/include \
	-I../../include -I../../../fpga/arty/linux
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
HOST_FLAGS := -O2
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
FONT_SOURCE := ../../../assets/fonts/astra_8x16.hex
FONT_HEADER := build/generated/astra_font8x8.inc
UI_FONT_SOURCE := fonts/astra-workbench.afnt
UI_FONT_TOOL := ../../../tools/fonts/afnt.py
UI_FONT_HEADER := build/generated/astra_ui_font.inc
MONO_FONT_SOURCE := fonts/astra-mono.afnt
MONO_FONT_HEADER := build/generated/astra_mono_font.inc
SOURCES := src/surface.c src/shared_surface.c src/render_builder.c
TARGET := build/m68k/libastragraphics.a
RENDERER_TARGET := build/m68k/libastragraphics-renderer.a
TARGET_OBJECTS := build/m68k/surface.o build/m68k/shared_surface.o \
	build/m68k/render_builder.o
RENDERER_OBJECTS := build/m68k/pic/render_builder.o
PIC_TARGET := build/m68k/libastragraphics-pic.a
PIC_OBJECTS := build/m68k/pic/surface.o build/m68k/pic/shared_surface.o \
	build/m68k/pic/render_builder.o
HOST_TEST := build/host/test_surface
LIBRARY_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_SHARED_PIC_FLAGS)
GRAPHICS_LIBRARY := build/m68k/libraries/graphics.library.2
FONT_LIBRARY := build/m68k/libraries/font.library.2
GRAPHICS_LIBRARY_MAP := graphics.library.exports
FONT_LIBRARY_MAP := font.library.exports
GRAPHICS_LIBRARY_OBJECTS := build/m68k/library/graphics_library.o \
	build/m68k/library/graphics_surface.o \
	build/m68k/library/graphics_shared_surface.o
FONT_LIBRARY_OBJECTS := build/m68k/library/font_library.o \
	build/m68k/library/font_surface.o
SYSTEM_LIBRARY := $(NDK)/build/m68k/libraries/system.library.2
RUNTIME_LIBRARY := $(RUNTIME)/build/m68k/libraries/runtime.library.1
COMPILER_LIBRARY := $(COMPILER)/build/m68k/libraries/compiler.library.1
SYMBOL_OWNERSHIP_CHECK := ../../../tools/check_shared_symbol_ownership.py

all: $(TARGET) $(RENDERER_TARGET) $(GRAPHICS_LIBRARY) $(FONT_LIBRARY) \
	library-contract

graphics-library: $(GRAPHICS_LIBRARY)

font-library: $(FONT_LIBRARY)

pic: $(PIC_TARGET)

renderer: $(RENDERER_TARGET)

$(TARGET): $(TARGET_OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

$(RENDERER_TARGET): $(RENDERER_OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

$(PIC_TARGET): $(PIC_OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

build/m68k/pic/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -Ibuild/generated $(COMMON_FLAGS) $(LIBRARY_FLAGS) \
		$(DEPFLAGS) -c $< -o $@

$(FONT_HEADER): $(FONT_SOURCE)
	@mkdir -p $(@D)
	@{ printf 'static const uint8_t astra_font8x8[] = {\n'; \
	  awk '{ printf "0x%s,\n", $$1 }' $<; \
	  printf '};\n'; } > $@

$(UI_FONT_HEADER): $(UI_FONT_SOURCE) $(UI_FONT_TOOL)
	@mkdir -p $(@D)
	python3 $(UI_FONT_TOOL) emit-c $< $@

$(MONO_FONT_HEADER): $(MONO_FONT_SOURCE) $(UI_FONT_TOOL)
	@mkdir -p $(@D)
	python3 $(UI_FONT_TOOL) emit-c --prefix astra_mono $< $@

build/m68k/%.o: src/%.c ../../../ndk/include/astra/surface.h \
	../../../ndk/include/astra/window.h ../../include/astra/gui.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -Ibuild/generated $(COMMON_FLAGS) $(TARGET_FLAGS) \
		$(DEPFLAGS) -c $< -o $@

build/m68k/surface.o: $(FONT_HEADER) $(UI_FONT_HEADER) $(MONO_FONT_HEADER)
build/m68k/render_builder.o: $(UI_FONT_HEADER) $(MONO_FONT_HEADER) \
	../../../fpga/arty/linux/astra_render_protocol.h
build/m68k/pic/surface.o: $(FONT_HEADER) $(UI_FONT_HEADER) $(MONO_FONT_HEADER)
build/m68k/pic/render_builder.o: $(UI_FONT_HEADER) $(MONO_FONT_HEADER) \
	../../../fpga/arty/linux/astra_render_protocol.h

build/m68k/library/graphics_library.o: src/graphics_library.c \
		../../../ndk/include/astra/graphics_library.h \
		../../include/astra/library.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

build/m68k/library/font_library.o: src/font_library.c \
		../../../ndk/include/astra/font_library.h \
		../../include/astra/library.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) -Ibuild/generated \
		$(DEPFLAGS) -c $< -o $@

build/m68k/library/graphics_surface.o: src/surface.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -Ibuild/generated $(COMMON_FLAGS) $(LIBRARY_FLAGS) \
		-DASTRA_SURFACE_CORE=1 $(DEPFLAGS) -c $< -o $@

build/m68k/library/font_surface.o: src/surface.c $(FONT_HEADER) \
		$(UI_FONT_HEADER) $(MONO_FONT_HEADER)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -Ibuild/generated $(COMMON_FLAGS) $(LIBRARY_FLAGS) \
		-DASTRA_SURFACE_FONT=1 $(DEPFLAGS) -c $< -o $@

build/m68k/library/graphics_shared_surface.o: src/shared_surface.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

$(TARGET_OBJECTS) $(PIC_OBJECTS): Makefile
$(GRAPHICS_LIBRARY_OBJECTS) $(FONT_LIBRARY_OBJECTS): Makefile

shared-libraries-ready:
	$(MAKE) -C $(NDK) library
	$(MAKE) -C $(COMPILER) library

$(GRAPHICS_LIBRARY): $(GRAPHICS_LIBRARY_OBJECTS) $(GRAPHICS_LIBRARY_MAP) \
		$(ASTRA_SHARED_LINK_INPUTS) | shared-libraries-ready
	@mkdir -p $(@D)
	$(CC) $(LIBRARY_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,graphics.library.2 -o $@ \
		-Wl,--version-script=$(GRAPHICS_LIBRARY_MAP) \
		$(GRAPHICS_LIBRARY_OBJECTS) \
		-L$(dir $(SYSTEM_LIBRARY)) -L$(dir $(RUNTIME_LIBRARY)) \
		-L$(dir $(COMPILER_LIBRARY)) -Wl,--no-as-needed \
		-Wl,-l:system.library.2 -Wl,-l:runtime.library.1 \
		-Wl,-l:compiler.library.1

$(FONT_LIBRARY): $(FONT_LIBRARY_OBJECTS) $(FONT_LIBRARY_MAP) \
		$(GRAPHICS_LIBRARY) \
		$(ASTRA_SHARED_LINK_INPUTS) | shared-libraries-ready
	@mkdir -p $(@D)
	$(CC) $(LIBRARY_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,font.library.2 -o $@ $(FONT_LIBRARY_OBJECTS) \
		-Wl,--version-script=$(FONT_LIBRARY_MAP) \
		-L$(dir $(GRAPHICS_LIBRARY)) -L$(dir $(RUNTIME_LIBRARY)) \
		-L$(dir $(COMPILER_LIBRARY)) -Wl,--no-as-needed \
		-Wl,-l:graphics.library.2 -Wl,-l:runtime.library.1 \
		-Wl,-l:compiler.library.1

library-contract: $(GRAPHICS_LIBRARY) $(FONT_LIBRARY)
	@python3 ../../../tools/check_shared_library_layout.py \
		$(GRAPHICS_LIBRARY)
	@python3 ../../../tools/check_shared_library_layout.py \
		$(FONT_LIBRARY)
	@python3 $(SYMBOL_OWNERSHIP_CHECK) --readelf $(READELF) \
		$(GRAPHICS_LIBRARY) $(SYSTEM_LIBRARY) $(RUNTIME_LIBRARY) \
		$(COMPILER_LIBRARY)
	@python3 $(SYMBOL_OWNERSHIP_CHECK) --readelf $(READELF) \
		$(FONT_LIBRARY) $(GRAPHICS_LIBRARY) $(RUNTIME_LIBRARY) \
		$(COMPILER_LIBRARY)
	@python3 ../../../tools/check_elf_exports.py --readelf $(READELF) \
		$(GRAPHICS_LIBRARY_MAP) $(GRAPHICS_LIBRARY)
	@python3 ../../../tools/check_elf_exports.py --readelf $(READELF) \
		$(FONT_LIBRARY_MAP) $(FONT_LIBRARY)
	@python3 ../../../tools/check_library_exports.py $(FONT_LIBRARY_MAP) \
		$(NDK)/include/astra/font_library.h $(NDK)/include/astra/surface.h
	@python3 ../../../tools/library_info.py $(GRAPHICS_LIBRARY) | \
		grep -q '^graphics.library.2 2.1.0 ABI 2.1$$'
	@python3 ../../../tools/library_info.py $(FONT_LIBRARY) | \
		grep -q '^font.library.2 2.2.0 ABI 2.2$$'
	@test "$$($(READELF) -dW $(GRAPHICS_LIBRARY) | \
		grep -Fc 'Shared library: [system.library.2]')" = 1
	@test "$$($(READELF) -dW $(GRAPHICS_LIBRARY) | \
		grep -Fc 'Shared library: [runtime.library.1]')" = 1
	@test "$$($(READELF) -dW $(GRAPHICS_LIBRARY) | \
		grep -Fc 'Shared library: [compiler.library.1]')" = 1
	@test "$$($(READELF) -dW $(GRAPHICS_LIBRARY) | \
		awk '/NEEDED/{n++} END{print n+0}')" = 3
	@test "$$($(READELF) -dW $(FONT_LIBRARY) | \
		grep -Fc 'Shared library: [graphics.library.2]')" = 1
	@test "$$($(READELF) -dW $(FONT_LIBRARY) | \
		grep -Fc 'Shared library: [runtime.library.1]')" = 1
	@test "$$($(READELF) -dW $(FONT_LIBRARY) | \
		grep -Fc 'Shared library: [compiler.library.1]')" = 1
	@test "$$($(READELF) -dW $(FONT_LIBRARY) | \
		awk '/NEEDED/{n++} END{print n+0}')" = 3
	$(call ASTRA_CHECK_DSO,$(GRAPHICS_LIBRARY) $(FONT_LIBRARY))

$(HOST_TEST): src/surface.c src/render_builder.c tests/test_surface.c \
		../../../ndk/include/astra/surface.h include/astra/render_builder.h \
		../../include/astra/rounded.h ../../include/astra/window_scene.h \
		$(FONT_HEADER)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) -Ibuild/generated $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/surface.c src/render_builder.c \
		tests/test_surface.c -o $@

$(HOST_TEST): $(UI_FONT_HEADER) $(MONO_FONT_HEADER)

test: $(HOST_TEST)
	./$(HOST_TEST)

sanitize: $(FONT_HEADER) $(UI_FONT_HEADER) $(MONO_FONT_HEADER)
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) -Ibuild/generated $(COMMON_FLAGS) $(HOST_FLAGS) $(SANITIZER_FLAGS) \
		src/surface.c src/render_builder.c tests/test_surface.c \
		-o build/host/test_surface_sanitize
	./build/host/test_surface_sanitize

analyze: $(FONT_HEADER) $(UI_FONT_HEADER) $(MONO_FONT_HEADER)
	@mkdir -p build/analyzer
	$(ANALYZER_CC) $(CPPFLAGS) -Ibuild/generated $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/surface.c -o build/analyzer/surface.o
	$(ANALYZER_CC) $(CPPFLAGS) -Ibuild/generated $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/shared_surface.c -o build/analyzer/shared_surface.o
	$(ANALYZER_CC) $(CPPFLAGS) -Ibuild/generated $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/render_builder.c -o build/analyzer/render_builder.o
	$(ANALYZER_CC) $(CPPFLAGS) -Ibuild/generated $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/graphics_library.c -o build/analyzer/graphics_library.o
	$(ANALYZER_CC) $(CPPFLAGS) -Ibuild/generated $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/font_library.c -o build/analyzer/font_library.o

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d build/m68k/*/*.d)

.PHONY: all graphics-library font-library renderer pic test sanitize analyze \
	library-contract clean shared-libraries-ready
