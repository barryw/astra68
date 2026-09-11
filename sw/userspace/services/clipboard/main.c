#include <astra/area.h>
#include <astra/bytes.h>
#include <astra/clipboard.h>
#include <astra/clipboard_service.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>

ASTRA_PROGRAM("clipboard", 0, 1, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

typedef struct ClipboardState {
    uint32_t document;
    uint32_t document_size;
    uint32_t generation;
} ClipboardState;

static uint32_t next_generation(uint32_t generation)
{
    ++generation;
    return generation == 0u ? 1u : generation;
}

static int request_valid(const AstraClipboardRequest *request,
                         uint32_t size)
{
    return size == sizeof(*request) &&
           request->header.total_size == sizeof(*request) &&
           request->header.header_size == ASTRA_MESSAGE_HEADER_SIZE &&
           request->header.flags == 0u &&
           request->header.protocol == ASTRA_CLIPBOARD_PROTOCOL &&
           request->header.protocol_version ==
               ASTRA_CLIPBOARD_PROTOCOL_VERSION &&
           request->header.operation >= ASTRA_CLIPBOARD_OPERATION_SET &&
           request->header.operation <= ASTRA_CLIPBOARD_OPERATION_CLEAR &&
           request->header.transaction_id != 0u &&
           request->header.reserved == 0u &&
           astra_words_zero(request->reserved, 3u);
}

static uint32_t validate_document(uint32_t handle, uint32_t document_size)
{
    void *address = NULL;
    uint32_t mapped_size = 0u;
    uint32_t status;

    if (document_size == 0u || document_size > ASTRA_AREA_SIZE_MAX)
        return ASTRA_STATUS_PROTOCOL;
    status = astra_rt_area_map(handle, ASTRA_AREA_MAP_READ,
                               &address, &mapped_size);
    if (status != ASTRA_SYSCALL_OK)
        return status == ASTRA_SYSCALL_ACCESS_DENIED ? ASTRA_STATUS_ACCESS :
                                                       ASTRA_STATUS_BAD_HANDLE;
    status = mapped_size >= document_size &&
             astra_clipboard_document_validate(address, document_size) ==
                 ASTRA_OK ? ASTRA_STATUS_OK : ASTRA_STATUS_PROTOCOL;
    if (astra_rt_area_unmap(address) != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_IO;
    return status;
}

static void send_reply(uint32_t reply_handle,
                       const AstraClipboardRequest *request,
                       uint32_t status, const ClipboardState *state,
                       uint32_t transfer)
{
    AstraClipboardReply reply = {0};
    const uint32_t *handles = transfer == 0u ? NULL : &transfer;

    astra_message_header_set(&reply.header, sizeof(reply),
                             ASTRA_CLIPBOARD_PROTOCOL,
                             ASTRA_CLIPBOARD_PROTOCOL_VERSION,
                             ASTRA_CLIPBOARD_OPERATION_REPLY,
                             request->header.transaction_id);
    reply.status = status;
    reply.generation = state->generation;
    if (status == ASTRA_STATUS_OK &&
        request->header.operation == ASTRA_CLIPBOARD_OPERATION_GET)
        reply.document_size = state->document_size;
    if (astra_port_send(reply_handle, &reply, sizeof(reply), handles,
                        transfer == 0u ? 0u : 1u) != ASTRA_SYSCALL_OK &&
        transfer != 0u)
        (void)astra_close(transfer);
}

static void serve_request(uint32_t receive, ClipboardState *state)
{
    AstraClipboardRequest request = {0};
    uint32_t handles[ASTRA_MESSAGE_HANDLES_MAX] = {0};
    uint32_t size = 0u;
    uint32_t count = 0u;
    uint32_t reply_handle;
    uint32_t transfer = 0u;
    uint32_t status = astra_port_receive(
        receive, &request, sizeof(request), handles,
        ASTRA_MESSAGE_HANDLES_MAX, &size, &count);

    if (status != ASTRA_SYSCALL_OK)
        return;
    if (count == 0u) {
        return;
    }
    reply_handle = handles[count - 1u];
    handles[count - 1u] = 0u;
    status = request_valid(&request, size) ? ASTRA_STATUS_OK :
                                             ASTRA_STATUS_PROTOCOL;
    if (status == ASTRA_STATUS_OK) {
        uint32_t expected = request.header.operation ==
                                ASTRA_CLIPBOARD_OPERATION_SET ? 2u : 1u;

        if (count != expected ||
            (request.header.operation != ASTRA_CLIPBOARD_OPERATION_SET &&
             request.document_size != 0u))
            status = ASTRA_STATUS_PROTOCOL;
    }
    if (status == ASTRA_STATUS_OK &&
        request.header.operation == ASTRA_CLIPBOARD_OPERATION_SET) {
        status = validate_document(handles[0], request.document_size);
        if (status == ASTRA_STATUS_OK) {
            uint32_t previous = state->document;

            if (previous != 0u &&
                astra_close(previous) != ASTRA_SYSCALL_OK) {
                status = ASTRA_STATUS_IO;
            } else {
                state->document = handles[0];
                state->document_size = request.document_size;
                state->generation = next_generation(state->generation);
                handles[0] = 0u;
            }
        }
    } else if (status == ASTRA_STATUS_OK &&
               request.header.operation == ASTRA_CLIPBOARD_OPERATION_GET) {
        if (state->document == 0u) {
            status = ASTRA_STATUS_NOT_FOUND;
        } else {
            uint32_t duplicate_status = astra_rt_handle_duplicate(
                state->document,
                ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER,
                &transfer);

            if (duplicate_status != ASTRA_SYSCALL_OK)
                status = duplicate_status == ASTRA_SYSCALL_RESOURCE_LIMIT ?
                         ASTRA_STATUS_LIMIT : ASTRA_STATUS_IO;
        }
    } else if (status == ASTRA_STATUS_OK) {
        if (state->document != 0u &&
            astra_close(state->document) != ASTRA_SYSCALL_OK) {
            status = ASTRA_STATUS_IO;
        } else {
            state->document = 0u;
            state->document_size = 0u;
            state->generation = next_generation(state->generation);
        }
    }
    send_reply(reply_handle, &request, status, state, transfer);
    (void)astra_close(reply_handle);
    for (uint32_t index = 0u; index < count; ++index)
        if (handles[index] != 0u)
            (void)astra_close(handles[index]);
}

int astra_main(const AstraStartupInfo *startup)
{
    ClipboardState state = {0};
    const AstraStartupCapability *bootstrap;
    uint32_t receive = 0u;
    uint32_t send = 0u;
    uint32_t published;
    uint32_t status;

    if (!astra_startup_validate(startup) ||
        startup->capabilities_address == 0u)
        return ASTRA_STATUS_INVALID;
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    if (bootstrap == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    status = astra_rt_port_create(16u, 16u * sizeof(AstraClipboardRequest),
                                  &receive, &send);
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_service_ready(bootstrap->handle, ASTRA_STATUS_LIMIT,
                                  NULL, 0u);
        return ASTRA_STATUS_LIMIT;
    }
    published = send;
    status = astra_service_ready(bootstrap->handle, ASTRA_STATUS_OK,
                                 &published, 1u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_PEER_DEAD;
    for (;;) {
        status = astra_wait_one(receive, ASTRA_DEADLINE_FOREVER, NULL);
        if (status != ASTRA_SYSCALL_OK)
            return ASTRA_STATUS_PEER_DEAD;
        serve_request(receive, &state);
    }
}
