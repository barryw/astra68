#ifndef ASTRA_VFS_SERVICE_H
#define ASTRA_VFS_SERVICE_H

/**
 * @file vfs_service.h
 * @brief Wire contract for the storage protocol between a client and a
 *        filesystem service.
 *
 * See the overview comment below for the protocol's rationale, and
 * docs/DRIVER_AND_SERVICE_ARCHITECTURE.md sections 9.1 and 9.2 for why the
 * protocol version and the client Kit version are separate numbers.
 */

#include <astra/compiler.h>
#include <stddef.h>
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
 * big-endian MC68040 and must not acquire a host dependency.
 *
 * Control records only. Inline I/O is deliberately smaller than the complete
 * message payload; bulk data uses shared areas. Version 3 adds bounded
 * shared-area reads; version 4 adds batched directory replies. Version 9 adds
 * shared-area writes after event persistence measured one service round trip
 * per 72-byte stored record.
 */

/** Native-big-endian `STOR` storage-protocol signature. */
#define ASTRA_VFS_PROTOCOL UINT32_C(0x53544f52) /* STOR */
/** Current storage-protocol wire-format version. */
#define ASTRA_VFS_VERSION  UINT16_C(39)

/** Current version only: in-tree clients and providers change together.
 * Version 38 carries complete pathname operands in the bound transfer area.
 * Version 37 carried full-width native volume identity. A mismatch
 * fails closed; no rolling-upgrade or obsolete request format is supported. */
#define ASTRA_VFS_VERSION_MIN UINT16_C(39)

/** Maximum byte length of a path, including its terminating NUL, on the wire. */
#define ASTRA_VFS_PATH_MAX 4096u
/** Storage for one 255-byte UTF-8 name plus its terminating NUL. */
#define ASTRA_VFS_NAME_MAX 256u
/**
 * Inline byte capacity for data and continuations. Complete request and reply
 * records are checked against the port's message limit below.
 */
#define ASTRA_VFS_IO_MAX 208u

/**
 * One READDIR_BATCH entry on the wire: a fixed record, then the name.
 *
 * The metadata travels with the name because the alternative is a stat per
 * entry, and a cross-process round trip costs about 7.5 ms on this machine --
 * a forty-name `ls -l` would spend a third of a second doing nothing but
 * switching address spaces. Fewer entries fit in a batch than when an entry
 * was three bytes and a name; that trade is the right way round, because the
 * batch is bounded by one message and the stats would have been bounded by
 * nothing.
 *
 * Big-endian, field by field, so the record does not depend on how a compiler
 * lays a struct out:
 *
 *   0   u16  kind
 *   2   u16  mode
 *   4   u16  nlink
 *   6   u8   name length, 1..ASTRA_VFS_NAME_MAX-1
 *   7   u8   reserved, zero
 *   8   u32  uid
 *   12  u32  gid
 *   16  u64  size
 *   24  i64  mtime, seconds since the epoch
 *   32  ..   name, not terminated
 *
 * ponytail: an entry is 32 bytes plus a name, so a 208-byte payload carries
 * about four of them. When a listing of a large directory measures badly, the
 * fix is READDIR into the bound transfer area the way READ_PATH already does,
 * not a smaller record.
 */
#define ASTRA_VFS_DIRENT_HEADER 32u

/* Operations. Values are frozen once published; new ones append. */
/** Open a session and negotiate a protocol version. */
#define ASTRA_VFS_OP_HELLO    UINT32_C(1)
/** Close a session. */
#define ASTRA_VFS_OP_BYE      UINT32_C(2)
/** Open a file or directory by path within the session. */
#define ASTRA_VFS_OP_OPEN     UINT32_C(3)
/** Close an open file or directory handle. */
#define ASTRA_VFS_OP_CLOSE    UINT32_C(4)
/** Read bytes from an open file at a given offset. */
#define ASTRA_VFS_OP_READ     UINT32_C(5)
/** Write bytes to an open file at a given offset. */
#define ASTRA_VFS_OP_WRITE    UINT32_C(6)
/** Look up metadata for a node by path. */
#define ASTRA_VFS_OP_STAT     UINT32_C(7)
/** Read one directory entry by path and cursor. */
#define ASTRA_VFS_OP_READDIR  UINT32_C(8)
/** Create a directory by path. */
#define ASTRA_VFS_OP_MKDIR    UINT32_C(9)
/** Remove a file or empty directory by path. */
#define ASTRA_VFS_OP_UNLINK   UINT32_C(10)
/** Bind the session's shared transfer area for bulk reads and writes. */
#define ASTRA_VFS_OP_BIND_AREA UINT32_C(11)
/** Read a bounded range of an open file into the bound transfer area. */
#define ASTRA_VFS_OP_READ_AREA UINT32_C(12)
/** Read a batch of directory entries, metadata included, in one reply. */
#define ASTRA_VFS_OP_READDIR_BATCH UINT32_C(13)
/**
 * Whole-file read by path, into the bound area.
 *
 * Opening, reading and closing are three round trips for what is almost
 * always one intent, and a round trip to a service costs milliseconds --
 * reading a 5 KiB icon cost more in round trips than in bytes. A program
 * start is mostly small whole-file reads, so this is the shape that matters.
 */
#define ASTRA_VFS_OP_READ_PATH UINT32_C(14)
/** Write a bounded range from the session's bound transfer area. */
#define ASTRA_VFS_OP_WRITE_AREA UINT32_C(15)
/** Read a directory listing into the session's bound transfer area. */
#define ASTRA_VFS_OP_READDIR_AREA UINT32_C(16)
/* 17 and 18 were the staged RENAME_FROM/RENAME_TO pair, retired with the
 * last pre-28 peer; the numbers are not reused. */
/** Flush an open file's data to the backing store. */
#define ASTRA_VFS_OP_SYNC        UINT32_C(19)
/** Truncate or extend an open file to a given length. */
#define ASTRA_VFS_OP_TRUNCATE    UINT32_C(20)
/** Change the permission bits of a node by path. */
#define ASTRA_VFS_OP_CHMOD       UINT32_C(21)
/** Read the target of a symbolic link by path. */
#define ASTRA_VFS_OP_READLINK    UINT32_C(22)
/* 23 and 24 were the staged SYMLINK_TARGET/SYMLINK_TO pair, retired with
 * the last pre-28 peer; the numbers are not reused. */
/** Atomically rename or move a path, carrying both paths in one request. */
#define ASTRA_VFS_OP_RENAME      UINT32_C(25)
/** RENAME refuses an occupied destination when this flag is set. */
#define ASTRA_VFS_RENAME_NOREPLACE UINT32_C(1)
/** Atomically create a symbolic link, carrying target and path in one request. */
#define ASTRA_VFS_OP_SYMLINK     UINT32_C(26)
/** Bind one reply channel and transfer area to the calling thread. */
#define ASTRA_VFS_OP_BIND_LANE   UINT32_C(27)
/** Atomically create a hard link, carrying both paths in one request. */
#define ASTRA_VFS_OP_LINK        UINT32_C(28)
/** Open a file or directory by path, relative to an open directory handle. */
#define ASTRA_VFS_OP_OPEN_AT     UINT32_C(29)
/** Remove a file or directory by path, relative to an open directory handle. */
#define ASTRA_VFS_OP_UNLINK_AT   UINT32_C(30)
/** Change the permission bits of an already-open file or directory handle. */
#define ASTRA_VFS_OP_CHMOD_FILE  UINT32_C(31)
/** Change the permission bits of a path relative to an open directory handle. */
#define ASTRA_VFS_OP_CHMOD_AT    UINT32_C(32)
/** Report filesystem capacity and naming limits, by path or by open handle. */
#define ASTRA_VFS_OP_FILESYSTEM_INFO UINT32_C(33)
/** Look up no-follow metadata for a path relative to an open directory handle. */
#define ASTRA_VFS_OP_STAT_AT     UINT32_C(34)
/** Look up metadata for an already-open file or directory handle. */
#define ASTRA_VFS_OP_STAT_FILE   UINT32_C(35)
/**
 * Derive a narrower grant: `flags` a subset of the session's grant rights,
 * the first complete path operand is the new root relative to its root
 * ("" keeps it). The reply
 * carries the new grant's send handle; HELLO on it hands out its scope.
 */
#define ASTRA_VFS_OP_GRANT_DERIVE UINT32_C(36)
/** Ordered provider lookup; flags select final following and missing leaves. */
#define ASTRA_VFS_OP_LOOKUP       UINT32_C(37)
/** Reposition a shared description; offset is signed and flags selects origin. */
#define ASTRA_VFS_OP_SEEK         UINT32_C(38)
/** Retain a one-use share, or withdraw offset's token with SHARE_DISCARD. */
#define ASTRA_VFS_OP_SHARE        UINT32_C(39)
/** Consume offset's share token into a handle owned by this session's caller. */
#define ASTRA_VFS_OP_ADOPT        UINT32_C(40)
/** Query or change shared APPEND/NONBLOCK; flags is mask, offset is value. */
#define ASTRA_VFS_OP_FILE_FLAGS   UINT32_C(41)
/** Return the current path of an open directory within its grant root. */
#define ASTRA_VFS_OP_DIRECTORY_PATH UINT32_C(42)
/** Highest operation code this build defines. */
#define ASTRA_VFS_OP_MAX          ASTRA_VFS_OP_DIRECTORY_PATH

/** STAT_AT follows the final symbolic link when this flag is set. */
#define ASTRA_VFS_STAT_FOLLOW UINT32_C(1)

/* Rights a grant carries, in GRANT_DERIVE's flags. */
/** Read files, list directories, stat. Every grant has it. */
#define ASTRA_VFS_GRANT_READ     (UINT32_C(1) << 0)
/** Create, write, truncate, rename, unlink, chmod, link. */
#define ASTRA_VFS_GRANT_WRITE    (UINT32_C(1) << 1)
/**
 * Its holders may hand its scope on (the scope keeps TRANSFER) and derive
 * grants that keep this right.
 */
#define ASTRA_VFS_GRANT_DELEGATE (UINT32_C(1) << 2)

/* AstraVfsReply::features, answered by HELLO. */
/** The session is bound to a grant the provider enforces. */
#define ASTRA_VFS_FEATURE_GRANTS (UINT32_C(1) << 0)
/** The provider resolves components in order and returns namespace continuations. */
#define ASTRA_VFS_FEATURE_RESOLVES (UINT32_C(1) << 1)
/** HELLO feature: the provider reports node identity and metadata validity. */
#define ASTRA_VFS_FEATURE_NODE_META (UINT32_C(1) << 2)
/** The provider owns shared positions, cursors and mutable open status flags. */
#define ASTRA_VFS_FEATURE_DESCRIPTIONS (UINT32_C(1) << 3)
/** The provider supports anchored path operations and directory identity paths. */
#define ASTRA_VFS_FEATURE_DIRECTORY_PATH (UINT32_C(1) << 4)
/** Read at an open description's current directory cursor without seeking. */
#define ASTRA_VFS_CURSOR_CURRENT UINT64_MAX

/** Known filesystem identity. */
#define ASTRA_VFS_META_VOLUME     (UINT32_C(1) << 0)
/** Known inode number. */
#define ASTRA_VFS_META_INODE      (UINT32_C(1) << 1)
/** Known inode birth generation. */
#define ASTRA_VFS_META_GENERATION (UINT32_C(1) << 2)
/** Known allocation and preferred I/O size. */
#define ASTRA_VFS_META_BLOCKS     (UINT32_C(1) << 3)
/** Known access time. */
#define ASTRA_VFS_META_ATIME      (UINT32_C(1) << 4)
/** Known modification time. */
#define ASTRA_VFS_META_MTIME      (UINT32_C(1) << 5)
/** Known status-change time. */
#define ASTRA_VFS_META_CTIME      (UINT32_C(1) << 6)
/** Known permission mode, including a real zero. */
#define ASTRA_VFS_META_MODE       (UINT32_C(1) << 7)
/** Known full-width hard-link count. */
#define ASTRA_VFS_META_NLINK      (UINT32_C(1) << 8)
/** Known user and group owners. */
#define ASTRA_VFS_META_OWNER      (UINT32_C(1) << 9)
/** All defined metadata validity bits. */
#define ASTRA_VFS_META_ALL        ((UINT32_C(1) << 10) - 1u)

/** Node identity and metadata validity, shared by native result records.
 * A field with its validity bit clear is unknown, even when its value is zero. */
typedef struct AstraVfsNodeMeta {
    uint32_t valid; /**< ASTRA_VFS_META_* bits. */
    uint32_t identity_reserved; /**< Zero; keeps the wire layout identical on host and MC68040. */
    uint64_t volume; /**< Native mount identity; never a host device number. */
    uint64_t inode; /**< Filesystem node number. */
    uint64_t generation; /**< Node birth time in nanoseconds when known. */
    uint64_t blocks; /**< Allocated 512-byte blocks. */
    uint32_t block_size; /**< Preferred I/O size. */
    uint32_t nlink; /**< Full-width hard-link count. */
    int64_t atime; /**< Access time, seconds since the epoch. */
    uint32_t atime_nsec; /**< Access-time nanoseconds. */
    uint32_t mtime_nsec; /**< Modification-time nanoseconds. */
    int64_t ctime; /**< Status-change time, seconds since the epoch. */
    uint32_t ctime_nsec; /**< Status-change-time nanoseconds. */
    uint32_t reserved; /**< Zero. */
} AstraVfsNodeMeta;
/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraVfsNodeMeta) == 80u, "VFS node metadata ABI changed");
_Static_assert(offsetof(AstraVfsNodeMeta, volume) == 8u, "VFS volume identity offset changed");
/** @endcond */
/** LOOKUP follows the final symbolic link. */
#define ASTRA_VFS_LOOKUP_FOLLOW (UINT32_C(1) << 0)
/** LOOKUP permits a missing final leaf, returning UNKNOWN metadata. */
#define ASTRA_VFS_LOOKUP_MISSING (UINT32_C(1) << 1)
/** Resolve a directory entry for mutation; a final link with '/' is NOT_DIR. */
#define ASTRA_VFS_LOOKUP_ENTRY (UINT32_C(1) << 2)
/** Maximum links followed by one native namespace lookup, across providers. */
#define ASTRA_VFS_LINK_MAX 40u
/** Parent traversal left the provider root. */
#define ASTRA_VFS_ESCAPE_PARENT 1u
/** Symbolic-link traversal left the provider root. */
#define ASTRA_VFS_ESCAPE_SYMLINK 2u
/** Fixed prefix of the LOOKUP payload. */
#define ASTRA_VFS_ESCAPE_HEADER 12u
/** Maximum native continuation bytes, excluding NUL. */
#define ASTRA_VFS_ESCAPE_PATH_MAX (ASTRA_VFS_PATH_MAX - 1u)
/** Native LOOKUP result on OK or ESCAPED. The continuation is complete: callers
 * resume it without rereading any link or appending the original suffix.
 * Request length is the remaining link budget, from zero through 40. Wire
 * replies encode the 12-byte metadata prefix and path in the transfer area. */
typedef struct AstraVfsEscape {
    uint16_t kind; /**< Zero on OK, otherwise ASTRA_VFS_ESCAPE_*. */
    uint16_t length; /**< Continuation byte count, without a terminator. */
    uint32_t consumed; /**< Original provider-request byte position. */
    uint32_t links; /**< Links followed by this provider lookup. */
    uint8_t *path; /**< Caller-owned absolute continuation buffer. */
    uint32_t capacity; /**< Available continuation bytes, excluding NUL. */
} AstraVfsEscape;

/**
 * One shared-area transfer, and the unit the whole read path is sized around.
 *
 * One complete VM area is the transport boundary. Clients choose and grow
 * their actual area size; the protocol does not impose a smaller file quota.
 */
#define ASTRA_VFS_BULK_MAX ASTRA_AREA_SIZE_MAX

/* Open modes. */
/** Open for reading. */
#define ASTRA_VFS_OPEN_READ     (UINT32_C(1) << 0)
/** Open for writing. */
#define ASTRA_VFS_OPEN_WRITE    (UINT32_C(1) << 1)
/**
 * Create the file if it does not exist.
 *
 * An existing file is opened as-is, unread and unmodified, unless
 * ::ASTRA_VFS_OPEN_TRUNCATE is also set -- the two flags are independent, the
 * same as POSIX `open()`'s `O_CREAT` and `O_TRUNC`.
 */
#define ASTRA_VFS_OPEN_CREATE   (UINT32_C(1) << 2)
/** Truncate an existing file to zero length on open. */
#define ASTRA_VFS_OPEN_TRUNCATE (UINT32_C(1) << 3)
/** Open the path as a directory; fails if it does not name one. */
#define ASTRA_VFS_OPEN_DIRECTORY (UINT32_C(1) << 4)
/** With ::ASTRA_VFS_OPEN_CREATE, fail atomically if the path already exists. */
#define ASTRA_VFS_OPEN_EXCLUSIVE (UINT32_C(1) << 5)
/**
 * Writes to this handle append at the current end of file.
 *
 * Before protocol version 24 this applied to every write on the handle;
 * since version 24 an individual write may instead target its explicit
 * offset -- see ::ASTRA_VFS_WRITE_APPEND.
 */
#define ASTRA_VFS_OPEN_APPEND    (UINT32_C(1) << 6)
/** Refuse a final symbolic link with LOOP instead of following it. */
#define ASTRA_VFS_OPEN_NOFOLLOW  (UINT32_C(1) << 7)
/** Request nonblocking I/O where the node supports it. */
#define ASTRA_VFS_OPEN_NONBLOCK  (UINT32_C(1) << 8)
/** Defined open flags; unknown bits are refused. */
#define ASTRA_VFS_OPEN_MASK     ((UINT32_C(1) << 9) - 1u)
/** Mutable flags shared by every owner of an open description. */
#define ASTRA_VFS_FILE_STATUS_MASK \
    (ASTRA_VFS_OPEN_APPEND | ASTRA_VFS_OPEN_NONBLOCK)

/* Per-write behavior. These are deliberately distinct from open flags. */
/**
 * Append this write atomically at the current end of file, for this request
 * only, independent of whether the handle was opened with
 * ::ASTRA_VFS_OPEN_APPEND.
 */
#define ASTRA_VFS_WRITE_APPEND   (UINT32_C(1) << 0)
/** READ/WRITE use and advance the provider description's shared position. */
#define ASTRA_VFS_IO_SEQUENTIAL  (UINT32_C(1) << 1)
/** ADOPT withdraws an unclaimed share; only its sharer may withdraw it. */
#define ASTRA_VFS_ADOPT_DISCARD  (UINT32_C(1) << 0)

/* Flags for namespace operations. */
/** With UNLINK or UNLINK_AT, require a directory rather than a non-directory. */
#define ASTRA_VFS_UNLINK_DIRECTORY (UINT32_C(1) << 0)
/** With ::ASTRA_VFS_OP_CHMOD_AT, change the symlink itself rather than following it. */
#define ASTRA_VFS_AT_SYMLINK_NOFOLLOW (UINT32_C(1) << 1)

/**
 * No caller-supplied creation mode; the backend chooses its own default.
 *
 * Every real mode is confined to ::ASTRA_VFS_MODE_MASK, so this sentinel can
 * never collide with one.
 */
#define ASTRA_VFS_MODE_DEFAULT UINT16_C(0xffff)
/** Every bit a real POSIX permission-and-type mode may set. */
#define ASTRA_VFS_MODE_MASK    UINT16_C(07777)

/* Node kinds. */
/** The node's kind was not reported. */
#define ASTRA_VFS_KIND_UNKNOWN   UINT16_C(0)
/** A regular file. */
#define ASTRA_VFS_KIND_FILE      UINT16_C(1)
/** A directory. */
#define ASTRA_VFS_KIND_DIRECTORY UINT16_C(2)
/** A symbolic link. */
#define ASTRA_VFS_KIND_SYMLINK   UINT16_C(3)

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
/** The request succeeded. */
#define ASTRA_VFS_OK              ((uint32_t)ASTRA_STATUS_OK)
/** The request was malformed, or negotiated an unsupported version. */
#define ASTRA_VFS_ERR_PROTOCOL    ((uint32_t)ASTRA_STATUS_PROTOCOL)
/** No node exists at the given path or handle. */
#define ASTRA_VFS_ERR_NOT_FOUND   ((uint32_t)ASTRA_STATUS_NOT_FOUND)
/** A node already exists where the operation required none. */
#define ASTRA_VFS_ERR_EXISTS      ((uint32_t)ASTRA_STATUS_EXISTS)
/** The path or handle does not name a directory. */
#define ASTRA_VFS_ERR_NOT_DIR     ((uint32_t)ASTRA_STATUS_NOT_DIR)
/** The path or handle names a directory where the operation required a file. */
#define ASTRA_VFS_ERR_IS_DIR      ((uint32_t)ASTRA_STATUS_IS_DIR)
/** The operation is not permitted on this node. */
#define ASTRA_VFS_ERR_ACCESS      ((uint32_t)ASTRA_STATUS_ACCESS)
/** The backend has no space left to complete the operation. */
#define ASTRA_VFS_ERR_NO_SPACE    ((uint32_t)ASTRA_STATUS_NO_SPACE)
/** An argument in the request is invalid for this operation. */
#define ASTRA_VFS_ERR_INVALID     ((uint32_t)ASTRA_STATUS_INVALID)
/** The session or file handle is unknown or stale. */
#define ASTRA_VFS_ERR_BAD_HANDLE  ((uint32_t)ASTRA_STATUS_BAD_HANDLE)
/** A backend or service resource limit was reached. */
#define ASTRA_VFS_ERR_LIMIT       ((uint32_t)ASTRA_STATUS_LIMIT)
/** The backend reported an I/O failure. */
#define ASTRA_VFS_ERR_IO          ((uint32_t)ASTRA_STATUS_IO)
/** The directory is not empty. */
#define ASTRA_VFS_ERR_NOT_EMPTY   ((uint32_t)ASTRA_STATUS_NOT_EMPTY)
/** The operation is not supported by this backend or negotiated version. */
#define ASTRA_VFS_ERR_UNSUPPORTED ((uint32_t)ASTRA_STATUS_UNSUPPORTED)
/** The resource is temporarily busy; retry later. */
#define ASTRA_VFS_ERR_BUSY        ((uint32_t)ASTRA_STATUS_BUSY)
/** The caller's buffer cannot hold what the reply carried; not a wire fault. */
#define ASTRA_VFS_ERR_BUFFER_TOO_SMALL ((uint32_t)ASTRA_STATUS_BUFFER_TOO_SMALL)
/**
 * The service is gone.
 *
 * Only a transport that crosses a process can produce this -- a local call
 * cannot fail to be delivered -- and it is the one thing a caller needs from
 * a transport that it cannot get from a reply.
 */
#define ASTRA_VFS_ERR_PEER        ((uint32_t)ASTRA_STATUS_PEER_DEAD)
/** The operation would cross between different backing filesystems. */
#define ASTRA_VFS_ERR_CROSS_DEVICE ((uint32_t)ASTRA_STATUS_CROSS_DEVICE)
/** Too many symbolic links were encountered resolving the path. */
#define ASTRA_VFS_ERR_LOOP          ((uint32_t)ASTRA_STATUS_LOOP)
/** The filesystem or file is read-only. */
#define ASTRA_VFS_ERR_READ_ONLY     ((uint32_t)ASTRA_STATUS_READ_ONLY)
/** Continue lookup in the native namespace using the provider payload. */
#define ASTRA_VFS_ERR_ESCAPED       ((uint32_t)ASTRA_STATUS_ESCAPED)
/** The file, scope or service was lost; old handles stay lost. */
#define ASTRA_VFS_ERR_LOST          ((uint32_t)ASTRA_STATUS_LOST)
/** A nonempty write cannot fit within its exclusive maximum position. */
#define ASTRA_VFS_ERR_FILE_TOO_LARGE ((uint32_t)ASTRA_STATUS_FILE_TOO_LARGE)
/** A pathname or filename exceeds the native naming limit. */
#define ASTRA_VFS_ERR_NAME_TOO_LONG  ((uint32_t)ASTRA_STATUS_NAME_TOO_LONG)

/**
 * A protocol status as a word, or NULL for a number nothing has named yet.
 *
 * One table, because four copies is four chances for a machine to call the
 * same refusal two different things.
 *
 * @param status An ::ASTRA_VFS_OK or ASTRA_VFS_ERR_* value.
 * @return The status's name, or NULL if `status` names nothing known.
 */
const char *astra_vfs_status_text(uint32_t status);

/**
 * An open file or directory handle.
 *
 * A file handle is a slot index plus a generation, so a stale handle is
 * refused rather than reused -- the same rule the kernel applies to its own
 * handles. A service that skipped it would let a client reach whatever now
 * occupies the slot it used to own.
 */
typedef uint32_t AstraVfsFile;
/** No file; never a handle a backend hands out. */
#define ASTRA_VFS_FILE_INVALID UINT32_C(0)
/** Low half of a generation-safe file handle is its one-based slot. */
#define ASTRA_VFS_FILE_HANDLE_MAX UINT32_C(65535)

/** A negotiated session with a filesystem service. */
typedef uint32_t AstraVfsSession;
/** No session; never a value HELLO negotiates. */
#define ASTRA_VFS_SESSION_INVALID UINT32_C(0)

/** Encoded byte size of ::AstraVfsFilesystemInfo. */
#define ASTRA_VFS_FILESYSTEM_INFO_SIZE 64u
/** Backend-neutral filesystem capacity and naming limits. */
typedef struct AstraVfsFilesystemInfo {
    /** Structure size; always ::ASTRA_VFS_FILESYSTEM_INFO_SIZE. */
    uint32_t size;
    /** Backend-defined capability or attribute flags; zero if none apply. */
    uint32_t flags;
    /** Allocation block size in bytes. */
    uint32_t block_size;
    /** Fragment size in bytes; equal to `block_size` when the backend has none. */
    uint32_t fragment_size;
    /** Total blocks on the filesystem. */
    uint64_t blocks;
    /** Free blocks on the filesystem. */
    uint64_t blocks_free;
    /** Blocks free and available to this caller. */
    uint64_t blocks_available;
    /** Total inodes, or the backend's equivalent. */
    uint64_t files;
    /** Free inodes, or the backend's equivalent. */
    uint64_t files_free;
    /** Longest file name the filesystem accepts, in bytes. */
    uint32_t name_max;
    /** Must be zero. */
    uint32_t reserved;
} AstraVfsFilesystemInfo;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraVfsFilesystemInfo) ==
                   ASTRA_VFS_FILESYSTEM_INFO_SIZE,
               "VFS filesystem-info ABI size changed");
/** @endcond */

/** Encoded byte size of ::AstraVfsRequest. */
#define ASTRA_VFS_REQUEST_SIZE 256u
/** Encoded byte size of ::AstraVfsReply. */
#define ASTRA_VFS_REPLY_SIZE   376u

/*
 * One request record covers every operation. A union of per-operation records
 * would save a few bytes on the wire and cost a decode step that has to be
 * right for every operation forever; a fixed record is checked once.
 */
/** Complete NUL-terminated pathname operands in one bound transfer area.
 * Lengths include NUL; the first starts at zero, the second immediately
 * follows it. Unused fields are zero. No pathname is carried inline. */
typedef struct AstraVfsPathFields {
    uint32_t first_offset; /**< Zero for the first operand. */
    uint32_t first_length; /**< First operand bytes including NUL, or zero. */
    uint32_t second_offset; /**< First length for a pair, otherwise zero. */
    uint32_t second_length; /**< Second operand bytes including NUL, or zero. */
} AstraVfsPathFields;

/** Typed pathname descriptors or inline binary write bytes. */
typedef union AstraVfsBody {
    /** Complete operands reside in the session's existing transfer area. */
    AstraVfsPathFields paths;
    /** The arm used by ::ASTRA_VFS_OP_WRITE's binary payload. */
    uint8_t payload[ASTRA_VFS_IO_MAX];
} AstraVfsBody;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraVfsBody) == ASTRA_VFS_IO_MAX,
               "VFS body size must match the payload capacity");
/** @endcond */

/**
 * One client-to-service request record.
 *
 * The same fixed-size record covers every operation; see each
 * ASTRA_VFS_OP_* value for how its fields are used.
 */
typedef struct AstraVfsRequest {
    /** Structure size; must equal ::ASTRA_VFS_REQUEST_SIZE. */
    uint16_t size;
    /** The protocol version the sender is speaking. */
    uint16_t version;
    /** The session this request belongs to; ::ASTRA_VFS_SESSION_INVALID on HELLO. */
    uint32_t session;
    /** The subject file or directory handle, or ::ASTRA_VFS_FILE_INVALID. */
    uint32_t file;
    /** Destination directory for RENAME/LINK; zero for other operations. */
    uint32_t second_file;
    /** Must be zero; keeps the fixed wire alignment explicit. */
    uint32_t file_reserved;
    /** Operation-specific flags, such as the ASTRA_VFS_OPEN_* open modes. */
    uint32_t flags;
    /**
     * Where in the node to start: bytes for READ and WRITE, and for READDIR
     * the backend's own cursor into the directory, zero to begin a scan. A
     * directory is read from a position like everything else here; what
     * differs is that only the backend can say what the next position is, so
     * the reply carries it back.
     */
    uint64_t offset;
    /** Bytes to move for READ or WRITE, at most ::ASTRA_VFS_IO_MAX. */
    uint32_t length;
    /**
     * What the caller was doing when it asked.
     *
     * The Kit fills this from the calling thread's current activity and the
     * service adopts it for the duration of handling, so one request is one
     * story across every process it touches -- and no caller writes
     * correlation code to get it. Zero means no activity.
     */
    uint32_t activity;
    /** WRITE/WRITE_AREA exclusive maximum byte position; UINT64_MAX is unbounded. */
    uint64_t write_limit;
    /** The path or payload this operation carries; see ::AstraVfsBody. */
    AstraVfsBody body;
} AstraVfsRequest;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraVfsRequest) == ASTRA_VFS_REQUEST_SIZE,
               "VFS request ABI size changed");
/** @endcond */

/**
 * One service-to-client reply record.
 *
 * The same fixed-size record answers every operation; most fields are zero
 * except where a particular operation fills them in.
 */
typedef struct AstraVfsReply {
    /** Structure size; always ::ASTRA_VFS_REPLY_SIZE. */
    uint16_t size;
    /** The protocol version the service chose. */
    uint16_t version;
    /** ::ASTRA_VFS_OK, or one of the ASTRA_VFS_ERR_* codes. */
    uint32_t status;
    /** The session this reply belongs to. */
    uint32_t session;
    /** The subject file or directory handle, where the operation returns one. */
    uint32_t file;
    /** The node's current size in bytes, where the operation reports one. */
    uint64_t node_size;
    /**
     * READDIR: the cursor that reaches the entry after this one, to pass as
     * the next request's offset. Zero on every other operation.
     *
     * A field of its own rather than a second meaning for `node_size`: a
     * listing that resumes from the wrong number silently skips or repeats
     * entries, and that is not a bug anybody finds by reading a struct whose
     * fields mean two things.
     */
    uint64_t cursor;
    /**
     * Node metadata, version 6.
     *
     * A listing that can only show a name and a size is not a listing, and
     * an editor that writes a file back has to be able to put the mode and
     * the times back the way it found them.
     *
     * `meta.valid` distinguishes an unknown field from a real zero value.
     *
     * Seconds since the epoch.
     */
    int64_t mtime;
    /** Bytes moved by READ, WRITE, or READ_PATH, or an entry name's length. */
    uint32_t count;
    /** The node's owning user id, when the backend reports one. */
    uint32_t uid;
    /** The node's owning group id, when the backend reports one. */
    uint32_t gid;
    /** One of the ASTRA_VFS_KIND_* values. */
    uint16_t kind;
    /** POSIX permission and type bits, when the backend reports them. */
    uint16_t mode;
    /** The node's hard-link count, when the backend reports one. */
    uint16_t nlink;
    /** Must be zero. */
    uint16_t reserved;
    /**
     * HELLO: ASTRA_VFS_FEATURE_* the service offers this
     * session. Every other operation: zero.
     */
    uint32_t features;
    /** READ/WRITE and SEEK: byte position after the operation. */
    uint64_t position;
    /** SHARE: a one-use token bound to the originating grant. */
    uint64_t token;
    /** FILE_FLAGS: the description's current ASTRA_VFS_OPEN_* flags. */
    uint32_t open_flags;
    /** Must be zero. */
    uint32_t description_reserved;
    /** Node identity and validity; separate from operation payloads. */
    AstraVfsNodeMeta meta;
    /** The entry name, symlink target, inline read bytes, or other operation payload. */
    uint8_t payload[ASTRA_VFS_IO_MAX];
} AstraVfsReply;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraVfsReply) == ASTRA_VFS_REPLY_SIZE,
               "VFS reply ABI size changed");
/** @endcond */

/** An ::AstraVfsRequest wrapped in its message-port envelope. */
typedef struct AstraVfsRequestMessage {
    /** The message envelope; see astra/%message_abi.h. */
    AstraMessageHeader header;
    /** The request itself. */
    AstraVfsRequest request;
} AstraVfsRequestMessage;

/** An ::AstraVfsReply wrapped in its message-port envelope. */
typedef struct AstraVfsReplyMessage {
    /** The message envelope; see astra/%message_abi.h. */
    AstraMessageHeader header;
    /** The reply itself. */
    AstraVfsReply reply;
} AstraVfsReplyMessage;

/** @cond ASTRA_INTERNAL */
/*
 * The whole point of the derived ASTRA_VFS_IO_MAX above: if either message
 * outgrows what a port will carry, this fails the build rather than the boot.
 */
_Static_assert(sizeof(AstraVfsRequestMessage) <= ASTRA_MESSAGE_SIZE_MAX,
               "VFS request message exceeds the port message limit");
_Static_assert(sizeof(AstraVfsReplyMessage) <= ASTRA_MESSAGE_SIZE_MAX,
               "VFS reply message exceeds the port message limit");
/** @endcond */

#endif
