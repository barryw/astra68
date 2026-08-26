include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
READELF := $(CROSS)readelf
HOST_CC ?= cc
ANALYZER_CC ?= gcc
RUNTIME := ../runtime
NDK := ../../../ndk
CPPFLAGS := -Iinclude -I$(RUNTIME)/include -I../../include \
	-I$(NDK)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
HOST_FLAGS := -O2
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
LIBRARY_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC -fvisibility=hidden
LIBRARY_LINK_FLAGS := -nostdlib -shared -Wl,-Bsymbolic \
	-Wl,--no-undefined -Wl,--build-id=none -Wl,--gc-sections \
	-Wl,--hash-style=sysv -Wl,-z,max-page-size=0x1000 \
	-T $(RUNTIME)/astra_library.ld
TARGET := build/m68k/libraries/messaging.library
OBJECTS := build/m68k/messaging_library.o
NDK_PIC_LIB := $(NDK)/build/m68k/libastra-pic.a
RUNTIME_PIC_LIB := $(RUNTIME)/build/m68k/libastrart-pic.a
HOST_TEST := build/host/test_messaging_library

all: $(TARGET) library-contract

build/m68k/messaging_library.o: src/messaging_library.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) -MMD -MP -c $< -o $@

$(NDK_PIC_LIB):
	$(MAKE) -C $(NDK) pic

$(RUNTIME_PIC_LIB):
	$(MAKE) -C $(RUNTIME) pic

$(TARGET): $(OBJECTS) $(NDK_PIC_LIB) $(RUNTIME_PIC_LIB) \
		$(RUNTIME)/astra_library.ld
	@mkdir -p $(@D)
	$(CC) $(LIBRARY_FLAGS) $(LIBRARY_LINK_FLAGS) \
		-Wl,-soname,messaging.library.1 -o $@ $(OBJECTS) \
		-Wl,--start-group $(NDK_PIC_LIB) $(RUNTIME_PIC_LIB) \
		-Wl,--end-group -lgcc

library-contract: $(TARGET)
	@python3 ../../../tools/library_info.py $(TARGET) | \
		grep -q '^messaging.library 1.0.0 ABI 1.0$$'
	@! $(READELF) -r $(TARGET) | grep -E 'R_68K_(GLOB_DAT|JMP_SLOT|32|PC32)'

$(HOST_TEST): src/messaging_library.c tests/test_messaging_library.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -Wno-unused-parameter \
		$(HOST_FLAGS) $^ -o $@

test: $(HOST_TEST)
	./$(HOST_TEST)

sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -Wno-unused-parameter \
		$(HOST_FLAGS) $(SANITIZER_FLAGS) src/messaging_library.c \
		tests/test_messaging_library.c \
		-o build/host/test_messaging_library_sanitize
	./build/host/test_messaging_library_sanitize

analyze:
	@mkdir -p build/analyzer
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/messaging_library.c -o build/analyzer/messaging_library.o

clean:
	rm -rf build

-include $(OBJECTS:.o=.d)

.PHONY: all test sanitize analyze clean library-contract
