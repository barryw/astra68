include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
HOST_CC ?= cc
ANALYZER_CC ?= gcc
LWEXT4 := ../../../third_party/lwext4
RUNTIME := ../runtime
STORAGE := ../storage
CPPFLAGS := -Iinclude -I../../include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
HOST_FLAGS := -O2
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

# The core speaks no filesystem and needs no lwext4 header. Keeping its
# include path clean is what stops a backend detail leaking into it by
# accident, so the two are compiled with different flags on purpose.
CORE_SOURCES := src/vfs_service_core.c src/vfs_client.c \
	src/vfs_local_transport.c src/vfs_assign.c src/vfs_path.c
EXT4_CPPFLAGS := -I$(LWEXT4)/include -I$(STORAGE)/port/include \
	-DCONFIG_USE_DEFAULT_CFG=0 -I$(RUNTIME)/freestanding

HEADERS := include/astra/vfs_service_core.h include/astra/vfs_backend.h \
	include/astra/vfs_client.h include/astra/vfs_local_transport.h \
	include/astra/vfs_assign.h include/astra/vfs_path.h \
	../../include/astra/vfs_service.h \
	../../include/astra/syscall.h

TARGET := build/m68k/libastravfs.a
TARGET_OBJECTS := build/m68k/vfs_service_core.o build/m68k/vfs_client.o \
	build/m68k/vfs_assign.o build/m68k/vfs_path.o \
	build/m68k/vfs_local_transport.o build/m68k/vfs_ext4_backend.o
HOST_TEST := build/host/test_vfs_service_core

all: $(TARGET)

$(TARGET): $(TARGET_OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

build/m68k/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

# The one translation unit that is allowed to know what lwext4 is.
build/m68k/vfs_ext4_backend.o: src/vfs_ext4_backend.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(EXT4_CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) \
		$(DEPFLAGS) -c $< -o $@

$(HOST_TEST): $(CORE_SOURCES) tests/test_vfs_service_core.c $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/vfs_service_core.c src/vfs_client.c \
		src/vfs_local_transport.c tests/test_vfs_service_core.c -o $@

ASSIGN_TEST := build/host/test_vfs_assign

$(ASSIGN_TEST): src/vfs_assign.c tests/test_vfs_assign.c $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/vfs_assign.c tests/test_vfs_assign.c -o $@

PATH_TEST := build/host/test_vfs_path

$(PATH_TEST): src/vfs_path.c tests/test_vfs_path.c $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/vfs_path.c tests/test_vfs_path.c -o $@

test: $(HOST_TEST) $(ASSIGN_TEST) $(PATH_TEST)
	./$(HOST_TEST)
	./$(ASSIGN_TEST)
	./$(PATH_TEST)

# The path parser is nothing but bounds arithmetic over caller buffers, so it
# is the one piece here where a sanitizer earns its run.
sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/vfs_service_core.c src/vfs_client.c \
		src/vfs_local_transport.c tests/test_vfs_service_core.c \
		-o build/host/test_vfs_service_core_sanitize
	./build/host/test_vfs_service_core_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/vfs_path.c tests/test_vfs_path.c \
		-o build/host/test_vfs_path_sanitize
	./build/host/test_vfs_path_sanitize

analyze:
	@mkdir -p build/analyzer
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/vfs_service_core.c -o build/analyzer/vfs_service_core.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/vfs_client.c -o build/analyzer/vfs_client.o

# Header dependencies: a stale object compiled against an older ABI
# constant is indistinguishable from a working one until it runs.
-include $(wildcard build/m68k/*.d build/m68k/*/*.d)

clean:
	rm -rf build

.PHONY: all test sanitize analyze clean
