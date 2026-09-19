#define _DEFAULT_SOURCE 1

#include <astra/entropy.h>
#include <astra/posix.h>
#include <astra/runtime.h>
#include <astra/status.h>

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static AstraStartupInfo startup;
static AstraStartupCapability entropy_capability = {.handle = 7u};
static int capability_present = 1;
static uint32_t send_status = ASTRA_SYSCALL_OK;
static uint32_t reply_status = ASTRA_STATUS_OK;
static int malformed_reply;
static uint32_t close_count;

const AstraStartupInfo *astra_posix_startup(void) { return &startup; }

const AstraStartupCapability *
astra_startup_capability(const AstraStartupInfo *ignored, const char *name)
{
    assert(ignored == &startup);
    assert(strcmp(name, ASTRA_CAPABILITY_ENTROPY) == 0);
    return capability_present ? &entropy_capability : NULL;
}

uint32_t astra_rt_port_create(uint32_t messages, uint32_t bytes,
                              uint32_t *receive, uint32_t *send)
{
    assert(messages == 1u && bytes == sizeof(AstraEntropyReply));
    *receive = 8u;
    *send = 9u;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_port_send(uint32_t handle, const void *message, uint32_t size,
                         const uint32_t *handles, uint32_t handle_count)
{
    const AstraEntropyRequest *request = message;

    assert(handle == entropy_capability.handle);
    assert(size == sizeof(*request));
    assert(request->header.protocol == ASTRA_ENTROPY_PROTOCOL);
    assert(request->header.operation == ASTRA_ENTROPY_OPERATION_GET);
    assert(request->length <= ASTRA_ENTROPY_MAX);
    assert(handles != NULL && handles[0] == 9u && handle_count == 1u);
    return send_status;
}

uint32_t astra_port_receive(uint32_t handle, void *message, uint32_t capacity,
                            uint32_t *handles, uint32_t handle_capacity,
                            uint32_t *size, uint32_t *handle_count)
{
    AstraEntropyReply *reply = message;
    uint32_t length = 32u;

    assert(handle == 8u && capacity == sizeof(*reply));
    assert(handles == NULL && handle_capacity == 0u);
    memset(reply, 0, sizeof(*reply));
    astra_message_header_set(&reply->header,
                             ASTRA_ENTROPY_REPLY_PREFIX_SIZE + length,
                             ASTRA_ENTROPY_PROTOCOL,
                             ASTRA_ENTROPY_PROTOCOL_VERSION,
                             ASTRA_ENTROPY_OPERATION_REPLY, 23u);
    reply->status = reply_status;
    reply->length = reply_status == ASTRA_STATUS_OK ? length : 0u;
    for (uint32_t index = 0u; index < length; ++index)
        reply->data[index] = (uint8_t)(index ^ 0xa5u);
    if (malformed_reply)
        reply->header.protocol = 0u;
    *size = reply->header.total_size;
    if (handle_count != NULL)
        *handle_count = 0u;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_activity_current(void) { return 23u; }

uint32_t astra_close(uint32_t handle)
{
    assert(handle == 8u || handle == 9u);
    ++close_count;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_wait_one(uint32_t handle, uint64_t deadline,
                        uint32_t *observed)
{
    (void)handle;
    (void)deadline;
    (void)observed;
    return ASTRA_SYSCALL_OK;
}

int main(void)
{
    uint8_t bytes[32];
    uint8_t oversized[ASTRA_ENTROPY_MAX + 1u];
    int (*entropy_call)(void *, size_t) = getentropy;

    memset(bytes, 0, sizeof(bytes));
    assert(getentropy(bytes, sizeof(bytes)) == 0);
    for (uint32_t index = 0u; index < sizeof(bytes); ++index)
        assert(bytes[index] == (uint8_t)(index ^ 0xa5u));
    assert(close_count == 1u);

    errno = 0;
    assert(entropy_call(NULL, 1u) == -1 && errno == EFAULT);
    errno = 0;
    assert(entropy_call(oversized, sizeof(oversized)) == -1 && errno == EIO);
    assert(entropy_call(NULL, 0u) == 0);

    capability_present = 0;
    errno = 0;
    assert(getentropy(bytes, sizeof(bytes)) == -1 && errno == ENOSYS);
    capability_present = 1;

    malformed_reply = 1;
    errno = 0;
    assert(getentropy(bytes, sizeof(bytes)) == -1 && errno == EIO);
    malformed_reply = 0;

    send_status = ASTRA_SYSCALL_PEER_DEAD;
    errno = 0;
    assert(getentropy(bytes, sizeof(bytes)) == -1 && errno == EIO);

    puts("ASTRA POSIX ENTROPY PASS");
    return 0;
}
