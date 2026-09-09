include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE := $(CROSS)size
HOST_CC ?= cc
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
LINK_FLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections -T ../../runtime/astra_user.ld
CPPFLAGS := -I../../../include -I../../runtime/include
TARGET := build/m68k/posixd.elf
IMAGE := build/m68k/posixd
OBJECTS := build/m68k/main.o build/m68k/process_table.o
RUNTIME_LIB := ../../runtime/build/m68k/libastrart.a
CRT0 := ../../runtime/build/m68k/crt0.o
ASTRA_PROGRAM_OWNER_DIRS := ../../runtime
include ../../program.mk
TEST := build/host/test_process_table

$(TARGET): $(OBJECTS) $(CRT0) $(RUNTIME_LIB) ../../runtime/astra_user.ld
	@mkdir -p $(@D)
	$(CC) $(TARGET_FLAGS) $(LINK_FLAGS) -o $@ $(CRT0) $(OBJECTS) \
		$(RUNTIME_LIB) -lgcc

$(IMAGE): $(TARGET)
	$(OBJCOPY) --strip-all --remove-section=.astra_events $< $@

size: $(IMAGE)
	$(SIZE) $(IMAGE)

build/m68k/%.o: %.c process_table.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

$(TEST): process_table.c process_table.h tests/test_process_table.c
	@mkdir -p $(@D)
	$(HOST_CC) -I../../../include $(COMMON_FLAGS) -O2 \
		process_table.c tests/test_process_table.c -o $@

test: $(TEST)
	./$(TEST)

all: $(IMAGE)

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
