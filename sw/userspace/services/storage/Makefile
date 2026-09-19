include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
RUNTIME := ../../runtime
include $(RUNTIME)/astra-link.mk
STORAGE := ../../storage
ALLOC := ../../alloc
VFS := ../../vfs
NDK := ../../../../ndk
LWEXT4 := ../../../../third_party/lwext4
CPPFLAGS := -I$(NDK)/include -I../../include -I../../../include \
	-I$(RUNTIME)/include \
	-I$(STORAGE)/include -I$(ALLOC)/include -I$(VFS)/include \
	-I$(LWEXT4)/include -I$(STORAGE)/port/include \
	-DCONFIG_USE_DEFAULT_CFG=0 -I$(RUNTIME)/freestanding
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -O2 -flto -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
# Storage mounts SYS: before loader.library or any ordinary shared library can
# be read.  It is therefore one of the two production bootstrap images that
# deliberately use the canonical static link profile (the ROM Supervisor is
# the other); every post-mount service uses dynamic-program.mk.
LINK_FLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections \
	$(ASTRA_STATIC_LINK_SCRIPT_FLAGS)
TARGET := build/m68k/storage.elf
IMAGE := build/m68k/storage
OBJECT := build/m68k/main.o
RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart.a
STORAGE_LIB := $(STORAGE)/build/m68k/libastrablock.a
EXT4_LIB := $(STORAGE)/build/m68k/libastraext4.a
ALLOC_LIB := $(ALLOC)/build/m68k/libastraalloc.a
VFS_LIB := $(VFS)/build/m68k/libastravfs.a
CRT0 := $(RUNTIME)/build/m68k/crt0.o
ASTRA_PROGRAM_OWNER_TARGETS := $(RUNTIME):all $(STORAGE):all $(ALLOC):all \
	$(VFS):all $(NDK):all
include ../../program.mk

$(TARGET): $(OBJECT) $(CRT0) $(VFS_LIB) $(EXT4_LIB) $(STORAGE_LIB) \
		$(ALLOC_LIB) $(RUNTIME_LIB) $(ASTRA_STATIC_USER_LD_INPUTS)
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
