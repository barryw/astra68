include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
READELF := $(CROSS)readelf
RUNTIME := ../../runtime
include $(RUNTIME)/astra-link.mk
VFS := ../../vfs
CONFIG := ../../config
SYSTEM := ../../system
METRICS := ../../metrics
NDK := ../../../../ndk
CPPFLAGS := -I$(NDK)/include -I../../include -I../../../include \
	-I$(RUNTIME)/include \
	-I$(VFS)/include -I$(CONFIG)/include -I$(METRICS)/include -I$(RUNTIME)/freestanding
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -O2 -flto -ffreestanding \
	-fno-builtin -ffunction-sections -fdata-sections \
	$(ASTRA_DEBUG_FLAGS)
TARGET := build/m68k/hostfs.elf
IMAGE := build/m68k/hostfs
OBJECT := build/m68k/main.o
# HostFS publishes SYSTEM before any shared library can be loaded. It and
# the ROM Supervisor are the canonical static bootstrap images.
VFS_LIB := $(VFS)/build/m68k/libastravfs.a
METRICS_LIB := $(METRICS)/build/m68k/libastrametric.a
RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart.a
SYSTEM_LIB := $(SYSTEM)/build/m68k/libastra.a
CONFIG_OBJECT := $(CONFIG)/build/m68k/config_document.o
CRT0 := $(RUNTIME)/build/m68k/crt0.o
LINK_FLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections $(ASTRA_STATIC_LINK_SCRIPT_FLAGS)
ASTRA_PROGRAM_OWNER_TARGETS := $(RUNTIME):all $(VFS):all $(METRICS):all \
	$(SYSTEM):all $(CONFIG):build/m68k/config_document.o
include ../../program.mk

$(TARGET): $(OBJECT) $(CONFIG_OBJECT) $(CRT0) $(VFS_LIB) $(METRICS_LIB) \
		$(SYSTEM_LIB) $(RUNTIME_LIB) $(ASTRA_STATIC_USER_LD_INPUTS)
	@mkdir -p $(@D)
	$(CC) $(TARGET_FLAGS) $(LINK_FLAGS) -o $@ $(CRT0) $(OBJECT) \
		$(CONFIG_OBJECT) $(METRICS_LIB) $(VFS_LIB) $(SYSTEM_LIB) $(RUNTIME_LIB) -lgcc

$(IMAGE): $(TARGET)
	$(OBJCOPY) --strip-all --remove-section=.astra_events $< $@

size: $(IMAGE)
	$(SIZE) $(IMAGE)

$(OBJECT): main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

HOST_CC ?= cc
HOST_TEST_CPPFLAGS := $(filter-out -I$(RUNTIME)/freestanding,$(CPPFLAGS))
HOST_TEST_FLAGS := -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections

build/host/test_settings: tests/test_settings.c main.c $(CONFIG)/src/config_document.c $(SYSTEM)/src/manifest.c ../../../common/utf8.c $(RUNTIME)/src/string.c $(VFS)/src/volume.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_TEST_CPPFLAGS) $(COMMON_FLAGS) $(HOST_TEST_FLAGS) $< $(CONFIG)/src/config_document.c $(SYSTEM)/src/manifest.c ../../../common/utf8.c $(RUNTIME)/src/string.c $(VFS)/src/volume.c -o $@

test: build/host/test_settings
	./build/host/test_settings

sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(HOST_TEST_CPPFLAGS) $(COMMON_FLAGS) $(HOST_TEST_FLAGS) \
		-fsanitize=address,undefined -fno-omit-frame-pointer tests/test_settings.c \
		$(CONFIG)/src/config_document.c $(SYSTEM)/src/manifest.c ../../../common/utf8.c $(RUNTIME)/src/string.c $(VFS)/src/volume.c -o build/host/test_settings_sanitize
	./build/host/test_settings_sanitize

analyze:
	@mkdir -p build/analyzer
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c main.c -o build/analyzer/main.o

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)
.PHONY: all size test sanitize analyze clean
