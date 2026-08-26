/*
 * The storage protocol across a port, against a port model rather than a
 * kernel.
 *
 * The claim being tested is not "the transport works" -- it is that **a caller
 * cannot tell**. So the same operations run twice, once through
 * `astra_vfs_local_transport` and once through the port, and the replies are
 * compared field for field. A transport that quietly dropped a cursor or a
 * kind would pass a test that only checked statuses.
 *
 * The port model is the streams one: handles move when attached to a message,
 * because that is the semantic that has already caught one real bug here.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <astra/runtime.h>
#include <astra/vfs_backend.h>
#include <astra/vfs_client.h>
#include <astra/vfs_local_transport.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_service_core.h>

#define MOCK_PORT_MAX (ASTRA_VFS_SESSION_MAX * 4u + 32u)
#define MOCK_QUEUE_MAX 4u

typedef struct MockPort {
    uint8_t  message[MOCK_QUEUE_MAX][ASTRA_MESSAGE_SIZE_MAX];
    uint32_t size[MOCK_QUEUE_MAX];
    uint32_t attached[MOCK_QUEUE_MAX];
    uint32_t sender[MOCK_QUEUE_MAX];
    uint32_t count;
    uint32_t capacity;
    int      open;
    /* Attaching a handle to a message moves it: the sender stops holding it. */
    int      given_away;
    uint32_t target;
    uint32_t rights;
} MockPort;

static MockPort ports[MOCK_PORT_MAX];
static uint32_t next_port = 1u;
static uint32_t mock_activity;
static uint8_t mock_area[ASTRA_VFS_BULK_MAX];
/* Set while the transport is blocked, so the service runs inside its wait. */
static AstraVfsPortService *served;
static int refuse_send;
static uint32_t dead_reply_handle;
static uint32_t mock_empty_receives;
static uint32_t mock_receive_resource_limits;
static uint32_t mock_area_maps;
static uint32_t mock_sender;
static uint8_t backend_written[64];
static uint64_t backend_write_offset;
static uint32_t backend_write_length;

static void
mock_reset(void)
{
    memset(ports, 0, sizeof(ports));
    next_port = 1u;
    served = NULL;
    refuse_send = 0;
    dead_reply_handle = 0u;
    mock_activity = 0u;
    mock_empty_receives = 0u;
    mock_receive_resource_limits = 0u;
    mock_area_maps = 0u;
    mock_sender = 0x10000001u;
    memset(backend_written, 0, sizeof(backend_written));
    backend_write_offset = 0u;
    backend_write_length = 0u;
}

static uint32_t
mock_open(uint32_t capacity)
{
    uint32_t handle = next_port++;

    assert(handle < MOCK_PORT_MAX);
    ports[handle].capacity = capacity > MOCK_QUEUE_MAX ? MOCK_QUEUE_MAX :
                                                         capacity;
    ports[handle].open = 1;
    ports[handle].target = handle;
    ports[handle].rights = UINT32_MAX;
    return handle;
}

uint32_t
astra_rt_handle_duplicate(uint32_t handle, uint32_t rights, uint32_t *duplicate)
{
    uint32_t copy;

    (void)rights;
    if (duplicate == NULL || handle == 0u || handle >= MOCK_PORT_MAX ||
        !ports[handle].open || ports[handle].given_away)
        return ASTRA_SYSCALL_INVALID_HANDLE;
    copy = mock_open(1u);
    ports[copy].target = ports[handle].target;
    ports[copy].rights = rights;
    *duplicate = copy;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_rt_area_create(uint32_t byte_size, uint32_t rights, uint32_t *handle)
{
    (void)rights;
    if (handle == NULL || byte_size > sizeof(mock_area))
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    *handle = mock_open(1u);
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_rt_area_create_flagged(uint32_t byte_size, uint32_t rights,
                             uint32_t flags, uint32_t *handle)
{
    (void)flags;
    return astra_rt_area_create(byte_size, rights, handle);
}

uint32_t
astra_rt_area_map(uint32_t handle, uint32_t permissions, void **address,
               uint32_t *byte_size)
{
    (void)permissions;
    if (address == NULL || byte_size == NULL || handle == 0u ||
        handle >= MOCK_PORT_MAX || !ports[handle].open)
        return ASTRA_SYSCALL_INVALID_HANDLE;
    *address = mock_area;
    *byte_size = sizeof(mock_area);
    ++mock_area_maps;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_rt_area_unmap(void *address)
{
    if (address != mock_area || mock_area_maps == 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    --mock_area_maps;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_rt_port_create(uint32_t message_max, uint32_t byte_max,
                  uint32_t *receive_handle, uint32_t *send_handle)
{
    (void)byte_max;
    if (next_port >= MOCK_PORT_MAX) {
        return ASTRA_SYSCALL_RESOURCE_LIMIT;
    }
    *receive_handle = mock_open(message_max == 0u ? 1u : message_max);
    *send_handle = *receive_handle;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_port_send(uint32_t handle, const void *message, uint32_t size,
                const uint32_t *handles, uint32_t handle_count)
{
    MockPort *port;

    if (refuse_send) {
        return ASTRA_SYSCALL_PEER_DEAD;
    }
    if (handle == 0u || handle >= MOCK_PORT_MAX || !ports[handle].open ||
        ports[handle].given_away) {
        return ASTRA_SYSCALL_INVALID_HANDLE;
    }
    port = &ports[ports[handle].target];
    if (port->count >= port->capacity) {
        return ASTRA_SYSCALL_WOULD_BLOCK;
    }
    assert(size <= ASTRA_MESSAGE_SIZE_MAX);
    memcpy(port->message[port->count], message, size);
    port->size[port->count] = size;
    port->attached[port->count] = handle_count == 1u ? handles[0] : 0u;
    port->sender[port->count] = mock_sender;
    if (handle_count == 1u) {
        ports[handles[0]].given_away = 1;
    }
    ++port->count;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_port_receive_from(uint32_t handle, void *message, uint32_t capacity,
                        uint32_t *handles, uint32_t handle_capacity,
                        uint32_t *size, uint32_t *handle_count,
                        uint32_t *sender)
{
    MockPort *port;
    uint32_t moved;

    *size = 0u;
    if (handle_count != NULL) {
        *handle_count = 0u;
    }
    if (handle == 0u || handle >= MOCK_PORT_MAX || !ports[handle].open) {
        return ASTRA_SYSCALL_INVALID_HANDLE;
    }
    if (mock_receive_resource_limits != 0u) {
        --mock_receive_resource_limits;
        return ASTRA_SYSCALL_RESOURCE_LIMIT;
    }
    port = &ports[ports[handle].target];
    if (port->count == 0u) {
        ++mock_empty_receives;
        return ASTRA_SYSCALL_WOULD_BLOCK;
    }
    if (sender != NULL)
        *sender = port->sender[0];
    moved = port->size[0];
    if (moved > capacity) {
        *size = moved;
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    memcpy(message, port->message[0], moved);
    *size = moved;
    if (port->attached[0] != 0u && handle_capacity >= 1u) {
        handles[0] = port->attached[0];
        ports[handles[0]].given_away = 0;
        if (handle_count != NULL) {
            *handle_count = 1u;
        }
    }
    for (uint32_t index = 1u; index < port->count; ++index) {
        memcpy(port->message[index - 1u], port->message[index],
               port->size[index]);
        port->size[index - 1u] = port->size[index];
        port->attached[index - 1u] = port->attached[index];
        port->sender[index - 1u] = port->sender[index];
    }
    --port->count;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_port_receive(uint32_t handle, void *message, uint32_t capacity,
                   uint32_t *handles, uint32_t handle_capacity, uint32_t *size,
                   uint32_t *handle_count)
{
    return astra_port_receive_from(handle, message, capacity, handles,
                                   handle_capacity, size, handle_count, NULL);
}

uint32_t
astra_close(uint32_t handle)
{
    (void)handle;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_yield(void)
{
    return ASTRA_SYSCALL_OK;
}

/* Monotonic enough for a deadline the test never means to reach. */
static uint64_t mock_now;

uint64_t
astra_clock_monotonic(void)
{
    mock_now += 1000u;
    return mock_now;
}

uint32_t
astra_activity_current(void)
{
    return mock_activity;
}

uint32_t
astra_activity_adopt(uint32_t activity)
{
    mock_activity = activity;
    return mock_activity;
}

uint32_t
astra_activity_exchange(uint32_t activity, uint32_t *previous)
{
    if (previous != NULL)
        *previous = mock_activity;
    mock_activity = activity;
    return mock_activity;
}

/*
 * The service runs inside the client's wait. Not a convenience: on the machine
 * the request and the reply are two processes, and here they are two halves of
 * one thread, so the wait is the only place the other half can run. It is also
 * exactly the shape the supervisor has -- its pump runs while a child blocks.
 */
uint32_t
astra_wait_one(uint32_t handle, uint64_t deadline_ns, uint32_t *detail)
{
    if (detail != NULL) {
        *detail = 0u;
    }
    assert(handle != 0u && handle < MOCK_PORT_MAX);
    if ((ports[handle].rights & ASTRA_RIGHT_WAIT) == 0u)
        return ASTRA_SYSCALL_ACCESS_DENIED;
    if (deadline_ns == 0u)
        return handle == dead_reply_handle ? ASTRA_SYSCALL_PEER_DEAD :
                                             ASTRA_SYSCALL_OK;
    if (served != NULL) {
        (void)astra_vfs_port_service_pump(served, 4u);
    }
    assert(ports[handle].count != 0u &&
           "a wait nothing can wake: the reply was never sent");
    return ASTRA_SYSCALL_OK;
}

/*
 * A backend that answers without a filesystem. What matters is that it gives
 * distinct values in every reply field, so a transport that lost one is caught
 * rather than passing because everything happened to be zero.
 */
static uint32_t backend_activity;

static uint32_t
backend_open(void *context, const char *path, uint32_t flags,
             uint16_t create_mode, uintptr_t *node, AstraVfsNodeInfo *info)
{
    (void)context;
    (void)flags;
    (void)create_mode;
    if (path == NULL || path[0] == '\0') {
        return ASTRA_VFS_ERR_INVALID;
    }
    /* Recorded so a test can ask what the service's activity was mid-call. */
    backend_activity = astra_activity_current();
    *node = 0x1234u;
    info->size = strcmp(path, "/small") == 0 ? 8u : 0xAABBCCDDu;
    info->kind = ASTRA_VFS_KIND_FILE;
    return ASTRA_VFS_OK;
}

static uint32_t
backend_close(void *context, uintptr_t node)
{
    (void)context;
    (void)node;
    return ASTRA_VFS_OK;
}

static uint32_t
backend_read(void *context, uintptr_t node, uint64_t offset, void *buffer,
             uint32_t length, uint32_t *moved)
{
    uint8_t *out = buffer;

    (void)context;
    (void)node;
    backend_activity = astra_activity_current();
    *moved = length < 8u ? length : 8u;
    for (uint32_t index = 0u; index < *moved; ++index) {
        out[index] = (uint8_t)(offset + index);
    }
    return ASTRA_VFS_OK;
}

/*
 * The core refuses a backend with a hole in it, so the operations this test
 * does not exercise still have to be there. They refuse rather than pretend:
 * a stub that returned OK would make a transport that sent the wrong operation
 * look like one that worked.
 */
static uint32_t
backend_write(void *context, uintptr_t node, uint64_t offset,
              uint32_t flags, const void *buffer, uint32_t length,
              uint32_t *moved, uint64_t *position)
{
    (void)context;
    (void)node;
    (void)flags;
    if (length > sizeof(backend_written))
        return ASTRA_VFS_ERR_LIMIT;
    memcpy(backend_written, buffer, length);
    backend_write_offset = offset;
    backend_write_length = length;
    *moved = length;
    *position = offset + length;
    return ASTRA_VFS_OK;
}

static uint32_t backend_sync(void *context, uintptr_t node)
{
    (void)context;
    (void)node;
    return ASTRA_VFS_OK;
}

static uint32_t backend_truncate(void *context, uintptr_t node, uint64_t size)
{
    (void)context;
    (void)node;
    (void)size;
    return ASTRA_VFS_OK;
}

static uint32_t
backend_stat(void *context, const char *path, AstraVfsNodeInfo *info)
{
    (void)context;
    (void)path;
    (void)info;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

static uint32_t
backend_readdir(void *context, const char *path, uint64_t cookie, char *name,
                uint32_t name_capacity, AstraVfsNodeInfo *info,
                uint64_t *next)
{
    (void)context;
    if (strcmp(path, "/dir") != 0 || cookie >= 12u)
        return ASTRA_VFS_ERR_NOT_FOUND;
    if (snprintf(name, name_capacity, "entry%02lu",
                 (unsigned long)cookie) >= (int)name_capacity)
        return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    memset(info, 0, sizeof(*info));
    info->size = 100u + cookie;
    info->mtime = 1000 + (int64_t)cookie;
    info->uid = 10u;
    info->gid = 20u;
    info->kind = ASTRA_VFS_KIND_FILE;
    info->mode = 0100644u;
    info->nlink = 1u;
    *next = cookie + 1u;
    return ASTRA_VFS_OK;
}

static uint32_t
backend_mkdir(void *context, const char *path, uint16_t create_mode)
{
    (void)context;
    (void)path;
    (void)create_mode;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

static uint32_t
backend_unlink(void *context, const char *path)
{
    (void)context;
    (void)path;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

static uint32_t
backend_rename(void *context, const char *from, const char *to)
{
    (void)context;
    (void)from;
    (void)to;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

static uint32_t
backend_chmod(void *context, const char *path, uint16_t mode)
{
    (void)context;
    (void)path;
    (void)mode;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

static uint32_t
backend_readlink(void *context, const char *path, void *buffer,
                 uint32_t capacity, uint32_t *length)
{
    (void)context;
    (void)path;
    (void)buffer;
    (void)capacity;
    (void)length;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

static const AstraVfsBackendOps backend_ops = {
    .open = backend_open,
    .close = backend_close,
    .read = backend_read,
    .write = backend_write,
    .sync = backend_sync,
    .truncate = backend_truncate,
    .stat = backend_stat,
    .readdir = backend_readdir,
    .mkdir = backend_mkdir,
    .unlink = backend_unlink,
    .rename = backend_rename,
    .chmod = backend_chmod,
    .readlink = backend_readlink,
};

static AstraVfsService service;
static AstraVfsOpenFile service_files[128];

static void
service_start(void)
{
    assert(astra_vfs_service_init(
        &service, &backend_ops, NULL, service_files,
        (uint32_t)(sizeof(service_files) / sizeof(service_files[0]))));
}

/*
 * Two clients over one service: one local, one across a port. Every assertion
 * below is that they agree.
 */
static void
test_a_request_crosses_and_the_reply_is_the_same(void)
{
    AstraVfsPortService host;
    AstraVfsClient local;
    AstraVfsClient remote;
    AstraVfsPortService *saved;
    uint32_t service_handle;
    AstraVfsFile local_file = ASTRA_VFS_FILE_INVALID;
    AstraVfsFile remote_file = ASTRA_VFS_FILE_INVALID;
    uint64_t local_size = 0u;
    uint64_t remote_size = 0u;
    uint16_t local_kind = 0u;
    uint16_t remote_kind = 0u;
    uint8_t local_bytes[16];
    uint8_t remote_bytes[16];
    uint32_t local_moved = 0u;
    uint32_t remote_moved = 0u;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    saved = &host;

    assert(astra_vfs_connect(&local, astra_vfs_local_transport, &service) ==
           ASTRA_VFS_OK);
    /* The handshake itself crosses the port, which is the first thing to work. */
    served = saved;
    assert(astra_vfs_port_connect(&remote, service_handle) == ASTRA_VFS_OK);
    assert(remote.version == local.version);
    /* Two clients, so two sessions: the service numbers them independently. */
    assert(remote.session != ASTRA_VFS_SESSION_INVALID);

    assert(astra_vfs_open(&local, "/a", ASTRA_VFS_OPEN_READ, &local_file,
                          &local_size, &local_kind) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&remote, "/a", ASTRA_VFS_OPEN_READ, &remote_file,
                          &remote_size, &remote_kind) == ASTRA_VFS_OK);
    assert(remote_size == local_size);
    assert(remote_kind == local_kind);
    assert(remote_size == 0xAABBCCDDu);

    assert(astra_vfs_read(&local, local_file, 5u, local_bytes,
                          sizeof(local_bytes), &local_moved) == ASTRA_VFS_OK);
    assert(astra_vfs_read(&remote, remote_file, 5u, remote_bytes,
                          sizeof(remote_bytes), &remote_moved) ==
           ASTRA_VFS_OK);
    assert(remote_moved == local_moved && remote_moved == 8u);
    assert(memcmp(remote_bytes, local_bytes, remote_moved) == 0);

    /* A refusal crosses unchanged too, which is the half that is easy to lose. */
    assert(astra_vfs_open(&local, "", ASTRA_VFS_OPEN_READ, &local_file, NULL,
                          NULL) == ASTRA_VFS_ERR_INVALID);
    assert(astra_vfs_open(&remote, "", ASTRA_VFS_OPEN_READ, &remote_file, NULL,
                          NULL) == ASTRA_VFS_ERR_INVALID);

    /* One reply port was created at HELLO and reused by every remote call. */
    assert(next_port == 4u);
    assert(host.reply_sessions[0] == remote.session);
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    assert(host.reply_sessions[0] == ASTRA_VFS_SESSION_INVALID);

    assert(host.requests >= 4u);
    assert(host.refused == 0u && host.dropped == 0u);
    /* Only the service pump probes its input empty; clients wait for replies. */
    assert(mock_empty_receives == host.requests);
    served = NULL;
}

static void
test_a_dead_peer_is_reported_and_not_waited_on(void)
{
    AstraVfsClient remote;
    uint32_t service_handle;
    AstraVfsPortService host;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    served = &host;
    assert(astra_vfs_port_connect(&remote, service_handle) == ASTRA_VFS_OK);

    /*
     * The service goes. A caller must be told there is nobody there rather
     * than waiting for an answer nothing will send -- which is the failure
     * this whole return value exists to make impossible.
     */
    refuse_send = 1;
    assert(astra_vfs_open(&remote, "/a", ASTRA_VFS_OPEN_READ, &file, NULL,
                          NULL) == ASTRA_VFS_ERR_PEER);
    refuse_send = 0;

    /*
     * A handle that names nothing is **not** the same fact, and used to be
     * reported as though it were.
     *
     * A service that died is a thing to give up on; a handle this caller never
     * held is this caller's own mistake, and it is fixed somewhere completely
     * different. Collapsing both into "peer dead" is what made a wrong handle
     * look like a missing service and cost an afternoon finding out which, so
     * the two are asserted apart here rather than together.
     */
    {
        uint32_t nothing = MOCK_PORT_MAX - 1u;
        AstraVfsClient orphan;

        assert(astra_vfs_port_connect(&orphan, nothing) ==
               ASTRA_VFS_ERR_BAD_HANDLE);
    }
    served = NULL;
}

static void
test_clients_own_independent_reply_channels(void)
{
    AstraVfsPortService host;
    AstraVfsClient first;
    AstraVfsClient second;
    uint32_t service_handle;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    served = &host;
    assert(astra_vfs_port_connect(&first, service_handle) == ASTRA_VFS_OK);
    assert(astra_vfs_port_connect(&second, service_handle) == ASTRA_VFS_OK);
    assert(first.port_reply_receive != second.port_reply_receive);
    assert(first.port_reply_source != second.port_reply_source);
    assert(astra_vfs_disconnect(&first) == ASTRA_VFS_OK);
    assert(astra_vfs_disconnect(&second) == ASTRA_VFS_OK);
    served = NULL;
}

static void
test_first_operation_shares_the_hello_round_trip(void)
{
    AstraVfsPortService host;
    AstraVfsClient remote;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    uint32_t service_handle;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    served = &host;
    assert(astra_vfs_port_connect_lazy(&remote, service_handle) ==
           ASTRA_VFS_OK);
    assert(remote.session == ASTRA_VFS_SESSION_INVALID && host.requests == 0u);
    assert(astra_vfs_open(&remote, "/a", ASTRA_VFS_OPEN_READ, &file, NULL,
                          NULL) == ASTRA_VFS_OK);
    assert(remote.session != ASTRA_VFS_SESSION_INVALID && host.requests == 1u);
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    served = NULL;
}

static uint32_t
legacy_transport(void *context, uint32_t operation,
                 const AstraVfsRequest *request, AstraVfsReply *reply)
{
    AstraVfsClient *client = context;
    AstraVfsRequest legacy = *request;

    legacy.version = UINT16_C(2);
    client->version = UINT16_C(2);
    return astra_vfs_port_transport(client, operation, &legacy, reply);
}

static void
test_version_two_keeps_per_request_reply_ports(void)
{
    AstraVfsPortService host;
    AstraVfsClient remote;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    uint32_t service_handle;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    memset(&remote, 0, sizeof(remote));
    remote.port_service = service_handle;
    served = &host;
    assert(astra_vfs_connect(&remote, legacy_transport, &remote) ==
           ASTRA_VFS_OK);
    assert(remote.version == UINT16_C(2));
    assert(astra_vfs_open(&remote, "/a", ASTRA_VFS_OPEN_READ, &file, NULL,
                          NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_close(&remote, file) == ASTRA_VFS_OK);
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    /* Service + one reply port and one duplicated send end per operation. */
    assert(next_port == 7u);
    for (uint32_t index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index)
        assert(host.reply_sessions[index] == ASTRA_VFS_SESSION_INVALID);
    served = NULL;
}

static void
test_bulk_read_crosses_once_through_a_shared_area(void)
{
    AstraVfsPortService host;
    AstraVfsClient remote;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    uint32_t service_handle;
    uint32_t moved = 0u;
    uint8_t bytes[32];

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    served = &host;
    assert(astra_vfs_port_connect(&remote, service_handle) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&remote, "/a", ASTRA_VFS_OPEN_READ, &file, NULL,
                          NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_port_read_bulk(&remote, file, 5u, bytes, sizeof(bytes),
                                    &moved) == ASTRA_VFS_OK);
    assert(moved == 8u);
    for (uint32_t index = 0u; index < moved; ++index)
        assert(bytes[index] == (uint8_t)(5u + index));
    assert(host.area_addresses[0] == mock_area);
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    assert(remote.port_area == 0u);
    assert(remote.port_area_address == NULL);
    assert(mock_area_maps == 0u);
    served = NULL;
}

static void
test_bulk_write_crosses_once_through_a_shared_area(void)
{
    AstraVfsPortService host;
    AstraVfsClient remote;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    uint32_t service_handle;
    uint32_t before;
    uint32_t moved = 0u;
    const uint8_t first[] = {1u, 2u, 3u, 4u};
    const uint8_t second[] = {5u, 6u, 7u};

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    served = &host;
    assert(astra_vfs_port_connect(&remote, service_handle) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&remote, "/a", ASTRA_VFS_OPEN_WRITE, &file, NULL,
                          NULL) == ASTRA_VFS_OK);

    before = host.requests;
    assert(astra_vfs_port_write_bulk(&remote, file, 5u, first,
                                     sizeof(first), &moved) == ASTRA_VFS_OK);
    assert(moved == sizeof(first) && host.requests == before + 2u);
    assert(backend_write_offset == 5u &&
           backend_write_length == sizeof(first));
    assert(memcmp(backend_written, first, sizeof(first)) == 0);

    before = host.requests;
    assert(astra_vfs_port_write_bulk(&remote, file, 9u, second,
                                     sizeof(second), &moved) == ASTRA_VFS_OK);
    assert(moved == sizeof(second) && host.requests == before + 1u);
    assert(backend_write_offset == 9u &&
           backend_write_length == sizeof(second));
    assert(memcmp(backend_written, second, sizeof(second)) == 0);
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    assert(mock_area_maps == 0u);
    served = NULL;
}

static void
test_bulk_readdir_crosses_once_through_a_shared_area(void)
{
    AstraVfsPortService host;
    AstraVfsClient remote;
    AstraVfsDirEntry entries[16];
    uint32_t service_handle;
    uint32_t before;
    uint32_t count = 0u;
    uint64_t next = 0u;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    served = &host;
    assert(astra_vfs_port_connect(&remote, service_handle) == ASTRA_VFS_OK);

    before = host.requests;
    assert(astra_vfs_readdir_batch(&remote, "/dir", 0u, entries, 16u,
                                   &count, &next) == ASTRA_VFS_OK);
    assert(count == 12u && next == 0u && host.requests == before + 2u);
    assert(strcmp(entries[0].name, "entry00") == 0);
    assert(strcmp(entries[11].name, "entry11") == 0);
    assert(entries[11].size == 111u && entries[11].mode == 0100644u);
    assert(host.area_addresses[0] == mock_area);

    before = host.requests;
    assert(astra_vfs_readdir_batch(&remote, "/dir", 0u, entries, 16u,
                                   &count, &next) == ASTRA_VFS_OK);
    assert(count == 12u && next == 0u && host.requests == before + 1u);
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    assert(mock_area_maps == 0u);
    served = NULL;
}

static void
test_small_path_read_needs_no_shared_area(void)
{
    AstraVfsPortService host;
    AstraVfsClient remote;
    const uint8_t *bytes = NULL;
    uint32_t service_handle;
    uint32_t moved = 0u;
    uint64_t size = 0u;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    served = &host;
    assert(astra_vfs_port_connect(&remote, service_handle) == ASTRA_VFS_OK);
    assert(astra_vfs_port_read_path_inline(&remote, "/small", &bytes, &moved,
                                           &size) == ASTRA_VFS_OK);
    assert(moved == 8u && size == 8u && bytes != NULL);
    for (uint32_t index = 0u; index < moved; ++index)
        assert(bytes[index] == (uint8_t)index);
    assert(remote.port_area == 0u && mock_area_maps == 0u);
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    served = NULL;
}

static void
test_bulk_resources_are_reusable_after_disconnect(void)
{
    AstraVfsPortService host;
    uint32_t service_handle;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    served = &host;

    for (uint32_t cycle = 0u; cycle <= ASTRA_VFS_SESSION_MAX; ++cycle) {
        AstraVfsClient remote;
        AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
        uint8_t bytes[16];
        uint32_t moved = 0u;

        assert(astra_vfs_port_connect(&remote, service_handle) ==
               ASTRA_VFS_OK);
        assert(astra_vfs_open(&remote, "/a", ASTRA_VFS_OPEN_READ, &file,
                              NULL, NULL) == ASTRA_VFS_OK);
        assert(astra_vfs_port_read_bulk(&remote, file, 0u, bytes,
                                        sizeof(bytes), &moved) ==
               ASTRA_VFS_OK);
        assert(moved == 8u);
        assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
        assert(remote.port_area == 0u);
        assert(remote.port_area_address == NULL);
        assert(mock_area_maps == 0u);
    }
    served = NULL;
}

static void
test_a_dead_client_is_reaped_when_the_session_table_is_full(void)
{
    AstraVfsPortService host;
    AstraVfsRequestMessage hello;
    uint32_t service_handle;
    uint32_t replies[ASTRA_VFS_SESSION_MAX + 1u];

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    memset(&hello, 0, sizeof(hello));
    hello.header.total_size = sizeof(hello);
    hello.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    hello.header.protocol = ASTRA_VFS_PROTOCOL;
    hello.header.protocol_version = ASTRA_VFS_VERSION;
    hello.header.operation = ASTRA_VFS_OP_HELLO;
    hello.request.size = ASTRA_VFS_REQUEST_SIZE;
    hello.request.version = ASTRA_VFS_VERSION;

    for (uint32_t index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index) {
        uint32_t handles[1];

        mock_sender = index + 1u;
        replies[index] = mock_open(1u);
        handles[0] = replies[index];
        assert(astra_port_send(service_handle, &hello, sizeof(hello),
                               handles, 1u) == ASTRA_SYSCALL_OK);
        assert(astra_vfs_port_service_pump(&host, 1u) == 1u);
    }
    assert(service.open_sessions == ASTRA_VFS_SESSION_MAX);
    dead_reply_handle = replies[0];
    mock_sender = ASTRA_VFS_SESSION_MAX + 1u;
    replies[ASTRA_VFS_SESSION_MAX] = mock_open(1u);
    {
        uint32_t handles[1] = {replies[ASTRA_VFS_SESSION_MAX]};

        assert(astra_port_send(service_handle, &hello, sizeof(hello),
                               handles, 1u) == ASTRA_SYSCALL_OK);
    }
    assert(astra_vfs_port_service_pump(&host, 1u) == 1u);
    assert(service.open_sessions == ASTRA_VFS_SESSION_MAX);
    assert(service.stats.sessions_opened == ASTRA_VFS_SESSION_MAX + 1u);
    assert(service.stats.sessions_closed == 1u);
    assert(host.reply_sessions[0] != ASTRA_VFS_SESSION_INVALID);
}

static void
test_handle_pressure_reaps_a_dead_client_and_retries_receive(void)
{
    AstraVfsPortService host;
    AstraVfsRequestMessage hello;
    uint32_t service_handle;
    uint32_t reply_handle;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    memset(&hello, 0, sizeof(hello));
    hello.header.total_size = sizeof(hello);
    hello.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    hello.header.protocol = ASTRA_VFS_PROTOCOL;
    hello.header.protocol_version = ASTRA_VFS_VERSION;
    hello.header.operation = ASTRA_VFS_OP_HELLO;
    hello.request.size = ASTRA_VFS_REQUEST_SIZE;
    hello.request.version = ASTRA_VFS_VERSION;

    reply_handle = mock_open(1u);
    {
        uint32_t handles[1] = {reply_handle};

        assert(astra_port_send(service_handle, &hello, sizeof(hello),
                               handles, 1u) == ASTRA_SYSCALL_OK);
    }
    assert(astra_vfs_port_service_pump(&host, 1u) == 1u);
    dead_reply_handle = host.reply_handles[0];

    reply_handle = mock_open(1u);
    {
        uint32_t handles[1] = {reply_handle};

        mock_sender = 2u;
        assert(astra_port_send(service_handle, &hello, sizeof(hello),
                               handles, 1u) == ASTRA_SYSCALL_OK);
    }
    mock_receive_resource_limits = 1u;
    assert(astra_vfs_port_service_pump(&host, 1u) == 1u);
    assert(service.stats.sessions_opened == 2u);
    assert(service.stats.sessions_closed == 1u);
    assert(host.stalled == 0u);
}

static void
test_the_service_adopts_the_callers_activity(void)
{
    AstraVfsPortService host;
    AstraVfsClient remote;
    uint32_t service_handle;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    served = &host;
    assert(astra_vfs_port_connect(&remote, service_handle) == ASTRA_VFS_OK);

    /*
     * One request is one story across every process it touches. The caller
     * stamps what it was doing, the service adopts it for the duration, and
     * everything the backend emits underneath belongs to that story -- without
     * a single caller writing correlation code.
     */
    mock_activity = 0u;
    remote.activity = 0x00ABCDEFu;
    backend_activity = 0u;
    assert(astra_vfs_open(&remote, "/a", ASTRA_VFS_OPEN_READ, &file, NULL,
                          NULL) == ASTRA_VFS_OK);
    assert(backend_activity == 0x00ABCDEFu);

    /*
     * And restored afterwards. A service still standing in the last caller's
     * story would attribute its own work, and the next caller's, to whoever
     * happened to call first.
     */
    assert(astra_activity_current() == 0u);

    /* A second caller is a second story, not a continuation of the first. */
    mock_activity = 0x11u;
    remote.activity = 0x22u;
    assert(astra_vfs_open(&remote, "/a", ASTRA_VFS_OPEN_READ, &file, NULL,
                          NULL) == ASTRA_VFS_OK);
    assert(backend_activity == 0x22u);
    assert(astra_activity_current() == 0x11u);

    /* Zero means no caller story. It must not allocate one in the service. */
    mock_activity = 0x33u;
    remote.activity = 0u;
    assert(astra_vfs_open(&remote, "/a", ASTRA_VFS_OPEN_READ, &file, NULL,
                          NULL) == ASTRA_VFS_OK);
    assert(backend_activity == 0x33u);
    assert(astra_activity_current() == 0x33u);
    served = NULL;
}

static void
test_a_message_that_is_not_the_protocol_is_refused(void)
{
    AstraVfsPortService host;
    AstraVfsRequestMessage message;
    uint32_t service_handle;
    uint32_t reply_handle;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    reply_handle = mock_open(1u);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));

    memset(&message, 0, sizeof(message));
    message.header.total_size = (uint32_t)sizeof(message);
    message.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    message.header.protocol = UINT32_C(0x494e5054);   /* INPT */
    message.request.size = ASTRA_VFS_REQUEST_SIZE;
    {
        uint32_t handles[1] = {reply_handle};

        assert(astra_port_send(service_handle, &message, sizeof(message),
                               handles, 1u) == ASTRA_SYSCALL_OK);
    }
    assert(astra_vfs_port_service_pump(&host, 4u) == 0u);
    assert(host.refused == 1u);

    /* A request with nowhere to reply is dropped rather than answered. */
    message.header.protocol = ASTRA_VFS_PROTOCOL;
    assert(astra_port_send(service_handle, &message, sizeof(message), NULL,
                           0u) == ASTRA_SYSCALL_OK);
    assert(astra_vfs_port_service_pump(&host, 4u) == 0u);
    assert(host.refused == 2u);
}

static void
test_an_answer_with_nowhere_to_go_is_counted(void)
{
    AstraVfsPortService host;
    AstraVfsRequestMessage message;
    uint32_t service_handle;
    uint32_t reply_handle;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    /* A reply port with no room: the caller is gone as far as this can tell. */
    reply_handle = mock_open(1u);
    ports[reply_handle].capacity = 0u;
    assert(astra_vfs_port_service_init(&host, service_handle, &service));

    memset(&message, 0, sizeof(message));
    message.header.total_size = (uint32_t)sizeof(message);
    message.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    message.header.protocol = ASTRA_VFS_PROTOCOL;
    message.header.protocol_version = ASTRA_VFS_VERSION;
    message.header.operation = ASTRA_VFS_OP_HELLO;
    message.request.size = ASTRA_VFS_REQUEST_SIZE;
    message.request.version = ASTRA_VFS_VERSION;
    message.request.session = ASTRA_VFS_SESSION_INVALID;
    {
        uint32_t handles[1] = {reply_handle};

        assert(astra_port_send(service_handle, &message, sizeof(message),
                               handles, 1u) == ASTRA_SYSCALL_OK);
    }
    /*
     * The work happened and the answer had nowhere to go. Counted rather than
     * retried: there is no second address to try, and a service that spun on
     * this would stop serving everybody else for one caller that had left.
     */
    assert(astra_vfs_port_service_pump(&host, 4u) == 0u);
    assert(host.dropped == 1u);
    assert(host.refused == 0u);
}

int
main(void)
{
    test_a_request_crosses_and_the_reply_is_the_same();
    test_a_dead_peer_is_reported_and_not_waited_on();
    test_clients_own_independent_reply_channels();
    test_first_operation_shares_the_hello_round_trip();
    test_version_two_keeps_per_request_reply_ports();
    test_bulk_read_crosses_once_through_a_shared_area();
    test_bulk_write_crosses_once_through_a_shared_area();
    test_bulk_readdir_crosses_once_through_a_shared_area();
    test_small_path_read_needs_no_shared_area();
    test_bulk_resources_are_reusable_after_disconnect();
    test_a_dead_client_is_reaped_when_the_session_table_is_full();
    test_handle_pressure_reaps_a_dead_client_and_retries_receive();
    test_the_service_adopts_the_callers_activity();
    test_a_message_that_is_not_the_protocol_is_refused();
    test_an_answer_with_nowhere_to_go_is_counted();
    puts("ASTRA VFS PORT PASS");
    return 0;
}
