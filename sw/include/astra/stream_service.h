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
#define ASTRA_STREAM_SERVICE_VERSION  UINT16_C(1)

/*
 * One message of text. Sized so the whole message stays inside the port's
 * inline limit with room to spare: 24 bytes of header, 8 of its own, and this.
 * A longer line is several messages, which the client does without being asked.
 */
#define ASTRA_STREAM_WRITE_MAX 192u

#define ASTRA_STREAM_OPERATION_WRITE UINT32_C(1)
#define ASTRA_STREAM_OPERATION_READ  UINT32_C(2)
#define ASTRA_STREAM_OPERATION_DATA  UINT32_C(3) /* the reply to a read */

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
    uint16_t reserved;
    uint32_t status;             /* ASTRA_VFS_OK-style; 0 is fine */
    uint8_t  bytes[ASTRA_STREAM_WRITE_MAX];
} AstraStreamData;

#define ASTRA_STREAM_WRITE_SIZE (ASTRA_MESSAGE_HEADER_SIZE + 8u + \
                                 ASTRA_STREAM_WRITE_MAX)
#define ASTRA_STREAM_READ_SIZE  (ASTRA_MESSAGE_HEADER_SIZE + 8u)
#define ASTRA_STREAM_DATA_SIZE  ASTRA_STREAM_WRITE_SIZE

_Static_assert(sizeof(AstraStreamWrite) == ASTRA_STREAM_WRITE_SIZE,
               "stream write message ABI size changed");
_Static_assert(sizeof(AstraStreamRead) == ASTRA_STREAM_READ_SIZE,
               "stream read message ABI size changed");
_Static_assert(sizeof(AstraStreamData) == ASTRA_STREAM_DATA_SIZE,
               "stream data message ABI size changed");
_Static_assert(ASTRA_STREAM_WRITE_SIZE <= ASTRA_MESSAGE_SIZE_MAX,
               "a stream message must fit one port message");

#endif
