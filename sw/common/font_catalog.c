#include <astra/font_catalog.h>
#include <astra/afnt.h>
#include <astra/utf8.h>

#include <stddef.h>
#include <string.h>

static int span(uint32_t offset, uint32_t size, uint32_t total,
                uint32_t minimum)
{
    return offset >= minimum && offset <= total && size <= total - offset;
}

static int id_valid(const char *id, uint32_t length)
{
    if (id == NULL || length == 0u) return 0;
    for (uint32_t at = 0u; at < length; ++at) {
        unsigned char value = (unsigned char)id[at];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') ||
              value == '-' || value == '_' || value == ':' || value == '.'))
            return 0;
    }
    return 1;
}

static int strike_valid(const AstraFontCatalogStrike *strike)
{
    return strike->pixel_height != 0u && strike->bitmap_format >= 1u &&
           strike->bitmap_format <= 5u && strike->reserved == 0u;
}

static int face_valid(const AstraFontCatalogFace *face)
{
    if (face == NULL || !id_valid(face->id, face->id_length) ||
        face->family == NULL || face->family_length == 0u ||
        !astra_utf8_validate(face->family, face->family_length, 0u) ||
        face->style == NULL || face->style_length == 0u ||
        !astra_utf8_validate(face->style, face->style_length, 0u) ||
        face->strikes == NULL || face->strike_count == 0u ||
        (face->flags & ~ASTRA_FONT_CATALOG_SYSTEM) != 0u)
        return 0;
    for (uint32_t at = 0u; at < face->strike_count; ++at)
        if (!strike_valid(&face->strikes[at])) return 0;
    return 1;
}

int astra_font_catalog_pack(const AstraFontCatalogFace *faces, uint32_t count,
    uint32_t generation, void *output, uint32_t capacity, uint32_t *length)
{
    uint64_t total = sizeof(AstraFontCatalogHeader) +
                     (uint64_t)count * sizeof(AstraFontCatalogRecord);
    uint8_t *bytes = output;
    uint32_t offset;

    if (length == NULL || generation == 0u || (count != 0u && faces == NULL) ||
        total > UINT32_MAX) return 0;
    for (uint32_t at = 0u; at < count; ++at) {
        const AstraFontCatalogFace *face = &faces[at];
        if (!face_valid(face)) return 0;
        total += (uint64_t)face->id_length + face->family_length +
                 face->style_length;
        total = (total + 1u) & ~UINT64_C(1);
        total += (uint64_t)face->strike_count * sizeof(AstraFontCatalogStrike);
        if (total > UINT32_MAX) return 0;
    }
    *length = (uint32_t)total;
    if (output == NULL) return 1;
    if (capacity < total) return 0;
    memset(bytes, 0, (size_t)total);
    AstraFontCatalogHeader header = {
        .magic = ASTRA_FONT_CATALOG_MAGIC,
        .version = ASTRA_FONT_CATALOG_VERSION,
        .header_size = sizeof(AstraFontCatalogHeader),
        .total_size = (uint32_t)total,
        .generation = generation,
        .face_count = count,
        .records_offset = sizeof(AstraFontCatalogHeader)
    };
    memcpy(bytes, &header, sizeof(header));
    offset = (uint32_t)(sizeof(header) + count * sizeof(AstraFontCatalogRecord));
    for (uint32_t at = 0u; at < count; ++at) {
        const AstraFontCatalogFace *face = &faces[at];
        AstraFontCatalogRecord record = {0};
        record.id_offset = offset;
        record.id_length = face->id_length;
        memcpy(bytes + offset, face->id, face->id_length);
        offset += face->id_length;
        record.family_offset = offset;
        record.family_length = face->family_length;
        memcpy(bytes + offset, face->family, face->family_length);
        offset += face->family_length;
        record.style_offset = offset;
        record.style_length = face->style_length;
        memcpy(bytes + offset, face->style, face->style_length);
        offset += face->style_length;
        offset = (offset + 1u) & ~1u;
        record.strikes_offset = offset;
        record.strike_count = face->strike_count;
        memcpy(bytes + offset, face->strikes,
               (size_t)face->strike_count * sizeof(AstraFontCatalogStrike));
        offset += face->strike_count * sizeof(AstraFontCatalogStrike);
        record.flags = face->flags;
        memcpy(bytes + header.records_offset + at * sizeof(record),
               &record, sizeof(record));
    }
    return offset == total;
}

int astra_font_catalog_validate(const void *source, uint32_t length)
{
    const uint8_t *bytes = source;
    AstraFontCatalogHeader header;
    uint64_t records_end;

    if (bytes == NULL || length < sizeof(header)) return 0;
    memcpy(&header, bytes, sizeof(header));
    if (header.magic != ASTRA_FONT_CATALOG_MAGIC ||
        header.version != ASTRA_FONT_CATALOG_VERSION ||
        header.header_size != sizeof(header) || header.total_size != length ||
        header.generation == 0u || header.records_offset != sizeof(header) ||
        header.reserved[0] != 0u || header.reserved[1] != 0u)
        return 0;
    records_end = (uint64_t)header.records_offset +
                  (uint64_t)header.face_count * sizeof(AstraFontCatalogRecord);
    if (records_end > length) return 0;
    for (uint32_t at = 0u; at < header.face_count; ++at) {
        AstraFontCatalogRecord record;
        memcpy(&record, bytes + header.records_offset + at * sizeof(record),
               sizeof(record));
        if (!span(record.id_offset, record.id_length, length,
                  (uint32_t)records_end) ||
            !span(record.family_offset, record.family_length, length,
                  (uint32_t)records_end) ||
            !span(record.style_offset, record.style_length, length,
                  (uint32_t)records_end) ||
            (record.strikes_offset & 1u) != 0u ||
            record.strike_count > UINT32_MAX / sizeof(AstraFontCatalogStrike) ||
            !span(record.strikes_offset,
                  record.strike_count * sizeof(AstraFontCatalogStrike),
                  length, (uint32_t)records_end) ||
            !id_valid((const char *)bytes + record.id_offset,
                      record.id_length) ||
            record.family_length == 0u ||
            !astra_utf8_validate(bytes + record.family_offset,
                                 record.family_length, 0u) ||
            record.style_length == 0u ||
            !astra_utf8_validate(bytes + record.style_offset,
                                 record.style_length, 0u) ||
            record.strike_count == 0u ||
            (record.flags & ~ASTRA_FONT_CATALOG_SYSTEM) != 0u ||
            record.reserved != 0u) return 0;
        for (uint32_t strike = 0u; strike < record.strike_count; ++strike) {
            AstraFontCatalogStrike info;
            memcpy(&info, bytes + record.strikes_offset +
                   strike * sizeof(info), sizeof(info));
            if (!strike_valid(&info)) return 0;
        }
    }
    return 1;
}

int astra_font_catalog_face(const void *source, uint32_t length,
    uint32_t index, AstraFontCatalogFace *face)
{
    const uint8_t *bytes = source;
    AstraFontCatalogHeader header;
    AstraFontCatalogRecord record;

    if (bytes == NULL || face == NULL ||
        ((uintptr_t)bytes & (_Alignof(AstraFontCatalogStrike) - 1u)) != 0u ||
        length < sizeof(header)) return 0;
    memcpy(&header, bytes, sizeof(header));
    if (header.magic != ASTRA_FONT_CATALOG_MAGIC ||
        header.version != ASTRA_FONT_CATALOG_VERSION ||
        header.total_size != length || index >= header.face_count ||
        header.face_count > (length - sizeof(header)) / sizeof(record)) return 0;
    memcpy(&record, bytes + sizeof(header) + index * sizeof(record),
           sizeof(record));
    if (!span(record.id_offset, record.id_length, length,
              sizeof(header) + header.face_count * sizeof(record)) ||
        !span(record.family_offset, record.family_length, length,
              sizeof(header) + header.face_count * sizeof(record)) ||
        !span(record.style_offset, record.style_length, length,
              sizeof(header) + header.face_count * sizeof(record)) ||
        record.strike_count > UINT32_MAX / sizeof(AstraFontCatalogStrike) ||
        !span(record.strikes_offset,
              record.strike_count * sizeof(AstraFontCatalogStrike), length,
              sizeof(header) + header.face_count * sizeof(record))) return 0;
    *face = (AstraFontCatalogFace){
        .id = (const char *)bytes + record.id_offset,
        .id_length = record.id_length,
        .family = (const char *)bytes + record.family_offset,
        .family_length = record.family_length,
        .style = (const char *)bytes + record.style_offset,
        .style_length = record.style_length,
        .strikes = (const AstraFontCatalogStrike *)(const void *)(
            bytes + record.strikes_offset),
        .strike_count = record.strike_count,
        .flags = record.flags
    };
    return 1;
}

int astra_font_catalog_face_from_afnt(
    const void *afnt, uint32_t length, const char *id, uint32_t id_length,
    uint32_t flags, char *family, uint32_t family_capacity,
    char *style, uint32_t style_capacity, AstraFontCatalogStrike *strikes,
    uint32_t strike_capacity, AstraFontCatalogFace *face)
{
    AstraAfntView view;

    if (afnt == NULL || !id_valid(id, id_length) ||
        (flags & ~ASTRA_FONT_CATALOG_SYSTEM) != 0u ||
        family == NULL || family_capacity == 0u ||
        style == NULL || style_capacity == 0u ||
        strikes == NULL || face == NULL ||
        !astra_afnt_validate(afnt, length, &view) ||
        view.strike_count == 0u || view.strike_count > strike_capacity ||
        !astra_afnt_name_string(&view, "family", family, family_capacity) ||
        !astra_afnt_name_string(&view, "style", style, style_capacity) ||
        family[0] == '\0' || style[0] == '\0') return 0;
    for (uint32_t at = 0u; at < view.strike_count; ++at) {
        AstraAfntStrikeInfo info;

        if (!astra_afnt_strike_info(&view, at, &info)) return 0;
        strikes[at] = (AstraFontCatalogStrike){
            info.pixel_width, info.pixel_height, info.bitmap_format, 0u
        };
        if (!strike_valid(&strikes[at])) return 0;
    }
    *face = (AstraFontCatalogFace){
        id, id_length, family, (uint32_t)strlen(family),
        style, (uint32_t)strlen(style), strikes, view.strike_count, flags
    };
    return face_valid(face);
}
