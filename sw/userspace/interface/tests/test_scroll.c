#include <astra/scroll.h>

#include <assert.h>
#include <stddef.h>

static void assert_state(const AstraScrollModel *model, uint32_t x,
                         uint32_t y, uint32_t maximum_x,
                         uint32_t maximum_y, uint32_t generation)
{
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;

    assert(astra_scroll_get_state(model, &state) == ASTRA_OK);
    assert(state.offset_x == x && state.offset_y == y);
    assert(state.maximum_x == maximum_x && state.maximum_y == maximum_y);
    assert(state.generation == generation);
}

int main(void)
{
    AstraScrollModel model = ASTRA_SCROLL_MODEL_INIT;
    AstraScrollModelInfo info = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraScrollMark marks[2] = {
        { 40u, 300u, ASTRA_SCROLL_MARK_INFORMATION, 0u },
        { 900u, 700u, ASTRA_SCROLL_MARK_FAULT, 0u }};
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;

    info.content_width = 1000u;
    info.content_height = 800u;
    info.viewport_width = 200u;
    info.viewport_height = 120u;
    info.offset_x = 40u;
    info.offset_y = 60u;
    info.line_width = 8u;
    info.line_height = 16u;
    info.marks = marks;
    info.mark_count = 2u;
    assert(astra_scroll_init(&model, &info) == ASTRA_OK);
    assert_state(&model, 40u, 60u, 800u, 680u, 1u);

    assert(astra_scroll_set_offset(&model, UINT32_MAX, UINT32_MAX) ==
           ASTRA_OK);
    assert_state(&model, 800u, 680u, 800u, 680u, 2u);
    assert(astra_scroll_by(&model, INT32_MAX, INT32_MAX) == ASTRA_OK);
    assert_state(&model, 800u, 680u, 800u, 680u, 2u);
    assert(astra_scroll_by(&model, INT32_MIN, INT32_MIN) == ASTRA_OK);
    assert_state(&model, 0u, 0u, 800u, 680u, 3u);

    assert(astra_scroll_set_extents(&model, 100u, 80u, 200u, 120u) ==
           ASTRA_OK);
    assert_state(&model, 0u, 0u, 0u, 0u, 4u);
    assert(astra_scroll_set_offset(&model, 10u, 10u) == ASTRA_OK);
    assert_state(&model, 0u, 0u, 0u, 0u, 4u);

    assert(astra_scroll_set_extents(&model, 1000u, 800u, 200u, 120u) ==
           ASTRA_OK);
    assert(astra_scroll_set_marks(&model, NULL, 0u) == ASTRA_OK);
    assert(astra_scroll_get_state(&model, &state) == ASTRA_OK);
    assert(state.marks == NULL && state.mark_count == 0u);
    marks[0].reserved = 1u;
    assert(astra_scroll_set_marks(&model, marks, 2u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_scroll_get_state(&model, &state) == ASTRA_OK);
    assert(state.marks == NULL && state.mark_count == 0u);

    info.offset_x = 801u;
    assert(astra_scroll_init(&model, &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    info.offset_x = 0u;
    info.line_height = 0u;
    assert(astra_scroll_init(&model, &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);

    return 0;
}
