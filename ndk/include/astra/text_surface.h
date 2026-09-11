#ifndef ASTRA_TEXT_SURFACE_H
#define ASTRA_TEXT_SURFACE_H

/** @file text_surface.h @brief Shared grid/code/flow text presentation. */

#include <stdint.h>

#include <astra/attributes.h>
#include <astra/surface.h>
#include <astra/text_style.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

#define ASTRA_TEXT_COLOR_DEFAULT UINT32_MAX
#define ASTRA_TEXT_COLOR_RGB(red, green, blue)                              \
    (UINT32_C(0x01000000) | ((uint32_t)(red) << 16u) |                     \
     ((uint32_t)(green) << 8u) | (uint32_t)(blue))
#define ASTRA_TEXT_COLOR_IS_RGB(color)                                      \
    (((color) & UINT32_C(0xff000000)) == UINT32_C(0x01000000))

enum {
    ASTRA_TEXT_SURFACE_GRID = 1u,
    ASTRA_TEXT_SURFACE_CODE = 2u,
    ASTRA_TEXT_SURFACE_FLOW = 3u
};

enum {
    ASTRA_TEXT_CARET_BLOCK = 1u,
    ASTRA_TEXT_CARET_BAR = 2u,
    ASTRA_TEXT_CARET_UNDERLINE = 3u
};

/** One fixed-grid scalar and its logical colors and styles. */
typedef struct AstraTextCell {
    uint32_t codepoint;
    uint32_t foreground;
    uint32_t background;
    uint16_t attributes;
    uint8_t width;
    uint8_t reserved;
} AstraTextCell;

/** One cell boundary in a fixed-grid text surface. */
typedef struct AstraTextGridPosition {
    uint32_t row;
    uint32_t column;
} AstraTextGridPosition;

/** Half-open fixed-grid selection with logical foreground/background colors. */
typedef struct AstraTextGridSelection {
    uint32_t size;
    AstraTextGridPosition anchor;
    AstraTextGridPosition focus;
    uint32_t foreground;
    uint32_t background;
    uint32_t reserved[4];
} AstraTextGridSelection;

#define ASTRA_TEXT_GRID_SELECTION_INIT {                                   \
    sizeof(AstraTextGridSelection), { 0u, 0u }, { 0u, 0u },                \
    ASTRA_TEXT_COLOR_DEFAULT, ASTRA_TEXT_COLOR_DEFAULT, { 0u, 0u, 0u, 0u } \
}

typedef uint16_t (*AstraTextColorResolver)(void *context, uint32_t color,
                                           uint16_t fallback);

/** Construction data for the grid renderer; caller retains scratch storage. */
typedef struct AstraTextSurfaceInfo {
    uint32_t size;
    uint32_t mode;
    uint16_t font_height;
    uint16_t cell_width;
    uint16_t line_height;
    uint16_t default_foreground;
    uint16_t default_background;
    uint16_t reserved16;
    char *scratch;
    uint32_t scratch_bytes;
    AstraTextColorResolver resolve_color;
    void *color_context;
    uint32_t reserved[4];
} AstraTextSurfaceInfo;

#define ASTRA_TEXT_SURFACE_INFO_INIT {                                      \
    sizeof(AstraTextSurfaceInfo), ASTRA_TEXT_SURFACE_GRID, 0, 0, 0,         \
    0xffffu, 0u, 0u, 0, 0u, 0, 0, { 0, 0, 0, 0 }                          \
}

/** Caller-owned TextSurface state. Private fields must not be modified. */
typedef struct AstraTextSurface {
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
} AstraTextSurface;

#define ASTRA_TEXT_SURFACE_INIT {                                           \
    sizeof(AstraTextSurface), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1u,        \
    { 0, 0, 0 }                                                            \
}

ASTRA_NODISCARD AstraResult astra_text_surface_init(
    AstraTextSurface *text_surface, const AstraTextSurfaceInfo *info);
ASTRA_NODISCARD AstraResult astra_text_surface_render_cells(
    const AstraTextSurface *text_surface, AstraSurfaceView *target,
    int32_t origin_x, int32_t origin_y, uint32_t row, uint32_t column,
    const AstraTextCell *cells, uint32_t count);
/** Render cells with an optional half-open selection overlay. */
ASTRA_NODISCARD AstraResult astra_text_surface_render_grid(
    const AstraTextSurface *text_surface, AstraSurfaceView *target,
    int32_t origin_x, int32_t origin_y, uint32_t row, uint32_t column,
    const AstraTextCell *cells, uint32_t count,
    const AstraTextGridSelection *selection);
/** Map a content-local point to the nearest fixed-grid cell boundary. */
ASTRA_NODISCARD AstraResult astra_text_surface_grid_hit_test(
    const AstraTextSurface *text_surface, int32_t origin_x, int32_t origin_y,
    uint32_t columns, uint32_t rows, int32_t x, int32_t y,
    AstraTextGridPosition *position);
/**
 * Copy a half-open grid selection as UTF-8, trimming trailing spaces on each
 * selected row. Newlines preserve selected row boundaries. The operation is
 * atomic: a short output buffer is not modified and `bytes` receives the
 * required size. `output` may be null only when `capacity` is zero.
 */
ASTRA_NODISCARD AstraResult astra_text_surface_copy_grid_selection(
    const AstraTextSurface *text_surface, const AstraTextCell *cells,
    uint32_t columns, uint32_t rows,
    const AstraTextGridSelection *selection, char *output,
    uint32_t capacity, uint32_t *bytes);
ASTRA_NODISCARD AstraResult astra_text_surface_draw_caret(
    const AstraTextSurface *text_surface, AstraSurfaceView *target,
    int32_t origin_x, int32_t origin_y, uint32_t row, uint32_t column,
    uint32_t kind, uint16_t color);
ASTRA_NODISCARD AstraResult astra_text_surface_scroll(
    const AstraTextSurface *text_surface, AstraSurfaceView *target,
    uint32_t origin_x, uint32_t origin_y, uint32_t columns, uint32_t rows,
    uint32_t moved_rows, uint32_t preserved_rows);
ASTRA_NODISCARD AstraResult astra_text_surface_set_blink(
    AstraTextSurface *text_surface, uint32_t visible);

ASTRA_EXTERN_C_END

#endif
