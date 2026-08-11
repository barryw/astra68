include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
HOST_CC ?= cc
ANALYZER_CC ?= gcc
CPPFLAGS := -I../../../ndk/include -Iinclude -I../runtime/include \
	-I../../include -I../../../fpga/arty/linux
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	$(ASTRA_DEBUG_FLAGS)
HOST_FLAGS := -O2
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
FONT_SOURCE := ../../../fpga/soc/post_fonts.hex
FONT_HEADER := build/generated/astra_font8x8.inc
UI_FONT_SOURCE := fonts/astra-workbench.afnt
UI_FONT_TOOL := ../../../tools/fonts/afnt.py
UI_FONT_HEADER := build/generated/astra_ui_font.inc
SOURCES := src/surface.c src/shared_surface.c src/render_builder.c
TARGET := build/m68k/libastragraphics.a
TARGET_OBJECTS := build/m68k/surface.o build/m68k/shared_surface.o \
	build/m68k/render_builder.o
HOST_TEST := build/host/test_surface

all: $(TARGET)

$(TARGET): $(TARGET_OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

$(FONT_HEADER): $(FONT_SOURCE)
	@mkdir -p $(@D)
	@{ printf 'static const uint8_t astra_font8x8[] = {\n'; \
	  awk '{ printf "0x%s,\n", $$1 }' $<; \
	  printf '};\n'; } > $@

$(UI_FONT_HEADER): $(UI_FONT_SOURCE) $(UI_FONT_TOOL)
	@mkdir -p $(@D)
	python3 $(UI_FONT_TOOL) emit-c $< $@

build/m68k/%.o: src/%.c include/astra/surface.h include/astra/window.h \
	../../include/astra/gui.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -Ibuild/generated $(COMMON_FLAGS) $(TARGET_FLAGS) \
		$(DEPFLAGS) -c $< -o $@

build/m68k/surface.o: $(FONT_HEADER) $(UI_FONT_HEADER)
build/m68k/render_builder.o: $(UI_FONT_HEADER) \
	../../../fpga/arty/linux/astra_render_protocol.h

$(HOST_TEST): src/surface.c src/render_builder.c tests/test_surface.c \
		include/astra/surface.h include/astra/render_builder.h \
		$(FONT_HEADER)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) -Ibuild/generated $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/surface.c src/render_builder.c \
		tests/test_surface.c -o $@

$(HOST_TEST): $(UI_FONT_HEADER)

test: $(HOST_TEST)
	./$(HOST_TEST)

sanitize: $(FONT_HEADER) $(UI_FONT_HEADER)
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) -Ibuild/generated $(COMMON_FLAGS) $(HOST_FLAGS) $(SANITIZER_FLAGS) \
		src/surface.c src/render_builder.c tests/test_surface.c \
		-o build/host/test_surface_sanitize
	./build/host/test_surface_sanitize

analyze: $(FONT_HEADER) $(UI_FONT_HEADER)
	@mkdir -p build/analyzer
	$(ANALYZER_CC) $(CPPFLAGS) -Ibuild/generated $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/surface.c -o build/analyzer/surface.o

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)

.PHONY: all test sanitize analyze clean
