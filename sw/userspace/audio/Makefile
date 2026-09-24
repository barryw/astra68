include ../../../mk/m68k-cross.mk
include ../../../mk/astra-shared-library.mk

CC := $(CROSS)gcc
HOST_CC ?= cc
READELF := $(CROSS)readelf
RUNTIME := ../runtime
COMPILER := ../compiler
NDK := ../../../ndk
CPPFLAGS := -I$(NDK)/include -I../../include -I$(RUNTIME)/include
FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic \
	$(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_SHARED_PIC_FLAGS)
TARGET := build/m68k/libraries/pcm.library.2
OBJECTS := build/m68k/pcm_library.o build/m68k/pcm_library_identity.o
EXPORTS := pcm.library.exports
RUNTIME_LIBRARY := $(RUNTIME)/build/m68k/libraries/runtime.library.1
COMPILER_LIBRARY := $(COMPILER)/build/m68k/libraries/compiler.library.1

all: $(TARGET) library-contract
library: $(TARGET)

build/m68k/%.o: src/%.c Makefile $(NDK)/include/astra/pcm.h \
		../../include/astra/pcm_service.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) -MMD -MP -c $< -o $@

runtime-library-ready:
	$(MAKE) -C $(RUNTIME) library

$(TARGET): $(OBJECTS) $(EXPORTS) $(ASTRA_SHARED_LINK_INPUTS) | runtime-library-ready
	@mkdir -p $(@D)
	$(CC) $(ASTRA_TARGET_ABI_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,pcm.library.2 -o $@ \
		-Wl,--version-script=$(EXPORTS) $(OBJECTS) \
		-L$(dir $(RUNTIME_LIBRARY)) -L$(dir $(COMPILER_LIBRARY)) \
		-Wl,--no-as-needed -Wl,-l:runtime.library.1 \
		-Wl,-l:compiler.library.1 $(ASTRA_SHARED_LINK_END)

library-contract: $(TARGET)
	@python3 ../../../tools/check_shared_library_layout.py $(TARGET)
	@python3 ../../../tools/check_library_exports.py $(EXPORTS) \
		$(NDK)/include/astra/pcm.h
	@python3 ../../../tools/check_elf_exports.py --readelf $(READELF) \
		$(EXPORTS) $(TARGET)
	@$(READELF) -dW $(TARGET) | grep -Fq 'Library soname: [pcm.library.2]'
	@$(READELF) -dW $(TARGET) | grep -q 'BIND_NOW'
	@! $(READELF) -dW $(TARGET) | grep -q 'TEXTREL'
	@$(READELF) -lW $(TARGET) | grep -q ' GNU_RELRO '
	@echo 'astra PCM shared library: PASS'

build/host/test_pcm_library: src/pcm_library.c tests/test_pcm_library.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) -std=c11 -Wall -Wextra -Werror -pedantic \
		-O2 $^ -o $@

test: library-contract build/host/test_pcm_library
	./build/host/test_pcm_library

sanitize: library-contract
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) -std=c11 -Wall -Wextra -Werror -pedantic \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		src/pcm_library.c tests/test_pcm_library.c \
		-o build/host/test_pcm_library_sanitize
	./build/host/test_pcm_library_sanitize

analyze: library-contract
	@mkdir -p build/analyzer
	$(HOST_CC) $(CPPFLAGS) -std=c11 -Wall -Wextra -Werror -pedantic \
		-fanalyzer -c src/pcm_library.c -o build/analyzer/pcm_library.o

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)
.PHONY: all library library-contract test sanitize analyze clean runtime-library-ready
