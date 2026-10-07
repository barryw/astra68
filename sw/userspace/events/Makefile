include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
HOST_CC ?= cc
ANALYZER_CC ?= gcc
RUNTIME := ../runtime
VFS := ../vfs
NDK := ../../../ndk
CPPFLAGS := -Iinclude -I../../include -I$(RUNTIME)/include -I$(VFS)/include \
	-I$(NDK)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC -ftls-model=initial-exec \
	$(ASTRA_DEBUG_FLAGS)
HOST_FLAGS := -O2
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

HEADERS := include/astra/event_store.h include/astra/event_backend.h \
	include/astra/event_persist.h include/astra/log_format.h \
	include/astra/log_file.h include/astra/log_clients.h \
	include/astra/log_names.h \
	../../include/astra/crc32.h \
	$(NDK)/include/astra/event_catalog.h \
	../../include/astra/event.h ../../include/astra/event_descriptor.h

TARGET := build/m68k/libastraevents.a
SOURCES := src/event_store.c src/event_backend.c src/event_persist.c \
	src/log_format.c src/log_file.c src/log_clients.c src/log_names.c
TARGET_OBJECTS := $(patsubst src/%.c,build/m68k/%.o,$(SOURCES)) \
	build/m68k/crc32.o
ANALYZER_SOURCES := $(SOURCES)
ANALYZER_OBJECTS := $(patsubst src/%.c,build/analyzer/%.o,$(ANALYZER_SOURCES)) \
	build/analyzer/common_crc32.o
STORE_TEST := build/host/test_event_store
BACKEND_TEST := build/host/test_event_backend
LOG_FILE_TEST := build/host/test_log_file
LOG_CLIENTS_TEST := build/host/test_log_clients
LOG_NAMES_TEST := build/host/test_log_names
# The catalog reader comes along because an eviction is attributed by looking a
# message id up in it, so the two cannot be tested apart.
STORE_TEST_SOURCES := src/event_store.c src/event_persist.c \
	../../common/crc32.c $(RUNTIME)/src/event_catalog.c tests/test_event_store.c
LOG_FILE_TEST_SOURCES := src/log_format.c src/log_file.c \
	tests/test_log_file.c ../../common/civil.c ../../common/divide.c
LOG_CLIENTS_TEST_SOURCES := src/log_clients.c src/log_format.c \
	$(RUNTIME)/src/log_ring.c tests/test_log_clients.c \
	../../common/civil.c ../../common/divide.c
LOG_NAMES_TEST_SOURCES := src/log_names.c ../../common/crc32.c \
	tests/test_log_names.c

all: $(TARGET)

$(TARGET): $(TARGET_OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

build/m68k/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/crc32.o: ../../common/crc32.c ../../include/astra/crc32.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

$(TARGET_OBJECTS): Makefile

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

$(LOG_FILE_TEST): $(LOG_FILE_TEST_SOURCES) include/astra/log_format.h \
		include/astra/log_file.h
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(LOG_FILE_TEST_SOURCES) -o $@

$(LOG_CLIENTS_TEST): $(LOG_CLIENTS_TEST_SOURCES) include/astra/log_clients.h \
		include/astra/log_format.h
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(LOG_CLIENTS_TEST_SOURCES) -o $@

$(LOG_NAMES_TEST): $(LOG_NAMES_TEST_SOURCES) include/astra/log_names.h
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(LOG_NAMES_TEST_SOURCES) -o $@

test: $(STORE_TEST) $(BACKEND_TEST) $(LOG_FILE_TEST) $(LOG_CLIENTS_TEST) \
		$(LOG_NAMES_TEST)
	./$(STORE_TEST)
	./$(BACKEND_TEST)
	./$(LOG_FILE_TEST)
	./$(LOG_CLIENTS_TEST)
	./$(LOG_NAMES_TEST)

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
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) $(LOG_FILE_TEST_SOURCES) \
		-o build/host/test_log_file_sanitize
	./build/host/test_log_file_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) $(LOG_CLIENTS_TEST_SOURCES) \
		-o build/host/test_log_clients_sanitize
	./build/host/test_log_clients_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) $(LOG_NAMES_TEST_SOURCES) \
		-o build/host/test_log_names_sanitize
	./build/host/test_log_names_sanitize

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

.PHONY: all test sanitize analyze clean
