include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
SIZE := $(CROSS)size
HOST_CC ?= cc
ANALYZER_CC ?= gcc
CPPFLAGS := -Iinclude -I../../include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
HOST_FLAGS := -O2 -fno-builtin
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
TARGET_C_SOURCES := src/startup.c src/syscall.c src/launch.c src/memory.c \
	src/log.c src/sort.c src/assert.c src/event_catalog.c
TARGET_ASM_SOURCES := src/m68k/syscall.S
TARGET_OBJECTS := $(patsubst src/%.c,build/m68k/%.o,$(TARGET_C_SOURCES)) \
	$(patsubst src/%.S,build/m68k/%.o,$(TARGET_ASM_SOURCES))
TARGET := build/m68k/libastrart.a
CRT0 := build/m68k/crt0.o
HOST_TEST := build/host/test_runtime

all: $(TARGET) $(CRT0) size

$(TARGET): $(TARGET_OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

build/m68k/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/%.o: src/%.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -m68030 -msoft-float $(DEPFLAGS) -c $< -o $@

$(CRT0): src/m68k/crt0.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -m68030 -msoft-float $(DEPFLAGS) -c $< -o $@

HOST_TEST_SOURCES := src/startup.c src/syscall.c src/launch.c src/memory.c \
	src/sort.c src/log.c src/event_catalog.c \
	tests/test_runtime.c

$(HOST_TEST): $(HOST_TEST_SOURCES)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $^ -o $@

test: $(HOST_TEST)
	./$(HOST_TEST)

sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) $(HOST_TEST_SOURCES) \
		-o build/host/test_runtime_sanitize
	./build/host/test_runtime_sanitize

analyze:
	@mkdir -p build/analyzer
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/startup.c -o build/analyzer/startup.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/syscall.c -o build/analyzer/syscall.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/launch.c -o build/analyzer/launch.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/memory.c -o build/analyzer/memory.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/sort.c -o build/analyzer/sort.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/event_catalog.c -o build/analyzer/event_catalog.o

size: $(TARGET_OBJECTS) $(CRT0)
	$(SIZE) $^

# Header dependencies: a stale object compiled against an older ABI
# constant is indistinguishable from a working one until it runs.
-include $(wildcard build/m68k/*.d build/m68k/*/*.d)

clean:
	rm -rf build

.PHONY: all test sanitize analyze size clean
