include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
HOST_CC ?= cc
ANALYZER_CC ?= gcc
CPPFLAGS := -Iinclude -I../runtime/include -I../../include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	$(ASTRA_DEBUG_FLAGS)
HOST_FLAGS := -O2
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
SOURCES := src/surface.c src/shared_surface.c src/window.c
TARGET := build/m68k/libastragraphics.a
TARGET_OBJECTS := build/m68k/surface.o build/m68k/shared_surface.o \
	build/m68k/window.o
HOST_TEST := build/host/test_surface

all: $(TARGET)

$(TARGET): $(TARGET_OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

build/m68k/%.o: src/%.c include/astra/surface.h include/astra/window.h \
	../../include/astra/gui.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

$(HOST_TEST): src/surface.c tests/test_surface.c include/astra/surface.h
	@mkdir -p $(@D)
	$(HOST_CC) -Iinclude $(COMMON_FLAGS) $(HOST_FLAGS) src/surface.c \
		tests/test_surface.c -o $@

test: $(HOST_TEST)
	./$(HOST_TEST)

sanitize:
	@mkdir -p build/host
	$(HOST_CC) -Iinclude $(COMMON_FLAGS) $(HOST_FLAGS) $(SANITIZER_FLAGS) \
		src/surface.c tests/test_surface.c -o build/host/test_surface_sanitize
	./build/host/test_surface_sanitize

analyze:
	@mkdir -p build/analyzer
	$(ANALYZER_CC) -Iinclude $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/surface.c -o build/analyzer/surface.o

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)

.PHONY: all test sanitize analyze clean
