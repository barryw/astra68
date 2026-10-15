include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
HOST_CC ?= cc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
READELF := $(CROSS)readelf
RUNTIME := ../../runtime
include $(RUNTIME)/astra-link.mk
include ../../libraries.mk
GRAPHICS := ../../graphics
VFS := ../../vfs
NDK := ../../../../ndk
CPPFLAGS := -I../../../../ndk/include -I../../../include -I$(RUNTIME)/include \
	-I$(GRAPHICS)/include -I$(VFS)/include

CPPFLAGS += -I../../interface/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
TARGET_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC $(ASTRA_DEBUG_FLAGS)
TARGET := build/m68k/desktop.elf
IMAGE := build/m68k/desktop
OBJECT := build/m68k/main.o
OBJECTS := build/m68k/crc32.o
HOST_TEST := build/host/test_desktop_layout
HOST_TEST_SANITIZE := build/host/test_desktop_layout_sanitize
GRAPHICS_HOST_SURFACE := $(GRAPHICS)/build/host/surface.o
GRAPHICS_HOST_SURFACE_SANITIZE := $(GRAPHICS)/build/host/surface_sanitize.o
SYSTEM_HOST_UTF8 := ../../system/build/host/utf8.o
SOUND_SOURCE := assets/AstraStartup.wav
SOUND_RENDERER := render_startup_sound.py
SOUND_ASSET := build/m68k/startup.pcm
LIBRARIES := $(ASTRA_PCM_LIBRARY) $(ASTRA_INTERFACE_LIBRARY) $(ASTRA_GRAPHICS_LIBRARY) \
	$(ASTRA_FONT_LIBRARY) \
	$(ASTRA_FILESYSTEM_LIBRARY) $(ASTRA_SYSTEM_LIBRARY) \
	$(ASTRA_RUNTIME_LIBRARY) $(ASTRA_COMPILER_LIBRARY)
# The startup sound is a product of this program like its image.
ASTRA_PROGRAM_TARGETS := $(IMAGE) size $(SOUND_ASSET)
include ../../dynamic-program.mk

$(OBJECT): main.c desktop_layout.h desktop_usage.h desktop_usage_layout.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

build/m68k/crc32.o: ../../../common/crc32.c ../../../include/astra/crc32.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

$(SOUND_ASSET): $(SOUND_SOURCE) $(SOUND_RENDERER)
	@mkdir -p $(@D)
	python3 $(SOUND_RENDERER) $(SOUND_SOURCE) $@

.DELETE_ON_ERROR: $(SOUND_ASSET)

$(GRAPHICS_HOST_SURFACE) $(GRAPHICS_HOST_SURFACE_SANITIZE): FORCE
	$(MAKE) -C $(GRAPHICS) $(patsubst $(GRAPHICS)/%,%,$@)

$(SYSTEM_HOST_UTF8): FORCE
	$(MAKE) -C ../../system build/host/utf8.o

$(HOST_TEST): tests/test_desktop_layout.c desktop_layout.h desktop_usage_layout.h $(GRAPHICS_HOST_SURFACE) $(SYSTEM_HOST_UTF8)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O2 -ffunction-sections -Wl,--gc-sections \
		$< $(GRAPHICS_HOST_SURFACE) $(SYSTEM_HOST_UTF8) -o $@

test: $(HOST_TEST) $(SOUND_ASSET)
	./$(HOST_TEST)
	python3 tests/test_startup_sound.py $(SOUND_SOURCE) $(SOUND_ASSET)

$(HOST_TEST_SANITIZE): tests/test_desktop_layout.c desktop_layout.h desktop_usage_layout.h $(GRAPHICS_HOST_SURFACE_SANITIZE) $(SYSTEM_HOST_UTF8)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O1 \
		-fsanitize=address,undefined -fno-omit-frame-pointer -ffunction-sections -Wl,--gc-sections \
		$< $(GRAPHICS_HOST_SURFACE_SANITIZE) $(SYSTEM_HOST_UTF8) -o $@

sanitize: $(HOST_TEST_SANITIZE) $(SOUND_ASSET)
	./$(HOST_TEST_SANITIZE)
	python3 tests/test_startup_sound.py $(SOUND_SOURCE) $(SOUND_ASSET)

analyze:
	@mkdir -p build/analyzer
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -fanalyzer \
		-c main.c -o build/analyzer/main.o

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)

FORCE:
.PHONY: all size test sanitize analyze clean FORCE
