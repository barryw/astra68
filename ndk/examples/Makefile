# Builds every example as a program from an installed NDK:
#   make -C examples ASTRA_NDK_ROOT=/path/to/astra68-ndk-<version>
# Each example links the libraries it calls and nothing else.
EXAMPLES_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
ASTRA_NDK_ROOT ?= $(abspath $(EXAMPLES_DIR)..)
BUILD_DIR ?= build

include $(ASTRA_NDK_ROOT)/make/astra-native.mk

comma := ,
STREAMS := -Wl,-l:streams.library.1
SYSTEM := -Wl,-l:system.library.3
INTERFACE := -Wl,-l:interface.library.5
PCM := -Wl,-l:pcm.library.2

hello_LIBS := $(STREAMS)
timer_LIBS := $(STREAMS)
port_message_LIBS := $(STREAMS) $(SYSTEM)
bulk_ring_LIBS := $(STREAMS) $(SYSTEM)
undo_LIBS := $(STREAMS) $(INTERFACE)
text_field_LIBS := $(STREAMS) $(INTERFACE)
text_clipboard_LIBS := $(STREAMS) $(SYSTEM) $(INTERFACE)
interface_controls_LIBS := $(STREAMS) $(INTERFACE)
interface_tabs_LIBS := $(STREAMS) $(INTERFACE)
interface_scroll_LIBS := $(STREAMS) $(INTERFACE)
interface_splitter_LIBS := $(STREAMS) $(INTERFACE)
interface_disclosure_LIBS := $(STREAMS) $(INTERFACE)
pcm_tone_LIBS := $(STREAMS) $(PCM)
midi_notes_LIBS := $(STREAMS) $(PCM)

EXAMPLES := $(patsubst %.c,%,$(notdir $(wildcard $(EXAMPLES_DIR)*.c)))
PROGRAMS := $(addprefix $(BUILD_DIR)/,$(addsuffix .elf,$(EXAMPLES)))

all: $(PROGRAMS)

$(BUILD_DIR)/%.elf: $(EXAMPLES_DIR)%.c
	@mkdir -p $(@D)
	@test -n "$($*_LIBS)" || { echo "examples: no libraries for $*" >&2; \
		exit 1; }
	$(ASTRA_CC) $(ASTRA_CPPFLAGS) $(ASTRA_CFLAGS) -std=c11 -Wall -Wextra \
		-Werror $(ASTRA_LDFLAGS) -o $@ $(ASTRA_CRT0) $< $($*_LIBS) \
		$(ASTRA_NATIVE_LIBS)
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(ASTRA_READELF) \
		$(patsubst -Wl$(comma)-l:%,--needed %,$($*_LIBS)) \
		--needed runtime.library.1 --needed compiler.library.2 $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
