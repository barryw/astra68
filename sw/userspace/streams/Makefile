include ../../../mk/m68k-cross.mk
include ../../../mk/astra-shared-library.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
READELF := $(CROSS)readelf
HOST_CC ?= cc
ANALYZER_CC ?= gcc
RUNTIME := ../runtime
TERMINAL_DIR := ../terminal
NDK := ../../../ndk
CPPFLAGS := -I$(NDK)/include -Iinclude -I../../include -I$(RUNTIME)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
HOST_FLAGS := -O2 -fno-builtin
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

SOURCES := src/stream_sink.c src/stream_client.c
HEADERS := $(NDK)/include/astra/stream.h ../../include/astra/stream_service.h \
	../../include/astra/syscall.h

TARGET := build/m68k/libastrastreams.a
TARGET_OBJECTS := build/m68k/stream_sink.o build/m68k/stream_client.o
DYNAMIC_LIBRARY := build/m68k/libraries/streams.library.1
DYNAMIC_IDENTITY := build/m68k/dynamic/streams_library.o
DYNAMIC_OBJECTS := build/m68k/dynamic/stream_sink.o \
	build/m68k/dynamic/stream_client.o
DYNAMIC_MAP := streams.library.exports
RUNTIME_LIBRARY := $(RUNTIME)/build/m68k/libraries/runtime.library.1
KERNEL_ELF_TEST := ../../kernel/build/test_elf
# The sink renders into the terminal model, so the test links it: "a write
# renders" is only worth asserting against what will actually be on the screen.
TEST_CPPFLAGS := $(CPPFLAGS)
TEST_SOURCES := $(SOURCES) $(TERMINAL_DIR)/src/terminal.c \
	tests/test_streams.c
HOST_TEST := build/host/test_streams

all: $(TARGET) streams-library-contract

library: $(DYNAMIC_LIBRARY)

$(TARGET): $(TARGET_OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

build/m68k/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/dynamic/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) \
		$(ASTRA_SHARED_PIC_FLAGS) $(DEPFLAGS) -c $< -o $@

$(DYNAMIC_IDENTITY): src/streams_library.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) \
		$(ASTRA_SHARED_PIC_FLAGS) $(DEPFLAGS) -c $< -o $@

runtime-library-ready:
	$(MAKE) -C $(RUNTIME) library

$(KERNEL_ELF_TEST):
	$(MAKE) -C ../../kernel build/test_elf

$(DYNAMIC_LIBRARY): $(DYNAMIC_IDENTITY) $(DYNAMIC_OBJECTS) \
		$(DYNAMIC_MAP) $(ASTRA_SHARED_LINK_INPUTS) | runtime-library-ready
	@mkdir -p $(@D)
	$(CC) $(ASTRA_TARGET_ABI_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,streams.library.1 \
		-Wl,--version-script=$(DYNAMIC_MAP) -o $@ \
		$(DYNAMIC_IDENTITY) $(DYNAMIC_OBJECTS) \
		-L$(dir $(RUNTIME_LIBRARY)) -Wl,--no-as-needed \
		-Wl,-l:runtime.library.1 $(ASTRA_SHARED_LINK_END)

streams-library-contract: $(DYNAMIC_LIBRARY) $(KERNEL_ELF_TEST)
	@python3 ../../../tools/check_shared_library_layout.py $(DYNAMIC_LIBRARY)
	@python3 ../../../tools/check_library_exports.py $(DYNAMIC_MAP) \
		$(NDK)/include/astra/stream.h
	@python3 ../../../tools/library_info.py $< | \
		grep -q '^streams.library.1 1.0.0 ABI 1.0$$'
	@$(READELF) -hW $< | grep -q 'Type:.*DYN'
	@$(READELF) -dW $< | grep -Fq 'Library soname: [streams.library.1]'
	@test "$$($(READELF) -dW $< | grep -Fc 'Shared library: [runtime.library.1]')" = 1
	@$(READELF) -dW $< | grep -q 'BIND_NOW'
	@! $(READELF) -dW $< | grep -q 'TEXTREL'
	@$(READELF) -lW $< | grep -q ' GNU_RELRO '
	@$(READELF) -VW $< | grep -q 'ASTRA_STREAMS_1.0'
	@ASTRA_LIBRARY_FIXTURE=$(abspath $<) $(KERNEL_ELF_TEST) >/dev/null
	@echo "astra streams shared library: PASS"

$(HOST_TEST): $(TEST_SOURCES) $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(TEST_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(TEST_SOURCES) -o $@

test: $(HOST_TEST)
	./$(HOST_TEST)

# Every buffer here is a fixed message copied into another fixed message, which
# is exactly the arithmetic a sanitizer is for.
sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(TEST_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) $(TEST_SOURCES) -o build/host/test_streams_sanitize
	./build/host/test_streams_sanitize

analyze:
	@mkdir -p build/analyzer
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/stream_sink.c -o build/analyzer/stream_sink.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/stream_client.c -o build/analyzer/stream_client.o

# Header dependencies: a stale object compiled against an older ABI
# constant is indistinguishable from a working one until it runs.
-include $(wildcard build/m68k/*.d build/m68k/dynamic/*.d)

clean:
	rm -rf build

.PHONY: all library runtime-library-ready streams-library-contract test sanitize \
	analyze clean
