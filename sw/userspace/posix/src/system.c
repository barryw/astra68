/* POSIX system(), executed by the shell that launched this process. */

#include <astra/posix_descriptor.h>

#include <astra/process.h>
#include <astra/runtime.h>
#include <astra/shell_service.h>
#include <astra/status.h>
#include <astra/syscall.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern char **environ;

static const AstraStartupCapability *
shell_capability(void)
{
    return astra_startup_capability(astra_posix_startup(),
                                    ASTRA_CAPABILITY_SHELL);
}

static int
pack_environment(char *out, uint32_t capacity, uint32_t *length,
                 uint16_t *count)
{
    uint32_t used = 0u;
    uint16_t entries = 0u;

    if (environ != NULL)
        for (char **entry = environ; *entry != NULL; ++entry) {
            uint32_t size = 0u;
            int equals = 0;

            while ((*entry)[size] != '\0') {
                if ((*entry)[size] == '=' && size != 0u)
                    equals = 1;
                if (size + 1u >= capacity) {
                    errno = E2BIG;
                    return 0;
                }
                ++size;
            }
            ++size;
            if (!equals) {
                errno = EINVAL;
                return 0;
            }
            if (entries == ASTRA_LAUNCH_ENVIRONMENT_MAX ||
                size > capacity - used) {
                errno = E2BIG;
                return 0;
            }
            if (out != NULL)
                (void)memcpy(out + used, *entry, size);
            used += size;
            ++entries;
        }
    *length = used;
    *count = entries;
    return 1;
}

static int
duplicate_stream(int fd, uint32_t *handles, uint32_t *count, uint8_t *index)
{
    uint32_t source = astra_posix_descriptor_handle(fd);
    uint32_t duplicate;

    *index = ASTRA_SHELL_HANDLE_NONE;
    if (source == 0u)
        return 1;
    if (astra_rt_handle_duplicate(source,
                                  ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT |
                                      ASTRA_RIGHT_TRANSFER,
                                  &duplicate) != ASTRA_SYSCALL_OK) {
        errno = EACCES;
        return 0;
    }
    *index = (uint8_t)*count;
    handles[(*count)++] = duplicate;
    return 1;
}

static void
close_handles(uint32_t *handles, uint32_t count)
{
    for (uint32_t index = 0u; index < count; ++index)
        if (handles[index] != 0u)
            (void)astra_close(handles[index]);
}

int
system(const char *command)
{
    const AstraStartupCapability *service = shell_capability();
    AstraShellExecuteRequest request = {0};
    AstraShellExecuteReply reply;
    uint32_t handles[5] = {0u, 0u, 0u, 0u, 0u};
    uint32_t received[1];
    uint32_t handle_count = 0u;
    uint32_t received_count = 0u;
    uint32_t received_size = 0u;
    uint32_t reply_receive = 0u;
    uint32_t reply_send = 0u;
    uint32_t area = 0u;
    uint32_t area_send = 0u;
    uint32_t area_size = 0u;
    void *area_address = NULL;
    uint32_t command_length;
    uint32_t environment_length;
    uint32_t execute_size;
    uint16_t environment_count;
    uint32_t status;
    int sent = 0;
    int result = -1;

    if (command == NULL)
        return service != NULL &&
               (service->rights & ASTRA_RIGHT_SIGNAL) != 0u;
    if (service == NULL || (service->rights & ASTRA_RIGHT_SIGNAL) == 0u) {
        errno = ENOSYS;
        return -1;
    }
    if (!pack_environment(NULL, ASTRA_LAUNCH_ENVIRONMENT_BYTES,
                          &environment_length, &environment_count))
        return -1;
    command_length = 0u;
    while (command_length <
               ASTRA_AREA_SIZE_MAX - environment_length - 1u &&
           command[command_length] != '\0')
        ++command_length;
    if (command[command_length] != '\0') {
        errno = E2BIG;
        return -1;
    }
    execute_size = command_length + 1u + environment_length;
    status = astra_rt_port_create(1u, ASTRA_SHELL_EXECUTE_REPLY_SIZE,
                                  &reply_receive, &reply_send);
    if (status != ASTRA_SYSCALL_OK)
        goto resource_failure;
    status = astra_rt_area_create(
        execute_size,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
            ASTRA_RIGHT_TRANSFER,
        &area);
    if (status != ASTRA_SYSCALL_OK)
        goto resource_failure;
    status = astra_rt_area_map(area,
                               ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                               &area_address, &area_size);
    if (status != ASTRA_SYSCALL_OK)
        goto resource_failure;
    (void)memcpy(area_address, command, command_length + 1u);
    if (!pack_environment((char *)area_address + command_length + 1u,
                          area_size - command_length - 1u,
                          &environment_length, &environment_count))
        goto cleanup;
    if (command_length + 1u + environment_length > area_size) {
        errno = E2BIG;
        goto cleanup;
    }

    handles[handle_count++] = reply_send;
    status = astra_rt_handle_duplicate(
        area, ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER,
        &area_send);
    if (status != ASTRA_SYSCALL_OK)
        goto resource_failure;
    handles[handle_count++] = area_send;
    if (!duplicate_stream(0, handles, &handle_count, &request.stdin_index) ||
        !duplicate_stream(1, handles, &handle_count, &request.stdout_index) ||
        !duplicate_stream(2, handles, &handle_count, &request.stderr_index))
        goto cleanup;

    astra_message_header_set(&request.header, sizeof(request),
                             ASTRA_SHELL_SERVICE_PROTOCOL,
                             ASTRA_SHELL_SERVICE_VERSION,
                             ASTRA_SHELL_EXECUTE,
                             astra_activity_current());
    request.command_length = command_length;
    request.environment_length = environment_length;
    request.environment_count = environment_count;
    request.handle_count = (uint8_t)handle_count;
    status = astra_port_send(service->handle, &request, sizeof(request),
                             handles, handle_count);
    if (status != ASTRA_SYSCALL_OK) {
        errno = status == ASTRA_SYSCALL_WOULD_BLOCK ? EAGAIN : EIO;
        goto cleanup;
    }
    sent = 1;
    reply_send = 0u;
    area_send = 0u;
    status = astra_wait_one(reply_receive, ASTRA_DEADLINE_FOREVER, NULL);
    if (status != ASTRA_SYSCALL_OK ||
        astra_port_receive(reply_receive, &reply, sizeof(reply), received, 1u,
                           &received_size, &received_count) !=
            ASTRA_SYSCALL_OK ||
        received_size != sizeof(reply) || received_count != 0u ||
        reply.header.total_size != sizeof(reply) ||
        reply.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        reply.header.protocol != ASTRA_SHELL_SERVICE_PROTOCOL ||
        reply.header.protocol_version != ASTRA_SHELL_SERVICE_VERSION ||
        reply.header.operation != ASTRA_SHELL_EXECUTED ||
        reply.header.transaction_id != request.header.transaction_id) {
        errno = EIO;
        goto cleanup;
    }
    if (reply.status != ASTRA_STATUS_OK) {
        errno = reply.status == ASTRA_STATUS_LIMIT ? E2BIG : EIO;
        goto cleanup;
    }
    result = (int)((reply.command_status & UINT32_C(0xff)) << 8);
    goto cleanup;

resource_failure:
    errno = ENOMEM;
cleanup:
    if (!sent)
        close_handles(handles, handle_count);
    if (area_address != NULL)
        (void)astra_rt_area_unmap(area_address);
    if (area != 0u)
        (void)astra_close(area);
    if (reply_receive != 0u)
        (void)astra_close(reply_receive);
    return result;
}
