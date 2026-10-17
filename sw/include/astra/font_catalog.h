#ifndef ASTRA_FONT_CATALOG_H
#define ASTRA_FONT_CATALOG_H

#include <stdint.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

#define ASTRA_FONT_CATALOG_MAGIC UINT32_C(0x4146434c) /* AFCL */
#define ASTRA_FONT_CATALOG_VERSION UINT16_C(3)

#define ASTRA_FONT_CATALOG_SYSTEM (1u << 0)
#define ASTRA_FONT_CATALOG_SELECTABLE (1u << 1)
#define ASTRA_FONT_CATALOG_BASIC_TEXT (1u << 2)

typedef struct AstraFontCatalogHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t total_size;
    uint32_t generation;
    uint32_t face_count;
    uint32_t records_offset;
    uint32_t reserved[2];
} AstraFontCatalogHeader;

typedef struct AstraFontCatalogRecord {
    uint32_t id_offset;
    uint32_t id_length;
    uint32_t family_offset;
    uint32_t family_length;
    uint32_t style_offset;
    uint32_t style_length;
    uint32_t strikes_offset;
    uint32_t strike_count;
    uint32_t flags;
    uint32_t reserved;
} AstraFontCatalogRecord;

typedef struct AstraFontCatalogStrike {
    uint16_t pixel_width;
    uint16_t pixel_height;
    uint16_t bitmap_format;
    uint16_t reserved;
} AstraFontCatalogStrike;

typedef struct AstraFontCatalogFace {
    const char *id;
    uint32_t id_length;
    const char *family;
    uint32_t family_length;
    const char *style;
    uint32_t style_length;
    const AstraFontCatalogStrike *strikes;
    uint32_t strike_count;
    uint32_t flags;
} AstraFontCatalogFace;

_Static_assert(sizeof(AstraFontCatalogHeader) == 32u, "font catalog header ABI");
_Static_assert(sizeof(AstraFontCatalogRecord) == 40u, "font catalog record ABI");
_Static_assert(sizeof(AstraFontCatalogStrike) == 8u, "font catalog strike ABI");

/* Caller owns input strings and strikes. NULL output queries exact size. */
int astra_font_catalog_pack(const AstraFontCatalogFace *faces, uint32_t count,
    uint32_t generation, void *output, uint32_t capacity, uint32_t *length);
/* Reject malformed offsets, names, strike records, and unknown flags. */
int astra_font_catalog_validate(const void *bytes, uint32_t length);
/* Borrow one face from an already validated immutable document. */
int astra_font_catalog_face(const void *bytes, uint32_t length,
    uint32_t index, AstraFontCatalogFace *face);
/* Validate AFNT and copy one face's catalog metadata into caller storage. */
int astra_font_catalog_face_from_afnt(
    const void *afnt, uint32_t length, const char *id, uint32_t id_length,
    uint32_t flags, char *family, uint32_t family_capacity,
    char *style, uint32_t style_capacity, AstraFontCatalogStrike *strikes,
    uint32_t strike_capacity, AstraFontCatalogFace *face);

ASTRA_EXTERN_C_END

#endif
