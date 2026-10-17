/** @file fonts.h @brief Installed font catalog and system font choice. */
#ifndef ASTRA_FONTS_H
#define ASTRA_FONTS_H

#include <stdint.h>
#include <astra/area.h>
#include <astra/attributes.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/** Read-only installed-font catalog startup capability. */
#define ASTRA_CAPABILITY_FONTS "FONTS"
/** Administrative font-choice startup capability. */
#define ASTRA_CAPABILITY_FONTS_ADMIN "FONTS_ADMIN"
/** Maximum bytes in a face ID, including its terminator. */
#define ASTRA_FONT_ID_MAX 64u

/** One borrowed face; strings are counted and live until catalog close. */
typedef struct AstraInstalledFont {
    /** Counted stable catalog ID, borrowed from the snapshot. */
    const char *id;
    /** Bytes in @ref id, excluding any terminator. */
    uint32_t id_length;
    /** Counted UTF-8 family name, borrowed from the snapshot. */
    const char *family;
    /** Bytes in @ref family. */
    uint32_t family_length;
    /** Counted UTF-8 designed style, borrowed from the snapshot. */
    const char *style;
    /** Bytes in @ref style. */
    uint32_t style_length;
    /** Number of installed bitmap strikes. */
    uint32_t strike_count;
    /** ASTRA_INSTALLED_FONT_SYSTEM when shipped with the system. */
    uint32_t flags;
} AstraInstalledFont;

enum { ASTRA_INSTALLED_FONT_SYSTEM = 1u << 0 };

/** One installed bitmap size. */
typedef struct AstraInstalledFontStrike {
    /** Nominal width in pixels. */
    uint16_t pixel_width;
    /** Nominal height in pixels. */
    uint16_t pixel_height;
    /** AFNT bitmap format code. */
    uint16_t bitmap_format;
} AstraInstalledFontStrike;

/** Owned immutable catalog snapshot; initialize before opening. */
typedef struct AstraFontsCatalog {
    /** @cond ASTRA_INTERNAL */
    AstraArea _private_area;
    uint32_t _catalog_bytes;
    /** @endcond */
    /** Catalog generation captured at open. */
    uint32_t generation;
    /** Number of available face records. */
    uint32_t face_count;
    /** Selected face ID, or empty string when no choice is available. */
    char selected_id[ASTRA_FONT_ID_MAX];
} AstraFontsCatalog;

/** Empty initializer for an owned catalog snapshot. */
#define ASTRA_FONTS_CATALOG_INIT { ASTRA_AREA_INIT, 0u, 0u, 0u, {0} }

/** Read and validate current catalog through FONTS capability.
 * @param service FONTS send capability.
 * @param catalog Empty caller-owned snapshot.
 * @return ::ASTRA_OK or a negative ::AstraResult. */
ASTRA_NODISCARD AstraResult astra_fonts_catalog_open(
    AstraHandle service, AstraFontsCatalog *catalog);
/** Borrow one face from an open immutable catalog.
 * @param catalog Open snapshot.
 * @param index Zero-based face index.
 * @param face Receives snapshot-backed counted names and ID.
 * @return ::ASTRA_OK or a negative ::AstraResult. */
ASTRA_NODISCARD AstraResult astra_fonts_catalog_face(
    const AstraFontsCatalog *catalog, uint32_t index,
    AstraInstalledFont *face);
/** Copy one strike of a face in an open immutable catalog.
 * @param catalog Open snapshot.
 * @param face_index Zero-based face index.
 * @param strike_index Zero-based strike index within that face.
 * @param strike Receives copied strike metrics.
 * @return ::ASTRA_OK or a negative ::AstraResult. */
ASTRA_NODISCARD AstraResult astra_fonts_catalog_strike(
    const AstraFontsCatalog *catalog, uint32_t face_index,
    uint32_t strike_index, AstraInstalledFontStrike *strike);
/** Release catalog mapping and capability.
 * @param catalog Open snapshot.
 * @return ::ASTRA_OK or a negative ::AstraResult. */
ASTRA_NODISCARD AstraResult astra_fonts_catalog_close(
    AstraFontsCatalog *catalog);
/** Persist selected face via Fonts service and Config Kit.
 * @param admin FONTS_ADMIN send capability.
 * @param id Counted catalog face ID.
 * @param id_length Bytes at @p id, without a terminator.
 * @return ::ASTRA_OK or a negative ::AstraResult. */
ASTRA_NODISCARD AstraResult astra_fonts_select(
    AstraHandle admin, const char *id, uint32_t id_length);

ASTRA_EXTERN_C_END
#endif
