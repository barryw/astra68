/*
 * The client half of a stream: what a program calls to write and to read.
 *
 * A handle per call, never a global. A program that writes says where, because
 * the alternative is ambient state that decides on a program's behalf where its
 * output went -- which is descriptor 1 with the serial numbers filed off, and
 * the launch spec's 4.1 refuses it.
 */

#include <astra/stream.h>

#include <astra/runtime.h>
#include <astra/syscall.h>

#include <stddef.h>

/*
 * One reply at a time, one message deep. A reader has exactly one read
 * outstanding -- the call does not return until the answer is in -- so a deeper
 * port would be capacity nothing can use.
 */
#define STREAM_REPLY_MESSAGES 1u
#define STREAM_REPLY_BYTES    ASTRA_STREAM_DATA_SIZE

static void
fill_header(AstraMessageHeader *header, uint32_t operation, uint32_t size)
{
    astra_message_header_set(header, size, ASTRA_STREAM_SERVICE_PROTOCOL,
                             ASTRA_STREAM_SERVICE_VERSION, operation,
                             astra_activity_current());
}

uint32_t
astra_stream_write_one(uint32_t handle, const void *bytes, uint32_t length)
{
    AstraStreamWrite message;
    const uint8_t *source = bytes;

    if (handle == 0u || bytes == NULL || length == 0u ||
        length > ASTRA_STREAM_WRITE_MAX) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    fill_header(&message.header, ASTRA_STREAM_OPERATION_WRITE,
                (uint32_t)sizeof(message));
    message.length = (uint16_t)length;
    message.reserved = 0u;
    message.activity = astra_activity_current();
    /*
     * The tail is cleared rather than left as stack. The message is a fixed
     * size on the wire, so whatever is past `length` travels to the sink, and
     * a sink that trusted its own buffer would be reading this process's
     * leftovers.
     */
    for (uint32_t index = 0u; index < ASTRA_STREAM_WRITE_MAX; ++index) {
        message.bytes[index] = index < length ? source[index] : 0u;
    }
    return astra_port_send(handle, &message, sizeof(message), NULL, 0u);
}

uint32_t
astra_stream_write(uint32_t handle, const void *bytes, uint32_t length,
                   uint32_t *written)
{
    const uint8_t *source = bytes;
    uint32_t sent = 0u;
    uint32_t status = ASTRA_SYSCALL_OK;

    if (written != NULL) {
        *written = 0u;
    }
    if (handle == 0u || bytes == NULL || written == NULL) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    while (sent < length) {
        uint32_t chunk = length - sent;

        if (chunk > ASTRA_STREAM_WRITE_MAX) {
            chunk = ASTRA_STREAM_WRITE_MAX;
        }
        status = astra_stream_write_one(handle, source + sent, chunk);
        if (status != ASTRA_SYSCALL_OK) {
            break;
        }
        sent += chunk;
        *written = sent;
    }
    return status;
}

uint32_t
astra_stream_write_all(uint32_t handle, const void *bytes, uint32_t length)
{
    uint32_t sent = 0u;

    if (handle == 0u || bytes == NULL) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    if (length == 0u) {
        return ASTRA_SYSCALL_OK;
    }
    /*
     * Yields and retries, because a print that gave up on a busy sink would be
     * a program whose output depends on how loaded the machine was. Only the
     * sink going away ends this -- back pressure is not a failure, and the
     * caller has nothing useful to do with a partial line.
     */
    for (;;) {
        uint32_t moved = 0u;
        uint32_t status = astra_stream_write(handle, (const uint8_t *)bytes +
                                             sent, length - sent, &moved);

        sent += moved;
        if (sent >= length) {
            return ASTRA_SYSCALL_OK;
        }
        if (status != ASTRA_SYSCALL_WOULD_BLOCK) {
            return status;
        }
        (void)astra_yield();
    }
}

uint32_t
astra_print(uint32_t handle, const char *text)
{
    uint32_t length = 0u;

    if (handle == 0u || text == NULL) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    while (text[length] != '\0') {
        ++length;
    }
    return astra_stream_write_all(handle, text, length);
}

uint32_t
astra_print_u32(uint32_t handle, uint32_t value)
{
    char digits[10];
    uint32_t at = sizeof(digits);

    do {
        uint32_t quotient = value / 10u;

        digits[--at] = (char)('0' + value - quotient * 10u);
        value = quotient;
    } while (value != 0u);
    return astra_stream_write_all(handle, &digits[at],
                                  (uint32_t)sizeof(digits) - at);
}

/*
 * The request-and-wait both askers share: create a reply port, attach its send
 * end, wait for the answer. One copy, because the reply port per call and the
 * blocking wait are the two things easiest to get subtly wrong twice.
 */
static uint32_t
exchange_handles(uint32_t handle, const void *request, uint32_t request_size,
                 void *reply, uint32_t reply_size,
                 uint32_t expected_operation, uint32_t *reply_handles,
                 uint32_t reply_handle_capacity, uint32_t *reply_handle_count)
{
    uint32_t handles[1];
    uint32_t receive = 0u;
    uint32_t size = 0u;
    uint32_t status;

    status = astra_rt_port_create(STREAM_REPLY_MESSAGES, reply_size, &receive,
                               &handles[0]);
    if (status != ASTRA_SYSCALL_OK) {
        return status;
    }
    status = astra_port_send(handle, request, request_size, handles, 1u);
    if (status != ASTRA_SYSCALL_OK) {
        /* The send failed, so the handle did not go: both ends are still ours. */
        (void)astra_close(handles[0]);
        (void)astra_close(receive);
        return status;
    }
    for (;;) {
        status = astra_port_receive(receive, reply, reply_size, reply_handles,
                                    reply_handle_capacity, &size,
                                    reply_handle_count);
        if (status == ASTRA_SYSCALL_OK) {
            break;
        }
        if (status != ASTRA_SYSCALL_WOULD_BLOCK) {
            (void)astra_close(receive);
            return status;
        }
        /*
         * Blocked on the reply port, not spinning on it. A yield loop would
         * burn a scheduling slot per pass for as long as somebody takes to
         * type, which on a machine whose shell is also the thing that has to
         * answer is time taken from the answer.
         */
        status = astra_wait_one(receive, ASTRA_DEADLINE_FOREVER, NULL);
        if (status != ASTRA_SYSCALL_OK) {
            (void)astra_close(receive);
            return status;
        }
    }
    (void)astra_close(receive);
    {
        const AstraMessageHeader *header = reply;

        if (size != reply_size ||
            header->total_size != reply_size ||
            header->header_size != ASTRA_MESSAGE_HEADER_SIZE ||
            header->flags != 0u || header->reserved != 0u ||
            header->protocol != ASTRA_STREAM_SERVICE_PROTOCOL ||
            header->protocol_version != ASTRA_STREAM_SERVICE_VERSION ||
            header->operation != expected_operation) {
            if (reply_handles != NULL && reply_handle_count != NULL)
                for (uint32_t index = 0u; index < *reply_handle_count;
                     ++index)
                    (void)astra_close(reply_handles[index]);
            return ASTRA_SYSCALL_INVALID_ARGUMENT;
        }
    }
    return ASTRA_SYSCALL_OK;
}

static uint32_t
exchange(uint32_t handle, const void *request, uint32_t request_size,
         void *reply, uint32_t reply_size, uint32_t expected_operation)
{
    return exchange_handles(handle, request, request_size, reply, reply_size,
                            expected_operation, NULL, 0u, NULL);
}

static uint32_t
ask(uint32_t handle, uint32_t operation, uint32_t capacity, void *reply,
    uint32_t reply_size, uint32_t expected_operation)
{
    AstraStreamRead request;

    fill_header(&request.header, operation, (uint32_t)sizeof(request));
    request.capacity = (uint16_t)capacity;
    request.reserved = 0u;
    request.activity = astra_activity_current();
    return exchange(handle, &request, (uint32_t)sizeof(request), reply,
                    reply_size, expected_operation);
}

uint32_t
astra_stream_size(uint32_t handle, uint32_t *columns, uint32_t *rows)
{
    AstraStreamSize reply;
    uint32_t status;

    if (columns != NULL) {
        *columns = 0u;
    }
    if (rows != NULL) {
        *rows = 0u;
    }
    if (handle == 0u || columns == NULL || rows == NULL) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    status = ask(handle, ASTRA_STREAM_OPERATION_INFO, 0u, &reply,
                 (uint32_t)sizeof(reply), ASTRA_STREAM_OPERATION_SIZE);
    if (status != ASTRA_SYSCALL_OK) {
        return status;
    }
    if (reply.status != ASTRA_SYSCALL_OK) {
        return reply.status;
    }
    /* Zero and zero is an answer: this sink has no geometry. */
    *columns = reply.columns;
    *rows = reply.rows;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_stream_read_ex(uint32_t source, void *bytes, uint32_t capacity,
                     uint32_t *length, uint32_t *flags)
{
    static AstraStreamData reply;
    uint8_t *out = bytes;
    uint32_t status;
    uint32_t give;

    if (length != NULL) {
        *length = 0u;
    }
    if (flags != NULL)
        *flags = 0u;
    if (source == 0u || bytes == NULL || length == NULL || flags == NULL ||
        capacity == 0u) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    if (capacity > ASTRA_STREAM_WRITE_MAX) {
        capacity = ASTRA_STREAM_WRITE_MAX;
    }
    status = ask(source, ASTRA_STREAM_OPERATION_READ, capacity, &reply,
                 (uint32_t)sizeof(reply), ASTRA_STREAM_OPERATION_DATA);
    if (status != ASTRA_SYSCALL_OK) {
        return status;
    }
    if (reply.length > ASTRA_STREAM_WRITE_MAX) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    if ((reply.flags & ~ASTRA_STREAM_DATA_EOF) != 0u ||
        ((reply.flags & ASTRA_STREAM_DATA_EOF) != 0u && reply.length != 0u))
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    if (reply.status != ASTRA_SYSCALL_OK) {
        return reply.status;
    }
    give = reply.length < capacity ? reply.length : capacity;
    for (uint32_t index = 0u; index < give; ++index) {
        out[index] = reply.bytes[index];
    }
    *length = give;
    *flags = reply.flags;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_stream_read(uint32_t source, void *bytes, uint32_t capacity,
                  uint32_t *length)
{
    uint32_t flags;

    return astra_stream_read_ex(source, bytes, capacity, length, &flags);
}

uint32_t
astra_stream_read_wait(uint32_t source, uint32_t *wait_handle,
                       uint32_t *events)
{
    AstraStreamRead request;
    AstraStreamWaitState reply;
    uint32_t received = 0u;
    uint32_t count = 0u;
    uint32_t status;

    if (wait_handle == NULL || events == NULL || source == 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    *wait_handle = 0u;
    *events = 0u;
    fill_header(&request.header, ASTRA_STREAM_OPERATION_READ_WAIT,
                (uint32_t)sizeof(request));
    request.capacity = 0u;
    request.reserved = 0u;
    request.activity = astra_activity_current();
    status = exchange_handles(source, &request, (uint32_t)sizeof(request),
                              &reply, (uint32_t)sizeof(reply),
                              ASTRA_STREAM_OPERATION_WAIT_STATE, &received,
                              1u, &count);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    if (reply.status != ASTRA_SYSCALL_OK || count != 1u || received == 0u) {
        if (received != 0u)
            (void)astra_close(received);
        return reply.status != ASTRA_SYSCALL_OK ? reply.status :
                                                 ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    *wait_handle = received;
    *events = reply.events;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_stream_tty_get(uint32_t handle, AstraTtyState *state)
{
    AstraTtyReply reply;
    uint32_t status;

    if (handle == 0u || state == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    status = ask(handle, ASTRA_STREAM_OPERATION_TTY_GET, 0u, &reply,
                 (uint32_t)sizeof(reply), ASTRA_STREAM_OPERATION_TTY_STATE);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    if (reply.status != ASTRA_SYSCALL_OK)
        return reply.status;
    if (reply.state.reserved8 != 0u || reply.state.generation == 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    *state = reply.state;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_stream_tty_set(uint32_t handle, uint32_t action,
                     const AstraTtyState *state)
{
    AstraTtySet request = {0};
    AstraTtyReply reply;
    uint32_t status;

    if (handle == 0u || state == NULL || action > ASTRA_TTY_FLUSH_BOTH)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    fill_header(&request.header, ASTRA_STREAM_OPERATION_TTY_SET,
                (uint32_t)sizeof(request));
    request.action = (uint16_t)action;
    request.state = *state;
    request.state.reserved8 = 0u;
    status = exchange(handle, &request, (uint32_t)sizeof(request), &reply,
                      (uint32_t)sizeof(reply),
                      ASTRA_STREAM_OPERATION_TTY_STATE);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    return reply.status;
}
