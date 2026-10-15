/** @file filesystem_library.h @brief Capability-based Filesystem Kit ABI. */
#ifndef ASTRA_FILESYSTEM_LIBRARY_H
#define ASTRA_FILESYSTEM_LIBRARY_H

#include <stdint.h>

#include <astra/vfs_assign.h>
#include <astra/vfs_client.h>
#include <astra/vfs_union.h>

/** Package provider name resolved by the eager ELF loader. */
#define ASTRA_FILESYSTEM_LIBRARY_NAME "filesystem.library"
/** Minimum compatible Filesystem Kit ABI major requested by applications. */
#define ASTRA_FILESYSTEM_LIBRARY_VERSION 14u
/** Current Filesystem Kit ABI major. */
#define ASTRA_FILESYSTEM_LIBRARY_ABI_MAJOR ASTRA_FILESYSTEM_LIBRARY_VERSION
/** Current backward-compatible Filesystem Kit ABI revision. */
#define ASTRA_FILESYSTEM_LIBRARY_ABI_MINOR 0u
enum {
    ASTRA_FILE_SEEK_BEGIN = 0,
    ASTRA_FILE_SEEK_CURRENT = 1,
    ASTRA_FILE_SEEK_END = 2
};

/**
 * Filesystem backend read callback.
 * @param client Connected transport client.
 * @param file Open backend file token.
 * @param offset Positioned byte offset, ignored for sequential I/O.
 * @param flags Zero or ASTRA_VFS_IO_SEQUENTIAL.
 * @param buffer Receives at most `length` bytes.
 * @param length Requested byte count.
 * @param moved Receives bytes read.
 * @param position Receives the position after.
 * @return ASTRA_VFS_* status.
 */
typedef uint32_t (*AstraFilesystemReadFn)(AstraVfsClient *client,
                                            AstraVfsFile file,
                                            uint64_t offset, uint32_t flags,
                                            void *buffer, uint32_t length,
                                            uint32_t *moved, uint64_t *position);
/**
 * Filesystem backend write callback.
 * @param client Connected transport client.
 * @param file Open backend file token.
 * @param offset Byte offset.
 * @param flags ASTRA_VFS_WRITE_* operation flags.
 * @param max_end Exclusive maximum byte position, UINT64_MAX for no bound.
 * @param buffer Source bytes.
 * @param length Requested byte count.
 * @param moved Receives bytes written.
 * @param position Receives the resulting file position.
 * @return ASTRA_VFS_* status.
 */
typedef uint32_t (*AstraFilesystemWriteFn)(AstraVfsClient *client,
                                             AstraVfsFile file,
                                             uint64_t offset, uint32_t flags,
                                             uint64_t max_end,
                                             const void *buffer,
                                             uint32_t length, uint32_t *moved,
                                             uint64_t *position);

/** Open file state owned by one caller. */
typedef struct AstraFile {
    /** @cond ASTRA_INTERNAL */
    AstraVfsClient *_private_client;
    AstraFilesystemReadFn _private_read;
    AstraFilesystemWriteFn _private_write;
    AstraVfsFile _private_file;
    uint32_t _private_ownership_epoch;
    uint32_t _private_flags;
    uint64_t _private_offset;
    uint64_t _private_size;
    uint16_t _private_kind;
    uint16_t _private_member;
    /** @endcond */
} AstraFile;

/** Filesystem Kit context initialized by attach or attach_io. */
typedef struct AstraFilesystem {
    /** @cond ASTRA_INTERNAL */
    const AstraAssignTable *_private_assigns;
    AstraVfsAssignClientFn _private_client_for;
    AstraFilesystemReadFn _private_read;
    AstraFilesystemWriteFn _private_write;
    void *_private_context;
    uint32_t (*_private_cwd_acquire)(const AstraFile **directory);
    void (*_private_cwd_release)(void);
    uint32_t (*_private_cwd_qualify)(const char *, char *, uint32_t);
    uint32_t (*_private_namespace_acquire)(const AstraAssignTable **, void **);
    void (*_private_namespace_release)(void *);
    /** @endcond */
} AstraFilesystem;

/** Stable metadata returned for an open file or path. */
typedef struct AstraFileInfo {
    uint32_t size; /**< Caller-supplied structure size. */
    uint32_t open_flags; /**< Flags used to open the file. */
    uint64_t byte_size; /**< File length in bytes. */
    uint64_t offset; /**< Current open-file byte offset. */
    /*
     * Node metadata. meta.valid distinguishes unknown fields from real zeros.
     */
    int64_t mtime; /**< Modification time in Unix seconds. */
    uint32_t uid; /**< Owning user identifier, or zero when unavailable. */
    uint32_t gid; /**< Owning group identifier, or zero when unavailable. */
    uint16_t kind; /**< ASTRA_VFS_NODE_* kind. */
    uint16_t member; /**< Union-assign member that supplied the node. */
    uint16_t mode; /**< POSIX permission and type bits. */
    uint16_t nlink; /**< Hard-link count, or zero when unavailable. */
    AstraVfsNodeMeta meta; /**< Identity and explicit metadata validity. */
} AstraFileInfo;

/** Filesystem capacity and naming limits, independent of backend format. */
typedef AstraVfsFilesystemInfo AstraFilesystemInfo;

/*
 * A directory entry carries the same metadata as a stat, and it carries it
 * because the alternative is a stat per entry. A cross-process round trip is
 * about 7.5 ms on this machine, so a forty-entry `ls -l` that stats each name
 * costs a third of a second doing nothing but switching address spaces.
 */
/** One directory entry shared with the low-level VFS client representation. */
typedef AstraVfsDirEntry AstraDirectoryEntry;

/** Streaming directory enumeration state owned by one caller. */
typedef struct AstraDirectory {
    /** @cond ASTRA_INTERNAL */
    AstraFilesystem *_private_filesystem;
    char *_private_path;
    uint64_t _private_cursor;
    AstraVfsClient *_private_client;
    AstraVfsFile _private_file;
    uint32_t _private_ownership_epoch;
    uint32_t _private_member;
    uint32_t _private_worst;
    uint8_t _private_active;
    uint8_t _private_done;
    uint16_t _private_reserved;
    AstraFilesystem *_private_owned_filesystem;
    void *_private_namespace;
    void (*_private_namespace_release)(void *);
    /** @endcond */
} AstraDirectory;

/** Static initializer for a detached AstraFilesystem. */
#define ASTRA_FILESYSTEM_INIT { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
/** Static initializer for a closed AstraFile. */
#define ASTRA_FILE_INIT { 0, 0, 0, ASTRA_VFS_FILE_INVALID, 0, 0, 0, 0, 0, 0 }
/** Static initializer for an ABI-sized AstraFileInfo. */
#define ASTRA_FILE_INFO_INIT { sizeof(AstraFileInfo), 0, 0, 0, 0, 0, 0, \
                               0, 0, 0, 0, { 0 } }
/** Static initializer for an inactive AstraDirectory. */
#define ASTRA_DIRECTORY_INIT \
    { 0, 0, 0, 0, ASTRA_VFS_FILE_INVALID, 0, 0, \
      ASTRA_VFS_ERR_NOT_FOUND, 0, 0, 0, 0, 0, 0 }

/**
 * Attach a filesystem with read-only accelerated I/O.
 * @param filesystem Detached filesystem state.
 * @param assigns Borrowed assign table retained until detach.
 * @param client_for Callback resolving an assign to its VFS client.
 * @param read_at Optional accelerated positioned-read callback.
 * @param context Borrowed callback context retained until detach.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_filesystem_attach(AstraFilesystem *filesystem,
    const AstraAssignTable *assigns, AstraVfsAssignClientFn client_for,
    AstraFilesystemReadFn read_at, void *context);
/**
 * Attach a filesystem with accelerated read and write I/O.
 * @param filesystem Detached filesystem state.
 * @param assigns Borrowed assign table retained until detach.
 * @param client_for Callback resolving an assign to its VFS client.
 * @param read_at Optional accelerated positioned-read callback.
 * @param write_at Optional accelerated positioned-write callback.
 * @param context Borrowed callback context retained until detach.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_filesystem_attach_io(AstraFilesystem *filesystem,
    const AstraAssignTable *assigns, AstraVfsAssignClientFn client_for,
    AstraFilesystemReadFn read_at, AstraFilesystemWriteFn write_at,
    void *context);
/** Detach a filesystem and invalidate its borrowed context. @param filesystem Attached state. */
void astra_filesystem_detach(AstraFilesystem *filesystem);

/**
 * Open a file with default creation permissions.
 * @param filesystem Attached filesystem.
 * @param path Absolute native path, or relative path on a process facade.
 * @param flags ASTRA_VFS_OPEN_* flags.
 * @param file Receives open-file state.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_filesystem_open(AstraFilesystem *filesystem, const char *path,
                               uint32_t flags, AstraFile *file);
/**
 * Open a file with explicit creation permissions.
 * @param filesystem Attached filesystem.
 * @param path Absolute native path, or relative path on a process facade.
 * @param flags ASTRA_VFS_OPEN_* flags.
 * @param create_mode POSIX permission bits used only when creating.
 * @param file Receives open-file state.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_filesystem_open_mode(AstraFilesystem *filesystem,
    const char *path, uint32_t flags, uint16_t create_mode, AstraFile *file);
/**
 * Open a relative path beneath an open directory.
 * @param filesystem Attached namespace for continuations. @param directory Open directory that anchors resolution.
 * @param path Nonempty relative UTF-8 path.
 * @param flags ASTRA_VFS_OPEN_* flags.
 * @param file Receives open-file state.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_filesystem_open_at(AstraFilesystem *filesystem, const AstraFile *directory,
                                  const char *path, uint32_t flags,
                                  AstraFile *file);
/**
 * Open a relative path with explicit creation permissions.
 * @param filesystem Attached namespace for continuations. @param directory Open directory that anchors resolution.
 * @param path Nonempty relative UTF-8 path.
 * @param flags ASTRA_VFS_OPEN_* flags.
 * @param create_mode POSIX permission bits used only when creating.
 * @param file Receives open-file state.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_filesystem_open_at_mode(AstraFilesystem *filesystem, const AstraFile *directory, const char *path, uint32_t flags,
    uint16_t create_mode, AstraFile *file);
/** Close an open file. @param file Open state. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_close(AstraFile *file);
/** Make an open file durable. @param file Open state. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_sync(AstraFile *file);
/** Set an open file's length. @param file Open state. @param size New byte length. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_truncate(AstraFile *file, uint64_t size);
/**
 * Read and advance the current file position.
 * @param file Open state.
 * @param buffer Receives bytes.
 * @param length Requested byte count.
 * @param moved Receives bytes read.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_filesystem_read(AstraFile *file, void *buffer,
                               uint32_t length, uint32_t *moved);
/**
 * Write and advance the current file position.
 * @param file Open state.
 * @param max_end Exclusive maximum byte position, UINT64_MAX for no bound.
 * @param buffer Source bytes.
 * @param length Requested byte count.
 * @param moved Receives bytes written.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_filesystem_write(AstraFile *file, uint64_t max_end, const void *buffer,
                                uint32_t length, uint32_t *moved);
/**
 * Read without changing the current file position.
 * @param file Open state.
 * @param offset Byte offset.
 * @param buffer Receives bytes.
 * @param length Requested byte count.
 * @param moved Receives bytes read.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_filesystem_read_at(AstraFile *file, uint64_t offset,
                                  void *buffer, uint32_t length,
                                  uint32_t *moved);
/**
 * Write without changing the current file position.
 * @param file Open state.
 * @param offset Byte offset.
 * @param max_end Exclusive maximum byte position, UINT64_MAX for no bound.
 * @param buffer Source bytes.
 * @param length Requested byte count.
 * @param moved Receives bytes written.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_filesystem_write_at(AstraFile *file, uint64_t offset, uint64_t max_end,
                                   const void *buffer, uint32_t length,
                                   uint32_t *moved);
/**
 * Reposition an open file.
 * @param file Open state.
 * @param delta Signed offset from the selected origin.
 * @param origin ASTRA_FILE_SEEK_* origin.
 * @param offset Receives the resulting byte offset.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_filesystem_seek(AstraFile *file, int64_t delta,
                               uint32_t origin, uint64_t *offset);
/**
 * Read metadata for an open file.
 * @param file Open state.
 * @param info Receives metadata.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_filesystem_file_info(const AstraFile *file,
                                    AstraFileInfo *info);

/**
 * Query open flags or change shared status flags selected by mask.
 * @param file Open state.
 * @param mask Zero to query, or ASTRA_VFS_OPEN_APPEND/NONBLOCK bits.
 * @param flags Desired values for the selected bits.
 * @param result Receives the current open flags.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_filesystem_file_flags(AstraFile *file, uint32_t mask,
                                     uint32_t flags, uint32_t *result);
/**
 * Read path metadata while following the final symbolic link.
 * @param filesystem Attached filesystem.
 * @param path Absolute native path, or relative path on a process facade.
 * @param info Receives metadata.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_filesystem_stat(AstraFilesystem *filesystem, const char *path,
                               AstraFileInfo *info);
/**
 * Read path metadata without following the final symbolic link.
 * @param filesystem Attached filesystem.
 * @param path Absolute native path, or relative path on a process facade.
 * @param info Receives metadata.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_filesystem_lstat(AstraFilesystem *filesystem,
                                const char *path, AstraFileInfo *info);
/**
 * Read metadata for a relative path beneath an open directory.
 * @param filesystem Attached namespace for continuations. @param directory Open directory state.
 * @param path Path relative to @p directory.
 * @param flags Zero or ASTRA_VFS_STAT_FOLLOW.
 * @param info Receives metadata.
 * @return ASTRA_VFS_* status; ASTRA_VFS_ERR_BAD_HANDLE if @p directory is
 * not a valid open handle, ASTRA_VFS_ERR_NOT_DIR if it is not a directory.
 */
uint32_t astra_filesystem_stat_at(AstraFilesystem *filesystem, const AstraFile *directory,
                                  const char *path, uint32_t flags, AstraFileInfo *info);
/** Create a directory with default permissions. @param filesystem Attached filesystem. @param path Assign-qualified UTF-8 path. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_mkdir(AstraFilesystem *filesystem,
                                const char *path);
/** Create a directory with explicit permissions. @param filesystem Attached filesystem. @param path Assign-qualified UTF-8 path. @param create_mode POSIX permission bits. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_mkdir_mode(AstraFilesystem *filesystem,
                                     const char *path, uint16_t create_mode);
/** Remove an entry. @param filesystem Attached filesystem. @param path Assign-qualified UTF-8 path. @param flags Zero for a non-directory, or ASTRA_VFS_UNLINK_DIRECTORY for an empty directory. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_unlink(AstraFilesystem *filesystem,
                                 const char *path, uint32_t flags);
/** Remove a relative entry beneath an open directory. @param filesystem Attached namespace for continuations. @param directory Open directory. @param path Relative UTF-8 path. @param flags Zero for a non-directory, or ASTRA_VFS_UNLINK_DIRECTORY for an empty directory. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_unlink_at(AstraFilesystem *filesystem, const AstraFile *directory,
                                    const char *path, uint32_t flags);
/** Atomically rename within one backing filesystem. @param filesystem Attached filesystem. @param from Existing path. @param to Destination path. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_rename(AstraFilesystem *filesystem,
                                 const char *from, const char *to);
/** Create a hard link within one backing filesystem. @param filesystem Attached filesystem. @param from Existing path. @param to New path. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_link(AstraFilesystem *filesystem,
                               const char *from, const char *to);
/** Change path permissions. @param filesystem Attached filesystem. @param path Assign-qualified UTF-8 path. @param mode POSIX permission bits. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_chmod(AstraFilesystem *filesystem,
                                const char *path, uint16_t mode);
/** Change permissions on an open node. @param file Open node. @param mode POSIX permission bits. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_chmod_file(AstraFile *file, uint16_t mode);
/** Change permissions beneath an open directory. @param filesystem Attached namespace for continuations. @param directory Open directory. @param path Relative UTF-8 path. @param mode POSIX permission bits. @param flags ASTRA_VFS_AT_* flags. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_chmod_at(AstraFilesystem *filesystem, const AstraFile *directory,
                                   const char *path, uint16_t mode,
                                   uint32_t flags);
/** Report capacity for the filesystem containing a path. @param filesystem Attached filesystem. @param path Assign-qualified path. @param info Receives capacity. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_capacity_path(AstraFilesystem *filesystem,
                                        const char *path,
                                        AstraFilesystemInfo *info);
/** Report capacity for the filesystem containing an open node. @param file Open node. @param info Receives capacity. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_capacity_file(const AstraFile *file,
                                        AstraFilesystemInfo *info);
/** Read a symbolic-link target. @param filesystem Attached filesystem. @param path Link path. @param buffer Receives target bytes. @param capacity Buffer capacity. @param length Receives target length. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_readlink(AstraFilesystem *filesystem,
    const char *path, void *buffer, uint32_t capacity, uint32_t *length);
/** Create a symbolic link. @param target Link target text. @param filesystem Attached filesystem. @param path New link path. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_symlink(const char *target,
                                  AstraFilesystem *filesystem,
                                  const char *path);
/** Begin directory enumeration. @param filesystem Attached filesystem. @param path Directory path. @param directory Receives enumeration state. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_directory_open(AstraFilesystem *filesystem,
    const char *path, AstraDirectory *directory);
/** Begin enumeration through an already-open directory without taking ownership. @param file Open directory retained by the caller. @param directory Receives enumeration state. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_directory_from_file(const AstraFile *file,
                                              AstraDirectory *directory);
/** Read a directory batch. @param directory Active enumeration. @param entries Receives entries. @param capacity Entry capacity. @param count Receives entries written. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_directory_read(AstraDirectory *directory,
    AstraDirectoryEntry *entries, uint32_t capacity, uint32_t *count);
/** Rewind an active directory enumeration to its first entry. @param directory Active enumeration. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_directory_rewind(AstraDirectory *directory);
/** End directory enumeration. @param directory Active enumeration. */
void astra_filesystem_directory_close(AstraDirectory *directory);


/** Create a relative directory through a retained directory.
 * @param filesystem Attached namespace for continuations. @param directory Open directory. @param path Relative UTF-8 path.
 * @param mode Creation permissions. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_mkdir_at(AstraFilesystem *filesystem, const AstraFile *directory, const char *path, uint16_t mode);
/** Rename between retained directories on one grant.
 * @param filesystem Attached namespace for continuations. @param from_directory Source directory. @param from Relative source path.
 * @param to_directory Destination directory. @param to Relative destination path.
 * @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_rename_at(AstraFilesystem *filesystem, const AstraFile *from_directory, const char *from, const AstraFile *to_directory, const char *to);
/** Link between retained directories on one grant.
 * @param filesystem Attached namespace for continuations. @param from_directory Source directory. @param from Relative source path.
 * @param to_directory Destination directory. @param to Relative destination path.
 * @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_link_at(AstraFilesystem *filesystem, const AstraFile *from_directory, const char *from, const AstraFile *to_directory, const char *to);
/** Create a link in a retained directory.
 * @param filesystem Attached namespace for continuations. @param target Literal target text. @param directory Open directory.
 * @param path Relative new link path. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_symlink_at(AstraFilesystem *filesystem, const char *target, const AstraFile *directory, const char *path);
/** Read a link in a retained directory.
 * @param filesystem Attached namespace for continuations. @param directory Open directory. @param path Relative link path.
 * @param buffer Receives target bytes. @param capacity Buffer capacity.
 * @param length Receives bytes written. @return ASTRA_VFS_* status. */
uint32_t astra_filesystem_readlink_at(AstraFilesystem *filesystem, const AstraFile *directory, const char *path, void *buffer, uint32_t capacity, uint32_t *length);
/** Return a directory's current path relative to its grant root.
 * @param directory Open directory. @param buffer Receives UTF-8 bytes without NUL.
 * @param capacity Buffer capacity. @param length Receives bytes written.
 * @return ASTRA_VFS_* status; NOT_FOUND when removed or outside its grant. */
uint32_t astra_filesystem_directory_path(const AstraFile *directory, void *buffer, uint32_t capacity, uint32_t *length);

#endif
