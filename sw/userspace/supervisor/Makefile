include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
HOST_CC ?= cc
ANALYZER_CC ?= gcc
RUNTIME := ../runtime
STORAGE := ../storage
ALLOC := ../alloc
VFS := ../vfs
SHELL_DIR := ../shell
TERMINAL_DIR := ../terminal
LWEXT4 := ../../../third_party/lwext4
CPPFLAGS := -Iinclude -I../../include -I$(RUNTIME)/include \
	-I$(STORAGE)/include -I../metrics/include -I$(ALLOC)/include \
	-I$(SHELL_DIR)/include -I$(TERMINAL_DIR)/include -I$(VFS)/include
# The volume check speaks lwext4 as well as Astra and links freestanding, so it
# needs the vendored headers, the build profile, and the libc shims.
EXT4_CPPFLAGS := -I$(LWEXT4)/include -I$(STORAGE)/port/include \
	-DCONFIG_USE_DEFAULT_CFG=0 -I$(RUNTIME)/freestanding
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
HOST_FLAGS := -O2 -fno-builtin
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
# --gc-sections is what makes the -ffunction-sections every module already
# compiles with actually pay. Without it the sections were cut up and then all
# linked in regardless: lwext4 alone contributes paths this service never
# calls, and collecting them takes the image from 82,084 bytes of text to
# 70,304 with BSS unchanged. It is the initial user image, so that is ROM the
# firmware carries and pages the loader maps on every boot.
LINK_FLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections -T $(RUNTIME)/astra_user.ld
SOURCES := src/supervisor.c src/main.c src/volume.c src/console_shell.c \
	src/vfs_host.c
OBJECTS := build/m68k/supervisor.o build/m68k/main.o build/m68k/vfs_host.o \
	build/m68k/volume.o build/m68k/console_shell.o
RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart.a
STORAGE_LIB := $(STORAGE)/build/m68k/libastrablock.a
EXT4_LIB := $(STORAGE)/build/m68k/libastraext4.a
ALLOC_LIB := $(ALLOC)/build/m68k/libastraalloc.a
VFS_LIB := $(VFS)/build/m68k/libastravfs.a
SHELL_LIB := $(SHELL_DIR)/build/m68k/libastrashell.a
TERMINAL_LIB := $(TERMINAL_DIR)/build/m68k/libastraterminal.a
RUNTIME_CRT0 := $(RUNTIME)/build/m68k/crt0.o
TARGET := build/m68k/astra_supervisor.elf
# What the ROM carries. Identical to TARGET as far as the loader is concerned
# -- same segments, same entry -- with the debug information removed, because
# the payload is compressed into a fixed ROM window and DWARF has no business
# in it. TARGET keeps the symbols, and that is the file a debugger is pointed
# at.
IMAGE := build/m68k/astra_supervisor.image.elf
# The catalog the machine reads: the .astra_events section's bytes verbatim, so
# a lookup on the machine is (id - base) / 128 and never a parse. It is a build
# product and belongs on SYS:, not in git.
CATALOG := build/m68k/astra_events.cat
HOST_TEST := build/host/test_supervisor
# log.c comes along because validating a startup block is what binds the
# diagnostic channel, so the two cannot be linked apart.
TEST_SOURCES := src/supervisor.c $(RUNTIME)/src/startup.c \
	$(RUNTIME)/src/log.c tests/test_supervisor.c

# Two invocations on purpose. Make has no rule here for the libraries the sub-
# makes build, so it stats them once, before those sub-makes run, and compares
# the image against that stale stat. A library rebuilt in the same invocation
# therefore left the previous ELF in place — the stale-object trap wearing a
# different hat, and it costs an experiment every time it happens. A second
# invocation stats them afresh.
all:
	$(MAKE) libraries
	$(MAKE) image

image: $(IMAGE) $(CATALOG) size

libraries:
	$(MAKE) -C $(RUNTIME) all
	$(MAKE) -C $(ALLOC) all
	$(MAKE) -C $(STORAGE) $(STORAGE_LIB:$(STORAGE)/%=%)
	$(MAKE) -C $(STORAGE) $(EXT4_LIB:$(STORAGE)/%=%)
	$(MAKE) -C $(VFS) all
	$(MAKE) -C $(SHELL_DIR) all
	$(MAKE) -C $(TERMINAL_DIR) all

# The image is the deliverable, so the link contract is part of the build:
# astra_user.ld produces exactly the shape kernel_elf_accept() takes. The
# archives are named as prerequisites so a rebuilt library relinks the image.
$(TARGET): $(OBJECTS) $(RUNTIME)/astra_user.ld $(RUNTIME_CRT0) $(EXT4_LIB) \
		$(STORAGE_LIB) $(ALLOC_LIB) $(RUNTIME_LIB) $(SHELL_LIB) \
		$(TERMINAL_LIB) $(VFS_LIB)
	@mkdir -p $(@D)
	$(CC) $(TARGET_FLAGS) $(LINK_FLAGS) -o $@ $(RUNTIME_CRT0) $(OBJECTS) \
		$(VFS_LIB) $(EXT4_LIB) $(STORAGE_LIB) $(ALLOC_LIB) $(TERMINAL_LIB) \
		$(SHELL_LIB) $(RUNTIME_LIB) -lgcc

# The catalog section goes the way DWARF does: read out at build time, never
# carried in the window the firmware compresses into ROM.
$(IMAGE): $(TARGET)
	$(OBJCOPY) --strip-debug --remove-section=.astra_events $< $@

# --set-section-flags because the section is (INFO) in the linker script and
# objcopy's binary output emits allocatable sections only; without it this
# produces an empty file rather than an error, which is the kind of build step
# that goes unnoticed until a reader shows message ids instead of text.
$(CATALOG): $(TARGET)
	$(OBJCOPY) -O binary --only-section=.astra_events \
		--set-section-flags .astra_events=alloc,load,contents $< $@

build/m68k/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

# vfs_host binds lwext4 to the storage protocol, so it is the only file here
# besides volume.c that gets the vendored headers. console_shell.c deliberately
# does not: if a filesystem call reappears in the shell, it fails to compile.
build/m68k/vfs_host.o: src/vfs_host.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(EXT4_CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) \
		$(DEPFLAGS) -c $< -o $@

build/m68k/volume.o: src/volume.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(EXT4_CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) \
		$(DEPFLAGS) -c $< -o $@

$(HOST_TEST): $(TEST_SOURCES)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $^ -o $@

test: $(HOST_TEST)
	./$(HOST_TEST)

sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) $(TEST_SOURCES) \
		-o build/host/test_supervisor_sanitize
	./build/host/test_supervisor_sanitize

analyze:
	@mkdir -p build/analyzer
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/supervisor.c -o build/analyzer/supervisor.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/main.c -o build/analyzer/main.o

size: $(IMAGE)
	$(SIZE) $(IMAGE)

# Header dependencies: a stale object compiled against an older ABI
# constant is indistinguishable from a working one until it runs.
-include $(wildcard build/m68k/*.d build/m68k/*/*.d)

clean:
	rm -rf build

.PHONY: all image libraries test sanitize analyze size clean
