#ifndef ASTRA_THEME_H
#define ASTRA_THEME_H

/**
 * @file theme.h
 * @brief Shared semantic appearance of Astra user-interface components.
 */

#include <stdint.h>

#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/** Version of the immutable system theme snapshot. */
#define ASTRA_THEME_GENERATION UINT32_C(5)

/** Native strike sizes selected by the immutable Astra 0.1 theme. */
#define ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT UINT16_C(11)
#define ASTRA_THEME_SYSTEM_TITLE_FONT_HEIGHT UINT16_C(13)
#define ASTRA_THEME_SYSTEM_MONO_FONT_HEIGHT UINT16_C(16)
#define ASTRA_THEME_SYSTEM_MONO_CELL_WIDTH UINT16_C(8)

/**
 * Semantic colors and geometry shared by applications and the window server.
 * Applications copy this value and use its roles instead of embedding colors
 * or control geometry in individual windows.
 */
typedef struct AstraTheme {
    uint32_t size;
    uint32_t generation;
    AstraColorRGBA8 canvas;
    AstraColorRGBA8 system_bar;
    AstraColorRGBA8 frame;
    AstraColorRGBA8 title_active;
    AstraColorRGBA8 title_inactive;
    AstraColorRGBA8 client;
    AstraColorRGBA8 control;
    AstraColorRGBA8 control_hover;
    AstraColorRGBA8 control_pressed;
    AstraColorRGBA8 text_primary;
    AstraColorRGBA8 text_secondary;
    AstraColorRGBA8 text_tertiary;
    AstraColorRGBA8 text_muted;
    AstraColorRGBA8 client_text;
    AstraColorRGBA8 client_text_secondary;
    AstraColorRGBA8 surface_raised;
    AstraColorRGBA8 surface_inset;
    AstraColorRGBA8 border_soft;
    AstraColorRGBA8 border_emphasis;
    AstraColorRGBA8 client_border;
    AstraColorRGBA8 accent;
    AstraColorRGBA8 accent_text;
    AstraColorRGBA8 warning;
    AstraColorRGBA8 fault;
    AstraColorRGBA8 control_disabled;
    AstraColorRGBA8 control_selected;
    AstraColorRGBA8 control_error;
    AstraColorRGBA8 control_focus;
    uint16_t scale;
    uint16_t body_font_height;
    uint16_t title_font_height;
    uint16_t mono_font_height;
    uint16_t mono_cell_width;
    uint16_t control_font_height;
    uint16_t spacing_unit;
    uint16_t window_radius;
    uint16_t frame_width;
    uint16_t titlebar_height;
    uint16_t utility_titlebar_height;
    uint16_t signal_height;
    uint16_t gadget_extent;
    uint16_t gadget_glyph;
    uint16_t control_radius;
    uint16_t card_radius;
    uint16_t resize_hit;
    uint16_t control_height;
    uint16_t control_padding_x;
    uint16_t control_border_width;
    uint16_t focus_width;
    uint32_t reserved[4];
} AstraTheme;

/** The sole Astra 0.1 theme. Runtime theme selection is not yet exposed. */
#define ASTRA_THEME_SYSTEM_INIT { \
    sizeof(AstraTheme), ASTRA_THEME_GENERATION, \
    { 22, 45, 58, 255 }, { 9, 16, 23, 255 }, \
    { 3, 6, 9, 255 }, { 48, 63, 72, 255 }, \
    { 25, 34, 42, 255 }, { 236, 239, 240, 255 }, \
    { 70, 79, 86, 255 }, { 92, 104, 112, 255 }, \
    { 45, 54, 61, 255 }, { 239, 244, 246, 255 }, \
    { 201, 211, 216, 255 }, { 151, 163, 169, 255 }, \
    { 111, 126, 139, 255 }, { 26, 36, 43, 255 }, \
    { 92, 104, 112, 255 }, { 247, 249, 249, 255 }, \
    { 227, 231, 233, 255 }, { 38, 48, 59, 255 }, \
    { 45, 54, 61, 255 }, { 214, 220, 223, 255 }, \
    { 45, 174, 184, 255 }, { 6, 34, 39, 255 }, \
    { 211, 154, 57, 255 }, { 177, 73, 73, 255 }, \
    { 214, 220, 223, 255 }, { 45, 174, 184, 255 }, \
    { 177, 73, 73, 255 }, { 45, 174, 184, 255 }, \
    1, \
    ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT, \
    ASTRA_THEME_SYSTEM_TITLE_FONT_HEIGHT, \
    ASTRA_THEME_SYSTEM_MONO_FONT_HEIGHT, \
    ASTRA_THEME_SYSTEM_MONO_CELL_WIDTH, \
    ASTRA_THEME_SYSTEM_TITLE_FONT_HEIGHT, \
    4, 12, 2, 26, 22, 2, 20, 10, 8, 8, 6, 28, 14, 1, 2, \
    { 0, 0, 0, 0 } \
}

ASTRA_EXTERN_C_END

#endif
