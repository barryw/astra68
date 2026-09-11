/** @file vfs_union.h @brief Ordered union-assign filesystem operations. */
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

/**
 * Which client speaks for a member. A callback for the same reason the
 * transport is one: the supervisor maps an assign onto one of several clients
 * it owns, a launched program has one per handle it was granted, and neither
 * should be a special case inside the Kit.
 * @param assign Borrowed namespace member.
 * @param context Caller-owned resolver context.
 * @return Borrowed connected client for `assign`, or NULL when unavailable.
 */
typedef AstraVfsClient *(*AstraVfsAssignClientFn)(const AstraAssign *assign,
                                                  void *context);

/** Directory traversal state spanning every member of a union assign. */
typedef struct AstraVfsUnionDirectory {
    const AstraAssignTable *table; /**< Borrowed process namespace. */
    AstraVfsAssignClientFn client_for; /**< Member-to-client resolver. */
    void *context; /**< Resolver context. */
    char path[ASTRA_VFS_PATH_MAX]; /**< Assign-qualified UTF-8 path. */
    uint64_t cursor; /**< Current member's directory cursor. */
    AstraVfsClient *client; /**< Current member's borrowed client. */
    AstraVfsFile file; /**< Current open directory handle. */
    uint32_t member; /**< Current union member index. */
    uint32_t worst; /**< Most informative error seen while scanning. */
    uint8_t active; /**< Nonzero while one member is open. */
    uint8_t done; /**< Nonzero after every member is exhausted. */
} AstraVfsUnionDirectory;

/** Static initializer for an unopened union-directory traversal. */
#define ASTRA_VFS_UNION_DIRECTORY_INIT \
    { 0, 0, 0, { 0 }, 0, 0, ASTRA_VFS_FILE_INVALID, 0, \
      ASTRA_VFS_ERR_NOT_FOUND, 0, 0 }

/**
 * Resolve the first union member carrying the requested rights.
 * @param table Process namespace.
 * @param path Assign-qualified path.
 * @param rights Required rights.
 * @param client_for Member-to-client resolver.
 * @param context Resolver context.
 * @param wire Receives the mount-relative path.
 * @param capacity Bytes available in `wire`.
 * @param client Receives the borrowed member client.
 * @param member Receives the union-member index.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_assign_primary(
    const AstraAssignTable *table, const char *path, uint32_t rights,
    AstraVfsAssignClientFn client_for, void *context, char *wire,
    uint32_t capacity, AstraVfsClient **client, uint32_t *member);

/**
 * Read metadata through a union, following the final symbolic link.
 * @param table Process namespace.
 * @param path Assign-qualified path.
 * @param rights Required rights.
 * @param client_for Member-to-client resolver.
 * @param context Resolver context.
 * @param wire Receives the answering mount-relative path.
 * @param capacity Bytes available in `wire`.
 * @param entry Receives node metadata.
 * @param client Receives the borrowed answering client.
 * @param found_assign Receives the borrowed answering binding.
 * @param member Receives the answering union-member index.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_assign_stat(
    const AstraAssignTable *table, const char *path, uint32_t rights,
    AstraVfsAssignClientFn client_for, void *context, char *wire,
    uint32_t capacity, AstraVfsDirEntry *entry, AstraVfsClient **client,
    const AstraAssign **found_assign, uint32_t *member);

/**
 * Read metadata for a union node without following the final symbolic link.
 * @param table Process namespace.
 * @param path Assign-qualified path.
 * @param rights Required rights.
 * @param client_for Member-to-client resolver.
 * @param context Resolver context.
 * @param wire Receives the answering mount-relative path.
 * @param capacity Bytes available in `wire`.
 * @param entry Receives node metadata.
 * @param client Receives the borrowed answering client.
 * @param found_assign Receives the borrowed answering binding.
 * @param member Receives the answering union-member index.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_assign_lstat(
    const AstraAssignTable *table, const char *path, uint32_t rights,
    AstraVfsAssignClientFn client_for, void *context, char *wire,
    uint32_t capacity, AstraVfsDirEntry *entry, AstraVfsClient **client,
    const AstraAssign **found_assign, uint32_t *member);

/**
 * Resolve symbolic links while preserving the assign security boundary.
 * @param table Process namespace.
 * @param path Assign-qualified path.
 * @param rights Required rights.
 * @param follow_final Nonzero to follow the final component.
 * @param allow_missing_final Nonzero to permit a missing final component.
 * @param client_for Member-to-client resolver.
 * @param context Resolver context.
 * @param logical Receives the resolved assign-qualified path.
 * @param capacity Bytes available in `logical`.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_assign_resolve_links(
    const AstraAssignTable *table, const char *path, uint32_t rights,
    int follow_final, int allow_missing_final,
    AstraVfsAssignClientFn client_for, void *context, char *logical,
    uint32_t capacity);

/**
 * Resolve a create or rename destination exactly once.
 * An existing node returns its serving member; a missing final node returns
 * the writable primary.
 * @param table Process namespace.
 * @param path Assign-qualified destination.
 * @param rights Required rights.
 * @param follow_final Nonzero to follow an existing final link.
 * @param client_for Member-to-client resolver.
 * @param context Resolver context.
 * @param logical Receives the resolved assign-qualified path.
 * @param logical_capacity Bytes available in `logical`.
 * @param wire Receives the mount-relative destination.
 * @param wire_capacity Bytes available in `wire`.
 * @param client Receives the borrowed destination client.
 * @param member Receives the destination member index.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_assign_destination(
    const AstraAssignTable *table, const char *path, uint32_t rights,
    int follow_final, AstraVfsAssignClientFn client_for, void *context,
    char *logical, uint32_t logical_capacity, char *wire,
    uint32_t wire_capacity, AstraVfsClient **client, uint32_t *member);

/**
 * One directory walk for every caller. filesystem.library keeps its ABI by
 * adapting this state, while direct clients such as ls use it unchanged.
 * @param table Process namespace.
 * @param path Assign-qualified directory path.
 * @param client_for Member-to-client resolver.
 * @param context Resolver context.
 * @param directory Traversal storage initialized on success.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_union_directory_open(
    const AstraAssignTable *table, const char *path,
    AstraVfsAssignClientFn client_for, void *context,
    AstraVfsUnionDirectory *directory);

/**
 * Read the next batch across union members in join order.
 * @param directory Open traversal state.
 * @param entries Receives at most `capacity` entries.
 * @param capacity Number of entries available.
 * @param count Receives entries written.
 * @param member Receives the member that supplied the batch.
 * @return ASTRA_VFS_* status; NOT_FOUND means traversal is complete.
 */
uint32_t astra_vfs_union_directory_read(
    AstraVfsUnionDirectory *directory, AstraVfsDirEntry *entries,
    uint32_t capacity, uint32_t *count, uint32_t *member);

/**
 * Close the active member and reset a union-directory traversal.
 * @param directory Traversal state to close.
 */
void astra_vfs_union_directory_close(AstraVfsUnionDirectory *directory);

/**
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
 * @param table Process namespace.
 * @param path Assign-qualified path.
 * @param rights Required rights.
 * @param flags ASTRA_VFS_OPEN_* flags.
 * @param client_for Member-to-client resolver.
 * @param context Resolver context.
 * @param wire Receives the answering mount-relative path.
 * @param capacity Bytes available in `wire`.
 * @param file Receives the open file token.
 * @param size Receives byte length.
 * @param kind Receives ASTRA_VFS_NODE_* kind.
 * @param client Receives the borrowed answering client.
 * @param member Receives the answering member index.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_assign_open(const AstraAssignTable *table, const char *path,
                               uint32_t rights, uint32_t flags,
                               AstraVfsAssignClientFn client_for,
                               void *context, char *wire, uint32_t capacity,
                               AstraVfsFile *file, uint64_t *size,
                               uint16_t *kind, AstraVfsClient **client,
                               uint32_t *member);

#endif
