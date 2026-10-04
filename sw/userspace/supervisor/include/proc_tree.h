#ifndef ASTRA_SUPERVISOR_PROC_TREE_H
#define ASTRA_SUPERVISOR_PROC_TREE_H

#include <stddef.h>
#include <stdint.h>

#include <astra/proc.h>
#include <astra/process.h>
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
 * One write to PROC:<id>/ctl, Plan 9's /proc/n/ctl: a single command, with
 * at most one trailing newline.
 *
 *   kill          end it
 *   stop, start   suspend and resume it
 *   signal N      deliver signal N (0-31) the way kill(2) does
 *   priority N    set its scheduler priority (ASTRA_PROCESS_PRIORITY_MIN to
 *                 _MAX); the process's own ceiling still applies
 */
enum SupervisorProcControlAction {
    SUPERVISOR_PROC_CONTROL_KILL = 1,
    SUPERVISOR_PROC_CONTROL_STOP,
    SUPERVISOR_PROC_CONTROL_START,
    SUPERVISOR_PROC_CONTROL_SIGNAL,
    SUPERVISOR_PROC_CONTROL_PRIORITY
};

typedef struct SupervisorProcControl {
    uint32_t action;
    uint32_t value;
} SupervisorProcControl;

static inline int
supervisor_proc_control_word(const char *text, uint32_t length,
                             const char *word, uint32_t *rest)
{
    uint32_t at = 0u;

    while (word[at] != '\0') {
        if (at == length || text[at] != word[at])
            return 0;
        ++at;
    }
    *rest = at;
    return 1;
}

static inline int
supervisor_proc_control_parse(const char *text, uint32_t length,
                              SupervisorProcControl *control)
{
    uint32_t at = 0u;
    uint32_t value = 0u;
    uint32_t digits = 0u;
    uint32_t maximum;

    if (text == NULL || control == NULL)
        return 0;
    if (length != 0u && text[length - 1u] == '\n')
        --length;
    if (supervisor_proc_control_word(text, length, "kill", &at) &&
        at == length) {
        control->action = SUPERVISOR_PROC_CONTROL_KILL;
    } else if (supervisor_proc_control_word(text, length, "stop", &at) &&
               at == length) {
        control->action = SUPERVISOR_PROC_CONTROL_STOP;
    } else if (supervisor_proc_control_word(text, length, "start", &at) &&
               at == length) {
        control->action = SUPERVISOR_PROC_CONTROL_START;
    } else if (supervisor_proc_control_word(text, length, "signal ", &at)) {
        control->action = SUPERVISOR_PROC_CONTROL_SIGNAL;
    } else if (supervisor_proc_control_word(text, length, "priority ",
                                            &at)) {
        control->action = SUPERVISOR_PROC_CONTROL_PRIORITY;
    } else {
        return 0;
    }
    control->value = 0u;
    if (control->action != SUPERVISOR_PROC_CONTROL_SIGNAL &&
        control->action != SUPERVISOR_PROC_CONTROL_PRIORITY)
        return 1;
    maximum = control->action == SUPERVISOR_PROC_CONTROL_SIGNAL ?
        31u : ASTRA_PROCESS_PRIORITY_MAX;
    for (; at < length; ++at, ++digits) {
        if (text[at] < '0' || text[at] > '9' || value > maximum)
            return 0;
        value = value * 10u + (uint32_t)(text[at] - '0');
    }
    if (digits == 0u || value > maximum ||
        (control->action == SUPERVISOR_PROC_CONTROL_PRIORITY &&
         value < ASTRA_PROCESS_PRIORITY_MIN))
        return 0;
    control->value = value;
    return 1;
}

/*
 * The backend behind the PROC: assign. Rendered from the supervisor's own
 * process handles, because holding them is what makes an answer possible --
 * see proc_tree.c and docs/OBSERVABILITY.md.
 */
const AstraVfsBackendOps *supervisor_proc_ops(void);
/* The backend context for the read port (0) or the control port (1). */
void *supervisor_proc_context(int control);

#endif
