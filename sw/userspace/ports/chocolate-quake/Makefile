include ../../../../mk/astra-version.mk
include ../../../../tools/astra-bundle.mk

ROOT := ../../../..
SOURCE ?= $(HOME)/Git/chocolate-quake
VENDOR := build/vendor/chocolate-quake
BUILD := build/cmake
NDK := $(abspath $(ROOT)/ndk/build/dist/check/astra68-ndk-$(ASTRA_OS_VERSION))
PROGRAM := $(BUILD)/src/chocolate-quake
PAK_DIR := build/pak
PAK := $(PAK_DIR)/id1/pak0.pak
ICON := assets/quake.png

.DEFAULT_GOAL := all
all: build/.apps

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

$(PAK): fetch_pak.py
	python3 fetch_pak.py $(PAK_DIR)

$(PAK_DIR)/id1 $(PAK_DIR)/licinfo.txt $(PAK_DIR)/slicnse.txt: $(PAK) ;

$(eval $(call astra_app_bundle,build,ChocolateQuake,$(PROGRAM),apps/ChocolateQuake.app/manifest,$(ICON),$(PAK_DIR)/id1 $(PAK_DIR)/licinfo.txt $(PAK_DIR)/slicnse.txt $(SOURCE)/LICENSE))
build/.ChocolateQuake.app.stamp: $(PAK)
$(eval $(call astra_app_inventory,build,ChocolateQuake))
bundle: build/.apps

clean:
	rm -rf build

.PHONY: all test bundle clean FORCE_SOURCE_CHECK
