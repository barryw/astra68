include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
READELF := $(CROSS)readelf
RUNTIME := ../../runtime
include $(RUNTIME)/astra-link.mk
include ../../libraries.mk
VFS := ../../vfs
NDK := ../../../../ndk
CPPFLAGS := -I../../../include -I$(RUNTIME)/include -I$(VFS)/include \
	-I$(NDK)/include -I../../storage/include \
	-I../../../../third_party/odfs/include -DODFS_FEATURE_CDDA=0 -DODFS_FEATURE_LOG=0
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -O2 -flto -ffreestanding \
	-fno-builtin -ffunction-sections -fdata-sections -fPIC $(ASTRA_DEBUG_FLAGS)
TARGET := build/m68k/cdfs.elf
IMAGE := build/m68k/cdfs
OBJECT := build/m68k/main.o
CDFS_BACKEND := $(VFS)/build/m68k/libastracdfs.a
VFS_SERVICE_LIB := $(VFS)/build/m68k/libastravfs-service.a
LIBRARIES := $(ASTRA_CONFIG_LIBRARY) $(ASTRA_FILESYSTEM_LIBRARY) \
	$(ASTRA_RUNTIME_LIBRARY) $(ASTRA_COMPILER_LIBRARY)
ASTRA_PROGRAM_PRIVATE_INPUTS := $(CDFS_BACKEND) $(VFS_SERVICE_LIB)
ASTRA_PROGRAM_PRIVATE_OWNER_TARGETS := $(VFS):service $(VFS):cdfs-backend
include ../../dynamic-program.mk

$(OBJECT): main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

test sanitize:
	$(MAKE) -C $(VFS) cdfs-$@

analyze:
	@mkdir -p build/analyzer
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c main.c -o build/analyzer/main.o

clean:
	rm -rf build

-include $(OBJECT:.o=.d)
.PHONY: test sanitize analyze clean
