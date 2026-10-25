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
OBJECTS := build/m68k/main.o build/m68k/policy.o build/m68k/host.o
ASTRA_PROGRAM_PRIVATE_INPUTS := $(COMMON)
FONT_HEADERS := $(NDK)/include/astra/font_import.h ../../../include/astra/font_service.h
LIBRARIES := $(ASTRA_CONFIG_LIBRARY) $(ASTRA_LIBC_LIBRARY) \
	$(ASTRA_RUNTIME_LIBRARY) $(ASTRA_FILESYSTEM_LIBRARY) \
	$(ASTRA_COMPILER_LIBRARY)
include ../../dynamic-program.mk

$(COMMON): ../../../common/font_catalog.c ../../../common/afnt.c ../../../common/json.c \
		../../../include/astra/afnt.h ../../../include/astra/json.h ../../../common/Makefile \
		../../../common/crc32.c ../../../common/utf8.c \
		../../../common/sha256.c ../../../common/font_store.c
	$(MAKE) -C ../../../common m68k

$(HOST_COMMON): ../../../common/font_catalog.c ../../../common/afnt.c ../../../common/json.c \
		../../../include/astra/afnt.h ../../../include/astra/json.h ../../../common/Makefile \
		../../../common/crc32.c ../../../common/utf8.c \
		../../../common/sha256.c ../../../common/font_store.c
	$(MAKE) -C ../../../common host

build/m68k/main.o: main.c policy.h ../../../include/astra/font_service.h \
		../../../include/astra/font_catalog.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) -MMD -MP -c $< -o $@

build/m68k/policy.o: policy.c policy.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) -MMD -MP -c $< -o $@

build/m68k/host.o: host.c host.h ../../../include/astra/host.h \
		../../../include/astra/font_worker.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) -MMD -MP -c $< -o $@

build/host/test_policy: policy.c policy.h tests/test_policy.c $(HOST_COMMON) $(FONT_HEADERS)
	@mkdir -p $(@D)
	cc $(CPPFLAGS) -std=c11 -Wall -Wextra -Werror -pedantic \
		-O2 policy.c tests/test_policy.c $(HOST_COMMON) -o $@

build/host/test_selection: tests/test_selection.c main.c policy.c policy.h $(HOST_COMMON) $(FONT_HEADERS)
	@mkdir -p $(@D)
	cc $(CPPFLAGS) -std=c11 -Wall -Wextra -Werror -pedantic -O2 \
		-ffunction-sections -fdata-sections tests/test_selection.c policy.c \
		-Wl,--gc-sections $(HOST_COMMON) -o $@

build/host/test_import: tests/test_import.c main.c policy.c policy.h $(HOST_COMMON) $(FONT_HEADERS)
	@mkdir -p $(@D)
	cc $(CPPFLAGS) -std=c11 -Wall -Wextra -Werror -pedantic -O2 \
		-ffunction-sections -fdata-sections tests/test_import.c policy.c \
		-Wl,--gc-sections $(HOST_COMMON) -o $@

test: $(TARGET) build/host/test_policy build/host/test_selection build/host/test_import
	./build/host/test_policy
	./build/host/test_selection
	./build/host/test_import

sanitize: test
	cc $(CPPFLAGS) -std=c11 -Wall -Wextra -Werror -pedantic \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		policy.c ../../../common/font_catalog.c ../../../common/afnt.c ../../../common/json.c \
		../../../common/crc32.c ../../../common/utf8.c \
		../../../common/sha256.c ../../../common/font_store.c \
		tests/test_policy.c \
		-o build/host/test_policy_sanitize
	./build/host/test_policy_sanitize
	cc $(CPPFLAGS) -std=c11 -Wall -Wextra -Werror -pedantic \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-ffunction-sections -fdata-sections tests/test_selection.c policy.c \
		-Wl,--gc-sections $(HOST_COMMON) -o build/host/test_selection_sanitize
	./build/host/test_selection_sanitize
	cc $(CPPFLAGS) -std=c11 -Wall -Wextra -Werror -pedantic \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-ffunction-sections -fdata-sections tests/test_import.c policy.c \
		-Wl,--gc-sections $(HOST_COMMON) -o build/host/test_import_sanitize
	./build/host/test_import_sanitize

analyze: $(TARGET)
	@mkdir -p build/analyzer
	$(CC) $(CPPFLAGS) $(FLAGS) -fanalyzer -c main.c \
		-o build/analyzer/main.o
	$(CC) $(CPPFLAGS) $(FLAGS) -fanalyzer -c policy.c \
		-o build/analyzer/policy.o
	$(CC) $(CPPFLAGS) $(FLAGS) -fanalyzer -c host.c \
		-o build/analyzer/host.o

clean:
	rm -rf build

-include $(OBJECTS:.o=.d)
.PHONY: test sanitize analyze clean
