include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
HOST_CC ?= cc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
READELF := $(CROSS)readelf
RUNTIME := ../../runtime
include $(RUNTIME)/astra-link.mk
include ../../libraries.mk
VFS := ../../vfs
NDK := ../../../../ndk
CPPFLAGS := -I$(NDK)/include -I../../../include -I$(RUNTIME)/include -I$(VFS)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC $(ASTRA_DEBUG_FLAGS)
TARGET := build/m68k/atlas.elf
IMAGE := build/m68k/atlas
OBJECT := build/m68k/main.o
OBJECTS := build/m68k/crc32.o build/m68k/directory_snapshot.o build/m68k/folder_ops.o \
	build/m68k/volume_action.o build/m68k/volume_usage_dialog.o
LIBRARIES := $(ASTRA_INTERFACE_LIBRARY) $(ASTRA_GRAPHICS_LIBRARY) \
	$(ASTRA_FONT_LIBRARY) $(ASTRA_FILESYSTEM_LIBRARY) $(ASTRA_SYSTEM_LIBRARY) \
	$(ASTRA_PACKAGE_LIBRARY) $(ASTRA_RUNTIME_LIBRARY) $(ASTRA_COMPILER_LIBRARY)
include ../../dynamic-program.mk

build/m68k/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/m68k/directory_snapshot.o: ../../common/directory_snapshot.c ../../common/directory_snapshot.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/m68k/folder_ops.o: ../../common/folder_ops.c ../../common/folder_ops.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/m68k/crc32.o: ../../../common/crc32.c ../../../include/astra/crc32.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/m68k/volume_action.o: ../../common/volume_action.c \
	../../common/volume_action.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/m68k/volume_usage_dialog.o: ../../common/volume_usage_dialog.c \
	../../common/volume_usage_dialog.h ../../common/volume_usage_layout.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/host/test_directory: ../../common/directory_snapshot.c ../../common/directory_snapshot.h tests/test_directory.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O2 ../../common/directory_snapshot.c tests/test_directory.c -o $@

test: build/host/test_directory
	./$<

sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O1 -fsanitize=address,undefined \
		-fno-omit-frame-pointer ../../common/directory_snapshot.c tests/test_directory.c -o build/host/test_directory_sanitize
	./build/host/test_directory_sanitize

analyze:
	@mkdir -p build/analyzer
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c ../../common/directory_snapshot.c -o build/analyzer/directory.o
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c main.c -o build/analyzer/main.o

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)
.PHONY: test sanitize analyze clean
