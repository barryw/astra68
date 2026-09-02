include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
RUNTIME := ../../runtime
VFS := ../../vfs
CPPFLAGS := -I../../include -I../../../include -I$(RUNTIME)/include \
	-I$(VFS)/include -I$(RUNTIME)/freestanding
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -O2 -flto -ffreestanding \
	-fno-builtin -ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
LINK_FLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections -T $(RUNTIME)/astra_user.ld
TARGET := build/m68k/hostfs.elf
IMAGE := build/m68k/hostfs
OBJECT := build/m68k/main.o
RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart.a
VFS_LIB := $(VFS)/build/m68k/libastravfs.a
CRT0 := $(RUNTIME)/build/m68k/crt0.o
ASTRA_PROGRAM_OWNER_DIRS := $(RUNTIME) $(VFS)
include ../../program.mk

$(TARGET): $(OBJECT) $(CRT0) $(VFS_LIB) $(RUNTIME_LIB) \
		$(RUNTIME)/astra_user.ld
	@mkdir -p $(@D)
	$(CC) $(TARGET_FLAGS) $(LINK_FLAGS) -o $@ $(CRT0) $(OBJECT) \
		$(VFS_LIB) $(RUNTIME_LIB) -lgcc

$(IMAGE): $(TARGET)
	$(OBJCOPY) --strip-all --remove-section=.astra_events $< $@

size: $(IMAGE)
	$(SIZE) $(IMAGE)

$(OBJECT): main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

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
