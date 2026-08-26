#include <astra/port.h>

#include "internal/syscall.h"

#include <stddef.h>

_Static_assert(sizeof(AstraMessageHeader) == ASTRA_MESSAGE_HEADER_SIZE,
               "AstraMessageHeader ABI size");
_Static_assert(offsetof(AstraMessageHeader, total_size) == 0u,
               "AstraMessageHeader total_size offset");
_Static_assert(offsetof(AstraMessageHeader, protocol) == 8u,
               "AstraMessageHeader protocol offset");
_Static_assert(offsetof(AstraMessageHeader, operation) == 16u,
               "AstraMessageHeader operation offset");
_Static_assert(offsetof(AstraMessageHeader, transaction_id) == 20u,
               "AstraMessageHeader transaction offset");
_Static_assert(sizeof(AstraPort) == 8u, "AstraPort ABI size");

static int pointer_aligned(const void *pointer)
{
    return ((uintptr_t)pointer & (sizeof(uint32_t) - 1u)) == 0u;
}

static int message_valid(const void *message, uint32_t message_size)
{
    const AstraMessageHeader *header;

    if (message == 0 || !pointer_aligned(message) ||
        message_size < ASTRA_MESSAGE_HEADER_SIZE ||
        message_size > ASTRA_MESSAGE_SIZE_MAX)
        return 0;
    header = message;
    return header->total_size == message_size &&
           header->header_size == ASTRA_MESSAGE_HEADER_SIZE &&
           header->flags == ASTRA_MESSAGE_FLAGS_NONE &&
           header->reserved == 0u;
}

static int handle_vector_valid(const AstraHandle *handles, uint32_t count)
{
    return count <= ASTRA_MESSAGE_HANDLES_MAX &&
           (count == 0u || (handles != 0 && pointer_aligned(handles)));
}

static int deadline_valid(AstraMonotonicDeadline deadline_ns)
{
    return deadline_ns >= ASTRA_DEADLINE_POLL;
}

static AstraResult wait_for_endpoint(AstraHandle endpoint,
                                     AstraMonotonicDeadline deadline_ns)
{
    uint64_t deadline = (uint64_t)deadline_ns;
    uint32_t ignored_d1;
    uint32_t ignored_d2;

    return astra_internal_result(astra_internal_syscall(
        ASTRA_SYSCALL_WAIT_ONE, endpoint, (uint32_t)(deadline >> 32),
        (uint32_t)deadline, 0, 0, &ignored_d1, &ignored_d2));
}

static AstraResult port_send_once(AstraHandle send_endpoint,
                                  const void *message,
                                  uint32_t message_size,
                                  AstraHandle *handles,
                                  uint32_t handle_count)
{
    AstraResult result;
    uint32_t ignored_d1;
    uint32_t ignored_d2;

    result = astra_internal_result(astra_internal_syscall(
        ASTRA_SYSCALL_PORT_SEND_TRY, send_endpoint, (uintptr_t)message,
        message_size, (uintptr_t)handles, handle_count,
        &ignored_d1, &ignored_d2));
    if (result == ASTRA_OK) {
        for (uint32_t index = 0u; index < handle_count; ++index)
            handles[index] = ASTRA_INVALID_HANDLE;
    }
    return result;
}

static AstraResult port_receive_once(AstraHandle receive_endpoint,
                                     void *message,
                                     uint32_t message_capacity,
                                     AstraHandle *handles,
                                     uint32_t handle_capacity,
                                     uint32_t *message_size,
                                     uint32_t *handle_count)
{
    AstraResult result;
    uint32_t returned_size;
    uint32_t returned_handles;

    result = astra_internal_result(astra_internal_syscall(
        ASTRA_SYSCALL_PORT_RECEIVE_TRY, receive_endpoint,
        (uintptr_t)message, message_capacity, (uintptr_t)handles,
        handle_capacity, &returned_size, &returned_handles));
    if (result == ASTRA_OK || result == ASTRA_ERROR_BUFFER_TOO_SMALL) {
        *message_size = returned_size;
        *handle_count = returned_handles;
    }
    return result;
}

AstraResult astra_message_header_init(AstraMessageHeader *header,
                                      uint32_t total_size,
                                      uint32_t protocol,
                                      uint16_t protocol_version,
                                      uint32_t operation,
                                      uint32_t transaction_id)
{
    if (header == 0 || !pointer_aligned(header) ||
        total_size < ASTRA_MESSAGE_HEADER_SIZE ||
        total_size > ASTRA_MESSAGE_SIZE_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    astra_message_header_set(header, total_size, protocol, protocol_version,
                             operation, transaction_id);
    return ASTRA_OK;
}

AstraResult astra_port_create(uint32_t maximum_messages,
                              uint32_t maximum_bytes,
                              AstraPort *port)
{
    AstraResult result;
    uint32_t receive;
    uint32_t send;

    if (port == 0 || port->receive != ASTRA_INVALID_HANDLE ||
        port->send != ASTRA_INVALID_HANDLE || maximum_messages == 0u ||
        maximum_messages > ASTRA_PORT_MESSAGES_MAX ||
        maximum_bytes < ASTRA_MESSAGE_HEADER_SIZE ||
        maximum_bytes > ASTRA_PORT_BYTES_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_internal_result(astra_internal_syscall(
        ASTRA_SYSCALL_PORT_CREATE, maximum_messages, maximum_bytes,
        0, 0, 0, &receive, &send));
    if (result == ASTRA_OK) {
        if (receive == ASTRA_INVALID_HANDLE ||
            send == ASTRA_INVALID_HANDLE)
            return ASTRA_ERROR_IO;
        port->receive = receive;
        port->send = send;
    }
    return result;
}

AstraResult astra_port_close(AstraPort *port)
{
    AstraResult first = ASTRA_OK;
    AstraResult result;
    uint32_t attempted = 0u;

    if (port == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (port->send != ASTRA_INVALID_HANDLE) {
        ++attempted;
        result = astra_handle_close(&port->send);
        if (result != ASTRA_OK)
            first = result;
    }
    if (port->receive != ASTRA_INVALID_HANDLE) {
        ++attempted;
        result = astra_handle_close(&port->receive);
        if (result != ASTRA_OK && first == ASTRA_OK)
            first = result;
    }
    return attempted == 0u ? ASTRA_ERROR_INVALID_HANDLE : first;
}

void astra_port_cleanup(AstraPort *port)
{
    AstraResult ignored = astra_port_close(port);

    (void)ignored;
}

AstraResult astra_port_send_try(AstraHandle send_endpoint,
                                const void *message,
                                uint32_t message_size,
                                AstraHandle *handles,
                                uint32_t handle_count)
{
    if (send_endpoint == ASTRA_INVALID_HANDLE ||
        !message_valid(message, message_size) ||
        !handle_vector_valid(handles, handle_count))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return port_send_once(send_endpoint, message, message_size,
                          handles, handle_count);
}

AstraResult astra_port_send_until(AstraHandle send_endpoint,
                                  const void *message,
                                  uint32_t message_size,
                                  AstraHandle *handles,
                                  uint32_t handle_count,
                                  AstraMonotonicDeadline deadline_ns)
{
    AstraResult result;

    if (send_endpoint == ASTRA_INVALID_HANDLE ||
        !message_valid(message, message_size) ||
        !handle_vector_valid(handles, handle_count) ||
        !deadline_valid(deadline_ns))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (;;) {
        result = port_send_once(send_endpoint, message, message_size,
                                handles, handle_count);
        if (result != ASTRA_ERROR_WOULD_BLOCK ||
            deadline_ns == ASTRA_DEADLINE_POLL)
            return result;
        result = wait_for_endpoint(send_endpoint, deadline_ns);
        if (result != ASTRA_OK)
            return result;
    }
}

AstraResult astra_port_receive_try(AstraHandle receive_endpoint,
                                   void *message,
                                   uint32_t message_capacity,
                                   AstraHandle *handles,
                                   uint32_t handle_capacity,
                                   uint32_t *message_size,
                                   uint32_t *handle_count)
{
    if (message_size == 0 || handle_count == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *message_size = 0u;
    *handle_count = 0u;
    if (receive_endpoint == ASTRA_INVALID_HANDLE ||
        message_capacity > ASTRA_MESSAGE_SIZE_MAX ||
        handle_capacity > ASTRA_MESSAGE_HANDLES_MAX ||
        (message_capacity != 0u &&
         (message == 0 || !pointer_aligned(message))) ||
        !handle_vector_valid(handles, handle_capacity))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return port_receive_once(receive_endpoint, message, message_capacity,
                             handles, handle_capacity, message_size,
                             handle_count);
}

AstraResult astra_port_receive_until(AstraHandle receive_endpoint,
                                     void *message,
                                     uint32_t message_capacity,
                                     AstraHandle *handles,
                                     uint32_t handle_capacity,
                                     uint32_t *message_size,
                                     uint32_t *handle_count,
                                     AstraMonotonicDeadline deadline_ns)
{
    AstraResult result;

    if (message_size == 0 || handle_count == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *message_size = 0u;
    *handle_count = 0u;
    if (receive_endpoint == ASTRA_INVALID_HANDLE ||
        message_capacity > ASTRA_MESSAGE_SIZE_MAX ||
        handle_capacity > ASTRA_MESSAGE_HANDLES_MAX ||
        (message_capacity != 0u &&
         (message == 0 || !pointer_aligned(message))) ||
        !handle_vector_valid(handles, handle_capacity) ||
        !deadline_valid(deadline_ns))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (;;) {
        result = port_receive_once(receive_endpoint, message,
                                   message_capacity, handles,
                                   handle_capacity, message_size,
                                   handle_count);
        if (result != ASTRA_ERROR_WOULD_BLOCK ||
            deadline_ns == ASTRA_DEADLINE_POLL)
            return result;
        result = wait_for_endpoint(receive_endpoint, deadline_ns);
        if (result != ASTRA_OK)
            return result;
    }
}
