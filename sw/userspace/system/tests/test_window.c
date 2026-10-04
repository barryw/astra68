#define _GNU_SOURCE

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>

#include <astra/gui.h>
#include <astra/port.h>
#include <astra/status.h>
#include <astra/window.h>

#include "syscall.h"

static uint32_t call_count;
static uint32_t port_sequence;
static uint32_t event_receive;
static uint32_t next_open_status;
static uint32_t expected_icon_area;
static uint32_t expected_icon_length;
static uint8_t expected_type = ASTRA_WINDOW_STANDARD;
/* Rights of the content-area duplicate; a content-surface window's area is
   its staging area, which the service writes readback into. */
static uint32_t expected_content_rights =
    ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER;
static uint16_t next_event_type = ASTRA_WINDOW_EVENT_POINTER_MOTION;
static uint32_t next_text_codepoint;
static uint32_t next_state = ASTRA_WINDOW_STATE_NORMAL;
static uint32_t next_state_flags = ASTRA_WINDOW_ACTIVE;
static uint32_t next_resize_width = 500u;
static uint32_t next_resize_height = 240u;
static uint32_t next_system_action = ASTRA_SYSTEM_ACTION_ABOUT;
static AstraGuiWindowCommand last_command;
static uint8_t *pointer_area;

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

uint32_t astra_system_test_syscall(uint32_t number, uintptr_t d1, uintptr_t d2,
                                uintptr_t d3, uintptr_t d4, uintptr_t d5,
                                uint32_t *out_d1, uint32_t *out_d2)
{
    ++call_count;
    *out_d1 = 0u;
    *out_d2 = 0u;
    if (number == ASTRA_SYSCALL_HANDLE_DUPLICATE) {
        assert((d1 == 2u || d1 == expected_icon_area || d1 == 0x505u) &&
               d2 == (d1 == 2u ? expected_content_rights :
                      (ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP |
                       ASTRA_RIGHT_TRANSFER)));
        *out_d1 = d1 == 2u ? 0x101u :
                  (d1 == 0x505u ? 0x103u : 0x102u);
    } else if (number == ASTRA_SYSCALL_AREA_CREATE) {
        assert(d1 == ASTRA_HARDWARE_POINTER_WIDTH *
                     ASTRA_HARDWARE_POINTER_HEIGHT *
                     sizeof(AstraColorRGBA8));
        assert(d2 == (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                      ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER));
        *out_d1 = 0x505u;
    } else if (number == ASTRA_SYSCALL_AREA_MAP) {
        assert(d1 == 0x505u &&
               d2 == (ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE));
        *out_d1 = (uint32_t)(uintptr_t)pointer_area;
        *out_d2 = 4096u;
    } else if (number == ASTRA_SYSCALL_AREA_UNMAP) {
        assert(d1 == (uintptr_t)pointer_area);
    } else if (number == ASTRA_SYSCALL_PORT_CREATE) {
        assert(d1 == 8u && d2 == sizeof(AstraGuiWindowEvent) * 8u);
        ++port_sequence;
        *out_d1 = 0x200u + port_sequence * 2u;
        *out_d2 = *out_d1 + 1u;
        event_receive = *out_d1;
    } else if (number == ASTRA_SYSCALL_PORT_CALL) {
        const AstraCall *call = astra_system_test_call;
        uint32_t port = ((const AstraPortCall *)d1)->port;

        if (port == 1u) {
            const AstraGuiOpenWindow *request = call->request;
            AstraGuiWindowOpened *reply = call->reply;

            /* Content, events, the reply capability, then the icon. */
            assert(call->handle_count ==
                   (expected_icon_area != 0u ? 3u : 2u));
            assert(call->reply_index == 2u);
            assert(call->request_size == sizeof(*request) &&
                   call->handles[0] == 0x101u &&
                   call->handles[1] == event_receive + 1u);
            assert(request->title_icon_length ==
                   (expected_icon_area != 0u ? expected_icon_length : 0u));
            if (expected_icon_area != 0u)
                assert(call->handles[2] == 0x102u);
            assert(request->type == expected_type);
            if (expected_type == ASTRA_WINDOW_STANDARD)
                assert(request->title_length == 7u && request->title[0] == 'G');
            else
                assert(request->flags == 0u && request->gadgets == 0u &&
                       request->event_mask == 0u);
            assert(call->reply_capacity == sizeof(*reply) &&
                   call->reply_handle_capacity == 2u);
            header(&reply->header, sizeof(*reply), ASTRA_GUI_WINDOW_OPENED,
                   request->header.transaction_id);
            reply->status = next_open_status;
            *out_d1 = sizeof(*reply);
            if (next_open_status == ASTRA_STATUS_OK) {
                reply->window = 7u;
                reply->generation = 9u;
                call->reply_handles[0] = 0x303u;
                call->reply_handles[1] = 0x404u;
                *out_d2 = 2u;
            }
        } else {
            const AstraGuiWindowCommand *request = call->request;
            AstraGuiWindowState *reply = call->reply;

            assert(port == 0x303u && call->request_size == sizeof(*request));
            assert(request->window == 7u && request->generation != 0u);
            assert(request->action >= ASTRA_GUI_WINDOW_QUERY &&
                   request->action <= ASTRA_GUI_WINDOW_FULLSCREEN &&
                   request->action != ASTRA_GUI_WINDOW_CLOSE);
            assert(call->reply_index == 0u);
            if (request->action == ASTRA_GUI_WINDOW_SET_POINTER_IMAGE)
                assert(call->handle_count == 1u &&
                       call->handles[0] == 0x103u);
            else
                assert(call->handle_count == 0u);
            assert(call->reply_capacity == sizeof(*reply) &&
                   call->reply_handle_capacity == 0u);
            last_command = *request;
            header(&reply->header, sizeof(*reply), ASTRA_GUI_WINDOW_STATE,
                   request->header.transaction_id);
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
            reply->state = request->action == ASTRA_GUI_WINDOW_MINIMIZE ?
                           ASTRA_WINDOW_STATE_MINIMIZED :
                           (request->action == ASTRA_GUI_WINDOW_MAXIMIZE ?
                            ASTRA_WINDOW_STATE_MAXIMIZED :
                            (request->action == ASTRA_GUI_WINDOW_FULLSCREEN ?
                             ASTRA_WINDOW_STATE_FULLSCREEN :
                             ASTRA_WINDOW_STATE_NORMAL));
            reply->z_order = 3u;
            *out_d1 = sizeof(*reply);
        }
    } else if (number == ASTRA_SYSCALL_PORT_SEND_TRY) {
        const AstraGuiWindowCommand *request =
            (const AstraGuiWindowCommand *)d2;

        assert(d1 == 0x303u && d3 == sizeof(*request) && d4 == 0u &&
               d5 == 0u);
        assert(request->window == 7u && request->generation != 0u &&
               request->action == ASTRA_GUI_WINDOW_CLOSE);
        last_command = *request;
    } else if (number == ASTRA_SYSCALL_PORT_RECEIVE_TRY) {
        AstraGuiWindowEvent *message = (AstraGuiWindowEvent *)d2;

        assert(d1 == event_receive && d3 == sizeof(*message) && d5 == 0u);
        header(&message->header, sizeof(*message),
               ASTRA_GUI_WINDOW_EVENT, 9u);
        message->event.size = sizeof(message->event);
        message->event.version = ASTRA_WINDOW_EVENT_VERSION;
        message->event.type = next_event_type;
        message->event.generation = 9u;
        if (next_event_type == ASTRA_WINDOW_EVENT_TEXT) {
            message->event.data.text.codepoint = next_text_codepoint;
        } else if (next_event_type == ASTRA_WINDOW_EVENT_STATE) {
            message->event.data.state.state = next_state;
            message->event.data.state.flags = next_state_flags;
        } else if (next_event_type == ASTRA_WINDOW_EVENT_RESIZE) {
            message->event.data.resize.width = next_resize_width;
            message->event.data.resize.height = next_resize_height;
        } else if (next_event_type == ASTRA_WINDOW_EVENT_SYSTEM_ACTION) {
            message->event.data.system_action.action = next_system_action;
        } else {
            message->event.data.pointer.x = 12;
            message->event.data.pointer.y = 18;
            message->event.data.pointer.screen_x = 52;
            message->event.data.pointer.screen_y = 68;
            message->event.data.pointer.modifiers =
                ASTRA_INPUT_MOD_LEFT_SHIFT | ASTRA_INPUT_MOD_META;
        }
        *out_d1 = sizeof(*message);
    } else {
        assert(number == ASTRA_SYSCALL_CLOSE);
        assert(d1 == event_receive || d1 == 0x303u || d1 == 0x404u ||
               d1 == 0x505u);
    }
    return ASTRA_SYSCALL_OK;
}

static void expect_action(uint32_t before, uint32_t action)
{
    assert(call_count == before + 1u);
    assert(last_command.action == action);
}

int main(void)
{
    static const char title[] = "Gallery";
    static const char malformed_utf8[] = {(char)0xf4, (char)0x90,
                                          (char)0x80, (char)0x80};
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraWindowCreateInfo create = ASTRA_WINDOW_CREATE_INFO_INIT;
    AstraWindowInfo info = ASTRA_WINDOW_INFO_INIT;
    AstraWindowFrame frame = { 10u, 40u, 400u, 200u };
    AstraWindow window = ASTRA_WINDOW_INIT;
    AstraColorRGBA8 pointer_pixel = {255u, 255u, 255u, 255u};
    AstraHardwarePointerImage pointer_image =
        ASTRA_HARDWARE_POINTER_IMAGE_INIT;
    uint32_t before;

    pointer_area = mmap((void *)(uintptr_t)UINT32_C(0x40010000), 4096u,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                        -1, 0);
    assert(pointer_area != MAP_FAILED);

    _Static_assert(sizeof(AstraWindow) == 20u,
                   "AstraWindow V3 layout changed; bump the ABI major");

    assert(theme.size == sizeof(theme));
    assert(theme.generation == ASTRA_THEME_GENERATION);
    assert(theme.body_font_height == ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT &&
           theme.title_font_height == ASTRA_THEME_SYSTEM_TITLE_FONT_HEIGHT &&
           theme.mono_font_height == ASTRA_THEME_SYSTEM_MONO_FONT_HEIGHT &&
           theme.mono_cell_width == ASTRA_THEME_SYSTEM_MONO_CELL_WIDTH);
    assert(theme.window_radius == 12 && theme.signal_height == 2);
    assert(theme.title_active.red > theme.title_inactive.red);
    assert(create.gadgets == ASTRA_WINDOW_GADGET_AUTO);
    create.width = 320;
    create.height = 180;
    create.pitch = 640;
    create.title = title;
    create.title_length = sizeof(title) - 1u;
    assert(astra_window_create(1, 2, &create, &window) == ASTRA_OK);
    assert(call_count == 3u && window._private_control == 0x303u &&
           window._private_events == event_receive);
    assert(astra_window_event_wait_handle(&window) == event_receive);
    assert(astra_window_vblank_wait_handle(&window) == 0x404u);

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
    CHECK_ACTION(astra_window_fullscreen, ASTRA_GUI_WINDOW_FULLSCREEN);
#undef CHECK_ACTION

    before = call_count;
    assert(astra_window_set_title(&window, "Renamed", 7u) == ASTRA_OK);
    expect_action(before, ASTRA_GUI_WINDOW_SET_TITLE);
    assert(last_command.title_length == 7u && last_command.title[0] == 'R');
    before = call_count;
    assert(astra_window_set_application_name(&window, "Gallery", 7u) ==
           ASTRA_OK);
    expect_action(before, ASTRA_GUI_WINDOW_SET_APPLICATION_NAME);
    assert(last_command.title_length == 7u &&
           last_command.title[0] == 'G');
    before = call_count;
    assert(astra_window_set_application_name(&window, "", 0u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_window_set_application_name(&window, NULL, 1u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_window_set_application_name(&window, malformed_utf8,
                                              sizeof(malformed_utf8)) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(call_count == before);
    before = call_count;
    assert(astra_window_set_title(&window, malformed_utf8,
                                  sizeof(malformed_utf8)) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(call_count == before);
    before = call_count;
    assert(astra_window_set_event_mask(
               &window, ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION |
                            ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON |
                            ASTRA_WINDOW_SUBSCRIBE_VBLANK) == ASTRA_OK);
    expect_action(before, ASTRA_GUI_WINDOW_SET_EVENT_MASK);
    assert(last_command.flags ==
           (ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION |
            ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON |
            ASTRA_WINDOW_SUBSCRIBE_VBLANK));
    before = call_count;
    assert(astra_window_set_pointer_shape(
               &window, ASTRA_POINTER_SHAPE_RESIZE_HORIZONTAL) == ASTRA_OK);
    expect_action(before, ASTRA_GUI_WINDOW_SET_POINTER_SHAPE);
    assert(last_command.flags == ASTRA_POINTER_SHAPE_RESIZE_HORIZONTAL);
    before = call_count;
    assert(astra_window_set_pointer_shape(
               &window, ASTRA_POINTER_SHAPE_CUSTOM) == ASTRA_OK);
    expect_action(before, ASTRA_GUI_WINDOW_SET_POINTER_SHAPE);
    before = call_count;
    assert(astra_window_set_pointer_image(&window, &pointer_image) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(call_count == before);
    pointer_image.pixels = &pointer_pixel;
    pointer_image.width = 1u;
    pointer_image.height = 1u;
    pointer_image.pitch = sizeof(pointer_pixel);
    pointer_image.hotspot.x = 1;
    assert(astra_window_set_pointer_image(&window, &pointer_image) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(call_count == before);
    pointer_image.hotspot.x = 0;
    pointer_image.reserved[0] = 1u;
    assert(astra_window_set_pointer_image(&window, &pointer_image) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(call_count == before);
    pointer_image.reserved[0] = 0u;
    before = call_count;
    assert(astra_window_set_pointer_image(&window, &pointer_image) ==
           ASTRA_OK);
    assert(call_count == before + 6u);
    assert(last_command.action == ASTRA_GUI_WINDOW_SET_POINTER_IMAGE &&
           last_command.x == 0u && last_command.y == 0u &&
           last_command.width == 1u && last_command.height == 1u &&
           last_command.flags == ASTRA_HARDWARE_POINTER_WIDTH *
                                     sizeof(AstraColorRGBA8));
    assert(pointer_area[0] == 255u && pointer_area[1] == 255u &&
           pointer_area[2] == 255u && pointer_area[3] == 255u &&
           pointer_area[4] == 0u);
    before = call_count;
    assert(astra_window_present(&window) == ASTRA_OK);
    expect_action(before, ASTRA_GUI_WINDOW_PRESENT);
    assert(last_command.flags == 0u && last_command.width == 0u);
    before = call_count;
    assert(astra_window_present_discard(&window) == ASTRA_OK);
    expect_action(before, ASTRA_GUI_WINDOW_PRESENT);
    assert(last_command.flags == ASTRA_GUI_PRESENT_DISCARD &&
           last_command.width == 0u && last_command.height == 0u);
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
        assert(event.data.pointer.modifiers ==
               (ASTRA_INPUT_MOD_LEFT_SHIFT | ASTRA_INPUT_MOD_META));
        assert(window._private_generation == generation);

        next_event_type = ASTRA_WINDOW_EVENT_TEXT;
        next_text_codepoint = 0xd800u;
        before = call_count;
        assert(astra_window_event_try(&window, &event) == ASTRA_ERROR_IO);
        assert(call_count == before + 1u);
        next_text_codepoint = 0x1f600u;
        assert(astra_window_event_try(&window, &event) == ASTRA_OK);
        assert(event.type == ASTRA_WINDOW_EVENT_TEXT &&
               event.data.text.codepoint == 0x1f600u);
        next_event_type = ASTRA_WINDOW_EVENT_STATE;
        assert(astra_window_event_try(&window, &event) == ASTRA_OK);
        assert(event.data.state.state == ASTRA_WINDOW_STATE_NORMAL &&
               (event.data.state.flags & ASTRA_WINDOW_ACTIVE) != 0u);
        next_state = ASTRA_WINDOW_STATE_FULLSCREEN;
        assert(astra_window_event_try(&window, &event) == ASTRA_OK &&
               event.data.state.state == ASTRA_WINDOW_STATE_FULLSCREEN);
        next_state = ASTRA_WINDOW_STATE_FULLSCREEN + 1u;
        assert(astra_window_event_try(&window, &event) == ASTRA_ERROR_IO);
        next_state = ASTRA_WINDOW_STATE_NORMAL;
        next_event_type = ASTRA_WINDOW_EVENT_RESIZE;
        assert(astra_window_event_try(&window, &event) == ASTRA_OK);
        assert(event.data.resize.width == 500u &&
               event.data.resize.height == 240u);
        next_resize_width = 0u;
        assert(astra_window_event_try(&window, &event) == ASTRA_ERROR_IO);
        next_resize_width = 500u;
        next_event_type = ASTRA_WINDOW_EVENT_SYSTEM_ACTION;
        for (uint32_t action = ASTRA_SYSTEM_ACTION_ABOUT;
             action <= ASTRA_SYSTEM_ACTION_RESTART; ++action) {
            next_system_action = action;
            assert(astra_window_event_try(&window, &event) == ASTRA_OK);
            assert(event.data.system_action.action == action);
        }
        next_system_action = 0u;
        assert(astra_window_event_try(&window, &event) == ASTRA_ERROR_IO);
        next_system_action = ASTRA_SYSTEM_ACTION_RESTART + 1u;
        assert(astra_window_event_try(&window, &event) == ASTRA_ERROR_IO);
        next_event_type = ASTRA_WINDOW_EVENT_POINTER_MOTION;
    }
    before = call_count;
    {
        uint32_t before_ports = port_sequence;

        assert(astra_window_close(&window) == ASTRA_OK);
        assert(call_count == before + 4u);
        assert(port_sequence == before_ports);
    }
    assert(last_command.action == ASTRA_GUI_WINDOW_CLOSE);
    assert(window._private_control == 0u && window._private_events == 0u &&
           window._private_id == 0u && window._private_vblank == 0u);
    assert(astra_window_event_wait_handle(&window) == ASTRA_INVALID_HANDLE);
    assert(astra_window_vblank_wait_handle(&window) == ASTRA_INVALID_HANDLE);

    window = (AstraWindow)ASTRA_WINDOW_INIT;
    create.title_icon_area = 9u;
    create.title_icon_length = 8193u;
    expected_icon_area = 9u;
    expected_icon_length = create.title_icon_length;
    assert(astra_window_create(1u, 2u, &create, &window) == ASTRA_OK);
    assert(astra_window_close(&window) == ASTRA_OK);
    create.title_icon_length = ASTRA_WINDOW_TITLE_ICON_BYTES_MAX + 1u;
    before = call_count;
    assert(astra_window_create(1u, 2u, &create, &window) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(call_count == before);
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
    window = (AstraWindow)ASTRA_WINDOW_INIT;
    create.content_format = ASTRA_WINDOW_CONTENT_SURFACE;
    expected_content_rights |= ASTRA_RIGHT_WRITE;
    assert(astra_window_create(1, 2, &create, &window) == ASTRA_OK);
    expected_content_rights &= ~(uint32_t)ASTRA_RIGHT_WRITE;
    assert(astra_window_close(&window) == ASTRA_OK);
    window = (AstraWindow)ASTRA_WINDOW_INIT;
    create.pitch = 640u;
    before = call_count;
    assert(astra_window_create(1, 2, &create, &window) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(call_count == before);
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

    create.gadgets = ASTRA_WINDOW_GADGET_AUTO |
                     ASTRA_WINDOW_GADGET_MINIMIZE;
    before = call_count;
    assert(astra_window_create(1, 2, &create, &window) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(call_count == before);
    create.gadgets = ASTRA_WINDOW_GADGET_AUTO;

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
    create.title = malformed_utf8;
    create.title_length = sizeof(malformed_utf8);
    assert(astra_window_create(1, 2, &create, &window) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    create.title = title;
    create.title_length = sizeof(title) - 1u;
    next_open_status = ASTRA_STATUS_LIMIT;
    assert(astra_window_create(1, 2, &create, &window) ==
           ASTRA_ERROR_NO_RESOURCES);
    assert(window._private_control == 0u && window._private_id == 0u &&
           window._private_generation == 0u);
    assert(munmap(pointer_area, 4096u) == 0);
    puts("window contract tests passed");
    return 0;
}
