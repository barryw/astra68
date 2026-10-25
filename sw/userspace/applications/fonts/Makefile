include ../../../../mk/m68k-cross.mk

CC := $(CROSS)gcc
HOST_CC ?= cc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
READELF := $(CROSS)readelf
RUNTIME := ../../runtime
include $(RUNTIME)/astra-link.mk
include ../../libraries.mk
NDK := ../../../../ndk
CPPFLAGS := -I$(NDK)/include -I../../../include -I$(RUNTIME)/include
FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic \
	$(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fPIC $(ASTRA_DEBUG_FLAGS)
TARGET := build/m68k/fonts.elf
IMAGE := build/m68k/fonts
OBJECT := build/m68k/main.o
LIBRARIES := $(ASTRA_INTERFACE_LIBRARY) $(ASTRA_FONT_LIBRARY) \
	$(ASTRA_FILESYSTEM_LIBRARY) \
	$(ASTRA_GRAPHICS_LIBRARY) $(ASTRA_SYSTEM_LIBRARY) \
	$(ASTRA_RUNTIME_LIBRARY) $(ASTRA_COMPILER_LIBRARY)
include ../../dynamic-program.mk

$(OBJECT): main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) -MMD -MP -c $< -o $@

PROBE_LIBRARIES := $(ASTRA_FONT_LIBRARY) $(ASTRA_FILESYSTEM_LIBRARY) \
	$(ASTRA_SYSTEM_LIBRARY) $(ASTRA_RUNTIME_LIBRARY) \
	$(ASTRA_COMPILER_LIBRARY)

build/m68k/import_probe.o: tests/import_probe.c tests/import_support.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) -MMD -MP -c $< -o $@

build/m68k/import_probe.elf: build/m68k/import_probe.o $(PROBE_LIBRARIES) \
		$(ASTRA_PROGRAM_CRT0)
	$(CC) $(TARGET_FLAGS) $(ASTRA_PROGRAM_LINK_FLAGS) -o $@ \
		$(ASTRA_PROGRAM_CRT0) build/m68k/import_probe.o \
		$(foreach library,$(PROBE_LIBRARIES),-Wl,-l:$(notdir $(library)))
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(READELF) \
		$(foreach library,$(PROBE_LIBRARIES),--needed $(notdir $(library))) $@

build/m68k/import_probe: build/m68k/import_probe.elf
	$(OBJCOPY) $(ASTRA_PROGRAM_STRIP_FLAGS) $< $@

build/m68k/outline_probe.o: tests/outline_probe.c tests/import_support.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) -MMD -MP -c $< -o $@

build/m68k/outline_probe.elf: build/m68k/outline_probe.o $(PROBE_LIBRARIES) \
		$(ASTRA_PROGRAM_CRT0)
	$(CC) $(TARGET_FLAGS) $(ASTRA_PROGRAM_LINK_FLAGS) -o $@ \
		$(ASTRA_PROGRAM_CRT0) build/m68k/outline_probe.o \
		$(foreach library,$(PROBE_LIBRARIES),-Wl,-l:$(notdir $(library)))
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(READELF) \
		$(foreach library,$(PROBE_LIBRARIES),--needed $(notdir $(library))) $@

build/m68k/outline_probe: build/m68k/outline_probe.elf
	$(OBJCOPY) $(ASTRA_PROGRAM_STRIP_FLAGS) $< $@

build/FontImportProbe.app/manifest: tests/FontImportProbe.app/manifest
	@mkdir -p $(@D)
	cp $< $@

build/FontImportProbe.app/bin/m68k-68040/FontImportProbe: build/m68k/import_probe
	@mkdir -p $(@D)
	cp $< $@

build/FontImportProbe.app/resources/FontImportProbe.aicon: \
		../../../../tools/aicon.py
	@mkdir -p $(@D)
	python3 $< fonts $@

import-probe: all build/FontImportProbe.app/manifest \
		build/FontImportProbe.app/bin/m68k-68040/FontImportProbe \
		build/FontImportProbe.app/resources/FontImportProbe.aicon
	../../../../tools/build/astra-bundle check build/FontImportProbe.app

build/FontOutlineProbe.app/manifest: tests/FontOutlineProbe.app/manifest
	@mkdir -p $(@D)
	cp $< $@

build/FontOutlineProbe.app/bin/m68k-68040/FontOutlineProbe: build/m68k/outline_probe
	@mkdir -p $(@D)
	cp $< $@

build/FontOutlineProbe.app/resources/FontOutlineProbe.aicon: \
		../../../../tools/aicon.py
	@mkdir -p $(@D)
	python3 $< fonts $@

outline-probe: all build/FontOutlineProbe.app/manifest \
		build/FontOutlineProbe.app/bin/m68k-68040/FontOutlineProbe \
		build/FontOutlineProbe.app/resources/FontOutlineProbe.aicon
	../../../../tools/build/astra-bundle check build/FontOutlineProbe.app

build/m68k/amiga_probe.o: tests/outline_probe.c tests/import_support.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) -DASTRA_FONT_PROBE_AMIGA -MMD -MP -c $< -o $@

build/m68k/amiga_probe.elf: build/m68k/amiga_probe.o $(PROBE_LIBRARIES) \
		$(ASTRA_PROGRAM_CRT0)
	$(CC) $(TARGET_FLAGS) $(ASTRA_PROGRAM_LINK_FLAGS) -o $@ \
		$(ASTRA_PROGRAM_CRT0) build/m68k/amiga_probe.o \
		$(foreach library,$(PROBE_LIBRARIES),-Wl,-l:$(notdir $(library)))
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(READELF) \
		$(foreach library,$(PROBE_LIBRARIES),--needed $(notdir $(library))) $@

build/m68k/amiga_probe: build/m68k/amiga_probe.elf
	$(OBJCOPY) $(ASTRA_PROGRAM_STRIP_FLAGS) $< $@

build/FontAmigaProbe.app/manifest: tests/FontAmigaProbe.app/manifest
	@mkdir -p $(@D)
	cp $< $@

build/FontAmigaProbe.app/bin/m68k-68040/FontAmigaProbe: build/m68k/amiga_probe
	@mkdir -p $(@D)
	cp $< $@

build/FontAmigaProbe.app/resources/FontAmigaProbe.aicon: \
		../../../../tools/aicon.py
	@mkdir -p $(@D)
	python3 $< fonts $@

amiga-probe: all build/FontAmigaProbe.app/manifest \
		build/FontAmigaProbe.app/bin/m68k-68040/FontAmigaProbe \
		build/FontAmigaProbe.app/resources/FontAmigaProbe.aicon
	../../../../tools/build/astra-bundle check build/FontAmigaProbe.app

UI_HOST_SOURCES := tests/test_layout.c ../../interface/src/control.c \
	../../interface/src/settings_pane.c \
	../../interface/src/text_model.c ../../interface/src/text_surface.c ../../interface/src/scroll.c \
	../../graphics/src/surface.c ../../graphics/src/font_data.c \
	../../graphics/src/render_builder.c \
	../../../common/utf8.c ../../../common/afnt.c ../../../common/json.c ../../../common/crc32.c \
	$(RUNTIME)/src/string.c \
	$(RUNTIME)/tests/uncontended_mutex.c
UI_HOST_FLAGS := $(CPPFLAGS) -I../../graphics/include -I../../graphics/build/generated \
	-I../../../../fpga/arty/linux \
	-std=c11 -Wall -Wextra -Werror -pedantic -O2 \
	-ffunction-sections -fdata-sections -Wl,--gc-sections
UI_FONT_HEADERS := ../../graphics/build/generated/astra_font8x8.inc \
	../../graphics/build/generated/astra_ui_font.inc \
	../../graphics/build/generated/astra_mono_font.inc
UI_HOST_HEADERS := $(wildcard $(NDK)/include/astra/*.h ../../../include/astra/*.h \
	$(RUNTIME)/include/astra/*.h ../../interface/src/*.h \
	../../graphics/include/astra/*.h ../../graphics/src/*.h)
UI_FONT_INPUTS := ../../graphics/fonts/astra-workbench.afnt \
	../../graphics/fonts/astra-mono.afnt ../../../../assets/fonts/astra_8x16.hex \
	../../../../tools/fonts/afnt.py $(NDK)/include/astra/font_library.h

$(UI_FONT_HEADERS): $(UI_FONT_INPUTS)
	$(MAKE) -C ../../graphics $(@:../../graphics/%=%)

build/host/test_layout: main.c $(UI_HOST_SOURCES) $(UI_HOST_HEADERS) $(UI_FONT_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(UI_HOST_FLAGS) $(UI_HOST_SOURCES) -o $@

test: build/host/test_layout
	./build/host/test_layout

sanitize: $(UI_FONT_HEADERS)
	@mkdir -p build/host
	$(HOST_CC) $(UI_HOST_FLAGS) -O1 -fsanitize=address,undefined \
		-fno-omit-frame-pointer $(UI_HOST_SOURCES) -o build/host/test_layout_sanitize
	./build/host/test_layout_sanitize

analyze:
	@mkdir -p build/analyzer
	$(CC) $(CPPFLAGS) $(FLAGS) -fanalyzer -c main.c \
		-o build/analyzer/main.o
	$(CC) $(CPPFLAGS) $(FLAGS) -fanalyzer -c tests/import_probe.c \
		-o build/analyzer/import_probe.o
	$(CC) $(CPPFLAGS) $(FLAGS) -fanalyzer -c tests/outline_probe.c \
		-o build/analyzer/outline_probe.o

clean:
	rm -rf build

-include $(wildcard build/m68k/*.d)

.PHONY: test sanitize analyze clean import-probe outline-probe
