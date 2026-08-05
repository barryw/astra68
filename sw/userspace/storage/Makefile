include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
SIZE := $(CROSS)size
HOST_CC ?= cc
ANALYZER_CC ?= gcc
CPPFLAGS := -Iinclude -I../../include -I../runtime/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections
HOST_FLAGS := -O2 -fno-builtin
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
SOURCES := src/block_device.c src/memory_block.c
# The lease backend calls syscalls, so it is target-only.
OBJECTS := build/m68k/block_device.o build/m68k/memory_block.o \
	build/m68k/lease_block.o
TARGET := build/m68k/libastrablock.a
HOST_TEST := build/host/test_block_device

all: $(TARGET) size

$(TARGET): $(OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

build/m68k/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

$(HOST_TEST): $(SOURCES) tests/test_block_device.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $^ -o $@

LEASE_TEST_SOURCES := src/lease_block.c tests/test_lease_block.c

build/host/test_lease_block: $(LEASE_TEST_SOURCES)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $^ -o $@

test: $(HOST_TEST) build/host/test_lease_block
	./$(HOST_TEST)
	./build/host/test_lease_block

sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) $(LEASE_TEST_SOURCES) \
		-o build/host/test_lease_block_sanitize
	./build/host/test_lease_block_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) $(SOURCES) tests/test_block_device.c \
		-o build/host/test_block_device_sanitize
	./build/host/test_block_device_sanitize

analyze:
	@mkdir -p build/analyzer
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/block_device.c -o build/analyzer/block_device.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/memory_block.c -o build/analyzer/memory_block.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/lease_block.c -o build/analyzer/lease_block.o

size: $(OBJECTS)
	$(SIZE) $^

# Header dependencies: a stale object compiled against an older ABI
# constant is indistinguishable from a working one until it runs.
-include $(wildcard build/m68k/*.d build/m68k/*/*.d)

clean:
	rm -rf build

.PHONY: all test sanitize analyze size clean
