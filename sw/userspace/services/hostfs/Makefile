include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
READELF := $(CROSS)readelf
RUNTIME := ../../runtime
include $(RUNTIME)/astra-link.mk
include ../../libraries.mk
VFS := ../../vfs
METRICS := ../../metrics
NDK := ../../../../ndk
CPPFLAGS := -I$(NDK)/include -I../../include -I../../../include \
	-I$(RUNTIME)/include \
	-I$(VFS)/include -I$(METRICS)/include -I$(RUNTIME)/freestanding
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -O2 -flto -ffreestanding \
	-fno-builtin -ffunction-sections -fdata-sections -fPIC \
	$(ASTRA_DEBUG_FLAGS)
TARGET := build/m68k/hostfs.elf
IMAGE := build/m68k/hostfs
OBJECT := build/m68k/main.o
VFS_SERVICE_LIB := $(VFS)/build/m68k/libastravfs-service.a
METRICS_LIB := $(METRICS)/build/m68k/libastrametric.a
LIBRARIES := $(ASTRA_CONFIG_LIBRARY) $(ASTRA_FILESYSTEM_LIBRARY) \
	$(ASTRA_RUNTIME_LIBRARY) $(ASTRA_COMPILER_LIBRARY)
ASTRA_PROGRAM_PRIVATE_INPUTS := $(METRICS_LIB) $(VFS_SERVICE_LIB)
ASTRA_PROGRAM_PRIVATE_OWNER_TARGETS := $(METRICS):all $(VFS):service
include ../../dynamic-program.mk

$(OBJECT): main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

HOST_CC ?= cc
HOST_TEST_CPPFLAGS := $(filter-out -I$(RUNTIME)/freestanding,$(CPPFLAGS))
HOST_TEST_FLAGS := -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections

build/host/test_settings: tests/test_settings.c main.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_TEST_CPPFLAGS) $(COMMON_FLAGS) $(HOST_TEST_FLAGS) $< -o $@

test: build/host/test_settings
	./build/host/test_settings

sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(HOST_TEST_CPPFLAGS) $(COMMON_FLAGS) $(HOST_TEST_FLAGS) \
		-fsanitize=address,undefined -fno-omit-frame-pointer tests/test_settings.c \
		-o build/host/test_settings_sanitize
	./build/host/test_settings_sanitize

analyze:
	@mkdir -p build/analyzer
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c main.c -o build/analyzer/main.o

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)
.PHONY: all size test sanitize analyze clean
