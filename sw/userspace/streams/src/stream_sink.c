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

static void
source_reset_readable(AstraStreamSource *source)
{
    if (source != NULL && source->readable_event != 0u &&
        astra_rt_event_reset(source->readable_event) != ASTRA_SYSCALL_OK)
        ++source->readiness_failures;
}

static void
source_signal_readable(AstraStreamSource *source)
{
    if (source != NULL && source->readable_event != 0u &&
        astra_rt_signal(source->readable_event, 1u, NULL) != ASTRA_SYSCALL_OK)
        ++source->readiness_failures;
}

void
astra_stream_tty_state_init(AstraTtyState *state)
{
    if (state == NULL)
        return;
    *state = (AstraTtyState){0};
    state->input_flags = ASTRA_TTY_IFLAG_ICRNL;
    state->output_flags = ASTRA_TTY_OFLAG_OPOST | ASTRA_TTY_OFLAG_ONLCR;
    state->control_flags = ASTRA_TTY_CFLAG_CS8 | ASTRA_TTY_CFLAG_CREAD;
    state->local_flags = ASTRA_TTY_LFLAG_ECHO | ASTRA_TTY_LFLAG_ECHOE |
                         ASTRA_TTY_LFLAG_ECHOK | ASTRA_TTY_LFLAG_ICANON |
                         ASTRA_TTY_LFLAG_IEXTEN | ASTRA_TTY_LFLAG_ISIG;
    state->control_characters[ASTRA_TTY_VEOF] = 0x04u;
    state->control_characters[ASTRA_TTY_VERASE] = 0x7fu;
    state->control_characters[ASTRA_TTY_VINTR] = 0x03u;
    state->control_characters[ASTRA_TTY_VKILL] = 0x15u;
    state->control_characters[ASTRA_TTY_VMIN] = 1u;
    state->control_characters[ASTRA_TTY_VQUIT] = 0x1cu;
    state->control_characters[ASTRA_TTY_VSTART] = 0x11u;
    state->control_characters[ASTRA_TTY_VSTOP] = 0x13u;
    state->control_characters[ASTRA_TTY_VSUSP] = 0x1au;
    state->input_speed = 38400u;
    state->output_speed = 38400u;
    state->generation = 1u;
}

void
astra_stream_tty_bind(AstraStreamSink *output, AstraStreamSource *input,
                      AstraTtyState *state)
{
    if (output != NULL) {
        output->tty = state;
        output->input = input;
    }
    if (input != NULL) {
        input->tty = state;
        input->output = output;
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
           header->flags == 0u && header->reserved == 0u &&
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
    sink->pixel_width = 0u;
    sink->pixel_height = 0u;
    sink->messages = 0u;
    sink->bytes = 0u;
    sink->refused = 0u;
    sink->dropped = 0u;
    sink->tty = NULL;
    sink->input = NULL;
    sink->idle = 1u;
    return 1;
}

void
astra_stream_sink_size(AstraStreamSink *sink, uint32_t columns, uint32_t rows,
                       uint32_t pixel_width, uint32_t pixel_height)
{
    if (sink == NULL) {
        return;
    }
    sink->columns = columns > 0xFFFFu ? 0xFFFFu : (uint16_t)columns;
    sink->rows = rows > 0xFFFFu ? 0xFFFFu : (uint16_t)rows;
    sink->pixel_width = pixel_width > 0xFFFFu ?
        0xFFFFu : (uint16_t)pixel_width;
    sink->pixel_height = pixel_height > 0xFFFFu ?
        0xFFFFu : (uint16_t)pixel_height;
    if (sink->tty != NULL &&
        (sink->tty->columns != sink->columns ||
         sink->tty->rows != sink->rows ||
         sink->tty->pixel_width != sink->pixel_width ||
         sink->tty->pixel_height != sink->pixel_height)) {
        sink->tty->columns = sink->columns;
        sink->tty->rows = sink->rows;
        sink->tty->pixel_width = sink->pixel_width;
        sink->tty->pixel_height = sink->pixel_height;
        if (++sink->tty->generation == 0u)
            sink->tty->generation = 1u;
    }
}

static void
reply_header(AstraMessageHeader *header, const AstraMessageHeader *request,
             uint32_t operation, uint32_t size)
{
    astra_message_header_set(header, size, ASTRA_STREAM_SERVICE_PROTOCOL,
                             ASTRA_STREAM_SERVICE_VERSION, operation,
                             request->transaction_id);
}

static void
answer_tty(const AstraMessageHeader *request, uint32_t reply_handle,
           uint32_t status, const AstraTtyState *state, uint32_t *dropped)
{
    AstraTtyReply reply = {0};

    reply_header(&reply.header, request, ASTRA_STREAM_OPERATION_TTY_STATE,
                 (uint32_t)sizeof(reply));
    reply.status = status;
    if (state != NULL)
        reply.state = *state;
    if (astra_port_send(reply_handle, &reply, sizeof(reply), NULL, 0u) !=
        ASTRA_SYSCALL_OK)
        ++*dropped;
}

static int
handle_tty(void *message, uint32_t size, uint32_t reply_handle,
           uint32_t handle_count, AstraTtyState *state,
           AstraStreamSource *input, AstraStreamSink *output,
           uint32_t *dropped)
{
    AstraMessageHeader *header = message;

    if (accept_header(header, size, ASTRA_STREAM_OPERATION_TTY_GET,
                      (uint32_t)sizeof(AstraStreamRead))) {
        if (handle_count == 1u)
            answer_tty(header, reply_handle,
                       state != NULL ? ASTRA_SYSCALL_OK :
                                       ASTRA_SYSCALL_INVALID_ARGUMENT,
                       state, dropped);
        return 1;
    }
    if (accept_header(header, size, ASTRA_STREAM_OPERATION_TTY_SET,
                      (uint32_t)sizeof(AstraTtySet))) {
        AstraTtySet *request = message;
        uint32_t status = ASTRA_SYSCALL_INVALID_ARGUMENT;

        if (handle_count == 1u && state != NULL && request->reserved == 0u &&
            request->state.reserved8 == 0u &&
            request->action <= ASTRA_TTY_FLUSH_BOTH) {
            if ((request->action == ASTRA_TTY_APPLY_DRAIN ||
                 request->action == ASTRA_TTY_APPLY_DRAIN_FLUSH_INPUT) &&
                output != NULL) {
                do {
                    (void)astra_stream_sink_pump(output, 4u);
                } while (!output->idle);
            }
            if ((request->action == ASTRA_TTY_APPLY_DRAIN_FLUSH_INPUT ||
                 request->action == ASTRA_TTY_FLUSH_INPUT ||
                 request->action == ASTRA_TTY_FLUSH_BOTH) && input != NULL) {
                input->head = 0u;
                input->length = 0u;
                input->committed = 0u;
                input->eof_pending = 0u;
                source_reset_readable(input);
            }
            if (request->action <= ASTRA_TTY_APPLY_DRAIN_FLUSH_INPUT) {
                uint16_t columns = state->columns;
                uint16_t rows = state->rows;
                uint16_t pixel_width = state->pixel_width;
                uint16_t pixel_height = state->pixel_height;
                uint32_t generation = state->generation + 1u;

                *state = request->state;
                state->columns = columns;
                state->rows = rows;
                state->pixel_width = pixel_width;
                state->pixel_height = pixel_height;
                state->reserved8 = 0u;
                state->generation = generation != 0u ? generation : 1u;
            }
            /*
             * Output writes become terminal cells when dequeued; there is no
             * device-side transmit queue left to discard at this point.
             */
            status = ASTRA_SYSCALL_OK;
        }
        if (handle_count == 1u)
            answer_tty(header, reply_handle, status, state, dropped);
        return 1;
    }
    return 0;
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

    reply_header(&reply.header, &request->header,
                 ASTRA_STREAM_OPERATION_SIZE, (uint32_t)sizeof(reply));
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
    uint32_t processed = 0u;

    if (sink == NULL || sink->receive == 0u) {
        return 0u;
    }
    while (processed < budget) {
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
            sink->idle = 1u;
            break;
        }
        sink->idle = 0u;
        ++processed;
        if (handle_tty(&message, size, handles[0], handle_count, sink->tty,
                       sink->input, NULL, &sink->dropped)) {
            if (handle_count == 1u)
                (void)astra_close(handles[0]);
            continue;
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
    return source != NULL ? astra_stream_source_init_storage(
        source, receive, source->pending, sizeof(source->pending)) : 0;
}

int
astra_stream_source_init_storage(AstraStreamSource *source, uint32_t receive,
                                 void *storage, uint32_t capacity)
{
    uint32_t readable_event = 0u;

    if (source == NULL || receive == 0u || storage == NULL || capacity == 0u)
        return 0;
    if (astra_rt_event_create(ASTRA_EVENT_MANUAL_RESET,
                              ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT |
                                  ASTRA_RIGHT_TRANSFER |
                                  ASTRA_RIGHT_ADMINISTER,
                              &readable_event) != ASTRA_SYSCALL_OK)
        return 0;
    source->receive = receive;
    source->buffer = storage;
    source->capacity = capacity;
    source->head = 0u;
    source->length = 0u;
    source->committed = 0u;
    source->eof_pending = 0u;
    source->requests = 0u;
    source->refused = 0u;
    source->readable_event = readable_event;
    source->readiness_failures = 0u;
    source->tty = NULL;
    source->output = NULL;
    return 1;
}

void
astra_stream_source_destroy(AstraStreamSource *source)
{
    if (source == NULL)
        return;
    if (source->readable_event != 0u)
        (void)astra_close(source->readable_event);
    source->readable_event = 0u;
    source->receive = 0u;
    source->buffer = NULL;
    source->capacity = 0u;
    source->head = 0u;
    source->length = 0u;
    source->committed = 0u;
    source->eof_pending = 0u;
}

int
astra_stream_source_ready(const AstraStreamSource *source)
{
    return source != NULL &&
        (source->committed != 0u || source->eof_pending != 0u);
}

uint32_t
astra_stream_source_space(const AstraStreamSource *source)
{
    return source != NULL && source->length <= source->capacity ?
        source->capacity - source->length : 0u;
}

uint32_t
astra_stream_source_offer(AstraStreamSource *source, const uint8_t *bytes,
                          uint32_t length)
{
    if (source == NULL || bytes == NULL || length == 0u) {
        return 0u;
    }
    {
        uint32_t available = source->capacity - source->length;
        uint32_t take = length < available ? length : available;
        uint32_t tail = (source->head + source->length) % source->capacity;
        uint32_t first = take < source->capacity - tail ?
            take : source->capacity - tail;

        copy_bytes(source->buffer + tail, bytes, first);
        copy_bytes(source->buffer, bytes + first, take - first);
        int was_ready = astra_stream_source_ready(source);

        source->length += take;
        source->committed += take;
        if (!was_ready && take != 0u)
            source_signal_readable(source);
        return take;
    }
}

static void
tty_echo(AstraStreamSource *source, const uint8_t *bytes, uint32_t length)
{
    if (source->output != NULL && source->output->render != NULL && length != 0u)
        source->output->render(source->output->context, bytes, length,
                               astra_activity_current());
}

static int
tty_control(const AstraTtyState *tty, uint32_t index, uint8_t value)
{
    return tty->control_characters[index] != 0u &&
           tty->control_characters[index] == value;
}

uint32_t
astra_stream_tty_input(AstraStreamSource *source, const uint8_t *bytes,
                       uint32_t length)
{
    AstraTtyState *tty;
    uint32_t consumed = 0u;

    if (source == NULL || bytes == NULL || length == 0u)
        return 0u;
    tty = source->tty;
    if (tty == NULL)
        return astra_stream_source_offer(source, bytes, length);
    while (consumed < length) {
        uint8_t value = bytes[consumed];
        int canonical = (tty->local_flags & ASTRA_TTY_LFLAG_ICANON) != 0u;
        int echo = (tty->local_flags & ASTRA_TTY_LFLAG_ECHO) != 0u;

        if (value == '\r') {
            if ((tty->input_flags & ASTRA_TTY_IFLAG_IGNCR) != 0u) {
                ++consumed;
                continue;
            }
            if ((tty->input_flags & ASTRA_TTY_IFLAG_ICRNL) != 0u)
                value = '\n';
        } else if (value == '\n' &&
                   (tty->input_flags & ASTRA_TTY_IFLAG_INLCR) != 0u) {
            value = '\r';
        }
        if ((tty->input_flags & ASTRA_TTY_IFLAG_ISTRIP) != 0u)
            value &= 0x7fu;

        if (canonical && tty_control(tty, ASTRA_TTY_VERASE, value)) {
            if (source->length > source->committed) {
                static const uint8_t erase[] = "\b \b";

                --source->length;
                if (echo &&
                    (tty->local_flags & ASTRA_TTY_LFLAG_ECHOE) != 0u)
                    tty_echo(source, erase, sizeof(erase) - 1u);
                else if (echo)
                    tty_echo(source, &value, 1u);
            }
            ++consumed;
            continue;
        }
        if (canonical && tty_control(tty, ASTRA_TTY_VKILL, value)) {
            source->length = source->committed;
            if ((tty->local_flags & ASTRA_TTY_LFLAG_ECHOK) != 0u) {
                static const uint8_t newline = '\n';

                tty_echo(source, &newline, 1u);
            }
            ++consumed;
            continue;
        }
        if (canonical && tty_control(tty, ASTRA_TTY_VEOF, value)) {
            int was_ready = astra_stream_source_ready(source);

            if (source->length != source->committed)
                source->committed = source->length;
            else
                source->eof_pending = 1u;
            if (!was_ready)
                source_signal_readable(source);
            ++consumed;
            continue;
        }
        if (source->length == source->capacity)
            break;
        {
            uint32_t tail = (source->head + source->length) % source->capacity;
            int was_ready = astra_stream_source_ready(source);

            source->buffer[tail] = value;
            ++source->length;
            if (!canonical || value == '\n' ||
                tty_control(tty, ASTRA_TTY_VEOL, value))
                source->committed = source->length;
            if (echo || (value == '\n' &&
                         (tty->local_flags & ASTRA_TTY_LFLAG_ECHONL) != 0u))
                tty_echo(source, &value, 1u);
            if (!was_ready && astra_stream_source_ready(source))
                source_signal_readable(source);
        }
        ++consumed;
    }
    return consumed;
}

static void
answer_read_wait(AstraStreamSource *source, const AstraStreamRead *request,
                 uint32_t reply_handle)
{
    AstraStreamWaitState reply = {0};
    uint32_t duplicate = 0u;
    uint32_t status = source->readiness_failures == 0u ?
        astra_rt_handle_duplicate(source->readable_event,
                                  ASTRA_RIGHT_WAIT | ASTRA_RIGHT_TRANSFER,
                                  &duplicate) : ASTRA_SYSCALL_IO_ERROR;

    reply_header(&reply.header, &request->header,
                 ASTRA_STREAM_OPERATION_WAIT_STATE,
                 (uint32_t)sizeof(reply));
    reply.status = status;
    reply.events = astra_stream_source_ready(source) ?
        ASTRA_STREAM_READY_READ : 0u;
    if (astra_port_send(reply_handle, &reply, sizeof(reply),
                        status == ASTRA_SYSCALL_OK ? &duplicate : NULL,
                        status == ASTRA_SYSCALL_OK ? 1u : 0u) !=
        ASTRA_SYSCALL_OK) {
        if (duplicate != 0u)
            (void)astra_close(duplicate);
        ++source->readiness_failures;
    }
}

uint32_t
astra_stream_source_pump(AstraStreamSource *source, uint32_t budget)
{
    static AstraStreamWrite message;
    static AstraStreamData reply;
    uint32_t answered = 0u;
    uint32_t processed = 0u;

    if (source == NULL || source->receive == 0u) {
        return 0u;
    }
    while (processed < budget) {
        uint32_t handles[1] = {0u};
        uint32_t handle_count = 0u;
        uint32_t size = 0u;
        uint32_t available;
        uint32_t give;
        AstraStreamRead *request = (AstraStreamRead *)(void *)&message;
        uint32_t status = astra_port_receive(source->receive, &message,
                                             sizeof(message), handles, 1u,
                                             &size, &handle_count);

        if (status != ASTRA_SYSCALL_OK) {
            break;
        }
        ++processed;
        if (handle_tty(&message, size, handles[0], handle_count, source->tty,
                       source, source->output, &source->refused)) {
            if (handle_count == 1u)
                (void)astra_close(handles[0]);
            continue;
        }
        if (accept_header(&request->header, size,
                          ASTRA_STREAM_OPERATION_READ_WAIT,
                          (uint32_t)sizeof(*request))) {
            if (handle_count == 1u)
                answer_read_wait(source, request, handles[0]);
            else
                ++source->refused;
            if (handle_count == 1u)
                (void)astra_close(handles[0]);
            continue;
        }
        /*
         * No reply handle, no reply. A request that did not say where the
         * answer goes cannot be answered, and there is nothing to close.
         */
        if (!accept_header(&request->header, size,
                           ASTRA_STREAM_OPERATION_READ,
                           (uint32_t)sizeof(*request)) ||
            handle_count != 1u) {
            ++source->refused;
            if (handle_count == 1u) {
                (void)astra_close(handles[0]);
            }
            continue;
        }

        available = source->committed;
        give = request->capacity < available ? request->capacity : available;
        if (give > ASTRA_STREAM_WRITE_MAX) {
            give = ASTRA_STREAM_WRITE_MAX;
        }

        reply_header(&reply.header, &request->header,
                     ASTRA_STREAM_OPERATION_DATA,
                     (uint32_t)sizeof(reply));
        reply.length = (uint16_t)give;
        reply.flags = available == 0u && source->eof_pending != 0u ?
            ASTRA_STREAM_DATA_EOF : 0u;
        reply.status = ASTRA_SYSCALL_OK;
        {
            uint32_t first = give < source->capacity - source->head ?
                give : source->capacity - source->head;

            copy_bytes(reply.bytes, source->buffer + source->head, first);
            copy_bytes(reply.bytes + first, source->buffer, give - first);
        }

        /*
         * The bytes are only spent if the reply left. A send that was refused
         * has told the reader nothing, so consuming here would be a line the
         * machine believes it delivered and nobody received.
         */
        if (astra_port_send(handles[0], &reply, sizeof(reply), NULL, 0u) ==
            ASTRA_SYSCALL_OK) {
            source->head = (source->head + give) % source->capacity;
            source->length -= give;
            source->committed -= give;
            if ((reply.flags & ASTRA_STREAM_DATA_EOF) != 0u)
                source->eof_pending = 0u;
            if (!astra_stream_source_ready(source))
                source_reset_readable(source);
            ++source->requests;
            ++answered;
        }
        (void)astra_close(handles[0]);
    }
    return answered;
}
