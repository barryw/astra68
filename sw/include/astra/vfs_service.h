#ifndef ASTRA_VFS_SERVICE_H
#define ASTRA_VFS_SERVICE_H

#include <stdint.h>

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
 * Control records only. ASTRA_MESSAGE_INLINE_MAX is 256 bytes, so the payload
 * a read or write can carry inline is deliberately small. Bulk transfer moves
 * to shared areas and bounded rings, which section 5 of the architecture makes
 * LOCKED; until then a large read is many small ones and the service is
 * correct but not fast. That is the right order to build it in.
 */

#define ASTRA_VFS_PROTOCOL UINT32_C(0x53544f52) /* STOR */
#define ASTRA_VFS_VERSION  UINT16_C(1)

/*
 * The oldest version this build can still speak. A client asks for a minimum
 * and the service replies with the version it chose; when the ranges do not
 * overlap the session is refused rather than downgraded silently.
 */
#define ASTRA_VFS_VERSION_MIN UINT16_C(1)

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
#define ASTRA_VFS_OP_MAX      ASTRA_VFS_OP_UNLINK

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
 */
#define ASTRA_VFS_OK              UINT32_C(0)
#define ASTRA_VFS_ERR_PROTOCOL    UINT32_C(1)  /* malformed or wrong version */
#define ASTRA_VFS_ERR_NOT_FOUND   UINT32_C(2)
#define ASTRA_VFS_ERR_EXISTS      UINT32_C(3)
#define ASTRA_VFS_ERR_NOT_DIR     UINT32_C(4)
#define ASTRA_VFS_ERR_IS_DIR      UINT32_C(5)
#define ASTRA_VFS_ERR_ACCESS      UINT32_C(6)
#define ASTRA_VFS_ERR_NO_SPACE    UINT32_C(7)
#define ASTRA_VFS_ERR_INVALID     UINT32_C(8)
#define ASTRA_VFS_ERR_BAD_HANDLE  UINT32_C(9)
#define ASTRA_VFS_ERR_LIMIT       UINT32_C(10) /* out of sessions or files */
#define ASTRA_VFS_ERR_IO          UINT32_C(11)
#define ASTRA_VFS_ERR_NOT_EMPTY   UINT32_C(12)
#define ASTRA_VFS_ERR_UNSUPPORTED UINT32_C(13)
#define ASTRA_VFS_ERR_BUSY        UINT32_C(14)
/* The caller's buffer cannot hold what the reply carried; not a wire fault. */
#define ASTRA_VFS_ERR_BUFFER_TOO_SMALL UINT32_C(15)

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
#define ASTRA_VFS_REPLY_SIZE   224u

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
    uint32_t flags;         /* open modes, or a readdir index */
    uint64_t offset;
    uint32_t length;        /* bytes for READ/WRITE, at most ASTRA_VFS_IO_MAX */
    uint32_t reserved;      /* must be zero */
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
    uint32_t count;         /* bytes moved, or entries remaining */
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
