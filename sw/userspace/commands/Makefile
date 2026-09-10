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
AR := $(CROSS)gcc-ar
RANLIB := $(CROSS)gcc-ranlib
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
RUNTIME := ../runtime
STREAMS := ../streams
VFS := ../vfs
POSIX := ../posix
NTP := ../ntp
NETWORK := ../network
NDK := ../../../ndk
LUA := ../../../third_party/lua/src
VIM_SOURCE ?= $(HOME)/vim-astra-v9.2.1001/src
VIM_TERMINFO_SOURCE ?= $(HOME)/ncurses-astra-6.6
VIM_TERMINFO ?= $(HOME)/ncurses-astra
VIM_TERMINFO_BUILD ?= $(HOME)/ncurses-astra-build
VIM_IMAGE := $(VIM_SOURCE)/vim
ZSH_SOURCE ?= $(HOME)/zsh-astra-v5.9.2
ZSH_BUILD ?= $(HOME)/zsh-astra-build
ZSH_IMAGE := $(ZSH_BUILD)/Src/zsh
ZSH_CONFIG_SITE := $(CURDIR)/zsh/config.site
# picolibc, built out of tree: see third_party/picolibc/ASTRA_VENDOR.md.
PICOLIBC ?= $(HOME)/picolibc-astra
CPPFLAGS := -I../../include -I$(RUNTIME)/include -I$(STREAMS)/include \
	-I$(VFS)/include -I$(POSIX)/include -I$(NDK)/include \
	-I$(NTP)/include \
	-I$(NETWORK)/include \
	-isystem $(PICOLIBC)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -O3 -flto -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_DEBUG_FLAGS)
LINK_FLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections \
	-Wl,--undefined=astra_posix_entry_contract -T $(RUNTIME)/astra_user.ld
VENDOR_INCLUDE_FLAGS := -I$(abspath ../../include) \
	-I$(abspath $(RUNTIME)/include) -I$(abspath $(STREAMS)/include) \
	-I$(abspath $(VFS)/include) -I$(abspath $(POSIX)/include) \
	-I$(abspath $(NDK)/include) -I$(VIM_TERMINFO)/include \
	-I$(VIM_TERMINFO)/include/ncursesw \
	-isystem $(PICOLIBC)/include \
	-isystem $(shell $(CC) -print-file-name=include) \
	-D_GNU_SOURCE -D_DEFAULT_SOURCE -DNDEBUG
# Ncurses deliberately exposes an opaque public TERMINAL while compiling a
# larger internal definition.  Cross-unit LTO treats those as incompatible;
# keep LTO on the applications, but not on this ABI boundary.
VIM_TERMINFO_CFLAGS := $(filter-out -flto,$(TARGET_FLAGS))

RUNTIME_LIB := $(RUNTIME)/build/m68k/libastrart.a
STREAMS_LIB := $(STREAMS)/build/m68k/libastrastreams.a
VFS_LIB := $(VFS)/build/m68k/libastravfs.a
POSIX_LIB := $(POSIX)/build/m68k/libastraposix.a
NTP_LIB := $(NTP)/build/m68k/libastrantp.a
NETWORK_LIB := $(NETWORK)/build/m68k/libastranetwork.a
PICOLIBC_LIB := $(PICOLIBC)/lib/libc.a
PICOLIBC_MATH := $(PICOLIBC)/lib/libm.a
RUNTIME_CRT0 := $(RUNTIME)/build/m68k/crt0-hosted.o

COMMANDS := status events devices which hello ls ps metrics cat mkdir rm posix \
            heapbench fsstress date echo ntp ping lua cxx vim zsh
SOURCE_COMMANDS := $(filter-out lua cxx vim zsh,$(COMMANDS))
ANALYZER_OBJECTS := $(patsubst %,build/analyzer/%.o,$(SOURCE_COMMANDS)) \
	build/analyzer/lua-adapter.o
# What the gate installs on the volume: stripped, the way the ROM's own image
# is, because a command carries no more DWARF onto a card than the supervisor
# carries into ROM. The unstripped ELF stays beside it for a debugger.
IMAGES := $(patsubst %,build/m68k/%,$(COMMANDS))
COMMAND_MANIFEST := build/m68k/.commands
ASTRA_PROGRAM_OWNER_DIRS := $(RUNTIME) $(STREAMS) $(VFS) $(POSIX) $(NTP) \
	$(NETWORK)
ASTRA_PROGRAM_PREPARE_TARGETS := cxx-runtime
ASTRA_PROGRAM_TARGETS := $(IMAGES) $(COMMAND_MANIFEST) size
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
	$(ASTRA_REPLACE_ARCHIVE)

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

CXX_IMAGE := $(POSIX)/build/m68k/cxx-runtime-test.elf

cxx-runtime:
	$(MAKE) -C $(POSIX) build/m68k/cxx-runtime-test.elf

build/m68k/cxx: $(CXX_IMAGE) $(POSIX)/tests/cxx_runtime.cpp \
		$(POSIX)/kit/program.c \
		$(POSIX_LIB) $(RUNTIME_LIB) $(STREAMS_LIB) $(VFS_LIB) \
		$(PICOLIBC_LIB) $(PICOLIBC_MATH) $(RUNTIME_CRT0) \
		$(RUNTIME)/astra_user.ld $(ASTRA_TOOLCHAIN_STAMP)
	$(OBJCOPY) --strip-all --remove-section=.astra_events $(CXX_IMAGE) $@

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
VIM_TERMINFO_LDFLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none -Wl,--gc-sections \
	-Wl,--undefined=astra_posix_entry_contract \
	-T $(abspath $(RUNTIME)/astra_user.ld) $(abspath $(RUNTIME_CRT0)) \
	$(abspath $(VIM_PROGRAM))
VIM_TERMINFO_LIBS := -Wl,--start-group $(abspath $(VFS_LIB)) \
	$(abspath $(STREAMS_LIB)) $(abspath $(POSIX_LIB)) \
	$(abspath $(RUNTIME_LIB)) $(abspath $(PICOLIBC_LIB)) \
	$(abspath $(PICOLIBC_MATH)) -lgcc -Wl,--end-group
VIM_LIBS := -Wl,--start-group -ltinfow $(abspath $(VFS_LIB)) \
	$(abspath $(STREAMS_LIB)) $(abspath $(POSIX_LIB)) \
	$(abspath $(RUNTIME_LIB)) $(abspath $(PICOLIBC_LIB)) \
	$(abspath $(PICOLIBC_MATH)) -lgcc -Wl,--end-group

$(VIM_PROGRAM): $(POSIX)/kit/program.c $(VIM_PROGRAM_CONFIG)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) \
		$(VIM_PROGRAM_DEFINES) \
		-c $< -o $@

# Ncurses' generated dependencies do not include installed system headers.
# Rebuild its archive when the public terminal ABI changes; otherwise an old
# four-byte winsize object accepts an eight-byte TIOCGWINSZ write.
$(VIM_TERMINFO)/lib/libtinfow.a: $(PICOLIBC)/include/sys/termios.h \
		$(PICOLIBC)/include/sys/ioctl.h $(POSIX_LIB) \
		$(RUNTIME_LIB) $(STREAMS_LIB) $(VFS_LIB) $(RUNTIME_CRT0) \
		$(ASTRA_TOOLCHAIN_STAMP) | $(VIM_PROGRAM)
	rm -rf $(VIM_TERMINFO_BUILD)
	mkdir -p $(VIM_TERMINFO_BUILD)
	cd $(VIM_TERMINFO_BUILD) && \
		CC='$(CC)' AR='$(AR)' RANLIB='$(RANLIB)' \
		CFLAGS='$(VIM_TERMINFO_CFLAGS)' \
		CPPFLAGS='-nostdinc $(VENDOR_INCLUDE_FLAGS)' \
		LDFLAGS='$(VIM_TERMINFO_LDFLAGS)' \
		LIBS='$(VIM_TERMINFO_LIBS)' \
		$(VIM_TERMINFO_SOURCE)/configure \
		--build=x86_64-pc-linux-gnu --host=m68k-unknown-elf \
		--prefix=$(VIM_TERMINFO) \
		--with-default-terminfo-dir=/LIBS/terminfo \
		--with-terminfo-dirs=/LIBS/terminfo --with-termlib \
		--enable-widec --without-shared --with-normal --without-cxx \
		--without-cxx-binding --without-ada --without-progs \
		--without-tests --without-manpages --without-debug \
		--without-profile --disable-db-install --disable-home-terminfo
	$(MAKE) -j1 -C $(VIM_TERMINFO_BUILD) install.libs

# Vim's generated Makefile does not name external static archives as target
# prerequisites. Relink its unchanged objects whenever Astra's shared POSIX
# implementation changes, instead of silently packaging a stale executable.
$(VIM_BUILD_CONFIG): $(RUNTIME_LIB) $(STREAMS_LIB) $(VFS_LIB) $(POSIX_LIB) \
		$(PICOLIBC_LIB) $(PICOLIBC_MATH) $(VIM_TERMINFO)/lib/libtinfow.a \
		$(ASTRA_TOOLCHAIN_STAMP)
	$(MAKE) -C $(VIM_SOURCE) clean
	@mkdir -p $(@D)
	@rm -f build/.vendor-config/vim-*
	@touch $@

$(VIM_IMAGE): Makefile $(VIM_BUILD_CONFIG) $(VIM_PROGRAM) \
		$(RUNTIME_LIB) $(STREAMS_LIB) $(VFS_LIB) \
		$(POSIX_LIB) $(PICOLIBC_LIB) $(PICOLIBC_MATH) \
		$(VIM_TERMINFO)/lib/libtinfow.a $(ASTRA_TOOLCHAIN_STAMP)
	@rm -f $(VIM_IMAGE)
	$(MAKE) -C $(VIM_SOURCE) vim \
		CC='$(CC)' \
		CPPFLAGS='$(VENDOR_INCLUDE_FLAGS)' \
		CFLAGS='$(TARGET_FLAGS) -D_REENTRANT \
		-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=1' \
		LDFLAGS='-nostdlib -static -L$(VIM_TERMINFO)/lib \
		-Wl,-z,max-page-size=0x1000 -Wl,--build-id=none \
		-Wl,--gc-sections -Wl,--undefined=astra_posix_entry_contract \
		-T $(abspath $(RUNTIME)/astra_user.ld) \
		$(abspath $(RUNTIME_CRT0)) $(abspath $(VIM_PROGRAM)) \
		-Wl,--as-needed' \
		LIBS='$(VIM_LIBS)'

build/m68k/vim: $(VIM_IMAGE)
	@mkdir -p $(@D)
	$(OBJCOPY) --strip-all --remove-section=.astra_events $< $@

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
ZSH_LDFLAGS := $(abspath $(RUNTIME_CRT0)) -nostdlib -static \
	-Wl,-z,max-page-size=0x1000 -Wl,--build-id=none -Wl,--gc-sections \
	-Wl,--undefined=astra_posix_entry_contract \
	-T $(abspath $(RUNTIME)/astra_user.ld)
ZSH_LIBS := $(abspath $(ZSH_PROGRAM)) -Wl,--start-group \
	$(abspath $(VIM_TERMINFO)/lib/libtinfow.a) $(abspath $(POSIX_LIB)) \
	$(abspath $(STREAMS_LIB)) $(abspath $(VFS_LIB)) \
	$(abspath $(RUNTIME_LIB)) $(abspath $(PICOLIBC_LIB)) \
	$(abspath $(PICOLIBC_MATH)) -lgcc -Wl,--end-group

$(ZSH_PROGRAM): $(POSIX)/kit/program.c $(ZSH_PROGRAM_CONFIG)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) \
		$(ZSH_PROGRAM_DEFINES) \
		-c $< -o $@

build/.program-config/%:
	@mkdir -p $(@D)
	@touch $@

$(ZSH_BUILD_CONFIG): $(ZSH_CONFIG_SITE) $(ZSH_SOURCE)/configure \
		$(POSIX_LIB) $(RUNTIME_LIB) \
		$(STREAMS_LIB) $(VFS_LIB) $(PICOLIBC_LIB) $(PICOLIBC_MATH) \
		$(VIM_TERMINFO)/lib/libtinfow.a $(ASTRA_TOOLCHAIN_STAMP)
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

$(ZSH_IMAGE): $(ZSH_SOURCES) $(ZSH_BUILD_CONFIG) $(ZSH_PROGRAM)
	@rm -f $(ZSH_IMAGE)
	$(MAKE) -C $(ZSH_BUILD)

build/m68k/zsh: $(ZSH_IMAGE)
	@mkdir -p $(@D)
	$(OBJCOPY) --strip-all --remove-section=.astra_events $< $@

$(patsubst %,build/m68k/%,$(SOURCE_COMMANDS)): build/m68k/%: \
		$$*/$$*.c $(RUNTIME_LIB) $(STREAMS_LIB) $(VFS_LIB) \
		$(POSIX_LIB) $(NTP_LIB) $(NETWORK_LIB) $(RUNTIME_CRT0) \
		$(RUNTIME)/astra_user.ld
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) $(DEPFLAGS) \
		$(LINK_FLAGS) -o $@.elf $(RUNTIME_CRT0) $< \
		-Wl,--start-group $(NTP_LIB) $(NETWORK_LIB) $(VFS_LIB) \
		$(STREAMS_LIB) $(POSIX_LIB) \
		$(RUNTIME_LIB) $(PICOLIBC_LIB) -lgcc -Wl,--end-group
	$(OBJCOPY) --strip-all --remove-section=.astra_events $@.elf $@

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

.PHONY: all libraries cxx-runtime size test sanitize analyze clean
