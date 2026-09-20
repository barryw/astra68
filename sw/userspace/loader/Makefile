include ../../../mk/m68k-cross.mk
ASTRA_SHARED_NO_CRT := 1
include ../../../mk/astra-shared-library.mk

CC := $(CROSS)gcc
READELF := $(CROSS)readelf
HOST_CC ?= cc
RUNTIME := ../runtime
VFS := ../vfs
NDK := ../../../ndk
PICOLIBC ?= $(HOME)/picolibc-astra
CPPFLAGS := -I../../include -I$(RUNTIME)/include -I$(VFS)/include \
	-I$(NDK)/include -isystem $(PICOLIBC)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -O3 -flto -ffreestanding \
	-fno-builtin -ffunction-sections -fdata-sections \
	$(ASTRA_SHARED_PIC_FLAGS) \
	-fvisibility=hidden $(ASTRA_DEBUG_FLAGS)
DEPFLAGS := -MMD -MP
RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart-loader-pic.a
VFS_LIB := $(VFS)/build/m68k/libastravfs-loader-pic.a
PICOLIBC_LIB := $(PICOLIBC)/lib/libc.a
OBJECTS := build/m68k/start.o build/m68k/self_relocate.o build/m68k/main.o \
	build/m68k/loader_identity.o
TARGET := build/m68k/loader.library.1
SELF_RELOCATE_TEST := build/host/test_self_relocate

all: contract

dependencies:
	+$(MAKE) -C $(RUNTIME) loader-pic
	+$(MAKE) -C $(VFS) loader-pic

ifeq ($(ASTRA_LOADER_DEPS_READY),)
contract:
	+$(MAKE) dependencies
	+$(MAKE) ASTRA_LOADER_DEPS_READY=1 contract
else

build/m68k/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/%.o: %.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ASTRA_TARGET_ABI_FLAGS) -fPIC $(DEPFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS) $(RUNTIME_LIB) $(VFS_LIB) \
		$(PICOLIBC_LIB) $(ASTRA_SHARED_LINK_INPUTS)
	@mkdir -p $(@D)
	$(CC) $(TARGET_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) -Wl,-e,_start \
		-Wl,-soname,loader.library.1 -o $@ $(OBJECTS) \
		-Wl,--start-group $(VFS_LIB) $(RUNTIME_LIB) \
		$(PICOLIBC_LIB) -lgcc -Wl,--end-group $(ASTRA_SHARED_LINK_END)

contract: $(TARGET)
	@python3 ../../../tools/check_shared_library_layout.py \
		--no-frame-registration $(TARGET)
	@$(READELF) -hW $(TARGET) | grep -q 'Type:.*DYN'
	@$(READELF) -hW $(TARGET) | grep -Eq 'Entry point address:.*0x[1-9a-f]'
	@$(READELF) -dW $(TARGET) | grep -Fq 'Library soname: [loader.library.1]'
	@$(READELF) -dW $(TARGET) | grep -q '(RELA).*0x[1-9a-f]'
	@$(READELF) -dW $(TARGET) | grep -q '(RELASZ).*[1-9][0-9]* (bytes)'
	@$(READELF) -dW $(TARGET) | grep -q '(RELAENT).*12 (bytes)'
	@test "$$($(READELF) -dW $(TARGET) | awk '/NEEDED/{n++} END{print n+0}')" = 0
	@test "$$($(READELF) -rW $(TARGET) | \
		awk '/R_68K_/ && $$3 != "R_68K_RELATIVE" && \
		     $$3 != "R_68K_TLS_TPREL32" {n++} END{print n+0}')" = 0
	@echo "astra process interpreter: PASS"

endif

$(SELF_RELOCATE_TEST): self_relocate.c self_relocate.h test_self_relocate.c
	@mkdir -p $(@D)
	$(HOST_CC) -I$(NDK)/include -I../../include $(COMMON_FLAGS) -O2 \
		self_relocate.c test_self_relocate.c -o $@

test: all $(SELF_RELOCATE_TEST)
	./$(SELF_RELOCATE_TEST)

sanitize: all
	@mkdir -p build/host
	$(HOST_CC) -I$(NDK)/include -I../../include $(COMMON_FLAGS) -O2 \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		self_relocate.c test_self_relocate.c \
		-o build/host/test_self_relocate_sanitize
	./build/host/test_self_relocate_sanitize

analyze: all
	@mkdir -p build/analyzer
	$(HOST_CC) -I$(NDK)/include -I../../include $(COMMON_FLAGS) -O2 -fanalyzer \
		-c self_relocate.c -o build/analyzer/self_relocate.o

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)

.PHONY: all dependencies contract test sanitize analyze clean
