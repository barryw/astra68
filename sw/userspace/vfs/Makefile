include ../../../mk/m68k-cross.mk
include ../../../mk/astra-shared-library.mk

CC := $(CROSS)gcc
AR := $(CROSS)gcc-ar
READELF := $(CROSS)readelf
HOST_CC ?= cc
ANALYZER_CC ?= gcc
LWEXT4 := ../../../third_party/lwext4
RUNTIME := ../runtime
STORAGE := ../storage
NDK := ../../../ndk
UTF8_OBJECT := $(NDK)/build/host/utf8.o
CPPFLAGS := -I$(NDK)/include -Iinclude -I../../include
PORT_CPPFLAGS := $(CPPFLAGS) -I$(RUNTIME)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -O3 -flto -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
HOST_FLAGS := -O2
HOST_THREAD_FLAGS := -pthread
VFS_TEST_CPPFLAGS := -DASTRA_VFS_TEST_HOOKS=1
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
THREAD_SANITIZER_FLAGS := -fsanitize=thread -fno-omit-frame-pointer -g
LIBRARY_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -O3 -flto -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_SHARED_PIC_FLAGS)

# The core speaks no filesystem and needs no lwext4 header. Keeping its
# include path clean is what stops a backend detail leaking into it by
# accident, so the two are compiled with different flags on purpose.
CORE_SOURCES := src/vfs_service_core.c src/vfs_client.c \
	src/vfs_local_transport.c src/vfs_port_transport.c src/vfs_assign.c \
	src/vfs_path.c src/vfs_union.c src/vfs_process.c src/vfs_reader.c \
	src/process_filesystem.c \
	src/vfs_provider_index.c src/vfs_status.c
EXT4_CPPFLAGS := -I$(LWEXT4)/include -I$(STORAGE)/port/include \
	-DCONFIG_USE_DEFAULT_CFG=0 -I$(RUNTIME)/freestanding

HEADERS := include/astra/vfs_service_core.h include/astra/vfs_backend.h \
	$(NDK)/include/astra/vfs_client.h include/astra/vfs_local_transport.h \
	$(NDK)/include/astra/vfs_assign.h $(NDK)/include/astra/vfs_path.h \
	include/astra/vfs_port_transport.h $(NDK)/include/astra/vfs_union.h \
	$(NDK)/include/astra/vfs_process.h include/astra/vfs_reader.h \
	include/astra/vfs_host_backend.h include/astra/vfs_host_transport.h \
	include/astra/vfs_host_direct.h \
	include/astra/vfs_provider_index.h \
	../../include/astra/vfs_service.h \
	../../include/astra/syscall.h

TARGET := build/m68k/libastravfs.a
SERVICE_TARGET := build/m68k/libastravfs-service.a
NDK_OBJECTS := $(NDK)/build/m68k/manifest.o $(NDK)/build/m68k/bundle.o \
	$(NDK)/build/m68k/utf8.o
TARGET_OBJECTS := build/m68k/vfs_service_core.o build/m68k/vfs_client.o \
	build/m68k/filesystem_library.o \
	build/m68k/vfs_assign.o build/m68k/vfs_path.o \
	build/m68k/vfs_local_transport.o build/m68k/vfs_port_transport.o \
	build/m68k/vfs_ext4_backend.o build/m68k/vfs_host_backend.o \
	build/m68k/vfs_host_transport.o \
	build/m68k/vfs_host_direct.o \
	build/m68k/vfs_union.o \
	build/m68k/vfs_process.o build/m68k/process_filesystem.o \
	build/m68k/vfs_reader.o \
	build/m68k/vfs_provider_index.o $(NDK_OBJECTS) \
	build/m68k/vfs_status.o
SERVICE_OBJECTS := build/m68k/library/vfs_service_core.o \
	build/m68k/library/vfs_port_transport.o \
	build/m68k/library/vfs_host_backend.o \
	build/m68k/library/vfs_host_transport.o \
	build/m68k/library/vfs_host_direct.o
LOADER_PIC_TARGET := build/m68k/libastravfs-loader-pic.a
LOADER_PIC_SOURCES := vfs_service_core vfs_client vfs_assign vfs_path vfs_local_transport \
	vfs_port_transport vfs_host_transport vfs_host_direct vfs_union \
	vfs_host_backend vfs_process vfs_reader vfs_provider_index vfs_status
LOADER_PIC_OBJECTS := $(patsubst %,build/m68k/loader/%.o,$(LOADER_PIC_SOURCES)) \
	build/m68k/loader/ndk_manifest.o build/m68k/loader/ndk_bundle.o \
	build/m68k/loader/ndk_utf8.o
FILESYSTEM_LIBRARY := build/m68k/libraries/filesystem.library.2
FILESYSTEM_LIBRARY_OBJECTS := \
	build/m68k/library/filesystem_library_identity.o \
	build/m68k/library/filesystem_library.o \
	build/m68k/library/vfs_service_core.o \
	build/m68k/library/vfs_client.o build/m68k/library/vfs_assign.o \
	build/m68k/library/vfs_path.o build/m68k/library/vfs_union.o \
	build/m68k/library/vfs_port_transport.o \
	build/m68k/library/vfs_host_transport.o \
	build/m68k/library/vfs_host_direct.o \
	build/m68k/library/vfs_host_backend.o \
	build/m68k/library/vfs_process.o \
	build/m68k/library/process_filesystem.o \
	build/m68k/library/vfs_reader.o \
	build/m68k/library/vfs_provider_index.o \
	build/m68k/library/vfs_status.o
FILESYSTEM_LIBRARY_MAP := filesystem.library.exports
SYSTEM_LIBRARY := $(NDK)/build/m68k/libraries/system.library.1
RUNTIME_LIBRARY := $(RUNTIME)/build/m68k/libraries/runtime.library.1
SYMBOL_OWNERSHIP_CHECK := ../../../tools/check_shared_symbol_ownership.py
KERNEL_ELF_TEST := ../../kernel/build/test_elf
HOST_TEST := build/host/test_vfs_service_core

all: $(TARGET) $(SERVICE_TARGET) $(LOADER_PIC_TARGET) \
	$(FILESYSTEM_LIBRARY) library-contract

library: $(FILESYSTEM_LIBRARY)

loader-pic: $(LOADER_PIC_TARGET)

service: $(SERVICE_TARGET)

$(TARGET): $(TARGET_OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

$(SERVICE_TARGET): $(SERVICE_OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

$(LOADER_PIC_TARGET): $(LOADER_PIC_OBJECTS) Makefile
	$(ASTRA_REPLACE_ARCHIVE)

build/m68k/loader/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

build/m68k/loader/ndk_%.o: $(NDK)/src/%.c
	@mkdir -p $(@D)
	$(CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

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

build/m68k/vfs_host_transport.o: src/vfs_host_transport.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

build/m68k/vfs_host_direct.o: src/vfs_host_direct.c $(HEADERS)
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

$(NDK_OBJECTS): | libraries-ready
	@test -f $@

$(UTF8_OBJECT):
	$(MAKE) -C $(NDK) build/host/utf8.o

libraries-ready:
	$(MAKE) -C $(RUNTIME) library
	$(MAKE) -C $(NDK) build/m68k/manifest.o build/m68k/bundle.o \
		build/m68k/utf8.o library

build/m68k/library/%.o: src/%.c $(HEADERS) \
		$(NDK)/include/astra/filesystem_library.h
	@mkdir -p $(@D)
	$(CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

build/m68k/library/ndk_%.o: $(NDK)/src/%.c
	@mkdir -p $(@D)
	$(CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

$(TARGET_OBJECTS) $(LOADER_PIC_OBJECTS) $(FILESYSTEM_LIBRARY_OBJECTS): Makefile

$(KERNEL_ELF_TEST):
	$(MAKE) -C ../../kernel build/test_elf

$(FILESYSTEM_LIBRARY): $(FILESYSTEM_LIBRARY_OBJECTS) \
		$(FILESYSTEM_LIBRARY_MAP) $(ASTRA_SHARED_LINK_INPUTS) \
		| libraries-ready
	@mkdir -p $(@D)
	$(CC) $(LIBRARY_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,filesystem.library.2 \
		-Wl,--version-script=$(FILESYSTEM_LIBRARY_MAP) -o $@ \
		$(FILESYSTEM_LIBRARY_OBJECTS) \
		-L$(dir $(SYSTEM_LIBRARY)) -L$(dir $(RUNTIME_LIBRARY)) \
		-Wl,--no-as-needed -Wl,-l:system.library.1 \
		-Wl,-l:runtime.library.1

library-contract: $(FILESYSTEM_LIBRARY) $(KERNEL_ELF_TEST)
	@python3 ../../../tools/check_shared_library_layout.py \
		$(FILESYSTEM_LIBRARY)
	@python3 $(SYMBOL_OWNERSHIP_CHECK) --readelf $(READELF) \
		$(FILESYSTEM_LIBRARY) $(SYSTEM_LIBRARY) $(RUNTIME_LIBRARY)
	@python3 ../../../tools/check_library_exports.py \
		$(FILESYSTEM_LIBRARY_MAP) $(NDK)/include/astra/vfs_client.h \
		$(NDK)/include/astra/vfs_assign.h \
		$(NDK)/include/astra/vfs_path.h \
		$(NDK)/include/astra/vfs_union.h \
		$(NDK)/include/astra/filesystem_library.h \
		$(NDK)/include/astra/vfs_process.h \
		include/astra/vfs_reader.h include/astra/vfs_port_transport.h \
		../../include/astra/vfs_service.h
	@python3 ../../../tools/library_info.py $(FILESYSTEM_LIBRARY) | \
		grep -q '^filesystem.library.2 2.4.0 ABI 2.4$$'
	@$(READELF) -hW $< | grep 'Type:.*DYN' >/dev/null
	@$(READELF) -dW $< | grep 'SONAME.*filesystem.library.2' >/dev/null
	@test "$$($(READELF) -dW $< | \
		grep -c 'NEEDED.*system.library.1')" = 1
	@test "$$($(READELF) -dW $< | \
		grep -c 'NEEDED.*runtime.library.1')" = 1
	@test "$$($(READELF) -dW $< | awk '/NEEDED/{n++} END{print n+0}')" = 2
	@$(READELF) -dW $< | grep 'BIND_NOW' >/dev/null
	@test "$$($(READELF) -dW $< | awk '/TEXTREL/{n++} END{print n+0}')" = 0
	@$(READELF) -lW $< | grep ' GNU_RELRO ' >/dev/null
	@$(READELF) -VW $< | grep 'ASTRA_FILESYSTEM_2.2' >/dev/null
	@$(READELF) -VW $< | grep 'ASTRA_FILESYSTEM_2.3' >/dev/null
	@$(READELF) -VW $< | grep 'ASTRA_FILESYSTEM_2.4' >/dev/null
	@ASTRA_LIBRARY_FIXTURE=$(abspath $<) $(KERNEL_ELF_TEST) >/dev/null
	@echo "astra filesystem shared library: PASS"

# The one translation unit that is allowed to know what lwext4 is.
build/m68k/vfs_ext4_backend.o: src/vfs_ext4_backend.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(EXT4_CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) \
		$(DEPFLAGS) -c $< -o $@

$(HOST_TEST): $(CORE_SOURCES) $(UTF8_OBJECT) tests/test_vfs_service_core.c \
		$(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(VFS_TEST_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(HOST_THREAD_FLAGS) \
		src/vfs_service_core.c src/vfs_client.c \
        src/vfs_local_transport.c $(UTF8_OBJECT) \
		tests/test_vfs_service_core.c -o $@

ASSIGN_TEST := build/host/test_vfs_assign

$(ASSIGN_TEST): src/vfs_assign.c src/vfs_path.c $(UTF8_OBJECT) tests/test_vfs_assign.c \
		$(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/vfs_assign.c src/vfs_path.c $(UTF8_OBJECT) \
		tests/test_vfs_assign.c -o $@

UNION_TEST := build/host/test_vfs_union

$(UNION_TEST): src/vfs_union.c src/vfs_assign.c src/vfs_path.c $(UTF8_OBJECT) \
		tests/test_vfs_union.c $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/vfs_union.c src/vfs_assign.c src/vfs_path.c $(UTF8_OBJECT) \
		tests/test_vfs_union.c -o $@

# The port transport needs the runtime's port wrappers declared, and replaces
# them in the test: the seam is the three syscalls, so what is under test is
# the transport, the serve loop and the client above them.
PORT_TEST := build/host/test_vfs_port
PORT_THREAD_TEST := build/host/test_vfs_port_threads

$(PORT_TEST): src/vfs_port_transport.c src/vfs_service_core.c \
		src/vfs_client.c src/vfs_local_transport.c tests/test_vfs_port.c \
		$(UTF8_OBJECT) $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(PORT_CPPFLAGS) $(VFS_TEST_CPPFLAGS) \
		$(COMMON_FLAGS) $(HOST_FLAGS) \
		$(HOST_THREAD_FLAGS) \
		src/vfs_port_transport.c src/vfs_service_core.c src/vfs_client.c \
		src/vfs_local_transport.c $(UTF8_OBJECT) tests/test_vfs_port.c -o $@

$(PORT_THREAD_TEST): src/vfs_port_transport.c \
		$(UTF8_OBJECT) tests/test_vfs_port_threads.c $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(HOST_THREAD_FLAGS) -ffunction-sections -Wl,--gc-sections \
		-DASTRA_VFS_EMBEDDED_THREAD_STATE=1 \
		src/vfs_port_transport.c $(UTF8_OBJECT) \
		tests/test_vfs_port_threads.c -o $@

PATH_TEST := build/host/test_vfs_path
PROCESS_TEST := build/host/test_vfs_process
READER_TEST := build/host/test_vfs_reader
FILESYSTEM_LIBRARY_TEST := build/host/test_filesystem_library
HOST_BACKEND_TEST := build/host/test_vfs_host_backend
HOST_TRANSPORT_TEST := build/host/test_vfs_host_transport

$(PATH_TEST): src/vfs_path.c $(UTF8_OBJECT) tests/test_vfs_path.c $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/vfs_path.c $(UTF8_OBJECT) tests/test_vfs_path.c -o $@

$(PROCESS_TEST): src/vfs_process.c src/process_filesystem.c \
		src/vfs_provider_index.c \
		tests/test_vfs_process.c $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		-DASTRA_VFS_PROCESS_TEST -ffunction-sections -Wl,--gc-sections \
		src/vfs_process.c src/process_filesystem.c src/vfs_provider_index.c \
		tests/test_vfs_process.c -o $@

$(READER_TEST): src/vfs_reader.c src/vfs_provider_index.c \
		tests/test_vfs_reader.c $(HEADERS) \
		include/astra/vfs_reader.h
	@mkdir -p $(@D)
	$(HOST_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/vfs_reader.c src/vfs_provider_index.c \
		tests/test_vfs_reader.c -o $@

$(FILESYSTEM_LIBRARY_TEST): src/filesystem_library.c src/vfs_union.c \
		src/vfs_assign.c src/vfs_path.c $(UTF8_OBJECT) \
		tests/test_filesystem_library.c \
		$(HEADERS) $(NDK)/include/astra/filesystem_library.h
	@mkdir -p $(@D)
	$(HOST_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/filesystem_library.c src/vfs_union.c src/vfs_assign.c \
		src/vfs_path.c $(UTF8_OBJECT) tests/test_filesystem_library.c -o $@

$(HOST_BACKEND_TEST): src/vfs_host_backend.c \
		tests/test_vfs_host_backend.c $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/vfs_host_backend.c tests/test_vfs_host_backend.c -o $@

$(HOST_TRANSPORT_TEST): src/vfs_host_transport.c \
		tests/test_vfs_host_transport.c $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		-fno-pie -no-pie src/vfs_host_transport.c \
		tests/test_vfs_host_transport.c -o $@

test: $(HOST_TEST) $(ASSIGN_TEST) $(UNION_TEST) $(PATH_TEST) $(PORT_TEST) \
		$(PORT_THREAD_TEST) \
		$(PROCESS_TEST) $(READER_TEST) $(FILESYSTEM_LIBRARY_TEST) \
		$(HOST_BACKEND_TEST) $(HOST_TRANSPORT_TEST)
	./$(HOST_TEST)
	./$(ASSIGN_TEST)
	./$(UNION_TEST)
	./$(PATH_TEST)
	./$(PORT_TEST)
	./$(PORT_THREAD_TEST)
	./$(PROCESS_TEST)
	./$(READER_TEST)
	./$(FILESYSTEM_LIBRARY_TEST)
	./$(HOST_BACKEND_TEST)
	./$(HOST_TRANSPORT_TEST)

# The path parser is nothing but bounds arithmetic over caller buffers, so it
# is the one piece here where a sanitizer earns its run.
sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(VFS_TEST_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(HOST_THREAD_FLAGS) $(SANITIZER_FLAGS) \
		src/vfs_service_core.c src/vfs_client.c \
		src/vfs_local_transport.c $(UTF8_OBJECT) \
		tests/test_vfs_service_core.c \
		-o build/host/test_vfs_service_core_sanitize
	./build/host/test_vfs_service_core_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/vfs_path.c $(UTF8_OBJECT) \
		tests/test_vfs_path.c \
		-o build/host/test_vfs_path_sanitize
	./build/host/test_vfs_path_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/vfs_assign.c src/vfs_path.c $(UTF8_OBJECT) \
		tests/test_vfs_assign.c -o build/host/test_vfs_assign_sanitize
	./build/host/test_vfs_assign_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/vfs_union.c src/vfs_assign.c src/vfs_path.c \
		$(UTF8_OBJECT) \
		tests/test_vfs_union.c -o build/host/test_vfs_union_sanitize
	./build/host/test_vfs_union_sanitize
	$(HOST_CC) $(PORT_CPPFLAGS) $(VFS_TEST_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(HOST_THREAD_FLAGS) $(SANITIZER_FLAGS) \
		src/vfs_port_transport.c src/vfs_service_core.c \
		src/vfs_client.c src/vfs_local_transport.c $(UTF8_OBJECT) \
		tests/test_vfs_port.c \
		-o build/host/test_vfs_port_sanitize
	./build/host/test_vfs_port_sanitize
	$(HOST_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/vfs_reader.c src/vfs_provider_index.c \
		tests/test_vfs_reader.c \
		-o build/host/test_vfs_reader_sanitize
	./build/host/test_vfs_reader_sanitize
	$(HOST_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/filesystem_library.c src/vfs_union.c \
		src/vfs_assign.c src/vfs_path.c $(UTF8_OBJECT) \
		tests/test_filesystem_library.c \
		-o build/host/test_filesystem_library_sanitize
	./build/host/test_filesystem_library_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/vfs_host_backend.c \
		tests/test_vfs_host_backend.c \
		-o build/host/test_vfs_host_backend_sanitize
	./build/host/test_vfs_host_backend_sanitize
	$(HOST_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) -fno-pie -no-pie src/vfs_host_transport.c \
		tests/test_vfs_host_transport.c \
		-o build/host/test_vfs_host_transport_sanitize
	./build/host/test_vfs_host_transport_sanitize

tsan:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(VFS_TEST_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(HOST_THREAD_FLAGS) $(THREAD_SANITIZER_FLAGS) \
		src/vfs_service_core.c src/vfs_client.c \
		src/vfs_local_transport.c $(UTF8_OBJECT) \
		tests/test_vfs_service_core.c \
		-o build/host/test_vfs_service_core_tsan
	setarch x86_64 -R ./build/host/test_vfs_service_core_tsan
	$(HOST_CC) $(PORT_CPPFLAGS) $(VFS_TEST_CPPFLAGS) \
		$(COMMON_FLAGS) $(HOST_FLAGS) $(HOST_THREAD_FLAGS) \
		$(THREAD_SANITIZER_FLAGS) src/vfs_port_transport.c \
		src/vfs_service_core.c src/vfs_client.c src/vfs_local_transport.c \
		$(UTF8_OBJECT) tests/test_vfs_port.c \
		-o build/host/test_vfs_port_tsan
	setarch x86_64 -R ./build/host/test_vfs_port_tsan

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
		-c src/vfs_host_transport.c -o build/analyzer/vfs_host_transport.o
	$(ANALYZER_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/vfs_process.c -o build/analyzer/vfs_process.o
	$(ANALYZER_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/process_filesystem.c -o build/analyzer/process_filesystem.o
	$(ANALYZER_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/vfs_reader.c -o build/analyzer/vfs_reader.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/vfs_status.c -o build/analyzer/vfs_status.o
	$(ANALYZER_CC) $(CPPFLAGS) $(EXT4_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		-fanalyzer -c src/vfs_ext4_backend.c \
		-o build/analyzer/vfs_ext4_backend.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/vfs_host_backend.c -o build/analyzer/vfs_host_backend.o
	$(ANALYZER_CC) $(PORT_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/filesystem_library.c -o build/analyzer/filesystem_library.o

# Header dependencies: a stale object compiled against an older ABI
# constant is indistinguishable from a working one until it runs.
-include $(wildcard build/m68k/*.d build/m68k/*/*.d)

clean:
	rm -rf build

.PHONY: all library service loader-pic test sanitize tsan analyze \
	library-contract clean libraries-ready
