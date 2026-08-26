include ../../../mk/m68k-cross.mk

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
AR := $(CROSS)ar
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
RUNTIME := ../runtime
STREAMS := ../streams
VFS := ../vfs
POSIX := ../posix
NDK := ../../../ndk
LUA := ../../../third_party/lua/src
VIM_SOURCE ?= $(HOME)/vim-astra-v9.2.1001/src
VIM_TERMINFO ?= $(HOME)/ncurses-astra
VIM_IMAGE := $(VIM_SOURCE)/vim
# picolibc, built out of tree: see third_party/picolibc/ASTRA_VENDOR.md.
PICOLIBC ?= $(HOME)/picolibc-astra
CPPFLAGS := -I../../include -I$(RUNTIME)/include -I$(STREAMS)/include \
	-I$(VFS)/include -I$(POSIX)/include -I$(NDK)/include \
	-isystem $(PICOLIBC)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := -m68030 -msoft-float -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
LINK_FLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections -T $(RUNTIME)/astra_user.ld

RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart.a
STREAMS_LIB := $(STREAMS)/build/m68k/libastrastreams.a
VFS_LIB := $(VFS)/build/m68k/libastravfs.a
POSIX_LIB := $(POSIX)/build/m68k/libastraposix.a
PICOLIBC_LIB := $(PICOLIBC)/lib/libc.a
PICOLIBC_MATH := $(PICOLIBC)/lib/libm.a
RUNTIME_CRT0 := $(RUNTIME)/build/m68k/crt0.o

COMMANDS := status events devices which hello ls ps cat mkdir rm posix \
            heapbench date echo lua vim
NATIVE_COMMANDS := $(filter-out lua vim,$(COMMANDS))
ANALYZER_OBJECTS := $(patsubst %,build/analyzer/%.o,$(NATIVE_COMMANDS)) \
	build/analyzer/lua-adapter.o
# What the gate installs on the volume: stripped, the way the ROM's own image
# is, because a command carries no more DWARF onto a card than the supervisor
# carries into ROM. The unstripped ELF stays beside it for a debugger.
IMAGES := $(patsubst %,build/m68k/%,$(COMMANDS))
ASTRA_PROGRAM_OWNER_DIRS := $(RUNTIME) $(STREAMS) $(VFS) $(POSIX)
ASTRA_PROGRAM_TARGETS := $(IMAGES) size
include ../program.mk

# .SECONDEXPANSION so a command's source is <name>/<name>.c rather than a
# second list to keep in step with the first.
.SECONDEXPANSION:
# Picolibc's own archive also defines this symbol. Make the shared POSIX
# integer formatter unresolved before archive scanning so date selects the
# small implementation without carrying a command-private copy.
build/m68k/date: LINK_FLAGS += -Wl,--undefined=__d_snprintf
LUA_SOURCES := $(filter-out $(LUA)/lua.c $(LUA)/luac.c,$(wildcard $(LUA)/*.c))
LUA_OBJECTS := $(patsubst $(LUA)/%.c,build/m68k/lua-obj/%.o,$(LUA_SOURCES))
LUA_LIBRARY := build/m68k/liblua.a
LUA_CPPFLAGS := -I$(LUA) \
	-DLUA_PATH_DEFAULT='"?.lua;?/init.lua;LIBS:Lua.kit/share/lua/5.5/?.lua;LIBS:Lua.kit/share/lua/5.5/?/init.lua"' \
	-DLUA_CPATH_DEFAULT='""'
LUA_FLAGS := -std=gnu11 -Wall -Wextra -Werror

$(LUA_LIBRARY): $(LUA_OBJECTS)
	$(AR) rcs $@ $^

build/m68k/lua-obj/%.o: $(LUA)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(LUA_CPPFLAGS) $(LUA_FLAGS) $(TARGET_FLAGS) \
		$(DEPFLAGS) -c $< -o $@

build/m68k/lua-obj/interpreter.o: $(LUA)/lua.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(LUA_CPPFLAGS) $(LUA_FLAGS) $(TARGET_FLAGS) \
		$(DEPFLAGS) -Dmain=astra_lua_main -c $< -o $@

build/m68k/lua: lua/lua.c $(LUA_LIBRARY) build/m68k/lua-obj/interpreter.o \
		$(RUNTIME_LIB) $(STREAMS_LIB) $(VFS_LIB) $(POSIX_LIB) $(RUNTIME_CRT0) \
		$(RUNTIME)/astra_user.ld
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(LUA_CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) \
		$(DEPFLAGS) $(LINK_FLAGS) -o $@.elf $(RUNTIME_CRT0) $< \
		build/m68k/lua-obj/interpreter.o \
		-Wl,--start-group $(LUA_LIBRARY) $(VFS_LIB) $(STREAMS_LIB) \
		$(POSIX_LIB) $(RUNTIME_LIB) $(PICOLIBC_LIB) \
		$(PICOLIBC)/lib/libm.a -lgcc -Wl,--end-group
	$(OBJCOPY) --strip-all --remove-section=.astra_events $@.elf $@

VIM_PROGRAM := build/m68k/vim-program.o

$(VIM_PROGRAM): $(POSIX)/kit/program.c Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) \
		-DASTRA_POSIX_PROGRAM_NAME='"vim"' \
		-DASTRA_POSIX_PROGRAM_MAJOR=9 -DASTRA_POSIX_PROGRAM_MINOR=2 \
		-DASTRA_POSIX_PROGRAM_PATCH=1001 \
		-DASTRA_POSIX_PROGRAM_AUTHOR='"Vim contributors"' \
		-DASTRA_POSIX_PROGRAM_COPYRIGHT='"Copyright Vim contributors"' \
		-c $< -o $@

# Vim's generated Makefile does not name external static archives as target
# prerequisites. Relink its unchanged objects whenever Astra's shared POSIX
# implementation changes, instead of silently packaging a stale executable.
$(VIM_IMAGE): $(VIM_PROGRAM) $(RUNTIME_LIB) $(STREAMS_LIB) $(VFS_LIB) \
		$(POSIX_LIB) $(PICOLIBC_LIB) $(PICOLIBC_MATH) \
		$(VIM_TERMINFO)/lib/libtinfow.a
	$(MAKE) -C $(VIM_SOURCE) -W objects/main.o vim \
		LDFLAGS='-nostdlib -static -L$(VIM_TERMINFO)/lib \
		-Wl,-z,max-page-size=0x1000 -Wl,--build-id=none \
		-Wl,--gc-sections -T $(abspath $(RUNTIME)/astra_user.ld) \
		$(abspath $(RUNTIME_CRT0)) $(abspath $(VIM_PROGRAM)) \
		-Wl,--as-needed' \
		LIBS='-Wl,--start-group -ltinfow $(abspath $(VFS_LIB)) \
		$(abspath $(STREAMS_LIB)) $(abspath $(POSIX_LIB)) \
		$(abspath $(RUNTIME_LIB)) $(abspath $(PICOLIBC_LIB)) \
		$(abspath $(PICOLIBC_MATH)) -lgcc -Wl,--end-group'

build/m68k/vim: $(VIM_IMAGE)
	@mkdir -p $(@D)
	$(OBJCOPY) --strip-all --remove-section=.astra_events $< $@

build/m68k/%: $$*/$$*.c $(RUNTIME_LIB) $(STREAMS_LIB) $(VFS_LIB) \
		$(POSIX_LIB) $(RUNTIME_CRT0) $(RUNTIME)/astra_user.ld
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) \
		$(LINK_FLAGS) -o $@.elf $(RUNTIME_CRT0) $< \
		-Wl,--start-group $(VFS_LIB) $(STREAMS_LIB) $(POSIX_LIB) \
		$(RUNTIME_LIB) $(PICOLIBC_LIB) -lgcc -Wl,--end-group
	$(OBJCOPY) --strip-all --remove-section=.astra_events $@.elf $@

size: $(IMAGES)
	$(SIZE) $(IMAGES)

# Header dependencies: a stale object compiled against an older ABI
# constant is indistinguishable from a working one until it runs.
-include $(wildcard build/m68k/*.d build/m68k/lua-obj/*.d)

clean:
	rm -rf build

# Nothing to run on this host, and the targets have to exist so the userspace
# Makefile's loop does not stop here.
test sanitize:
	@:

analyze: $(ANALYZER_OBJECTS)

build/analyzer/lua-adapter.o: lua/lua.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer -c $< -o $@

build/analyzer/%.o: $$*/$$*.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer -c $< -o $@

.PHONY: all libraries size test sanitize analyze clean
