include ../../../mk/m68k-cross.mk
include ../../../mk/astra-shared-library.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
READELF := $(CROSS)readelf
HOST_CC ?= cc
ANALYZER_CC ?= gcc
RUNTIME := ../runtime
COMPILER := ../compiler
NDK := ../../../ndk
CPPFLAGS := -Iinclude -I../../include -I$(RUNTIME)/include -I$(NDK)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections
LIBRARY_FLAGS := $(TARGET_FLAGS) $(ASTRA_SHARED_PIC_FLAGS)
HOST_FLAGS := -O2 -fno-builtin
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := src/network_core.c
PING_CORE := src/ping_core.c
CORE_OBJECTS := build/m68k/network_library.o build/m68k/network_core.o \
	build/m68k/ping_core.o
HOST_TEST := build/host/test_network_core
LIBRARY_TEST := build/host/test_network_library
PING_TEST := build/host/test_ping_core
TARGET := build/m68k/libastranetwork.a
SERVICE_TARGET := build/m68k/libastranetwork-service.a
NETWORK_LIBRARY := build/m68k/libraries/network.library.1
NETWORK_LIBRARY_OBJECTS := build/m68k/library/network_library_identity.o \
	build/m68k/library/network_library.o \
	build/m68k/library/network_core.o build/m68k/library/ping_core.o
NETWORK_LIBRARY_MAP := network.library.exports
RUNTIME_LIBRARY := $(RUNTIME)/build/m68k/libraries/runtime.library.1
COMPILER_LIBRARY := $(COMPILER)/build/m68k/libraries/compiler.library.1
SYMBOL_OWNERSHIP_CHECK := ../../../tools/check_shared_symbol_ownership.py

all: $(TARGET) $(SERVICE_TARGET) $(NETWORK_LIBRARY) library-contract

library: $(NETWORK_LIBRARY)

service: $(SERVICE_TARGET)

$(TARGET): $(CORE_OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

$(SERVICE_TARGET): build/m68k/library/network_core.o Makefile
	$(ASTRA_REPLACE_ARCHIVE)

build/m68k/network_core.o: $(CORE) include/astra/network_core.h ../../include/astra/network.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -c $< -o $@

build/m68k/ping_core.o: $(PING_CORE) include/astra/ping_core.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -c $< -o $@

build/m68k/network_library.o: src/network_library.c \
		$(NDK)/include/astra/network_library.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP \
		-c $< -o $@

build/m68k/library/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) -MMD -MP \
		-c $< -o $@

$(CORE_OBJECTS) $(NETWORK_LIBRARY_OBJECTS): Makefile

runtime-library-ready:
	$(MAKE) -C $(RUNTIME) library

$(NETWORK_LIBRARY): $(NETWORK_LIBRARY_OBJECTS) $(NETWORK_LIBRARY_MAP) \
		$(ASTRA_SHARED_LINK_INPUTS) | runtime-library-ready
	@mkdir -p $(@D)
	$(CC) $(ASTRA_TARGET_ABI_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,network.library.1 -o $@ \
		-Wl,--version-script=$(NETWORK_LIBRARY_MAP) \
		$(NETWORK_LIBRARY_OBJECTS) \
		-L$(dir $(RUNTIME_LIBRARY)) -L$(dir $(COMPILER_LIBRARY)) \
		-Wl,--no-as-needed -Wl,-l:runtime.library.1 \
		-Wl,-l:compiler.library.1

library-contract: $(NETWORK_LIBRARY)
	@python3 ../../../tools/check_shared_library_layout.py $(NETWORK_LIBRARY)
	@python3 $(SYMBOL_OWNERSHIP_CHECK) --readelf $(READELF) \
		$(NETWORK_LIBRARY) $(RUNTIME_LIBRARY) $(COMPILER_LIBRARY)
	@python3 ../../../tools/check_library_exports.py \
		$(NETWORK_LIBRARY_MAP) include/astra/ping_core.h \
		$(NDK)/include/astra/network_library.h
	@python3 ../../../tools/check_elf_exports.py --readelf $(READELF) \
		$(NETWORK_LIBRARY_MAP) $(NETWORK_LIBRARY)
	@python3 ../../../tools/library_info.py $(NETWORK_LIBRARY) | \
		grep -q '^network.library.1 1.0.0 ABI 1.1$$'
	@$(READELF) -dW $< | grep -Fq 'Library soname: [network.library.1]'
	@test "$$($(READELF) -dW $< | grep -Fc 'Shared library: [runtime.library.1]')" = 1
	@test "$$($(READELF) -dW $< | grep -Fc 'Shared library: [compiler.library.1]')" = 1
	@test "$$($(READELF) -dW $< | awk '/NEEDED/{n++} END{print n+0}')" = 2
	@$(READELF) -dW $< | grep -q 'BIND_NOW'
	@! $(READELF) -dW $< | grep -q 'TEXTREL'
	@$(READELF) -lW $< | grep -q ' GNU_RELRO '
	@$(READELF) -VW $< | grep -q 'ASTRA_NETWORK_1.1'
	@echo "astra network shared library: PASS"

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

-include $(wildcard build/m68k/*.d build/m68k/library/*.d)

FORCE:

.PHONY: all library service test sanitize analyze clean library-contract \
	runtime-library-ready
