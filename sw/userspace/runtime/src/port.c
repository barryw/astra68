/*
 * Ports, from userspace.
 *
 * The kernel has had these since the message ABI landed and nothing in
 * userspace could reach them: every service on this machine is in the
 * supervisor's own process and talks to its clients through a function
 * pointer. That stops being true the moment a program can be launched, because
 * a child cannot call into its parent's address space.
 *
 * Three wrappers and no policy. A send that does not fit, a receive with
 * nothing waiting, a peer that has gone -- all of them are the kernel's answers
 * and all of them are returned rather than acted on. In particular
 * ASTRA_SYSCALL_WOULD_BLOCK on a send is *the* back pressure mechanism, not an
 * error: the port is the queue, and a caller that retries is a caller doing the
 * right thing.
 */

#include <astra/runtime.h>
#include <astra/syscall.h>

uint32_t
astra_rt_port_create(uint32_t message_max, uint32_t byte_max,
                  uint32_t *receive_handle, uint32_t *send_handle)
{
    AstraSyscallResult result;

    if (receive_handle != NULL) {
        *receive_handle = 0u;
    }
    if (send_handle != NULL) {
        *send_handle = 0u;
    }
    if (receive_handle == NULL || send_handle == NULL) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    astra_syscall5(ASTRA_SYSCALL_PORT_CREATE, message_max, byte_max, 0u, 0u,
                   0u, &result);
    if (result.status == ASTRA_SYSCALL_OK) {
        *receive_handle = result.value0;
        *send_handle = result.value1;
    }
    return result.status;
}

uint32_t
astra_port_send(uint32_t handle, const void *message, uint32_t size,
                const uint32_t *handles, uint32_t handle_count)
{
    AstraSyscallResult result;

    if (message == NULL || size < ASTRA_MESSAGE_HEADER_SIZE) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    astra_syscall5(ASTRA_SYSCALL_PORT_SEND_TRY, handle,
                   (uint32_t)(uintptr_t)message, size,
                   (uint32_t)(uintptr_t)handles, handle_count, &result);
    return result.status;
}

uint32_t
astra_port_receive(uint32_t handle, void *message, uint32_t capacity,
                   uint32_t *handles, uint32_t handle_capacity,
                   uint32_t *size, uint32_t *handle_count)
{
    AstraSyscallResult result;

    /*
     * Cleared first, and set from the kernel's answer whatever that answer was.
     * A receive that found the buffer too small reports what it would have
     * needed, and that number is the point of the refusal -- a caller that read
     * it only on success could not act on it.
     */
    if (size != NULL) {
        *size = 0u;
    }
    if (handle_count != NULL) {
        *handle_count = 0u;
    }
    if (message == NULL || size == NULL) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    astra_syscall5(ASTRA_SYSCALL_PORT_RECEIVE_TRY, handle,
                   (uint32_t)(uintptr_t)message, capacity,
                   (uint32_t)(uintptr_t)handles, handle_capacity, &result);
    *size = result.value0;
    if (handle_count != NULL) {
        *handle_count = result.value1;
    }
    return result.status;
}
