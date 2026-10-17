/** @file fonts.h @brief Installed font catalog and system font choice. */
#ifndef ASTRA_FONTS_H
#define ASTRA_FONTS_H

#include <stdint.h>
#include <astra/area.h>
#include <astra/attributes.h>
#include <astra/font_encoding.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/** Read-only installed-font catalog startup capability. */
#define ASTRA_CAPABILITY_FONTS "FONTS"
/** Administrative font-choice startup capability. */
#define ASTRA_CAPABILITY_FONTS_ADMIN "FONTS_ADMIN"
/** Read-only AFNT face delivery to the privileged display renderer. */
#define ASTRA_CAPABILITY_FONTS_RENDER "FONTS_RENDER"
/** Maximum bytes in a face ID, including its terminator. */
#define ASTRA_FONT_ID_MAX 64u
/** Largest source accepted by native font import. */
#define ASTRA_FONT_IMPORT_MAX (32u * 1024u * 1024u)
/** Maximum source name bytes, including its terminator. */
#define ASTRA_FONT_IMPORT_NAME_MAX 128u

/** Adopt Display's immutable system-face snapshot for this process's UI.
 * Common text measurement and draw-list text then use the same face ID.
 * Call on the UI thread between frames, before laying out native controls.
 * Failed snapshot validation preserves the previous context. Independent fonts
 * and monospaced text are unaffected.
 * @param gui Borrowed GUI startup/session capability.
 * @return ::ASTRA_OK or a negative ::AstraResult. */
ASTRA_NODISCARD AstraResult astra_fonts_use_system(AstraHandle gui);

/** Adopt a mapped, read-only AFNT snapshot and its installed catalog ID.
 * The caller must keep all aliases immutable and ensure the ID identifies
 * these exact bytes. Call on the UI thread between frames. Adoption consumes
 * @p area; validation failure leaves it owned by the caller. An empty area,
 * zero length and empty ID select the built-in recovery face.
 * @param area Owned mapped immutable area.
 * @param length Valid AFNT bytes, excluding page padding.
 * @param id NUL-terminated installed face ID, fewer than ASTRA_FONT_ID_MAX bytes.
 * @return ::ASTRA_OK or a negative ::AstraResult. */
ASTRA_NODISCARD AstraResult astra_fonts_use_snapshot(
    AstraArea *area, uint32_t length, const char *id);

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
    /** Shipped and system-choice eligibility flags. */
    uint32_t flags;
} AstraInstalledFont;

enum {
    ASTRA_INSTALLED_FONT_SYSTEM = 1u << 0,
    ASTRA_INSTALLED_FONT_SELECTABLE = 1u << 1,
    /** Every printable ASCII scalar has an exact character-map entry. */
    ASTRA_INSTALLED_FONT_BASIC_TEXT = 1u << 2
};

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
/** Open validated AFNT bytes through display's FONTS_RENDER capability.
 * The mapped bytes remain immutable until @p face closes.
 * Only the display service receives this capability; ordinary applications
 * use catalog metadata and draw-list text. The renderer must validate the
 * returned AFNT before consuming it.
 * @param render FONTS_RENDER send capability.
 * @param id Counted installed face ID.
 * @param id_length Bytes in @p id, excluding terminator.
 * @param face Empty area; close with astra_area_close().
 * @param length Exact AFNT byte count, excluding area page padding.
 * @return ::ASTRA_OK or a negative ::AstraResult. */
ASTRA_NODISCARD AstraResult astra_fonts_face_open(
    AstraHandle render, const char *id, uint32_t id_length,
    AstraArea *face, uint32_t *length);
/** Import one native AFNT source into protected installed-font storage.
 * The service rejects unsupported source formats until its converter worker
 * is available. Returned ID is NUL-terminated on success and empty on error.
 * @param admin FONTS_ADMIN send capability.
 * @param bytes Complete source bytes, up to 32 MiB.
 * @param length Source byte count.
 * @param source_name Counted UTF-8 display name of source file.
 * @param name_length Bytes of @p source_name, without terminator.
 * @param encoding AUTO for AFNT/outline, explicit ISO-8859 codepage for Amiga.
 * @param installed_id Receives stable face ID; at least ASTRA_FONT_ID_MAX bytes.
 * @return ::ASTRA_OK or a negative ::AstraResult. */
ASTRA_NODISCARD AstraResult astra_fonts_import(
    AstraHandle admin, const void *bytes, uint32_t length,
    const char *source_name, uint32_t name_length, uint32_t encoding,
    char installed_id[ASTRA_FONT_ID_MAX]);
/** Uninstall one user-imported face after choosing a different system face.
 * Shipped faces cannot be removed. The catalog stops exposing a successfully
 * uninstalled face even if cleanup of its now-unreferenced AFNT file fails.
 * @param admin FONTS_ADMIN send capability.
 * @param id Counted catalog face ID.
 * @param id_length Bytes at @p id, without a terminator.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_BUSY for current system choice,
 * ::ASTRA_ERROR_PERMISSION for a shipped face, or another negative result. */
ASTRA_NODISCARD AstraResult astra_fonts_uninstall(
    AstraHandle admin, const char *id, uint32_t id_length);

ASTRA_EXTERN_C_END
#endif
