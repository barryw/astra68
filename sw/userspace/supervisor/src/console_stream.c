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
static AstraTerminal *sink_terminal;
static uint32_t sink_send;
static uint32_t sink_receive;
static uint32_t source_send;
static uint32_t source_receive;
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
    astra_terminal_write_bytes(terminal, bytes, length);
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
                              ASTRA_STREAM_READ_SIZE,
                          &source_receive, &source_send) != ASTRA_SYSCALL_OK) {
        (void)astra_close(sink_send);
        (void)astra_close(sink_receive);
        sink_send = 0u;
        sink_receive = 0u;
        return 0;
    }
    sink_terminal = terminal;
    if (!astra_stream_sink_init(&sink, sink_receive, render, sink_terminal) ||
        !astra_stream_source_init(&source, source_receive)) {
        return 0;
    }
    /*
     * How big this sink is, for a program that pages. It comes from the
     * terminal rather than a constant, which is the whole reason the question
     * is a message: a program that assumed 80x24 would be wrong on the 90x30
     * plane this machine actually has.
     */
    astra_stream_sink_size(&sink, terminal->columns, terminal->rows);
    stream_ready = 1;
    return 1;
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

uint32_t
console_stream_offer(const uint8_t *bytes, uint32_t length)
{
    if (!stream_ready) {
        return 0u;
    }
    return astra_stream_source_offer(&source, bytes, length);
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
