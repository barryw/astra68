/*
 * The storage protocol across a process boundary.
 *
 * The client sends a request and waits for the reply. The service pump lives
 * in vfs_port_service.c so native services can link it independently.
 *
 * Nothing here emits an event. This is the path the events service's own
 * clients take, so a transport that logged would be logging about logging --
 * and the rule has to be structural rather than careful, which is why this
 * file does not include astra/event_emit.h and must not grow an include of it.
 */

#include <astra/vfs_port_transport.h>
#include <astra/secret_memory.h>

#include <astra/vfs_backend.h>
#include <astra/vfs_path_payload.h>
#include <astra/vfs_reply.h>
#include <astra/vfs_path_storage.h>
#include <astra/vfs_call_storage.h>

#include <astra/bytes.h>

#include <astra/runtime.h>
#include <astra/utf8.h>
#include <astra/syscall.h>
#include <astra/pager_service.h>
#include <astra/address_space.h>

#include <stddef.h>
#include <string.h>

typedef AstraVfsPortThreadState AstraVfsPortCallState;

#ifdef ASTRA_VFS_TEST_HOOKS
void astra_vfs_test_port_lane_lookup(AstraVfsClient *client);
uint32_t astra_vfs_test_mutex_lock(volatile uint32_t *state);
uint32_t astra_vfs_test_mutex_unlock(volatile uint32_t *state);
#define test_port_lane_lookup(client) astra_vfs_test_port_lane_lookup(client)
#define astra_mutex_lock(state) astra_vfs_test_mutex_lock(state)
#define astra_mutex_unlock(state) astra_vfs_test_mutex_unlock(state)
#else
#define test_port_lane_lookup(client) ((void)(client))
#endif

#if !defined(ASTRA_VFS_EMBEDDED_THREAD_STATE)
static _Thread_local AstraVfsPortCallState port_tls;
#endif

void
astra_vfs_port_after_fork_child(AstraVfsClient *client)
{
#if defined(ASTRA_VFS_EMBEDDED_THREAD_STATE)
    if (client != NULL && client->port_thread_states != NULL) {
        memset(client->port_thread_states, 0,
               client->port_thread_capacity *
                   sizeof(*client->port_thread_states));
        client->port_thread_lock = 0u;
    }
#else
    (void)client;
    memset(&port_tls, 0, sizeof(port_tls));
#endif
}

int
astra_vfs_port_set_thread_storage(AstraVfsClient *client,
                                  AstraVfsPortThreadState *states,
                                  uint32_t capacity)
{
    if (client == NULL || states == NULL ||
        capacity != ASTRA_PROCESS_THREAD_SLOT_COUNT ||
        client->transport != NULL)
        return 0;
    memset(states, 0, capacity * sizeof(*states));
    client->port_thread_states = states;
    client->port_thread_capacity = capacity;
    client->port_thread_lock = 0u;
    return 1;
}

static int
thread_gone(uint32_t thread)
{
    uint32_t status = astra_wait_one(thread, 0u, NULL);

    return status == ASTRA_SYSCALL_OK ||
           status == ASTRA_SYSCALL_INVALID_HANDLE ||
           status == ASTRA_SYSCALL_PEER_DEAD ||
           status == ASTRA_SYSCALL_CLOSED;
}

#if defined(ASTRA_VFS_EMBEDDED_THREAD_STATE)
static AstraVfsPortCallState *
port_state(AstraVfsClient *client)
{
    AstraVfsPortCallState *states;
    uint32_t capacity;
    uint32_t owners[ASTRA_PROCESS_THREAD_SLOT_COUNT];
    uint32_t thread = 0u;

    if (client == NULL ||
        astra_query_abi(NULL, NULL, &thread) != ASTRA_SYSCALL_OK ||
        thread == 0u || client->port_thread_states == NULL ||
        client->port_thread_capacity != ASTRA_PROCESS_THREAD_SLOT_COUNT)
        astra_assert_failed(__FILE__, __LINE__,
                            "VFS per-thread call storage unavailable");
    states = client->port_thread_states;
    capacity = client->port_thread_capacity;
    for (;;) {
        if (astra_mutex_lock(&client->port_thread_lock) != ASTRA_SYSCALL_OK)
            astra_assert_failed(__FILE__, __LINE__,
                                "VFS thread-state lock failed");
        for (uint32_t index = 0u; index < capacity; ++index) {
            if (states[index].owner_thread == thread) {
                (void)astra_mutex_unlock(&client->port_thread_lock);
                return &states[index];
            }
        }
        for (uint32_t index = 0u; index < capacity; ++index) {
            if (states[index].owner_thread == 0u) {
                memset(&states[index], 0, sizeof(states[index]));
                states[index].owner_thread = thread;
                (void)astra_mutex_unlock(&client->port_thread_lock);
                return &states[index];
            }
        }
        for (uint32_t index = 0u; index < capacity; ++index)
            owners[index] = states[index].owner_thread;
        (void)astra_mutex_unlock(&client->port_thread_lock);

        for (uint32_t index = 0u; index < capacity; ++index) {
            uint32_t owner = owners[index];
            if (owner == 0u || !thread_gone(owner))
                continue;
            if (astra_mutex_lock(&client->port_thread_lock) !=
                ASTRA_SYSCALL_OK)
                astra_assert_failed(__FILE__, __LINE__,
                                    "VFS thread-state lock failed");
            if (states[index].owner_thread != owner) {
                (void)astra_mutex_unlock(&client->port_thread_lock);
                continue;
            }
            memset(&states[index], 0, sizeof(states[index]));
            states[index].owner_thread = thread;
            (void)astra_mutex_unlock(&client->port_thread_lock);
            return &states[index];
        }
        astra_assert_failed(__FILE__, __LINE__,
                            "VFS thread-state capacity contradicts kernel");
    }
}
#else
static AstraVfsPortCallState *
port_state(AstraVfsClient *client)
{
    if (port_tls.owner_thread == 0u &&
        (astra_query_abi(NULL, NULL, &port_tls.owner_thread) !=
             ASTRA_SYSCALL_OK ||
         port_tls.owner_thread == 0u))
        astra_assert_failed(__FILE__, __LINE__,
                            "VFS current thread unavailable");
    (void)client;
    return &port_tls;
}
#endif

#define port_call (*port_state(client))

static void
lane_scratch_area_close(AstraVfsPortLane *lane)
{
    if (lane->scratch_address != NULL)
        (void)astra_rt_area_unmap(lane->scratch_address);
    if (lane->scratch_area != 0u)
        (void)astra_close(lane->scratch_area);
    lane->scratch_address = NULL;
    lane->scratch_area = lane->scratch_size = 0u;
}

static void
lane_resources_close(AstraVfsPortLane *lane)
{
    if (lane->reply_send != 0u)
        (void)astra_close(lane->reply_send);
    if (lane->reply_source != 0u)
        (void)astra_close(lane->reply_source);
    if (lane->reply_receive != 0u)
        (void)astra_close(lane->reply_receive);
    if (lane->area_send != 0u)
        (void)astra_close(lane->area_send);
    if (lane->area_address != NULL)
        (void)astra_rt_area_unmap(lane->area_address);
    if (lane->area != 0u)
        (void)astra_close(lane->area);
    lane_scratch_area_close(lane);
    memset(lane, 0, sizeof(*lane));
}

static AstraVfsPortLane *
port_lane(AstraVfsClient *client)
{
    uint32_t owners[ASTRA_PROCESS_THREAD_SLOT_COUNT];
    uint32_t thread = port_state(client)->owner_thread;

    test_port_lane_lookup(client);

    for (;;) {
        if (astra_mutex_lock(&client->port_lane_lock) != ASTRA_SYSCALL_OK)
            astra_assert_failed(__FILE__, __LINE__,
                                "VFS lane lock failed");
        for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_SLOT_COUNT;
             ++index) {
            if (client->port_lanes[index].owner_thread == thread) {
                (void)astra_mutex_unlock(&client->port_lane_lock);
                return &client->port_lanes[index];
            }
        }
        for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_SLOT_COUNT;
             ++index) {
            if (client->port_lanes[index].owner_thread == 0u) {
                client->port_lanes[index].owner_thread = thread;
                client->port_lanes[index].session =
                    ASTRA_VFS_SESSION_INVALID;
                (void)astra_mutex_unlock(&client->port_lane_lock);
                return &client->port_lanes[index];
            }
        }
        for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_SLOT_COUNT;
             ++index)
            owners[index] = client->port_lanes[index].owner_thread;
        (void)astra_mutex_unlock(&client->port_lane_lock);

        for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_SLOT_COUNT;
             ++index) {
            AstraVfsPortLane detached;

            if (!thread_gone(owners[index]))
                continue;
            if (astra_mutex_lock(&client->port_lane_lock) !=
                ASTRA_SYSCALL_OK)
                astra_assert_failed(__FILE__, __LINE__,
                                    "VFS lane lock failed");
            if (client->port_lanes[index].owner_thread != owners[index]) {
                (void)astra_mutex_unlock(&client->port_lane_lock);
                continue;
            }
            detached = client->port_lanes[index];
            memset(&client->port_lanes[index], 0,
                   sizeof(client->port_lanes[index]));
            client->port_lanes[index].owner_thread = thread;
            client->port_lanes[index].session = ASTRA_VFS_SESSION_INVALID;
            (void)astra_mutex_unlock(&client->port_lane_lock);
            lane_resources_close(&detached);
            return &client->port_lanes[index];
        }
        astra_assert_failed(__FILE__, __LINE__,
                            "VFS lane capacity contradicts kernel");
    }
}

AstraVfsCallState *
astra_vfs_port_call_acquire(AstraVfsClient *client)
{
    return &port_state(client)->client_call;
}

uint32_t
astra_vfs_port_exec_lane_export(AstraVfsClient *client,
                                AstraVfsPortExecLane *state)
{
    AstraVfsPortLane *lane;

    if (client == NULL || state == NULL)
        return ASTRA_VFS_ERR_INVALID;
    lane = port_lane(client);
    if (astra_mutex_lock(&client->port_lane_lock) != ASTRA_SYSCALL_OK)
        return ASTRA_VFS_ERR_IO;
    *state = (AstraVfsPortExecLane){
        .owner_thread = lane->owner_thread,
        .session = lane->session,
        .reply_receive = lane->reply_receive,
        .reply_source = lane->reply_source,
        .reply_send = lane->reply_send,
        .area = lane->area,
        .area_send = lane->area_send,
        .area_size = lane->area_size,
    };
    (void)astra_mutex_unlock(&client->port_lane_lock);
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_port_exec_lane_import(AstraVfsClient *client,
                                const AstraVfsPortExecLane *state)
{
    AstraVfsPortLane *lane;
    void *address = NULL;
    uint32_t mapped = 0u;

    if (client == NULL || state == NULL || state->owner_thread == 0u ||
        state->owner_thread != port_state(client)->owner_thread ||
        (state->reply_receive == 0u) != (state->reply_source == 0u) ||
        (state->area == 0u) != (state->area_size == 0u))
        return ASTRA_VFS_ERR_INVALID;
    if (state->area != 0u &&
        (astra_rt_area_map(state->area,
                           ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                           &address, &mapped) != ASTRA_SYSCALL_OK ||
         mapped != state->area_size))
        return ASTRA_VFS_ERR_INVALID;
    lane = port_lane(client);
    if (lane->reply_receive != 0u || lane->area != 0u) {
        if (address != NULL)
            (void)astra_rt_area_unmap(address);
        return ASTRA_VFS_ERR_INVALID;
    }
    lane->session = state->session;
    lane->reply_receive = state->reply_receive;
    lane->reply_source = state->reply_source;
    lane->reply_send = state->reply_send;
    lane->area = state->area;
    lane->area_send = state->area_send;
    lane->area_address = address;
    lane->area_size = state->area_size;
    return ASTRA_VFS_OK;
}

/*
 * One reply at a time, one message deep. The protocol is synchronous, so a
 * deeper reply port would be capacity nothing can use.
 */
#define VFS_PORT_REPLY_MESSAGES 1u

static void
port_client_handles_reset(AstraVfsClient *client)
{
    for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_SLOT_COUNT;
         ++index)
        lane_resources_close(&client->port_lanes[index]);
    if (client->port_connect_lock != 0u)
        (void)astra_close(client->port_connect_lock);
    client->port_area_capable = 0u;
    client->port_connect_lock = 0u;
    client->port_lane_lock = 0u;
}

enum {
    PORT_CLIENT_OPEN = 0u,
    PORT_CLIENT_DRAINING,
    PORT_CLIENT_FAILED,
    PORT_CLIENT_CLEANING,
    PORT_CLIENT_CLOSED,
    PORT_CLIENT_REFRESHING
};

static uint32_t
port_client_session(const AstraVfsClient *client)
{
    return atomic_load_explicit(&client->session, memory_order_acquire);
}

static void
port_client_publish_session(AstraVfsClient *client, uint32_t session,
                            uint16_t version, uint32_t features)
{
    client->version = version;
    client->features = features;
    atomic_store_explicit(&client->session, session, memory_order_release);
}

static void
port_client_finish(AstraVfsClient *client)
{
    uint32_t state = __atomic_load_n(&client->port_lifecycle,
                                     __ATOMIC_ACQUIRE);

    while (state == PORT_CLIENT_DRAINING || state == PORT_CLIENT_FAILED) {
        if (__atomic_compare_exchange_n(
                &client->port_lifecycle, &state, PORT_CLIENT_CLEANING, 0,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            break;
    }
    if (state != PORT_CLIENT_DRAINING && state != PORT_CLIENT_FAILED)
        return;
    port_client_handles_reset(client);
    atomic_store_explicit(&client->session, ASTRA_VFS_SESSION_INVALID,
                          memory_order_release);
    __atomic_store_n(&client->port_lifecycle, PORT_CLIENT_CLOSED,
                     __ATOMIC_RELEASE);
    (void)astra_futex_wake(&client->port_lifecycle, UINT32_MAX, NULL);
}

uint32_t
astra_vfs_port_client_enter(AstraVfsClient *client)
{
    uint32_t state;

retry:
    state = __atomic_load_n(&client->port_lifecycle, __ATOMIC_ACQUIRE);
    while (state == PORT_CLIENT_REFRESHING) {
        (void)astra_futex_wait(&client->port_lifecycle, state, ASTRA_DEADLINE_FOREVER);
        state = __atomic_load_n(&client->port_lifecycle, __ATOMIC_ACQUIRE);
    }
    if (state != PORT_CLIENT_OPEN) {
        (void)astra_log_failure("VFS port lifecycle", state);
        return ASTRA_VFS_ERR_PEER;
    }
    (void)__atomic_add_fetch(&client->port_inflight, 1u, __ATOMIC_ACQ_REL);
    if (__atomic_load_n(&client->port_lifecycle, __ATOMIC_ACQUIRE) ==
        PORT_CLIENT_OPEN) {
        port_state(client)->borrow_valid = 0u;
        return ASTRA_VFS_OK;
    }
    if (__atomic_sub_fetch(&client->port_inflight, 1u, __ATOMIC_ACQ_REL) == 0u &&
        __atomic_load_n(&client->port_inflight_waiters,
                        __ATOMIC_ACQUIRE) != 0u)
        (void)astra_futex_wake(&client->port_inflight, UINT32_MAX, NULL);
    state = __atomic_load_n(&client->port_lifecycle, __ATOMIC_ACQUIRE);
    if (state == PORT_CLIENT_REFRESHING)
        goto retry;
    (void)astra_log_failure("VFS port lifecycle changed", state);
    return ASTRA_VFS_ERR_PEER;
}

void
astra_vfs_port_client_leave(AstraVfsClient *client)
{
    if (__atomic_sub_fetch(&client->port_inflight, 1u, __ATOMIC_ACQ_REL) != 0u)
        return;
    if (__atomic_load_n(&client->port_inflight_waiters,
                        __ATOMIC_ACQUIRE) != 0u)
        (void)astra_futex_wake(&client->port_inflight, UINT32_MAX, NULL);
    if (__atomic_load_n(&client->port_lifecycle, __ATOMIC_ACQUIRE) ==
        PORT_CLIENT_FAILED)
        port_client_finish(client);
}

static void
port_client_wait_idle(AstraVfsClient *client)
{
    uint32_t active;

    while ((active = __atomic_load_n(&client->port_inflight,
                                     __ATOMIC_ACQUIRE)) != 0u) {
        (void)__atomic_add_fetch(&client->port_inflight_waiters, 1u,
                                 __ATOMIC_ACQ_REL);
        active = __atomic_load_n(&client->port_inflight, __ATOMIC_ACQUIRE);
        if (active != 0u)
            (void)astra_futex_wait(&client->port_inflight, active,
                                   ASTRA_DEADLINE_FOREVER);
        (void)__atomic_sub_fetch(&client->port_inflight_waiters, 1u,
                                 __ATOMIC_ACQ_REL);
    }
}

static int
port_client_begin_drain(AstraVfsClient *client)
{
    uint32_t expected;

    for (;;) {
        expected = PORT_CLIENT_OPEN;
        if (__atomic_compare_exchange_n(
                &client->port_lifecycle, &expected, PORT_CLIENT_DRAINING, 0,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            break;
        if (expected != PORT_CLIENT_REFRESHING)
            return 0;
        (void)astra_futex_wait(&client->port_lifecycle, expected, ASTRA_DEADLINE_FOREVER);
    }
    port_client_wait_idle(client);
    return 1;
}

static void
port_client_fail(AstraVfsClient *client)
{
    uint32_t state = __atomic_load_n(&client->port_lifecycle,
                                     __ATOMIC_ACQUIRE);

    while (state == PORT_CLIENT_OPEN || state == PORT_CLIENT_DRAINING ||
           state == PORT_CLIENT_REFRESHING) {
        if (__atomic_compare_exchange_n(
                &client->port_lifecycle, &state, PORT_CLIENT_FAILED, 0,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            break;
    }
    if (state != PORT_CLIENT_OPEN && state != PORT_CLIENT_DRAINING &&
        state != PORT_CLIENT_REFRESHING)
        return;
    /* Closing receive capabilities wakes every lane blocked on a dead peer.
     * Mappings remain valid until the last in-flight caller leaves. */
    for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_SLOT_COUNT;
         ++index)
        if (client->port_lanes[index].reply_receive != 0u)
            (void)astra_close(client->port_lanes[index].reply_receive);
    (void)astra_futex_wake(&client->port_lifecycle, UINT32_MAX, NULL);
}

void astra_vfs_port_fail(AstraVfsClient *client)
{
    if (client == NULL)
        return;
    port_client_fail(client);
    if (__atomic_load_n(&client->port_inflight, __ATOMIC_ACQUIRE) == 0u)
        port_client_finish(client);
}

int astra_vfs_port_client_closed(const AstraVfsClient *client)
{
    return client != NULL &&
        __atomic_load_n(&client->port_lifecycle, __ATOMIC_ACQUIRE) ==
            PORT_CLIENT_CLOSED;
}

static void
port_client_close(AstraVfsClient *client)
{
    uint32_t state;

    if (port_client_begin_drain(client)) {
        port_client_finish(client);
        return;
    }
    state = __atomic_load_n(&client->port_lifecycle, __ATOMIC_ACQUIRE);
    while (state == PORT_CLIENT_FAILED || state == PORT_CLIENT_CLEANING) {
        if (state == PORT_CLIENT_FAILED) {
            port_client_wait_idle(client);
            port_client_finish(client);
        } else {
            (void)astra_futex_wait(&client->port_lifecycle, state,
                                   ASTRA_DEADLINE_FOREVER);
        }
        state = __atomic_load_n(&client->port_lifecycle, __ATOMIC_ACQUIRE);
    }
}

void
astra_vfs_port_abandon(AstraVfsClient *client)
{
    if (client == NULL)
        return;
    port_client_close(client);
}

static uint32_t
reply_channel(AstraVfsPortLane *lane)
{
    if (lane->reply_receive != 0u)
        return ASTRA_SYSCALL_OK;
    return astra_rt_port_create(VFS_PORT_REPLY_MESSAGES,
                                (uint32_t)sizeof(AstraVfsReplyMessage),
                                &lane->reply_receive,
                                &lane->reply_source);
}

static uint32_t
duplicate_reply_send(AstraVfsPortLane *lane, uint32_t *duplicate)
{
    uint32_t status = reply_channel(lane);

    if (status != ASTRA_SYSCALL_OK)
        return status;
    status = astra_rt_handle_duplicate(lane->reply_source,
                                    ASTRA_RIGHT_SIGNAL |
                                        ASTRA_RIGHT_WAIT |
                                        ASTRA_RIGHT_TRANSFER,
                                    duplicate);
    if (status != ASTRA_SYSCALL_INVALID_HANDLE)
        return status;

    /* A test reset or a closed channel: recreate once, then report honestly. */
    (void)astra_close(lane->reply_source);
    (void)astra_close(lane->reply_receive);
    lane->reply_source = 0u;
    lane->reply_receive = 0u;
    status = reply_channel(lane);
    return status == ASTRA_SYSCALL_OK ?
        astra_rt_handle_duplicate(lane->reply_source,
                               ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT |
                                   ASTRA_RIGHT_TRANSFER,
                               duplicate) : status;
}

uint32_t
astra_vfs_port_connect(AstraVfsClient *client, uint32_t service)
{
    uint32_t status;

    if (client == NULL || service == 0u)
        return ASTRA_VFS_ERR_INVALID;
    client->port_service = service;
    client->description_acquire = NULL;
    client->description_release = NULL;
    client->ownership_epoch = 0u;
    client->features = 0u;
    memset(client->port_lanes, 0, sizeof(client->port_lanes));
    client->port_lane_lock = 0u;
    client->port_connect_lock = 0u;
    client->port_inflight = 0u;
    client->port_inflight_waiters = 0u;
    client->port_lifecycle = PORT_CLIENT_OPEN;
    if (astra_rt_semaphore_create(
            1u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
            &client->port_connect_lock) != ASTRA_SYSCALL_OK)
        return ASTRA_VFS_ERR_LIMIT;
    status = astra_vfs_connect(client, astra_vfs_port_transport, client);
    if (status != ASTRA_VFS_OK) {
        port_client_close(client);
    } else {
        client->port_area_capable = 1u;
        client->area_payload = astra_vfs_port_call_area;
        client->call_acquire = astra_vfs_port_call_acquire;
    }
    return status;
}

uint32_t
astra_vfs_client_connect_service(AstraVfsClient *client, uint32_t service)
{
    if (client == NULL || service == 0u)
        return ASTRA_VFS_ERR_INVALID;
    client->transport = astra_vfs_port_transport;
    client->context = client;
    client->description_acquire = NULL;
    client->description_release = NULL;
    client->ownership_epoch = 0u;
    atomic_store_explicit(&client->session, ASTRA_VFS_SESSION_INVALID,
                          memory_order_relaxed);
    client->version = ASTRA_VFS_VERSION;
    client->port_area_capable = 1u;
    client->port_service = service;
    client->features = 0u;
    memset(client->port_lanes, 0, sizeof(client->port_lanes));
    client->port_lane_lock = 0u;
    client->port_connect_lock = 0u;
    client->port_inflight = 0u;
    client->port_inflight_waiters = 0u;
    client->port_lifecycle = PORT_CLIENT_OPEN;
    if (astra_rt_semaphore_create(
            1u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
            &client->port_connect_lock) != ASTRA_SYSCALL_OK)
        return ASTRA_VFS_ERR_LIMIT;
    client->area_payload = astra_vfs_port_call_area;
    client->call_acquire = astra_vfs_port_call_acquire;
    return ASTRA_VFS_OK;
}

static uint32_t
vfs_port_transport_call(void *context, uint32_t operation,
                        const AstraVfsRequest *request, const AstraVfsTransfer *transfer, AstraVfsReply *reply,
                        AstraVfsPortLane *selected_lane);

static uint32_t ensure_area_size(AstraVfsClient *client, uint32_t needed);

/* Admit an already constructed client without resetting its shared dispatch or
 * owner hooks. Another thread may already be preparing an independent call. */
static uint32_t connect_session(AstraVfsClient *client)
{
    AstraVfsPortCallState *call = port_state(client);
    uint32_t status;

    memset(&call->request, 0, sizeof(call->request));
    call->request.size = ASTRA_VFS_REQUEST_SIZE;
    call->request.version = ASTRA_VFS_VERSION;
    call->request.session = ASTRA_VFS_SESSION_INVALID;
    call->request.activity = client->activity;
    status = vfs_port_transport_call(client, ASTRA_VFS_OP_HELLO,
                                      &call->request, NULL, &call->reply, NULL);
    return status == ASTRA_VFS_OK ? call->reply.status : status;
}

static uint32_t
ensure_thread_lane(AstraVfsClient *client, AstraVfsPortLane *lane)
{
    AstraVfsPortCallState *call = port_state(client);
    uint32_t status;

    if (lane->session == port_client_session(client))
        return ASTRA_VFS_OK;
    memset(&call->area_request, 0, sizeof(call->area_request));
    call->area_request.size = ASTRA_VFS_REQUEST_SIZE;
    call->area_request.version = client->version;
    call->area_request.session = port_client_session(client);
    call->area_request.activity = client->activity;
    status = vfs_port_transport_call(client, ASTRA_VFS_OP_BIND_LANE,
                                      &call->area_request, NULL, &call->reply, lane);
    if (status != ASTRA_VFS_OK)
        return status;
    if (call->reply.status != ASTRA_VFS_OK)
        return call->reply.status;
    lane->session = port_client_session(client);
    return ASTRA_VFS_OK;
}

static uint32_t
ensure_connection(AstraVfsClient *client)
{
    uint32_t status;

    if (port_client_session(client) != ASTRA_VFS_SESSION_INVALID)
        return ASTRA_VFS_OK;
    if (client->port_connect_lock == 0u) {
        (void)astra_log_failure("VFS port connect lock", 0u);
        return ASTRA_VFS_ERR_PEER;
    }
    status = astra_wait_one_restart(client->port_connect_lock,
                                    ASTRA_DEADLINE_FOREVER, NULL);
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_log_failure("VFS port connect wait", status);
        return ASTRA_VFS_ERR_PEER;
    }
    if (port_client_session(client) == ASTRA_VFS_SESSION_INVALID) {
        status = connect_session(client);
    } else {
        status = ASTRA_VFS_OK;
    }
    uint32_t signalled = astra_rt_signal(client->port_connect_lock, 1u, NULL);
    if (signalled != ASTRA_SYSCALL_OK) {
        (void)astra_log_failure("VFS port connect signal", signalled);
        return ASTRA_VFS_ERR_PEER;
    }
    return status;
}

static uint32_t
ensure_scratch_area(AstraVfsClient *client, uint32_t needed)
{
    AstraVfsPortLane *lane = port_lane(client);
    void *address = NULL;
    uint32_t area = 0u;
    uint32_t size = 0u;
    uint32_t status;

    if (needed == 0u || needed > ASTRA_VFS_BULK_MAX)
        return ASTRA_VFS_ERR_LIMIT;
    if (lane->scratch_address != NULL && lane->scratch_size >= needed)
        return ASTRA_VFS_OK;
    status = astra_rt_area_create(
        needed, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP,
        &area);
    if (status != ASTRA_SYSCALL_OK)
        return status == ASTRA_SYSCALL_RESOURCE_LIMIT ? ASTRA_VFS_ERR_LIMIT :
                                                       ASTRA_VFS_ERR_IO;
    status = astra_rt_area_map(area,
                              ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                              &address, &size);
    if (status != ASTRA_SYSCALL_OK || address == NULL || size < needed) {
        if (address != NULL)
            (void)astra_rt_area_unmap(address);
        (void)astra_close(area);
        return status == ASTRA_SYSCALL_RESOURCE_LIMIT ? ASTRA_VFS_ERR_LIMIT :
                                                       ASTRA_VFS_ERR_IO;
    }
    lane_scratch_area_close(lane);
    lane->scratch_area = area;
    lane->scratch_address = address;
    lane->scratch_size = size;
    return ASTRA_VFS_OK;
}

const uint8_t *
astra_vfs_port_call_area(const AstraVfsClient *client, uint32_t *capacity)
{
    AstraVfsPortLane *lane;

    if (capacity != NULL)
        *capacity = 0u;
    if (client == NULL)
        return NULL;
    lane = port_lane((AstraVfsClient *)client);
    if (capacity != NULL)
        *capacity = lane->area_size;
    return lane->area_address;
}

void astra_vfs_port_wipe_area(AstraVfsClient *client, uint32_t bytes)
{
    AstraVfsPortLane *lane;

    if (client == NULL)
        return;
    lane = port_lane(client);
    if (lane->area_address != NULL) {
        if (bytes > lane->area_size)
            bytes = lane->area_size;
        astra_secret_wipe(lane->area_address, bytes);
    }
}

static uint32_t request_message_size(uint32_t operation,
    const AstraVfsRequest *request)
{
    (void)operation;
    return request->size == ASTRA_VFS_REQUEST_SIZE ?
        (uint32_t)sizeof(AstraVfsRequestMessage) : 0u;
}

static AstraVfsPortLane *
port_session_lane(AstraVfsClient *client)
{
    AstraVfsPortLane *lane = NULL;

    if (astra_mutex_lock(&client->port_lane_lock) != ASTRA_SYSCALL_OK)
        return NULL;
    for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_SLOT_COUNT;
         ++index)
        if (client->port_lanes[index].session ==
                port_client_session(client) &&
            client->port_lanes[index].reply_receive != 0u) {
            lane = &client->port_lanes[index];
            break;
        }
    (void)astra_mutex_unlock(&client->port_lane_lock);
    return lane;
}

uint32_t astra_vfs_port_refresh(AstraVfsClient *client)
{
    AstraVfsPortLane *lane;
    AstraVfsRequest request = {0};
    AstraVfsReply reply;
    uint32_t expected = PORT_CLIENT_OPEN;
    uint32_t status;

    if (!__atomic_compare_exchange_n(&client->port_lifecycle, &expected,
            PORT_CLIENT_REFRESHING, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        while (expected == PORT_CLIENT_REFRESHING) {
            (void)astra_futex_wait(&client->port_lifecycle, expected, ASTRA_DEADLINE_FOREVER);
            expected = __atomic_load_n(&client->port_lifecycle, __ATOMIC_ACQUIRE);
        }
        return expected == PORT_CLIENT_OPEN ? ASTRA_VFS_OK : ASTRA_VFS_ERR_LOST;
    }
    port_client_wait_idle(client);
    if (__atomic_load_n(&client->port_lifecycle, __ATOMIC_ACQUIRE) != PORT_CLIENT_REFRESHING)
        return ASTRA_VFS_ERR_LOST;
    request.size = ASTRA_VFS_REQUEST_SIZE;
    request.version = client->version;
    request.session = port_client_session(client);
    request.activity = client->activity;
    lane = request.session == ASTRA_VFS_SESSION_INVALID ? port_lane(client) : port_session_lane(client);
    status = lane == NULL ? ASTRA_VFS_ERR_BAD_HANDLE :
        vfs_port_transport_call(client, ASTRA_VFS_OP_HELLO, &request, NULL, &reply, lane);
    if (status == ASTRA_VFS_OK)
        status = reply.status;
    expected = PORT_CLIENT_REFRESHING;
    if (!__atomic_compare_exchange_n(&client->port_lifecycle, &expected,
            PORT_CLIENT_OPEN, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        port_client_finish(client);
        status = ASTRA_VFS_ERR_LOST;
    }
    (void)astra_futex_wake(&client->port_lifecycle, UINT32_MAX, NULL);
    return astra_vfs_storage_status(status);
}

uint32_t
astra_vfs_port_transport(void *context, uint32_t operation,
                         const AstraVfsRequest *request, const AstraVfsTransfer *transfer, AstraVfsReply *reply)
{
    AstraVfsClient *client = context;
    uint32_t status;

    if (client == NULL || request == NULL || reply == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (operation == ASTRA_VFS_OP_BYE) {
        AstraVfsPortLane *lane;

        if (!port_client_begin_drain(client))
            return ASTRA_VFS_ERR_PEER;
        lane = port_session_lane(client);
        status = lane == NULL ? ASTRA_VFS_ERR_BAD_HANDLE :
            vfs_port_transport_call(context, operation, request, transfer, reply, lane);
        port_call.request.flags = 0u;
        port_client_finish(client);
        return status;
    }
    status = astra_vfs_port_client_enter(client);
    if (status != ASTRA_VFS_OK)
        return status;
    status = vfs_port_transport_call(context, operation, request, transfer, reply,
                                     NULL);
    port_call.request.flags = 0u;
    astra_vfs_port_client_leave(client);
    return status;
}

static uint32_t
vfs_port_transport_call(void *context, uint32_t operation,
                        const AstraVfsRequest *request, const AstraVfsTransfer *transfer, AstraVfsReply *reply,
                        AstraVfsPortLane *selected_lane)
{
    AstraVfsClient *client = context;
    AstraVfsRequest saved_request;
    AstraVfsTransfer saved_transfer = transfer == NULL ? (AstraVfsTransfer){0} : *transfer;
    AstraVfsPortLane *lane;
    AstraVfsRequestMessage *outgoing;
    AstraVfsRequest *wire_request;
    AstraMessageHeader *wire_header;
    AstraVfsReplyMessage *incoming;
    uint32_t handles[1] = {0u};
    uint32_t handle_count = 0u;
    uint32_t reply_handles[1] = {0u};
    uint32_t reply_handle_count = 0u;
    uint32_t size = 0u;
    uint32_t status;
    uint32_t wire_operation = operation;
    uint32_t message_size;


    if (client == NULL || client->port_service == 0u || request == NULL ||
        reply == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    saved_request = *request;
    request = &saved_request;
    transfer = &saved_transfer;
    lane = selected_lane;
    if (operation != ASTRA_VFS_OP_HELLO &&
        port_client_session(client) == ASTRA_VFS_SESSION_INVALID) {
        status = ensure_connection(client);
        if (status != ASTRA_VFS_OK) return status;
        saved_request.session = port_client_session(client);
        saved_request.version = client->version;
    }
    outgoing = &port_call.outgoing;
    incoming = &port_call.incoming;
    message_size = request_message_size(operation, request);
    if (message_size == 0u) return ASTRA_VFS_ERR_INVALID;
    if (operation != ASTRA_VFS_OP_HELLO && operation != ASTRA_VFS_OP_BIND_LANE) {
        if (lane == NULL) lane = port_lane(client);
        status = ensure_thread_lane(client, lane);
        if (status != ASTRA_VFS_OK) return status;
    }
    if (lane == NULL)
        lane = port_lane(client);
    if (transfer->input_size != 0u || transfer->output_capacity != 0u) {
        uint32_t needed = transfer->input_size > transfer->output_capacity ?
            transfer->input_size : transfer->output_capacity;
        if (transfer->input_size != 0u && transfer->input == NULL)
            return ASTRA_VFS_ERR_INVALID;
        status = ensure_area_size(client, needed);
        if (status != ASTRA_VFS_OK)
            return status;
        if (lane->area_address == NULL || lane->area_size < needed)
            return ASTRA_VFS_ERR_LIMIT;
        if (transfer->input_size != 0u)
            memcpy(lane->area_address, transfer->input, transfer->input_size);
    }
    /* Bind each reply lane and transfer area once to its exact session. */
    if (operation == ASTRA_VFS_OP_BIND_AREA) {
        if (lane->area_send == 0u)
            return ASTRA_VFS_ERR_BAD_HANDLE;
        handles[0] = lane->area_send;
        handle_count = 1u;
    } else if (wire_operation == ASTRA_VFS_OP_HELLO ||
               wire_operation == ASTRA_VFS_OP_BIND_LANE) {
        status = duplicate_reply_send(lane, &lane->reply_send);
        if (status != ASTRA_SYSCALL_OK)
            return status == ASTRA_SYSCALL_RESOURCE_LIMIT ?
                ASTRA_VFS_ERR_LIMIT : ASTRA_VFS_ERR_BAD_HANDLE;
        handles[0] = lane->reply_send;
        handle_count = 1u;
    }
    outgoing->request = *request;
    wire_header = &outgoing->header;
    wire_request = &outgoing->request;
    if (operation != ASTRA_VFS_OP_HELLO) {
        wire_request->session = port_client_session(client);
        wire_request->version = client->version;
    }
    astra_message_header_set(wire_header, message_size, ASTRA_VFS_PROTOCOL,
        ASTRA_VFS_VERSION, wire_operation, lane->owner_thread);

    status = astra_port_send(client->port_service, outgoing,
                             message_size, handles, handle_count);
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_log_failure("VFS port send", status);
        if (operation == ASTRA_VFS_OP_BIND_AREA) {
            if (lane->area_send != 0u)
                (void)astra_close(lane->area_send);
            lane->area_send = 0u;
        } else if (handle_count != 0u) {
            port_client_fail(client);
        }
        /*
         * Which failure it was, because they call for different things. A full
         * port is a service that is behind and a caller may try again; a
         * refused message is this client's own mistake; anything else is a
         * service that is not there. Collapsing all three into "peer dead" is
         * what made a wrong message size look like a missing service, and cost
         * an afternoon finding out which.
         */
        if (status == ASTRA_SYSCALL_WOULD_BLOCK) {
            return ASTRA_VFS_ERR_BUSY;
        }
        if (status == ASTRA_SYSCALL_INVALID_ARGUMENT ||
            status == ASTRA_SYSCALL_BAD_ADDRESS) {
            return ASTRA_VFS_ERR_INVALID;
        }
        if (status == ASTRA_SYSCALL_INVALID_HANDLE) {
            return ASTRA_VFS_ERR_BAD_HANDLE;
        }
        /*
         * The service is gone. This is the case the whole return value exists
         * for, and splitting the three above out of it left it falling into
         * the default -- so a caller whose service had died was told
         * ASTRA_VFS_ERR_NOT_FOUND, which to anything above here means the file
         * is not there rather than that nobody is. The receive path in this
         * same file has always answered ASTRA_VFS_ERR_PEER for it.
         */
        if (status == ASTRA_SYSCALL_PEER_DEAD ||
            status == ASTRA_SYSCALL_CLOSED) {
            port_client_fail(client);
            return ASTRA_VFS_ERR_PEER;
        }
        return ASTRA_VFS_ERR_NOT_FOUND;
    }
    if (handle_count != 0u) {
        if (operation == ASTRA_VFS_OP_BIND_AREA)
            lane->area_send = 0u;
        else
            lane->reply_send = 0u; /* moved into the service message */
    }
    /*
     * The request cannot be answered until this thread lets the service run.
     * Peer closure wakes the wait; elapsed time does not prove peer death.
     */
    status = astra_wait_one_restart(lane->reply_receive,
                                    ASTRA_DEADLINE_FOREVER, NULL);
    if (status != ASTRA_SYSCALL_OK)
        (void)astra_log_failure("VFS port reply wait", status);
    if (status == ASTRA_SYSCALL_OK) {
        status = astra_port_receive(lane->reply_receive, incoming,
                                    sizeof(*incoming), reply_handles, 1u,
                                    &size, &reply_handle_count);
        if (status != ASTRA_SYSCALL_OK)
            (void)astra_log_failure("VFS port reply receive", status);
    }
    if (status != ASTRA_SYSCALL_OK) {
        port_client_fail(client);
        return ASTRA_VFS_ERR_PEER;
    }

    /*
     * A reply that is not this protocol is not an answer. It cannot be
     * forwarded to the caller as a status, because the caller would read a
     * field nobody filled in.
     */
    /* Only explicit capability operations may attach a reply handle. */
    if (size != sizeof(*incoming) || reply_handle_count > 1u ||
        (reply_handle_count != 0u &&
         !(wire_operation == ASTRA_VFS_OP_GRANT_DERIVE ||
           wire_operation == ASTRA_VFS_OP_FILE_AREA)) ||
        incoming->header.protocol != ASTRA_VFS_PROTOCOL ||
        incoming->header.operation != wire_operation ||
        incoming->header.transaction_id != lane->owner_thread ||
        !astra_vfs_reply_valid(&incoming->reply) ||
        (reply_handle_count != 0u && incoming->reply.status != ASTRA_VFS_OK) ||
        (wire_operation == ASTRA_VFS_OP_HELLO &&
         incoming->reply.status == ASTRA_VFS_OK &&
         incoming->reply.session == ASTRA_VFS_SESSION_INVALID)) {
        port_client_fail(client);
        if (reply_handle_count != 0u)
            (void)astra_close(reply_handles[0]);
        return ASTRA_VFS_ERR_PROTOCOL;
    }
    *reply = incoming->reply;
    if (reply_handle_count == 1u &&
        (wire_operation == ASTRA_VFS_OP_GRANT_DERIVE ||
         wire_operation == ASTRA_VFS_OP_FILE_AREA)) {
        port_state(client)->received_handle = reply_handles[0];
    }
    if (operation == ASTRA_VFS_OP_HELLO &&
        reply->status == ASTRA_VFS_OK &&
        reply->session != ASTRA_VFS_SESSION_INVALID) {
        port_client_publish_session(client, reply->session, reply->version,
                                    reply->features);
        lane->session = reply->session;
    }
    if (transfer->output != NULL &&
        (reply->status == ASTRA_VFS_OK || reply->status == ASTRA_VFS_ERR_ESCAPED)) {
        uint32_t output_count = reply->count;
        if (operation == ASTRA_VFS_OP_READDIR)
            ++output_count; /* include the entry terminator */
        if (output_count > transfer->output_capacity || output_count > lane->area_size)
            return ASTRA_VFS_ERR_PROTOCOL;
        memcpy(transfer->output, lane->area_address, output_count);
    }
    return ASTRA_VFS_OK;
}

/* Capability replies share admission with lazy connection and feature checks.
 * The returned handle is owned by the caller even on error. */
static uint32_t
port_control_handle(AstraVfsClient *client, uint32_t operation,
    const AstraVfsRequest *request, const AstraVfsTransfer *transfer,
    AstraVfsReply *reply, uint32_t feature, uint32_t *handle)
{
    uint32_t status = astra_vfs_port_client_enter(client);
    if (status != ASTRA_VFS_OK) return status;
    status = ensure_connection(client);
    if (status == ASTRA_VFS_OK && (client->features & feature) == 0u)
        status = ASTRA_VFS_ERR_UNSUPPORTED;
    if (status != ASTRA_VFS_OK) {
        astra_vfs_port_client_leave(client);
        return status;
    }
    status = astra_wait_one_restart(client->port_connect_lock,
        ASTRA_DEADLINE_FOREVER, NULL);
    if (status != ASTRA_SYSCALL_OK) {
        astra_vfs_port_client_leave(client);
        return ASTRA_VFS_ERR_LOST;
    }
    port_state(client)->received_handle = 0u;
    status = vfs_port_transport_call(client, operation, request, transfer, reply, NULL);
    *handle = port_state(client)->received_handle;
    port_state(client)->received_handle = 0u;
    (void)astra_rt_signal(client->port_connect_lock, 1u, NULL);
    astra_vfs_port_client_leave(client);
    return status;
}

/* A narrower grant of the connected client; control plane only. */
uint32_t
astra_vfs_grant_derive(AstraVfsClient *client, const char *root,
                       uint32_t rights, uint32_t *handle)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t length;
    uint32_t status;
    AstraVfsTransfer transfer = {0};

    if (handle != NULL)
        *handle = 0u;
    if (client == NULL || root == NULL || handle == NULL ||
        (rights & ~(ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE |
                    ASTRA_VFS_GRANT_DELEGATE)) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_vfs_path_measure(root, &length);
    if (status != ASTRA_VFS_OK)
        return status;
    /* Only a port serves grants; a lane client connects on first use. */
    if (client->transport != astra_vfs_port_transport)
        return ASTRA_VFS_ERR_UNSUPPORTED;
    memset(&request, 0, sizeof(request));
    request.size = ASTRA_VFS_REQUEST_SIZE;
    request.version = ASTRA_VFS_VERSION;
    request.activity = client->activity;
    request.flags = rights;
    request.body.paths.first_length = length + 1u;
    transfer.input = root;
    transfer.input_size = length + 1u;
    status = port_control_handle(client, ASTRA_VFS_OP_GRANT_DERIVE,
                                 &request, &transfer, &reply,
                                 ASTRA_VFS_FEATURE_GRANTS, handle);
    if (status == ASTRA_VFS_OK)
        status = reply.status;
    if (status != ASTRA_VFS_OK) {
        if (*handle != 0u)
            (void)astra_close(*handle);
        *handle = 0u;
        return astra_vfs_storage_status(status);
    }
    return *handle != 0u ? ASTRA_VFS_OK : ASTRA_VFS_ERR_PROTOCOL;
}

uint32_t
astra_vfs_file_area(AstraVfsClient *client, AstraVfsFile file,
    uint64_t base, uint32_t span, uint32_t *area, uint64_t *file_size)
{
    AstraVfsRequest request = {0};
    AstraVfsReply reply = {0};
    uint32_t status;

    if (area != NULL) *area = 0u;
    if (file_size != NULL) *file_size = 0u;
    if (client == NULL || file == ASTRA_VFS_FILE_INVALID ||
        area == NULL || file_size == NULL || span == 0u ||
        span > ASTRA_AREA_SIZE_MAX ||
        (base & (ASTRA_MEMORY_PAGE_SIZE - 1u)) != 0u ||
        (span & (ASTRA_MEMORY_PAGE_SIZE - 1u)) != 0u ||
        span > ASTRA_AREA_SIZE_MAX - base % ASTRA_AREA_SIZE_MAX ||
        base > UINT64_MAX - span ||
        client->transport != astra_vfs_port_transport)
        return ASTRA_VFS_ERR_INVALID;
    request.size = ASTRA_VFS_REQUEST_SIZE;
    request.version = ASTRA_VFS_VERSION;
    request.file = file;
    request.activity = client->activity;
    request.write_limit = base;
    request.length = span;
    status = port_control_handle(client, ASTRA_VFS_OP_FILE_AREA,
                                 &request, NULL, &reply,
                                 ASTRA_VFS_FEATURE_FILE_AREAS, area);
    if (status == ASTRA_VFS_OK)
        status = reply.status;
    if (status != ASTRA_VFS_OK || *area == 0u) {
        if (*area != 0u) (void)astra_close(*area);
        *area = 0u;
        return astra_vfs_storage_status(status == ASTRA_VFS_OK ?
            ASTRA_VFS_ERR_PROTOCOL : status);
    }
    *file_size = reply.node_size;
    return ASTRA_VFS_OK;
}

static uint32_t
port_cache_available(AstraVfsClient *client)
{
    if (client->transport != astra_vfs_port_transport)
        return ASTRA_VFS_ERR_UNSUPPORTED;
    uint32_t status = astra_vfs_port_client_enter(client);
    if (status != ASTRA_VFS_OK) return astra_vfs_storage_status(status);
    status = ensure_connection(client);
    if (status == ASTRA_VFS_OK && (client->features & ASTRA_VFS_FEATURE_FILE_AREAS) == 0u)
        status = ASTRA_VFS_ERR_UNSUPPORTED;
    astra_vfs_port_client_leave(client);
    return astra_vfs_storage_status(status);
}

uint32_t
astra_vfs_port_read_cached(AstraVfsClient *client, AstraVfsFile file,
    uint64_t offset, void *buffer, uint32_t length, uint32_t *moved)
{
    AstraVfsDirEntry info = {0};
    uint32_t area = 0u, copied = 0u, status, wanted;
    uint64_t owner_base = UINT64_MAX, size = 0u;

    if (client == NULL || buffer == NULL || moved == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *moved = 0u;
    status = port_cache_available(client);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_vfs_stat_file_meta(client, file, &info);
    if (status != ASTRA_VFS_OK) return status;
    if (info.kind != ASTRA_VFS_KIND_FILE)
        return ASTRA_VFS_ERR_UNSUPPORTED;
    if (offset >= info.size || length == 0u) {
        uint32_t flags = 0u;
        status = astra_vfs_file_flags(client, file, 0u, 0u, &flags);
        return status != ASTRA_VFS_OK ? status :
            (flags & ASTRA_VFS_OPEN_READ) != 0u ? ASTRA_VFS_OK :
                                                ASTRA_VFS_ERR_ACCESS;
    }
    wanted = info.size - offset < length ? (uint32_t)(info.size - offset) : length;
    while (copied < wanted) {
        uint64_t position = offset + copied;
        uint64_t base = position - position % ASTRA_AREA_SIZE_MAX;
        uint32_t within = (uint32_t)(position - base);
        uint32_t page = within - within % ASTRA_MEMORY_PAGE_SIZE;
        uint32_t chunk = ASTRA_MEMORY_PAGE_SIZE - within % ASTRA_MEMORY_PAGE_SIZE;
        if (chunk > wanted - copied) chunk = wanted - copied;
        if (base != owner_base) {
            if (area != 0u) (void)astra_close(area);
            area = 0u;
            status = astra_vfs_file_area(client, file,
                position - position % ASTRA_MEMORY_PAGE_SIZE,
                ASTRA_MEMORY_PAGE_SIZE, &area, &size);
            if (status != ASTRA_VFS_OK) goto done;
            owner_base = base;
            if (position >= size) break;
            if (size - offset < wanted) wanted = (uint32_t)(size - offset);
            if (chunk > wanted - copied) chunk = wanted - copied;
        }
        uint64_t deadline = astra_clock_monotonic() + ASTRA_PAGER_REQUEST_TIMEOUT_NS;
        for (;;) {
            uint32_t result = astra_rt_area_read_resident(area, within,
                (uint8_t *)buffer + copied, chunk);
            if (result == ASTRA_SYSCALL_OK) break;
            if (result != ASTRA_SYSCALL_WOULD_BLOCK) {
                (void)astra_log_failure("VFS cached page read", result);
                status = ASTRA_VFS_ERR_IO;
                goto done;
            }
            result = astra_rt_area_prefetch(area, page);
            /* Signal delivery interrupts the wait, not the page request. */
            if (result != ASTRA_SYSCALL_OK && result != ASTRA_SYSCALL_CANCELLED) {
                (void)astra_log_failure("VFS cached page prefetch", result);
                status = ASTRA_VFS_ERR_IO;
                goto done;
            }
            if (astra_clock_monotonic() >= deadline) {
                status = ASTRA_VFS_ERR_BUSY;
                goto done;
            }
        }
        copied += chunk;
    }
    status = ASTRA_VFS_OK;
done:
    if (area != 0u) (void)astra_close(area);
    if (copied != 0u) {
        uint32_t checked = astra_vfs_stat_file_meta(client, file, &info);
        if (checked != ASTRA_VFS_OK) return checked;
        if (info.kind != ASTRA_VFS_KIND_FILE) return ASTRA_VFS_ERR_LOST;
        if (offset >= info.size) copied = 0u;
        else if (info.size - offset < copied) copied = (uint32_t)(info.size - offset);
    }
    *moved = copied;
    return copied != 0u ? ASTRA_VFS_OK : status;
}

uint32_t
astra_vfs_port_read_cached_position(AstraVfsClient *client, AstraVfsFile file,
    uint64_t offset, uint32_t flags, void *buffer, uint32_t length,
    uint32_t *moved, uint64_t *position)
{
    uint32_t status;

    if (client == NULL || buffer == NULL || moved == NULL || position == NULL ||
        (flags & ~ASTRA_VFS_IO_SEQUENTIAL) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    *moved = 0u;
    *position = offset;
    if (flags == 0u) {
        status = astra_vfs_port_read_cached(client, file, offset,
                                            buffer, length, moved);
        *position = offset + *moved;
        return status;
    }
    status = port_cache_available(client);
    if (status != ASTRA_VFS_OK) return status;
    const uint32_t features = ASTRA_VFS_FEATURE_DESCRIPTIONS |
                              ASTRA_VFS_FEATURE_POSITION_ADVANCE;
    if ((client->features & features) != features)
        return ASTRA_VFS_ERR_UNSUPPORTED;
    status = astra_vfs_seek(client, file, 0, 1u, position);
    if (status != ASTRA_VFS_OK) return status;
    uint64_t deadline = astra_clock_monotonic() + ASTRA_PAGER_REQUEST_TIMEOUT_NS;
    for (;;) {
        uint64_t expected = *position;
        uint32_t copied = 0u;
        AstraVfsRequest request = {0};
        AstraVfsReply reply = {0};

        status = astra_vfs_port_read_cached(client, file, expected,
                                            buffer, length, &copied);
        if (status != ASTRA_VFS_OK) return status;
        request.size = ASTRA_VFS_REQUEST_SIZE;
        request.version = client->version;
        request.session = port_client_session(client);
        request.file = file;
        request.activity = client->activity;
        request.offset = expected;
        request.length = copied;
        status = astra_vfs_port_transport(client, ASTRA_VFS_OP_POSITION_ADVANCE,
                                           &request, NULL, &reply);
        if (status == ASTRA_VFS_OK) status = reply.status;
        if (status != ASTRA_VFS_OK) return status;
        if (!astra_vfs_position_advance_valid(expected, copied,
                reply.position, reply.count))
            return ASTRA_VFS_ERR_PROTOCOL;
        *position = reply.position;
        if (reply.count != 0u) {
            *moved = copied;
            return ASTRA_VFS_OK;
        }
        if (astra_clock_monotonic() >= deadline) return ASTRA_VFS_ERR_BUSY;
    }
}

static void
prepare_request(AstraVfsClient *client, uint32_t operation)
{
    memset(&port_call.request, 0, sizeof(port_call.request));
    port_call.request.size = ASTRA_VFS_REQUEST_SIZE;
    port_call.request.version = client->version;
    port_call.request.session = port_client_session(client);
    port_call.request.activity = client->activity;
    (void)operation;
}

/*
 * The transfer area, sized to what has actually been asked for.
 *
 * A client that only ever reads manifests should not commit -- and the kernel
 * should not zero -- the largest transfer the protocol allows. Most clients
 * never grow past the first step; the ones that load a library pay one
 * rebind. Growth is one-way, because shrinking would trade a rebind for
 * nothing.
 */
/*
 * Measured: starting small and growing on first large read cost more than it
 * saved -- the grow is a rebind plus a repeated request, and it lands on
 * exactly the large reads that matter. One commit up front wins.
 */
/* Measured fast default, not a refusal ceiling; ensure_area_size grows it. */
#define VFS_PORT_AREA_INITIAL (512u * 1024u)

static void release_area(AstraVfsPortLane *lane)
{
    if (lane->area_send != 0u)
        (void)astra_close(lane->area_send);
    if (lane->area_address != NULL)
        (void)astra_rt_area_unmap(lane->area_address);
    if (lane->area != 0u)
        (void)astra_close(lane->area);
    lane->area = 0u;
    lane->area_send = 0u;
    lane->area_address = NULL;
    lane->area_size = 0u;
}

static uint32_t
ensure_area_size(AstraVfsClient *client, uint32_t needed)
{
    AstraVfsPortLane *lane = port_lane(client);
    uint32_t status;
    void *address = NULL;
    uint32_t size = 0u;
    uint32_t wanted = VFS_PORT_AREA_INITIAL;

    if (needed > ASTRA_VFS_BULK_MAX)
        needed = ASTRA_VFS_BULK_MAX;
    /*
     * BIND_AREA belongs to a session.  A lazy client's first operation can be
     * a bulk path read. Establish the session first: otherwise the nested
     * HELLO overwrites client->request, and the retried bind publishes a
     * zero-length area.
     */
    if (port_client_session(client) == ASTRA_VFS_SESSION_INVALID) {
        status = ensure_connection(client);
        if (status != ASTRA_VFS_OK)
            return status;
    }
    while (wanted < needed)
        wanted <<= 1;
    if (lane->area_address != NULL) {
        if (lane->area_size >= wanted)
            return ASTRA_VFS_OK;
        release_area(lane);
    }
    status = astra_rt_area_create(
        wanted,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
            ASTRA_RIGHT_TRANSFER | ASTRA_RIGHT_ADMINISTER,
        &lane->area);
    if (status != ASTRA_SYSCALL_OK)
        return ASTRA_VFS_ERR_LIMIT;
    status = astra_rt_area_map(lane->area,
                            ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                            &address, &size);
    if (status != ASTRA_SYSCALL_OK) {
        status = status == ASTRA_SYSCALL_RESOURCE_LIMIT ? ASTRA_VFS_ERR_LIMIT :
                                                       ASTRA_VFS_ERR_IO;
        goto fail;
    }
    status = astra_rt_handle_duplicate(
        lane->area,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
            ASTRA_RIGHT_TRANSFER,
        &lane->area_send);
    if (status != ASTRA_SYSCALL_OK) {
        status = status == ASTRA_SYSCALL_RESOURCE_LIMIT ? ASTRA_VFS_ERR_LIMIT :
                                                       ASTRA_VFS_ERR_IO;
        goto fail;
    }
    lane->area_address = address;
    lane->area_size = size;
    prepare_request(client, ASTRA_VFS_OP_BIND_AREA);
    port_call.request.length = size;
    status = vfs_port_transport_call(client, ASTRA_VFS_OP_BIND_AREA,
                                      &port_call.request, NULL, &port_call.reply, NULL);
    if (status == ASTRA_VFS_OK)
        status = port_call.reply.status;
    if (status == ASTRA_VFS_OK)
        return status;

fail:
    if (lane->area_send != 0u)
        (void)astra_close(lane->area_send);
    if (address != NULL)
        (void)astra_rt_area_unmap(address);
    (void)astra_close(lane->area);
    lane->area = 0u;
    lane->area_send = 0u;
    lane->area_address = NULL;
    lane->area_size = 0u;
    return status;
}

static uint32_t port_set_path(AstraVfsClient *client, AstraVfsRequest *request, const char *path,
    AstraVfsTransfer *transfer)
{
    AstraVfsCallState *call = &port_call.client_call;
    uint32_t used;
    if (call->path_capacity < ASTRA_VFS_PATH_MAX) {
        void *grown = astra_runtime_reallocate(call->path_bytes, ASTRA_VFS_PATH_MAX);
        if (grown == NULL)
            return ASTRA_VFS_ERR_LIMIT;
        call->path_bytes = grown;
        call->path_capacity = ASTRA_VFS_PATH_MAX;
    }
    uint32_t status = astra_vfs_path_pack(path, NULL, call->path_bytes,
        call->path_capacity, &request->body.paths, &used);
    if (status == ASTRA_VFS_OK)
        *transfer = (AstraVfsTransfer){.input = call->path_bytes, .input_size = used};
    return status;
}

static uint32_t
port_read_path_call(AstraVfsClient *client, const char *path,
                    const uint8_t **bytes, uint32_t *moved,
                    uint64_t *node_size)
{
    AstraVfsPortLane *lane;
    uint32_t status;
    AstraVfsCallState *scratch ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    AstraVfsTransfer transfer = {0};
    if (client == NULL || path == NULL || bytes == NULL || moved == NULL)
        return ASTRA_VFS_ERR_INVALID;
    scratch = &port_call.client_call;
    *bytes = NULL;
    *moved = 0u;
    if (node_size != NULL)
        *node_size = 0u;
    status = ensure_area_size(client, VFS_PORT_AREA_INITIAL);
    if (status != ASTRA_VFS_OK)
        return status;
    lane = port_lane(client);
    /*
     * Two attempts at most. The first asks with whatever the area already is;
     * a file too big for it is answered with its size and nothing read, and
     * the second asks again with the area grown to fit. That costs a round
     * trip only for a file bigger than this client has ever read.
     */
    for (uint32_t attempt = 0u; attempt < 2u; ++attempt) {
        prepare_request(client, ASTRA_VFS_OP_READ_PATH);
        status = port_set_path(client, &port_call.request, path, &transfer);
        if (status != ASTRA_VFS_OK)
            return status;
        port_call.request.length = lane->area_size;
        status = vfs_port_transport_call(client, ASTRA_VFS_OP_READ_PATH,
                                     &port_call.request, &transfer, &port_call.reply, NULL);
        if (status != ASTRA_VFS_OK)
            return status;
        if (node_size != NULL)
            *node_size = port_call.reply.node_size;
        if (port_call.reply.status != ASTRA_VFS_ERR_LIMIT ||
            attempt != 0u ||
            port_call.reply.node_size <= port_call.request.length ||
            port_call.reply.node_size > ASTRA_VFS_BULK_MAX)
            break;
        status = ensure_area_size(client, (uint32_t)port_call.reply.node_size);
        if (status != ASTRA_VFS_OK)
            return status;
    }
    if (port_call.reply.status != ASTRA_VFS_OK)
        return port_call.reply.status;
    if (port_call.reply.count > port_call.request.length)
        return ASTRA_VFS_ERR_PROTOCOL;
    *bytes = astra_vfs_port_call_area(client, NULL);
    *moved = port_call.reply.count;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_port_read_path(AstraVfsClient *client, const char *path,
                         const uint8_t **bytes, uint32_t *moved,
                         uint64_t *node_size)
{
    uint32_t status;

    if (client == NULL || path == NULL || bytes == NULL || moved == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *bytes = NULL;
    *moved = 0u;
    if (node_size != NULL) *node_size = 0u;
    status = port_cache_available(client);
    if (status != ASTRA_VFS_ERR_UNSUPPORTED) {
        AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
        uint64_t size = 0u;
        uint16_t kind = 0u;
        if (status != ASTRA_VFS_OK) return status;
        status = astra_vfs_open(client, path, ASTRA_VFS_OPEN_READ,
                               &file, &size, &kind);
        if (status != ASTRA_VFS_OK) return status;
        if (node_size != NULL) *node_size = size;
        status = kind != ASTRA_VFS_KIND_FILE ? ASTRA_VFS_ERR_INVALID :
            size > ASTRA_VFS_BULK_MAX ? ASTRA_VFS_ERR_LIMIT :
            astra_vfs_port_read_borrow(client, file, 0u,
                size != 0u ? (uint32_t)size : 1u, bytes, moved);
        uint32_t closed = astra_vfs_close(client, file);
        if (status == ASTRA_VFS_OK) status = closed;
        if (status != ASTRA_VFS_OK) {
            *bytes = NULL;
            *moved = 0u;
        }
        return status;
    }
    status = astra_vfs_port_client_enter(client);
    if (status == ASTRA_VFS_OK) {
        status = port_read_path_call(client, path, bytes, moved, node_size);
        astra_vfs_port_client_leave(client);
    }
    return astra_vfs_storage_status(status);
}

static uint32_t
port_read_borrow_call(AstraVfsClient *client, AstraVfsFile file,
                      uint64_t offset, uint32_t length,
                      const uint8_t **bytes, uint32_t *moved)
{
    AstraVfsPortLane *lane;
    uint32_t status;

    if (client == NULL || bytes == NULL || moved == NULL || length == 0u)
        return ASTRA_VFS_ERR_INVALID;
    *bytes = NULL;
    *moved = 0u;
    status = ensure_area_size(client, length);
    if (status != ASTRA_VFS_OK) return status;
    lane = port_lane(client);
    if (length > lane->area_size) return ASTRA_VFS_ERR_LIMIT;
    prepare_request(client, ASTRA_VFS_OP_READ_AREA);
    port_call.request.file = file;
    port_call.request.offset = offset;
    port_call.request.length = length;
    status = vfs_port_transport_call(client, ASTRA_VFS_OP_READ_AREA,
        &port_call.request, NULL, &port_call.reply, NULL);
    if (status != ASTRA_VFS_OK) return status;
    uint64_t position = offset;
    status = astra_vfs_reply_io_result(&port_call.reply, port_call.reply.status,
        0u, length, moved, &position);
    if (status != ASTRA_VFS_OK && *moved == 0u) return status;
    *bytes = astra_vfs_port_call_area(client, NULL);
    port_call.borrow_valid = status == ASTRA_VFS_OK;
    port_call.request.file = file;
    port_call.request.offset = offset;
    port_call.request.length = *moved;
    port_call.borrow_client = client;
    port_call.borrow_base = *bytes;
    return status;
}

static uint32_t
port_read_borrow_pages(AstraVfsClient *client, AstraVfsFile file,
    uint64_t offset, uint32_t length, const uint8_t **bytes, uint32_t *moved)
{
    AstraVfsPortLane owned = {0};
    uint32_t status = astra_vfs_port_client_enter(client);
    if (status != ASTRA_VFS_OK) return status;
    status = ensure_scratch_area(client, length);
    if (status == ASTRA_VFS_OK) {
        AstraVfsPortLane *lane = port_lane(client);
        owned.scratch_address = lane->scratch_address;
        owned.scratch_area = lane->scratch_area;
        owned.scratch_size = lane->scratch_size;
        lane->scratch_address = NULL;
        lane->scratch_area = lane->scratch_size = 0u;
    }
    astra_vfs_port_client_leave(client);
    if (status != ASTRA_VFS_OK) return status;
    /* Cache fill may refresh the client; keep its output alive without
       holding the inflight guard that refresh itself must drain. */
    status = astra_vfs_port_read_cached(client, file, offset,
                                       owned.scratch_address, length, moved);
    if (status == ASTRA_VFS_OK) {
        status = astra_vfs_port_client_enter(client);
        if (status == ASTRA_VFS_OK) {
            AstraVfsPortLane *lane = port_lane(client);
            lane_scratch_area_close(lane);
            lane->scratch_address = owned.scratch_address;
            lane->scratch_area = owned.scratch_area;
            lane->scratch_size = owned.scratch_size;
            *bytes = owned.scratch_address;
            owned.scratch_address = NULL;
            owned.scratch_area = owned.scratch_size = 0u;
            astra_vfs_port_client_leave(client);
        }
    }
    lane_scratch_area_close(&owned);
    if (status != ASTRA_VFS_OK) *moved = 0u;
    return status;
}

static int
port_read_borrow_cached(AstraVfsClient *client, AstraVfsFile file,
                        uint64_t offset, uint32_t length,
                        const uint8_t **bytes, uint32_t *moved)
{
    AstraVfsPortCallState *state = port_state(client);
    uint64_t within;

    if (__atomic_load_n(&client->port_lifecycle, __ATOMIC_ACQUIRE) !=
            PORT_CLIENT_OPEN ||
        state->borrow_valid == 0u ||
        state->borrow_client != client || state->request.file != file ||
        offset < state->request.offset)
        return 0;
    within = offset - state->request.offset;
    if (within > state->request.length ||
        length > state->request.length - (uint32_t)within)
        return 0;
    *bytes = state->borrow_base + within;
    *moved = length;
    return 1;
}

uint32_t
astra_vfs_port_read_borrow(AstraVfsClient *client, AstraVfsFile file,
                           uint64_t offset, uint32_t length,
                           const uint8_t **bytes, uint32_t *moved)
{
    uint32_t status;

    if (client == NULL || bytes == NULL || moved == NULL || length == 0u)
        return ASTRA_VFS_ERR_INVALID;
    *bytes = NULL;
    *moved = 0u;
    if (port_read_borrow_cached(client, file, offset, length, bytes, moved))
        return ASTRA_VFS_OK;
    status = port_cache_available(client);
    if (status != ASTRA_VFS_ERR_UNSUPPORTED) {
        if (status != ASTRA_VFS_OK) return status;
        status = port_read_borrow_pages(client, file, offset, length,
                                        bytes, moved);
        if (status != ASTRA_VFS_ERR_UNSUPPORTED) return status;
    }
    status = astra_vfs_port_client_enter(client);
    if (status == ASTRA_VFS_OK) {
        status = port_read_borrow_call(client, file, offset, length, bytes,
                                       moved);
        astra_vfs_port_client_leave(client);
    }
    return astra_vfs_storage_status(status);
}

static uint32_t
port_read_bulk_call(AstraVfsClient *client, AstraVfsFile file,
                    uint64_t offset, uint32_t flags, void *buffer,
                    uint32_t length, uint32_t *moved, uint64_t *position)
{
    AstraVfsPortLane *lane;
    uint8_t *out = buffer;
    const uint8_t *shared;
    uint32_t status;

    if (client == NULL || buffer == NULL || moved == NULL ||
        position == NULL || length == 0u ||
        (flags & ~ASTRA_VFS_IO_SEQUENTIAL) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    *moved = 0u;
    *position = offset;
    if (client->transport != astra_vfs_port_transport)
        return astra_vfs_read_position(
            client, file, offset, flags, buffer,
            length < ASTRA_VFS_IO_MAX ? length : ASTRA_VFS_IO_MAX,
            moved, position);
    lane = port_lane(client);
    status = ensure_area_size(client, length);
    if (status != ASTRA_VFS_OK)
        return status;
    if (length > lane->area_size)
        length = lane->area_size;
    prepare_request(client, ASTRA_VFS_OP_READ_AREA);
    port_call.request.file = file;
    port_call.request.offset = offset;
    port_call.request.length = length;
    port_call.request.flags = flags;
    status = vfs_port_transport_call(client, ASTRA_VFS_OP_READ_AREA,
                                      &port_call.request, NULL, &port_call.reply, NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    status = astra_vfs_reply_io_result(&port_call.reply, port_call.reply.status,
        flags, length, moved, position);
    shared = lane->area_address;
    memcpy(out, shared, *moved);
    return status;
}

uint32_t
astra_vfs_port_read_bulk(AstraVfsClient *client, AstraVfsFile file,
                         uint64_t offset, void *buffer, uint32_t length,
                         uint32_t *moved)
{
    uint64_t position;

    return astra_vfs_port_read_bulk_position(client, file, offset, 0u,
                                             buffer, length, moved,
                                             &position);
}

uint32_t
astra_vfs_port_read_bulk_position(AstraVfsClient *client, AstraVfsFile file,
                                  uint64_t offset, uint32_t flags,
                                  void *buffer, uint32_t length,
                                  uint32_t *moved, uint64_t *position)
{
    uint32_t status;

    if (client == NULL || buffer == NULL || moved == NULL ||
        position == NULL || length == 0u)
        return ASTRA_VFS_ERR_INVALID;
    *moved = 0u;
    *position = offset;
    status = astra_vfs_port_read_cached_position(client, file, offset, flags,
                                                buffer, length, moved, position);
    if (status != ASTRA_VFS_ERR_UNSUPPORTED) return status;
    status = astra_vfs_port_client_enter(client);
    if (status == ASTRA_VFS_OK) {
        status = port_read_bulk_call(client, file, offset, flags, buffer,
                                     length, moved, position);
        astra_vfs_port_client_leave(client);
    }
    return astra_vfs_storage_status(status);
}

static uint32_t
port_write_bulk_call(AstraVfsClient *client, AstraVfsFile file,
                     uint64_t offset, uint32_t flags, uint64_t max_end, const void *buffer,
                     uint32_t length, uint32_t *moved, uint64_t *position)
{
    AstraVfsPortLane *lane;
    uint32_t status;

    if (client == NULL || buffer == NULL || moved == NULL ||
        position == NULL || length == 0u ||
        (flags & ~(ASTRA_VFS_WRITE_APPEND |
                    ASTRA_VFS_IO_SEQUENTIAL)) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    *moved = 0u;
    *position = offset;
    if (client->transport != astra_vfs_port_transport)
        return astra_vfs_write_position(
            client, file, offset, flags, max_end, buffer,
            length < ASTRA_VFS_IO_MAX ? length : ASTRA_VFS_IO_MAX,
            moved, position);
    lane = port_lane(client);
    status = ensure_area_size(client, length);
    if (status != ASTRA_VFS_OK)
        return status;
    if (length > lane->area_size)
        length = lane->area_size;
    memcpy(lane->area_address, buffer, length);
    prepare_request(client, ASTRA_VFS_OP_WRITE_AREA);
    port_call.request.file = file;
    port_call.request.offset = offset;
    port_call.request.length = length;
    port_call.request.flags = flags;
    port_call.request.write_limit = max_end;
    status = vfs_port_transport_call(client, ASTRA_VFS_OP_WRITE_AREA,
                                      &port_call.request, NULL, &port_call.reply, NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    return astra_vfs_reply_io_result(&port_call.reply, port_call.reply.status,
        flags, length, moved, position);
}

uint32_t
astra_vfs_port_write_bulk(AstraVfsClient *client, AstraVfsFile file,
                          uint64_t offset, uint64_t max_end, const void *buffer, uint32_t length,
                          uint32_t *moved)
{
    uint64_t position = offset;

    return astra_vfs_port_write_bulk_position(
        client, file, offset, 0u, max_end, buffer, length, moved, &position);
}

uint32_t
astra_vfs_port_write_bulk_position(
    AstraVfsClient *client, AstraVfsFile file, uint64_t offset,
    uint32_t flags, uint64_t max_end, const void *buffer, uint32_t length, uint32_t *moved,
    uint64_t *position)
{
    uint32_t status;

    if (client == NULL || buffer == NULL || moved == NULL ||
        position == NULL || length == 0u)
        return ASTRA_VFS_ERR_INVALID;
    *moved = 0u;
    *position = offset;
    status = astra_vfs_port_client_enter(client);
    if (status == ASTRA_VFS_OK) {
        status = port_write_bulk_call(client, file, offset, flags, max_end, buffer,
                                      length, moved, position);
        astra_vfs_port_client_leave(client);
    }
    return astra_vfs_storage_status(status);
}
