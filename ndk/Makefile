include ../mk/m68k-cross.mk
CC := $(CROSS)gcc
AR := $(CROSS)ar
HOST_CC ?= cc
ANALYZER_CC ?= gcc
CPU_FLAGS ?= -m68030 -msoft-float

CPPFLAGS := -Iinclude -I../sw/include -I../sw/userspace/runtime/include
TARGET_CFLAGS := $(CPU_FLAGS) -Os -ffreestanding -fno-builtin -std=c11 \
	-Wall -Wextra -Werror
PIC_CFLAGS := $(TARGET_CFLAGS) -ffunction-sections -fdata-sections -fPIC \
	-fvisibility=hidden
HOST_CFLAGS := -O2 -std=c11 -Wall -Wextra -Werror -DASTRA_NDK_TEST \
	$(CPPFLAGS) -Isrc/internal
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

LIB := build/m68k/libastra.a
PIC_LIB := build/m68k/libastra-pic.a
SOURCES := src/resource.c src/area.c src/port.c src/bulk_ring.c \
	src/front_panel.c src/font.c src/graphics.c src/pointer.c src/window.c \
	src/application.c src/fixed.c src/manifest.c src/bundle.c \
	src/datetime.c
TARGET_OBJECTS := $(patsubst src/%.c,build/m68k/%.o,$(SOURCES))
PIC_OBJECTS := $(patsubst build/m68k/%,build/m68k/pic/%,$(TARGET_OBJECTS))
HOST_TESTS := build/host/test_front_panel build/host/test_font \
	build/host/test_graphics build/host/test_port \
	build/host/test_area build/host/test_bulk_ring
HOST_TESTS += build/host/test_pointer build/host/test_window
HOST_TESTS += build/host/test_application
HOST_TESTS += build/host/test_fixed build/host/test_bundle

SANITIZER_TESTS := build/host/test_front_panel_sanitize \
	build/host/test_font_sanitize build/host/test_graphics_sanitize \
	build/host/test_port_sanitize build/host/test_area_sanitize \
	build/host/test_bulk_ring_sanitize
SANITIZER_TESTS += build/host/test_pointer_sanitize \
	build/host/test_window_sanitize build/host/test_fixed_sanitize \
	build/host/test_bundle_sanitize
SANITIZER_TESTS += build/host/test_application_sanitize
EXAMPLE_OBJECTS := build/m68k/front_panel_example.o \
	build/m68k/font_layout_example.o build/m68k/graphics_frame_example.o \
	build/m68k/port_message_example.o build/m68k/bulk_ring_example.o
ANALYZER_OBJECTS := $(patsubst src/%.c,build/analyzer/%.o,$(SOURCES))

all: $(LIB)

pic: $(PIC_LIB)

$(LIB): $(TARGET_OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

$(PIC_LIB): $(PIC_OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

build/m68k/pic/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(PIC_CFLAGS) -MMD -MP -c $< -o $@

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

build/host/test_application: src/resource.c src/port.c src/application.c \
		../sw/userspace/runtime/src/arguments.c tests/test_application.c
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

build/host/test_fixed: src/fixed.c tests/test_fixed.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $^ -o $@

build/host/test_bundle: src/manifest.c src/bundle.c tests/test_bundle.c
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
	./build/host/test_application
	./build/host/test_window
	./build/host/test_fixed
	./build/host/test_bundle

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

build/host/test_application_sanitize: src/resource.c src/port.c \
		src/application.c ../sw/userspace/runtime/src/arguments.c \
		tests/test_application.c
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

build/host/test_fixed_sanitize: src/fixed.c tests/test_fixed.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(SANITIZER_FLAGS) $^ -o $@

build/host/test_bundle_sanitize: src/manifest.c src/bundle.c tests/test_bundle.c
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
	./build/host/test_application_sanitize
	./build/host/test_window_sanitize
	./build/host/test_fixed_sanitize
	./build/host/test_bundle_sanitize

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
	install -m 0644 ../sw/include/astra/message_abi.h \
		$(DESTDIR)$(PREFIX)/include/astra/
	install -m 0644 $(LIB) $(DESTDIR)$(PREFIX)/lib/

clean:
	rm -rf build

-include $(TARGET_OBJECTS:.o=.d) $(PIC_OBJECTS:.o=.d)

.PHONY: all pic test sanitize check analyze example docs docs-html docs-pdf \
	docs-check docs-clean sdk install clean
