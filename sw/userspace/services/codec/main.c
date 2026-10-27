#include <astra/area.h>
#include <astra/codec_service.h>
#include <astra/host.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>

#include <stdint.h>
#include <string.h>

ASTRA_PROGRAM("codec", 0, 1, 0, "Astra68 contributors", "Astra native");

typedef struct CodecSession {
    struct CodecSession *next;
    uint32_t receive;
    uint32_t reply;
    uint32_t area;
    uint32_t host_handle;
    void *bytes;
} CodecSession;

static AstraHostChannelClient host;
static CodecSession *sessions;
static uint32_t count;

static uint32_t exchange(CodecSession *session, uint32_t operation,
                         uint64_t offset, uint32_t length,
                         uint32_t capacity, AstraCodecWorkerMessage *event)
{
    AstraHostCommand *command = astra_host_client_prepare(
        &host, ASTRA_HOST_SERVICE_CODEC, (uint16_t)operation);

    if (command == NULL)
        return ASTRA_STATUS_PEER_DEAD;
    command->handle = session->host_handle;
    command->offset_hi = (uint32_t)(offset >> 32);
    command->offset_lo = (uint32_t)offset;
    command->data_length = length;
    command->data_capacity = capacity == 0u ? 0u :
                             capacity + ASTRA_HOST_CODEC_RESULT_BYTES;
    if (operation == ASTRA_HOST_CODEC_SOURCE && length != 0u)
        memcpy((void *)(uintptr_t)(host.data + ASTRA_HOST_CODEC_RESULT_BYTES),
               session->bytes, length);
    if (astra_host_client_submit(&host) != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_PEER_DEAD;
    if (command->status != ASTRA_STATUS_OK)
        return command->status;
    if (operation == ASTRA_HOST_CODEC_CLOSE) {
        session->host_handle = 0u;
        return ASTRA_STATUS_OK;
    }
    if (command->result_length < ASTRA_HOST_CODEC_RESULT_BYTES ||
        command->result_length > command->data_capacity)
        return ASTRA_STATUS_PROTOCOL;
    memcpy(event, (const void *)(uintptr_t)host.data, sizeof(*event));
    if (event->magic != ASTRA_CODEC_WORKER_MAGIC ||
        event->version != ASTRA_CODEC_WORKER_VERSION ||
        event->length != command->result_length - ASTRA_HOST_CODEC_RESULT_BYTES ||
        event->length > ASTRA_CODEC_TRANSFER_BYTES)
        return ASTRA_STATUS_PROTOCOL;
    if (operation == ASTRA_HOST_CODEC_OPEN) {
        if (command->result_value == 0u)
            return ASTRA_STATUS_PROTOCOL;
        session->host_handle = command->result_value;
    } else if (command->result_value != session->host_handle) {
        return ASTRA_STATUS_PROTOCOL;
    }
    if (event->kind == ASTRA_CODEC_WORKER_CHUNK && event->length != 0u)
        memcpy(session->bytes,
               (const void *)(uintptr_t)(host.data +
                   ASTRA_HOST_CODEC_RESULT_BYTES), event->length);
    return ASTRA_STATUS_OK;
}

static void release(CodecSession *session)
{
    CodecSession **link = &sessions;

    while (*link != NULL && *link != session)
        link = &(*link)->next;
    if (*link == session) {
        *link = session->next;
        --count;
    }
    if (session->host_handle != 0u) {
        AstraCodecWorkerMessage ignored;
        (void)exchange(session, ASTRA_HOST_CODEC_CLOSE, 0u, 0u, 0u, &ignored);
    }
    if (session->bytes != NULL)
        (void)astra_rt_area_unmap(session->bytes);
    if (session->area != 0u)
        (void)astra_close(session->area);
    if (session->reply != 0u)
        (void)astra_close(session->reply);
    if (session->receive != 0u)
        (void)astra_close(session->receive);
    astra_runtime_deallocate(session);
}

static void respond(uint32_t port, const AstraCodecServiceRequest *request,
                    uint32_t status, const AstraCodecWorkerMessage *event,
                    uint32_t send)
{
    AstraCodecServiceReply reply = {0};

    astra_message_header_set(&reply.header, sizeof(reply),
        ASTRA_CODEC_PROTOCOL, ASTRA_CODEC_PROTOCOL_VERSION,
        ASTRA_CODEC_SERVICE_REPLY, request->header.transaction_id);
    reply.status = status;
    if (status == ASTRA_STATUS_OK && event != NULL)
        reply.event = *event;
    (void)astra_port_send(port, &reply, sizeof(reply),
                          send == 0u ? NULL : &send,
                          send == 0u ? 0u : 1u);
}

static int valid(const AstraCodecServiceRequest *request, uint32_t bytes,
                 uint32_t operation)
{
    return bytes == sizeof(*request) &&
           request->header.total_size == sizeof(*request) &&
           request->header.header_size == ASTRA_MESSAGE_HEADER_SIZE &&
           request->header.flags == 0u &&
           request->header.reserved == 0u &&
           request->header.protocol == ASTRA_CODEC_PROTOCOL &&
           request->header.protocol_version == ASTRA_CODEC_PROTOCOL_VERSION &&
           request->header.operation == operation &&
           request->header.transaction_id != 0u;
}

static void serve_open(uint32_t factory)
{
    AstraCodecServiceRequest request = {0};
    AstraCodecWorkerMessage event = {0};
    uint32_t handles[2] = {0};
    uint32_t bytes = 0u, received = 0u, mapped = 0u, send = 0u;
    uint32_t status;
    CodecSession *session = NULL;

    if (astra_port_receive(factory, &request, sizeof(request), handles, 2u,
                           &bytes, &received) != ASTRA_SYSCALL_OK)
        return;
    status = received == 2u && valid(&request, bytes,
                ASTRA_CODEC_SERVICE_OPEN) && request.bytes == 0u &&
             count < ASTRA_WAIT_MULTIPLE_MAX - 1u ?
             ASTRA_STATUS_OK : ASTRA_STATUS_PROTOCOL;
    if (status == ASTRA_STATUS_OK) {
        session = astra_runtime_callocate(1u, sizeof(*session));
        if (session == NULL)
            status = ASTRA_STATUS_NO_SPACE;
    }
    if (status == ASTRA_STATUS_OK) {
        session->area = handles[0]; handles[0] = 0u;
        session->reply = handles[1]; handles[1] = 0u;
        status = astra_rt_area_map(session->area,
            ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
            &session->bytes, &mapped);
        status = status == ASTRA_SYSCALL_OK &&
                 mapped >= ASTRA_CODEC_TRANSFER_BYTES ?
                 ASTRA_STATUS_OK : ASTRA_STATUS_BAD_HANDLE;
    }
    if (status == ASTRA_STATUS_OK) {
        status = astra_rt_port_create(1u, sizeof(request),
                                      &session->receive, &send);
        status = status == ASTRA_SYSCALL_OK ?
                 ASTRA_STATUS_OK : ASTRA_STATUS_NO_SPACE;
    }
    if (status == ASTRA_STATUS_OK)
        status = exchange(session, ASTRA_HOST_CODEC_OPEN,
            ((uint64_t)request.offset_hi << 32) | request.offset_lo,
            0u, ASTRA_CODEC_TRANSFER_BYTES, &event);
    if (session != NULL && session->reply != 0u)
        respond(session->reply, &request, status, &event,
                status == ASTRA_STATUS_OK ? send : 0u);
    else if (handles[1] != 0u)
        respond(handles[1], &request, status, &event, 0u);
    if (status == ASTRA_STATUS_OK) {
        session->next = sessions;
        sessions = session;
        ++count;
        session = NULL;
        send = 0u;
    }
    if (send != 0u)
        (void)astra_close(send);
    if (session != NULL)
        release(session);
    for (uint32_t at = 0; at < received; ++at)
        if (handles[at] != 0u) (void)astra_close(handles[at]);
}

static void serve_session(CodecSession *session)
{
    AstraCodecServiceRequest request = {0};
    AstraCodecWorkerMessage event = {0};
    uint32_t bytes = 0u, received = 0u;
    uint32_t operation, status;
    uint64_t offset;

    if (astra_port_receive(session->receive, &request, sizeof(request),
                           NULL, 0u, &bytes, &received) != ASTRA_SYSCALL_OK)
        return;
    operation = request.header.operation;
    status = valid(&request, bytes, operation) && received == 0u &&
             operation >= ASTRA_CODEC_SERVICE_SOURCE &&
             operation <= ASTRA_CODEC_SERVICE_CLOSE &&
             request.bytes <= ASTRA_CODEC_TRANSFER_BYTES ?
             ASTRA_STATUS_OK : ASTRA_STATUS_PROTOCOL;
    offset = ((uint64_t)request.offset_hi << 32) | request.offset_lo;
    if (status == ASTRA_STATUS_OK &&
        (operation == ASTRA_CODEC_SERVICE_SOURCE ||
         operation == ASTRA_CODEC_SERVICE_READ)) {
        if (request.bytes == 0u && operation == ASTRA_CODEC_SERVICE_READ)
            status = ASTRA_STATUS_INVALID;
    } else if (status == ASTRA_STATUS_OK && request.bytes != 0u) {
        status = ASTRA_STATUS_INVALID;
    }
    if (status == ASTRA_STATUS_OK)
        status = exchange(session,
            operation == ASTRA_CODEC_SERVICE_SOURCE ? ASTRA_HOST_CODEC_SOURCE :
            operation == ASTRA_CODEC_SERVICE_READ ? ASTRA_HOST_CODEC_READ :
            operation == ASTRA_CODEC_SERVICE_SEEK ? ASTRA_HOST_CODEC_SEEK :
            ASTRA_HOST_CODEC_CLOSE,
            offset, operation == ASTRA_CODEC_SERVICE_SOURCE ?
                        request.bytes : 0u,
            operation == ASTRA_CODEC_SERVICE_CLOSE ? 0u :
            operation == ASTRA_CODEC_SERVICE_READ ? request.bytes :
            ASTRA_CODEC_TRANSFER_BYTES, &event);
    respond(session->reply, &request, status, &event, 0u);
    if (operation == ASTRA_CODEC_SERVICE_CLOSE ||
        status == ASTRA_STATUS_PEER_DEAD)
        release(session);
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *bootstrap, *device;
    uint32_t factory = 0u, publication = 0u;
    uint32_t status;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    device = astra_startup_capability(startup, ASTRA_CAPABILITY_HOST_DEVICE);
    if (bootstrap == NULL || device == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    status = astra_host_client_open(device->handle, ASTRA_HOST_CAP_CODEC,
        ASTRA_CODEC_TRANSFER_BYTES + ASTRA_HOST_CODEC_RESULT_BYTES, &host);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_rt_port_create(1u, sizeof(AstraCodecServiceRequest),
                                      &factory, &publication);
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_service_ready(bootstrap->handle, ASTRA_STATUS_UNSUPPORTED,
                                  NULL, 0u);
        return ASTRA_STATUS_UNSUPPORTED;
    }
    status = astra_service_ready(bootstrap->handle, ASTRA_STATUS_OK,
                                 &publication, 1u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_PEER_DEAD;
    for (;;) {
        uint32_t waits[ASTRA_WAIT_MULTIPLE_MAX];
        CodecSession *selected[ASTRA_WAIT_MULTIPLE_MAX];
        uint32_t used = 1u, index = ASTRA_WAIT_INDEX_NONE;

        waits[0] = factory;
        selected[0] = NULL;
        for (CodecSession *session = sessions; session != NULL;
             session = session->next) {
            waits[used] = session->receive;
            selected[used++] = session;
        }
        status = astra_wait_multiple(waits, used, ASTRA_DEADLINE_FOREVER,
                                     &index, NULL);
        if (index >= used)
            return ASTRA_STATUS_PROTOCOL;
        if (status == ASTRA_SYSCALL_PEER_DEAD ||
            status == ASTRA_SYSCALL_CLOSED) {
            if (index != 0u)
                release(selected[index]);
            continue;
        }
        if (status != ASTRA_SYSCALL_OK)
            return ASTRA_STATUS_IO;
        if (index == 0u) serve_open(factory);
        else serve_session(selected[index]);
    }
}
