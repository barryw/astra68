/*
 * Streams, against a port model rather than a kernel.
 *
 * The three port wrappers are the seam. Replacing them here means the client,
 * the sink and the source are the real code under test -- the message they
 * build, the length they check, what they do when the port is full -- while the
 * thing standing in for the kernel is forty lines that can be read in one go.
 *
 * The sink renders into the real terminal model, because "a write renders" is
 * only worth asserting against the thing that will actually be on the screen.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <astra/runtime.h>
#include <astra/stream.h>
#include <astra/syscall.h>
#include <astra/terminal.h>

#define MOCK_PORT_MAX 8u
#define MOCK_QUEUE_MAX 4u

typedef struct MockPort {
    uint8_t  message[MOCK_QUEUE_MAX][ASTRA_MESSAGE_SIZE_MAX];
    uint32_t size[MOCK_QUEUE_MAX];
    uint32_t attached[MOCK_QUEUE_MAX];
    uint32_t count;
    uint32_t capacity;
    int      open;
    /*
     * Attaching a handle to a message *moves* it. Ports carry no retain, so
     * they travel through the kernel's transfer machinery rather than being
     * copied, and a sender that attached one no longer holds it. Modelling
     * that here is the whole reason this mock is not three lines shorter: a
     * client that cached a reply handle across two reads passed against a mock
     * that copied, and named nothing on the machine.
     */
    int      given_away;
} MockPort;

/* Handle N names port N, both ends. Port 0 is "no handle". */
static MockPort ports[MOCK_PORT_MAX];
static uint32_t next_port = 1u;
static uint32_t mock_activity = 0x5A5A5A5Au;
static uint32_t yields;
static uint32_t closes;
/*
 * The service side runs inside the client's wait. Not a convenience for the
 * test: on the machine the supervisor's loop pumps while a child is blocked
 * inside a call, and serving the child you are waiting for is the whole shape
 * of this architecture.
 */
static AstraStreamSource *served_source;
static AstraStreamSink *served_sink;

static void
mock_reset(void)
{
    memset(ports, 0, sizeof(ports));
    next_port = 1u;
    yields = 0u;
    closes = 0u;
    served_source = NULL;
    served_sink = NULL;
}

static uint32_t
mock_open(uint32_t capacity)
{
    uint32_t handle = next_port++;

    assert(handle < MOCK_PORT_MAX);
    assert(capacity <= MOCK_QUEUE_MAX);
    ports[handle].capacity = capacity;
    ports[handle].open = 1;
    return handle;
}

uint32_t
astra_port_create(uint32_t message_max, uint32_t byte_max,
                  uint32_t *receive_handle, uint32_t *send_handle)
{
    (void)byte_max;
    if (next_port >= MOCK_PORT_MAX) {
        return ASTRA_SYSCALL_RESOURCE_LIMIT;
    }
    *receive_handle = mock_open(message_max == 0u ? 1u : message_max);
    *send_handle = *receive_handle;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_port_send(uint32_t handle, const void *message, uint32_t size,
                const uint32_t *handles, uint32_t handle_count)
{
    MockPort *port;

    if (handle == 0u || handle >= MOCK_PORT_MAX || !ports[handle].open ||
        ports[handle].given_away) {
        return ASTRA_SYSCALL_INVALID_HANDLE;
    }
    port = &ports[handle];
    if (handle_count == 1u &&
        (handles[0] == 0u || handles[0] >= MOCK_PORT_MAX ||
         ports[handles[0]].given_away)) {
        return ASTRA_SYSCALL_INVALID_HANDLE;
    }
    if (port->count >= port->capacity) {
        return ASTRA_SYSCALL_WOULD_BLOCK;
    }
    assert(size <= ASTRA_MESSAGE_SIZE_MAX);
    memcpy(port->message[port->count], message, size);
    port->size[port->count] = size;
    port->attached[port->count] = handle_count == 1u ? handles[0] : 0u;
    if (handle_count == 1u) {
        ports[handles[0]].given_away = 1;   /* moved, not copied */
    }
    ++port->count;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_port_receive(uint32_t handle, void *message, uint32_t capacity,
                   uint32_t *handles, uint32_t handle_capacity, uint32_t *size,
                   uint32_t *handle_count)
{
    MockPort *port;
    uint32_t moved;

    *size = 0u;
    if (handle_count != NULL) {
        *handle_count = 0u;
    }
    if (handle == 0u || handle >= MOCK_PORT_MAX || !ports[handle].open) {
        return ASTRA_SYSCALL_INVALID_HANDLE;
    }
    port = &ports[handle];
    if (port->count == 0u) {
        return ASTRA_SYSCALL_WOULD_BLOCK;
    }
    moved = port->size[0];
    if (moved > capacity) {
        *size = moved;
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    memcpy(message, port->message[0], moved);
    *size = moved;
    if (port->attached[0] != 0u && handle_capacity >= 1u) {
        handles[0] = port->attached[0];
        /* The other end of the move: the receiver now holds it. */
        ports[handles[0]].given_away = 0;
        if (handle_count != NULL) {
            *handle_count = 1u;
        }
    }
    for (uint32_t index = 1u; index < port->count; ++index) {
        memcpy(port->message[index - 1u], port->message[index],
               port->size[index]);
        port->size[index - 1u] = port->size[index];
        port->attached[index - 1u] = port->attached[index];
    }
    --port->count;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_activity_current(void)
{
    return mock_activity;
}

uint32_t
astra_yield(void)
{
    ++yields;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_close(uint32_t handle)
{
    (void)handle;
    ++closes;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_wait_one(uint32_t handle, uint64_t deadline_ns, uint32_t *detail)
{
    (void)deadline_ns;
    if (detail != NULL) {
        *detail = 0u;
    }
    assert(handle != 0u && handle < MOCK_PORT_MAX);
    if (served_source != NULL) {
        (void)astra_stream_source_pump(served_source, 4u);
    }
    if (served_sink != NULL) {
        (void)astra_stream_sink_pump(served_sink, 4u);
    }
    assert(ports[handle].count != 0u &&
           "a wait nothing can wake: the reply was never sent");
    return ASTRA_SYSCALL_OK;
}

/* The sink's other end: the terminal model the shell already owns. */
static AstraTerminal terminal;
static uint32_t rendered_activity;

static int
no_render(void *context, uint32_t row, uint32_t column, const uint8_t *cells,
          uint32_t count)
{
    (void)context;
    (void)row;
    (void)column;
    (void)cells;
    (void)count;
    return 1;
}

static void
to_terminal(void *context, const uint8_t *bytes, uint32_t length,
            uint32_t activity)
{
    (void)context;
    rendered_activity = activity;
    astra_terminal_write_bytes(&terminal, bytes, length);
}

static void
terminal_row(char *out, uint32_t row, uint32_t columns)
{
    uint32_t index;

    for (index = 0u; index < columns; ++index) {
        out[index] = (char)astra_terminal_cell(&terminal, row, index);
    }
    while (index != 0u && out[index - 1u] == ' ') {
        --index;
    }
    out[index] = '\0';
}

static void
test_a_write_reaches_the_terminal(void)
{
    AstraStreamSink sink;
    uint32_t handle;
    uint32_t written = 0u;
    char row[40];

    mock_reset();
    assert(astra_terminal_init(&terminal, 32u, 4u, no_render, NULL) ==
           ASTRA_TERMINAL_OK);
    handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_stream_sink_init(&sink, handle, to_terminal, NULL));

    assert(astra_stream_write(handle, "hello", 5u, &written) ==
           ASTRA_SYSCALL_OK);
    assert(written == 5u);
    /* Nothing renders until somebody pumps: the port is where it waits. */
    terminal_row(row, 0u, 32u);
    assert(row[0] == '\0');

    assert(astra_stream_sink_pump(&sink, 4u) == 1u);
    terminal_row(row, 0u, 32u);
    assert(strcmp(row, "hello") == 0);
    assert(sink.bytes == 5u && sink.messages == 1u && sink.refused == 0u);
    /* The activity travelled with the text, so a line and its events agree. */
    assert(rendered_activity == mock_activity);

    /* A pump with nothing waiting is not an error and renders nothing. */
    assert(astra_stream_sink_pump(&sink, 4u) == 0u);
    assert(sink.messages == 1u);
}

static void
test_a_long_write_is_several_messages_never_a_cut_one(void)
{
    AstraStreamSink sink;
    static char text[ASTRA_STREAM_WRITE_MAX * 2u + 7u];
    uint32_t handle;
    uint32_t written = 0u;

    mock_reset();
    assert(astra_terminal_init(&terminal, 128u, 8u, no_render, NULL) ==
           ASTRA_TERMINAL_OK);
    handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_stream_sink_init(&sink, handle, to_terminal, NULL));
    memset(text, 'x', sizeof(text));

    /*
     * One message is one message. A caller that meant to send more than fits
     * and was told nothing went wrong would have sent most of a line.
     */
    assert(astra_stream_write_one(handle, text, ASTRA_STREAM_WRITE_MAX + 1u) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(ports[handle].count == 0u);
    assert(astra_stream_write_one(handle, text, ASTRA_STREAM_WRITE_MAX) ==
           ASTRA_SYSCALL_OK);
    assert(ports[handle].count == 1u);

    /* The looping form takes any length and says how much went. */
    mock_reset();
    handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_stream_sink_init(&sink, handle, to_terminal, NULL));
    assert(astra_stream_write(handle, text, (uint32_t)sizeof(text),
                              &written) == ASTRA_SYSCALL_OK);
    assert(written == sizeof(text));
    assert(ports[handle].count == 3u);
    assert(astra_stream_sink_pump(&sink, 8u) == 3u);
    assert(sink.bytes == sizeof(text));
}

static void
test_a_full_sink_says_so_and_loses_nothing(void)
{
    AstraStreamSink sink;
    static char text[ASTRA_STREAM_WRITE_MAX * 3u];
    uint32_t handle;
    uint32_t written = 0u;
    uint32_t again = 0u;

    mock_reset();
    assert(astra_terminal_init(&terminal, 128u, 8u, no_render, NULL) ==
           ASTRA_TERMINAL_OK);
    /* A port two messages deep, and three messages to put through it. */
    handle = mock_open(2u);
    assert(astra_stream_sink_init(&sink, handle, to_terminal, NULL));
    memset(text, 'y', sizeof(text));

    assert(astra_stream_write(handle, text, (uint32_t)sizeof(text),
                              &written) == ASTRA_SYSCALL_WOULD_BLOCK);
    /*
     * Back pressure, and the number that makes it lossless: exactly what the
     * sink took, so the caller retries from there rather than from a guess.
     */
    assert(written == ASTRA_STREAM_WRITE_MAX * 2u);
    assert(ports[handle].count == 2u);

    /* Draining makes room, and the rest goes with nothing repeated. */
    assert(astra_stream_sink_pump(&sink, 8u) == 2u);
    assert(astra_stream_write(handle, text + written,
                              (uint32_t)sizeof(text) - written, &again) ==
           ASTRA_SYSCALL_OK);
    assert(again == ASTRA_STREAM_WRITE_MAX);
    assert(astra_stream_sink_pump(&sink, 8u) == 1u);
    assert(sink.bytes == sizeof(text));
}

static void
test_a_print_retries_until_the_text_is_gone(void)
{
    AstraStreamSink sink;
    uint32_t handle;
    char row[64];

    mock_reset();
    assert(astra_terminal_init(&terminal, 48u, 4u, no_render, NULL) ==
           ASTRA_TERMINAL_OK);
    handle = mock_open(1u);
    assert(astra_stream_sink_init(&sink, handle, to_terminal, NULL));

    assert(astra_print(handle, "a line") == ASTRA_SYSCALL_OK);
    assert(astra_stream_sink_pump(&sink, 4u) == 1u);
    terminal_row(row, 0u, 48u);
    assert(strcmp(row, "a line") == 0);

    /* An empty string is nothing to say, not a refusal. */
    assert(astra_print(handle, "") == ASTRA_SYSCALL_OK);
    assert(ports[handle].count == 0u);
}

static void
test_a_stream_nobody_granted_is_a_handle_nobody_has(void)
{
    uint32_t written = 0u;
    uint32_t length = 0xffffffffu;
    uint8_t buffer[16];

    mock_reset();
    /*
     * The whole point of a stream being a capability. A program that was not
     * granted one holds nothing, and nothing is what it gets -- rather than
     * whatever inherited the number.
     */
    assert(astra_stream_write(0u, "x", 1u, &written) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(astra_print(0u, "x") == ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(astra_stream_read(0u, buffer, sizeof(buffer), &length) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(length == 0u);

    /* A handle it holds that names nothing is the kernel's answer, not ours. */
    assert(astra_stream_write_one(MOCK_PORT_MAX - 1u, "x", 1u) ==
           ASTRA_SYSCALL_INVALID_HANDLE);
    assert(astra_stream_read(MOCK_PORT_MAX - 1u, buffer, sizeof(buffer),
                             &length) == ASTRA_SYSCALL_INVALID_HANDLE);
    assert(length == 0u);

    /*
     * And the model itself, because a mock that quietly copied handles is what
     * let a broken client pass once already. A handle that has been attached to
     * a message is a handle this process no longer holds.
     */
    {
        uint32_t carrier = mock_open(2u);
        uint32_t passenger = mock_open(1u);
        AstraStreamWrite message;
        uint32_t attached[1] = {passenger};

        memset(&message, 0, sizeof(message));
        message.header.total_size = (uint32_t)sizeof(message);
        message.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
        assert(astra_port_send(carrier, &message, sizeof(message), attached,
                               1u) == ASTRA_SYSCALL_OK);
        assert(astra_port_send(passenger, &message, sizeof(message), NULL,
                               0u) == ASTRA_SYSCALL_INVALID_HANDLE);
    }
}

static void
test_a_read_gets_what_there_is(void)
{
    AstraStreamSource source;
    uint32_t handle;
    uint32_t length = 0xffffffffu;
    uint8_t buffer[32];

    mock_reset();
    handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_stream_source_init(&source, handle));
    assert(astra_stream_source_offer(&source, (const uint8_t *)"typed", 5u) ==
           5u);
    assert(astra_stream_source_ready(&source));

    /* The source runs inside the reader's wait, the way the machine does it. */
    served_source = &source;

    /* Never more than asked for: three of the five that were offered. */
    assert(astra_stream_read(handle, buffer, 3u, &length) ==
           ASTRA_SYSCALL_OK);
    assert(length == 3u);
    assert(memcmp(buffer, "typ", 3u) == 0);

    /* A source still holding text takes no more: nothing typed is overwritten. */
    assert(astra_stream_source_ready(&source));
    assert(astra_stream_source_offer(&source, (const uint8_t *)"more", 4u) ==
           0u);

    /*
     * A second read, which is the one that caught the reply handle being moved
     * rather than copied. A client that kept its reply port across calls got
     * this far and then sent to a handle it no longer held.
     */
    length = 0xffffffffu;
    assert(astra_stream_read(handle, buffer, sizeof(buffer), &length) ==
           ASTRA_SYSCALL_OK);
    assert(length == 2u);
    assert(memcmp(buffer, "ed", 2u) == 0);
    assert(!astra_stream_source_ready(&source));

    /*
     * Now it takes the next line, because the last one was read. A source is
     * one message deep on purpose: how far behind a reader may fall is the
     * reader's business, not something a buffer here decides for it.
     */
    assert(astra_stream_source_offer(&source, (const uint8_t *)"more", 4u) ==
           4u);
    served_source = NULL;
}

static void
test_a_source_with_nothing_answers_with_nothing(void)
{
    AstraStreamSource source;
    AstraStreamRead request;
    AstraStreamData reply;
    uint32_t handle;
    uint32_t reply_handle;
    uint32_t handles[1];
    uint32_t size = 0u;

    mock_reset();
    handle = mock_open(MOCK_QUEUE_MAX);
    reply_handle = mock_open(1u);
    assert(astra_stream_source_init(&source, handle));
    assert(!astra_stream_source_ready(&source));

    request.header.total_size = (uint32_t)sizeof(request);
    request.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    request.header.flags = 0u;
    request.header.protocol = ASTRA_STREAM_SERVICE_PROTOCOL;
    request.header.protocol_version = ASTRA_STREAM_SERVICE_VERSION;
    request.header.reserved = 0u;
    request.header.operation = ASTRA_STREAM_OPERATION_READ;
    request.header.transaction_id = 1u;
    request.capacity = 16u;
    request.reserved = 0u;
    request.activity = mock_activity;
    handles[0] = reply_handle;
    assert(astra_port_send(handle, &request, sizeof(request), handles, 1u) ==
           ASTRA_SYSCALL_OK);

    assert(astra_stream_source_pump(&source, 4u) == 1u);
    assert(astra_port_receive(reply_handle, &reply, sizeof(reply), NULL, 0u,
                              &size, NULL) == ASTRA_SYSCALL_OK);
    assert(reply.status == ASTRA_SYSCALL_OK);
    assert(reply.length == 0u);

    /*
     * And what that is to a program: a successful read of nothing. Not an
     * error, because a source that failed here would make "no input yet"
     * indistinguishable from "this stream is broken", and a program cannot
     * retry the first and must not retry the second.
     */
    {
        uint8_t buffer[16];
        uint32_t length = 0xffffffffu;

        served_source = &source;
        assert(astra_stream_read(handle, buffer, sizeof(buffer), &length) ==
               ASTRA_SYSCALL_OK);
        assert(length == 0u);
        served_source = NULL;
    }
}

static void
test_a_message_that_is_not_the_protocol_is_counted_not_rendered(void)
{
    AstraStreamSink sink;
    AstraStreamSource source;
    AstraStreamWrite message;
    uint32_t handle;
    char row[40];

    mock_reset();
    assert(astra_terminal_init(&terminal, 32u, 4u, no_render, NULL) ==
           ASTRA_TERMINAL_OK);
    handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_stream_sink_init(&sink, handle, to_terminal, NULL));

    memset(&message, 0, sizeof(message));
    message.header.total_size = (uint32_t)sizeof(message);
    message.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    message.header.protocol = ASTRA_STREAM_SERVICE_PROTOCOL;
    message.header.protocol_version = ASTRA_STREAM_SERVICE_VERSION;
    message.header.operation = ASTRA_STREAM_OPERATION_WRITE;
    /*
     * A length past the bytes that arrived. Believing it would render this
     * process's leftovers as though somebody had sent them, which is why the
     * message's own length is checked against the port's rather than trusted.
     */
    message.length = ASTRA_STREAM_WRITE_MAX + 1u;
    memcpy(message.bytes, "leak", 4u);
    assert(astra_port_send(handle, &message, sizeof(message), NULL, 0u) ==
           ASTRA_SYSCALL_OK);

    /* Another protocol entirely. */
    message.length = 4u;
    message.header.protocol = UINT32_C(0x494e5054); /* INPT */
    assert(astra_port_send(handle, &message, sizeof(message), NULL, 0u) ==
           ASTRA_SYSCALL_OK);

    assert(astra_stream_sink_pump(&sink, 8u) == 0u);
    assert(sink.refused == 2u);
    assert(sink.messages == 0u);
    terminal_row(row, 0u, 32u);
    assert(row[0] == '\0');

    /* A read request with no reply handle is dropped rather than answered. */
    mock_reset();
    handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_stream_source_init(&source, handle));
    {
        AstraStreamRead request;

        memset(&request, 0, sizeof(request));
        request.header.total_size = (uint32_t)sizeof(request);
        request.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
        request.header.protocol = ASTRA_STREAM_SERVICE_PROTOCOL;
        request.header.protocol_version = ASTRA_STREAM_SERVICE_VERSION;
        request.header.operation = ASTRA_STREAM_OPERATION_READ;
        request.capacity = 8u;
        assert(astra_port_send(handle, &request, sizeof(request), NULL, 0u) ==
               ASTRA_SYSCALL_OK);
    }
    assert(astra_stream_source_pump(&source, 4u) == 0u);
    assert(source.refused == 1u);
}

/*
 * How big the far end is, which is the question every pager has to ask and
 * every other machine's programs answer with a guess.
 */
static void
test_a_program_can_ask_how_big_the_screen_is(void)
{
    AstraStreamSink sink;
    uint32_t handle;
    uint32_t columns = 0xffffffffu;
    uint32_t rows = 0xffffffffu;

    mock_reset();
    assert(astra_terminal_init(&terminal, 90u, 30u, no_render, NULL) ==
           ASTRA_TERMINAL_OK);
    handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_stream_sink_init(&sink, handle, to_terminal, NULL));
    astra_stream_sink_size(&sink, terminal.columns, terminal.rows);

    /* The sink answers inside the asker's wait, the way the machine does it. */
    served_sink = &sink;
    assert(astra_stream_size(handle, &columns, &rows) == ASTRA_SYSCALL_OK);
    assert(columns == 90u && rows == 30u);
    served_sink = NULL;

    /*
     * A sink with no geometry -- a file, and a pipe when there is one --
     * answers zero, and that is an answer rather than a failure. A program
     * that treated it as one could not be redirected.
     */
    mock_reset();
    handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_stream_sink_init(&sink, handle, to_terminal, NULL));
    served_sink = &sink;
    columns = 0xffffffffu;
    rows = 0xffffffffu;
    assert(astra_stream_size(handle, &columns, &rows) == ASTRA_SYSCALL_OK);
    assert(columns == 0u && rows == 0u);
    served_sink = NULL;

    /* And a stream nobody granted still has no size to give. */
    assert(astra_stream_size(0u, &columns, &rows) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);
}

int
main(void)
{
    test_a_write_reaches_the_terminal();
    test_a_program_can_ask_how_big_the_screen_is();
    test_a_long_write_is_several_messages_never_a_cut_one();
    test_a_full_sink_says_so_and_loses_nothing();
    test_a_print_retries_until_the_text_is_gone();
    test_a_stream_nobody_granted_is_a_handle_nobody_has();
    test_a_read_gets_what_there_is();
    test_a_source_with_nothing_answers_with_nothing();
    test_a_message_that_is_not_the_protocol_is_counted_not_rendered();
    puts("ASTRA STREAMS PASS");
    return 0;
}
