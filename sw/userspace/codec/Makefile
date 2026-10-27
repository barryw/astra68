include ../../../mk/m68k-cross.mk
include ../../../mk/astra-shared-library.mk

CC := $(CROSS)gcc
READELF := $(CROSS)readelf
RUNTIME := ../runtime
COMPILER := ../compiler
NDK := ../../../ndk
CPPFLAGS := -I$(NDK)/include -I../../include -I$(RUNTIME)/include
FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic \
	$(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_SHARED_PIC_FLAGS)
TARGET := build/m68k/libraries/codec.library.1
OBJECTS := build/m68k/codec_library.o build/m68k/codec_library_identity.o
EXPORTS := codec.library.exports
RUNTIME_LIBRARY := $(RUNTIME)/build/m68k/libraries/runtime.library.1
COMPILER_LIBRARY := $(COMPILER)/build/m68k/libraries/compiler.library.2

all: $(TARGET) library-contract
library: $(TARGET)

build/m68k/%.o: src/%.c Makefile $(NDK)/include/astra/codec.h \
		../../include/astra/codec_service.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) -MMD -MP -c $< -o $@

runtime-library-ready:
	$(MAKE) -C $(RUNTIME) library

$(TARGET): $(OBJECTS) $(EXPORTS) $(ASTRA_SHARED_LINK_INPUTS) | runtime-library-ready
	@mkdir -p $(@D)
	$(CC) $(ASTRA_TARGET_ABI_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,codec.library.1 -o $@ \
		-Wl,--version-script=$(EXPORTS) $(OBJECTS) \
		-L$(dir $(RUNTIME_LIBRARY)) -L$(dir $(COMPILER_LIBRARY)) \
		-Wl,--no-as-needed -Wl,-l:runtime.library.1 \
		-Wl,-l:compiler.library.2 $(ASTRA_SHARED_LINK_END)

library-contract: $(TARGET)
	@python3 ../../../tools/check_shared_library_layout.py $(TARGET)
	@python3 ../../../tools/check_library_exports.py $(EXPORTS) \
		$(NDK)/include/astra/codec.h
	@python3 ../../../tools/check_elf_exports.py --readelf $(READELF) \
		$(EXPORTS) $(TARGET)
	@$(READELF) -dW $(TARGET) | grep -Fq 'Library soname: [codec.library.1]'
	@$(READELF) -dW $(TARGET) | grep -q 'BIND_NOW'
	@! $(READELF) -dW $(TARGET) | grep -q 'TEXTREL'
	@$(READELF) -lW $(TARGET) | grep -q ' GNU_RELRO '

test: library-contract
sanitize: test
analyze: library-contract
clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)
.PHONY: all library library-contract test sanitize analyze clean runtime-library-ready
