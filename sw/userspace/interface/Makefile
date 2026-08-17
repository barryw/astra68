include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
READELF := $(CROSS)readelf
HOST_CC ?= cc
RUNTIME := ../runtime
GRAPHICS := ../graphics
NDK := ../../../ndk
CPPFLAGS := -Iinclude -I$(GRAPHICS)/include -I$(RUNTIME)/include \
	-I../../include -I$(NDK)/include -I$(NDK)/src
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
LIBRARY_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC -fvisibility=hidden
LIBRARY_LINK_FLAGS := -nostdlib -shared -Wl,-Bsymbolic \
	-Wl,--no-undefined -Wl,--build-id=none -Wl,--gc-sections \
	-Wl,--hash-style=sysv -Wl,-z,max-page-size=0x1000 \
	-T $(RUNTIME)/astra_library.ld
INTERFACE_TARGET := build/m68k/libraries/interface.library
INTERFACE_OBJECTS := build/m68k/interface_library.o build/m68k/surface.o \
	build/m68k/shared_surface.o build/m68k/window.o build/m68k/port.o \
	build/m68k/resource.o build/m68k/runtime_syscall.o \
	build/m68k/runtime_memory.o build/m68k/runtime_syscall_asm.o
INPUT_TARGET := build/m68k/libraries/input.library
INPUT_OBJECTS := build/m68k/input_library.o build/m68k/pointer.o \
	build/m68k/port.o build/m68k/resource.o \
	build/m68k/runtime_memory.o build/m68k/runtime_syscall_asm.o
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

build/m68k/surface.o: $(GRAPHICS)/src/surface.c \
		$(GRAPHICS)/build/generated/astra_font8x8.inc \
		$(GRAPHICS)/build/generated/astra_ui_font.inc \
		$(GRAPHICS)/build/generated/astra_mono_font.inc
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(GRAPHICS)/build/generated $(COMMON_FLAGS) \
		$(LIBRARY_FLAGS) -c $< -o $@

build/m68k/shared_surface.o: $(GRAPHICS)/src/shared_surface.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) -c $< -o $@

build/m68k/%.o: $(NDK)/src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) -c $< -o $@

build/m68k/runtime_%.o: $(RUNTIME)/src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) -c $< -o $@

build/m68k/runtime_syscall_asm.o: $(RUNTIME)/src/m68k/syscall.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -m68030 -msoft-float -fPIC -c $< -o $@

$(INTERFACE_TARGET): $(INTERFACE_OBJECTS) $(RUNTIME)/astra_library.ld
	@mkdir -p $(@D)
	$(CC) $(LIBRARY_FLAGS) $(LIBRARY_LINK_FLAGS) \
		-Wl,-soname,interface.library.1 -o $@ $(INTERFACE_OBJECTS) -lgcc

$(INPUT_TARGET): $(INPUT_OBJECTS) $(RUNTIME)/astra_library.ld
	@mkdir -p $(@D)
	$(CC) $(LIBRARY_FLAGS) $(LIBRARY_LINK_FLAGS) \
		-Wl,-soname,input.library.1 -o $@ $(INPUT_OBJECTS) -lgcc

library-contract: $(INTERFACE_TARGET) $(INPUT_TARGET)
	@python3 ../../../tools/library_info.py $(INTERFACE_TARGET) | \
		grep -q '^interface.library 1.1.0 ABI 1.1$$'
	@python3 ../../../tools/library_info.py $(INPUT_TARGET) | \
		grep -q '^input.library 1.0.0 ABI 1.0$$'
	@! $(READELF) -r $(INTERFACE_TARGET) | \
		grep -E 'R_68K_(GLOB_DAT|JMP_SLOT|32|PC32)'
	@! $(READELF) -r $(INPUT_TARGET) | \
		grep -E 'R_68K_(GLOB_DAT|JMP_SLOT|32|PC32)'

$(INTERFACE_HOST_TEST): src/interface_library.c tests/test_interface.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O2 -DASTRA_INTERFACE_TEST \
		-DASTRA_NDK_TEST \
		-ffunction-sections -Wl,--gc-sections $^ -o $@

$(INPUT_HOST_TEST): src/input_library.c tests/test_input_library.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O2 $^ -o $@

test: $(INTERFACE_HOST_TEST) $(INPUT_HOST_TEST)
	./$(INTERFACE_HOST_TEST)
	./$(INPUT_HOST_TEST)

sanitize analyze:
	@true

clean:
	rm -rf build

.PHONY: all test sanitize analyze library-contract clean
