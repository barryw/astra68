include ../../posix/kit/astra-posix.mk
include ../../../../tools/astra-bundle.mk

# Chocolate Doom 3.1.1, built from the pinned upstream tree with its own
# configure and Makefiles, unmodified, and linked dynamically against the
# shared SDL, SDL_mixer, SDL_net, libc, runtime and compiler libraries.
# Nothing here patches Doom: what it needs from the platform is implemented
# in the platform (docs/CHOCOLATE_DOOM_PORT.md).

CHOCOLATE_DOOM_ROOT ?= ../../../../../chocolate-doom
SDL_PORT := ../../sdl2
VENDOR := build/vendor/chocolate-doom
OBJ := build/obj
STAMP := $(VENDOR)/.astra-source-stamp
CONFIGURED := $(OBJ)/.astra-configured
PROGRAM_OBJECT := build/m68k/program.o
DOOM := $(OBJ)/src/chocolate-doom
WAD_DIR := build/wad

DOOM_LIBRARIES := $(ASTRA_SDL2_LIBRARY) $(ASTRA_SDL2_MIXER_LIBRARY) \
	$(ASTRA_SDL2_NET_LIBRARY) $(ASTRA_LIBC_LIBRARY) \
	$(ASTRA_RUNTIME_LIBRARY) $(ASTRA_COMPILER_LIBRARY)
# Upstream repeats LDFLAGS inside LDADD (src/Makefile.am EXTRA_LIBS), and
# the linker accepts one -T and one crt0. Both ride in LIBS, which appears once,
# until the compiler driver applies Astra's dynamic link itself.
DOOM_LDFLAGS := $(filter-out -T $(ASTRA_LINKER_SCRIPT),$(ASTRA_POSIX_LDFLAGS))
DOOM_LINK := -T $(ASTRA_LINKER_SCRIPT) $(abspath $(ASTRA_POSIX_CRT0)) \
	$(abspath $(PROGRAM_OBJECT)) \
	$(foreach library,$(DOOM_LIBRARIES),-Wl,-l:$(notdir $(library)))
SDL_INCLUDE := $(abspath $(SDL_PORT)/build/vendor/SDL2/include)

.DEFAULT_GOAL := all
all: build/.apps

$(STAMP): prepare_source.py FORCE_SOURCE_CHECK
	python3 prepare_source.py $(CHOCOLATE_DOOM_ROOT) $(VENDOR)

# The SDL port owns SDL's headers and libraries.
sdl-libraries:
	$(MAKE) -C $(SDL_PORT) shared mixer-shared net-shared

$(PROGRAM_OBJECT): $(ASTRA_POSIX_PROGRAM_SOURCE) Makefile
	@mkdir -p $(@D)
	$(ASTRA_CC) $(ASTRA_POSIX_CPPFLAGS) $(ASTRA_POSIX_CFLAGS) \
		-DASTRA_POSIX_PROGRAM_NAME='"chocolate-doom"' \
		-DASTRA_POSIX_PROGRAM_MAJOR=3 -DASTRA_POSIX_PROGRAM_MINOR=1 \
		-DASTRA_POSIX_PROGRAM_PATCH=1 \
		-DASTRA_POSIX_PROGRAM_AUTHOR='"Chocolate Doom contributors"' \
		-DASTRA_POSIX_PROGRAM_COPYRIGHT='"Copyright Chocolate Doom contributors"' \
		-c $< -o $@

# Upstream's own build: autoreconf in the disposable copy, then configure
# out of tree. SDL comes in through configure's standard *_CFLAGS/*_LIBS
# overrides; math is in libc.library, so -lm is not sought.
$(CONFIGURED): $(STAMP) $(PROGRAM_OBJECT) Makefile | sdl-libraries
	cd $(VENDOR) && autoreconf -fi >/dev/null
	rm -rf $(OBJ)
	mkdir -p $(OBJ)
	cd $(OBJ) && \
		CC='$(ASTRA_CC)' \
		CPPFLAGS='$(ASTRA_POSIX_CPPFLAGS)' \
		CFLAGS='$(ASTRA_POSIX_CFLAGS)' \
		LDFLAGS='$(DOOM_LDFLAGS)' \
		LIBS='$(DOOM_LINK)' \
		SDL_CFLAGS='-I$(SDL_INCLUDE)' SDL_LIBS=' ' \
		SDLMIXER_CFLAGS='-I$(abspath $(SDL_PORT)/build/vendor/SDL2_mixer/include)' \
		SDLMIXER_LIBS=' ' \
		SDLNET_CFLAGS='-I$(abspath $(SDL_PORT)/build/vendor/SDL2_net/include)' \
		SDLNET_LIBS=' ' \
		ac_cv_lib_m_log=no \
		$(abspath $(VENDOR))/configure --host=m68k-unknown-elf \
		--cache-file=/dev/null --without-libsamplerate --without-libpng \
		--without-fluidsynth --disable-doc --disable-fonts --disable-icons \
		--disable-bash-completion
	@touch $@

$(DOOM): $(CONFIGURED) FORCE_SOURCE_CHECK
	$(MAKE) -C $(OBJ)/textscreen libtextscreen.a
	$(MAKE) -C $(OBJ)/opl libopl.a
	$(MAKE) -C $(OBJ)/pcsound libpcsound.a
	$(MAKE) -C $(OBJ)/src chocolate-doom
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(ASTRA_CROSS)readelf \
		$(foreach library,$(DOOM_LIBRARIES),--needed $(notdir $(library))) \
		$@

$(WAD_DIR)/doom1.wad: fetch_wad.py
	python3 fetch_wad.py $(WAD_DIR)

# fetch_wad.py writes the licence beside the WAD.
$(WAD_DIR)/doom1.wad.license.txt: $(WAD_DIR)/doom1.wad ;

# The icon is the game's own status-bar face, from the IWAD the bundle ships.
$(eval $(call astra_app_bundle,build,ChocolateDoom,$(DOOM),apps/ChocolateDoom.app/manifest,$(WAD_DIR)/doom1.wad,$(WAD_DIR)/doom1.wad $(WAD_DIR)/doom1.wad.license.txt $(CHOCOLATE_DOOM_ROOT)/COPYING.md,defaults/chocolate-doom.cfg defaults/default.cfg))
$(eval $(call astra_app_inventory,build,ChocolateDoom))

# The bundle build checks the executable's libraries and the bundle; the
# game itself is proved on the machine (emu/qemu/test-chocolate-doom.py).
test: all

clean:
	rm -rf build

.PHONY: all test clean sdl-libraries FORCE_SOURCE_CHECK
