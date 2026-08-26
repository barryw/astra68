#ifndef ASTRA_STREAM_H
#define ASTRA_STREAM_H

/*
 * Streams: the client that writes and reads, and the sink and source that
 * answer.
 *
 * The protocol is in astra/stream_service.h and everything here is built on
 * ports, because a launched program cannot call into its parent's address
 * space. That is the whole reason this module exists rather than being another
 * function pointer like the storage service still is.
 *
 * Nothing here is a global. There is no "current output" and no descriptor
 * table: a stream is a handle a program was granted, and a program that writes
 * has to say where. A print with no handle is a print to descriptor 1 wearing a
 * different name, and that is the thing the launch spec's 4.1 refuses.
 */

#include <stdint.h>

#include <astra/stream_service.h>

/*
 * What a sink does with a message. The activity comes across so a line on a
 * terminal and the events emitted around it belong to the same story; a sink
 * rendering to a screen will ignore it and a sink writing to a file will not.
 */
typedef void (*AstraStreamRender)(void *context, const uint8_t *bytes,
                                  uint32_t length, uint32_t activity);

typedef struct AstraStreamSource AstraStreamSource;

/*
 * A text sink. It has no queue of its own: the port *is* the queue, so a writer
 * that finds it full is told ASTRA_SYSCALL_WOULD_BLOCK by the kernel and
 * nothing is lost or duplicated on the way. Adding a second buffer here would
 * add a second place for a message to be dropped.
 */
typedef struct AstraStreamSink {
    uint32_t receive;
    AstraStreamRender render;
    void *context;
    /*
     * How big the far end is, for a program that pages. Zero means no
     * geometry, which is an answer -- a file has none -- rather than a
     * failure, so a program that is redirected simply does not page.
     */
    uint16_t columns;
    uint16_t rows;
    uint16_t pixel_width;
    uint16_t pixel_height;
    uint32_t messages;   /* rendered */
    uint32_t bytes;
    uint32_t refused;    /* received, and not this protocol */
    uint32_t dropped;    /* answered nobody: the reply had nowhere to go */
    AstraTtyState *tty;
    AstraStreamSource *input;
    uint8_t idle;
} AstraStreamSink;

int astra_stream_sink_init(AstraStreamSink *sink, uint32_t receive,
                           AstraStreamRender render, void *context);

/* What this sink tells a program that asks. Zero and zero until it is set. */
void astra_stream_sink_size(AstraStreamSink *sink, uint32_t columns,
                            uint32_t rows, uint32_t pixel_width,
                            uint32_t pixel_height);

/*
 * Drains at most `budget` messages and returns how many it rendered. Bounded
 * because this runs on the loop a person is typing at: a burst costs several
 * passes rather than a stall.
 */
uint32_t astra_stream_sink_pump(AstraStreamSink *sink, uint32_t budget);

/*
 * A text source. Whoever owns the keyboard offers bytes into its caller-owned
 * circular buffer and a reader takes what is there -- possibly none, which is
 * ordinary. The owner chooses and pays for the capacity; the protocol adds no
 * second, arbitrary input ceiling.
 */
struct AstraStreamSource {
    uint32_t receive;
    uint8_t  pending[ASTRA_STREAM_WRITE_MAX];
    uint8_t *buffer;
    uint32_t capacity;
    uint32_t head;
    uint32_t length;
    /* Readable prefix; canonical input after it remains private until EOL. */
    uint32_t committed;
    uint8_t eof_pending;
    uint32_t requests;
    uint32_t refused;
    uint32_t readable_event;
    uint32_t readiness_failures;
    AstraTtyState *tty;
    AstraStreamSink *output;
};

int astra_stream_source_init(AstraStreamSource *source, uint32_t receive);
int astra_stream_source_init_storage(AstraStreamSource *source,
                                     uint32_t receive, void *storage,
                                     uint32_t capacity);

/*
 * Offers bytes to whoever reads next, and returns how many fit. Unread bytes
 * remain ahead of new bytes; the offerer keeps any remainder rather than
 * overwriting input a reader has not collected.
 */
uint32_t astra_stream_source_offer(AstraStreamSource *source,
                                   const uint8_t *bytes, uint32_t length);

/* Master-to-slave input through the shared terminal line discipline. */
uint32_t astra_stream_tty_input(AstraStreamSource *source,
                                const uint8_t *bytes, uint32_t length);

/* Non-zero while a reader has something waiting for it. */
int astra_stream_source_ready(const AstraStreamSource *source);
uint32_t astra_stream_source_space(const AstraStreamSource *source);

uint32_t astra_stream_source_pump(AstraStreamSource *source, uint32_t budget);
void astra_stream_source_destroy(AstraStreamSource *source);

/* Binds both stream directions to one terminal-control state. */
void astra_stream_tty_state_init(AstraTtyState *state);
void astra_stream_tty_bind(AstraStreamSink *output, AstraStreamSource *input,
                           AstraTtyState *state);

/*
 * One message of text, and never part of one. A length past
 * ASTRA_STREAM_WRITE_MAX is refused rather than cut, because a caller that
 * meant to send a line and sent most of it has been told nothing went wrong.
 */
uint32_t astra_stream_write_one(uint32_t handle, const void *bytes,
                                uint32_t length);

/*
 * Any length, as as many messages as it takes. `written` always says how much
 * arrived, including when the answer is ASTRA_SYSCALL_WOULD_BLOCK: that is what
 * makes back pressure lossless, because a caller retries from exactly where the
 * sink stopped taking it.
 */
uint32_t astra_stream_write(uint32_t handle, const void *bytes,
                            uint32_t length, uint32_t *written);

/* Any length, retried through back pressure until all bytes arrive. */
uint32_t astra_stream_write_all(uint32_t handle, const void *bytes,
                                uint32_t length);

/*
 * A line, the way a program wants to write one: it yields and retries until the
 * text is gone or the sink is. Every program will want this, and a print that
 * silently gave up on a busy sink would be a program whose output depends on
 * how loaded the machine was.
 */
uint32_t astra_print(uint32_t handle, const char *text);

/* An unsigned decimal value, without padding or a newline. */
uint32_t astra_print_u32(uint32_t handle, uint32_t value);

/*
 * How big the far end is. Zero and zero is a successful answer meaning "no
 * geometry" -- a sink writing to a file has none -- and a program that paged
 * anyway on that answer would be one that could not be redirected.
 */
uint32_t astra_stream_size(uint32_t handle, uint32_t *columns,
                           uint32_t *rows);

/*
 * At most `capacity` bytes from `source`, the STDIN handle a launch granted.
 * A short read is ordinary -- including a read of nothing, which is what a
 * source with nothing ready answers, and a reader that needs more asks again.
 * The same rule the storage protocol has, for the same reason: a source that
 * decided waiting was better than doing something else would be deciding it
 * for its caller.
 *
 * **A reply port per call, not per reader.** Attaching a handle to a port
 * message *moves* it -- ports are not cloneable, so a handle sent is a handle
 * given away -- which means a cached reply handle works exactly once and then
 * names nothing. Two syscalls per read is what that costs, and reads are
 * human-paced.
 */
uint32_t astra_stream_read(uint32_t source, void *bytes, uint32_t capacity,
                           uint32_t *length);
uint32_t astra_stream_read_ex(uint32_t source, void *bytes, uint32_t capacity,
                              uint32_t *length, uint32_t *flags);

/* Returns a caller-owned wait handle whose signalled state tracks readable
 * input, plus an atomic current-state sample. */
uint32_t astra_stream_read_wait(uint32_t source, uint32_t *wait_handle,
                                uint32_t *events);

uint32_t astra_stream_tty_get(uint32_t handle, AstraTtyState *state);
uint32_t astra_stream_tty_set(uint32_t handle, uint32_t action,
                              const AstraTtyState *state);

#endif
