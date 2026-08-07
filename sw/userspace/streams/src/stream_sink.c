/*
 * The service half of a stream: a sink that takes text, and a source that
 * produces it.
 *
 * Both are pumps rather than loops. They run inside whichever process hosts
 * them -- the supervisor today, alongside the shell's input read and the event
 * drain -- and a service that blocked in its own receive would stop serving the
 * child it is serving, which is the deadlock this architecture makes easiest to
 * write.
 *
 * Nothing here emits an event. A sink is where text goes when something has
 * gone wrong, and a sink that logged about failing to render would be the
 * logging subsystem taking the machine down by a different route.
 */

#include <astra/stream.h>

#include <astra/runtime.h>
#include <astra/syscall.h>

#include <stddef.h>

static void
copy_bytes(uint8_t *out, const uint8_t *in, uint32_t count)
{
    for (uint32_t index = 0u; index < count; ++index) {
        out[index] = in[index];
    }
}

/*
 * Is this message the protocol, and does its own length agree with the port's?
 * A message whose `length` runs past what arrived would render bytes nobody
 * sent, so the two have to be checked against each other rather than either one
 * being believed.
 */
static int
accept_header(const AstraMessageHeader *header, uint32_t size,
              uint32_t operation, uint32_t expected)
{
    return size == expected && header->total_size == expected &&
           header->header_size == ASTRA_MESSAGE_HEADER_SIZE &&
           header->protocol == ASTRA_STREAM_SERVICE_PROTOCOL &&
           header->protocol_version == ASTRA_STREAM_SERVICE_VERSION &&
           header->operation == operation;
}

int
astra_stream_sink_init(AstraStreamSink *sink, uint32_t receive,
                       AstraStreamRender render, void *context)
{
    if (sink == NULL || receive == 0u || render == NULL) {
        return 0;
    }
    sink->receive = receive;
    sink->render = render;
    sink->context = context;
    sink->columns = 0u;
    sink->rows = 0u;
    sink->messages = 0u;
    sink->bytes = 0u;
    sink->refused = 0u;
    sink->dropped = 0u;
    return 1;
}

void
astra_stream_sink_size(AstraStreamSink *sink, uint32_t columns, uint32_t rows)
{
    if (sink == NULL) {
        return;
    }
    sink->columns = columns > 0xFFFFu ? 0xFFFFu : (uint16_t)columns;
    sink->rows = rows > 0xFFFFu ? 0xFFFFu : (uint16_t)rows;
}

/*
 * Answers "how big are you". The one message here that has a reply, and it has
 * one because the alternative is every program assuming 80x24.
 */
static void
answer_size(AstraStreamSink *sink, const AstraStreamRead *request,
            uint32_t reply_handle)
{
    AstraStreamSize reply;

    reply.header.total_size = (uint32_t)sizeof(reply);
    reply.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    reply.header.flags = 0u;
    reply.header.protocol = ASTRA_STREAM_SERVICE_PROTOCOL;
    reply.header.protocol_version = ASTRA_STREAM_SERVICE_VERSION;
    reply.header.reserved = 0u;
    reply.header.operation = ASTRA_STREAM_OPERATION_SIZE;
    reply.header.transaction_id = request->header.transaction_id;
    reply.columns = sink->columns;
    reply.rows = sink->rows;
    reply.status = ASTRA_SYSCALL_OK;
    if (astra_port_send(reply_handle, &reply, sizeof(reply), NULL, 0u) !=
        ASTRA_SYSCALL_OK) {
        ++sink->dropped;
    }
}

uint32_t
astra_stream_sink_pump(AstraStreamSink *sink, uint32_t budget)
{
    /*
     * Static, not automatic: a user thread gets one 4 KiB stack and this
     * message is 224 bytes of it. The same reason the event drain's batch and
     * the shell's input batch live outside their frames.
     *
     * One buffer for both shapes: a write is the larger of the two, and an
     * info request is read out of its front.
     */
    static AstraStreamWrite message;
    uint32_t rendered = 0u;

    if (sink == NULL || sink->receive == 0u) {
        return 0u;
    }
    while (rendered < budget) {
        uint32_t handles[1] = {0u};
        uint32_t handle_count = 0u;
        uint32_t size = 0u;
        uint32_t status = astra_port_receive(sink->receive, &message,
                                             sizeof(message), handles, 1u,
                                             &size, &handle_count);

        if (status != ASTRA_SYSCALL_OK) {
            /*
             * WOULD_BLOCK is an empty port and the ordinary way out. Anything
             * else is a port that will not serve, and retrying it every pass
             * would spend the machine's time saying nothing.
             */
            break;
        }
        if (accept_header(&message.header, size,
                          ASTRA_STREAM_OPERATION_INFO,
                          (uint32_t)sizeof(AstraStreamRead))) {
            if (handle_count != 1u) {
                /* Asked how big, and did not say where to answer. */
                ++sink->refused;
                continue;
            }
            answer_size(sink, (const AstraStreamRead *)&message, handles[0]);
            (void)astra_close(handles[0]);
            ++rendered;
            continue;
        }
        if (!accept_header(&message.header, size,
                           ASTRA_STREAM_OPERATION_WRITE,
                           (uint32_t)sizeof(message)) ||
            message.length > ASTRA_STREAM_WRITE_MAX || handle_count != 0u) {
            ++sink->refused;
            if (handle_count == 1u) {
                (void)astra_close(handles[0]);
            }
            continue;
        }
        sink->render(sink->context, message.bytes, message.length,
                     message.activity);
        sink->bytes += message.length;
        ++sink->messages;
        ++rendered;
    }
    return rendered;
}

int
astra_stream_source_init(AstraStreamSource *source, uint32_t receive)
{
    if (source == NULL || receive == 0u) {
        return 0;
    }
    source->receive = receive;
    source->length = 0u;
    source->taken = 0u;
    source->requests = 0u;
    source->refused = 0u;
    return 1;
}

int
astra_stream_source_ready(const AstraStreamSource *source)
{
    return source != NULL && source->taken < source->length;
}

uint32_t
astra_stream_source_offer(AstraStreamSource *source, const uint8_t *bytes,
                          uint32_t length)
{
    if (source == NULL || bytes == NULL || length == 0u) {
        return 0u;
    }
    /*
     * Nothing is taken while a reader still has text waiting. Overwriting would
     * lose a line somebody typed, and the offerer is the one holding the rest
     * of it -- so refusing here keeps the whole of what was typed in one place.
     */
    if (astra_stream_source_ready(source)) {
        return 0u;
    }
    if (length > ASTRA_STREAM_WRITE_MAX) {
        length = ASTRA_STREAM_WRITE_MAX;
    }
    copy_bytes(source->pending, bytes, length);
    source->length = (uint16_t)length;
    source->taken = 0u;
    return length;
}

uint32_t
astra_stream_source_pump(AstraStreamSource *source, uint32_t budget)
{
    static AstraStreamRead request;
    static AstraStreamData reply;
    uint32_t answered = 0u;

    if (source == NULL || source->receive == 0u) {
        return 0u;
    }
    while (answered < budget) {
        uint32_t handles[1] = {0u};
        uint32_t handle_count = 0u;
        uint32_t size = 0u;
        uint32_t available;
        uint32_t give;
        uint32_t status = astra_port_receive(source->receive, &request,
                                             sizeof(request), handles, 1u,
                                             &size, &handle_count);

        if (status != ASTRA_SYSCALL_OK) {
            break;
        }
        /*
         * No reply handle, no reply. A request that did not say where the
         * answer goes cannot be answered, and there is nothing to close.
         */
        if (!accept_header(&request.header, size, ASTRA_STREAM_OPERATION_READ,
                           (uint32_t)sizeof(request)) || handle_count != 1u) {
            ++source->refused;
            if (handle_count == 1u) {
                (void)astra_close(handles[0]);
            }
            continue;
        }

        available = (uint32_t)(source->length - source->taken);
        give = request.capacity < available ? request.capacity : available;
        if (give > ASTRA_STREAM_WRITE_MAX) {
            give = ASTRA_STREAM_WRITE_MAX;
        }

        reply.header.total_size = (uint32_t)sizeof(reply);
        reply.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
        reply.header.flags = 0u;
        reply.header.protocol = ASTRA_STREAM_SERVICE_PROTOCOL;
        reply.header.protocol_version = ASTRA_STREAM_SERVICE_VERSION;
        reply.header.reserved = 0u;
        reply.header.operation = ASTRA_STREAM_OPERATION_DATA;
        reply.header.transaction_id = request.header.transaction_id;
        reply.length = (uint16_t)give;
        reply.reserved = 0u;
        reply.status = ASTRA_SYSCALL_OK;
        copy_bytes(reply.bytes, source->pending + source->taken, give);

        /*
         * The bytes are only spent if the reply left. A send that was refused
         * has told the reader nothing, so consuming here would be a line the
         * machine believes it delivered and nobody received.
         */
        if (astra_port_send(handles[0], &reply, sizeof(reply), NULL, 0u) ==
            ASTRA_SYSCALL_OK) {
            source->taken = (uint16_t)(source->taken + give);
            ++source->requests;
            ++answered;
        }
        (void)astra_close(handles[0]);
    }
    return answered;
}
