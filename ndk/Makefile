include ../mk/m68k-cross.mk
include ../mk/astra-version.mk
CC := $(CROSS)gcc
AR := $(CROSS)ar
HOST_CC ?= cc
HOST_CXX ?= c++
ANALYZER_CC ?= gcc
CPU_FLAGS ?= -m68040 -msoft-float

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
	src/datetime.c src/display_mode.c src/keymap.c src/utf8.c src/clipboard.c
TARGET_OBJECTS := $(patsubst src/%.c,build/m68k/%.o,$(SOURCES))
PIC_OBJECTS := $(patsubst build/m68k/%,build/m68k/pic/%,$(TARGET_OBJECTS))
HOST_TESTS := build/host/test_front_panel build/host/test_font \
	build/host/test_graphics build/host/test_port \
	build/host/test_area build/host/test_bulk_ring
HOST_TESTS += build/host/test_pointer build/host/test_window
HOST_TESTS += build/host/test_application
HOST_TESTS += build/host/test_fixed build/host/test_bundle
HOST_TESTS += build/host/test_utf8
HOST_TESTS += build/host/test_clipboard

SANITIZER_TESTS := build/host/test_front_panel_sanitize \
	build/host/test_font_sanitize build/host/test_graphics_sanitize \
	build/host/test_port_sanitize build/host/test_area_sanitize \
	build/host/test_bulk_ring_sanitize
SANITIZER_TESTS += build/host/test_pointer_sanitize \
	build/host/test_window_sanitize build/host/test_fixed_sanitize \
	build/host/test_bundle_sanitize
SANITIZER_TESTS += build/host/test_application_sanitize
SANITIZER_TESTS += build/host/test_utf8_sanitize
SANITIZER_TESTS += build/host/test_clipboard_sanitize
EXAMPLE_OBJECTS := build/m68k/front_panel_example.o \
	build/m68k/font_layout_example.o build/m68k/graphics_frame_example.o \
	build/m68k/port_message_example.o build/m68k/bulk_ring_example.o \
	build/m68k/interface_controls_example.o \
	build/m68k/text_clipboard_example.o build/m68k/text_field_example.o \
	build/m68k/undo_example.o
ANALYZER_OBJECTS := $(patsubst src/%.c,build/analyzer/%.o,$(SOURCES))
DIST_NAME := astra68-ndk-$(ASTRA_OS_VERSION)
DIST_DIR := build/dist
DIST_STAGE := $(DIST_DIR)/stage/$(DIST_NAME)
DIST_ARCHIVE := $(DIST_DIR)/$(DIST_NAME).tar.xz
DOC_STAMP := build/docs/.complete
DOC_INPUTS := $(shell find docs -type f) $(wildcard include/astra/*.h examples/*.c) \
	../sw/include/astra/gui.h ../sw/include/astra/message_abi.h
OS_COMPONENTS := runtime graphics vfs streams posix network events config \
	messaging interface

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

build/host/test_font: src/font.c src/utf8.c tests/test_font.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $^ -o $@

build/host/test_graphics: src/graphics.c src/display_mode.c tests/test_graphics.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $^ -o $@

build/host/test_window: src/resource.c src/port.c src/window.c src/utf8.c \
		tests/test_window.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $^ -o $@

build/host/test_pointer: src/resource.c src/port.c src/pointer.c \
		tests/test_pointer.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $^ -o $@

build/host/test_application: src/resource.c src/port.c src/application.c \
		src/utf8.c ../sw/userspace/runtime/src/arguments.c \
		tests/test_application.c
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

build/host/test_bundle: src/manifest.c src/bundle.c src/utf8.c \
		tests/test_bundle.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $^ -o $@

build/host/test_utf8: src/utf8.c tests/test_utf8.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $^ -o $@

build/host/utf8.o: src/utf8.c include/astra/utf8.h
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) -c $< -o $@

build/host/test_clipboard: src/clipboard.c src/resource.c src/area.c \
		src/port.c src/utf8.c tests/test_clipboard.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $^ -o $@

test: $(HOST_TESTS) header-contract
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
	./build/host/test_utf8
	./build/host/test_clipboard

build/host/test_front_panel_sanitize: src/front_panel.c tests/test_front_panel.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(SANITIZER_FLAGS) $^ -o $@

build/host/test_font_sanitize: src/font.c src/utf8.c tests/test_font.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(SANITIZER_FLAGS) $^ -o $@

build/host/test_graphics_sanitize: src/graphics.c src/display_mode.c \
		tests/test_graphics.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(SANITIZER_FLAGS) $^ -o $@

build/host/test_window_sanitize: src/resource.c src/port.c src/window.c \
		src/utf8.c \
		tests/test_window.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(SANITIZER_FLAGS) $^ -o $@

build/host/test_pointer_sanitize: src/resource.c src/port.c src/pointer.c \
		tests/test_pointer.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(SANITIZER_FLAGS) $^ -o $@

build/host/test_application_sanitize: src/resource.c src/port.c \
		src/application.c src/utf8.c ../sw/userspace/runtime/src/arguments.c \
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

build/host/test_bundle_sanitize: src/manifest.c src/bundle.c src/utf8.c \
		tests/test_bundle.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(SANITIZER_FLAGS) $^ -o $@

build/host/test_utf8_sanitize: src/utf8.c tests/test_utf8.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(SANITIZER_FLAGS) $^ -o $@

build/host/test_clipboard_sanitize: src/clipboard.c src/resource.c src/area.c \
		src/port.c src/utf8.c tests/test_clipboard.c
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
	./build/host/test_utf8_sanitize
	./build/host/test_clipboard_sanitize

check: test sanitize

build/analyzer/%.o: src/%.c
	@mkdir -p $(@D)
	$(ANALYZER_CC) $(HOST_CFLAGS) -fanalyzer -c $< -o $@

analyze: $(ANALYZER_OBJECTS)

build/m68k/%_example.o: examples/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(TARGET_CFLAGS) -c $< -o $@

example: $(LIB) $(EXAMPLE_OBJECTS)

$(DOC_STAMP): $(DOC_INPUTS)
	sh docs/build.sh all
	@test -f build/docs/html/index.html
	@test -f build/docs/astra68-ndk.pdf
	@touch $@

docs: $(DOC_STAMP)

docs-html:
	sh docs/build.sh html

docs-pdf:
	sh docs/build.sh pdf

docs-check:
	sh docs/build.sh check
	@touch $(DOC_STAMP)

docs-clean:
	rm -rf build/docs

developer-components: all pic
	$(MAKE) -C ../sw/userspace/runtime all pic
	$(MAKE) -C ../sw/userspace/graphics all pic
	$(MAKE) -C ../sw/userspace/vfs all
	$(MAKE) -C ../sw/userspace/streams all
	$(MAKE) -C ../sw/userspace/posix all
	$(MAKE) -C ../sw/userspace/network all
	$(MAKE) -C ../sw/userspace/events all

$(DIST_ARCHIVE): developer-components $(DOC_STAMP)
	rm -rf $(DIST_STAGE)
	install -d $(DIST_STAGE)/include/posix $(DIST_STAGE)/lib/m68040 \
		$(DIST_STAGE)/make $(DIST_STAGE)/src $(DIST_STAGE)/examples \
		$(DIST_STAGE)/docs
	$(MAKE) install-headers DESTDIR=$(abspath $(DIST_STAGE)) PREFIX=
	cp -R ../sw/userspace/posix/include/. $(DIST_STAGE)/include/posix/
	install -m 0644 README.md $(DIST_STAGE)/README.md
	install -m 0644 make/*.mk $(DIST_STAGE)/make/
	install -m 0644 ../sw/userspace/posix/kit/program.c \
		$(DIST_STAGE)/src/astra-posix-program.c
	install -m 0644 examples/*.c $(DIST_STAGE)/examples/
	install -m 0644 ../sw/userspace/runtime/astra_library.ld \
		../sw/userspace/runtime/astra_user.ld \
		../sw/userspace/runtime/build/m68k/crt0.o \
		../sw/userspace/runtime/build/m68k/crt0-hosted.o \
		build/m68k/libastra.a build/m68k/libastra-pic.a \
		../sw/userspace/runtime/build/m68k/libastrart.a \
		../sw/userspace/runtime/build/m68k/libastrart-pic.a \
		../sw/userspace/graphics/build/m68k/libastragraphics.a \
		../sw/userspace/graphics/build/m68k/libastragraphics-pic.a \
		../sw/userspace/vfs/build/m68k/libastravfs.a \
		../sw/userspace/streams/build/m68k/libastrastreams.a \
		../sw/userspace/posix/build/m68k/libastraposix.a \
		../sw/userspace/network/build/m68k/libastranetwork.a \
		../sw/userspace/events/build/m68k/libastraevents.a \
		$(DIST_STAGE)/lib/m68040/
	cp -R build/docs/html $(DIST_STAGE)/docs/html
	install -m 0644 build/docs/astra68-ndk.pdf $(DIST_STAGE)/docs/
	printf '%s\n' 'name=Astra 68 Native Developer Kit' \
		'version=$(ASTRA_OS_VERSION)' \
		'astra_os_version=$(ASTRA_OS_VERSION)' 'target=m68k-astra' \
		'cpu=mc68040' > $(DIST_STAGE)/METADATA
	cd $(DIST_STAGE) && find . -type f ! -name SHA256SUMS \
		-printf '%P\0' | LC_ALL=C sort -z | xargs -0 sha256sum \
		> SHA256SUMS
	@mkdir -p $(DIST_DIR)
	tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
		-cJf $(DIST_ARCHIVE).tmp -C $(DIST_DIR)/stage $(DIST_NAME)
	mv $(DIST_ARCHIVE).tmp $(DIST_ARCHIVE)

dist-check: $(DIST_ARCHIVE)
	python3 tests/test_distribution.py $(DIST_ARCHIVE) $(ASTRA_OS_VERSION)
	rm -rf $(DIST_DIR)/check
	install -d $(DIST_DIR)/check
	tar -xJf $(DIST_ARCHIVE) -C $(DIST_DIR)/check
	$(MAKE) -f tests/distribution_smoke.mk \
		ASTRA_NDK_ROOT=$(abspath $(DIST_DIR)/check/$(DIST_NAME)) \
		BUILD_DIR=$(abspath $(DIST_DIR)/smoke) all

dist: dist-check
	@echo "NDK: $(DIST_ARCHIVE)"

os-library-tests:
	@set -e; for component in $(OS_COMPONENTS); do \
		$(MAKE) -C ../sw/userspace/$$component all test sanitize analyze; \
	done

certify: check analyze example dist-check os-library-tests

sdk: certify

header-contract:
	rm -rf build/header-contract
	$(MAKE) install-headers DESTDIR=$(abspath build/header-contract) PREFIX=
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -pedantic \
		-Ibuild/header-contract/include -fsyntax-only tests/headers.c
	$(HOST_CXX) -std=c++17 -Wall -Wextra -Werror -pedantic \
		-Ibuild/header-contract/include -fsyntax-only tests/headers.cpp

install-headers:
	install -d $(DESTDIR)$(PREFIX)/include/astra $(DESTDIR)$(PREFIX)/lib
	install -m 0644 include/astra/*.h $(DESTDIR)$(PREFIX)/include/astra/
	install -m 0644 ../sw/include/astra/*.h $(DESTDIR)$(PREFIX)/include/astra/

install: $(LIB) install-headers
	install -m 0644 $(LIB) $(DESTDIR)$(PREFIX)/lib/

clean:
	rm -rf build

-include $(TARGET_OBJECTS:.o=.d) $(PIC_OBJECTS:.o=.d)

.PHONY: all pic test sanitize check analyze example docs docs-html docs-pdf \
	docs-check docs-clean developer-components dist dist-check \
	os-library-tests certify sdk header-contract install-headers install clean
