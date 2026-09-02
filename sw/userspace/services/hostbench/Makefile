include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
RUNTIME := ../../runtime
VFS := ../../vfs
NDK := ../../../../ndk
CPPFLAGS := -I../../include -I../../../include -I$(RUNTIME)/include \
	-I$(VFS)/include -I$(NDK)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -O3 -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
LINK_FLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections -T $(RUNTIME)/astra_user.ld
TARGET := build/m68k/hostbench.elf
IMAGE := build/m68k/hostbench
OBJECTS := build/m68k/main.o build/m68k/hot_loop.o
RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart.a
VFS_LIB := $(VFS)/build/m68k/libastravfs.a
FILESYSTEM_LIBRARY_OBJECT := \
	$(VFS)/build/m68k/library/filesystem_library.o
CRT0 := $(RUNTIME)/build/m68k/crt0.o
ASTRA_PROGRAM_OWNER_DIRS := $(RUNTIME) $(VFS)
include ../../program.mk

$(TARGET): $(OBJECTS) $(CRT0) $(FILESYSTEM_LIBRARY_OBJECT) $(VFS_LIB) \
		$(RUNTIME_LIB) \
		$(RUNTIME)/astra_user.ld
	@mkdir -p $(@D)
	$(CC) $(TARGET_FLAGS) $(LINK_FLAGS) -o $@ $(CRT0) $(OBJECTS) \
		$(FILESYSTEM_LIBRARY_OBJECT) $(VFS_LIB) $(RUNTIME_LIB) -lgcc

$(IMAGE): $(TARGET)
	$(OBJCOPY) --strip-all --remove-section=.astra_events $< $@

size: $(IMAGE)
	$(SIZE) $(IMAGE)

build/m68k/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/m68k/%.o: %.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ASTRA_TARGET_ABI_FLAGS) -MMD -MP -c $< -o $@

test sanitize:
	@true

analyze:
	@mkdir -p build/analyzer
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c main.c -o build/analyzer/main.o

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)
.PHONY: all size test sanitize analyze clean
