include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
SIZE := $(CROSS)size
HOST_CC ?= cc
ANALYZER_CC ?= gcc
CPPFLAGS := -I../../include -I../alloc/include -I../storage/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections
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
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

build/m68k/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -c $< -o $@

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

clean:
	rm -rf build

.PHONY: all test sanitize analyze size clean
