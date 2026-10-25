#include "font_data.h"
#include <astra/utf8.h>
#include <stddef.h>

#include "astra_ui_font.inc"
#include "astra_mono_font.inc"

#define ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))

const AstraFontBank astra_ui_font_bank = {
    astra_ui_cmap_codepoints, astra_ui_cmap_glyphs,
    ARRAY_COUNT(astra_ui_cmap_codepoints), astra_ui_replacement_glyph,
    astra_ui_strikes,
    ARRAY_COUNT(astra_ui_strikes), astra_ui_glyphs, astra_ui_bitmap
};

const AstraFontBank astra_mono_font_bank = {
    astra_mono_cmap_codepoints, astra_mono_cmap_glyphs,
    ARRAY_COUNT(astra_mono_cmap_codepoints), astra_mono_replacement_glyph,
    astra_mono_strikes,
    ARRAY_COUNT(astra_mono_strikes), astra_mono_glyphs, astra_mono_bitmap
};

static const AstraUiStrike *font_strike(const AstraFontBank *font,
                                        uint16_t pixel_height)
{
    for (uint32_t index = 0u; index < font->strike_count / 4u; ++index)
        if (font->strikes[index].pixel_height == pixel_height)
            return &font->strikes[index];
    return NULL;
}

const AstraUiStrike *astra_font_bank_strike(const AstraFontBank *font,
                                               uint16_t pixel_height,
                                               uint32_t style_flags)
{
    const AstraUiStrike *regular = font_strike(font, pixel_height);
    uint32_t style =
        ((style_flags & ASTRA_TEXT_STYLE_BOLD) != 0u ? 1u : 0u) |
        ((style_flags & ASTRA_TEXT_STYLE_ITALIC) != 0u ? 2u : 0u);

    return regular == NULL ? NULL :
        regular + style * (font->strike_count / 4u);
}

const AstraUiStrike *astra_ui_font_strike(uint16_t pixel_height)
{
    return font_strike(&astra_ui_font_bank, pixel_height);
}

const AstraUiStrike *astra_ui_font_strike_styled(uint16_t pixel_height,
                                                 uint32_t style_flags)
{
    return astra_font_bank_strike(&astra_ui_font_bank, pixel_height, style_flags);
}

const AstraUiStrike *astra_mono_font_strike(uint16_t pixel_height)
{
    return font_strike(&astra_mono_font_bank, pixel_height);
}

const AstraUiStrike *astra_mono_font_strike_styled(uint16_t pixel_height,
                                                   uint32_t style_flags)
{
    return astra_font_bank_strike(&astra_mono_font_bank, pixel_height, style_flags);
}

uint32_t astra_ui_font_scalar(const char *text, uint32_t length,
                              uint32_t *consumed)
{
    return astra_utf8_decode(text, length, consumed);
}

static uint32_t font_glyph_id(const AstraFontBank *font, uint32_t scalar)
{
    uint32_t low = 0u;
    uint32_t high = font->cmap_count;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        uint32_t candidate = font->codepoints[middle];

        if (candidate < scalar)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low < font->cmap_count && font->codepoints[low] == scalar)
        return font->glyph_ids[low];
    return font->replacement_glyph;
}

const AstraUiGlyph *astra_font_bank_glyph(const AstraFontBank *font,
                                      const AstraUiStrike *strike,
                                      uint32_t scalar)
{
    uint32_t glyph_id = font_glyph_id(font, scalar);

    if (glyph_id >= strike->glyph_count)
        glyph_id = strike->glyph_count - 1u;
    return &font->glyphs[strike->glyph_first + glyph_id];
}

const AstraUiGlyph *astra_ui_font_glyph(const AstraUiStrike *strike,
                                        uint32_t scalar)
{
    return astra_font_bank_glyph(&astra_ui_font_bank, strike, scalar);
}

const AstraUiGlyph *astra_mono_font_glyph(const AstraUiStrike *strike,
                                          uint32_t scalar)
{
    return astra_font_bank_glyph(&astra_mono_font_bank, strike, scalar);
}

const uint8_t *astra_ui_font_bitmap(const AstraUiGlyph *glyph)
{
    return glyph == NULL ? NULL : &astra_ui_bitmap[glyph->bitmap_offset];
}

const uint8_t *astra_mono_font_bitmap(const AstraUiGlyph *glyph)
{
    return glyph == NULL ? NULL : &astra_mono_bitmap[glyph->bitmap_offset];
}
