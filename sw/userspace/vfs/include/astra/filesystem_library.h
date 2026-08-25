#ifndef ASTRA_FILESYSTEM_LIBRARY_H
#define ASTRA_FILESYSTEM_LIBRARY_H

#include <stdint.h>

#include <astra/filesystem_kit.h>
#include <astra/vfs_assign.h>
#include <astra/vfs_client.h>
#include <astra/vfs_union.h>

#define ASTRA_FILESYSTEM_LIBRARY_ABI_MAJOR 1u
#define ASTRA_FILESYSTEM_LIBRARY_ABI_MINOR 2u
#define ASTRA_FILESYSTEM_DIRECTORY_BATCH_MAX 32u

enum {
    ASTRA_FILE_SEEK_BEGIN = 0,
    ASTRA_FILE_SEEK_CURRENT = 1,
    ASTRA_FILE_SEEK_END = 2
};

typedef uint32_t (*AstraFilesystemReadAtFn)(AstraVfsClient *, AstraVfsFile,
                                            uint64_t, void *, uint32_t,
                                            uint32_t *);

typedef struct AstraFilesystem {
    const AstraAssignTable *_private_assigns;
    AstraVfsAssignClientFn _private_client_for;
    AstraFilesystemReadAtFn _private_read_at;
    void *_private_context;
} AstraFilesystem;

typedef struct AstraFile {
    AstraVfsClient *_private_client;
    AstraFilesystemReadAtFn _private_read_at;
    AstraVfsFile _private_file;
    uint32_t _private_flags;
    uint64_t _private_offset;
    uint64_t _private_size;
    uint16_t _private_kind;
    uint16_t _private_member;
} AstraFile;

typedef struct AstraFileInfo {
    uint32_t size;
    uint32_t open_flags;
    uint64_t byte_size;
    uint64_t offset;
    /*
     * Node metadata. Zero means the filesystem does not carry the field, not
     * that its value is zero -- a caller that cannot tell those apart prints a
     * confident lie about a filesystem that never claimed to know.
     */
    int64_t mtime;          /* seconds since the epoch */
    uint32_t uid;
    uint32_t gid;
    uint16_t kind;
    uint16_t member;
    uint16_t mode;          /* POSIX permission and type bits */
    uint16_t nlink;
} AstraFileInfo;

/*
 * A directory entry carries the same metadata as a stat, and it carries it
 * because the alternative is a stat per entry. A cross-process round trip is
 * about 7.5 ms on this machine, so a forty-entry `ls -l` that stats each name
 * costs a third of a second doing nothing but switching address spaces.
 */
typedef struct AstraDirectoryEntry {
    char name[ASTRA_VFS_NAME_MAX];
    uint64_t byte_size;
    int64_t mtime;
    uint32_t uid;
    uint32_t gid;
    uint16_t kind;
    uint16_t member;
    uint16_t mode;
    uint16_t nlink;
} AstraDirectoryEntry;

typedef struct AstraDirectory {
    AstraFilesystem *_private_filesystem;
    char _private_path[ASTRA_VFS_PATH_MAX];
    uint64_t _private_cursor;
    uint32_t _private_member;
    uint32_t _private_worst;
    uint8_t _private_active;
    uint8_t _private_done;
    uint16_t _private_reserved;
} AstraDirectory;

#define ASTRA_FILESYSTEM_INIT { 0, 0, 0, 0 }
#define ASTRA_FILE_INIT { 0, 0, ASTRA_VFS_FILE_INVALID, 0, 0, 0, 0, 0 }
#define ASTRA_FILE_INFO_INIT { sizeof(AstraFileInfo), 0, 0, 0, 0, 0, 0, \
                               0, 0, 0, 0 }
#define ASTRA_DIRECTORY_INIT { 0, { 0 }, 0, 0, ASTRA_VFS_ERR_NOT_FOUND, \
                               0, 0, 0 }

typedef struct AstraFilesystemLibraryV1 {
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t structure_size;

    uint32_t (*attach)(AstraFilesystem *, const AstraAssignTable *,
                       AstraVfsAssignClientFn, AstraFilesystemReadAtFn,
                       void *);
    void (*detach)(AstraFilesystem *);
    uint32_t (*qualify)(const char *, const char *, const char *, char *,
                        uint32_t);
    uint32_t (*path_split)(const char *, char *, uint32_t, char *, uint32_t);
    uint32_t (*path_normalise)(const char *, char *, uint32_t);
    uint32_t (*open)(AstraFilesystem *, const char *, uint32_t, AstraFile *);
    uint32_t (*close)(AstraFile *);
    uint32_t (*read)(AstraFile *, void *, uint32_t, uint32_t *);
    uint32_t (*write)(AstraFile *, const void *, uint32_t, uint32_t *);
    uint32_t (*read_at)(AstraFile *, uint64_t, void *, uint32_t, uint32_t *);
    uint32_t (*write_at)(AstraFile *, uint64_t, const void *, uint32_t,
                         uint32_t *);
    uint32_t (*seek)(AstraFile *, int64_t, uint32_t, uint64_t *);
    uint32_t (*file_info)(const AstraFile *, AstraFileInfo *);
    uint32_t (*stat)(AstraFilesystem *, const char *, AstraFileInfo *);
    uint32_t (*mkdir)(AstraFilesystem *, const char *);
    uint32_t (*unlink)(AstraFilesystem *, const char *);
    uint32_t (*directory_open)(AstraFilesystem *, const char *,
                               AstraDirectory *);
    uint32_t (*directory_read)(AstraDirectory *, AstraDirectoryEntry *,
                               uint32_t, uint32_t *);
    void (*directory_close)(AstraDirectory *);

    uint32_t (*client_connect)(AstraVfsClient *, AstraVfsTransport, void *);
    uint32_t (*client_disconnect)(AstraVfsClient *);
    uint32_t (*client_open)(AstraVfsClient *, const char *, uint32_t,
                            AstraVfsFile *, uint64_t *, uint16_t *);
    uint32_t (*client_close)(AstraVfsClient *, AstraVfsFile);
    uint32_t (*client_read_at)(AstraVfsClient *, AstraVfsFile, uint64_t,
                               void *, uint32_t, uint32_t *);
    uint32_t (*client_write_at)(AstraVfsClient *, AstraVfsFile, uint64_t,
                                const void *, uint32_t, uint32_t *);
    uint32_t (*client_stat)(AstraVfsClient *, const char *, uint64_t *,
                            uint16_t *);
    uint32_t (*client_readdir_batch)(AstraVfsClient *, const char *, uint64_t,
                                     AstraVfsDirEntry *, uint32_t, uint32_t *,
                                     uint64_t *);
    uint32_t (*client_mkdir)(AstraVfsClient *, const char *);
    uint32_t (*client_unlink)(AstraVfsClient *, const char *);
    uint32_t (*assign_resolve)(const AstraAssignTable *, const char *,
                               uint32_t, uint32_t, char *, uint32_t,
                               const AstraAssign **);
    const AstraAssign *(*assign_lookup)(const AstraAssignTable *,
                                        const char *);
    const AstraAssign *(*assign_member)(const AstraAssignTable *,
                                        const char *, uint32_t);
    uint32_t (*assign_open)(const AstraAssignTable *, const char *, uint32_t,
                            uint32_t, AstraVfsAssignClientFn, void *, char *,
                            uint32_t, AstraVfsFile *, uint64_t *, uint16_t *,
                            AstraVfsClient **, uint32_t *);
    uint32_t (*rename)(AstraFilesystem *, const char *, const char *);
    uint32_t (*client_rename)(AstraVfsClient *, const char *, const char *);
    uint32_t (*sync)(AstraFile *);
    uint32_t (*truncate)(AstraFile *, uint64_t);
    uint32_t (*client_sync)(AstraVfsClient *, AstraVfsFile);
    uint32_t (*client_truncate)(AstraVfsClient *, AstraVfsFile, uint64_t);
} AstraFilesystemLibraryV1;

#endif
