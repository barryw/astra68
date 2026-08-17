include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
READELF := $(CROSS)readelf
HOST_CC ?= cc
RUNTIME := ../runtime
NDK := ../../../ndk
CPPFLAGS := -Iinclude -I$(RUNTIME)/include -I../../include \
	-I$(NDK)/include -I$(NDK)/src
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
LIBRARY_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC -fvisibility=hidden
LIBRARY_LINK_FLAGS := -nostdlib -shared -Wl,-Bsymbolic \
	-Wl,--no-undefined -Wl,--build-id=none -Wl,--gc-sections \
	-Wl,--hash-style=sysv -Wl,-z,max-page-size=0x1000 \
	-T $(RUNTIME)/astra_library.ld
TARGET := build/m68k/libraries/messaging.library
OBJECTS := build/m68k/messaging_library.o build/m68k/port.o \
	build/m68k/resource.o build/m68k/runtime_syscall_asm.o
HOST_TEST := build/host/test_messaging_library

all: $(TARGET) library-contract

build/m68k/messaging_library.o: src/messaging_library.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) -MMD -MP -c $< -o $@

build/m68k/%.o: $(NDK)/src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) -MMD -MP -c $< -o $@

build/m68k/runtime_syscall_asm.o: $(RUNTIME)/src/m68k/syscall.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -m68030 -msoft-float -fPIC -MMD -MP -c $< -o $@

$(TARGET): $(OBJECTS) $(RUNTIME)/astra_library.ld
	@mkdir -p $(@D)
	$(CC) $(LIBRARY_FLAGS) $(LIBRARY_LINK_FLAGS) \
		-Wl,-soname,messaging.library.1 -o $@ $(OBJECTS) -lgcc

library-contract: $(TARGET)
	@python3 ../../../tools/library_info.py $(TARGET) | \
		grep -q '^messaging.library 1.0.0 ABI 1.0$$'
	@! $(READELF) -r $(TARGET) | grep -E 'R_68K_(GLOB_DAT|JMP_SLOT|32|PC32)'

$(HOST_TEST): src/messaging_library.c tests/test_messaging_library.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -Wno-unused-parameter -O2 $^ -o $@

test: $(HOST_TEST)
	./$(HOST_TEST)

sanitize analyze:
	@true

clean:
	rm -rf build

-include $(OBJECTS:.o=.d)

.PHONY: all test sanitize analyze clean library-contract
