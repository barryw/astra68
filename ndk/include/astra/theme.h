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
/** Native system-title font height in pixels. */
#define ASTRA_THEME_SYSTEM_TITLE_FONT_HEIGHT UINT16_C(13)
/** Native terminal/code font height in pixels. */
#define ASTRA_THEME_SYSTEM_MONO_FONT_HEIGHT UINT16_C(16)
/** Native terminal/code cell width in pixels. */
#define ASTRA_THEME_SYSTEM_MONO_CELL_WIDTH UINT16_C(8)

/**
 * Semantic colors and geometry shared by applications and the window server.
 * Applications copy this value and use its roles instead of embedding colors
 * or control geometry in individual windows.
 */
typedef struct AstraTheme {
    uint32_t size; /**< Caller-visible structure size. */
    uint32_t generation; /**< Theme generation identifier. */
    AstraColorRGBA8 canvas; /**< Desktop canvas. */
    AstraColorRGBA8 system_bar; /**< System bar background. */
    AstraColorRGBA8 frame; /**< Window frame. */
    AstraColorRGBA8 title_active; /**< Active title bar. */
    AstraColorRGBA8 title_inactive; /**< Inactive title bar. */
    AstraColorRGBA8 client; /**< Window client background. */
    AstraColorRGBA8 control; /**< Normal control surface. */
    AstraColorRGBA8 control_hover; /**< Hovered control surface. */
    AstraColorRGBA8 control_pressed; /**< Pressed control surface. */
    AstraColorRGBA8 text_primary; /**< Primary text. */
    AstraColorRGBA8 text_secondary; /**< Secondary text. */
    AstraColorRGBA8 text_tertiary; /**< Tertiary text. */
    AstraColorRGBA8 text_muted; /**< Muted text. */
    AstraColorRGBA8 client_text; /**< Primary client-area text. */
    AstraColorRGBA8 client_text_secondary; /**< Secondary client text. */
    AstraColorRGBA8 surface_raised; /**< Raised surface. */
    AstraColorRGBA8 surface_inset; /**< Inset surface. */
    AstraColorRGBA8 border_soft; /**< Low-emphasis border. */
    AstraColorRGBA8 border_emphasis; /**< High-emphasis border. */
    AstraColorRGBA8 client_border; /**< Client-area border. */
    AstraColorRGBA8 accent; /**< Accent and selection fill. */
    AstraColorRGBA8 accent_text; /**< Text displayed over accent. */
    AstraColorRGBA8 warning; /**< Warning state. */
    AstraColorRGBA8 fault; /**< Fault state. */
    AstraColorRGBA8 control_disabled; /**< Disabled control. */
    AstraColorRGBA8 control_selected; /**< Selected control. */
    AstraColorRGBA8 control_error; /**< Invalid control. */
    AstraColorRGBA8 control_focus; /**< Keyboard focus indicator. */
    uint16_t scale; /**< Logical UI scale. */
    uint16_t body_font_height; /**< Body font pixel height. */
    uint16_t title_font_height; /**< Window-title font pixel height. */
    uint16_t mono_font_height; /**< Monospace font pixel height. */
    uint16_t mono_cell_width; /**< Monospace cell pixel width. */
    uint16_t control_font_height; /**< Control-label font pixel height. */
    uint16_t spacing_unit; /**< Base layout spacing unit. */
    uint16_t window_radius; /**< Window corner radius. */
    uint16_t frame_width; /**< Window frame width. */
    uint16_t titlebar_height; /**< Standard title-bar height. */
    uint16_t utility_titlebar_height; /**< Utility title-bar height. */
    uint16_t signal_height; /**< Active-window signal height. */
    uint16_t gadget_extent; /**< Window gadget hit-box extent. */
    uint16_t gadget_glyph; /**< Window gadget glyph extent. */
    uint16_t control_radius; /**< Control corner radius. */
    uint16_t card_radius; /**< Card corner radius. */
    uint16_t resize_hit; /**< Resize-edge hit width. */
    uint16_t control_height; /**< Standard control height. */
    uint16_t control_padding_x; /**< Horizontal control padding. */
    uint16_t control_border_width; /**< Control border width. */
    uint16_t focus_width; /**< Keyboard focus-ring width. */
    uint32_t reserved[4]; /**< Must be zero. */
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
