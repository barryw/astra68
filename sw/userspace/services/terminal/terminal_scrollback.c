#include <terminal_scrollback.h>

#include <limits.h>
#include <string.h>

static int bytes_fit(uint32_t count, size_t size)
{
    return size == 0u || count <= SIZE_MAX / size;
}

static uint32_t grown_capacity(uint32_t capacity, uint32_t required,
                               size_t element_size)
{
    uint32_t grown = capacity == 0u ? 1u : capacity;
    uint32_t maximum = element_size > SIZE_MAX / UINT32_MAX ?
        (uint32_t)(SIZE_MAX / element_size) : UINT32_MAX;

    if (required > maximum)
        return 0u;
    while (grown < required) {
        if (grown > maximum / 2u) {
            grown = maximum;
            break;
        }
        grown *= 2u;
    }
    return grown;
}

static int set_extents(TerminalScrollback *backlog, uint32_t width,
                       uint32_t viewport_rows)
{
    uint64_t content_rows = (uint64_t)backlog->row_count + viewport_rows;
    uint64_t content_height = content_rows * backlog->line_height;
    uint64_t viewport_height = (uint64_t)viewport_rows * backlog->line_height;

    if (content_height > UINT32_MAX || viewport_height > UINT32_MAX)
        return 0;
    if (astra_scroll_set_extents(&backlog->scroll, width,
                                 (uint32_t)content_height, width,
                                 (uint32_t)viewport_height) != ASTRA_OK)
        return 0;
    backlog->viewport_rows = viewport_rows;
    return 1;
}

int terminal_scrollback_init(TerminalScrollback *backlog,
                             uint32_t viewport_width,
                             uint32_t viewport_rows,
                             uint32_t line_height,
                             TerminalScrollbackReallocate reallocate)
{
    AstraScrollModelInfo info = ASTRA_SCROLL_MODEL_INFO_INIT;
    uint64_t height = (uint64_t)viewport_rows * line_height;

    if (backlog == NULL || viewport_rows == 0u || line_height == 0u ||
        reallocate == NULL || height > UINT32_MAX)
        return 0;
    (void)memset(backlog, 0, sizeof(*backlog));
    backlog->reallocate = reallocate;
    backlog->viewport_rows = viewport_rows;
    backlog->line_height = line_height;
    info.content_width = viewport_width;
    info.viewport_width = viewport_width;
    info.content_height = (uint32_t)height;
    info.viewport_height = (uint32_t)height;
    info.line_height = line_height;
    return astra_scroll_init(&backlog->scroll, &info) == ASTRA_OK;
}

void terminal_scrollback_destroy(TerminalScrollback *backlog)
{
    if (backlog == NULL || backlog->reallocate == NULL)
        return;
    (void)backlog->reallocate(backlog->rows, 0u);
    (void)backlog->reallocate(backlog->cells, 0u);
    (void)memset(backlog, 0, sizeof(*backlog));
}

int terminal_scrollback_append(TerminalScrollback *backlog,
                               const AstraTextCell *cells,
                               uint32_t columns)
{
    AstraScrollState before = ASTRA_SCROLL_STATE_INIT;
    TerminalScrollbackRow *rows;
    AstraTextCell *stored_cells;
    uint32_t row_capacity;
    uint32_t cell_capacity;
    uint32_t required_cells;
    int pinned;

    if (backlog == NULL || backlog->reallocate == NULL || cells == NULL ||
        columns == 0u || backlog->row_count == UINT32_MAX ||
        columns > UINT32_MAX - backlog->cell_count ||
        ((uint64_t)backlog->row_count + 1u + backlog->viewport_rows) *
            backlog->line_height > UINT32_MAX ||
        astra_scroll_get_state(&backlog->scroll, &before) != ASTRA_OK)
        return 0;
    required_cells = backlog->cell_count + columns;
    row_capacity = grown_capacity(backlog->row_capacity,
                                  backlog->row_count + 1u,
                                  sizeof(*backlog->rows));
    cell_capacity = grown_capacity(backlog->cell_capacity, required_cells,
                                   sizeof(*backlog->cells));
    if (row_capacity == 0u || cell_capacity == 0u ||
        !bytes_fit(row_capacity, sizeof(*backlog->rows)) ||
        !bytes_fit(cell_capacity, sizeof(*backlog->cells)))
        return 0;
    rows = backlog->rows;
    if (row_capacity != backlog->row_capacity) {
        rows = backlog->reallocate(
            rows, (size_t)row_capacity * sizeof(*rows));
        if (rows == NULL)
            return 0;
        backlog->rows = rows;
        backlog->row_capacity = row_capacity;
    }
    stored_cells = backlog->cells;
    if (cell_capacity != backlog->cell_capacity) {
        stored_cells = backlog->reallocate(
            stored_cells, (size_t)cell_capacity * sizeof(*stored_cells));
        if (stored_cells == NULL)
            return 0;
        backlog->cells = stored_cells;
        backlog->cell_capacity = cell_capacity;
    }
    pinned = before.offset_y == before.maximum_y;
    backlog->rows[backlog->row_count++] =
        (TerminalScrollbackRow){backlog->cell_count, columns};
    (void)memcpy(backlog->cells + backlog->cell_count, cells,
                 (size_t)columns * sizeof(*cells));
    backlog->cell_count = required_cells;
    if (!set_extents(backlog, before.viewport_width,
                     backlog->viewport_rows))
        return 0;
    return !pinned || terminal_scrollback_to_bottom(backlog);
}

int terminal_scrollback_resize(TerminalScrollback *backlog,
                               uint32_t viewport_width,
                               uint32_t viewport_rows)
{
    if (backlog == NULL || viewport_rows == 0u)
        return 0;
    return set_extents(backlog, viewport_width, viewport_rows);
}

static int32_t wheel_pixels(int32_t notches, uint32_t step)
{
    int64_t pixels = -(int64_t)notches * step;

    if (pixels < INT32_MIN)
        return INT32_MIN;
    if (pixels > INT32_MAX)
        return INT32_MAX;
    return (int32_t)pixels;
}

int terminal_scrollback_wheel(TerminalScrollback *backlog,
                              int32_t delta_x, int32_t delta_y)
{
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;

    if (backlog == NULL ||
        astra_scroll_get_state(&backlog->scroll, &state) != ASTRA_OK)
        return 0;
    return astra_scroll_by(&backlog->scroll,
                           wheel_pixels(delta_x, state.line_width),
                           wheel_pixels(delta_y, state.line_height)) ==
        ASTRA_OK;
}

int terminal_scrollback_to_bottom(TerminalScrollback *backlog)
{
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;

    if (backlog == NULL ||
        astra_scroll_get_state(&backlog->scroll, &state) != ASTRA_OK)
        return 0;
    return astra_scroll_set_offset(&backlog->scroll, state.offset_x,
                                   state.maximum_y) == ASTRA_OK;
}

int terminal_scrollback_at_bottom(const TerminalScrollback *backlog)
{
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;

    return backlog != NULL &&
           astra_scroll_get_state(&backlog->scroll, &state) == ASTRA_OK &&
           state.offset_y == state.maximum_y;
}

const AstraTextCell *terminal_scrollback_row(
    const TerminalScrollback *backlog, uint32_t row, uint32_t *columns)
{
    if (backlog == NULL || columns == NULL || row >= backlog->row_count)
        return NULL;
    *columns = backlog->rows[row].columns;
    return backlog->cells + backlog->rows[row].offset;
}
