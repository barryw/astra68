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

        resolved = astra_assign_resolve(table, path, rights, index, wire,
                                        capacity, &assign);
        /*
         * Past the last member, and the loop is over. A member that refused
         * for its own reasons -- rights it does not carry, a `..` that would
         * climb out of it -- is a member that did not answer, and the next one
         * still might.
         */
        if (resolved == ASTRA_VFS_ERR_NOT_FOUND && index != 0u) {
            break;
        }
        if (resolved != ASTRA_VFS_OK) {
            status = resolved;
            if (resolved == ASTRA_VFS_ERR_NOT_FOUND) {
                break;
            }
            continue;
        }
        serving = client_for(assign, context);
        if (serving == NULL) {
            status = ASTRA_VFS_ERR_NOT_FOUND;
            continue;
        }
        status = astra_vfs_open(serving, wire, flags, file, size, kind);
        if (status != ASTRA_VFS_OK) {
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
