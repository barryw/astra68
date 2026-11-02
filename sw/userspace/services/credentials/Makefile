include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
READELF := $(CROSS)readelf
OBJCOPY := $(CROSS)objcopy
SIZE := $(CROSS)size
HOST_CC ?= cc
RUNTIME := ../../runtime
include $(RUNTIME)/astra-link.mk
include ../../libraries.mk
NDK := ../../../../ndk
CPPFLAGS := -I$(NDK)/include -I../../../include -I$(RUNTIME)/include
FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic \
	$(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC $(ASTRA_DEBUG_FLAGS)
TARGET := build/m68k/credentials.elf
IMAGE := build/m68k/credentials
OBJECTS := build/m68k/main.o build/m68k/policy.o
LIBRARIES := $(ASTRA_CONFIG_LIBRARY) $(ASTRA_RUNTIME_LIBRARY) \
	$(ASTRA_COMPILER_LIBRARY) $(ASTRA_FILESYSTEM_LIBRARY)
include ../../dynamic-program.mk

build/m68k/%.o: %.c policy.h ../../../include/astra/credential_service.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) -MMD -MP -c $< -o $@

build/host/test_policy: tests/test_policy.c policy.c policy.h
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) -std=c11 -Wall -Wextra -Werror -pedantic -O2 \
		tests/test_policy.c policy.c -o $@

test: build/host/test_policy
	./build/host/test_policy
sanitize: test
analyze: $(TARGET)
clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)
.PHONY: test sanitize analyze clean
