#include <astra/window.h>

#include <astra/bytes.h>
#include <astra/gui.h>
#include <astra/port.h>
#include <astra/resource.h>

#include "internal/status.h"

static int state_valid(uint8_t state)
{
    return state <= ASTRA_GADGET_DISABLED;
}

static int window_live(const AstraWindow *window)
{
    return window != 0 &&
           window->_private_control != ASTRA_INVALID_HANDLE &&
           window->_private_events != ASTRA_INVALID_HANDLE &&
           window->_private_id != 0u && window->_private_generation != 0u;
}

static void copy_info(AstraWindowInfo *info,
                      const AstraGuiWindowState *state)
{
    if (info == 0)
        return;
    info->frame.x = state->x;
    info->frame.y = state->y;
    info->frame.width = state->width;
    info->frame.height = state->height;
    info->flags = state->flags;
    info->state = state->state;
    info->z_order = state->z_order;
    info->generation = state->generation;
}

static AstraResult command(AstraWindow *window, uint32_t action,
                           const AstraWindowFrame *frame,
                           const char *title, uint16_t title_length,
                           uint32_t flags,
                           AstraWindowInfo *info)
{
    AstraGuiWindowCommand request = {0};
    AstraGuiWindowState reply = {0};
    AstraPort reply_port = ASTRA_PORT_INIT;
    AstraHandle transferred = ASTRA_INVALID_HANDLE;
    uint32_t reply_size = 0u;
    uint32_t reply_handles = 0u;
    AstraResult result;

    if (!window_live(window) || action < ASTRA_GUI_WINDOW_QUERY ||
        action > ASTRA_GUI_WINDOW_PRESENT ||
        (title_length != 0u && title == 0) ||
        title_length > ASTRA_WINDOW_TITLE_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_port_create(1u, sizeof(reply), &reply_port);
    if (result != ASTRA_OK)
        return result;
    transferred = reply_port.send;
    result = astra_message_header_init(
        &request.header, sizeof(request), ASTRA_GUI_PROTOCOL,
        ASTRA_GUI_VERSION, ASTRA_GUI_WINDOW_COMMAND,
        window->_private_generation);
    if (result != ASTRA_OK)
        goto done;
    request.window = window->_private_id;
    request.generation = window->_private_generation;
    request.action = action;
    request.flags = flags;
    if (frame != 0) {
        request.x = frame->x;
        request.y = frame->y;
        request.width = frame->width;
        request.height = frame->height;
    }
    request.title_length = title_length;
    for (uint32_t index = 0u; index < title_length; ++index)
        request.title[index] = title[index];
    result = astra_port_send_until(
        window->_private_control, &request, sizeof(request), &transferred, 1u,
        ASTRA_DEADLINE_INFINITE);
    reply_port.send = transferred;
    if (result != ASTRA_OK)
        goto done;
    result = astra_port_receive_until(
        reply_port.receive, &reply, sizeof(reply), 0, 0,
        &reply_size, &reply_handles, ASTRA_DEADLINE_INFINITE);
    if (result != ASTRA_OK)
        goto done;
    if (reply_size != sizeof(reply) || reply_handles != 0u ||
        reply.header.total_size != sizeof(reply) ||
        reply.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        reply.header.flags != 0u ||
        reply.header.protocol != ASTRA_GUI_PROTOCOL ||
        reply.header.protocol_version != ASTRA_GUI_VERSION ||
        reply.header.reserved != 0u ||
        reply.header.operation != ASTRA_GUI_WINDOW_STATE ||
        reply.header.transaction_id != request.header.transaction_id ||
        reply.window != window->_private_id ||
        reply.generation == 0u || !astra_words_zero(reply.reserved, 2u)) {
        result = ASTRA_ERROR_IO;
        goto done;
    }
    result = astra_internal_service_result(reply.status);
    if (result == ASTRA_OK) {
        window->_private_generation = reply.generation;
        copy_info(info, &reply);
    }

done:
    {
        AstraResult close_result = astra_port_close(&reply_port);

        if (result == ASTRA_OK && close_result != ASTRA_OK)
            result = close_result;
    }
    return result;
}

AstraResult astra_window_create(uint32_t gui_endpoint,
                                uint32_t content_area,
                                const AstraWindowCreateInfo *info,
                                AstraWindow *window)
{
    AstraGuiOpenWindow request = {0};
    AstraGuiWindowOpened reply = {0};
    AstraPort reply_port = ASTRA_PORT_INIT;
    AstraPort event_port = ASTRA_PORT_INIT;
    AstraHandle transferred[4] = { ASTRA_INVALID_HANDLE,
                                   ASTRA_INVALID_HANDLE,
                                   ASTRA_INVALID_HANDLE,
                                   ASTRA_INVALID_HANDLE };
    AstraHandle control = ASTRA_INVALID_HANDLE;
    uint32_t reply_size = 0u;
    uint32_t reply_handles = 0u;
    uint32_t known_flags = ASTRA_WINDOW_RESIZABLE | ASTRA_WINDOW_MODAL |
                           ASTRA_WINDOW_ACTIVE;
    uint32_t known_gadgets = ASTRA_WINDOW_GADGET_CLOSE |
                             ASTRA_WINDOW_GADGET_MINIMIZE |
                             ASTRA_WINDOW_GADGET_MAXIMIZE;
    uint32_t known_events = ASTRA_WINDOW_SUBSCRIBE_ALL;
    AstraResult result;

    if (info == 0 || window == 0 || info->size < sizeof(*info) ||
        gui_endpoint == 0 || content_area == 0 || info->width == 0 ||
        info->height == 0 ||
        ((info->content_format == ASTRA_WINDOW_CONTENT_RGB565 &&
          (info->pitch < (uint32_t)info->width * 2u ||
           (info->pitch & 1u) != 0u)) ||
         (info->content_format == ASTRA_WINDOW_CONTENT_DRAW_LIST &&
          info->pitch != 0u) ||
         (info->content_format != ASTRA_WINDOW_CONTENT_RGB565 &&
          info->content_format != ASTRA_WINDOW_CONTENT_DRAW_LIST)) ||
        info->type < ASTRA_WINDOW_STANDARD ||
        info->type > ASTRA_WINDOW_DESKTOP ||
        (info->flags & ~known_flags) != 0 ||
        (info->gadgets & ~known_gadgets) != 0 ||
        !state_valid(info->close_state) ||
        !state_valid(info->minimize_state) ||
        !state_valid(info->maximize_state) ||
        info->title_length > ASTRA_WINDOW_TITLE_MAX ||
        (info->title_length != 0 && info->title == 0) ||
        (info->event_mask & ~known_events) != 0u ||
        ((info->title_icon_area == ASTRA_INVALID_HANDLE) !=
         (info->title_icon_length == 0u)) ||
        info->title_icon_length > ASTRA_WINDOW_TITLE_ICON_BYTES_MAX ||
        info->reserved != 0u || window_live(window) ||
        window->_private_control != ASTRA_INVALID_HANDLE ||
        window->_private_events != ASTRA_INVALID_HANDLE ||
        window->_private_id != 0u || window->_private_generation != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_handle_duplicate(
        content_area,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER,
        &transferred[0]);
    if (result != ASTRA_OK)
        return result;
    result = astra_port_create(1u, sizeof(reply), &reply_port);
    if (result != ASTRA_OK)
        goto done;
    result = astra_port_create(8u, ASTRA_GUI_WINDOW_EVENT_SIZE * 8u,
                               &event_port);
    if (result != ASTRA_OK)
        goto done;
    transferred[1] = event_port.send;
    transferred[2] = reply_port.send;
    result = astra_message_header_init(
        &request.header, sizeof(request), ASTRA_GUI_PROTOCOL,
        ASTRA_GUI_VERSION, ASTRA_GUI_OPEN_WINDOW, 1u);
    if (result != ASTRA_OK)
        goto done;
    request.x = info->x;
    request.y = info->y;
    request.width = info->width;
    request.height = info->height;
    request.pitch = info->pitch;
    request.flags = info->flags;
    request.gadgets = info->gadgets;
    request.type = info->type;
    request.close_state = info->close_state;
    request.minimize_state = info->minimize_state;
    request.maximize_state = info->maximize_state;
    request.title_length = info->title_length;
    request.content_format = info->content_format;
    request.event_mask = info->event_mask;
    request.title_icon_length = info->title_icon_length;
    for (uint32_t index = 0u; index < info->title_length; ++index)
        request.title[index] = info->title[index];
    if (info->title_icon_length != 0u) {
        result = astra_handle_duplicate(
            info->title_icon_area,
            ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER,
            &transferred[3]);
        if (result != ASTRA_OK)
            goto done;
    }
    result = astra_port_send_until(gui_endpoint, &request, sizeof(request),
                                   transferred,
                                   info->title_icon_length != 0u ? 4u : 3u,
                                   ASTRA_DEADLINE_INFINITE);
    event_port.send = transferred[1];
    reply_port.send = transferred[2];
    if (result != ASTRA_OK)
        goto done;
    result = astra_port_receive_until(
        reply_port.receive, &reply, sizeof(reply), &control, 1u,
        &reply_size, &reply_handles, ASTRA_DEADLINE_INFINITE);
    if (result != ASTRA_OK)
        goto done;
    if (reply_size != sizeof(reply)) {
        result = ASTRA_ERROR_IO;
        goto done;
    }
    if (reply.header.total_size != sizeof(reply) ||
        reply.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        reply.header.flags != 0u ||
        reply.header.protocol != ASTRA_GUI_PROTOCOL ||
        reply.header.protocol_version != ASTRA_GUI_VERSION ||
        reply.header.reserved != 0u ||
        reply.header.operation != ASTRA_GUI_WINDOW_OPENED ||
        reply.header.transaction_id != request.header.transaction_id) {
        result = ASTRA_ERROR_IO;
        goto done;
    }
    result = astra_internal_service_result(reply.status);
    if (result == ASTRA_OK) {
        if (reply_handles != 1u || reply.window == 0u ||
            reply.generation == 0u || control == ASTRA_INVALID_HANDLE) {
            result = ASTRA_ERROR_IO;
        } else {
            window->_private_control = control;
            window->_private_events = event_port.receive;
            window->_private_id = reply.window;
            window->_private_generation = reply.generation;
            control = ASTRA_INVALID_HANDLE;
            event_port.receive = ASTRA_INVALID_HANDLE;
        }
    } else if (reply_handles != 0u || control != ASTRA_INVALID_HANDLE ||
               reply.window != 0u || reply.generation != 0u) {
        result = ASTRA_ERROR_IO;
    }

done:
    for (uint32_t index = 0u; index < 4u; ++index)
        if (transferred[index] != ASTRA_INVALID_HANDLE) {
            AstraResult ignored = astra_handle_close(&transferred[index]);
            (void)ignored;
        }
    if (control != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_handle_close(&control);
        (void)ignored;
    }
    if (event_port.send != ASTRA_INVALID_HANDLE ||
        event_port.receive != ASTRA_INVALID_HANDLE) {
        AstraResult close_result = astra_port_close(&event_port);

        if (result == ASTRA_OK && close_result != ASTRA_OK)
            result = close_result;
    }
    {
        AstraResult close_result = astra_port_close(&reply_port);

        if (result == ASTRA_OK && close_result != ASTRA_OK)
            result = close_result;
    }
    return result;
}

AstraResult astra_window_get_info(AstraWindow *window, AstraWindowInfo *info)
{
    if (info == 0 || info->size < sizeof(*info) ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return command(window, ASTRA_GUI_WINDOW_QUERY, 0, 0, 0u, 0u, info);
}

AstraResult astra_window_set_frame(AstraWindow *window,
                                   const AstraWindowFrame *frame)
{
    if (frame == 0 || frame->width == 0u || frame->height == 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return command(window, ASTRA_GUI_WINDOW_SET_FRAME, frame, 0, 0u, 0u, 0);
}

AstraResult astra_window_move(AstraWindow *window, uint16_t x, uint16_t y)
{
    AstraWindowFrame frame = { x, y, 0u, 0u };
    return command(window, ASTRA_GUI_WINDOW_MOVE, &frame, 0, 0u, 0u, 0);
}

AstraResult astra_window_resize(AstraWindow *window,
                                uint16_t width, uint16_t height)
{
    AstraWindowFrame frame = { 0u, 0u, width, height };

    if (width == 0u || height == 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return command(window, ASTRA_GUI_WINDOW_RESIZE, &frame, 0, 0u, 0u, 0);
}

#define WINDOW_ACTION(name, action) \
    AstraResult name(AstraWindow *window) \
    { \
        return command(window, action, 0, 0, 0u, 0u, 0); \
    }

WINDOW_ACTION(astra_window_raise, ASTRA_GUI_WINDOW_RAISE)
WINDOW_ACTION(astra_window_lower, ASTRA_GUI_WINDOW_LOWER)
WINDOW_ACTION(astra_window_activate, ASTRA_GUI_WINDOW_ACTIVATE)
WINDOW_ACTION(astra_window_deactivate, ASTRA_GUI_WINDOW_DEACTIVATE)
WINDOW_ACTION(astra_window_minimize, ASTRA_GUI_WINDOW_MINIMIZE)
WINDOW_ACTION(astra_window_maximize, ASTRA_GUI_WINDOW_MAXIMIZE)
WINDOW_ACTION(astra_window_restore, ASTRA_GUI_WINDOW_RESTORE)
WINDOW_ACTION(astra_window_present, ASTRA_GUI_WINDOW_PRESENT)

AstraResult astra_window_present_region(AstraWindow *window,
                                        const AstraWindowFrame *damage)
{
    if (damage == 0 || damage->width == 0u || damage->height == 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return command(window, ASTRA_GUI_WINDOW_PRESENT, damage, 0, 0u, 0u, 0);
}

AstraResult astra_window_set_title(AstraWindow *window, const char *title,
                                   uint16_t title_length)
{
    return command(window, ASTRA_GUI_WINDOW_SET_TITLE, 0, title,
                   title_length, 0u, 0);
}

AstraResult astra_window_set_event_mask(AstraWindow *window,
                                        uint32_t event_mask)
{
    if ((event_mask & ~ASTRA_WINDOW_SUBSCRIBE_ALL) != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return command(window, ASTRA_GUI_WINDOW_SET_EVENT_MASK, 0, 0, 0u,
                   event_mask, 0);
}

AstraResult astra_window_close(AstraWindow *window)
{
    AstraGuiWindowCommand request = {0};
    AstraResult result;

    if (!window_live(window))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_message_header_init(
        &request.header, sizeof(request), ASTRA_GUI_PROTOCOL,
        ASTRA_GUI_VERSION, ASTRA_GUI_WINDOW_COMMAND,
        window->_private_generation);
    if (result == ASTRA_OK) {
        request.window = window->_private_id;
        request.generation = window->_private_generation;
        request.action = ASTRA_GUI_WINDOW_CLOSE;
        result = astra_port_send_until(
            window->_private_control, &request, sizeof(request), 0, 0u,
            ASTRA_DEADLINE_INFINITE);
    }

    if (result == ASTRA_OK) {
        AstraResult event_result =
            astra_handle_close(&window->_private_events);

        result = astra_handle_close(&window->_private_control);
        if (result == ASTRA_OK)
            result = event_result;
        if (result == ASTRA_OK) {
            window->_private_id = 0u;
            window->_private_generation = 0u;
        }
    }
    return result;
}

static AstraResult receive_event(AstraWindow *window, AstraWindowEvent *event,
                                 AstraMonotonicDeadline deadline_ns)
{
    AstraGuiWindowEvent message = {0};
    uint32_t size = 0u;
    uint32_t handles = 0u;
    AstraResult result;

    if (!window_live(window) || event == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_port_receive_until(
        window->_private_events, &message, sizeof(message), 0, 0,
        &size, &handles, deadline_ns);
    if (result != ASTRA_OK)
        return result;
    if (size != sizeof(message) || handles != 0u ||
        message.header.total_size != sizeof(message) ||
        message.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        message.header.flags != 0u ||
        message.header.protocol != ASTRA_GUI_PROTOCOL ||
        message.header.protocol_version != ASTRA_GUI_VERSION ||
        message.header.reserved != 0u ||
        message.header.operation != ASTRA_GUI_WINDOW_EVENT ||
        message.event.size != sizeof(message.event) ||
        message.event.version != ASTRA_WINDOW_EVENT_VERSION ||
        message.event.type < ASTRA_WINDOW_EVENT_POINTER_MOTION ||
        message.event.type > ASTRA_WINDOW_EVENT_TEXT ||
        message.event.generation == 0u)
        return ASTRA_ERROR_IO;
    if (message.event.generation > window->_private_generation)
        window->_private_generation = message.event.generation;
    *event = message.event;
    return ASTRA_OK;
}

AstraResult astra_window_event_try(AstraWindow *window,
                                   AstraWindowEvent *event)
{
    return receive_event(window, event, ASTRA_DEADLINE_POLL);
}

AstraResult astra_window_event_wait(AstraWindow *window,
                                    AstraWindowEvent *event,
                                    AstraMonotonicDeadline deadline_ns)
{
    return receive_event(window, event, deadline_ns);
}

AstraHandle astra_window_event_wait_handle(const AstraWindow *window)
{
    return window_live(window) ? window->_private_events :
                                 ASTRA_INVALID_HANDLE;
}
