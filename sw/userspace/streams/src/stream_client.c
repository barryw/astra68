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
    header->total_size = size;
    header->header_size = ASTRA_MESSAGE_HEADER_SIZE;
    header->flags = 0u;
    header->protocol = ASTRA_STREAM_SERVICE_PROTOCOL;
    header->protocol_version = ASTRA_STREAM_SERVICE_VERSION;
    header->reserved = 0u;
    header->operation = operation;
    header->transaction_id = astra_activity_current();
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
astra_print(uint32_t handle, const char *text)
{
    uint32_t length = 0u;
    uint32_t sent = 0u;

    if (handle == 0u || text == NULL) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    while (text[length] != '\0') {
        ++length;
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
        uint32_t status = astra_stream_write(handle, (const uint8_t *)text +
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
astra_stream_read(uint32_t source, void *bytes, uint32_t capacity,
                  uint32_t *length)
{
    AstraStreamRead request;
    static AstraStreamData reply;
    uint8_t *out = bytes;
    uint32_t handles[1];
    uint32_t receive = 0u;
    uint32_t size = 0u;
    uint32_t status;
    uint32_t give;

    if (length != NULL) {
        *length = 0u;
    }
    if (source == 0u || bytes == NULL || length == NULL || capacity == 0u) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    if (capacity > ASTRA_STREAM_WRITE_MAX) {
        capacity = ASTRA_STREAM_WRITE_MAX;
    }
    /*
     * A port per read, and this is not a missed optimisation. Attaching a
     * handle to a message *moves* it: ports carry no retain, so they travel
     * through the transfer machinery rather than being copied, and a reply
     * handle kept across calls would name nothing the second time. Two
     * syscalls, on a path a person is typing into.
     */
    status = astra_port_create(STREAM_REPLY_MESSAGES, STREAM_REPLY_BYTES,
                               &receive, &handles[0]);
    if (status != ASTRA_SYSCALL_OK) {
        return status;
    }
    fill_header(&request.header, ASTRA_STREAM_OPERATION_READ,
                (uint32_t)sizeof(request));
    request.capacity = (uint16_t)capacity;
    request.reserved = 0u;
    request.activity = astra_activity_current();
    /*
     * The reply end travels with the request, which is what lets the source
     * answer exactly the caller that asked and hold nothing afterwards.
     */
    status = astra_port_send(source, &request, sizeof(request), handles, 1u);
    if (status != ASTRA_SYSCALL_OK) {
        /* The send failed, so the handle did not go: both ends are still ours. */
        (void)astra_close(handles[0]);
        (void)astra_close(receive);
        return status;
    }
    /*
     * Blocked on the reply port, not spinning on it. A yield loop would burn a
     * scheduling slot per pass for as long as somebody takes to type, which on
     * a machine whose shell is also the thing that has to answer is time taken
     * from the answer. The kernel wakes this thread when the message lands.
     */
    for (;;) {
        status = astra_port_receive(receive, &reply, sizeof(reply), NULL, 0u,
                                    &size, NULL);
        if (status == ASTRA_SYSCALL_OK) {
            break;
        }
        if (status != ASTRA_SYSCALL_WOULD_BLOCK) {
            (void)astra_close(receive);
            return status;
        }
        status = astra_wait_one(receive, ASTRA_DEADLINE_FOREVER, NULL);
        if (status != ASTRA_SYSCALL_OK) {
            (void)astra_close(receive);
            return status;
        }
    }
    (void)astra_close(receive);
    if (size != sizeof(reply) ||
        reply.header.protocol != ASTRA_STREAM_SERVICE_PROTOCOL ||
        reply.header.operation != ASTRA_STREAM_OPERATION_DATA ||
        reply.length > ASTRA_STREAM_WRITE_MAX) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    if (reply.status != ASTRA_SYSCALL_OK) {
        return reply.status;
    }
    give = reply.length < capacity ? reply.length : capacity;
    for (uint32_t index = 0u; index < give; ++index) {
        out[index] = reply.bytes[index];
    }
    *length = give;
    return ASTRA_SYSCALL_OK;
}
