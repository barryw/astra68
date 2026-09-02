include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
READELF := $(CROSS)readelf
HOST_CC ?= cc
ANALYZER_CC ?= gcc
RUNTIME := ../runtime
GRAPHICS := ../graphics
NDK := ../../../ndk
CPPFLAGS := -Iinclude -I$(GRAPHICS)/include -I$(RUNTIME)/include \
	-I../../include -I$(NDK)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
HOST_FLAGS := -O2
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
LIBRARY_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC -fvisibility=hidden
LIBRARY_LINK_FLAGS := -nostdlib -shared -Wl,-Bsymbolic \
	-Wl,--no-undefined -Wl,--build-id=none -Wl,--gc-sections \
	-Wl,--hash-style=sysv -Wl,-z,max-page-size=0x1000 \
	-T $(RUNTIME)/astra_library.ld
INTERFACE_TARGET := build/m68k/libraries/interface.library
INTERFACE_OBJECTS := build/m68k/interface_library.o
INPUT_TARGET := build/m68k/libraries/input.library
INPUT_OBJECTS := build/m68k/input_library.o
GRAPHICS_PIC_LIB := $(GRAPHICS)/build/m68k/libastragraphics-pic.a
NDK_PIC_LIB := $(NDK)/build/m68k/libastra-pic.a
RUNTIME_PIC_LIB := $(RUNTIME)/build/m68k/libastrart-pic.a
INTERFACE_HOST_TEST := build/host/test_interface
INPUT_HOST_TEST := build/host/test_input_library

all: $(INTERFACE_TARGET) $(INPUT_TARGET) library-contract

$(GRAPHICS)/build/generated/astra_font8x8.inc \
$(GRAPHICS)/build/generated/astra_ui_font.inc \
$(GRAPHICS)/build/generated/astra_mono_font.inc:
	$(MAKE) -C $(GRAPHICS) all

build/m68k/interface_library.o: src/interface_library.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(GRAPHICS)/build/generated $(COMMON_FLAGS) \
		$(LIBRARY_FLAGS) -c $< -o $@

build/m68k/input_library.o: src/input_library.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) -c $< -o $@

$(GRAPHICS_PIC_LIB):
	$(MAKE) -C $(GRAPHICS) pic

$(NDK_PIC_LIB):
	$(MAKE) -C $(NDK) pic

$(RUNTIME_PIC_LIB):
	$(MAKE) -C $(RUNTIME) pic

$(INTERFACE_TARGET): $(INTERFACE_OBJECTS) $(GRAPHICS_PIC_LIB) \
		$(NDK_PIC_LIB) $(RUNTIME_PIC_LIB) $(RUNTIME)/astra_library.ld
	@mkdir -p $(@D)
	$(CC) $(LIBRARY_FLAGS) $(LIBRARY_LINK_FLAGS) \
		-Wl,-soname,interface.library.1 -o $@ $(INTERFACE_OBJECTS) \
		-Wl,--start-group $(GRAPHICS_PIC_LIB) $(NDK_PIC_LIB) \
		$(RUNTIME_PIC_LIB) -Wl,--end-group -lgcc

$(INPUT_TARGET): $(INPUT_OBJECTS) $(NDK_PIC_LIB) $(RUNTIME_PIC_LIB) \
		$(RUNTIME)/astra_library.ld
	@mkdir -p $(@D)
	$(CC) $(LIBRARY_FLAGS) $(LIBRARY_LINK_FLAGS) \
		-Wl,-soname,input.library.1 -o $@ $(INPUT_OBJECTS) \
		-Wl,--start-group $(NDK_PIC_LIB) $(RUNTIME_PIC_LIB) \
		-Wl,--end-group -lgcc

library-contract: $(INTERFACE_TARGET) $(INPUT_TARGET)
	@python3 ../../../tools/library_info.py $(INTERFACE_TARGET) | \
		grep -q '^interface.library 1.1.0 ABI 1.1$$'
	@python3 ../../../tools/library_info.py $(INPUT_TARGET) | \
		grep -q '^input.library 1.0.0 ABI 1.0$$'
	$(call ASTRA_CHECK_DSO,$(INTERFACE_TARGET) $(INPUT_TARGET))

$(INTERFACE_HOST_TEST): src/interface_library.c tests/test_interface.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -DASTRA_INTERFACE_TEST \
		-DASTRA_NDK_TEST \
		-ffunction-sections -Wl,--gc-sections $^ -o $@

$(INPUT_HOST_TEST): src/input_library.c tests/test_input_library.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $^ -o $@

test: $(INTERFACE_HOST_TEST) $(INPUT_HOST_TEST)
	./$(INTERFACE_HOST_TEST)
	./$(INPUT_HOST_TEST)

sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) -DASTRA_INTERFACE_TEST -DASTRA_NDK_TEST \
		-ffunction-sections -Wl,--gc-sections src/interface_library.c \
		tests/test_interface.c -o build/host/test_interface_sanitize
	./build/host/test_interface_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/input_library.c tests/test_input_library.c \
		-o build/host/test_input_library_sanitize
	./build/host/test_input_library_sanitize

analyze:
	@mkdir -p build/analyzer
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/interface_library.c -o build/analyzer/interface_library.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/input_library.c -o build/analyzer/input_library.o

clean:
	rm -rf build

.PHONY: all test sanitize analyze library-contract clean
