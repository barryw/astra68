#include <astra/control.h>
#include <astra/input.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "../src/control_internal.h"

static void buttons(AstraControl *controls, uint32_t count)
{
    for (uint32_t at = 0u; at < count; ++at) {
        AstraButtonInfo info = ASTRA_BUTTON_INFO_INIT;

        controls[at] = (AstraControl)ASTRA_CONTROL_INIT;
        info.id = at + 1u;
        info.text = "AA";
        info.text_length = 2u;
        assert(astra_interface_button_init(&controls[at], &info) == ASTRA_OK);
    }
}

static void set_item(AstraControl *control, uint32_t grow, uint32_t shrink,
                     uint32_t basis, uint32_t minimum_width,
                     uint32_t maximum_width, uint32_t align, uint32_t flags)
{
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;

    item.grow = grow;
    item.shrink = shrink;
    item.basis = basis;
    item.minimum_width = minimum_width;
    item.maximum_width = maximum_width;
    item.align_self = align;
    item.flags = flags;
    assert(astra_interface_control_set_flex(control, &item) == ASTRA_OK);
}

static AstraWindowEvent frame_event(uint16_t width, uint16_t height)
{
    AstraWindowEvent event = {0};

    event.size = sizeof(event);
    event.version = ASTRA_WINDOW_EVENT_VERSION;
    event.type = ASTRA_WINDOW_EVENT_FRAME;
    event.data.frame.frame.width = width;
    event.data.frame.frame.height = height;
    return event;
}

static AstraWindowEvent pointer_event(uint32_t flags, int32_t x, int32_t y)
{
    AstraWindowEvent event = {0};

    event.size = sizeof(event);
    event.version = ASTRA_WINDOW_EVENT_VERSION;
    event.type = ASTRA_WINDOW_EVENT_POINTER_BUTTON;
    event.flags = flags;
    event.data.pointer.x = x;
    event.data.pointer.y = y;
    event.data.pointer.button = ASTRA_INPUT_BUTTON_LEFT;
    return event;
}

static void test_empty_and_invalid(void)
{
    AstraUIContext empty = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraButtonInfo info = ASTRA_BUTTON_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;

    assert(astra_interface_ui_init(&empty, NULL, 0u, 1u, 1u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&empty, &layout) == ASTRA_OK);
    assert(astra_interface_ui_layout(&empty, NULL) ==
           ASTRA_ERROR_INVALID_ARGUMENT);

    info.id = 1u;
    info.text = "AA";
    info.text_length = 2u;
    assert(astra_interface_button_init(&control, &info) == ASTRA_OK);
    item.minimum_width = 2u;
    item.maximum_width = 1u;
    assert(astra_interface_control_set_flex(&control, &item) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.align_self = ASTRA_FLEX_ALIGN_AUTO + 1u;
    assert(astra_interface_control_set_flex(&control, &item) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.flags = UINT32_C(1) << 31;
    assert(astra_interface_control_set_flex(&control, &item) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.reserved[0] = 1u;
    assert(astra_interface_control_set_flex(&control, &item) ==
           ASTRA_ERROR_INVALID_ARGUMENT);

    layout = (AstraFlexLayout)ASTRA_FLEX_LAYOUT_INIT;
    layout.direction = ASTRA_FLEX_COLUMN + 1u;
    assert(astra_interface_ui_layout(&empty, &layout) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    layout = (AstraFlexLayout)ASTRA_FLEX_LAYOUT_INIT;
    layout.flags = UINT32_C(1) << 31;
    assert(astra_interface_ui_layout(&empty, &layout) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    layout = (AstraFlexLayout)ASTRA_FLEX_LAYOUT_INIT;
    layout.justify = ASTRA_FLEX_JUSTIFY_SPACE_BETWEEN + 1u;
    assert(astra_interface_ui_layout(&empty, &layout) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    layout = (AstraFlexLayout)ASTRA_FLEX_LAYOUT_INIT;
    layout.align_items = ASTRA_FLEX_ALIGN_AUTO;
    assert(astra_interface_ui_layout(&empty, &layout) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    layout = (AstraFlexLayout)ASTRA_FLEX_LAYOUT_INIT;
    layout.reserved[0] = 1u;
    assert(astra_interface_ui_layout(&empty, &layout) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
}

static void test_weighted_growth(void)
{
    AstraControl controls[3];
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;

    buttons(controls, 3u);
    set_item(&controls[0], 1u, 1u, ASTRA_FLEX_AUTO, 0u, 34u,
             ASTRA_FLEX_ALIGN_AUTO, 0u);
    set_item(&controls[1], 2u, 1u, ASTRA_FLEX_AUTO, 0u, UINT32_MAX,
             ASTRA_FLEX_ALIGN_AUTO, 0u);
    set_item(&controls[2], 1u, 1u, ASTRA_FLEX_AUTO, 0u, UINT32_MAX,
             ASTRA_FLEX_ALIGN_AUTO, 0u);
    assert(astra_interface_ui_init(&context, controls, 3u, 120u, 28u) ==
           ASTRA_OK);
    layout.main_gap = 3u;
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(controls[0]._private_frame.x == 0 &&
           controls[0]._private_frame.width == 34u);
    assert(controls[1]._private_frame.x == 37 &&
           controls[1]._private_frame.width == 43u);
    assert(controls[2]._private_frame.x == 83 &&
           controls[2]._private_frame.width == 37u);
}

static void test_weighted_shrink(void)
{
    AstraControl controls[3];
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;

    buttons(controls, 3u);
    set_item(&controls[0], 0u, 1u, 50u, 0u, UINT32_MAX,
             ASTRA_FLEX_ALIGN_AUTO, 0u);
    set_item(&controls[1], 0u, 2u, 50u, 40u, UINT32_MAX,
             ASTRA_FLEX_ALIGN_AUTO, 0u);
    set_item(&controls[2], 0u, 1u, 50u, 0u, UINT32_MAX,
             ASTRA_FLEX_ALIGN_AUTO, 0u);
    assert(astra_interface_ui_init(&context, controls, 3u, 120u, 28u) ==
           ASTRA_OK);
    layout.main_gap = 3u;
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(controls[0]._private_frame.x == 0 &&
           controls[0]._private_frame.width == 37u);
    assert(controls[1]._private_frame.x == 40 &&
           controls[1]._private_frame.width == 40u);
    assert(controls[2]._private_frame.x == 83 &&
           controls[2]._private_frame.width == 37u);
}

static void test_wrap_and_reflow(void)
{
    AstraControl controls[3];
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraControlFrame damage;
    AstraWindowEvent event;

    buttons(controls, 3u);
    assert(astra_interface_ui_init(&context, controls, 3u, 70u, 100u) ==
           ASTRA_OK);
    layout.flags = ASTRA_FLEX_WRAP;
    layout.main_gap = 4u;
    layout.cross_gap = 5u;
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(controls[0]._private_frame.x == 0 &&
           controls[0]._private_frame.y == 0);
    assert(controls[1]._private_frame.x == 34 &&
           controls[1]._private_frame.y == 0);
    assert(controls[2]._private_frame.x == 0 &&
           controls[2]._private_frame.y == 33);

    astra_interface_ui_damage_clear(&context);
    event = frame_event(30u, 100u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(controls[0]._private_frame.y == 0);
    assert(controls[1]._private_frame.x == 0 &&
           controls[1]._private_frame.y == 33);
    assert(controls[2]._private_frame.x == 0 &&
           controls[2]._private_frame.y == 66);
    assert(astra_interface_ui_damage(&context, &damage) == ASTRA_OK);
    assert(damage.x == 0 && damage.y == 0 && damage.width == 30u &&
           damage.height == 100u);

    astra_interface_ui_damage_clear(&context);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(astra_interface_ui_damage(&context, &damage) ==
           ASTRA_ERROR_WOULD_BLOCK);

    event = frame_event(100u, 100u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(controls[0]._private_frame.x == 0 &&
           controls[1]._private_frame.x == 34 &&
           controls[2]._private_frame.x == 68);
    assert(controls[0]._private_frame.y == 0 &&
           controls[1]._private_frame.y == 0 &&
           controls[2]._private_frame.y == 0);
}

static void test_breaks(void)
{
    AstraControl controls[4];
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;

    buttons(controls, 4u);
    set_item(&controls[0], 0u, 1u, ASTRA_FLEX_AUTO, 0u, UINT32_MAX,
             ASTRA_FLEX_ALIGN_AUTO, ASTRA_FLEX_BREAK_AFTER);
    set_item(&controls[2], 0u, 1u, ASTRA_FLEX_AUTO, 0u, UINT32_MAX,
             ASTRA_FLEX_ALIGN_AUTO, ASTRA_FLEX_BREAK_BEFORE);
    assert(astra_interface_ui_init(&context, controls, 4u, 200u, 120u) ==
           ASTRA_OK);
    layout.flags = ASTRA_FLEX_WRAP;
    layout.main_gap = 4u;
    layout.cross_gap = 5u;
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(controls[0]._private_frame.y == 0);
    assert(controls[1]._private_frame.x == 0 &&
           controls[1]._private_frame.y == 33);
    assert(controls[2]._private_frame.x == 0 &&
           controls[2]._private_frame.y == 66);
    assert(controls[3]._private_frame.x == 34 &&
           controls[3]._private_frame.y == 66);
}

static void test_justification(void)
{
    AstraControl controls[3];
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;

    buttons(controls, 3u);
    assert(astra_interface_ui_init(&context, controls, 3u, 109u, 28u) ==
           ASTRA_OK);
    layout.main_gap = 5u;
    layout.justify = ASTRA_FLEX_JUSTIFY_CENTER;
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(controls[0]._private_frame.x == 4 &&
           controls[1]._private_frame.x == 39 &&
           controls[2]._private_frame.x == 74);
    layout.justify = ASTRA_FLEX_JUSTIFY_END;
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(controls[0]._private_frame.x == 9 &&
           controls[1]._private_frame.x == 44 &&
           controls[2]._private_frame.x == 79);
    layout.justify = ASTRA_FLEX_JUSTIFY_SPACE_BETWEEN;
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(controls[0]._private_frame.x == 0 &&
           controls[1]._private_frame.x == 40 &&
           controls[2]._private_frame.x == 79);
}

static void test_column_and_alignment(void)
{
    AstraControl controls[2];
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;

    buttons(controls, 2u);
    set_item(&controls[0], 0u, 1u, ASTRA_FLEX_AUTO, 0u, UINT32_MAX,
             ASTRA_FLEX_ALIGN_CENTER, 0u);
    set_item(&controls[1], 0u, 1u, ASTRA_FLEX_AUTO, 0u, UINT32_MAX,
             ASTRA_FLEX_ALIGN_END, 0u);
    assert(astra_interface_ui_init(&context, controls, 2u, 80u, 80u) ==
           ASTRA_OK);
    layout.direction = ASTRA_FLEX_COLUMN;
    layout.padding_left = 5u;
    layout.padding_top = 5u;
    layout.padding_right = 5u;
    layout.padding_bottom = 5u;
    layout.main_gap = 4u;
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(controls[0]._private_frame.x == 25 &&
           controls[0]._private_frame.y == 5 &&
           controls[0]._private_frame.width == 30u &&
           controls[0]._private_frame.height == 28u);
    assert(controls[1]._private_frame.x == 45 &&
           controls[1]._private_frame.y == 37);

    controls[0]._private_align_self = ASTRA_FLEX_ALIGN_AUTO;
    controls[1]._private_align_self = ASTRA_FLEX_ALIGN_AUTO;
    controls[0]._private_maximum_width = 35u;
    controls[1]._private_maximum_width = 35u;
    layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(controls[0]._private_frame.x == 5 &&
           controls[0]._private_frame.width == 35u);
    assert(controls[1]._private_frame.x == 5 &&
           controls[1]._private_frame.width == 35u);
}

static void test_tiny_parent_and_maximal_weights(void)
{
    AstraControl controls[2];
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;

    buttons(controls, 2u);
    set_item(&controls[0], UINT32_MAX, 1u, ASTRA_FLEX_AUTO, 0u, UINT32_MAX,
             ASTRA_FLEX_ALIGN_AUTO, 0u);
    set_item(&controls[1], UINT32_MAX, 1u, ASTRA_FLEX_AUTO, 0u, UINT32_MAX,
             ASTRA_FLEX_ALIGN_AUTO, 0u);
    assert(astra_interface_ui_init(&context, controls, 2u, 100u, 50u) ==
           ASTRA_OK);
    layout.main_gap = 4u;
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(controls[0]._private_frame.width == 48u &&
           controls[1]._private_frame.width == 48u);

    layout.padding_left = 60u;
    layout.padding_right = 60u;
    layout.padding_top = 30u;
    layout.padding_bottom = 30u;
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(controls[0]._private_frame.x == 60 &&
           controls[0]._private_frame.y == 30 &&
           controls[0]._private_frame.width == 30u &&
           controls[0]._private_frame.height == 28u);
}

static void parent(AstraControl *control, uint32_t parent_id, uint32_t grow)
{
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;

    item.parent_id = parent_id;
    item.grow = grow;
    assert(astra_interface_control_set_flex(control, &item) == ASTRA_OK);
}

static void test_nested_layout_and_reflow(void)
{
    AstraControl controls[4] = {
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT,
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraContainerInfo outer = ASTRA_CONTAINER_INFO_INIT;
    AstraContainerInfo inner = ASTRA_CONTAINER_INFO_INIT;
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraFlexLayout root = ASTRA_FLEX_LAYOUT_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent event;

    outer.id = 1u;
    outer.layout.main_gap = 4u;
    outer.layout.padding_left = 5u;
    outer.layout.padding_top = 5u;
    outer.layout.padding_right = 5u;
    outer.layout.padding_bottom = 5u;
    outer.layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    assert(astra_interface_container_init(&controls[0], &outer) == ASTRA_OK);
    parent(&controls[0], 0u, 1u);

    button.id = 2u;
    button.text = "AA";
    button.text_length = 2u;
    assert(astra_interface_button_init(&controls[1], &button) == ASTRA_OK);
    parent(&controls[1], 1u, 1u);

    inner.id = 3u;
    inner.layout.direction = ASTRA_FLEX_COLUMN;
    inner.layout.padding_left = 2u;
    inner.layout.padding_top = 2u;
    inner.layout.padding_right = 2u;
    inner.layout.padding_bottom = 2u;
    inner.layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    assert(astra_interface_container_init(&controls[2], &inner) == ASTRA_OK);
    parent(&controls[2], 1u, 1u);

    button.id = 4u;
    assert(astra_interface_button_init(&controls[3], &button) == ASTRA_OK);
    parent(&controls[3], 3u, 0u);

    assert(astra_interface_ui_init(&context, controls, 4u, 120u, 80u) ==
           ASTRA_OK);
    root.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    assert(astra_interface_ui_layout(&context, &root) == ASTRA_OK);
    assert(controls[0]._private_frame.x == 0 &&
           controls[0]._private_frame.y == 0 &&
           controls[0]._private_frame.width == 120u &&
           controls[0]._private_frame.height == 80u);
    assert(controls[1]._private_frame.x == 5 &&
           controls[1]._private_frame.width == 51u);
    assert(controls[2]._private_frame.x == 60 &&
           controls[2]._private_frame.width == 55u);
    assert(controls[3]._private_frame.x == 62 &&
           controls[3]._private_frame.y == 7 &&
           controls[3]._private_frame.width == 51u);
    assert(controls[3]._private_clip.x == 60 &&
           controls[3]._private_clip.y == 5 &&
           controls[3]._private_clip.width == 55u &&
           controls[3]._private_clip.height == 70u);

    event = frame_event(160u, 80u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(controls[1]._private_frame.width == 71u &&
           controls[2]._private_frame.x == 80 &&
           controls[2]._private_frame.width == 75u &&
           controls[3]._private_frame.x == 82 &&
           controls[3]._private_frame.width == 71u);
}

static void test_nested_layout_rejections(void)
{
    AstraControl controls[2] = {ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraContainerInfo container = ASTRA_CONTAINER_INFO_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;

    button.id = 1u;
    button.text = "AA";
    button.text_length = 2u;
    assert(astra_interface_button_init(&controls[0], &button) == ASTRA_OK);
    parent(&controls[0], 2u, 0u);
    container.id = 2u;
    assert(astra_interface_container_init(&controls[1], &container) ==
           ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 2u, 100u, 50u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);

    controls[0] = (AstraControl)ASTRA_CONTROL_INIT;
    controls[1] = (AstraControl)ASTRA_CONTROL_INIT;
    assert(astra_interface_button_init(&controls[0], &button) == ASTRA_OK);
    button.id = 2u;
    assert(astra_interface_button_init(&controls[1], &button) == ASTRA_OK);
    parent(&controls[1], 1u, 0u);
    assert(astra_interface_ui_init(&context, controls, 2u, 100u, 50u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
}

static void test_nested_clip_blocks_input(void)
{
    AstraControl controls[2] = {ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraContainerInfo container = ASTRA_CONTAINER_INFO_INIT;
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraFlexLayout root = ASTRA_FLEX_LAYOUT_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent event;

    container.id = 1u;
    assert(astra_interface_container_init(&controls[0], &container) ==
           ASTRA_OK);
    button.id = 2u;
    button.text = "AA";
    button.text_length = 2u;
    assert(astra_interface_button_init(&controls[1], &button) == ASTRA_OK);
    parent(&controls[1], 1u, 0u);
    assert(astra_interface_ui_init(&context, controls, 2u, 100u, 20u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &root) == ASTRA_OK);
    assert(controls[1]._private_frame.height == 28u &&
           controls[1]._private_clip.height == 20u);

    event = pointer_event(ASTRA_WINDOW_EVENT_DOWN, 5, 25);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = pointer_event(0u, 5, 25);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_NONE);

    event = pointer_event(ASTRA_WINDOW_EVENT_DOWN, 5, 10);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = pointer_event(0u, 5, 10);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_ACTIVATE &&
           action.control_id == 2u);
}

static void test_deep_layout_has_no_stack_or_depth_limit(void)
{
    static AstraControl controls[256];
    AstraContainerInfo container = ASTRA_CONTAINER_INFO_INIT;
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraFlexLayout root = ASTRA_FLEX_LAYOUT_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;

    for (uint32_t at = 0u; at + 1u < 256u; ++at) {
        container.id = at + 1u;
        controls[at] = (AstraControl)ASTRA_CONTROL_INIT;
        assert(astra_interface_container_init(&controls[at], &container) ==
               ASTRA_OK);
        parent(&controls[at], at, 0u);
    }
    button.id = 256u;
    button.text = "AA";
    button.text_length = 2u;
    controls[255] = (AstraControl)ASTRA_CONTROL_INIT;
    assert(astra_interface_button_init(&controls[255], &button) == ASTRA_OK);
    parent(&controls[255], 255u, 0u);
    assert(astra_interface_ui_init(&context, controls, 256u, 100u, 50u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &root) == ASTRA_OK);
    assert(controls[255]._private_laid_out != 0u &&
           controls[255]._private_frame.x == 0 &&
           controls[255]._private_frame.y == 0);
}

void astra_interface_test_layout(void)
{
    test_empty_and_invalid();
    test_weighted_growth();
    test_weighted_shrink();
    test_wrap_and_reflow();
    test_breaks();
    test_justification();
    test_column_and_alignment();
    test_tiny_parent_and_maximal_weights();
    test_nested_layout_and_reflow();
    test_nested_layout_rejections();
    test_nested_clip_blocks_input();
    test_deep_layout_has_no_stack_or_depth_limit();
}
