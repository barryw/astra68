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
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <astra/runtime.h>
#include <astra/vfs_backend.h>
#include <astra/vfs_client.h>
#include <astra/vfs_local_transport.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_service_core.h>

#define MOCK_PORT_MAX (ASTRA_VFS_SESSION_MAX * 5u + 32u)
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
    uint32_t semaphore_count;
    uint8_t semaphore;
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
static uint32_t mock_futex_wakes;
static uint32_t mock_sender;
static uint8_t backend_written[64];
static uint64_t backend_write_offset;
static uint32_t backend_write_length;
static uint32_t backend_write_flags;
static char backend_rename_from[ASTRA_VFS_PATH_MAX];
static char backend_rename_to[ASTRA_VFS_PATH_MAX];
static uint32_t direct_connects;
static uint32_t direct_disconnects;
static uint32_t direct_operations;
static uint32_t direct_last_operation;
static uint32_t direct_bulk_operations;
static void *direct_bulk_buffer;
static uint32_t port_lane_lookups;
static pthread_mutex_t direct_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t direct_changed = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t mock_port_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mock_pump_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mock_wait_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t mock_wait_changed = PTHREAD_COND_INITIALIZER;
static int direct_hold;
static uint32_t direct_entered;
static int mock_hold_waits;
static uint32_t mock_blocked_waits;
static pthread_mutex_t port_threads_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t port_threads_changed = PTHREAD_COND_INITIALIZER;
static uint32_t port_threads_ready;
static int port_threads_go;
static atomic_uint mock_next_thread = 1u;
static _Thread_local uint32_t mock_thread;

void
astra_assert_failed(const char *file, unsigned int line,
                    const char *expression)
{
    fprintf(stderr, "%s:%u: %s\n", file, line, expression);
    abort();
}

uint32_t
astra_query_abi(uint32_t *abi_version, uint32_t *process_handle,
                uint32_t *thread_handle)
{
    if (mock_thread == 0u)
        mock_thread = UINT32_C(0x80000000) |
                      atomic_fetch_add(&mock_next_thread, 1u);
    if (abi_version != NULL)
        *abi_version = 1u;
    if (process_handle != NULL)
        *process_handle = 1u;
    if (thread_handle != NULL)
        *thread_handle = mock_thread;
    return ASTRA_SYSCALL_OK;
}

static AstraVfsPortLane *
current_lane(AstraVfsClient *client)
{
    uint32_t thread = 0u;

    assert(astra_query_abi(NULL, NULL, &thread) == ASTRA_SYSCALL_OK);
    for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_COUNT_MAX;
         ++index)
        if (client->port_lanes[index].owner_thread == thread)
            return &client->port_lanes[index];
    return NULL;
}

static int
lanes_have_no_areas(const AstraVfsClient *client)
{
    for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_COUNT_MAX;
         ++index)
        if (client->port_lanes[index].area != 0u ||
            client->port_lanes[index].area_address != NULL)
            return 0;
    return 1;
}

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
    mock_futex_wakes = 0u;
    mock_sender = 0x10000001u;
    memset(backend_written, 0, sizeof(backend_written));
    backend_write_offset = 0u;
    backend_write_length = 0u;
    backend_write_flags = 0u;
    backend_rename_from[0] = '\0';
    backend_rename_to[0] = '\0';
    direct_connects = 0u;
    direct_disconnects = 0u;
    direct_operations = 0u;
    direct_last_operation = 0u;
    direct_bulk_operations = 0u;
    direct_bulk_buffer = NULL;
    port_lane_lookups = 0u;
    direct_hold = 0;
    direct_entered = 0u;
    mock_hold_waits = 0;
    mock_blocked_waits = 0u;
    port_threads_ready = 0u;
    port_threads_go = 0;
}

void
astra_vfs_test_port_lane_lookup(AstraVfsClient *client)
{
    assert(client != NULL);
    (void)__atomic_add_fetch(&port_lane_lookups, 1u, __ATOMIC_RELAXED);
}

uint32_t astra_vfs_host_direct_connect(AstraVfsClient *client,
                                       uint32_t device)
{
    assert(client != NULL && device != 0u);
    ++direct_connects;
    client->port_direct_address = (void *)(uintptr_t)1u;
    client->port_direct_device = device;
    client->port_direct_session = 1u;
    return ASTRA_VFS_OK;
}

void astra_vfs_host_direct_disconnect(AstraVfsClient *client)
{
    if (client != NULL && client->port_direct_address != NULL) {
        ++direct_disconnects;
        client->port_direct_address = NULL;
        client->port_direct_device = 0u;
        client->port_direct_session = 0u;
    }
}

void astra_vfs_host_direct_abandon(AstraVfsClient *client)
{
    astra_vfs_host_direct_disconnect(client);
}

uint32_t astra_vfs_host_direct_transport(
    AstraVfsClient *client, uint32_t operation,
    const AstraVfsRequest *request, AstraVfsReply *reply)
{
    assert(client != NULL && request != NULL && reply != NULL);
    assert(client->port_direct_address != NULL);
    ++direct_operations;
    direct_last_operation = operation;
    memset(reply, 0, sizeof(*reply));
    reply->size = ASTRA_VFS_REPLY_SIZE;
    reply->version = client->version;
    reply->session = client->session;
    reply->status = ASTRA_VFS_OK;
    if (operation == ASTRA_VFS_OP_OPEN) {
        reply->file = 0x12340001u;
        reply->kind = ASTRA_VFS_KIND_FILE;
    }
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_host_direct_bulk(
    AstraVfsClient *client, uint32_t operation,
    const AstraVfsRequest *request, void *buffer, uint32_t capacity,
    AstraVfsReply *reply)
{
    assert(client != NULL && request != NULL && buffer != NULL &&
           reply != NULL && request->length <= capacity);
    assert(pthread_mutex_lock(&direct_mutex) == 0);
    ++direct_bulk_operations;
    direct_last_operation = operation;
    direct_bulk_buffer = buffer;
    if (direct_hold != 0) {
        ++direct_entered;
        assert(pthread_cond_broadcast(&direct_changed) == 0);
        while (direct_hold != 0)
            assert(pthread_cond_wait(&direct_changed, &direct_mutex) == 0);
    }
    assert(pthread_mutex_unlock(&direct_mutex) == 0);
    memset(reply, 0, sizeof(*reply));
    reply->size = ASTRA_VFS_REPLY_SIZE;
    reply->version = client->version;
    reply->session = client->session;
    reply->status = ASTRA_VFS_OK;
    reply->count = request->length;
    reply->node_size =
        (request->flags & ASTRA_VFS_OPEN_APPEND) != 0u ?
            700u + request->length : request->offset + request->length;
    if (operation == ASTRA_VFS_OP_READ_AREA)
        memset(buffer, 0x5au, request->length);
    return ASTRA_VFS_OK;
}

typedef struct DirectReadCall {
    AstraVfsClient *client;
    AstraVfsFile file;
    uint8_t bytes[8];
    uint32_t moved;
    uint32_t status;
} DirectReadCall;

typedef struct DisconnectCall {
    AstraVfsClient *client;
    atomic_int done;
    uint32_t status;
} DisconnectCall;

typedef struct PortThreadCall {
    AstraVfsClient *client;
    uint32_t warm_status;
    uint32_t status;
} PortThreadCall;

static void *
port_stat_thread(void *context)
{
    PortThreadCall *call = context;
    uint64_t size = 0u;

    call->warm_status = astra_vfs_stat(call->client, "/a", &size, NULL);
    assert(pthread_mutex_lock(&port_threads_mutex) == 0);
    ++port_threads_ready;
    assert(pthread_cond_broadcast(&port_threads_changed) == 0);
    while (port_threads_go == 0)
        assert(pthread_cond_wait(&port_threads_changed,
                                 &port_threads_mutex) == 0);
    assert(pthread_mutex_unlock(&port_threads_mutex) == 0);
    call->status = astra_vfs_stat(call->client, "/a", &size, NULL);
    return NULL;
}

static void *
port_one_stat_thread(void *context)
{
    PortThreadCall *call = context;
    uint64_t size = 0u;

    call->status = astra_vfs_stat(call->client, "/a", &size, NULL);
    return NULL;
}

static void *
direct_read_thread(void *context)
{
    DirectReadCall *call = context;

    call->status = astra_vfs_port_read_bulk(
        call->client, call->file, 0u, call->bytes, sizeof(call->bytes),
        &call->moved);
    return NULL;
}

static void *
disconnect_thread(void *context)
{
    DisconnectCall *call = context;

    call->status = astra_vfs_disconnect(call->client);
    atomic_store(&call->done, 1);
    return NULL;
}

static const AstraVfsPortAcceleratorOps mock_accelerator_ops = {
    astra_vfs_host_direct_connect,
    astra_vfs_host_direct_disconnect,
    astra_vfs_host_direct_abandon,
    astra_vfs_host_direct_transport,
    astra_vfs_host_direct_bulk
};

static uint32_t
mock_open_unlocked(uint32_t capacity)
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

static uint32_t
mock_open(uint32_t capacity)
{
    uint32_t handle;

    assert(pthread_mutex_lock(&mock_port_mutex) == 0);
    handle = mock_open_unlocked(capacity);
    assert(pthread_mutex_unlock(&mock_port_mutex) == 0);
    return handle;
}

uint32_t
astra_rt_handle_duplicate(uint32_t handle, uint32_t rights, uint32_t *duplicate)
{
    uint32_t copy;
    uint32_t status = ASTRA_SYSCALL_OK;

    (void)rights;
    assert(pthread_mutex_lock(&mock_port_mutex) == 0);
    if (duplicate == NULL || handle == 0u || handle >= MOCK_PORT_MAX ||
        !ports[handle].open || ports[handle].given_away) {
        status = ASTRA_SYSCALL_INVALID_HANDLE;
    } else {
        copy = mock_open_unlocked(1u);
        ports[copy].target = ports[handle].target;
        ports[copy].rights = rights;
        *duplicate = copy;
    }
    assert(pthread_mutex_unlock(&mock_port_mutex) == 0);
    return status;
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
    assert(pthread_mutex_lock(&mock_port_mutex) == 0);
    if (next_port >= MOCK_PORT_MAX) {
        assert(pthread_mutex_unlock(&mock_port_mutex) == 0);
        return ASTRA_SYSCALL_RESOURCE_LIMIT;
    }
    *receive_handle = mock_open_unlocked(message_max == 0u ? 1u :
                                                             message_max);
    *send_handle = *receive_handle;
    assert(pthread_mutex_unlock(&mock_port_mutex) == 0);
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_rt_semaphore_create(uint32_t initial, uint32_t maximum,
                                   uint32_t rights, uint32_t *handle)
{
    (void)maximum;
    assert(pthread_mutex_lock(&mock_port_mutex) == 0);
    if (handle == NULL || next_port >= MOCK_PORT_MAX) {
        assert(pthread_mutex_unlock(&mock_port_mutex) == 0);
        return ASTRA_SYSCALL_RESOURCE_LIMIT;
    }
    *handle = mock_open_unlocked(1u);
    ports[*handle].rights = rights;
    ports[*handle].semaphore = 1u;
    ports[*handle].semaphore_count = initial;
    assert(pthread_mutex_unlock(&mock_port_mutex) == 0);
    return ASTRA_SYSCALL_OK;
}

static uint32_t
mock_port_send(uint32_t handle, const void *message, uint32_t size,
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
astra_port_send(uint32_t handle, const void *message, uint32_t size,
                const uint32_t *handles, uint32_t handle_count)
{
    uint32_t status;

    assert(pthread_mutex_lock(&mock_port_mutex) == 0);
    status = mock_port_send(handle, message, size, handles, handle_count);
    assert(pthread_mutex_unlock(&mock_port_mutex) == 0);
    return status;
}

static uint32_t
mock_port_receive_from(uint32_t handle, void *message, uint32_t capacity,
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
astra_port_receive_from(uint32_t handle, void *message, uint32_t capacity,
                        uint32_t *handles, uint32_t handle_capacity,
                        uint32_t *size, uint32_t *handle_count,
                        uint32_t *sender)
{
    uint32_t status;

    assert(pthread_mutex_lock(&mock_port_mutex) == 0);
    status = mock_port_receive_from(handle, message, capacity, handles,
                                    handle_capacity, size, handle_count,
                                    sender);
    assert(pthread_mutex_unlock(&mock_port_mutex) == 0);
    return status;
}

uint32_t
astra_port_receive(uint32_t handle, void *message, uint32_t capacity,
                   uint32_t *handles, uint32_t handle_capacity, uint32_t *size,
                   uint32_t *handle_count)
{
    return astra_port_receive_from(handle, message, capacity, handles,
                                   handle_capacity, size, handle_count, NULL);
}

uint32_t astra_futex_wait(volatile uint32_t *address, uint32_t expected,
                          uint64_t deadline_ns)
{
    (void)address;
    (void)expected;
    (void)deadline_ns;
    return ASTRA_SYSCALL_WOULD_BLOCK;
}

uint32_t astra_futex_wake(volatile uint32_t *address, uint32_t count,
                          uint32_t *woken)
{
    (void)address;
    ++mock_futex_wakes;
    if (woken != NULL)
        *woken = count;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_rt_signal(uint32_t handle, uint32_t count, uint32_t *woken)
{
    if (handle != 0u && handle < MOCK_PORT_MAX &&
        ports[handle].semaphore != 0u)
        ports[handle].semaphore_count += count;
    if (woken != NULL)
        *woken = count;
    return ASTRA_SYSCALL_OK;
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
    if (ports[handle].semaphore != 0u) {
        if (ports[handle].semaphore_count == 0u)
            return ASTRA_SYSCALL_WOULD_BLOCK;
        --ports[handle].semaphore_count;
        return ASTRA_SYSCALL_OK;
    }
    if (deadline_ns == 0u)
        return handle == dead_reply_handle ? ASTRA_SYSCALL_PEER_DEAD :
                                             ASTRA_SYSCALL_OK;
    assert(pthread_mutex_lock(&mock_wait_mutex) == 0);
    if (mock_hold_waits != 0) {
        ++mock_blocked_waits;
        assert(pthread_cond_broadcast(&mock_wait_changed) == 0);
        while (mock_hold_waits != 0)
            assert(pthread_cond_wait(&mock_wait_changed, &mock_wait_mutex) ==
                   0);
    }
    assert(pthread_mutex_unlock(&mock_wait_mutex) == 0);
    if (served != NULL) {
        assert(pthread_mutex_lock(&mock_pump_mutex) == 0);
        (void)astra_vfs_port_service_pump(served, 4u);
        assert(pthread_mutex_unlock(&mock_pump_mutex) == 0);
    }
    assert(pthread_mutex_lock(&mock_port_mutex) == 0);
    assert(ports[handle].count != 0u &&
           "a wait nothing can wake: the reply was never sent");
    assert(pthread_mutex_unlock(&mock_port_mutex) == 0);
    return ASTRA_SYSCALL_OK;
}

/*
 * A backend that answers without a filesystem. What matters is that it gives
 * distinct values in every reply field, so a transport that lost one is caught
 * rather than passing because everything happened to be zero.
 */
static uint32_t backend_activity;
static uintptr_t backend_readdir_directory;

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
    info->kind = (flags & ASTRA_VFS_OPEN_DIRECTORY) != 0u
                     ? ASTRA_VFS_KIND_DIRECTORY
                     : ASTRA_VFS_KIND_FILE;
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
    if (length > sizeof(backend_written))
        return ASTRA_VFS_ERR_LIMIT;
    memcpy(backend_written, buffer, length);
    backend_write_offset = offset;
    backend_write_length = length;
    backend_write_flags = flags;
    *moved = length;
    *position = (flags & ASTRA_VFS_OPEN_APPEND) != 0u ?
                    900u + length : offset + length;
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
backend_readdir(void *context, uintptr_t directory, const char *path,
                uint64_t cookie, char *name, uint32_t name_capacity,
                AstraVfsNodeInfo *info, uint64_t *next)
{
    (void)context;
    backend_readdir_directory = directory;
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
    assert(snprintf(backend_rename_from, sizeof(backend_rename_from), "%s",
                    from) < (int)sizeof(backend_rename_from));
    assert(snprintf(backend_rename_to, sizeof(backend_rename_to), "%s", to) <
           (int)sizeof(backend_rename_to));
    return ASTRA_VFS_OK;
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
    .symlink = astra_vfs_backend_deny_symlink,
};

static AstraVfsService service;
static AstraVfsSessionSlot service_sessions[ASTRA_VFS_SESSION_MAX];
static AstraVfsOpenFile service_files[128];

static void
service_start(void)
{
    assert(astra_vfs_service_init(
        &service, &backend_ops, NULL, service_sessions,
        ASTRA_VFS_SESSION_MAX, service_files,
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
    {
        uint32_t wakes = mock_futex_wakes;

    assert(astra_vfs_open(&remote, "/a", ASTRA_VFS_OPEN_READ, &remote_file,
                          &remote_size, &remote_kind) == ASTRA_VFS_OK);
        assert(mock_futex_wakes == wakes);
    }
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

    /* One reply port and one first-connect lock are reused by every call. */
    assert(next_port == 5u);
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
    assert(current_lane(&first)->reply_receive !=
           current_lane(&second)->reply_receive);
    assert(current_lane(&first)->reply_source !=
           current_lane(&second)->reply_source);
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

static void
test_host_accelerator_moves_data_plane_out_of_the_service(void)
{
    AstraVfsPortService host;
    AstraVfsClient remote;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    uint32_t service_handle;
    uint32_t accelerator;
    uint32_t moved = 0u;
    uint8_t bytes[8];

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    accelerator = mock_open(1u);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    assert(astra_vfs_port_service_set_accelerator(&host, accelerator));
    served = &host;
    assert(astra_vfs_port_connect_lazy_with_accelerator(
               &remote, service_handle, &mock_accelerator_ops) ==
           ASTRA_VFS_OK);
    assert(astra_vfs_open(&remote, "/a", ASTRA_VFS_OPEN_READ, &file, NULL,
                          NULL) == ASTRA_VFS_OK);
    assert(file == 0x12340001u);
    assert(direct_connects == 1u && direct_operations == 1u);
    assert(direct_last_operation == ASTRA_VFS_OP_OPEN);
    assert(host.requests == 1u);
    assert(port_lane_lookups == 1u);
    assert(lanes_have_no_areas(&remote));
    assert(astra_vfs_port_read_bulk(&remote, file, 0u, bytes,
                                    sizeof(bytes), &moved) == ASTRA_VFS_OK);
    assert(moved == sizeof(bytes) && direct_bulk_operations == 1u);
    assert(direct_bulk_buffer == bytes && bytes[0] == 0x5au &&
           bytes[sizeof(bytes) - 1u] == 0x5au);
    assert(lanes_have_no_areas(&remote));
    assert(port_lane_lookups == 1u);
    assert(astra_vfs_port_write_bulk(&remote, file, 8u, bytes,
                                     sizeof(bytes), &moved) == ASTRA_VFS_OK);
    assert(moved == sizeof(bytes) && direct_bulk_operations == 2u);
    assert(direct_bulk_buffer == bytes);
    assert(port_lane_lookups == 1u);
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    assert(direct_disconnects == 1u);
    served = NULL;
}

static void
test_shared_accelerated_client_keeps_thread_requests_in_flight(void)
{
    AstraVfsPortService host;
    AstraVfsClient remote;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    DirectReadCall first;
    DirectReadCall second;
    DisconnectCall disconnect;
    pthread_t first_thread;
    pthread_t second_thread;
    pthread_t closing_thread;
    uint32_t service_handle;
    uint32_t accelerator;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    accelerator = mock_open(1u);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    assert(astra_vfs_port_service_set_accelerator(&host, accelerator));
    served = &host;
    assert(astra_vfs_port_connect_lazy_with_accelerator(
               &remote, service_handle, &mock_accelerator_ops) ==
           ASTRA_VFS_OK);
    assert(astra_vfs_open(&remote, "/a", ASTRA_VFS_OPEN_READ, &file, NULL,
                          NULL) == ASTRA_VFS_OK);
    first = (DirectReadCall){.client = &remote, .file = file};
    second = (DirectReadCall){.client = &remote, .file = file + 1u};
    assert(pthread_mutex_lock(&direct_mutex) == 0);
    direct_hold = 1;
    assert(pthread_mutex_unlock(&direct_mutex) == 0);
    assert(pthread_create(&first_thread, NULL, direct_read_thread, &first) ==
           0);
    assert(pthread_create(&second_thread, NULL, direct_read_thread, &second) ==
           0);
    assert(pthread_mutex_lock(&direct_mutex) == 0);
    while (direct_entered != 2u)
        assert(pthread_cond_wait(&direct_changed, &direct_mutex) == 0);
    disconnect = (DisconnectCall){.client = &remote};
    atomic_init(&disconnect.done, 0);
    assert(pthread_create(&closing_thread, NULL, disconnect_thread,
                          &disconnect) == 0);
    while (__atomic_load_n(&remote.port_lifecycle, __ATOMIC_ACQUIRE) == 0u)
        sched_yield();
    assert(atomic_load(&disconnect.done) == 0);
    assert(direct_disconnects == 0u);
    direct_hold = 0;
    assert(pthread_cond_broadcast(&direct_changed) == 0);
    assert(pthread_mutex_unlock(&direct_mutex) == 0);
    assert(pthread_join(first_thread, NULL) == 0);
    assert(pthread_join(second_thread, NULL) == 0);
    assert(pthread_join(closing_thread, NULL) == 0);
    assert(first.status == ASTRA_VFS_OK && second.status == ASTRA_VFS_OK);
    assert(first.moved == sizeof(first.bytes) &&
           second.moved == sizeof(second.bytes));
    assert(first.bytes[0] == 0x5au && second.bytes[0] == 0x5au);
    assert(disconnect.status == ASTRA_VFS_OK);
    assert(direct_disconnects == 1u);
    served = NULL;
}

static void
test_shared_port_client_keeps_thread_requests_in_flight(void)
{
    AstraVfsPortService host;
    AstraVfsClient remote;
    PortThreadCall first;
    PortThreadCall second;
    pthread_t first_thread;
    pthread_t second_thread;
    uint32_t service_handle;
    uint32_t lanes = 0u;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    served = &host;
    assert(astra_vfs_port_connect(&remote, service_handle) == ASTRA_VFS_OK);
    first = (PortThreadCall){.client = &remote};
    second = (PortThreadCall){.client = &remote};

    assert(pthread_create(&first_thread, NULL, port_stat_thread, &first) == 0);
    assert(pthread_mutex_lock(&port_threads_mutex) == 0);
    while (port_threads_ready != 1u)
        assert(pthread_cond_wait(&port_threads_changed,
                                 &port_threads_mutex) == 0);
    assert(pthread_mutex_unlock(&port_threads_mutex) == 0);
    assert(pthread_create(&second_thread, NULL, port_stat_thread, &second) ==
           0);
    assert(pthread_mutex_lock(&port_threads_mutex) == 0);
    while (port_threads_ready != 2u)
        assert(pthread_cond_wait(&port_threads_changed,
                                 &port_threads_mutex) == 0);
    assert(pthread_mutex_unlock(&port_threads_mutex) == 0);

    for (uint32_t index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index)
        if (host.reply_sessions[index] == remote.session)
            ++lanes;
    assert(lanes == 3u);
    assert(pthread_mutex_lock(&mock_wait_mutex) == 0);
    mock_hold_waits = 1;
    assert(pthread_mutex_unlock(&mock_wait_mutex) == 0);
    assert(pthread_mutex_lock(&port_threads_mutex) == 0);
    port_threads_go = 1;
    assert(pthread_cond_broadcast(&port_threads_changed) == 0);
    assert(pthread_mutex_unlock(&port_threads_mutex) == 0);

    assert(pthread_mutex_lock(&mock_wait_mutex) == 0);
    while (mock_blocked_waits != 2u)
        assert(pthread_cond_wait(&mock_wait_changed, &mock_wait_mutex) == 0);
    assert(pthread_mutex_unlock(&mock_wait_mutex) == 0);
    assert(pthread_mutex_lock(&mock_port_mutex) == 0);
    assert(ports[service_handle].count == 2u);
    assert(pthread_mutex_unlock(&mock_port_mutex) == 0);

    assert(pthread_mutex_lock(&mock_wait_mutex) == 0);
    mock_hold_waits = 0;
    assert(pthread_cond_broadcast(&mock_wait_changed) == 0);
    assert(pthread_mutex_unlock(&mock_wait_mutex) == 0);
    assert(pthread_join(first_thread, NULL) == 0);
    assert(pthread_join(second_thread, NULL) == 0);
    assert(first.warm_status == ASTRA_VFS_ERR_UNSUPPORTED &&
           second.warm_status == ASTRA_VFS_ERR_UNSUPPORTED);
    assert(first.status == ASTRA_VFS_ERR_UNSUPPORTED &&
           second.status == ASTRA_VFS_ERR_UNSUPPORTED);
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    served = NULL;
}

static void
test_shared_lazy_client_opens_one_session_for_two_threads(void)
{
    AstraVfsPortService host;
    AstraVfsClient remote;
    PortThreadCall first;
    PortThreadCall second;
    pthread_t first_thread;
    pthread_t second_thread;
    uint32_t service_handle;
    uint32_t lanes = 0u;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    served = &host;
    assert(astra_vfs_port_connect_lazy(&remote, service_handle) ==
           ASTRA_VFS_OK);
    first = (PortThreadCall){.client = &remote};
    second = (PortThreadCall){.client = &remote};
    assert(pthread_create(&first_thread, NULL, port_one_stat_thread, &first) ==
           0);
    assert(pthread_create(&second_thread, NULL, port_one_stat_thread,
                          &second) == 0);
    assert(pthread_join(first_thread, NULL) == 0);
    assert(pthread_join(second_thread, NULL) == 0);
    assert(first.status == ASTRA_VFS_ERR_UNSUPPORTED &&
           second.status == ASTRA_VFS_ERR_UNSUPPORTED);
    assert(service.open_sessions == 1u);
    for (uint32_t index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index)
        if (host.reply_sessions[index] == remote.session)
            ++lanes;
    assert(lanes == 2u);
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    served = NULL;
}

static void
test_atomic_rename_crosses_in_one_request(void)
{
    AstraVfsPortService host;
    AstraVfsClient remote;
    uint32_t service_handle;
    uint32_t before;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    served = &host;
    assert(astra_vfs_port_connect(&remote, service_handle) == ASTRA_VFS_OK);
    before = host.requests;
    assert(astra_vfs_rename(&remote, "/from", "/to") == ASTRA_VFS_OK);
    assert(host.requests == before + 1u);
    assert(strcmp(backend_rename_from, "/from") == 0);
    assert(strcmp(backend_rename_to, "/to") == 0);
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
    assert(lanes_have_no_areas(&remote));
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
test_bulk_append_returns_the_atomic_backend_position(void)
{
    AstraVfsPortService host;
    AstraVfsClient remote;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    uint32_t service_handle;
    uint32_t moved = 0u;
    uint64_t position = 0u;
    const uint8_t bytes[] = {1u, 2u, 3u, 4u};

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    served = &host;
    assert(astra_vfs_port_connect(&remote, service_handle) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&remote, "/a",
                          ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_APPEND,
                          &file, NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_port_write_bulk_position(
               &remote, file, 17u, ASTRA_VFS_OPEN_APPEND, bytes,
               sizeof(bytes), &moved, &position) == ASTRA_VFS_OK);
    assert(moved == sizeof(bytes));
    assert(position == 900u + sizeof(bytes));
    assert(backend_write_flags ==
           (ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_APPEND));
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    served = NULL;
}

static void
test_accelerated_bulk_append_returns_the_atomic_backend_position(void)
{
    AstraVfsPortService host;
    AstraVfsClient remote;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    uint32_t service_handle;
    uint32_t accelerator;
    uint32_t moved = 0u;
    uint64_t position = 0u;
    const uint8_t bytes[] = {5u, 6u, 7u};

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    accelerator = mock_open(1u);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    assert(astra_vfs_port_service_set_accelerator(&host, accelerator));
    served = &host;
    assert(astra_vfs_port_connect_lazy_with_accelerator(
               &remote, service_handle, &mock_accelerator_ops) ==
           ASTRA_VFS_OK);
    assert(astra_vfs_open(&remote, "/a",
                          ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_APPEND,
                          &file, NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_port_write_bulk_position(
               &remote, file, 19u, ASTRA_VFS_OPEN_APPEND, bytes,
               sizeof(bytes), &moved, &position) == ASTRA_VFS_OK);
    assert(moved == sizeof(bytes));
    assert(position == 700u + sizeof(bytes));
    assert(direct_bulk_operations == 1u);
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    served = NULL;
}

static void
test_bulk_readdir_crosses_once_through_a_shared_area(void)
{
    AstraVfsPortService host;
    AstraVfsClient remote;
    AstraVfsDirEntry entries[16];
    AstraVfsFile directory = ASTRA_VFS_FILE_INVALID;
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

    assert(astra_vfs_open(&remote, "/dir",
                          ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY,
                          &directory, NULL, NULL) == ASTRA_VFS_OK);
    backend_readdir_directory = 0u;
    assert(astra_vfs_readdir_file_batch(
               &remote, directory, "/dir", 0u, entries, 16u, &count,
               &next) == ASTRA_VFS_OK);
    assert(count == 12u && backend_readdir_directory == 0x1234u);
    assert(astra_vfs_close(&remote, directory) == ASTRA_VFS_OK);
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
    assert(lanes_have_no_areas(&remote) && mock_area_maps == 0u);
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    served = NULL;
}

static void
test_lazy_bulk_path_read_binds_a_sized_area(void)
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
    assert(astra_vfs_port_connect_lazy(&remote, service_handle) ==
           ASTRA_VFS_OK);
    assert(astra_vfs_port_read_path(&remote, "/small", &bytes, &moved,
                                    &size) == ASTRA_VFS_OK);
    assert(moved == 8u && size == 8u && bytes != NULL);
    assert(host.area_sizes[0] != 0u);
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    assert(mock_area_maps == 0u);
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
        assert(lanes_have_no_areas(&remote));
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
    hello.header.transaction_id = 1u;
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
test_a_dead_client_is_reaped_when_the_next_client_connects(void)
{
    AstraVfsPortService host;
    AstraVfsClient first;
    AstraVfsClient second;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    uint8_t bytes[16];
    uint32_t moved = 0u;
    uint32_t service_handle;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    served = &host;
    assert(astra_vfs_port_connect(&first, service_handle) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&first, "/a", ASTRA_VFS_OPEN_READ, &file,
                          NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_port_read_bulk(&first, file, 0u, bytes,
                                    sizeof(bytes), &moved) == ASTRA_VFS_OK);
    assert(moved == 8u && mock_area_maps == 2u);

    dead_reply_handle = host.reply_handles[0];
    astra_vfs_port_abandon(&first);
    assert(mock_area_maps == 1u);
    mock_sender = 2u;
    assert(astra_vfs_port_connect(&second, service_handle) == ASTRA_VFS_OK);
    assert(service.open_sessions == 1u);
    assert(service.stats.sessions_closed == 1u);
    assert(mock_area_maps == 0u);
    assert(astra_vfs_disconnect(&second) == ASTRA_VFS_OK);
    served = NULL;
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
    hello.header.transaction_id = 1u;
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
    message.header.transaction_id = 1u;
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

static void
test_fork_child_rebinds_lane_owner(void)
{
    AstraVfsClient client;
    AstraVfsPortExecLane before;
    AstraVfsPortExecLane after;
    uint32_t service_handle;

    mock_reset();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_connect_lazy(&client, service_handle) ==
           ASTRA_VFS_OK);
    assert(astra_vfs_port_exec_lane_export(&client, &before) == ASTRA_VFS_OK);
    mock_thread = before.owner_thread + 1u;
    astra_vfs_port_after_fork_child(&client);
    assert(astra_vfs_port_exec_lane_export(&client, &after) == ASTRA_VFS_OK);
    assert(after.owner_thread == mock_thread &&
           after.owner_thread != before.owner_thread);
    astra_vfs_port_abandon(&client);
}

int
main(void)
{
    test_a_request_crosses_and_the_reply_is_the_same();
    test_a_dead_peer_is_reported_and_not_waited_on();
    test_clients_own_independent_reply_channels();
    test_first_operation_shares_the_hello_round_trip();
    test_host_accelerator_moves_data_plane_out_of_the_service();
    test_shared_accelerated_client_keeps_thread_requests_in_flight();
    test_shared_port_client_keeps_thread_requests_in_flight();
    test_shared_lazy_client_opens_one_session_for_two_threads();
    test_atomic_rename_crosses_in_one_request();
    test_version_two_keeps_per_request_reply_ports();
    test_bulk_read_crosses_once_through_a_shared_area();
    test_bulk_write_crosses_once_through_a_shared_area();
    test_bulk_append_returns_the_atomic_backend_position();
    test_accelerated_bulk_append_returns_the_atomic_backend_position();
    test_bulk_readdir_crosses_once_through_a_shared_area();
    test_small_path_read_needs_no_shared_area();
    test_lazy_bulk_path_read_binds_a_sized_area();
    test_bulk_resources_are_reusable_after_disconnect();
    test_a_dead_client_is_reaped_when_the_session_table_is_full();
    test_a_dead_client_is_reaped_when_the_next_client_connects();
    test_handle_pressure_reaps_a_dead_client_and_retries_receive();
    test_the_service_adopts_the_callers_activity();
    test_a_message_that_is_not_the_protocol_is_refused();
    test_an_answer_with_nowhere_to_go_is_counted();
    test_fork_child_rebinds_lane_owner();
    puts("ASTRA VFS PORT PASS");
    return 0;
}
