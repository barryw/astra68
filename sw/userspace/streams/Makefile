include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
HOST_CC ?= cc
ANALYZER_CC ?= gcc
RUNTIME := ../runtime
TERMINAL_DIR := ../terminal
CPPFLAGS := -Iinclude -I../../include -I$(RUNTIME)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
HOST_FLAGS := -O2 -fno-builtin
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

SOURCES := src/stream_sink.c src/stream_client.c
HEADERS := include/astra/stream.h ../../include/astra/stream_service.h \
	../../include/astra/syscall.h

TARGET := build/m68k/libastrastreams.a
TARGET_OBJECTS := build/m68k/stream_sink.o build/m68k/stream_client.o
# The sink renders into the terminal model, so the test links it: "a write
# renders" is only worth asserting against what will actually be on the screen.
TEST_CPPFLAGS := $(CPPFLAGS) -I$(TERMINAL_DIR)/include
TEST_SOURCES := $(SOURCES) $(TERMINAL_DIR)/src/terminal.c \
	tests/test_streams.c
HOST_TEST := build/host/test_streams

all: $(TARGET)

$(TARGET): $(TARGET_OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

build/m68k/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

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
-include $(wildcard build/m68k/*.d)

clean:
	rm -rf build

.PHONY: all test sanitize analyze clean
