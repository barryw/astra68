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
OBJECTS :=
HOST_TEST := build/host/test_desktop_layout
HOST_TEST_SANITIZE := build/host/test_desktop_layout_sanitize
SOUND_SOURCE := assets/AstraStartup.wav
SOUND_RENDERER := render_startup_sound.py
SOUND_ASSET := build/m68k/startup.pcm
LIBRARIES := $(ASTRA_PCM_LIBRARY) $(ASTRA_INTERFACE_LIBRARY) $(ASTRA_GRAPHICS_LIBRARY) \
	$(ASTRA_FONT_LIBRARY) \
	$(ASTRA_FILESYSTEM_LIBRARY) $(ASTRA_SYSTEM_LIBRARY) \
	$(ASTRA_RUNTIME_LIBRARY) $(ASTRA_COMPILER_LIBRARY)
include ../../dynamic-program.mk

all: $(SOUND_ASSET)

$(OBJECT): main.c desktop_layout.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@

$(SOUND_ASSET): $(SOUND_SOURCE) $(SOUND_RENDERER)
	@mkdir -p $(@D)
	python3 $(SOUND_RENDERER) $(SOUND_SOURCE) $@

.DELETE_ON_ERROR: $(SOUND_ASSET)

$(HOST_TEST): tests/test_desktop_layout.c desktop_layout.h
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O2 $< -o $@

test: $(HOST_TEST) $(SOUND_ASSET)
	./$(HOST_TEST)
	python3 tests/test_startup_sound.py $(SOUND_SOURCE) $(SOUND_ASSET)

$(HOST_TEST_SANITIZE): tests/test_desktop_layout.c desktop_layout.h
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) -O1 \
		-fsanitize=address,undefined -fno-omit-frame-pointer $< -o $@

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

.PHONY: all size test sanitize analyze clean
