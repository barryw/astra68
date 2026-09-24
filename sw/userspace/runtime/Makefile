include ../../../mk/m68k-cross.mk
include ../../../mk/astra-shared-library.mk

CC := $(CROSS)gcc
AR := $(CROSS)ar
SIZE := $(CROSS)size
READELF := $(CROSS)readelf
HOST_CC ?= cc
ANALYZER_CC ?= gcc
NDK := ../../../ndk
COMPILER := ../compiler
CPPFLAGS := -I$(NDK)/include -Iinclude -I../../include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -O3 -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
PIC_FLAGS := $(TARGET_FLAGS) -fPIC -fvisibility=hidden
HOST_FLAGS := -O2 -fno-builtin
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
TARGET_C_SOURCES := src/startup.c src/lifecycle.c src/service.c src/arguments.c src/syscall.c src/stream_source.c src/executable.c src/launch.c src/port.c src/posix_process.c \
	src/event_control.c src/memory.c \
	src/host_channel_client.c \
	src/log.c src/sort.c src/assert.c src/event_catalog.c src/allocator_heap.c src/allocator.c \
	src/dynamic_loader.c src/dynamic_process.c
TARGET_ASM_SOURCES := src/m68k/syscall.S src/m68k/thread_start.S \
	src/m68k/read_tp.S
COMMON_ASM_SOURCES := ../../common/memory_m68k.S
# The calendar is shared with the kernel rather than copied into userspace:
# `ls -l`, `date` and the boot line render the same instant, and two
# implementations of a leap year is one too many.
COMMON_C_SOURCES := ../../common/civil.c ../../common/divide.c
NDK_C_SOURCES := $(NDK)/src/utf8.c
TARGET_OBJECTS := $(patsubst src/%.c,build/m68k/%.o,$(TARGET_C_SOURCES)) \
	$(patsubst ../../common/%.c,build/m68k/common_%.o,$(COMMON_C_SOURCES)) \
	build/m68k/ndk_utf8.o \
	$(patsubst src/%.S,build/m68k/%.o,$(TARGET_ASM_SOURCES)) \
	$(patsubst ../../common/%.S,build/m68k/common_%.o,$(COMMON_ASM_SOURCES))
ANALYZER_OBJECTS := $(patsubst src/%.c,build/analyzer/%.o,$(TARGET_C_SOURCES)) \
	$(patsubst ../../common/%.c,build/analyzer/common_%.o,$(COMMON_C_SOURCES))
TARGET := build/m68k/libastrart.a
PIC_TARGET := build/m68k/libastrart-pic.a
PIC_OBJECTS := build/m68k/pic/startup.o build/m68k/pic/syscall.o build/m68k/pic/port.o \
	build/m68k/pic/memory.o \
	build/m68k/pic/log.o build/m68k/pic/assert.o \
	build/m68k/pic/event_catalog.o \
	build/m68k/pic/m68k/syscall.o build/m68k/pic/common_memory_m68k.o
DYNAMIC_PIC_C_SOURCES := $(filter-out src/dynamic_process.c,\
	$(TARGET_C_SOURCES))
DYNAMIC_PIC_OBJECTS := \
	$(patsubst src/%.c,build/m68k/dynamic/%.o,$(DYNAMIC_PIC_C_SOURCES)) \
	$(patsubst ../../common/%.c,build/m68k/dynamic/common_%.o,$(COMMON_C_SOURCES)) \
	build/m68k/dynamic/ndk_utf8.o \
	$(patsubst src/%.S,build/m68k/dynamic/%.o,$(TARGET_ASM_SOURCES)) \
	$(patsubst ../../common/%.S,build/m68k/dynamic/common_%.o,$(COMMON_ASM_SOURCES))
LOADER_PIC_TARGET := build/m68k/libastrart-loader-pic.a
LOADER_PIC_OBJECTS := $(DYNAMIC_PIC_OBJECTS) \
	build/m68k/dynamic/dynamic_process.o
DYNAMIC_IDENTITY := build/m68k/dynamic/runtime_library.o
DYNAMIC_LIBRARY := build/m68k/libraries/runtime.library.1
COMPILER_LIBRARY := $(COMPILER)/build/m68k/libraries/compiler.library.1
DYNAMIC_LIBRARY_LD := $(ASTRA_SHARED_LINK_INPUTS)
DYNAMIC_LIBRARY_MAP := runtime.library.exports
SYMBOL_OWNERSHIP_CHECK := ../../../tools/check_shared_symbol_ownership.py
DYNAMIC_PIC_FLAGS := $(TARGET_FLAGS) $(ASTRA_SHARED_PIC_FLAGS) \
	-DASTRA_RUNTIME_DYNAMIC_LIBRARY=1
DYNAMIC_PUBLIC_HEADERS := $(wildcard $(NDK)/include/astra/*.h) \
	$(wildcard ../../include/astra/*.h) $(wildcard include/astra/*.h)
KERNEL_ELF_TEST := ../../kernel/build/test_elf
CRT0 := build/m68k/crt0.o
HOSTED_CRT0 := build/m68k/crt0-hosted.o
DYNAMIC_CRT0 := build/m68k/crt0-dynamic.o
HOST_TEST := build/host/test_runtime
STARTUP_SERVICE_TEST := build/host/test_startup_service
LIFECYCLE_TEST := build/host/test_lifecycle
POSIX_PROCESS_TEST := build/host/test_posix_process
DYNAMIC_LOADER_TEST := build/host/test_dynamic_loader
ALLOCATOR_TEST := build/host/test_allocator
HOST_CHANNEL_CLIENT_TEST := build/host/test_host_channel_client
DYNAMIC_CONTRACT_LIBRARY := build/contract/dynamic.library
DYNAMIC_CONTRACT_PROGRAM := build/contract/dynamic.elf
DYNAMIC_TLS_CONTRACT_LIBRARY := build/contract/dynamic-tls.library

all: $(TARGET) $(CRT0) $(HOSTED_CRT0) $(DYNAMIC_CRT0) $(LOADER_PIC_TARGET) link-contract \
	dynamic-link-contract runtime-library-contract size

library: $(DYNAMIC_LIBRARY)

pic: $(PIC_TARGET)

loader-pic: $(LOADER_PIC_TARGET)

$(TARGET): $(TARGET_OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

$(PIC_TARGET): $(PIC_OBJECTS)
	$(ASTRA_REPLACE_ARCHIVE)

$(LOADER_PIC_TARGET): $(LOADER_PIC_OBJECTS) Makefile
	$(ASTRA_REPLACE_ARCHIVE)

build/m68k/pic/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(PIC_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/pic/%.o: src/%.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ASTRA_TARGET_ABI_FLAGS) -fPIC $(DEPFLAGS) -c $< -o $@

build/m68k/pic/common_%.o: ../../common/%.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ASTRA_TARGET_ABI_FLAGS) -fPIC $(DEPFLAGS) -c $< -o $@

build/m68k/dynamic/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(DYNAMIC_PIC_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

build/m68k/dynamic/%.o: src/%.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ASTRA_TARGET_ABI_FLAGS) -fPIC $(DEPFLAGS) \
		-c $< -o $@

build/m68k/dynamic/common_%.o: ../../common/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(DYNAMIC_PIC_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

build/m68k/dynamic/common_%.o: ../../common/%.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ASTRA_TARGET_ABI_FLAGS) -fPIC $(DEPFLAGS) \
		-c $< -o $@

build/m68k/dynamic/ndk_utf8.o: $(NDK)/src/utf8.c $(NDK)/include/astra/utf8.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(DYNAMIC_PIC_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

$(DYNAMIC_IDENTITY): src/runtime_library.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(DYNAMIC_PIC_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

compiler-library-ready:
	$(MAKE) -C $(COMPILER) library

$(DYNAMIC_LIBRARY): $(DYNAMIC_IDENTITY) $(DYNAMIC_PIC_OBJECTS) \
		$(DYNAMIC_LIBRARY_LD) $(DYNAMIC_LIBRARY_MAP) \
		| compiler-library-ready
	@mkdir -p $(@D)
	$(CC) $(ASTRA_TARGET_ABI_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,runtime.library.1 \
		-Wl,--version-script=$(DYNAMIC_LIBRARY_MAP) \
		-o $@ \
		$(DYNAMIC_IDENTITY) $(DYNAMIC_PIC_OBJECTS) \
		-L$(dir $(COMPILER_LIBRARY)) -Wl,--no-as-needed \
		-Wl,-l:compiler.library.1 $(ASTRA_SHARED_LINK_END)

$(KERNEL_ELF_TEST):
	$(MAKE) -C ../../kernel build/test_elf

runtime-library-contract: $(DYNAMIC_LIBRARY) $(KERNEL_ELF_TEST)
	@python3 ../../../tools/check_shared_library_layout.py $(DYNAMIC_LIBRARY)
	@python3 $(SYMBOL_OWNERSHIP_CHECK) --readelf $(READELF) \
		$(DYNAMIC_LIBRARY) $(COMPILER_LIBRARY)
	@python3 ../../../tools/check_library_exports.py $(DYNAMIC_LIBRARY_MAP) \
		$(DYNAMIC_PUBLIC_HEADERS) --allow __m68k_read_tp \
		--allow astra_runtime_initialize --allow astra_runtime_finalize \
		--allow memcmp --allow memcpy --allow memmove --allow memset \
		--allow strcmp --allow strcpy --allow strlen \
		--allow strncmp --allow strncpy
	@python3 ../../../tools/library_info.py $< | \
		grep -q '^runtime.library.1 1.8.0 ABI 1.8$$'
	@$(READELF) -hW $< | grep 'Type:.*DYN' >/dev/null
	@$(READELF) -dW $< | grep -F 'Library soname: [runtime.library.1]' >/dev/null
	@$(READELF) -dW $< | grep 'BIND_NOW' >/dev/null
	@test "$$($(READELF) -dW $< | \
		grep -Fc 'Shared library: [compiler.library.1]')" = 1
	@test "$$($(READELF) -dW $< | awk '/NEEDED/{n++} END{print n+0}')" = 1
	@test "$$($(READELF) -dW $< | awk '/TEXTREL/{n++} END{print n+0}')" = 0
	@test "$$($(READELF) -SW $< | \
		awk '/ \\.(tdata|tbss) /{n++} END{print n+0}')" = 0
	@$(READELF) -lW $< | grep ' GNU_RELRO ' >/dev/null
	@$(READELF) -VW $< | grep 'ASTRA_RUNTIME_1.0' >/dev/null
	@$(READELF) -VW $< | grep 'ASTRA_RUNTIME_1.2' >/dev/null
	@$(READELF) -VW $< | grep 'ASTRA_RUNTIME_1.3' >/dev/null
	@$(READELF) -VW $< | grep 'ASTRA_RUNTIME_1.4' >/dev/null
	@$(READELF) -VW $< | grep 'ASTRA_RUNTIME_1.5' >/dev/null
	@$(READELF) -VW $< | grep 'ASTRA_RUNTIME_1.6' >/dev/null
	@ASTRA_LIBRARY_FIXTURE=$(abspath $<) $(KERNEL_ELF_TEST) >/dev/null
	@echo "astra runtime shared library: PASS"

build/m68k/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/common_%.o: ../../common/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/ndk_utf8.o: $(NDK)/src/utf8.c $(NDK)/include/astra/utf8.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/common_%.o: ../../common/%.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ASTRA_TARGET_ABI_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/%.o: src/%.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ASTRA_TARGET_ABI_FLAGS) $(DEPFLAGS) -c $< -o $@

$(CRT0): src/m68k/crt0.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ASTRA_TARGET_ABI_FLAGS) $(DEPFLAGS) -c $< -o $@

$(HOSTED_CRT0): src/m68k/crt0.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ASTRA_TARGET_ABI_FLAGS) -DASTRA_HOSTED_STARTUP=1 \
		$(DEPFLAGS) -c $< -o $@

$(DYNAMIC_CRT0): src/m68k/crt0.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ASTRA_TARGET_ABI_FLAGS) -fPIC \
		-DASTRA_DYNAMIC_STARTUP=1 $(DEPFLAGS) -c $< -o $@

HOST_TEST_SOURCES := src/startup.c src/arguments.c src/syscall.c src/stream_source.c src/executable.c src/launch.c src/port.c \
	src/event_control.c src/memory.c \
	src/host_channel_client.c \
	src/sort.c src/log.c src/event_catalog.c src/dynamic_loader.c \
	$(COMMON_C_SOURCES) $(NDK_C_SOURCES) \
	tests/test_runtime.c

$(HOST_TEST): $(HOST_TEST_SOURCES)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		-no-pie $^ -o $@

$(STARTUP_SERVICE_TEST): src/startup.c src/service.c tests/test_startup_service.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -no-pie $^ -o $@

$(LIFECYCLE_TEST): src/lifecycle.c tests/test_lifecycle.c
	@mkdir -p $(@D)
	$(HOST_CC) $(COMMON_FLAGS) $(HOST_FLAGS) $^ -o $@

$(POSIX_PROCESS_TEST): src/posix_process.c tests/test_posix_process.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $^ -o $@

$(ALLOCATOR_TEST): src/allocator_heap.c src/allocator.c \
		src/allocator_internal.h tests/test_allocator.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -pthread \
		src/allocator_heap.c src/allocator.c tests/test_allocator.c -o $@

$(HOST_CHANNEL_CLIENT_TEST): src/host_channel_client.c \
		tests/test_host_channel_client.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -no-pie $^ -o $@

$(DYNAMIC_LOADER_TEST): src/dynamic_loader.c tests/test_dynamic_loader.c \
		src/dynamic_process.c \
		$(DYNAMIC_CONTRACT_PROGRAM) $(DYNAMIC_CONTRACT_LIBRARY) \
		$(DYNAMIC_TLS_CONTRACT_LIBRARY)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		-DASTRA_DYNAMIC_PROCESS_TEST src/dynamic_loader.c \
		src/dynamic_process.c tests/test_dynamic_loader.c -o $@

test: $(HOST_TEST) $(STARTUP_SERVICE_TEST) $(LIFECYCLE_TEST) \
		$(POSIX_PROCESS_TEST) $(ALLOCATOR_TEST) $(HOST_CHANNEL_CLIENT_TEST) \
		$(DYNAMIC_LOADER_TEST)
	./$(HOST_TEST)
	./$(STARTUP_SERVICE_TEST)
	./$(LIFECYCLE_TEST)
	./$(POSIX_PROCESS_TEST)
	./$(ALLOCATOR_TEST)
	./$(HOST_CHANNEL_CLIENT_TEST)
	./$(DYNAMIC_LOADER_TEST) $(DYNAMIC_CONTRACT_PROGRAM) \
		$(DYNAMIC_CONTRACT_LIBRARY) $(DYNAMIC_TLS_CONTRACT_LIBRARY)

sanitize:
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) -DASTRA_LIBRARY_LOADER_TEST -no-pie \
		$(HOST_TEST_SOURCES) \
		-o build/host/test_runtime_sanitize
	./build/host/test_runtime_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) -DASTRA_DYNAMIC_PROCESS_TEST \
		src/dynamic_loader.c src/dynamic_process.c \
		tests/test_dynamic_loader.c -o build/host/test_dynamic_loader_sanitize
	./build/host/test_dynamic_loader_sanitize $(DYNAMIC_CONTRACT_PROGRAM) \
		$(DYNAMIC_CONTRACT_LIBRARY) $(DYNAMIC_TLS_CONTRACT_LIBRARY)

analyze: $(ANALYZER_OBJECTS)

build/analyzer/%.o: src/%.c
	@mkdir -p $(@D)
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c $< -o $@

build/analyzer/common_%.o: ../../common/%.c
	@mkdir -p $(@D)
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c $< -o $@

# The rule that every Astra image declares its own provenance, checked where
# images are made rather than where headers are included. Two links against the
# real astra_user.ld: one that must succeed and one that must fail.
#
# --gc-sections is on, matching the supervisor's link, because the collector is
# the whole reason .astra_program needs KEEP -- a contract test that dropped the
# flag would pass while the real build silently shipped no record.
#
# The successful image is then read back by tools/program_info.py. That is the
# only check anywhere that the C struct and the Python parser agree: the tool's
# own pytest builds its fixture by hand, which pins the format but cannot
# notice the compiler laying the struct out differently.
USER_LINK_SCRIPTS := astra_user_contract.ld astra_user_sections.ld
LINK_CONTRACT_FLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections -Wl,-L. \
	-T astra_static_user.ld

link-contract: tests/link_contract_with.c tests/link_contract_without.c \
		astra_static_user.ld $(USER_LINK_SCRIPTS)
	@mkdir -p build/contract
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) \
		$(LINK_CONTRACT_FLAGS) tests/link_contract_with.c \
		-o build/contract/with.elf
	@python3 ../../../tools/program_info.py build/contract/with.elf | \
		grep -q '^contract 1\.2\.3$$' || \
		{ echo "link contract: the record did not survive the link"; \
		  python3 ../../../tools/program_info.py build/contract/with.elf; \
		  exit 1; }
	@$(READELF) -lW build/contract/with.elf | grep -q ' TLS ' || \
		{ echo "link contract: PT_TLS is missing"; exit 1; }
	@$(READELF) -SW build/contract/with.elf | grep -q ' \.init_array ' || \
		{ echo "link contract: constructors are missing"; exit 1; }
	@$(READELF) -SW build/contract/with.elf | grep -q ' \.fini_array ' || \
		{ echo "link contract: destructors are missing"; exit 1; }
	@if $(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) \
		$(LINK_CONTRACT_FLAGS) tests/link_contract_without.c \
		-o build/contract/without.elf >/dev/null 2>&1; then \
		echo "link contract: an image with no ASTRA_PROGRAM record linked"; \
		exit 1; \
	fi
	@echo "link contract: PASS"

$(DYNAMIC_CONTRACT_LIBRARY): tests/dynamic_library_contract.c \
		tests/dynamic_contract.exports Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(ASTRA_TARGET_ABI_FLAGS) -Os \
		-fPIC -fvisibility=hidden -nostdlib -shared \
		-Wl,--build-id=none -Wl,-Bsymbolic \
		-Wl,-soname,dynamic.library.1 \
		-Wl,--version-script=tests/dynamic_contract.exports \
		-Wl,-z,now -Wl,-z,relro -o $@ $<

$(DYNAMIC_CONTRACT_PROGRAM): tests/dynamic_program_contract.c \
		$(DYNAMIC_CONTRACT_LIBRARY) astra_user.ld $(USER_LINK_SCRIPTS) \
		../../../tools/check_dynamic_executable.py \
		../../../tools/check_dynamic_relocations.py Makefile
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(ASTRA_TARGET_ABI_FLAGS) -Os \
		-fPIC -pie -ffunction-sections -fdata-sections -nostdlib \
		-Wl,-Bdynamic -Wl,--no-as-needed \
		-Wl,-z,now -Wl,-z,relro -Wl,-z,max-page-size=0x1000 \
		-Wl,--build-id=none -Wl,--gc-sections -Wl,-L. -T astra_user.ld \
		-o $@ $< $(DYNAMIC_CONTRACT_LIBRARY)
	python3 ../../../tools/check_dynamic_executable.py --readelf $(READELF) \
		--needed dynamic.library.1 $@

$(DYNAMIC_TLS_CONTRACT_LIBRARY): tests/dynamic_tls_library_contract.c \
		tests/dynamic_tls_contract.exports Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(ASTRA_TARGET_ABI_FLAGS) -Os \
		-fPIC -ftls-model=initial-exec -fvisibility=hidden -nostdlib -shared \
		-Wl,--build-id=none -Wl,-soname,dynamic-tls.library.1 \
		-Wl,--version-script=tests/dynamic_tls_contract.exports \
		-Wl,-z,now -Wl,-z,relro -o $@ $<

dynamic-link-contract: $(DYNAMIC_CONTRACT_PROGRAM)
	@$(READELF) -hW $< | grep -q 'Type:.*EXEC'
	@$(READELF) -lW $< | grep -q ' DYNAMIC '
	@$(READELF) -lW $< | grep -q ' INTERP '
	@$(READELF) -p .interp $< | grep -q 'loader.library.1'
	@$(READELF) -dW $< | grep -Fq 'Shared library: [dynamic.library.1]'
	@$(READELF) -dW $(DYNAMIC_CONTRACT_LIBRARY) | grep -q 'SYMBOLIC'
	@$(READELF) -dW $< | grep -q 'BIND_NOW'
	@$(READELF) -rW $< | grep -Eq 'R_68K_(JMP_SLOT|GLOB_DAT)'
	@$(READELF) -VW $< | grep -q 'ASTRA_1.0'
	@echo "astra eager dynamic link: PASS"

size: $(TARGET_OBJECTS) $(CRT0) $(HOSTED_CRT0) $(DYNAMIC_CRT0)
	$(SIZE) $^

# Header dependencies: a stale object compiled against an older ABI
# constant is indistinguishable from a working one until it runs.
-include $(wildcard build/m68k/*.d build/m68k/*/*.d build/m68k/*/*/*.d)

clean:
	rm -rf build

.PHONY: all library pic loader-pic test sanitize analyze link-contract dynamic-link-contract \
	compiler-library-ready runtime-library-contract size clean
