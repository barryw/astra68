/** @file font_library.h @brief Font Kit direct-symbol API. */
#ifndef ASTRA_FONT_LIBRARY_H
#define ASTRA_FONT_LIBRARY_H

#include <stdint.h>

#include <astra/font.h>
#include <astra/surface.h>

ASTRA_EXTERN_C_BEGIN

/** Breaking Font Kit ABI generation carried by font.library.2. */
#define ASTRA_FONT_LIBRARY_ABI_MAJOR 2u
/** Current backward-compatible Font Kit revision. */
#define ASTRA_FONT_LIBRARY_ABI_MINOR 2u

/**
 * Immutable metrics and glyph range for one built-in bitmap-font strike.
 * Distances are signed 26.6 fixed-point values unless documented otherwise.
 */
typedef struct AstraUiStrike {
    /** Nominal strike width in pixels. */
    uint16_t pixel_width;
    /** Nominal strike height in pixels. */
    uint16_t pixel_height;
    /** One `ASTRA_FONT_BITMAP_*` value. */
    uint16_t bitmap_format;
    /** Reserved; currently zero. */
    uint16_t reserved16;
    /** Positive baseline-to-ascent distance. */
    int32_t ascent;
    /** Positive baseline-to-descent distance. */
    int32_t descent;
    /** Recommended space between lines. */
    int32_t line_gap;
    /** Baseline-to-cap-height distance. */
    int32_t cap_height;
    /** Baseline-to-x-height distance. */
    int32_t x_height;
    /** Maximum horizontal advance. */
    int32_t max_advance;
    /** Signed underline offset from the baseline. */
    int32_t underline_position;
    /** Recommended underline thickness. */
    int32_t underline_thickness;
    /** Signed strikeout offset from the baseline. */
    int32_t strikeout_position;
    /** Recommended strikeout thickness. */
    int32_t strikeout_thickness;
    /** First glyph record belonging to this strike. */
    uint32_t glyph_first;
    /** Number of glyph records belonging to this strike. */
    uint32_t glyph_count;
} AstraUiStrike;

/** Immutable raster record for one glyph in a built-in font strike. */
typedef struct AstraUiGlyph {
    /** Byte offset of the glyph raster in its font bitmap. */
    uint32_t bitmap_offset;
    /** Number of raster bytes. */
    uint32_t bitmap_length;
    /** Raster width in pixels. */
    uint16_t width;
    /** Raster height in pixels. */
    uint16_t height;
    /** Raster row pitch in bytes. */
    uint16_t pitch;
    /** Horizontal bearing in signed 26.6 fixed point. */
    int32_t bearing_x;
    /** Vertical bearing in signed 26.6 fixed point. */
    int32_t bearing_y;
    /** Horizontal advance in signed 26.6 fixed point. */
    int32_t advance_x;
} AstraUiGlyph;

/** Return the nearest built-in UI strike for a pixel height. @param pixel_height Requested pixel height. @return Immutable strike, or NULL when unavailable. */
const AstraUiStrike *astra_ui_font_strike(uint16_t pixel_height);
/** Decode one scalar from a UTF-8 byte span. @param text UTF-8 bytes. @param length Available bytes. @param consumed Receives bytes consumed. @return Decoded scalar or U+FFFD. */
uint32_t astra_ui_font_scalar(const char *text, uint32_t length,
                              uint32_t *consumed);
/** Resolve a Unicode scalar in a built-in UI strike. @param strike Strike returned by astra_ui_font_strike(). @param scalar Unicode scalar. @return Immutable glyph, using the replacement glyph when absent. */
const AstraUiGlyph *astra_ui_font_glyph(const AstraUiStrike *strike,
                                        uint32_t scalar);
/** Return the raster bytes for a built-in UI glyph. @param glyph Glyph returned by astra_ui_font_glyph(). @return Immutable raster bytes, or NULL for NULL. */
const uint8_t *astra_ui_font_bitmap(const AstraUiGlyph *glyph);

/** Return the nearest built-in monospaced strike for a pixel height. @param pixel_height Requested pixel height. @return Immutable strike, or NULL when unavailable. */
const AstraUiStrike *astra_mono_font_strike(uint16_t pixel_height);
/** Resolve a Unicode scalar in a built-in monospaced strike. @param strike Strike returned by astra_mono_font_strike(). @param scalar Unicode scalar. @return Immutable glyph, using the replacement glyph when absent. */
const AstraUiGlyph *astra_mono_font_glyph(const AstraUiStrike *strike,
                                          uint32_t scalar);
/** Return the raster bytes for a built-in monospaced glyph. @param glyph Glyph returned by astra_mono_font_glyph(). @return Immutable raster bytes, or NULL for NULL. */
const uint8_t *astra_mono_font_bitmap(const AstraUiGlyph *glyph);

ASTRA_EXTERN_C_END

#endif
