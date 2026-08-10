#ifndef ASTRA_VFS_SERVICE_H
#define ASTRA_VFS_SERVICE_H

#include <stdint.h>

#include <astra/status.h>
#include <astra/syscall.h>

/*
 * The storage protocol: what a client says to a filesystem service.
 *
 * This is a wire contract, not an API. Nothing here names lwext4, ext4, or any
 * other implementation, and that is the point -- a client compiled against
 * these records keeps working when the filesystem behind them is replaced.
 * See docs/DRIVER_AND_SERVICE_ARCHITECTURE.md sections 9.1 and 9.2 for why the
 * protocol version and the client Kit version are separate numbers.
 *
 * Every record is fixed-width, four-byte aligned, and explicit about its own
 * size, so a mismatched build is refused rather than misread. There are no
 * pointers, no bitfields, and no compiler-native enums: the encoding is
 * big-endian MC68030 today and must not acquire a host dependency.
 *
 * Control records only. ASTRA_MESSAGE_INLINE_MAX is 256 bytes, so inline I/O
 * stays small. Version 3 adds bounded shared-area reads; version 4 adds
 * batched directory replies. Writes remain inline until a measured workload
 * justifies the second bulk operation.
 */

#define ASTRA_VFS_PROTOCOL UINT32_C(0x53544f52) /* STOR */
#define ASTRA_VFS_VERSION  UINT16_C(4)

/*
 * The oldest version this build can still speak. A client asks for a minimum
 * and the service replies with the version it chose; when the ranges do not
 * overlap the session is refused rather than downgraded silently.
 *
 * Version 3 keeps one reply port for the session instead of creating and
 * transferring one for every operation. Version 2 remains accepted so a
 * resident version 2 supervisor can boot a newer storage service and use its
 * per-request reply ports during a rolling image update. Version 1 is not
 * spoken: it would send a directory index where version 2 and later read a
 * backend cursor.
 */
#define ASTRA_VFS_VERSION_MIN UINT16_C(2)

#define ASTRA_VFS_PATH_MAX 192u
#define ASTRA_VFS_NAME_MAX 64u
/*
 * What one message can carry. The header, the reply record and the payload all
 * have to fit ASTRA_MESSAGE_INLINE_MAX together, so this is derived rather
 * than chosen: read replies are the widest record and set the ceiling.
 */
#define ASTRA_VFS_IO_MAX 192u

/* Operations. Values are frozen once published; new ones append. */
#define ASTRA_VFS_OP_HELLO    UINT32_C(1)  /* open a session, agree a version */
#define ASTRA_VFS_OP_BYE      UINT32_C(2)  /* close a session */
#define ASTRA_VFS_OP_OPEN     UINT32_C(3)
#define ASTRA_VFS_OP_CLOSE    UINT32_C(4)
#define ASTRA_VFS_OP_READ     UINT32_C(5)
#define ASTRA_VFS_OP_WRITE    UINT32_C(6)
#define ASTRA_VFS_OP_STAT     UINT32_C(7)
#define ASTRA_VFS_OP_READDIR  UINT32_C(8)
#define ASTRA_VFS_OP_MKDIR    UINT32_C(9)
#define ASTRA_VFS_OP_UNLINK   UINT32_C(10)
#define ASTRA_VFS_OP_BIND_AREA UINT32_C(11)
#define ASTRA_VFS_OP_READ_AREA UINT32_C(12)
#define ASTRA_VFS_OP_READDIR_BATCH UINT32_C(13)
#define ASTRA_VFS_OP_MAX      ASTRA_VFS_OP_READDIR_BATCH

/* One shared-area transfer replaces up to 86 inline READ round trips. */
#define ASTRA_VFS_BULK_MAX 16384u

/* Open modes. */
#define ASTRA_VFS_OPEN_READ     (UINT32_C(1) << 0)
#define ASTRA_VFS_OPEN_WRITE    (UINT32_C(1) << 1)
#define ASTRA_VFS_OPEN_CREATE   (UINT32_C(1) << 2)
#define ASTRA_VFS_OPEN_TRUNCATE (UINT32_C(1) << 3)
#define ASTRA_VFS_OPEN_DIRECTORY (UINT32_C(1) << 4)

/* Node kinds. */
#define ASTRA_VFS_KIND_UNKNOWN   UINT16_C(0)
#define ASTRA_VFS_KIND_FILE      UINT16_C(1)
#define ASTRA_VFS_KIND_DIRECTORY UINT16_C(2)

/*
 * Status values.
 *
 * Deliberately not errno. A protocol that returned the backend's errno would
 * leak which backend is behind it, which is exactly the coupling this whole
 * arrangement exists to prevent, and errno sets differ between implementations
 * anyway. A backend maps its own failures onto these.
 *
 * These are the machine's own status vocabulary rather than a set of their
 * own -- see astra/status.h. The numbers are unchanged and are on the wire;
 * the names stay because callers read them, and both spellings mean one
 * value so the two can never drift apart.
 */
#define ASTRA_VFS_OK              ((uint32_t)ASTRA_STATUS_OK)
#define ASTRA_VFS_ERR_PROTOCOL    ((uint32_t)ASTRA_STATUS_PROTOCOL)
#define ASTRA_VFS_ERR_NOT_FOUND   ((uint32_t)ASTRA_STATUS_NOT_FOUND)
#define ASTRA_VFS_ERR_EXISTS      ((uint32_t)ASTRA_STATUS_EXISTS)
#define ASTRA_VFS_ERR_NOT_DIR     ((uint32_t)ASTRA_STATUS_NOT_DIR)
#define ASTRA_VFS_ERR_IS_DIR      ((uint32_t)ASTRA_STATUS_IS_DIR)
#define ASTRA_VFS_ERR_ACCESS      ((uint32_t)ASTRA_STATUS_ACCESS)
#define ASTRA_VFS_ERR_NO_SPACE    ((uint32_t)ASTRA_STATUS_NO_SPACE)
#define ASTRA_VFS_ERR_INVALID     ((uint32_t)ASTRA_STATUS_INVALID)
#define ASTRA_VFS_ERR_BAD_HANDLE  ((uint32_t)ASTRA_STATUS_BAD_HANDLE)
#define ASTRA_VFS_ERR_LIMIT       ((uint32_t)ASTRA_STATUS_LIMIT)
#define ASTRA_VFS_ERR_IO          ((uint32_t)ASTRA_STATUS_IO)
#define ASTRA_VFS_ERR_NOT_EMPTY   ((uint32_t)ASTRA_STATUS_NOT_EMPTY)
#define ASTRA_VFS_ERR_UNSUPPORTED ((uint32_t)ASTRA_STATUS_UNSUPPORTED)
#define ASTRA_VFS_ERR_BUSY        ((uint32_t)ASTRA_STATUS_BUSY)
/* The caller's buffer cannot hold what the reply carried; not a wire fault. */
#define ASTRA_VFS_ERR_BUFFER_TOO_SMALL ((uint32_t)ASTRA_STATUS_BUFFER_TOO_SMALL)
/*
 * The service is gone. Only a transport that crosses a process can produce
 * this -- a local call cannot fail to be delivered -- and it is the one thing
 * a caller needs from a transport that it cannot get from a reply.
 */
#define ASTRA_VFS_ERR_PEER        ((uint32_t)ASTRA_STATUS_PEER_DEAD)

/*
 * A file handle is a slot index plus a generation, so a stale handle is
 * refused rather than reused. Same rule the kernel applies to its own handles;
 * a service that skipped it would let a client reach whatever now occupies
 * the slot it used to own.
 */
typedef uint32_t AstraVfsFile;
#define ASTRA_VFS_FILE_INVALID UINT32_C(0)

typedef uint32_t AstraVfsSession;
#define ASTRA_VFS_SESSION_INVALID UINT32_C(0)

#define ASTRA_VFS_REQUEST_SIZE 224u
#define ASTRA_VFS_REPLY_SIZE   232u

/*
 * One request record covers every operation. A union of per-operation records
 * would save a few bytes on the wire and cost a decode step that has to be
 * right for every operation forever; a fixed record is checked once.
 */
/*
 * The trailing bytes are a path or a payload, never both: every operation is
 * addressed either by path or by an already-open handle, and no operation is
 * addressed by both. Spelling that as a union rather than reusing one array
 * keeps the two validation rules apart -- a path must be NUL-terminated inside
 * the record, and payload bytes must not be required to contain a NUL at all,
 * which is what a binary write of exactly ASTRA_VFS_IO_MAX bytes looks like.
 *
 * Both arms are the same length, so the union adds no padding and the record
 * size does not depend on which arm a build happens to touch first.
 */
typedef union AstraVfsBody {
    uint8_t path[ASTRA_VFS_PATH_MAX];   /* path-addressed operations */
    uint8_t payload[ASTRA_VFS_IO_MAX];  /* WRITE */
} AstraVfsBody;

_Static_assert(ASTRA_VFS_PATH_MAX == ASTRA_VFS_IO_MAX,
               "VFS body arms must match or the record size becomes ambiguous");

typedef struct AstraVfsRequest {
    uint16_t size;          /* ASTRA_VFS_REQUEST_SIZE */
    uint16_t version;       /* the version the sender is speaking */
    uint32_t session;       /* ASTRA_VFS_SESSION_INVALID on HELLO */
    uint32_t file;          /* the subject handle, or ASTRA_VFS_FILE_INVALID */
    uint32_t flags;         /* open modes */
    /*
     * Where in the node to start: bytes for READ and WRITE, and for READDIR
     * the backend's own cursor into the directory, zero to begin a scan. A
     * directory is read from a position like everything else here; what
     * differs is that only the backend can say what the next position is, so
     * the reply carries it back.
     */
    uint64_t offset;
    uint32_t length;        /* bytes for READ/WRITE, at most ASTRA_VFS_IO_MAX */
    /*
     * What the caller was doing when it asked. The Kit fills this from the
     * calling thread's current activity and the service adopts it for the
     * duration of handling, so one request is one story across every process
     * it touches -- and no caller writes correlation code to get it.
     *
     * Was `reserved, must be zero`, and zero still means no activity.
     */
    uint32_t activity;
    AstraVfsBody body;
} AstraVfsRequest;

_Static_assert(sizeof(AstraVfsRequest) == ASTRA_VFS_REQUEST_SIZE,
               "VFS request ABI size changed");

typedef struct AstraVfsReply {
    uint16_t size;          /* ASTRA_VFS_REPLY_SIZE */
    uint16_t version;       /* the version the service chose */
    uint32_t status;        /* ASTRA_VFS_OK or an ASTRA_VFS_ERR_* */
    uint32_t session;
    uint32_t file;
    uint64_t node_size;
    /*
     * READDIR: the cursor that reaches the entry after this one, to pass as
     * the next request's offset. Zero on every other operation.
     *
     * A field of its own rather than a second meaning for `node_size`: a
     * listing that resumes from the wrong number silently skips or repeats
     * entries, and that is not a bug anybody finds by reading a struct whose
     * fields mean two things.
     */
    uint64_t cursor;
    uint32_t count;         /* bytes moved, or the entry name's length */
    uint16_t kind;
    uint16_t reserved;      /* must be zero */
    uint8_t payload[ASTRA_VFS_IO_MAX];
} AstraVfsReply;

_Static_assert(sizeof(AstraVfsReply) == ASTRA_VFS_REPLY_SIZE,
               "VFS reply ABI size changed");

typedef struct AstraVfsRequestMessage {
    AstraMessageHeader header;
    AstraVfsRequest request;
} AstraVfsRequestMessage;

typedef struct AstraVfsReplyMessage {
    AstraMessageHeader header;
    AstraVfsReply reply;
} AstraVfsReplyMessage;

/*
 * The whole point of the derived ASTRA_VFS_IO_MAX above: if either message
 * outgrows what a port will carry, this fails the build rather than the boot.
 */
_Static_assert(sizeof(AstraVfsRequestMessage) <= ASTRA_MESSAGE_SIZE_MAX,
               "VFS request message exceeds the port message limit");
_Static_assert(sizeof(AstraVfsReplyMessage) <= ASTRA_MESSAGE_SIZE_MAX,
               "VFS reply message exceeds the port message limit");

#endif
