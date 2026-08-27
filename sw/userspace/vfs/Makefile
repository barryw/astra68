include ../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
READELF := $(CROSS)readelf
HOST_CC ?= cc
ANALYZER_CC ?= gcc
LWEXT4 := ../../../third_party/lwext4
RUNTIME := ../runtime
STORAGE := ../storage
NDK := ../../../ndk
CPPFLAGS := -Iinclude -I../../include
PORT_CPPFLAGS := $(CPPFLAGS) -I$(RUNTIME)/include -I$(NDK)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
HOST_FLAGS := -O2
HOST_THREAD_FLAGS := -pthread
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
LIBRARY_LD := ../runtime/astra_library.ld
LIBRARY_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC -fvisibility=hidden
LIBRARY_LINK_FLAGS := -nostdlib -shared -Wl,-Bsymbolic \
	-Wl,--no-undefined -Wl,--build-id=none -Wl,--gc-sections \
	-Wl,--hash-style=sysv -Wl,-z,max-page-size=0x1000 -T $(LIBRARY_LD)

# The core speaks no filesystem and needs no lwext4 header. Keeping its
# include path clean is what stops a backend detail leaking into it by
# accident, so the two are compiled with different flags on purpose.
CORE_SOURCES := src/vfs_service_core.c src/vfs_client.c \
	src/vfs_local_transport.c src/vfs_port_transport.c src/vfs_assign.c \
	src/vfs_path.c src/vfs_union.c src/vfs_process.c src/vfs_reader.c \
	src/vfs_status.c
EXT4_CPPFLAGS := -I$(LWEXT4)/include -I$(STORAGE)/port/include \
	-DCONFIG_USE_DEFAULT_CFG=0 -I$(RUNTIME)/freestanding

HEADERS := include/astra/vfs_service_core.h include/astra/vfs_backend.h \
	include/astra/vfs_client.h include/astra/vfs_local_transport.h \
	include/astra/vfs_assign.h include/astra/vfs_path.h \
	include/astra/vfs_port_transport.h include/astra/vfs_union.h \
	include/astra/vfs_process.h include/astra/vfs_reader.h \
	$(NDK)/include/astra/shared_library.h \
	../../include/astra/vfs_service.h \
	../../include/astra/syscall.h

TARGET := build/m68k/libastravfs.a
TARGET_OBJECTS := build/m68k/vfs_service_core.o build/m68k/vfs_client.o \
	build/m68k/vfs_assign.o build/m68k/vfs_path.o \
	build/m68k/vfs_local_transport.o build/m68k/vfs_port_transport.o \
	build/m68k/vfs_ext4_backend.o build/m68k/vfs_union.o \
	build/m68k/vfs_process.o build/m68k/vfs_reader.o \
	$(NDK)/build/m68k/manifest.o $(NDK)/build/m68k/bundle.o \
	build/m68k/vfs_status.o
FILESYSTEM_LIBRARY := build/m68k/libraries/filesystem.library
FILESYSTEM_LIBRARY_OBJECTS := build/m68k/library/filesystem_library.o \
	build/m68k/library/vfs_client.o build/m68k/library/vfs_assign.o \
	build/m68k/library/vfs_path.o build/m68k/library/vfs_union.o

RUNTIME_PIC_LIB := $(RUNTIME)/build/m68k/libastrart-pic.a
HOST_TEST := build/host/test_vfs_service_core

all: $(TARGET) $(FILESYSTEM_LIBRARY) library-contract

$(TARGET): $(TARGET_OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

build/m68k/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

# The one translation unit here that makes syscalls, so the one that needs the
# runtime's declarations. The core deliberately does not: a service core that
# could reach the kernel directly would be one that could skip the protocol.
build/m68k/vfs_port_transport.o: src/vfs_port_transport.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

build/m68k/vfs_process.o: src/vfs_process.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

build/m68k/vfs_reader.o: src/vfs_reader.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

$(NDK)/build/m68k/manifest.o $(NDK)/build/m68k/bundle.o:
	$(MAKE) -C $(NDK) $(@:$(NDK)/%=%)

build/m68k/library/%.o: src/%.c $(HEADERS) \
		include/astra/filesystem_library.h
	@mkdir -p $(@D)
	$(CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

$(RUNTIME_PIC_LIB):
	$(MAKE) -C $(RUNTIME) pic

$(FILESYSTEM_LIBRARY): $(FILESYSTEM_LIBRARY_OBJECTS) $(RUNTIME_PIC_LIB) \
		$(LIBRARY_LD)
	@mkdir -p $(@D)
	$(CC) $(LIBRARY_FLAGS) $(LIBRARY_LINK_FLAGS) \
		-Wl,-soname,filesystem.library.1 -o $@ \
		$(FILESYSTEM_LIBRARY_OBJECTS) $(RUNTIME_PIC_LIB) -lgcc

library-contract: $(FILESYSTEM_LIBRARY)
	@python3 ../../../tools/library_info.py $(FILESYSTEM_LIBRARY) | \
		grep -q '^filesystem.library 1.4.0 ABI 1.4$$'
	@! $(READELF) -r $(FILESYSTEM_LIBRARY) | \
		grep -E 'R_68K_(GLOB_DAT|JMP_SLOT|32|PC32)'

# The one translation unit that is allowed to know what lwext4 is.
build/m68k/vfs_ext4_backend.o: src/vfs_ext4_backend.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(EXT4_CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) \
		$(DEPFLAGS) -c $< -o $@

$(HOST_TEST): $(CORE_SOURCES) tests/test_vfs_service_core.c $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(HOST_THREAD_FLAGS) \
		src/vfs_service_core.c src/vfs_client.c \
		src/vfs_local_transport.c tests/test_vfs_service_core.c -o $@

ASSIGN_TEST := build/host/test_vfs_assign

$(ASSIGN_TEST): src/vfs_assign.c src/vfs_path.c tests/test_vfs_assign.c \
		$(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/vfs_assign.c src/vfs_path.c tests/test_vfs_assign.c -o $@

UNION_TEST := build/host/test_vfs_union

$(UNION_TEST): src/vfs_union.c src/vfs_assign.c src/vfs_path.c \
		tests/test_vfs_union.c $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/vfs_union.c src/vfs_assign.c src/vfs_path.c \
		tests/test_vfs_union.c -o $@

# The port transport needs the runtime's port wrappers declared, and replaces
# them in the test: the seam is the three syscalls, so what is under test is
# the transport, the serve loop and the client above them.
PORT_TEST := build/host/test_vfs_port

$(PORT_TEST): src/vfs_port_transport.c src/vfs_service_core.c \
		src/vfs_client.c src/vfs_local_transport.c tests/test_vfs_port.c \
		$(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/vfs_port_transport.c src/vfs_service_core.c src/vfs_client.c \
		src/vfs_local_transport.c tests/test_vfs_port.c -o $@

PATH_TEST := build/host/test_vfs_path
PROCESS_TEST := build/host/test_vfs_process
READER_TEST := build/host/test_vfs_reader
FILESYSTEM_LIBRARY_TEST := build/host/test_filesystem_library

$(PATH_TEST): src/vfs_path.c tests/test_vfs_path.c $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/vfs_path.c tests/test_vfs_path.c -o $@

$(PROCESS_TEST): src/vfs_process.c tests/test_vfs_process.c $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		-DASTRA_VFS_PROCESS_TEST -ffunction-sections -Wl,--gc-sections \
		src/vfs_process.c tests/test_vfs_process.c -o $@

$(READER_TEST): src/vfs_reader.c tests/test_vfs_reader.c $(HEADERS) \
		include/astra/vfs_reader.h
	@mkdir -p $(@D)
	$(HOST_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/vfs_reader.c tests/test_vfs_reader.c -o $@

$(FILESYSTEM_LIBRARY_TEST): src/filesystem_library.c src/vfs_union.c \
		src/vfs_assign.c src/vfs_path.c tests/test_filesystem_library.c \
		$(HEADERS) include/astra/filesystem_library.h
	@mkdir -p $(@D)
	$(HOST_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/filesystem_library.c src/vfs_union.c src/vfs_assign.c \
		src/vfs_path.c tests/test_filesystem_library.c -o $@

test: $(HOST_TEST) $(ASSIGN_TEST) $(UNION_TEST) $(PATH_TEST) $(PORT_TEST) \
		$(PROCESS_TEST) $(READER_TEST) $(FILESYSTEM_LIBRARY_TEST)
	./$(HOST_TEST)
	./$(ASSIGN_TEST)
	./$(UNION_TEST)
	./$(PATH_TEST)
	./$(PORT_TEST)
	./$(PROCESS_TEST)
	./$(READER_TEST)
	./$(FILESYSTEM_LIBRARY_TEST)

# The path parser is nothing but bounds arithmetic over caller buffers, so it
# is the one piece here where a sanitizer earns its run.
sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(HOST_THREAD_FLAGS) $(SANITIZER_FLAGS) \
		src/vfs_service_core.c src/vfs_client.c \
		src/vfs_local_transport.c tests/test_vfs_service_core.c \
		-o build/host/test_vfs_service_core_sanitize
	./build/host/test_vfs_service_core_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/vfs_path.c tests/test_vfs_path.c \
		-o build/host/test_vfs_path_sanitize
	./build/host/test_vfs_path_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/vfs_assign.c src/vfs_path.c \
		tests/test_vfs_assign.c -o build/host/test_vfs_assign_sanitize
	./build/host/test_vfs_assign_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/vfs_union.c src/vfs_assign.c src/vfs_path.c \
		tests/test_vfs_union.c -o build/host/test_vfs_union_sanitize
	./build/host/test_vfs_union_sanitize
	$(HOST_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/vfs_port_transport.c src/vfs_service_core.c \
		src/vfs_client.c src/vfs_local_transport.c tests/test_vfs_port.c \
		-o build/host/test_vfs_port_sanitize
	./build/host/test_vfs_port_sanitize
	$(HOST_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/vfs_reader.c tests/test_vfs_reader.c \
		-o build/host/test_vfs_reader_sanitize
	./build/host/test_vfs_reader_sanitize
	$(HOST_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/filesystem_library.c src/vfs_union.c \
		src/vfs_assign.c src/vfs_path.c tests/test_filesystem_library.c \
		-o build/host/test_filesystem_library_sanitize
	./build/host/test_filesystem_library_sanitize

analyze:
	@mkdir -p build/analyzer
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/vfs_service_core.c -o build/analyzer/vfs_service_core.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/vfs_client.c -o build/analyzer/vfs_client.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/vfs_local_transport.c -o build/analyzer/vfs_local_transport.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/vfs_assign.c -o build/analyzer/vfs_assign.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/vfs_path.c -o build/analyzer/vfs_path.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/vfs_union.c -o build/analyzer/vfs_union.o
	$(ANALYZER_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/vfs_port_transport.c -o build/analyzer/vfs_port_transport.o
	$(ANALYZER_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/vfs_process.c -o build/analyzer/vfs_process.o
	$(ANALYZER_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/vfs_reader.c -o build/analyzer/vfs_reader.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/vfs_status.c -o build/analyzer/vfs_status.o
	$(ANALYZER_CC) $(CPPFLAGS) $(EXT4_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		-fanalyzer -c src/vfs_ext4_backend.c \
		-o build/analyzer/vfs_ext4_backend.o
	$(ANALYZER_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/filesystem_library.c -o build/analyzer/filesystem_library.o

# Header dependencies: a stale object compiled against an older ABI
# constant is indistinguishable from a working one until it runs.
-include $(wildcard build/m68k/*.d build/m68k/*/*.d)

clean:
	rm -rf build

.PHONY: all test sanitize analyze library-contract clean
