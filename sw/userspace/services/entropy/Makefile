include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
READELF := $(CROSS)readelf
HOST_CC ?= cc
RUNTIME := ../../runtime
include $(RUNTIME)/astra-link.mk
include ../../libraries.mk
NDK := ../../../../ndk
CPPFLAGS := -I. -I../../../include -I$(RUNTIME)/include -I$(NDK)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC $(ASTRA_DEBUG_FLAGS)
TARGET := build/m68k/entropy.elf
IMAGE := build/m68k/entropy
OBJECTS := build/m68k/main.o build/m68k/entropy_service.o
LIBRARIES := $(ASTRA_RUNTIME_LIBRARY) $(ASTRA_COMPILER_LIBRARY)
HOST_TEST := build/host/test_entropy_service
include ../../dynamic-program.mk

build/m68k/%.o: %.c entropy_service.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

$(HOST_TEST): entropy_service.c entropy_service.h tests/test_entropy_service.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O2 \
		entropy_service.c tests/test_entropy_service.c -o $@

test: $(HOST_TEST)
	./$(HOST_TEST)

sanitize: $(HOST_TEST)
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O2 \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		entropy_service.c tests/test_entropy_service.c \
		-o build/host/test_entropy_service_sanitize
	./build/host/test_entropy_service_sanitize

analyze:
	@mkdir -p build/analyzer
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O2 -fanalyzer \
		-c entropy_service.c -o build/analyzer/entropy_service.o

clean:
	rm -rf build

-include $(OBJECTS:.o=.d)

.PHONY: size test sanitize analyze clean
