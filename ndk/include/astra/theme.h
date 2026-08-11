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
#define ASTRA_THEME_GENERATION UINT32_C(3)

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
    AstraColorRGBA8 text_muted;
    AstraColorRGBA8 accent;
    AstraColorRGBA8 warning;
    AstraColorRGBA8 fault;
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
    uint16_t reserved16;
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
    { 151, 163, 169, 255 }, { 45, 174, 184, 255 }, \
    { 211, 154, 57, 255 }, { 177, 73, 73, 255 }, \
    4, 12, 2, 26, 22, 2, 20, 10, 8, 8, 6, 0, { 0, 0, 0, 0 } \
}

ASTRA_EXTERN_C_END

#endif
