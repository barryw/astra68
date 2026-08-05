include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
HOST_CC ?= cc
ANALYZER_CC ?= gcc
CPPFLAGS := -Iinclude -I../../include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin
HOST_FLAGS := -O2
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
SOURCES := src/input_service_core.c src/input_port_sink.c
HEADERS := include/astra/input_service_core.h \
	include/astra/input_port_sink.h ../../include/astra/input.h \
	../../include/astra/input_service.h ../../include/astra/syscall.h
TARGET := build/m68k/libastrainput.a
TARGET_OBJECTS := build/m68k/input_service_core.o \
	build/m68k/input_port_sink.o
HOST_TEST := build/host/test_input_service_core

all: $(TARGET)

$(TARGET): $(TARGET_OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

build/m68k/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -c $< -o $@

$(HOST_TEST): $(SOURCES) tests/test_input_service_core.c $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $(SOURCES) \
		tests/test_input_service_core.c -o $@

test: $(HOST_TEST)
	./$(HOST_TEST)

sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) $(SOURCES) tests/test_input_service_core.c \
		-o build/host/test_input_service_core_sanitize
	./build/host/test_input_service_core_sanitize

analyze:
	@mkdir -p build/analyzer
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/input_service_core.c -o build/analyzer/input_service_core.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/input_port_sink.c -o build/analyzer/input_port_sink.o

clean:
	rm -rf build

.PHONY: all test sanitize analyze clean
