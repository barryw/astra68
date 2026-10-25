#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>

#include <astra/library.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_process.h>
#include <astra/vfs_path.h>
#include <astra/vfs_provider_index.h>
#include <astra/volume.h>
#include <astra/runtime.h>
#include <astra/vfs_file_lifetime.h>
#include <astra/ascii.h>

static uint32_t closes;
static uint32_t connects;
static uint32_t disconnects;
static uint32_t abandons;
static uint32_t fail_connect;
static uint32_t seed_distinct;
static uint32_t direct_connect;
static uint32_t direct_resumes;
static uint32_t fail_direct_resume;
static uint32_t direct_forks;
static uint32_t port_fork_rebinds;
static uint32_t next_semaphore = 100u;
static uint32_t seeds;
static int fail_allocate;
static int force_move;
static int whole_file_enabled;
static uint32_t whole_file_moved;
static uint64_t whole_file_node_size;
static const uint8_t *whole_file_bytes;
static const uint8_t default_file_content[] = "hello";
static const uint8_t *mock_file_content = default_file_content;
static uint32_t mock_file_length = 5u;
static const char *held_directory_path = "/before";
static uint32_t held_directory_status;
static uint32_t adopt_status;
static uint32_t share_status;
static AstraAssign mock_assigns[ASTRA_STARTUP_CAPABILITY_MAX + 16u];
static int catalog_enabled;
static uint32_t catalog_count;
static AstraVolumeRecord catalog_records[8];
static int catalog_changed;
static AstraResult catalog_failure;
static uint32_t root_requests;
static uint32_t root_closes;
static uint32_t file_closes;
static uint32_t watch_opens;
static uint32_t next_root = 50000u;
static uint32_t global_brokers = ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE;
static uint32_t ram_brokers;
static AstraVolumeReference ram_source = {77u, 600u, 1u};
static AstraResult ram_info_failure;
static AstraHandle last_root_broker;
static uint32_t last_root_rights;

const AstraStartupCapability *astra_startup_capability(const AstraStartupInfo *startup, const char *name)
{
    (void)startup;
    static const AstraStartupCapability capabilities[] = {
        {.name = "VOLUMES", .handle = 90001u},
        {.name = "VOLUME_READ", .handle = 90002u},
        {.name = "VOLUME_WRITE", .handle = 90003u},
        {.name = "RAM_READ", .handle = 90005u},
        {.name = "RAM_WRITE", .handle = 90006u}};
    if (!catalog_enabled) return NULL;
    for (uint32_t at = 0u; at < sizeof(capabilities) / sizeof(capabilities[0]); ++at)
        if (strcmp(name, capabilities[at].name) == 0) {
            if (at == 1u && (global_brokers & ASTRA_RIGHT_READ) == 0u) return NULL;
            if (at == 2u && (global_brokers & ASTRA_RIGHT_WRITE) == 0u) return NULL;
            if (at == 3u && (ram_brokers & ASTRA_RIGHT_READ) == 0u) return NULL;
            if (at == 4u && (ram_brokers & ASTRA_RIGHT_WRITE) == 0u) return NULL;
            return &capabilities[at];
        }
    return NULL;
}
uint32_t astra_close(uint32_t handle) { if (handle >= 50000u && handle < 90000u) ++root_closes; return ASTRA_SYSCALL_OK; }
AstraResult astra_volume_watch_open(AstraHandle service, AstraVolumeWatch *watch)
{
    assert(service == 90001u); ++watch_opens;
    *watch = (AstraVolumeWatch){watch_opens, 90004u, 0u};
    return ASTRA_OK;
}
AstraResult astra_volume_watch_receive(const AstraVolumeWatch *watch, AstraVolumeChange *change)
{
    (void)watch; (void)change;
    if (!catalog_changed) return ASTRA_ERROR_WOULD_BLOCK;
    catalog_changed = 0;
    return ASTRA_OK;
}
AstraResult astra_volume_watch_close(AstraHandle service, AstraVolumeWatch *watch)
{
    (void)service; *watch = (AstraVolumeWatch)ASTRA_VOLUME_WATCH_INIT; return ASTRA_OK;
}
AstraResult astra_volume_snapshot_open(AstraHandle service, AstraVolumeSnapshot *snapshot)
{
    assert(service == 90001u);
    if (catalog_failure != ASTRA_OK) return catalog_failure;
    if (catalog_count != 0u) {
        snapshot->records = astra_runtime_allocate(catalog_count * sizeof(AstraVolumeRecord));
        if (snapshot->records == NULL) return ASTRA_ERROR_OUT_OF_MEMORY;
        memcpy(snapshot->records, catalog_records, catalog_count * sizeof(AstraVolumeRecord));
    }
    snapshot->epoch = 77u; snapshot->sequence = 1u; snapshot->count = catalog_count;
    return ASTRA_OK;
}
void astra_volume_snapshot_close(AstraVolumeSnapshot *snapshot)
{
    astra_runtime_deallocate(snapshot->records); *snapshot = (AstraVolumeSnapshot)ASTRA_VOLUME_SNAPSHOT_INIT;
}
AstraResult astra_volume_root(AstraHandle broker, const AstraVolumeReference *reference, uint32_t rights, AstraHandle *root)
{
    assert(broker == 90002u || broker == 90003u || broker == 90005u || broker == 90006u);
    assert((rights & ASTRA_RIGHT_READ) != 0u);
    if (broker == 90002u || broker == 90005u) assert(rights == ASTRA_RIGHT_READ);
    for (uint32_t at = 0u; at < catalog_count; ++at)
        if (reference->epoch == 77u && reference->id == catalog_records[at].id &&
            reference->generation == catalog_records[at].generation) {
            if (broker == 90005u || broker == 90006u)
                assert(catalog_records[at].parent == ram_source.id &&
                    catalog_records[at].parent_generation == ram_source.generation);
            last_root_broker = broker; last_root_rights = rights;
            ++root_requests; *root = next_root++; return ASTRA_OK;
        }
    return ASTRA_ERROR_IO;
}
AstraResult astra_volume_broker_info(AstraHandle broker, AstraVolumeBrokerInfo *info)
{
    assert(broker == 90005u || broker == 90006u);
    if (ram_info_failure != ASTRA_OK) return ram_info_failure;
    *info = (AstraVolumeBrokerInfo){.source = ram_source,
        .rights = ASTRA_RIGHT_READ | (broker == 90006u ? ASTRA_RIGHT_WRITE : 0u)};
    return ASTRA_OK;
}

static uint32_t mock_binding(AstraAssignTable *table, const char *name,
    uint32_t handle, uint32_t rights, const char *root, const AstraVolumeReference *volume)
{
    if (table->count == table->capacity) {
        uint32_t capacity = table->capacity == 0u ? 8u : table->capacity * 2u;
        AstraAssign *grown = astra_runtime_reallocate(table->entries, capacity * sizeof(*grown));
        if (grown == NULL) return ASTRA_VFS_ERR_LIMIT;
        table->entries = grown; table->capacity = capacity;
    }
    AstraAssign *binding = &table->entries[table->count++];
    *binding = (AstraAssign){.handle = handle, .rights = rights};
    for (uint32_t at = 0u; at + 1u < sizeof(binding->name) && name[at] != '\0'; ++at)
        binding->name[at] = astra_ascii_upper(name[at]);
    binding->root = astra_runtime_allocate(strlen(root) + 1u);
    if (binding->root == NULL) { --table->count; return ASTRA_VFS_ERR_LIMIT; }
    strcpy(binding->root, root);
    if (volume != NULL) binding->volume = *volume;
    return ASTRA_VFS_OK;
}
uint32_t astra_assign_bind(AstraAssignTable *table, const char *name, uint32_t handle, uint32_t rights, const char *root)
{ return mock_binding(table, name, handle, rights, root, NULL); }
uint32_t astra_assign_join(AstraAssignTable *table, const char *name, uint32_t handle, uint32_t rights, const char *root)
{ return mock_binding(table, name, handle, rights, root, NULL); }
uint32_t astra_assign_bind_volume(AstraAssignTable *table, const char *name, const AstraVolumeReference *volume, uint32_t broker, uint32_t rights)
{
    if (broker == 0u) return ASTRA_VFS_ERR_INVALID;
    uint32_t status = mock_binding(table, name, 0u, rights, "", volume);
    if (status == ASTRA_VFS_OK) table->entries[table->count - 1u].broker = broker;
    return status;
}
void astra_assign_table_destroy(AstraAssignTable *table)
{
    if (table->entries != mock_assigns) {
        for (uint32_t at = 0u; at < table->count; ++at)
            astra_runtime_deallocate(table->entries[at].root);
        astra_runtime_deallocate(table->entries);
    }
    *table = (AstraAssignTable)ASTRA_ASSIGN_TABLE_INIT;
}
uint32_t astra_vfs_assign_clone(AstraAssignTable *target, const AstraAssignTable *source)
{
    for (uint32_t at = 0u; at < source->count; ++at) {
        const AstraAssign *entry = &source->entries[at];
        uint32_t status = mock_binding(target, entry->name, entry->handle,
            entry->rights, entry->root, &entry->volume);
        if (status != ASTRA_VFS_OK) { astra_assign_table_destroy(target); return status; }
        target->entries[at].broker = entry->broker;
    }
    return ASTRA_VFS_OK;
}

typedef union MockAllocation {
    max_align_t alignment;
    size_t size;
} MockAllocation;

void *astra_runtime_allocate(size_t size)
{
    if (fail_allocate || size > SIZE_MAX - sizeof(MockAllocation))
        return NULL;
    MockAllocation *allocation = malloc(sizeof(*allocation) + size);
    if (allocation == NULL) return NULL;
    allocation->size = size;
    return allocation + 1;
}
void *astra_runtime_reallocate(void *pointer, size_t size)
{
    MockAllocation *old = pointer != NULL ? (MockAllocation *)pointer - 1 : NULL;
    MockAllocation *grown;

    if (fail_allocate || size > SIZE_MAX - sizeof(MockAllocation))
        return NULL;
    if (force_move && old != NULL) {
        grown = malloc(sizeof(*grown) + size);
        if (grown == NULL)
            return NULL;
        memcpy(grown + 1, pointer, size < old->size ? size : old->size);
        free(old);
    } else {
        grown = realloc(old, sizeof(*grown) + size);
    }
    if (grown != NULL)
        grown->size = size;
    return grown != NULL ? grown + 1 : NULL;
}
void astra_runtime_deallocate(void *pointer)
{ if (pointer != NULL) free((MockAllocation *)pointer - 1); }

uint32_t astra_log_failure(const char *operation, uint32_t status)
{
    (void)operation;
    return status;
}

int astra_startup_validate(const AstraStartupInfo *startup)
{
    return startup != NULL;
}

uint32_t astra_rt_semaphore_create(uint32_t initial, uint32_t maximum,
                                   uint32_t rights, uint32_t *handle)
{
    (void)initial;
    (void)maximum;
    (void)rights;
    if (handle == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    *handle = next_semaphore++;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_assign_seed(AstraAssignTable *table,
                           const AstraStartupCapability *capabilities,
                           uint32_t count)
{
    (void)capabilities;
    (void)count;
    ++seeds;
    memset(table, 0, sizeof(*table));
    memset(mock_assigns, 0, sizeof(mock_assigns));
    table->entries = mock_assigns;
    for (uint32_t at = 0u; at < sizeof(mock_assigns) / sizeof(mock_assigns[0]); ++at)
        mock_assigns[at].root = "";
    table->capacity = (uint32_t)(sizeof(mock_assigns) / sizeof(mock_assigns[0]));
    if (seed_distinct != 0u) {
        table->count = ASTRA_STARTUP_CAPABILITY_MAX;
        for (uint32_t index = 0u; index < table->count; ++index)
            table->entries[index].handle = index + 1u;
        return ASTRA_VFS_OK;
    }
    table->count = 3u;
    (void)strcpy(table->entries[0].name, "CWD");
    table->entries[0].handle = 11u;
    table->entries[0].rights = ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE;
    (void)strcpy(table->entries[1].name, "WORK");
    table->entries[1].handle = 11u;
    table->entries[1].rights = ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE;
    (void)strcpy(table->entries[2].name, "LIBS");
    table->entries[2].handle = 22u;
    table->entries[2].rights = ASTRA_RIGHT_READ;
    return ASTRA_VFS_OK;
}

const AstraAssign *astra_assign_lookup(const AstraAssignTable *table,
                                       const char *name)
{
    for (uint32_t index = 0u; index < table->count; ++index)
        if (strcmp(table->entries[index].name, name) == 0)
            return &table->entries[index];
    return NULL;
}

/*
 * astra_process_read_file takes the one-round-trip path when the service can
 * offer it. This suite exercises the resolver, not the transport, so both
 * refuse and the caller falls back to the library path it already covers.
 */
uint32_t astra_assign_resolve_raw(const AstraAssignTable *table,
                              const char *path,
                              uint32_t rights, uint32_t member, char *wire,
                              uint32_t capacity, const AstraAssign **assign)
{
    (void)path;
    (void)rights;
    (void)member;
    if (whole_file_enabled != 0 && table != NULL && table->count != 0u &&
        wire != NULL && capacity >= 2u && assign != NULL) {
        wire[0] = '/';
        wire[1] = '\0';
        *assign = &table->entries[0];
        return ASTRA_VFS_OK;
    }
    return ASTRA_VFS_ERR_NOT_FOUND;
}

uint32_t astra_vfs_port_read_path(AstraVfsClient *client, const char *path,
                                  const uint8_t **bytes, uint32_t *moved,
                                  uint64_t *node_size)
{
    (void)client;
    (void)path;
    if (whole_file_enabled != 0 && bytes != NULL && moved != NULL &&
        node_size != NULL) {
        *bytes = whole_file_bytes;
        *moved = whole_file_moved;
        *node_size = whole_file_node_size;
        return ASTRA_VFS_OK;
    }
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

uint32_t astra_vfs_port_read_bulk_position(AstraVfsClient *client, AstraVfsFile file,
                                  uint64_t offset, uint32_t flags, void *buffer,
                                  uint32_t length, uint32_t *moved,
                                  uint64_t *position)
{
    (void)client; (void)file; (void)offset; (void)buffer; (void)length;
    (void)moved; (void)flags; (void)position;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

uint32_t astra_vfs_port_write_bulk_position(
    AstraVfsClient *client, AstraVfsFile file, uint64_t offset,
    uint32_t flags, uint64_t max_end, const void *buffer, uint32_t length, uint32_t *moved,
    uint64_t *position)
{
    (void)max_end;
    (void)client; (void)file; (void)offset; (void)flags; (void)buffer;
    (void)length; (void)moved; (void)position;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

uint32_t astra_vfs_port_transport(void *context, uint32_t operation,
                                  const AstraVfsRequest *request,
                                  const AstraVfsTransfer *transfer,
                                  AstraVfsReply *reply)
{
    (void)transfer;
    (void)context; (void)operation; (void)request; (void)reply;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

AstraVfsCallState *astra_vfs_port_call_acquire(AstraVfsClient *client)
{
    return &client->call;
}

const uint8_t *astra_vfs_port_call_area(const AstraVfsClient *client,
                                        uint32_t *capacity)
{
    if (capacity != NULL)
        *capacity = client->port_lanes[0].area_size;
    return client->port_lanes[0].area_address;
}

uint32_t astra_vfs_port_exec_lane_export(AstraVfsClient *client,
                                         AstraVfsPortExecLane *state)
{
    if (client == NULL || state == NULL)
        return ASTRA_VFS_ERR_INVALID;
    memset(state, 0, sizeof(*state));
    state->owner_thread = 1u;
    state->session = client->session;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_port_exec_lane_import(AstraVfsClient *client,
                                         const AstraVfsPortExecLane *state)
{
    if (client == NULL || state == NULL || state->owner_thread == 0u)
        return ASTRA_VFS_ERR_INVALID;
    client->port_lanes[0].owner_thread = state->owner_thread;
    client->port_lanes[0].session = state->session;
    return ASTRA_VFS_OK;
}

uint32_t astra_rt_area_map(uint32_t handle, uint32_t flags, void **address,
                           uint32_t *span)
{
    (void)handle; (void)flags; (void)address; (void)span;
    return ASTRA_SYSCALL_INVALID_HANDLE;
}

uint32_t astra_vfs_port_connect(AstraVfsClient *client, uint32_t service)
{
    ++connects;
    if (connects == fail_connect)
        return ASTRA_VFS_ERR_IO;
    client->session = service;
    client->port_service = service;
    client->version = ASTRA_VFS_VERSION;
    client->port_lifecycle = 0u;
    return ASTRA_VFS_OK;
}

int astra_vfs_port_client_closed(const AstraVfsClient *client)
{
    return client->port_lifecycle == 1u;
}

uint32_t astra_vfs_client_connect_service(AstraVfsClient *client,
                                          uint32_t service)
{
    uint32_t status = astra_vfs_port_connect(client, service);

    if (status == ASTRA_VFS_OK && direct_connect != 0u) {
        client->port_direct_address = (void *)(uintptr_t)1u;
        client->port_direct_area = 0x41u;
        client->port_direct_scope = 0x42u;
        client->port_direct_session = 0x43u;
        client->features = ASTRA_VFS_FEATURE_GRANTS;
    }
    return status;
}

uint32_t astra_vfs_host_direct_resume(AstraVfsClient *client, uint32_t area,
                                      uint32_t scope, uint32_t session)
{
    assert(client != NULL && area == 0x41u && scope == 0x42u &&
           session == 0x43u);
    ++direct_resumes;
    if (fail_direct_resume != 0u)
        return ASTRA_VFS_ERR_IO;
    client->port_direct_address = (void *)(uintptr_t)1u;
    client->port_direct_area = area;
    client->port_direct_scope = scope;
    client->port_direct_session = session;
    return ASTRA_VFS_OK;
}

static uint32_t fork_child_calls;

void astra_vfs_host_direct_fork_child(void)
{
    ++fork_child_calls;
}

uint32_t astra_vfs_host_direct_after_fork(AstraVfsClient *client)
{
    assert(client != NULL && client->port_direct_address != NULL);
    ++direct_forks;
    return ASTRA_VFS_OK;
}

void astra_vfs_port_abandon(AstraVfsClient *client)
{
    ++abandons;
    client->session = ASTRA_VFS_SESSION_INVALID;
}

void astra_vfs_port_after_fork_child(AstraVfsClient *client)
{
    assert(client != NULL);
    ++port_fork_rebinds;
}

uint32_t astra_vfs_disconnect(AstraVfsClient *client)
{
    assert(client->session != ASTRA_VFS_SESSION_INVALID);
    ++disconnects;
    client->session = ASTRA_VFS_SESSION_INVALID;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_open(AstraFilesystem *filesystem, const char *path,
                               uint32_t flags, AstraFile *file)
{
    (void)filesystem;
    assert((strcmp(path, "/app/test") == 0 ||
            strcmp(path, "/app/link/../test") == 0) && flags == ASTRA_VFS_OPEN_READ);
    file->_private_file = 1u;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_attach_io(AstraFilesystem *filesystem,
    const AstraAssignTable *table, AstraVfsAssignClientFn lookup,
    AstraFilesystemReadFn read_at, AstraFilesystemWriteFn write_at, void *context)
{
    (void)lookup; (void)read_at; (void)write_at; (void)context;
    filesystem->_private_assigns = table;
    return ASTRA_VFS_OK;
}

void astra_filesystem_detach(AstraFilesystem *filesystem)
{
    *filesystem = (AstraFilesystem)ASTRA_FILESYSTEM_INIT;
}

uint32_t astra_filesystem_file_info(const AstraFile *file,
                                    AstraFileInfo *info)
{
    assert(file->_private_file == 1u);
    info->byte_size = mock_file_length;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_read(AstraFile *file, void *bytes,
                               uint32_t capacity, uint32_t *moved)
{
    uint32_t offset = (uint32_t)file->_private_offset;
    uint32_t count = capacity < 2u ? capacity : 2u;

    if (count > mock_file_length - offset) count = mock_file_length - offset;
    memcpy(bytes, mock_file_content + offset, count);
    file->_private_offset += count;
    *moved = count;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_close(AstraFile *file)
{
    ++closes;
    file->_private_file = ASTRA_VFS_FILE_INVALID;
    return ASTRA_VFS_OK;
}

uint32_t astra_futex_wait(volatile uint32_t *word, uint32_t expected, uint64_t deadline)
{
    (void)word; (void)expected; (void)deadline;
    sched_yield();
    return ASTRA_SYSCALL_WOULD_BLOCK;
}
uint32_t astra_futex_wake(volatile uint32_t *word, uint32_t limit, uint32_t *woken)
{
    (void)word; (void)limit;
    if (woken != NULL) *woken = 0u;
    return ASTRA_SYSCALL_OK;
}
const AstraAssign *astra_assign_member(const AstraAssignTable *table, const char *name, uint32_t member)
{
    return member == 0u ? astra_assign_lookup(table, name) : NULL;
}
uint32_t astra_vfs_assign_stat(const AstraAssignTable *table, const char *path,
    uint32_t rights, AstraVfsAssignClientFn lookup, void *context, char *wire,
    uint32_t capacity, AstraVfsDirEntry *info, AstraVfsClient **client,
    const AstraAssign **binding, uint32_t *member)
{
    char name[ASTRA_CAPABILITY_NAME_MAX], rest[ASTRA_VFS_PATH_MAX];
    (void)rights;
    if (astra_path_split(path, name, sizeof(name), rest, sizeof(rest)) != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_INVALID;
    *binding = astra_assign_lookup(table, name);
    if (*binding == NULL) return ASTRA_VFS_ERR_NOT_FOUND;
    *client = lookup(*binding, context);
    if (*client == NULL) return ASTRA_VFS_ERR_IO;
    assert(strlen(rest) < capacity);
    strcpy(wire, rest);
    *info = (AstraVfsDirEntry){.kind = ASTRA_VFS_KIND_DIRECTORY};
    *member = 0u;
    return ASTRA_VFS_OK;
}
uint32_t astra_vfs_open(AstraVfsClient *client, const char *path, uint32_t flags,
    AstraVfsFile *file, uint64_t *size, uint16_t *kind)
{
    (void)client; (void)path;
    assert(flags == (ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY));
    *file = 1000u;
    if (size != NULL) *size = 0u;
    if (kind != NULL) *kind = ASTRA_VFS_KIND_DIRECTORY;
    if (client->description_acquire != NULL) client->description_acquire(client);
    return ASTRA_VFS_OK;
}
uint32_t astra_vfs_open_at(AstraVfsClient *client, AstraVfsFile directory,
    const char *path, uint32_t flags, AstraVfsFile *file, uint64_t *size, uint16_t *kind)
{
    (void)directory;
    return astra_vfs_open(client, path, flags, file, size, kind);
}
uint32_t astra_vfs_close(AstraVfsClient *client, AstraVfsFile file)
{
    assert(file != ASTRA_VFS_FILE_INVALID);
    ++file_closes;
    if (client->description_release != NULL)
        client->description_release(client, client->ownership_epoch);
    return ASTRA_VFS_OK;
}
uint32_t astra_vfs_directory_path(AstraVfsClient *client, AstraVfsFile file,
    void *buffer, uint32_t capacity, uint32_t *length)
{
    assert(client != NULL && file == 1000u);
    if (held_directory_status != ASTRA_VFS_OK)
        return held_directory_status;
    *length = (uint32_t)strlen(held_directory_path);
    if (*length > capacity)
        return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    memcpy(buffer, held_directory_path, *length);
    return ASTRA_VFS_OK;
}
uint32_t astra_vfs_adopt(AstraVfsClient *client, uint64_t token, AstraVfsFile *file)
{
    (void)client;
    assert(token != 0u);
    if (adopt_status != ASTRA_VFS_OK)
        return adopt_status;
    *file = 1000u;
    if (client->description_acquire != NULL) client->description_acquire(client);
    return ASTRA_VFS_OK;
}
uint32_t astra_vfs_share(AstraVfsClient *client, AstraVfsFile file, uint64_t *token)
{
    assert(client != NULL && file == 1000u && token != NULL);
    if (share_status != ASTRA_VFS_OK) return share_status;
    *token = 1u;
    return ASTRA_VFS_OK;
}
static pthread_barrier_t cache_start;
static AstraVfsClient *thread_clients[16];

static void *lookup_client_thread(void *argument)
{
    uint32_t index = (uint32_t)(uintptr_t)argument;
    AstraStartupInfo startup = {.capabilities_address = 1u};
    AstraAssign assign = {.handle = 9000u + index};

    pthread_barrier_wait(&cache_start);
    assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
    thread_clients[index] = astra_process_vfs_client_for(&assign);
    assert(thread_clients[index] != NULL);
    for (uint32_t pass = 0u; pass < 50u; ++pass)
        assert(astra_process_vfs_client_for(&assign) == thread_clients[index]);
    return NULL;
}

int main(void)
{
    static const uint8_t indexed[] = {
        'A', 'P', 'R', 'V', 0, 2, 0, 24,
        0, 1, 0, 2, 0, 3, 0, 1, 0, 4, 0, 0,
        0x12, 0x34, 0x56, 0x78,
        'F', 'i', 'l', 'e'
    };
    static const uint8_t legacy_absolute[] = {
        'A', 'P', 'R', 'V', 0, 1, 0, 24,
        0, 1, 0, 2, 0, 3, 0, 1, 0, 4, 0, 0,
        0x12, 0x34, 0x56, 0x78,
        'L', 'I', 'B', 'S', ':', 'F', 'i', 'l', 'e'
    };
    static const uint8_t absolute[] = {
        'A', 'P', 'R', 'V', 0, 2, 0, 24,
        0, 1, 0, 2, 0, 3, 0, 1, 0, 4, 0, 0,
        0x12, 0x34, 0x56, 0x78,
        'L', 'I', 'B', 'S', ':', 'F', 'i', 'l', 'e'
    };
    static const uint8_t traversal[] = {
        'A', 'P', 'R', 'V', 0, 2, 0, 24,
        0, 1, 0, 2, 0, 3, 0, 1, 0, 4, 0, 0,
        0x12, 0x34, 0x56, 0x78,
        '.', '.', '/', 'F', 'i', 'l', 'e'
    };
    char path[128];
    AstraLibraryReference reference;
    uint16_t abi = 0u;

    assert(astra_vfs_provider_identity_parse(
        "loader.library.1", path, sizeof(path), &abi));
    assert(strcmp(path, "loader.library") == 0 && abi == 1u);
    assert(astra_vfs_provider_identity_parse(
        "runtime.library.65535", path, sizeof(path), &abi));
    assert(strcmp(path, "runtime.library") == 0 && abi == 65535u);
    assert(!astra_vfs_provider_identity_parse(
        "loader.library", path, sizeof(path), &abi));
    assert(!astra_vfs_provider_identity_parse(
        "loader.library.01", path, sizeof(path), &abi));
    assert(!astra_vfs_provider_identity_parse(
        "loader.library.65536", path, sizeof(path), &abi));
    assert(!astra_vfs_provider_identity_parse(
        "loader/library.1", path, sizeof(path), &abi));

    assert(!astra_vfs_provider_index_parse(
        (const uint8_t *)"/libs/Filesystem.kit/library", 27u,
        "LIBS", "filesystem.library", 1u, path, sizeof(path), &reference));
    assert(path[0] == '\0');
    assert(reference.size == 0u);
    assert(astra_vfs_provider_index_parse(
        indexed, sizeof(indexed), "LIBS", "filesystem.library", 1u, path,
        sizeof(path), &reference));
    assert(strcmp(path, "/libs/File") == 0);
    assert(reference.size == ASTRA_LIBRARY_REFERENCE_SIZE);
    assert(strcmp(reference.name, "filesystem.library.1") == 0);
    assert(reference.major == 1u && reference.minor == 2u &&
           reference.patch == 3u && reference.abi_major == 1u &&
           reference.abi_minor == 4u &&
           reference.build_id == 0x12345678u);
    assert(!astra_vfs_provider_index_parse(
        (const uint8_t *)"/sys/Filesystem.kit/library", 26u,
        "LIBS", "filesystem.library", 1u, path, sizeof(path), &reference));
    assert(!astra_vfs_provider_index_parse(
        (const uint8_t *)"/libs/Filesystem kit/library", 27u,
        "LIBS", "filesystem.library", 1u, path, sizeof(path), &reference));
    assert(!astra_vfs_provider_index_parse(
        legacy_absolute, sizeof(legacy_absolute), "LIBS",
        "filesystem.library", 1u, path, sizeof(path), &reference));
    assert(!astra_vfs_provider_index_parse(
        absolute, sizeof(absolute), "LIBS", "filesystem.library", 1u,
        path, sizeof(path), &reference));
    assert(!astra_vfs_provider_index_parse(
        traversal, sizeof(traversal), "LIBS", "filesystem.library", 1u,
        path, sizeof(path), &reference));
    assert(!astra_vfs_provider_index_parse(
        indexed, sizeof(indexed), "BAD:ASSIGN", "filesystem.library", 1u,
        path, sizeof(path), &reference));

    {
        AstraStartupInfo startup = { .capabilities_address = 1u };

        /*
         * Seeding connects nothing. A connect is two cross-process round trips
         * and a program that never names a mount must never pay for it -- so
         * this counter staying at zero here is the whole of that claim.
         */
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        {
            char path[64];
            uint32_t seeded = seeds;

            assert(astra_process_path("file", path, sizeof(path)) ==
                       ASTRA_VFS_OK &&
                   strcmp(path, "file") == 0);
            assert(astra_process_vfs_set_current_directory("/cwd/proto") ==
                   ASTRA_VFS_OK);
            assert(astra_process_path("file", path, sizeof(path)) ==
                       ASTRA_VFS_OK &&
                   strcmp(path, "file") == 0);
            assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
            assert(seeds == seeded);
            assert(astra_process_path("file", path, sizeof(path)) ==
                       ASTRA_VFS_OK &&
                   strcmp(path, "file") == 0);
            assert(astra_process_vfs_after_fork_child(&startup, NULL) ==
                   ASTRA_VFS_OK);
            assert(seeds == seeded);
            assert(astra_process_path("file", path, sizeof(path)) ==
                       ASTRA_VFS_OK &&
                   strcmp(path, "file") == 0);
            assert(astra_process_vfs_set_current_directory("/work/other") ==
                   ASTRA_VFS_OK);
            assert(astra_process_path("file", path, sizeof(path)) ==
                       ASTRA_VFS_OK &&
                   strcmp(path, "file") == 0);
            assert(astra_process_vfs_set_current_directory("/missing") ==
                   ASTRA_VFS_ERR_NOT_FOUND);
        }
        assert(connects == 2u);
        /* First use is what connects, and only once however often it is asked. */
        assert(astra_process_vfs_client() != NULL);
        assert(connects == 2u);
        {
            AstraFile original = ASTRA_FILE_INIT;
            AstraFile restored = ASTRA_FILE_INIT;
            AstraProcessFileState state;

            original._private_client = astra_process_vfs_client();
            original._private_ownership_epoch = original._private_client->ownership_epoch;
            original._private_read = astra_vfs_port_read_bulk_position;
            original._private_file = 0x1234u;
            original._private_flags = ASTRA_VFS_OPEN_READ |
                                      ASTRA_VFS_OPEN_WRITE;
            original._private_offset = UINT64_C(0x123456789);
            original._private_size = UINT64_C(0x223456789);
            original._private_kind = ASTRA_VFS_KIND_FILE;
            original._private_member = 2u;
            assert(astra_process_file_export(&original, &state) ==
                   ASTRA_VFS_OK);
            assert(astra_process_file_import(&state, &restored) ==
                   ASTRA_VFS_OK);
            assert(restored._private_client == original._private_client);
            assert(restored._private_read == astra_vfs_port_read_bulk_position);
            assert(restored._private_write ==
                   astra_vfs_port_write_bulk_position);
            assert(restored._private_file == original._private_file);
            assert(restored._private_flags == original._private_flags);
            assert(restored._private_offset == original._private_offset);
            assert(restored._private_size == original._private_size);
            assert(restored._private_kind == original._private_kind);
            assert(restored._private_member == original._private_member);
            state.service = 0xdeadbeefu;
            assert(astra_process_file_import(&state, &restored) ==
                   ASTRA_VFS_ERR_BAD_HANDLE);
        }
        assert(astra_process_vfs_client() != NULL);
        assert(connects == 2u);
        astra_process_vfs_set_activity(0x12345678u);
        assert(astra_process_vfs_client_for(
                   &astra_process_vfs_assigns()->entries[0])->activity ==
               0x12345678u);
        assert(astra_process_vfs_client_for(
                   &astra_process_vfs_assigns()->entries[2])->activity ==
               0x12345678u);
        astra_process_vfs_close();
        assert(disconnects == 2u);

        /* A fork child drops its cloned reply channel without closing the
         * parent's service session, then reconnects lazily under its owner. */
        connects = 0u;
        disconnects = 0u;
        abandons = 0u;
        port_fork_rebinds = 0u;
        fork_child_calls = 0u;
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        assert(astra_process_vfs_client() != NULL && connects == 1u);
        assert(astra_process_vfs_after_fork_child(&startup, NULL) == ASTRA_VFS_OK);
        assert(abandons == 1u && disconnects == 0u &&
               port_fork_rebinds == 1u);
        assert(fork_child_calls == 1u);
        assert(astra_process_vfs_client() != NULL && connects == 2u);
        astra_process_vfs_close();
        assert(disconnects == 1u);

        /* A direct child replaces only its process-local channel. It does not
         * throw away the namespace and pay another service HELLO. */
        connects = 0u;
        disconnects = 0u;
        direct_connect = 1u;
        direct_forks = 0u;
        port_fork_rebinds = 0u;
        fork_child_calls = 0u;
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        assert(astra_process_vfs_client() != NULL && connects == 1u);
        assert(astra_process_vfs_after_fork_child(&startup, NULL) == ASTRA_VFS_OK);
        assert(direct_forks == 1u && connects == 1u &&
               port_fork_rebinds == 1u);
        /* Once per fork, however many clients the child carries. */
        assert(fork_child_calls == 1u);
        astra_process_vfs_close();
        assert(disconnects == 1u);
        direct_connect = 0u;

        /* Exec keeps the local service's session and open-file table. The
         * replacement image must resume that accelerator instead of silently
         * routing every operation back through the service port. */
        {
            void *state;
            uint32_t size;
            uint32_t used = 0u;
            AstraVfsClient *client;

            connects = 0u;
            disconnects = 0u;
            direct_connect = 1u;
            direct_resumes = 0u;
            assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
            client = astra_process_vfs_client();
            assert(client != NULL && client->port_direct_address != NULL);
            size = astra_process_vfs_state_size();
            state = malloc(size);
            assert(state != NULL);
            assert(astra_process_vfs_export(state, size, &used) ==
                       ASTRA_VFS_OK &&
                   used == size);
            direct_connect = 0u;
            fail_direct_resume = 1u;
            assert(astra_process_vfs_import(&startup, state, size) ==
                   ASTRA_VFS_ERR_IO);
            fail_direct_resume = 0u;
            assert(astra_process_vfs_import(&startup, state, size) ==
                   ASTRA_VFS_OK);
            free(state);
            client = astra_process_vfs_client();
            assert(client != NULL && direct_resumes == 2u &&
                   client->port_direct_address != NULL);
            /* A grant's client may still derive after exec (Kit 7). */
            assert(client->features == ASTRA_VFS_FEATURE_GRANTS);
            astra_process_vfs_close();
        }

        /* A mount nobody named is a mount nobody disconnects either. */
        connects = 0u;
        disconnects = 0u;
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        astra_process_vfs_close();
        assert(connects == 0u && disconnects == 0u);

        /*
         * A connect that refuses answers NULL and is retried by the next
         * caller rather than remembered as fatal: seeding no longer has an
         * opinion about whether a mount is reachable, because seeding no
         * longer talks to one.
         */
        connects = 0u;
        disconnects = 0u;
        fail_connect = 1u;
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        assert(astra_process_vfs_client() == NULL);
        assert(connects == 1u);
        fail_connect = 0u;
        assert(astra_process_vfs_client() != NULL);
        assert(connects == 2u);
        astra_process_vfs_close();
        assert(disconnects == 1u);

        /* Allocation failure refuses only the new cache entry. */
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        AstraVfsClient *borrowed = astra_process_vfs_client_for(
            &astra_process_vfs_assigns()->entries[0]);
        for (uint32_t index = 3u; index < 9u; ++index) {
            AstraAssign extra = {.handle = index + 100u};
            assert(astra_process_vfs_client_for(&extra) !=
                   NULL);
        }
        {
            const AstraAssignTable *table = astra_process_vfs_assigns();
            AstraAssign extra = {.handle = 109u};
            fail_allocate = 1;
            assert(astra_process_vfs_client_for(&extra) == NULL);
            fail_allocate = 0;
            assert(astra_process_vfs_client_for(&table->entries[0]) != NULL);
            force_move = 1;
            assert(astra_process_vfs_client_for(&extra) != NULL);
            assert(borrowed == astra_process_vfs_client_for(&table->entries[0]));
            force_move = 0;
        }
        astra_process_vfs_close();

        /* Growing the cache must preserve clients borrowed by open files. */
        AstraFile held = ASTRA_FILE_INIT;
        AstraProcessFileState held_state;
        seed_distinct = 1u;
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        assert(astra_process_vfs_client_for(
                   &astra_process_vfs_assigns()->entries[
                       ASTRA_STARTUP_CAPABILITY_MAX - 1u]) != NULL);
        held._private_client = astra_process_vfs_client_for(
            &astra_process_vfs_assigns()->entries[0]);
        held._private_ownership_epoch = held._private_client->ownership_epoch;
        held._private_file = 1u;
        assert(astra_process_file_export(&held, &held_state) == ASTRA_VFS_OK);
        AstraAssign extra = {.handle = 1000u};
        assert(astra_process_vfs_client_for(&extra) != NULL);
        assert(astra_process_file_export(&held, &held_state) == ASTRA_VFS_OK);
        assert(held._private_client == astra_process_vfs_client_for(
            &astra_process_vfs_assigns()->entries[0]));
        uint32_t old_connects = connects;
        held._private_client->port_lifecycle = 1u;
        assert(held._private_client == astra_process_vfs_client_for(
            &astra_process_vfs_assigns()->entries[0]));
        assert(connects == old_connects && held._private_client->port_lifecycle == 1u);
        astra_process_vfs_close();
        seed_distinct = 0u;
    }

    {
        AstraStartupInfo startup = {.capabilities_address = 1u};
        AstraProcessFilesystem first = ASTRA_PROCESS_FILESYSTEM_INIT;
        AstraProcessFilesystem second = ASTRA_PROCESS_FILESYSTEM_INIT;
        AstraProcessFilesystem unopened = ASTRA_PROCESS_FILESYSTEM_INIT;
        AstraFile held = ASTRA_FILE_INIT;
        AstraProcessFileState state;

        disconnects = 0u;
        assert(astra_process_filesystem_open(&first, &startup) == ASTRA_VFS_OK);
        assert(astra_process_filesystem_open(&second, &startup) == ASTRA_VFS_OK);
        held._private_client = astra_process_vfs_client();
        held._private_ownership_epoch = held._private_client->ownership_epoch;
        held._private_file = 1u;
        astra_process_filesystem_close(&unopened);
        astra_process_filesystem_close(&first);
        astra_process_filesystem_close(&first);
        astra_process_filesystem_close(&second);
        assert(disconnects == 0u);
        assert(astra_process_file_export(&held, &state) == ASTRA_VFS_OK);
        assert(held._private_client == astra_process_vfs_client());
        astra_process_vfs_close();
        assert(disconnects == 1u);
    }

    {
        pthread_t threads[16];
        uint32_t before_seeds = seeds;
        uint32_t before_connects = connects;

        force_move = 1;
        assert(pthread_barrier_init(&cache_start, NULL, 16) == 0);
        for (uint32_t index = 0u; index < 16u; ++index)
            assert(pthread_create(&threads[index], NULL, lookup_client_thread,
                                  (void *)(uintptr_t)index) == 0);
        for (uint32_t index = 0u; index < 16u; ++index)
            assert(pthread_join(threads[index], NULL) == 0);
        assert(seeds == before_seeds + 1u && connects == before_connects + 16u);
        for (uint32_t index = 0u; index < 16u; ++index) {
            assert(astra_process_vfs_client_handle(thread_clients[index]) == 9000u + index);
            for (uint32_t prior = 0u; prior < index; ++prior)
                assert(thread_clients[prior] != thread_clients[index]);
        }
        pthread_barrier_destroy(&cache_start);
        force_move = 0;
        astra_process_vfs_close();
    }

    {
        AstraProcessFilesystem filesystem = ASTRA_PROCESS_FILESYSTEM_INIT;
        char bytes[5];
        uint32_t length = 0u;

        filesystem.filesystem._private_assigns = (const void *)1u;

        assert(astra_process_read_file(&filesystem, "/app/test", bytes,
                                       sizeof(bytes), &length) ==
               ASTRA_VFS_OK);
        assert(length == sizeof(bytes) && memcmp(bytes, "hello", 5u) == 0);
        assert(closes == 1u);
        assert(astra_process_read_file(&filesystem, "/app/test", bytes, 4u,
                                       &length) == ASTRA_VFS_ERR_LIMIT);
        assert(closes == 2u);
        {
            void *allocated = NULL;
            static uint8_t large[65u * 128u];

            assert(astra_process_read_file_alloc(
                       &filesystem, "/app/test", &allocated, &length) ==
                   ASTRA_VFS_OK);
            assert(length == 5u && memcmp(allocated, "hello\0", 6u) == 0);
            astra_runtime_deallocate(allocated);
            memset(large, 0xa5, sizeof(large));
            mock_file_content = large;
            mock_file_length = sizeof(large);
            assert(astra_process_read_file_alloc(
                       &filesystem, "/app/test", &allocated, &length) ==
                   ASTRA_VFS_OK);
            assert(length == sizeof(large));
            assert(memcmp(allocated, large, sizeof(large)) == 0);
            assert(((uint8_t *)allocated)[length] == '\0');
            astra_runtime_deallocate(allocated);
            fail_allocate = 1;
            assert(astra_process_read_file_alloc(
                       &filesystem, "/app/test", &allocated, &length) ==
                   ASTRA_VFS_ERR_LIMIT);
            fail_allocate = 0;
            assert(allocated == NULL && length == 0u);
            mock_file_content = default_file_content;
            mock_file_length = 5u;
            assert(astra_process_read_file_alloc(
                       &filesystem, "/app/test", NULL, &length) ==
                   ASTRA_VFS_ERR_INVALID);
        }
    }

    {
        AstraStartupInfo startup = { .capabilities_address = 1u };
        AstraProcessFilesystem filesystem = ASTRA_PROCESS_FILESYSTEM_INIT;
        char bytes[5];
        uint32_t length = 0u;

        filesystem.filesystem._private_assigns = (const void *)1u;
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        whole_file_enabled = 1;
        whole_file_bytes = (const uint8_t *)"fast";
        whole_file_moved = 4u;
        whole_file_node_size = 4u;
        assert(astra_process_read_file(&filesystem, "/app/test", bytes,
                                       sizeof(bytes), &length) ==
               ASTRA_VFS_OK);
        assert(length == 4u && memcmp(bytes, "fast", 4u) == 0);
        assert(astra_process_read_file(&filesystem, "/app/link/../test", bytes,
                                       sizeof(bytes), &length) == ASTRA_VFS_OK);
        assert(length == 5u && memcmp(bytes, "hello", 5u) == 0);

        whole_file_bytes = (const uint8_t *)"evil";
        whole_file_node_size = UINT64_C(0x100000004);
        assert(astra_process_read_file(&filesystem, "/app/test", bytes,
                                       sizeof(bytes), &length) ==
               ASTRA_VFS_OK);
        assert(length == 5u && memcmp(bytes, "hello", 5u) == 0);
        whole_file_enabled = 0;
        astra_process_vfs_close();
    }

    {
        AstraStartupInfo startup = {.capabilities_address = 1u};
        char cwd[ASTRA_VFS_PATH_MAX];
        uint32_t used;

        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        AstraVfsClient *client = astra_process_vfs_client();
        client->features |= ASTRA_VFS_FEATURE_DESCRIPTIONS | ASTRA_VFS_FEATURE_DIRECTORY_PATH;
        assert(astra_process_vfs_set_current_directory("/work/before") == ASTRA_VFS_OK);
        assert(astra_process_vfs_current_directory(cwd, sizeof(cwd)) == ASTRA_VFS_OK &&
               strcmp(cwd, "/work/before") == 0);
        held_directory_path = "/after";
        assert(astra_process_vfs_current_directory(cwd, sizeof(cwd)) == ASTRA_VFS_OK &&
               strcmp(cwd, "/work/after") == 0);
        assert(astra_process_vfs_current_directory(cwd, 2u) == ASTRA_VFS_ERR_BUFFER_TOO_SMALL);
        uint32_t size = astra_process_vfs_state_size();
        void *state = malloc(size);
        assert(state != NULL && astra_process_vfs_export(state, size, &used) == ASTRA_VFS_OK && used == size);
        assert(astra_process_vfs_import(&startup, state, size) == ASTRA_VFS_OK);
        assert(astra_process_vfs_current_directory(cwd, sizeof(cwd)) == ASTRA_VFS_OK &&
               strcmp(cwd, "/work/after") == 0);
        free(state);
        held_directory_status = ASTRA_VFS_ERR_NOT_FOUND;
        assert(astra_process_vfs_current_directory(cwd, sizeof(cwd)) == ASTRA_VFS_ERR_NOT_FOUND);
        assert(astra_process_vfs_set_current_directory("/") == ASTRA_VFS_OK);
        assert(astra_process_vfs_current_directory(cwd, sizeof(cwd)) == ASTRA_VFS_OK && strcmp(cwd, "/") == 0);
        held_directory_status = ASTRA_VFS_OK;
        astra_process_vfs_close();
    }

    {
        AstraStartupInfo startup = {.capabilities_address = 1u};
        const AstraAssignTable *first, *second;
        void *first_owner, *second_owner;
        catalog_enabled = 1; catalog_count = 1u;
        catalog_records[0] = (AstraVolumeRecord){.id = 7u, .generation = 1u,
            .kind = ASTRA_VOLUME_MOUNT, .state = ASTRA_VOLUME_MOUNTED,
            .origin = ASTRA_VOLUME_ORIGIN_DEVICE, .name = "/usb0"};
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        assert(astra_process_vfs_namespace_acquire(&first, &first_owner) == ASTRA_VFS_OK);
        const AstraAssign *mount = astra_assign_lookup(first, "USB0");
        assert(mount != NULL && mount->handle == 0u && mount->volume.generation == 1u);
        assert(root_requests == 0u);
        AstraVfsClient *old = astra_process_vfs_client_for(mount);
        assert(old != NULL && root_requests == 1u);
        AstraVfsFile held;
        assert(astra_vfs_open(old, "/", ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY,
            &held, NULL, NULL) == ASTRA_VFS_OK);
        /* A replacement never rewrites a table or client already borrowed. */
        catalog_records[0].generation = 2u; catalog_changed = 1;
        assert(astra_process_vfs_namespace_acquire(&second, &second_owner) == ASTRA_VFS_OK);
        assert(astra_assign_lookup(first, "USB0")->volume.generation == 1u);
        mount = astra_assign_lookup(second, "USB0");
        assert(mount != NULL && mount->volume.generation == 2u);
        AstraVfsClient *replacement = astra_process_vfs_client_for(mount);
        assert(replacement != NULL && replacement != old && root_requests == 2u);
        astra_process_vfs_namespace_release(first_owner);
        assert(root_closes == 0u);
        /* Transient catalog failure keeps the last complete namespace. */
        catalog_changed = 1; catalog_failure = ASTRA_ERROR_BUSY;
        astra_process_vfs_namespace_release(second_owner);
        assert(astra_process_vfs_namespace_acquire(&second, &second_owner) == ASTRA_VFS_OK);
        assert(astra_assign_lookup(second, "USB0")->volume.generation == 2u);
        catalog_failure = ASTRA_OK;
        astra_process_vfs_namespace_release(second_owner);
        /* Exec state includes the current binding and the held old description. */
        uint32_t size = astra_process_vfs_state_size(), used;
        void *state = malloc(size); assert(state != NULL);
        assert(astra_process_vfs_export(state, size, &used) == ASTRA_VFS_OK && used == size);
        assert(astra_process_vfs_import(&startup, state, size) == ASTRA_VFS_OK);
        free(state);
        assert(astra_process_vfs_namespace_acquire(&second, &second_owner) == ASTRA_VFS_OK);
        assert(astra_assign_lookup(second, "USB0")->volume.generation == 2u);
        astra_process_vfs_namespace_release(second_owner);
        assert(astra_vfs_close(old, held) == ASTRA_VFS_OK);
        assert(astra_process_vfs_namespace_acquire(&second, &second_owner) == ASTRA_VFS_OK);
        astra_process_vfs_namespace_release(second_owner);
        assert(root_closes == 1u);
        /* Repeated hotplug retires roots rather than leaking one per incarnation. */
        for (uint32_t generation = 3u; generation < 103u; ++generation) {
            catalog_records[0].generation = generation; catalog_changed = 1;
            assert(astra_process_vfs_namespace_acquire(&second, &second_owner) == ASTRA_VFS_OK);
            mount = astra_assign_lookup(second, "USB0");
            assert(astra_process_vfs_client_for(mount) != NULL);
            astra_process_vfs_namespace_release(second_owner);
        }
        catalog_count = 0u; catalog_changed = 1;
        assert(astra_process_vfs_namespace_acquire(&second, &second_owner) == ASTRA_VFS_OK);
        assert(astra_assign_lookup(second, "USB0") == NULL);
        astra_process_vfs_namespace_release(second_owner);
        assert(root_requests == root_closes);
        astra_process_vfs_close(); catalog_enabled = 0;
    }
    {
        /* RAM role authority follows its configured SOURCE through unmount and
         * a fresh process start, without exposing other RAM or USB sources. */
        AstraStartupInfo startup = {.capabilities_address = 1u};
        const AstraAssignTable *view;
        void *owner;
        global_brokers = 0u; ram_brokers = ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE;
        catalog_enabled = 1; catalog_count = 4u;
        catalog_records[0] = (AstraVolumeRecord){.id = ram_source.id,
            .generation = ram_source.generation, .kind = ASTRA_VOLUME_SOURCE,
            .state = ASTRA_VOLUME_UNMOUNTED, .class_id = ASTRA_VOLUME_RAM,
            .origin = ASTRA_VOLUME_ORIGIN_RAM, .flags = ASTRA_VOLUME_CONFIGURED_RAM};
        catalog_records[1] = (AstraVolumeRecord){.id = 601u, .generation = 1u,
            .parent = ram_source.id, .parent_generation = ram_source.generation,
            .kind = ASTRA_VOLUME_MOUNT, .state = ASTRA_VOLUME_MOUNTED,
            .class_id = ASTRA_VOLUME_RAM, .origin = ASTRA_VOLUME_ORIGIN_RAM,
            .name = "/ram0"};
        catalog_records[2] = (AstraVolumeRecord){.id = 602u, .generation = 1u,
            .parent = 599u, .parent_generation = 1u, .kind = ASTRA_VOLUME_MOUNT,
            .state = ASTRA_VOLUME_MOUNTED, .class_id = ASTRA_VOLUME_RAM,
            .origin = ASTRA_VOLUME_ORIGIN_RAM, .name = "/ram1"};
        catalog_records[3] = (AstraVolumeRecord){.id = 603u, .generation = 1u,
            .kind = ASTRA_VOLUME_MOUNT, .state = ASTRA_VOLUME_MOUNTED,
            .origin = ASTRA_VOLUME_ORIGIN_DEVICE, .name = "/usb0"};
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        assert(astra_process_vfs_namespace_acquire(&view, &owner) == ASTRA_VFS_OK);
        const AstraAssign *mount = astra_assign_lookup(view, "RAM0");
        assert(mount != NULL && mount->broker == 90006u &&
            mount->rights == (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE));
        assert(astra_assign_lookup(view, "RAM1") == NULL && astra_assign_lookup(view, "USB0") == NULL);
        AstraVfsClient *old = astra_process_vfs_client_for(mount);
        assert(old != NULL && last_root_broker == 90006u && last_root_rights == mount->rights);
        AstraAssign other_authority = *mount;
        other_authority.broker = 90003u;
        assert(astra_process_vfs_client_for(&other_authority) == NULL);
        AstraVfsFile held;
        assert(astra_vfs_open(old, "/", ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY,
            &held, NULL, NULL) == ASTRA_VFS_OK);
        astra_process_vfs_namespace_release(owner);
        catalog_records[1].state = ASTRA_VOLUME_UNMOUNTED; catalog_changed = 1;
        assert(astra_process_vfs_namespace_acquire(&view, &owner) == ASTRA_VFS_OK);
        assert(astra_assign_lookup(view, "RAM0") == NULL);
        astra_process_vfs_namespace_release(owner);
        catalog_records[1].id = 604u; catalog_records[1].state = ASTRA_VOLUME_MOUNTED;
        catalog_changed = 1;
        assert(astra_process_vfs_namespace_acquire(&view, &owner) == ASTRA_VFS_OK);
        mount = astra_assign_lookup(view, "RAM0");
        assert(mount != NULL && mount->volume.id == 604u && astra_process_vfs_client_for(mount) != old);
        astra_process_vfs_namespace_release(owner);
        assert(astra_vfs_close(old, held) == ASTRA_VFS_OK);
        /* A same-name mount from another source incarnation is not the role. */
        ++catalog_records[1].parent_generation; catalog_changed = 1;
        assert(astra_process_vfs_namespace_acquire(&view, &owner) == ASTRA_VFS_OK);
        assert(astra_assign_lookup(view, "RAM0") == NULL);
        astra_process_vfs_namespace_release(owner);
        catalog_records[1].parent_generation = ram_source.generation;
        catalog_records[1].state = ASTRA_VOLUME_UNMOUNTED;
        astra_process_vfs_close();
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        assert(astra_process_vfs_namespace_acquire(&view, &owner) == ASTRA_VFS_OK);
        assert(astra_assign_lookup(view, "RAM0") == NULL);
        astra_process_vfs_namespace_release(owner);
        catalog_records[1].id = 605u; catalog_records[1].state = ASTRA_VOLUME_MOUNTED;
        catalog_records[1].flags = ASTRA_VOLUME_READ_ONLY; catalog_changed = 1;
        assert(astra_process_vfs_namespace_acquire(&view, &owner) == ASTRA_VFS_OK);
        mount = astra_assign_lookup(view, "RAM0");
        assert(mount != NULL && mount->rights == ASTRA_RIGHT_READ);
        assert(astra_process_vfs_client_for(mount) != NULL && last_root_rights == ASTRA_RIGHT_READ);
        astra_process_vfs_namespace_release(owner);
        /* A transient info failure cannot publish a partial replacement. */
        catalog_records[1].id = 606u; catalog_changed = 1;
        ram_info_failure = ASTRA_ERROR_BUSY;
        assert(astra_process_vfs_namespace_acquire(&view, &owner) == ASTRA_VFS_OK);
        assert(astra_assign_lookup(view, "RAM0")->volume.id == 605u);
        astra_process_vfs_namespace_release(owner);
        ram_info_failure = ASTRA_OK;
        assert(astra_process_vfs_namespace_acquire(&view, &owner) == ASTRA_VFS_OK);
        assert(astra_assign_lookup(view, "RAM0")->volume.id == 606u);
        astra_process_vfs_namespace_release(owner);
        astra_process_vfs_close();
        /* A read role never acquires writes even when the mount is writable. */
        ram_brokers = ASTRA_RIGHT_READ; catalog_records[1].flags = 0u;
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        assert(astra_process_vfs_namespace_acquire(&view, &owner) == ASTRA_VFS_OK);
        mount = astra_assign_lookup(view, "RAM0");
        assert(mount != NULL && mount->broker == 90005u && mount->rights == ASTRA_RIGHT_READ);
        assert(astra_process_vfs_client_for(mount) != NULL && last_root_broker == 90005u);
        astra_process_vfs_namespace_release(owner);
        astra_process_vfs_close();
        catalog_enabled = 0; ram_brokers = 0u; global_brokers = ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE;
    }
    {
        /* A child invalidates raw copied tokens but retains their exact local
           pins. Adoption replaces one pin; refresh cannot free the other. */
        AstraStartupInfo startup = {.capabilities_address = 1u};
        const AstraAssignTable *view;
        void *owner;
        catalog_enabled = 1; catalog_count = 1u;
        catalog_records[0] = (AstraVolumeRecord){.id = 707u, .generation = 1u,
            .kind = ASTRA_VOLUME_MOUNT, .state = ASTRA_VOLUME_MOUNTED,
            .origin = ASTRA_VOLUME_ORIGIN_DEVICE, .name = "/usb0"};
        uint32_t roots_before = root_closes;
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        assert(astra_process_vfs_namespace_acquire(&view, &owner) == ASTRA_VFS_OK);
        AstraVfsClient *old = astra_process_vfs_client_for(astra_assign_lookup(view, "USB0"));
        assert(old != NULL);
        AstraFile raw = ASTRA_FILE_INIT, adopted = ASTRA_FILE_INIT;
        AstraVfsFile token;
        assert(astra_vfs_open(old, "/", ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY,
            &token, NULL, NULL) == ASTRA_VFS_OK);
        AstraProcessFileState record = {.service = old->port_service, .file = token,
            .flags = ASTRA_VFS_OPEN_READ, .kind = ASTRA_VFS_KIND_DIRECTORY};
        assert(astra_process_file_import(&record, &raw) == ASTRA_VFS_OK);
        assert(astra_vfs_open(old, "/", ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY,
            &token, NULL, NULL) == ASTRA_VFS_OK);
        assert(astra_process_file_import(&record, &adopted) == ASTRA_VFS_OK);
        AstraProcessFileShare receipt = {.file = record, .token = 1u};
        uint32_t parent_epoch = old->ownership_epoch;
        astra_process_vfs_namespace_release(owner);
        assert(astra_process_vfs_after_fork_child(&startup, NULL) == ASTRA_VFS_OK);
        assert(old->ownership_epoch == parent_epoch + 1u);
        assert(astra_process_file_export(&raw, &record) == ASTRA_VFS_ERR_LOST);
        assert(astra_process_file_adopt(&receipt, &adopted) == ASTRA_VFS_OK);
        assert(adopted._private_ownership_epoch == old->ownership_epoch);
        assert(astra_process_file_export(&adopted, &record) == ASTRA_VFS_OK);
        /* Another fork must retain already-invalid pins as well as move the
           newly adopted owner. Epochs cannot revive an older raw token. */
        receipt.file = record; receipt.token = 2u;
        assert(astra_process_vfs_after_fork_child(&startup, NULL) == ASTRA_VFS_OK);
        assert(old->ownership_epoch == parent_epoch + 2u);
        assert(astra_process_file_export(&adopted, &record) == ASTRA_VFS_ERR_LOST);
        assert(astra_process_file_adopt(&receipt, &adopted) == ASTRA_VFS_OK);
        assert(astra_process_file_export(&adopted, &record) == ASTRA_VFS_OK);
        catalog_records[0].generation = 2u; catalog_changed = 1;
        assert(astra_process_vfs_namespace_acquire(&view, &owner) == ASTRA_VFS_OK);
        astra_process_vfs_namespace_release(owner);
        assert(root_closes == roots_before);
        assert(astra_vfs_file_owner_status(raw._private_client, raw._private_ownership_epoch) == ASTRA_VFS_ERR_LOST);
        assert(astra_vfs_file_owner_close(adopted._private_client, adopted._private_file,
            adopted._private_ownership_epoch) == ASTRA_VFS_OK);
        adopted = (AstraFile)ASTRA_FILE_INIT;
        assert(astra_process_vfs_namespace_acquire(&view, &owner) == ASTRA_VFS_OK);
        astra_process_vfs_namespace_release(owner);
        assert(root_closes == roots_before);
        uint32_t closed_before = file_closes;
        assert(astra_vfs_file_owner_close(raw._private_client, raw._private_file,
            raw._private_ownership_epoch) == ASTRA_VFS_ERR_LOST);
        raw = (AstraFile)ASTRA_FILE_INIT;
        assert(file_closes == closed_before);
        assert(astra_process_vfs_namespace_acquire(&view, &owner) == ASTRA_VFS_OK);
        astra_process_vfs_namespace_release(owner);
        assert(root_closes == roots_before + 1u);
        astra_process_vfs_close();
        catalog_enabled = 0;
    }
    {
        AstraStartupInfo startup = {.capabilities_address = 1u};
        AstraProcessFileShare share;
        catalog_enabled = 1; catalog_count = 1u;
        catalog_records[0] = (AstraVolumeRecord){.id = 808u, .generation = 1u,
            .kind = ASTRA_VOLUME_MOUNT, .state = ASTRA_VOLUME_MOUNTED,
            .origin = ASTRA_VOLUME_ORIGIN_DEVICE, .name = "/usb0"};
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        const AstraAssignTable *view;
        void *owner;
        assert(astra_process_vfs_namespace_acquire(&view, &owner) == ASTRA_VFS_OK);
        AstraVfsClient *client = astra_process_vfs_client_for(astra_assign_lookup(view, "USB0"));
        assert(client != NULL);
        client->features |= ASTRA_VFS_FEATURE_DESCRIPTIONS | ASTRA_VFS_FEATURE_DIRECTORY_PATH;
        astra_process_vfs_namespace_release(owner);
        assert(astra_process_vfs_set_current_directory("/usb0") == ASTRA_VFS_OK);
        share_status = ASTRA_VFS_ERR_IO;
        assert(astra_process_vfs_share_current_directory(&share) == ASTRA_VFS_ERR_IO);
        catalog_records[0].state = ASTRA_VOLUME_UNMOUNTED;
        assert(astra_process_vfs_share_current_directory(&share) == ASTRA_VFS_ERR_LOST);
        catalog_records[0].id = 809u;
        catalog_records[0].state = ASTRA_VOLUME_MOUNTED;
        share_status = ASTRA_VFS_ERR_INVALID;
        assert(astra_process_vfs_share_current_directory(&share) == ASTRA_VFS_ERR_LOST);
        catalog_failure = ASTRA_ERROR_BUSY;
        assert(astra_process_vfs_share_current_directory(&share) == ASTRA_VFS_ERR_INVALID);
        catalog_failure = ASTRA_OK; share_status = ASTRA_VFS_OK;
        astra_process_vfs_close();
        catalog_enabled = 0;
    }
    {
        /* Failed CWD receipt adoption leaves an inherited local owner. It must
         * neither issue another old-token request nor close a new child token. */
        AstraStartupInfo startup = {.capabilities_address = 1u};
        AstraProcessFileShare receipt = {.token = 3u};
        const AstraFile *directory;
        char cwd[64];
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        AstraVfsClient *client = astra_process_vfs_client();
        client->features |= ASTRA_VFS_FEATURE_DESCRIPTIONS | ASTRA_VFS_FEATURE_DIRECTORY_PATH;
        assert(astra_process_vfs_set_current_directory("/work/before") == ASTRA_VFS_OK);
        assert(astra_process_vfs_current_acquire(&directory) == ASTRA_VFS_OK && directory != NULL);
        assert(astra_process_file_export(directory, &receipt.file) == ASTRA_VFS_OK);
        astra_process_vfs_current_release();
        adopt_status = ASTRA_VFS_ERR_IO;
        assert(astra_process_vfs_after_fork_child(&startup, &receipt) == ASTRA_VFS_ERR_IO);
        adopt_status = ASTRA_VFS_OK;
        assert(astra_process_vfs_current_directory(cwd, sizeof(cwd)) == ASTRA_VFS_ERR_LOST);
        uint32_t closed_before = file_closes;
        assert(astra_process_vfs_set_current_directory("/") == ASTRA_VFS_OK);
        assert(file_closes == closed_before);
        assert(astra_process_vfs_current_directory(cwd, sizeof(cwd)) == ASTRA_VFS_OK && strcmp(cwd, "/") == 0);
        astra_process_vfs_close();
    }
    puts("astra process library resolver: PASS");
    return 0;
}
