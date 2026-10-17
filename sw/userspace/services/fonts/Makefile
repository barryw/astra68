include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
READELF := $(CROSS)readelf
RUNTIME := ../../runtime
include $(RUNTIME)/astra-link.mk
include ../../libraries.mk
NDK := ../../../../ndk
COMMON := ../../../common/build/m68k/libastraafnt.a
HOST_COMMON := ../../../common/build/host/libastraafnt.a
CPPFLAGS := -I$(NDK)/include -I../../../include -I$(RUNTIME)/include
FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic \
	$(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC $(ASTRA_DEBUG_FLAGS)
TARGET := build/m68k/fonts.elf
IMAGE := build/m68k/fonts
OBJECTS := build/m68k/main.o build/m68k/policy.o
ASTRA_PROGRAM_PRIVATE_INPUTS := $(COMMON)
LIBRARIES := $(ASTRA_CONFIG_LIBRARY) $(ASTRA_LIBC_LIBRARY) \
	$(ASTRA_RUNTIME_LIBRARY) $(ASTRA_FILESYSTEM_LIBRARY) \
	$(ASTRA_COMPILER_LIBRARY)
include ../../dynamic-program.mk

$(COMMON): ../../../common/font_catalog.c ../../../common/afnt.c \
		../../../common/crc32.c ../../../common/utf8.c
	$(MAKE) -C ../../../common m68k

$(HOST_COMMON): ../../../common/font_catalog.c ../../../common/afnt.c \
		../../../common/crc32.c ../../../common/utf8.c
	$(MAKE) -C ../../../common host

build/m68k/main.o: main.c policy.h ../../../include/astra/font_service.h \
		../../../include/astra/font_catalog.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) -MMD -MP -c $< -o $@

build/m68k/policy.o: policy.c policy.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) -MMD -MP -c $< -o $@

build/host/test_policy: policy.c policy.h tests/test_policy.c $(HOST_COMMON)
	@mkdir -p $(@D)
	cc $(CPPFLAGS) -std=c11 -Wall -Wextra -Werror -pedantic \
		-O2 policy.c tests/test_policy.c $(HOST_COMMON) -o $@

test: $(TARGET) build/host/test_policy
	./build/host/test_policy

sanitize: test
	cc $(CPPFLAGS) -std=c11 -Wall -Wextra -Werror -pedantic \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		policy.c ../../../common/font_catalog.c ../../../common/afnt.c \
		../../../common/crc32.c ../../../common/utf8.c tests/test_policy.c \
		-o build/host/test_policy_sanitize
	./build/host/test_policy_sanitize

analyze: $(TARGET)
	@mkdir -p build/analyzer
	$(CC) $(CPPFLAGS) $(FLAGS) -fanalyzer -c main.c \
		-o build/analyzer/main.o
	$(CC) $(CPPFLAGS) $(FLAGS) -fanalyzer -c policy.c \
		-o build/analyzer/policy.o

clean:
	rm -rf build

-include $(OBJECTS:.o=.d)
.PHONY: test sanitize analyze clean
