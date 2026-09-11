#ifndef ASTRA_TEXT_SURFACE_H
#define ASTRA_TEXT_SURFACE_H

/** @file text_surface.h @brief Shared grid/code/flow text presentation. */

#include <stdint.h>

#include <astra/attributes.h>
#include <astra/surface.h>
#include <astra/text_style.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/** Select the TextSurface's configured default color. */
#define ASTRA_TEXT_COLOR_DEFAULT UINT32_MAX
/** Encode one literal 24-bit RGB logical color. */
#define ASTRA_TEXT_COLOR_RGB(red, green, blue)                              \
    (UINT32_C(0x01000000) | ((uint32_t)(red) << 16u) |                     \
     ((uint32_t)(green) << 8u) | (uint32_t)(blue))
/** Test whether a logical color contains a literal RGB value. */
#define ASTRA_TEXT_COLOR_IS_RGB(color)                                      \
    (((color) & UINT32_C(0xff000000)) == UINT32_C(0x01000000))

/** Text layout mode. Grid is implemented by Interface Kit 2.5. */
enum {
    ASTRA_TEXT_SURFACE_GRID = 1u,
    ASTRA_TEXT_SURFACE_CODE = 2u,
    ASTRA_TEXT_SURFACE_FLOW = 3u
};

/** Caret presentation. */
enum {
    ASTRA_TEXT_CARET_BLOCK = 1u,
    ASTRA_TEXT_CARET_BAR = 2u,
    ASTRA_TEXT_CARET_UNDERLINE = 3u
};

/** One fixed-grid scalar and its logical colors and styles. */
typedef struct AstraTextCell {
    /** Unicode scalar value, or zero for an empty cell. */
    uint32_t codepoint;
    /** Logical foreground color. */
    uint32_t foreground;
    /** Logical background color. */
    uint32_t background;
    /** Combination of ASTRA_TEXT_STYLE_* attributes. */
    uint16_t attributes;
    /** Display-cell width; zero marks a continuation cell. */
    uint8_t width;
    /** Must be zero. */
    uint8_t reserved;
} AstraTextCell;

/** One cell boundary in a fixed-grid text surface. */
typedef struct AstraTextGridPosition {
    /** Zero-based row. */
    uint32_t row;
    /** Zero-based cell boundary. */
    uint32_t column;
} AstraTextGridPosition;

/** Half-open fixed-grid selection with logical foreground/background colors. */
typedef struct AstraTextGridSelection {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** Fixed end of the selection. */
    AstraTextGridPosition anchor;
    /** Moving end of the selection. */
    AstraTextGridPosition focus;
    /** Logical selection foreground. */
    uint32_t foreground;
    /** Logical selection background. */
    uint32_t background;
    /** Must be zero. */
    uint32_t reserved[4];
} AstraTextGridSelection;

/** Empty half-open grid selection. */
#define ASTRA_TEXT_GRID_SELECTION_INIT {                                   \
    sizeof(AstraTextGridSelection), { 0u, 0u }, { 0u, 0u },                \
    ASTRA_TEXT_COLOR_DEFAULT, ASTRA_TEXT_COLOR_DEFAULT, { 0u, 0u, 0u, 0u } \
}

/**
 * Resolve one logical TextSurface color to RGB565.
 * @param context Application context supplied at initialization.
 * @param color Logical default, palette, or literal RGB color.
 * @param fallback RGB565 fallback for ::ASTRA_TEXT_COLOR_DEFAULT.
 * @return Resolved RGB565 color.
 */
typedef uint16_t (*AstraTextColorResolver)(void *context, uint32_t color,
                                          uint16_t fallback);

/** Construction data for the grid renderer; caller retains scratch storage. */
typedef struct AstraTextSurfaceInfo {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** ASTRA_TEXT_SURFACE_* layout mode. */
    uint32_t mode;
    /** AFNT strike height in pixels. */
    uint16_t font_height;
    /** Fixed grid cell advance in pixels. */
    uint16_t cell_width;
    /** Baseline-to-baseline grid distance in pixels. */
    uint16_t line_height;
    /** Default RGB565 foreground. */
    uint16_t default_foreground;
    /** Default RGB565 background. */
    uint16_t default_background;
    /** Must be zero. */
    uint16_t reserved16;
    /** Caller-owned draw scratch storage retained by the surface. */
    char *scratch;
    /** Bytes available at @ref scratch. */
    uint32_t scratch_bytes;
    /** Optional logical-color resolver. */
    AstraTextColorResolver resolve_color;
    /** Opaque context passed to @ref resolve_color. */
    void *color_context;
    /** Must be zero. */
    uint32_t reserved[4];
} AstraTextSurfaceInfo;

/** Empty TextSurface construction data. */
#define ASTRA_TEXT_SURFACE_INFO_INIT {                                      \
    sizeof(AstraTextSurfaceInfo), ASTRA_TEXT_SURFACE_GRID, 0, 0, 0,         \
    0xffffu, 0u, 0u, 0, 0u, 0, 0, { 0, 0, 0, 0 }                          \
}

/** Caller-owned TextSurface state. Private fields must not be modified. */
typedef struct AstraTextSurface {
    /** @cond ASTRA_INTERNAL */
    uint32_t _private_structure_size;
    uint32_t _private_mode;
    uint16_t _private_font_height;
    uint16_t _private_cell_width;
    uint16_t _private_line_height;
    uint16_t _private_default_foreground;
    uint16_t _private_default_background;
    uint16_t _private_reserved16;
    char *_private_scratch;
    uint32_t _private_scratch_bytes;
    AstraTextColorResolver _private_resolve_color;
    void *_private_color_context;
    uint32_t _private_blink_visible;
    uint32_t _private_reserved[3];
    /** @endcond */
} AstraTextSurface;

/** Empty caller-owned TextSurface state. */
#define ASTRA_TEXT_SURFACE_INIT {                                           \
    sizeof(AstraTextSurface), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1u,        \
    { 0, 0, 0 }                                                            \
}

/**
 * Initialize a caller-owned TextSurface.
 * @param text_surface Empty state initialized with ::ASTRA_TEXT_SURFACE_INIT.
 * @param info Complete construction data and retained scratch storage.
 * @return ::ASTRA_OK or a negative ::AstraResult validation error.
 */
ASTRA_NODISCARD AstraResult astra_text_surface_init(
    AstraTextSurface *text_surface, const AstraTextSurfaceInfo *info);
/**
 * Render a contiguous fixed-grid cell run.
 * @param text_surface Initialized grid TextSurface.
 * @param target Destination surface.
 * @param origin_x Grid origin x coordinate.
 * @param origin_y Grid origin y coordinate.
 * @param row Zero-based destination row.
 * @param column Zero-based destination column.
 * @param cells Cells to render.
 * @param count Number of cells.
 * @return ::ASTRA_OK or a negative ::AstraResult.
 */
ASTRA_NODISCARD AstraResult astra_text_surface_render_cells(
    const AstraTextSurface *text_surface, AstraSurfaceView *target,
    int32_t origin_x, int32_t origin_y, uint32_t row, uint32_t column,
    const AstraTextCell *cells, uint32_t count);
/**
 * Render cells with an optional half-open selection overlay.
 * @param text_surface Initialized grid TextSurface.
 * @param target Destination surface.
 * @param origin_x Grid origin x coordinate.
 * @param origin_y Grid origin y coordinate.
 * @param row Zero-based destination row.
 * @param column Zero-based destination column.
 * @param cells Cells to render.
 * @param count Number of cells.
 * @param selection Optional validated grid selection.
 * @return ::ASTRA_OK or a negative ::AstraResult.
 */
ASTRA_NODISCARD AstraResult astra_text_surface_render_grid(
    const AstraTextSurface *text_surface, AstraSurfaceView *target,
    int32_t origin_x, int32_t origin_y, uint32_t row, uint32_t column,
    const AstraTextCell *cells, uint32_t count,
    const AstraTextGridSelection *selection);
/**
 * Map a content-local point to the nearest fixed-grid cell boundary.
 * @param text_surface Initialized grid TextSurface.
 * @param origin_x Grid origin x coordinate.
 * @param origin_y Grid origin y coordinate.
 * @param columns Visible column count.
 * @param rows Visible row count.
 * @param x Point x coordinate.
 * @param y Point y coordinate.
 * @param position Receives the clamped cell boundary.
 * @return ::ASTRA_OK or a negative ::AstraResult.
 */
ASTRA_NODISCARD AstraResult astra_text_surface_grid_hit_test(
    const AstraTextSurface *text_surface, int32_t origin_x, int32_t origin_y,
    uint32_t columns, uint32_t rows, int32_t x, int32_t y,
    AstraTextGridPosition *position);
/**
 * Copy a half-open grid selection as UTF-8, trimming trailing spaces on each
 * selected row. Newlines preserve selected row boundaries. The operation is
 * atomic: a short output buffer is not modified and `bytes` receives the
 * required size. `output` may be null only when `capacity` is zero.
 * @param text_surface Initialized grid TextSurface.
 * @param cells Row-major cell storage.
 * @param stride Stored cells between row starts; at least @p columns.
 * @param columns Visible column count.
 * @param rows Visible row count.
 * @param selection Selection to normalize and extract.
 * @param output Optional UTF-8 destination.
 * @param capacity Bytes available at @p output.
 * @param bytes Receives the required or written byte count.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_BUFFER_TOO_SMALL, or another negative
 *         ::AstraResult.
 */
ASTRA_NODISCARD AstraResult astra_text_surface_copy_grid_selection(
    const AstraTextSurface *text_surface, const AstraTextCell *cells,
    uint32_t stride, uint32_t columns, uint32_t rows,
    const AstraTextGridSelection *selection, char *output,
    uint32_t capacity, uint32_t *bytes);
/**
 * Draw one caret in grid coordinates.
 * @param text_surface Initialized grid TextSurface.
 * @param target Destination surface.
 * @param origin_x Grid origin x coordinate.
 * @param origin_y Grid origin y coordinate.
 * @param row Zero-based row.
 * @param column Zero-based column.
 * @param kind ASTRA_TEXT_CARET_* presentation.
 * @param color RGB565 caret color.
 * @return ::ASTRA_OK or a negative ::AstraResult.
 */
ASTRA_NODISCARD AstraResult astra_text_surface_draw_caret(
    const AstraTextSurface *text_surface, AstraSurfaceView *target,
    int32_t origin_x, int32_t origin_y, uint32_t row, uint32_t column,
    uint32_t kind, uint16_t color);
/**
 * Scroll a rectangular grid using the shared hardware-blit path.
 * @param text_surface Initialized grid TextSurface.
 * @param target Destination surface.
 * @param origin_x Grid origin x coordinate.
 * @param origin_y Grid origin y coordinate.
 * @param columns Width in cells.
 * @param rows Height in cells.
 * @param moved_rows Source displacement in rows.
 * @param preserved_rows Rows copied into their new position.
 * @return ::ASTRA_OK or a negative ::AstraResult.
 */
ASTRA_NODISCARD AstraResult astra_text_surface_scroll(
    const AstraTextSurface *text_surface, AstraSurfaceView *target,
    uint32_t origin_x, uint32_t origin_y, uint32_t columns, uint32_t rows,
    uint32_t moved_rows, uint32_t preserved_rows);
/**
 * Set the phase used by blinking cells.
 * @param text_surface Initialized TextSurface.
 * @param visible Nonzero to show blink-marked cells.
 * @return ::ASTRA_OK or a negative ::AstraResult validation error.
 */
ASTRA_NODISCARD AstraResult astra_text_surface_set_blink(
    AstraTextSurface *text_surface, uint32_t visible);

ASTRA_EXTERN_C_END

#endif
