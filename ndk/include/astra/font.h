#ifndef ASTRA_FONT_H
#define ASTRA_FONT_H

/**
 * @file font.h
 * @brief Immutable font-face, font-instance, and text-layout contracts.
 */

#include <stdint.h>

#include <astra/resource.h>

ASTRA_EXTERN_C_BEGIN

/**
 * @defgroup astra_fonts Fonts and text layout
 * @brief Managed bitmap fonts, shaping, measurement, and hit testing.
 *
 * Applications receive opaque handles rather than AFNT records, glyph atlas
 * pointers, or hardware addresses. Faces, resolved fonts, and layouts are
 * immutable after creation. A layout owns an internal copy of its UTF-8 input
 * and strong references to every font and fallback strike it uses.
 *
 * The direct-MMIO backend publishes these symbols but has no font service and
 * reports ::ASTRA_ERROR_NOT_PRESENT from open operations. The operating-system
 * backend will implement the same contract. Rendering is submitted through the
 * future graphics draw-list API rather than through a private font queue.
 *
 * Live handle wrappers are move-only by convention: do not copy them, and
 * close each successful acquisition exactly once.
 *
 * @{
 */

/** Stable semantic roles for fonts guaranteed or selected by the system. */
enum {
    /** Default proportional user-interface face. */
    ASTRA_FONT_ROLE_UI = 1,
    /** Emphasized user-interface face, normally a heavier weight. */
    ASTRA_FONT_ROLE_UI_EMPHASIS = 2,
    /** Default monospaced face for code, terminals, and tables. */
    ASTRA_FONT_ROLE_MONO = 3,
    /** Non-replaceable emergency face available to recovery facilities. */
    ASTRA_FONT_ROLE_RESCUE = 4
};

/** Bitmap formats that may back a resolved font strike. */
enum {
    /** No strike has been selected. */
    ASTRA_FONT_BITMAP_NONE = 0,
    /** One-bit transparent or opaque glyph mask. */
    ASTRA_FONT_BITMAP_MASK1 = 1,
    /** Four-bit foreground coverage. */
    ASTRA_FONT_BITMAP_A4 = 2,
    /** Four-bit palette index with binary transparency. */
    ASTRA_FONT_BITMAP_INDEX4 = 3,
    /** Eight-bit palette index with binary transparency. */
    ASTRA_FONT_BITMAP_INDEX8 = 4
};

/** Face style characteristics. */
enum {
    /** Designed italic face. */
    ASTRA_FONT_STYLE_ITALIC = 1u << 0,
    /** Designed oblique face. */
    ASTRA_FONT_STYLE_OBLIQUE = 1u << 1
};

/** Font matching policy in ::AstraFontRequest. */
enum {
    /** Reject the request unless an exact native strike size exists. */
    ASTRA_FONT_MATCH_EXACT_STRIKE = 1u << 0,
    /** Permit the service to add system fallback faces for missing glyphs. */
    ASTRA_FONT_MATCH_ALLOW_FALLBACK = 1u << 1,
    /** Prefer an indexed-color strike when otherwise equivalent. */
    ASTRA_FONT_MATCH_PREFER_COLOR = 1u << 2,
    /** Reject faces that cannot provide an indexed-color strike. */
    ASTRA_FONT_MATCH_REQUIRE_COLOR = 1u << 3
};

/** Capabilities reported in ::AstraFontInfo. */
enum {
    /** Every glyph in the face has one fixed advance. */
    ASTRA_FONT_CAP_FIXED_PITCH = 1u << 0,
    /** The face contains pair-kerning information. */
    ASTRA_FONT_CAP_KERNING = 1u << 1,
    /** At least one strike contains embedded color. */
    ASTRA_FONT_CAP_COLOR = 1u << 2,
    /** At least one MASK1 strike is available. */
    ASTRA_FONT_CAP_MASK1 = 1u << 3,
    /** At least one A4 strike is available. */
    ASTRA_FONT_CAP_A4 = 1u << 4,
    /** At least one INDEX4 strike is available. */
    ASTRA_FONT_CAP_INDEX4 = 1u << 5,
    /** At least one INDEX8 strike is available. */
    ASTRA_FONT_CAP_INDEX8 = 1u << 6
};

/** Metadata strings available from a face or resolved font. */
enum {
    /** UTF-8 family name. */
    ASTRA_FONT_STRING_FAMILY = 1,
    /** UTF-8 style name. */
    ASTRA_FONT_STRING_STYLE = 2,
    /** UTF-8 author or foundry attribution. */
    ASTRA_FONT_STRING_AUTHOR = 3,
    /** UTF-8 license identifier or notice. */
    ASTRA_FONT_STRING_LICENSE = 4,
    /** UTF-8 source revision and conversion provenance. */
    ASTRA_FONT_STRING_PROVENANCE = 5
};

/** Text wrapping and overflow behavior. */
enum {
    /** Wrap at language-appropriate word boundaries. */
    ASTRA_TEXT_LAYOUT_WRAP_WORD = 1u << 0,
    /** Wrap at grapheme boundaries when a line has no word boundary. */
    ASTRA_TEXT_LAYOUT_WRAP_GRAPHEME = 1u << 1,
    /** Replace the end of the final visible line with an ellipsis on overflow. */
    ASTRA_TEXT_LAYOUT_ELLIPSIZE_END = 1u << 2,
    /** Include trailing whitespace in reported logical line widths. */
    ASTRA_TEXT_LAYOUT_INCLUDE_TRAILING_WHITESPACE = 1u << 3
};

/** Logical line alignment. Start and end follow the resolved text direction. */
enum {
    /** Align each line to its logical start edge. */
    ASTRA_TEXT_ALIGNMENT_START = 0,
    /** Center each line in the available width. */
    ASTRA_TEXT_ALIGNMENT_CENTER = 1,
    /** Align each line to its logical end edge. */
    ASTRA_TEXT_ALIGNMENT_END = 2,
    /** Expand inter-word spacing to fill eligible lines. */
    ASTRA_TEXT_ALIGNMENT_JUSTIFY = 3
};

/** Base-direction selection for a text layout. */
enum {
    /** Resolve base direction from the text and language. */
    ASTRA_TEXT_DIRECTION_AUTO = 0,
    /** Force a left-to-right base direction. */
    ASTRA_TEXT_DIRECTION_LEFT_TO_RIGHT = 1,
    /** Force a right-to-left base direction. */
    ASTRA_TEXT_DIRECTION_RIGHT_TO_LEFT = 2
};

/** Caret edge at a UTF-8 source boundary. */
enum {
    /** Leading edge in the glyph run's resolved direction. */
    ASTRA_TEXT_EDGE_LEADING = 0,
    /** Trailing edge in the glyph run's resolved direction. */
    ASTRA_TEXT_EDGE_TRAILING = 1
};

/** Flags returned in ::AstraTextHit. */
enum {
    /** The queried point lies inside the layout's logical bounds. */
    ASTRA_TEXT_HIT_INSIDE = 1u << 0,
    /** The queried point lies on visible glyph ink. */
    ASTRA_TEXT_HIT_ON_INK = 1u << 1
};

/** Policy for a glyph strike that contains embedded colors. */
enum {
    /** Render the strike's embedded palette colors. */
    ASTRA_TEXT_EMBEDDED_COLOR_USE = 0,
    /** Treat glyph coverage as a mask for the foreground color. */
    ASTRA_TEXT_EMBEDDED_COLOR_FOREGROUND = 1,
    /** Reject a draw that requires an embedded-color strike. */
    ASTRA_TEXT_EMBEDDED_COLOR_REJECT = 2
};

/** Flags controlling text paint behavior. */
enum {
    /** Fill the glyph cell background with the paint background color. */
    ASTRA_TEXT_PAINT_OPAQUE_BACKGROUND = 1u << 0
};

/**
 * Loaded family and style plus its available native bitmap strikes.
 *
 * @since 0.1.0
 */
typedef struct AstraFontFace {
    /** Private NDK handle; applications must not access this field. */
    AstraHandle _private_handle;
} AstraFontFace;

/**
 * Immutable, draw-ready face and native-strike selection.
 *
 * @since 0.1.0
 */
typedef struct AstraFont {
    /** Private NDK handle; applications must not access this field. */
    AstraHandle _private_handle;
} AstraFont;

/**
 * Immutable copied text, line breaks, metrics, and positioned glyph runs.
 *
 * @since 0.1.0
 */
typedef struct AstraTextLayout {
    /** Private NDK handle; applications must not access this field. */
    AstraHandle _private_handle;
} AstraTextLayout;

/** Native strike and matching request. */
typedef struct AstraFontRequest {
    /** Size of this structure in bytes. */
    uint32_t size;
    /** Bitwise combination of `ASTRA_FONT_MATCH_*` values. */
    uint32_t match_flags;
    /** Requested pixel height in 26.6 units; must be positive. */
    AstraFixed26_6 pixel_height;
    /** Requested pixel width, or zero to preserve the strike aspect. */
    AstraFixed26_6 pixel_width;
    /** CSS-compatible weight from 1 through 1000; 400 is regular. */
    uint16_t weight;
    /** Width stretch percentage; 100 is normal. */
    uint16_t stretch_percent;
    /** Bitwise combination of `ASTRA_FONT_STYLE_*` values. */
    uint32_t style_flags;
    /** Reserved for source-compatible growth; initialize to zero. */
    uint32_t reserved[5];
} AstraFontRequest;

/** Face metadata and, for ::AstraFont, the selected native strike. */
typedef struct AstraFontInfo {
    /** Size of this structure in bytes. */
    uint32_t size;
    /** Bitwise combination of `ASTRA_FONT_CAP_*` values. */
    uint32_t capabilities;
    /** Number of glyph records in the selected face. */
    uint32_t glyph_count;
    /** Number of native strikes available in the selected face. */
    uint32_t strike_count;
    /** Selected strike pixel height, or zero for an unresolved face. */
    AstraFixed26_6 pixel_height;
    /** Selected strike pixel width, or zero for an unresolved face. */
    AstraFixed26_6 pixel_width;
    /** Actual face weight from 1 through 1000. */
    uint16_t weight;
    /** Actual face stretch percentage. */
    uint16_t stretch_percent;
    /** Bitwise combination of `ASTRA_FONT_STYLE_*` values. */
    uint32_t style_flags;
    /** One `ASTRA_FONT_BITMAP_*` value, or NONE for an unresolved face. */
    uint16_t bitmap_format;
    /** Reserved; currently zero. */
    uint16_t reserved16;
    /** Family string length in bytes, excluding its null terminator. */
    uint32_t family_bytes;
    /** Style string length in bytes, excluding its null terminator. */
    uint32_t style_bytes;
    /** Provenance string length in bytes, excluding its null terminator. */
    uint32_t provenance_bytes;
    /** Reserved for source-compatible growth; currently zero. */
    uint32_t reserved[5];
} AstraFontInfo;

/** Baseline-relative metrics for a resolved font strike. */
typedef struct AstraFontMetrics {
    /** Size of this structure in bytes. */
    uint32_t size;
    /** Positive distance from the baseline to the ascent edge. */
    AstraFixed26_6 ascent;
    /** Positive distance from the baseline to the descent edge. */
    AstraFixed26_6 descent;
    /** Recommended additional distance between adjacent lines. */
    AstraFixed26_6 line_gap;
    /** Baseline-to-cap-height distance, or zero when unavailable. */
    AstraFixed26_6 cap_height;
    /** Baseline-to-x-height distance, or zero when unavailable. */
    AstraFixed26_6 x_height;
    /** Maximum horizontal glyph advance. */
    AstraFixed26_6 max_advance;
    /** Signed underline offset from the baseline; positive is downward. */
    AstraFixed26_6 underline_position;
    /** Recommended positive underline thickness. */
    AstraFixed26_6 underline_thickness;
    /** Reserved for source-compatible growth; currently zero. */
    uint32_t reserved[4];
} AstraFontMetrics;

/** Text-layout constraints and paragraph policy. */
typedef struct AstraTextLayoutOptions {
    /** Size of this structure in bytes. */
    uint32_t size;
    /** Bitwise combination of `ASTRA_TEXT_LAYOUT_*` values. */
    uint32_t flags;
    /** Maximum logical width, or zero for unbounded width. */
    AstraFixed26_6 max_width;
    /** Maximum logical height, or zero for unbounded height. */
    AstraFixed26_6 max_height;
    /** Baseline spacing, or zero for the font default. */
    AstraFixed26_6 line_height;
    /** One `ASTRA_TEXT_ALIGNMENT_*` value. */
    uint16_t alignment;
    /** One `ASTRA_TEXT_DIRECTION_*` value. */
    uint16_t direction;
    /** Spaces per tab stop, or zero for the service default. */
    uint16_t tab_columns;
    /** Reserved; initialize to zero. */
    uint16_t reserved16;
    /** Reserved for source-compatible growth; initialize to zero. */
    uint32_t reserved[5];
} AstraTextLayoutOptions;

/** Rectangle in layout-local 26.6 coordinates. */
typedef struct AstraTextRect {
    /** Left coordinate. */
    AstraFixed26_6 x;
    /** Top coordinate. */
    AstraFixed26_6 y;
    /** Nonnegative width. */
    AstraFixed26_6 width;
    /** Nonnegative height. */
    AstraFixed26_6 height;
} AstraTextRect;

/** Aggregate dimensions and source consumption for a text layout. */
typedef struct AstraTextMetrics {
    /** Size of this structure in bytes. */
    uint32_t size;
    /** Bounds used for alignment, selection, and background painting. */
    AstraTextRect logical_bounds;
    /** Tight bounds of visible glyph ink. */
    AstraTextRect ink_bounds;
    /** Y coordinate of the first line baseline. */
    AstraFixed26_6 first_baseline;
    /** Y coordinate of the final line baseline. */
    AstraFixed26_6 last_baseline;
    /** Maximum laid-out line advance. */
    AstraFixed26_6 advance_width;
    /** Total paragraph advance including line spacing. */
    AstraFixed26_6 advance_height;
    /** Number of laid-out lines. */
    uint32_t line_count;
    /** Number of positioned glyphs across all fallback runs. */
    uint32_t glyph_count;
    /** Number of source UTF-8 bytes represented by the layout. */
    uint32_t consumed_utf8_bytes;
    /** Reserved for source-compatible growth; currently zero. */
    uint32_t reserved[5];
} AstraTextMetrics;

/** Source position and caret geometry produced by hit testing. */
typedef struct AstraTextHit {
    /** Size of this structure in bytes. */
    uint32_t size;
    /** Valid UTF-8 byte boundary in the copied source text. */
    uint32_t utf8_byte_offset;
    /** Zero-based visual line index. */
    uint32_t line_index;
    /** One `ASTRA_TEXT_EDGE_*` value. */
    uint16_t edge;
    /** Bitwise combination of `ASTRA_TEXT_HIT_*` values. */
    uint16_t flags;
    /** Caret x coordinate in layout-local coordinates. */
    AstraFixed26_6 caret_x;
    /** Caret top y coordinate in layout-local coordinates. */
    AstraFixed26_6 caret_y;
    /** Positive caret height. */
    AstraFixed26_6 caret_height;
    /** Reserved for source-compatible growth; currently zero. */
    uint32_t reserved[5];
} AstraTextHit;

/** Paint parameters consumed by the future graphics draw-list text command. */
typedef struct AstraTextPaint {
    /** Size of this structure in bytes. */
    uint32_t size;
    /** Bitwise combination of `ASTRA_TEXT_PAINT_*` values. */
    uint32_t flags;
    /** Color used by monochrome and coverage glyphs. */
    AstraColorRGBA8 foreground;
    /** Glyph-cell background color when opaque background is enabled. */
    AstraColorRGBA8 background;
    /** One `ASTRA_TEXT_EMBEDDED_COLOR_*` policy value. */
    uint16_t embedded_color_policy;
    /** Reserved; initialize to zero. */
    uint16_t reserved16;
    /** Reserved for source-compatible growth; initialize to zero. */
    uint32_t reserved[5];
} AstraTextPaint;

/** Initializer for an empty ::AstraFontFace. */
#define ASTRA_FONT_FACE_INIT { ASTRA_INVALID_HANDLE }
/** Initializer for an empty ::AstraFont. */
#define ASTRA_FONT_INIT { ASTRA_INVALID_HANDLE }
/** Initializer for an empty ::AstraTextLayout. */
#define ASTRA_TEXT_LAYOUT_INIT { ASTRA_INVALID_HANDLE }

/** Default font request; the caller must set a positive pixel height. */
#define ASTRA_FONT_REQUEST_INIT \
    { sizeof(AstraFontRequest), ASTRA_FONT_MATCH_ALLOW_FALLBACK, 0, 0, \
      400, 100, 0, { 0, 0, 0, 0, 0 } }
/** Initializer for an output ::AstraFontInfo. */
#define ASTRA_FONT_INFO_INIT \
    { sizeof(AstraFontInfo), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
      { 0, 0, 0, 0, 0 } }
/** Initializer for an output ::AstraFontMetrics. */
#define ASTRA_FONT_METRICS_INIT \
    { sizeof(AstraFontMetrics), 0, 0, 0, 0, 0, 0, 0, 0, \
      { 0, 0, 0, 0 } }
/** Default unconstrained, start-aligned, automatic-direction layout options. */
#define ASTRA_TEXT_LAYOUT_OPTIONS_INIT \
    { sizeof(AstraTextLayoutOptions), 0, 0, 0, 0, \
      ASTRA_TEXT_ALIGNMENT_START, ASTRA_TEXT_DIRECTION_AUTO, 0, 0, \
      { 0, 0, 0, 0, 0 } }
/** Initializer for output ::AstraTextMetrics. */
#define ASTRA_TEXT_METRICS_INIT \
    { sizeof(AstraTextMetrics), { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, \
      0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0 } }
/** Initializer for output ::AstraTextHit. */
#define ASTRA_TEXT_HIT_INIT \
    { sizeof(AstraTextHit), 0, 0, ASTRA_TEXT_EDGE_LEADING, 0, 0, 0, 0, \
      { 0, 0, 0, 0, 0 } }
/** Default opaque-white foreground with transparent background. */
#define ASTRA_TEXT_PAINT_INIT \
    { sizeof(AstraTextPaint), 0, { 255, 255, 255, 255 }, { 0, 0, 0, 0 }, \
      ASTRA_TEXT_EMBEDDED_COLOR_USE, 0, { 0, 0, 0, 0, 0 } }

/**
 * Declare an empty face that closes itself on normal scope exit.
 *
 * Cleanup failures are ignored. Use astra_font_face_close() explicitly when
 * the result matters.
 */
#define ASTRA_AUTO_FONT_FACE(name) \
    AstraFontFace name ASTRA_CLEANUP(astra_font_face_cleanup) = \
        ASTRA_FONT_FACE_INIT
/**
 * Declare an empty font that closes itself on normal scope exit.
 *
 * Cleanup failures are ignored. Use astra_font_close() explicitly when the
 * result matters.
 */
#define ASTRA_AUTO_FONT(name) \
    AstraFont name ASTRA_CLEANUP(astra_font_cleanup) = ASTRA_FONT_INIT
/**
 * Declare an empty text layout that closes itself on normal scope exit.
 *
 * Cleanup failures are ignored. Use astra_text_layout_close() explicitly when
 * the result matters.
 */
#define ASTRA_AUTO_TEXT_LAYOUT(name) \
    AstraTextLayout name ASTRA_CLEANUP(astra_text_layout_cleanup) = \
        ASTRA_TEXT_LAYOUT_INIT

/**
 * Test whether the current backend provides the font and layout service.
 *
 * @return Nonzero when font open and layout operations are available.
 * @par Blocking
 * Never blocks.
 * @par Thread safety
 * Reentrant.
 * @since 0.1.0
 */
int astra_fonts_present(void);

/**
 * Open a face selected by a stable system role.
 *
 * @param[in] role One `ASTRA_FONT_ROLE_*` value.
 * @param[in,out] face Empty initialized wrapper that receives the face.
 * @retval ASTRA_OK The face was opened.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT The role or output wrapper is invalid.
 * @retval ASTRA_ERROR_NOT_PRESENT The font service or requested role is absent.
 * @retval ASTRA_ERROR_NO_RESOURCES The process or service handle limit was hit.
 * @return An ::AstraResult value.
 *
 * @par Ownership
 * On success, the caller owns @p face and must close it exactly once.
 * @par Blocking
 * May block while the OS font service resolves or validates a resident face.
 * @par Thread safety
 * Safe for concurrent calls using different output wrappers.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_font_face_open_system(
    uint32_t role, AstraFontFace *face);

/**
 * Open an installed face by exact UTF-8 family and optional style names.
 *
 * Strings are explicit byte spans and need not be null-terminated. Empty style
 * selects the family's normal style. Matching is case-insensitive according to
 * Unicode simple case folding, not the current process locale.
 *
 * @param[in] family_utf8 Nonempty valid UTF-8 family bytes.
 * @param[in] family_bytes Number of family bytes.
 * @param[in] style_utf8 Valid UTF-8 style bytes, or null when style_bytes is 0.
 * @param[in] style_bytes Number of style bytes.
 * @param[in,out] face Empty initialized wrapper that receives the face.
 * @retval ASTRA_OK The face was opened.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT A span, string, or wrapper is invalid.
 * @retval ASTRA_ERROR_NOT_PRESENT The service or requested face is absent.
 * @retval ASTRA_ERROR_NO_RESOURCES The process or service handle limit was hit.
 * @return An ::AstraResult value.
 *
 * @par Ownership
 * On success, the caller owns @p face and must close it exactly once.
 * @par Blocking
 * May block for filesystem lookup and AFNT validation.
 * @par Thread safety
 * Safe for concurrent calls using different output wrappers.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_font_face_open_family(
    const char *family_utf8,
    uint32_t family_bytes,
    const char *style_utf8,
    uint32_t style_bytes,
    AstraFontFace *face);

/**
 * Close a face handle and invalidate its wrapper.
 *
 * Existing fonts and layouts retain their internal strong references.
 *
 * @param[in,out] face Live face to consume.
 * @retval ASTRA_OK The face handle was closed.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT @p face is null.
 * @retval ASTRA_ERROR_INVALID_HANDLE The wrapper is empty, stale, or invalid.
 * @return An ::AstraResult value.
 *
 * @par Ownership
 * Consumes the face on success; a failed close leaves it unchanged.
 * @par Blocking
 * Does not perform file I/O, but an OS call may schedule.
 * @par Thread safety
 * Must not race with another operation using the same wrapper.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_font_face_close(AstraFontFace *face);

/**
 * Query immutable metadata for a face.
 *
 * Set `info->size` with ::ASTRA_FONT_INFO_INIT before calling. No native strike
 * is selected for a face, so the pixel dimensions and bitmap format are zero.
 *
 * @param[in] face Live face handle.
 * @param[in,out] info Versioned output record.
 * @retval ASTRA_OK Metadata was returned.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT An argument or size is invalid.
 * @retval ASTRA_ERROR_INVALID_HANDLE The face is empty, stale, or invalid.
 * @return An ::AstraResult value.
 * @par Ownership
 * No ownership is transferred.
 * @par Blocking
 * Never performs file I/O after the face has opened.
 * @par Thread safety
 * Reentrant for one live immutable face.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_font_face_get_info(
    const AstraFontFace *face, AstraFontInfo *info);

/**
 * Copy one null-terminated metadata string from a face.
 *
 * A null @p buffer with zero capacity queries the required byte count. The
 * required count includes the terminator. Insufficient capacity returns
 * ::ASTRA_ERROR_BUFFER_TOO_SMALL and writes no partial UTF-8 string.
 *
 * @param[in] face Live face handle.
 * @param[in] property One `ASTRA_FONT_STRING_*` value.
 * @param[out] buffer Destination buffer, or null when buffer_bytes is zero.
 * @param[in] buffer_bytes Destination capacity in bytes.
 * @param[out] required_bytes Receives required capacity including terminator.
 * @retval ASTRA_OK The complete string was copied.
 * @retval ASTRA_ERROR_BUFFER_TOO_SMALL The buffer is too small.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT An argument or property is invalid.
 * @retval ASTRA_ERROR_INVALID_HANDLE The face is empty, stale, or invalid.
 * @return An ::AstraResult value.
 * @par Ownership
 * The caller owns the copied bytes; no service storage is exposed.
 * @par Blocking
 * Never performs file I/O after the face has opened.
 * @par Thread safety
 * Reentrant for one live immutable face and distinct output buffers.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_font_face_get_string(
    const AstraFontFace *face,
    uint32_t property,
    char *buffer,
    uint32_t buffer_bytes,
    uint32_t *required_bytes);

/**
 * Resolve a face to a draw-ready native strike and fallback policy.
 *
 * The service never synthesizes runtime bitmap scaling. Without
 * ::ASTRA_FONT_MATCH_EXACT_STRIKE it selects the nearest designed strike and
 * reports its actual dimensions through astra_font_get_info(). The optional
 * language tag is an ASCII BCP 47 tag used for fallback and shaping; null
 * with zero bytes selects the process default. The NDK validates the tag's
 * alphanumeric subtag and hyphen structure. The font service determines
 * whether a syntactically valid tag is supported.
 *
 * @param[in] face Live face handle.
 * @param[in] request Versioned native-strike request.
 * @param[in] language_utf8 Optional syntactically valid ASCII BCP 47 tag.
 * @param[in] language_bytes Number of language-tag bytes.
 * @param[in,out] font Empty initialized wrapper that receives the font.
 * @retval ASTRA_OK A resolved font was created.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT A request, span, or wrapper is invalid.
 * @retval ASTRA_ERROR_INVALID_HANDLE The face is empty, stale, or invalid.
 * @retval ASTRA_ERROR_NOT_PRESENT No strike satisfies an exact requirement.
 * @retval ASTRA_ERROR_UNSUPPORTED A required bitmap capability is unavailable.
 * @retval ASTRA_ERROR_NO_RESOURCES A handle or cache limit was reached.
 * @return An ::AstraResult value.
 *
 * @par Ownership
 * On success, the caller owns @p font. It retains the face internally, so the
 * application may close its face wrapper immediately afterward.
 * @par Blocking
 * May block while a strike is validated or converted into a protected cache.
 * @par Thread safety
 * Safe for concurrent calls using one immutable face and distinct outputs.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_font_create(
    const AstraFontFace *face,
    const AstraFontRequest *request,
    const char *language_utf8,
    uint32_t language_bytes,
    AstraFont *font);

/**
 * Close a resolved font and invalidate its wrapper.
 *
 * Existing layouts retain their internal strong references.
 *
 * @param[in,out] font Live font to consume.
 * @retval ASTRA_OK The font handle was closed.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT @p font is null.
 * @retval ASTRA_ERROR_INVALID_HANDLE The wrapper is empty, stale, or invalid.
 * @return An ::AstraResult value.
 * @par Ownership
 * Consumes the font on success; a failed close leaves it unchanged.
 * @par Blocking
 * Does not perform file I/O, but an OS call may schedule.
 * @par Thread safety
 * Must not race with another operation using the same wrapper.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_font_close(AstraFont *font);

/**
 * Query immutable face and selected-strike metadata.
 *
 * @param[in] font Live resolved font.
 * @param[in,out] info Output initialized with ::ASTRA_FONT_INFO_INIT.
 * @retval ASTRA_OK Metadata was returned.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT An argument or size is invalid.
 * @retval ASTRA_ERROR_INVALID_HANDLE The font is empty, stale, or invalid.
 * @return An ::AstraResult value.
 * @par Ownership
 * No ownership is transferred.
 * @par Blocking
 * Never performs file I/O after font creation.
 * @par Thread safety
 * Reentrant for one live immutable font.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_font_get_info(
    const AstraFont *font, AstraFontInfo *info);

/**
 * Copy one null-terminated metadata string from a resolved font.
 *
 * Buffer-query and failure behavior matches astra_font_face_get_string().
 *
 * @param[in] font Live resolved font.
 * @param[in] property One `ASTRA_FONT_STRING_*` value.
 * @param[out] buffer Destination buffer, or null when buffer_bytes is zero.
 * @param[in] buffer_bytes Destination capacity in bytes.
 * @param[out] required_bytes Receives required capacity including terminator.
 * @retval ASTRA_OK The complete string was copied.
 * @retval ASTRA_ERROR_BUFFER_TOO_SMALL The buffer is too small.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT An argument or property is invalid.
 * @retval ASTRA_ERROR_INVALID_HANDLE The font is empty, stale, or invalid.
 * @return An ::AstraResult value.
 * @par Ownership
 * The caller owns the copied bytes.
 * @par Blocking
 * Never performs file I/O after font creation.
 * @par Thread safety
 * Reentrant for one live immutable font and distinct output buffers.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_font_get_string(
    const AstraFont *font,
    uint32_t property,
    char *buffer,
    uint32_t buffer_bytes,
    uint32_t *required_bytes);

/**
 * Query baseline-relative metrics for a resolved native strike.
 *
 * @param[in] font Live resolved font.
 * @param[in,out] metrics Output initialized with ::ASTRA_FONT_METRICS_INIT.
 * @retval ASTRA_OK Metrics were returned.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT An argument or size is invalid.
 * @retval ASTRA_ERROR_INVALID_HANDLE The font is empty, stale, or invalid.
 * @return An ::AstraResult value.
 * @par Ownership
 * No ownership is transferred.
 * @par Blocking
 * Never performs file I/O after font creation.
 * @par Thread safety
 * Reentrant for one live immutable font.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_font_get_metrics(
    const AstraFont *font, AstraFontMetrics *metrics);

/**
 * Create an immutable layout from copied UTF-8 text.
 *
 * The source span may contain embedded null bytes only when they represent the
 * Unicode null scalar in otherwise valid UTF-8. Byte offsets in later calls
 * always identify validated scalar or grapheme boundaries. A null options
 * pointer selects ::ASTRA_TEXT_LAYOUT_OPTIONS_INIT behavior.
 *
 * @param[in] font Live primary font and fallback policy.
 * @param[in] utf8 Source text bytes, or null when utf8_bytes is zero.
 * @param[in] utf8_bytes Number of source bytes.
 * @param[in] options Optional versioned layout policy.
 * @param[in,out] layout Empty initialized wrapper that receives the layout.
 * @retval ASTRA_OK The immutable layout was created.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT Text, options, or output is invalid.
 * @retval ASTRA_ERROR_INVALID_HANDLE The font is empty, stale, or invalid.
 * @retval ASTRA_ERROR_NO_RESOURCES A layout memory or complexity limit was hit.
 * @retval ASTRA_ERROR_UNSUPPORTED Required shaping support is unavailable.
 * @return An ::AstraResult value.
 *
 * @par Ownership
 * On success, the caller owns @p layout. The service copies @p utf8 and retains
 * every referenced font; caller buffers and font wrappers may then be released.
 * @par Blocking
 * May block for shaping, fallback resolution, and protected strike caching.
 * @par Thread safety
 * Safe for concurrent calls using one immutable font and distinct outputs.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_text_layout_create(
    const AstraFont *font,
    const char *utf8,
    uint32_t utf8_bytes,
    const AstraTextLayoutOptions *options,
    AstraTextLayout *layout);

/**
 * Close a text layout and invalidate its wrapper.
 *
 * A graphics submission retains its own references until its completion fence
 * signals, so closing after submission does not invalidate queued work.
 *
 * @param[in,out] layout Live layout to consume.
 * @retval ASTRA_OK The layout handle was closed.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT @p layout is null.
 * @retval ASTRA_ERROR_INVALID_HANDLE The wrapper is empty, stale, or invalid.
 * @return An ::AstraResult value.
 * @par Ownership
 * Consumes the layout on success; a failed close leaves it unchanged.
 * @par Blocking
 * Does not wait for submitted graphics work.
 * @par Thread safety
 * Must not race with another operation using the same wrapper.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_text_layout_close(AstraTextLayout *layout);

/**
 * Return logical and ink measurements for an immutable layout.
 *
 * @param[in] layout Live text layout.
 * @param[in,out] metrics Output initialized with ::ASTRA_TEXT_METRICS_INIT.
 * @retval ASTRA_OK Measurements were returned.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT An argument or size is invalid.
 * @retval ASTRA_ERROR_INVALID_HANDLE The layout is empty, stale, or invalid.
 * @return An ::AstraResult value.
 * @par Ownership
 * No ownership is transferred.
 * @par Blocking
 * Never reshapes or performs file I/O.
 * @par Thread safety
 * Reentrant for one live immutable layout.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_text_layout_measure(
    const AstraTextLayout *layout, AstraTextMetrics *metrics);

/**
 * Map a layout-local point to the nearest valid source caret boundary.
 *
 * Points outside the layout still return the nearest boundary, with the INSIDE
 * flag clear. The result never splits a UTF-8 scalar or shaped grapheme.
 *
 * @param[in] layout Live text layout.
 * @param[in] x Layout-local x coordinate.
 * @param[in] y Layout-local y coordinate.
 * @param[in,out] hit Output initialized with ::ASTRA_TEXT_HIT_INIT.
 * @retval ASTRA_OK A source position and caret geometry were returned.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT An argument or size is invalid.
 * @retval ASTRA_ERROR_INVALID_HANDLE The layout is empty, stale, or invalid.
 * @return An ::AstraResult value.
 * @par Ownership
 * No ownership is transferred.
 * @par Blocking
 * Never reshapes or performs file I/O.
 * @par Thread safety
 * Reentrant for one live immutable layout.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_text_layout_hit_test(
    const AstraTextLayout *layout,
    AstraFixed26_6 x,
    AstraFixed26_6 y,
    AstraTextHit *hit);

/**
 * Return caret geometry for a valid UTF-8 source boundary.
 *
 * @param[in] layout Live text layout.
 * @param[in] utf8_byte_offset Source boundary from zero through source length.
 * @param[in] edge One `ASTRA_TEXT_EDGE_*` value.
 * @param[in,out] hit Output initialized with ::ASTRA_TEXT_HIT_INIT.
 * @retval ASTRA_OK Caret geometry was returned.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT The offset, edge, output, or size is invalid.
 * @retval ASTRA_ERROR_INVALID_HANDLE The layout is empty, stale, or invalid.
 * @return An ::AstraResult value.
 * @par Ownership
 * No ownership is transferred.
 * @par Blocking
 * Never reshapes or performs file I/O.
 * @par Thread safety
 * Reentrant for one live immutable layout.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_text_layout_get_caret(
    const AstraTextLayout *layout,
    uint32_t utf8_byte_offset,
    uint32_t edge,
    AstraTextHit *hit);

/**
 * Cleanup callback used by ::ASTRA_AUTO_FONT_FACE.
 *
 * @param[in,out] face Face to close when live; null and empty wrappers are ignored.
 * @par Blocking
 * Does not perform file I/O.
 * @par Thread safety
 * Must not race with another operation using the same wrapper.
 * @since 0.1.0
 */
void astra_font_face_cleanup(AstraFontFace *face);

/**
 * Cleanup callback used by ::ASTRA_AUTO_FONT.
 *
 * @param[in,out] font Font to close when live; null and empty wrappers are ignored.
 * @par Blocking
 * Does not perform file I/O.
 * @par Thread safety
 * Must not race with another operation using the same wrapper.
 * @since 0.1.0
 */
void astra_font_cleanup(AstraFont *font);

/**
 * Cleanup callback used by ::ASTRA_AUTO_TEXT_LAYOUT.
 *
 * @param[in,out] layout Layout to close when live; null and empty wrappers are ignored.
 * @par Blocking
 * Does not wait for submitted graphics work.
 * @par Thread safety
 * Must not race with another operation using the same wrapper.
 * @since 0.1.0
 */
void astra_text_layout_cleanup(AstraTextLayout *layout);

/** @} */

ASTRA_EXTERN_C_END

#endif
