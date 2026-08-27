include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
RUNTIME := ../../runtime
STORAGE := ../../storage
VFS := ../../vfs
STREAMS := ../../streams
SHELL_DIR := ../../shell
TERMINAL := ../../terminal
SUPERVISOR := ../../supervisor
GRAPHICS := ../../graphics
CONFIG := ../../config
NDK := ../../../../ndk
CPPFLAGS := -Iinclude -I$(NDK)/include -I../../../include -I$(RUNTIME)/include \
	-I$(STORAGE)/include \
	-I$(VFS)/include \
	-I$(STREAMS)/include -I$(SHELL_DIR)/include -I$(TERMINAL)/include \
	-I$(GRAPHICS)/include -I$(CONFIG)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
LINK_FLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections
TARGET := build/m68k/terminal.elf
IMAGE := build/m68k/terminal
CATALOG := build/m68k/astra_events.cat
OBJECTS := build/m68k/main.o build/m68k/console_shell.o \
	build/m68k/console_stream.o build/m68k/config_document.o
RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart.a
VFS_LIB := $(VFS)/build/m68k/libastravfs.a
STREAMS_LIB := $(STREAMS)/build/m68k/libastrastreams.a
SHELL_LIB := $(SHELL_DIR)/build/m68k/libastrashell.a
TERMINAL_LIB := $(TERMINAL)/build/m68k/libastraterminal.a
NDK_LIB := $(NDK)/build/m68k/libastra.a
CRT0 := $(RUNTIME)/build/m68k/crt0.o
SUPERVISOR_CATALOG := $(SUPERVISOR)/build/m68k/astra_events.cat
EVENT_BASE_LD := build/m68k/event_base.ld
CATALOG_TOOL := ../../../../tools/system_event_catalog.py
ASTRA_PROGRAM_OWNER_DIRS := $(RUNTIME) $(VFS) $(STREAMS) $(SHELL_DIR) \
	$(TERMINAL) $(NDK) $(SUPERVISOR)
ASTRA_PROGRAM_TARGETS := $(IMAGE) catalog-contract size
include ../../program.mk

$(TARGET): $(OBJECTS) $(EVENT_BASE_LD) $(CRT0) $(STREAMS_LIB) $(VFS_LIB) $(TERMINAL_LIB) \
		$(SHELL_LIB) $(NDK_LIB) $(RUNTIME_LIB) \
		$(RUNTIME)/astra_user.ld
	@mkdir -p $(@D)
	$(CC) $(TARGET_FLAGS) $(LINK_FLAGS) -T $(EVENT_BASE_LD) \
		-T $(RUNTIME)/astra_user.ld -o $@ $(CRT0) $(OBJECTS) \
		$(STREAMS_LIB) $(VFS_LIB) $(TERMINAL_LIB) $(SHELL_LIB) \
		$(NDK_LIB) $(RUNTIME_LIB) -lgcc

$(NDK_LIB):
	$(MAKE) -C $(NDK) all

$(SUPERVISOR_CATALOG):
	$(MAKE) -C $(SUPERVISOR) all

$(IMAGE): $(TARGET)
	$(OBJCOPY) --strip-all --remove-section=.astra_events $< $@

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

build/m68k/console_shell.o: console_shell.c include/console_shell.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/m68k/console_stream.o: console_stream.c include/console_stream.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/m68k/config_document.o: $(CONFIG)/src/config_document.c \
		$(CONFIG)/include/astra/config_document.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

size: $(IMAGE)
	$(SIZE) $(IMAGE)

test sanitize:
	@true

analyze:
	@mkdir -p build/analyzer
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c main.c -o build/analyzer/main.o
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c console_shell.c -o build/analyzer/console_shell.o
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c console_stream.c -o build/analyzer/console_stream.o

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)

.PHONY: all catalog-contract size test sanitize analyze clean
