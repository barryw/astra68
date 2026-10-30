#include <astra/control.h>
#include <astra/input.h>
#include <astra/interface_library.h>

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

static AstraWindowEvent resize_event(uint16_t width, uint16_t height)
{
    AstraWindowEvent event = {0};

    event.size = sizeof(event);
    event.version = ASTRA_WINDOW_EVENT_VERSION;
    event.type = ASTRA_WINDOW_EVENT_RESIZE;
    event.data.resize.width = width;
    event.data.resize.height = height;
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
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
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
           controls[2]._private_frame.y ==
               (int32_t)theme.control_height + 5);

    astra_interface_ui_damage_clear(&context);
    event = resize_event(30u, 100u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(controls[0]._private_frame.y == 0);
    assert(controls[1]._private_frame.x == 0 &&
           controls[1]._private_frame.y ==
               (int32_t)theme.control_height + 5);
    assert(controls[2]._private_frame.x == 0 &&
           controls[2]._private_frame.y ==
               ((int32_t)theme.control_height + 5) * 2);
    assert(astra_interface_ui_damage(&context, &damage) == ASTRA_OK);
    assert(damage.x == 0 && damage.y == 0 && damage.width == 30u &&
           damage.height == 100u);

    astra_interface_ui_damage_clear(&context);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(astra_interface_ui_damage(&context, &damage) ==
           ASTRA_ERROR_WOULD_BLOCK);

    event = resize_event(100u, 100u);
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
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
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
           controls[1]._private_frame.y ==
               (int32_t)theme.control_height + 5);
    assert(controls[2]._private_frame.x == 0 &&
           controls[2]._private_frame.y ==
               ((int32_t)theme.control_height + 5) * 2);
    assert(controls[3]._private_frame.x == 34 &&
           controls[3]._private_frame.y ==
               ((int32_t)theme.control_height + 5) * 2);
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
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
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
           controls[0]._private_frame.height == theme.control_height);
    assert(controls[1]._private_frame.x == 45 &&
           controls[1]._private_frame.y ==
               5 + (int32_t)theme.control_height + 4);

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
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
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
           controls[0]._private_frame.height == theme.control_height);
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

    event = resize_event(160u, 80u);
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
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
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
    assert(controls[1]._private_frame.height == theme.control_height &&
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

static void test_public_geometry(void)
{
    AstraControl controls[2];
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraControlFrame frame = {7, 8, 9, 10};
    AstraControlFrame clip = frame;

    buttons(controls, 2u);
    assert(astra_interface_ui_init(&context, controls, 2u, 100u, 20u) ==
           ASTRA_OK);
    assert(astra_interface_ui_geometry(&context, 0u, &frame, &clip) ==
           ASTRA_ERROR_WOULD_BLOCK);
    assert(frame.x == 7 && clip.height == 10u);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(astra_interface_ui_geometry(&context, 0u, &frame, &clip) ==
           ASTRA_OK);
    assert(frame.x == controls[0]._private_frame.x &&
           frame.height == controls[0]._private_frame.height &&
           clip.height == 20u);
    frame = (AstraControlFrame){7, 8, 9, 10};
    assert(astra_interface_ui_geometry(&context, 2u, &frame, &clip) ==
           ASTRA_ERROR_INVALID_ARGUMENT && frame.x == 7);
    assert(astra_interface_ui_geometry(NULL, 0u, &frame, &clip) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_interface_ui_geometry(&context, 0u, NULL, &clip) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_interface_ui_set_state(&context, &controls[0],
                                             ASTRA_CONTROL_COLLAPSED) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    clip = frame;
    assert(astra_interface_ui_geometry(&context, 0u, &frame, &clip) ==
           ASTRA_ERROR_WOULD_BLOCK && frame.x == 7 && clip.x == 7);
}

static void test_scroll_content_respects_maximum(void)
{
    AstraControl controls[2] = {ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraScrollModel model = ASTRA_SCROLL_MODEL_INIT;
    AstraScrollModelInfo scroll = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraScrollViewInfo view = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout root = ASTRA_FLEX_LAYOUT_INIT;
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;

    assert(astra_scroll_init(&model, &scroll) == ASTRA_OK);
    view.id = 1u;
    view.model = &model;
    view.preferred_width = 40u;
    view.preferred_height = 20u;
    assert(astra_interface_scroll_view_init(&controls[0], &view) == ASTRA_OK);
    label.id = 2u;
    label.text = "A VERY LONG DIRECTORY NAME";
    label.text_length = sizeof("A VERY LONG DIRECTORY NAME") - 1u;
    assert(astra_interface_label_init(&controls[1], &label) == ASTRA_OK);
    item.parent_id = 1u;
    item.minimum_width = item.maximum_width = 40u;
    item.minimum_height = 80u;
    assert(astra_interface_control_set_flex(&controls[1], &item) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 2u, 40u, 20u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &root) == ASTRA_OK);
    assert(astra_scroll_get_state(&model, &state) == ASTRA_OK);
    assert(state.content_width == 40u && state.maximum_x == 0u &&
           state.content_height == 80u && state.maximum_y == 60u);
}

static void test_scroll_child_fills_viewport(void)
{
    AstraControl controls[2] = {ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraScrollModel model = ASTRA_SCROLL_MODEL_INIT;
    AstraScrollModelInfo scroll = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraScrollViewInfo view = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout root = ASTRA_FLEX_LAYOUT_INIT;
    AstraControlFrame frame, clip;

    assert(astra_scroll_init(&model, &scroll) == ASTRA_OK);
    view.id = 1u;
    view.model = &model;
    view.preferred_width = 200u;
    view.preferred_height = 100u;
    assert(astra_interface_scroll_view_init(&controls[0], &view) == ASTRA_OK);
    label.id = 2u;
    label.text = "Short";
    label.text_length = 5u;
    assert(astra_interface_label_init(&controls[1], &label) == ASTRA_OK);
    item.parent_id = 1u;
    assert(astra_interface_control_set_flex(&controls[1], &item) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 2u, 200u, 100u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &root) == ASTRA_OK);
    assert(astra_interface_ui_geometry(&context, 1u, &frame, &clip) == ASTRA_OK);
    assert(frame.width == 200u && frame.height == 100u);
}

static void test_settings_pane(void)
{
    AstraControl controls[7];
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraSettingsPaneInfo pane = ASTRA_SETTINGS_PANE_INFO_INIT;
    AstraFlexLayout root = ASTRA_SETTINGS_PANE_LAYOUT_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraToggleInfo option = ASTRA_TOGGLE_INFO_INIT;
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraControlFrame frame, clip;

    pane.first_id = 10u;
    pane.title = "Preferences";
    pane.title_length = 11u;
    pane.description = "Common controls";
    pane.description_length = 15u;
    assert(astra_interface_settings_pane_init(NULL, &pane) == ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_interface_settings_pane_init(controls, NULL) == ASTRA_ERROR_INVALID_ARGUMENT);
    pane.first_id = UINT32_MAX;
    assert(astra_interface_settings_pane_init(controls, &pane) == ASTRA_ERROR_INVALID_ARGUMENT);
    pane.first_id = 10u;
    pane.reserved[2] = 1u;
    assert(astra_interface_settings_pane_init(controls, &pane) == ASTRA_ERROR_INVALID_ARGUMENT);
    pane.reserved[2] = 0u;
    pane.title = "\xff";
    pane.title_length = 1u;
    assert(astra_interface_settings_pane_init(controls, &pane) == ASTRA_ERROR_INVALID_ARGUMENT);
    pane.title = "Preferences";
    pane.title_length = 11u;
    assert(astra_interface_settings_pane_init(controls, &pane) == ASTRA_OK);
    option.id = 20u;
    option.text = "Enabled";
    option.text_length = 7u;
    item.parent_id = pane.first_id + ASTRA_SETTINGS_PANE_BODY;
    assert(astra_interface_checkbox_init(&controls[4], &option) == ASTRA_OK);
    assert(astra_interface_control_set_flex(&controls[4], &item) == ASTRA_OK);
    button.text = "Apply settings";
    button.text_length = 14u;
    item.parent_id = pane.first_id + ASTRA_SETTINGS_PANE_ACTIONS;
    item.minimum_width = 128u;
    for (uint32_t at = 5u; at < 7u; ++at) {
        button.id = 20u + at;
        assert(astra_interface_button_init(&controls[at], &button) == ASTRA_OK);
        assert(astra_interface_control_set_flex(&controls[at], &item) == ASTRA_OK);
    }
    assert(astra_interface_ui_init(&context, controls, 7u, 800u, 300u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &root) == ASTRA_OK);
    assert(astra_interface_ui_geometry(&context, ASTRA_SETTINGS_PANE_TITLE, &frame, &clip) == ASTRA_OK);
    assert(frame.x == 24 && frame.y == 24 && frame.width == 752u);
    assert(controls[5]._private_frame.y < controls[6]._private_frame.y);
    AstraWindowEvent resize = resize_event(200u, 300u);
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    assert(astra_interface_ui_handle_event(&context, &resize, &action) == ASTRA_OK);
    assert(controls[5]._private_frame.y < controls[6]._private_frame.y);
    for (uint32_t at = 4u; at < 7u; ++at) {
        assert(astra_interface_ui_geometry(&context, at, &frame, &clip) == ASTRA_OK);
        assert(frame.x >= 24 && frame.y >= 24 &&
            (uint32_t)frame.x + frame.width <= 176u &&
            (uint32_t)frame.y + frame.height <= 276u);
    }
    AstraWindowEvent key = {0};
    key.size = sizeof(key); key.version = ASTRA_WINDOW_EVENT_VERSION;
    key.type = ASTRA_WINDOW_EVENT_KEY; key.flags = ASTRA_WINDOW_EVENT_DOWN;
    key.data.key.usage = 0x2bu;
    assert(astra_interface_ui_handle_event(&context, &key, &action) == ASTRA_OK);
    key.data.key.usage = 0x28u;
    assert(astra_interface_ui_handle_event(&context, &key, &action) == ASTRA_OK);
    key.flags = 0u;
    assert(astra_interface_ui_handle_event(&context, &key, &action) == ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_VALUE_CHANGED && action.control_id == option.id);
    pane.description = NULL; pane.description_length = 0u;
    assert(astra_interface_settings_pane_init(controls, &pane) == ASTRA_ERROR_INVALID_ARGUMENT);
}

static void test_form_row(void)
{
    AstraControl controls[6] = {0};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFormRowInfo row = ASTRA_FORM_ROW_INFO_INIT;
    AstraToggleInfo value = ASTRA_TOGGLE_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout root = ASTRA_SETTINGS_PANE_LAYOUT_INIT;
    AstraControlFrame label_frame, value_frame, message_frame, clip;

    row.first_id = 10u;
    row.label = "Audio output";
    row.label_length = 12u;
    row.message = "Uses HDMI speakers";
    row.message_length = 18u;
    assert(astra_interface_form_row_init(NULL, &row) == ASTRA_ERROR_INVALID_ARGUMENT);
    row.first_id = UINT32_MAX;
    assert(astra_interface_form_row_init(controls, &row) == ASTRA_ERROR_INVALID_ARGUMENT);
    row.first_id = 10u;
    row.message_role = ASTRA_TEXT_CLIENT_PRIMARY;
    assert(astra_interface_form_row_init(controls, &row) == ASTRA_ERROR_INVALID_ARGUMENT);
    row.message_role = ASTRA_TEXT_CLIENT_SECONDARY;
    row.reserved[0] = 1u;
    assert(astra_interface_form_row_init(controls, &row) == ASTRA_ERROR_INVALID_ARGUMENT);
    row.reserved[0] = 0u;
    assert(astra_interface_form_row_init(controls, &row) == ASTRA_OK);
    value.id = 20u;
    value.text = "Enabled";
    value.text_length = 7u;
    item.parent_id = row.first_id + ASTRA_FORM_ROW_VALUE;
    assert(astra_interface_checkbox_init(&controls[5], &value) == ASTRA_OK);
    assert(astra_interface_control_set_flex(&controls[5], &item) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 6u, 640u, 180u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &root) == ASTRA_OK);
    assert(astra_interface_ui_geometry(&context, ASTRA_FORM_ROW_LABEL,
                                       &label_frame, &clip) == ASTRA_OK);
    assert(astra_interface_ui_geometry(&context, ASTRA_FORM_ROW_VALUE,
                                       &value_frame, &clip) == ASTRA_OK);
    assert(astra_interface_ui_geometry(&context, ASTRA_FORM_ROW_MESSAGE,
                                       &message_frame, &clip) == ASTRA_OK);
    assert(label_frame.width == 110u);
    assert(value_frame.x > label_frame.x);
    assert(message_frame.x == value_frame.x);
    assert(message_frame.y >= value_frame.y + (int32_t)value_frame.height);
    row.message = NULL;
    row.message_length = 0u;
    assert(astra_interface_form_row_init(controls, &row) == ASTRA_OK);
    assert((controls[ASTRA_FORM_ROW_MESSAGE]._private_state &
            ASTRA_CONTROL_COLLAPSED) != 0u);
}

static void test_columns_browser(void)
{
    const AstraCollectionItem first[] = {
        {11u, "Applications", 12u, {0u, 0u}},
        {12u, "Documents", 9u, {0u, 0u}}
    };
    const AstraCollectionItem second[] = {
        {21u, "Doom", 4u, {0u, 0u}},
        {22u, "Quake", 5u, {0u, 0u}}
    };
    const AstraCollectionItem reordered[] = {
        {22u, "Quake", 5u, {0u, 0u}},
        {21u, "Doom", 4u, {0u, 0u}}
    };
    AstraCollectionModel models[2] = {
        ASTRA_COLLECTION_MODEL_INIT, ASTRA_COLLECTION_MODEL_INIT
    };
    AstraScrollModel scrolls[2] = {
        ASTRA_SCROLL_MODEL_INIT, ASTRA_SCROLL_MODEL_INIT
    };
    AstraScrollModelInfo scroll_info = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraColumnView columns[2] = {
        {&models[0], &scrolls[0], 120u, {0u, 0u}},
        {&models[1], &scrolls[1], 120u, {0u, 0u}}
    };
    AstraColumnsInfo info = ASTRA_COLUMNS_INFO_INIT;
    AstraControl controls[5] = {0};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout root = ASTRA_FLEX_LAYOUT_INIT;
    AstraControlFrame left, right, clip;

    assert(astra_scroll_init(&scrolls[0], &scroll_info) == ASTRA_OK);
    assert(astra_scroll_init(&scrolls[1], &scroll_info) == ASTRA_OK);
    assert(astra_interface_collection_init(&models[0], first, 2u, 11u) == ASTRA_OK);
    assert(astra_interface_collection_init(&models[1], second, 2u, 22u) == ASTRA_OK);
    info.first_id = 30u;
    info.columns = columns;
    info.column_count = 2u;
    info.viewport_height = 80u;
    assert(astra_interface_columns_init(NULL, &info) == ASTRA_ERROR_INVALID_ARGUMENT);
    columns[1].scroll = &scrolls[0];
    assert(astra_interface_columns_init(controls, &info) == ASTRA_ERROR_INVALID_ARGUMENT);
    columns[1].scroll = &scrolls[1];
    assert(astra_interface_columns_init(controls, &info) == ASTRA_OK);
    AstraFlexItem expanded = ASTRA_FLEX_ITEM_INIT;
    expanded.grow = 1u;
    assert(astra_interface_control_set_flex(&controls[0], &expanded) == ASTRA_OK);
    assert(controls[2]._private_control_kind == ASTRA_CONTROL_LIST);
    assert(controls[4]._private_control_kind == ASTRA_CONTROL_LIST);
    root.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    assert(astra_interface_ui_init(&context, controls, 5u, 400u, 120u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &root) == ASTRA_OK);
    assert(astra_interface_ui_geometry(&context, 1u, &left, &clip) == ASTRA_OK);
    assert(astra_interface_ui_geometry(&context, 3u, &right, &clip) == ASTRA_OK);
    assert(left.width >= 199u && right.width >= 199u);
    assert(left.width + right.width + 1u == 400u);
    assert(left.height == 120u && right.height == 120u);
    assert(right.x == left.x + (int32_t)left.width + 1);
    assert(astra_interface_list_set_items(&context, &controls[4],
        reordered, 2u) == ASTRA_OK);
    assert(models[1].selected_id == 22u);
    assert(models[0].selected_id == 11u);
    assert(controls[4]._private_value == 0);
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
    test_public_geometry();
    test_scroll_content_respects_maximum();
    test_scroll_child_fills_viewport();
    test_settings_pane();
    test_form_row();
    test_columns_browser();
}
