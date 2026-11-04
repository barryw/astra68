include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
HOST_CC ?= cc
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
HOST_TEST := build/host/test_mappings
LIBRARIES := $(ASTRA_CONFIG_LIBRARY) $(ASTRA_RUNTIME_LIBRARY) $(ASTRA_COMPILER_LIBRARY)
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
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O2 -fno-builtin \
		-ffunction-sections -fdata-sections \
		tests/test_mappings.c $(NETWORK)/src/network_core.c \
		../../../common/utf8.c $(RUNTIME)/src/string.c \
		-Wl,--gc-sections -o $(HOST_TEST)
	./$(HOST_TEST)

sanitize:
	$(MAKE) -C $(NETWORK) sanitize

analyze:
	$(MAKE) -C $(NETWORK) analyze

-include $(OBJECT:.o=.d)

.PHONY: size test sanitize analyze clean
