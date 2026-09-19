#include <astra/entropy.h>
#include <astra/posix.h>
#include <astra/runtime.h>
#include <astra/status.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
getentropy(void *buffer, size_t length)
{
    const AstraStartupCapability *service;
    AstraEntropyRequest request = {0};
    AstraEntropyReply reply = {0};
    uint32_t receive = 0u;
    uint32_t carried = 0u;
    uint32_t reply_size = 0u;
    uint32_t transaction;
    uint32_t status;

    if (length > ASTRA_ENTROPY_MAX) {
        errno = EIO;
        return -1;
    }
    if (length != 0u && buffer == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (length == 0u)
        return 0;
    service = astra_startup_capability(astra_posix_startup(),
                                       ASTRA_CAPABILITY_ENTROPY);
    if (service == NULL) {
        errno = ENOSYS;
        return -1;
    }
    status = astra_rt_port_create(1u, sizeof(reply), &receive, &carried);
    if (status != ASTRA_SYSCALL_OK) {
        errno = EIO;
        return -1;
    }
    transaction = astra_activity_current();
    astra_message_header_set(&request.header, sizeof(request),
                             ASTRA_ENTROPY_PROTOCOL,
                             ASTRA_ENTROPY_PROTOCOL_VERSION,
                             ASTRA_ENTROPY_OPERATION_GET, transaction);
    request.length = (uint32_t)length;
    status = astra_port_send(service->handle, &request, sizeof(request),
                             &carried, 1u);
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_close(carried);
        (void)astra_close(receive);
        errno = EIO;
        return -1;
    }
    for (;;) {
        status = astra_port_receive(receive, &reply, sizeof(reply), NULL, 0u,
                                    &reply_size, NULL);
        if (status == ASTRA_SYSCALL_OK)
            break;
        if (status != ASTRA_SYSCALL_WOULD_BLOCK ||
            astra_wait_one_restart(receive, ASTRA_DEADLINE_FOREVER, NULL) !=
                ASTRA_SYSCALL_OK) {
            (void)astra_close(receive);
            errno = EIO;
            return -1;
        }
    }
    (void)astra_close(receive);
    if (reply_size != ASTRA_ENTROPY_REPLY_PREFIX_SIZE + length ||
        reply.header.total_size != reply_size ||
        reply.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        reply.header.flags != 0u ||
        reply.header.protocol != ASTRA_ENTROPY_PROTOCOL ||
        reply.header.protocol_version != ASTRA_ENTROPY_PROTOCOL_VERSION ||
        reply.header.operation != ASTRA_ENTROPY_OPERATION_REPLY ||
        reply.header.transaction_id != transaction ||
        reply.header.reserved != 0u ||
        reply.status != ASTRA_STATUS_OK || reply.length != length ||
        reply.reserved[0] != 0u || reply.reserved[1] != 0u) {
        errno = EIO;
        return -1;
    }
    (void)memcpy(buffer, reply.data, length);
    return 0;
}

int
arc4random_fork_detect(void)
{
    static pid_t process;
    pid_t current = getpid();

    if (process == 0) {
        process = current;
        return 0;
    }
    if (process == current)
        return 0;
    process = current;
    return 1;
}

void
arc4random_abort(void)
{
    abort();
}
