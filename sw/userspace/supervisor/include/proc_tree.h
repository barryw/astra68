#ifndef ASTRA_SUPERVISOR_PROC_TREE_H
#define ASTRA_SUPERVISOR_PROC_TREE_H

#include <stddef.h>
#include <stdint.h>

#include <astra/proc.h>
#include <astra/vfs_backend.h>

typedef void *(*SupervisorProcReallocate)(void *pointer, size_t size);

typedef struct SupervisorProcSnapshotStore {
    AstraProcSnapshot *records;
    uint32_t count;
    uint32_t capacity;
} SupervisorProcSnapshotStore;

static inline int
supervisor_proc_snapshot_reserve(SupervisorProcSnapshotStore *store,
                                 uint32_t required,
                                 SupervisorProcReallocate reallocate)
{
    AstraProcSnapshot *grown;

    if (store == NULL || reallocate == NULL)
        return 0;
    if (required <= store->capacity)
        return 1;
#if SIZE_MAX < UINT32_MAX
    if (required > SIZE_MAX / sizeof(*store->records))
        return 0;
#endif
    grown = reallocate(store->records,
                       (size_t)required * sizeof(*store->records));
    if (grown == NULL)
        return 0;
    store->records = grown;
    store->capacity = required;
    return 1;
}

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
