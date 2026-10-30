include ../../../mk/m68k-cross.mk
include ../../../mk/astra-shared-library.mk

CC := $(CROSS)gcc
READELF := $(CROSS)readelf
HOST_CC ?= cc
ANALYZER_CC ?= gcc
RUNTIME := ../runtime
COMPILER := ../compiler
GRAPHICS := ../graphics
NDK := ../../../ndk
SYSTEM := ../system
CPPFLAGS := -Iinclude -I$(GRAPHICS)/include -I$(RUNTIME)/include \
	-I../../include -I$(NDK)/include
COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
DEPFLAGS := -MMD -MP
HOST_FLAGS := -O2
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
LIBRARY_FLAGS := $(ASTRA_TARGET_ABI_FLAGS) -Os -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(ASTRA_SHARED_PIC_FLAGS)
INTERFACE_TARGET := build/m68k/libraries/interface.library.12
INTERFACE_OBJECTS := build/m68k/interface_library.o build/m68k/control.o \
	build/m68k/field_family.o \
	build/m68k/system_panels.o \
	build/m68k/file_panel.o \
	build/m68k/font_panel.o \
	build/m68k/display_widgets.o \
	build/m68k/settings_pane.o build/m68k/form_row.o build/m68k/columns.o build/m68k/toolbar.o \
	build/m68k/toolbar_actions.o \
	build/m68k/icon_tile.o build/m68k/image_view.o build/m68k/inline_edit.o \
	build/m68k/text_surface.o build/m68k/undo.o build/m68k/text_model.o \
	build/m68k/scroll.o build/m68k/command.o build/m68k/menu.o build/m68k/menu_context.o \
	build/m68k/combo_box.o build/m68k/menu_multi.o
INTERFACE_LIBRARY_MAP := interface.library.exports
NDK_HOST_UTF8 := $(SYSTEM)/build/host/utf8.o
GRAPHICS_LIBRARY := $(GRAPHICS)/build/m68k/libraries/graphics.library.3
FONT_LIBRARY := $(GRAPHICS)/build/m68k/libraries/font.library.5
SYSTEM_LIBRARY := $(SYSTEM)/build/m68k/libraries/system.library.4
RUNTIME_LIBRARY := $(RUNTIME)/build/m68k/libraries/runtime.library.1
COMPILER_LIBRARY := $(COMPILER)/build/m68k/libraries/compiler.library.2
SYMBOL_OWNERSHIP_CHECK := ../../../tools/check_shared_symbol_ownership.py
INTERFACE_HOST_TEST := build/host/test_interface
TEXT_SURFACE_HOST_TEST := build/host/test_text_surface
UNDO_HOST_TEST := build/host/test_undo
TEXT_MODEL_HOST_TEST := build/host/test_text_model
SCROLL_HOST_TEST := build/host/test_scroll
COMMAND_HOST_TEST := build/host/test_command
MENU_HOST_TEST := build/host/test_menu
ICON_TILE_HOST_TEST := build/host/test_icon_tile
IMAGE_VIEW_HOST_TEST := build/host/test_image_view
GRAPHICS_HOST_SURFACE := $(GRAPHICS)/build/host/surface.o $(GRAPHICS)/build/host/font_data.o
GRAPHICS_HOST_SURFACE_SANITIZE := $(GRAPHICS)/build/host/surface_sanitize.o $(GRAPHICS)/build/host/font_data_sanitize.o
FONT_HOST_INPUTS := $(RUNTIME)/tests/uncontended_mutex.c \
	../../common/afnt.c ../../common/json.c ../../common/crc32.c
GRAPHICS_FONT_HEADERS := $(GRAPHICS)/build/generated/astra_font8x8.inc \
	$(GRAPHICS)/build/generated/astra_ui_font.inc \
	$(GRAPHICS)/build/generated/astra_mono_font.inc
GRAPHICS_FONT_INPUTS := $(GRAPHICS)/fonts/astra-workbench.afnt \
	$(GRAPHICS)/fonts/astra-mono.afnt ../../../assets/fonts/astra_8x16.hex \
	../../../tools/fonts/afnt.py $(NDK)/include/astra/font_library.h

all: $(INTERFACE_TARGET) library-contract

interface-library: $(INTERFACE_TARGET)

$(GRAPHICS_FONT_HEADERS): $(GRAPHICS_FONT_INPUTS)
	$(MAKE) -C $(GRAPHICS) all

build/m68k/interface_library.o: src/interface_library.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(GRAPHICS)/build/generated $(COMMON_FLAGS) \
		$(LIBRARY_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/control.o: src/control.c src/control_internal.h src/text_surface_internal.h src/display_widgets_internal.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(GRAPHICS)/build/generated $(COMMON_FLAGS) \
		$(LIBRARY_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/settings_pane.o: src/settings_pane.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/display_widgets.o: src/display_widgets.c src/display_widgets_internal.h src/control_internal.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/form_row.o: src/form_row.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/columns.o: src/columns.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/field_family.o: src/field_family.c src/control_internal.h $(NDK)/include/astra/field_family.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/toolbar.o: src/toolbar.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/icon_tile.o build/m68k/image_view.o build/m68k/inline_edit.o: build/m68k/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/text_surface.o: src/text_surface.c src/text_surface_internal.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(GRAPHICS)/build/generated $(COMMON_FLAGS) \
		$(LIBRARY_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/system_panels.o: src/system_panels.c src/panel_internal.h $(NDK)/include/astra/system_panels.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/file_panel.o: src/file_panel.c src/panel_internal.h $(NDK)/include/astra/system_panels.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/font_panel.o: src/font_panel.c src/panel_internal.h $(NDK)/include/astra/system_panels.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/toolbar_actions.o: src/toolbar_actions.c $(NDK)/include/astra/toolbar_actions.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/undo.o: src/undo.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

build/m68k/text_model.o: src/text_model.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

build/m68k/scroll.o: src/scroll.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

build/m68k/command.o: src/command.c $(NDK)/include/astra/command.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) \
		-c $< -o $@

build/m68k/menu.o build/m68k/menu_context.o: build/m68k/%.o: src/%.c $(NDK)/include/astra/menu.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) -c $< -o $@

build/m68k/combo_box.o build/m68k/menu_multi.o: build/m68k/%.o: src/%.c $(NDK)/include/astra/choice_popups.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) $(DEPFLAGS) -c $< -o $@

$(INTERFACE_OBJECTS): Makefile

shared-libraries-ready:
	$(MAKE) -C $(GRAPHICS) graphics-library font-library
	$(MAKE) -C $(SYSTEM) library
	$(MAKE) -C $(COMPILER) library

$(INTERFACE_TARGET): $(INTERFACE_OBJECTS) $(INTERFACE_LIBRARY_MAP) \
		$(ASTRA_SHARED_LINK_INPUTS) \
		| shared-libraries-ready
	@mkdir -p $(@D)
	$(CC) $(LIBRARY_FLAGS) $(ASTRA_SHARED_LINK_FLAGS) \
		-Wl,-soname,interface.library.12 \
		-Wl,--version-script=$(INTERFACE_LIBRARY_MAP) \
		-o $@ $(INTERFACE_OBJECTS) \
		-L$(dir $(GRAPHICS_LIBRARY)) -L$(dir $(SYSTEM_LIBRARY)) \
		-L$(dir $(RUNTIME_LIBRARY)) -L$(dir $(COMPILER_LIBRARY)) \
		-Wl,--no-as-needed -Wl,-l:graphics.library.3 \
		-Wl,-l:font.library.5 -Wl,-l:system.library.4 \
		-Wl,-l:runtime.library.1 -Wl,-l:compiler.library.2 \
		$(ASTRA_SHARED_LINK_END)

library-contract: $(INTERFACE_TARGET)
	@python3 ../../../tools/check_shared_library_layout.py $(INTERFACE_TARGET)
	@python3 ../../../tools/check_elf_exports.py --readelf $(READELF) \
		$(INTERFACE_LIBRARY_MAP) $(INTERFACE_TARGET)
	@$(READELF) --dyn-syms -W $(INTERFACE_TARGET) | \
		grep -Eq ' astra_interface_button_init@@ASTRA_INTERFACE_12\.0$$'
	@$(READELF) --dyn-syms -W $(INTERFACE_TARGET) | \
		grep -Eq ' astra_interface_command_invoke@@ASTRA_INTERFACE_12\.0$$'
	@$(READELF) --dyn-syms -W $(INTERFACE_TARGET) | \
		grep -Eq ' astra_interface_list_init@@ASTRA_INTERFACE_12\.0$$'
	@$(READELF) --dyn-syms -W $(INTERFACE_TARGET) | \
		grep -Eq ' astra_interface_text_view_init@@ASTRA_INTERFACE_12\.0$$'
	@$(READELF) --dyn-syms -W $(INTERFACE_TARGET) | \
		grep -Eq ' astra_interface_text_view_refresh@@ASTRA_INTERFACE_12\.0$$'
	@$(READELF) --dyn-syms -W $(INTERFACE_TARGET) | \
		grep -Eq ' astra_interface_settings_pane_init@@ASTRA_INTERFACE_12\.0$$'
	@$(READELF) --dyn-syms -W $(INTERFACE_TARGET) | \
		grep -Eq ' astra_interface_toolbar_init@@ASTRA_INTERFACE_12\.0$$'
	@$(READELF) --dyn-syms -W $(INTERFACE_TARGET) | \
		grep -Eq ' astra_interface_toolbar_set_icons@@ASTRA_INTERFACE_12\.0$$'
	@$(READELF) --dyn-syms -W $(INTERFACE_TARGET) | \
		grep -Eq ' astra_interface_text_view_set_inverted@@ASTRA_INTERFACE_12\.0$$'
	@$(READELF) --dyn-syms -W $(INTERFACE_TARGET) | \
		grep -Eq ' astra_interface_image_view_render@@ASTRA_INTERFACE_12\.0$$'
	@$(READELF) --dyn-syms -W $(INTERFACE_TARGET) | \
		grep -Eq ' astra_interface_text_view_set_wrap_width@@ASTRA_INTERFACE_12\.0$$'
	@! $(READELF) --dyn-syms -W $(INTERFACE_TARGET) | \
		grep -q 'ASTRA_INTERFACE_5\.1'
	@python3 $(SYMBOL_OWNERSHIP_CHECK) --readelf $(READELF) \
		$(INTERFACE_TARGET) $(GRAPHICS_LIBRARY) $(FONT_LIBRARY) \
		$(SYSTEM_LIBRARY) $(RUNTIME_LIBRARY) $(COMPILER_LIBRARY)
	@python3 ../../../tools/library_info.py $(INTERFACE_TARGET) | \
		grep -q '^interface.library.12 12.0.0 ABI 12.0$$'
	@test "$$($(READELF) -dW $(INTERFACE_TARGET) | \
		grep -Fc 'Shared library: [graphics.library.3]')" = 1
	@test "$$($(READELF) -dW $(INTERFACE_TARGET) | \
		grep -Fc 'Shared library: [font.library.5]')" = 1
	@test "$$($(READELF) -dW $(INTERFACE_TARGET) | \
		grep -Fc 'Shared library: [system.library.4]')" = 1
	@test "$$($(READELF) -dW $(INTERFACE_TARGET) | \
		grep -Fc 'Shared library: [runtime.library.1]')" = 1
	@test "$$($(READELF) -dW $(INTERFACE_TARGET) | \
		grep -Fc 'Shared library: [compiler.library.2]')" = 1
	@test "$$($(READELF) -dW $(INTERFACE_TARGET) | \
		awk '/NEEDED/{n++} END{print n+0}')" = 5
	$(call ASTRA_CHECK_DSO,$(INTERFACE_TARGET))

$(INTERFACE_HOST_TEST): src/interface_library.c src/control.c src/display_widgets.c src/field_family.c src/settings_pane.c src/system_panels.c src/file_panel.c src/font_panel.c src/form_row.c src/columns.c src/toolbar.c src/toolbar_actions.c src/menu.c src/icon_tile.c src/text_surface.c src/undo.c \
		src/text_model.c src/scroll.c $(RUNTIME)/src/string.c ../../common/civil.c ../../common/divide.c $(NDK_HOST_UTF8) tests/test_interface.c \
		tests/test_layout.c src/text_surface_internal.h src/panel_internal.h
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -DASTRA_INTERFACE_TEST \
		-DASTRA_SYSTEM_TEST \
		-ffunction-sections -Wl,--gc-sections $(filter %.c %.o,$^) -o $@

$(TEXT_SURFACE_HOST_TEST): src/text_surface.c tests/test_text_surface.c \
		$(NDK_HOST_UTF8) src/text_surface_internal.h
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/text_surface.c $(NDK_HOST_UTF8) tests/test_text_surface.c -o $@

$(UNDO_HOST_TEST): src/undo.c tests/test_undo.c $(NDK_HOST_UTF8)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/undo.c $(NDK_HOST_UTF8) tests/test_undo.c -o $@

$(TEXT_MODEL_HOST_TEST): src/text_model.c tests/test_text_model.c \
		$(NDK_HOST_UTF8)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/text_model.c $(NDK_HOST_UTF8) tests/test_text_model.c -o $@

$(SCROLL_HOST_TEST): src/scroll.c tests/test_scroll.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $^ -o $@

$(COMMAND_HOST_TEST): src/command.c tests/test_command.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $^ -o $@

MENU_TEST_SOURCES := src/menu.c src/menu_context.c src/menu_multi.c src/combo_box.c src/command.c src/control.c src/display_widgets.c src/field_family.c src/toolbar_actions.c src/icon_tile.c src/text_surface.c src/undo.c \
	src/text_model.c src/scroll.c \
	tests/test_menu.c
$(GRAPHICS_HOST_SURFACE) $(GRAPHICS_HOST_SURFACE_SANITIZE): FORCE
	$(MAKE) -C $(GRAPHICS) $(patsubst $(GRAPHICS)/%,%,$@)

$(MENU_HOST_TEST): $(MENU_TEST_SOURCES) $(NDK_HOST_UTF8) $(GRAPHICS_FONT_HEADERS) $(GRAPHICS_HOST_SURFACE) $(FONT_HOST_INPUTS) src/text_surface_internal.h
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) -I$(GRAPHICS)/build/generated $(COMMON_FLAGS) $(HOST_FLAGS) \
		-ffunction-sections -Wl,--gc-sections $(MENU_TEST_SOURCES) $(NDK_HOST_UTF8) $(GRAPHICS_HOST_SURFACE) $(FONT_HOST_INPUTS) -o $@

$(NDK_HOST_UTF8):
	$(MAKE) -C $(SYSTEM) build/host/utf8.o

$(ICON_TILE_HOST_TEST): src/icon_tile.c tests/test_icon_tile.c $(NDK_HOST_UTF8)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$^ -o $@

$(IMAGE_VIEW_HOST_TEST): src/image_view.c tests/test_image_view.c
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$^ -o $@

test: $(INTERFACE_HOST_TEST) $(TEXT_SURFACE_HOST_TEST) \
		$(UNDO_HOST_TEST) $(TEXT_MODEL_HOST_TEST) $(SCROLL_HOST_TEST) \
		$(COMMAND_HOST_TEST) $(MENU_HOST_TEST) $(ICON_TILE_HOST_TEST) \
		$(IMAGE_VIEW_HOST_TEST)
	./$(INTERFACE_HOST_TEST)
	./$(TEXT_SURFACE_HOST_TEST)
	./$(UNDO_HOST_TEST)
	./$(TEXT_MODEL_HOST_TEST)
	./$(SCROLL_HOST_TEST)
	./$(COMMAND_HOST_TEST)
	./$(MENU_HOST_TEST)
	./$(ICON_TILE_HOST_TEST)
	./$(IMAGE_VIEW_HOST_TEST)

sanitize: $(NDK_HOST_UTF8) $(GRAPHICS_FONT_HEADERS) $(GRAPHICS_HOST_SURFACE_SANITIZE)
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) -DASTRA_INTERFACE_TEST -DASTRA_SYSTEM_TEST \
		-ffunction-sections -Wl,--gc-sections src/interface_library.c \
		src/control.c src/display_widgets.c src/field_family.c src/settings_pane.c src/system_panels.c src/file_panel.c src/font_panel.c src/form_row.c src/columns.c src/toolbar.c src/toolbar_actions.c src/menu.c src/icon_tile.c src/text_surface.c src/undo.c src/text_model.c src/scroll.c $(RUNTIME)/src/string.c ../../common/civil.c ../../common/divide.c \
		$(NDK_HOST_UTF8) \
		tests/test_interface.c tests/test_layout.c \
		-o build/host/test_interface_sanitize
	./build/host/test_interface_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/text_surface.c $(NDK_HOST_UTF8) \
		tests/test_text_surface.c -o build/host/test_text_surface_sanitize
	./build/host/test_text_surface_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/undo.c $(NDK_HOST_UTF8) tests/test_undo.c \
		-o build/host/test_undo_sanitize
	./build/host/test_undo_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/text_model.c $(NDK_HOST_UTF8) \
		tests/test_text_model.c -o build/host/test_text_model_sanitize
	./build/host/test_text_model_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/scroll.c tests/test_scroll.c \
		-o build/host/test_scroll_sanitize
	./build/host/test_scroll_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/command.c tests/test_command.c \
		-o build/host/test_command_sanitize
	./build/host/test_command_sanitize

	$(HOST_CC) $(CPPFLAGS) -I$(GRAPHICS)/build/generated $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) -ffunction-sections -Wl,--gc-sections \
		$(MENU_TEST_SOURCES) $(NDK_HOST_UTF8) $(GRAPHICS_HOST_SURFACE_SANITIZE) $(FONT_HOST_INPUTS) -o build/host/test_menu_sanitize

	./build/host/test_menu_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/icon_tile.c tests/test_icon_tile.c \
		$(NDK_HOST_UTF8) -o build/host/test_icon_tile_sanitize
	./build/host/test_icon_tile_sanitize
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) src/image_view.c tests/test_image_view.c \
		-o build/host/test_image_view_sanitize
	./build/host/test_image_view_sanitize

analyze:
	@mkdir -p build/analyzer
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/interface_library.c -o build/analyzer/interface_library.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/control.c -o build/analyzer/control.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) -fanalyzer -c src/display_widgets.c -o build/analyzer/display_widgets.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/settings_pane.c -o build/analyzer/settings_pane.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) -fanalyzer \
		-c src/form_row.c -o build/analyzer/form_row.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) -fanalyzer \
		-c src/columns.c -o build/analyzer/columns.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/icon_tile.c -o build/analyzer/icon_tile.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/image_view.c -o build/analyzer/image_view.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/inline_edit.c -o build/analyzer/inline_edit.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/text_surface.c -o build/analyzer/text_surface.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/undo.c -o build/analyzer/undo.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/text_model.c -o build/analyzer/text_model.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/scroll.c -o build/analyzer/scroll.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/command.c -o build/analyzer/command.o

	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/menu.c -o build/analyzer/menu.o
	$(ANALYZER_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) -fanalyzer \
		-c src/menu_context.c -o build/analyzer/menu_context.o

clean:
	rm -rf build

$(INTERFACE_HOST_TEST) $(TEXT_SURFACE_HOST_TEST): ../../include/astra/json.h

-include $(wildcard build/m68k/*.d)

.PHONY: all interface-library test sanitize analyze library-contract clean \
	shared-libraries-ready

FORCE:
.PHONY: FORCE
