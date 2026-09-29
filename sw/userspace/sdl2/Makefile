include ../posix/kit/astra-posix.mk
include ../../../mk/astra-shared-library.mk
include ../../../tools/astra-bundle.mk

SDL2_ROOT ?= ../../../../SDL2
SDL_NET_ROOT ?= ../../../../SDL2_net
SDL_IMAGE_ROOT ?= ../../../../SDL2_image
SDL_MIXER_ROOT ?= ../../../../SDL2_mixer
HOST_CC ?= cc
TARGET_CC ?= $(ASTRA_CC)
TARGET_AR ?= $(ASTRA_CROSS)ar
TARGET_RANLIB ?= $(ASTRA_CROSS)ranlib
SDL2_INCLUDES := -I$(SDL2_ROOT)/include -I$(SDL2_ROOT)/src \
	-I$(SDL2_ROOT)/src/audio
ASTRA_INCLUDES := -I../../../ndk/include -I../../include
HOST_TEST := build/host/test_astraaudio
HOST_TEST_VIDEO := build/host/test_astravideo
HOST_TEST_RENDER := build/host/test_astrarender
TARGET_AUDIO := build/m68k/SDL_astraaudio.o
TARGET_REGISTRY := build/m68k/SDL_audio.o
TARGET_VIDEO := build/m68k/SDL_astravideo.o
TARGET_RENDER := build/m68k/SDL_astrarender.o
TARGET_VIDEO_REGISTRY := build/m68k/SDL_video.o
SDL2_SHARED := build/m68k/libraries/SDL2.library.2
SDL2_IDENTITY := build/m68k/library_identity.o
SDL2_EXPORTS := build/m68k/SDL2.library.exports
SDL2_UNDEFINED := build/m68k/SDL2.library.undefined
SDL2_ARCHIVE := build/vendor/SDL2/libSDL2.a
SDL2_UPSTREAM_EXPORTS := build/vendor/SDL2/src/dynapi/SDL2.exports
SDL_NET_STAMP := build/vendor/SDL2_net/.astra-source-stamp
SDL_NET_SOURCES := SDLnet.c SDLnetTCP.c SDLnetUDP.c SDLnetselect.c
SDL_NET_OBJECTS := $(patsubst %.c,build/m68k/net/%.o,$(SDL_NET_SOURCES))
SDL_NET_IDENTITY := build/m68k/library_identity_net.o
SDL_NET_EXPORTS := build/m68k/SDL2_net.library.exports
SDL_NET_UNDEFINED := build/m68k/SDL2_net.library.undefined
SDL_NET_SHARED := build/m68k/libraries/SDL2_net.library.2
SDL_IMAGE_STAMP := build/vendor/SDL2_image/.astra-source-stamp
SDL_IMAGE_SOURCES := IMG.c IMG_avif.c IMG_bmp.c IMG_gif.c IMG_jpg.c \
	IMG_jxl.c IMG_lbm.c IMG_pcx.c IMG_png.c IMG_pnm.c IMG_qoi.c \
	IMG_stb.c IMG_svg.c IMG_tga.c IMG_tif.c IMG_webp.c IMG_xcf.c \
	IMG_xpm.c IMG_xv.c
SDL_IMAGE_OBJECTS := $(patsubst %.c,build/m68k/image/%.o,$(SDL_IMAGE_SOURCES))
SDL_IMAGE_IDENTITY := build/m68k/library_identity_image.o
SDL_IMAGE_EXPORTS := build/m68k/SDL2_image.library.exports
SDL_IMAGE_UNDEFINED := build/m68k/SDL2_image.library.undefined
SDL_IMAGE_SHARED := build/m68k/libraries/SDL2_image.library.2
SDL_MIXER_STAMP := build/vendor/SDL2_mixer/.astra-source-stamp
SDL_MIXER_SOURCES := src/codecs/load_aiff.c src/codecs/load_voc.c \
	src/codecs/mp3utils.c src/codecs/music_cmd.c \
	src/codecs/music_drflac.c src/codecs/music_flac.c \
	src/codecs/music_fluidsynth.c src/codecs/music_gme.c \
	src/codecs/music_minimp3.c src/codecs/music_modplug.c \
	src/codecs/music_mpg123.c src/codecs/music_nativemidi.c \
	src/codecs/music_ogg.c src/codecs/music_ogg_stb.c \
	src/codecs/music_opus.c src/codecs/music_timidity.c \
	src/codecs/music_wav.c src/codecs/music_wavpack.c \
	src/codecs/music_xmp.c src/codecs/remap_channels.c \
	src/effect_position.c src/effect_stereoreverse.c \
	src/effects_internal.c src/mixer.c src/music.c src/utils.c
SDL_MIXER_OBJECTS := $(patsubst %.c,build/m68k/mixer/%.o,$(SDL_MIXER_SOURCES))
SDL_MIXER_IDENTITY := build/m68k/library_identity_mixer.o
SDL_MIXER_EXPORTS := build/m68k/SDL2_mixer.library.exports
SDL_MIXER_UNDEFINED := build/m68k/SDL2_mixer.library.undefined
SDL_MIXER_SHARED := build/m68k/libraries/SDL2_mixer.library.2
SDL_TTF_ROOT ?= ../../../../SDL2_ttf
SDL_TTF_STAMP := build/vendor/SDL2_ttf/.astra-source-stamp
SDL_TTF_OBJECT := build/m68k/ttf/SDL_ttf.o
SDL_TTF_IDENTITY := build/m68k/library_identity_ttf.o
SDL_TTF_EXPORTS := build/m68k/SDL2_ttf.library.exports
SDL_TTF_UNDEFINED := build/m68k/SDL2_ttf.library.undefined
SDL_TTF_SHARED := build/m68k/libraries/SDL2_ttf.library.2
FREETYPE_BUILD := build/vendor/SDL2_ttf/build/freetype
FREETYPE_ARCHIVE := $(FREETYPE_BUILD)/libfreetype.a
# The disposable vendor tree has its own content/toolchain stamp and Makefile.
# Root build-policy prerequisites cannot remake its generated source/archive.
$(SDL2_ARCHIVE) $(SDL2_UPSTREAM_EXPORTS): .EXTRA_PREREQS :=

prepare:
	python3 prepare_source.py $(SDL2_ROOT)

prepare-net:
	python3 prepare_addon_source.py SDL2_net $(SDL_NET_ROOT)

prepare-image:
	python3 prepare_addon_source.py SDL2_image $(SDL_IMAGE_ROOT)

prepare-mixer:
	python3 prepare_addon_source.py SDL2_mixer $(SDL_MIXER_ROOT)

prepare-ttf:
	python3 prepare_addon_source.py SDL2_ttf $(SDL_TTF_ROOT)

$(SDL_NET_STAMP): prepare prepare-net
	@test -f $@

$(SDL_IMAGE_STAMP): prepare prepare-image
	@test -f $@

$(SDL_MIXER_STAMP): prepare prepare-mixer
	@test -f $@

$(SDL_TTF_STAMP): prepare prepare-ttf
	@test -f $@

build/m68k/net/%.o: $(SDL_NET_STAMP) Makefile
	@mkdir -p $(@D)
	$(TARGET_CC) $(ASTRA_POSIX_CPPFLAGS) \
		-Ibuild/vendor/SDL2/include -Ibuild/vendor/SDL2_net/include \
		-D_DEFAULT_SOURCE=1 -std=c11 -Wall -Wextra -Werror \
		$(ASTRA_POSIX_CFLAGS) -c build/vendor/SDL2_net/src/$*.c -o $@

# SDL programs the system image installs as applications (build/.apps).
# Probes are built on demand by their *-bundle targets and never shipped.
# Upstream demos run unchanged; testutils.c finds their files in the
# bundle's resources through SDL_GetBasePath.
SDL_DEMOS := testsprite2 testgeometry testrendertarget testscale
SHIPPED_APPS := SDLTestDraw2 SDLFrameBench SDLTestSprite2 SDLTestGeometry \
	SDLTestRenderTarget SDLTestScale

all: library-contract net-library-contract image-library-contract \
	mixer-library-contract ttf-library-contract build/.apps

core: prepare
	$(MAKE) -C build/vendor/SDL2 -f Makefile.minimal \
		CC=$(TARGET_CC) AR=$(TARGET_AR) RANLIB=$(TARGET_RANLIB) \
		CFLAGS='$(ASTRA_POSIX_CFLAGS)' \
		CPPFLAGS='-I./include -I./src -I./src/audio \
		-DSDL_AUDIO_DRIVER_ASTRA=1 -DDYNAPI_NEEDS_DLOPEN=1 \
		-DSDL_VIDEO_DRIVER_ASTRA=1 -DSDL_VIDEO_RENDER_ASTRA=1 \
		-DHAVE_MALLOC=1 -DHAVE_STDLIB_H=1 \
		-I$(abspath ../posix/include) -I$(abspath ../../../ndk/include) \
		-I$(abspath ../../include)'

$(SDL2_ARCHIVE): core
	@test -f $@

$(SDL2_UPSTREAM_EXPORTS): $(SDL2_ARCHIVE)
	@test -f $@

shared: library-contract

net-shared: net-library-contract

$(SDL2_IDENTITY): library_identity.c Makefile
	@mkdir -p $(@D)
	$(TARGET_CC) $(ASTRA_POSIX_CPPFLAGS) $(ASTRA_POSIX_CFLAGS) \
		-c $< -o $@

$(SDL2_EXPORTS) $(SDL2_UNDEFINED) &: $(SDL2_ARCHIVE) \
		$(SDL2_UPSTREAM_EXPORTS) generate_exports.py Makefile
	python3 generate_exports.py --readelf $(ASTRA_READELF) \
		--upstream $(SDL2_UPSTREAM_EXPORTS) --archive $(SDL2_ARCHIVE) \
		--exports-out $(SDL2_EXPORTS) \
		--undefined-out $(SDL2_UNDEFINED)

$(SDL2_SHARED): $(SDL2_IDENTITY) $(SDL2_ARCHIVE) \
		$(SDL2_EXPORTS) $(SDL2_UNDEFINED) $(ASTRA_SHARED_LINK_INPUTS) \
		$(ASTRA_PCM_LIBRARY) $(ASTRA_GRAPHICS_LIBRARY) \
		$(ASTRA_SYSTEM_LIBRARY) $(ASTRA_LIBC_LIBRARY)
	@mkdir -p $(@D)
	$(TARGET_CC) $(ASTRA_ABI_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,SDL2.library.2 -Wl,--version-script=$(SDL2_EXPORTS) \
		-Wl,@$(SDL2_UNDEFINED) -o $@ $(SDL2_IDENTITY) \
		$(ASTRA_SHARED_LIBRARY_SEARCH_FLAGS) -Wl,--no-as-needed \
		-Wl,-l:pcm.library.2 -Wl,-l:graphics.library.2 \
		-Wl,-l:system.library.2 -Wl,-l:libc.library.2 \
		-Wl,-l:runtime.library.1 -Wl,-l:compiler.library.1 \
		$(SDL2_ARCHIVE) $(ASTRA_SHARED_LINK_END)

library-contract: $(SDL2_SHARED)
	python3 ../../../tools/check_shared_library_layout.py $(SDL2_SHARED)
	python3 ../../../tools/check_elf_exports.py \
		--readelf $(ASTRA_READELF) $(SDL2_EXPORTS) $(SDL2_SHARED)
	@$(ASTRA_READELF) -dW $(SDL2_SHARED) | grep -Fq \
		'Library soname: [SDL2.library.2]'
	@echo 'Astra SDL2 shared library: PASS'

$(SDL_NET_IDENTITY): library_identity_net.c Makefile
	@mkdir -p $(@D)
	$(TARGET_CC) $(ASTRA_POSIX_CPPFLAGS) $(ASTRA_POSIX_CFLAGS) \
		-c $< -o $@

$(SDL_NET_EXPORTS) $(SDL_NET_UNDEFINED) &: $(SDL_NET_OBJECTS) \
		$(SDL_NET_STAMP) generate_addon_exports.py Makefile
	python3 generate_addon_exports.py --readelf $(ASTRA_READELF) \
		--header build/vendor/SDL2_net/include/SDL_net.h \
		--prefix SDLNet_ --version ASTRA_SDL2_NET_2_0 \
		--required SDLNet_Init --required SDLNet_TCP_Open \
		--required SDLNet_UDP_Open --required SDLNet_GetLocalAddresses \
		--exports-out $(SDL_NET_EXPORTS) \
		--undefined-out $(SDL_NET_UNDEFINED) $(SDL_NET_OBJECTS)

$(SDL_NET_SHARED): $(SDL_NET_IDENTITY) $(SDL_NET_OBJECTS) \
		$(SDL_NET_EXPORTS) $(SDL_NET_UNDEFINED) $(SDL2_SHARED) \
		$(ASTRA_SHARED_LINK_INPUTS) $(ASTRA_LIBC_LIBRARY)
	@mkdir -p $(@D)
	$(TARGET_CC) $(ASTRA_ABI_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,SDL2_net.library.2 \
		-Wl,--version-script=$(SDL_NET_EXPORTS) \
		-Wl,@$(SDL_NET_UNDEFINED) -o $@ \
		$(SDL_NET_IDENTITY) $(SDL_NET_OBJECTS) \
		$(ASTRA_SHARED_LIBRARY_SEARCH_FLAGS) -Wl,--no-as-needed \
		-Wl,-l:SDL2.library.2 -Wl,-l:libc.library.2 \
		-Wl,-l:runtime.library.1 -Wl,-l:compiler.library.1 \
		$(ASTRA_SHARED_LINK_END)

net-library-contract: $(SDL_NET_SHARED)
	python3 ../../../tools/check_shared_library_layout.py $(SDL_NET_SHARED)
	python3 ../../../tools/check_elf_exports.py \
		--readelf $(ASTRA_READELF) $(SDL_NET_EXPORTS) $(SDL_NET_SHARED)
	@$(ASTRA_READELF) -dW $(SDL_NET_SHARED) | grep -Fq \
		'Library soname: [SDL2_net.library.2]'
	@echo 'Astra SDL_net shared library: PASS'

build/m68k/image/%.o: $(SDL_IMAGE_STAMP) Makefile
	@mkdir -p $(@D)
	$(TARGET_CC) $(ASTRA_POSIX_CPPFLAGS) \
		-Ibuild/vendor/SDL2/include -Ibuild/vendor/SDL2_image/include \
		-DBUILD_SDL=1 -DUSE_STBIMAGE=1 \
		-DLOAD_BMP=1 -DLOAD_GIF=1 -DLOAD_JPG=1 \
		-DLOAD_LBM=1 -DLOAD_PCX=1 -DLOAD_PNG=1 \
		-DLOAD_PNM=1 -DLOAD_QOI=1 -DLOAD_SVG=1 \
		-DLOAD_TGA=1 -DLOAD_XCF=1 -DLOAD_XPM=1 -DLOAD_XV=1 \
		-DSDL_IMAGE_SAVE_JPG=1 -DSDL_IMAGE_SAVE_PNG=1 \
		-std=c11 -Wall -Wextra $(ASTRA_POSIX_CFLAGS) \
		-c build/vendor/SDL2_image/src/$*.c -o $@

$(SDL_IMAGE_IDENTITY): library_identity_image.c Makefile
	@mkdir -p $(@D)
	$(TARGET_CC) $(ASTRA_POSIX_CPPFLAGS) $(ASTRA_POSIX_CFLAGS) \
		-c $< -o $@

$(SDL_IMAGE_EXPORTS) $(SDL_IMAGE_UNDEFINED) &: $(SDL_IMAGE_OBJECTS) \
		$(SDL_IMAGE_STAMP) generate_addon_exports.py Makefile
	python3 generate_addon_exports.py --readelf $(ASTRA_READELF) \
		--header build/vendor/SDL2_image/include/SDL_image.h \
		--prefix IMG_ --version ASTRA_SDL2_IMAGE_2_0 \
		--required IMG_Init --required IMG_Load --required IMG_Load_RW \
		--required IMG_LoadPNG_RW --required IMG_LoadJPG_RW \
		--exports-out $(SDL_IMAGE_EXPORTS) \
		--undefined-out $(SDL_IMAGE_UNDEFINED) $(SDL_IMAGE_OBJECTS)

$(SDL_IMAGE_SHARED): $(SDL_IMAGE_IDENTITY) $(SDL_IMAGE_OBJECTS) \
		$(SDL_IMAGE_EXPORTS) $(SDL_IMAGE_UNDEFINED) $(SDL2_SHARED) \
		$(ASTRA_SHARED_LINK_INPUTS) $(ASTRA_LIBC_LIBRARY)
	@mkdir -p $(@D)
	$(TARGET_CC) $(ASTRA_ABI_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,SDL2_image.library.2 \
		-Wl,--version-script=$(SDL_IMAGE_EXPORTS) \
		-Wl,@$(SDL_IMAGE_UNDEFINED) -o $@ \
		$(SDL_IMAGE_IDENTITY) $(SDL_IMAGE_OBJECTS) \
		$(ASTRA_SHARED_LIBRARY_SEARCH_FLAGS) -Wl,--no-as-needed \
		-Wl,-l:SDL2.library.2 -Wl,-l:libc.library.2 \
		-Wl,-l:runtime.library.1 -Wl,-l:compiler.library.1 \
		$(ASTRA_SHARED_LINK_END)

image-library-contract: $(SDL_IMAGE_SHARED)
	python3 ../../../tools/check_shared_library_layout.py $(SDL_IMAGE_SHARED)
	python3 ../../../tools/check_elf_exports.py \
		--readelf $(ASTRA_READELF) $(SDL_IMAGE_EXPORTS) $(SDL_IMAGE_SHARED)
	@$(ASTRA_READELF) -dW $(SDL_IMAGE_SHARED) | grep -Fq \
		'Library soname: [SDL2_image.library.2]'
	@echo 'Astra SDL_image shared library: PASS'

image-shared: image-library-contract

build/m68k/mixer/%.o: $(SDL_MIXER_STAMP) Makefile
	@mkdir -p $(@D)
	$(TARGET_CC) $(ASTRA_POSIX_CPPFLAGS) \
		-Ibuild/vendor/SDL2/include -Ibuild/vendor/SDL2_mixer/include \
		-Ibuild/vendor/SDL2_mixer/src \
		-Ibuild/vendor/SDL2_mixer/src/codecs \
		-DBUILD_SDL=1 -DMUSIC_WAV=1 -DMUSIC_OGG=1 -DOGG_USE_STB=1 \
		-DMUSIC_MP3_MINIMP3=1 -DMUSIC_FLAC_DRFLAC=1 \
		-std=c11 -Wall -Wextra $(ASTRA_POSIX_CFLAGS) \
		-c build/vendor/SDL2_mixer/$*.c -o $@

$(SDL_MIXER_IDENTITY): library_identity_mixer.c Makefile
	@mkdir -p $(@D)
	$(TARGET_CC) $(ASTRA_POSIX_CPPFLAGS) $(ASTRA_POSIX_CFLAGS) \
		-c $< -o $@

$(SDL_MIXER_EXPORTS) $(SDL_MIXER_UNDEFINED) &: $(SDL_MIXER_OBJECTS) \
		$(SDL_MIXER_STAMP) generate_addon_exports.py Makefile
	python3 generate_addon_exports.py --readelf $(ASTRA_READELF) \
		--header build/vendor/SDL2_mixer/include/SDL_mixer.h \
		--prefix Mix_ --version ASTRA_SDL2_MIXER_2_0 \
		--required Mix_Init --required Mix_OpenAudio --required Mix_LoadWAV_RW \
		--required Mix_PlayChannelTimed --required Mix_LoadMUS \
		--exports-out $(SDL_MIXER_EXPORTS) \
		--undefined-out $(SDL_MIXER_UNDEFINED) $(SDL_MIXER_OBJECTS)

$(SDL_MIXER_SHARED): $(SDL_MIXER_IDENTITY) $(SDL_MIXER_OBJECTS) \
		$(SDL_MIXER_EXPORTS) $(SDL_MIXER_UNDEFINED) $(SDL2_SHARED) \
		$(ASTRA_SHARED_LINK_INPUTS) $(ASTRA_LIBC_LIBRARY)
	@mkdir -p $(@D)
	$(TARGET_CC) $(ASTRA_ABI_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,SDL2_mixer.library.2 \
		-Wl,--version-script=$(SDL_MIXER_EXPORTS) \
		-Wl,@$(SDL_MIXER_UNDEFINED) -o $@ \
		$(SDL_MIXER_IDENTITY) $(SDL_MIXER_OBJECTS) \
		$(ASTRA_SHARED_LIBRARY_SEARCH_FLAGS) -Wl,--no-as-needed \
		-Wl,-l:SDL2.library.2 -Wl,-l:libc.library.2 \
		-Wl,-l:runtime.library.1 -Wl,-l:compiler.library.1 \
		$(ASTRA_SHARED_LINK_END)

mixer-library-contract: $(SDL_MIXER_SHARED)
	python3 ../../../tools/check_shared_library_layout.py $(SDL_MIXER_SHARED)
	python3 ../../../tools/check_elf_exports.py \
		--readelf $(ASTRA_READELF) $(SDL_MIXER_EXPORTS) $(SDL_MIXER_SHARED)
	@$(ASTRA_READELF) -dW $(SDL_MIXER_SHARED) | grep -Fq \
		'Library soname: [SDL2_mixer.library.2]'
	@echo 'Astra SDL_mixer shared library: PASS'

mixer-shared: mixer-library-contract

$(FREETYPE_ARCHIVE): $(SDL_TTF_STAMP) Makefile
	cmake -S build/vendor/SDL2_ttf/external/freetype -B $(FREETYPE_BUILD) \
		-DCMAKE_SYSTEM_NAME=Generic \
		-DCMAKE_C_COMPILER=$(TARGET_CC) \
		-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
		-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_FLAGS='$(ASTRA_ABI_FLAGS) -Os -ffreestanding -fno-builtin' \
		-DCMAKE_C_FLAGS_RELEASE='-Os -DNDEBUG' \
		-DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON \
		-DFT_DISABLE_PNG=ON -DFT_DISABLE_HARFBUZZ=ON \
		-DFT_DISABLE_BROTLI=ON -DBUILD_SHARED_LIBS=OFF \
		-DSKIP_INSTALL_ALL=ON
	cmake --build $(FREETYPE_BUILD) --parallel 4
	@test -s $@

$(SDL_TTF_OBJECT): $(SDL_TTF_STAMP) $(FREETYPE_ARCHIVE) Makefile
	@mkdir -p $(@D)
	$(TARGET_CC) $(ASTRA_POSIX_CPPFLAGS) \
		-Ibuild/vendor/SDL2/include -Ibuild/vendor/SDL2_ttf \
		-I$(FREETYPE_BUILD)/include \
		-Ibuild/vendor/SDL2_ttf/external/freetype/include \
		-DBUILD_SDL=1 -DSDL_BUILD_MAJOR_VERSION=2 \
		-DSDL_BUILD_MINOR_VERSION=24 -DSDL_BUILD_MICRO_VERSION=0 \
		-std=c11 -Wall -Wextra $(ASTRA_POSIX_CFLAGS) \
		-c build/vendor/SDL2_ttf/SDL_ttf.c -o $@

$(SDL_TTF_IDENTITY): library_identity_ttf.c Makefile
	@mkdir -p $(@D)
	$(TARGET_CC) $(ASTRA_POSIX_CPPFLAGS) $(ASTRA_POSIX_CFLAGS) \
		-c $< -o $@

$(SDL_TTF_EXPORTS) $(SDL_TTF_UNDEFINED) &: $(SDL_TTF_OBJECT) \
		$(SDL_TTF_STAMP) generate_addon_exports.py Makefile
	python3 generate_addon_exports.py --readelf $(ASTRA_READELF) \
		--header build/vendor/SDL2_ttf/SDL_ttf.h \
		--prefix TTF_ --version ASTRA_SDL2_TTF_2_0 \
		--required TTF_Init --required TTF_OpenFontRW \
		--required TTF_RenderUTF8_Blended \
		--exports-out $(SDL_TTF_EXPORTS) \
		--undefined-out $(SDL_TTF_UNDEFINED) $(SDL_TTF_OBJECT)

$(SDL_TTF_SHARED): $(SDL_TTF_IDENTITY) $(SDL_TTF_OBJECT) \
		$(FREETYPE_ARCHIVE) $(SDL_TTF_EXPORTS) $(SDL_TTF_UNDEFINED) \
		$(SDL2_SHARED) $(ASTRA_SHARED_LINK_INPUTS) $(ASTRA_LIBC_LIBRARY)
	@mkdir -p $(@D)
	$(TARGET_CC) $(ASTRA_ABI_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,SDL2_ttf.library.2 \
		-Wl,--version-script=$(SDL_TTF_EXPORTS) \
		-Wl,@$(SDL_TTF_UNDEFINED) -o $@ \
		$(SDL_TTF_IDENTITY) $(SDL_TTF_OBJECT) $(FREETYPE_ARCHIVE) \
		$(ASTRA_SHARED_LIBRARY_SEARCH_FLAGS) -Wl,--no-as-needed \
		-Wl,-l:SDL2.library.2 -Wl,-l:libc.library.2 \
		-Wl,-l:runtime.library.1 -Wl,-l:compiler.library.1 \
		$(ASTRA_SHARED_LINK_END)

ttf-library-contract: $(SDL_TTF_SHARED)
	python3 ../../../tools/check_shared_library_layout.py $(SDL_TTF_SHARED)
	python3 ../../../tools/check_elf_exports.py \
		--readelf $(ASTRA_READELF) $(SDL_TTF_EXPORTS) $(SDL_TTF_SHARED)
	@$(ASTRA_READELF) -dW $(SDL_TTF_SHARED) | grep -Fq \
		'Library soname: [SDL2_ttf.library.2]'
	@echo 'Astra SDL_ttf shared library: PASS'

ttf-shared: ttf-library-contract

build/m68k/sdlttfprobe: tests/ttf_probe.c $(SDL_TTF_SHARED) \
		$(SDL2_SHARED) $(ASTRA_LIBC_LIBRARY)
	@mkdir -p $(@D)
	$(TARGET_CC) $(ASTRA_POSIX_CPPFLAGS) \
		-Ibuild/vendor/SDL2/include -Ibuild/vendor/SDL2_ttf \
		-DASTRA_POSIX_PROGRAM_NAME='"sdlttfprobe"' \
		-DASTRA_POSIX_PROGRAM_MAJOR=1 -DASTRA_POSIX_PROGRAM_MINOR=0 \
		-DASTRA_POSIX_PROGRAM_PATCH=0 \
		-DASTRA_POSIX_PROGRAM_AUTHOR='"Astra project"' \
		-DASTRA_POSIX_PROGRAM_COPYRIGHT='"Copyright 2026 Astra project"' \
		$(ASTRA_POSIX_CFLAGS) $(ASTRA_POSIX_LDFLAGS) -o $@ \
		$(ASTRA_POSIX_CRT0) tests/ttf_probe.c \
		$(ASTRA_POSIX_PROGRAM_SOURCE) \
		-L$(dir $(SDL_TTF_SHARED)) \
		-Wl,-rpath-link,$(dir $(SDL_TTF_SHARED)) \
		-Wl,-l:SDL2_ttf.library.2 -Wl,-l:SDL2.library.2 \
		-Wl,-l:libc.library.2 -Wl,-l:runtime.library.1 \
		-Wl,-l:compiler.library.1
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(ASTRA_READELF) \
		--needed SDL2_ttf.library.2 --needed SDL2.library.2 \
		--needed libc.library.2 --needed runtime.library.1 \
		--needed compiler.library.1 $@

$(eval $(call astra_app_bundle,build,SDLTTFProbe,build/m68k/sdlttfprobe,tests/SDLTTFProbe.app/manifest,terminal,../graphics/fonts/atkinson-hyperlegible-next/AtkinsonHyperlegibleNext-Regular.ttf))
ttf-probe-bundle: build/.SDLTTFProbe.app.stamp

build/m68k/sdlmixerprobe: tests/mixer_probe.c $(SDL_MIXER_SHARED) \
		$(SDL2_SHARED) $(ASTRA_LIBC_LIBRARY)
	@mkdir -p $(@D)
	$(TARGET_CC) $(ASTRA_POSIX_CPPFLAGS) \
		-Ibuild/vendor/SDL2/include -Ibuild/vendor/SDL2_mixer/include \
		-DASTRA_POSIX_PROGRAM_NAME='"sdlmixerprobe"' \
		-DASTRA_POSIX_PROGRAM_MAJOR=1 -DASTRA_POSIX_PROGRAM_MINOR=0 \
		-DASTRA_POSIX_PROGRAM_PATCH=0 \
		-DASTRA_POSIX_PROGRAM_AUTHOR='"Astra project"' \
		-DASTRA_POSIX_PROGRAM_COPYRIGHT='"Copyright 2026 Astra project"' \
		$(ASTRA_POSIX_CFLAGS) $(ASTRA_POSIX_LDFLAGS) -o $@ \
		$(ASTRA_POSIX_CRT0) tests/mixer_probe.c \
		$(ASTRA_POSIX_PROGRAM_SOURCE) \
		-L$(dir $(SDL_MIXER_SHARED)) \
		-Wl,-rpath-link,$(dir $(SDL_MIXER_SHARED)) \
		-Wl,-l:SDL2_mixer.library.2 -Wl,-l:SDL2.library.2 \
		-Wl,-l:libc.library.2 -Wl,-l:runtime.library.1 \
		-Wl,-l:compiler.library.1
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(ASTRA_READELF) \
		--needed SDL2_mixer.library.2 --needed SDL2.library.2 \
		--needed libc.library.2 --needed runtime.library.1 \
		--needed compiler.library.1 $@

build/mixer-fixtures/sample.mp3:
	@mkdir -p $(@D)
	ffmpeg -nostdin -loglevel error -f lavfi \
		-i 'sine=frequency=440:duration=0.25' -ac 2 -ar 48000 \
		-c:a libmp3lame -y $@

build/mixer-fixtures/sample.ogg:
	@mkdir -p $(@D)
	ffmpeg -nostdin -loglevel error -f lavfi \
		-i 'sine=frequency=440:duration=0.25' -ac 2 -ar 48000 \
		-c:a libvorbis -y $@

build/mixer-fixtures/sample.flac:
	@mkdir -p $(@D)
	ffmpeg -nostdin -loglevel error -f lavfi \
		-i 'sine=frequency=440:duration=0.25' -ac 2 -ar 48000 \
		-c:a flac -y $@

$(eval $(call astra_app_bundle,build,SDLMixerProbe,build/m68k/sdlmixerprobe,tests/SDLMixerProbe.app/manifest,terminal,build/mixer-fixtures/sample.mp3 build/mixer-fixtures/sample.ogg build/mixer-fixtures/sample.flac))
mixer-probe-bundle: build/.SDLMixerProbe.app.stamp

build/m68k/sdlimageprobe: tests/image_probe.c $(SDL_IMAGE_SHARED) \
		$(SDL2_SHARED) $(ASTRA_LIBC_LIBRARY)
	@mkdir -p $(@D)
	$(TARGET_CC) $(ASTRA_POSIX_CPPFLAGS) \
		-Ibuild/vendor/SDL2/include -Ibuild/vendor/SDL2_image/include \
		-DASTRA_POSIX_PROGRAM_NAME='"sdlimageprobe"' \
		-DASTRA_POSIX_PROGRAM_MAJOR=1 -DASTRA_POSIX_PROGRAM_MINOR=0 \
		-DASTRA_POSIX_PROGRAM_PATCH=0 \
		-DASTRA_POSIX_PROGRAM_AUTHOR='"Astra project"' \
		-DASTRA_POSIX_PROGRAM_COPYRIGHT='"Copyright 2026 Astra project"' \
		$(ASTRA_POSIX_CFLAGS) $(ASTRA_POSIX_LDFLAGS) -o $@ \
		$(ASTRA_POSIX_CRT0) tests/image_probe.c \
		$(ASTRA_POSIX_PROGRAM_SOURCE) \
		-L$(dir $(SDL_IMAGE_SHARED)) \
		-Wl,-rpath-link,$(dir $(SDL_IMAGE_SHARED)) \
		-Wl,-l:SDL2_image.library.2 -Wl,-l:SDL2.library.2 \
		-Wl,-l:libc.library.2 -Wl,-l:runtime.library.1 \
		-Wl,-l:compiler.library.1
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(ASTRA_READELF) \
		--needed SDL2_image.library.2 --needed SDL2.library.2 \
		--needed libc.library.2 --needed runtime.library.1 \
		--needed compiler.library.1 $@

# The image probe also opens a PNG under a .jpg name to test detection.
IMAGE_PROBE_FIXTURES := build/vendor/SDL2_image/test/sample.png \
	build/vendor/SDL2_image/test/sample.jpg \
	build/vendor/SDL2_image/test/sample.qoi build/image-fixtures/misnamed.jpg

$(filter build/vendor/%,$(IMAGE_PROBE_FIXTURES)): $(SDL_IMAGE_STAMP)

build/image-fixtures/misnamed.jpg: build/vendor/SDL2_image/test/sample.png
	@mkdir -p $(@D)
	cp $< $@

$(eval $(call astra_app_bundle,build,SDLImageProbe,build/m68k/sdlimageprobe,tests/SDLImageProbe.app/manifest,terminal,$(IMAGE_PROBE_FIXTURES)))
image-probe-bundle: build/.SDLImageProbe.app.stamp

build/m68k/testaudioinfo: SDL_EXAMPLE_SOURCE = build/vendor/SDL2/test/testaudioinfo.c
build/m68k/testver: SDL_EXAMPLE_SOURCE = build/vendor/SDL2/test/testver.c
build/m68k/testdraw2: SDL_EXAMPLE_SOURCE = build/vendor/SDL2/test/testdraw2.c
$(foreach demo,$(SDL_DEMOS),$(eval build/m68k/$(demo): SDL_EXAMPLE_SOURCE = build/vendor/SDL2/test/$(demo).c build/vendor/SDL2/test/testutils.c))
$(foreach demo,$(SDL_DEMOS),$(eval build/m68k/$(demo): $(SDL2_ROOT)/test/$(demo).c $(SDL2_ROOT)/test/testutils.c))
build/m68k/sdlprobe: SDL_EXAMPLE_SOURCE = tests/probe.c
build/m68k/sdlvideoprobe: SDL_EXAMPLE_SOURCE = tests/video_probe.c
build/m68k/sdlrenderprobe: SDL_EXAMPLE_SOURCE = tests/render_probe.c
build/m68k/sdlframebench: SDL_EXAMPLE_SOURCE = tests/frame_bench.c
build/m68k/testaudioinfo: $(SDL2_ROOT)/test/testaudioinfo.c
build/m68k/testver: $(SDL2_ROOT)/test/testver.c
build/m68k/testdraw2: $(SDL2_ROOT)/test/testdraw2.c
build/m68k/sdlprobe: tests/probe.c
build/m68k/sdlvideoprobe: tests/video_probe.c
build/m68k/sdlrenderprobe: tests/render_probe.c
build/m68k/sdlframebench: tests/frame_bench.c
build/m68k/testaudioinfo build/m68k/testver build/m68k/testdraw2 \
build/m68k/sdlprobe build/m68k/sdlvideoprobe build/m68k/sdlrenderprobe \
build/m68k/sdlframebench $(addprefix build/m68k/,$(SDL_DEMOS)): \
		$(SDL2_SHARED) $(ASTRA_LIBC_LIBRARY)
	@mkdir -p $(@D)
	$(TARGET_CC) $(ASTRA_POSIX_CPPFLAGS) -Ibuild/vendor/SDL2/include \
		-DASTRA_POSIX_PROGRAM_NAME='"$(@F)"' \
		-DASTRA_POSIX_PROGRAM_MAJOR=1 -DASTRA_POSIX_PROGRAM_MINOR=0 \
		-DASTRA_POSIX_PROGRAM_PATCH=0 \
		-DASTRA_POSIX_PROGRAM_AUTHOR='"SDL contributors"' \
		-DASTRA_POSIX_PROGRAM_COPYRIGHT='"Copyright SDL contributors"' \
		$(ASTRA_POSIX_CFLAGS) $(ASTRA_POSIX_LDFLAGS) -o $@ \
		$(ASTRA_POSIX_CRT0) $(SDL_EXAMPLE_SOURCE) \
		$(ASTRA_POSIX_PROGRAM_SOURCE) \
		build/vendor/SDL2/libSDL2_test.a \
		-L$(dir $(SDL2_SHARED)) -Wl,-rpath-link,$(dir $(SDL2_SHARED)) \
		-Wl,-l:SDL2.library.2 -Wl,-l:libc.library.2 \
		-Wl,-l:runtime.library.1 -Wl,-l:compiler.library.1
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(ASTRA_READELF) \
		--needed SDL2.library.2 --needed libc.library.2 \
		--needed runtime.library.1 --needed compiler.library.1 $@

build/m68k/showinterfaces: SDL_NET_EXAMPLE_SOURCE = build/vendor/SDL2_net/examples/showinterfaces.c
build/m68k/chatd: SDL_NET_EXAMPLE_SOURCE = build/vendor/SDL2_net/examples/chatd.c
build/m68k/chat: SDL_NET_EXAMPLE_SOURCE = build/vendor/SDL2_net/examples/chat.c
build/m68k/showinterfaces build/m68k/chatd build/m68k/chat: \
		$(SDL_NET_SHARED) $(SDL2_SHARED) $(ASTRA_LIBC_LIBRARY) \
		$(SDL_NET_STAMP)
	@mkdir -p $(@D)
	$(TARGET_CC) $(ASTRA_POSIX_CPPFLAGS) \
		-Ibuild/vendor/SDL2/include -Ibuild/vendor/SDL2_net/include \
		-D_DEFAULT_SOURCE=1 \
		-DASTRA_POSIX_PROGRAM_NAME='"$(@F)"' \
		-DASTRA_POSIX_PROGRAM_MAJOR=1 -DASTRA_POSIX_PROGRAM_MINOR=0 \
		-DASTRA_POSIX_PROGRAM_PATCH=0 \
		-DASTRA_POSIX_PROGRAM_AUTHOR='"SDL contributors"' \
		-DASTRA_POSIX_PROGRAM_COPYRIGHT='"Copyright SDL contributors"' \
		$(ASTRA_POSIX_CFLAGS) $(ASTRA_POSIX_LDFLAGS) -o $@ \
		$(ASTRA_POSIX_CRT0) $(SDL_NET_EXAMPLE_SOURCE) \
		$(ASTRA_POSIX_PROGRAM_SOURCE) \
		build/vendor/SDL2/libSDL2_test.a \
		-L$(dir $(SDL_NET_SHARED)) \
		-Wl,-rpath-link,$(dir $(SDL_NET_SHARED)) \
		-Wl,-l:SDL2_net.library.2 -Wl,-l:SDL2.library.2 \
		-Wl,-l:libc.library.2 -Wl,-l:runtime.library.1 \
		-Wl,-l:compiler.library.1
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(ASTRA_READELF) \
		--needed SDL2_net.library.2 --needed SDL2.library.2 \
		--needed libc.library.2 --needed runtime.library.1 \
		--needed compiler.library.1 $@

build/m68k/sdlnetprobe: tests/net_probe.c $(SDL_NET_SHARED) \
		$(SDL2_SHARED) $(ASTRA_LIBC_LIBRARY)
	@mkdir -p $(@D)
	$(TARGET_CC) $(ASTRA_POSIX_CPPFLAGS) \
		-Ibuild/vendor/SDL2/include -Ibuild/vendor/SDL2_net/include \
		-DASTRA_POSIX_PROGRAM_NAME='"sdlnetprobe"' \
		-DASTRA_POSIX_PROGRAM_MAJOR=1 -DASTRA_POSIX_PROGRAM_MINOR=0 \
		-DASTRA_POSIX_PROGRAM_PATCH=0 \
		-DASTRA_POSIX_PROGRAM_AUTHOR='"Astra project"' \
		-DASTRA_POSIX_PROGRAM_COPYRIGHT='"Copyright 2026 Astra project"' \
		$(ASTRA_POSIX_CFLAGS) $(ASTRA_POSIX_LDFLAGS) -o $@ \
		$(ASTRA_POSIX_CRT0) tests/net_probe.c \
		$(ASTRA_POSIX_PROGRAM_SOURCE) \
		-L$(dir $(SDL_NET_SHARED)) \
		-Wl,-rpath-link,$(dir $(SDL_NET_SHARED)) \
		-Wl,-l:SDL2_net.library.2 -Wl,-l:SDL2.library.2 \
		-Wl,-l:libc.library.2 -Wl,-l:runtime.library.1 \
		-Wl,-l:compiler.library.1
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(ASTRA_READELF) \
		--needed SDL2_net.library.2 --needed SDL2.library.2 \
		--needed libc.library.2 --needed runtime.library.1 \
		--needed compiler.library.1 $@

$(eval $(call astra_app_bundle,build,SDLNetProbe,build/m68k/sdlnetprobe,tests/SDLNetProbe.app/manifest,terminal,))
net-probe-bundle: build/.SDLNetProbe.app.stamp

$(eval $(call astra_app_bundle,build,SDLVideoProbe,build/m68k/sdlvideoprobe,tests/SDLVideoProbe.app/manifest,terminal,))
video-probe-bundle: build/.SDLVideoProbe.app.stamp

$(eval $(call astra_app_bundle,build,SDLRenderProbe,build/m68k/sdlrenderprobe,tests/SDLRenderProbe.app/manifest,terminal,))
render-probe-bundle: build/.SDLRenderProbe.app.stamp

$(eval $(call astra_app_bundle,build,SDLFrameBench,build/m68k/sdlframebench,apps/SDLFrameBench.app/manifest,$(SDL2_ROOT)/test/icon.bmp,))
frame-bench-bundle: build/.SDLFrameBench.app.stamp

$(eval $(call astra_app_bundle,build,SDLTestDraw2,build/m68k/testdraw2,apps/SDLTestDraw2.app/manifest,$(SDL2_ROOT)/test/icon.bmp,))
testdraw2-bundle: build/.SDLTestDraw2.app.stamp

SDL_DEMO_ICON := $(SDL2_ROOT)/test/icon.bmp
$(eval $(call astra_app_bundle,build,SDLTestSprite2,build/m68k/testsprite2,apps/SDLTestSprite2.app/manifest,$(SDL_DEMO_ICON),$(SDL_DEMO_ICON)))
$(eval $(call astra_app_bundle,build,SDLTestGeometry,build/m68k/testgeometry,apps/SDLTestGeometry.app/manifest,$(SDL_DEMO_ICON),$(SDL_DEMO_ICON)))
$(eval $(call astra_app_bundle,build,SDLTestRenderTarget,build/m68k/testrendertarget,apps/SDLTestRenderTarget.app/manifest,$(SDL_DEMO_ICON),$(SDL_DEMO_ICON) $(SDL2_ROOT)/test/sample.bmp))
$(eval $(call astra_app_bundle,build,SDLTestScale,build/m68k/testscale,apps/SDLTestScale.app/manifest,$(SDL_DEMO_ICON),$(SDL_DEMO_ICON) $(SDL2_ROOT)/test/sample.bmp))
demo-bundles: $(foreach demo,SDLTestSprite2 SDLTestGeometry SDLTestRenderTarget SDLTestScale,build/.$(demo).app.stamp)

$(eval $(call astra_app_inventory,build,$(SHIPPED_APPS)))

$(HOST_TEST): audio/SDL_astraaudio.c tests/host/test_astraaudio.c
	@mkdir -p $(@D)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(SDL2_INCLUDES) \
		$(ASTRA_INCLUDES) tests/host/test_astraaudio.c -o $@

$(HOST_TEST_VIDEO): video/SDL_astravideo.c tests/host/test_astravideo.c | prepare
	@mkdir -p $(@D)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror \
		-Ibuild/vendor/SDL2/include -Ibuild/vendor/SDL2/src \
		$(ASTRA_INCLUDES) tests/host/test_astravideo.c -o $@

$(HOST_TEST_RENDER): render/SDL_astrarender.c tests/host/test_astrarender.c \
		video/SDL_astravideo.h | prepare
	@mkdir -p $(@D)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror \
		-Ibuild/vendor/SDL2/include -Ibuild/vendor/SDL2/src \
		$(ASTRA_INCLUDES) tests/host/test_astrarender.c -lm -o $@

target-check: prepare
	@mkdir -p build/m68k
	$(TARGET_CC) -std=c11 -Wall -Wextra -Werror \
		-DSDL_AUDIO_DRIVER_ASTRA=1 $(SDL2_INCLUDES) $(ASTRA_INCLUDES) \
		-I$(PICOLIBC)/include -c audio/SDL_astraaudio.c -o $(TARGET_AUDIO)
	$(TARGET_CC) -std=c11 -DSDL_AUDIO_DRIVER_ASTRA=1 \
		-Ibuild/vendor/SDL2/include -Ibuild/vendor/SDL2/src \
		-I$(PICOLIBC)/include \
		-c build/vendor/SDL2/src/audio/SDL_audio.c -o $(TARGET_REGISTRY)
	$(TARGET_CC) -std=c11 -Wall -Wextra -Werror \
		-DSDL_VIDEO_DRIVER_ASTRA=1 -Ibuild/vendor/SDL2/include \
		-Ibuild/vendor/SDL2/src $(ASTRA_INCLUDES) \
		-I$(PICOLIBC)/include \
		-c build/vendor/SDL2/src/video/astra/SDL_astravideo.c \
		-o $(TARGET_VIDEO)
	$(TARGET_CC) -std=c11 -DSDL_VIDEO_DRIVER_ASTRA=1 \
		-Ibuild/vendor/SDL2/include -Ibuild/vendor/SDL2/src \
		-I$(PICOLIBC)/include \
		-c build/vendor/SDL2/src/video/SDL_video.c -o $(TARGET_VIDEO_REGISTRY)
	$(TARGET_CC) -std=c11 -Wall -Wextra -Werror \
		-DSDL_VIDEO_RENDER_ASTRA=1 -Ibuild/vendor/SDL2/include \
		-Ibuild/vendor/SDL2/src $(ASTRA_INCLUDES) \
		-I$(PICOLIBC)/include \
		-c build/vendor/SDL2/src/render/astra/SDL_astrarender.c \
		-o $(TARGET_RENDER)

test: net-shared image-shared mixer-shared ttf-shared \
		$(HOST_TEST) $(HOST_TEST_VIDEO) $(HOST_TEST_RENDER) target-check
	./$(HOST_TEST)
	./$(HOST_TEST_VIDEO)
	./$(HOST_TEST_RENDER)
	python3 test_prepare_source.py
	python3 test_prepare_addon_source.py
	python3 test_probe_manifests.py
	python3 test_generate_exports.py

sanitize analyze: test

clean:
	rm -rf build

.PHONY: all prepare prepare-net prepare-image prepare-mixer prepare-ttf \
	core shared net-shared image-shared mixer-shared ttf-shared \
	library-contract net-library-contract \
	image-library-contract mixer-library-contract ttf-library-contract \
	target-check test \
	video-probe-bundle render-probe-bundle frame-bench-bundle testdraw2-bundle \
	demo-bundles net-probe-bundle image-probe-bundle mixer-probe-bundle ttf-probe-bundle \
	sanitize analyze clean
