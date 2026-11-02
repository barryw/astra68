#include <astra/config_library.h>
#include <astra/credential_service.h>
#include <astra/port.h>
#include <astra/runtime.h>
#include <astra/secret_memory.h>

#include <string.h>

static uint32_t call(const AstraStartupInfo *startup, uint32_t operation,
                     const char *key, uint32_t length, uint32_t *area,
                     AstraCredentialReply *reply, uint32_t *received)
{
    const char *capability_name = operation == ASTRA_CREDENTIAL_GET ?
        ASTRA_CAPABILITY_CREDENTIAL_READ : ASTRA_CAPABILITY_CREDENTIAL_WRITE;
    const AstraStartupCapability *capability;
    AstraCredentialRequest request = {0};
    AstraCall exchange = {0};
    uint32_t at = 0u;

    if (key == NULL || reply == NULL || received == NULL ||
        !astra_startup_validate(startup)) return ASTRA_CONFIG_INVALID;
    while (at <= ASTRA_CREDENTIAL_KEY_MAX && key[at] != '\0') ++at;
    if (at == 0u || at > ASTRA_CREDENTIAL_KEY_MAX)
        return ASTRA_CONFIG_INVALID;
    capability = astra_startup_capability(startup, capability_name);
    if (capability == NULL) return ASTRA_CONFIG_ACCESS;
    memcpy(request.key, key, at);
    request.length = length;
    astra_message_header_set(&request.header, sizeof(request),
        ASTRA_CREDENTIAL_PROTOCOL, ASTRA_CREDENTIAL_VERSION, operation, 1u);
    exchange.request = &request;
    exchange.request_size = sizeof(request);
    exchange.handles = area;
    exchange.handle_count = area != NULL ? 1u : 0u;
    exchange.reply_index = exchange.handle_count;
    exchange.reply = reply;
    exchange.reply_capacity = sizeof(*reply);
    exchange.reply_handles = received;
    exchange.reply_handle_capacity = 1u;
    *received = 0u;
    if (astra_port_call(capability->handle, &exchange,
                        ASTRA_DEADLINE_INFINITE) != ASTRA_OK)
        return ASTRA_CONFIG_IO;
    if (exchange.reply_size != sizeof(*reply) ||
        reply->header.total_size != sizeof(*reply) ||
        reply->header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        reply->header.flags != 0u ||
        reply->header.protocol != ASTRA_CREDENTIAL_PROTOCOL ||
        reply->header.protocol_version != ASTRA_CREDENTIAL_VERSION ||
        reply->header.operation != ASTRA_CREDENTIAL_REPLY ||
        reply->header.transaction_id != 1u ||
        reply->header.reserved != 0u || reply->reserved != 0u ||
        reply->status > ASTRA_CONFIG_AUTH_FAILED ||
        exchange.reply_handle_count > 1u ||
        ((operation != ASTRA_CREDENTIAL_GET ||
          reply->status != ASTRA_CONFIG_OK) &&
         exchange.reply_handle_count != 0u) ||
        (operation == ASTRA_CREDENTIAL_GET &&
         reply->status == ASTRA_CONFIG_OK &&
         exchange.reply_handle_count != 1u)) {
        if (*received != 0u) (void)astra_close(*received);
        *received = 0u;
        return ASTRA_CONFIG_MALFORMED;
    }
    return reply->status;
}

uint32_t astra_credential_set(const AstraStartupInfo *startup,
                              const char *key, const void *secret,
                              uint32_t length)
{
    AstraCredentialReply reply = {0};
    uint32_t area = 0u, transfer = 0u, received = 0u, span = 0u;
    void *mapping = NULL;
    uint32_t status;

    if (secret == NULL || length == 0u ||
        length > ASTRA_CREDENTIAL_VALUE_MAX) return ASTRA_CONFIG_INVALID;
    status = astra_rt_area_create(length,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
        ASTRA_RIGHT_TRANSFER, &area);
    if (status != ASTRA_SYSCALL_OK) return ASTRA_CONFIG_NO_MEMORY;
    status = astra_rt_area_map(area,
        ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE, &mapping, &span);
    if (status != ASTRA_SYSCALL_OK || span < length) {
        if (mapping != NULL) (void)astra_rt_area_unmap(mapping);
        (void)astra_close(area);
        return ASTRA_CONFIG_NO_MEMORY;
    }
    memcpy(mapping, secret, length);
    status = astra_rt_handle_duplicate(area,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER,
        &transfer);
    if (status == ASTRA_SYSCALL_OK)
        status = call(startup, ASTRA_CREDENTIAL_SET, key, length,
                      &transfer, &reply, &received);
    else status = ASTRA_CONFIG_NO_MEMORY;
    astra_secret_wipe(mapping, length);
    if (astra_rt_area_unmap(mapping) != ASTRA_SYSCALL_OK &&
        status == ASTRA_CONFIG_OK) status = ASTRA_CONFIG_IO;
    (void)astra_close(area);
    if (transfer != 0u) (void)astra_close(transfer);
    if (received != 0u) (void)astra_close(received);
    return status;
}

uint32_t astra_credential_get(const AstraStartupInfo *startup,
                              const char *key, void *secret,
                              uint32_t capacity, uint32_t *length)
{
    AstraCredentialReply reply = {0};
    uint32_t received = 0u, span = 0u, status;
    void *mapping = NULL;

    if (length == NULL || (secret == NULL && capacity != 0u))
        return ASTRA_CONFIG_INVALID;
    *length = 0u;
    status = call(startup, ASTRA_CREDENTIAL_GET, key,
                  secret == NULL ? 0u :
                  (capacity > ASTRA_CREDENTIAL_VALUE_MAX ?
                   ASTRA_CREDENTIAL_VALUE_MAX : capacity),
                  NULL, &reply, &received);
    if (status == ASTRA_CONFIG_OK || status == ASTRA_CONFIG_BUFFER_TOO_SMALL)
        *length = reply.length;
    if (status != ASTRA_CONFIG_OK) return status;
    if (secret == NULL || reply.length == 0u || reply.length > capacity ||
        reply.length > ASTRA_CREDENTIAL_VALUE_MAX) {
        (void)astra_close(received);
        return ASTRA_CONFIG_MALFORMED;
    }
    status = astra_rt_area_map(received,
        ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE, &mapping, &span);
    if (status == ASTRA_SYSCALL_OK && span >= reply.length) {
        memcpy(secret, mapping, reply.length);
        astra_secret_wipe(mapping, reply.length);
    } else status = ASTRA_SYSCALL_INVALID_ARGUMENT;
    if (mapping != NULL && astra_rt_area_unmap(mapping) != ASTRA_SYSCALL_OK)
        status = ASTRA_SYSCALL_INVALID_ARGUMENT;
    (void)astra_close(received);
    return status == ASTRA_SYSCALL_OK ? ASTRA_CONFIG_OK : ASTRA_CONFIG_IO;
}

uint32_t astra_credential_remove(const AstraStartupInfo *startup,
                                 const char *key)
{
    AstraCredentialReply reply = {0};
    uint32_t received = 0u;
    return call(startup, ASTRA_CREDENTIAL_REMOVE, key, 0u,
                NULL, &reply, &received);
}
