include ../../../../mk/m68k-cross.mk

RUNTIME := ../../runtime
include $(RUNTIME)/astra-link.mk
include ../../libraries.mk

CC := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE := $(CROSS)size
READELF := $(CROSS)readelf
HOST_CC ?= cc
NDK := ../../../../ndk
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC $(ASTRA_DEBUG_FLAGS)
CPPFLAGS := -I$(NDK)/include -I../../../include -I../../runtime/include
TARGET := build/m68k/posixd.elf
IMAGE := build/m68k/posixd
OBJECTS := build/m68k/main.o build/m68k/process_table.o
LIBRARIES := $(ASTRA_RUNTIME_LIBRARY) $(ASTRA_COMPILER_LIBRARY)
include ../../dynamic-program.mk
TEST := build/host/test_process_table

build/m68k/%.o: %.c process_table.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

$(TEST): process_table.c process_table.h tests/test_process_table.c
	@mkdir -p $(@D)
	$(HOST_CC) -I../../../include $(COMMON_FLAGS) -O2 \
		process_table.c tests/test_process_table.c -o $@

test: $(TEST)
	./$(TEST)

sanitize:
	@true

analyze:
	@mkdir -p build/analyzer
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c process_table.c -o build/analyzer/process_table.o
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c main.c -o build/analyzer/main.o

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)

.PHONY: all size test sanitize analyze clean
