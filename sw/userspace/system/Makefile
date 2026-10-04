include ../../../mk/m68k-cross.mk
include ../../../mk/astra-shared-library.mk

# system.library: the client side of the kernel objects and system services
# every program uses -- handles, areas, ports, bulk rings, launch, service
# management, bundles, configuration scopes, windows, the pointer, managed
# graphics, the clipboard, keymaps and the date. Its public headers are in
# ndk/include/astra; this directory is the implementation.
# libastra.a is the same code for the static bootstrap images (Supervisor).

CC := $(CROSS)gcc
AR := $(CROSS)ar
READELF := $(CROSS)readelf
HOST_CC ?= cc
ANALYZER_CC ?= gcc
CPU_FLAGS ?= -m68040
NDK := ../../../ndk
RUNTIME := ../runtime
ASTRA_COMMON := ../../common

CPPFLAGS := -I$(NDK)/include -I../../include -I$(RUNTIME)/include
TARGET_CFLAGS := $(CPU_FLAGS) -Os -ffreestanding -fno-builtin -std=c11 \
	-Wall -Wextra -Werror
PIC_CFLAGS := $(TARGET_CFLAGS) -ffunction-sections -fdata-sections -fPIC \
	-fvisibility=hidden
HOST_CFLAGS := -O2 -std=c11 -Wall -Wextra -Werror -DASTRA_SYSTEM_TEST \
	$(CPPFLAGS) -Isrc/internal
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

LIB := build/m68k/libastra.a
PIC_LIB := build/m68k/libastra-pic.a
RUNTIME_LIBRARY := $(RUNTIME)/build/m68k/libraries/runtime.library.1
SYSTEM_LIBRARY := build/m68k/libraries/system.library.2
SYSTEM_IDENTITY := build/m68k/dynamic/system_library.o
SYSTEM_SOURCES := src/resource.c src/area.c src/port.c src/bulk_ring.c \
	src/application.c src/service_definition.c src/service_manager.c \
	src/manifest.c src/bundle.c src/config_scope.c src/window.c \
	src/pointer.c src/graphics.c src/display_mode.c src/clipboard.c \
	src/keymap.c src/datetime.c
SYSTEM_OBJECTS := $(patsubst src/%.c,build/m68k/dynamic/%.o,$(SYSTEM_SOURCES))
SYSTEM_MAP := system.library.exports
SYSTEM_HEADERS := $(addprefix $(NDK)/include/astra/,resource.h area.h \
	port.h bulk_ring.h application.h service_manager.h manifest.h bundle.h \
	config_library.h window.h pointer.h graphics.h clipboard.h keymap.h \
	datetime.h)
SYMBOL_OWNERSHIP_CHECK := ../../../tools/check_shared_symbol_ownership.py
# The static archive also carries UTF-8, which the shared library takes from
# runtime.library; static images link it from here or from libastrart.
SOURCES := $(SYSTEM_SOURCES) $(ASTRA_COMMON)/utf8.c
TARGET_OBJECTS := $(patsubst src/%.c,build/m68k/%.o,$(SYSTEM_SOURCES)) \
	build/m68k/utf8.o
PIC_OBJECTS := $(patsubst build/m68k/%,build/m68k/pic/%,$(TARGET_OBJECTS))
ANALYZER_OBJECTS := $(patsubst src/%.c,build/analyzer/%.o,\
	$(patsubst $(ASTRA_COMMON)/%.c,src/%.c,$(SOURCES)))

TEST_UTF8 := $(ASTRA_COMMON)/utf8.c
HOST_TESTS := application area bulk_ring bundle clipboard datetime graphics \
	pointer port service_manager utf8 window
SANITIZER_TESTS := application area bulk_ring bundle clipboard graphics \
	pointer port service_manager utf8 window

TEST_SOURCES_application := src/resource.c src/port.c src/application.c \
	$(TEST_UTF8) $(RUNTIME)/src/arguments.c
TEST_SOURCES_area := src/resource.c src/area.c
TEST_SOURCES_bulk_ring := src/resource.c src/bulk_ring.c
TEST_SOURCES_bundle := src/manifest.c src/bundle.c $(TEST_UTF8) \
	$(RUNTIME)/src/string.c
TEST_SOURCES_clipboard := src/clipboard.c src/resource.c src/area.c \
	src/port.c $(TEST_UTF8)
TEST_SOURCES_datetime := src/datetime.c $(ASTRA_COMMON)/civil.c \
	$(ASTRA_COMMON)/divide.c
TEST_SOURCES_graphics := src/graphics.c src/display_mode.c src/resource.c \
	src/area.c src/port.c src/window.c $(TEST_UTF8)
TEST_SOURCES_pointer := src/resource.c src/port.c src/pointer.c
TEST_SOURCES_port := src/resource.c src/port.c
TEST_SOURCES_service_manager := src/resource.c src/area.c src/port.c \
	src/service_definition.c src/service_manager.c $(TEST_UTF8)
TEST_SOURCES_utf8 := $(TEST_UTF8)
TEST_SOURCES_window := src/resource.c src/area.c src/port.c src/window.c \
	$(TEST_UTF8)

all: $(LIB) system-library-contract

library: $(SYSTEM_LIBRARY)

pic: $(PIC_LIB)

$(LIB): $(TARGET_OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

$(PIC_LIB): $(PIC_OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

build/m68k/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(TARGET_CFLAGS) -MMD -MP -c $< -o $@

build/m68k/utf8.o: $(ASTRA_COMMON)/utf8.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(TARGET_CFLAGS) -MMD -MP -c $< -o $@

build/m68k/pic/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(PIC_CFLAGS) -MMD -MP -c $< -o $@

build/m68k/pic/utf8.o: $(ASTRA_COMMON)/utf8.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(PIC_CFLAGS) -MMD -MP -c $< -o $@

build/m68k/dynamic/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(TARGET_CFLAGS) $(ASTRA_SHARED_PIC_FLAGS) -MMD -MP \
		-c $< -o $@

build/host/utf8.o: $(ASTRA_COMMON)/utf8.c $(NDK)/include/astra/utf8.h
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) -c $< -o $@

runtime-library-ready:
	$(MAKE) -C $(RUNTIME) library

$(SYSTEM_LIBRARY): $(SYSTEM_IDENTITY) $(SYSTEM_OBJECTS) $(SYSTEM_MAP) \
		$(ASTRA_SHARED_LINK_INPUTS) | runtime-library-ready
	@mkdir -p $(@D)
	$(CC) $(CPU_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,system.library.2 \
		-Wl,--version-script=$(SYSTEM_MAP) -o $@ \
		$(SYSTEM_IDENTITY) $(SYSTEM_OBJECTS) \
		-L$(dir $(RUNTIME_LIBRARY)) -Wl,--no-as-needed \
		-Wl,-l:runtime.library.1 $(ASTRA_SHARED_LINK_END)

system-library-contract: $(SYSTEM_LIBRARY)
	@python3 ../../../tools/check_shared_library_layout.py $(SYSTEM_LIBRARY)
	@python3 $(SYMBOL_OWNERSHIP_CHECK) --readelf $(READELF) \
		$(SYSTEM_LIBRARY) $(RUNTIME_LIBRARY)
	@python3 ../../../tools/check_library_exports.py $(SYSTEM_MAP) \
		$(SYSTEM_HEADERS)
	@python3 ../../../tools/check_elf_exports.py --readelf $(READELF) \
		$(SYSTEM_MAP) $(SYSTEM_LIBRARY)
	@python3 ../../../tools/library_info.py $< | \
		grep -q '^system.library.2 2.8.0 ABI 2.8$$'
	@$(READELF) -hW $< | grep -q 'Type:.*DYN'
	@$(READELF) -dW $< | grep -Fq 'Library soname: [system.library.2]'
	@test "$$($(READELF) -dW $< | grep -Fc 'Shared library: [runtime.library.1]')" = 1
	@test "$$($(READELF) -dW $< | awk '/NEEDED/{n++} END{print n+0}')" = 1
	@$(READELF) -dW $< | grep -q 'BIND_NOW'
	@! $(READELF) -dW $< | grep -q 'TEXTREL'
	@$(READELF) -lW $< | grep -q ' GNU_RELRO '
	@$(READELF) -VW $< | grep -q 'ASTRA_SYSTEM_2.8'
	@echo "astra system shared library: PASS"

define system_host_test
build/host/test_$(1): $$(TEST_SOURCES_$(1)) tests/test_$(1).c
	@mkdir -p $$(@D)
	$$(HOST_CC) $$(HOST_CFLAGS) $$^ -o $$@

build/host/test_$(1)_sanitize: $$(TEST_SOURCES_$(1)) tests/test_$(1).c
	@mkdir -p $$(@D)
	$$(HOST_CC) $$(HOST_CFLAGS) $$(SANITIZER_FLAGS) $$^ -o $$@
endef
$(foreach test,$(HOST_TESTS),$(eval $(call system_host_test,$(test))))

test: $(addprefix build/host/test_,$(HOST_TESTS))
	@set -e; for test in $^; do ./$$test; done

sanitize: $(addsuffix _sanitize,$(addprefix build/host/test_,$(SANITIZER_TESTS)))
	@set -e; for test in $^; do ./$$test; done

build/analyzer/%.o: src/%.c
	@mkdir -p $(@D)
	$(ANALYZER_CC) $(HOST_CFLAGS) -fanalyzer -c $< -o $@

build/analyzer/utf8.o: $(ASTRA_COMMON)/utf8.c
	@mkdir -p $(@D)
	$(ANALYZER_CC) $(HOST_CFLAGS) -fanalyzer -c $< -o $@

analyze: $(ANALYZER_OBJECTS)

clean:
	rm -rf build

-include $(TARGET_OBJECTS:.o=.d) $(PIC_OBJECTS:.o=.d) \
	$(SYSTEM_IDENTITY:.o=.d) $(SYSTEM_OBJECTS:.o=.d)

.PHONY: all library pic runtime-library-ready system-library-contract test \
	sanitize analyze clean
