#include <astra/window.h>

#include <astra/gui.h>
#include <astra/runtime.h>

uint32_t astra_window_open(uint32_t gui, const AstraSharedSurface *surface,
                           uint16_t x, uint16_t y, AstraWindow *window)
{
    AstraGuiOpenWindow request = {0};
    AstraGuiWindowOpened reply;
    uint32_t handles[2] = {0u, 0u};
    uint32_t receive = 0u;
    uint32_t size = 0u;
    uint32_t status;

    if (window != NULL) {
        window->id = 0u;
        window->generation = 0u;
    }
    if (gui == 0u || surface == NULL || surface->area == 0u ||
        surface->mapping == NULL || window == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    status = astra_handle_duplicate(
        surface->area,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER,
        &handles[0]);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    status = astra_port_create(1u, sizeof(reply), &receive, &handles[1]);
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_close(handles[0]);
        return status;
    }
    request.header.total_size = sizeof(request);
    request.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    request.header.protocol = ASTRA_GUI_PROTOCOL;
    request.header.protocol_version = ASTRA_GUI_VERSION;
    request.header.operation = ASTRA_GUI_OPEN_WINDOW;
    request.header.transaction_id = 1u;
    request.x = x;
    request.y = y;
    request.width = surface->view.width;
    request.height = surface->view.height;
    request.pitch = surface->view.pitch;
    status = astra_port_send(gui, &request, sizeof(request), handles, 2u);
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_close(handles[0]);
        (void)astra_close(handles[1]);
        (void)astra_close(receive);
        return status;
    }
    status = astra_wait_one(receive, ASTRA_DEADLINE_FOREVER, NULL);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_port_receive(receive, &reply, sizeof(reply), NULL, 0u,
                                    &size, NULL);
    (void)astra_close(receive);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    if (size != sizeof(reply) || reply.header.total_size != sizeof(reply) ||
        reply.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        reply.header.flags != 0u || reply.header.protocol != ASTRA_GUI_PROTOCOL ||
        reply.header.protocol_version != ASTRA_GUI_VERSION ||
        reply.header.reserved != 0u ||
        reply.header.operation != ASTRA_GUI_WINDOW_OPENED ||
        reply.header.transaction_id != request.header.transaction_id)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    if (reply.status != ASTRA_SYSCALL_OK)
        return reply.status;
    if (reply.window == 0u || reply.generation == 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    window->id = reply.window;
    window->generation = reply.generation;
    return ASTRA_SYSCALL_OK;
}
