include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
READELF := $(CROSS)readelf
HOST_CC ?= cc
ANALYZER_CC ?= gcc
CPPFLAGS := -I../../../ndk/include -Iinclude -I../runtime/include \
	-I../../include -I../../../fpga/arty/linux
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
HOST_FLAGS := -O2
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
FONT_SOURCE := ../../../fpga/soc/post_fonts.hex
FONT_HEADER := build/generated/astra_font8x8.inc
UI_FONT_SOURCE := fonts/astra-workbench.afnt
UI_FONT_TOOL := ../../../tools/fonts/afnt.py
UI_FONT_HEADER := build/generated/astra_ui_font.inc
MONO_FONT_SOURCE := fonts/astra-mono.afnt
MONO_FONT_HEADER := build/generated/astra_mono_font.inc
SOURCES := src/surface.c src/shared_surface.c src/render_builder.c
TARGET := build/m68k/libastragraphics.a
TARGET_OBJECTS := build/m68k/surface.o build/m68k/shared_surface.o \
	build/m68k/render_builder.o
PIC_TARGET := build/m68k/libastragraphics-pic.a
PIC_OBJECTS := build/m68k/pic/surface.o build/m68k/pic/shared_surface.o \
	build/m68k/pic/render_builder.o
HOST_TEST := build/host/test_surface
LIBRARY_LD := ../runtime/astra_library.ld
LIBRARY_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC -fvisibility=hidden
LIBRARY_LINK_FLAGS := -nostdlib -shared -Wl,-Bsymbolic \
	-Wl,--no-undefined -Wl,--build-id=none -Wl,--gc-sections \
	-Wl,--hash-style=sysv -Wl,-z,max-page-size=0x1000 -T $(LIBRARY_LD)
GRAPHICS_LIBRARY := build/m68k/libraries/graphics.library
FONT_LIBRARY := build/m68k/libraries/font.library
GRAPHICS_LIBRARY_OBJECTS := build/m68k/library/graphics_library.o \
	build/m68k/library/graphics_surface.o \
	build/m68k/library/graphics_shared_surface.o
FONT_LIBRARY_OBJECTS := build/m68k/library/font_library.o \
	build/m68k/library/font_surface.o
NDK_PIC_LIB := ../../../ndk/build/m68k/libastra-pic.a
RUNTIME_PIC_LIB := ../runtime/build/m68k/libastrart-pic.a

all: $(TARGET) $(GRAPHICS_LIBRARY) $(FONT_LIBRARY) library-contract

pic: $(PIC_TARGET)

$(TARGET): $(TARGET_OBJECTS)
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

build/m68k/%.o: src/%.c include/astra/surface.h \
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
		include/astra/graphics_library.h ../../include/astra/library.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) -c $< -o $@

build/m68k/library/font_library.o: src/font_library.c \
		include/astra/font_library.h ../../include/astra/library.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) -Ibuild/generated \
		-c $< -o $@

build/m68k/library/graphics_surface.o: src/surface.c $(FONT_HEADER) \
		$(UI_FONT_HEADER) $(MONO_FONT_HEADER)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -Ibuild/generated $(COMMON_FLAGS) $(LIBRARY_FLAGS) \
		-c $< -o $@

build/m68k/library/font_surface.o: src/surface.c $(FONT_HEADER) \
		$(UI_FONT_HEADER) $(MONO_FONT_HEADER)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -Ibuild/generated $(COMMON_FLAGS) $(LIBRARY_FLAGS) \
		-c $< -o $@

build/m68k/library/graphics_shared_surface.o: src/shared_surface.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) -c $< -o $@

$(NDK_PIC_LIB):
	$(MAKE) -C ../../../ndk pic

$(RUNTIME_PIC_LIB):
	$(MAKE) -C ../runtime pic

$(GRAPHICS_LIBRARY): $(GRAPHICS_LIBRARY_OBJECTS) $(NDK_PIC_LIB) \
		$(RUNTIME_PIC_LIB) $(LIBRARY_LD)
	@mkdir -p $(@D)
	$(CC) $(LIBRARY_FLAGS) $(LIBRARY_LINK_FLAGS) \
		-Wl,-soname,graphics.library.1 -o $@ \
		$(GRAPHICS_LIBRARY_OBJECTS) -Wl,--start-group $(NDK_PIC_LIB) \
		$(RUNTIME_PIC_LIB) -Wl,--end-group -lgcc

$(FONT_LIBRARY): $(FONT_LIBRARY_OBJECTS) $(RUNTIME_PIC_LIB) $(LIBRARY_LD)
	@mkdir -p $(@D)
	$(CC) $(LIBRARY_FLAGS) $(LIBRARY_LINK_FLAGS) \
		-Wl,-soname,font.library.1 -o $@ $(FONT_LIBRARY_OBJECTS) \
		$(RUNTIME_PIC_LIB) -lgcc

library-contract: $(GRAPHICS_LIBRARY) $(FONT_LIBRARY)
	@python3 ../../../tools/library_info.py $(GRAPHICS_LIBRARY) | \
		grep -q '^graphics.library 1.3.0 ABI 1.3$$'
	@python3 ../../../tools/library_info.py $(FONT_LIBRARY) | \
		grep -q '^font.library 1.0.0 ABI 1.0$$'
	@! $(READELF) -r $(GRAPHICS_LIBRARY) $(FONT_LIBRARY) | \
		grep -E 'R_68K_(GLOB_DAT|JMP_SLOT|32|PC32)'

$(HOST_TEST): src/surface.c src/render_builder.c tests/test_surface.c \
		include/astra/surface.h include/astra/render_builder.h \
		src/rounded.h $(FONT_HEADER)
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

.PHONY: all pic test sanitize analyze library-contract clean
