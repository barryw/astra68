#include <astra/runtime.h>
#include <astra/status.h>
#include <astra/syscall.h>

static uint32_t
status_from_syscall(uint32_t status)
{
    if (status == ASTRA_SYSCALL_PEER_DEAD || status == ASTRA_SYSCALL_CLOSED)
        return ASTRA_STATUS_PEER_DEAD;
    if (status == ASTRA_SYSCALL_RESOURCE_LIMIT ||
        status == ASTRA_SYSCALL_OUT_OF_MEMORY)
        return ASTRA_STATUS_LIMIT;
    if (status == ASTRA_SYSCALL_ACCESS_DENIED)
        return ASTRA_STATUS_ACCESS;
    return ASTRA_STATUS_INVALID;
}

static uint32_t
call(uint32_t service, uint32_t operation, int32_t process, int32_t value,
     uint32_t flags, uint32_t process_handle,
     AstraPosixProcessReply *returned)
{
    AstraPosixProcessRequest request = {0};
    AstraPosixProcessReply reply;
    uint32_t handles[2] = {0u};
    uint32_t handle_count = process_handle != 0u ? 2u : 1u;
    uint32_t received_handles = 0u;
    uint32_t received_size = 0u;
    uint32_t receive = 0u;
    uint32_t status;
    int sent = 0;

    if (service == 0u)
        return ASTRA_STATUS_BAD_HANDLE;
    if (process_handle != 0u) {
        status = astra_rt_handle_duplicate(
            process_handle,
            ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_SIGNAL |
                ASTRA_RIGHT_WAIT | ASTRA_RIGHT_TRANSFER,
            &handles[1]);
        if (status != ASTRA_SYSCALL_OK)
            return status_from_syscall(status);
    }
    status = astra_rt_port_create(1u, sizeof(reply), &receive, &handles[0]);
    if (status != ASTRA_SYSCALL_OK) {
        if (handles[1] != 0u)
            (void)astra_close(handles[1]);
        return status_from_syscall(status);
    }
    astra_message_header_set(&request.header, sizeof(request),
                             ASTRA_POSIX_PROCESS_PROTOCOL,
                             ASTRA_POSIX_PROCESS_VERSION, operation, 0u);
    request.process = process;
    request.value = value;
    request.flags = flags;
    for (;;) {
        status = astra_port_send(service, &request, sizeof(request), handles,
                                 handle_count);
        if (status != ASTRA_SYSCALL_WOULD_BLOCK)
            break;
        status = astra_wait_one_restart(service, ASTRA_DEADLINE_FOREVER,
                                        NULL);
        if (status != ASTRA_SYSCALL_OK)
            break;
    }
    if (status != ASTRA_SYSCALL_OK)
        goto failed;
    sent = 1;
    for (;;) {
        status = astra_port_receive(receive, &reply, sizeof(reply), NULL, 0u,
                                    &received_size, &received_handles);
        if (status != ASTRA_SYSCALL_WOULD_BLOCK)
            break;
        status = astra_wait_one_restart(receive, ASTRA_DEADLINE_FOREVER,
                                        NULL);
        if (status != ASTRA_SYSCALL_OK)
            break;
    }
    if (status != ASTRA_SYSCALL_OK)
        goto failed;
    if (received_size != sizeof(reply) || received_handles != 0u ||
        reply.header.total_size != sizeof(reply) ||
        reply.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        reply.header.flags != 0u || reply.header.reserved != 0u ||
        reply.header.protocol != ASTRA_POSIX_PROCESS_PROTOCOL ||
        reply.header.protocol_version != ASTRA_POSIX_PROCESS_VERSION ||
        reply.header.operation != operation ||
        reply.header.transaction_id != 0u) {
        status = ASTRA_SYSCALL_INVALID_ARGUMENT;
        goto failed;
    }
    (void)astra_close(receive);
    if (returned != NULL)
        *returned = reply;
    return reply.status;

failed:
    (void)astra_close(receive);
    if (!sent) {
        (void)astra_close(handles[0]);
        if (handles[1] != 0u)
            (void)astra_close(handles[1]);
    }
    return status_from_syscall(status);
}

uint32_t
astra_posix_process_register(uint32_t service, uint32_t process_handle,
                             uint32_t process_id, uint32_t flags)
{
    if (process_handle == 0u || process_id == 0u ||
        (flags & ~ASTRA_POSIX_PROCESS_FLAG_MASK) != 0u)
        return ASTRA_STATUS_INVALID;
    return call(service, ASTRA_POSIX_PROCESS_REGISTER, (int32_t)process_id,
                0, flags, process_handle, NULL);
}

uint32_t
astra_posix_process_signal(uint32_t service, int32_t selector,
                           uint32_t signal)
{
    if (signal >= 32u)
        return ASTRA_STATUS_INVALID;
    return call(service, ASTRA_POSIX_PROCESS_SIGNAL, selector,
                (int32_t)signal, 0u, 0u, NULL);
}

uint32_t
astra_posix_process_signal_group(uint32_t service, int32_t group,
                                 uint32_t signal)
{
    if (group <= 0 || signal >= 32u)
        return ASTRA_STATUS_INVALID;
    return call(service, ASTRA_POSIX_PROCESS_SIGNAL_GROUP, group,
                (int32_t)signal, 0u, 0u, NULL);
}

uint32_t
astra_posix_process_setpgid(uint32_t service, int32_t process, int32_t group)
{
    return call(service, ASTRA_POSIX_PROCESS_SETPGID, process, group, 0u, 0u,
                NULL);
}

uint32_t
astra_posix_process_setsid(uint32_t service, AstraPosixProcessReply *reply)
{
    if (reply == NULL)
        return ASTRA_STATUS_INVALID;
    return call(service, ASTRA_POSIX_PROCESS_SETSID, 0, 0, 0u, 0u, reply);
}

uint32_t
astra_posix_process_query(uint32_t service, int32_t process,
                          AstraPosixProcessReply *reply)
{
    if (reply == NULL)
        return ASTRA_STATUS_INVALID;
    return call(service, ASTRA_POSIX_PROCESS_QUERY, process, 0, 0u, 0u,
                reply);
}

uint32_t
astra_posix_process_tty_attach(uint32_t service)
{
    return call(service, ASTRA_POSIX_PROCESS_TTY_ATTACH, 0, 0, 0u, 0u, NULL);
}

uint32_t
astra_posix_process_tty_foreground(uint32_t service,
                                   AstraPosixProcessReply *reply)
{
    if (reply == NULL)
        return ASTRA_STATUS_INVALID;
    return call(service, ASTRA_POSIX_PROCESS_TTY_FOREGROUND, 0, 0, 0u, 0u,
                reply);
}

uint32_t
astra_posix_process_tty_set_foreground(uint32_t service, int32_t group)
{
    if (group <= 0)
        return ASTRA_STATUS_INVALID;
    return call(service, ASTRA_POSIX_PROCESS_TTY_SET_FOREGROUND, 0, group, 0u,
                0u, NULL);
}

uint32_t
astra_posix_process_tty_signal(uint32_t service, uint32_t signal)
{
    if (signal == 0u || signal >= 32u)
        return ASTRA_STATUS_INVALID;
    return call(service, ASTRA_POSIX_PROCESS_TTY_SIGNAL, 0, (int32_t)signal,
                0u, 0u, NULL);
}
