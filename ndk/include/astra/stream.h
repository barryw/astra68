#ifndef ASTRA_STREAM_H
#define ASTRA_STREAM_H

/** @file stream.h @brief Capability-based byte streams and terminal control. */

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

/**
 * What a sink does with a message. The activity comes across so a line on a
 * terminal and the events emitted around it belong to the same story; a sink
 * rendering to a screen will ignore it and a sink writing to a file will not.
 * @param context Renderer-defined context.
 * @param bytes Message bytes.
 * @param length Byte count.
 * @param activity Cross-process activity identifier.
 */
typedef void (*AstraStreamRender)(void *context, const uint8_t *bytes,
                                  uint32_t length, uint32_t activity);
/** Deliver a terminal-control signal. @param context Receiver context. @param control ASTRA_TTY_CONTROL_* value. */
typedef void (*AstraStreamTtySignal)(void *context, uint32_t control);

typedef struct AstraStreamSource AstraStreamSource;

/*
 * A text sink. It has no queue of its own: the port *is* the queue, so a writer
 * that finds it full is told ASTRA_SYSCALL_WOULD_BLOCK by the kernel and
 * nothing is lost or duplicated on the way. Adding a second buffer here would
 * add a second place for a message to be dropped.
 */
/** Port-backed output sink and its terminal geometry. */
typedef struct AstraStreamSink {
    uint32_t receive; /**< Sink receive-port handle. */
    AstraStreamRender render; /**< Render callback. */
    void *context; /**< Render callback context. */
    /*
     * How big the far end is, for a program that pages. Zero means no
     * geometry, which is an answer -- a file has none -- rather than a
     * failure, so a program that is redirected simply does not page.
     */
    uint16_t columns; /**< Character columns, or zero. */
    uint16_t rows; /**< Character rows, or zero. */
    uint16_t pixel_width; /**< Pixel width, or zero. */
    uint16_t pixel_height; /**< Pixel height, or zero. */
    uint32_t messages; /**< Rendered message count. */
    uint32_t bytes; /**< Rendered byte count. */
    uint32_t refused; /**< Messages rejected for protocol mismatch. */
    uint32_t dropped; /**< Replies that had no destination. */
    AstraTtyState *tty; /**< Bound line-discipline state. */
    AstraStreamSource *input; /**< Bound terminal input source. */
    uint32_t terminal_id; /**< Terminal identity shared by both directions. */
    uint8_t idle; /**< Nonzero after a pump finds no work. */
} AstraStreamSink;

/** Initialize an output sink. @param sink Sink state. @param receive Receive-port handle. @param render Render callback. @param context Callback context. @return Nonzero on success. */
int astra_stream_sink_init(AstraStreamSink *sink, uint32_t receive,
                           AstraStreamRender render, void *context);

/* What this sink tells a program that asks. Zero and zero until it is set. */
/** Publish sink geometry. @param sink Initialized sink. @param columns Character columns. @param rows Character rows. @param pixel_width Pixel width. @param pixel_height Pixel height. */
void astra_stream_sink_size(AstraStreamSink *sink, uint32_t columns,
                            uint32_t rows, uint32_t pixel_width,
                            uint32_t pixel_height);

/*
 * Drains at most `budget` messages and returns how many it rendered. Bounded
 * because this runs on the loop a person is typing at: a burst costs several
 * passes rather than a stall.
 */
/** Drain a bounded number of output messages. @param sink Initialized sink. @param budget Maximum messages. @return Messages rendered. */
uint32_t astra_stream_sink_pump(AstraStreamSink *sink, uint32_t budget);

/*
 * A text source. Whoever owns the keyboard offers bytes into its caller-owned
 * circular buffer and a reader takes what is there -- possibly none, which is
 * ordinary. The owner chooses and pays for the capacity; the protocol adds no
 * second, arbitrary input ceiling.
 */
/** Port-backed input source with caller-owned buffering. */
struct AstraStreamSource {
    uint32_t receive; /**< Source receive-port handle. */
    uint8_t pending[ASTRA_STREAM_WRITE_MAX]; /**< Pending reply bytes. */
    uint8_t *buffer; /**< Caller-owned circular buffer. */
    uint32_t capacity; /**< Circular-buffer byte capacity. */
    uint32_t head; /**< First unread byte index. */
    uint32_t length; /**< Buffered byte count. */
    /* Readable prefix; canonical input after it remains private until EOL. */
    uint32_t committed; /**< Readable prefix byte count. */
    uint8_t eof_pending; /**< Nonzero when EOF awaits delivery. */
    uint32_t requests; /**< Served read request count. */
    uint32_t refused; /**< Messages rejected for protocol mismatch. */
    uint32_t readable_event; /**< Wait handle tracking readable state. */
    uint32_t readiness_failures; /**< Failed readiness notifications. */
    AstraTtyState *tty; /**< Bound line-discipline state. */
    AstraStreamSink *output; /**< Bound terminal output sink. */
    uint32_t terminal_id; /**< Terminal identity shared by both directions. */
    AstraStreamTtySignal signal; /**< Terminal-control signal callback. */
    void *signal_context; /**< Context passed to signal. */
};

/** Initialize a source with its embedded fallback storage. @param source Source state. @param receive Receive-port handle. @return Nonzero on success. */
int astra_stream_source_init(AstraStreamSource *source, uint32_t receive);
/** Initialize a source with caller-owned storage. @param source Source state. @param receive Receive-port handle. @param storage Circular-buffer bytes. @param capacity Byte capacity. @return Nonzero on success. */
int astra_stream_source_init_storage(AstraStreamSource *source,
                                     uint32_t receive, void *storage,
                                     uint32_t capacity);

/*
 * Offers bytes to whoever reads next, and returns how many fit. Unread bytes
 * remain ahead of new bytes; the offerer keeps any remainder rather than
 * overwriting input a reader has not collected.
 */
/** Offer bytes without overwriting unread input. @param source Initialized source. @param bytes Bytes to append. @param length Byte count. @return Bytes accepted. */
uint32_t astra_stream_source_offer(AstraStreamSource *source,
                                   const uint8_t *bytes, uint32_t length);

/* Master-to-slave input through the shared terminal line discipline. */
/** Apply terminal line discipline to input bytes. @param source Bound terminal source. @param bytes Input bytes. @param length Byte count. @return Bytes accepted. */
uint32_t astra_stream_tty_input(AstraStreamSource *source,
                                const uint8_t *bytes, uint32_t length);

/* Non-zero while a reader has something waiting for it. */
/** Test whether input is readable. @param source Initialized source. @return Nonzero when a read can progress. */
int astra_stream_source_ready(const AstraStreamSource *source);
/** Query free input-buffer space. @param source Initialized source. @return Free bytes. */
uint32_t astra_stream_source_space(const AstraStreamSource *source);

/** Serve bounded read requests. @param source Initialized source. @param budget Maximum requests. @return Requests served. */
uint32_t astra_stream_source_pump(AstraStreamSource *source, uint32_t budget);
/** Release source-owned handles. @param source Initialized source. */
void astra_stream_source_destroy(AstraStreamSource *source);

/* Binds both stream directions to one terminal-control state. */
/** Initialize canonical terminal-control state. @param state State to initialize. */
void astra_stream_tty_state_init(AstraTtyState *state);
/** Bind input and output to one terminal state. @param output Output sink. @param input Input source. @param state Shared terminal state. @param terminal_id Stable terminal identity. */
void astra_stream_tty_bind(AstraStreamSink *output, AstraStreamSource *input,
                           AstraTtyState *state, uint32_t terminal_id);
/** Install terminal signal delivery. @param input Input source. @param signal Signal callback. @param context Callback context. */
void astra_stream_tty_signal(AstraStreamSource *input,
                             AstraStreamTtySignal signal, void *context);

/*
 * One message of text, and never part of one. A length past
 * ASTRA_STREAM_WRITE_MAX is refused rather than cut, because a caller that
 * meant to send a line and sent most of it has been told nothing went wrong.
 */
/** Send one bounded stream message. @param handle Sink send handle. @param bytes Bytes to send. @param length Byte count. @return Astra syscall status. */
uint32_t astra_stream_write_one(uint32_t handle, const void *bytes,
                                uint32_t length);

/*
 * Any length, as as many messages as it takes. `written` always says how much
 * arrived, including when the answer is ASTRA_SYSCALL_WOULD_BLOCK: that is what
 * makes back pressure lossless, because a caller retries from exactly where the
 * sink stopped taking it.
 */
/** Write as much as current back pressure permits. @param handle Sink send handle. @param bytes Bytes to send. @param length Byte count. @param written Receives bytes sent. @return Astra syscall status. */
uint32_t astra_stream_write(uint32_t handle, const void *bytes,
                            uint32_t length, uint32_t *written);

/* Any length, retried through back pressure until all bytes arrive. */
/** Write all bytes while yielding through back pressure. @param handle Sink send handle. @param bytes Bytes to send. @param length Byte count. @return Astra syscall status. */
uint32_t astra_stream_write_all(uint32_t handle, const void *bytes,
                                uint32_t length);

/*
 * A line, the way a program wants to write one: it yields and retries until the
 * text is gone or the sink is. Every program will want this, and a print that
 * silently gave up on a busy sink would be a program whose output depends on
 * how loaded the machine was.
 */
/** Write a NUL-terminated UTF-8 string. @param handle Sink send handle. @param text String to write. @return Astra syscall status. */
uint32_t astra_print(uint32_t handle, const char *text);

/* An unsigned decimal value, without padding or a newline. */
/** Write an unsigned decimal value. @param handle Sink send handle. @param value Value to write. @return Astra syscall status. */
uint32_t astra_print_u32(uint32_t handle, uint32_t value);

/*
 * How big the far end is. Zero and zero is a successful answer meaning "no
 * geometry" -- a sink writing to a file has none -- and a program that paged
 * anyway on that answer would be one that could not be redirected.
 */
/** Query downstream character geometry. @param handle Stream handle. @param columns Receives columns. @param rows Receives rows. @return Astra syscall status. */
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
/** Read currently available bytes. @param source Source send handle. @param bytes Destination. @param capacity Destination bytes. @param length Receives bytes read. @return Astra syscall status. */
uint32_t astra_stream_read(uint32_t source, void *bytes, uint32_t capacity,
                           uint32_t *length);
/** Read bytes and result flags. @param source Source send handle. @param bytes Destination. @param capacity Destination bytes. @param length Receives bytes read. @param flags Receives ASTRA_STREAM_READ_* flags. @return Astra syscall status. */
uint32_t astra_stream_read_ex(uint32_t source, void *bytes, uint32_t capacity,
                              uint32_t *length, uint32_t *flags);

/* Returns a caller-owned wait handle whose signalled state tracks readable
 * input, plus an atomic current-state sample. */
/** Acquire readable-state notification. @param source Source send handle. @param wait_handle Receives caller-owned wait handle. @param events Receives current state. @return Astra syscall status. */
uint32_t astra_stream_read_wait(uint32_t source, uint32_t *wait_handle,
                                uint32_t *events);

/** Read terminal-control state. @param handle Stream handle. @param state Receives state. @return Astra syscall status. */
uint32_t astra_stream_tty_get(uint32_t handle, AstraTtyState *state);
/** Update terminal-control state. @param handle Stream handle. @param action ASTRA_TTY_* action. @param state Requested state. @return Astra syscall status. */
uint32_t astra_stream_tty_set(uint32_t handle, uint32_t action,
                              const AstraTtyState *state);
/** Read stream identity. @param handle Stream handle. @param identity Receives identity. @return Astra syscall status. */
uint32_t astra_stream_identity(uint32_t handle, AstraStreamIdentity *identity);

#endif
