#ifndef ASTRA_USERSPACE_UI_FONT_H
#define ASTRA_USERSPACE_UI_FONT_H

#include <stdint.h>

typedef struct AstraUiStrike {
    uint16_t height;
    uint16_t ascent;
    uint16_t descent;
    uint16_t leading;
    uint32_t glyph_first;
    uint32_t glyph_count;
} AstraUiStrike;

typedef struct AstraUiGlyph {
    uint32_t bitmap_offset;
    uint32_t bitmap_length;
    uint16_t width;
    uint16_t height;
    uint16_t pitch;
    int32_t bearing_x;
    int32_t bearing_y;
    int32_t advance_x;
} AstraUiGlyph;

const AstraUiStrike *astra_ui_font_strike(uint16_t pixel_height);
uint32_t astra_ui_font_scalar(const char *text, uint32_t length,
                              uint32_t *consumed);
const AstraUiGlyph *astra_ui_font_glyph(const AstraUiStrike *strike,
                                        uint32_t scalar);
const uint8_t *astra_ui_font_bitmap(const AstraUiGlyph *glyph);

const AstraUiStrike *astra_mono_font_strike(uint16_t pixel_height);
const AstraUiGlyph *astra_mono_font_glyph(const AstraUiStrike *strike,
                                          uint32_t scalar);
const uint8_t *astra_mono_font_bitmap(const AstraUiGlyph *glyph);

#endif
