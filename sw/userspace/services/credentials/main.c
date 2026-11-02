#include <astra/config_library.h>
#include <astra/credential_service.h>
#include <astra/port.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/secret_memory.h>
#include <astra/service.h>
#include <astra/status.h>

#include "policy.h"

#include <string.h>

ASTRA_PROGRAM("credentials", 0, 1, 0, "Astra68 contributors",
              "Copyright 2026 Astra68 contributors");

static AstraConfig config = ASTRA_CONFIG_INIT;

static uint32_t store(uint32_t handle, const AstraCredentialRequest *request)
{
    uint8_t secret[ASTRA_CREDENTIAL_VALUE_MAX];
    void *mapping = NULL;
    uint32_t span = 0u;
    uint32_t status = astra_rt_area_map(handle, ASTRA_AREA_MAP_READ,
                                         &mapping, &span);
    if (status != ASTRA_SYSCALL_OK) return ASTRA_CONFIG_ACCESS;
    if (span < request->length) status = ASTRA_CONFIG_INVALID;
    else {
        /* Sender can still mutate its area; seal a private snapshot. */
        memcpy(secret, mapping, request->length);
        status = ASTRA_CONFIG_OK;
    }
    if (astra_rt_area_unmap(mapping) != ASTRA_SYSCALL_OK)
        status = ASTRA_CONFIG_IO;
    if (status == ASTRA_CONFIG_OK)
        status = astra_config_set_secret(&config, request->key,
                                          secret, request->length);
    astra_secret_wipe(secret, sizeof(secret));
    return status;
}

static uint32_t fetch(const AstraCredentialRequest *request,
                       uint32_t *area, uint32_t *length)
{
    uint8_t secret[ASTRA_CREDENTIAL_VALUE_MAX];
    void *mapping = NULL;
    uint32_t span = 0u, writable = 0u;
    uint32_t status = astra_config_get_secret(&config, request->key,
                                                NULL, 0u, length);
    if (status != ASTRA_CONFIG_BUFFER_TOO_SMALL) return status;
    if (*length > ASTRA_CREDENTIAL_VALUE_MAX) return ASTRA_CONFIG_MALFORMED;
    if (request->length < *length) return ASTRA_CONFIG_BUFFER_TOO_SMALL;
    status = astra_config_get_secret(&config, request->key, secret,
                                     sizeof(secret), length);
    if (status != ASTRA_CONFIG_OK) goto done;
    if (astra_rt_area_create(*length,
          ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
          ASTRA_RIGHT_TRANSFER, &writable) != ASTRA_SYSCALL_OK) {
        status = ASTRA_CONFIG_NO_MEMORY;
        goto done;
    }
    if (astra_rt_area_map(writable,
          ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
          &mapping, &span) != ASTRA_SYSCALL_OK || span < *length) {
        status = ASTRA_CONFIG_IO;
        goto done;
    }
    memcpy(mapping, secret, *length);
    if (astra_rt_area_unmap(mapping) != ASTRA_SYSCALL_OK) {
        status = ASTRA_CONFIG_IO;
        mapping = NULL;
        goto done;
    }
    mapping = NULL;
    if (astra_rt_handle_duplicate(writable,
          ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
          ASTRA_RIGHT_TRANSFER, area) != ASTRA_SYSCALL_OK)
        status = ASTRA_CONFIG_NO_MEMORY;
done:
    astra_secret_wipe(secret, sizeof(secret));
    if (mapping != NULL) (void)astra_rt_area_unmap(mapping);
    if (writable != 0u) (void)astra_close(writable);
    return status;
}

static void serve(uint32_t receive, uint32_t reader)
{
    uint8_t packet[ASTRA_MESSAGE_SIZE_MAX];
    AstraCredentialRequest request = {0};
    AstraCredentialReply reply = {0};
    uint32_t handles[ASTRA_MESSAGE_HANDLES_MAX] = {0};
    uint32_t bytes = 0u, count = 0u, area = 0u;
    uint32_t status = astra_port_receive(receive, packet, sizeof(packet),
        handles, ASTRA_MESSAGE_HANDLES_MAX, &bytes, &count);
    if (status != ASTRA_SYSCALL_OK) return;
    if (bytes == sizeof(request)) memcpy(&request, packet, bytes);
    status = credential_request_valid(&request, bytes, count, reader) ?
             ASTRA_CONFIG_OK : ASTRA_CONFIG_INVALID;
    if (status == ASTRA_CONFIG_OK && reader)
        status = fetch(&request, &area, &reply.length);
    else if (status == ASTRA_CONFIG_OK &&
             request.header.operation == ASTRA_CREDENTIAL_SET)
        status = store(handles[0], &request);
    else if (status == ASTRA_CONFIG_OK)
        status = astra_config_remove_secret(&config, request.key);
    astra_message_header_set(&reply.header, sizeof(reply),
        ASTRA_CREDENTIAL_PROTOCOL, ASTRA_CREDENTIAL_VERSION,
        ASTRA_CREDENTIAL_REPLY, request.header.transaction_id);
    reply.status = status;
    if (count != 0u) {
        uint32_t sent = astra_port_send(handles[count - 1u], &reply,
            sizeof(reply), area == 0u ? NULL : &area,
            area == 0u ? 0u : 1u);
        if (sent != ASTRA_SYSCALL_OK && area != 0u)
            (void)astra_close(area);
    } else if (area != 0u) (void)astra_close(area);
    for (uint32_t at = 0u; at < count; ++at)
        (void)astra_close(handles[at]);
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *bootstrap;
    uint32_t receive[2] = {0};
    uint32_t published[2] = {0};
    uint32_t status;

    if (!astra_startup_validate(startup)) return ASTRA_STATUS_INVALID;
    bootstrap = astra_startup_capability(startup,
        ASTRA_CAPABILITY_SERVICE_READY);
    if (bootstrap == NULL) return ASTRA_STATUS_BAD_HANDLE;
    status = astra_config_open_system(startup, 1u,
        ASTRA_CONFIG_OPEN_READ | ASTRA_CONFIG_OPEN_WRITE, &config, NULL);
    if (status != ASTRA_CONFIG_OK) {
        (void)astra_service_ready(bootstrap->handle, ASTRA_STATUS_IO,
            NULL, 0u);
        return ASTRA_STATUS_IO;
    }
    for (uint32_t at = 0u; at < 2u; ++at) {
        status = astra_rt_port_create(16u,
            16u * sizeof(AstraCredentialRequest),
            &receive[at], &published[at]);
        if (status != ASTRA_SYSCALL_OK) return ASTRA_STATUS_LIMIT;
    }
    status = astra_service_ready(bootstrap->handle, ASTRA_STATUS_OK,
        published, 2u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_SYSCALL_OK) return ASTRA_STATUS_PEER_DEAD;
    for (;;) {
        uint32_t index = 0u;
        status = astra_wait_multiple(receive, 2u, ASTRA_DEADLINE_FOREVER,
            &index, NULL);
        if (status != ASTRA_SYSCALL_OK || index >= 2u)
            return ASTRA_STATUS_PEER_DEAD;
        serve(receive[index], index);
    }
}
