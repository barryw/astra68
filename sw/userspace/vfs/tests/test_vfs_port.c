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

void *astra_runtime_reallocate(void *pointer, size_t size) { return realloc(pointer, size); }
void astra_runtime_deallocate(void *pointer) { free(pointer); }

void *astra_runtime_allocate(size_t size) { return malloc(size); }
#include <string.h>

#include <astra/runtime.h>
#include <astra/vfs_backend.h>
#include <astra/vfs_client.h>
#include <astra/vfs_local_transport.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_path_payload.h>
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
    uint32_t area_size;
    uint8_t *area_address;
    uint32_t area_maps;
    uint8_t semaphore;
} MockPort;

static MockPort ports[MOCK_PORT_MAX];
static uint32_t next_port = 1u;
static uint32_t mock_activity;
/* Set while the transport is blocked, so the service runs inside its wait. */
static AstraVfsPortService *served;
/* When set, the wait serves `served` on this grant's port (FS15). */
static const AstraVfsPortGrant *served_grant;
static AstraVfsPortWorker served_worker;
static uint32_t backend_mkdir_calls;
static int refuse_send;
static uint32_t dead_reply_handle;
static uint32_t mock_empty_receives;
static uint32_t mock_receive_resource_limits;
static uint32_t mock_area_maps;
static uint32_t mock_area_create_size;
static uint32_t mock_close_count;
static int mock_fail_area_create;
static int mock_fail_area_map;
static int mock_fail_duplicate;
static atomic_uint mock_futex_wakes;
static atomic_uint backend_bad_stat_paths;
static uint32_t mock_sender;
static uint8_t backend_written[64];
static uint64_t backend_write_offset;
static uint32_t backend_write_length;
static uint32_t backend_write_flags;
static char backend_rename_from[ASTRA_VFS_PATH_MAX];
static char backend_rename_to[ASTRA_VFS_PATH_MAX];
static int backend_mutate_path_area;
static uint32_t backend_two_path_operation;
static uint32_t direct_connects;
static uint32_t direct_disconnects;
static uint32_t direct_operations;
static uint32_t refresh_before_enter;
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
static uint32_t mock_cancelled_waits;
static pthread_mutex_t port_threads_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t port_threads_changed = PTHREAD_COND_INITIALIZER;
static uint32_t port_threads_ready;
static int port_threads_go;
static atomic_uint mock_next_thread = 1u;
static _Thread_local uint32_t mock_thread;

uint32_t
astra_log_failure(const char *operation, uint32_t status)
{
    (void)operation;
    return status;
}

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
    for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_SLOT_COUNT;
         ++index)
        if (client->port_lanes[index].owner_thread == thread)
            return &client->port_lanes[index];
    return NULL;
}

static int
lanes_have_no_areas(const AstraVfsClient *client)
{
    for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_SLOT_COUNT;
         ++index)
        if (client->port_lanes[index].area != 0u ||
            client->port_lanes[index].area_address != NULL)
            return 0;
    return 1;
}

static void
mock_reset(void)
{
    for (uint32_t index = 0u; index < MOCK_PORT_MAX; ++index)
        free(ports[index].area_address);
    memset(ports, 0, sizeof(ports));
    next_port = 1u;
    served = NULL;
    served_grant = NULL;
    backend_mkdir_calls = 0u;
    refuse_send = 0;
    dead_reply_handle = 0u;
    mock_activity = 0u;
    mock_empty_receives = 0u;
    mock_receive_resource_limits = 0u;
    mock_area_maps = 0u;
    mock_area_create_size = 0u;
    mock_close_count = 0u;
    mock_fail_area_create = 0;
    mock_fail_area_map = 0;
    mock_fail_duplicate = 0;
    atomic_store_explicit(&mock_futex_wakes, 0u, memory_order_relaxed);
    atomic_store_explicit(&backend_bad_stat_paths, 0u,
                          memory_order_relaxed);
    mock_sender = 0x10000001u;
    memset(backend_written, 0, sizeof(backend_written));
    backend_write_offset = 0u;
    backend_write_length = 0u;
    backend_write_flags = 0u;
    backend_rename_from[0] = '\0';
    backend_rename_to[0] = '\0';
    backend_two_path_operation = 0u;
    direct_connects = 0u;
    direct_disconnects = 0u;
    direct_operations = 0u;
    refresh_before_enter = 0u;
    direct_last_operation = 0u;
    direct_bulk_operations = 0u;
    direct_bulk_buffer = NULL;
    port_lane_lookups = 0u;
    direct_hold = 0;
    direct_entered = 0u;
    mock_hold_waits = 0;
    mock_blocked_waits = 0u;
    mock_cancelled_waits = 0u;
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
                                       uint32_t scope)
{
    assert(client != NULL && scope != 0u);
    ++direct_connects;
    client->port_direct_address = (void *)(uintptr_t)1u;
    client->port_direct_scope = scope;
    client->port_direct_session = 1u;
    return ASTRA_VFS_OK;
}

void astra_vfs_host_direct_disconnect(AstraVfsClient *client)
{
    if (client != NULL && client->port_direct_address != NULL) {
        ++direct_disconnects;
        client->port_direct_address = NULL;
        client->port_direct_scope = 0u;
        client->port_direct_session = 0u;
    }
}

void astra_vfs_host_direct_abandon(AstraVfsClient *client)
{
    astra_vfs_host_direct_disconnect(client);
}

uint32_t astra_vfs_host_direct_transport(
    AstraVfsClient *client, uint32_t operation,
    const AstraVfsRequest *request, const AstraVfsTransfer *transfer, AstraVfsReply *reply)
{
    (void)transfer;
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
    } else if (operation == ASTRA_VFS_OP_SHARE) {
        reply->token = 42u;
    }
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_host_direct_bulk(
    AstraVfsClient *client, uint32_t operation,
    const AstraVfsRequest *request, const AstraVfsTransfer *transfer, void *buffer, uint32_t capacity,
    AstraVfsReply *reply)
{
    (void)transfer;
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
    reply->position =
        (request->flags & ASTRA_VFS_WRITE_APPEND) != 0u ?
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
    int refresh;
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

    call->status = call->refresh ? astra_vfs_port_refresh(call->client) :
                                   astra_vfs_disconnect(call->client);
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
    if (mock_fail_duplicate != 0) {
        status = ASTRA_SYSCALL_RESOURCE_LIMIT;
    } else if (duplicate == NULL || handle == 0u || handle >= MOCK_PORT_MAX ||
        !ports[handle].open || ports[handle].given_away) {
        status = ASTRA_SYSCALL_INVALID_HANDLE;
    } else {
        copy = mock_open_unlocked(1u);
        ports[copy].target = ports[handle].target;
        ports[copy].rights = rights;
        ports[copy].area_size = ports[handle].area_size;
        *duplicate = copy;
    }
    assert(pthread_mutex_unlock(&mock_port_mutex) == 0);
    return status;
}

uint32_t
astra_rt_area_create(uint32_t byte_size, uint32_t rights, uint32_t *handle)
{
    uint8_t *address;
    uint32_t span;

    (void)rights;
    if (mock_fail_area_create != 0 || handle == NULL || byte_size == 0u ||
        byte_size > ASTRA_VFS_BULK_MAX)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    span = (byte_size + ASTRA_MEMORY_PAGE_SIZE - 1u) &
        ~(ASTRA_MEMORY_PAGE_SIZE - 1u);
    address = calloc(1u, span);
    if (address == NULL)
        return ASTRA_SYSCALL_RESOURCE_LIMIT;
    assert(pthread_mutex_lock(&mock_port_mutex) == 0);
    *handle = mock_open_unlocked(1u);
    ports[*handle].area_size = byte_size;
    ports[*handle].area_address = address;
    mock_area_create_size = byte_size;
    assert(pthread_mutex_unlock(&mock_port_mutex) == 0);
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
    uint32_t status = ASTRA_SYSCALL_OK;

    (void)permissions;
    assert(pthread_mutex_lock(&mock_port_mutex) == 0);
    if (mock_fail_area_map != 0 || address == NULL || byte_size == NULL ||
        handle == 0u ||
        handle >= MOCK_PORT_MAX || !ports[handle].open ||
        ports[ports[handle].target].area_address == NULL) {
        status = ASTRA_SYSCALL_INVALID_HANDLE;
    } else {
        MockPort *area = &ports[ports[handle].target];
        *address = area->area_address;
        *byte_size = (area->area_size + ASTRA_MEMORY_PAGE_SIZE - 1u) &
                     ~(ASTRA_MEMORY_PAGE_SIZE - 1u);
        ++area->area_maps;
        ++mock_area_maps;
    }
    assert(pthread_mutex_unlock(&mock_port_mutex) == 0);
    return status;
}

uint32_t
astra_rt_area_unmap(void *address)
{
    uint32_t status = ASTRA_SYSCALL_INVALID_ARGUMENT;

    assert(pthread_mutex_lock(&mock_port_mutex) == 0);
    for (uint32_t index = 0u; index < MOCK_PORT_MAX; ++index) {
        MockPort *area = &ports[index];
        if (address != NULL && area->area_address == address && area->area_maps != 0u) {
            --area->area_maps;
            --mock_area_maps;
            status = ASTRA_SYSCALL_OK;
            break;
        }
    }
    assert(pthread_mutex_unlock(&mock_port_mutex) == 0);
    return status;
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

/* PORT_SEND_TRY with dispositions: each moved handle keeps its rights AND
 * the mask, as the kernel does (ABI 0x00010042). */
uint32_t
astra_port_send_rights(uint32_t handle, const void *message, uint32_t size,
                       const AstraHandleDisposition *dispositions,
                       uint32_t count)
{
    uint32_t handles[1];
    uint32_t status;

    assert(count == 1u);
    if (dispositions[0].rights == 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    handles[0] = dispositions[0].handle;
    assert(pthread_mutex_lock(&mock_port_mutex) == 0);
    status = mock_port_send(handle, message, size, handles, 1u);
    if (status == ASTRA_SYSCALL_OK)
        ports[handles[0]].rights &= dispositions[0].rights;
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
    (void)atomic_fetch_add_explicit(&mock_futex_wakes, 1u,
                                    memory_order_relaxed);
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
    ++mock_close_count;
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
    if (mock_cancelled_waits != 0u) {
        --mock_cancelled_waits;
        return ASTRA_SYSCALL_CANCELLED;
    }
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
        if (served_grant != NULL)
            (void)astra_vfs_port_service_grant_pump(served, &served_worker,
                                                    served_grant, 4u);
        else
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
static uint32_t backend_read_calls;

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
    ++backend_read_calls;
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
              uint32_t flags, uint64_t max_end, const void *buffer, uint32_t length,
              uint32_t *moved, uint64_t *position)
{
    (void)max_end;
    (void)context;
    (void)node;
    if (length > sizeof(backend_written))
        return ASTRA_VFS_ERR_LIMIT;
    memcpy(backend_written, buffer, length);
    backend_write_offset = offset;
    backend_write_length = length;
    backend_write_flags = flags;
    *moved = length;
    *position = (flags & ASTRA_VFS_WRITE_APPEND) != 0u ?
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
    (void)info;
    if (path == NULL || strcmp(path, "/a") != 0) {
        (void)atomic_fetch_add(&backend_bad_stat_paths, 1u);
        return ASTRA_VFS_ERR_INVALID;
    }
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
    ++backend_mkdir_calls;
    (void)context;
    (void)path;
    (void)create_mode;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

static uint32_t
backend_unlink(void *context, const char *path, uint32_t flags)
{
    (void)context;
    (void)path;
    (void)flags;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

static uint32_t
backend_rename(void *context, const char *from, const char *to)
{
    if (backend_mutate_path_area) {
        assert(served != NULL);
        for (uint32_t slot = 0u; slot < ASTRA_VFS_SESSION_MAX; ++slot)
            if (served->area_addresses[slot] != NULL)
                memset(served->area_addresses[slot], '!', served->area_sizes[slot]);
    }
    (void)context;
    assert(snprintf(backend_rename_from, sizeof(backend_rename_from), "%s",
                    from) < (int)sizeof(backend_rename_from));
    assert(snprintf(backend_rename_to, sizeof(backend_rename_to), "%s", to) <
           (int)sizeof(backend_rename_to));
    backend_two_path_operation = ASTRA_VFS_OP_RENAME;
    return ASTRA_VFS_OK;
}

static uint32_t
backend_symlink(void *context, const char *target, const char *path)
{
    uint32_t status = backend_rename(context, target, path);

    backend_two_path_operation = ASTRA_VFS_OP_SYMLINK;
    return status;
}

static uint32_t
backend_link(void *context, const char *from, const char *to)
{
    uint32_t status = backend_rename(context, from, to);

    backend_two_path_operation = ASTRA_VFS_OP_LINK;
    return status;
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
    .symlink = backend_symlink,
    .link = backend_link,
    .open_at = astra_vfs_backend_no_open_at,
    .unlink_at = astra_vfs_backend_no_unlink_at,
    .chmod_node = astra_vfs_backend_no_chmod_node,
    .chmod_at = astra_vfs_backend_no_chmod_at,
    .filesystem_info = astra_vfs_backend_no_filesystem_info,
    .stat_at = astra_vfs_backend_no_stat_at,
    .stat_node = astra_vfs_backend_no_stat_node,
    NULL,
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

/* A grant answered on `receive` with every right and a scope of its own. */
static AstraVfsPortGrant
full_grant(uint32_t receive, uint32_t id)
{
    AstraVfsPortGrant grant;

    memset(&grant, 0, sizeof(grant));
    grant.receive = receive;
    grant.id = id;
    grant.rights = ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE |
                   ASTRA_VFS_GRANT_DELEGATE;
    grant.scope = mock_open(1u);
    return grant;
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
        uint32_t wakes = atomic_load_explicit(&mock_futex_wakes,
                                              memory_order_relaxed);

    assert(astra_vfs_open(&remote, "/a", ASTRA_VFS_OPEN_READ, &remote_file,
                          &remote_size, &remote_kind) == ASTRA_VFS_OK);
        assert(atomic_load_explicit(&mock_futex_wakes,
                                    memory_order_relaxed) == wakes);
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

    /* Reply lane, connection lock and operand area are reused by every call. */
    assert(next_port == 7u);
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
                          NULL) == ASTRA_VFS_ERR_LOST);
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

static uint32_t owner_pins;
static void owner_acquire(AstraVfsClient *client)
{
    assert(client->ownership_epoch == 73u);
    ++owner_pins;
}
static void owner_release(AstraVfsClient *client, uint32_t epoch)
{
    assert(client->ownership_epoch == 73u && epoch == 73u && owner_pins != 0u);
    --owner_pins;
}
static void test_lazy_session_admission_preserves_native_owner(void)
{
    AstraVfsPortService host;
    AstraVfsClient client;
    AstraVfsFile file;
    mock_reset();
    service_start();
    uint32_t handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, handle, &service));
    served = &host;
    assert(astra_vfs_port_connect_lazy_with_accelerator(&client, handle, NULL) == ASTRA_VFS_OK);
    client.ownership_epoch = 73u;
    client.description_acquire = owner_acquire;
    client.description_release = owner_release;
    owner_pins = 0u;
    assert(astra_vfs_open(&client, "/a", ASTRA_VFS_OPEN_READ, &file, NULL, NULL) == ASTRA_VFS_OK);
    assert(owner_pins == 1u && client.ownership_epoch == 73u);
    assert(client.description_acquire == owner_acquire && client.description_release == owner_release);
    assert(astra_vfs_close(&client, file) == ASTRA_VFS_OK && owner_pins == 0u);
    assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK);
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
test_first_operation_binds_complete_operand_transport(void)
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
    assert(astra_vfs_port_connect_lazy_with_accelerator(&remote, service_handle, NULL) ==
           ASTRA_VFS_OK);
    assert(remote.session == ASTRA_VFS_SESSION_INVALID && host.requests == 0u);
    assert(astra_vfs_open(&remote, "/a", ASTRA_VFS_OPEN_READ, &file, NULL,
                          NULL) == ASTRA_VFS_OK);
    assert(remote.session != ASTRA_VFS_SESSION_INVALID && host.requests == 3u);
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    served = NULL;
}

static void
test_signal_cancellation_does_not_abandon_an_inflight_reply(void)
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
    assert(astra_vfs_port_connect_lazy_with_accelerator(&remote, service_handle, NULL) ==
           ASTRA_VFS_OK);
    mock_cancelled_waits = 1u;
    assert(astra_vfs_open(&remote, "/a", ASTRA_VFS_OPEN_READ, &file, NULL,
                          NULL) == ASTRA_VFS_OK);
    assert(file != ASTRA_VFS_FILE_INVALID && host.requests == 3u);
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
    AstraVfsPortGrant grant;
    uint32_t moved = 0u;
    uint8_t bytes[8];

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    grant = full_grant(service_handle, 1u);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    served = &host;
    served_grant = &grant;
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
    assert(astra_vfs_port_write_bulk(&remote, file, 8u, UINT64_MAX, bytes,
                                     sizeof(bytes), &moved) == ASTRA_VFS_OK);
    assert(moved == sizeof(bytes) && direct_bulk_operations == 2u);
    assert(direct_bulk_buffer == bytes);
    assert(port_lane_lookups == 1u);
    {
        const uint8_t *borrowed;

        assert(astra_vfs_port_read_borrow(&remote, file, 0u, sizeof(bytes), &borrowed, &moved) == ASTRA_VFS_OK);
        assert(astra_vfs_port_read_borrow(&remote, file, 0u, sizeof(bytes), &borrowed, &moved) == ASTRA_VFS_OK);
        assert(direct_bulk_operations == 4u);
    }
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    assert(direct_disconnects == 1u);
    served = NULL;
}

static void
test_shared_accelerated_client_keeps_thread_requests_in_flight(int refresh)
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
    AstraVfsPortGrant grant;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    grant = full_grant(service_handle, 1u);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    served = &host;
    served_grant = &grant;
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
    disconnect = (DisconnectCall){.client = &remote, .refresh = refresh};
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
    if (refresh) {
        assert(service.open_sessions == 1u && service.stats.sessions_opened == 1u);
        assert(direct_connects == 2u);
        assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    }
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
    assert(atomic_load(&backend_bad_stat_paths) == 0u);
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
    assert(astra_vfs_port_connect_lazy_with_accelerator(&remote, service_handle, NULL) ==
           ASTRA_VFS_OK);
    first = (PortThreadCall){.client = &remote};
    second = (PortThreadCall){.client = &remote};
    remote.ownership_epoch = 73u;
    remote.description_acquire = owner_acquire;
    remote.description_release = owner_release;
    assert(pthread_mutex_lock(&mock_wait_mutex) == 0);
    mock_hold_waits = 1;
    assert(pthread_mutex_unlock(&mock_wait_mutex) == 0);
    assert(pthread_create(&first_thread, NULL, port_one_stat_thread, &first) ==
           0);
    assert(pthread_mutex_lock(&mock_wait_mutex) == 0);
    while (mock_blocked_waits != 1u)
        assert(pthread_cond_wait(&mock_wait_changed, &mock_wait_mutex) == 0);
    /* HELLO is in flight while a second caller prepares its path. Admission
     * must leave the per-thread dispatch and canonical owner immutable. */
    assert(remote.call_acquire == astra_vfs_port_call_acquire);
    assert(remote.ownership_epoch == 73u);
    assert(remote.description_acquire == owner_acquire &&
           remote.description_release == owner_release);
    assert(pthread_mutex_unlock(&mock_wait_mutex) == 0);
    assert(pthread_create(&second_thread, NULL, port_one_stat_thread,
                          &second) == 0);
    assert(pthread_mutex_lock(&mock_wait_mutex) == 0);
    mock_hold_waits = 0;
    assert(pthread_cond_broadcast(&mock_wait_changed) == 0);
    assert(pthread_mutex_unlock(&mock_wait_mutex) == 0);
    assert(pthread_join(first_thread, NULL) == 0);
    assert(pthread_join(second_thread, NULL) == 0);
    assert(first.status == ASTRA_VFS_ERR_UNSUPPORTED &&
           second.status == ASTRA_VFS_ERR_UNSUPPORTED);
    assert(atomic_load(&backend_bad_stat_paths) == 0u);
    assert(host.refused == 0u);
    assert(service.open_sessions == 1u);
    for (uint32_t index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index)
        if (host.reply_sessions[index] == remote.session)
            ++lanes;
    assert(lanes == 2u);
    void *first_area = NULL;
    uint32_t independent_areas = 0u;
    for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_SLOT_COUNT; ++index) {
        void *area = remote.port_lanes[index].area_address;
        if (area != NULL) {
            if (first_area == NULL)
                first_area = area;
            else
                assert(area != first_area);
            ++independent_areas;
        }
    }
    assert(independent_areas == 2u);
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    served = NULL;
}

static void
test_two_path_operations_cross_atomically(void)
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
    assert(host.requests == before + 2u); /* one area bind, one atomic rename */
    assert(backend_two_path_operation == ASTRA_VFS_OP_RENAME);
    assert(strcmp(backend_rename_from, "/from") == 0);
    assert(strcmp(backend_rename_to, "/to") == 0);
    before = host.requests;
    assert(astra_vfs_symlink(&remote, "/target", "/link") == ASTRA_VFS_OK);
    assert(host.requests == before + 1u);
    assert(backend_two_path_operation == ASTRA_VFS_OP_SYMLINK);
    assert(strcmp(backend_rename_from, "/target") == 0);
    assert(strcmp(backend_rename_to, "/link") == 0);
    before = host.requests;
    assert(astra_vfs_link(&remote, "/from", "/hard-link") == ASTRA_VFS_OK);
    assert(host.requests == before + 1u);
    assert(backend_two_path_operation == ASTRA_VFS_OP_LINK);
    assert(strcmp(backend_rename_from, "/from") == 0);
    assert(strcmp(backend_rename_to, "/hard-link") == 0);
    {
        char *paths = malloc(2u * ASTRA_VFS_PATH_MAX);
        assert(paths != NULL);
        char *from = paths, *to = paths + ASTRA_VFS_PATH_MAX;
        memset(from, 'a', ASTRA_VFS_PATH_MAX - 1u);
        memset(to, 'b', ASTRA_VFS_PATH_MAX - 1u);
        for (uint32_t at = 0u; at < ASTRA_VFS_PATH_MAX - 1u; at += ASTRA_VFS_NAME_MAX)
            from[at] = to[at] = '/';
        from[ASTRA_VFS_PATH_MAX - 1u] = to[ASTRA_VFS_PATH_MAX - 1u] = '\0';
        backend_mutate_path_area = 1;
        before = host.requests;
        assert(astra_vfs_rename(&remote, from, to) == ASTRA_VFS_OK);
        assert(host.requests == before + 1u);
        assert(strcmp(backend_rename_from, from) == 0 && strcmp(backend_rename_to, to) == 0);
        backend_mutate_path_area = 0;
        free(paths);
    }
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    served = NULL;
}

static uint32_t
legacy_transport(void *context, uint32_t operation,
                 const AstraVfsRequest *request, const AstraVfsTransfer *transfer, AstraVfsReply *reply)
{
    (void)transfer;
    AstraVfsClient *client = context;
    AstraVfsRequest legacy = *request;

    legacy.version = UINT16_C(2);
    client->version = UINT16_C(2);
    return astra_vfs_port_transport(client, operation, &legacy, transfer, reply);
}

/* Storage protocol 28 is the oldest served: an older HELLO is answered
 * PROTOCOL and opens no session. */
static void
test_a_version_below_the_minimum_is_refused(void)
{
    AstraVfsPortService host;
    AstraVfsClient remote;
    uint32_t service_handle;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    memset(&remote, 0, sizeof(remote));
    remote.port_service = service_handle;
    served = &host;
    assert(astra_vfs_connect(&remote, legacy_transport, &remote) ==
           ASTRA_VFS_ERR_PROTOCOL);
    for (uint32_t index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index)
        assert(host.reply_sessions[index] == ASTRA_VFS_SESSION_INVALID);
    served = NULL;
}

/* Sends one HELLO carrying `fused` as its first operation (MKDIR "/x") at
 * `version` and answers the reply's status and attached handle count. */
static uint32_t
scoped_hello(AstraVfsPortService *host, const AstraVfsPortGrant *grant,
            uint16_t version, uint32_t fused, uint32_t *attached)
{
    AstraVfsPortWorker worker;
    AstraVfsRequestMessage message;
    AstraVfsReplyMessage reply;
    uint32_t handles[1] = {0u};
    uint32_t reply_handle = mock_open(1u);
    uint32_t size = 0u;
    uint32_t count = 0u;

    memset(&message, 0, sizeof(message));
    astra_message_header_set(&message.header, (uint32_t)sizeof(message),
                             ASTRA_VFS_PROTOCOL, ASTRA_VFS_VERSION,
                             ASTRA_VFS_OP_HELLO, 1u);
    message.request.size = ASTRA_VFS_REQUEST_SIZE;
    message.request.version = version;
    message.request.file = fused;
    handles[0] = reply_handle;
    assert(astra_port_send(grant->receive, &message, sizeof(message),
                           handles, 1u) == ASTRA_SYSCALL_OK);
    assert(astra_vfs_port_service_grant_pump(host, &worker, grant, 1u) ==
           1u);
    handles[0] = 0u;
    assert(astra_port_receive(reply_handle, &reply, sizeof(reply), handles,
                              1u, &size, &count) == ASTRA_SYSCALL_OK);
    *attached = count;
    return reply.reply.status;
}

/*
 * The relay does not come back as a fused HELLO (review I1): a scoped port
 * runs no operation fused with a HELLO at any version -- below 28 the HELLO
 * itself is refused, at 28 it is answered alone with the scope and the
 * client runs the operation on its lane.
 */
static void
test_hello_does_not_execute_data(void)
{
    AstraVfsPortService host;
    AstraVfsPortGrant grant;
    uint32_t attached = 0u;

    mock_reset();
    service_start();
    grant = full_grant(mock_open(MOCK_QUEUE_MAX), 1u);
    assert(astra_vfs_port_service_init(&host, grant.receive, &service));
    assert(scoped_hello(&host, &grant, UINT16_C(27),
                       ASTRA_VFS_OP_MKDIR, &attached) ==
           ASTRA_VFS_ERR_PROTOCOL);
    assert(attached == 0u && backend_mkdir_calls == 0u);
    assert(scoped_hello(&host, &grant, ASTRA_VFS_VERSION,
                       ASTRA_VFS_OP_MKDIR, &attached) == ASTRA_VFS_ERR_PROTOCOL);
    assert(attached == 0u && backend_mkdir_calls == 0u);
    assert(scoped_hello(&host, &grant, ASTRA_VFS_VERSION,
                       ASTRA_VFS_FILE_INVALID, &attached) == ASTRA_VFS_OK);
    assert(attached == 1u && backend_mkdir_calls == 0u);
}

/* A scoped HELLO that cannot attach its scope is refused: no session
 * without a lane, and no fall-through to the service core. */
static void
test_a_scoped_hello_without_its_scope_is_refused(void)
{
    AstraVfsPortService host;
    AstraVfsPortGrant grant;
    uint32_t attached = 0u;

    mock_reset();
    service_start();
    grant = full_grant(mock_open(MOCK_QUEUE_MAX), 1u);
    assert(astra_vfs_port_service_init(&host, grant.receive, &service));
    mock_fail_duplicate = 1;
    assert(scoped_hello(&host, &grant, ASTRA_VFS_VERSION,
                       ASTRA_VFS_OP_MKDIR, &attached) != ASTRA_VFS_OK);
    assert(scoped_hello(&host, &grant, ASTRA_VFS_VERSION,
                       ASTRA_VFS_FILE_INVALID, &attached) != ASTRA_VFS_OK);
    mock_fail_duplicate = 0;
    assert(attached == 0u && backend_mkdir_calls == 0u);
    for (uint32_t index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index)
        assert(host.reply_sessions[index] == ASTRA_VFS_SESSION_INVALID);
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
    assert(host.area_addresses[0] == current_lane(&remote)->area_address);
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    assert(lanes_have_no_areas(&remote));
    assert(mock_area_maps == 0u);
    served = NULL;
}

static void
test_borrowed_read_reuses_only_an_undisturbed_area(void)
{
    AstraVfsPortService host;
    AstraVfsClient remote;
    AstraVfsClient second;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    AstraVfsFile other = ASTRA_VFS_FILE_INVALID;
    AstraVfsFile second_file = ASTRA_VFS_FILE_INVALID;
    const uint8_t *bytes = NULL;
    uint32_t service_handle;
    uint32_t moved = 0u;

    mock_reset();
    backend_read_calls = 0u;
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    served = &host;
    assert(astra_vfs_port_connect(&remote, service_handle) == ASTRA_VFS_OK);
    assert(astra_vfs_port_connect(&second, service_handle) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&remote, "/small", ASTRA_VFS_OPEN_READ, &file,
                          NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&second, "/small", ASTRA_VFS_OPEN_READ,
                          &second_file, NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_port_read_borrow(&remote, file, 0u, 8u, &bytes,
                                      &moved) == ASTRA_VFS_OK);
    assert(moved == 8u && bytes[0] == 0u && bytes[7] == 7u);
    {
        uint32_t lookups = port_lane_lookups;

        assert(astra_vfs_port_read_borrow(&remote, file, 4u, 4u, &bytes,
                                          &moved) == ASTRA_VFS_OK);
        assert(moved == 4u && bytes[0] == 4u && bytes[3] == 7u);
        assert(backend_read_calls == 1u);
        assert(port_lane_lookups == lookups);
    }

    assert(astra_vfs_port_read_borrow(&second, second_file, 0u, 8u, &bytes,
                                      &moved) == ASTRA_VFS_OK);
    assert(astra_vfs_port_read_borrow(&remote, file, 4u, 4u, &bytes,
                                      &moved) == ASTRA_VFS_OK);
    assert(backend_read_calls == 3u);

    assert(astra_vfs_open(&remote, "/other", ASTRA_VFS_OPEN_READ, &other,
                          NULL, NULL) == ASTRA_VFS_OK);
    {
        uint32_t lookups = port_lane_lookups;

        assert(astra_vfs_port_read_borrow(&remote, file, 4u, 4u, &bytes,
                                          &moved) == ASTRA_VFS_OK);
        assert(moved == 4u && bytes[0] == 4u && bytes[3] == 7u);
        assert(backend_read_calls == 4u);
        assert(port_lane_lookups > lookups);
    }
    assert(astra_vfs_close(&remote, other) == ASTRA_VFS_OK);
    assert(astra_vfs_close(&second, second_file) == ASTRA_VFS_OK);
    assert(astra_vfs_port_read_borrow(&remote, file, 0u, 8u, &bytes,
                                      &moved) == ASTRA_VFS_OK);
    assert(backend_read_calls == 5u);
    assert(astra_vfs_disconnect(&second) == ASTRA_VFS_OK);
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    bytes = NULL;
    moved = 0u;
    assert(astra_vfs_port_read_borrow(&remote, file, 4u, 4u, &bytes,
                                      &moved) == ASTRA_VFS_ERR_LOST);
    assert(bytes == NULL && moved == 0u && backend_read_calls == 5u);
    served = NULL;
}

static void
test_direct_borrowed_read_reuses_only_an_undisturbed_area(void)
{
    AstraVfsPortService host;
    AstraVfsClient client;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    AstraVfsFile other = ASTRA_VFS_FILE_INVALID;
    const uint8_t *bytes = NULL;
    uint32_t service_handle;
    uint32_t moved = 0u;

    mock_reset();
    backend_read_calls = 0u;
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    served = &host;
    assert(astra_vfs_port_connect(&client, service_handle) == ASTRA_VFS_OK);
    client.direct_backend_ops = &backend_ops;
    client.direct_backend_context = &service;
    client.direct_backend_enter = astra_vfs_port_client_enter;
    client.direct_backend_leave = astra_vfs_port_client_leave;
    assert(astra_vfs_open(&client, "/small", ASTRA_VFS_OPEN_READ, &file,
                          NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_port_read_borrow(&client, file, 0u, 8u, &bytes,
                                      &moved) == ASTRA_VFS_OK);
    assert(moved == 8u && bytes[0] == 0u && bytes[7] == 7u);
    {
        uint32_t lookups = port_lane_lookups;

        assert(astra_vfs_port_read_borrow(&client, file, 4u, 4u, &bytes,
                                          &moved) == ASTRA_VFS_OK);
        assert(moved == 4u && bytes[0] == 4u && bytes[3] == 7u);
        assert(backend_read_calls == 1u);
        assert(port_lane_lookups == lookups);
    }

    assert(astra_vfs_open(&client, "/other", ASTRA_VFS_OPEN_READ, &other,
                          NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_port_read_borrow(&client, file, 4u, 4u, &bytes,
                                      &moved) == ASTRA_VFS_OK);
    assert(moved == 4u && bytes[0] == 4u && bytes[3] == 7u);
    assert(backend_read_calls == 2u);
    assert(astra_vfs_close(&client, other) == ASTRA_VFS_OK);
    assert(astra_vfs_close(&client, file) == ASTRA_VFS_OK);
    assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK);
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
    assert(astra_vfs_port_write_bulk(&remote, file, 5u, UINT64_MAX, first,
                                     sizeof(first), &moved) == ASTRA_VFS_OK);
    assert(moved == sizeof(first) && host.requests == before + 1u);
    assert(backend_write_offset == 5u &&
           backend_write_length == sizeof(first));
    assert(memcmp(backend_written, first, sizeof(first)) == 0);

    before = host.requests;
    assert(astra_vfs_port_write_bulk(&remote, file, 9u, UINT64_MAX, second,
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
               &remote, file, 17u, ASTRA_VFS_WRITE_APPEND, UINT64_MAX, bytes,
               sizeof(bytes), &moved, &position) == ASTRA_VFS_OK);
    assert(moved == sizeof(bytes));
    assert(position == 900u + sizeof(bytes));
    assert(backend_write_flags == ASTRA_VFS_WRITE_APPEND);
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
    AstraVfsPortGrant grant;
    uint32_t moved = 0u;
    uint64_t position = 0u;
    const uint8_t bytes[] = {5u, 6u, 7u};

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    grant = full_grant(service_handle, 1u);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    served = &host;
    served_grant = &grant;
    assert(astra_vfs_port_connect_lazy_with_accelerator(
               &remote, service_handle, &mock_accelerator_ops) ==
           ASTRA_VFS_OK);
    assert(astra_vfs_open(&remote, "/a",
                          ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_APPEND,
                          &file, NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_port_write_bulk_position(
               &remote, file, 19u, ASTRA_VFS_WRITE_APPEND, UINT64_MAX, bytes,
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
    assert(host.area_addresses[0] == current_lane(&remote)->area_address);

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
test_small_path_read_uses_canonical_area(void)
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
    assert(astra_vfs_port_read_path(&remote, "/small", &bytes, &moved,
                                           &size) == ASTRA_VFS_OK);
    assert(moved == 8u && size == 8u && bytes != NULL);
    for (uint32_t index = 0u; index < moved; ++index)
        assert(bytes[index] == (uint8_t)index);
    assert(!lanes_have_no_areas(&remote) && mock_area_maps != 0u);
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
    assert(astra_vfs_port_connect_lazy_with_accelerator(&remote, service_handle, NULL) ==
           ASTRA_VFS_OK);
    assert(astra_vfs_port_read_path(&remote, "/small", &bytes, &moved,
                                    &size) == ASTRA_VFS_OK);
    assert(moved == 8u && size == 8u && bytes != NULL);
    assert(host.area_sizes[0] != 0u);
    bytes = NULL;
    moved = 0u;
    size = 0u;
    assert(astra_vfs_port_read_path(&remote, "/small", &bytes, &moved,
                                           &size) == ASTRA_VFS_OK);
    assert(moved == 8u && size == 8u && bytes != NULL);
    for (uint32_t index = 0u; index < moved; ++index)
        assert(bytes[index] == (uint8_t)index);
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
test_a_zero_session_request_is_refused(void)
{
    AstraVfsPortService host;
    AstraVfsClient remote;
    AstraVfsRequestMessage message;
    uint32_t service_handle;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    served = &host;
    assert(astra_vfs_port_connect(&remote, service_handle) == ASTRA_VFS_OK);

    memset(&message, 0, sizeof(message));
    message.header.total_size = (uint32_t)sizeof(message);
    message.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    message.header.protocol = ASTRA_VFS_PROTOCOL;
    message.header.protocol_version = ASTRA_VFS_VERSION;
    message.header.operation = ASTRA_VFS_OP_STAT;
    message.header.transaction_id = UINT32_C(0x8000ffff);
    message.request.size = ASTRA_VFS_REQUEST_SIZE;
    message.request.version = ASTRA_VFS_VERSION;
    message.request.session = ASTRA_VFS_SESSION_INVALID;
    message.request.body.paths.first_length = 3u;
    assert(astra_port_send(service_handle, &message, sizeof(message), NULL,
                           0u) == ASTRA_SYSCALL_OK);
    assert(astra_vfs_port_service_pump(&host, 1u) == 0u);
    assert(host.refused == 1u);
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    served = NULL;
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
    assert(astra_vfs_port_connect_lazy_with_accelerator(&client, service_handle, NULL) ==
           ASTRA_VFS_OK);
    assert(astra_vfs_port_exec_lane_export(&client, &before) == ASTRA_VFS_OK);
    mock_thread = before.owner_thread + 1u;
    astra_vfs_port_after_fork_child(&client);
    assert(astra_vfs_port_exec_lane_export(&client, &after) == ASTRA_VFS_OK);
    assert(after.owner_thread == mock_thread &&
           after.owner_thread != before.owner_thread);
    astra_vfs_port_abandon(&client);
}

static void
test_quota_storage_reserves_only_the_representable_table(void)
{
    AstraArea storage = ASTRA_AREA_INIT;
    uint32_t capacity = UINT32_MAX;
    uint32_t expected =
        (uint32_t)sizeof(AstraVfsOpenFile) * ASTRA_VFS_FILE_HANDLE_MAX;
    uint32_t closes;

    mock_reset();
    assert(expected < ASTRA_AREA_SIZE_MAX);
    assert(astra_vfs_port_quota_storage(sizeof(AstraVfsOpenFile), &storage,
                                        &capacity));
    assert(mock_area_create_size == expected);
    assert(storage.handle != ASTRA_INVALID_HANDLE &&
           storage.address == ports[storage.handle].area_address &&
           capacity == ASTRA_VFS_FILE_HANDLE_MAX);
    assert(astra_rt_area_unmap(storage.address) == ASTRA_SYSCALL_OK);
    assert(astra_close(storage.handle) == ASTRA_SYSCALL_OK);
    storage = (AstraArea)ASTRA_AREA_INIT;

    capacity = UINT32_MAX;
    assert(!astra_vfs_port_quota_storage(0u, &storage, &capacity));
    assert(storage.address == NULL && storage.handle == ASTRA_INVALID_HANDLE && capacity == 0u);

    mock_fail_area_create = 1;
    assert(!astra_vfs_port_quota_storage(sizeof(AstraVfsOpenFile), &storage,
                                         &capacity));
    assert(storage.address == NULL && storage.handle == ASTRA_INVALID_HANDLE && capacity == 0u);
    mock_fail_area_create = 0;

    closes = mock_close_count;
    mock_fail_area_map = 1;
    assert(!astra_vfs_port_quota_storage(sizeof(AstraVfsOpenFile), &storage,
                                         &capacity));
    assert(storage.address == NULL && storage.handle == ASTRA_INVALID_HANDLE && capacity == 0u &&
           mock_close_count == closes + 1u);
}

static uint32_t derive_calls;
static uint32_t derive_rights;
static uint32_t derive_deriver;
static uint32_t derive_session;
static char derive_root[ASTRA_VFS_PATH_MAX];

static uint32_t
mock_derive(void *context, const AstraVfsPortDeriveRequest *request,
            uint32_t *send)
{
    (void)context;
    assert(request->parent != NULL);
    ++derive_calls;
    derive_rights = request->rights;
    derive_deriver = request->deriver;
    derive_session = request->session;
    strcpy(derive_root, request->root);
    *send = mock_open(1u);
    return ASTRA_VFS_OK;
}

/* Sends one standard request for `session` on `grant`'s port and pumps it. */
static uint32_t
grant_round_trip(AstraVfsPortService *host, const AstraVfsPortGrant *grant,
                 uint32_t operation, uint32_t session, uint32_t flags,
                 const char *path, uint32_t attach, uint32_t reply_handle,
                 AstraVfsReplyMessage *reply, uint32_t *received)
{
    AstraVfsRequestMessage message;
    uint32_t handles[1] = {attach};
    uint32_t size = 0u;
    uint32_t count = 0u;
    AstraVfsPortWorker worker;

    memset(&message, 0, sizeof(message));
    astra_message_header_set(&message.header, (uint32_t)sizeof(message),
                             ASTRA_VFS_PROTOCOL, ASTRA_VFS_VERSION, operation,
                             1u);
    message.request.size = ASTRA_VFS_REQUEST_SIZE;
    message.request.version = ASTRA_VFS_VERSION;
    message.request.session = session;
    message.request.flags = flags;
    if (operation == ASTRA_VFS_OP_BIND_AREA)
        message.request.length = 2u * ASTRA_VFS_PATH_MAX;
    if (astra_vfs_path_operand_count(operation, &message.request) != 0u) {
        uint32_t slot;
        for (slot = 0u; slot < ASTRA_VFS_SESSION_MAX; ++slot)
            if (host->reply_sessions[slot] == session)
                break;
        assert(slot < ASTRA_VFS_SESSION_MAX);
        if (host->area_addresses[slot] == NULL) {
            uint32_t area, ignored;
            AstraVfsReplyMessage bound;
            assert(astra_rt_area_create(2u * ASTRA_VFS_PATH_MAX,
                ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER,
                &area) == ASTRA_SYSCALL_OK);
            assert(grant_round_trip(host, grant, ASTRA_VFS_OP_BIND_AREA, session,
                0u, "", area, reply_handle, &bound, &ignored) == ASTRA_SYSCALL_OK);
            assert(bound.reply.status == ASTRA_VFS_OK);
        }
        uint32_t used;
        assert(astra_vfs_path_pack(path, NULL, host->area_addresses[slot],
            host->area_sizes[slot], &message.request.body.paths, &used) == ASTRA_VFS_OK);
    }
    assert(astra_port_send(grant->receive, &message, sizeof(message),
                           attach != 0u ? handles : NULL,
                           attach != 0u ? 1u : 0u) == ASTRA_SYSCALL_OK);
    if (astra_vfs_port_service_grant_pump(host, &worker, grant, 1u) == 0u)
        return ASTRA_SYSCALL_WOULD_BLOCK;
    handles[0] = 0u;
    assert(astra_port_receive(reply_handle, reply, sizeof(*reply), handles,
                              1u, &size, &count) == ASTRA_SYSCALL_OK);
    *received = count != 0u ? handles[0] : 0u;
    return ASTRA_SYSCALL_OK;
}

/*
 * Storage protocol 29 (docs/FILESYSTEM_PRODUCTION_PRD.md FS15): a grant is a
 * port. HELLO on it answers a session, the GRANTS feature and the grant's
 * scope; a session is refused on any other grant's port; the port carries
 * no data; GRANT_DERIVE only narrows.
 */
static void
test_grants_hand_out_their_scope_and_only_narrow(void)
{
    AstraVfsPortService host;
    AstraVfsPortGrant writable;
    AstraVfsPortGrant readable;
    AstraVfsReplyMessage reply;
    uint32_t reply_handle;
    uint32_t reply_two;
    uint32_t received = 0u;
    uint32_t session;

    mock_reset();
    service_start();
    derive_calls = 0u;
    writable = full_grant(mock_open(MOCK_QUEUE_MAX), 1u);
    readable = full_grant(mock_open(MOCK_QUEUE_MAX), 2u);
    readable.rights = ASTRA_VFS_GRANT_READ;
    assert(astra_vfs_port_service_init(&host, writable.receive, &service));
    assert(astra_vfs_port_service_set_derive(&host, mock_derive, NULL));
    reply_handle = mock_open(4u);
    reply_two = mock_open(4u);

    /* HELLO on a grant: a session, the GRANTS feature, the grant's scope. */
    assert(grant_round_trip(&host, &writable, ASTRA_VFS_OP_HELLO, 0u, 0u,
                            "", reply_handle, reply_handle, &reply,
                            &received) == ASTRA_SYSCALL_OK);
    assert(reply.reply.status == ASTRA_VFS_OK);
    assert(reply.reply.version == ASTRA_VFS_VERSION);
    assert(reply.reply.features == ASTRA_VFS_FEATURE_GRANTS);
    assert(received != 0u && received != writable.scope &&
           ports[received].target == ports[writable.scope].target);
    session = reply.reply.session;
    assert(grant_round_trip(&host, &writable, ASTRA_VFS_OP_HELLO, session, 0u,
                            "", reply_handle, reply_handle, &reply,
                            &received) == ASTRA_SYSCALL_OK);
    assert(reply.reply.status == ASTRA_VFS_OK && reply.reply.session == session);
    assert(service.open_sessions == 1u && service.stats.sessions_opened == 1u);
    assert(received != 0u);

    /* The same session on the read-only grant's port: refused unanswered. */
    assert(grant_round_trip(&host, &readable, ASTRA_VFS_OP_BYE, session, 0u,
                            "", 0u, reply_handle, &reply, &received) ==
           ASTRA_SYSCALL_WOULD_BLOCK);
    assert(host.refused == 1u);

    /* A grant port is control only: the data is the lane's. */
    assert(grant_round_trip(&host, &writable, ASTRA_VFS_OP_MKDIR, session,
                            0u, "/x", 0u, reply_handle, &reply,
                            &received) == ASTRA_SYSCALL_OK);
    assert(reply.reply.status == ASTRA_VFS_ERR_UNSUPPORTED);
    assert(reply.reply.features == 0u);
    assert(backend_mkdir_calls == 0u);

    /* Derive: never wider, and DELEGATE only from a delegable grant. */
    assert(grant_round_trip(&host, &readable, ASTRA_VFS_OP_HELLO, 0u, 0u,
                            "", reply_two, reply_two, &reply,
                            &received) == ASTRA_SYSCALL_OK);
    assert(reply.reply.status == ASTRA_VFS_OK && received != 0u);
    {
        uint32_t readonly_session = reply.reply.session;

        assert(grant_round_trip(&host, &readable, ASTRA_VFS_OP_GRANT_DERIVE,
                                readonly_session,
                                ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE,
                                "", 0u, reply_two, &reply,
                                &received) == ASTRA_SYSCALL_OK);
        assert(reply.reply.status == ASTRA_VFS_ERR_ACCESS && received == 0u);
        assert(grant_round_trip(&host, &readable, ASTRA_VFS_OP_GRANT_DERIVE,
                                readonly_session,
                                ASTRA_VFS_GRANT_READ |
                                    ASTRA_VFS_GRANT_DELEGATE,
                                "", 0u, reply_two, &reply,
                                &received) == ASTRA_SYSCALL_OK);
        assert(reply.reply.status == ASTRA_VFS_ERR_ACCESS && received == 0u);
        /* Every grant reads: a derived grant without READ is no grant. */
        assert(grant_round_trip(&host, &readable, ASTRA_VFS_OP_GRANT_DERIVE,
                                readonly_session, 0u, "", 0u, reply_two,
                                &reply, &received) == ASTRA_SYSCALL_OK);
        assert(reply.reply.status == ASTRA_VFS_ERR_ACCESS && received == 0u);
        assert(derive_calls == 0u);
    }
    assert(grant_round_trip(&host, &writable, ASTRA_VFS_OP_GRANT_DERIVE,
                            session, ASTRA_VFS_GRANT_READ, "sub", 0u,
                            reply_handle, &reply,
                            &received) == ASTRA_SYSCALL_OK);
    assert(reply.reply.status == ASTRA_VFS_OK);
    assert(received != 0u && derive_calls == 1u);
    assert(derive_rights == ASTRA_VFS_GRANT_READ);
    assert(strcmp(derive_root, "sub") == 0);
    /* The deriver is the kernel-stamped sender on its own session. A
     * delegable parent's derived port arrives with every right. */
    assert(derive_deriver == mock_sender && derive_session == session);
    assert((ports[received].rights & ASTRA_RIGHT_TRANSFER) != 0u);
    /* A non-delegable parent's derived port arrives without TRANSFER: it
     * cannot travel on (review C1, the kernel's dispositions). */
    {
        uint32_t readonly_session = 0u;

        for (uint32_t index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index)
            if (host.reply_handles[index] == reply_two)
                readonly_session = host.reply_sessions[index];
        assert(readonly_session != 0u);
        assert(grant_round_trip(&host, &readable, ASTRA_VFS_OP_GRANT_DERIVE,
                                readonly_session, ASTRA_VFS_GRANT_READ, "",
                                0u, reply_two, &reply,
                                &received) == ASTRA_SYSCALL_OK);
        assert(reply.reply.status == ASTRA_VFS_OK && received != 0u);
        assert(derive_calls == 2u && derive_session == readonly_session);
        assert((ports[received].rights & ASTRA_RIGHT_TRANSFER) == 0u);
        assert((ports[received].rights & ASTRA_RIGHT_SIGNAL) != 0u);
    }

    /* The plain pump serves no grant: it neither answers features nor
     * derives. */
    {
        uint32_t plain = mock_open(MOCK_QUEUE_MAX);
        AstraVfsPortService unscoped;
        AstraVfsReplyMessage answer;
        AstraVfsRequestMessage message;
        uint32_t handles[1];
        uint32_t size = 0u;
        uint32_t count = 0u;
        uint32_t reply_three = mock_open(4u);
        uint32_t area = 0u;
        uint32_t span = 0u;
        uint32_t used = 0u;
        void *area_bytes = NULL;

        assert(astra_vfs_port_service_init(&unscoped, plain, &service));
        assert(astra_vfs_port_service_set_derive(&unscoped, mock_derive,
                                                 NULL));
        memset(&message, 0, sizeof(message));
        astra_message_header_set(&message.header, (uint32_t)sizeof(message),
                                 ASTRA_VFS_PROTOCOL, ASTRA_VFS_VERSION,
                                 ASTRA_VFS_OP_HELLO, 1u);
        message.request.size = ASTRA_VFS_REQUEST_SIZE;
        message.request.version = ASTRA_VFS_VERSION;
        handles[0] = reply_three;
        assert(astra_port_send(plain, &message, sizeof(message), handles,
                               1u) == ASTRA_SYSCALL_OK);
        assert(astra_vfs_port_service_pump(&unscoped, 1u) == 1u);
        assert(astra_port_receive(reply_three, &answer, sizeof(answer),
                                  handles, 1u, &size, &count) ==
               ASTRA_SYSCALL_OK);
        assert(answer.reply.status == ASTRA_VFS_OK && count == 0u);
        assert(answer.reply.features == 0u);
        message.request.session = answer.reply.session;
        assert(astra_rt_area_create(2u * ASTRA_VFS_PATH_MAX,
            ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
                ASTRA_RIGHT_TRANSFER, &area) == ASTRA_SYSCALL_OK);
        assert(astra_rt_area_map(area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
            &area_bytes, &span) == ASTRA_SYSCALL_OK);
        message.header.operation = ASTRA_VFS_OP_BIND_AREA;
        message.request.length = span;
        handles[0] = area;
        assert(astra_port_send(plain, &message, sizeof(message), handles, 1u) ==
            ASTRA_SYSCALL_OK);
        assert(astra_vfs_port_service_pump(&unscoped, 1u) == 1u);
        assert(astra_port_receive(reply_three, &answer, sizeof(answer),
            handles, 1u, &size, &count) == ASTRA_SYSCALL_OK);
        assert(answer.reply.status == ASTRA_VFS_OK && count == 0u);
        assert(astra_vfs_path_pack("", NULL, area_bytes, span,
            &message.request.body.paths, &used) == ASTRA_VFS_OK);
        message.request.length = 0u;
        message.request.flags = ASTRA_VFS_GRANT_READ;
        message.header.operation = ASTRA_VFS_OP_GRANT_DERIVE;
        assert(astra_port_send(plain, &message, sizeof(message), NULL, 0u) ==
               ASTRA_SYSCALL_OK);
        assert(astra_vfs_port_service_pump(&unscoped, 1u) == 1u);
        assert(astra_port_receive(reply_three, &answer, sizeof(answer),
                                  handles, 1u, &size, &count) ==
               ASTRA_SYSCALL_OK);
        assert(answer.reply.status == ASTRA_VFS_ERR_UNSUPPORTED &&
               count == 0u && derive_calls == 2u);
    }
}

typedef struct DeriveCall {
    AstraVfsClient *client;
    uint32_t handle;
    uint32_t status;
} DeriveCall;

static uint32_t refresh_enter(AstraVfsClient *client)
{
    if (refresh_before_enter != 0u) {
        uint32_t status = astra_vfs_port_refresh(client);
        if (status != ASTRA_VFS_OK)
            return status;
        refresh_before_enter = 0u;
    }
    return astra_vfs_port_client_enter(client);
}

static void *
derive_thread(void *context)
{
    DeriveCall *call = context;

    call->status = astra_vfs_grant_derive(call->client, "", ASTRA_VFS_GRANT_READ,
                                          &call->handle);
    return NULL;
}

/*
 * The Kit's GRANT_DERIVE (Kit 7): a lane client sends it to the grant's port,
 * never to its lane, on the lane its HELLO bound -- from any thread -- and
 * answers the new grant's send handle. A client without the GRANTS feature
 * is refused locally.
 */
static void
test_the_kit_derives_on_its_hello_lane(void)
{
    AstraVfsPortService host;
    AstraVfsPortGrant grant;
    AstraVfsClient remote;
    AstraVfsClient plain;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    DeriveCall call;
    pthread_t thread;
    uint32_t handle = 0u;
    uint32_t operations;
    uint32_t plain_handle;
    uint64_t token = 0u;

    mock_reset();
    service_start();
    derive_calls = 0u;
    grant = full_grant(mock_open(MOCK_QUEUE_MAX), 1u);
    grant.features = ASTRA_VFS_FEATURE_DESCRIPTIONS;
    assert(astra_vfs_port_service_init(&host, grant.receive, &service));
    assert(astra_vfs_port_service_set_derive(&host, mock_derive, NULL));
    served = &host;
    served_grant = &grant;
    assert(astra_vfs_port_connect_lazy_with_accelerator(
               &remote, grant.receive, &mock_accelerator_ops) ==
           ASTRA_VFS_OK);
    assert(remote.features == 0u);
    assert(astra_vfs_open(&remote, "/a", ASTRA_VFS_OPEN_READ, &file, NULL,
                          NULL) == ASTRA_VFS_OK);
    assert(remote.features == (ASTRA_VFS_FEATURE_GRANTS | ASTRA_VFS_FEATURE_DESCRIPTIONS));
    /* SHARE is the shell's first data operation after a reset. It must use
     * the same refresh guard as direct backend and bulk operations. */
    remote.direct_backend_enter = refresh_enter;
    refresh_before_enter = 1u;
    uint32_t session = remote.session;
    assert(astra_vfs_share(&remote, file, &token) == ASTRA_VFS_OK && token == 42u);
    assert(refresh_before_enter == 0u && remote.session == session &&
           direct_connects == 2u && service.stats.sessions_opened == 1u);
    operations = direct_operations;
    assert(astra_vfs_grant_derive(&remote, "sub", ASTRA_VFS_GRANT_READ,
                                  &handle) == ASTRA_VFS_OK);
    assert(handle != 0u && derive_calls == 1u);
    assert(derive_rights == ASTRA_VFS_GRANT_READ);
    assert(strcmp(derive_root, "sub") == 0);
    assert(direct_operations == operations);
    /* Another thread derives on the lane the HELLO bound: the service
     * answers it rather than refusing a lane it never saw. */
    call = (DeriveCall){.client = &remote};
    assert(pthread_create(&thread, NULL, derive_thread, &call) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(call.status == ASTRA_VFS_OK && call.handle != 0u &&
           call.handle != handle && derive_calls == 2u);
    assert(host.refused == 0u);
    handle = 0u;
    assert(astra_vfs_grant_derive(&remote, "", UINT32_C(1) << 3, &handle) ==
           ASTRA_VFS_ERR_INVALID && handle == 0u);
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);

    /* An eager connect records the feature too; a port that serves no
     * grant leaves it clear, and the Kit refuses without sending. */
    assert(astra_vfs_port_connect_with_accelerator(
               &remote, grant.receive, &mock_accelerator_ops) ==
           ASTRA_VFS_OK);
    assert(remote.features == (ASTRA_VFS_FEATURE_GRANTS | ASTRA_VFS_FEATURE_DESCRIPTIONS));
    refuse_send = 1;
    assert(astra_vfs_grant_derive(&remote, "", ASTRA_VFS_GRANT_READ,
                                  &handle) == ASTRA_VFS_ERR_LOST &&
           handle == 0u);
    refuse_send = 0;
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    served_grant = NULL;
    plain_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, plain_handle, &service));
    assert(astra_vfs_port_connect(&plain, plain_handle) == ASTRA_VFS_OK);
    assert(plain.features == 0u);
    operations = host.requests;
    assert(astra_vfs_grant_derive(&plain, "", ASTRA_VFS_GRANT_READ,
                                  &handle) == ASTRA_VFS_ERR_UNSUPPORTED &&
           handle == 0u && host.requests == operations);
    assert(astra_vfs_disconnect(&plain) == ASTRA_VFS_OK);
    served = NULL;
}

static void
test_descriptions_require_a_connected_data_plane(void)
{
    AstraVfsPortService host;
    AstraVfsPortGrant grant;
    AstraVfsClient remote;
    uint64_t token;
    AstraVfsFile file;
    uint32_t operations;

    mock_reset();
    service_start();
    grant = full_grant(mock_open(MOCK_QUEUE_MAX), 1u);
    grant.features = ASTRA_VFS_FEATURE_DESCRIPTIONS;
    grant.scope = 0u;
    assert(astra_vfs_port_service_init(&host, grant.receive, &service));
    served = &host;
    served_grant = &grant;
    /* Eager and lazy plain clients, and an accelerator with no negotiated
     * scope, all retain grant control without claiming description support. */
    for (uint32_t mode = 0u; mode < 3u; ++mode) {
        if (mode == 1u) {
            assert(astra_vfs_port_connect_lazy_with_accelerator(
                &remote, grant.receive, NULL) == ASTRA_VFS_OK);
            assert(remote.features == 0u);
            assert(astra_vfs_port_refresh(&remote) == ASTRA_VFS_OK);
        } else {
            assert(astra_vfs_port_connect_with_accelerator(&remote,
                grant.receive, mode == 2u ? &mock_accelerator_ops : NULL) ==
                ASTRA_VFS_OK);
        }
        assert(remote.features == ASTRA_VFS_FEATURE_GRANTS &&
               direct_connects == 0u);
        operations = host.requests;
        token = 99u;
        assert(astra_vfs_share(&remote, 1u, &token) ==
               ASTRA_VFS_ERR_UNSUPPORTED && token == 0u);
        file = 1u;
        assert(astra_vfs_adopt(&remote, 42u, &file) ==
               ASTRA_VFS_ERR_UNSUPPORTED && file == ASTRA_VFS_FILE_INVALID);
        assert(host.requests == operations);
        assert(astra_vfs_port_refresh(&remote) == ASTRA_VFS_OK &&
               remote.features == ASTRA_VFS_FEATURE_GRANTS);
        assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    }
    served = NULL;
    served_grant = NULL;
}

/* Retiring a grant drops its sessions' reply lanes at once, not at the
 * next reap of a dead peer (review M4). */
static void
test_retiring_a_grant_drops_its_reply_lanes(void)
{
    AstraVfsPortService host;
    AstraVfsPortGrant grant;
    AstraVfsPortGrant other;
    AstraVfsReplyMessage reply;
    uint32_t reply_handle;
    uint32_t reply_other;
    uint32_t received = 0u;
    uint32_t session;
    uint32_t kept;
    uint32_t closes;

    mock_reset();
    service_start();
    grant = full_grant(mock_open(MOCK_QUEUE_MAX), 4u);
    other = full_grant(mock_open(MOCK_QUEUE_MAX), 5u);
    assert(astra_vfs_port_service_init(&host, grant.receive, &service));
    reply_handle = mock_open(4u);
    reply_other = mock_open(4u);
    assert(grant_round_trip(&host, &grant, ASTRA_VFS_OP_HELLO, 0u, 0u, "",
                            reply_handle, reply_handle, &reply,
                            &received) == ASTRA_SYSCALL_OK);
    session = reply.reply.session;
    assert(grant_round_trip(&host, &other, ASTRA_VFS_OP_HELLO, 0u, 0u, "",
                            reply_other, reply_other, &reply,
                            &received) == ASTRA_SYSCALL_OK);
    kept = reply.reply.session;
    closes = mock_close_count;
    astra_vfs_port_service_release_grant(&host, grant.id);
    for (uint32_t index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index)
        assert(host.reply_sessions[index] != session);
    assert(mock_close_count > closes);
    assert(!astra_vfs_service_session_bound(&service, session, mock_sender,
                                            grant.id));
    assert(astra_vfs_service_session_bound(&service, kept, mock_sender,
                                           other.id));
}

int
main(void)
{
    test_quota_storage_reserves_only_the_representable_table();
    test_a_request_crosses_and_the_reply_is_the_same();
    test_a_dead_peer_is_reported_and_not_waited_on();
    {
        AstraVfsPortService host;
        AstraVfsClient remote;
        mock_reset();
        service_start();
        uint32_t service_handle = mock_open(MOCK_QUEUE_MAX);
        assert(astra_vfs_port_service_init(&host, service_handle, &service));
        served = &host;
        assert(astra_vfs_port_connect(&remote, service_handle) == ASTRA_VFS_OK);
        assert(!astra_vfs_port_client_closed(&remote));
        uint32_t kept_session = remote.session;
        uint32_t opened = service.stats.sessions_opened;
        uint32_t refreshed = astra_vfs_port_refresh(&remote);
        if (refreshed != ASTRA_VFS_OK)
            fprintf(stderr, "refresh status=%u session=%u opened=%u live=%u\n", refreshed, remote.session, service.stats.sessions_opened, service.open_sessions);
        assert(refreshed == ASTRA_VFS_OK);
        assert(remote.session == kept_session);
        assert(service.stats.sessions_opened == opened);
        assert(service.open_sessions == 1u);
        astra_vfs_port_fail(&remote);
        assert(astra_vfs_port_client_closed(&remote));
        astra_vfs_port_fail(&remote);
        assert(astra_vfs_port_client_closed(&remote));
        served = NULL;
    }
    test_clients_own_independent_reply_channels();
    test_lazy_session_admission_preserves_native_owner();
    test_first_operation_binds_complete_operand_transport();
    test_signal_cancellation_does_not_abandon_an_inflight_reply();
    test_host_accelerator_moves_data_plane_out_of_the_service();
    test_grants_hand_out_their_scope_and_only_narrow();
    test_the_kit_derives_on_its_hello_lane();
    test_descriptions_require_a_connected_data_plane();
    test_retiring_a_grant_drops_its_reply_lanes();
    test_hello_does_not_execute_data();
    test_a_scoped_hello_without_its_scope_is_refused();
    test_shared_accelerated_client_keeps_thread_requests_in_flight(0);
    test_shared_accelerated_client_keeps_thread_requests_in_flight(1);
    test_shared_port_client_keeps_thread_requests_in_flight();
    test_shared_lazy_client_opens_one_session_for_two_threads();
    test_two_path_operations_cross_atomically();
    test_a_version_below_the_minimum_is_refused();
    test_bulk_read_crosses_once_through_a_shared_area();
    test_borrowed_read_reuses_only_an_undisturbed_area();
    test_direct_borrowed_read_reuses_only_an_undisturbed_area();
    test_bulk_write_crosses_once_through_a_shared_area();
    test_bulk_append_returns_the_atomic_backend_position();
    test_accelerated_bulk_append_returns_the_atomic_backend_position();
    test_bulk_readdir_crosses_once_through_a_shared_area();
    test_small_path_read_uses_canonical_area();
    test_lazy_bulk_path_read_binds_a_sized_area();
    test_bulk_resources_are_reusable_after_disconnect();
    test_a_dead_client_is_reaped_when_the_session_table_is_full();
    test_a_dead_client_is_reaped_when_the_next_client_connects();
    test_handle_pressure_reaps_a_dead_client_and_retries_receive();
    test_the_service_adopts_the_callers_activity();
    test_a_message_that_is_not_the_protocol_is_refused();
    test_a_zero_session_request_is_refused();
    test_an_answer_with_nowhere_to_go_is_counted();
    test_fork_child_rebinds_lane_owner();
    mock_reset();
    puts("ASTRA VFS PORT PASS");
    return 0;
}
