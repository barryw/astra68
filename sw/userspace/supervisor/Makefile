include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
SIZE := $(CROSS)size
HOST_CC ?= cc
ANALYZER_CC ?= gcc
RUNTIME := ../runtime
CPPFLAGS := -Iinclude -I../../include -I$(RUNTIME)/include \
	-I../storage/include -I../metrics/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections
HOST_FLAGS := -O2 -fno-builtin
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
LINK_FLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -T $(RUNTIME)/astra_user.ld
SOURCES := src/supervisor.c src/main.c
OBJECTS := build/m68k/supervisor.o build/m68k/main.o
RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart.a
STORAGE := ../storage
STORAGE_LIB := $(STORAGE)/build/m68k/libastrablock.a
RUNTIME_CRT0 := $(RUNTIME)/build/m68k/crt0.o
TARGET := build/m68k/astra_supervisor.elf
HOST_TEST := build/host/test_supervisor
TEST_SOURCES := src/supervisor.c $(RUNTIME)/src/startup.c \
	tests/test_supervisor.c

all: $(TARGET) size

# The image is the deliverable, so the link contract is part of the build:
# astra_user.ld produces exactly the shape kernel_elf_accept() takes.
$(TARGET): $(OBJECTS) $(RUNTIME)/astra_user.ld | runtime
	@mkdir -p $(@D)
	$(CC) $(TARGET_FLAGS) $(LINK_FLAGS) -o $@ $(RUNTIME_CRT0) $(OBJECTS) \
		$(STORAGE_LIB) $(RUNTIME_LIB)

runtime:
	$(MAKE) -C $(RUNTIME) all
	$(MAKE) -C $(STORAGE) $(STORAGE_LIB:$(STORAGE)/%=%)

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
		-o build/host/test_supervisor_sanitize
	./build/host/test_supervisor_sanitize

analyze:
	@mkdir -p build/analyzer
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/supervisor.c -o build/analyzer/supervisor.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/main.c -o build/analyzer/main.o

size: $(TARGET)
	$(SIZE) $(TARGET)

# Header dependencies: a stale object compiled against an older ABI
# constant is indistinguishable from a working one until it runs.
-include $(wildcard build/m68k/*.d build/m68k/*/*.d)

clean:
	rm -rf build

.PHONY: all runtime test sanitize analyze size clean
