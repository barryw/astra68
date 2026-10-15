include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
HOST_CC ?= cc
ANALYZER_CC ?= gcc
RUNTIME := ../runtime
include $(RUNTIME)/astra-link.mk
VFS := ../vfs
CONFIG := ../config
NDK := ../../../ndk
SYSTEM := ../system
ASTRA_COMMON := ../../common
NDK_LIB := $(SYSTEM)/build/m68k/libastra.a
CPPFLAGS := -Iinclude -I../../include -I$(RUNTIME)/include \
	-I../metrics/include \
	-I$(VFS)/include -I$(CONFIG)/include -I$(NDK)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
HOST_FLAGS := -O2 -fno-builtin
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
# Bootstrap contains only canonical native libraries and the provider reader.
LINK_FLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections \
	$(ASTRA_STATIC_LINK_SCRIPT_FLAGS)
SOURCES := src/supervisor.c src/main.c src/volume.c src/loader.c \
	src/launch_report.c \
	src/loader_manifest.c src/service_definition_store.c src/vfs_host.c \
	src/vfs_read.c src/proc_tree.c
OBJECTS := $(patsubst src/%.c,build/m68k/%.o,$(SOURCES))
CONFIG_OBJECT := build/m68k/config_document.o
ANALYZER_OBJECTS := $(patsubst src/%.c,build/analyzer/%.o,$(SOURCES))
RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart.a
VFS_LIB := $(VFS)/build/m68k/libastravfs.a
RUNTIME_CRT0 := $(RUNTIME)/build/m68k/crt0.o
TARGET := build/m68k/astra_supervisor.elf
# What the ROM carries. Identical to TARGET as far as the loader is concerned
# -- same segments, same entry -- with every non-runtime symbol removed,
# because the payload is compressed into a fixed ROM window and neither DWARF
# nor a static symbol table has business in it. TARGET keeps both, and is the
# file a debugger uses.
IMAGE := build/m68k/astra_supervisor.image.elf
# The catalog the machine reads: the .astra_events section's bytes verbatim, so
# a lookup on the machine is (id - base) / 128 and never a parse. It is a build
# product and belongs on /system, not in git.
CATALOG := build/m68k/astra_events.cat
HOST_TEST := build/host/test_supervisor
LOADER_TEST := build/host/test_loader_manifest
PROC_TREE_ROOT_TEST := build/host/test_proc_tree_root
SERVICE_DEFINITION_TEST := build/host/test_service_definition_store
LOADER_STATUS_TEST := build/host/test_loader_status
LAUNCH_REPORT_TEST := build/host/test_launch_report
VOLUME_READ_TEST := build/host/test_volume_read
LOADER_RECOVERY_TEST := build/host/test_loader_recovery
TEST_SOURCES := src/supervisor.c $(RUNTIME)/src/startup.c $(ASTRA_COMMON)/utf8.c \
	tests/test_supervisor.c
ASTRA_PROGRAM_OWNER_TARGETS := $(RUNTIME):all \
	$(VFS):all ../graphics:all $(SYSTEM):all
ASTRA_PROGRAM_TARGETS := image
include ../program.mk

image: $(IMAGE) $(CATALOG) size

# The image is the deliverable, so the link contract is part of the build:
# astra_user.ld produces exactly the shape kernel_elf_accept() takes. The
# archives are named as prerequisites so a rebuilt library relinks the image.
$(TARGET): $(OBJECTS) $(CONFIG_OBJECT) $(ASTRA_STATIC_USER_LD_INPUTS) $(RUNTIME_CRT0) $(RUNTIME_LIB) $(VFS_LIB) $(NDK_LIB)
	@mkdir -p $(@D)
	$(CC) $(TARGET_FLAGS) $(LINK_FLAGS) -o $@ $(RUNTIME_CRT0) $(OBJECTS) \
		$(CONFIG_OBJECT) \
		$(VFS_LIB) \
		$(NDK_LIB) $(RUNTIME_LIB) -lgcc

# The catalog section goes the way DWARF does: read out at build time, never
# carried in the window the firmware compresses into ROM.
$(IMAGE): $(TARGET)
	$(OBJCOPY) --strip-all --strip-section-headers \
		--remove-section=.astra_events $< $@

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

$(CONFIG_OBJECT): $(CONFIG)/src/config_document.c \
		$(CONFIG)/include/astra/config_document.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/proc_tree.o: src/proc_tree.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

$(HOST_TEST): $(TEST_SOURCES)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $^ -o $@

$(LOADER_TEST): src/loader_manifest.c $(SYSTEM)/src/manifest.c \
		$(ASTRA_COMMON)/utf8.c \
		$(SYSTEM)/src/bundle.c $(RUNTIME)/src/string.c \
		tests/test_loader_manifest.c \
		include/loader.h
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/loader_manifest.c $(SYSTEM)/src/manifest.c $(SYSTEM)/src/bundle.c \
		$(RUNTIME)/src/string.c \
		$(ASTRA_COMMON)/utf8.c \
		tests/test_loader_manifest.c -o $@

$(PROC_TREE_ROOT_TEST): tests/test_proc_tree_root.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $< -o $@

$(SERVICE_DEFINITION_TEST): src/service_definition_store.c \
		src/loader_manifest.c $(CONFIG)/src/config_document.c \
		$(SYSTEM)/src/config_scope.c \
		$(SYSTEM)/src/manifest.c $(ASTRA_COMMON)/utf8.c \
		$(SYSTEM)/src/service_definition.c $(RUNTIME)/src/string.c \
		tests/test_service_definition_store.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $^ -o $@

$(LOADER_STATUS_TEST): tests/test_loader_status.c include/loader.h \
		../../include/astra/status.h
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $< -o $@

$(LAUNCH_REPORT_TEST): src/launch_report.c tests/test_launch_report.c \
		include/launch_report.h $(RUNTIME)/src/string.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/launch_report.c $(RUNTIME)/src/string.c \
		tests/test_launch_report.c -o $@

$(VOLUME_READ_TEST): src/volume.c tests/test_volume_read.c include/volume.h
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/volume.c tests/test_volume_read.c -o $@

LOADER_RECOVERY_SOURCES := tests/test_loader_recovery.c \
		src/service_definition_store.c src/loader_manifest.c \
		$(CONFIG)/src/config_document.c $(SYSTEM)/src/config_scope.c \
		$(SYSTEM)/src/manifest.c $(ASTRA_COMMON)/utf8.c \
		$(SYSTEM)/src/service_definition.c $(RUNTIME)/src/string.c

$(LOADER_RECOVERY_TEST): $(LOADER_RECOVERY_SOURCES) src/loader.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		-ffunction-sections -fdata-sections $(LOADER_RECOVERY_SOURCES) \
		-Wl,--gc-sections -o $@

test: $(HOST_TEST) $(LOADER_TEST) $(PROC_TREE_ROOT_TEST) \
		$(SERVICE_DEFINITION_TEST) $(LOADER_STATUS_TEST) $(LAUNCH_REPORT_TEST) \
		$(VOLUME_READ_TEST) $(LOADER_RECOVERY_TEST)
	./$(HOST_TEST)
	./$(LOADER_TEST)
	./$(PROC_TREE_ROOT_TEST)
	./$(SERVICE_DEFINITION_TEST)
	./$(LOADER_STATUS_TEST)
	./$(LAUNCH_REPORT_TEST)
	./$(VOLUME_READ_TEST)
	./$(LOADER_RECOVERY_TEST)

sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) $(TEST_SOURCES) \
		-o build/host/test_supervisor_sanitize
	./build/host/test_supervisor_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/loader_manifest.c $(SYSTEM)/src/manifest.c \
		$(ASTRA_COMMON)/utf8.c \
		$(SYSTEM)/src/bundle.c $(RUNTIME)/src/string.c \
		tests/test_loader_manifest.c -o build/host/test_loader_manifest_sanitize
	./build/host/test_loader_manifest_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/service_definition_store.c src/loader_manifest.c \
		$(CONFIG)/src/config_document.c $(SYSTEM)/src/config_scope.c \
		$(SYSTEM)/src/manifest.c $(ASTRA_COMMON)/utf8.c \
		$(SYSTEM)/src/service_definition.c $(RUNTIME)/src/string.c \
		tests/test_service_definition_store.c -o build/host/test_service_definition_store_sanitize
	./build/host/test_service_definition_store_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/volume.c tests/test_volume_read.c \
		-o build/host/test_volume_read_sanitize
	./build/host/test_volume_read_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $(SANITIZER_FLAGS) \
		-ffunction-sections -fdata-sections $(LOADER_RECOVERY_SOURCES) \
		-Wl,--gc-sections -o build/host/test_loader_recovery_sanitize
	./build/host/test_loader_recovery_sanitize

analyze: $(ANALYZER_OBJECTS)

build/analyzer/%.o: src/%.c
	@mkdir -p $(@D)
	$(ANALYZER_CC) $(CPPFLAGS) $(ANALYZER_CPPFLAGS) $(COMMON_FLAGS) \
		$(HOST_FLAGS) -fanalyzer -c $< -o $@

size: $(IMAGE)
	$(SIZE) $(IMAGE)

# Header dependencies: a stale object compiled against an older ABI
# constant is indistinguishable from a working one until it runs.
-include $(wildcard build/m68k/*.d build/m68k/*/*.d)

clean:
	rm -rf build

.PHONY: all image libraries test sanitize analyze size clean
