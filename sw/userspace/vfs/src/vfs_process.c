#include <astra/bytes.h>
#include <astra/runtime.h>
#include <astra/syscall.h>
#include <astra/vfs_path.h>
#include <astra/vfs_host_direct.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_process.h>
#include <astra/vfs_reader.h>

#include <stddef.h>
#include <string.h>
#if defined(ASTRA_VFS_PROCESS_TEST)
#include <stdlib.h>
#endif

static AstraAssignTable assigns;
static char current_assign[ASTRA_CAPABILITY_NAME_MAX];
static char current_directory[ASTRA_VFS_PATH_MAX];
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

static ProcessVfsClient *clients;
static uint32_t client_count;
static uint32_t client_capacity;
static uint8_t vfs_initialized;
static AstraVfsClient *client_ready(uint32_t slot);

static uint32_t reserve_clients(uint32_t minimum)
{
    ProcessVfsClient *grown;
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
    clients = grown;
    client_capacity = capacity;
    return ASTRA_VFS_OK;
}

#define PROCESS_VFS_EXEC_MAGIC 0x56465345u
#define PROCESS_VFS_EXEC_VERSION 4u

typedef struct ProcessVfsExecHeader {
    uint32_t magic;
    uint32_t size;
    uint32_t client_count;
    uint32_t version;
} ProcessVfsExecHeader;

typedef struct ProcessVfsExecClient {
    uint32_t service;
    uint32_t session;
    uint32_t activity;
    uint32_t port_service;
    AstraVfsPortExecLane lane;
    uint32_t port_direct_area;
    uint32_t port_direct_device;
    uint32_t port_direct_session;
    uint16_t version;
    uint8_t port_area_capable;
    uint8_t reserved;
} ProcessVfsExecClient;

void astra_process_vfs_close(void)
{
    while (client_count != 0u) {
        --client_count;
        if (clients[client_count].connected != 0u)
            (void)astra_vfs_disconnect(&clients[client_count].client);
        clients[client_count].handle = 0u;
        clients[client_count].connected = 0u;
    }
    vfs_initialized = 0u;
    current_assign[0] = '\0';
    current_directory[0] = '\0';
}

uint32_t astra_process_vfs_state_size(void)
{
    uint32_t connected = 0u;

    for (uint32_t slot = 0u; slot < client_count; ++slot)
        if (clients[slot].connected != 0u)
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
        AstraVfsClient *client = &clients[slot].client;

        if (clients[slot].connected == 0u)
            continue;
        output[count].service = clients[slot].handle;
        output[count].session = client->session;
        output[count].activity = client->activity;
        output[count].port_service = client->port_service;
        if (astra_vfs_port_exec_lane_export(client, &output[count].lane) !=
            ASTRA_VFS_OK)
            return ASTRA_VFS_ERR_IO;
        output[count].port_direct_area = client->port_direct_area;
        output[count].port_direct_device = client->port_direct_device;
        output[count].port_direct_session = client->port_direct_session;
        output[count].version = client->version;
        output[count].port_area_capable = client->port_area_capable;
        ++count;
    }
    header->client_count = count;
    return ASTRA_VFS_OK;
}

uint32_t astra_process_vfs_client_handle(const AstraVfsClient *client)
{
    for (uint32_t slot = 0u; slot < client_count; ++slot)
        if (&clients[slot].client == client)
            return clients[slot].handle;
    return 0u;
}

AstraVfsClient *astra_process_vfs_client_handle_lookup(uint32_t handle)
{
    for (uint32_t slot = 0u; slot < client_count; ++slot)
        if (clients[slot].handle == handle)
            return client_ready(slot);
    return NULL;
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
    file->_private_read_at = astra_vfs_port_read_bulk;
    file->_private_write_at = astra_vfs_port_write_bulk_position;
    file->_private_file = state->file;
    file->_private_flags = state->flags;
    file->_private_offset = state->offset;
    file->_private_size = state->size;
    file->_private_kind = state->kind;
    file->_private_member = state->member;
    return ASTRA_VFS_OK;
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
        header->size != size ||
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
            if (clients[slot].handle == input[index].service)
                break;
        if (slot == client_count || input[index].port_service !=
                                    input[index].service ||
            ((input[index].port_direct_area == 0u) !=
                 (input[index].port_direct_device == 0u)) ||
            ((input[index].port_direct_area == 0u) !=
                 (input[index].port_direct_session ==
                  ASTRA_VFS_SESSION_INVALID)))
            goto invalid;
        client = &clients[slot].client;
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
        client->port_area_capable = input[index].port_area_capable;
        clients[slot].connected = 1u;
        status = astra_vfs_port_exec_lane_import(client, &input[index].lane);
        if (status != ASTRA_VFS_OK) {
            failure_operation = "VFS exec lane restore";
            failure = status;
            goto invalid;
        }
        if (input[index].port_direct_area != 0u &&
            (status = astra_vfs_host_direct_resume(
                client, input[index].port_direct_area,
                input[index].port_direct_device,
                input[index].port_direct_session)) != ASTRA_VFS_OK) {
            failure_operation = "VFS exec direct restore";
            failure = status;
            goto invalid;
        }
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
    if (clients[slot].connected == 0u) {
        uint32_t status = astra_vfs_host_port_connect_lazy(
            &clients[slot].client, clients[slot].handle);

        if (status != ASTRA_VFS_OK) {
            (void)astra_log_failure("VFS port reconnect", status);
            return NULL;
        }
        clients[slot].connected = 1u;
    }
    return &clients[slot].client;
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
    uint32_t previous_client_count = client_count;
    uint32_t status;

    if (!astra_startup_validate(startup) ||
        startup->capabilities_address == 0u)
        return ASTRA_VFS_ERR_INVALID;
    if (fork_child) {
        for (uint32_t index = 0u; index < client_count; ++index) {
            uint32_t status;

            if (clients[index].connected == 0u)
                continue;
            astra_vfs_port_after_fork_child(&clients[index].client);
            if (clients[index].client.port_direct_address != NULL) {
                status = astra_vfs_host_direct_after_fork(
                    &clients[index].client);
                if (status != ASTRA_VFS_OK)
                    return status;
            } else {
                astra_vfs_port_abandon(&clients[index].client);
                clients[index].connected = 0u;
            }
        }
        /* Fork preserves the namespace and its handles exactly. */
        return vfs_initialized ? ASTRA_VFS_OK : ASTRA_VFS_ERR_NOT_FOUND;
    }
    capabilities = (const AstraStartupCapability *)(uintptr_t)
        startup->capabilities_address;
    astra_process_vfs_close();
    status = astra_assign_seed(&assigns, capabilities,
                               startup->capability_count);
    if (status != ASTRA_VFS_OK)
        return status;
    /*
     * Only records below client_count have ever held state.  Clearing the
     * entire namespace-sized array in a COW child needlessly faults and copies
     * every page that backs it; widening namespace capacity must not make a
     * fork pay for mounts the process never had.
     */
    (void)memset(clients, 0,
                 previous_client_count * (uint32_t)sizeof(*clients));
    for (uint32_t index = 0u; index < assigns.count; ++index) {
        uint32_t slot;

        for (slot = 0u; slot < client_count; ++slot)
            if (clients[slot].handle == assigns.entries[index].handle)
                break;
        if (slot != client_count)
            continue;
        if (client_count == UINT32_MAX ||
            reserve_clients(client_count + 1u) != ASTRA_VFS_OK) {
            astra_process_vfs_close();
            return ASTRA_VFS_ERR_LIMIT;
        }
        clients[client_count] = (ProcessVfsClient){
            .handle = assigns.entries[index].handle,
        };
        ++client_count;
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
    return vfs_initialized ? ASTRA_VFS_OK : seed_process_vfs(startup, 0);
}

uint32_t astra_process_vfs_after_fork_child(
    const AstraStartupInfo *startup)
{
    return seed_process_vfs(startup, 1);
}

AstraAssignTable *astra_process_vfs_assigns(void)
{
    return &assigns;
}

uint32_t astra_process_vfs_set_current_directory(const char *assign,
                                                 const char *path)
{
    char normal[ASTRA_VFS_PATH_MAX];
    const AstraAssign *binding;
    uint32_t status;

    if (!vfs_initialized || assign == NULL || path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (assign[0] == '\0') {
        if (path[0] != '\0')
            return ASTRA_VFS_ERR_INVALID;
        current_assign[0] = '\0';
        current_directory[0] = '\0';
        return ASTRA_VFS_OK;
    }
    binding = astra_assign_lookup(&assigns, assign);
    if (binding == NULL)
        return ASTRA_VFS_ERR_NOT_FOUND;
    status = astra_path_normalise(path, normal, sizeof(normal));
    if (status != ASTRA_VFS_OK)
        return status;
    (void)strcpy(current_assign, binding->name);
    (void)strcpy(current_directory, normal);
    return ASTRA_VFS_OK;
}

AstraVfsClient *astra_process_vfs_client(void)
{
    return client_ready(0u);
}

AstraVfsClient *astra_process_vfs_client_for(const AstraAssign *assign)
{
    if (assign == NULL)
        return astra_process_vfs_client();
    for (uint32_t index = 0u; index < client_count; ++index)
        if (clients[index].handle == assign->handle)
            return client_ready(index);
    if (client_count == UINT32_MAX ||
        reserve_clients(client_count + 1u) != ASTRA_VFS_OK) {
        (void)astra_log_failure("VFS client allocation", ASTRA_VFS_ERR_LIMIT);
        return NULL;
    }
    clients[client_count] = (ProcessVfsClient){
        .handle = assign->handle,
    };
    return client_ready(client_count++);
}

AstraVfsClient *astra_process_vfs_assign_client(const AstraAssign *assign,
                                                void *context)
{
    (void)context;
    return astra_process_vfs_client_for(assign);
}

void astra_process_vfs_set_activity(uint32_t activity)
{
    for (uint32_t index = 0u; index < client_count; ++index)
        clients[index].client.activity = activity;
}

/*
 * A word somebody typed, to a path this process can resolve.
 *
 * `ASSIGN:path` is already absolute and comes back unchanged. A bare name is
 * relative to CWD:, which is where the launcher says the prompt is standing,
 * and to WORK: when nothing granted a CWD: -- a launcher that says nothing
 * about where it is still gets the writable volume rather than a refusal.
 *
 * Every command that takes a path needs exactly this, and each one that grew
 * its own copy grew a slightly different one: `ls` did not qualify at all, so
 * `ls probe` answered INVALID for a directory `mkdir probe` had just made.
 */
uint32_t astra_process_path(const char *typed, char *out, uint32_t capacity)
{
    if (current_assign[0] == '\0')
        return ASTRA_VFS_ERR_NOT_FOUND;
    return astra_path_qualify(current_assign, current_directory, typed, out,
                              capacity);
}
