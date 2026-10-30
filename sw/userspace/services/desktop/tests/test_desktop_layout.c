#include "../desktop_layout.h"
#include "../desktop_usage_layout.h"
#include <astra/ui_font.h>
#include <astra/utf8.h>

#include <assert.h>
#include <stdio.h>

static void test_usage_text_progress(void)
{
    uint32_t advance;
    static const char label[] = "PID 37 (zsh) — file #123 — read only — inode 456\n";
    assert(astra_ui_font_strike(ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT) != NULL);
    assert(usage_line(label, sizeof(label) - 1u, &advance) == sizeof(label) - 2u);
    assert(advance == sizeof(label) - 1u);
    assert(usage_line("\n", 1u, &advance) == 0u && advance == 1u);

    /* Real font metrics must make progress across a long UTF-8 path and keep
     * every fitted line on a scalar boundary. No font or fitting mock. */
    char path[1024];
    static const char prefix[] = "/cd0/Folder/café-";
    memcpy(path, prefix, sizeof(prefix) - 1u);
    uint32_t length = sizeof(prefix) - 1u;
    for (uint32_t at = 0u; at < 400u; ++at) {
        path[length++] = (char)0xc3; path[length++] = (char)0xa9;
    }
    memcpy(path + length, ".txt", 4u); length += 4u;
    uint32_t offset = 0u, lines = 0u;
    while (offset < length) {
        uint32_t fit = usage_line(path + offset, length - offset, &advance);
        assert(fit != 0u && fit == advance && advance <= length - offset);
        assert(astra_utf8_validate(path + offset, fit, 0u));
        assert(astra_surface_ui_text_width(path + offset, fit,
            ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT) <= DESKTOP_USAGE_VIEW_WIDTH - 12u);
        offset += advance; ++lines;
    }
    assert(offset == length && lines > 1u);
}

int main(void)
{
    test_usage_text_progress();
    const uint32_t width = 1920u;
    const uint32_t height = 1004u; /* 1080 minus both system bars */
    uint32_t dx;
    uint32_t dy;

    assert(ASTRA_DESKTOP_LABEL_FONT_HEIGHT ==
           ASTRA_THEME_SYSTEM_TITLE_FONT_HEIGHT);
    assert(ASTRA_DESKTOP_LABEL_FONT_HEIGHT >=
           ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT);
    /* Cell 0 is where the single Terminal icon always was. */
    assert(ASTRA_DESKTOP_ICON_X == 40u && ASTRA_DESKTOP_ICON_Y == 34u &&
           ASTRA_DESKTOP_LABEL_Y == 106u);
    assert(astra_desktop_centered_label_x(48u) == 48);
    assert(astra_desktop_centered_label_x(80u) == 32);
    assert(astra_desktop_centered_label_x(96u) == 32);

    /* 26 + 7 * 112 + 104 = 914 <= 1004 < 26 + 8 * 112 + 104. */
    assert(astra_desktop_rows(height) == 8u);
    assert(astra_desktop_columns(width) == 19u);
    assert(astra_desktop_capacity(width, height) == 152u);
    assert(astra_desktop_capacity(100u, 100u) == 0u);
    assert(astra_desktop_rows(ASTRA_DESKTOP_GRID_TOP +
                              ASTRA_DESKTOP_CELL_HEIGHT) == 1u);
    assert(astra_desktop_rows(ASTRA_DESKTOP_GRID_TOP +
                              ASTRA_DESKTOP_CELL_HEIGHT - 1u) == 0u);

    /* Columns fill top to bottom. */
    astra_desktop_cell_offset(0u, width, height, &dx, &dy);
    assert(dx == 0u && dy == 0u);
    astra_desktop_cell_offset(7u, width, height, &dx, &dy);
    assert(dx == 0u && dy == 7u * ASTRA_DESKTOP_CELL_PITCH_Y);
    astra_desktop_cell_offset(8u, width, height, &dx, &dy);
    assert(dx == ASTRA_DESKTOP_CELL_PITCH_X && dy == 0u);

    /* Hit testing: every cell's corners, the gaps, and the edges. */
    for (uint32_t index = 0u; index < astra_desktop_capacity(width, height);
         ++index) {
        int32_t left;
        int32_t top;

        astra_desktop_cell_offset(index, width, height, &dx, &dy);
        left = (int32_t)(ASTRA_DESKTOP_ICON_CELL_LEFT + dx);
        top = (int32_t)(ASTRA_DESKTOP_GRID_TOP + dy);
        assert(astra_desktop_cell_at(left, top, width, height, 0u) == index);
        assert(astra_desktop_cell_at(
                   left + (int32_t)ASTRA_DESKTOP_ICON_CELL_WIDTH - 1,
                   top + (int32_t)ASTRA_DESKTOP_CELL_HEIGHT - 1, width,
                   height, 0u) == index);
        assert(astra_desktop_cell_at(
                   left + (int32_t)ASTRA_DESKTOP_ICON_CELL_WIDTH, top, width,
                   height, 0u) == UINT32_MAX);
        assert(astra_desktop_cell_at(
                   left, top + (int32_t)ASTRA_DESKTOP_CELL_HEIGHT, width,
                   height, 0u) == UINT32_MAX);
    }
    assert(astra_desktop_cell_at(31, 30, width, height, 0u) == UINT32_MAX);
    assert(astra_desktop_cell_at(40, 25, width, height, 0u) == UINT32_MAX);
    assert(astra_desktop_cell_at(-5, -5, width, height, 0u) == UINT32_MAX);
    assert(astra_desktop_cell_at(1919, 30, width, height, 0u) == UINT32_MAX);
    assert(astra_desktop_cell_at(40, 1003, width, height, 0u) == UINT32_MAX);
    /* Further pages preserve each cell's identity across arbitrary scroll
       offsets, including the partial final page and its clamped bottom. */
    const uint32_t count = 503u;
    const uint32_t content = astra_desktop_content_height(count, width, height);
    assert(content == 3u * height + ASTRA_DESKTOP_GRID_TOP +
        7u * ASTRA_DESKTOP_CELL_PITCH_Y + ASTRA_DESKTOP_CELL_HEIGHT);
    for (uint32_t index = 0u; index < count; ++index) {
        astra_desktop_cell_offset(index, width, height, &dx, &dy);
        uint32_t offset = dy < content - height ? dy : content - height;
        int32_t left = (int32_t)(ASTRA_DESKTOP_ICON_CELL_LEFT + dx);
        int32_t top = (int32_t)(ASTRA_DESKTOP_GRID_TOP + dy - offset);
        assert(top >= 0 && top + (int32_t)ASTRA_DESKTOP_CELL_HEIGHT <= (int32_t)height);
        assert(astra_desktop_cell_at(left, top, width, height, offset) == index);
        assert(astra_desktop_cell_at(left + ASTRA_DESKTOP_ICON_CELL_WIDTH - 1u,
            top + ASTRA_DESKTOP_CELL_HEIGHT - 1u, width, height, offset) == index);
        assert(astra_desktop_cell_at(left + ASTRA_DESKTOP_ICON_CELL_WIDTH,
            top, width, height, offset) == UINT32_MAX);
    }
    astra_desktop_cell_offset(152u, width, height, &dx, &dy);
    assert(dx == 0u && dy == height);
    assert(astra_desktop_content_height(0u, width, height) == height);
    assert(astra_desktop_content_height(152u, width, height) == height);
    assert(astra_desktop_content_height(153u, width, height) == height +
        ASTRA_DESKTOP_GRID_TOP + ASTRA_DESKTOP_CELL_HEIGHT);
    assert(astra_desktop_content_height(1u, 1u, height) == 0u);
    assert(astra_desktop_content_height(UINT32_MAX, width, height) == 0u);
    assert(astra_desktop_cell_at(40, 20, width, height, height) == UINT32_MAX);
    assert(astra_desktop_cell_at(40, 30, width, height, INT32_MAX) == UINT32_MAX);
    puts("desktop layout tests passed");
    return 0;
}
