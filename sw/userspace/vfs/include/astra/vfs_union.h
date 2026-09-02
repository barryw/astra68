#ifndef ASTRA_VFS_UNION_H
#define ASTRA_VFS_UNION_H

#include <astra/vfs_assign.h>
#include <astra/vfs_client.h>

/*
 * Opening a path through a name that may have more than one member.
 *
 * Resolution is a string operation and stays one: it answers per member and
 * refuses once the index passes the last. The trying belongs here, where the
 * I/O already is, and this is the only place that does it -- the shell and a
 * launched program call the same function, which is what makes a union cross a
 * process boundary rather than being a shell feature.
 */

/*
 * Which client speaks for a member. A callback for the same reason the
 * transport is one: the supervisor maps an assign onto one of several clients
 * it owns, a launched program has one per handle it was granted, and neither
 * should be a special case inside the Kit.
 */
typedef AstraVfsClient *(*AstraVfsAssignClientFn)(const AstraAssign *assign,
                                                  void *context);

typedef struct AstraVfsUnionDirectory {
    const AstraAssignTable *table;
    AstraVfsAssignClientFn client_for;
    void *context;
    char path[ASTRA_VFS_PATH_MAX];
    uint64_t cursor;
    AstraVfsClient *client;
    AstraVfsFile file;
    uint32_t member;
    uint32_t worst;
    uint8_t active;
    uint8_t done;
} AstraVfsUnionDirectory;

#define ASTRA_VFS_UNION_DIRECTORY_INIT \
    { 0, 0, 0, { 0 }, 0, 0, ASTRA_VFS_FILE_INVALID, 0, \
      ASTRA_VFS_ERR_NOT_FOUND, 0, 0 }

uint32_t astra_vfs_assign_primary(
    const AstraAssignTable *table, const char *path, uint32_t rights,
    AstraVfsAssignClientFn client_for, void *context, char *wire,
    uint32_t capacity, AstraVfsClient **client, uint32_t *member);

uint32_t astra_vfs_assign_stat(
    const AstraAssignTable *table, const char *path, uint32_t rights,
    AstraVfsAssignClientFn client_for, void *context, char *wire,
    uint32_t capacity, AstraVfsDirEntry *entry, AstraVfsClient **client,
    const AstraAssign **found_assign, uint32_t *member);

/* Metadata for the named node itself; unlike stat, the final link is not followed. */
uint32_t astra_vfs_assign_lstat(
    const AstraAssignTable *table, const char *path, uint32_t rights,
    AstraVfsAssignClientFn client_for, void *context, char *wire,
    uint32_t capacity, AstraVfsDirEntry *entry, AstraVfsClient **client,
    const AstraAssign **found_assign, uint32_t *member);

/* Resolve intermediate links and optionally the final component. */
uint32_t astra_vfs_assign_resolve_links(
    const AstraAssignTable *table, const char *path, uint32_t rights,
    int follow_final, int allow_missing_final,
    AstraVfsAssignClientFn client_for, void *context, char *logical,
    uint32_t capacity);

/* Resolve a create/rename destination once. An existing node returns its
 * serving union member; a missing final node returns the writable primary. */
uint32_t astra_vfs_assign_destination(
    const AstraAssignTable *table, const char *path, uint32_t rights,
    int follow_final, AstraVfsAssignClientFn client_for, void *context,
    char *logical, uint32_t logical_capacity, char *wire,
    uint32_t wire_capacity, AstraVfsClient **client, uint32_t *member);

/*
 * One directory walk for every caller. filesystem.library keeps its ABI by
 * adapting this state, while direct clients such as ls use it unchanged.
 */
uint32_t astra_vfs_union_directory_open(
    const AstraAssignTable *table, const char *path,
    AstraVfsAssignClientFn client_for, void *context,
    AstraVfsUnionDirectory *directory);

uint32_t astra_vfs_union_directory_read(
    AstraVfsUnionDirectory *directory, AstraVfsDirEntry *entries,
    uint32_t capacity, uint32_t *count, uint32_t *member);

void astra_vfs_union_directory_close(AstraVfsUnionDirectory *directory);

/*
 * Tries each member in order and stops at the first that opens. `wire` receives
 * the path that answered and `*member` its index -- which is the answer to
 * "which one ran", held by the loop that found it rather than deduced
 * afterwards.
 *
 * `rights` is what the operation needs and is checked per member, so a
 * read-only member under a writable union is skipped for a write rather than
 * refusing the whole call. Every member is tried even after one fails, since a
 * union's value is that one broken member costs only the files on it.
 *
 * When nothing answers, this returns the *worst* status any member gave
 * rather than the last one: ASTRA_VFS_ERR_NOT_FOUND if that is all any member
 * ever said, or the first status that was not ASTRA_VFS_ERR_NOT_FOUND
 * otherwise. NOT_FOUND means "absent", the ordinary case, and must not bury
 * something like ASTRA_VFS_ERR_IO -- a caller told "not found" when a device
 * actually failed has no reason to stop asking it. ASTRA_VFS_ERR_NOT_FOUND is
 * also what a name with no members at all returns.
 */
uint32_t astra_vfs_assign_open(const AstraAssignTable *table, const char *path,
                               uint32_t rights, uint32_t flags,
                               AstraVfsAssignClientFn client_for,
                               void *context, char *wire, uint32_t capacity,
                               AstraVfsFile *file, uint64_t *size,
                               uint16_t *kind, AstraVfsClient **client,
                               uint32_t *member);

#endif
