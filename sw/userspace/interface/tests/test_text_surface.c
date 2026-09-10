#include <astra/text_surface.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(AstraTextGridPosition) == 8u,
               "grid position ABI");
_Static_assert(sizeof(AstraTextGridSelection) == 44u,
               "grid selection ABI");

static uint32_t fill_count;
static uint32_t text_count;
static uint32_t scroll_count;
static uint32_t last_style;
static uint16_t last_foreground;
static uint16_t last_fill_color;
static uint32_t last_text_length;
static char last_text[8];
static uint16_t fill_colors[8];
static uint16_t text_colors[8];

void astra_surface_fill(AstraSurfaceView *surface, int32_t x, int32_t y,
                        uint32_t width, uint32_t height, uint16_t color)
{
    (void)surface;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    ++fill_count;
    last_fill_color = color;
    if (fill_count <= sizeof(fill_colors) / sizeof(fill_colors[0]))
        fill_colors[fill_count - 1u] = color;
}

int astra_surface_mono_text_styled(AstraSurfaceView *surface, int32_t x,
                                   int32_t y, const char *utf8,
                                   uint32_t length, uint16_t pixel_height,
                                   uint16_t cell_width, uint16_t color,
                                   uint32_t style_flags)
{
    (void)surface;
    (void)x;
    (void)y;
    (void)pixel_height;
    (void)cell_width;
    ++text_count;
    last_style = style_flags;
    last_foreground = color;
    if (text_count <= sizeof(text_colors) / sizeof(text_colors[0]))
        text_colors[text_count - 1u] = color;
    last_text_length = length;
    memcpy(last_text, utf8, length);
    return 1;
}

int astra_text_box_scroll(AstraTextBox *text_box, int32_t pixels)
{
    assert(text_box != NULL && pixels == 20);
    ++scroll_count;
    return 1;
}

static uint16_t resolve(void *context, uint32_t value, uint16_t fallback)
{
    (void)context;
    return value == ASTRA_TEXT_COLOR_DEFAULT ? fallback : (uint16_t)value;
}

static void reset_calls(void)
{
    fill_count = text_count = scroll_count = 0u;
    last_style = last_text_length = 0u;
    last_foreground = last_fill_color = 0u;
    memset(last_text, 0, sizeof(last_text));
    memset(fill_colors, 0, sizeof(fill_colors));
    memset(text_colors, 0, sizeof(text_colors));
}

int main(void)
{
    char scratch[8];
    AstraTextSurfaceInfo info = ASTRA_TEXT_SURFACE_INFO_INIT;
    AstraTextSurface text = ASTRA_TEXT_SURFACE_INIT;
    AstraSurfaceView target = {0};
    AstraTextCell cells[] = {
        {'A', 0x1111u, 0x2222u, ASTRA_TEXT_STYLE_BOLD, 1u, 0u},
        {0x0105u, 0x1111u, 0x2222u,
         ASTRA_TEXT_STYLE_BOLD | ASTRA_TEXT_STYLE_ITALIC, 1u, 0u},
        {' ', 0x1111u, 0x2222u, ASTRA_TEXT_STYLE_UNDERLINE, 1u, 0u},
    };

    info.font_height = 16u;
    info.cell_width = 8u;
    info.line_height = 20u;
    info.default_foreground = 0xffffu;
    info.default_background = 0u;
    info.scratch = scratch;
    info.scratch_bytes = sizeof(scratch);
    info.resolve_color = resolve;
    assert(astra_text_surface_init(&text, &info) == ASTRA_OK);

    reset_calls();
    assert(astra_text_surface_render_cells(
               &text, &target, 10, 8, 2u, 3u, cells, 3u) == ASTRA_OK);
    assert(fill_count == 3u && text_count == 3u);
    assert(last_style == ASTRA_TEXT_STYLE_UNDERLINE);
    assert(last_text_length == 1u && last_text[0] == ' ');
    assert(last_foreground == 0x1111u && last_fill_color == 0x2222u);

    reset_calls();
    cells[0].attributes = ASTRA_TEXT_STYLE_INVERSE |
                          ASTRA_TEXT_STYLE_HIDDEN;
    assert(astra_text_surface_render_cells(
               &text, &target, 0, 0, 0u, 0u, cells, 1u) == ASTRA_OK);
    assert(last_foreground == 0x1111u && last_fill_color == 0x1111u);

    reset_calls();
    cells[0].codepoint = 0xd800u;
    assert(astra_text_surface_render_cells(
               &text, &target, 0, 0, 0u, 0u, cells, 1u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(fill_count == 0u && text_count == 0u);

    cells[0].codepoint = 'A';
    cells[0].attributes = 0u;
    cells[1].foreground = UINT32_C(0x10000);
    text._private_resolve_color = NULL;
    reset_calls();
    assert(astra_text_surface_render_cells(
               &text, &target, 0, 0, 0u, 0u, cells, 2u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(fill_count == 0u && text_count == 0u);
    text._private_resolve_color = resolve;
    cells[1].foreground = 0x1111u;

    {
        AstraTextCell selection_cells[] = {
            {'A', 0x1111u, 0x2222u, 0u, 1u, 0u},
            {'B', 0x1111u, 0x2222u, 0u, 1u, 0u},
            {'C', 0x1111u, 0x2222u, 0u, 1u, 0u},
            {'D', 0x1111u, 0x2222u, 0u, 1u, 0u},
        };
        AstraTextGridSelection selection = ASTRA_TEXT_GRID_SELECTION_INIT;

        selection.anchor = (AstraTextGridPosition){3u, 4u};
        selection.focus = (AstraTextGridPosition){3u, 2u};
        selection.foreground = 0xaaaau;
        selection.background = 0xbbbbu;
        reset_calls();
        assert(astra_text_surface_render_grid(
                   &text, &target, 0, 0, 3u, 1u,
                   selection_cells, 4u, &selection) == ASTRA_OK);
        assert(fill_count == 3u && text_count == 3u);
        assert(fill_colors[0] == 0x2222u);
        assert(fill_colors[1] == 0xbbbbu);
        assert(fill_colors[2] == 0x2222u);
        assert(text_colors[0] == 0x1111u);
        assert(text_colors[1] == 0xaaaau);
        assert(text_colors[2] == 0x1111u);

        selection.anchor = selection.focus;
        reset_calls();
        assert(astra_text_surface_render_grid(
                   &text, &target, 0, 0, 3u, 1u,
                   selection_cells, 4u, &selection) == ASTRA_OK);
        assert(fill_count == 1u && text_count == 1u);

        selection.size = 0u;
        assert(astra_text_surface_render_grid(
                   &text, &target, 0, 0, 3u, 1u,
                   selection_cells, 4u, &selection) ==
               ASTRA_ERROR_INVALID_ARGUMENT);
    }

    reset_calls();
    assert(astra_text_surface_draw_caret(
               &text, &target, 10, 8, 1u, 2u,
               ASTRA_TEXT_CARET_UNDERLINE, 0x1234u) == ASTRA_OK);
    assert(fill_count == 1u && last_fill_color == 0x1234u);
    assert(astra_text_surface_scroll(&text, &target, 10u, 8u, 20u, 6u,
                                     1u, 5u) == ASTRA_OK);
    assert(scroll_count == 1u);

    {
        AstraTextGridPosition position = {99u, 99u};

        assert(astra_text_surface_grid_hit_test(
                   &text, 10, 8, 20u, 6u, 10, 8, &position) == ASTRA_OK);
        assert(position.row == 0u && position.column == 0u);
        assert(astra_text_surface_grid_hit_test(
                   &text, 10, 8, 20u, 6u, 14, 27, &position) == ASTRA_OK);
        assert(position.row == 0u && position.column == 1u);
        assert(astra_text_surface_grid_hit_test(
                   &text, 10, 8, 20u, 6u, -100, -100, &position) == ASTRA_OK);
        assert(position.row == 0u && position.column == 0u);
        assert(astra_text_surface_grid_hit_test(
                   &text, 10, 8, 20u, 6u, 1000, 1000, &position) == ASTRA_OK);
        assert(position.row == 6u && position.column == 0u);
    }

    info.scratch_bytes = 3u;
    assert(astra_text_surface_init(&text, &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    puts("text surface tests passed");
    return 0;
}
