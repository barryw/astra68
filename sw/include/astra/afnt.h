#ifndef ASTRA_AFNT_H
#define ASTRA_AFNT_H

#include <stdint.h>

/* Includes multilingual source notices and their exact encoded bytes. */
#define ASTRA_AFNT_NAME_MAX (1024u * 1024u)

/* AFNT 0.2 is a passive file format. Views borrow the caller's byte buffer. */
typedef struct AstraAfntChunk {
    const uint8_t *bytes;
    uint32_t length;
} AstraAfntChunk;

typedef struct AstraAfntView {
    AstraAfntChunk name;
    AstraAfntChunk cmap;
    AstraAfntChunk strikes;
    AstraAfntChunk glyphs;
    AstraAfntChunk bitmap;
    uint32_t face_glyphs;
    uint32_t strike_count;
} AstraAfntView;

typedef struct AstraAfntStrikeInfo {
    uint16_t pixel_width;
    uint16_t pixel_height;
    uint16_t bitmap_format;
    int32_t ascent;
    int32_t descent;
    int32_t line_gap;
    int32_t cap_height;
    int32_t x_height;
    int32_t max_advance;
    int32_t underline_position;
    int32_t underline_thickness;
    int32_t strikeout_position;
    int32_t strikeout_thickness;
} AstraAfntStrikeInfo;

typedef struct AstraAfntGlyphInfo {
    const uint8_t *bitmap; /* Borrows the validated AFNT buffer. */
    uint32_t bitmap_length;
    uint16_t width;
    uint16_t height;
    uint16_t pitch;
    int32_t bearing_x;
    int32_t bearing_y;
    int32_t advance_x;
} AstraAfntGlyphInfo;

enum {
    ASTRA_AFNT_NAME = 0,
    ASTRA_AFNT_CMAP,
    ASTRA_AFNT_STRIKES,
    ASTRA_AFNT_GLYPHS,
    ASTRA_AFNT_BITMAP,
    ASTRA_AFNT_CHUNK_COUNT
};

/* Rejects malformed offsets, checksums, maps, metrics, and bitmap ranges. */
int astra_afnt_validate(const void *bytes, uint32_t length, AstraAfntView *view);
/* Read a top-level NAME string such as "family" or "style" into UTF-8. */
int astra_afnt_name_string(const AstraAfntView *view, const char *key,
                           char *output, uint32_t capacity);
/* Read one validated strike without exposing disk record layout to clients. */
int astra_afnt_strike_info(const AstraAfntView *view, uint32_t index,
                           AstraAfntStrikeInfo *info);
/* Exact CMAP coverage; never treats the replacement glyph as a match. */
int astra_afnt_has_scalar(const AstraAfntView *view, uint32_t scalar);
/* Resolve a Unicode scalar in one validated strike, using U+FFFD if absent. */
int astra_afnt_glyph_info(const AstraAfntView *view, uint32_t strike_index,
                          uint32_t scalar, AstraAfntGlyphInfo *info);
/* Packs chunks in canonical order. NULL output queries required length. */
int astra_afnt_pack(const AstraAfntChunk chunks[ASTRA_AFNT_CHUNK_COUNT],
                    void *output, uint32_t capacity, uint32_t *length);

#endif
