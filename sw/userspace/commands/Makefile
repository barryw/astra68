include ../posix/kit/astra-posix.mk

# The programs that live in COMMANDS:.
#
# Each one is a directory with a source file of the same name, linked exactly
# the way the supervisor is -- same linker script, same crt0, same runtime --
# because "the initial image is a program like any other" has to be true in the
# build before it can be true anywhere else.
#
# There is no host half. A command is an m68k image and nothing about it runs
# on this host; what is testable about them is tested where the logic lives,
# and what is not is proved by the terminal gate running them on the machine.

CC := $(CROSS)gcc
CXX := $(CROSS)g++
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
READELF := $(CROSS)readelf
RUNTIME := $(ASTRA_POSIX_RUNTIME)
STREAMS := $(ASTRA_POSIX_STREAMS)
VFS := $(ASTRA_POSIX_VFS)
POSIX := $(ASTRA_POSIX_LAYER)
NTP := ../ntp
NETWORK := ../network
LUA_RUNTIME := ../lua
COMPILER := ../compiler
CXX_RUNTIME := ../cxx
TERMINFO := ../terminfo
NDK := $(ASTRA_POSIX_NDK)
LUA := ../../../third_party/lua/src
VIM_SOURCE ?= $(HOME)/vim-astra-v9.2.1001/src
VIM_RUNTIME_SOURCE ?= $(abspath $(VIM_SOURCE)/../runtime)
NCURSES_PREFIX ?= $(HOME)/ncurses-astra
VIM_IMAGE := $(VIM_SOURCE)/vim
VIM_RUNTIME_IMAGE := build/m68k/vim-runtime
ZSH_SOURCE ?= $(HOME)/zsh-astra-v5.9.2
ZSH_BUILD ?= $(HOME)/zsh-astra-build
ZSH_IMAGE := $(ZSH_BUILD)/Src/zsh
ZSH_CONFIG_SITE := $(CURDIR)/zsh/config.site
CPPFLAGS := $(ASTRA_POSIX_CPPFLAGS) -I$(NTP)/include \
	-I$(NETWORK)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
CXX_FLAGS := -std=c++17 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := $(filter-out -Os,$(ASTRA_POSIX_CFLAGS)) -O3 -flto \
	$(ASTRA_DEBUG_FLAGS)
CXX_TARGET_FLAGS := $(filter-out -Os,$(ASTRA_POSIX_CXXFLAGS)) -O3 -flto \
	$(ASTRA_DEBUG_FLAGS)
VENDOR_INCLUDE_FLAGS := -I$(abspath ../../include) \
	-I$(abspath $(RUNTIME)/include) -I$(abspath $(STREAMS)/include) \
	-I$(abspath $(VFS)/include) -I$(abspath $(POSIX)/include) \
	-I$(abspath $(NDK)/include) -I$(NCURSES_PREFIX)/include \
	-I$(NCURSES_PREFIX)/include/ncursesw \
	-isystem $(PICOLIBC)/include \
	-isystem $(shell $(CC) -print-file-name=include) \
	-D_GNU_SOURCE -D_DEFAULT_SOURCE -DNDEBUG
RUNTIME_LIBRARY := $(ASTRA_RUNTIME_LIBRARY)
SYSTEM_LIBRARY := $(ASTRA_SYSTEM_LIBRARY)
FILESYSTEM_LIBRARY := $(ASTRA_FILESYSTEM_LIBRARY)
LIBC_LIBRARY := $(ASTRA_LIBC_LIBRARY)
NTP_LIBRARY := $(ASTRA_NTP_LIBRARY)
NETWORK_LIBRARY := $(ASTRA_NETWORK_LIBRARY)
LUA_SHARED_LIBRARY := $(ASTRA_LUA_LIBRARY)
COMPILER_LIBRARY := $(ASTRA_COMPILER_LIBRARY)
CXX_LIBRARY := $(ASTRA_CXX_LIBRARY)
UNWIND_LIBRARY := $(ASTRA_UNWIND_LIBRARY)
TERMINFO_LIBRARY := $(ASTRA_TERMINFO_LIBRARY)

COMMANDS := status events devices which open service ls ps metrics cat mkdir rm \
	    heapbench fsstress date echo ntp ping lua vim zsh
TEST_COMMANDS := posix hello cxx
SOURCE_COMMANDS := $(filter-out lua cxx vim zsh,$(COMMANDS) $(TEST_COMMANDS))
SOURCE_IMAGES := $(patsubst %,build/m68k/%,$(SOURCE_COMMANDS))
TEST_IMAGES := $(patsubst %,build/m68k/%,$(TEST_COMMANDS))
events_EXTRA_SOURCES := events/events_support.c
date_EXTRA_SOURCES := date/date_support.c
ps_EXTRA_SOURCES := ps/ps_support.c

# A command names only the libraries whose symbols it calls.  The executable
# contract below compares this source-of-truth list with DT_NEEDED, so a new
# dependency cannot silently become part of every command or hide behind a
# transitive library.
status_LIBRARIES := $(LIBC_LIBRARY)
events_LIBRARIES := $(LIBC_LIBRARY) $(RUNTIME_LIBRARY) $(FILESYSTEM_LIBRARY)
devices_LIBRARIES := $(LIBC_LIBRARY) $(RUNTIME_LIBRARY)
which_LIBRARIES := $(LIBC_LIBRARY) $(RUNTIME_LIBRARY) $(FILESYSTEM_LIBRARY)
open_LIBRARIES := $(LIBC_LIBRARY) $(SYSTEM_LIBRARY) $(RUNTIME_LIBRARY)
service_LIBRARIES := $(LIBC_LIBRARY) $(SYSTEM_LIBRARY) $(RUNTIME_LIBRARY)
hello_LIBRARIES := $(LIBC_LIBRARY)
ls_LIBRARIES := $(LIBC_LIBRARY) $(RUNTIME_LIBRARY) $(FILESYSTEM_LIBRARY)
ps_LIBRARIES := $(LIBC_LIBRARY) $(RUNTIME_LIBRARY) $(FILESYSTEM_LIBRARY)
metrics_LIBRARIES := $(LIBC_LIBRARY) $(RUNTIME_LIBRARY) $(FILESYSTEM_LIBRARY)
cat_LIBRARIES := $(LIBC_LIBRARY) $(RUNTIME_LIBRARY)
mkdir_LIBRARIES := $(LIBC_LIBRARY) $(RUNTIME_LIBRARY) $(FILESYSTEM_LIBRARY)
rm_LIBRARIES := $(LIBC_LIBRARY) $(RUNTIME_LIBRARY) $(FILESYSTEM_LIBRARY)
posix_LIBRARIES := $(LIBC_LIBRARY) $(RUNTIME_LIBRARY)
heapbench_LIBRARIES := $(LIBC_LIBRARY) $(RUNTIME_LIBRARY)
fsstress_LIBRARIES := $(LIBC_LIBRARY) $(RUNTIME_LIBRARY)
date_LIBRARIES := $(LIBC_LIBRARY) $(RUNTIME_LIBRARY)
echo_LIBRARIES := $(LIBC_LIBRARY) $(RUNTIME_LIBRARY)
ntp_LIBRARIES := $(LIBC_LIBRARY) $(NTP_LIBRARY) $(RUNTIME_LIBRARY)
ping_LIBRARIES := $(LIBC_LIBRARY) $(NETWORK_LIBRARY) $(RUNTIME_LIBRARY)
ANALYZER_OBJECTS := $(patsubst %,build/analyzer/%.o,$(SOURCE_COMMANDS)) \
	build/analyzer/lua-adapter.o
# What the gate installs on the volume: stripped, the way the ROM's own image
# is, because a command carries no more DWARF onto a card than the supervisor
# carries into ROM. The unstripped ELF stays beside it for a debugger.
IMAGES := $(patsubst %,build/m68k/%,$(COMMANDS))
COMMAND_MANIFEST := build/m68k/.commands

# .SECONDEXPANSION so a command's source is <name>/<name>.c rather than a
# second list to keep in step with the first.
.SECONDEXPANSION:
LUA_CPPFLAGS := -I$(LUA) \
	-DLUA_PATH_DEFAULT='"?.lua;?/init.lua;LIBS:Lua.kit/share/lua/5.5/?.lua;LIBS:Lua.kit/share/lua/5.5/?/init.lua"' \
	-DLUA_CPATH_DEFAULT='""'
LUA_FLAGS := -std=gnu11 -Wall -Wextra -Werror

build/m68k/lua-obj/interpreter.o: $(LUA)/lua.c $(ASTRA_POSIX_POLICY_INPUTS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(LUA_CPPFLAGS) $(LUA_FLAGS) $(TARGET_FLAGS) \
		$(DEPFLAGS) -Dmain=astra_lua_main -c $< -o $@

lua_LIBRARIES := $(LIBC_LIBRARY) $(LUA_SHARED_LIBRARY) $(RUNTIME_LIBRARY)

build/m68k/lua: lua/lua.c build/m68k/lua-obj/interpreter.o \
		$(ASTRA_POSIX_PROGRAM_SOURCE) $(lua_LIBRARIES) $(COMPILER_LIBRARY) \
		$(ASTRA_POSIX_CRT0) $(ASTRA_DYNAMIC_USER_LD_INPUTS) \
		$(ASTRA_DYNAMIC_EXECUTABLE_CHECK) $(ASTRA_POSIX_POLICY_INPUTS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(LUA_CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) \
		$(DEPFLAGS) -DASTRA_POSIX_EXTERNAL_PROGRAM_IDENTITY=1 \
		$(ASTRA_POSIX_LDFLAGS) \
		-o $@.elf $(ASTRA_POSIX_CRT0) $< build/m68k/lua-obj/interpreter.o \
		$(ASTRA_POSIX_PROGRAM_SOURCE) \
		$(foreach library,$(lua_LIBRARIES),\
			-Wl,-l:$(notdir $(library))) \
		-Wl,-l:$(notdir $(COMPILER_LIBRARY))
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(READELF) \
		$(foreach library,$(lua_LIBRARIES),\
			--needed $(notdir $(library))) \
		--needed $(notdir $(COMPILER_LIBRARY)) $@.elf
	$(OBJCOPY) --strip-all --remove-section=.astra_events $@.elf $@
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(READELF) \
		$(foreach library,$(lua_LIBRARIES),\
			--needed $(notdir $(library))) \
		--needed $(notdir $(COMPILER_LIBRARY)) $@

CXX_PROGRAM := build/m68k/cxx-program.o
CXX_PROGRAM_DEFINES := -DASTRA_POSIX_PROGRAM_NAME='"cxx-runtime-test"' \
	-DASTRA_POSIX_PROGRAM_MAJOR=1 -DASTRA_POSIX_PROGRAM_MINOR=0 \
	-DASTRA_POSIX_PROGRAM_PATCH=0 \
	-DASTRA_POSIX_PROGRAM_AUTHOR='"Astra68"' \
	-DASTRA_POSIX_PROGRAM_COPYRIGHT='"Copyright 2026 Astra68 contributors"'
CXX_CPPFLAGS := $(ASTRA_POSIX_CXXCPPFLAGS)
cxx_LIBRARIES := $(CXX_LIBRARY) $(LIBC_LIBRARY) $(RUNTIME_LIBRARY) \
	$(UNWIND_LIBRARY) $(COMPILER_LIBRARY)

$(CXX_PROGRAM): $(ASTRA_POSIX_PROGRAM_SOURCE) Makefile \
		$(ASTRA_POSIX_POLICY_INPUTS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) \
		$(CXX_PROGRAM_DEFINES) -c $< -o $@

build/m68k/cxx: $(POSIX)/tests/cxx_runtime.cpp $(CXX_PROGRAM) \
		$(cxx_LIBRARIES) $(ASTRA_POSIX_CRT0) $(ASTRA_CXX_CRTBEGIN) \
		$(ASTRA_CXX_CRTEND) $(ASTRA_DYNAMIC_USER_LD_INPUTS) \
		$(ASTRA_DYNAMIC_EXECUTABLE_CHECK) $(ASTRA_POSIX_POLICY_INPUTS)
	@mkdir -p $(@D)
	$(CXX) $(CXX_CPPFLAGS) $(CXX_FLAGS) $(CXX_TARGET_FLAGS) \
		$(ASTRA_POSIX_LDFLAGS) \
		-o $@.elf $(ASTRA_POSIX_CRT0) $(ASTRA_CXX_CRTBEGIN) $< $(CXX_PROGRAM) \
		$(foreach library,$(cxx_LIBRARIES),\
			-Wl,-l:$(notdir $(library))) $(ASTRA_CXX_CRTEND)
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(READELF) \
		$(foreach library,$(cxx_LIBRARIES),\
			--needed $(notdir $(library))) $@.elf
	$(OBJCOPY) --strip-all --remove-section=.astra_events $@.elf $@
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(READELF) \
		$(foreach library,$(cxx_LIBRARIES),\
			--needed $(notdir $(library))) $@

VIM_PROGRAM := build/m68k/vim-program.o
VIM_PROGRAM_DEFINES := -DASTRA_POSIX_PROGRAM_NAME='"vim"' \
	-DASTRA_POSIX_PROGRAM_MAJOR=9 -DASTRA_POSIX_PROGRAM_MINOR=2 \
	-DASTRA_POSIX_PROGRAM_PATCH=1001 \
	-DASTRA_POSIX_PROGRAM_AUTHOR='"Vim contributors"' \
	-DASTRA_POSIX_PROGRAM_COPYRIGHT='"Copyright Vim contributors"'
VIM_PROGRAM_CONFIG_ID := $(shell printf '%s\n' vim 9 2 1001 \
	'Vim contributors' 'Copyright Vim contributors' | cksum | awk '{print $$1 "-" $$2}')
VIM_PROGRAM_CONFIG := build/.program-config/vim-$(VIM_PROGRAM_CONFIG_ID)
VIM_BUILD_CONFIG := build/.vendor-config/vim-$(ASTRA_TOOLCHAIN_ID)
vim_LIBRARIES := $(TERMINFO_LIBRARY) $(LIBC_LIBRARY) $(RUNTIME_LIBRARY) \
	$(COMPILER_LIBRARY)
VIM_LIBS := $(foreach library,$(vim_LIBRARIES),\
	-Wl,-l:$(notdir $(library)))

$(VIM_PROGRAM): $(ASTRA_POSIX_PROGRAM_SOURCE) $(VIM_PROGRAM_CONFIG) \
		$(ASTRA_POSIX_POLICY_INPUTS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) \
		$(VIM_PROGRAM_DEFINES) \
		-c $< -o $@

# Vim's generated Makefile does not know Astra's library products. Reconfigure
# and relink when its direct ABI set changes instead of packaging a stale
# executable that names an old SONAME.
$(VIM_BUILD_CONFIG): $(vim_LIBRARIES) $(ASTRA_POSIX_CRT0) \
		$(ASTRA_DYNAMIC_USER_LD_INPUTS) \
		$(ASTRA_TOOLCHAIN_STAMP) $(ASTRA_POSIX_POLICY_INPUTS)
	$(MAKE) -C $(VIM_SOURCE) clean
	@mkdir -p $(@D)
	@rm -f build/.vendor-config/vim-*
	@touch $@

$(VIM_IMAGE): Makefile $(VIM_BUILD_CONFIG) $(VIM_PROGRAM) \
		$(vim_LIBRARIES) $(ASTRA_POSIX_CRT0) $(ASTRA_DYNAMIC_USER_LD_INPUTS) \
		$(ASTRA_DYNAMIC_EXECUTABLE_CHECK) $(ASTRA_TOOLCHAIN_STAMP)
	@rm -f $(VIM_IMAGE)
	$(MAKE) -C $(VIM_SOURCE) vim \
		CC='$(CC)' \
		CPPFLAGS='$(VENDOR_INCLUDE_FLAGS)' \
		CFLAGS='$(TARGET_FLAGS) -D_REENTRANT \
		-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=1' \
		LDFLAGS='$(ASTRA_POSIX_LDFLAGS) \
		$(abspath $(ASTRA_POSIX_CRT0)) $(abspath $(VIM_PROGRAM))' \
		LIBS='$(VIM_LIBS)'
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(READELF) \
		$(foreach library,$(vim_LIBRARIES),\
			--needed $(notdir $(library))) $(VIM_IMAGE)

build/m68k/vim: $(VIM_IMAGE)
	@mkdir -p $(@D)
	$(OBJCOPY) --strip-all --remove-section=.astra_events $< $@
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(READELF) \
		$(foreach library,$(vim_LIBRARIES),\
			--needed $(notdir $(library))) $@

ZSH_PROGRAM := build/m68k/zsh-program.o
ZSH_PROGRAM_DEFINES := -DASTRA_POSIX_PROGRAM_NAME='"zsh"' \
	-DASTRA_POSIX_PROGRAM_MAJOR=5 -DASTRA_POSIX_PROGRAM_MINOR=9 \
	-DASTRA_POSIX_PROGRAM_PATCH=2 \
	-DASTRA_POSIX_PROGRAM_AUTHOR='"zsh contributors"' \
	-DASTRA_POSIX_PROGRAM_COPYRIGHT='"Copyright zsh contributors"'
ZSH_PROGRAM_CONFIG_ID := $(shell printf '%s\n' zsh 5 9 2 \
	'zsh contributors' 'Copyright zsh contributors' | cksum | awk '{print $$1 "-" $$2}')
ZSH_PROGRAM_CONFIG := build/.program-config/zsh-$(ZSH_PROGRAM_CONFIG_ID)
ZSH_BUILD_CONFIG := build/.vendor-config/zsh-$(ASTRA_TOOLCHAIN_ID)
ZSH_SOURCES := $(shell find $(ZSH_SOURCE) -type f 2>/dev/null)
zsh_LIBRARIES := $(TERMINFO_LIBRARY) $(LIBC_LIBRARY) $(RUNTIME_LIBRARY) \
	$(COMPILER_LIBRARY)
ZSH_LDFLAGS := $(ASTRA_POSIX_LDFLAGS) $(abspath $(ASTRA_POSIX_CRT0))
ZSH_LIBS := $(abspath $(ZSH_PROGRAM)) \
	$(foreach library,$(zsh_LIBRARIES),-Wl,-l:$(notdir $(library)))

# A direct command build validates only the owners of the DSOs that command
# names.  Keep this after every *_LIBRARIES declaration: evaluating a vendor
# program before its declaration would silently omit its dependencies.
# Owner contracts recursively validate transitive dependencies, so unrelated
# libraries must not turn a one-command build into a userspace rebuild.  The
# full suite naturally selects every owner below.
REQUESTED_COMMANDS := $(filter $(COMMANDS) $(TEST_COMMANDS),\
	$(notdir $(filter build/m68k/%,$(MAKECMDGOALS))))
FULL_PROGRAM_GOALS := $(filter $(COMMAND_MANIFEST) size,$(MAKECMDGOALS))
SELECTED_COMMANDS := $(if $(FULL_PROGRAM_GOALS),\
	$(COMMANDS) $(REQUESTED_COMMANDS),\
	$(if $(REQUESTED_COMMANDS),$(REQUESTED_COMMANDS),$(COMMANDS)))
MISSING_LIBRARY_DECLARATIONS := $(foreach command,$(SELECTED_COMMANDS),\
	$(if $($(command)_LIBRARIES),,$(command)))
ifneq ($(strip $(MISSING_LIBRARY_DECLARATIONS)),)
$(error commands lack a direct shared-library declaration: $(MISSING_LIBRARY_DECLARATIONS))
endif
SELECTED_LIBRARIES := $(sort $(COMPILER_LIBRARY) \
	$(foreach command,$(SELECTED_COMMANDS),$($(command)_LIBRARIES)))
SELECTED_LIBRARY_KEYS := $(call astra_library_keys,$(SELECTED_LIBRARIES))
ifneq ($(words $(SELECTED_LIBRARY_KEYS)),$(words $(SELECTED_LIBRARIES)))
$(error selected command library is absent from libraries.mk: $(SELECTED_LIBRARIES))
endif
ASTRA_PROGRAM_OWNER_TARGETS := $(call astra_library_owner_targets,\
	$(SELECTED_LIBRARY_KEYS))
ASTRA_PROGRAM_TARGETS := $(IMAGES) $(VIM_RUNTIME_IMAGE) $(COMMAND_MANIFEST) size
ASTRA_PROGRAM_DIRECT_TARGETS := $(ASTRA_PROGRAM_TARGETS) $(TEST_IMAGES)
include ../program.mk

$(VIM_RUNTIME_IMAGE): __vim_runtime_force
	@test -f $(VIM_RUNTIME_SOURCE)/defaults.vim || { \
		echo "Vim runtime missing defaults.vim: $(VIM_RUNTIME_SOURCE)" >&2; \
		exit 1; \
	}
	@mkdir -p $@
	rsync -a --delete $(VIM_RUNTIME_SOURCE)/ $@/

.PHONY: __vim_runtime_force

$(ZSH_PROGRAM): $(ASTRA_POSIX_PROGRAM_SOURCE) $(ZSH_PROGRAM_CONFIG) \
		$(ASTRA_POSIX_POLICY_INPUTS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) \
		$(ZSH_PROGRAM_DEFINES) \
		-c $< -o $@

build/.program-config/%:
	@mkdir -p $(@D)
	@touch $@

$(ZSH_BUILD_CONFIG): $(ZSH_CONFIG_SITE) $(ZSH_SOURCE)/configure \
		$(zsh_LIBRARIES) $(ASTRA_POSIX_CRT0) $(ASTRA_DYNAMIC_USER_LD_INPUTS) \
		$(ASTRA_TOOLCHAIN_STAMP) $(ASTRA_POSIX_POLICY_INPUTS) $(ZSH_PROGRAM)
	rm -rf $(ZSH_BUILD)
	mkdir -p $(ZSH_BUILD)
	cd $(ZSH_BUILD) && \
		CONFIG_SITE='$(ZSH_CONFIG_SITE)' \
		CC='$(CC)' \
		CFLAGS='$(TARGET_FLAGS)' \
		CPPFLAGS='$(VENDOR_INCLUDE_FLAGS)' \
		LDFLAGS='$(ZSH_LDFLAGS)' \
		LIBS='$(ZSH_LIBS)' \
		$(ZSH_SOURCE)/configure --host=m68k-unknown-elf \
		--cache-file=/dev/null --disable-dynamic --disable-pcre \
		--enable-multibyte \
		--enable-etcdir=CONFIG:
	@mkdir -p $(@D)
	@rm -f build/.vendor-config/zsh-*
	@touch $@

$(ZSH_IMAGE): $(ZSH_SOURCES) $(ZSH_BUILD_CONFIG) $(ZSH_PROGRAM) \
		$(zsh_LIBRARIES) $(ASTRA_POSIX_CRT0) $(ASTRA_DYNAMIC_USER_LD_INPUTS) \
		$(ASTRA_DYNAMIC_EXECUTABLE_CHECK)
	@rm -f $(ZSH_IMAGE)
	$(MAKE) -C $(ZSH_BUILD)
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(READELF) \
		$(foreach library,$(zsh_LIBRARIES),\
			--needed $(notdir $(library))) $(ZSH_IMAGE)

build/m68k/zsh: $(ZSH_IMAGE)
	@mkdir -p $(@D)
	$(OBJCOPY) --strip-all --remove-section=.astra_events $< $@
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(READELF) \
		$(foreach library,$(zsh_LIBRARIES),\
			--needed $(notdir $(library))) $@

define ASTRA_SOURCE_COMMAND
build/m68k/$(1): $(1)/$(1).c $$($(1)_EXTRA_SOURCES) \
		$(ASTRA_POSIX_PROGRAM_SOURCE) \
		$$($(1)_LIBRARIES) $(COMPILER_LIBRARY) $(ASTRA_POSIX_CRT0) \
		$(ASTRA_DYNAMIC_USER_LD_INPUTS) $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) \
		$(ASTRA_POSIX_POLICY_INPUTS)
	@mkdir -p $$(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) \
		-DASTRA_POSIX_EXTERNAL_PROGRAM_IDENTITY=1 \
		$(ASTRA_POSIX_LDFLAGS) \
		-o $$@.elf $(ASTRA_POSIX_CRT0) $$< $$($(1)_EXTRA_SOURCES) \
		$(ASTRA_POSIX_PROGRAM_SOURCE) \
		$$(foreach library,$$($(1)_LIBRARIES),\
			-Wl,-l:$$(notdir $$(library))) \
		-Wl,-l:$$(notdir $$(COMPILER_LIBRARY))
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(READELF) \
		$$(foreach library,$$($(1)_LIBRARIES),\
			--needed $$(notdir $$(library))) \
		--needed $$(notdir $$(COMPILER_LIBRARY)) $$@.elf
	$(OBJCOPY) --strip-all --remove-section=.astra_events $$@.elf $$@
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(READELF) \
		$$(foreach library,$$($(1)_LIBRARIES),\
			--needed $$(notdir $$(library))) \
		--needed $$(notdir $$(COMPILER_LIBRARY)) $$@
endef

$(foreach command,$(SOURCE_COMMANDS),\
	$(eval $(call ASTRA_SOURCE_COMMAND,$(command))))

dynamic-source-commands: $(SOURCE_IMAGES)

size: $(IMAGES)
	$(SIZE) $(IMAGES)

$(COMMAND_MANIFEST): Makefile $(IMAGES)
	@{ for command in $(COMMANDS); do printf '%s\n' "$$command"; done; } >$@.tmp
	mv $@.tmp $@

# Header dependencies: a stale object compiled against an older ABI
# constant is indistinguishable from a working one until it runs.
-include $(wildcard build/m68k/*.d build/m68k/lua-obj/*.d)

clean:
	rm -rf build

HOST_CC ?= cc
HOST_CPPFLAGS := -I../../include -I../../../ndk/include -I../runtime/include \
	-I../vfs/include
HOST_CFLAGS := -std=c11 -Wall -Wextra -Werror -pedantic -O2

build/host/test_events_support: events/events_support.c \
		tests/test_events_support.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CPPFLAGS) $(HOST_CFLAGS) $^ -o $@

build/host/test_date_support: date/date_support.c tests/test_date_support.c \
		../../common/civil.c ../../common/divide.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CPPFLAGS) $(HOST_CFLAGS) $^ -o $@

build/host/test_ps_support: ps/ps_support.c tests/test_ps_support.c \
		../../common/divide.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CPPFLAGS) $(HOST_CFLAGS) -D_GNU_SOURCE $^ -o $@

build/host/test_open: tests/test_open.c open/open.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CPPFLAGS) $(HOST_CFLAGS) $< -o $@

test: build/host/test_events_support build/host/test_date_support \
		build/host/test_ps_support build/host/test_open
	./build/host/test_events_support
	./build/host/test_date_support
	./build/host/test_ps_support
	./build/host/test_open

sanitize: test

analyze: $(ANALYZER_OBJECTS)

build/analyzer/lua-adapter.o: lua/lua.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer -c $< -o $@

build/analyzer/%.o: $$*/$$*.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer -c $< -o $@

.PHONY: all libraries dynamic-source-commands size test sanitize \
	analyze clean
