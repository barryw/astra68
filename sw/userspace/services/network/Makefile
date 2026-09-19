include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
READELF := $(CROSS)readelf
RUNTIME := ../../runtime
include $(RUNTIME)/astra-link.mk
include ../../libraries.mk
NETWORK := ../../network
CPPFLAGS := -I$(RUNTIME)/include -I$(NETWORK)/include -I../../../include \
	-I../../../../ndk/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC $(ASTRA_DEBUG_FLAGS)
TARGET := build/m68k/network.elf
IMAGE := build/m68k/network
OBJECT := build/m68k/main.o
NETWORK_SERVICE_LIB := $(NETWORK)/build/m68k/libastranetwork-service.a
LIBRARIES := $(ASTRA_RUNTIME_LIBRARY) $(ASTRA_COMPILER_LIBRARY)
ASTRA_PROGRAM_PRIVATE_INPUTS := $(NETWORK_SERVICE_LIB)
ASTRA_PROGRAM_PRIVATE_OWNER_TARGETS := $(NETWORK):service
include ../../dynamic-program.mk

$(OBJECT): main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

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
