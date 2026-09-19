#ifndef ASTRA_USERSPACE_UI_FONT_H
#define ASTRA_USERSPACE_UI_FONT_H

#include <stdint.h>

#include <astra/font_library.h>
#include <astra/text_style.h>

static inline int32_t astra_ui_fixed_floor(int32_t value)
{
    int32_t pixels = value / 64;

    return pixels - (value < 0 && value % 64 != 0);
}

static inline int32_t astra_ui_glyph_x(int32_t pen_26_6,
                                       const AstraUiGlyph *glyph)
{
    return astra_ui_fixed_floor(pen_26_6 + glyph->bearing_x);
}

static inline int32_t astra_ui_glyph_y(int32_t baseline_26_6,
                                       const AstraUiGlyph *glyph)
{
    return astra_ui_fixed_floor(baseline_26_6 - glyph->bearing_y);
}

static inline int32_t astra_ui_glyph_advance(const AstraUiGlyph *glyph,
                                              uint16_t cell_width)
{
    return cell_width != 0u ? (int32_t)cell_width * 64 : glyph->advance_x;
}

static inline uint32_t astra_ui_bold_strength(uint16_t pixel_height,
                                              uint32_t style_flags)
{
    return (style_flags & ASTRA_TEXT_STYLE_BOLD) == 0u ? 0u :
           ((uint32_t)pixel_height + 15u) / 16u;
}

static inline int32_t astra_ui_italic_shift(const AstraUiGlyph *glyph,
                                            uint32_t row)
{
    int64_t vertical_26_6 =
        (int64_t)glyph->bearing_y - (int64_t)row * 64;

    return astra_ui_fixed_floor((int32_t)(vertical_26_6 * 12 / 64));
}

static inline void astra_ui_style_extents(const AstraUiGlyph *glyph,
                                          uint32_t style_flags,
                                          uint32_t embolden,
                                          int32_t *minimum_shift,
                                          uint32_t *width)
{
    int32_t minimum = 0;
    int32_t maximum = 0;

    if ((style_flags & ASTRA_TEXT_STYLE_ITALIC) != 0u)
        for (uint32_t row = 0u; row < glyph->height; ++row) {
            int32_t shift = astra_ui_italic_shift(glyph, row);

            if (shift < minimum) minimum = shift;
            if (shift > maximum) maximum = shift;
        }
    *minimum_shift = minimum;
    *width = glyph->width + (uint32_t)(maximum - minimum) + embolden;
}

#endif
