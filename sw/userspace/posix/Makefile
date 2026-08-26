include ../../../mk/m68k-cross.mk

# The POSIX layer: descriptors, and later a cwd, over capabilities and the VFS.
#
# A separate library rather than part of the runtime, because the runtime is
# what every Astra program links and this is what a *Unix* program needs. A
# command written against the NDK must not pay for printf it never calls.

CC := $(CROSS)gcc
AR := $(CROSS)ar
HOST_CC ?= cc
RUNTIME := ../runtime
STREAMS := ../streams
VFS := ../vfs
NDK := ../../../ndk
PICOLIBC ?= $(HOME)/picolibc-astra
include kit/astra-posix.mk
CPPFLAGS := -I include -I../../include -I$(RUNTIME)/include \
	-I$(STREAMS)/include -I$(VFS)/include -I$(NDK)/include \
	-isystem $(PICOLIBC)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)

SOURCES := src/startup.c src/console.c src/poll.c src/terminal.c src/file.c src/heap.c src/alloc.c src/time.c \
	src/system.c src/scheduling.c src/signal.c \
	src/identity.c src/process.c src/strftime_format.c src/path.c
OBJECTS := $(patsubst src/%.c,build/m68k/%.o,$(SOURCES))
ANALYZER_OBJECTS := $(patsubst src/%.c,build/analyzer/%.o,$(SOURCES))
LIB := build/m68k/libastraposix.a
RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart.a
RUNTIME_CRT0 := $(RUNTIME)/build/m68k/crt0.o
STREAMS_LIB := $(STREAMS)/build/m68k/libastrastreams.a
VFS_LIB := $(VFS)/build/m68k/libastravfs.a
LINK_CONTRACT := build/m68k/link-standard-main.elf
HOST_TEST := build/host/test_strftime_format
PATH_TEST := build/host/test_path
DESCRIPTOR_TEST := build/host/test_descriptor
STARTUP_TEST := build/host/test_startup
POLL_TEST := build/host/test_poll
IDENTITY_TEST := build/host/test_identity
PROCESS_TEST := build/host/test_process
LIMITS_CONTRACT := build/m68k/limits-contract.o

all: $(LIB)

$(LIB): $(OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $(OBJECTS)

build/m68k/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

$(HOST_TEST): src/strftime_format.c tests/test_strftime_format.c
	@mkdir -p $(@D)
	$(HOST_CC) -Iinclude -std=c11 -Wall -Wextra -Werror -pedantic -O2 \
		src/strftime_format.c tests/test_strftime_format.c -o $@

$(PATH_TEST): src/path.c tests/test_path.c
	@mkdir -p $(@D)
	$(HOST_CC) -Isrc -I../../include -std=c11 -Wall -Wextra -Werror -pedantic -O2 \
	src/path.c tests/test_path.c -o $@

$(DESCRIPTOR_TEST): src/console.c $(RUNTIME)/src/startup.c tests/test_descriptor.c
	@mkdir -p $(@D)
	$(HOST_CC) -Iinclude -I../../include -I$(RUNTIME)/include \
		-I$(STREAMS)/include -I$(VFS)/include -I$(NDK)/include \
		$(COMMON_FLAGS) -O2 -no-pie -ffunction-sections \
		-Wl,--gc-sections src/console.c $(RUNTIME)/src/startup.c \
		tests/test_descriptor.c -o $@

$(STARTUP_TEST): src/startup.c tests/test_startup.c
	@mkdir -p $(@D)
	$(HOST_CC) -Iinclude -I../../include -I$(RUNTIME)/include \
		-I$(STREAMS)/include -I$(VFS)/include -I$(NDK)/include \
		$(COMMON_FLAGS) -O2 -no-pie src/startup.c tests/test_startup.c -o $@

$(POLL_TEST): src/console.c src/poll.c $(RUNTIME)/src/startup.c tests/test_poll.c
	@mkdir -p $(@D)
	$(HOST_CC) -Iinclude -I../../include -I$(RUNTIME)/include \
		-I$(STREAMS)/include -I$(VFS)/include -I$(NDK)/include \
		$(COMMON_FLAGS) -O2 -no-pie -ffunction-sections \
		-Wl,--gc-sections src/console.c src/poll.c $(RUNTIME)/src/startup.c \
		tests/test_poll.c -o $@

$(IDENTITY_TEST): src/identity.c tests/test_identity.c
	@mkdir -p $(@D)
	$(HOST_CC) $(COMMON_FLAGS) -O2 src/identity.c tests/test_identity.c -o $@

$(PROCESS_TEST): src/process.c tests/test_process.c
	@mkdir -p $(@D)
	$(HOST_CC) -I../../include -I$(RUNTIME)/include $(COMMON_FLAGS) -O2 \
		src/process.c tests/test_process.c -o $@

$(LIMITS_CONTRACT): tests/limits_contract.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -c $< -o $@

$(RUNTIME_LIB) $(RUNTIME_CRT0):
	$(MAKE) -C $(RUNTIME) all

$(STREAMS_LIB):
	$(MAKE) -C $(STREAMS) all

$(VFS_LIB):
	$(MAKE) -C $(VFS) all

$(LINK_CONTRACT): tests/link_standard_main.c kit/program.c $(LIB) \
		$(RUNTIME_LIB) $(RUNTIME_CRT0) $(STREAMS_LIB) $(VFS_LIB)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) \
		-DASTRA_POSIX_PROGRAM_NAME='"posix-link-test"' \
		-DASTRA_POSIX_PROGRAM_MAJOR=1 -DASTRA_POSIX_PROGRAM_MINOR=0 \
		-DASTRA_POSIX_PROGRAM_PATCH=0 \
		-DASTRA_POSIX_PROGRAM_AUTHOR='"Astra68"' \
		-DASTRA_POSIX_PROGRAM_COPYRIGHT='"Copyright 2026 Astra68 contributors"' \
		$(ASTRA_POSIX_LDFLAGS) \
		-o $@ $(ASTRA_POSIX_CRT0) tests/link_standard_main.c \
		$(ASTRA_POSIX_PROGRAM_SOURCE) $(ASTRA_POSIX_LIBS)
	@$(CROSS)nm $@ | grep -q ' T astra_main$$'
	@$(CROSS)nm $@ | grep -q ' T main$$'
	@echo "astra posix standard main link: PASS"

test: $(HOST_TEST) $(PATH_TEST) $(DESCRIPTOR_TEST) $(STARTUP_TEST) $(POLL_TEST) $(IDENTITY_TEST) $(PROCESS_TEST) $(LIMITS_CONTRACT) $(LINK_CONTRACT)
	./$(HOST_TEST)
	./$(PATH_TEST)
	./$(DESCRIPTOR_TEST)
	./$(STARTUP_TEST)
	./$(POLL_TEST)
	./$(IDENTITY_TEST)
	./$(PROCESS_TEST)

sanitize:
	@mkdir -p build/host
	$(HOST_CC) -Iinclude -std=c11 -Wall -Wextra -Werror -pedantic -O2 \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		src/strftime_format.c tests/test_strftime_format.c \
		-o build/host/test_strftime_format_sanitize
	./build/host/test_strftime_format_sanitize
	$(HOST_CC) -Isrc -I../../include -std=c11 -Wall -Wextra -Werror -pedantic -O2 \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		src/path.c tests/test_path.c -o build/host/test_path_sanitize
	./build/host/test_path_sanitize
	$(HOST_CC) -Iinclude -I../../include -I$(RUNTIME)/include \
		-I$(STREAMS)/include -I$(VFS)/include -I$(NDK)/include \
		$(COMMON_FLAGS) -O2 -ffunction-sections -Wl,--gc-sections \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
		-Dread=astra_posix_test_read -Dwrite=astra_posix_test_write \
		-Dpipe=astra_posix_test_pipe \
		src/console.c $(RUNTIME)/src/startup.c tests/test_descriptor.c \
		-o build/host/test_descriptor_sanitize
	./build/host/test_descriptor_sanitize

analyze: $(ANALYZER_OBJECTS)

# GCC models these names as host descriptors.  This file implements Astra's
# descriptor table, so keep every other analyzer check while removing that
# inapplicable name-based model.
build/analyzer/file.o: ANALYZER_CPPFLAGS := \
	-Dopen=astra_analyzer_open -Dclose=astra_analyzer_close \
	-Dexecve=astra_analyzer_execve
build/analyzer/alloc.o: ANALYZER_CPPFLAGS := \
	-Dmalloc=astra_analyzer_malloc -Dfree=astra_analyzer_free \
	-Dcalloc=astra_analyzer_calloc -Drealloc=astra_analyzer_realloc \
	-Dmalloc_usable_size=astra_analyzer_malloc_usable_size

build/analyzer/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ANALYZER_CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) \
		-fanalyzer -c $< -o $@

clean:
	rm -rf build

.PHONY: all test sanitize analyze clean

-include $(OBJECTS:.o=.d)
