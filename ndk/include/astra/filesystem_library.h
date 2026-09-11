/** @file filesystem_library.h @brief Capability-based Filesystem Kit ABI. */
#ifndef ASTRA_FILESYSTEM_LIBRARY_H
#define ASTRA_FILESYSTEM_LIBRARY_H

#include <stdint.h>

#include <astra/vfs_assign.h>
#include <astra/vfs_client.h>
#include <astra/vfs_union.h>

/** Logical name resolved beneath `LIBS:` by OpenLibrary(). */
#define ASTRA_FILESYSTEM_LIBRARY_NAME "filesystem.library"
/** Minimum compatible Filesystem Kit ABI major requested by applications. */
#define ASTRA_FILESYSTEM_LIBRARY_VERSION 2u
/** Current Filesystem Kit ABI major. */
#define ASTRA_FILESYSTEM_LIBRARY_ABI_MAJOR ASTRA_FILESYSTEM_LIBRARY_VERSION
/** Current backward-compatible Filesystem Kit ABI revision. */
#define ASTRA_FILESYSTEM_LIBRARY_ABI_MINOR 1u
/** Maximum entries carried by one directory batch in the current wire ABI. */
#define ASTRA_FILESYSTEM_DIRECTORY_BATCH_MAX 32u

enum {
    ASTRA_FILE_SEEK_BEGIN = 0,
    ASTRA_FILE_SEEK_CURRENT = 1,
    ASTRA_FILE_SEEK_END = 2
};

/**
 * Filesystem backend positioned-read callback.
 * @param client Connected transport client.
 * @param file Open backend file token.
 * @param offset Byte offset to read.
 * @param buffer Receives at most `length` bytes.
 * @param length Requested byte count.
 * @param moved Receives bytes read.
 * @return ASTRA_VFS_* status.
 */
typedef uint32_t (*AstraFilesystemReadAtFn)(AstraVfsClient *client,
                                            AstraVfsFile file,
                                            uint64_t offset, void *buffer,
                                            uint32_t length, uint32_t *moved);
/**
 * Filesystem backend positioned-write callback.
 * @param client Connected transport client.
 * @param file Open backend file token.
 * @param offset Byte offset, or backend-defined append offset when flagged.
 * @param flags Write-operation flags.
 * @param buffer Source bytes.
 * @param length Requested byte count.
 * @param moved Receives bytes written.
 * @param position Receives the resulting file position.
 * @return ASTRA_VFS_* status.
 */
typedef uint32_t (*AstraFilesystemWriteAtFn)(AstraVfsClient *client,
                                             AstraVfsFile file,
                                             uint64_t offset, uint32_t flags,
                                             const void *buffer,
                                             uint32_t length, uint32_t *moved,
                                             uint64_t *position);

/** Filesystem Kit context initialized by attach or attach_io. */
typedef struct AstraFilesystem {
    /** @cond ASTRA_INTERNAL */
    const AstraAssignTable *_private_assigns;
    AstraVfsAssignClientFn _private_client_for;
    AstraFilesystemReadAtFn _private_read_at;
    AstraFilesystemWriteAtFn _private_write_at;
    void *_private_context;
    /** @endcond */
} AstraFilesystem;

/** Open file state owned by one caller. */
typedef struct AstraFile {
    /** @cond ASTRA_INTERNAL */
    AstraVfsClient *_private_client;
    AstraFilesystemReadAtFn _private_read_at;
    AstraFilesystemWriteAtFn _private_write_at;
    AstraVfsFile _private_file;
    uint32_t _private_flags;
    uint64_t _private_offset;
    uint64_t _private_size;
    uint16_t _private_kind;
    uint16_t _private_member;
    /** @endcond */
} AstraFile;

/** Stable metadata returned for an open file or path. */
typedef struct AstraFileInfo {
    uint32_t size; /**< Caller-supplied structure size. */
    uint32_t open_flags; /**< Flags used to open the file. */
    uint64_t byte_size; /**< File length in bytes. */
    uint64_t offset; /**< Current open-file byte offset. */
    /*
     * Node metadata. Zero means the filesystem does not carry the field, not
     * that its value is zero -- a caller that cannot tell those apart prints a
     * confident lie about a filesystem that never claimed to know.
     */
    int64_t mtime; /**< Modification time in Unix seconds. */
    uint32_t uid; /**< Owning user identifier, or zero when unavailable. */
    uint32_t gid; /**< Owning group identifier, or zero when unavailable. */
    uint16_t kind; /**< ASTRA_VFS_NODE_* kind. */
    uint16_t member; /**< Union-assign member that supplied the node. */
    uint16_t mode; /**< POSIX permission and type bits. */
    uint16_t nlink; /**< Hard-link count, or zero when unavailable. */
} AstraFileInfo;

/*
 * A directory entry carries the same metadata as a stat, and it carries it
 * because the alternative is a stat per entry. A cross-process round trip is
 * about 7.5 ms on this machine, so a forty-entry `ls -l` that stats each name
 * costs a third of a second doing nothing but switching address spaces.
 */
/** One directory entry and the metadata returned in the same exchange. */
typedef struct AstraDirectoryEntry {
    char name[ASTRA_VFS_NAME_MAX]; /**< NUL-terminated UTF-8 leaf name. */
    uint64_t byte_size; /**< File length in bytes. */
    int64_t mtime; /**< Modification time in Unix seconds. */
    uint32_t uid; /**< Owning user identifier, or zero when unavailable. */
    uint32_t gid; /**< Owning group identifier, or zero when unavailable. */
    uint16_t kind; /**< ASTRA_VFS_NODE_* kind. */
    uint16_t member; /**< Union-assign member that supplied the node. */
    uint16_t mode; /**< POSIX permission and type bits. */
    uint16_t nlink; /**< Hard-link count, or zero when unavailable. */
} AstraDirectoryEntry;

/** Streaming directory enumeration state owned by one caller. */
typedef struct AstraDirectory {
    /** @cond ASTRA_INTERNAL */
    AstraFilesystem *_private_filesystem;
    char _private_path[ASTRA_VFS_PATH_MAX];
    uint64_t _private_cursor;
    AstraVfsClient *_private_client;
    AstraVfsFile _private_file;
    uint32_t _private_member;
    uint32_t _private_worst;
    uint8_t _private_active;
    uint8_t _private_done;
    uint16_t _private_reserved;
    /** @endcond */
} AstraDirectory;

/** Static initializer for a detached AstraFilesystem. */
#define ASTRA_FILESYSTEM_INIT { 0, 0, 0, 0, 0 }
/** Static initializer for a closed AstraFile. */
#define ASTRA_FILE_INIT { 0, 0, 0, ASTRA_VFS_FILE_INVALID, 0, 0, 0, 0, 0 }
/** Static initializer for an ABI-sized AstraFileInfo. */
#define ASTRA_FILE_INFO_INIT { sizeof(AstraFileInfo), 0, 0, 0, 0, 0, 0, \
                               0, 0, 0, 0 }
/** Static initializer for an inactive AstraDirectory. */
#define ASTRA_DIRECTORY_INIT \
    { 0, { 0 }, 0, 0, ASTRA_VFS_FILE_INVALID, 0, \
      ASTRA_VFS_ERR_NOT_FOUND, 0, 0, 0 }

/** Filesystem Kit 2.x immutable export table. */
typedef struct AstraFilesystemLibraryV2 {
    uint16_t abi_major; /**< ASTRA_FILESYSTEM_LIBRARY_ABI_MAJOR. */
    uint16_t abi_minor; /**< ASTRA_FILESYSTEM_LIBRARY_ABI_MINOR. */
    uint32_t structure_size; /**< Bytes available in this table. */

    /** Attach a filesystem with read-only low-level I/O. */
    uint32_t (*attach)(AstraFilesystem *, const AstraAssignTable *,
                       AstraVfsAssignClientFn, AstraFilesystemReadAtFn,
                       void *);
    /** Detach a filesystem and invalidate its borrowed context. */
    void (*detach)(AstraFilesystem *);
    /** Build a qualified path from assign, directory, and leaf parts. */
    uint32_t (*qualify)(const char *, const char *, const char *, char *,
                        uint32_t);
    /** Split a qualified path into assign and relative portions. */
    uint32_t (*path_split)(const char *, char *, uint32_t, char *, uint32_t);
    /** Normalize separators and dot components in a path. */
    uint32_t (*path_normalise)(const char *, char *, uint32_t);
    /** Open a file with default creation permissions. */
    uint32_t (*open)(AstraFilesystem *, const char *, uint32_t, AstraFile *);
    /** Close an open file. */
    uint32_t (*close)(AstraFile *);
    /** Read at the file's current offset and advance it. */
    uint32_t (*read)(AstraFile *, void *, uint32_t, uint32_t *);
    /** Write at the file's current offset and advance it. */
    uint32_t (*write)(AstraFile *, const void *, uint32_t, uint32_t *);
    /** Read at an explicit offset without changing the current offset. */
    uint32_t (*read_at)(AstraFile *, uint64_t, void *, uint32_t, uint32_t *);
    /** Write at an explicit offset without changing the current offset. */
    uint32_t (*write_at)(AstraFile *, uint64_t, const void *, uint32_t,
                         uint32_t *);
    /** Reposition an open file. */
    uint32_t (*seek)(AstraFile *, int64_t, uint32_t, uint64_t *);
    /** Read metadata for an open file. */
    uint32_t (*file_info)(const AstraFile *, AstraFileInfo *);
    /** Read metadata while following a final symbolic link. */
    uint32_t (*stat)(AstraFilesystem *, const char *, AstraFileInfo *);
    /** Create a directory with default permissions. */
    uint32_t (*mkdir)(AstraFilesystem *, const char *);
    /** Remove a non-directory path. */
    uint32_t (*unlink)(AstraFilesystem *, const char *);
    /** Begin a directory enumeration. */
    uint32_t (*directory_open)(AstraFilesystem *, const char *,
                               AstraDirectory *);
    /** Read a batch of directory entries. */
    uint32_t (*directory_read)(AstraDirectory *, AstraDirectoryEntry *,
                               uint32_t, uint32_t *);
    /** End a directory enumeration. */
    void (*directory_close)(AstraDirectory *);

    /** Connect a low-level VFS client. */
    uint32_t (*client_connect)(AstraVfsClient *, AstraVfsTransport, void *);
    /** Disconnect a low-level VFS client. */
    uint32_t (*client_disconnect)(AstraVfsClient *);
    /** Open through a low-level VFS client. */
    uint32_t (*client_open)(AstraVfsClient *, const char *, uint32_t,
                            AstraVfsFile *, uint64_t *, uint16_t *);
    /** Close through a low-level VFS client. */
    uint32_t (*client_close)(AstraVfsClient *, AstraVfsFile);
    /** Read at an offset through a low-level VFS client. */
    uint32_t (*client_read_at)(AstraVfsClient *, AstraVfsFile, uint64_t,
                               void *, uint32_t, uint32_t *);
    /** Write at an offset through a low-level VFS client. */
    uint32_t (*client_write_at)(AstraVfsClient *, AstraVfsFile, uint64_t,
                                const void *, uint32_t, uint32_t *);
    /** Read basic metadata through a low-level VFS client. */
    uint32_t (*client_stat)(AstraVfsClient *, const char *, uint64_t *,
                            uint16_t *);
    /** Read a directory batch through a low-level VFS client. */
    uint32_t (*client_readdir_batch)(AstraVfsClient *, const char *, uint64_t,
                                     AstraVfsDirEntry *, uint32_t, uint32_t *,
                                     uint64_t *);
    /** Create a directory through a low-level VFS client. */
    uint32_t (*client_mkdir)(AstraVfsClient *, const char *);
    /** Remove a path through a low-level VFS client. */
    uint32_t (*client_unlink)(AstraVfsClient *, const char *);
    /** Resolve an assign-qualified path. */
    uint32_t (*assign_resolve)(const AstraAssignTable *, const char *,
                               uint32_t, uint32_t, char *, uint32_t,
                               const AstraAssign **);
    /** Look up the first member of a named assign. */
    const AstraAssign *(*assign_lookup)(const AstraAssignTable *,
                                        const char *);
    /** Look up one member of a named union assign. */
    const AstraAssign *(*assign_member)(const AstraAssignTable *,
                                        const char *, uint32_t);
    /** Resolve and open a path across assign members. */
    uint32_t (*assign_open)(const AstraAssignTable *, const char *, uint32_t,
                            uint32_t, AstraVfsAssignClientFn, void *, char *,
                            uint32_t, AstraVfsFile *, uint64_t *, uint16_t *,
                            AstraVfsClient **, uint32_t *);
    /** Atomically rename a path within one filesystem. */
    uint32_t (*rename)(AstraFilesystem *, const char *, const char *);
    /** Atomically rename through a low-level VFS client. */
    uint32_t (*client_rename)(AstraVfsClient *, const char *, const char *);
    /** Make one open file durable. */
    uint32_t (*sync)(AstraFile *);
    /** Set one open file's length. */
    uint32_t (*truncate)(AstraFile *, uint64_t);
    /** Make one low-level open file durable. */
    uint32_t (*client_sync)(AstraVfsClient *, AstraVfsFile);
    /** Set a low-level open file's length. */
    uint32_t (*client_truncate)(AstraVfsClient *, AstraVfsFile, uint64_t);
    /** Open a file with explicit creation permissions. */
    uint32_t (*open_mode)(AstraFilesystem *, const char *, uint32_t, uint16_t,
                          AstraFile *);
    /** Create a directory with explicit permissions. */
    uint32_t (*mkdir_mode)(AstraFilesystem *, const char *, uint16_t);
    /** Change path permissions. */
    uint32_t (*chmod)(AstraFilesystem *, const char *, uint16_t);
    /** Read a symbolic-link target. */
    uint32_t (*readlink)(AstraFilesystem *, const char *, void *, uint32_t,
                         uint32_t *);
    /** Low-level open with explicit creation permissions. */
    uint32_t (*client_open_mode)(AstraVfsClient *, const char *, uint32_t,
                                 uint16_t, AstraVfsFile *, uint64_t *,
                                 uint16_t *);
    /** Low-level directory creation with explicit permissions. */
    uint32_t (*client_mkdir_mode)(AstraVfsClient *, const char *, uint16_t);
    /** Change permissions through a low-level VFS client. */
    uint32_t (*client_chmod)(AstraVfsClient *, const char *, uint16_t);
    /** Read a link target through a low-level VFS client. */
    uint32_t (*client_readlink)(AstraVfsClient *, const char *, void *,
                                uint32_t, uint32_t *);
    /** Attach a filesystem with read and write low-level I/O. */
    uint32_t (*attach_io)(AstraFilesystem *, const AstraAssignTable *,
                          AstraVfsAssignClientFn, AstraFilesystemReadAtFn,
                          AstraFilesystemWriteAtFn, void *);
    /** Read metadata without following the final symbolic link. */
    uint32_t (*lstat)(AstraFilesystem *, const char *, AstraFileInfo *);
    /** Create a symbolic link. */
    uint32_t (*symlink)(const char *, AstraFilesystem *, const char *);
    /** Create a symbolic link through a low-level VFS client. */
    uint32_t (*client_symlink)(AstraVfsClient *, const char *, const char *);
    /** Create an atomic hard link; cross-filesystem links are refused. */
    uint32_t (*link)(AstraFilesystem *, const char *, const char *);
    /** Create a hard link through a low-level VFS client. */
    uint32_t (*client_link)(AstraVfsClient *, const char *, const char *);
} AstraFilesystemLibraryV2;

#endif
