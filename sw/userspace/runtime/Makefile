include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
SIZE := $(CROSS)size
HOST_CC ?= cc
ANALYZER_CC ?= gcc
CPPFLAGS := -Iinclude -I../../include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections
HOST_FLAGS := -O2 -fno-builtin
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
TARGET_C_SOURCES := src/startup.c src/syscall.c src/memory.c
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
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -c $< -o $@

build/m68k/%.o: src/%.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -m68030 -msoft-float -c $< -o $@

$(CRT0): src/m68k/crt0.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -m68030 -msoft-float -c $< -o $@

$(HOST_TEST): src/startup.c src/syscall.c src/memory.c tests/test_runtime.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $^ -o $@

test: $(HOST_TEST)
	./$(HOST_TEST)

sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/startup.c src/syscall.c src/memory.c \
		tests/test_runtime.c \
		-o build/host/test_runtime_sanitize
	./build/host/test_runtime_sanitize

analyze:
	@mkdir -p build/analyzer
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/startup.c -o build/analyzer/startup.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/syscall.c -o build/analyzer/syscall.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/memory.c -o build/analyzer/memory.o

size: $(TARGET_OBJECTS) $(CRT0)
	$(SIZE) $^

clean:
	rm -rf build

.PHONY: all test sanitize analyze size clean
