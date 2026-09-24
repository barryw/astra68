include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
READELF := $(CROSS)readelf
RUNTIME := ../../runtime
include $(RUNTIME)/astra-link.mk
include ../../libraries.mk
NDK := ../../../../ndk
CPPFLAGS := -I$(NDK)/include -I../../../include -I$(RUNTIME)/include
FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic \
	$(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC $(ASTRA_DEBUG_FLAGS)
TARGET := build/m68k/media.elf
IMAGE := build/m68k/media
OBJECT := build/m68k/main.o
ASTRA_PROGRAM_PRIVATE_INPUTS := build/m68k/pcm_protocol.o
LIBRARIES := $(ASTRA_RUNTIME_LIBRARY) $(ASTRA_COMPILER_LIBRARY)
include ../../dynamic-program.mk

$(OBJECT): main.c ../../../include/astra/pcm_service.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) -MMD -MP -c $< -o $@

build/m68k/pcm_protocol.o: pcm_protocol.c pcm_protocol.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) -MMD -MP -c $< -o $@

build/host/test_pcm_protocol: pcm_protocol.c tests/test_pcm_protocol.c
	@mkdir -p $(@D)
	cc $(CPPFLAGS) -std=c11 -Wall -Wextra -Werror -pedantic \
		-O2 $^ -o $@

test: $(TARGET) build/host/test_pcm_protocol
	./build/host/test_pcm_protocol

sanitize: build/host/test_pcm_protocol
	cc $(CPPFLAGS) -std=c11 -Wall -Wextra -Werror -pedantic \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		pcm_protocol.c tests/test_pcm_protocol.c \
		-o build/host/test_pcm_protocol_sanitize
	./build/host/test_pcm_protocol_sanitize

analyze: $(TARGET)
	@mkdir -p build/analyzer
	cc $(CPPFLAGS) -std=c11 -Wall -Wextra -Werror -pedantic \
		-fanalyzer -c pcm_protocol.c -o build/analyzer/pcm_protocol.o

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)
.PHONY: test sanitize analyze clean
