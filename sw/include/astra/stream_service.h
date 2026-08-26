#ifndef ASTRA_STREAM_SERVICE_H
#define ASTRA_STREAM_SERVICE_H

/*
 * The stream protocol: what a program says to somewhere it can write, and to
 * somewhere it can read.
 *
 * Deliberately smaller than the storage protocol. There is no open, no handle
 * space and no seek, because a stream is not a file: it is a capability that
 * accepts bytes or produces them, and everything a file needs beyond that is
 * what `WORK:` is for.
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

#include <stdint.h>

#include <astra/syscall.h>

#define ASTRA_STREAM_SERVICE_PROTOCOL UINT32_C(0x5354524d) /* STRM */
#define ASTRA_STREAM_SERVICE_VERSION  UINT16_C(3)

/*
 * One message of text. Sized so the whole message stays inside the port's
 * inline limit with room to spare: 24 bytes of header, 8 of its own, and this.
 * A longer line is several messages, which the client does without being asked.
 */
#define ASTRA_STREAM_WRITE_MAX 192u

#define ASTRA_STREAM_OPERATION_WRITE UINT32_C(1)
#define ASTRA_STREAM_OPERATION_READ  UINT32_C(2)
#define ASTRA_STREAM_OPERATION_DATA  UINT32_C(3) /* the reply to a read */
#define ASTRA_STREAM_OPERATION_INFO  UINT32_C(4) /* how big is this thing */
#define ASTRA_STREAM_OPERATION_SIZE  UINT32_C(5) /* the reply to an info */
#define ASTRA_STREAM_OPERATION_TTY_GET UINT32_C(6)
#define ASTRA_STREAM_OPERATION_TTY_STATE UINT32_C(7)
#define ASTRA_STREAM_OPERATION_TTY_SET UINT32_C(8)
#define ASTRA_STREAM_OPERATION_READ_WAIT UINT32_C(9)
#define ASTRA_STREAM_OPERATION_WAIT_STATE UINT32_C(10)

#define ASTRA_STREAM_READY_READ UINT32_C(0x0001)
#define ASTRA_STREAM_DATA_EOF   UINT16_C(0x0001)

/* Stable terminal-state bits. The POSIX personality translates to these. */
#define ASTRA_TTY_IFLAG_BRKINT UINT32_C(0x0001)
#define ASTRA_TTY_IFLAG_ICRNL  UINT32_C(0x0002)
#define ASTRA_TTY_IFLAG_IGNBRK UINT32_C(0x0004)
#define ASTRA_TTY_IFLAG_IGNCR  UINT32_C(0x0008)
#define ASTRA_TTY_IFLAG_IGNPAR UINT32_C(0x0010)
#define ASTRA_TTY_IFLAG_INLCR  UINT32_C(0x0020)
#define ASTRA_TTY_IFLAG_INPCK  UINT32_C(0x0040)
#define ASTRA_TTY_IFLAG_ISTRIP UINT32_C(0x0080)
#define ASTRA_TTY_IFLAG_IXANY  UINT32_C(0x0100)
#define ASTRA_TTY_IFLAG_IXOFF  UINT32_C(0x0200)
#define ASTRA_TTY_IFLAG_IXON   UINT32_C(0x0400)
#define ASTRA_TTY_IFLAG_PARMRK UINT32_C(0x0800)

#define ASTRA_TTY_OFLAG_OPOST  UINT32_C(0x0001)
#define ASTRA_TTY_OFLAG_ONLCR  UINT32_C(0x0002)

#define ASTRA_TTY_CFLAG_CSIZE  UINT32_C(0x000f)
#define ASTRA_TTY_CFLAG_CS8    UINT32_C(0x0008)
#define ASTRA_TTY_CFLAG_CREAD  UINT32_C(0x0020)

#define ASTRA_TTY_LFLAG_ECHO   UINT32_C(0x0001)
#define ASTRA_TTY_LFLAG_ECHOE  UINT32_C(0x0002)
#define ASTRA_TTY_LFLAG_ECHOK  UINT32_C(0x0004)
#define ASTRA_TTY_LFLAG_ECHONL UINT32_C(0x0008)
#define ASTRA_TTY_LFLAG_ICANON UINT32_C(0x0010)
#define ASTRA_TTY_LFLAG_IEXTEN UINT32_C(0x0020)
#define ASTRA_TTY_LFLAG_ISIG   UINT32_C(0x0040)
#define ASTRA_TTY_LFLAG_NOFLSH UINT32_C(0x0080)
#define ASTRA_TTY_LFLAG_TOSTOP UINT32_C(0x0100)

enum {
    ASTRA_TTY_VEOF = 0,
    ASTRA_TTY_VEOL,
    ASTRA_TTY_VERASE,
    ASTRA_TTY_VINTR,
    ASTRA_TTY_VKILL,
    ASTRA_TTY_VMIN,
    ASTRA_TTY_VQUIT,
    ASTRA_TTY_VSTART,
    ASTRA_TTY_VSTOP,
    ASTRA_TTY_VSUSP,
    ASTRA_TTY_VTIME,
    ASTRA_TTY_CONTROL_CHARACTERS
};

enum {
    ASTRA_TTY_APPLY_NOW = 0,
    ASTRA_TTY_APPLY_DRAIN = 1,
    ASTRA_TTY_APPLY_DRAIN_FLUSH_INPUT = 2,
    ASTRA_TTY_FLUSH_INPUT = 3,
    ASTRA_TTY_FLUSH_OUTPUT = 4,
    ASTRA_TTY_FLUSH_BOTH = 5
};

typedef struct AstraTtyState {
    uint32_t input_flags;
    uint32_t output_flags;
    uint32_t control_flags;
    uint32_t local_flags;
    uint8_t control_characters[ASTRA_TTY_CONTROL_CHARACTERS];
    uint8_t reserved8;
    uint16_t columns;
    uint16_t rows;
    uint16_t pixel_width;
    uint16_t pixel_height;
    uint32_t input_speed;
    uint32_t output_speed;
    uint32_t generation;
} AstraTtyState;

/*
 * Writing is fire and forget, with back pressure.
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
    AstraMessageHeader header;
    uint16_t length;             /* bytes of `bytes` that are text */
    uint16_t reserved;
    uint32_t activity;
    uint8_t  bytes[ASTRA_STREAM_WRITE_MAX];
} AstraStreamWrite;

/*
 * Reading is a request and a reply, and the reply needs somewhere to go. The
 * requester attaches a send handle to a port of its own; the source replies to
 * it and drops it. A reply channel that is a capability handed over per request
 * is the same rule as everywhere else here -- the source can answer exactly the
 * caller that asked and nothing else, and it holds no authority afterwards.
 */
typedef struct AstraStreamRead {
    AstraMessageHeader header;
    uint16_t capacity;           /* at most this many bytes, never more */
    uint16_t reserved;
    uint32_t activity;
} AstraStreamRead;

/*
 * What there was. A short reply is ordinary and so is an empty one: a source
 * with nothing ready says so rather than failing, the same short-read rule the
 * storage protocol already has. A reader that needs more asks again.
 */
typedef struct AstraStreamData {
    AstraMessageHeader header;
    uint16_t length;
    uint16_t flags;
    uint32_t status;             /* ASTRA_VFS_OK-style; 0 is fine */
    uint8_t  bytes[ASTRA_STREAM_WRITE_MAX];
} AstraStreamData;

/*
 * How big the far end is, when it has a size at all.
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
    AstraMessageHeader header;
    uint16_t columns;
    uint16_t rows;
    uint32_t status;
} AstraStreamSize;

/* READ_WAIT returns a wait-only event handle plus this current-state sample.
 * The event is manual-reset and remains signalled while input is readable. */
typedef struct AstraStreamWaitState {
    AstraMessageHeader header;
    uint32_t status;
    uint32_t events;
} AstraStreamWaitState;

typedef struct AstraTtySet {
    AstraMessageHeader header;
    uint16_t action;
    uint16_t reserved;
    AstraTtyState state;
} AstraTtySet;

typedef struct AstraTtyReply {
    AstraMessageHeader header;
    uint32_t status;
    AstraTtyState state;
} AstraTtyReply;

#define ASTRA_STREAM_WRITE_SIZE (ASTRA_MESSAGE_HEADER_SIZE + 8u + \
                                 ASTRA_STREAM_WRITE_MAX)
#define ASTRA_STREAM_SIZE_SIZE  (ASTRA_MESSAGE_HEADER_SIZE + 8u)
#define ASTRA_STREAM_READ_SIZE  (ASTRA_MESSAGE_HEADER_SIZE + 8u)
#define ASTRA_STREAM_DATA_SIZE  ASTRA_STREAM_WRITE_SIZE
#define ASTRA_TTY_SET_SIZE      (ASTRA_MESSAGE_HEADER_SIZE + 4u + 48u)
#define ASTRA_TTY_REPLY_SIZE    (ASTRA_MESSAGE_HEADER_SIZE + 4u + 48u)
#define ASTRA_STREAM_WAIT_STATE_SIZE (ASTRA_MESSAGE_HEADER_SIZE + 8u)

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

#endif
