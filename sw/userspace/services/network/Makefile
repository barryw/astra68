include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
RUNTIME := ../../runtime
NETWORK := ../../network
CPPFLAGS := -I$(RUNTIME)/include -I$(NETWORK)/include -I../../../include \
	-I../../../../ndk/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
LINK_FLAGS := -nostdlib -static -Wl,--build-id=none -Wl,--gc-sections \
	-Wl,-z,max-page-size=0x1000 -T $(RUNTIME)/astra_user.ld
TARGET := build/m68k/network.elf
IMAGE := build/m68k/network
OBJECT := build/m68k/main.o
RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart.a
NETWORK_LIB := $(NETWORK)/build/m68k/libastranetwork.a
CRT0 := $(RUNTIME)/build/m68k/crt0.o
ASTRA_PROGRAM_OWNER_DIRS := $(RUNTIME) $(NETWORK)
include ../../program.mk

$(OBJECT): main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

$(TARGET): $(OBJECT) $(CRT0) $(RUNTIME_LIB) $(NETWORK_LIB) \
		$(RUNTIME)/astra_user.ld
	$(CC) $(TARGET_FLAGS) $(LINK_FLAGS) -o $@ \
		$(CRT0) $(OBJECT) $(NETWORK_LIB) $(RUNTIME_LIB) -lgcc

$(IMAGE): $(TARGET)
	$(OBJCOPY) --strip-all $< $@

size: $(IMAGE)
	$(SIZE) $(IMAGE)

clean:
	rm -rf build

test:
	$(MAKE) -C $(NETWORK) test

sanitize:
	$(MAKE) -C $(NETWORK) sanitize

analyze:
	$(MAKE) -C $(NETWORK) analyze

-include $(OBJECT:.o=.d)

.PHONY: size test sanitize analyze clean
