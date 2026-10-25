#include <astra/bytes.h>
#include <astra/vfs_path_storage.h>
#include <astra/ascii.h>
#include <astra/runtime.h>
#include <astra/syscall.h>
#include <astra/vfs_path.h>
#include <astra/vfs_path_payload.h>
#include <astra/vfs_file_lifetime.h>
#include <astra/vfs_assign_internal.h>
#include <astra/vfs_host_direct.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_process.h>
#include <astra/vfs_library_source.h>
#include <astra/volume.h>
#include <astra/resource.h>

#include <stddef.h>
#include <string.h>
#if defined(ASTRA_VFS_PROCESS_TEST)
#include <stdlib.h>
#endif

static AstraAssignTable assigns;
static char current_assign[ASTRA_ASSIGN_NAME_MAX];
static char *current_directory;
static AstraFile current_file = ASTRA_FILE_INIT;
static _Alignas(4) volatile uint32_t current_lock;
static uint8_t current_ready;
static uint32_t current_prepare(void);
/*
 * One client per distinct mount handle, connected the first time something
 * asks for it.
 *
 * **It used to connect all of them at startup.** A connect is a HELLO and a
 * BIND_AREA -- two cross-process round trips, and a round trip on this machine
 * is ~7.5 ms because the MC68040 has no address-space tag and every switch
 * flushes the ATC. A command is granted WORK, COMMANDS twice, LIBS, EVENTS and
 * PROC, so six handles were connected before `main` and most programs use two.
 * `ls` never touches EVENTS: or PROC:; `status` touches nothing at all.
 *
 * Lazy costs one branch per lookup and nothing else: the handle is known at
 * seeding, and only the two round trips move.
 */
typedef struct ProcessVfsClient {
    AstraVfsClient client;
    uint32_t handle;
    AstraVolumeReference volume;
    uint32_t rights;
    uint32_t broker;
    uint32_t namespace_refs;
    uint32_t descriptions;
    uint32_t inherited_descriptions;
    uint8_t owned;
    uint8_t connected;
} ProcessVfsClient;

static ProcessVfsClient **clients;
static uint32_t client_count;
static uint32_t client_capacity;
static uint8_t vfs_initialized;
static _Alignas(4) volatile uint32_t cache_lock;
static AstraVfsClient *client_ready(uint32_t slot);

typedef struct ProcessNamespace {
    AstraAssignTable table;
    uint32_t references;
    struct ProcessNamespace *next;
} ProcessNamespace;

static ProcessNamespace *namespace_current;
static ProcessNamespace *namespace_owners;
static AstraVolumeWatch namespace_watch = ASTRA_VOLUME_WATCH_INIT;
static AstraHandle namespace_catalog;
static AstraHandle namespace_read;
static AstraHandle namespace_write;
static AstraHandle namespace_ram_read;
static AstraHandle namespace_ram_write;
static uint8_t namespace_dirty;

static int same_volume(const AstraVolumeReference *left, const AstraVolumeReference *right)
{
    return left->epoch == right->epoch && left->id == right->id &&
        left->generation == right->generation;
}

static ProcessVfsClient *volume_client(const AstraAssign *assign)
{
    for (uint32_t at = 0u; at < client_count; ++at)
        if (clients[at]->owned && clients[at]->rights == assign->rights &&
            clients[at]->broker == assign->broker &&
            same_volume(&clients[at]->volume, &assign->volume))
            return clients[at];
    return NULL;
}

static void description_acquire(AstraVfsClient *client)
{
    if (astra_mutex_lock(&cache_lock) != ASTRA_SYSCALL_OK) return;
    for (uint32_t at = 0u; at < client_count; ++at)
        if (&clients[at]->client == client) { ++clients[at]->descriptions; break; }
    (void)astra_mutex_unlock(&cache_lock);
}

static void description_release(AstraVfsClient *client, uint32_t epoch)
{
    if (astra_mutex_lock(&cache_lock) != ASTRA_SYSCALL_OK) return;
    for (uint32_t at = 0u; at < client_count; ++at)
        if (&clients[at]->client == client) {
            uint32_t *pins = epoch == client->ownership_epoch ?
                &clients[at]->descriptions : &clients[at]->inherited_descriptions;
            if (*pins != 0u) --*pins;
            break;
        }
    (void)astra_mutex_unlock(&cache_lock);
}

/* Called under cache_lock, after every borrower has dropped the cell. */
static void reap_clients(void)
{
    for (uint32_t at = 0u; at < client_count;) {
        ProcessVfsClient *entry = clients[at];
        if (!entry->owned || entry->namespace_refs != 0u || entry->descriptions != 0u ||
            entry->inherited_descriptions != 0u) { ++at; continue; }
        if (entry->connected) (void)astra_vfs_disconnect(&entry->client);
        if (entry->handle != 0u) (void)astra_close(entry->handle);
        astra_runtime_deallocate(entry);
        --client_count;
        memmove(clients + at, clients + at + 1u, (client_count - at) * sizeof(*clients));
        clients[client_count] = NULL;
    }
}

static void namespace_release_locked(ProcessNamespace *owner)
{
    if (owner == NULL || --owner->references != 0u) return;
    for (uint32_t at = 0u; at < owner->table.count; ++at) {
        AstraAssign *assign = &owner->table.entries[at];
        if (assign->volume.id != 0u) {
            ProcessVfsClient *entry = volume_client(assign);
            if (entry != NULL && entry->namespace_refs != 0u) --entry->namespace_refs;
        }
    }
    astra_assign_table_destroy(&owner->table);
    ProcessNamespace **link = &namespace_owners;
    while (*link != NULL && *link != owner) link = &(*link)->next;
    if (*link == owner) *link = owner->next;
    astra_runtime_deallocate(owner);
}

static uint32_t reserve_clients(uint32_t minimum)
{
    ProcessVfsClient **grown;
    uint32_t capacity;

    if (minimum <= client_capacity)
        return ASTRA_VFS_OK;
    capacity = client_capacity == 0u ? 8u : client_capacity;
    while (capacity < minimum) {
        if (capacity > UINT32_MAX / 2u)
            return ASTRA_VFS_ERR_LIMIT;
        capacity *= 2u;
    }
    if (capacity > UINT32_MAX / sizeof(*clients))
        return ASTRA_VFS_ERR_LIMIT;
    grown = astra_runtime_reallocate(
        clients, (size_t)capacity * sizeof(*clients));
    if (grown == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    memset(grown + client_capacity, 0,
           (size_t)(capacity - client_capacity) * sizeof(*grown));
    clients = grown;
    client_capacity = capacity;
    return ASTRA_VFS_OK;
}

/* Files borrow clients, so cache growth may move only the pointer table. */
static uint32_t append_client(uint32_t handle)
{
    if (client_count == UINT32_MAX ||
        reserve_clients(client_count + 1u) != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_LIMIT;
    if (clients[client_count] == NULL) {
        clients[client_count] = astra_runtime_allocate(sizeof(**clients));
        if (clients[client_count] == NULL)
            return ASTRA_VFS_ERR_LIMIT;
    }
    *clients[client_count++] = (ProcessVfsClient){
        .handle = handle, .client = {.ownership_epoch = 1u}};
    return ASTRA_VFS_OK;
}

/* Role ports identify an exact configured source, independently of whether it
 * currently has a mount. A mount spelling or RAM class alone is not authority. */
static int configured_ram_mount(const AstraVolumeSnapshot *catalog,
    const AstraVolumeRecord *mount, const AstraVolumeBrokerInfo *info)
{
    if (info->source.epoch != catalog->epoch ||
        mount->parent != info->source.id ||
        mount->parent_generation != info->source.generation)
        return 0;
    for (uint32_t at = 0u; at < catalog->count; ++at) {
        const AstraVolumeRecord *source = &catalog->records[at];
        if (source->id == info->source.id && source->generation == info->source.generation &&
            source->kind == ASTRA_VOLUME_SOURCE &&
            (source->flags & ASTRA_VOLUME_CONFIGURED_RAM) != 0u)
            return 1;
    }
    return 0;
}

static uint32_t ram_broker_info(AstraHandle broker, uint32_t rights,
    AstraVolumeBrokerInfo *info)
{
    *info = (AstraVolumeBrokerInfo){0};
    if (broker == ASTRA_INVALID_HANDLE) return ASTRA_VFS_OK;
    AstraResult result = astra_volume_broker_info(broker, info);
    if (result != ASTRA_OK) return ASTRA_VFS_ERR_IO;
    if (info->source.epoch == 0u || info->source.id == 0u ||
        info->source.generation == 0u || info->rights != rights || info->reserved != 0u)
        return ASTRA_VFS_ERR_PROTOCOL;
    return ASTRA_VFS_OK;
}

static uint32_t namespace_build_locked(const AstraVolumeSnapshot *catalog)
{
    ProcessNamespace *replacement = astra_runtime_allocate(sizeof(*replacement));
    if (replacement == NULL) return ASTRA_VFS_ERR_LIMIT;
    *replacement = (ProcessNamespace){.references = 1u};
    uint32_t status = astra_vfs_assign_clone(&replacement->table, &assigns);
    if (status != ASTRA_VFS_OK) {
        astra_runtime_deallocate(replacement);
        return status;
    }
    if (catalog != NULL) {
        AstraVolumeBrokerInfo ram_read, ram_write;
        status = ram_broker_info(namespace_ram_read, ASTRA_RIGHT_READ, &ram_read);
        if (status == ASTRA_VFS_OK)
            status = ram_broker_info(namespace_ram_write, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE, &ram_write);
        if (status == ASTRA_VFS_OK && namespace_ram_read != ASTRA_INVALID_HANDLE &&
            namespace_ram_write != ASTRA_INVALID_HANDLE && !same_volume(&ram_read.source, &ram_write.source))
            status = ASTRA_VFS_ERR_PROTOCOL;
        for (uint32_t at = 0u; at < catalog->count; ++at) {
            if (status != ASTRA_VFS_OK) break;
            const AstraVolumeRecord *record = &catalog->records[at];
            const char *name = record->name + 1u;
            if (record->kind != ASTRA_VOLUME_MOUNT ||
                (record->state != ASTRA_VOLUME_MOUNTED && record->state != ASTRA_VOLUME_QUIESCING) ||
                (record->flags & (ASTRA_VOLUME_SYSTEM | ASTRA_VOLUME_TEMPORARY)) != 0u ||
                record->origin == ASTRA_VOLUME_ORIGIN_EXPORT || record->origin == ASTRA_VOLUME_ORIGIN_SYSTEM_IMAGE)
                continue;
            if (record->name[0] != '/' || name[0] == '\0') {
                status = ASTRA_VFS_ERR_PROTOCOL;
                break;
            }
            if (astra_assign_lookup(&replacement->table, name) != NULL)
                continue;
            AstraVolumeReference reference = {catalog->epoch, record->id, record->generation};
            uint32_t rights = ASTRA_RIGHT_READ;
            AstraHandle broker = namespace_read;
            int writable = (record->flags & ASTRA_VOLUME_READ_ONLY) == 0u;
            if (namespace_write != ASTRA_INVALID_HANDLE && writable) {
                rights |= ASTRA_RIGHT_WRITE;
                broker = namespace_write;
            }
            if (namespace_ram_write != ASTRA_INVALID_HANDLE && configured_ram_mount(catalog, record, &ram_write)) {
                broker = namespace_ram_write;
                if (writable) rights |= ASTRA_RIGHT_WRITE;
            } else if (namespace_ram_read != ASTRA_INVALID_HANDLE && configured_ram_mount(catalog, record, &ram_read) &&
                (rights & ASTRA_RIGHT_WRITE) == 0u)
                broker = namespace_ram_read;
            if (broker == ASTRA_INVALID_HANDLE) continue;
            status = astra_assign_bind_volume(&replacement->table, name, &reference, broker, rights);
            if (status != ASTRA_VFS_OK) break;
            AstraAssign *binding = &replacement->table.entries[replacement->table.count - 1u];
            ProcessVfsClient *entry = volume_client(binding);
            if (entry != NULL) ++entry->namespace_refs;
        }
    }
    if (status != ASTRA_VFS_OK) {
        namespace_release_locked(replacement);
        reap_clients();
        return status;
    }
    ProcessNamespace *old = namespace_current;
    replacement->next = namespace_owners;
    namespace_owners = replacement;
    namespace_current = replacement;
    namespace_release_locked(old);
    reap_clients();
    return ASTRA_VFS_OK;
}

static void namespace_refresh_locked(void)
{
    if (namespace_catalog == ASTRA_INVALID_HANDLE) return;
    if (namespace_watch.receive == ASTRA_INVALID_HANDLE) {
        AstraResult opened = astra_volume_watch_open(namespace_catalog, &namespace_watch);
        if (opened == ASTRA_OK) namespace_dirty = 1u;
        else namespace_dirty = 1u;
    }
    if (namespace_watch.receive != ASTRA_INVALID_HANDLE) {
        AstraVolumeChange change;
        for (;;) {
            AstraResult result = astra_volume_watch_receive(&namespace_watch, &change);
            if (result == ASTRA_ERROR_WOULD_BLOCK) break;
            namespace_dirty = 1u;
            if (result != ASTRA_OK) {
                AstraResult closed = astra_volume_watch_close(namespace_catalog, &namespace_watch);
                (void)closed;
                if (result == ASTRA_ERROR_PEER_DEAD || result == ASTRA_ERROR_CLOSED) {
                    (void)namespace_build_locked(NULL);
                    return;
                }
                break;
            }
        }
    }
    if (namespace_dirty) {
        AstraVolumeSnapshot catalog = ASTRA_VOLUME_SNAPSHOT_INIT;
        AstraResult result = astra_volume_snapshot_open(namespace_catalog, &catalog);
        if (result == ASTRA_OK && namespace_build_locked(&catalog) == ASTRA_VFS_OK)
            namespace_dirty = 0u;
        else if (result == ASTRA_ERROR_PEER_DEAD || result == ASTRA_ERROR_CLOSED)
            (void)namespace_build_locked(NULL);
        astra_volume_snapshot_close(&catalog);
    }
}

uint32_t astra_process_vfs_namespace_acquire(const AstraAssignTable **table, void **owner)
{
    if (table == NULL || owner == NULL || !vfs_initialized) return ASTRA_VFS_ERR_INVALID;
    *table = NULL; *owner = NULL;
    if (astra_mutex_lock(&cache_lock) != ASTRA_SYSCALL_OK) return ASTRA_VFS_ERR_IO;
    namespace_refresh_locked();
    uint32_t status = namespace_current != NULL ? ASTRA_VFS_OK : namespace_build_locked(NULL);
    if (status == ASTRA_VFS_OK && namespace_current->references == UINT32_MAX)
        status = ASTRA_VFS_ERR_LIMIT;
    if (status == ASTRA_VFS_OK) {
        ++namespace_current->references;
        *table = &namespace_current->table;
        *owner = namespace_current;
    }
    (void)astra_mutex_unlock(&cache_lock);
    return status;
}

void astra_process_vfs_namespace_release(void *owner)
{
    if (owner == NULL || astra_mutex_lock(&cache_lock) != ASTRA_SYSCALL_OK) return;
    namespace_release_locked(owner);
    reap_clients();
    (void)astra_mutex_unlock(&cache_lock);
}

#define PROCESS_VFS_EXEC_MAGIC 0x56465345u
/* 12 serializes complete owned root/CWD strings and exact lazy broker identity.
 * No pointers or fixed pathname arrays cross the exec boundary. */
#define PROCESS_VFS_EXEC_VERSION 12u

typedef struct ProcessVfsExecHeader {
    uint32_t magic;
    uint32_t size;
    uint32_t client_count;
    uint32_t version;
    AstraProcessFileState cwd;
    char current_assign[ASTRA_ASSIGN_NAME_MAX];
    uint32_t directory_offset;
    uint32_t directory_length;
    uint32_t current_ready;
    uint32_t assign_count;
    AstraVolumeWatch watch;
} ProcessVfsExecHeader;

typedef struct ProcessVfsExecAssign {
    char name[ASTRA_ASSIGN_NAME_MAX];
    uint32_t root_offset;
    uint32_t root_length;
    uint32_t handle;
    uint32_t rights;
    AstraVolumeReference volume;
    uint32_t broker;
    uint32_t reserved;
} ProcessVfsExecAssign;

typedef struct ProcessVfsExecClient {
    uint32_t service;
    uint32_t session;
    uint32_t activity;
    uint32_t port_service;
    AstraVfsPortExecLane lane;
    uint32_t port_direct_area;
    uint32_t port_direct_scope;
    uint32_t port_direct_session;
    uint32_t features;
    uint16_t version;
    uint8_t port_area_capable;
    uint8_t reserved;
    AstraVolumeReference volume;
    uint32_t rights;
    uint32_t broker;
    uint32_t descriptions;
    uint8_t owned;
    uint8_t connected;
    uint16_t reserved_owner;
} ProcessVfsExecClient;

static void close_process_vfs(void)
{
    for (uint32_t at = 0u; at < client_count; ++at) {
        clients[at]->client.description_acquire = NULL;
        clients[at]->client.description_release = NULL;
    }
    if (current_file._private_file != ASTRA_VFS_FILE_INVALID)
        (void)astra_vfs_file_owner_close(current_file._private_client,
            current_file._private_file, current_file._private_ownership_epoch);
    current_file = (AstraFile)ASTRA_FILE_INIT;
    current_ready = 0u;
    if (namespace_watch.receive != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_volume_watch_close(namespace_catalog, &namespace_watch);
        (void)closed;
    }
    namespace_release_locked(namespace_current);
    namespace_current = NULL;
    namespace_catalog = namespace_read = namespace_write = ASTRA_INVALID_HANDLE;
    namespace_ram_read = namespace_ram_write = ASTRA_INVALID_HANDLE;
    namespace_dirty = 0u;
    while (client_count != 0u) {
        --client_count;
        if (clients[client_count]->connected != 0u)
            (void)astra_vfs_disconnect(&clients[client_count]->client);
        if (clients[client_count]->owned && clients[client_count]->handle != 0u)
            (void)astra_close(clients[client_count]->handle);
        clients[client_count]->handle = 0u;
        clients[client_count]->connected = 0u;
    }
    vfs_initialized = 0u;
    astra_assign_table_destroy(&assigns);
    current_assign[0] = '\0';
    astra_runtime_deallocate(current_directory);
    current_directory = NULL;
}

void astra_process_vfs_close(void)
{
    if (astra_mutex_lock(&current_lock) != ASTRA_SYSCALL_OK)
        return;
    if (astra_mutex_lock(&cache_lock) == ASTRA_SYSCALL_OK) {
        close_process_vfs();
        (void)astra_mutex_unlock(&cache_lock);
    }
    (void)astra_mutex_unlock(&current_lock);
}

static uint32_t file_export_locked(const AstraFile *file, AstraProcessFileState *state);

static uint32_t state_size_locked(void)
{
    const AstraAssignTable *table = namespace_current != NULL ? &namespace_current->table : &assigns;
    uint64_t size = sizeof(ProcessVfsExecHeader) +
        (uint64_t)client_count * sizeof(ProcessVfsExecClient) +
        (uint64_t)table->count * sizeof(ProcessVfsExecAssign) +
        strlen(current_directory == NULL ? "" : current_directory) + 1u;
    for (uint32_t at = 0u; at < table->count; ++at)
        size += strlen(table->entries[at].root) + 1u;
    return size <= UINT32_MAX ? (uint32_t)size : 0u;
}

static uint32_t export_locked(void *state, uint32_t capacity,
                                  uint32_t *used)
{
    ProcessVfsExecHeader *header = state;
    ProcessVfsExecClient *output;
    uint32_t required = state_size_locked();
    uint32_t count = 0u;

    if (used == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *used = required;
    if (required == 0u) return ASTRA_VFS_ERR_LIMIT;
    if (state == NULL || capacity < required)
        return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    memset(state, 0, required);
    header->magic = PROCESS_VFS_EXEC_MAGIC;
    header->size = required;
    header->version = PROCESS_VFS_EXEC_VERSION;
    output = (ProcessVfsExecClient *)(void *)(header + 1);
    for (uint32_t slot = 0u; slot < client_count; ++slot) {
        AstraVfsClient *client = &clients[slot]->client;

        output[count].service = clients[slot]->handle;
        output[count].volume = clients[slot]->volume;
        output[count].rights = clients[slot]->rights;
        output[count].broker = clients[slot]->broker;
        output[count].descriptions = clients[slot]->descriptions;
        output[count].owned = clients[slot]->owned;
        output[count].connected = clients[slot]->connected;
        if (clients[slot]->connected == 0u) { ++count; continue; }
        output[count].session = client->session;
        output[count].activity = client->activity;
        output[count].port_service = client->port_service;
        if (astra_vfs_port_exec_lane_export(client, &output[count].lane) !=
            ASTRA_VFS_OK)
            return ASTRA_VFS_ERR_IO;
        output[count].port_direct_area = client->port_direct_area;
        output[count].port_direct_scope = client->port_direct_scope;
        output[count].port_direct_session = client->port_direct_session;
        output[count].version = client->version;
        output[count].features = client->features;
        output[count].port_area_capable = client->port_area_capable;
        ++count;
    }
    header->client_count = count;
    const AstraAssignTable *table = namespace_current != NULL ? &namespace_current->table : &assigns;
    header->assign_count = table->count;
    ProcessVfsExecAssign *bindings = (ProcessVfsExecAssign *)(void *)(output + count);
    uint32_t string_at = sizeof(*header) + count * sizeof(*output) +
        table->count * sizeof(*bindings);
    header->directory_offset = string_at;
    header->directory_length = strlen(current_directory == NULL ? "" : current_directory) + 1u;
    memcpy((uint8_t *)state + string_at, current_directory == NULL ? "" : current_directory,
        header->directory_length);
    string_at += header->directory_length;
    for (uint32_t at = 0u; at < table->count; ++at) {
        const AstraAssign *entry = &table->entries[at];
        memcpy(bindings[at].name, entry->name, sizeof(entry->name));
        bindings[at].root_offset = string_at;
        bindings[at].root_length = strlen(entry->root) + 1u;
        bindings[at].handle = entry->handle;
        bindings[at].rights = entry->rights;
        bindings[at].volume = entry->volume;
        bindings[at].broker = entry->broker;
        memcpy((uint8_t *)state + string_at, entry->root, bindings[at].root_length);
        string_at += bindings[at].root_length;
    }
    if (string_at != required)
        return ASTRA_VFS_ERR_PROTOCOL;
    header->watch = namespace_watch;
    memcpy(header->current_assign, current_assign, sizeof(current_assign));
    header->current_ready = current_ready;
    if (current_file._private_file != ASTRA_VFS_FILE_INVALID)
        return file_export_locked(&current_file, &header->cwd);
    return ASTRA_VFS_OK;
}

uint32_t astra_process_vfs_state_size(void)
{
    if (astra_mutex_lock(&current_lock) != ASTRA_SYSCALL_OK) return 0u;
    uint32_t size = 0u;
    if (astra_mutex_lock(&cache_lock) == ASTRA_SYSCALL_OK) {
        size = state_size_locked();
        (void)astra_mutex_unlock(&cache_lock);
    }
    (void)astra_mutex_unlock(&current_lock);
    return size;
}

uint32_t astra_process_vfs_export(void *state, uint32_t capacity, uint32_t *used)
{
    if (astra_mutex_lock(&current_lock) != ASTRA_SYSCALL_OK) return ASTRA_VFS_ERR_IO;
    uint32_t status = ASTRA_VFS_ERR_IO;
    if (astra_mutex_lock(&cache_lock) == ASTRA_SYSCALL_OK) {
        status = export_locked(state, capacity, used);
        (void)astra_mutex_unlock(&cache_lock);
    }
    (void)astra_mutex_unlock(&current_lock);
    return status;
}

static uint32_t client_handle_locked(const AstraVfsClient *client)
{
    for (uint32_t slot = 0u; slot < client_count; ++slot)
        if (&clients[slot]->client == client)
            return clients[slot]->handle;
    return 0u;
}

uint32_t astra_process_vfs_client_handle(const AstraVfsClient *client)
{
    uint32_t handle = 0u;

    if (astra_mutex_lock(&cache_lock) != ASTRA_SYSCALL_OK)
        return 0u;
    handle = client_handle_locked(client);
    (void)astra_mutex_unlock(&cache_lock);
    return handle;
}

AstraVfsClient *astra_process_vfs_client_handle_lookup(uint32_t handle)
{
    AstraVfsClient *client = NULL;

    if (astra_mutex_lock(&cache_lock) != ASTRA_SYSCALL_OK)
        return NULL;
    for (uint32_t slot = 0u; slot < client_count; ++slot)
        if (clients[slot]->handle == handle) {
            client = client_ready(slot);
            break;
        }
    (void)astra_mutex_unlock(&cache_lock);
    return client;
}

static uint32_t file_export_locked(const AstraFile *file,
                                   AstraProcessFileState *state)
{
    uint32_t service;

    if (file == NULL || state == NULL || file->_private_client == NULL ||
        file->_private_file == ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_ERR_INVALID;
    service = client_handle_locked(file->_private_client);
    if (service == 0u)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    uint32_t owner_status = astra_vfs_file_owner_status(file->_private_client,
        file->_private_ownership_epoch);
    if (owner_status != ASTRA_VFS_OK) return owner_status;
    *state = (AstraProcessFileState){
        .offset = file->_private_offset,
        .size = file->_private_size,
        .service = service,
        .file = file->_private_file,
        .flags = file->_private_flags,
        .kind = file->_private_kind,
        .member = file->_private_member,
    };
    return ASTRA_VFS_OK;
}

uint32_t astra_process_file_export(const AstraFile *file, AstraProcessFileState *state)
{
    if (astra_mutex_lock(&cache_lock) != ASTRA_SYSCALL_OK) return ASTRA_VFS_ERR_IO;
    uint32_t status = file_export_locked(file, state);
    (void)astra_mutex_unlock(&cache_lock);
    return status;
}

uint32_t astra_process_file_import(const AstraProcessFileState *state,
                                   AstraFile *file)
{
    AstraVfsClient *client;

    if (state == NULL || file == NULL || state->service == 0u ||
        state->file == ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_ERR_INVALID;
    client = astra_process_vfs_client_handle_lookup(state->service);
    if (client == NULL)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    *file = (AstraFile)ASTRA_FILE_INIT;
    file->_private_client = client;
    file->_private_read = astra_vfs_port_read_bulk_position;
    file->_private_write = astra_vfs_port_write_bulk_position;
    file->_private_file = state->file;
    file->_private_ownership_epoch = client->ownership_epoch;
    file->_private_flags = state->flags;
    file->_private_offset = state->offset;
    file->_private_size = state->size;
    file->_private_kind = state->kind;
    file->_private_member = state->member;
    return ASTRA_VFS_OK;
}

uint32_t astra_process_file_share(const AstraFile *file,
                                  AstraProcessFileShare *share)
{
    uint32_t status;

    if (share == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *share = (AstraProcessFileShare){0};
    status = astra_process_file_export(file, &share->file);
    if (status != ASTRA_VFS_OK) return status;
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_share(file->_private_client,
                                  file->_private_file, &share->token);
    /* A lost scope refreshes before sharing the disconnected description. */
    if (status == ASTRA_VFS_ERR_LOST)
        status = astra_vfs_share(file->_private_client,
                                  file->_private_file, &share->token);
    return status;
}

uint32_t astra_process_file_adopt(const AstraProcessFileShare *share,
                                  AstraFile *file)
{
    AstraVfsClient *client;
    AstraProcessFileState state;
    uint32_t status;
    AstraFile inherited;

    if (share == NULL || file == NULL || share->token == 0u)
        return ASTRA_VFS_ERR_INVALID;
    inherited = *file;
    if (inherited._private_client != NULL &&
        inherited._private_file != ASTRA_VFS_FILE_INVALID &&
        astra_vfs_file_owner_status(inherited._private_client,
            inherited._private_ownership_epoch) == ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_BUSY;
    client = astra_process_vfs_client_handle_lookup(share->file.service);
    if (client == NULL)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    state = share->file;
    status = astra_vfs_adopt(client, share->token, &state.file);
    if (status == ASTRA_VFS_OK) {
        status = astra_process_file_import(&state, file);
        if (status != ASTRA_VFS_OK)
            (void)astra_vfs_close(client, state.file);
        else if (inherited._private_client != NULL &&
                 inherited._private_file != ASTRA_VFS_FILE_INVALID)
            (void)astra_vfs_file_owner_close(inherited._private_client,
                inherited._private_file, inherited._private_ownership_epoch);
    }
    return status;
}

uint32_t astra_process_file_unshare(const AstraProcessFileShare *share)
{
    AstraVfsClient *client;

    if (share == NULL || share->token == 0u)
        return ASTRA_VFS_ERR_INVALID;
    client = astra_process_vfs_client_handle_lookup(share->file.service);
    return client == NULL ? ASTRA_VFS_ERR_BAD_HANDLE :
        astra_vfs_unshare(client, share->token);
}

uint32_t astra_process_vfs_import(const AstraStartupInfo *startup,
                                  const void *state, uint32_t size)
{
    const ProcessVfsExecHeader *header = state;
    const ProcessVfsExecClient *input;
    const char *failure_operation = NULL;
    uint32_t failure = ASTRA_VFS_ERR_INVALID;
    uint32_t status;

    if (state == NULL || size < sizeof(*header) ||
        header->magic != PROCESS_VFS_EXEC_MAGIC ||
        header->version != PROCESS_VFS_EXEC_VERSION ||
        header->size != size || header->current_ready > 1u ||
        header->current_assign[sizeof(header->current_assign) - 1u] != '\0' ||
        header->client_count >
            (size - (uint32_t)sizeof(*header)) /
                (uint32_t)sizeof(ProcessVfsExecClient) ||
        header->assign_count > (size - sizeof(*header) -
            header->client_count * sizeof(ProcessVfsExecClient)) / sizeof(ProcessVfsExecAssign) ||
        header->watch.reserved != 0u || ((header->watch.id == 0u) !=
            (header->watch.receive == ASTRA_INVALID_HANDLE)))
        return ASTRA_VFS_ERR_INVALID;
    input = (const ProcessVfsExecClient *)(const void *)(header + 1);
    const ProcessVfsExecAssign *bindings = (const ProcessVfsExecAssign *)(const void *)(input + header->client_count);
    uint32_t string_at = sizeof(*header) + header->client_count * sizeof(*input) +
        header->assign_count * sizeof(*bindings);
    for (uint32_t at = 0u; at <= header->assign_count; ++at) {
        uint32_t offset = at == 0u ? header->directory_offset : bindings[at - 1u].root_offset;
        uint32_t length = at == 0u ? header->directory_length : bindings[at - 1u].root_length;
        if (offset != string_at || length == 0u || length > ASTRA_VFS_PATH_MAX ||
            offset > size || length > size - offset)
            return ASTRA_VFS_ERR_INVALID;
        const char *text = (const char *)state + offset;
        if (text[length - 1u] != '\0' || memchr(text, '\0', length - 1u) != NULL ||
            astra_vfs_path_measure(text, NULL) != ASTRA_VFS_OK)
            return ASTRA_VFS_ERR_INVALID;
        if (at != 0u && (bindings[at - 1u].reserved != 0u ||
            bindings[at - 1u].name[ASTRA_ASSIGN_NAME_MAX - 1u] != '\0'))
            return ASTRA_VFS_ERR_INVALID;
        string_at += length;
    }
    if (string_at != size)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_process_vfs_init(startup);
    if (status != ASTRA_VFS_OK)
        return status;
    input = (const ProcessVfsExecClient *)(const void *)(header + 1);
    for (uint32_t index = 0u; index < header->client_count; ++index) {
        uint32_t slot;
        AstraVfsClient *client;

        if (input[index].owned > 1u || input[index].connected > 1u ||
            (input[index].connected != 0u &&
                (input[index].version != ASTRA_VFS_VERSION || input[index].port_area_capable > 1u)) ||
            input[index].reserved_owner != 0u || input[index].reserved != 0u ||
            (input[index].connected == 0u && input[index].descriptions != 0u) ||
            (input[index].owned && (input[index].volume.epoch == 0u ||
                input[index].volume.id == 0u || input[index].volume.generation == 0u ||
                input[index].broker == 0u ||
                (input[index].rights & ASTRA_RIGHT_READ) == 0u ||
                (input[index].rights & ~(ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE)) != 0u)) ||
            (!input[index].owned && (input[index].broker != 0u || input[index].volume.epoch != 0u ||
                input[index].volume.id != 0u || input[index].volume.generation != 0u)))
            goto invalid;
        for (slot = 0u; slot < client_count; ++slot)
            if (input[index].owned ? clients[slot]->owned &&
                clients[slot]->broker == input[index].broker &&
                clients[slot]->rights == input[index].rights && same_volume(&clients[slot]->volume, &input[index].volume) :
                !clients[slot]->owned && clients[slot]->handle == input[index].service)
                break;
        if (slot == client_count && input[index].owned) {
            status = append_client(input[index].service);
            if (status != ASTRA_VFS_OK) { failure = status; goto invalid; }
        }
        if (slot == client_count) goto invalid;
        if (clients[slot]->owned && clients[slot]->handle != input[index].service &&
            clients[slot]->handle != 0u) {
            if (clients[slot]->connected) (void)astra_vfs_disconnect(&clients[slot]->client);
            (void)astra_close(clients[slot]->handle);
        }
        clients[slot]->handle = input[index].service;
        clients[slot]->owned = input[index].owned;
        clients[slot]->rights = input[index].rights;
        clients[slot]->broker = input[index].broker;
        clients[slot]->volume = input[index].volume;
        clients[slot]->descriptions = input[index].descriptions;
        clients[slot]->connected = input[index].connected;
        if (input[index].connected == 0u) continue;
        if (input[index].service == 0u || input[index].port_service !=
                                    input[index].service ||
            ((input[index].port_direct_area == 0u) !=
                 (input[index].port_direct_scope == 0u)) ||
            ((input[index].port_direct_area == 0u) !=
                 (input[index].port_direct_session ==
                  ASTRA_VFS_SESSION_INVALID)))
            goto invalid;
        client = &clients[slot]->client;
        memset(client, 0, sizeof(*client));
        client->ownership_epoch = 1u;
        client->transport = astra_vfs_port_transport;
        client->context = client;
        client->area_payload = astra_vfs_port_call_area;
        client->call_acquire = astra_vfs_port_call_acquire;
        client->description_acquire = description_acquire;
        client->description_release = description_release;
        status = astra_rt_semaphore_create(
            1u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
            &client->port_connect_lock);
        if (status != ASTRA_SYSCALL_OK) {
            failure_operation = "VFS exec lock restore";
            failure = status == ASTRA_SYSCALL_RESOURCE_LIMIT ||
                      status == ASTRA_SYSCALL_OUT_OF_MEMORY ?
                ASTRA_VFS_ERR_LIMIT : ASTRA_VFS_ERR_IO;
            goto invalid;
        }
        client->session = input[index].session;
        client->activity = input[index].activity;
        client->port_service = input[index].port_service;
        client->version = input[index].version;
        client->features = input[index].features;
        client->port_area_capable = input[index].port_area_capable;
        clients[slot]->connected = 1u;
        status = astra_vfs_port_exec_lane_import(client, &input[index].lane);
        if (status != ASTRA_VFS_OK) {
            failure_operation = "VFS exec lane restore";
            failure = status;
            goto invalid;
        }
        if (input[index].port_direct_area != 0u &&
            (status = astra_vfs_host_direct_resume(
                client, input[index].port_direct_area,
                input[index].port_direct_scope,
                input[index].port_direct_session)) != ASTRA_VFS_OK) {
            failure_operation = "VFS exec direct restore";
            failure = status;
            goto invalid;
        }
    }
    ProcessNamespace *restored = astra_runtime_allocate(sizeof(*restored));
    if (restored == NULL) { failure = ASTRA_VFS_ERR_LIMIT; goto invalid; }
    *restored = (ProcessNamespace){.references = 1u};
    for (uint32_t at = 0u; at < header->assign_count; ++at) {
        const ProcessVfsExecAssign *binding = &bindings[at];
        const char *root = (const char *)state + binding->root_offset;
        if (binding->name[sizeof(binding->name) - 1u] != '\0' ||
            (binding->volume.id == 0u && (binding->volume.epoch != 0u ||
                binding->volume.generation != 0u || binding->broker != 0u))) {
            status = ASTRA_VFS_ERR_INVALID; break;
        }
        if (binding->volume.id != 0u) {
            if (binding->handle != 0u || root[0] != '\0') { status = ASTRA_VFS_ERR_INVALID; break; }
            AstraAssign restored_binding = {.volume = binding->volume,
                .broker = binding->broker, .rights = binding->rights};
            ProcessVfsClient *entry = volume_client(&restored_binding);
            status = astra_assign_bind_volume(&restored->table, binding->name, &binding->volume, binding->broker, binding->rights);
            if (status == ASTRA_VFS_OK && entry != NULL) ++entry->namespace_refs;
        } else if (astra_assign_lookup(&restored->table, binding->name) != NULL)
            status = astra_assign_join(&restored->table, binding->name, binding->handle, binding->rights, root);
        else status = astra_assign_bind(&restored->table, binding->name, binding->handle, binding->rights, root);
        if (status != ASTRA_VFS_OK) break;
    }
    if (status != ASTRA_VFS_OK) { namespace_release_locked(restored); failure = status; goto invalid; }
    restored->next = namespace_owners;
    namespace_owners = restored;
    namespace_release_locked(namespace_current);
    namespace_current = restored;
    if (namespace_watch.receive != ASTRA_INVALID_HANDLE &&
        (namespace_watch.id != header->watch.id || namespace_watch.receive != header->watch.receive)) {
        AstraResult closed = astra_volume_watch_close(namespace_catalog, &namespace_watch);
        (void)closed;
    }
    namespace_watch = header->watch;
    namespace_dirty = 1u;
    memcpy(current_assign, header->current_assign, sizeof(current_assign));
    memcpy(current_directory, (const uint8_t *)state + header->directory_offset,
        header->directory_length);
    current_ready = (uint8_t)header->current_ready;
    if (header->cwd.file != ASTRA_VFS_FILE_INVALID) {
        failure = astra_process_file_import(&header->cwd, &current_file);
        if (failure != ASTRA_VFS_OK || current_file._private_kind != ASTRA_VFS_KIND_DIRECTORY)
            goto invalid;
    }
    return ASTRA_VFS_OK;

invalid:
    if (failure_operation != NULL)
        (void)astra_log_failure(failure_operation, failure);
    astra_process_vfs_close();
    return failure;
}

/* Initialise the client at first use. The transport admits its session and
 * path area before the first complete operation; no startup-wide connects. */
static AstraVfsClient *client_ready(uint32_t slot)
{
    if (slot >= client_count)
        return NULL;
    /* A dead grant stays lost; files borrow this cell until process teardown. */
    if (clients[slot]->connected == 0u) {
        uint32_t ownership_epoch = clients[slot]->client.ownership_epoch;
        if (clients[slot]->owned && clients[slot]->handle == 0u) {
            AstraHandle root = ASTRA_INVALID_HANDLE;
            AstraResult result = astra_volume_root(clients[slot]->broker, &clients[slot]->volume,
                clients[slot]->rights, &root);
            if (result != ASTRA_OK) return NULL;
            clients[slot]->handle = root;
        }
        uint32_t status = astra_vfs_client_connect_service(
            &clients[slot]->client, clients[slot]->handle);
        clients[slot]->client.ownership_epoch = ownership_epoch;

        if (status != ASTRA_VFS_OK) {
            (void)astra_log_failure("VFS port reconnect", status);
            return NULL;
        }
        clients[slot]->connected = 1u;
        clients[slot]->client.description_acquire = description_acquire;
        clients[slot]->client.description_release = description_release;
    }
    return &clients[slot]->client;
}

uint32_t astra_process_library_source_open(
    const char *identity, AstraVfsReadSource *source,
    AstraLibraryReference *reference)
{
    const AstraAssignTable *table;
    void *owner;
    uint32_t status = astra_process_vfs_namespace_acquire(&table, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_vfs_library_source_open(source, table, "LIBS", identity,
        astra_process_vfs_assign_client, NULL, reference);
    astra_process_vfs_namespace_release(owner);
    return status;
}

static uint32_t seed_process_vfs(const AstraStartupInfo *startup,
                                 int fork_child)
{
    const AstraStartupCapability *capabilities;
    uint32_t status;

    if (!astra_startup_validate(startup) ||
        startup->capabilities_address == 0u)
        return ASTRA_VFS_ERR_INVALID;
    if (fork_child) {
        for (uint32_t index = 0u; index < client_count; ++index)
            if (clients[index]->client.ownership_epoch == UINT32_MAX ||
                clients[index]->descriptions > UINT32_MAX - clients[index]->inherited_descriptions)
                return ASTRA_VFS_ERR_LIMIT;
        /* Receive endpoints are process-private and are not cloned. */
        namespace_watch = (AstraVolumeWatch)ASTRA_VOLUME_WATCH_INIT;
        namespace_dirty = 1u;
        /* The parent's lanes did not come across; forget them once, before
         * any client's after_fork opens a lane of the child's own. */
        astra_vfs_host_direct_fork_child();
        for (uint32_t index = 0u; index < client_count; ++index) {
            uint32_t status;
            /* Copied native owners keep invalid local pins until they close
               or receipt adoption replaces each pin with a child token. */
            uint32_t epoch = ++clients[index]->client.ownership_epoch;
            clients[index]->inherited_descriptions += clients[index]->descriptions;
            clients[index]->descriptions = 0u;

            if (clients[index]->connected == 0u)
                continue;
            astra_vfs_port_after_fork_child(&clients[index]->client);
            if (clients[index]->client.port_direct_address != NULL) {
                status = astra_vfs_host_direct_after_fork(
                    &clients[index]->client);
                if (status != ASTRA_VFS_OK)
                    return status;
            } else {
                astra_vfs_port_abandon(&clients[index]->client);
                clients[index]->connected = 0u;
            }
            clients[index]->client.ownership_epoch = epoch;
            clients[index]->client.description_acquire = description_acquire;
            clients[index]->client.description_release = description_release;
        }
        /* Fork preserves the namespace and its handles exactly. */
        return vfs_initialized ? ASTRA_VFS_OK : ASTRA_VFS_ERR_NOT_FOUND;
    }
    capabilities = (const AstraStartupCapability *)(uintptr_t)
        startup->capabilities_address;
    close_process_vfs();
    current_directory = astra_runtime_allocate(ASTRA_VFS_PATH_MAX);
    if (current_directory == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    current_directory[0] = '\0';
    status = astra_assign_seed(&assigns, capabilities,
                               startup->capability_count);
    if (status != ASTRA_VFS_OK)
        return status;
    const AstraStartupCapability *catalog = astra_startup_capability(startup, "VOLUMES");
    const AstraStartupCapability *reader = astra_startup_capability(startup, "VOLUME_READ");
    const AstraStartupCapability *writer = astra_startup_capability(startup, "VOLUME_WRITE");
    const AstraStartupCapability *ram_reader = astra_startup_capability(startup, ASTRA_CAPABILITY_RAM_READ);
    const AstraStartupCapability *ram_writer = astra_startup_capability(startup, ASTRA_CAPABILITY_RAM_WRITE);
    namespace_catalog = catalog != NULL ? catalog->handle : ASTRA_INVALID_HANDLE;
    namespace_read = reader != NULL ? reader->handle : ASTRA_INVALID_HANDLE;
    namespace_write = writer != NULL ? writer->handle : ASTRA_INVALID_HANDLE;
    namespace_ram_read = ram_reader != NULL ? ram_reader->handle : ASTRA_INVALID_HANDLE;
    namespace_ram_write = ram_writer != NULL ? ram_writer->handle : ASTRA_INVALID_HANDLE;
    if (namespace_read == ASTRA_INVALID_HANDLE) namespace_read = namespace_write;
    namespace_dirty = 1u;
    for (uint32_t index = 0u; index < assigns.count; ++index) {
        uint32_t slot;

        for (slot = 0u; slot < client_count; ++slot)
            if (clients[slot]->handle == assigns.entries[index].handle)
                break;
        if (slot != client_count)
            continue;
        if (append_client(assigns.entries[index].handle) != ASTRA_VFS_OK) {
            close_process_vfs();
            return ASTRA_VFS_ERR_LIMIT;
        }
    }
    vfs_initialized = client_count != 0u ||
        (namespace_catalog != ASTRA_INVALID_HANDLE && (namespace_read != ASTRA_INVALID_HANDLE ||
            namespace_ram_read != ASTRA_INVALID_HANDLE || namespace_ram_write != ASTRA_INVALID_HANDLE));
    if (vfs_initialized) {
        const AstraAssign *cwd = astra_assign_lookup(&assigns, "CWD");

        if (cwd == NULL)
            cwd = astra_assign_lookup(&assigns, "WORK");
        if (cwd != NULL)
            (void)strcpy(current_assign, cwd->name);
    }
    return vfs_initialized ? ASTRA_VFS_OK : ASTRA_VFS_ERR_NOT_FOUND;
}

uint32_t astra_process_vfs_init(const AstraStartupInfo *startup)
{
    uint32_t status;

    if (astra_mutex_lock(&cache_lock) != ASTRA_SYSCALL_OK)
        return ASTRA_VFS_ERR_IO;
    status = vfs_initialized ? ASTRA_VFS_OK : seed_process_vfs(startup, 0);
    (void)astra_mutex_unlock(&cache_lock);
    return status;
}

uint32_t astra_process_vfs_after_fork_child(
    const AstraStartupInfo *startup, const AstraProcessFileShare *cwd)
{
    uint32_t status;

    current_lock = 0u;
    cache_lock = 0u;
    if (current_file._private_file != ASTRA_VFS_FILE_INVALID &&
        (cwd == NULL || cwd->token == 0u))
        return ASTRA_VFS_ERR_INVALID;
    status = vfs_initialized ? seed_process_vfs(startup, 1) : ASTRA_VFS_OK;
    if (status == ASTRA_VFS_OK && cwd != NULL && cwd->token != 0u)
        status = astra_process_file_adopt(cwd, &current_file);
    return status;
}

const AstraAssignTable *astra_process_vfs_assigns(void)
{
    return &assigns;
}

/* This low-level unit also runs in the eager interpreter before Filesystem Kit. */
static uint32_t current_open(const char *path, AstraFile *file,
                              char assign_name[ASTRA_ASSIGN_NAME_MAX],
                              char relative[ASTRA_VFS_PATH_MAX])
{
    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    const AstraAssign *binding = NULL;
    AstraVfsDirEntry info = {0};
    AstraVfsClient *client = NULL;
    char *wire = astra_vfs_path_storage_at(&path_storage, 0u);
    uint32_t member = 0u;
    uint32_t status;
    const AstraAssignTable *table;
    void *owner;

    status = astra_process_vfs_namespace_acquire(&table, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_vfs_assign_stat(table, path, ASTRA_RIGHT_READ,
        astra_process_vfs_assign_client, NULL, wire, ASTRA_VFS_PATH_MAX, &info,
        &client, &binding, &member);
    if (status != ASTRA_VFS_OK) goto done;
    if (info.kind != ASTRA_VFS_KIND_DIRECTORY) {
        status = ASTRA_VFS_ERR_NOT_DIR;
        goto done;
    }
    status = astra_vfs_open(client, wire,
        ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY,
        &file->_private_file, &file->_private_size, &file->_private_kind);
    if (status != ASTRA_VFS_OK) goto done;
    file->_private_client = client;
    file->_private_read = astra_vfs_port_read_bulk_position;
    file->_private_write = astra_vfs_port_write_bulk_position;
    file->_private_flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY;
    file->_private_ownership_epoch = client->ownership_epoch;
    file->_private_member = (uint16_t)member | (uint16_t)(binding->rights << 8);
    strcpy(assign_name, binding->name);
    const char *rest = wire;
    size_t root = strlen(binding->root);
    if (rest[0] == '/')
        ++rest;
    if (root != 0u && strncmp(rest, binding->root, root) == 0 &&
        (rest[root] == '\0' || rest[root] == '/')) {
        rest += root;
        if (*rest == '/')
            ++rest;
    }
    strcpy(relative, rest);
done:
    astra_process_vfs_namespace_release(owner);
    return status;
}

static uint32_t current_prepare(void)
{
    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    uint32_t status;
    char *path = astra_vfs_path_storage_at(&path_storage, 0u);

    if (current_ready != 0u)
        return current_file._private_file == ASTRA_VFS_FILE_INVALID ? ASTRA_VFS_OK :
            astra_vfs_file_owner_status(current_file._private_client,
                current_file._private_ownership_epoch);
    if (current_assign[0] == '\0') {
        current_ready = 1u;
        return ASTRA_VFS_OK;
    }
    status = astra_path_qualify(current_assign, current_directory, NULL, path, ASTRA_VFS_PATH_MAX);
    if (status == ASTRA_VFS_OK)
        status = current_open(path, &current_file, current_assign, current_directory);
    if (status != ASTRA_VFS_OK)
        return status;
    if (!(current_file._private_client->features & ASTRA_VFS_FEATURE_DIRECTORY_PATH)) {
        (void)astra_vfs_file_owner_close(current_file._private_client,
            current_file._private_file, current_file._private_ownership_epoch);
        current_file = (AstraFile)ASTRA_FILE_INIT;
    }
    current_ready = 1u;
    return ASTRA_VFS_OK;
}

uint32_t astra_process_vfs_current_acquire(const AstraFile **directory)
{
    uint32_t status;

    if (directory == NULL || !vfs_initialized)
        return ASTRA_VFS_ERR_INVALID;
    *directory = NULL;
    if (astra_mutex_lock(&current_lock) != ASTRA_SYSCALL_OK)
        return ASTRA_VFS_ERR_IO;
    status = current_prepare();
    if (status == ASTRA_VFS_OK) {
        if (current_file._private_file != ASTRA_VFS_FILE_INVALID)
            *directory = &current_file;
        return ASTRA_VFS_OK;
    }
    astra_process_vfs_current_release();
    return status;
}

void astra_process_vfs_current_release(void)
{
    (void)astra_mutex_unlock(&current_lock);
}

/* Called with current_lock held, including namespace continuation fallback. */
uint32_t astra_process_vfs_current_qualify(const char *path, char *out, uint32_t capacity)
{
    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(2u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    char *spelling = astra_vfs_path_storage_at(&path_storage, 0u);
    char *relative = astra_vfs_path_storage_at(&path_storage, 1u);
    uint32_t length = 0u;
    uint32_t status;

    if (current_file._private_file != ASTRA_VFS_FILE_INVALID) {
        status = astra_vfs_file_owner_status(current_file._private_client,
            current_file._private_ownership_epoch);
        if (status != ASTRA_VFS_OK)
            return status;
        status = astra_vfs_directory_path(current_file._private_client,
            current_file._private_file, relative, ASTRA_VFS_PATH_MAX - 1u, &length);
        if (status != ASTRA_VFS_OK)
            return status;
        relative[length] = '\0';
        const AstraAssignTable *table;
        void *owner;
        status = astra_process_vfs_namespace_acquire(&table, &owner);
        if (status != ASTRA_VFS_OK) return status;
        const AstraAssign *binding = astra_assign_member(table, current_assign,
            current_file._private_member & 0xffu);
        ProcessVfsClient *held = NULL;
        if (astra_mutex_lock(&cache_lock) != ASTRA_SYSCALL_OK) {
            astra_process_vfs_namespace_release(owner);
            return ASTRA_VFS_ERR_IO;
        }
        for (uint32_t at = 0u; at < client_count; ++at)
            if (&clients[at]->client == current_file._private_client) held = clients[at];
        if (binding == NULL || held == NULL ||
            (held->owned ? !same_volume(&held->volume, &binding->volume) : binding->handle != held->handle))
            status = ASTRA_VFS_ERR_LOST;
        else status = astra_path_qualify_provider(current_assign, binding->root,
            relative, path, out, capacity);
        (void)astra_mutex_unlock(&cache_lock);
        astra_process_vfs_namespace_release(owner);
        return status;
    }
    if (current_assign[0] != '\0')
        return astra_path_qualify(current_assign, current_directory, path, out, capacity);
    if (strlen(path) + 2u > ASTRA_VFS_PATH_MAX)
        return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    spelling[0] = '/';
    strcpy(spelling + 1, path);
    return astra_path_collapse(spelling, out, capacity);
}

uint32_t astra_process_vfs_current_directory(char *path, uint32_t capacity)
{
    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    const AstraFile *directory;
    char *spelling = astra_vfs_path_storage_at(&path_storage, 0u);
    uint32_t status;

    if (path == NULL || capacity == 0u)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_process_vfs_current_acquire(&directory);
    if (status != ASTRA_VFS_OK)
        return status;
    status = astra_process_vfs_current_qualify("", spelling, ASTRA_VFS_PATH_MAX);
    astra_process_vfs_current_release();
    if (status == ASTRA_VFS_OK) {
        uint32_t length = (uint32_t)strlen(spelling) + 1u;
        if (length > capacity)
            return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
        memcpy(path, spelling, length);
    }
    return status;
}

uint32_t astra_process_vfs_share_current_directory(AstraProcessFileShare *share)
{
    const AstraFile *directory;
    uint32_t status;

    if (share == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *share = (AstraProcessFileShare){0};
    if (!vfs_initialized)
        return ASTRA_VFS_OK;
    status = astra_process_vfs_current_acquire(&directory);
    if (status == ASTRA_VFS_OK) {
        if (directory != NULL)
            status = astra_process_file_share(directory, share);
        if (status != ASTRA_VFS_OK && current_file._private_client != NULL &&
            namespace_catalog != ASTRA_INVALID_HANDLE) {
            const ProcessVfsClient *entry =
                (const ProcessVfsClient *)(const void *)current_file._private_client;
            if (entry->owned && entry->volume.id != 0u) {
                AstraVolumeSnapshot catalog = ASTRA_VOLUME_SNAPSHOT_INIT;
                if (astra_volume_snapshot_open(namespace_catalog, &catalog) == ASTRA_OK) {
                    int live = 0;
                    for (uint32_t at = 0u; at < catalog.count; ++at) {
                        const AstraVolumeRecord *record = &catalog.records[at];
                        if (catalog.epoch == entry->volume.epoch &&
                            record->id == entry->volume.id &&
                            record->generation == entry->volume.generation &&
                            record->kind == ASTRA_VOLUME_MOUNT &&
                            (record->state == ASTRA_VOLUME_MOUNTED ||
                             record->state == ASTRA_VOLUME_QUIESCING)) {
                            live = 1;
                            break;
                        }
                    }
                    astra_volume_snapshot_close(&catalog);
                    if (!live) status = ASTRA_VFS_ERR_LOST;
                }
            }
        }
        astra_process_vfs_current_release();
    }
    return status;
}

uint32_t astra_process_vfs_set_current_directory(const char *path)
{
    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(2u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    const AstraFile *directory;
    AstraFile opened = ASTRA_FILE_INIT;
    char *qualified = astra_vfs_path_storage_at(&path_storage, 0u);
    char name[ASTRA_ASSIGN_NAME_MAX];
    char *relative = astra_vfs_path_storage_at(&path_storage, 1u);
    uint32_t status;

    if (path == NULL || path[0] == '\0')
        return ASTRA_VFS_ERR_INVALID;
    int bare = path[0] != '/';
    if (bare)
        status = astra_process_vfs_current_acquire(&directory);
    else {
        directory = NULL;
        status = vfs_initialized && astra_mutex_lock(&current_lock) == ASTRA_SYSCALL_OK ?
            ASTRA_VFS_OK : ASTRA_VFS_ERR_INVALID;
    }
    if (status != ASTRA_VFS_OK)
        return status;
    if (bare && directory != NULL) {
        status = astra_vfs_open_at(directory->_private_client, directory->_private_file,
            path, ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY,
            &opened._private_file, &opened._private_size, &opened._private_kind);
        if (status == ASTRA_VFS_OK) {
            opened._private_client = directory->_private_client;
            opened._private_ownership_epoch = opened._private_client->ownership_epoch;
            opened._private_read = directory->_private_read;
            opened._private_write = directory->_private_write;
            opened._private_flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY;
            opened._private_member = directory->_private_member;
            strcpy(name, current_assign);
            relative[0] = '\0';
        }
    } else
        status = ASTRA_VFS_ERR_ESCAPED;
    if (status == ASTRA_VFS_ERR_ESCAPED) {
        status = bare ? astra_process_vfs_current_qualify(path, qualified, ASTRA_VFS_PATH_MAX) :
            astra_path_collapse(path, qualified, ASTRA_VFS_PATH_MAX);
        if (status == ASTRA_VFS_OK && strcmp(qualified, "/") == 0) {
            name[0] = '\0';
            relative[0] = '\0';
        } else if (status == ASTRA_VFS_OK)
            status = current_open(qualified, &opened, name, relative);
        if (status == ASTRA_VFS_ERR_ESCAPED) {
            name[0] = '\0';
            relative[0] = '\0';
            status = ASTRA_VFS_OK;
        }
    }
    if (status == ASTRA_VFS_OK) {
        if (opened._private_file != ASTRA_VFS_FILE_INVALID &&
            !(opened._private_client->features & ASTRA_VFS_FEATURE_DIRECTORY_PATH)) {
            (void)astra_vfs_close(opened._private_client, opened._private_file);
            opened = (AstraFile)ASTRA_FILE_INIT;
        }
        if (current_file._private_file != ASTRA_VFS_FILE_INVALID)
            (void)astra_vfs_file_owner_close(current_file._private_client,
                current_file._private_file, current_file._private_ownership_epoch);
        current_file = opened;
        strcpy(current_assign, name);
        strcpy(current_directory, relative);
        current_ready = 1u;
    }
    astra_process_vfs_current_release();
    return status;
}

uint32_t astra_process_vfs_source_open(const char *path, AstraVfsReadSource *source)
{
    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    const AstraFile *directory;
    char *qualified = astra_vfs_path_storage_at(&path_storage, 0u);
    uint64_t size = 0u;
    uint16_t kind = 0u;
    uint32_t status;

    if (path == NULL || source == NULL || source->file != ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_ERR_INVALID;
    if (path[0] == '/') {
        const AstraAssignTable *table;
        void *owner;
        status = astra_process_vfs_namespace_acquire(&table, &owner);
        if (status != ASTRA_VFS_OK) return status;
        status = astra_vfs_read_source_open(source, table, path, astra_process_vfs_assign_client, NULL);
        astra_process_vfs_namespace_release(owner);
        return status;
    }
    status = astra_process_vfs_current_acquire(&directory);
    if (status != ASTRA_VFS_OK)
        return status;
    if (directory != NULL) {
        status = astra_vfs_open_at(directory->_private_client, directory->_private_file,
            path, ASTRA_VFS_OPEN_READ, &source->file, &size, &kind);
        source->client = directory->_private_client;
        source->ownership_epoch = source->client->ownership_epoch;
        if (status == ASTRA_VFS_OK && (kind != ASTRA_VFS_KIND_FILE || size > UINT32_MAX)) {
            (void)astra_vfs_read_source_close(source);
            status = kind != ASTRA_VFS_KIND_FILE ? ASTRA_VFS_ERR_INVALID : ASTRA_VFS_ERR_LIMIT;
        }
        if (status == ASTRA_VFS_OK)
            source->length = (uint32_t)size;
    } else
        status = ASTRA_VFS_ERR_ESCAPED;
    if (status == ASTRA_VFS_ERR_ESCAPED) {
        status = astra_process_vfs_current_qualify(path, qualified, ASTRA_VFS_PATH_MAX);
        if (status == ASTRA_VFS_OK) {
            const AstraAssignTable *table;
            void *owner;
            status = astra_process_vfs_namespace_acquire(&table, &owner);
            if (status == ASTRA_VFS_OK) {
                status = astra_vfs_read_source_open(source, table, qualified, astra_process_vfs_assign_client, NULL);
                astra_process_vfs_namespace_release(owner);
            }
        }
    }
    astra_process_vfs_current_release();
    return status;
}

AstraVfsClient *astra_process_vfs_client(void)
{
    return astra_process_vfs_client_for(NULL);
}

AstraVfsClient *astra_process_vfs_client_for(const AstraAssign *assign)
{
    AstraVfsClient *client = NULL;
    uint32_t index = 0u;

    if (astra_mutex_lock(&cache_lock) != ASTRA_SYSCALL_OK)
        return NULL;
    if (assign != NULL) {
        for (; index < client_count; ++index)
            if (assign->volume.id != 0u ?
                clients[index]->owned && clients[index]->rights == assign->rights &&
                    clients[index]->broker == assign->broker &&
                    same_volume(&clients[index]->volume, &assign->volume) :
                clients[index]->handle == assign->handle && !clients[index]->owned)
                break;
    }
    if (assign != NULL && assign->volume.id != 0u && index == client_count) {
        uint32_t references = 0u;
        for (ProcessNamespace *view = namespace_owners; view != NULL; view = view->next)
            for (uint32_t at = 0u; at < view->table.count; ++at)
                if (view->table.entries[at].rights == assign->rights &&
                    view->table.entries[at].broker == assign->broker &&
                    same_volume(&view->table.entries[at].volume, &assign->volume))
                    ++references;
        if (references == 0u || append_client(0u) != ASTRA_VFS_OK) {
            (void)astra_mutex_unlock(&cache_lock);
            return NULL;
        }
        clients[index]->owned = 1u;
        clients[index]->rights = assign->rights;
        clients[index]->broker = assign->broker;
        clients[index]->volume = assign->volume;
        clients[index]->namespace_refs = references;
    }
    if (assign != NULL && index == client_count &&
        append_client(assign->handle) != ASTRA_VFS_OK) {
        (void)astra_log_failure("VFS client allocation", ASTRA_VFS_ERR_LIMIT);
    } else
        client = client_ready(index);
    (void)astra_mutex_unlock(&cache_lock);
    return client;
}

AstraVfsClient *astra_process_vfs_assign_client(const AstraAssign *assign,
                                                void *context)
{
    (void)context;
    return astra_process_vfs_client_for(assign);
}

void astra_process_vfs_set_activity(uint32_t activity)
{
    if (astra_mutex_lock(&cache_lock) != ASTRA_SYSCALL_OK)
        return;
    for (uint32_t index = 0u; index < client_count; ++index)
        clients[index]->client.activity = activity;
    (void)astra_mutex_unlock(&cache_lock);
}

/* Preserve relative input for the process facade, which owns CWD identity. */
uint32_t astra_process_path(const char *typed, char *out, uint32_t capacity)
{
    if (typed == NULL || out == NULL || strlen(typed) >= capacity)
        return ASTRA_VFS_ERR_INVALID;
    memcpy(out, typed, strlen(typed) + 1u);
    return ASTRA_VFS_OK;
}
