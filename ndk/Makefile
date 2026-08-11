include ../mk/m68k-cross.mk
CC := $(CROSS)gcc
AR := $(CROSS)ar
HOST_CC ?= cc
ANALYZER_CC ?= gcc
CPU_FLAGS ?= -m68030 -msoft-float

CPPFLAGS := -Iinclude -I../sw/include
TARGET_CFLAGS := $(CPU_FLAGS) -Os -ffreestanding -fno-builtin -std=c11 \
	-Wall -Wextra -Werror
HOST_CFLAGS := -O2 -std=c11 -Wall -Wextra -Werror -DASTRA_NDK_TEST \
	$(CPPFLAGS) -Isrc/internal
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

LIB := build/m68k/libastra.a
TARGET_OBJECTS := build/m68k/resource.o build/m68k/area.o \
	build/m68k/port.o build/m68k/bulk_ring.o \
	build/m68k/front_panel.o build/m68k/font.o build/m68k/graphics.o
TARGET_OBJECTS += build/m68k/pointer.o build/m68k/window.o
HOST_TESTS := build/host/test_front_panel build/host/test_font \
	build/host/test_graphics build/host/test_port \
	build/host/test_area build/host/test_bulk_ring
HOST_TESTS += build/host/test_pointer build/host/test_window
SANITIZER_TESTS := build/host/test_front_panel_sanitize \
	build/host/test_font_sanitize build/host/test_graphics_sanitize \
	build/host/test_port_sanitize build/host/test_area_sanitize \
	build/host/test_bulk_ring_sanitize
SANITIZER_TESTS += build/host/test_pointer_sanitize \
	build/host/test_window_sanitize
EXAMPLE_OBJECTS := build/m68k/front_panel_example.o \
	build/m68k/font_layout_example.o build/m68k/graphics_frame_example.o \
	build/m68k/port_message_example.o build/m68k/bulk_ring_example.o
ANALYZER_OBJECTS := build/analyzer/resource.o build/analyzer/area.o \
	build/analyzer/port.o build/analyzer/bulk_ring.o \
	build/analyzer/front_panel.o build/analyzer/font.o \
	build/analyzer/graphics.o
ANALYZER_OBJECTS += build/analyzer/pointer.o build/analyzer/window.o

all: $(LIB)

$(LIB): $(TARGET_OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

build/m68k/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(TARGET_CFLAGS) -MMD -MP -c $< -o $@

build/host/test_front_panel: src/front_panel.c tests/test_front_panel.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $^ -o $@

build/host/test_font: src/font.c tests/test_font.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $^ -o $@

build/host/test_graphics: src/graphics.c tests/test_graphics.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $^ -o $@

build/host/test_window: src/resource.c src/port.c src/window.c \
		tests/test_window.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $^ -o $@

build/host/test_pointer: src/resource.c src/port.c src/pointer.c \
		tests/test_pointer.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $^ -o $@

build/host/test_port: src/resource.c src/port.c tests/test_port.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $^ -o $@

build/host/test_area: src/resource.c src/area.c tests/test_area.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $^ -o $@

build/host/test_bulk_ring: src/resource.c src/bulk_ring.c \
		tests/test_bulk_ring.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $^ -o $@

test: $(HOST_TESTS)
	./build/host/test_front_panel
	./build/host/test_font
	./build/host/test_graphics
	./build/host/test_port
	./build/host/test_area
	./build/host/test_bulk_ring
	./build/host/test_pointer
	./build/host/test_window

build/host/test_front_panel_sanitize: src/front_panel.c tests/test_front_panel.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(SANITIZER_FLAGS) $^ -o $@

build/host/test_font_sanitize: src/font.c tests/test_font.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(SANITIZER_FLAGS) $^ -o $@

build/host/test_graphics_sanitize: src/graphics.c tests/test_graphics.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(SANITIZER_FLAGS) $^ -o $@

build/host/test_window_sanitize: src/resource.c src/port.c src/window.c \
		tests/test_window.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(SANITIZER_FLAGS) $^ -o $@

build/host/test_pointer_sanitize: src/resource.c src/port.c src/pointer.c \
		tests/test_pointer.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(SANITIZER_FLAGS) $^ -o $@

build/host/test_port_sanitize: src/resource.c src/port.c tests/test_port.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(SANITIZER_FLAGS) $^ -o $@

build/host/test_area_sanitize: src/resource.c src/area.c tests/test_area.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(SANITIZER_FLAGS) $^ -o $@

build/host/test_bulk_ring_sanitize: src/resource.c src/bulk_ring.c \
		tests/test_bulk_ring.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(SANITIZER_FLAGS) $^ -o $@

sanitize: $(SANITIZER_TESTS)
	./build/host/test_front_panel_sanitize
	./build/host/test_font_sanitize
	./build/host/test_graphics_sanitize
	./build/host/test_port_sanitize
	./build/host/test_area_sanitize
	./build/host/test_bulk_ring_sanitize
	./build/host/test_pointer_sanitize
	./build/host/test_window_sanitize

check: test sanitize

build/analyzer/%.o: src/%.c
	@mkdir -p $(@D)
	$(ANALYZER_CC) $(HOST_CFLAGS) -fanalyzer -c $< -o $@

analyze: $(ANALYZER_OBJECTS)

build/m68k/%_example.o: examples/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(TARGET_CFLAGS) -c $< -o $@

example: $(LIB) $(EXAMPLE_OBJECTS)

docs:
	sh docs/build.sh all

docs-html:
	sh docs/build.sh html

docs-pdf:
	sh docs/build.sh pdf

docs-check:
	sh docs/build.sh check

docs-clean:
	rm -rf build/docs

sdk: all check analyze example docs

install: $(LIB)
	install -d $(DESTDIR)$(PREFIX)/include/astra $(DESTDIR)$(PREFIX)/lib
	install -m 0644 include/astra/*.h $(DESTDIR)$(PREFIX)/include/astra/
	install -m 0644 $(LIB) $(DESTDIR)$(PREFIX)/lib/

clean:
	rm -rf build

-include $(TARGET_OBJECTS:.o=.d)

.PHONY: all test sanitize check analyze example docs docs-html docs-pdf \
	docs-check docs-clean sdk install clean
