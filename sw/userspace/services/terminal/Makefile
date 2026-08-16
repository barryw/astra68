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
NDK := ../../../../ndk
CPPFLAGS := -I$(NDK)/include -I../../../include -I$(RUNTIME)/include \
	-I$(STORAGE)/include \
	-I$(VFS)/include \
	-I$(STREAMS)/include -I$(SHELL_DIR)/include -I$(TERMINAL)/include \
	-I$(SUPERVISOR)/include -I$(GRAPHICS)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
LINK_FLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections -T $(RUNTIME)/astra_user.ld
TARGET := build/m68k/terminal.elf
IMAGE := build/m68k/terminal
CATALOG := build/m68k/astra_events.cat
OBJECTS := build/m68k/main.o build/m68k/console_shell.o \
	build/m68k/console_stream.o
RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart.a
VFS_LIB := $(VFS)/build/m68k/libastravfs.a
STREAMS_LIB := $(STREAMS)/build/m68k/libastrastreams.a
SHELL_LIB := $(SHELL_DIR)/build/m68k/libastrashell.a
TERMINAL_LIB := $(TERMINAL)/build/m68k/libastraterminal.a
NDK_LIB := $(NDK)/build/m68k/libastra.a
CRT0 := $(RUNTIME)/build/m68k/crt0.o
SUPERVISOR_CATALOG := $(SUPERVISOR)/build/m68k/astra_events.cat
EVENT_PREFIX_BYTES := 768

all: $(IMAGE) catalog-contract size

$(TARGET): $(OBJECTS) $(CRT0) $(STREAMS_LIB) $(VFS_LIB) $(TERMINAL_LIB) \
		$(SHELL_LIB) $(NDK_LIB) $(RUNTIME_LIB) \
		$(RUNTIME)/astra_user.ld
	@mkdir -p $(@D)
	$(CC) $(TARGET_FLAGS) $(LINK_FLAGS) -o $@ $(CRT0) $(OBJECTS) \
		$(STREAMS_LIB) $(VFS_LIB) $(TERMINAL_LIB) $(SHELL_LIB) \
		$(NDK_LIB) $(RUNTIME_LIB) -lgcc

$(NDK_LIB):
	$(MAKE) -C $(NDK) all

$(IMAGE): $(TARGET)
	$(OBJCOPY) --strip-all --remove-section=.astra_events $< $@

$(CATALOG): $(TARGET)
	$(OBJCOPY) -O binary --only-section=.astra_events \
		--set-section-flags .astra_events=alloc,load,contents $< $@

catalog-contract: $(CATALOG) $(SUPERVISOR_CATALOG)
	@test "$$(wc -c < $(CATALOG))" = "$$(wc -c < $(SUPERVISOR_CATALOG))"
	@cmp -s -i $(EVENT_PREFIX_BYTES):$(EVENT_PREFIX_BYTES) \
		$(CATALOG) $(SUPERVISOR_CATALOG)

build/m68k/main.o: main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/m68k/console_shell.o: $(SUPERVISOR)/src/console_shell.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) \
		-ffile-prefix-map=$(SUPERVISOR)/= -MMD -MP -c $< -o $@

build/m68k/console_stream.o: $(SUPERVISOR)/src/console_stream.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

size: $(IMAGE)
	$(SIZE) $(IMAGE)

test sanitize analyze:
	@true

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)

.PHONY: all catalog-contract size test sanitize analyze clean
