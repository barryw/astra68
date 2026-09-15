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
supervisor_proc_path_equal(const char *path, const char *want)
{
    if (path == NULL || want == NULL) return 0;
    if (*path == '/') ++path;
    while (*path == *want && *want != '\0') {
        ++path;
        ++want;
    }
    return *path == '\0' && *want == '\0';
}

static inline int
supervisor_proc_path_is_snapshot(const char *path)
{
    return supervisor_proc_path_equal(path, "snapshot");
}

static inline int
supervisor_proc_path_is_libraries(const char *path)
{
    return supervisor_proc_path_equal(path, "libraries");
}

static inline int
supervisor_proc_path_is_library_memory(const char *path)
{
    return supervisor_proc_path_equal(path, "libraries/memory");
}

static inline int
supervisor_proc_path_is_library_disk(const char *path)
{
    return supervisor_proc_path_equal(path, "libraries/disk");
}

/*
 * The backend behind the PROC: assign. Rendered from the supervisor's own
 * process handles, because holding them is what makes an answer possible --
 * see proc_tree.c and docs/OBSERVABILITY.md.
 */
const AstraVfsBackendOps *supervisor_proc_ops(void);

#endif
