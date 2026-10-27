include ../../../../mk/astra-version.mk
include ../../../../tools/astra-bundle.mk

ROOT := ../../../..
SOURCE ?= ../../../../../chocolate-quake
VENDOR := build/vendor/chocolate-quake
BUILD := build/cmake
NDK := $(abspath $(ROOT)/ndk/build/dist/check/astra68-ndk-$(ASTRA_OS_VERSION))
PROGRAM := $(BUILD)/src/chocolate-quake

.DEFAULT_GOAL := all
all: $(PROGRAM)

$(PROGRAM): prepare_source.py Makefile FORCE_SOURCE_CHECK
	python3 prepare_source.py $(SOURCE) $(VENDOR)
	test -f $(NDK)/cmake/m68k-astra-toolchain.cmake || \
		{ echo 'Build NDK first: make -C ndk dist-check' >&2; exit 1; }
	cmake -S $(VENDOR) -B $(BUILD) -G Ninja \
		-DCMAKE_TOOLCHAIN_FILE=$(NDK)/cmake/m68k-astra-toolchain.cmake \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_FLAGS_RELEASE='-O2 -DNDEBUG -fno-strict-aliasing'
	cmake --build $(BUILD) --target chocolate-quake --parallel 8
	python3 $(NDK)/tools/check_dynamic_executable.py \
		--readelf m68k-astra-readelf \
		--needed SDL2_net.library.2 --needed SDL2_mixer.library.3 \
		--needed runtime.library.1 \
		--needed compiler.library.2 --needed libc.library.4 \
		--needed SDL2.library.3 $@

test: all

$(eval $(call astra_app_bundle,build,ChocolateQuake,$(PROGRAM),apps/ChocolateQuake.app/manifest,geometry,$(SOURCE)/LICENSE))
bundle: build/.ChocolateQuake.app.stamp
	mkdir -p build/ChocolateQuake.app/resources/id1

clean:
	rm -rf build

.PHONY: all test bundle clean FORCE_SOURCE_CHECK
