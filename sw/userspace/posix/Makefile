include ../../../mk/m68k-cross.mk

# The POSIX layer: descriptors, and later a cwd, over capabilities and the VFS.
#
# A separate library rather than part of the runtime, because the runtime is
# what every Astra program links and this is what a *Unix* program needs. A
# command written against the NDK must not pay for printf it never calls.

CC := $(CROSS)gcc
AR := $(CROSS)ar
HOST_CC ?= cc
RUNTIME := ../runtime
STREAMS := ../streams
VFS := ../vfs
NDK := ../../../ndk
PICOLIBC ?= $(HOME)/picolibc-astra
CPPFLAGS := -I include -I../../include -I$(RUNTIME)/include \
	-I$(STREAMS)/include -I$(VFS)/include -I$(NDK)/include \
	-isystem $(PICOLIBC)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)

SOURCES := src/console.c src/file.c src/heap.c src/alloc.c src/time.c \
	src/system.c src/scheduling.c \
	src/strftime_format.c
OBJECTS := $(patsubst src/%.c,build/m68k/%.o,$(SOURCES))
LIB := build/m68k/libastraposix.a
HOST_TEST := build/host/test_strftime_format

all: $(LIB)

$(LIB): $(OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $(OBJECTS)

build/m68k/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

$(HOST_TEST): src/strftime_format.c tests/test_strftime_format.c
	@mkdir -p $(@D)
	$(HOST_CC) -Iinclude -std=c11 -Wall -Wextra -Werror -pedantic -O2 \
		src/strftime_format.c tests/test_strftime_format.c -o $@

test: $(HOST_TEST)
	./$(HOST_TEST)

sanitize:
	@mkdir -p build/host
	$(HOST_CC) -Iinclude -std=c11 -Wall -Wextra -Werror -pedantic -O2 \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		src/strftime_format.c tests/test_strftime_format.c \
		-o build/host/test_strftime_format_sanitize
	./build/host/test_strftime_format_sanitize

analyze:
	@mkdir -p build/analyzer
	$(HOST_CC) -Iinclude -std=c11 -Wall -Wextra -Werror -pedantic -O2 \
		-fanalyzer -c src/strftime_format.c \
		-o build/analyzer/strftime_format.o

clean:
	rm -rf build

.PHONY: all test sanitize analyze clean

-include $(OBJECTS:.o=.d)
