/*
 * PROC: -- process state as a filesystem, rendered on demand.
 *
 * `docs/OBSERVABILITY.md` specifies this tree and the reason it is a view
 * rather than an ambient namespace. A Unix /proc lets any process enumerate
 * every other one because the namespace is global; Astra is capability-based.
 * The kernel grants its complete fixed-slot snapshot only to the registered
 * initial supervisor, and a child sees this rendering only when it is granted
 * the PROC: mount.
 *
 * The layout, per that document:
 *
 *     PROC:
 *       snapshot     fixed AstraProcSnapshot records
 *       <id>/
 *         status      identity, state, priorities, exit reason
 *
 * `mem`, `cpu` and `threads` are named there too and are not separate leaves
 * yet. Their live counters are included in status so one query provides the
 * process list without caching or racing several reads.
 */

#include <loader.h>
#include <proc_tree.h>

#include <astra/process.h>
#include <astra/proc.h>
#include <astra/runtime.h>
#include <astra/syscall.h>
#include <astra/vfs_backend.h>
#include <astra/vfs_service.h>

#include <stdint.h>

#define PROC_RENDER_MAX 512u

static char render[PROC_RENDER_MAX];
static AstraProcSnapshot snapshot[ASTRA_PROCESS_COUNT_MAX];

static uint32_t
refresh_snapshot(void)
{
    uint32_t live = 0u;
    uint32_t status = astra_process_snapshot(
        supervisor_loader_process_handle(), snapshot,
        ASTRA_PROCESS_COUNT_MAX, &live);

    (void)live;
    if (status == ASTRA_SYSCALL_OK)
        return ASTRA_VFS_OK;
    if (status == ASTRA_SYSCALL_ACCESS_DENIED)
        return ASTRA_VFS_ERR_ACCESS;
    return ASTRA_VFS_ERR_IO;
}

static uint32_t
append_text(char *out, uint32_t used, const char *text)
{
    while (used < PROC_RENDER_MAX - 1u && *text != '\0')
        out[used++] = *text++;
    return used;
}

static uint32_t
append_number(char *out, uint32_t used, uint32_t value)
{
    char digits[10];
    uint32_t count = 0u;

    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(digits));
    while (count != 0u && used < PROC_RENDER_MAX - 1u)
        out[used++] = digits[--count];
    return used;
}

static uint32_t
append_field(char *out, uint32_t used, const char *name, uint32_t value)
{
    used = append_text(out, used, name);
    used = append_text(out, used, " ");
    used = append_number(out, used, value);
    return append_text(out, used, "\n");
}

static uint32_t
append_field64(char *out, uint32_t used, const char *name, uint64_t value)
{
    char digits[20];
    uint32_t count = 0u;

    used = append_text(out, used, name);
    used = append_text(out, used, " ");
    do {
        uint64_t quotient = value / 10u;

        digits[count++] = (char)('0' + value - quotient * 10u);
        value = quotient;
    } while (value != 0u && count < sizeof(digits));
    while (count != 0u && used < PROC_RENDER_MAX - 1u)
        out[used++] = digits[--count];
    return append_text(out, used, "\n");
}

/*
 * A path is "", "<id>" or "<id>/status", with an optional leading slash so a
 * caller that built one by joining is not punished for the join. Returns the
 * kernel slot, or the physical process count when no process matches.
 */
static uint32_t
parse_path(const char *path, int *leaf)
{
    uint32_t id = 0u;
    uint32_t digits = 0u;
    uint32_t at = 0u;

    *leaf = 0;
    if (path == NULL)
        return ASTRA_PROCESS_COUNT_MAX;
    while (path[at] == '/')
        ++at;
    while (path[at] >= '0' && path[at] <= '9' && digits < 10u) {
        id = (id * 10u) + (uint32_t)(path[at] - '0');
        ++at;
        ++digits;
    }
    if (digits == 0u)
        return ASTRA_PROCESS_COUNT_MAX;
    while (path[at] == '/')
        ++at;
    if (path[at] != '\0') {
        const char *want = "status";
        uint32_t index = 0u;

        while (want[index] != '\0' && path[at + index] == want[index])
            ++index;
        if (want[index] != '\0' || path[at + index] != '\0')
            return ASTRA_PROCESS_COUNT_MAX;
        *leaf = 1;
    }
    for (uint32_t index = 0u; index < ASTRA_PROCESS_COUNT_MAX; ++index) {
        if (snapshot[index].process.id == id)
            return index;
    }
    return ASTRA_PROCESS_COUNT_MAX;
}

static uint32_t
render_status(uint32_t index, uint32_t *length)
{
    const AstraProcessInfo *info;
    uint32_t used = 0u;

    if (index >= ASTRA_PROCESS_COUNT_MAX || snapshot[index].process.id == 0u)
        return ASTRA_VFS_ERR_NOT_FOUND;
    info = &snapshot[index].process;
    used = append_text(render, used, "name ");
    used = append_text(render, used, snapshot[index].name);
    used = append_text(render, used, "\n");
    used = append_field(render, used, "id", info->id);
    /*
     * The generation travels with the identifier because a number alone must
     * never name a process here: a control operation carries the generation
     * the caller observed and the kernel refuses it if the slot was recycled.
     */
    used = append_field(render, used, "generation", info->generation);
    used = append_field(render, used, "owner", info->owner);
    used = append_field(render, used, "state", info->process_state);
    used = append_field(render, used, "thread_state", info->thread_state);
    used = append_field(render, used, "suspended", info->suspended);
    used = append_field(render, used, "threads", info->thread_count);
    used = append_field(render, used, "live", info->live_threads);
    used = append_field(render, used, "priority", info->default_priority);
    used = append_field(render, used, "ceiling", info->priority_ceiling);
    used = append_field(render, used, "frames", info->resident_frames);
    /* Schedule counts, not time. Named so nobody reads them as seconds. */
    used = append_field(render, used, "runs", info->run_count);
    used = append_field(render, used, "ticks", info->timer_ticks);
    used = append_field(render, used, "syscalls", info->syscall_count);
    used = append_field(render, used, "handles", info->handle_references);
    used = append_field64(render, used, "runtime_ns", info->runtime_ns);
    used = append_field64(render, used, "elapsed_ns", info->elapsed_ns);
    used = append_field(render, used, "exit_reason", info->exit_reason);
    used = append_field(render, used, "exit_status", info->exit_status);
    render[used] = '\0';
    *length = used;
    return ASTRA_VFS_OK;
}

static uint32_t
read_snapshot(uint64_t offset, uint8_t *out, uint32_t length, uint32_t *moved)
{
    const uint8_t *bytes = (const uint8_t *)snapshot;
    uint32_t total = sizeof(snapshot);

    *moved = 0u;
    if (refresh_snapshot() != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_IO;
    if (offset >= total)
        return ASTRA_VFS_OK;
    total -= (uint32_t)offset;
    if (total > length)
        total = length;
    for (uint32_t at = 0u; at < total; ++at)
        out[at] = bytes[(uint32_t)offset + at];
    *moved = total;
    return ASTRA_VFS_OK;
}

static uint32_t
proc_open(void *context, const char *path, uint32_t flags,
          uint16_t create_mode, uintptr_t *node, AstraVfsNodeInfo *info)
{
    uint32_t length = 0u;
    int leaf = 0;
    uint32_t index;

    (void)context;
    (void)create_mode;
    if ((flags & ASTRA_VFS_OPEN_WRITE) != 0u ||
        (flags & ASTRA_VFS_OPEN_CREATE) != 0u)
        return ASTRA_VFS_ERR_ACCESS;
    if (supervisor_proc_path_is_root(path)) {
        *node = 0u;
        info->size = 0u;
        info->kind = ASTRA_VFS_KIND_DIRECTORY;
        info->mode = 0500u;
        info->nlink = 2u;
        return ASTRA_VFS_OK;
    }
    if (supervisor_proc_path_is_snapshot(path)) {
        *node = UINTPTR_MAX;
        info->size = sizeof(snapshot);
        info->kind = ASTRA_VFS_KIND_FILE;
        info->mode = 0400u;
        info->nlink = 1u;
        return ASTRA_VFS_OK;
    }
    if (refresh_snapshot() != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_IO;
    index = parse_path(path, &leaf);
    if (index >= ASTRA_PROCESS_COUNT_MAX)
        return ASTRA_VFS_ERR_NOT_FOUND;
    if (!leaf) {
        *node = 0u;
        info->size = 0u;
        info->kind = ASTRA_VFS_KIND_DIRECTORY;
        info->mode = 0500u;
        info->nlink = 2u;
        return ASTRA_VFS_OK;
    }
    if (render_status(index, &length) != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_NOT_FOUND;
    /* The generation-bearing id prevents an open node following slot reuse. */
    *node = (uintptr_t)snapshot[index].process.id;
    info->size = length;
    info->kind = ASTRA_VFS_KIND_FILE;
    info->mode = 0400u;
    info->nlink = 1u;
    return ASTRA_VFS_OK;
}

static uint32_t
proc_close(void *context, uintptr_t node)
{
    (void)context;
    (void)node;
    return ASTRA_VFS_OK;
}

static uint32_t
proc_read(void *context, uintptr_t node, uint64_t offset, void *buffer,
          uint32_t length, uint32_t *moved)
{
    uint32_t rendered = 0u;
    uint32_t index;
    uint8_t *out = buffer;

    (void)context;
    *moved = 0u;
    if (node == UINTPTR_MAX)
        return read_snapshot(offset, out, length, moved);
    if (node == 0u)
        return ASTRA_VFS_ERR_IS_DIR;
    if (refresh_snapshot() != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_IO;
    for (index = 0u; index < ASTRA_PROCESS_COUNT_MAX; ++index)
        if (snapshot[index].process.id == (uint32_t)node)
            break;
    if (index == ASTRA_PROCESS_COUNT_MAX ||
        render_status(index, &rendered) != ASTRA_VFS_OK)
        return ASTRA_VFS_OK; /* it exited; a short read is the honest answer */
    if (offset >= rendered)
        return ASTRA_VFS_OK;
    rendered -= (uint32_t)offset;
    if (rendered > length)
        rendered = length;
    for (uint32_t at = 0u; at < rendered; ++at)
        out[at] = (uint8_t)render[(uint32_t)offset + at];
    *moved = rendered;
    return ASTRA_VFS_OK;
}

static uint32_t
proc_stat(void *context, const char *path, AstraVfsNodeInfo *info)
{
    uintptr_t node = 0u;
    uint32_t status = proc_open(context, path, ASTRA_VFS_OPEN_READ,
                                ASTRA_VFS_MODE_DEFAULT, &node, info);

    if (status == ASTRA_VFS_OK)
        (void)proc_close(context, node);
    return status;
}

static uint32_t
proc_readdir(void *context, uintptr_t directory, const char *path,
             uint64_t cookie, char *name, uint32_t capacity,
             AstraVfsNodeInfo *info, uint64_t *next)
{
    int leaf = 0;

    (void)context;
    (void)directory;
    if (!supervisor_proc_path_is_root(path)) {
        if (refresh_snapshot() != ASTRA_VFS_OK)
            return ASTRA_VFS_ERR_IO;
        uint32_t index = parse_path(path, &leaf);

        if (leaf || index >= ASTRA_PROCESS_COUNT_MAX)
            return ASTRA_VFS_ERR_NOT_FOUND;
        if (cookie != 0u)
            return ASTRA_VFS_ERR_NOT_FOUND;
        if (capacity < 7u)
            return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
        for (uint32_t at = 0u; at < 7u; ++at)
            name[at] = "status"[at];
        info->size = 0u;
        info->kind = ASTRA_VFS_KIND_FILE;
        info->mode = 0400u;
        info->nlink = 1u;
        *next = 1u;
        return ASTRA_VFS_OK;
    }
    if (cookie == 0u) {
        if (capacity < sizeof("snapshot"))
            return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
        for (uint32_t at = 0u; at < sizeof("snapshot"); ++at)
            name[at] = "snapshot"[at];
        info->size = 0u;
        info->kind = ASTRA_VFS_KIND_FILE;
        info->mode = 0400u;
        info->nlink = 1u;
        *next = 1u;
        return ASTRA_VFS_OK;
    }
    if (refresh_snapshot() != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_IO;
    for (uint32_t index = (uint32_t)cookie - 1u;
         index < ASTRA_PROCESS_COUNT_MAX; ++index) {
        const AstraProcessInfo *process = &snapshot[index].process;
        uint32_t used = 0u;

        if (process->id == 0u)
            continue;
        if (capacity < 12u)
            return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
        used = append_number(name, used, process->id);
        name[used] = '\0';
        info->size = 0u;
        info->kind = ASTRA_VFS_KIND_DIRECTORY;
        info->mode = 0500u;
        info->nlink = 2u;
        *next = index + 2u;
        return ASTRA_VFS_OK;
    }
    return ASTRA_VFS_ERR_NOT_FOUND;
}

/* Process control needs explicit authority; PROC: is a read-only live view. */
static const AstraVfsBackendOps proc_ops = {
    .open = proc_open,
    .close = proc_close,
    .read = proc_read,
    .write = astra_vfs_backend_deny_write,
    .sync = astra_vfs_backend_deny_sync,
    .truncate = astra_vfs_backend_deny_truncate,
    .stat = proc_stat,
    .readdir = proc_readdir,
    .mkdir = astra_vfs_backend_deny_mkdir,
    .unlink = astra_vfs_backend_deny_unlink,
    .rename = astra_vfs_backend_deny_rename,
    .chmod = astra_vfs_backend_deny_chmod,
    .readlink = astra_vfs_backend_no_readlink,
    .symlink = astra_vfs_backend_deny_symlink,
    .link = astra_vfs_backend_deny_link,
};

const AstraVfsBackendOps *
supervisor_proc_ops(void)
{
    return &proc_ops;
}
