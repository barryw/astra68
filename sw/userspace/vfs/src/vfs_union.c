/*
 * The loop over a union's members. Everything it knows about a union is that
 * resolution stops answering at some index; everything it knows about I/O is
 * that a client opens a path.
 */

#include <astra/vfs_union.h>

#include <stddef.h>

uint32_t
astra_vfs_assign_open(const AstraAssignTable *table, const char *path,
                      uint32_t rights, uint32_t flags,
                      AstraVfsAssignClientFn client_for, void *context,
                      char *wire, uint32_t capacity, AstraVfsFile *file,
                      uint64_t *size, uint16_t *kind, AstraVfsClient **client,
                      uint32_t *member)
{
    uint32_t status = ASTRA_VFS_ERR_NOT_FOUND;

    if (table == NULL || client_for == NULL || wire == NULL || file == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    for (uint32_t index = 0u; index < ASTRA_ASSIGN_MAX; ++index) {
        const AstraAssign *assign = NULL;
        AstraVfsClient *serving;
        uint32_t resolved;
        uint32_t opened;

        resolved = astra_assign_resolve(table, path, rights, index, wire,
                                        capacity, &assign);
        if (resolved != ASTRA_VFS_OK) {
            /*
             * NOT_FOUND is the ordinary case -- a member that simply is not
             * the answer -- so it never displaces a status that already says
             * something more specific. See the comment below for why that
             * matters.
             *
             * NOT_FOUND is also what ends the loop, and it means either of
             * two things: past the last member, or a `..` in `path` that
             * would climb out of whichever member this was. The two are not
             * distinguished, and cannot be from here -- astra_assign_resolve
             * returns the identical status for both (astra/vfs_assign.h) --
             * but they do not need to be. A `..` escape is a property of
             * `path` itself, not of any one member's root: astra_path_normalise
             * refuses it by refusing to backtrack past its own output
             * buffer's start, before it has even looked at what member is
             * being tried, so every member would refuse it identically. The
             * next member was never going to answer differently.
             */
            if (status == ASTRA_VFS_ERR_NOT_FOUND) {
                status = resolved;
            }
            if (resolved == ASTRA_VFS_ERR_NOT_FOUND) {
                break;
            }
            continue;
        }
        serving = client_for(assign, context);
        if (serving == NULL) {
            continue;
        }
        opened = astra_vfs_open(serving, wire, flags, file, size, kind);
        if (opened != ASTRA_VFS_OK) {
            /*
             * Keep trying every member regardless of what this one said: a
             * union's whole value is that one broken member costs only the
             * files on it, and a device that is failing must not stop a
             * working member later in the list from answering. But between
             * two failures, remember the worse one rather than the last one
             * -- ASTRA_VFS_ERR_NOT_FOUND just means "absent", the ordinary
             * case, while ASTRA_VFS_ERR_IO and friends mean a device could
             * not answer at all. A caller told "not found" when the truth is
             * "the device failed" has no reason to stop retrying, and will
             * hammer a machine that can never answer -- the single most
             * expensive wrong answer this project has already paid for once.
             */
            if (status == ASTRA_VFS_ERR_NOT_FOUND) {
                status = opened;
            }
            continue;
        }
        if (client != NULL) {
            *client = serving;
        }
        if (member != NULL) {
            *member = index;
        }
        return ASTRA_VFS_OK;
    }
    return status;
}
