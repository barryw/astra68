#include <astra/interface_library.h>
#include <astra/draw_list.h>
#include <astra/input.h>
#include <astra/input_modifiers.h>
#include <astra/runtime.h>
#include <astra/fonts.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static uint32_t request, invocations, allocations;
static AstraResult open_result = ASTRA_OK;
static uint8_t enabled = 1u;
static char scratch[8];
static const char *labels[] = {"First", "Second"};
static uint32_t queries;
static uint8_t tree_menu;
static uint8_t popup_mode;
static AstraWindowEvent event(uint16_t type);

AstraResult astra_fonts_text_measure(AstraHandle gui, const char *id,
    uint32_t id_length, uint16_t height, const char *text, uint32_t length,
    uint32_t maximum_width, AstraFontTextMetrics *metrics)
{
    (void)gui; (void)id; (void)id_length; (void)height;
    (void)text; (void)length; (void)maximum_width; (void)metrics;
    assert(0 && "Popup test does not use an installed Label face");
    return ASTRA_ERROR_INVALID_ARGUMENT;
}

void *astra_runtime_allocate(size_t size)
{
    ++allocations;
    return malloc(size);
}
void astra_runtime_deallocate(void *pointer)
{
    if (pointer != NULL) { assert(allocations != 0u); --allocations; }
    free(pointer);
}
AstraResult astra_window_menu_open(AstraWindow *window, const AstraMenuItem *rows,
    uint32_t count, int32_t x, int32_t y, uint32_t cookie)
{
    if (popup_mode) {
        assert(window != NULL && count == 2u && cookie >= UINT32_C(0x80000001));
        assert(x >= 0 && y >= -4);
        assert(rows[0].label_length == 4u &&
               memcmp(rows[0].label, "Name", 4u) == 0);
        assert(rows[1].label_length == 13u &&
               memcmp(rows[1].label, "Date modified", 13u) == 0);
        assert(rows[0].flags == ASTRA_MENU_CHECKED && rows[1].flags == 0u);
        request = cookie;
        return open_result;
    }
    assert(window != NULL && count == 2u && x == 12 && y == 24 && cookie != 0u);
    /* Both queries reuse scratch. Each label must have been copied at query time. */
    assert(rows[0].label_length == 5u && memcmp(rows[0].label, "First", 5u) == 0);
    assert(rows[1].label_length == 6u && memcmp(rows[1].label, "Second", 6u) == 0);
    assert(rows[0].flags == (enabled ? 0u : ASTRA_MENU_DISABLED));
    assert(rows[1].flags == (ASTRA_MENU_CHECKED |
        (tree_menu ? ASTRA_MENU_PARENT(1u) : 0u)));
    request = cookie;
    return open_result;
}
static void popup_session(void)
{
    static const AstraChoiceItem items[] = {
        {"Name", 4u, {0u, 0u}},
        {"Date modified", 13u, {0u, 0u}}
    };
    AstraChoiceInfo info = ASTRA_CHOICE_INFO_INIT;
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraWindow window = ASTRA_WINDOW_INIT;
    AstraPopup popup = ASTRA_POPUP_INIT;
    AstraWindowEvent result = event(ASTRA_WINDOW_EVENT_MENU_RESULT);
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    int handled = 0;
    int64_t selected = -1;

    info.id = 44u;
    info.items = items;
    info.item_count = 2u;
    info.selected = 0u;
    assert(astra_interface_popup_init(&control, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &control, 1u, 320u, 120u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    popup_mode = 1u;
    assert(astra_interface_popup_open(&popup, &context, &control, &window) ==
           ASTRA_OK && allocations == 0u);
    assert(astra_interface_popup_open(&popup, &context, &control, &window) ==
           ASTRA_ERROR_BUSY);
    result.data.menu_result.request = request - 1u;
    assert(astra_interface_popup_handle_event(&popup, &result, &action,
           &handled) == ASTRA_OK && !handled);
    result.data.menu_result.request = request;
    result.data.menu_result.status = ASTRA_STATUS_OK;
    result.data.menu_result.selection = 2u;
    assert(astra_interface_popup_handle_event(&popup, &result, &action,
           &handled) == ASTRA_OK && handled &&
           action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.control_id == 44u && action.value == 1);
    assert(astra_interface_control_get_value(&control, &selected, NULL) ==
           ASTRA_OK && selected == 1);
    popup_mode = 0u;
}
AstraResult astra_window_menu_close(AstraWindow *window, uint32_t cookie)
{
    assert(window != NULL && cookie == request);
    return ASTRA_OK;
}
static AstraResult query(void *context, AstraCommandState *state)
{
    uint32_t at = (uint32_t)(uintptr_t)context;
    ++queries;
    strcpy(scratch, labels[at]);
    *state = (AstraCommandState){scratch, (uint32_t)strlen(scratch),
                                at == 0u ? enabled : 1u, at == 1u};
    return ASTRA_OK;
}
static AstraResult invoke(void *context, const AstraCommandArguments *args)
{
    assert((context == NULL || context == (void *)(uintptr_t)1u) && args == NULL);
    ++invocations;
    return ASTRA_OK;
}
static AstraWindowEvent event(uint16_t type)
{
    AstraWindowEvent value = {.size = sizeof(value), .version = ASTRA_WINDOW_EVENT_VERSION,
                              .type = type};
    return value;
}
static AstraMenuState state(const AstraMenu *menu)
{
    AstraMenuState value;
    assert(astra_interface_menu_get_state(menu, &value) == ASTRA_OK);
    return value;
}
static AstraUIAction send_key(AstraMenu *menu, uint32_t usage)
{
    AstraWindowEvent key = event(ASTRA_WINDOW_EVENT_KEY);
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    key.flags = ASTRA_WINDOW_EVENT_DOWN;
    key.data.key.usage = usage;
    assert(astra_interface_menu_handle_event(menu, &key, &action) == ASTRA_OK);
    return action;
}
static AstraUIAction pointer(AstraMenu *menu, int32_t x, int32_t y, uint32_t button, int down)
{
    AstraWindowEvent value = event(button == 0u ? ASTRA_WINDOW_EVENT_POINTER_MOTION :
                                                ASTRA_WINDOW_EVENT_POINTER_BUTTON);
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    value.data.pointer.screen_x = x;
    value.data.pointer.screen_y = y;
    value.data.pointer.button = button;
    value.flags = down ? ASTRA_WINDOW_EVENT_DOWN : 0u;
    assert(astra_interface_menu_handle_event(menu, &value, &action) == ASTRA_OK);
    return action;
}
static void model(void)
{
    const AstraControlFrame work = {0, 34, 640, 400};
    AstraMenuItem items[] = {{"Open", 4u, 0u}, {NULL, 0u, ASTRA_MENU_SEPARATOR},
        {"Unavailable", 11u, ASTRA_MENU_DISABLED}, {"Checked", 7u, ASTRA_MENU_CHECKED}};
    AstraMenu menu = ASTRA_MENU_INIT;
    AstraMenuState info;
    AstraUIAction action;
    uint32_t draw[ASTRA_DRAW_LIST_AREA_BYTES / 4u];
    AstraSurfaceView surface;

    assert(astra_interface_menu_init(&menu, items, 4u, &work, 630, 430, 160u, 0u) == ASTRA_OK);
    info = state(&menu);
    assert(info.frame.width == 160u && info.frame.height == 100u &&
           info.frame.x == 470 && info.frame.y == 330);
    AstraControlFrame first_row;
    assert(astra_interface_menu_row_frame(&menu, 1u, &first_row) == ASTRA_OK);
    assert(first_row.x == 470 && first_row.y == 334 && first_row.height == 28u);
    assert(astra_interface_menu_hit(&menu, 474, 334) == 1u);
    assert(astra_interface_menu_hit(&menu, 473, 334) == 0u);
    assert(astra_interface_menu_hit(&menu, 478, 364) == 0u); /* separator */
    assert(astra_interface_menu_hit(&menu, 478, 376) == 0u); /* disabled */
    assert(astra_interface_menu_hit(&menu, 478, 406) == 4u);
    assert(send_key(&menu, 0x51u).type == ASTRA_UI_ACTION_NONE && state(&menu).selection == 1u);
    assert(send_key(&menu, 0x51u).type == ASTRA_UI_ACTION_NONE && state(&menu).selection == 4u);
    assert(send_key(&menu, 0x51u).type == ASTRA_UI_ACTION_NONE && state(&menu).selection == 1u);
    assert(send_key(&menu, 0x52u).type == ASTRA_UI_ACTION_NONE && state(&menu).selection == 4u);
    assert(astra_draw_list_view_init(&surface, draw, sizeof(draw), 160u, 100u));
    assert(astra_interface_menu_render(&menu, &surface) == ASTRA_OK);
    surface.kind = ASTRA_SURFACE_VIEW_RGB565;
    assert(astra_interface_menu_render(&menu, &surface) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    surface.kind = ASTRA_SURFACE_VIEW_DRAW_LIST;
    assert(((AstraDrawListHeader *)draw)->command_count >= 8u);
    {
        AstraDrawListHeader *header = (AstraDrawListHeader *)draw;
        uint32_t before = header->command_count;
        header->command_count = ASTRA_DRAW_LIST_COMMAND_MAX;
        assert(astra_interface_menu_render(&menu, &surface) == ASTRA_ERROR_NO_RESOURCES);
        assert(header->command_count == ASTRA_DRAW_LIST_COMMAND_MAX);
        header->command_count = before;
    }
    assert(astra_draw_list_view_init(&surface, draw, sizeof(draw), 160u, 100u));
    assert(astra_interface_menu_render(&menu, &surface) == ASTRA_OK);
    surface.clip_right = 161u;
    assert(astra_interface_menu_render(&menu, &surface) == ASTRA_ERROR_INVALID_ARGUMENT);
    action = send_key(&menu, 0x28u);
    assert(action.type == ASTRA_UI_ACTION_ACTIVATE && action.control_id == 4u && !state(&menu).active);
    assert(send_key(&menu, 0x28u).type == ASTRA_UI_ACTION_NONE);

    assert(astra_interface_menu_init(&menu, items, 4u, &work, 50, 60, 160u, 0u) == ASTRA_OK);
    assert(pointer(&menu, 70, 80, ASTRA_INPUT_BUTTON_LEFT, 0).type == ASTRA_UI_ACTION_NONE);
    assert(pointer(&menu, 70, 80, ASTRA_INPUT_BUTTON_LEFT, 1).type == ASTRA_UI_ACTION_NONE);
    assert(pointer(&menu, 70, 174, ASTRA_INPUT_BUTTON_LEFT, 0).type == ASTRA_UI_ACTION_NONE);
    assert(pointer(&menu, 70, 80, ASTRA_INPUT_BUTTON_LEFT, 1).type == ASTRA_UI_ACTION_NONE);
    action = pointer(&menu, 70, 80, ASTRA_INPUT_BUTTON_LEFT, 0);
    assert(action.type == ASTRA_UI_ACTION_ACTIVATE && action.control_id == 1u);
    assert(astra_interface_menu_init(&menu, items, 4u, &work, 50, 60, 160u,
        ASTRA_MENU_TRACK_OPEN_PRESS) == ASTRA_OK);
    assert(pointer(&menu, 70, 80, ASTRA_INPUT_BUTTON_LEFT, 0).type == ASTRA_UI_ACTION_ACTIVATE);
    assert(astra_interface_menu_init(&menu, items, 4u, &work, 50, 60, 160u, 0u) == ASTRA_OK);
    assert(pointer(&menu, 0, 0, ASTRA_INPUT_BUTTON_RIGHT, 1).type == ASTRA_UI_ACTION_DISMISS);
    assert(astra_interface_menu_init(&menu, items, 4u, &work, 50, 60, 160u, 0u) == ASTRA_OK);
    assert(send_key(&menu, 0x29u).type == ASTRA_UI_ACTION_DISMISS);
    for (uint32_t loss = 0u; loss < 2u; ++loss) {
        AstraWindowEvent reset = event(loss ? ASTRA_WINDOW_EVENT_POINTER_BUTTON : ASTRA_WINDOW_EVENT_STATE_RESET);
        reset.flags = loss ? ASTRA_WINDOW_EVENT_LOSS : 0u;
        action = (AstraUIAction)ASTRA_UI_ACTION_INIT;
        assert(astra_interface_menu_init(&menu, items, 4u, &work, 50, 60, 160u, 0u) == ASTRA_OK);
        assert(astra_interface_menu_handle_event(&menu, &reset, &action) == ASTRA_OK);
        assert(action.type == ASTRA_UI_ACTION_DISMISS);
    }
    {
        AstraMenu before = menu;
        items[0].label = "Line\n";
        items[0].label_length = 5u;
        assert(astra_interface_menu_init(&menu, items, 4u, &work, 50, 60, 160u, 0u) == ASTRA_ERROR_INVALID_ARGUMENT);
        assert(memcmp(&before, &menu, sizeof(menu)) == 0);
        items[0] = (AstraMenuItem){NULL, 0u, ASTRA_MENU_SEPARATOR};
        assert(astra_interface_menu_init(&menu, items, 4u, &work, 50, 60, 160u, 0u) == ASTRA_ERROR_INVALID_ARGUMENT);
    }
    {
        AstraMenuItem many[200];
        AstraWindowEvent wheel = event(ASTRA_WINDOW_EVENT_POINTER_WHEEL);
        for (uint32_t at = 0u; at < 200u; ++at) many[at] = (AstraMenuItem){"Row", 3u, 0u};
        assert(astra_interface_menu_init(&menu, many, 200u, &work, 0, 34, 160u, 0u) == ASTRA_OK);
        assert(state(&menu).frame.height == 400u);
        assert(send_key(&menu, 0x4du).type == ASTRA_UI_ACTION_NONE);
        assert(state(&menu).selection == 200u && state(&menu).scroll == 5208u);
        assert(send_key(&menu, 0x4eu).type == ASTRA_UI_ACTION_NONE && state(&menu).selection == 200u);
        assert(astra_draw_list_view_init(&surface, draw, sizeof(draw), 160u, 400u));
        assert(astra_interface_menu_render(&menu, &surface) == ASTRA_OK);
        assert(((AstraDrawListHeader *)draw)->command_count < ASTRA_DRAW_LIST_COMMAND_MAX);
        wheel.data.wheel.delta_y = INT32_MAX;
        action = (AstraUIAction)ASTRA_UI_ACTION_INIT;
        assert(astra_interface_menu_handle_event(&menu, &wheel, &action) == ASTRA_OK);
        assert(state(&menu).scroll == 0u && state(&menu).selection == 0u);
        assert(send_key(&menu, 0x4au).type == ASTRA_UI_ACTION_NONE && state(&menu).selection == 1u);
        assert(send_key(&menu, 0x4bu).type == ASTRA_UI_ACTION_NONE && state(&menu).selection == 1u);
        wheel.data.wheel.delta_y = INT32_MIN;
        assert(astra_interface_menu_handle_event(&menu, &wheel, &action) == ASTRA_OK);
        assert(state(&menu).scroll == 5208u);
    }
}
static void context(void)
{
    const AstraCommand commands[] = {{"first", 5u, 0u, 0u, query, invoke, NULL},
        {"second", 6u, 0u, 0u, query, invoke, (void *)(uintptr_t)1u}};
    const AstraCommandScope scope = {NULL, commands, 2u};
    const AstraContextMenuItem items[] = {{"first", 5u, NULL}, {"second", 6u, NULL}};
    AstraContextMenu menu = ASTRA_CONTEXT_MENU_INIT;
    AstraWindow window = ASTRA_WINDOW_INIT;
    AstraWindowEvent result = event(ASTRA_WINDOW_EVENT_MENU_RESULT);
    uint32_t old;
    int handled;

    assert(astra_interface_context_menu_open(&menu, &window, &scope, items, 2u, 12, 24) == ASTRA_OK);
    assert(allocations == 0u);
    old = request;
    result.data.menu_result.request = request;
    result.data.menu_result.selection = 1u;
    enabled = 0u;
    assert(astra_interface_context_menu_handle_event(&menu, &result, &handled) == ASTRA_ERROR_PERMISSION);
    assert(handled && invocations == 0u && queries == 3u);
    enabled = 1u;
    assert(astra_interface_context_menu_open(&menu, &window, &scope, items, 2u, 12, 24) == ASTRA_OK);
    assert(request != old);
    result.data.menu_result.request = old;
    assert(astra_interface_context_menu_handle_event(&menu, &result, &handled) == ASTRA_OK && handled);
    assert(invocations == 0u);
    result.data.menu_result.request = request;
    assert(astra_interface_context_menu_handle_event(&menu, &result, &handled) == ASTRA_OK);
    assert(invocations == 1u);
    assert(astra_interface_context_menu_handle_event(&menu, &result, &handled) == ASTRA_OK);
    assert(invocations == 1u);
    assert(astra_interface_context_menu_open(&menu, &window, &scope, items, 2u, 12, 24) == ASTRA_OK);
    old = request;
    open_result = ASTRA_ERROR_BUSY;
    assert(astra_interface_context_menu_open(&menu, &window, &scope, items, 2u, 12, 24) == ASTRA_ERROR_BUSY);
    assert(allocations == 0u);
    result.data.menu_result.request = old;
    result.flags = ASTRA_WINDOW_EVENT_LOSS;
    assert(astra_interface_context_menu_handle_event(&menu, &result, &handled) == ASTRA_ERROR_IO);
    assert(invocations == 1u);
    open_result = ASTRA_OK;
    assert(astra_interface_context_menu_open(&menu, &window, &scope, items, 2u, 12, 24) == ASTRA_OK);
    assert(astra_interface_context_menu_close(&menu) == ASTRA_OK);
    result.flags = 0u;
    result.data.menu_result.request = request;
    assert(astra_interface_context_menu_handle_event(&menu, &result, &handled) == ASTRA_OK && invocations == 1u);
    assert(astra_interface_context_menu_open(&menu, &window, &scope, items, 2u, 12, 24) == ASTRA_OK);
    result.data.menu_result.request = request;
    result.data.menu_result.selection = 0u;
    result.data.menu_result.status = ASTRA_STATUS_LIMIT;
    assert(astra_interface_context_menu_handle_event(&menu, &result, &handled) == ASTRA_ERROR_NO_RESOURCES);
    assert(invocations == 1u);
    result.type = ASTRA_WINDOW_EVENT_KEY;
    assert(astra_interface_context_menu_handle_event(&menu, &result, &handled) == ASTRA_OK && !handled);
    {
        const uint16_t parents[] = {0u, 1u};
        const uint16_t invalid[] = {1u, 0u};
        tree_menu = 1u;
        assert(astra_interface_context_menu_open_tree(&menu, &window, &scope,
            items, invalid, 2u, 12, 24) == ASTRA_ERROR_INVALID_ARGUMENT);
        assert(astra_interface_context_menu_open_tree(&menu, &window, &scope,
            items, parents, 2u, 12, 24) == ASTRA_OK);
        result.type = ASTRA_WINDOW_EVENT_MENU_RESULT;
        result.data.menu_result.request = request;
        result.data.menu_result.selection = 2u;
        result.data.menu_result.status = ASTRA_STATUS_OK;
        assert(astra_interface_context_menu_handle_event(&menu, &result, &handled) == ASTRA_OK);
        assert(handled && invocations == 2u);
        tree_menu = 0u;
    }
}
int main(void)
{
    model();
    context();
    popup_session();
    return 0;
}
