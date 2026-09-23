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
 *       libraries/
 *         memory      resident libraries and their process mappings
 *         disk        installed library providers
 *       <id>/
 *         status      identity, state, priorities, exit reason
 *         libraries   resident libraries mapped by this process
 *
 * `mem`, `cpu` and `threads` are named there too and are not separate leaves
 * yet. Their live counters are included in status so one query provides the
 * process list without caching or racing several reads.
 */

#include <loader.h>
#include <proc_tree.h>
#include <vfs_host.h>

#include <astra/process.h>
#include <astra/proc.h>
#include <astra/runtime.h>
#include <astra/syscall.h>
#include <astra/vfs_backend.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_provider_index.h>
#include <astra/vfs_service.h>
#include <astra/vfs_union.h>

#include <stdint.h>

#define PROC_NODE_SNAPSHOT 1u
#define PROC_NODE_LIBRARY_MEMORY 2u
#define PROC_NODE_LIBRARY_DISK 3u
#define PROC_NODE_ROOT 4u
#define PROC_NODE_LIBRARIES 5u
#define PROC_OPEN_NODE_MAGIC 0x50524f43u

enum ProcProcessLeaf {
    PROC_PROCESS_DIRECTORY = 0,
    PROC_PROCESS_STATUS,
    PROC_PROCESS_LIBRARIES
};

typedef struct ProcOpenNode {
    uint32_t magic;
    uint32_t process_id;
    uint32_t generation;
    uint32_t leaf;
} ProcOpenNode;

static SupervisorProcSnapshotStore snapshot_store;
#define snapshot (snapshot_store.records)
#define snapshot_count (snapshot_store.count)
#define snapshot_capacity (snapshot_store.capacity)
#define PROC_LIBRARY_SNAPSHOT_BYTES 4096u
#define PROC_LIBRARY_SNAPSHOT_BATCH \
    (PROC_LIBRARY_SNAPSHOT_BYTES / sizeof(AstraProcLibrarySnapshot))
static AstraProcLibrarySnapshot libraries[PROC_LIBRARY_SNAPSHOT_BATCH];

_Static_assert(PROC_LIBRARY_SNAPSHOT_BATCH != 0u,
               "PROC library transfer page holds no records");

static uint32_t
refresh_snapshot(void)
{
    uint32_t live = 0u;
    uint32_t status;

    status = astra_process_snapshot(supervisor_loader_process_handle(),
                                    snapshot, snapshot_capacity, &live);
    if (status == ASTRA_SYSCALL_BUFFER_TOO_SMALL) {
        if (!supervisor_proc_snapshot_reserve(
                &snapshot_store, live, astra_runtime_reallocate))
            return ASTRA_VFS_ERR_LIMIT;
        status = astra_process_snapshot(
            supervisor_loader_process_handle(), snapshot,
            snapshot_capacity, &live);
    }
    if (status == ASTRA_SYSCALL_OK)
        snapshot_count = live;
    if (status == ASTRA_SYSCALL_OK)
        return ASTRA_VFS_OK;
    if (status == ASTRA_SYSCALL_ACCESS_DENIED)
        return ASTRA_VFS_ERR_ACCESS;
    return ASTRA_VFS_ERR_IO;
}

typedef struct ProcText {
    uint8_t *out;
    uint64_t offset;
    uint64_t length;
    uint32_t capacity;
    uint32_t moved;
} ProcText;

static void
proc_text_byte(ProcText *text, char value)
{
    if (text->length >= text->offset && text->moved < text->capacity)
        text->out[text->moved++] = (uint8_t)value;
    ++text->length;
}

static void
proc_text_string(ProcText *text, const char *value, uint32_t capacity)
{
    for (uint32_t at = 0u; at < capacity && value[at] != '\0'; ++at)
        proc_text_byte(text, value[at]);
}

static void
proc_text_number(ProcText *text, uint32_t value)
{
    char digits[10];
    uint32_t count = 0u;

    do {
        uint32_t quotient = value / 10u;

        digits[count++] = (char)('0' + value - quotient * 10u);
        value = quotient;
    } while (value != 0u);
    while (count != 0u)
        proc_text_byte(text, digits[--count]);
}

static void
proc_text_number64(ProcText *text, uint64_t value)
{
    char digits[20];
    uint32_t count = 0u;

    do {
        uint64_t quotient = value / 10u;

        digits[count++] = (char)('0' + value - quotient * 10u);
        value = quotient;
    } while (value != 0u);
    while (count != 0u)
        proc_text_byte(text, digits[--count]);
}

static void
proc_text_field(ProcText *text, const char *name, uint32_t value)
{
    proc_text_string(text, name, UINT32_MAX);
    proc_text_byte(text, ' ');
    proc_text_number(text, value);
    proc_text_byte(text, '\n');
}

static void
proc_text_field64(ProcText *text, const char *name, uint64_t value)
{
    proc_text_string(text, name, UINT32_MAX);
    proc_text_byte(text, ' ');
    proc_text_number64(text, value);
    proc_text_byte(text, '\n');
}

static void
proc_text_hex(ProcText *text, uint32_t value)
{
    static const char digits[] = "0123456789abcdef";

    proc_text_string(text, "0x", 2u);
    for (uint32_t shift = 28u; ; shift -= 4u) {
        proc_text_byte(text, digits[(value >> shift) & 0xfu]);
        if (shift == 0u)
            break;
    }
}

static uint32_t
render_library_text(uint32_t pid, ProcText *text)
{
    uint32_t start = 0u;
    uint32_t total = 0u;

    proc_text_string(text,
        "NAME VERSION ABI BUILD BASE SPAN CACHE RESIDENT MAPPED REFS PID\n",
        UINT32_MAX);
    do {
        uint32_t moved = 0u;
        uint32_t status = astra_library_snapshot(
            supervisor_loader_process_handle(), start, libraries,
            PROC_LIBRARY_SNAPSHOT_BATCH, &moved, &total);

        if (status == ASTRA_SYSCALL_ACCESS_DENIED)
            return ASTRA_VFS_ERR_ACCESS;
        if (status != ASTRA_SYSCALL_OK || moved > total - start ||
            (moved == 0u && start != total))
            return ASTRA_VFS_ERR_IO;
        for (uint32_t slot = 0u; slot < moved; ++slot) {
            const AstraProcLibrarySnapshot *library = &libraries[slot];

            if (pid != 0u && library->process_id != pid)
                continue;
            proc_text_string(text, library->library.name,
                             sizeof(library->library.name));
            proc_text_byte(text, ' ');
            proc_text_number(text, library->library.major);
            proc_text_byte(text, '.');
            proc_text_number(text, library->library.minor);
            proc_text_byte(text, '.');
            proc_text_number(text, library->library.patch);
            proc_text_byte(text, ' ');
            proc_text_number(text, library->library.abi_major);
            proc_text_byte(text, '.');
            proc_text_number(text, library->library.abi_minor);
            proc_text_byte(text, ' ');
            proc_text_hex(text, library->library.build_id);
            proc_text_byte(text, ' ');
            proc_text_hex(text, library->base);
            proc_text_byte(text, ' ');
            proc_text_number(text, library->image_span);
            proc_text_byte(text, ' ');
            proc_text_number(text, library->cache_bytes);
            proc_text_byte(text, ' ');
            proc_text_number(text, library->resident_bytes);
            proc_text_byte(text, ' ');
            proc_text_number(text, library->mapped_bytes);
            proc_text_byte(text, ' ');
            proc_text_number(text, library->reference_count);
            proc_text_byte(text, ' ');
            if (library->process_id == 0u)
                proc_text_byte(text, '-');
            else
                proc_text_number(text, library->process_id);
            proc_text_byte(text, '\n');
        }
        start += moved;
    } while (start < total);
    return ASTRA_VFS_OK;
}

static int
path_append(char *path, uint32_t capacity, const char *suffix)
{
    uint32_t at = 0u;
    uint32_t add = 0u;

    while (at < capacity && path[at] != '\0')
        ++at;
    while (suffix[add] != '\0')
        ++add;
    if (at == capacity || add >= capacity - at)
        return 0;
    for (uint32_t index = 0u; index <= add; ++index)
        path[at + index] = suffix[index];
    return 1;
}

static int
provider_leaf(const char *leaf, char *name, uint16_t *abi)
{
    const char marker[] = ".abi-";
    uint32_t length = 0u;
    uint32_t marker_at = UINT32_MAX;
    uint32_t value = 0u;

    while (leaf[length] != '\0')
        ++length;
    for (uint32_t at = 1u; at + sizeof(marker) - 1u < length; ++at) {
        uint32_t matched = 0u;

        while (matched < sizeof(marker) - 1u &&
               leaf[at + matched] == marker[matched])
            ++matched;
        if (matched == sizeof(marker) - 1u)
            marker_at = at;
    }
    if (marker_at == UINT32_MAX || marker_at >= ASTRA_LIBRARY_NAME_MAX)
        return 0;
    for (uint32_t at = marker_at + sizeof(marker) - 1u;
         at < length; ++at) {
        uint32_t digit;

        if (leaf[at] < '0' || leaf[at] > '9')
            return 0;
        digit = (uint32_t)(leaf[at] - '0');
        if (value > (UINT16_MAX - digit) / 10u)
            return 0;
        value = value * 10u + digit;
    }
    if (value == 0u)
        return 0;
    for (uint32_t at = 0u; at < marker_at; ++at)
        name[at] = leaf[at];
    name[marker_at] = '\0';
    *abi = (uint16_t)value;
    return 1;
}

static uint32_t
provider_size(const char *path, uint64_t *size)
{
    AstraVfsClient *client = NULL;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    uint16_t kind = ASTRA_VFS_KIND_UNKNOWN;
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t status = astra_vfs_assign_open(
        supervisor_assigns(), path, ASTRA_RIGHT_READ, ASTRA_VFS_OPEN_READ,
        supervisor_vfs_assign_client, NULL, wire, sizeof(wire), &file, size,
        &kind, &client, NULL);

    if (status != ASTRA_VFS_OK)
        return status;
    status = kind == ASTRA_VFS_KIND_FILE ? ASTRA_VFS_OK :
                                          ASTRA_VFS_ERR_NOT_FOUND;
    if (astra_vfs_close(client, file) != ASTRA_VFS_OK &&
        status == ASTRA_VFS_OK)
        status = ASTRA_VFS_ERR_IO;
    return status;
}

static void
render_disk_row(ProcText *text, const AstraLibraryReference *library,
                uint64_t size, const char *path)
{
    proc_text_string(text, library->name, sizeof(library->name));
    proc_text_byte(text, ' ');
    proc_text_number(text, library->major);
    proc_text_byte(text, '.');
    proc_text_number(text, library->minor);
    proc_text_byte(text, '.');
    proc_text_number(text, library->patch);
    proc_text_byte(text, ' ');
    proc_text_number(text, library->abi_major);
    proc_text_byte(text, '.');
    proc_text_number(text, library->abi_minor);
    proc_text_byte(text, ' ');
    proc_text_hex(text, library->build_id);
    proc_text_byte(text, ' ');
    proc_text_number64(text, size);
    proc_text_byte(text, ' ');
    proc_text_string(text, path, ASTRA_VFS_PATH_MAX);
    proc_text_byte(text, '\n');
}

static uint32_t
render_disk_libraries(ProcText *text)
{
    static AstraVfsDirEntry entries[8];
    AstraAssignTable *assigns = supervisor_assigns();
    AstraVfsUnionDirectory directory = ASTRA_VFS_UNION_DIRECTORY_INIT;
    const char base[] = "/libs/.providers";
    const char *failure = "PROC library directory open";
    uint32_t status;

    if (assigns == NULL)
        return ASTRA_VFS_ERR_NOT_FOUND;
    proc_text_string(text, "NAME VERSION ABI BUILD SIZE PATH\n", UINT32_MAX);
    status = astra_vfs_union_directory_open(
        assigns, base, supervisor_vfs_assign_client, NULL, &directory);
    if (status != ASTRA_VFS_OK)
        return status;
    for (;;) {
        const AstraAssign *assign = NULL;
        AstraVfsClient *client;
        char wire[ASTRA_VFS_PATH_MAX];
        uint32_t count = 0u;
        uint32_t member = 0u;

        failure = "PROC library directory read";
        status = astra_vfs_union_directory_read(
            &directory, entries,
            (uint32_t)(sizeof(entries) / sizeof(entries[0])), &count,
            &member);
        if (status != ASTRA_VFS_OK || count == 0u)
            break;
        failure = "PROC library member resolve";
        status = astra_assign_resolve(assigns, base, ASTRA_RIGHT_READ,
                                      member, wire, sizeof(wire), &assign);
        if (status != ASTRA_VFS_OK)
            break;
        client = supervisor_vfs_client_for(assign);
        if (client == NULL) {
            status = ASTRA_VFS_ERR_PEER;
            break;
        }
        for (uint32_t item = 0u; item < count; ++item) {
            AstraLibraryReference library;
            const uint8_t *bytes = NULL;
            uint32_t moved = 0u;
            uint64_t index_size = 0u;
            uint64_t binary_size = 0u;
            uint16_t abi = 0u;
            char name[ASTRA_LIBRARY_NAME_MAX];
            char index_path[ASTRA_VFS_PATH_MAX] = {0};
            char target[ASTRA_VFS_PATH_MAX];

            if (entries[item].kind != ASTRA_VFS_KIND_FILE)
                continue;
            if (!provider_leaf(entries[item].name, name, &abi)) {
                failure = "PROC library provider name";
                status = ASTRA_VFS_ERR_PROTOCOL;
                break;
            }
            if (!path_append(index_path, sizeof(index_path), wire) ||
                !path_append(index_path, sizeof(index_path), "/") ||
                !path_append(index_path, sizeof(index_path),
                             entries[item].name)) {
                failure = "PROC library provider path";
                status = ASTRA_VFS_ERR_LIMIT;
                break;
            }
            failure = "PROC library provider read";
            status = astra_vfs_port_read_path_inline(
                client, index_path, &bytes, &moved, &index_size);
            if (status != ASTRA_VFS_OK)
                break;
            if (moved != index_size || bytes == NULL) {
                failure = "PROC library provider length";
                status = ASTRA_VFS_ERR_PROTOCOL;
                break;
            }
            if (!astra_vfs_provider_index_parse(
                    bytes, moved, "LIBS", name, abi, target, sizeof(target),
                    &library)) {
                failure = "PROC library provider parse";
                status = ASTRA_VFS_ERR_PROTOCOL;
                break;
            }
            failure = "PROC library binary stat";
            status = provider_size(target, &binary_size);
            if (status != ASTRA_VFS_OK)
                break;
            render_disk_row(text, &library, binary_size, target);
        }
        if (status != ASTRA_VFS_OK)
            break;
    }
    astra_vfs_union_directory_close(&directory);
    if (status != ASTRA_VFS_OK)
        (void)astra_log_failure(failure, status);
    return status;
}

/*
 * A path is "", "<id>" or "<id>/status", with an optional leading slash so a
 * caller that built one by joining is not punished for the join. Returns the
 * snapshot index, or UINT32_MAX when no process matches.
 */
static uint32_t
parse_path(const char *path, enum ProcProcessLeaf *leaf)
{
    uint32_t id = 0u;
    uint32_t digits = 0u;
    uint32_t at = 0u;

    *leaf = PROC_PROCESS_DIRECTORY;
    if (path == NULL)
        return UINT32_MAX;
    while (path[at] == '/')
        ++at;
    while (path[at] >= '0' && path[at] <= '9' && digits < 10u) {
        uint32_t digit = (uint32_t)(path[at] - '0');

        if (id > (ASTRA_PROCESS_ID_MAX - digit) / 10u)
            return UINT32_MAX;
        id = (id * 10u) + digit;
        ++at;
        ++digits;
    }
    if (digits == 0u)
        return UINT32_MAX;
    while (path[at] == '/')
        ++at;
    if (path[at] != '\0') {
        if (supervisor_proc_path_equal(path + at, "status"))
            *leaf = PROC_PROCESS_STATUS;
        else if (supervisor_proc_path_equal(path + at, "libraries"))
            *leaf = PROC_PROCESS_LIBRARIES;
        else
            return UINT32_MAX;
    }
    for (uint32_t index = 0u; index < snapshot_count; ++index) {
        if (snapshot[index].process.id == id)
            return index;
    }
    return UINT32_MAX;
}

static uint32_t
render_status(uint32_t index, ProcText *text)
{
    const AstraProcessInfo *info;

    if (index >= snapshot_count || snapshot[index].process.id == 0u)
        return ASTRA_VFS_ERR_NOT_FOUND;
    info = &snapshot[index].process;
    proc_text_string(text, "name ", UINT32_MAX);
    proc_text_string(text, snapshot[index].name,
                     sizeof(snapshot[index].name));
    proc_text_byte(text, '\n');
    proc_text_field(text, "id", info->id);
    /*
     * The generation travels with the identifier because a number alone must
     * never name a process here: a control operation carries the generation
     * the caller observed and the kernel refuses it if the slot was recycled.
     */
    proc_text_field(text, "generation", info->generation);
    proc_text_field(text, "owner", info->owner);
    proc_text_field(text, "state", info->process_state);
    proc_text_field(text, "thread_state", info->thread_state);
    proc_text_field(text, "suspended", info->suspended);
    proc_text_field(text, "threads", info->thread_count);
    proc_text_field(text, "live", info->live_threads);
    proc_text_field(text, "priority", info->default_priority);
    proc_text_field(text, "ceiling", info->priority_ceiling);
    proc_text_field(text, "frames", info->resident_frames);
    /* Schedule counts, not time. Named so nobody reads them as seconds. */
    proc_text_field(text, "runs", info->run_count);
    proc_text_field(text, "ticks", info->timer_ticks);
    proc_text_field(text, "syscalls", info->syscall_count);
    proc_text_field(text, "handles", info->handle_references);
    proc_text_field64(text, "runtime_ns", info->runtime_ns);
    proc_text_field64(text, "elapsed_ns", info->elapsed_ns);
    proc_text_field(text, "exit_reason", info->exit_reason);
    proc_text_field(text, "exit_status", info->exit_status);
    return ASTRA_VFS_OK;
}

static uint32_t
find_snapshot_process(uint32_t process_id, uint32_t generation)
{
    for (uint32_t index = 0u; index < snapshot_count; ++index) {
        if (snapshot[index].process.id == process_id &&
            snapshot[index].process.generation == generation)
            return index;
    }
    return UINT32_MAX;
}

static uint32_t
read_snapshot(uint64_t offset, uint8_t *out, uint32_t length, uint32_t *moved)
{
    const uint8_t *bytes;
    uint32_t total;

    *moved = 0u;
    if (refresh_snapshot() != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_IO;
    if (snapshot_count > UINT32_MAX / sizeof(*snapshot))
        return ASTRA_VFS_ERR_LIMIT;
    bytes = (const uint8_t *)snapshot;
    total = snapshot_count * (uint32_t)sizeof(*snapshot);
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
    enum ProcProcessLeaf leaf = PROC_PROCESS_DIRECTORY;
    uint32_t length = 0u;
    uint32_t index;

    (void)context;
    (void)create_mode;
    if ((flags & ASTRA_VFS_OPEN_WRITE) != 0u ||
        (flags & ASTRA_VFS_OPEN_CREATE) != 0u)
        return ASTRA_VFS_ERR_ACCESS;
    if (supervisor_proc_path_is_root(path)) {
        *node = PROC_NODE_ROOT;
        info->size = 0u;
        info->kind = ASTRA_VFS_KIND_DIRECTORY;
        info->mode = 0500u;
        info->nlink = 2u;
        return ASTRA_VFS_OK;
    }
    if (supervisor_proc_path_is_snapshot(path)) {
        if (refresh_snapshot() != ASTRA_VFS_OK)
            return ASTRA_VFS_ERR_IO;
        *node = PROC_NODE_SNAPSHOT;
        info->size = (uint64_t)snapshot_count * sizeof(*snapshot);
        info->kind = ASTRA_VFS_KIND_FILE;
        info->mode = 0400u;
        info->nlink = 1u;
        return ASTRA_VFS_OK;
    }
    if (supervisor_proc_path_is_libraries(path)) {
        *node = PROC_NODE_LIBRARIES;
        info->size = 0u;
        info->kind = ASTRA_VFS_KIND_DIRECTORY;
        info->mode = 0500u;
        info->nlink = 2u;
        return ASTRA_VFS_OK;
    }
    if (supervisor_proc_path_is_library_memory(path)) {
        ProcText text = {0};

        if (render_library_text(0u, &text) != ASTRA_VFS_OK)
            return ASTRA_VFS_ERR_IO;
        *node = PROC_NODE_LIBRARY_MEMORY;
        info->size = text.length;
        info->kind = ASTRA_VFS_KIND_FILE;
        info->mode = 0400u;
        info->nlink = 1u;
        return ASTRA_VFS_OK;
    }
    if (supervisor_proc_path_is_library_disk(path)) {
        ProcText text = {0};
        uint32_t status = render_disk_libraries(&text);

        if (status != ASTRA_VFS_OK)
            return status;
        *node = PROC_NODE_LIBRARY_DISK;
        info->size = text.length;
        info->kind = ASTRA_VFS_KIND_FILE;
        info->mode = 0400u;
        info->nlink = 1u;
        return ASTRA_VFS_OK;
    }
    if (refresh_snapshot() != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_IO;
    index = parse_path(path, &leaf);
    if (index >= snapshot_count)
        return ASTRA_VFS_ERR_NOT_FOUND;
    if (leaf == PROC_PROCESS_STATUS) {
        ProcText text = {0};

        if (render_status(index, &text) != ASTRA_VFS_OK)
            return ASTRA_VFS_ERR_NOT_FOUND;
        length = (uint32_t)text.length;
    } else if (leaf == PROC_PROCESS_LIBRARIES) {
        ProcText text = {0};

        if (render_library_text(snapshot[index].process.id, &text) !=
            ASTRA_VFS_OK)
            return ASTRA_VFS_ERR_IO;
        length = (uint32_t)text.length;
    }
    {
        ProcOpenNode *opened = astra_runtime_reallocate(NULL,
                                                        sizeof(*opened));

        if (opened == NULL)
            return ASTRA_VFS_ERR_LIMIT;
        opened->magic = PROC_OPEN_NODE_MAGIC;
        opened->process_id = snapshot[index].process.id;
        opened->generation = snapshot[index].process.generation;
        opened->leaf = leaf;
        *node = (uintptr_t)opened;
    }
    info->size = length;
    info->kind = leaf == PROC_PROCESS_DIRECTORY ?
                 ASTRA_VFS_KIND_DIRECTORY : ASTRA_VFS_KIND_FILE;
    info->mode = leaf == PROC_PROCESS_DIRECTORY ? 0500u : 0400u;
    info->nlink = leaf == PROC_PROCESS_DIRECTORY ? 2u : 1u;
    return ASTRA_VFS_OK;
}

static uint32_t
proc_close(void *context, uintptr_t node)
{
    ProcOpenNode *opened = (ProcOpenNode *)node;

    (void)context;
    if (node > PROC_NODE_LIBRARIES) {
        if (opened->magic != PROC_OPEN_NODE_MAGIC)
            return ASTRA_VFS_ERR_INVALID;
        opened->magic = 0u;
        (void)astra_runtime_reallocate(opened, 0u);
    }
    return ASTRA_VFS_OK;
}

static uint32_t
proc_read(void *context, uintptr_t node, uint64_t offset, void *buffer,
          uint32_t length, uint32_t *moved)
{
    ProcOpenNode *opened;
    uint32_t index;
    uint8_t *out = buffer;

    (void)context;
    *moved = 0u;
    if (node == PROC_NODE_SNAPSHOT)
        return read_snapshot(offset, out, length, moved);
    if (node == PROC_NODE_LIBRARY_MEMORY) {
        ProcText text = {
            .out = out,
            .offset = offset,
            .capacity = length,
        };

        if (render_library_text(0u, &text) != ASTRA_VFS_OK)
            return ASTRA_VFS_ERR_IO;
        *moved = text.moved;
        return ASTRA_VFS_OK;
    }
    if (node == PROC_NODE_LIBRARY_DISK) {
        ProcText text = {
            .out = out,
            .offset = offset,
            .capacity = length,
        };
        uint32_t status = render_disk_libraries(&text);

        if (status != ASTRA_VFS_OK)
            return status;
        *moved = text.moved;
        return ASTRA_VFS_OK;
    }
    if (node == PROC_NODE_ROOT || node == PROC_NODE_LIBRARIES)
        return ASTRA_VFS_ERR_IS_DIR;
    if (node <= PROC_NODE_LIBRARIES)
        return ASTRA_VFS_ERR_INVALID;
    opened = (ProcOpenNode *)node;
    if (opened->magic != PROC_OPEN_NODE_MAGIC)
        return ASTRA_VFS_ERR_INVALID;
    if (opened->leaf == PROC_PROCESS_DIRECTORY)
        return ASTRA_VFS_ERR_IS_DIR;
    if (opened->leaf != PROC_PROCESS_STATUS &&
        opened->leaf != PROC_PROCESS_LIBRARIES)
        return ASTRA_VFS_ERR_INVALID;
    if (refresh_snapshot() != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_IO;
    index = find_snapshot_process(opened->process_id, opened->generation);
    if (index == UINT32_MAX)
        return ASTRA_VFS_OK;
    if (opened->leaf == PROC_PROCESS_LIBRARIES) {
        ProcText text = {
            .out = out,
            .offset = offset,
            .capacity = length,
        };

        if (render_library_text(snapshot[index].process.id, &text) !=
            ASTRA_VFS_OK)
            return ASTRA_VFS_ERR_IO;
        *moved = text.moved;
        return ASTRA_VFS_OK;
    }
    {
        ProcText text = {
            .out = out,
            .offset = offset,
            .capacity = length,
        };

        if (render_status(index, &text) != ASTRA_VFS_OK)
            return ASTRA_VFS_OK; /* it exited; a short read is honest */
        *moved = text.moved;
    }
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
proc_stat_node(void *context, uintptr_t node, AstraVfsNodeInfo *info)
{
    ProcText text = {0};
    uint32_t status = ASTRA_VFS_OK;

    (void)context;
    if (info == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (node == PROC_NODE_ROOT || node == PROC_NODE_LIBRARIES) {
        info->size = 0u;
        info->kind = ASTRA_VFS_KIND_DIRECTORY;
        info->mode = 0500u;
        info->nlink = 2u;
        return ASTRA_VFS_OK;
    }
    if (node == PROC_NODE_SNAPSHOT) {
        status = refresh_snapshot();
        if (status == ASTRA_VFS_OK)
            info->size = (uint64_t)snapshot_count * sizeof(*snapshot);
    } else if (node == PROC_NODE_LIBRARY_MEMORY) {
        status = render_library_text(0u, &text);
        info->size = text.length;
    } else if (node == PROC_NODE_LIBRARY_DISK) {
        status = render_disk_libraries(&text);
        info->size = text.length;
    } else {
        ProcOpenNode *opened = (ProcOpenNode *)node;
        uint32_t index;

        if (node <= PROC_NODE_LIBRARIES)
            return ASTRA_VFS_ERR_INVALID;
        if (opened->magic != PROC_OPEN_NODE_MAGIC ||
            (opened->leaf != PROC_PROCESS_DIRECTORY &&
             opened->leaf != PROC_PROCESS_STATUS &&
             opened->leaf != PROC_PROCESS_LIBRARIES))
            return ASTRA_VFS_ERR_INVALID;
        status = refresh_snapshot();
        if (status != ASTRA_VFS_OK)
            return status;
        index = find_snapshot_process(opened->process_id,
                                      opened->generation);
        if (opened->leaf == PROC_PROCESS_DIRECTORY) {
            if (index == UINT32_MAX)
                return ASTRA_VFS_ERR_NOT_FOUND;
            info->size = 0u;
            info->kind = ASTRA_VFS_KIND_DIRECTORY;
            info->mode = 0500u;
            info->nlink = 2u;
            return ASTRA_VFS_OK;
        }
        if (index != UINT32_MAX) {
            status = opened->leaf == PROC_PROCESS_STATUS ?
                render_status(index, &text) :
                render_library_text(opened->process_id, &text);
        }
        info->size = text.length;
    }
    if (status != ASTRA_VFS_OK)
        return status;
    info->kind = ASTRA_VFS_KIND_FILE;
    info->mode = 0400u;
    info->nlink = 1u;
    return ASTRA_VFS_OK;
}

static uint32_t
proc_readdir(void *context, uintptr_t directory, const char *path,
             uint64_t cookie, char *name, uint32_t capacity,
             AstraVfsNodeInfo *info, uint64_t *next)
{
    enum ProcProcessLeaf leaf = PROC_PROCESS_DIRECTORY;
    uint32_t directory_index = UINT32_MAX;

    (void)context;
    if (directory != 0u) {
        if (path == NULL || path[0] != '\0')
            return ASTRA_VFS_ERR_UNSUPPORTED;
        if (directory == PROC_NODE_ROOT)
            path = "/";
        else if (directory == PROC_NODE_LIBRARIES)
            path = "/libraries";
        else if (directory <= PROC_NODE_LIBRARIES)
            return ASTRA_VFS_ERR_NOT_DIR;
        else {
            const ProcOpenNode *opened = (const ProcOpenNode *)directory;

            if (opened->magic != PROC_OPEN_NODE_MAGIC)
                return ASTRA_VFS_ERR_BAD_HANDLE;
            if (opened->leaf != PROC_PROCESS_DIRECTORY)
                return ASTRA_VFS_ERR_NOT_DIR;
            if (refresh_snapshot() != ASTRA_VFS_OK)
                return ASTRA_VFS_ERR_IO;
            directory_index = find_snapshot_process(opened->process_id,
                                                     opened->generation);
            if (directory_index == UINT32_MAX)
                return ASTRA_VFS_ERR_NOT_FOUND;
        }
    }
    if (supervisor_proc_path_is_libraries(path)) {
        static const char *const names[] = {"memory", "disk"};

        if (cookie >= sizeof(names) / sizeof(names[0]))
            return ASTRA_VFS_ERR_NOT_FOUND;
        if (capacity < 7u)
            return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
        {
            uint32_t at = 0u;

            do {
                name[at] = names[cookie][at];
            } while (names[cookie][at++] != '\0');
        }
        info->size = 0u;
        info->kind = ASTRA_VFS_KIND_FILE;
        info->mode = 0400u;
        info->nlink = 1u;
        *next = cookie + 1u;
        return ASTRA_VFS_OK;
    }
    if (directory_index != UINT32_MAX ||
        !supervisor_proc_path_is_root(path)) {
        uint32_t index = directory_index;

        if (index == UINT32_MAX) {
            if (refresh_snapshot() != ASTRA_VFS_OK)
                return ASTRA_VFS_ERR_IO;
            index = parse_path(path, &leaf);
        }

        if (leaf != PROC_PROCESS_DIRECTORY ||
            index >= snapshot_count)
            return ASTRA_VFS_ERR_NOT_FOUND;
        if (cookie >= 2u)
            return ASTRA_VFS_ERR_NOT_FOUND;
        if (capacity < sizeof("libraries"))
            return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
        {
            const char *entry = cookie == 0u ? "status" : "libraries";
            uint32_t at = 0u;

            do {
                name[at] = entry[at];
            } while (entry[at++] != '\0');
        }
        info->size = 0u;
        info->kind = ASTRA_VFS_KIND_FILE;
        info->mode = 0400u;
        info->nlink = 1u;
        *next = cookie + 1u;
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
    if (cookie == 1u) {
        if (capacity < sizeof("libraries"))
            return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
        for (uint32_t at = 0u; at < sizeof("libraries"); ++at)
            name[at] = "libraries"[at];
        info->size = 0u;
        info->kind = ASTRA_VFS_KIND_DIRECTORY;
        info->mode = 0500u;
        info->nlink = 2u;
        *next = 2u;
        return ASTRA_VFS_OK;
    }
    if (refresh_snapshot() != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_IO;
    for (uint32_t index = (uint32_t)cookie - 2u;
         index < snapshot_count; ++index) {
        const AstraProcessInfo *process = &snapshot[index].process;
        ProcText text = {
            .out = (uint8_t *)(void *)name,
            .capacity = capacity == 0u ? 0u : capacity - 1u,
        };

        if (process->id == 0u)
            continue;
        proc_text_number(&text, process->id);
        if (text.length >= capacity)
            return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
        name[text.moved] = '\0';
        info->size = 0u;
        info->kind = ASTRA_VFS_KIND_DIRECTORY;
        info->mode = 0500u;
        info->nlink = 2u;
        *next = index + 3u;
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
    .open_at = astra_vfs_backend_no_open_at,
    .unlink_at = astra_vfs_backend_no_unlink_at,
    .chmod_node = astra_vfs_backend_no_chmod_node,
    .chmod_at = astra_vfs_backend_no_chmod_at,
    .filesystem_info = astra_vfs_backend_no_filesystem_info,
    .stat_at = astra_vfs_backend_no_stat_at,
    .stat_node = proc_stat_node,
};

const AstraVfsBackendOps *
supervisor_proc_ops(void)
{
    return &proc_ops;
}
