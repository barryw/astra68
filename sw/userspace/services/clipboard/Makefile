include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
RUNTIME := ../../runtime
NDK := ../../../../ndk
CPPFLAGS := -I$(NDK)/include -I../../../include -I$(RUNTIME)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
LINK_FLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections -T $(RUNTIME)/astra_user.ld
TARGET := build/m68k/clipboard.elf
IMAGE := build/m68k/clipboard
OBJECT := build/m68k/main.o
RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart.a
NDK_LIB := $(NDK)/build/m68k/libastra.a
CRT0 := $(RUNTIME)/build/m68k/crt0.o
ASTRA_PROGRAM_OWNER_DIRS := $(RUNTIME) $(NDK)
include ../../program.mk

$(TARGET): $(OBJECT) $(CRT0) $(NDK_LIB) $(RUNTIME_LIB) \
		$(RUNTIME)/astra_user.ld
	@mkdir -p $(@D)
	$(CC) $(TARGET_FLAGS) $(LINK_FLAGS) -o $@ $(CRT0) $(OBJECT) \
		-Wl,--start-group $(NDK_LIB) $(RUNTIME_LIB) -lgcc -Wl,--end-group

$(IMAGE): $(TARGET)
	$(OBJCOPY) --strip-all $< $@

$(OBJECT): main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

size: $(IMAGE)
	$(SIZE) $(IMAGE)

test sanitize:
	$(MAKE) -C $(NDK) $@

analyze:
	@mkdir -p build/analyzer
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c main.c -o build/analyzer/main.o

clean:
	rm -rf build

-include $(OBJECT:.o=.d)

.PHONY: size test sanitize analyze clean
