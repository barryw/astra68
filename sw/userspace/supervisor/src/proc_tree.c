/*
 * PROC: -- process state as a filesystem, rendered on demand.
 *
 * `docs/OBSERVABILITY.md` specifies this tree and the reason it is a view
 * rather than an ambient namespace. A Unix /proc lets any process enumerate
 * every other one because the namespace is global; Astra is capability-based,
 * and the kernel keeps `PROCESS_INFO` scoped to a handle the caller already
 * holds so that enumeration cannot be had by guessing numbers. That decision
 * is what makes this file necessary and what shapes it: the supervisor is the
 * process that holds handles to everything it launched, so the supervisor is
 * the one that can answer, and a child sees the tree only because it was
 * granted the mount.
 *
 * The layout, per that document:
 *
 *     PROC:
 *       <id>/
 *         status      identity, state, priorities, exit reason
 *
 * `mem`, `cpu` and `threads` are named there too and are not here yet. What is
 * here is what `AstraProcessInfo` can substantiate today, and it is rendered at
 * read time from a live query -- nothing is cached, so a listing cannot show a
 * process that has already gone.
 *
 * On `cpu`, the document is explicit and this file keeps to it: run counts and
 * timer ticks are schedule counts, not CPU time, and are reported under names
 * that say so. There is no cycle figure because nothing counts cycles per
 * process, and inventing one would be worse than the gap.
 */

#include <loader.h>
#include <proc_tree.h>

#include <astra/process.h>
#include <astra/runtime.h>
#include <astra/syscall.h>
#include <astra/vfs_backend.h>
#include <astra/vfs_service.h>

#include <stdint.h>

#define PROC_RENDER_MAX 512u

static char render[PROC_RENDER_MAX];

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

/*
 * A path is "", "<id>" or "<id>/status", with an optional leading slash so a
 * caller that built one by joining is not punished for the join. Returns the
 * table index, or the count when no process matches.
 */
static uint32_t
parse_path(const char *path, int *leaf)
{
    uint32_t id = 0u;
    uint32_t digits = 0u;
    uint32_t at = 0u;

    *leaf = 0;
    if (path == NULL)
        return supervisor_loader_process_count();
    while (path[at] == '/')
        ++at;
    while (path[at] >= '0' && path[at] <= '9' && digits < 10u) {
        id = (id * 10u) + (uint32_t)(path[at] - '0');
        ++at;
        ++digits;
    }
    if (digits == 0u)
        return supervisor_loader_process_count();
    while (path[at] == '/')
        ++at;
    if (path[at] != '\0') {
        const char *want = "status";
        uint32_t index = 0u;

        while (want[index] != '\0' && path[at + index] == want[index])
            ++index;
        if (want[index] != '\0' || path[at + index] != '\0')
            return supervisor_loader_process_count();
        *leaf = 1;
    }
    for (uint32_t index = 0u; index < supervisor_loader_process_count();
         ++index) {
        AstraProcessInfo info = {0};
        uint32_t handle = supervisor_loader_process_at(index, NULL);

        info.size = sizeof(info);
        if (handle != 0u &&
            astra_process_info(handle, &info) == ASTRA_SYSCALL_OK &&
            info.id == id)
            return index;
    }
    return supervisor_loader_process_count();
}

static uint32_t
render_status(uint32_t index, uint32_t *length)
{
    AstraProcessInfo info = {0};
    const char *path = NULL;
    uint32_t handle = supervisor_loader_process_at(index, &path);
    uint32_t used = 0u;

    info.size = sizeof(info);
    if (handle == 0u ||
        astra_process_info(handle, &info) != ASTRA_SYSCALL_OK)
        return ASTRA_VFS_ERR_NOT_FOUND;
    used = append_text(render, used, "name ");
    used = append_text(render, used, path != NULL ? path : "?");
    used = append_text(render, used, "\n");
    used = append_field(render, used, "id", info.id);
    /*
     * The generation travels with the identifier because a number alone must
     * never name a process here: a control operation carries the generation
     * the caller observed and the kernel refuses it if the slot was recycled.
     */
    used = append_field(render, used, "generation", info.generation);
    used = append_field(render, used, "owner", info.owner);
    used = append_field(render, used, "state", info.process_state);
    used = append_field(render, used, "threads", info.thread_count);
    used = append_field(render, used, "live", info.live_threads);
    used = append_field(render, used, "priority", info.default_priority);
    used = append_field(render, used, "ceiling", info.priority_ceiling);
    used = append_field(render, used, "frames", info.resident_frames);
    /* Schedule counts, not time. Named so nobody reads them as seconds. */
    used = append_field(render, used, "runs", info.run_count);
    used = append_field(render, used, "ticks", info.timer_ticks);
    used = append_field(render, used, "syscalls", info.syscall_count);
    used = append_field(render, used, "handles", info.handle_references);
    used = append_field(render, used, "exit_reason", info.exit_reason);
    used = append_field(render, used, "exit_status", info.exit_status);
    render[used] = '\0';
    *length = used;
    return ASTRA_VFS_OK;
}

static uint32_t
proc_open(void *context, const char *path, uint32_t flags, uintptr_t *node,
          AstraVfsNodeInfo *info)
{
    uint32_t length = 0u;
    int leaf = 0;
    uint32_t index;

    (void)context;
    if ((flags & ASTRA_VFS_OPEN_WRITE) != 0u ||
        (flags & ASTRA_VFS_OPEN_CREATE) != 0u)
        return ASTRA_VFS_ERR_ACCESS;
    if (path == NULL || path[0] == '\0') {
        *node = 0u;
        info->size = 0u;
        info->kind = ASTRA_VFS_KIND_DIRECTORY;
        info->mode = 0500u;
        info->nlink = 2u;
        return ASTRA_VFS_OK;
    }
    index = parse_path(path, &leaf);
    if (index >= supervisor_loader_process_count())
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
    /*
     * The node is the table index plus one. It is not a pointer and it is not
     * the identifier: a reader that holds this open while the process exits
     * gets a short read rather than another process's status, because the read
     * re-renders and the identifier will not match.
     */
    *node = (uintptr_t)(index + 1u);
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
    if (node == 0u)
        return ASTRA_VFS_ERR_IS_DIR;
    index = (uint32_t)node - 1u;
    if (index >= supervisor_loader_process_count() ||
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
proc_write(void *context, uintptr_t node, uint64_t offset, const void *buffer,
           uint32_t length, uint32_t *moved)
{
    (void)context;
    (void)node;
    (void)offset;
    (void)buffer;
    (void)length;
    *moved = 0u;
    /*
     * Killing is not a write to a control file. It is a process-control
     * operation needing process-control authority, and routing it through a
     * mount somebody was granted for reading would hand it to every reader.
     */
    return ASTRA_VFS_ERR_ACCESS;
}

static uint32_t
proc_stat(void *context, const char *path, AstraVfsNodeInfo *info)
{
    uintptr_t node = 0u;
    uint32_t status = proc_open(context, path, ASTRA_VFS_OPEN_READ, &node,
                                info);

    if (status == ASTRA_VFS_OK)
        (void)proc_close(context, node);
    return status;
}

static uint32_t
proc_readdir(void *context, const char *path, uint64_t cookie, char *name,
             uint32_t capacity, AstraVfsNodeInfo *info, uint64_t *next)
{
    int leaf = 0;

    (void)context;
    if (path != NULL && path[0] != '\0') {
        uint32_t index = parse_path(path, &leaf);

        if (leaf || index >= supervisor_loader_process_count())
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
    for (uint32_t index = (uint32_t)cookie;
         index < supervisor_loader_process_count(); ++index) {
        AstraProcessInfo process = {0};
        uint32_t handle = supervisor_loader_process_at(index, NULL);
        uint32_t used = 0u;

        process.size = sizeof(process);
        if (handle == 0u ||
            astra_process_info(handle, &process) != ASTRA_SYSCALL_OK)
            continue;
        if (capacity < 12u)
            return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
        used = append_number(name, used, process.id);
        name[used] = '\0';
        info->size = 0u;
        info->kind = ASTRA_VFS_KIND_DIRECTORY;
        info->mode = 0500u;
        info->nlink = 2u;
        *next = index + 1u;
        return ASTRA_VFS_OK;
    }
    return ASTRA_VFS_ERR_NOT_FOUND;
}

static uint32_t
proc_mkdir(void *context, const char *path)
{
    (void)context;
    (void)path;
    return ASTRA_VFS_ERR_ACCESS;
}

static uint32_t
proc_unlink(void *context, const char *path)
{
    (void)context;
    (void)path;
    return ASTRA_VFS_ERR_ACCESS;
}

static const AstraVfsBackendOps proc_ops = {
    .open = proc_open,
    .close = proc_close,
    .read = proc_read,
    .write = proc_write,
    .stat = proc_stat,
    .readdir = proc_readdir,
    .mkdir = proc_mkdir,
    .unlink = proc_unlink,
};

const AstraVfsBackendOps *
supervisor_proc_ops(void)
{
    return &proc_ops;
}
