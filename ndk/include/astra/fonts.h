/** @file fonts.h @brief Installed font catalog and system font choice. */
#ifndef ASTRA_FONTS_H
#define ASTRA_FONTS_H

#include <stdint.h>
#include <astra/area.h>
#include <astra/attributes.h>
#include <astra/font_encoding.h>
#include <astra/font_import.h>
#include <astra/gui.h>
#include <astra/surface.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/** Read-only installed-font catalog startup capability. */
#define ASTRA_CAPABILITY_FONT_CATALOG "FONT_CATALOG"
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

/** Copy this process's actually adopted system-font ID.
 * This reads the rendering context, independently of a newer saved choice.
 * Call between frames on the UI thread, like context adoption.
 * @param id Output buffer of at least ASTRA_FONT_ID_MAX bytes; empty selects
 * the built-in recovery face and is also returned on error.
 * @return ASTRA_OK or a negative AstraResult. */
ASTRA_NODISCARD AstraResult astra_fonts_context_id(char id[ASTRA_FONT_ID_MAX]);

/** Measured installed text; dimensions are rounded-up logical pixels. */
typedef struct AstraFontTextMetrics {
    uint32_t width; /**< Advance of the fitting UTF-8 prefix. */
    uint32_t height; /**< Line box including ascent, descent and line gap. */
    uint32_t fitted_bytes; /**< Complete UTF-8 bytes fitting the width limit. */
} AstraFontTextMetrics;

/** Empty text metrics. */
#define ASTRA_FONT_TEXT_METRICS_INIT { 0u, 0u, 0u }

/** Measure installed-face text without changing the process's system font.
 * Display uses the same exact strike and glyph advances as named-face drawing.
 * Long UTF-8 spans are transported in bounded chunks, preserving fractional
 * advances across chunks. No font bytes or bitmap addresses reach the client.
 * @param gui Borrowed GUI session capability.
 * @param id Counted installed face ID.
 * @param id_length ID bytes, excluding any terminator.
 * @param pixel_height Exact installed strike height.
 * @param text Valid UTF-8 bytes, or NULL for an empty span.
 * @param length Number of bytes at @p text.
 * @param maximum_width Inclusive horizontal advance limit in pixels.
 * @param metrics Receives the fitted advance, line height and complete prefix.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_UNSUPPORTED for an absent strike, or a
 * negative ::AstraResult. Metrics are zero on failure. */
ASTRA_NODISCARD AstraResult astra_fonts_text_measure(
    AstraHandle gui, const char *id, uint32_t id_length,
    uint16_t pixel_height, const char *text, uint32_t length,
    uint32_t maximum_width, AstraFontTextMetrics *metrics);

/** Append installed-face text to a hardware draw-list view.
 * This does not change the process's system font or rasterize on the CPU.
 * @param surface Hardware draw-list destination with its existing clip.
 * @param id Counted installed face ID.
 * @param id_length ID bytes, excluding any terminator.
 * @param x Left text origin in pixels.
 * @param y Top text origin in pixels.
 * @param text Valid UTF-8 span.
 * @param length Bytes in @p text.
 * @param pixel_height Exact installed strike height.
 * @param color RGB565 foreground color.
 * @return Nonzero on success; zero for invalid arguments or insufficient room. */
int astra_surface_font_text(AstraSurfaceView *surface,
    const char *id, uint32_t id_length, int32_t x, int32_t y,
    const char *text, uint32_t length, uint16_t pixel_height, uint16_t color);

/** Adopt Display's immutable system-face snapshot for this process's UI.
 * Common text measurement and draw-list text then use the same face ID.
 * Call on the UI thread between frames, before laying out native controls.
 * Failed snapshot validation preserves the previous context. Independent fonts
 * and monospaced text are unaffected.
 * The next request reports the context actually adopted, so Display keeps the
 * previous snapshot alive if mapping or validation of a delivered reply fails.
 * @param gui Borrowed GUI startup/session capability.
 * @param change Delivered FONT_CHANGED event for this window, or NULL during
 * initial process setup. After this call, dispatch the event to the retained
 * controls even on failure; their layout must match the actually active context.
 * Window-specific acknowledgement permits reliable retries for other windows.
 * @return ::ASTRA_OK or a negative ::AstraResult. */
ASTRA_NODISCARD AstraResult astra_fonts_use_system(
    AstraHandle gui, const AstraWindowEvent *change);

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

/** Borrow immutable AFNT bytes for this process's UI metrics and draw lists.
 * The caller retains the mapping and must keep it valid until replacing this
 * context. This avoids a second mapping owner when the renderer already owns
 * the face. Call on the UI thread between frames. Borrowed contexts may replace
 * built-in or borrowed contexts; replacing an owned snapshot returns
 * ::ASTRA_ERROR_BUSY. Failed validation preserves the current context.
 * @param bytes Borrowed immutable AFNT bytes.
 * @param length Valid AFNT byte count, from 1 through ASTRA_FONT_IMPORT_MAX.
 * @param id Nonempty installed catalog ID, fewer than ASTRA_FONT_ID_MAX bytes.
 * @return ::ASTRA_OK or a negative ::AstraResult. */
ASTRA_NODISCARD AstraResult astra_fonts_use_borrowed(
    const void *bytes, uint32_t length, const char *id);

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

/** Read and validate current catalog through FONT_CATALOG capability.
 * @param service FONT_CATALOG send capability.
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

/** Owned read-only source and license metadata. Strings are copied on demand;
 * no glyph data or writable font storage is exposed through FONTS. */
typedef struct AstraFontInformation {
    /** @cond ASTRA_INTERNAL */
    AstraArea _private_area;
    uint32_t _private_bytes;
    /** @endcond */
    uint32_t source_count; /**< Original source records, possibly zero. */
    uint32_t notice_count; /**< Embedded copyright/license/URL records. */
} AstraFontInformation;
/** Empty initializer; close before reusing an opened information snapshot. */
#define ASTRA_FONT_INFORMATION_INIT { ASTRA_AREA_INIT, 0u, 0u, 0u }

/** Text fields in font information. Root fields require index zero; source
 * and notice fields use their corresponding zero-based record index. */
enum {
    ASTRA_FONT_INFORMATION_FAMILY = 1u, /**< Family name. */
    ASTRA_FONT_INFORMATION_STYLE, /**< Designed style. */
    ASTRA_FONT_INFORMATION_AUTHOR, /**< Author, when supplied. */
    ASTRA_FONT_INFORMATION_LICENSE, /**< License summary, when supplied. */
    ASTRA_FONT_INFORMATION_CONVERTER, /**< Converter identity. */
    ASTRA_FONT_INFORMATION_ENCODING, /**< Original source character encoding. */
    ASTRA_FONT_INFORMATION_SOURCE_NAME, /**< Original source name. */
    ASTRA_FONT_INFORMATION_SOURCE_SHA256, /**< Original source SHA-256. */
    ASTRA_FONT_INFORMATION_NOTICE_TEXT, /**< Complete decoded notice. */
    ASTRA_FONT_INFORMATION_NOTICE_RAW_HEX, /**< Exact original notice bytes. */
    ASTRA_FONT_INFORMATION_NOTICE_LANGUAGE, /**< Decoded language tag. */
    ASTRA_FONT_INFORMATION_NOTICE_LANGUAGE_RAW_HEX /**< Original language tag. */
};

/** Original SFNT notice identifiers; interpreted together, not as Unicode. */
typedef struct AstraFontNotice {
    uint32_t name_id; /**< SFNT name ID (copyright 0, license 13, URL 14). */
    uint32_t platform_id; /**< Original platform ID. */
    uint32_t encoding_id; /**< Original platform encoding ID. */
    uint32_t language_id; /**< Original platform language ID. */
} AstraFontNotice;

/** Open immutable metadata through FONT_CATALOG; uninstall does not invalidate it.
 * @param service FONT_CATALOG capability.
 * @param id Counted installed face ID.
 * @param id_length ID bytes, excluding terminator.
 * @param information Empty owned snapshot.
 * @return ASTRA_OK or a negative AstraResult. */
ASTRA_NODISCARD AstraResult astra_fonts_information_open(
    AstraHandle service, const char *id, uint32_t id_length,
    AstraFontInformation *information);
/** Copy a counted UTF-8 metadata string, preserving multiline text.
 * @param information Open snapshot.
 * @param field ASTRA_FONT_INFORMATION_* field.
 * @param index Zero for root fields; source/notice index otherwise.
 * @param text Output buffer, or NULL with zero capacity to measure.
 * @param capacity Output bytes, including room for final NUL.
 * @param length Receives decoded byte count, excluding final NUL. Embedded
 * NUL is counted data. On BUFFER_TOO_SMALL, output is unchanged.
 * @return ASTRA_OK, NOT_PRESENT for an absent optional field, or a negative
 * AstraResult. Invalid metadata returns IO; text is never truncated. */
ASTRA_NODISCARD AstraResult astra_fonts_information_string(
    const AstraFontInformation *information, uint32_t field, uint32_t index,
    char *text, uint32_t capacity, uint32_t *length);
/** Read original notice IDs.
 * @param information Open snapshot.
 * @param index Notice index.
 * @param notice Receives copied IDs.
 * @return ASTRA_OK or a negative AstraResult. */
ASTRA_NODISCARD AstraResult astra_fonts_information_notice(
    const AstraFontInformation *information, uint32_t index,
    AstraFontNotice *notice);
/** Release information mapping and capability.
 * @param information Owned snapshot.
 * @return ASTRA_OK or a negative AstraResult. */
ASTRA_NODISCARD AstraResult astra_fonts_information_close(
    AstraFontInformation *information);
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
/** Owned import session. Closing its last control capability cancels unfinished
 * work. Keep it open until terminal state if cleanup confirmation is needed. */
typedef struct AstraFontImport {
    /** @cond ASTRA_INTERNAL */
    AstraHandle _private_control;
    AstraHandle _private_changed;
    /** @endcond */
} AstraFontImport;
/** Empty owned import session. */
#define ASTRA_FONT_IMPORT_INIT { ASTRA_INVALID_HANDLE, ASTRA_INVALID_HANDLE }

/** Authoritative import state. Progress counts complete staged faces, not an
 * estimate of conversion time. Zero total means source discovery is pending. */
typedef struct AstraFontImportProgress {
    uint32_t state; /**< ASTRA_FONT_IMPORT_* lifecycle state. */
    uint32_t completed_faces; /**< Faces staged successfully. */
    uint32_t total_faces; /**< Source faces, when known. */
    AstraResult result; /**< Terminal outcome; OK while running/cancelling. */
    char installed_id[ASTRA_FONT_ID_MAX]; /**< First face ID on success only. */
} AstraFontImportProgress;
/** Empty progress value; query before inspecting. */
#define ASTRA_FONT_IMPORT_PROGRESS_INIT { 0u, 0u, 0u, ASTRA_OK, {0} }

/** Begin native AFNT/outline/Amiga import into protected installed storage.
 * Copies source before returning; caller may release it. Conversion continues
 * in the service. A successful begin admits a job, not a completed import.
 * @param admin FONTS_ADMIN send capability.
 * @param bytes Complete source bytes, up to 32 MiB.
 * @param length Source byte count.
 * @param source_name Counted UTF-8 display name of source file.
 * @param name_length Bytes of @p source_name, without terminator.
 * @param encoding AUTO for AFNT/outline, explicit ISO-8859 codepage for Amiga.
 * @param import Empty owned session.
 * @return OK, BUSY when another session owns import, or a negative result. */
ASTRA_NODISCARD AstraResult astra_fonts_import_begin(
    AstraHandle admin, const void *bytes, uint32_t length,
    const char *source_name, uint32_t name_length, uint32_t encoding,
    AstraFontImport *import);
/** Read authoritative progress, including terminal outcome after cleanup.
 * @param import Open session. @param progress Receives complete state.
 * @return Query transport/protocol result; job outcome is progress.result. */
ASTRA_NODISCARD AstraResult astra_fonts_import_state(
    const AstraFontImport *import, AstraFontImportProgress *progress);
/** Request cancellation. An in-flight converter call may take up to its existing
 * 60-second timeout to retire. Continue waiting for CANCELLED or FAILED before
 * claiming cleanup. Already terminal jobs remain unchanged.
 * @param import Open session. @return Request result, not cleanup completion. */
ASTRA_NODISCARD AstraResult astra_fonts_import_cancel(const AstraFontImport *import);
/** Borrow notification receive endpoint for a native wait set. Query state
 * before each wait to drain coalesced hints. Service death wakes PEER_DEAD;
 * hints carry no progress data. Do not close the borrowed handle.
 * @param import Open session. @return Wait handle, or INVALID_HANDLE. */
ASTRA_NODISCARD AstraHandle astra_fonts_import_wait_handle(const AstraFontImport *import);
/** Close session. Unfinished work is cancelled by peer-death; no thread or
 * source storage is freed while conversion can still use it.
 * @param import Owned session. @return First close error after both closes. */
ASTRA_NODISCARD AstraResult astra_fonts_import_close(AstraFontImport *import);
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
