include ../../../../mk/astra-version.mk
include ../../../../tools/astra-bundle.mk

ROOT := ../../../..
NDK := $(abspath $(ROOT)/ndk/build/dist/check/astra68-ndk-$(ASTRA_OS_VERSION))
PROGRAM := build/cmake/astraz

.DEFAULT_GOAL := all
all: bundle

$(PROGRAM): CMakeLists.txt src/main.c src/audio.c src/audio.h \
	src/picture.c src/picture.h src/transcript_flow.c \
	src/canvas.c src/canvas.h src/line_input.c src/line_input.h \
	src/transcript_flow.h src/screen_grid.c src/screen_grid.h \
	src/engine.c include/astraz/engine.h
	test -f $(NDK)/cmake/m68k-astra-toolchain.cmake || \
		{ echo 'Build NDK first: make -C ndk dist-check' >&2; exit 1; }
	cmake -S . -B build/cmake -G Ninja \
		-DCMAKE_TOOLCHAIN_FILE=$(NDK)/cmake/m68k-astra-toolchain.cmake \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_FLAGS_RELEASE='-O2 -DNDEBUG -fno-strict-aliasing'
	cmake --build build/cmake --target astraz --parallel 8
	python3 $(NDK)/tools/check_dynamic_executable.py \
		--readelf m68k-astra-readelf \
		--needed interface.library.9 --needed graphics.library.3 \
		--needed font.library.5 --needed system.library.4 \
		--needed SDL2_mixer.library.3 \
		--needed runtime.library.1 --needed compiler.library.2 \
		--needed libc.library.4 --needed SDL2.library.3 \
		--needed SDL2_image.library.2 $@

$(eval $(call astra_app_bundle,build,AstraZ,$(PROGRAM),apps/AstraZ.app/manifest,document,))
$(eval $(call astra_app_inventory,build,AstraZ))
bundle: build/.apps

build/test_transcript_flow: src/transcript_flow.c src/transcript_flow.h \
		tests/test_transcript_flow.c ../../interface/src/text_model.c \
		../../../common/utf8.c
	@mkdir -p $(@D)
	cc -std=c99 -Wall -Wextra -Werror -Iinclude -Isrc \
		-I$(ROOT)/ndk/include -I$(ROOT)/sw/include \
		src/transcript_flow.c ../../interface/src/text_model.c \
		../../../common/utf8.c tests/test_transcript_flow.c \
		-o $@

build/test_frotz_stream_word: src/frotz_stream_word.h \
		tests/test_frotz_stream_word.c
	@mkdir -p $(@D)
	cc -std=c99 -Wall -Wextra -Werror -Isrc \
		tests/test_frotz_stream_word.c -o $@

build/test_canvas: src/canvas.c src/canvas.h tests/test_canvas.c \
        tests/fakes/SDL.h ../../../common/utf8.c
	@mkdir -p $(@D)
	cc -std=c99 -Wall -Wextra -Werror -Itests/fakes -Iinclude -Isrc \
		-I$(ROOT)/ndk/include -I$(ROOT)/sw/include \
		src/canvas.c ../../../common/utf8.c tests/test_canvas.c -o $@

build/test_line_input: src/line_input.c src/line_input.h \
		tests/test_line_input.c ../../interface/src/text_model.c \
		../../../common/utf8.c
	@mkdir -p $(@D)
	cc -std=c99 -Wall -Wextra -Werror -Iinclude -Isrc \
		-I$(ROOT)/ndk/include -I$(ROOT)/sw/include \
		src/line_input.c ../../interface/src/text_model.c \
		../../../common/utf8.c tests/test_line_input.c -o $@

test: build/test_transcript_flow build/test_frotz_stream_word \
		build/test_screen_grid build/test_canvas \
		build/test_line_input
	build/test_transcript_flow
	build/test_frotz_stream_word
	build/test_screen_grid
	build/test_canvas
	build/test_line_input

build/test_screen_grid: src/screen_grid.c src/screen_grid.h tests/test_screen_grid.c
	@mkdir -p $(@D)
	cc -std=c99 -Wall -Wextra -Werror -Iinclude -Isrc \
		-I$(ROOT)/ndk/include \
		src/screen_grid.c tests/test_screen_grid.c -o $@

clean:
	rm -rf build

.PHONY: all bundle test clean
