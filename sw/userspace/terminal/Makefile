include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
HOST_CC ?= cc
ANALYZER_CC ?= gcc
TIC ?= tic
INFOCMP ?= infocmp
CPPFLAGS := -Iinclude -I../../include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror
DEPFLAGS := -MMD -MP
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin $(ASTRA_DEBUG_FLAGS)
HOST_FLAGS := -O2
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

TARGET := build/m68k/libastraterminal.a
TARGET_OBJECTS := build/m68k/terminal.o build/m68k/keymap.o
HOST_TEST := build/host/test_terminal
HOST_KEYMAP_TEST := build/host/test_keymap
TERMINFO_SOURCE := terminfo/astra-256color.terminfo
TERMINFO_ENTRY := build/terminfo/a/astra-256color

all: $(TARGET) $(TERMINFO_ENTRY)

$(TERMINFO_ENTRY): $(TERMINFO_SOURCE)
	@mkdir -p build/terminfo
	$(TIC) -x -o build/terminfo $<
	@test -s $@

$(TARGET): $(TARGET_OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

build/m68k/%.o: src/%.c include/astra/terminal.h include/astra/keymap.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

$(HOST_TEST): src/terminal.c tests/test_terminal.c include/astra/terminal.h
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $(filter %.c,$^) -o $@

$(HOST_KEYMAP_TEST): src/keymap.c tests/test_keymap.c include/astra/keymap.h
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $(filter %.c,$^) -o $@

test: $(HOST_TEST) $(HOST_KEYMAP_TEST) $(TERMINFO_ENTRY)
	./$(HOST_TEST)
	./$(HOST_KEYMAP_TEST)
	$(INFOCMP) -A build/terminfo astra-256color >/dev/null

sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/terminal.c tests/test_terminal.c \
		-o build/host/test_terminal_sanitize
	./build/host/test_terminal_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/keymap.c tests/test_keymap.c \
		-o build/host/test_keymap_sanitize
	./build/host/test_keymap_sanitize

analyze:
	@mkdir -p build/analyzer
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/terminal.c -o build/analyzer/terminal.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/keymap.c -o build/analyzer/keymap.o

# Header dependencies: a stale object compiled against an older ABI
# constant is indistinguishable from a working one until it runs.
-include $(wildcard build/m68k/*.d build/m68k/*/*.d)

clean:
	rm -rf build

.PHONY: all test sanitize analyze clean
