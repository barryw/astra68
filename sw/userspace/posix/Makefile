include ../../../mk/m68k-cross.mk
include ../../../mk/astra-shared-library.mk

# The POSIX layer: descriptors, and later a cwd, over capabilities and the VFS.
#
# A separate library rather than part of the runtime, because the runtime is
# what every Astra program links and this is what a *Unix* program needs. A
# command written against the NDK must not pay for printf it never calls.

CC := $(CROSS)gcc
CXX := $(CROSS)g++
AR := $(CROSS)gcc-ar
READELF := $(CROSS)readelf
HOST_CC ?= cc
RUNTIME := ../runtime
COMPILER := ../compiler
STREAMS := ../streams
VFS := ../vfs
NETWORK := ../network
NDK := ../../../ndk
PICOLIBC ?= $(HOME)/picolibc-astra
include kit/astra-posix.mk
PROJECT_CPPFLAGS := -I$(NDK)/include -I include -I../../include \
	-I$(RUNTIME)/include -I$(STREAMS)/include -I$(VFS)/include \
	-I$(NETWORK)/include \

CPPFLAGS := $(PROJECT_CPPFLAGS) -isystem $(PICOLIBC)/include
CXX_CPPFLAGS := $(PROJECT_CPPFLAGS)
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
HOST_RESOURCE_DEFINES := -Dgetrlimit=astra_test_getrlimit \
	-Dsetrlimit=astra_test_setrlimit
DEPFLAGS := -MMD -MP
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -O3 -flto -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
LIBRARY_FLAGS := $(filter-out -flto,$(TARGET_FLAGS)) \
	$(ASTRA_SHARED_PIC_FLAGS)
CXX_TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -O3 -flto \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)

SOURCES := src/startup.c src/console.c src/poll.c src/socket.c src/inet.c src/terminal.c src/file.c src/heap.c src/alloc.c src/time.c src/pthread.c \
	src/system.c src/scheduling.c src/signal.c \
	src/identity.c src/process.c src/resource.c src/strftime_format.c src/path.c \
	src/unistd_compat.c src/exec_compat.c src/dirent_compat.c \
	src/time_compat.c src/entropy.c

SOURCES += src/temp_compat.c
OBJECTS := $(patsubst src/%.c,build/m68k/%.o,$(SOURCES))
LIBRARY_SOURCES := $(filter-out src/strftime_format.c,$(SOURCES))
LIBRARY_OBJECTS := $(patsubst src/%.c,build/m68k/library/%.o,$(LIBRARY_SOURCES))
ANALYZER_OBJECTS := $(patsubst src/%.c,build/analyzer/%.o,$(SOURCES))
LIB := build/m68k/libastraposix.a
PIC_LIB := build/m68k/libastraposix-pic.a
LIBC_LIBRARY := build/m68k/libraries/libc.library.1
LIBC_IDENTITY := build/m68k/library/libc_library.o
LIBC_EXPORT_SOURCE := libc.posix.exports
LIBC_DATA_SOURCE := libc.data.exports
LIBC_STANDARD_HEADER_EXTENSIONS := libc.standard-header.extensions
LIBC_ARCHIVE_API := build/m68k/libc.archive.exports
LIBC_MISSING_API := build/m68k/libc.missing
LIBC_API_STAMP := build/m68k/libc.api.inputs
LIBC_EXPORTS := build/m68k/libc.library.exports
LIBC_UNDEFINED := build/m68k/libc.library.undefined
LIBC_CONTRACT_STAMP := build/m68k/libc.library.inputs
LIBC_CONTRACT_TOOL := ../../../tools/generate_archive_contract.py
LIBC_EXPORT_CHECK := ../../../tools/check_elf_exports.py
LIBC_API_CHECK := ../../../tools/check_libc_api.py
SYMBOL_OWNERSHIP_CHECK := ../../../tools/check_shared_symbol_ownership.py
PICOLIBC_LIB := $(PICOLIBC)/lib/libc.a
PICOLIBC_MATH := $(PICOLIBC)/lib/libm.a
RUNTIME_LIBRARY := $(RUNTIME)/build/m68k/libraries/runtime.library.1
COMPILER_LIBRARY := $(COMPILER)/build/m68k/libraries/compiler.library.1
STREAMS_LIBRARY := $(STREAMS)/build/m68k/libraries/streams.library.1
FILESYSTEM_LIBRARY := $(VFS)/build/m68k/libraries/filesystem.library.2
NETWORK_LIBRARY := $(NETWORK)/build/m68k/libraries/network.library.1
NETWORK_LIB := $(NETWORK)/build/m68k/libastranetwork.a
RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart.a
RUNTIME_CRT0 := $(RUNTIME)/build/m68k/crt0-hosted.o
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
SIGNAL_TEST := build/host/test_signal
SOCKET_ADDRESS_TEST := build/host/test_socket_address
PTHREAD_TEST := build/host/test_pthread
RESOURCE_TEST := build/host/test_resource
SCHEDULING_TEST := build/host/test_scheduling
HEAP_TEST := build/host/test_heap
ALLOC_TEST := build/host/test_alloc
UNISTD_COMPAT_TEST := build/host/test_unistd_compat
ENTROPY_TEST := build/host/test_entropy
EXEC_COMPAT_TEST := build/host/test_exec_compat
DIRENT_COMPAT_TEST := build/host/test_dirent_compat
TIME_COMPAT_TEST := build/host/test_time_compat
TEMP_COMPAT_TEST := build/host/test_temp_compat
LIMITS_CONTRACT := build/m68k/limits-contract.o
CXX_HEADERS_CONTRACT := build/m68k/cxx-headers-contract.o
CXX_PROGRAM_OBJECT := build/m68k/cxx-program.o
CXX_LINK_CONTRACT := build/m68k/cxx-runtime-test.elf

all: $(LIB) $(PIC_LIB) libc-library-contract

library: $(LIBC_LIBRARY)

$(LIB): $(OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

$(PIC_LIB): $(LIBRARY_OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

build/m68k/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/library/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) -c $< -o $@

libraries-ready:
	$(MAKE) -C $(RUNTIME) library
	$(MAKE) -C $(STREAMS) library
	$(MAKE) -C $(VFS) library
	$(MAKE) -C $(NETWORK) library

# These files are owned by the recursive builds above.  Naming the concrete
# products as prerequisites keeps dependency changes visible to Make, while
# this single barrier guarantees a clean tree can produce them before Make
# tries to inspect their timestamps.
$(RUNTIME_LIBRARY) $(COMPILER_LIBRARY) $(STREAMS_LIBRARY) \
		$(FILESYSTEM_LIBRARY) $(NETWORK_LIBRARY): | libraries-ready
	@test -f $@

static-libraries-ready:
	$(MAKE) -C $(RUNTIME) build/m68k/libastrart.a \
		build/m68k/crt0.o build/m68k/crt0-hosted.o
	$(MAKE) -C $(STREAMS) build/m68k/libastrastreams.a
	$(MAKE) -C $(VFS) build/m68k/libastravfs.a
	$(MAKE) -C $(NETWORK) build/m68k/libastranetwork.a

PICOLIBC_HEADERS := $(shell find $(PICOLIBC)/include -type f -name '*.h')

$(LIBC_API_STAMP): $(PICOLIBC_LIB) $(PICOLIBC_HEADERS) \
		$(LIBC_EXPORT_SOURCE) $(LIBC_DATA_SOURCE) $(LIBC_API_CHECK) \
		$(ASTRA_TOOLCHAIN_STAMP)
	$(call ASTRA_UPDATE_CONTENT_STAMP,$(PICOLIBC_LIB) $(PICOLIBC_HEADERS) \
		$(LIBC_EXPORT_SOURCE) $(LIBC_DATA_SOURCE) $(LIBC_API_CHECK))

$(LIBC_ARCHIVE_API) $(LIBC_MISSING_API) &: $(LIBC_API_STAMP)
	python3 $(LIBC_API_CHECK) --cc $(CC) --readelf $(READELF) \
		--include-dir $(PICOLIBC)/include --archive $(PICOLIBC_LIB) \
		--extra $(LIBC_EXPORT_SOURCE) --data $(LIBC_DATA_SOURCE) \
		--api-out $(LIBC_ARCHIVE_API) --missing-out $(LIBC_MISSING_API)
	$(call ASTRA_MARK_CONTENT_OUTPUTS,$(LIBC_API_STAMP),\
		$(LIBC_ARCHIVE_API) $(LIBC_MISSING_API))

libc-api-audit: $(LIBC_ARCHIVE_API) $(LIBC_MISSING_API)
	@echo "Astra libc public API: $$(wc -l < $(LIBC_ARCHIVE_API)) implemented, $$(wc -l < $(LIBC_MISSING_API)) missing"

$(LIBC_CONTRACT_STAMP): $(PICOLIBC_LIB) $(PICOLIBC_MATH) \
		$(LIBC_EXPORT_SOURCE) $(LIBC_DATA_SOURCE) $(LIBC_ARCHIVE_API) \
		$(LIBC_CONTRACT_TOOL) $(RUNTIME_LIBRARY) $(COMPILER_LIBRARY) \
		$(STREAMS_LIBRARY) $(FILESYSTEM_LIBRARY) $(NETWORK_LIBRARY) \
		$(ASTRA_TOOLCHAIN_STAMP) \
		| libraries-ready
	$(call ASTRA_UPDATE_CONTENT_STAMP,$(PICOLIBC_LIB) $(PICOLIBC_MATH) \
		$(LIBC_EXPORT_SOURCE) $(LIBC_DATA_SOURCE) $(LIBC_ARCHIVE_API) \
		$(LIBC_CONTRACT_TOOL) $(RUNTIME_LIBRARY) $(COMPILER_LIBRARY) \
		$(STREAMS_LIBRARY) $(FILESYSTEM_LIBRARY) $(NETWORK_LIBRARY))

$(LIBC_EXPORTS) $(LIBC_UNDEFINED) &: $(LIBC_CONTRACT_STAMP)
	@mkdir -p build/m68k
	python3 $(LIBC_CONTRACT_TOOL) --readelf $(READELF) \
		--version ASTRA_LIBC_1_0 --exports-out $(LIBC_EXPORTS) \
		--undefined-out $(LIBC_UNDEFINED) --extra $(LIBC_EXPORT_SOURCE) \
		--force-extra \
		--allow $(LIBC_ARCHIVE_API) \
		--dependency $(RUNTIME_LIBRARY) \
		--dependency $(COMPILER_LIBRARY) \
		--dependency $(STREAMS_LIBRARY) \
		--dependency $(FILESYSTEM_LIBRARY) \
		--dependency $(NETWORK_LIBRARY) \
		$(PICOLIBC_LIB) $(PICOLIBC_MATH)
	$(call ASTRA_MARK_CONTENT_OUTPUTS,$(LIBC_CONTRACT_STAMP),\
		$(LIBC_EXPORTS) $(LIBC_UNDEFINED))

$(LIBC_LIBRARY): $(LIBC_IDENTITY) $(LIBRARY_OBJECTS) \
		$(LIBC_EXPORTS) $(LIBC_UNDEFINED) $(ASTRA_SHARED_LINK_INPUTS) \
		| libraries-ready
	@mkdir -p $(@D)
	$(CC) $(ASTRA_TARGET_ABI_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,libc.library.1 \
		-Wl,--version-script=$(LIBC_EXPORTS) \
		-Wl,@$(LIBC_UNDEFINED) -o $@ \
		$(LIBC_IDENTITY) $(LIBRARY_OBJECTS) \
		-L$(dir $(STREAMS_LIBRARY)) -L$(dir $(FILESYSTEM_LIBRARY)) \
		-L$(dir $(RUNTIME_LIBRARY)) -L$(dir $(COMPILER_LIBRARY)) \
		-L$(dir $(NETWORK_LIBRARY)) \
		-Wl,--no-as-needed \
		-Wl,-l:streams.library.1 -Wl,-l:filesystem.library.2 \
		-Wl,-l:network.library.1 \
		-Wl,-l:runtime.library.1 -Wl,-l:compiler.library.1 \
		-Wl,--start-group $(PICOLIBC_LIB) $(PICOLIBC_MATH) \
		-Wl,--end-group

libc-library-contract: $(LIBC_LIBRARY)
	@python3 ../../../tools/check_shared_library_layout.py $(LIBC_LIBRARY)
	@python3 $(SYMBOL_OWNERSHIP_CHECK) --readelf $(READELF) \
		$(LIBC_LIBRARY) $(RUNTIME_LIBRARY) $(COMPILER_LIBRARY) \
		$(STREAMS_LIBRARY) $(FILESYSTEM_LIBRARY) $(NETWORK_LIBRARY)
	@python3 $(LIBC_EXPORT_CHECK) --readelf $(READELF) \
		$(LIBC_EXPORTS) $(LIBC_LIBRARY)
	@python3 ../../../tools/library_info.py $(LIBC_LIBRARY) | \
		grep -q '^libc.library.1 1.0.0 ABI 1.0$$'
	@$(READELF) -hW $(LIBC_LIBRARY) | grep -q 'Type:.*DYN'
	@$(READELF) -dW $(LIBC_LIBRARY) | grep -Fq 'Library soname: [libc.library.1]'
	@test "$$($(READELF) -dW $(LIBC_LIBRARY) | \
		grep -Fc 'Shared library: [runtime.library.1]')" = 1
	@test "$$($(READELF) -dW $(LIBC_LIBRARY) | \
		grep -Fc 'Shared library: [streams.library.1]')" = 1
	@test "$$($(READELF) -dW $(LIBC_LIBRARY) | \
		grep -Fc 'Shared library: [filesystem.library.2]')" = 1
	@test "$$($(READELF) -dW $(LIBC_LIBRARY) | \
		grep -Fc 'Shared library: [compiler.library.1]')" = 1
	@test "$$($(READELF) -dW $(LIBC_LIBRARY) | \
		grep -Fc 'Shared library: [network.library.1]')" = 1
	@$(READELF) -dW $(LIBC_LIBRARY) | grep -q 'BIND_NOW'
	@! $(READELF) -dW $(LIBC_LIBRARY) | grep -q 'TEXTREL'
	@$(READELF) -lW $(LIBC_LIBRARY) | grep -q ' GNU_RELRO '
	@$(READELF) -lW $(LIBC_LIBRARY) | grep -q ' TLS '
	@$(READELF) -VW $(LIBC_LIBRARY) | grep -q 'ASTRA_LIBC_1_0'
	@python3 $(LIBC_API_CHECK) --cc $(CC) --readelf $(READELF) \
		--include-dir $(PICOLIBC)/include --archive $(PICOLIBC_LIB) \
		--library $(LIBC_LIBRARY) --extra $(LIBC_EXPORT_SOURCE) \
		--data $(LIBC_DATA_SOURCE) \
		--dependency $(RUNTIME_LIBRARY) --dependency $(COMPILER_LIBRARY) \
		--dependency $(STREAMS_LIBRARY) --dependency $(FILESYSTEM_LIBRARY) \
		--dependency $(NETWORK_LIBRARY)
	@python3 $(LIBC_API_CHECK) --cc $(CC) --readelf $(READELF) \
		--include-dir $(PICOLIBC)/include --archive $(PICOLIBC_LIB) \
		--library $(LIBC_LIBRARY) --extra $(LIBC_EXPORT_SOURCE) \
		--standard-only --require-all \
		--exclude $(LIBC_STANDARD_HEADER_EXTENSIONS) \
		--dependency $(RUNTIME_LIBRARY) --dependency $(COMPILER_LIBRARY) \
		--dependency $(STREAMS_LIBRARY) --dependency $(FILESYSTEM_LIBRARY) \
		--dependency $(NETWORK_LIBRARY)
	@echo "astra unified C/POSIX shared library: PASS"

$(HOST_TEST): src/strftime_format.c tests/test_strftime_format.c
	@mkdir -p $(@D)
	$(HOST_CC) -Iinclude -std=c11 -Wall -Wextra -Werror -pedantic -O2 \
		src/strftime_format.c tests/test_strftime_format.c -o $@

$(PATH_TEST): src/path.c tests/test_path.c
	@mkdir -p $(@D)
	$(HOST_CC) -Isrc -I../../include -std=c11 -Wall -Wextra -Werror -pedantic -O2 \
	src/path.c tests/test_path.c -o $@

$(DESCRIPTOR_TEST): src/console.c src/resource.c $(RUNTIME)/src/startup.c \
		tests/heap_prepare_stub.c tests/test_descriptor.c
	@mkdir -p $(@D)
	$(HOST_CC) -Iinclude -I../../include -I$(RUNTIME)/include \
		-I$(STREAMS)/include -I$(VFS)/include -I$(NDK)/include \
		$(COMMON_FLAGS) $(HOST_RESOURCE_DEFINES) -O2 -no-pie -ffunction-sections \
		-Wl,--gc-sections src/console.c src/resource.c $(RUNTIME)/src/startup.c \
		tests/heap_prepare_stub.c \
		tests/test_descriptor.c -o $@

$(STARTUP_TEST): src/startup.c tests/test_startup.c
	@mkdir -p $(@D)
	$(HOST_CC) -Iinclude -I../../include -I$(RUNTIME)/include \
		-I$(STREAMS)/include -I$(VFS)/include -I$(NDK)/include \
		$(COMMON_FLAGS) -O2 -no-pie src/startup.c tests/test_startup.c -o $@

$(POLL_TEST): src/console.c src/poll.c src/resource.c $(RUNTIME)/src/startup.c \
		tests/heap_prepare_stub.c tests/test_poll.c
	@mkdir -p $(@D)
	$(HOST_CC) -Iinclude -I../../include -I$(RUNTIME)/include \
		-I$(STREAMS)/include -I$(VFS)/include -I$(NDK)/include \
		$(COMMON_FLAGS) $(HOST_RESOURCE_DEFINES) -O2 -no-pie -ffunction-sections \
		-Wl,--gc-sections src/console.c src/poll.c src/resource.c $(RUNTIME)/src/startup.c \
		tests/heap_prepare_stub.c \
		tests/test_poll.c -o $@

$(IDENTITY_TEST): src/identity.c tests/test_identity.c
	@mkdir -p $(@D)
	$(HOST_CC) $(COMMON_FLAGS) -O2 src/identity.c tests/test_identity.c -o $@

$(PROCESS_TEST): src/process.c tests/test_process.c
	@mkdir -p $(@D)
	$(HOST_CC) -Iinclude -I../../include -I$(RUNTIME)/include \
		-I$(STREAMS)/include -I$(VFS)/include -I$(NDK)/include \
		$(COMMON_FLAGS) -O2 \
		src/process.c tests/test_process.c -o $@

$(SIGNAL_TEST): src/signal.c tests/test_signal.c
	@mkdir -p $(@D)
	$(HOST_CC) -Itests/include -Iinclude -I../../include -I$(RUNTIME)/include \
		-I$(STREAMS)/include -I$(VFS)/include -I$(NDK)/include \
		$(COMMON_FLAGS) -O2 src/signal.c tests/test_signal.c -o $@

$(SOCKET_ADDRESS_TEST): src/inet.c tests/test_socket_address.c
	@mkdir -p $(@D)
	$(HOST_CC) -Iinclude -I../../include -I$(RUNTIME)/include \
		-I$(VFS)/include -I$(NDK)/include $(COMMON_FLAGS) -O2 $^ -o $@

$(PTHREAD_TEST): src/pthread.c tests/test_pthread.c
	@mkdir -p $(@D)
	$(HOST_CC) -I$(NDK)/include -Iinclude -I../../include \
		-I$(RUNTIME)/include \
		$(COMMON_FLAGS) -O2 -D_POSIX_C_SOURCE=200809L \
		-D_BITS_PTHREADTYPES_COMMON_H=1 \
		-DASTRA_PTHREAD_NO_LIBC_LOCKS=1 $^ -o $@

$(RESOURCE_TEST): src/resource.c tests/test_resource.c
	@mkdir -p $(@D)
	$(HOST_CC) -I../../include $(COMMON_FLAGS) $(HOST_RESOURCE_DEFINES) -O2 \
		$^ -o $@

$(SCHEDULING_TEST): src/scheduling.c tests/test_scheduling.c
	@mkdir -p $(@D)
	$(HOST_CC) -I../../include -I$(NDK)/include $(COMMON_FLAGS) -O2 $^ -o $@

$(HEAP_TEST): src/heap.c src/heap_internal.h src/resource_internal.h tests/test_heap.c
	@mkdir -p $(@D)
	$(HOST_CC) -Iinclude -I../../include -I$(RUNTIME)/include \
		-I$(NDK)/include $(COMMON_FLAGS) -O2 src/heap.c tests/test_heap.c \
		-o $@

$(ALLOC_TEST): src/alloc.c tests/test_alloc.c
	@mkdir -p $(@D)
	$(HOST_CC) -I$(NDK)/include -I$(RUNTIME)/include -I../../include \
		-Dmalloc=astra_test_malloc -Dfree=astra_test_free \
		-Dcalloc=astra_test_calloc -Drealloc=astra_test_realloc \
		-Dmalloc_usable_size=astra_test_malloc_usable_size \
		-Dmemalign=astra_test_memalign \
		-Daligned_alloc=astra_test_aligned_alloc \
		-Dposix_memalign=astra_test_posix_memalign \
		$(COMMON_FLAGS) -O2 -fno-builtin $^ -o $@

$(UNISTD_COMPAT_TEST): src/unistd_compat.c tests/test_unistd_compat.c
	@mkdir -p $(@D)
	$(HOST_CC) -I../../include \
		-Dusleep=astra_test_usleep -Dtruncate=astra_test_truncate \
		-Dpathconf=astra_test_pathconf -Dsysconf=astra_test_sysconf \
		-Dposix_close=astra_test_posix_close \
		-DASTRA_POSIX_TEST=1 \
		$(COMMON_FLAGS) -Wno-deprecated-declarations -Wno-nonnull-compare \
		-O2 -fno-builtin $^ -o $@ \
		-Wl,--wrap=open -Wl,--wrap=ftruncate -Wl,--wrap=close \
		-Wl,--wrap=stat -Wl,--wrap=fstat -Wl,--wrap=fsync \
		-Wl,--wrap=getcwd -Wl,--wrap=nanosleep

$(ENTROPY_TEST): src/entropy.c tests/test_entropy.c
	@mkdir -p $(@D)
	$(HOST_CC) -Iinclude -I../../include -I$(RUNTIME)/include \
		-I$(NDK)/include $(COMMON_FLAGS) -O2 -fno-builtin $^ -o $@

$(EXEC_COMPAT_TEST): src/exec_compat.c tests/test_exec_compat.c
	@mkdir -p $(@D)
	$(HOST_CC) $(COMMON_FLAGS) -O2 -fno-builtin $^ -o $@

$(DIRENT_COMPAT_TEST): src/dirent_compat.c tests/test_dirent_compat.c
	@mkdir -p $(@D)
	$(HOST_CC) -Dalphasort=astra_test_alphasort \
		-Dversionsort=astra_test_versionsort \
		-Dreaddir_r=astra_test_readdir_r \
		-Dscandir=astra_test_scandir -Dscandirat=astra_test_scandirat \
		$(COMMON_FLAGS) -Wno-deprecated-declarations -O2 -fno-builtin $^ -o $@

$(TIME_COMPAT_TEST): src/time_compat.c tests/test_time_compat.c
	@mkdir -p $(@D)
	$(HOST_CC) -Dftime=astra_test_ftime -Dualarm=astra_test_ualarm \
		$(COMMON_FLAGS) -Wno-deprecated-declarations -Wno-nonnull-compare \
		-O2 -fno-builtin $^ \
		-Wl,--wrap=gettimeofday -Wl,--wrap=setitimer -o $@

$(TEMP_COMPAT_TEST): src/temp_compat.c tests/test_temp_compat.c
	@mkdir -p $(@D)
	$(HOST_CC) -Dmkdtemp=astra_test_mkdtemp \
		-Dmkostemp=astra_test_mkostemp $(COMMON_FLAGS) \
		-Wno-nonnull-compare -O2 -fno-builtin \
		$^ -o $@

$(LIMITS_CONTRACT): tests/limits_contract.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -c $< -o $@

$(CXX_HEADERS_CONTRACT): tests/cxx_headers.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXX_CPPFLAGS) -std=c++17 -Wall -Wextra -Werror -pedantic \
		$(CXX_TARGET_FLAGS) -c $< -o $@

$(CXX_PROGRAM_OBJECT): kit/program.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) \
		-DASTRA_POSIX_PROGRAM_NAME='"cxx-runtime-test"' \
		-DASTRA_POSIX_PROGRAM_MAJOR=1 -DASTRA_POSIX_PROGRAM_MINOR=0 \
		-DASTRA_POSIX_PROGRAM_PATCH=0 \
		-DASTRA_POSIX_PROGRAM_AUTHOR='"Astra68"' \
		-DASTRA_POSIX_PROGRAM_COPYRIGHT='"Copyright 2026 Astra68 contributors"' \
		-c $< -o $@

$(CXX_LINK_CONTRACT): tests/cxx_runtime.cpp $(CXX_PROGRAM_OBJECT) $(LIB) \
		$(RUNTIME_LIB) $(RUNTIME_CRT0) $(STREAMS_LIB) $(VFS_LIB) \
		$(NETWORK_LIB)
	$(CXX) $(CXX_CPPFLAGS) -std=c++17 -Wall -Wextra -Werror -pedantic \
		$(CXX_TARGET_FLAGS) $(ASTRA_POSIX_STATIC_LDFLAGS) \
		-o $@ $(ASTRA_POSIX_STATIC_CRT0) \
		$(shell $(CXX) -print-file-name=crtbegin.o) \
	tests/cxx_runtime.cpp $(CXX_PROGRAM_OBJECT) \
		$(ASTRA_POSIX_STATIC_CXX_LIBS) \
		$(shell $(CXX) -print-file-name=crtend.o)
	@$(CROSS)readelf -SW $@ | grep -q ' \.init_array ' || \
		{ echo "C++ link contract: missing .init_array" >&2; exit 1; }
	@$(CROSS)readelf -SW $@ | grep -q ' \.fini_array ' || \
		{ echo "C++ link contract: missing .fini_array" >&2; exit 1; }
	@! $(CROSS)readelf -SW $@ | grep -Eq ' \.(ctors|dtors)([[:space:]]|$$)' || \
		{ echo "C++ link contract: legacy constructor sections" >&2; exit 1; }
	@$(CROSS)readelf -lW $@ | grep -q ' TLS ' || \
		{ echo "C++ link contract: missing PT_TLS" >&2; exit 1; }
	@echo "astra C++ runtime link: PASS"

$(RUNTIME_LIB) $(RUNTIME_CRT0) $(ASTRA_POSIX_CRT0): | static-libraries-ready
	@test -f $@

$(STREAMS_LIB): | static-libraries-ready
	@test -f $@

$(VFS_LIB): | static-libraries-ready
	@test -f $@

$(NETWORK_LIB): | static-libraries-ready
	@test -f $@

$(LINK_CONTRACT): tests/link_standard_main.c kit/program.c $(LIBC_LIBRARY) \
		$(ASTRA_POSIX_CRT0) $(ASTRA_DYNAMIC_USER_LD_INPUTS)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(ASTRA_POSIX_CFLAGS) \
		-DASTRA_POSIX_PROGRAM_NAME='"posix-link-test"' \
		-DASTRA_POSIX_PROGRAM_MAJOR=1 -DASTRA_POSIX_PROGRAM_MINOR=0 \
		-DASTRA_POSIX_PROGRAM_PATCH=0 \
		-DASTRA_POSIX_PROGRAM_AUTHOR='"Astra68"' \
		-DASTRA_POSIX_PROGRAM_COPYRIGHT='"Copyright 2026 Astra68 contributors"' \
		$(ASTRA_POSIX_LDFLAGS) \
		-o $@ $(ASTRA_POSIX_CRT0) tests/link_standard_main.c \
		$(ASTRA_POSIX_PROGRAM_SOURCE) $(ASTRA_POSIX_LIBS)
	@$(CROSS)nm $@ | grep -q ' T astra_main$$'
	@$(READELF) -lW $@ | grep -q ' INTERP '
	@$(READELF) -p .interp $@ | grep -q 'loader.library.1'
	@python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(READELF) \
		--needed libc.library.1 --needed runtime.library.1 \
		--needed compiler.library.1 $@
	@echo "astra posix dynamic standard main link: PASS"

test: $(HOST_TEST) $(PATH_TEST) $(DESCRIPTOR_TEST) $(STARTUP_TEST) $(POLL_TEST) $(IDENTITY_TEST) $(PROCESS_TEST) $(SIGNAL_TEST) $(SOCKET_ADDRESS_TEST) $(PTHREAD_TEST) $(RESOURCE_TEST) $(SCHEDULING_TEST) $(HEAP_TEST) $(ALLOC_TEST) $(UNISTD_COMPAT_TEST) $(ENTROPY_TEST) $(EXEC_COMPAT_TEST) $(DIRENT_COMPAT_TEST) $(TIME_COMPAT_TEST) $(TEMP_COMPAT_TEST) $(LIMITS_CONTRACT) $(CXX_HEADERS_CONTRACT) $(LINK_CONTRACT) $(CXX_LINK_CONTRACT)
	./$(HOST_TEST)
	./$(PATH_TEST)
	./$(DESCRIPTOR_TEST)
	./$(STARTUP_TEST)
	./$(POLL_TEST)
	./$(IDENTITY_TEST)
	./$(PROCESS_TEST)
	./$(SIGNAL_TEST)
	./$(SOCKET_ADDRESS_TEST)
	./$(PTHREAD_TEST)
	./$(RESOURCE_TEST)
	./$(SCHEDULING_TEST)
	./$(HEAP_TEST)
	./$(ALLOC_TEST)
	./$(UNISTD_COMPAT_TEST)
	./$(ENTROPY_TEST)
	./$(EXEC_COMPAT_TEST)
	./$(DIRENT_COMPAT_TEST)
	./$(TIME_COMPAT_TEST)
	./$(TEMP_COMPAT_TEST)

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
		$(COMMON_FLAGS) $(HOST_RESOURCE_DEFINES) -O2 -ffunction-sections \
		-Wl,--gc-sections \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
		-Dread=astra_posix_test_read -Dwrite=astra_posix_test_write \
		-Dpipe=astra_posix_test_pipe \
		src/console.c src/resource.c $(RUNTIME)/src/startup.c \
		tests/heap_prepare_stub.c tests/test_descriptor.c \
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

.PHONY: all library test sanitize analyze clean libraries-ready \
	static-libraries-ready \
	libc-library-contract libc-api-audit

-include $(OBJECTS:.o=.d) $(LIBRARY_OBJECTS:.o=.d)
