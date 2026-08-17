#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <astra/gui.h>
#include <astra/status.h>
#include <astra/window.h>

static uint32_t call_count;
static uint32_t port_sequence;
static uint32_t reply_receive;
static uint32_t event_receive;
static uint32_t pending_operation;
static uint32_t pending_transaction;
static uint32_t pending_action;
static uint32_t next_open_status;
static uint32_t expected_icon_area;
static uint8_t expected_type = ASTRA_WINDOW_STANDARD;
static AstraGuiWindowCommand last_command;

static void header(AstraMessageHeader *value, uint32_t size,
                   uint32_t operation, uint32_t transaction)
{
    value->total_size = size;
    value->header_size = ASTRA_MESSAGE_HEADER_SIZE;
    value->protocol = ASTRA_GUI_PROTOCOL;
    value->protocol_version = ASTRA_GUI_VERSION;
    value->operation = operation;
    value->transaction_id = transaction;
}

uint32_t astra_ndk_test_syscall(uint32_t number, uintptr_t d1, uintptr_t d2,
                                uintptr_t d3, uintptr_t d4, uintptr_t d5,
                                uint32_t *out_d1, uint32_t *out_d2)
{
    ++call_count;
    *out_d1 = 0u;
    *out_d2 = 0u;
    if (number == ASTRA_SYSCALL_HANDLE_DUPLICATE) {
        assert((d1 == 2u || d1 == expected_icon_area) &&
               d2 == (ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP |
                      ASTRA_RIGHT_TRANSFER));
        *out_d1 = d1 == 2u ? 0x101u : 0x102u;
    } else if (number == ASTRA_SYSCALL_PORT_CREATE) {
        assert((d1 == 1u &&
                (d2 == sizeof(AstraGuiWindowOpened) ||
                 d2 == sizeof(AstraGuiWindowState))) ||
               (d1 == 8u && d2 == sizeof(AstraGuiWindowEvent) * 8u));
        ++port_sequence;
        *out_d1 = 0x200u + port_sequence * 2u;
        *out_d2 = *out_d1 + 1u;
        if (d1 == 8u)
            event_receive = *out_d1;
        else
            reply_receive = *out_d1;
    } else if (number == ASTRA_SYSCALL_PORT_SEND_TRY) {
        uint32_t *handles = (uint32_t *)d4;

        if (d1 == 1u) {
            const AstraGuiOpenWindow *request =
                (const AstraGuiOpenWindow *)d2;

            assert(d5 == (expected_icon_area != 0u ? 4u : 3u));
            assert(handles[2] == reply_receive + 1u);
            assert(d3 == sizeof(*request) && handles[0] == 0x101u &&
                   handles[1] == event_receive + 1u);
            assert(request->title_icon_length ==
                   (expected_icon_area != 0u ? 64u : 0u));
            if (expected_icon_area != 0u)
                assert(handles[3] == 0x102u);
            assert(request->type == expected_type);
            if (expected_type == ASTRA_WINDOW_STANDARD)
                assert(request->title_length == 7u && request->title[0] == 'G');
            else
                assert(request->flags == 0u && request->gadgets == 0u &&
                       request->event_mask == 0u);
            pending_operation = ASTRA_GUI_WINDOW_OPENED;
            pending_transaction = request->header.transaction_id;
        } else {
            const AstraGuiWindowCommand *request =
                (const AstraGuiWindowCommand *)d2;

            assert(d1 == 0x303u && d3 == sizeof(*request));
            assert(request->window == 7u && request->generation != 0u);
            assert(request->action >= ASTRA_GUI_WINDOW_QUERY &&
                   request->action <= ASTRA_GUI_WINDOW_PRESENT);
            if (request->action == ASTRA_GUI_WINDOW_CLOSE)
                assert(handles == 0 && d5 == 0u);
            else
                assert(d5 == 1u && handles[0] == reply_receive + 1u);
            last_command = *request;
            if (request->action != ASTRA_GUI_WINDOW_CLOSE) {
                pending_action = request->action;
                pending_operation = ASTRA_GUI_WINDOW_STATE;
                pending_transaction = request->header.transaction_id;
            }
        }
    } else if (number == ASTRA_SYSCALL_PORT_RECEIVE_TRY) {
        assert(d1 == reply_receive || d1 == event_receive);
        if (d1 == event_receive) {
            AstraGuiWindowEvent *message = (AstraGuiWindowEvent *)d2;

            assert(d3 == sizeof(*message) && d5 == 0u);
            header(&message->header, sizeof(*message),
                   ASTRA_GUI_WINDOW_EVENT, 9u);
            message->event.size = sizeof(message->event);
            message->event.version = ASTRA_WINDOW_EVENT_VERSION;
            message->event.type = ASTRA_WINDOW_EVENT_POINTER_MOTION;
            message->event.generation = 9u;
            message->event.data.pointer.x = 12;
            message->event.data.pointer.y = 18;
            message->event.data.pointer.screen_x = 52;
            message->event.data.pointer.screen_y = 68;
            *out_d1 = sizeof(*message);
        } else if (pending_operation == ASTRA_GUI_WINDOW_OPENED) {
            AstraGuiWindowOpened *reply = (AstraGuiWindowOpened *)d2;
            uint32_t *handles = (uint32_t *)d4;

            assert(d3 == sizeof(*reply) && d4 != 0u && d5 == 1u);
            header(&reply->header, sizeof(*reply), ASTRA_GUI_WINDOW_OPENED,
                   pending_transaction);
            reply->status = next_open_status;
            *out_d1 = sizeof(*reply);
            if (next_open_status == ASTRA_STATUS_OK) {
                reply->window = 7u;
                reply->generation = 9u;
                handles[0] = 0x303u;
                *out_d2 = 1u;
            }
        } else {
            AstraGuiWindowState *reply = (AstraGuiWindowState *)d2;

            assert(pending_operation == ASTRA_GUI_WINDOW_STATE);
            assert(d3 == sizeof(*reply) && d5 == 0u);
            header(&reply->header, sizeof(*reply), ASTRA_GUI_WINDOW_STATE,
                   pending_transaction);
            reply->window = 7u;
            reply->generation = last_command.generation + 1u;
            reply->x = last_command.action == ASTRA_GUI_WINDOW_MOVE ?
                       last_command.x : 40u;
            reply->y = last_command.action == ASTRA_GUI_WINDOW_MOVE ?
                       last_command.y : 50u;
            reply->width = last_command.action == ASTRA_GUI_WINDOW_RESIZE ?
                           last_command.width : 320u;
            reply->height = last_command.action == ASTRA_GUI_WINDOW_RESIZE ?
                            last_command.height : 180u;
            reply->flags = ASTRA_WINDOW_RESIZABLE;
            reply->state = pending_action == ASTRA_GUI_WINDOW_MINIMIZE ?
                           ASTRA_WINDOW_STATE_MINIMIZED :
                           (pending_action == ASTRA_GUI_WINDOW_MAXIMIZE ?
                            ASTRA_WINDOW_STATE_MAXIMIZED :
                            ASTRA_WINDOW_STATE_NORMAL);
            reply->z_order = 3u;
            *out_d1 = sizeof(*reply);
        }
        if (d1 == reply_receive)
            pending_operation = 0u;
    } else {
        assert(number == ASTRA_SYSCALL_CLOSE);
        assert(d1 == reply_receive || d1 == event_receive || d1 == 0x303u);
    }
    return ASTRA_SYSCALL_OK;
}

static void expect_action(uint32_t before, uint32_t action)
{
    assert(call_count == before + 4u);
    assert(last_command.action == action);
}

int main(void)
{
    static const char title[] = "Gallery";
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraWindowCreateInfo create = ASTRA_WINDOW_CREATE_INFO_INIT;
    AstraWindowInfo info = ASTRA_WINDOW_INFO_INIT;
    AstraWindowFrame frame = { 10u, 40u, 400u, 200u };
    AstraWindow window = ASTRA_WINDOW_INIT;
    uint32_t before;

    assert(theme.size == sizeof(theme));
    assert(theme.generation == ASTRA_THEME_GENERATION);
    assert(theme.body_font_height == ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT &&
           theme.title_font_height == ASTRA_THEME_SYSTEM_TITLE_FONT_HEIGHT &&
           theme.mono_font_height == ASTRA_THEME_SYSTEM_MONO_FONT_HEIGHT &&
           theme.mono_cell_width == ASTRA_THEME_SYSTEM_MONO_CELL_WIDTH);
    assert(theme.window_radius == 12 && theme.signal_height == 2);
    assert(theme.title_active.red > theme.title_inactive.red);
    create.width = 320;
    create.height = 180;
    create.pitch = 640;
    create.title = title;
    create.title_length = sizeof(title) - 1u;
    assert(astra_window_create(1, 2, &create, &window) == ASTRA_OK);
    assert(call_count == 6u && window._private_control == 0x303u &&
           window._private_events == event_receive);
    assert(astra_window_event_wait_handle(&window) == event_receive);

    before = call_count;
    assert(astra_window_get_info(&window, &info) == ASTRA_OK);
    expect_action(before, ASTRA_GUI_WINDOW_QUERY);
    assert(info.frame.x == 40u && info.z_order == 3u);
    before = call_count;
    assert(astra_window_set_frame(&window, &frame) == ASTRA_OK);
    expect_action(before, ASTRA_GUI_WINDOW_SET_FRAME);
    before = call_count;
    assert(astra_window_move(&window, 60u, 70u) == ASTRA_OK);
    expect_action(before, ASTRA_GUI_WINDOW_MOVE);
    assert(last_command.x == 60u && last_command.y == 70u);
    before = call_count;
    assert(astra_window_resize(&window, 500u, 240u) == ASTRA_OK);
    expect_action(before, ASTRA_GUI_WINDOW_RESIZE);
    assert(last_command.width == 500u && last_command.height == 240u);

#define CHECK_ACTION(function, action) do { \
        before = call_count; \
        assert(function(&window) == ASTRA_OK); \
        expect_action(before, action); \
    } while (0)
    CHECK_ACTION(astra_window_raise, ASTRA_GUI_WINDOW_RAISE);
    CHECK_ACTION(astra_window_lower, ASTRA_GUI_WINDOW_LOWER);
    CHECK_ACTION(astra_window_activate, ASTRA_GUI_WINDOW_ACTIVATE);
    CHECK_ACTION(astra_window_deactivate, ASTRA_GUI_WINDOW_DEACTIVATE);
    CHECK_ACTION(astra_window_minimize, ASTRA_GUI_WINDOW_MINIMIZE);
    CHECK_ACTION(astra_window_maximize, ASTRA_GUI_WINDOW_MAXIMIZE);
    CHECK_ACTION(astra_window_restore, ASTRA_GUI_WINDOW_RESTORE);
#undef CHECK_ACTION

    before = call_count;
    assert(astra_window_set_title(&window, "Renamed", 7u) == ASTRA_OK);
    expect_action(before, ASTRA_GUI_WINDOW_SET_TITLE);
    assert(last_command.title_length == 7u && last_command.title[0] == 'R');
    before = call_count;
    assert(astra_window_set_event_mask(
               &window, ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION |
                            ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON) == ASTRA_OK);
    expect_action(before, ASTRA_GUI_WINDOW_SET_EVENT_MASK);
    assert(last_command.flags ==
           (ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION |
            ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON));
    before = call_count;
    assert(astra_window_present(&window) == ASTRA_OK);
    expect_action(before, ASTRA_GUI_WINDOW_PRESENT);
    before = call_count;
    assert(astra_window_present_region(&window, &frame) == ASTRA_OK);
    expect_action(before, ASTRA_GUI_WINDOW_PRESENT);
    assert(last_command.x == frame.x && last_command.y == frame.y &&
           last_command.width == frame.width &&
           last_command.height == frame.height);
    frame.width = 0u;
    before = call_count;
    assert(astra_window_present_region(&window, &frame) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(call_count == before);
    frame.width = 400u;
    {
        AstraWindowEvent event = {0};
        uint32_t generation = window._private_generation;

        before = call_count;
        assert(astra_window_event_try(&window, &event) == ASTRA_OK);
        assert(call_count == before + 1u);
        assert(event.type == ASTRA_WINDOW_EVENT_POINTER_MOTION);
        assert(event.data.pointer.x == 12 && event.data.pointer.y == 18);
        assert(event.data.pointer.screen_x == 52 &&
               event.data.pointer.screen_y == 68);
        assert(window._private_generation == generation);
    }
    before = call_count;
    {
        uint32_t before_ports = port_sequence;

        assert(astra_window_close(&window) == ASTRA_OK);
        assert(call_count == before + 3u);
        assert(port_sequence == before_ports);
    }
    assert(last_command.action == ASTRA_GUI_WINDOW_CLOSE);
    assert(window._private_control == 0u && window._private_events == 0u &&
           window._private_id == 0u);
    assert(astra_window_event_wait_handle(&window) == ASTRA_INVALID_HANDLE);

    window = (AstraWindow)ASTRA_WINDOW_INIT;
    create.title_icon_area = 9u;
    create.title_icon_length = 64u;
    expected_icon_area = 9u;
    assert(astra_window_create(1u, 2u, &create, &window) == ASTRA_OK);
    assert(astra_window_close(&window) == ASTRA_OK);
    create.title_icon_area = ASTRA_INVALID_HANDLE;
    create.title_icon_length = 0u;
    expected_icon_area = 0u;

    window = (AstraWindow)ASTRA_WINDOW_INIT;
    create.type = ASTRA_WINDOW_DESKTOP;
    create.content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST;
    create.pitch = 0u;
    create.flags = 0u;
    create.gadgets = 0u;
    create.title = NULL;
    create.title_length = 0u;
    create.event_mask = 0u;
    expected_type = ASTRA_WINDOW_DESKTOP;
    assert(astra_window_create(1, 2, &create, &window) == ASTRA_OK);
    assert(astra_window_close(&window) == ASTRA_OK);
    create.type = ASTRA_WINDOW_STANDARD;
    create.content_format = ASTRA_WINDOW_CONTENT_RGB565;
    create.pitch = 640u;
    create.flags = ASTRA_WINDOW_RESIZABLE;
    create.gadgets = ASTRA_WINDOW_GADGET_CLOSE |
                     ASTRA_WINDOW_GADGET_MINIMIZE |
                     ASTRA_WINDOW_GADGET_MAXIMIZE;
    create.title = title;
    create.title_length = sizeof(title) - 1u;
    create.event_mask = ASTRA_WINDOW_SUBSCRIBE_DEFAULT;
    expected_type = ASTRA_WINDOW_STANDARD;

    before = call_count;
    assert(astra_window_move(&window, 1u, 2u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(call_count == before);
    window = (AstraWindow)ASTRA_WINDOW_INIT;
    create.close_state = ASTRA_GADGET_DISABLED + 1;
    assert(astra_window_create(1, 2, &create, &window) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    create.close_state = ASTRA_GADGET_NORMAL;
    create.title_length = ASTRA_WINDOW_TITLE_MAX + 1u;
    assert(astra_window_create(1, 2, &create, &window) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    create.title_length = sizeof(title) - 1u;
    next_open_status = ASTRA_STATUS_LIMIT;
    assert(astra_window_create(1, 2, &create, &window) ==
           ASTRA_ERROR_NO_RESOURCES);
    assert(window._private_control == 0u && window._private_id == 0u &&
           window._private_generation == 0u);
    puts("window contract tests passed");
    return 0;
}
