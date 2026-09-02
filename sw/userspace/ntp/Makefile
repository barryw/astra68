include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
HOST_CC ?= cc
PICOLIBC ?= $(HOME)/picolibc-astra
RUNTIME := ../runtime
CPPFLAGS := -Iinclude -I../../include -I../posix/include -I$(RUNTIME)/include \
	-isystem $(PICOLIBC)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
SOURCES := src/ntp_core.c src/ntp_client.c src/ntp_control.c
OBJECTS := $(patsubst src/%.c,build/m68k/%.o,$(SOURCES))
LIB := build/m68k/libastrantp.a
HOST_TEST := build/host/test_ntp_core

all: $(LIB)

$(LIB): $(OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

build/m68k/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

$(HOST_TEST): src/ntp_core.c tests/test_ntp_core.c
	@mkdir -p $(@D)
	$(HOST_CC) -Iinclude $(COMMON_FLAGS) -O2 $^ -o $@

test: $(HOST_TEST)
	./$(HOST_TEST)

sanitize:
	@mkdir -p build/host
	$(HOST_CC) -Iinclude $(COMMON_FLAGS) -O2 \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		src/ntp_core.c tests/test_ntp_core.c \
		-o build/host/test_ntp_core_sanitize
	./build/host/test_ntp_core_sanitize

analyze:
	@mkdir -p build/analyzer
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c src/ntp_core.c -o build/analyzer/ntp_core.o
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c src/ntp_client.c -o build/analyzer/ntp_client.o

clean:
	rm -rf build

-include $(OBJECTS:.o=.d)

.PHONY: all test sanitize analyze clean
