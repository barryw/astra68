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

/*
 * Tries each member in order and stops at the first that opens. `wire` receives
 * the path that answered and `*member` its index -- which is the answer to
 * "which one ran", held by the loop that found it rather than deduced
 * afterwards.
 *
 * `rights` is what the operation needs and is checked per member, so a
 * read-only member under a writable union is skipped for a write rather than
 * refusing the whole call. Returns what the last attempt returned when nothing
 * answered, and ASTRA_VFS_ERR_NOT_FOUND when the name has no members at all.
 */
uint32_t astra_vfs_assign_open(const AstraAssignTable *table, const char *path,
                               uint32_t rights, uint32_t flags,
                               AstraVfsAssignClientFn client_for,
                               void *context, char *wire, uint32_t capacity,
                               AstraVfsFile *file, uint64_t *size,
                               uint16_t *kind, AstraVfsClient **client,
                               uint32_t *member);

#endif
