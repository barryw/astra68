#include <astra/bytes.h>
#include <astra/ascii.h>
#include <astra/runtime.h>
#include <astra/syscall.h>
#include <astra/vfs_path.h>
#include <astra/vfs_host_direct.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_process.h>
#include <astra/vfs_library_source.h>

#include <stddef.h>
#include <string.h>
#if defined(ASTRA_VFS_PROCESS_TEST)
#include <stdlib.h>
#endif

static AstraAssignTable assigns;
static char current_assign[ASTRA_CAPABILITY_NAME_MAX];
static char current_directory[ASTRA_VFS_PATH_MAX];
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
    uint8_t connected;
} ProcessVfsClient;

static ProcessVfsClient **clients;
static uint32_t client_count;
static uint32_t client_capacity;
static uint8_t vfs_initialized;
static _Alignas(4) volatile uint32_t cache_lock;
static AstraVfsClient *client_ready(uint32_t slot);

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
    *clients[client_count++] = (ProcessVfsClient){.handle = handle};
    return ASTRA_VFS_OK;
}

#define PROCESS_VFS_EXEC_MAGIC 0x56465345u
/* 5 was 62b72fc2's layout (no lane fields), 6 had no features (storage
 * protocol 28); never reuse either. */
#define PROCESS_VFS_EXEC_VERSION 8u

typedef struct ProcessVfsExecHeader {
    uint32_t magic;
    uint32_t size;
    uint32_t client_count;
    uint32_t version;
    AstraProcessFileState cwd;
    char current_assign[ASTRA_CAPABILITY_NAME_MAX];
    char current_directory[ASTRA_VFS_PATH_MAX];
    uint32_t current_ready;
} ProcessVfsExecHeader;

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
} ProcessVfsExecClient;

static void close_process_vfs(void)
{
    if (current_file._private_file != ASTRA_VFS_FILE_INVALID)
        (void)astra_vfs_close(current_file._private_client, current_file._private_file);
    current_file = (AstraFile)ASTRA_FILE_INIT;
    current_ready = 0u;
    while (client_count != 0u) {
        --client_count;
        if (clients[client_count]->connected != 0u)
            (void)astra_vfs_disconnect(&clients[client_count]->client);
        clients[client_count]->handle = 0u;
        clients[client_count]->connected = 0u;
    }
    vfs_initialized = 0u;
    current_assign[0] = '\0';
    current_directory[0] = '\0';
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

uint32_t astra_process_vfs_state_size(void)
{
    uint32_t connected = 0u;

    for (uint32_t slot = 0u; slot < client_count; ++slot)
        if (clients[slot]->connected != 0u)
            ++connected;
    return (uint32_t)sizeof(ProcessVfsExecHeader) +
           connected * (uint32_t)sizeof(ProcessVfsExecClient);
}

uint32_t astra_process_vfs_export(void *state, uint32_t capacity,
                                  uint32_t *used)
{
    ProcessVfsExecHeader *header = state;
    ProcessVfsExecClient *output;
    uint32_t required = astra_process_vfs_state_size();
    uint32_t count = 0u;

    if (used == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *used = required;
    if (state == NULL || capacity < required)
        return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    memset(state, 0, required);
    header->magic = PROCESS_VFS_EXEC_MAGIC;
    header->size = required;
    header->version = PROCESS_VFS_EXEC_VERSION;
    output = (ProcessVfsExecClient *)(void *)(header + 1);
    for (uint32_t slot = 0u; slot < client_count; ++slot) {
        AstraVfsClient *client = &clients[slot]->client;

        if (clients[slot]->connected == 0u)
            continue;
        output[count].service = clients[slot]->handle;
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
    memcpy(header->current_assign, current_assign, sizeof(current_assign));
    memcpy(header->current_directory, current_directory, sizeof(current_directory));
    header->current_ready = current_ready;
    if (current_file._private_file != ASTRA_VFS_FILE_INVALID)
        return astra_process_file_export(&current_file, &header->cwd);
    return ASTRA_VFS_OK;
}

uint32_t astra_process_vfs_client_handle(const AstraVfsClient *client)
{
    uint32_t handle = 0u;

    if (astra_mutex_lock(&cache_lock) != ASTRA_SYSCALL_OK)
        return 0u;
    for (uint32_t slot = 0u; slot < client_count; ++slot)
        if (&clients[slot]->client == client) {
            handle = clients[slot]->handle;
            break;
        }
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

uint32_t astra_process_file_export(const AstraFile *file,
                                   AstraProcessFileState *state)
{
    uint32_t service;

    if (file == NULL || state == NULL || file->_private_client == NULL ||
        file->_private_file == ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_ERR_INVALID;
    service = astra_process_vfs_client_handle(file->_private_client);
    if (service == 0u)
        return ASTRA_VFS_ERR_BAD_HANDLE;
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

    if (share == NULL || file == NULL || share->token == 0u)
        return ASTRA_VFS_ERR_INVALID;
    client = astra_process_vfs_client_handle_lookup(share->file.service);
    if (client == NULL)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    state = share->file;
    status = astra_vfs_adopt(client, share->token, &state.file);
    if (status == ASTRA_VFS_OK) {
        status = astra_process_file_import(&state, file);
        if (status != ASTRA_VFS_OK)
            (void)astra_vfs_close(client, state.file);
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
        header->current_directory[sizeof(header->current_directory) - 1u] != '\0' ||
        header->client_count >
            (size - (uint32_t)sizeof(*header)) /
                (uint32_t)sizeof(ProcessVfsExecClient) ||
        sizeof(*header) +
                header->client_count * sizeof(ProcessVfsExecClient) != size)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_process_vfs_init(startup);
    if (status != ASTRA_VFS_OK)
        return status;
    input = (const ProcessVfsExecClient *)(const void *)(header + 1);
    for (uint32_t index = 0u; index < header->client_count; ++index) {
        uint32_t slot;
        AstraVfsClient *client;

        for (slot = 0u; slot < client_count; ++slot)
            if (clients[slot]->handle == input[index].service)
                break;
        if (slot == client_count || input[index].port_service !=
                                    input[index].service ||
            ((input[index].port_direct_area == 0u) !=
                 (input[index].port_direct_scope == 0u)) ||
            ((input[index].port_direct_area == 0u) !=
                 (input[index].port_direct_session ==
                  ASTRA_VFS_SESSION_INVALID)))
            goto invalid;
        client = &clients[slot]->client;
        memset(client, 0, sizeof(*client));
        client->transport = astra_vfs_port_transport;
        client->context = client;
        client->area_payload = astra_vfs_port_call_area;
        client->call_acquire = astra_vfs_port_call_acquire;
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
    memcpy(current_assign, header->current_assign, sizeof(current_assign));
    memcpy(current_directory, header->current_directory, sizeof(current_directory));
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

/*
 * Initialise the client at first use. STOR v8+ defers the actual HELLO so the
 * transport can fuse it with the first path operation; older peers complete
 * HELLO and retry that operation inside the transport.
 */
static AstraVfsClient *client_ready(uint32_t slot)
{
    if (slot >= client_count)
        return NULL;
    /* A dead grant stays lost; files borrow this cell until process teardown. */
    if (clients[slot]->connected == 0u) {
        uint32_t status = astra_vfs_host_port_connect_lazy(
            &clients[slot]->client, clients[slot]->handle);

        if (status != ASTRA_VFS_OK) {
            (void)astra_log_failure("VFS port reconnect", status);
            return NULL;
        }
        clients[slot]->connected = 1u;
    }
    return &clients[slot]->client;
}

uint32_t astra_process_library_source_open(
    const char *identity, AstraVfsReadSource *source,
    AstraLibraryReference *reference)
{
    return astra_vfs_library_source_open(
        source, &assigns, "LIBS", identity,
        astra_process_vfs_assign_client, NULL, reference);
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
        /* The parent's lanes did not come across; forget them once, before
         * any client's after_fork opens a lane of the child's own. */
        astra_vfs_host_direct_fork_child();
        for (uint32_t index = 0u; index < client_count; ++index) {
            uint32_t status;

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
        }
        /* Fork preserves the namespace and its handles exactly. */
        return vfs_initialized ? ASTRA_VFS_OK : ASTRA_VFS_ERR_NOT_FOUND;
    }
    capabilities = (const AstraStartupCapability *)(uintptr_t)
        startup->capabilities_address;
    close_process_vfs();
    status = astra_assign_seed(&assigns, capabilities,
                               startup->capability_count);
    if (status != ASTRA_VFS_OK)
        return status;
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
    vfs_initialized = client_count != 0u;
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
    current_file = (AstraFile)ASTRA_FILE_INIT;
    status = vfs_initialized ? seed_process_vfs(startup, 1) : ASTRA_VFS_OK;
    if (status == ASTRA_VFS_OK && cwd != NULL && cwd->token != 0u)
        status = astra_process_file_adopt(cwd, &current_file);
    return status;
}

AstraAssignTable *astra_process_vfs_assigns(void)
{
    return &assigns;
}

/* This low-level unit also runs in the eager interpreter before Filesystem Kit. */
static uint32_t current_open(const char *path, AstraFile *file,
                              char assign_name[ASTRA_CAPABILITY_NAME_MAX],
                              char relative[ASTRA_VFS_PATH_MAX])
{
    const AstraAssign *binding = NULL;
    AstraVfsDirEntry info = {0};
    AstraVfsClient *client = NULL;
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t member = 0u;
    uint32_t status;

    status = astra_vfs_assign_stat(&assigns, path, ASTRA_RIGHT_READ,
        astra_process_vfs_assign_client, NULL, wire, sizeof(wire), &info,
        &client, &binding, &member);
    if (status != ASTRA_VFS_OK)
        return status;
    if (info.kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_NOT_DIR;
    status = astra_vfs_open(client, wire,
        ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY,
        &file->_private_file, &file->_private_size, &file->_private_kind);
    if (status != ASTRA_VFS_OK)
        return status;
    file->_private_client = client;
    file->_private_read = astra_vfs_port_read_bulk_position;
    file->_private_write = astra_vfs_port_write_bulk_position;
    file->_private_flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY;
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
    return ASTRA_VFS_OK;
}

static uint32_t current_prepare(void)
{
    uint32_t status;
    char path[ASTRA_VFS_PATH_MAX];

    if (current_ready != 0u)
        return ASTRA_VFS_OK;
    if (current_assign[0] == '\0') {
        current_ready = 1u;
        return ASTRA_VFS_OK;
    }
    status = astra_path_qualify(current_assign, current_directory, NULL, path, sizeof(path));
    if (status == ASTRA_VFS_OK)
        status = current_open(path, &current_file, current_assign, current_directory);
    if (status != ASTRA_VFS_OK)
        return status;
    if (!(current_file._private_client->features & ASTRA_VFS_FEATURE_DIRECTORY_PATH)) {
        (void)astra_vfs_close(current_file._private_client, current_file._private_file);
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
    char spelling[ASTRA_VFS_PATH_MAX];
    char relative[ASTRA_VFS_PATH_MAX];
    uint32_t length = 0u;
    uint32_t status;

    if (current_file._private_file != ASTRA_VFS_FILE_INVALID) {
        status = astra_vfs_directory_path(current_file._private_client,
            current_file._private_file, relative, sizeof(relative) - 1u, &length);
        if (status != ASTRA_VFS_OK)
            return status;
        relative[length] = '\0';
        const AstraAssign *binding = astra_assign_member(&assigns, current_assign,
            current_file._private_member & 0xffu);
        if (binding == NULL)
            return ASTRA_VFS_ERR_NOT_FOUND;
        return astra_path_qualify_provider(current_assign, binding->root,
            relative, path, out, capacity);
    }
    if (current_assign[0] != '\0')
        return astra_path_qualify(current_assign, current_directory, path, out, capacity);
    if (strlen(path) + 2u > sizeof(spelling))
        return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    spelling[0] = '/';
    strcpy(spelling + 1, path);
    return astra_path_collapse(spelling, out, capacity);
}

uint32_t astra_process_vfs_current_directory(char *path, uint32_t capacity)
{
    const AstraFile *directory;
    char spelling[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    if (path == NULL || capacity == 0u)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_process_vfs_current_acquire(&directory);
    if (status != ASTRA_VFS_OK)
        return status;
    status = astra_process_vfs_current_qualify("", spelling, sizeof(spelling));
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
        astra_process_vfs_current_release();
    }
    return status;
}

uint32_t astra_process_vfs_set_current_directory(const char *path)
{
    const AstraFile *directory;
    AstraFile opened = ASTRA_FILE_INIT;
    char qualified[ASTRA_VFS_PATH_MAX];
    char name[ASTRA_CAPABILITY_NAME_MAX];
    char relative[ASTRA_VFS_PATH_MAX];
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
        status = bare ? astra_process_vfs_current_qualify(path, qualified, sizeof(qualified)) :
            astra_path_collapse(path, qualified, sizeof(qualified));
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
            (void)astra_vfs_close(current_file._private_client, current_file._private_file);
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
    const AstraFile *directory;
    char qualified[ASTRA_VFS_PATH_MAX];
    uint64_t size = 0u;
    uint16_t kind = 0u;
    uint32_t status;

    if (path == NULL || source == NULL || source->file != ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_ERR_INVALID;
    if (path[0] == '/')
        return astra_vfs_read_source_open(source, &assigns, path, astra_process_vfs_assign_client, NULL);
    status = astra_process_vfs_current_acquire(&directory);
    if (status != ASTRA_VFS_OK)
        return status;
    if (directory != NULL) {
        status = astra_vfs_open_at(directory->_private_client, directory->_private_file,
            path, ASTRA_VFS_OPEN_READ, &source->file, &size, &kind);
        source->client = directory->_private_client;
        if (status == ASTRA_VFS_OK && (kind != ASTRA_VFS_KIND_FILE || size > UINT32_MAX)) {
            (void)astra_vfs_read_source_close(source);
            status = kind != ASTRA_VFS_KIND_FILE ? ASTRA_VFS_ERR_INVALID : ASTRA_VFS_ERR_LIMIT;
        }
        if (status == ASTRA_VFS_OK)
            source->length = (uint32_t)size;
    } else
        status = ASTRA_VFS_ERR_ESCAPED;
    if (status == ASTRA_VFS_ERR_ESCAPED) {
        status = astra_process_vfs_current_qualify(path, qualified, sizeof(qualified));
        if (status == ASTRA_VFS_OK)
            status = astra_vfs_read_source_open(source, &assigns, qualified, astra_process_vfs_assign_client, NULL);
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
            if (clients[index]->handle == assign->handle)
                break;
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
