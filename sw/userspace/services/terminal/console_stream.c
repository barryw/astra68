/*
 * The terminal's end of a child's streams.
 *
 * A launched program writes somewhere and reads from somewhere, and on this
 * machine both are the terminal the supervisor already owns. So the supervisor
 * hosts a sink and a source, hands out send handles to them at launch, and
 * pumps both from the loop it already pumps everything else from.
 *
 * This file knows about the terminal model and the stream protocol and nothing
 * else. It does not know what a shell is, which is what will let the terminal
 * service take it whole.
 */

#include <console_stream.h>

#include <astra/runtime.h>
#include <astra/keymap.h>
#include <astra/stream.h>
#include <astra/syscall.h>

#include <stddef.h>

/*
 * Four messages of depth on the sink. It is the back pressure a writer sees, so
 * it is a real choice: one would make a program that writes two lines yield
 * between them, and a deep one would let a program get far enough ahead that
 * what is on the screen is not what it has finished saying. Four is a screen
 * line's worth of chunks in flight.
 */
#define CONSOLE_STREAM_SINK_MESSAGES 4u
#define CONSOLE_STREAM_SOURCE_MESSAGES 2u
#define CONSOLE_STREAM_PUMP_BUDGET 4u

_Static_assert(ASTRA_STREAM_READ_SIZE <= ASTRA_TTY_SET_SIZE,
               "source port sizing must cover every accepted request");

/*
 * How many pumps a drain will spend before giving up. A dead writer cannot
 * refill the sink, so a drain ends on its first empty pass and never reaches
 * this; the ceiling is here because a terminal that could hang relaying is
 * worse than one that drops a line, and a live writer must not be able to hold
 * the shell's prompt hostage by writing forever.
 */
#define CONSOLE_STREAM_DRAIN_MAX 16u

static AstraStreamSink sink;
static AstraStreamSource source;
static AstraTtyState tty;
static AstraTerminal *sink_terminal;
static uint32_t sink_send;
static uint32_t sink_receive;
static uint32_t source_send;
static uint32_t source_receive;
static uint32_t source_area;
static uint8_t *source_storage;
static uint32_t source_storage_size;
static int stream_ready;
/*
 * The other place STDOUT can point. One port, made the first time something
 * asks and kept afterwards, because a shell redirects often and a port per
 * command is handle churn for nothing. `redirect_sink.render` being NULL is
 * what says nothing is redirected right now.
 */
static AstraStreamSink redirect_sink;
static uint32_t redirect_send;
static uint32_t redirect_receive;

/*
 * Straight into the cell model, which is where the shell's own output goes.
 * The activity is dropped here on purpose: a screen has nowhere to put it, and
 * the events emitted around the line already carry it. A sink that wrote to a
 * file would keep it.
 */
static void
render(void *context, const uint8_t *bytes, uint32_t length,
       uint32_t activity)
{
    AstraTerminal *terminal = context;

    (void)activity;
    if (terminal == NULL) {
        return;
    }
    if ((tty.output_flags & ASTRA_TTY_OFLAG_OPOST) == 0u) {
        astra_terminal_write_bytes(terminal, bytes, length);
        return;
    }
    for (uint32_t index = 0u; index < length; ++index) {
        uint8_t value = bytes[index];

        if (value == '\n' &&
            (tty.output_flags & ASTRA_TTY_OFLAG_ONLCR) != 0u)
            astra_terminal_putc(terminal, '\r');
        astra_terminal_putc(terminal, value);
    }
}

int
console_stream_start(AstraTerminal *terminal)
{
    if (stream_ready) {
        return 1;
    }
    if (terminal == NULL) {
        return 0;
    }
    if (astra_rt_port_create(CONSOLE_STREAM_SINK_MESSAGES,
                          CONSOLE_STREAM_SINK_MESSAGES *
                              ASTRA_STREAM_WRITE_SIZE,
                          &sink_receive, &sink_send) != ASTRA_SYSCALL_OK) {
        return 0;
    }
    if (astra_rt_port_create(CONSOLE_STREAM_SOURCE_MESSAGES,
                          CONSOLE_STREAM_SOURCE_MESSAGES *
                              ASTRA_TTY_SET_SIZE,
                          &source_receive, &source_send) != ASTRA_SYSCALL_OK) {
        (void)astra_close(sink_send);
        (void)astra_close(sink_receive);
        sink_send = 0u;
        sink_receive = 0u;
        return 0;
    }
    if (astra_rt_area_create_flagged(
            ASTRA_AREA_SIZE_MAX,
            ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP,
            ASTRA_AREA_CREATE_RESERVED, &source_area) != ASTRA_SYSCALL_OK ||
        astra_rt_area_map(source_area,
                          ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                          (void **)&source_storage,
                          &source_storage_size) != ASTRA_SYSCALL_OK) {
        if (source_area != 0u)
            (void)astra_close(source_area);
        source_area = 0u;
        (void)astra_close(source_send);
        (void)astra_close(source_receive);
        (void)astra_close(sink_send);
        (void)astra_close(sink_receive);
        return 0;
    }
    sink_terminal = terminal;
    if (!astra_stream_sink_init(&sink, sink_receive, render, sink_terminal) ||
        !astra_stream_source_init_storage(&source, source_receive,
                                          source_storage,
                                          source_storage_size)) {
        (void)astra_rt_area_unmap(source_storage);
        source_storage = NULL;
        source_storage_size = 0u;
        (void)astra_close(source_area);
        source_area = 0u;
        (void)astra_close(source_send);
        (void)astra_close(source_receive);
        (void)astra_close(sink_send);
        (void)astra_close(sink_receive);
        source_send = source_receive = sink_send = sink_receive = 0u;
        return 0;
    }
    astra_stream_tty_state_init(&tty);
    astra_stream_tty_bind(&sink, &source, &tty);
    /*
     * How big this sink is, for a program that pages. It comes from the
     * terminal rather than a constant, which is the whole reason the question
     * is a message: a program that assumed 80x24 would be wrong on the 90x30
     * plane this machine actually has.
     */
    astra_stream_sink_size(&sink, terminal->columns, terminal->rows, 0u, 0u);
    stream_ready = 1;
    return 1;
}

void
console_stream_resize(uint32_t columns, uint32_t rows,
                      uint32_t pixel_width, uint32_t pixel_height)
{
    if (stream_ready)
        astra_stream_sink_size(&sink, columns, rows,
                               pixel_width, pixel_height);
}

void
console_stream_tty_state(AstraTtyState *state)
{
    if (state != NULL)
        *state = tty;
}

void
console_stream_tty_restore(const AstraTtyState *state)
{
    if (!stream_ready || state == NULL)
        return;
    {
        uint16_t columns = tty.columns;
        uint16_t rows = tty.rows;
        uint16_t pixel_width = tty.pixel_width;
        uint16_t pixel_height = tty.pixel_height;
        uint32_t generation = tty.generation + 1u;

        tty = *state;
        tty.columns = columns;
        tty.rows = rows;
        tty.pixel_width = pixel_width;
        tty.pixel_height = pixel_height;
        tty.reserved8 = 0u;
        tty.generation = generation != 0u ? generation : 1u;
    }
    source.head = 0u;
    source.length = 0u;
    source.committed = 0u;
    source.eof_pending = 0u;
}

int
console_stream_key(uint32_t key)
{
    static const uint8_t up[] = "\x1b[A";
    static const uint8_t down[] = "\x1b[B";
    static const uint8_t right[] = "\x1b[C";
    static const uint8_t left[] = "\x1b[D";
    static const uint8_t home[] = "\x1b[H";
    static const uint8_t end[] = "\x1b[F";
    static const uint8_t delete_key[] = "\x1b[3~";
    const uint8_t *bytes = NULL;
    uint32_t length = 0u;
    uint8_t one;

    if (!stream_ready)
        return 0;
    if (key != 0u && key < 0x80u) {
        one = (uint8_t)key;
        bytes = &one;
        length = 1u;
    } else {
        switch (key) {
        case ASTRA_KEYMAP_ENTER: one = '\r'; bytes = &one; length = 1u; break;
        case ASTRA_KEYMAP_BACKSPACE:
            one = tty.control_characters[ASTRA_TTY_VERASE];
            bytes = &one;
            length = 1u;
            break;
        case ASTRA_KEYMAP_TAB: one = '\t'; bytes = &one; length = 1u; break;
        case ASTRA_KEYMAP_ESCAPE: one = 0x1bu; bytes = &one; length = 1u; break;
        case ASTRA_KEYMAP_UP: bytes = up; length = sizeof(up) - 1u; break;
        case ASTRA_KEYMAP_DOWN: bytes = down; length = sizeof(down) - 1u; break;
        case ASTRA_KEYMAP_RIGHT: bytes = right; length = sizeof(right) - 1u; break;
        case ASTRA_KEYMAP_LEFT: bytes = left; length = sizeof(left) - 1u; break;
        case ASTRA_KEYMAP_HOME: bytes = home; length = sizeof(home) - 1u; break;
        case ASTRA_KEYMAP_END: bytes = end; length = sizeof(end) - 1u; break;
        case ASTRA_KEYMAP_DELETE:
            bytes = delete_key;
            length = sizeof(delete_key) - 1u;
            break;
        default: return 1;
        }
    }
    if (astra_stream_tty_input(&source, bytes, length) != length)
        return 0;
    return 1;
}

int
console_stream_terminal_reply(void *context, const uint8_t *bytes,
                              uint32_t length)
{
    (void)context;
    return stream_ready && length != 0u &&
           astra_stream_source_offer(&source, bytes, length) == length;
}

int
console_stream_ready(void)
{
    return stream_ready;
}

uint32_t
console_stream_wait_handle(void)
{
    return stream_ready ? sink_receive : 0u;
}

uint32_t
console_stream_input_wait_handle(void)
{
    return stream_ready ? source_receive : 0u;
}

/*
 * One port, two handles. They are separate grants so a launcher can later point
 * them at different things -- which is what makes redirecting one of them a
 * capability operation rather than a shell trick -- and today they are the same
 * send handle because there is one terminal to render into.
 */
uint32_t
console_stream_stdout(void)
{
    if (!stream_ready) {
        return 0u;
    }
    return redirect_sink.render != NULL ? redirect_send : sink_send;
}

int
console_stream_redirect(AstraStreamRender render, void *context)
{
    if (!stream_ready) {
        return 0;
    }
    if (render == NULL) {
        redirect_sink.render = NULL;
        redirect_sink.context = NULL;
        return 1;
    }
    if (redirect_receive == 0u &&
        astra_rt_port_create(CONSOLE_STREAM_SINK_MESSAGES,
                             CONSOLE_STREAM_SINK_MESSAGES *
                                 ASTRA_STREAM_WRITE_SIZE,
                             &redirect_receive, &redirect_send) !=
            ASTRA_SYSCALL_OK) {
        redirect_receive = 0u;
        redirect_send = 0u;
        return 0;
    }
    /*
     * Re-initialised each time rather than kept, so the counters a redirect
     * reports are that redirect's and not every redirect since boot. The
     * geometry stays zero on purpose: a file has none, and a program that asks
     * is told so and does not page.
     */
    if (!astra_stream_sink_init(&redirect_sink, redirect_receive, render,
                                context)) {
        return 0;
    }
    return 1;
}

int
console_stream_redirected(void)
{
    return stream_ready && redirect_sink.render != NULL;
}

uint32_t
console_stream_redirect_wait_handle(void)
{
    return redirect_sink.render != NULL ? redirect_receive : 0u;
}

uint32_t
console_stream_stderr(void)
{
    return stream_ready ? sink_send : 0u;
}

uint32_t
console_stream_stdin(void)
{
    return stream_ready ? source_send : 0u;
}

int
console_stream_pending(void)
{
    return stream_ready && astra_stream_source_ready(&source);
}

uint32_t
console_stream_pump(void)
{
    uint32_t rendered;

    if (!stream_ready) {
        return 0u;
    }
    rendered = astra_stream_sink_pump(&sink, CONSOLE_STREAM_PUMP_BUDGET);
    if (redirect_sink.render != NULL) {
        (void)astra_stream_sink_pump(&redirect_sink,
                                     CONSOLE_STREAM_PUMP_BUDGET);
    }
    (void)astra_stream_source_pump(&source, CONSOLE_STREAM_PUMP_BUDGET);
    return rendered;
}

/*
 * Relays what the sink still holds, and returns when it holds nothing.
 *
 * **A writer's exit does not mean its output has arrived.** The last thing a
 * child wrote is still queued on this port when its process record goes, and a
 * launcher that reported the exit without draining first would print its own
 * account of the child above the child's last words. That is what put
 * "exited 13" above the line explaining why, and a reader who has to
 * reassemble the order is being told something false about it.
 */
uint32_t
console_stream_drain(void)
{
    uint32_t relayed = 0u;

    if (!stream_ready) {
        return 0u;
    }
    for (uint32_t pass = 0u; pass < CONSOLE_STREAM_DRAIN_MAX; ++pass) {
        uint32_t moved = astra_stream_sink_pump(&sink,
                                                CONSOLE_STREAM_PUMP_BUDGET);

        if (redirect_sink.render != NULL) {
            /*
             * The redirected sink drains on the same passes. A child's last
             * write is queued on its port when its process record goes,
             * exactly as it is on the terminal's -- and there it costs a line
             * printed out of order, while here it costs a line missing from
             * the file.
             */
            moved += astra_stream_sink_pump(&redirect_sink,
                                            CONSOLE_STREAM_PUMP_BUDGET);
        }
        if (moved == 0u) {
            break;
        }
        relayed += moved;
    }
    return relayed;
}

uint32_t
console_stream_messages(void)
{
    return stream_ready ? sink.messages : 0u;
}
