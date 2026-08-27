include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
READELF := $(CROSS)readelf
HOST_CC ?= cc
ANALYZER_CC ?= gcc
RUNTIME := ../runtime
NDK := ../../../ndk
CPPFLAGS := -Iinclude -I../../include -I$(RUNTIME)/include -I$(NDK)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections
LIBRARY_FLAGS := $(TARGET_FLAGS) -fPIC -fvisibility=hidden
LIBRARY_LINK_FLAGS := -nostdlib -shared -Wl,-Bsymbolic \
	-Wl,--no-undefined -Wl,--build-id=none -Wl,--gc-sections \
	-Wl,--hash-style=sysv -Wl,-z,max-page-size=0x1000 \
	-T $(RUNTIME)/astra_library.ld
HOST_FLAGS := -O2 -fno-builtin
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := src/network_core.c
PING_CORE := src/ping_core.c
CORE_OBJECTS := build/m68k/network_core.o build/m68k/ping_core.o
HOST_TEST := build/host/test_network_core
LIBRARY_TEST := build/host/test_network_library
PING_TEST := build/host/test_ping_core
TARGET := build/m68k/libastranetwork.a
NETWORK_LIBRARY := build/m68k/libraries/network.library
NETWORK_LIBRARY_OBJECTS := build/m68k/library/network_library.o \
	build/m68k/library/network_core.o
RUNTIME_PIC_LIB := $(RUNTIME)/build/m68k/libastrart-pic.a

all: $(TARGET) $(NETWORK_LIBRARY) library-contract

$(TARGET): $(CORE_OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

build/m68k/network_core.o: $(CORE) include/astra/network_core.h ../../include/astra/network.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -c $< -o $@

build/m68k/ping_core.o: $(PING_CORE) include/astra/ping_core.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -c $< -o $@

build/m68k/library/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) -MMD -MP \
		-c $< -o $@

$(RUNTIME_PIC_LIB): FORCE
	$(MAKE) -C $(RUNTIME) pic

$(NETWORK_LIBRARY): $(NETWORK_LIBRARY_OBJECTS) $(RUNTIME_PIC_LIB) \
		$(RUNTIME)/astra_library.ld
	@mkdir -p $(@D)
	$(CC) $(LIBRARY_FLAGS) $(LIBRARY_LINK_FLAGS) \
		-Wl,-soname,network.library.1 -o $@ \
		$(NETWORK_LIBRARY_OBJECTS) $(RUNTIME_PIC_LIB) -lgcc

library-contract: $(NETWORK_LIBRARY)
	@python3 ../../../tools/library_info.py $(NETWORK_LIBRARY) | \
		grep -q '^network.library 1.0.0 ABI 1.1$$'
	@! $(READELF) -r $(NETWORK_LIBRARY) | \
		grep -E 'R_68K_(GLOB_DAT|JMP_SLOT|32|PC32)'

$(HOST_TEST): $(CORE) tests/test_network_core.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $^ -o $@

$(LIBRARY_TEST): $(CORE) src/network_library.c \
		tests/test_network_library.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $^ -o $@

$(PING_TEST): $(PING_CORE) tests/test_ping_core.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $^ -o $@

test: $(HOST_TEST) $(LIBRARY_TEST) $(PING_TEST)
	./$(HOST_TEST)
	./$(LIBRARY_TEST)
	./$(PING_TEST)

sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $(SANITIZER_FLAGS) \
	$(CORE) tests/test_network_core.c -o build/host/test_network_core_sanitize
	./build/host/test_network_core_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $(SANITIZER_FLAGS) \
		$(CORE) src/network_library.c tests/test_network_library.c \
		-o build/host/test_network_library_sanitize
	./build/host/test_network_library_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $(SANITIZER_FLAGS) \
		$(PING_CORE) tests/test_ping_core.c \
		-o build/host/test_ping_core_sanitize
	./build/host/test_ping_core_sanitize

analyze:
	@mkdir -p build/analyzer
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c $(CORE) -o build/analyzer/network_core.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c $(PING_CORE) -o build/analyzer/ping_core.o

clean:
	rm -rf build

-include $(wildcard build/m68k/library/*.d)

FORCE:

.PHONY: all test sanitize analyze clean library-contract FORCE
