#include "../desktop_layout.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    const uint32_t width = 1920u;
    const uint32_t height = 1004u; /* 1080 minus both system bars */
    uint32_t dx;
    uint32_t dy;

    assert(ASTRA_DESKTOP_LABEL_FONT_HEIGHT == 13u);
    assert(ASTRA_DESKTOP_LABEL_FONT_HEIGHT >
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
    astra_desktop_cell_offset(0u, height, &dx, &dy);
    assert(dx == 0u && dy == 0u);
    astra_desktop_cell_offset(7u, height, &dx, &dy);
    assert(dx == 0u && dy == 7u * ASTRA_DESKTOP_CELL_PITCH_Y);
    astra_desktop_cell_offset(8u, height, &dx, &dy);
    assert(dx == ASTRA_DESKTOP_CELL_PITCH_X && dy == 0u);

    /* Hit testing: every cell's corners, the gaps, and the edges. */
    for (uint32_t index = 0u; index < astra_desktop_capacity(width, height);
         ++index) {
        int32_t left;
        int32_t top;

        astra_desktop_cell_offset(index, height, &dx, &dy);
        left = (int32_t)(ASTRA_DESKTOP_ICON_CELL_LEFT + dx);
        top = (int32_t)(ASTRA_DESKTOP_GRID_TOP + dy);
        assert(astra_desktop_cell_at(left, top, width, height) == index);
        assert(astra_desktop_cell_at(
                   left + (int32_t)ASTRA_DESKTOP_ICON_CELL_WIDTH - 1,
                   top + (int32_t)ASTRA_DESKTOP_CELL_HEIGHT - 1, width,
                   height) == index);
        assert(astra_desktop_cell_at(
                   left + (int32_t)ASTRA_DESKTOP_ICON_CELL_WIDTH, top, width,
                   height) == UINT32_MAX);
        assert(astra_desktop_cell_at(
                   left, top + (int32_t)ASTRA_DESKTOP_CELL_HEIGHT, width,
                   height) == UINT32_MAX);
    }
    assert(astra_desktop_cell_at(31, 30, width, height) == UINT32_MAX);
    assert(astra_desktop_cell_at(40, 25, width, height) == UINT32_MAX);
    assert(astra_desktop_cell_at(-5, -5, width, height) == UINT32_MAX);
    assert(astra_desktop_cell_at(1919, 30, width, height) == UINT32_MAX);
    assert(astra_desktop_cell_at(40, 1003, width, height) == UINT32_MAX);
    puts("desktop layout tests passed");
    return 0;
}
