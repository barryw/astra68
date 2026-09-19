#include "entropy_service.h"

#include <astra/compiler.h>
#include <astra/host.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>

#include <stdint.h>

ASTRA_PROGRAM("entropy", 1, 0, 0, "Astra68 contributors",
              "Copyright 2026 Astra68 contributors");

static uint32_t host_fill(AstraHostChannelClient *host, uint32_t length)
{
    AstraHostCommand *command = astra_host_client_prepare(
        host, ASTRA_HOST_SERVICE_ENTROPY, ASTRA_HOST_ENTROPY_FILL);
    uint32_t status;

    if (command == NULL)
        return ASTRA_STATUS_IO;
    command->data_capacity = length;
    status = astra_host_client_submit(host);
    if (status != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_PEER_DEAD;
    if (command->status != ASTRA_STATUS_OK ||
        command->result_length != length)
        return command->status == ASTRA_STATUS_OK ? ASTRA_STATUS_PROTOCOL :
                                                    command->status;
    return ASTRA_STATUS_OK;
}

static void serve_one(uint32_t receive, AstraHostChannelClient *host)
{
    AstraEntropyRequest request = {0};
    AstraEntropyReply reply;
    uint32_t handles[ASTRA_MESSAGE_HANDLES_MAX] = {0};
    uint32_t size = 0u;
    uint32_t count = 0u;
    uint32_t status = astra_port_receive(
        receive, &request, sizeof(request), handles,
        ASTRA_MESSAGE_HANDLES_MAX, &size, &count);
    uint32_t reply_handle;

    if (status != ASTRA_SYSCALL_OK)
        return;
    if (count == 0u)
        return;
    reply_handle = handles[count - 1u];
    handles[count - 1u] = 0u;
    status = astra_entropy_request_valid(&request, size, count) ?
             host_fill(host, request.length) :
             ASTRA_STATUS_PROTOCOL;
    astra_entropy_reply_init(&reply, &request, status,
                             (const void *)(uintptr_t)host->data,
                             status == ASTRA_STATUS_OK ? request.length : 0u);
    (void)astra_port_send(reply_handle, &reply, reply.header.total_size,
                          NULL, 0u);
    (void)astra_close(reply_handle);
    for (uint32_t index = 0u; index < count; ++index)
        if (handles[index] != 0u)
            (void)astra_close(handles[index]);
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *device;
    const AstraStartupCapability *bootstrap;
    AstraHostChannelClient host = {0};
    uint32_t receive = 0u;
    uint32_t send = 0u;
    uint32_t status;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    device = astra_startup_capability(startup,
                                      ASTRA_CAPABILITY_HOST_DEVICE);
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    if (device == NULL || bootstrap == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    status = astra_host_client_open(device->handle, ASTRA_HOST_CAP_ENTROPY,
                                    ASTRA_ENTROPY_MAX, &host);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_rt_port_create(
            ASTRA_PORT_MESSAGES_MAX,
            ASTRA_PORT_MESSAGES_MAX * (uint32_t)sizeof(AstraEntropyRequest),
            &receive, &send);
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_service_ready(bootstrap->handle, ASTRA_STATUS_IO,
                                  NULL, 0u);
        (void)astra_host_client_close(&host);
        return ASTRA_STATUS_IO;
    }
    status = astra_service_ready(bootstrap->handle, ASTRA_STATUS_OK,
                                 &send, 1u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_host_client_close(&host);
        return ASTRA_STATUS_PEER_DEAD;
    }
    for (;;) {
        status = astra_wait_one_restart(receive, ASTRA_DEADLINE_FOREVER,
                                        NULL);
        if (status != ASTRA_SYSCALL_OK)
            return ASTRA_STATUS_PEER_DEAD;
        do {
            uint32_t before = host.producer;

            serve_one(receive, &host);
            if (host.producer == before)
                break;
        } while (1);
    }
}
