include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
HOST_CC ?= cc
ANALYZER_CC ?= gcc
RUNTIME := ../runtime
VFS := ../vfs
CPPFLAGS := -Iinclude -I../../include -I$(RUNTIME)/include -I$(VFS)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
HOST_FLAGS := -O2
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

HEADERS := include/astra/event_store.h \
	$(RUNTIME)/include/astra/event_catalog.h \
	../../include/astra/event.h ../../include/astra/event_descriptor.h

TARGET := build/m68k/libastraevents.a
TARGET_OBJECTS := build/m68k/event_store.o
STORE_TEST := build/host/test_event_store
# The catalog reader comes along because an eviction is attributed by looking a
# message id up in it, so the two cannot be tested apart.
STORE_TEST_SOURCES := src/event_store.c $(RUNTIME)/src/event_catalog.c \
	tests/test_event_store.c

all: $(TARGET)

$(TARGET): $(TARGET_OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

build/m68k/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

$(STORE_TEST): $(STORE_TEST_SOURCES) $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(STORE_TEST_SOURCES) -o $@

test: $(STORE_TEST)
	./$(STORE_TEST)

# Ring arithmetic over a caller-owned array, which is exactly the shape a
# sanitizer catches and a reviewer does not.
sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) $(STORE_TEST_SOURCES) \
		-o build/host/test_event_store_sanitize
	./build/host/test_event_store_sanitize

analyze:
	@mkdir -p build/analyzer
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/event_store.c -o build/analyzer/event_store.o

# Header dependencies: a stale object compiled against an older ABI
# constant is indistinguishable from a working one until it runs.
-include $(wildcard build/m68k/*.d build/m68k/*/*.d)

clean:
	rm -rf build

.PHONY: all test sanitize analyze clean
