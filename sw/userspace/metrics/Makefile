include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
SIZE := $(CROSS)size
HOST_CC ?= cc
ANALYZER_CC ?= gcc
CPPFLAGS := -I../../include -I../alloc/include -I../storage/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
HOST_FLAGS := -O2 -fno-builtin
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
SOURCES := src/metrics.c
OBJECTS := build/m68k/metrics.o
TARGET := build/m68k/libastrametric.a
HOST_TEST := build/host/test_metrics

# The registry is only meaningful with real publishers, so the test links the
# allocator and the block facade and reads them back through the contract.
TEST_SOURCES := $(SOURCES) ../alloc/src/alloc.c ../storage/src/block_device.c \
	../storage/src/memory_block.c ../runtime/src/memory.c \
	tests/test_metrics.c

all: $(TARGET) size

$(TARGET): $(OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

build/m68k/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

$(HOST_TEST): $(TEST_SOURCES)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $^ -o $@

test: $(HOST_TEST)
	./$(HOST_TEST)

sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) $(TEST_SOURCES) \
		-o build/host/test_metrics_sanitize
	./build/host/test_metrics_sanitize

analyze:
	@mkdir -p build/analyzer
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/metrics.c -o build/analyzer/metrics.o

size: $(OBJECTS)
	$(SIZE) $^

# Header dependencies: a stale object compiled against an older ABI
# constant is indistinguishable from a working one until it runs.
-include $(wildcard build/m68k/*.d build/m68k/*/*.d)

clean:
	rm -rf build

.PHONY: all test sanitize analyze size clean
