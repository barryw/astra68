#ifndef ASTRA_SUPERVISOR_PROC_TREE_H
#define ASTRA_SUPERVISOR_PROC_TREE_H

#include <stddef.h>

#include <astra/vfs_backend.h>

static inline int
supervisor_proc_path_is_root(const char *path)
{
    return path == NULL || path[0] == '\0' ||
           (path[0] == '/' && path[1] == '\0');
}

static inline int
supervisor_proc_path_is_snapshot(const char *path)
{
    if (path == NULL)
        return 0;
    if (path[0] == '/')
        ++path;
    return path[0] == 's' && path[1] == 'n' && path[2] == 'a' &&
           path[3] == 'p' && path[4] == 's' && path[5] == 'h' &&
           path[6] == 'o' && path[7] == 't' && path[8] == '\0';
}

/*
 * The backend behind the PROC: assign. Rendered from the supervisor's own
 * process handles, because holding them is what makes an answer possible --
 * see proc_tree.c and docs/OBSERVABILITY.md.
 */
const AstraVfsBackendOps *supervisor_proc_ops(void);

#endif
