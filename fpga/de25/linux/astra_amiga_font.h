#ifndef ASTRA_AMIGA_FONT_H
#define ASTRA_AMIGA_FONT_H

#include <stdint.h>

/* Borrowed bitmap plus copied, bounded character records. No hunk is loaded
 * or relocated; source bytes must outlive this view. */
typedef struct AstraAmigaFont {
    const uint8_t *bitmap;
    uint32_t bitmap_bytes;
    uint16_t modulo;
    uint16_t height;
    uint16_t baseline;
    uint16_t nominal_width;
    uint16_t revision;
    uint8_t style;
    uint8_t flags;
    uint8_t first_code;
    uint8_t last_code;
    uint16_t glyph_count;
    char name[33];
    uint16_t location[257];
    uint16_t width[257];
    int16_t spacing[257];
    int16_t kerning[257];
} AstraAmigaFont;

/* Accept one classic monochrome disk-font hunk. Color and reverse-path
 * fonts require their own conversion contract and are rejected. */
int astra_amiga_font_parse(const void *source, uint32_t length,
                           AstraAmigaFont *font);

#endif
