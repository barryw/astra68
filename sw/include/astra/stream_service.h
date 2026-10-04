#ifndef ASTRA_STREAM_SERVICE_H
#define ASTRA_STREAM_SERVICE_H

/**
 * @file stream_service.h
 * @brief Raw stream read/write protocol: what a program says to somewhere
 *        it can write, and to somewhere it can read.
 *
 * Deliberately smaller than the storage protocol. There is no open, no handle
 * space and no seek, because a stream is not a file: it is a capability that
 * accepts bytes or produces them, and everything a file needs beyond that is
 * what `WORK:` is for. This is the raw wire protocol between a client and a
 * sink or source; the NDK's higher-level %stream.h wrapper (astra_print,
 * astra_stream_write, astra_stream_read, and friends) is the API built on
 * top of it.
 *
 * **Streams are capabilities, not numbers.** `STDOUT`, `STDERR` and `STDIN` are
 * grants with names, so a program that was not given `STDIN` does not have one
 * and says so, rather than reading from whatever inherited descriptor 0. An
 * integer table with a dup and a close belongs to the POSIX personality. See
 * the launch spec's 4.1.
 *
 * The three are separate grants pointing at whatever the launcher chose, which
 * is what makes `events > log.txt` a capability operation rather than a shell
 * trick: redirection is handing over a different sink. It is not built yet and
 * nothing here closes the door on it.
 */

#include <astra/compiler.h>
#include <stdint.h>

#include <astra/syscall.h>

/** Native-big-endian `STRM` stream-protocol identifier. */
#define ASTRA_STREAM_SERVICE_PROTOCOL UINT32_C(0x5354524d) /* STRM */
/** Current stream-protocol wire-format version. */
#define ASTRA_STREAM_SERVICE_VERSION  UINT16_C(4)

/** Largest `bytes` payload that still fills the complete port message after
 *  the common and stream headers. */
#define ASTRA_STREAM_WRITE_MAX \
    (ASTRA_MESSAGE_SIZE_MAX - ASTRA_MESSAGE_HEADER_SIZE - 8u)

/** Operation: AstraStreamWrite, fire-and-forget bytes to a sink. */
#define ASTRA_STREAM_OPERATION_WRITE UINT32_C(1)
/** Operation: AstraStreamRead, ask a source for up to `capacity` bytes. */
#define ASTRA_STREAM_OPERATION_READ  UINT32_C(2)
/** Operation: AstraStreamData, the reply to ::ASTRA_STREAM_OPERATION_READ. */
#define ASTRA_STREAM_OPERATION_DATA  UINT32_C(3) /* the reply to a read */
/** Operation: AstraStreamRead, ask a sink how big it is. */
#define ASTRA_STREAM_OPERATION_INFO  UINT32_C(4) /* how big is this thing */
/** Operation: AstraStreamSize, the reply to ::ASTRA_STREAM_OPERATION_INFO. */
#define ASTRA_STREAM_OPERATION_SIZE  UINT32_C(5) /* the reply to an info */
/** Operation: AstraStreamRead, ask a terminal for its current TTY state. */
#define ASTRA_STREAM_OPERATION_TTY_GET UINT32_C(6)
/** Operation: AstraTtyReply, the reply to ::ASTRA_STREAM_OPERATION_TTY_GET
 *  or ::ASTRA_STREAM_OPERATION_TTY_SET. */
#define ASTRA_STREAM_OPERATION_TTY_STATE UINT32_C(7)
/** Operation: AstraTtySet, apply new TTY state to a terminal. */
#define ASTRA_STREAM_OPERATION_TTY_SET UINT32_C(8)
/** Operation: AstraStreamRead, wait for a source to become readable. */
#define ASTRA_STREAM_OPERATION_READ_WAIT UINT32_C(9)
/** Operation: AstraStreamWaitState, the reply to
 *  ::ASTRA_STREAM_OPERATION_READ_WAIT. */
#define ASTRA_STREAM_OPERATION_WAIT_STATE UINT32_C(10)
/** Operation: AstraStreamRead, ask the far end what kind of stream it is. */
#define ASTRA_STREAM_OPERATION_IDENTITY UINT32_C(11)
/** Operation: AstraStreamIdentity, the reply to
 *  ::ASTRA_STREAM_OPERATION_IDENTITY. */
#define ASTRA_STREAM_OPERATION_IDENTITY_REPLY UINT32_C(12)

/** AstraStreamWaitState.events bit: the source currently has data to read. */
#define ASTRA_STREAM_READY_READ UINT32_C(0x0001)
/** AstraStreamData.flags bit: no bytes follow because the source is at
 *  end-of-file. */
#define ASTRA_STREAM_DATA_EOF   UINT16_C(0x0001)

/** AstraStreamIdentity.directions bit: the far end accepts reads. */
#define ASTRA_STREAM_DIRECTION_READ  UINT32_C(0x0001)
/** AstraStreamIdentity.directions bit: the far end accepts writes. */
#define ASTRA_STREAM_DIRECTION_WRITE UINT32_C(0x0002)
/** AstraStreamIdentity.kind bit: the far end is a terminal. */
#define ASTRA_STREAM_KIND_TERMINAL    UINT32_C(0x0001)

/*
 * Stable terminal-state bits. The POSIX personality translates to these, and
 * astra_stream_tty_input() (stream.h) enforces the subset a native sink or
 * source needs to editor-line input and deliver signal characters; the rest
 * travel as stable state for tcgetattr/tcsetattr.
 */
/** input_flags bit: signal an interrupt when a break condition is detected
 *  on the line (standard termios BRKINT; only meaningful when
 *  ::ASTRA_TTY_IFLAG_IGNBRK is clear). */
#define ASTRA_TTY_IFLAG_BRKINT UINT32_C(0x0001)
/** input_flags bit: translate an input CR to NL. */
#define ASTRA_TTY_IFLAG_ICRNL  UINT32_C(0x0002)
/** input_flags bit: ignore a break condition on the line. */
#define ASTRA_TTY_IFLAG_IGNBRK UINT32_C(0x0004)
/** input_flags bit: discard an input CR entirely. */
#define ASTRA_TTY_IFLAG_IGNCR  UINT32_C(0x0008)
/** input_flags bit: ignore a framing or parity error on input. */
#define ASTRA_TTY_IFLAG_IGNPAR UINT32_C(0x0010)
/** input_flags bit: translate an input NL to CR. */
#define ASTRA_TTY_IFLAG_INLCR  UINT32_C(0x0020)
/** input_flags bit: check input parity. */
#define ASTRA_TTY_IFLAG_INPCK  UINT32_C(0x0040)
/** input_flags bit: strip the input byte to seven bits. */
#define ASTRA_TTY_IFLAG_ISTRIP UINT32_C(0x0080)
/** input_flags bit: let any input byte restart output stopped by
 *  ::ASTRA_TTY_IFLAG_IXOFF. */
#define ASTRA_TTY_IFLAG_IXANY  UINT32_C(0x0100)
/** input_flags bit: enable start/stop flow control on output. */
#define ASTRA_TTY_IFLAG_IXOFF  UINT32_C(0x0200)
/** input_flags bit: enable start/stop flow control on input. */
#define ASTRA_TTY_IFLAG_IXON   UINT32_C(0x0400)
/** input_flags bit: mark a parity or framing error in the input stream. */
#define ASTRA_TTY_IFLAG_PARMRK UINT32_C(0x0800)

/** output_flags bit: enable output processing. */
#define ASTRA_TTY_OFLAG_OPOST  UINT32_C(0x0001)
/** output_flags bit: translate an output NL to CR-NL. */
#define ASTRA_TTY_OFLAG_ONLCR  UINT32_C(0x0002)

/** control_flags mask: the character-size field. */
#define ASTRA_TTY_CFLAG_CSIZE  UINT32_C(0x000f)
/** control_flags value within ::ASTRA_TTY_CFLAG_CSIZE: eight-bit characters. */
#define ASTRA_TTY_CFLAG_CS8    UINT32_C(0x0008)
/** control_flags bit: enable the receiver. */
#define ASTRA_TTY_CFLAG_CREAD  UINT32_C(0x0020)

/** local_flags bit: echo input bytes back to the terminal. */
#define ASTRA_TTY_LFLAG_ECHO   UINT32_C(0x0001)
/** local_flags bit: erase the last echoed character on ::ASTRA_TTY_VERASE. */
#define ASTRA_TTY_LFLAG_ECHOE  UINT32_C(0x0002)
/** local_flags bit: echo a newline on ::ASTRA_TTY_VKILL. */
#define ASTRA_TTY_LFLAG_ECHOK  UINT32_C(0x0004)
/** local_flags bit: echo NL even when ::ASTRA_TTY_LFLAG_ECHO is clear. */
#define ASTRA_TTY_LFLAG_ECHONL UINT32_C(0x0008)
/** local_flags bit: canonical (line-buffered, editable) input mode. */
#define ASTRA_TTY_LFLAG_ICANON UINT32_C(0x0010)
/** local_flags bit: enable implementation-defined input processing. */
#define ASTRA_TTY_LFLAG_IEXTEN UINT32_C(0x0020)
/** local_flags bit: recognize INTR/QUIT/SUSP control characters and signal
 *  them instead of passing them through as data. */
#define ASTRA_TTY_LFLAG_ISIG   UINT32_C(0x0040)
/** local_flags bit: do not flush buffered input on INTR/QUIT/SUSP. */
#define ASTRA_TTY_LFLAG_NOFLSH UINT32_C(0x0080)
/** local_flags bit: stop background output to this terminal. */
#define ASTRA_TTY_LFLAG_TOSTOP UINT32_C(0x0100)

/** Index into AstraTtyState::control_characters, one per special
 *  character, plus the sentinel array length. */
enum {
    /** Index of the end-of-file character. */
    ASTRA_TTY_VEOF = 0,
    /** Index of the additional end-of-line character. */
    ASTRA_TTY_VEOL,
    /** Index of the erase-last-character character. */
    ASTRA_TTY_VERASE,
    /** Index of the interrupt character. */
    ASTRA_TTY_VINTR,
    /** Index of the erase-current-line character. */
    ASTRA_TTY_VKILL,
    /** Index of the canonical-mode minimum-bytes-read value. */
    ASTRA_TTY_VMIN,
    /** Index of the quit character. */
    ASTRA_TTY_VQUIT,
    /** Index of the resume-output character. */
    ASTRA_TTY_VSTART,
    /** Index of the suspend-output character. */
    ASTRA_TTY_VSTOP,
    /** Index of the suspend-process character. */
    ASTRA_TTY_VSUSP,
    /** Index of the canonical-mode read-timeout value. */
    ASTRA_TTY_VTIME,
    /** Sentinel: count of entries in control_characters. */
    ASTRA_TTY_CONTROL_CHARACTERS
};

/** AstraTtySet::action: when and how to apply new terminal state. */
enum {
    /** Apply immediately, without waiting for queued output to drain. */
    ASTRA_TTY_APPLY_NOW = 0,
    /** Apply once all queued output has drained. */
    ASTRA_TTY_APPLY_DRAIN = 1,
    /** Apply once output has drained, after discarding unread input. */
    ASTRA_TTY_APPLY_DRAIN_FLUSH_INPUT = 2,
    /** Discard unread input only; state is left unchanged. */
    ASTRA_TTY_FLUSH_INPUT = 3,
    /** Accepted for protocol symmetry with ::ASTRA_TTY_FLUSH_INPUT; state
     *  is left unchanged. A write has nothing queued to discard once it
     *  has reached the terminal. */
    ASTRA_TTY_FLUSH_OUTPUT = 4,
    /** Discard unread input, the same as ::ASTRA_TTY_FLUSH_INPUT; state is
     *  left unchanged. */
    ASTRA_TTY_FLUSH_BOTH = 5
};

/** Complete terminal attributes and geometry for one TTY stream. */
typedef struct AstraTtyState {
    /** Combination of ASTRA_TTY_IFLAG_* input-processing flags. */
    uint32_t input_flags;
    /** Combination of ASTRA_TTY_OFLAG_* output-processing flags. */
    uint32_t output_flags;
    /** Combination of ASTRA_TTY_CFLAG_* hardware-control flags. */
    uint32_t control_flags;
    /** Combination of ASTRA_TTY_LFLAG_* local (line-discipline) flags. */
    uint32_t local_flags;
    /** Special characters, indexed by ASTRA_TTY_V*; zero disables one. */
    uint8_t control_characters[ASTRA_TTY_CONTROL_CHARACTERS];
    /** Reserved for alignment; always zero. */
    uint8_t reserved8;
    /** Terminal width in character columns, or zero if unknown. */
    uint16_t columns;
    /** Terminal height in character rows, or zero if unknown. */
    uint16_t rows;
    /** Terminal width in pixels, or zero if unknown. */
    uint16_t pixel_width;
    /** Terminal height in pixels, or zero if unknown. */
    uint16_t pixel_height;
    /** Input line speed; carried for POSIX compatibility. */
    uint32_t input_speed;
    /** Output line speed; carried for POSIX compatibility. */
    uint32_t output_speed;
    /** Nonzero sequence bumped by every applied state change. */
    uint32_t generation;
} AstraTtyState;

/**
 * Fire-and-forget bytes sent to a sink, with back pressure but no reply.
 *
 * There is no reply, because a reply per line doubles the round trips at 30 MHz
 * and nothing a program does depends on the sink's opinion of its text. A sink
 * that cannot take the message answers ASTRA_SYSCALL_WOULD_BLOCK -- from the
 * port itself, which is already the queue -- and a writer retries. That is the
 * whole of the back pressure and the only answer a writer needs.
 *
 * The activity travels with the text so a line on a terminal and the events
 * emitted around it belong to the same story. A sink is free to ignore it; a
 * sink that writes to a file will not.
 */
typedef struct AstraStreamWrite {
    /** Common header; `operation` is ::ASTRA_STREAM_OPERATION_WRITE. */
    AstraMessageHeader header;
    /** Bytes of `bytes` that are text. */
    uint16_t length;             /* bytes of `bytes` that are text */
    /** Reserved for future use; always zero. */
    uint16_t reserved;
    /** Activity identifier correlating this write with related events. */
    uint32_t activity;
    /** Fixed-size payload; only the first `length` bytes are meaningful. */
    uint8_t  bytes[ASTRA_STREAM_WRITE_MAX];
} AstraStreamWrite;

/**
 * A request and a reply, where the reply needs somewhere to go.
 *
 * The requester attaches a send handle to a port of its own; the source replies to
 * it and drops it. A reply channel that is a capability handed over per request
 * is the same rule as everywhere else here -- the source can answer exactly the
 * caller that asked and nothing else, and it holds no authority afterwards.
 *
 * Reused as the request for every operation that only needs a capacity and
 * a reply channel: ::ASTRA_STREAM_OPERATION_READ,
 * ::ASTRA_STREAM_OPERATION_INFO, ::ASTRA_STREAM_OPERATION_TTY_GET,
 * ::ASTRA_STREAM_OPERATION_READ_WAIT, and ::ASTRA_STREAM_OPERATION_IDENTITY.
 */
typedef struct AstraStreamRead {
    /** Common header; `operation` names which of the shared request kinds
     *  this is. */
    AstraMessageHeader header;
    /** At most this many bytes, never more; unused by operations that do
     *  not read data. */
    uint16_t capacity;           /* at most this many bytes, never more */
    /** Reserved for future use; always zero. */
    uint16_t reserved;
    /** Activity identifier correlating this request with related events. */
    uint32_t activity;
} AstraStreamRead;

/**
 * What there was: the reply to ::ASTRA_STREAM_OPERATION_READ.
 *
 * A short reply is ordinary and so is an empty one: a source
 * with nothing ready says so rather than failing, the same short-read rule the
 * storage protocol already has. A reader that needs more asks again.
 */
typedef struct AstraStreamData {
    /** Common header; `operation` is ::ASTRA_STREAM_OPERATION_DATA. */
    AstraMessageHeader header;
    /** Bytes of `bytes` that are valid; may be less than requested, or
     *  zero. */
    uint16_t length;
    /** Combination of ASTRA_STREAM_DATA_* flags. */
    uint16_t flags;
    /** ASTRA_VFS_OK-style status; 0 is fine. */
    uint32_t status;             /* ASTRA_VFS_OK-style; 0 is fine */
    /** Fixed-size payload; only the first `length` bytes are meaningful. */
    uint8_t  bytes[ASTRA_STREAM_WRITE_MAX];
} AstraStreamData;

/**
 * How big the far end is, when it has a size at all: the reply to
 * ::ASTRA_STREAM_OPERATION_INFO.
 *
 * A program that pages its output has to know how tall the screen is, and the
 * only thing that knows is whatever is rendering it. Asking is one message on a
 * protocol that already exists; the alternative is a program that assumes 80x24
 * and is wrong on every terminal that is not, which is what every other machine
 * gets wrong.
 *
 * A sink with no geometry -- a file, a pipe when there is one -- answers zero
 * for both, and that is an answer: it means "do not page", not "something went
 * wrong". A program that treated it as an error could not be redirected.
 */
typedef struct AstraStreamSize {
    /** Common header; `operation` is ::ASTRA_STREAM_OPERATION_SIZE. */
    AstraMessageHeader header;
    /** Width in character columns, or zero if the sink has no geometry. */
    uint16_t columns;
    /** Height in character rows, or zero if the sink has no geometry. */
    uint16_t rows;
    /** ASTRA_SYSCALL_OK-style status. */
    uint32_t status;
} AstraStreamSize;

/**
 * The reply to ::ASTRA_STREAM_OPERATION_READ_WAIT.
 *
 * Carries a wait-only event handle (attached to the message, not a field
 * here) plus this current-state sample. The event is manual-reset and
 * remains signalled while input is readable.
 */
typedef struct AstraStreamWaitState {
    /** Common header; `operation` is ::ASTRA_STREAM_OPERATION_WAIT_STATE. */
    AstraMessageHeader header;
    /** ASTRA_SYSCALL_OK-style status; the attached event handle is valid
     *  only on success. */
    uint32_t status;
    /** Combination of ASTRA_STREAM_READY_* flags, sampled at reply time. */
    uint32_t events;
} AstraStreamWaitState;

/**
 * Stable identity, separate from terminal attributes and window geometry:
 * the reply to ::ASTRA_STREAM_OPERATION_IDENTITY.
 */
typedef struct AstraStreamIdentity {
    /** Common header; `operation` is
     *  ::ASTRA_STREAM_OPERATION_IDENTITY_REPLY. */
    AstraMessageHeader header;
    /** ASTRA_SYSCALL_OK-style status. */
    uint32_t status;
    /** Combination of ASTRA_STREAM_KIND_* flags. */
    uint32_t kind;
    /** Combination of ASTRA_STREAM_DIRECTION_* flags; never zero. */
    uint32_t directions;
    /** Stable identifier of the underlying object, nonzero only when
     *  ::ASTRA_STREAM_KIND_TERMINAL is set. */
    uint32_t object_id;
} AstraStreamIdentity;

/** Request to apply new terminal state: the request for
 *  ::ASTRA_STREAM_OPERATION_TTY_SET. */
typedef struct AstraTtySet {
    /** Common header; `operation` is ::ASTRA_STREAM_OPERATION_TTY_SET. */
    AstraMessageHeader header;
    /** One of the ASTRA_TTY_APPLY_* or ASTRA_TTY_FLUSH_* values. */
    uint16_t action;
    /** Reserved for future use; always zero. */
    uint16_t reserved;
    /** New state to apply; geometry fields are ignored and left as the
     *  terminal's own. */
    AstraTtyState state;
} AstraTtySet;

/** Reply to ::ASTRA_STREAM_OPERATION_TTY_GET or
 *  ::ASTRA_STREAM_OPERATION_TTY_SET. */
typedef struct AstraTtyReply {
    /** Common header; `operation` is ::ASTRA_STREAM_OPERATION_TTY_STATE. */
    AstraMessageHeader header;
    /** ASTRA_SYSCALL_OK-style status. */
    uint32_t status;
    /** Current state after the request was applied (TTY_SET) or as it
     *  stood (TTY_GET). */
    AstraTtyState state;
} AstraTtyReply;

/** Wire size of AstraStreamWrite. */
#define ASTRA_STREAM_WRITE_SIZE (ASTRA_MESSAGE_HEADER_SIZE + 8u + \
                                 ASTRA_STREAM_WRITE_MAX)
/** Wire size of AstraStreamSize. */
#define ASTRA_STREAM_SIZE_SIZE  (ASTRA_MESSAGE_HEADER_SIZE + 8u)
/** Wire size of AstraStreamRead. */
#define ASTRA_STREAM_READ_SIZE  (ASTRA_MESSAGE_HEADER_SIZE + 8u)
/** Wire size of AstraStreamData. */
#define ASTRA_STREAM_DATA_SIZE  ASTRA_STREAM_WRITE_SIZE
/** Wire size of AstraTtySet. */
#define ASTRA_TTY_SET_SIZE      (ASTRA_MESSAGE_HEADER_SIZE + 4u + 48u)
/** Wire size of AstraTtyReply. */
#define ASTRA_TTY_REPLY_SIZE    (ASTRA_MESSAGE_HEADER_SIZE + 4u + 48u)
/** Wire size of AstraStreamWaitState. */
#define ASTRA_STREAM_WAIT_STATE_SIZE (ASTRA_MESSAGE_HEADER_SIZE + 8u)
/** Wire size of AstraStreamIdentity. */
#define ASTRA_STREAM_IDENTITY_SIZE (ASTRA_MESSAGE_HEADER_SIZE + 16u)

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraStreamWrite) == ASTRA_STREAM_WRITE_SIZE,
               "stream write message ABI size changed");
_Static_assert(sizeof(AstraStreamRead) == ASTRA_STREAM_READ_SIZE,
               "stream read message ABI size changed");
_Static_assert(sizeof(AstraStreamData) == ASTRA_STREAM_DATA_SIZE,
               "stream data message ABI size changed");
_Static_assert(sizeof(AstraStreamSize) == ASTRA_STREAM_SIZE_SIZE,
               "stream size message ABI size changed");
_Static_assert(sizeof(AstraStreamWaitState) == ASTRA_STREAM_WAIT_STATE_SIZE,
               "stream wait-state message ABI size changed");
_Static_assert(sizeof(AstraStreamIdentity) == ASTRA_STREAM_IDENTITY_SIZE,
               "stream identity message ABI size changed");
_Static_assert(sizeof(AstraTtyState) == 48u,
               "terminal state ABI size changed");
_Static_assert(sizeof(AstraTtySet) == ASTRA_TTY_SET_SIZE,
               "terminal set message ABI size changed");
_Static_assert(sizeof(AstraTtyReply) == ASTRA_TTY_REPLY_SIZE,
               "terminal reply message ABI size changed");
_Static_assert(ASTRA_STREAM_WRITE_SIZE <= ASTRA_MESSAGE_SIZE_MAX,
               "a stream message must fit one port message");
_Static_assert(ASTRA_TTY_SET_SIZE <= ASTRA_MESSAGE_SIZE_MAX &&
               ASTRA_TTY_REPLY_SIZE <= ASTRA_MESSAGE_SIZE_MAX,
               "a terminal-control message must fit one port message");
/** @endcond */

#endif
