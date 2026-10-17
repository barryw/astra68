#ifndef ASTRA_FONT_DATA_H
#define ASTRA_FONT_DATA_H

#include <astra/font_library.h>

typedef struct AstraFontBank {
    const uint32_t *codepoints;
    const uint32_t *glyph_ids;
    uint32_t cmap_count;
    uint32_t replacement_glyph;
    const AstraUiStrike *strikes;
    uint32_t strike_count;
    const AstraUiGlyph *glyphs;
    const uint8_t *bitmap;
} AstraFontBank;

extern const AstraFontBank astra_ui_font_bank;
extern const AstraFontBank astra_mono_font_bank;
const AstraUiStrike *astra_font_bank_strike(const AstraFontBank *font,
    uint16_t pixel_height, uint32_t style_flags);
const AstraUiGlyph *astra_font_bank_glyph(const AstraFontBank *font,
    const AstraUiStrike *strike, uint32_t scalar);

#endif
