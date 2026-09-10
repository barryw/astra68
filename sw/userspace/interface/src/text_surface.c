#include <astra/text_surface.h>
#include <astra/utf8.h>

#include <astra/bytes.h>

#include <limits.h>
#include <stddef.h>

static int valid(const AstraTextSurface *text)
{
    return text != NULL &&
           text->_private_structure_size == sizeof(*text) &&
           text->_private_mode == ASTRA_TEXT_SURFACE_GRID &&
           text->_private_font_height != 0u &&
           text->_private_cell_width != 0u &&
           text->_private_line_height >= text->_private_font_height &&
           text->_private_scratch != NULL &&
           text->_private_scratch_bytes >= 4u &&
           text->_private_blink_visible <= 1u &&
           text->_private_reserved16 == 0u &&
           astra_words_zero(text->_private_reserved, 3u);
}

static uint16_t color(const AstraTextSurface *text, uint32_t value,
                      uint16_t fallback, int *ok)
{
    if (text->_private_resolve_color != NULL)
        return text->_private_resolve_color(
            text->_private_color_context, value, fallback);
    if (value == ASTRA_TEXT_COLOR_DEFAULT)
        return fallback;
    if (value > UINT16_MAX) {
        *ok = 0;
        return 0u;
    }
    return (uint16_t)value;
}

static uint16_t dim(uint16_t foreground, uint16_t background)
{
    uint32_t red = ((foreground >> 11u) + (background >> 11u)) / 2u;
    uint32_t green = (((foreground >> 5u) & 0x3fu) +
                      ((background >> 5u) & 0x3fu)) / 2u;
    uint32_t blue = ((foreground & 0x1fu) + (background & 0x1fu)) / 2u;

    return (uint16_t)((red << 11u) | (green << 5u) | blue);
}

AstraResult astra_text_surface_init(AstraTextSurface *text,
                                    const AstraTextSurfaceInfo *info)
{
    if (text == NULL || info == NULL ||
        text->_private_structure_size != sizeof(*text) ||
        info->size != sizeof(*info) ||
        info->mode != ASTRA_TEXT_SURFACE_GRID ||
        info->font_height == 0u || info->cell_width == 0u ||
        info->line_height < info->font_height || info->reserved16 != 0u ||
        info->scratch == NULL || info->scratch_bytes < 4u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *text = (AstraTextSurface){
        ._private_structure_size = sizeof(*text),
        ._private_mode = info->mode,
        ._private_font_height = info->font_height,
        ._private_cell_width = info->cell_width,
        ._private_line_height = info->line_height,
        ._private_default_foreground = info->default_foreground,
        ._private_default_background = info->default_background,
        ._private_scratch = info->scratch,
        ._private_scratch_bytes = info->scratch_bytes,
        ._private_resolve_color = info->resolve_color,
        ._private_color_context = info->color_context,
        ._private_blink_visible = 1u,
    };
    return ASTRA_OK;
}

static int run_matches(const AstraTextCell *left, const AstraTextCell *right)
{
    return left->foreground == right->foreground &&
           left->background == right->background &&
           left->attributes == right->attributes;
}

static int position_before(AstraTextGridPosition left,
                           AstraTextGridPosition right)
{
    return left.row < right.row ||
           (left.row == right.row && left.column < right.column);
}

static int selection_columns(const AstraTextGridSelection *selection,
                             uint32_t row, uint32_t *first, uint32_t *last)
{
    AstraTextGridPosition start;
    AstraTextGridPosition end;

    if (selection == NULL ||
        (selection->anchor.row == selection->focus.row &&
         selection->anchor.column == selection->focus.column))
        return 0;
    start = selection->anchor;
    end = selection->focus;
    if (position_before(end, start)) {
        AstraTextGridPosition swap = start;
        start = end;
        end = swap;
    }
    if (row < start.row || row > end.row)
        return 0;
    *first = row == start.row ? start.column : 0u;
    *last = row == end.row ? end.column : UINT32_MAX;
    return *first < *last;
}

static AstraResult render_run(const AstraTextSurface *text,
                              AstraSurfaceView *target,
                              int32_t x, int32_t y,
                              const AstraTextCell *cells, uint32_t count,
                              uint16_t foreground, uint32_t style)
{
    uint32_t first = 0u;

    while (first < count) {
        uint32_t at = first;
        uint32_t bytes = 0u;

        while (at < count) {
            if (text->_private_scratch_bytes - bytes < 4u)
                break;
            uint32_t encoded = astra_utf8_encode(
                cells[at].codepoint, text->_private_scratch + bytes);

            bytes += encoded;
            ++at;
        }
        if (at == first ||
            !astra_surface_mono_text_styled(
                target,
                x + (int32_t)((uint64_t)first *
                              text->_private_cell_width),
                y, text->_private_scratch, bytes,
                text->_private_font_height, text->_private_cell_width,
                foreground, style))
            return ASTRA_ERROR_IO;
        first = at;
    }
    return ASTRA_OK;
}

static AstraResult render_cells(
    const AstraTextSurface *text, AstraSurfaceView *target,
    int32_t origin_x, int32_t origin_y, uint32_t row, uint32_t column,
    const AstraTextCell *cells, uint32_t count,
    const AstraTextGridSelection *selection)
{
    uint64_t x64;
    uint64_t y64;
    uint64_t last_x64;
    uint32_t first;
    uint16_t selection_foreground = 0u;
    uint16_t selection_background = 0u;
    uint32_t selection_first = 0u;
    uint32_t selection_last = 0u;
    int selection_on_row;
    int selection_colors_valid = 1;

    if (!valid(text) || target == NULL || (count != 0u && cells == NULL) ||
        (selection != NULL &&
         (selection->size < sizeof(*selection) ||
          !astra_words_zero(selection->reserved, 4u) ||
          (text->_private_resolve_color == NULL &&
           ((selection->foreground != ASTRA_TEXT_COLOR_DEFAULT &&
             selection->foreground > UINT16_MAX) ||
            (selection->background != ASTRA_TEXT_COLOR_DEFAULT &&
             selection->background > UINT16_MAX))))))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    x64 = (uint64_t)column * text->_private_cell_width;
    y64 = (uint64_t)row * text->_private_line_height;
    last_x64 = x64 + (count == 0u ? 0u :
                     (uint64_t)(count - 1u) * text->_private_cell_width);
    if (x64 > INT32_MAX || y64 > INT32_MAX || last_x64 > INT32_MAX ||
        origin_x > INT32_MAX - (int32_t)last_x64 ||
        origin_y > INT32_MAX - (int32_t)y64)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index < count; ++index)
        if (!astra_unicode_scalar_valid(cells[index].codepoint) ||
            (cells[index].attributes & ~ASTRA_TEXT_STYLE_ALLOWED_MASK) != 0u ||
            cells[index].width != 1u || cells[index].reserved != 0u ||
            (text->_private_resolve_color == NULL &&
             ((cells[index].foreground != ASTRA_TEXT_COLOR_DEFAULT &&
               cells[index].foreground > UINT16_MAX) ||
              (cells[index].background != ASTRA_TEXT_COLOR_DEFAULT &&
               cells[index].background > UINT16_MAX))))
            return ASTRA_ERROR_INVALID_ARGUMENT;
    selection_on_row = selection_columns(
        selection, row, &selection_first, &selection_last);
    if (selection_on_row) {
        selection_foreground = color(
            text, selection->foreground,
            text->_private_default_foreground, &selection_colors_valid);
        selection_background = color(
            text, selection->background,
            text->_private_default_background, &selection_colors_valid);
        if (!selection_colors_valid)
            return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    first = 0u;
    while (first < count) {
        uint32_t last = first + 1u;
        uint32_t visible_first;
        uint32_t visible_last;
        uint32_t attributes = cells[first].attributes;
        uint32_t style = attributes & ASTRA_TEXT_RENDER_STYLE_MASK;
        uint64_t run_x64 = x64 + (uint64_t)first * text->_private_cell_width;
        uint64_t run_width64;
        uint16_t foreground;
        uint16_t background;
        int selected = selection_on_row &&
                       column + first >= selection_first &&
                       column + first < selection_last;
        int colors_valid = 1;

        while (last < count && run_matches(&cells[first], &cells[last]) &&
               (selection_on_row && column + last >= selection_first &&
                column + last < selection_last) == selected)
            ++last;
        run_width64 = (uint64_t)(last - first) * text->_private_cell_width;
        if (run_x64 > INT32_MAX || run_width64 > UINT32_MAX)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        foreground = color(text, cells[first].foreground,
                           text->_private_default_foreground, &colors_valid);
        background = color(text, cells[first].background,
                           text->_private_default_background, &colors_valid);
        if (!colors_valid)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if ((attributes & ASTRA_TEXT_STYLE_INVERSE) != 0u) {
            uint16_t swap = foreground;
            foreground = background;
            background = swap;
        }
        if (selected) {
            foreground = selection_foreground;
            background = selection_background;
        }
        if ((attributes & ASTRA_TEXT_STYLE_HIDDEN) != 0u ||
            ((attributes & ASTRA_TEXT_STYLE_BLINK) != 0u &&
             text->_private_blink_visible == 0u))
            foreground = background;
        else if ((attributes & ASTRA_TEXT_STYLE_FAINT) != 0u)
            foreground = dim(foreground, background);
        astra_surface_fill(target, origin_x + (int32_t)run_x64,
                           origin_y + (int32_t)y64,
                           (uint32_t)run_width64,
                           text->_private_line_height, background);
        visible_first = first;
        visible_last = last;
        if ((style & (ASTRA_TEXT_STYLE_UNDERLINE |
                      ASTRA_TEXT_STYLE_STRIKETHROUGH)) == 0u) {
            while (visible_first < visible_last &&
                   cells[visible_first].codepoint == ' ')
                ++visible_first;
            while (visible_last > visible_first &&
                   cells[visible_last - 1u].codepoint == ' ')
                --visible_last;
        }
        if (visible_first < visible_last) {
            AstraResult result = render_run(
                text, target,
                origin_x + (int32_t)x64 +
                    (int32_t)((uint64_t)visible_first *
                              text->_private_cell_width),
                origin_y + (int32_t)y64, cells + visible_first,
                visible_last - visible_first, foreground, style);

            if (result != ASTRA_OK)
                return result;
        }
        first = last;
    }
    return ASTRA_OK;
}

AstraResult astra_text_surface_render_cells(
    const AstraTextSurface *text, AstraSurfaceView *target,
    int32_t origin_x, int32_t origin_y, uint32_t row, uint32_t column,
    const AstraTextCell *cells, uint32_t count)
{
    return render_cells(text, target, origin_x, origin_y, row, column,
                        cells, count, NULL);
}

AstraResult astra_text_surface_render_grid(
    const AstraTextSurface *text, AstraSurfaceView *target,
    int32_t origin_x, int32_t origin_y, uint32_t row, uint32_t column,
    const AstraTextCell *cells, uint32_t count,
    const AstraTextGridSelection *selection)
{
    return render_cells(text, target, origin_x, origin_y, row, column,
                        cells, count, selection);
}

AstraResult astra_text_surface_grid_hit_test(
    const AstraTextSurface *text, int32_t origin_x, int32_t origin_y,
    uint32_t columns, uint32_t rows, int32_t x, int32_t y,
    AstraTextGridPosition *position)
{
    int64_t relative_x;
    int64_t relative_y;
    uint64_t width;
    uint64_t height;

    if (!valid(text) || columns == 0u || rows == 0u || position == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    relative_x = (int64_t)x - origin_x;
    relative_y = (int64_t)y - origin_y;
    width = (uint64_t)columns * text->_private_cell_width;
    height = (uint64_t)rows * text->_private_line_height;
    if (relative_y >= 0 && (uint64_t)relative_y >= height) {
        *position = (AstraTextGridPosition){rows, 0u};
        return ASTRA_OK;
    }
    position->row = relative_y <= 0 ? 0u :
        (uint32_t)((uint64_t)relative_y / text->_private_line_height);
    if (relative_x <= 0)
        position->column = 0u;
    else if ((uint64_t)relative_x >= width)
        position->column = columns;
    else
        position->column = (uint32_t)(
            ((uint64_t)relative_x + text->_private_cell_width / 2u) /
            text->_private_cell_width);
    return ASTRA_OK;
}

AstraResult astra_text_surface_draw_caret(
    const AstraTextSurface *text, AstraSurfaceView *target,
    int32_t origin_x, int32_t origin_y, uint32_t row, uint32_t column,
    uint32_t kind, uint16_t color_value)
{
    uint64_t x;
    uint64_t y;
    uint32_t width;
    uint32_t height;

    if (!valid(text) || target == NULL || kind < ASTRA_TEXT_CARET_BLOCK ||
        kind > ASTRA_TEXT_CARET_UNDERLINE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    x = (uint64_t)column * text->_private_cell_width;
    y = (uint64_t)row * text->_private_line_height;
    if (x > INT32_MAX || y > INT32_MAX ||
        origin_x > INT32_MAX - (int32_t)x ||
        origin_y > INT32_MAX - (int32_t)y)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (kind == ASTRA_TEXT_CARET_BLOCK) {
        width = text->_private_cell_width;
        height = text->_private_line_height;
    } else if (kind == ASTRA_TEXT_CARET_BAR) {
        width = text->_private_cell_width < 2u ? 1u : 2u;
        height = text->_private_line_height;
    } else {
        width = text->_private_cell_width;
        height = text->_private_line_height < 2u ? 1u : 2u;
        y += text->_private_line_height - height;
    }
    astra_surface_fill(target, origin_x + (int32_t)x,
                       origin_y + (int32_t)y, width, height, color_value);
    return ASTRA_OK;
}

AstraResult astra_text_surface_scroll(
    const AstraTextSurface *text, AstraSurfaceView *target,
    uint32_t origin_x, uint32_t origin_y, uint32_t columns, uint32_t rows,
    uint32_t moved_rows, uint32_t preserved_rows)
{
    uint64_t width;
    uint64_t height;
    uint64_t pixels;
    AstraTextBox box;

    if (!valid(text) || target == NULL || columns == 0u || rows == 0u ||
        moved_rows == 0u || moved_rows > rows ||
        preserved_rows > rows - moved_rows)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    width = (uint64_t)columns * text->_private_cell_width;
    height = (uint64_t)(moved_rows + preserved_rows) *
             text->_private_line_height;
    pixels = (uint64_t)moved_rows * text->_private_line_height;
    if (width > UINT32_MAX || height > UINT32_MAX || pixels > INT32_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    box = (AstraTextBox){target, origin_x, origin_y,
                         (uint32_t)width, (uint32_t)height};
    return astra_text_box_scroll(&box, (int32_t)pixels) ?
           ASTRA_OK : ASTRA_ERROR_IO;
}

AstraResult astra_text_surface_set_blink(AstraTextSurface *text,
                                         uint32_t visible)
{
    if (!valid(text) || visible > 1u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    text->_private_blink_visible = visible;
    return ASTRA_OK;
}
