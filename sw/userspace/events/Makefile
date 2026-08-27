include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
READELF := $(CROSS)readelf
HOST_CC ?= cc
ANALYZER_CC ?= gcc
RUNTIME := ../runtime
VFS := ../vfs
NDK := ../../../ndk
CPPFLAGS := -Iinclude -I../../include -I$(RUNTIME)/include -I$(VFS)/include \
	-I$(NDK)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
LIBRARY_FLAGS := $(TARGET_FLAGS) -fPIC -fvisibility=hidden
LIBRARY_LINK_FLAGS := -nostdlib -shared -Wl,-Bsymbolic \
	-Wl,--no-undefined -Wl,--build-id=none -Wl,--gc-sections \
	-Wl,--hash-style=sysv -Wl,-z,max-page-size=0x1000 \
	-T $(RUNTIME)/astra_library.ld
HOST_FLAGS := -O2
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

HEADERS := include/astra/event_store.h include/astra/event_backend.h \
	include/astra/event_persist.h ../../include/astra/crc32.h \
	$(RUNTIME)/include/astra/event_catalog.h \
	../../include/astra/event.h ../../include/astra/event_descriptor.h

TARGET := build/m68k/libastraevents.a
SOURCES := src/event_store.c src/event_backend.c src/event_persist.c
TARGET_OBJECTS := $(patsubst src/%.c,build/m68k/%.o,$(SOURCES)) \
	build/m68k/crc32.o
ANALYZER_SOURCES := $(SOURCES) src/events_library.c
ANALYZER_OBJECTS := $(patsubst src/%.c,build/analyzer/%.o,$(ANALYZER_SOURCES)) \
	build/analyzer/common_crc32.o
EVENTS_LIBRARY := build/m68k/libraries/events.library
EVENTS_LIBRARY_OBJECTS := build/m68k/library/events_library.o
RUNTIME_PIC_LIB := $(RUNTIME)/build/m68k/libastrart-pic.a
STORE_TEST := build/host/test_event_store
BACKEND_TEST := build/host/test_event_backend
# The catalog reader comes along because an eviction is attributed by looking a
# message id up in it, so the two cannot be tested apart.
STORE_TEST_SOURCES := src/event_store.c src/event_persist.c \
	../../common/crc32.c $(RUNTIME)/src/event_catalog.c tests/test_event_store.c

all: $(TARGET) $(EVENTS_LIBRARY) library-contract

$(TARGET): $(TARGET_OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

build/m68k/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/crc32.o: ../../common/crc32.c ../../include/astra/crc32.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/library/events_library.o: src/events_library.c \
		$(NDK)/include/astra/events_library.h ../../include/astra/library.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) -c $< -o $@

$(RUNTIME_PIC_LIB):
	$(MAKE) -C $(RUNTIME) pic

$(EVENTS_LIBRARY): $(EVENTS_LIBRARY_OBJECTS) $(RUNTIME_PIC_LIB) \
		$(RUNTIME)/astra_library.ld
	@mkdir -p $(@D)
	$(CC) $(LIBRARY_FLAGS) $(LIBRARY_LINK_FLAGS) \
		-Wl,-soname,events.library.1 -o $@ $(EVENTS_LIBRARY_OBJECTS) \
		$(RUNTIME_PIC_LIB) -lgcc

library-contract: $(EVENTS_LIBRARY)
	@python3 ../../../tools/library_info.py $(EVENTS_LIBRARY) | \
		grep -q '^events.library 1.0.0 ABI 1.0$$'
	@! $(READELF) -r $(EVENTS_LIBRARY) | \
		grep -E 'R_68K_(GLOB_DAT|JMP_SLOT|32|PC32)'

BACKEND_TEST_SOURCES := src/event_backend.c src/event_store.c \
	$(RUNTIME)/src/event_catalog.c tests/test_event_backend.c

$(STORE_TEST): $(STORE_TEST_SOURCES) $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(STORE_TEST_SOURCES) -o $@

$(BACKEND_TEST): $(BACKEND_TEST_SOURCES) $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(BACKEND_TEST_SOURCES) -o $@

test: $(STORE_TEST) $(BACKEND_TEST)
	./$(STORE_TEST)
	./$(BACKEND_TEST)

# Ring arithmetic over a caller-owned array, which is exactly the shape a
# sanitizer catches and a reviewer does not.
sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) $(STORE_TEST_SOURCES) \
		-o build/host/test_event_store_sanitize
	./build/host/test_event_store_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) $(BACKEND_TEST_SOURCES) \
		-o build/host/test_event_backend_sanitize
	./build/host/test_event_backend_sanitize

analyze: $(ANALYZER_OBJECTS)

build/analyzer/%.o: src/%.c
	@mkdir -p $(@D)
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c $< -o $@

build/analyzer/common_crc32.o: ../../common/crc32.c
	@mkdir -p $(@D)
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c $< -o $@

# Header dependencies: a stale object compiled against an older ABI
# constant is indistinguishable from a working one until it runs.
-include $(wildcard build/m68k/*.d build/m68k/*/*.d)

clean:
	rm -rf build

.PHONY: all test sanitize analyze clean library-contract
