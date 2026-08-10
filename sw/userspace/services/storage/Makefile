include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
RUNTIME := ../../runtime
STORAGE := ../../storage
ALLOC := ../../alloc
VFS := ../../vfs
LWEXT4 := ../../../../third_party/lwext4
CPPFLAGS := -I../../include -I../../../include -I$(RUNTIME)/include \
	-I$(STORAGE)/include -I$(ALLOC)/include -I$(VFS)/include \
	-I$(LWEXT4)/include -I$(STORAGE)/port/include \
	-DCONFIG_USE_DEFAULT_CFG=0 -I$(RUNTIME)/freestanding
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
LINK_FLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections -T $(RUNTIME)/astra_user.ld
TARGET := build/m68k/storage.elf
IMAGE := build/m68k/storage
OBJECT := build/m68k/main.o
RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart.a
STORAGE_LIB := $(STORAGE)/build/m68k/libastrablock.a
EXT4_LIB := $(STORAGE)/build/m68k/libastraext4.a
ALLOC_LIB := $(ALLOC)/build/m68k/libastraalloc.a
VFS_LIB := $(VFS)/build/m68k/libastravfs.a
CRT0 := $(RUNTIME)/build/m68k/crt0.o

all: $(IMAGE) size

$(TARGET): $(OBJECT) $(CRT0) $(VFS_LIB) $(EXT4_LIB) $(STORAGE_LIB) \
		$(ALLOC_LIB) $(RUNTIME_LIB) $(RUNTIME)/astra_user.ld
	@mkdir -p $(@D)
	$(CC) $(TARGET_FLAGS) $(LINK_FLAGS) -o $@ $(CRT0) $(OBJECT) \
		$(VFS_LIB) $(EXT4_LIB) $(STORAGE_LIB) $(ALLOC_LIB) \
		$(RUNTIME_LIB) -lgcc

$(IMAGE): $(TARGET)
	$(OBJCOPY) --strip-all --remove-section=.astra_events $< $@

$(OBJECT): main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

size: $(IMAGE)
	$(SIZE) $(IMAGE)

test sanitize analyze:
	@true

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)

.PHONY: all size test sanitize analyze clean
