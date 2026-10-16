#ifndef ASTRA_AFNT_H
#define ASTRA_AFNT_H

#include <stdint.h>

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
/* Packs chunks in canonical order. NULL output queries required length. */
int astra_afnt_pack(const AstraAfntChunk chunks[ASTRA_AFNT_CHUNK_COUNT],
                    void *output, uint32_t capacity, uint32_t *length);

#endif
