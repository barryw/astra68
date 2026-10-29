#ifndef ASTRA_SYSTEM_PANELS_H
#define ASTRA_SYSTEM_PANELS_H

/** @file system_panels.h @brief Shared, retained system panels. */

#include <astra/control.h>

ASTRA_EXTERN_C_BEGIN

enum {
    ASTRA_COLOUR_PANEL_RGB565 = 1u,
    ASTRA_COLOUR_PANEL_XRGB8888 = 2u,
    ASTRA_COLOUR_PANEL_INDEX8 = 3u
};

enum {
    ASTRA_COLOUR_PANEL_ROOT = 0u,
    ASTRA_COLOUR_PANEL_TITLE,
    ASTRA_COLOUR_PANEL_SWATCH,
    ASTRA_COLOUR_PANEL_RED_LABEL,
    ASTRA_COLOUR_PANEL_RED,
    ASTRA_COLOUR_PANEL_GREEN_LABEL,
    ASTRA_COLOUR_PANEL_GREEN,
    ASTRA_COLOUR_PANEL_BLUE_LABEL,
    ASTRA_COLOUR_PANEL_BLUE,
    ASTRA_COLOUR_PANEL_ACTIONS,
    ASTRA_COLOUR_PANEL_CANCEL,
    ASTRA_COLOUR_PANEL_USE,
    ASTRA_COLOUR_PANEL_CONTROL_COUNT
};

enum {
    ASTRA_COLOUR_PANEL_NONE = 0u,
    ASTRA_COLOUR_PANEL_CHANGED,
    ASTRA_COLOUR_PANEL_CANCELLED,
    ASTRA_COLOUR_PANEL_ACCEPTED
};

/** Caller-owned panel state. The palette remains borrowed until the panel
 * closes. packed holds RGB565, 0x00RRGGBB, or an 8-bit palette index. */
typedef struct AstraColourPanel {
    uint32_t size;
    uint32_t first_id;
    uint32_t format;
    uint32_t packed;
    const AstraColorRGBA8 *palette;
    uint32_t palette_count;
    uint32_t reserved[4];
} AstraColourPanel;

#define ASTRA_COLOUR_PANEL_INIT { \
    sizeof(AstraColourPanel), 0u, 0u, 0u, 0, 0u, { 0u, 0u, 0u, 0u } \
}

/** Compose the Colour panel from ordinary Container, Label, Colour Well,
 * Slider, and Button controls. All 12 slots and IDs are caller-owned.
 * INDEX8 requires a borrowed palette of 1..256 opaque entries. */
AstraResult astra_interface_colour_panel_init(AstraColourPanel *panel,
    AstraControl *controls, uint32_t first_id, uint32_t format,
    uint32_t initial_packed, const AstraColorRGBA8 *palette,
    uint32_t palette_count);

/** Apply one ordinary UI action; returns semantic panel outcome. On ACCEPTED
 * read panel->packed. A VALUE_CHANGED action refreshes the swatch. */
AstraResult astra_interface_colour_panel_action(AstraColourPanel *panel,
    AstraUIContext *context, AstraControl *controls,
    const AstraUIAction *action, uint32_t *outcome);

/** Resolve the selected encoded color to opaque RGB for preview/use. */
AstraResult astra_interface_colour_panel_color(
    const AstraColourPanel *panel, AstraColorRGBA8 *color);

ASTRA_EXTERN_C_END

#endif
