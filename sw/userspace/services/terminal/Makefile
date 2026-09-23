include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
HOST_CC ?= cc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
READELF := $(CROSS)readelf
RUNTIME := ../../runtime
include $(RUNTIME)/astra-link.mk
include ../../libraries.mk
STORAGE := ../../storage
VFS := ../../vfs
STREAMS := ../../streams
TERMINAL := ../../terminal
SUPERVISOR := ../../supervisor
GRAPHICS := ../../graphics
CONFIG := ../../config
INTERFACE := ../../interface
NDK := ../../../../ndk
CPPFLAGS := -Iinclude -I$(NDK)/include -I../../../include -I$(RUNTIME)/include \
	-I$(STORAGE)/include \
	-I$(VFS)/include \
	-I$(STREAMS)/include \
	-I$(GRAPHICS)/include -I$(CONFIG)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC $(ASTRA_DEBUG_FLAGS)
TARGET := build/m68k/terminal.elf
IMAGE := build/m68k/terminal
CATALOG := build/m68k/astra_events.cat
OBJECTS := build/m68k/main.o build/m68k/console_session.o \
	build/m68k/console_stream.o build/m68k/terminal_cursor.o \
	build/m68k/terminal_scrollback.o
HOST_TEST := build/host/test_terminal_cursor build/host/test_terminal_scrollback
HOST_TEST_SANITIZE := build/host/test_terminal_cursor_sanitize \
	build/host/test_terminal_scrollback_sanitize
TERMINAL_LIB := $(TERMINAL)/build/m68k/libastraterminal.a
LIBRARIES := $(ASTRA_INTERFACE_LIBRARY) $(ASTRA_GRAPHICS_LIBRARY) \
	$(ASTRA_STREAMS_LIBRARY) $(ASTRA_FILESYSTEM_LIBRARY) \
	$(ASTRA_SYSTEM_LIBRARY) $(ASTRA_RUNTIME_LIBRARY) \
	$(ASTRA_COMPILER_LIBRARY)
SUPERVISOR_CATALOG := $(SUPERVISOR)/build/m68k/astra_events.cat
EVENT_BASE_LD := build/m68k/event_base.ld
CATALOG_TOOL := ../../../../tools/system_event_catalog.py
ASTRA_PROGRAM_TARGETS := $(IMAGE) catalog-contract size
ASTRA_PROGRAM_PRIVATE_INPUTS := $(TERMINAL_LIB)
ASTRA_PROGRAM_PRIVATE_OWNER_TARGETS := $(TERMINAL):all $(SUPERVISOR):all
ASTRA_PROGRAM_LINK_PREREQUISITES := $(EVENT_BASE_LD)
ASTRA_PROGRAM_LINK_SCRIPT_FLAGS := -T $(EVENT_BASE_LD)
include ../../dynamic-program.mk

$(SUPERVISOR_CATALOG):
	$(MAKE) -C $(SUPERVISOR) all

$(CATALOG): $(TARGET)
	$(OBJCOPY) -O binary --only-section=.astra_events \
		--set-section-flags .astra_events=alloc,load,contents $< $@

$(EVENT_BASE_LD): $(SUPERVISOR_CATALOG) $(CATALOG_TOOL)
	@mkdir -p $(@D)
	python3 $(CATALOG_TOOL) base $(SUPERVISOR_CATALOG) > $@

catalog-contract: $(CATALOG)
	python3 ../../../../tools/event_catalog.py $(TARGET) >/dev/null

build/m68k/main.o: main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/m68k/console_session.o: console_session.c include/console_session.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/m68k/console_stream.o: console_stream.c include/console_stream.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/m68k/terminal_cursor.o: terminal_cursor.c include/terminal_cursor.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/m68k/terminal_scrollback.o: terminal_scrollback.c \
		include/terminal_scrollback.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/host/test_terminal_cursor: terminal_cursor.c include/terminal_cursor.h \
		tests/test_terminal_cursor.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O2 terminal_cursor.c \
		tests/test_terminal_cursor.c -o $@

test: $(HOST_TEST)
	./build/host/test_terminal_cursor
	./build/host/test_terminal_scrollback

build/host/test_terminal_scrollback: terminal_scrollback.c \
		include/terminal_scrollback.h tests/test_terminal_scrollback.c \
		$(INTERFACE)/src/scroll.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O2 terminal_scrollback.c \
		$(INTERFACE)/src/scroll.c tests/test_terminal_scrollback.c \
		-o $@

build/host/test_terminal_cursor_sanitize: terminal_cursor.c \
		include/terminal_cursor.h \
		tests/test_terminal_cursor.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O1 \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		terminal_cursor.c tests/test_terminal_cursor.c -o $@

sanitize: $(HOST_TEST_SANITIZE)
	./build/host/test_terminal_cursor_sanitize
	./build/host/test_terminal_scrollback_sanitize

build/host/test_terminal_scrollback_sanitize: terminal_scrollback.c \
		include/terminal_scrollback.h tests/test_terminal_scrollback.c \
		$(INTERFACE)/src/scroll.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O1 \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		terminal_scrollback.c $(INTERFACE)/src/scroll.c \
		tests/test_terminal_scrollback.c -o $@

analyze:
	@mkdir -p build/analyzer
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c main.c -o build/analyzer/main.o
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c console_session.c -o build/analyzer/console_session.o
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c console_stream.c -o build/analyzer/console_stream.o
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c terminal_cursor.c -o build/analyzer/terminal_cursor.o
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c terminal_scrollback.c -o build/analyzer/terminal_scrollback.o

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)

.PHONY: all catalog-contract size test sanitize analyze clean
