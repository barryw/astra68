/*
 * The storage protocol across a process boundary.
 *
 * Both ends of one crossing. The client sends a request and waits for the
 * reply; the service receives, dispatches and replies. Nothing above either
 * end changes: the Kit's transport is a callback precisely so that the process
 * boundary could become a deployment decision rather than a rewrite, and this
 * is the file that cashes that in.
 *
 * Nothing here emits an event. This is the path the events service's own
 * clients take, so a transport that logged would be logging about logging --
 * and the rule has to be structural rather than careful, which is why this
 * file does not include astra/event_emit.h and must not grow an include of it.
 */

#include <astra/vfs_port_transport.h>

#include <astra/vfs_backend.h>

#include <astra/bytes.h>

#include <astra/runtime.h>
#include <astra/syscall.h>

#include <stddef.h>
#include <string.h>

typedef AstraVfsPortThreadState AstraVfsPortCallState;

#ifdef ASTRA_VFS_TEST_HOOKS
void astra_vfs_test_port_lane_lookup(AstraVfsClient *client);
#define test_port_lane_lookup(client) astra_vfs_test_port_lane_lookup(client)
#else
#define test_port_lane_lookup(client) ((void)(client))
#endif

#if !defined(ASTRA_VFS_EMBEDDED_THREAD_STATE)
static _Thread_local AstraVfsPortCallState port_tls;
#endif

int
astra_vfs_port_set_thread_storage(AstraVfsClient *client,
                                  AstraVfsPortThreadState *states,
                                  uint32_t capacity)
{
    if (client == NULL || states == NULL ||
        capacity != ASTRA_PROCESS_THREAD_COUNT_MAX ||
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
    uint32_t owners[ASTRA_PROCESS_THREAD_COUNT_MAX];
    uint32_t thread = 0u;

    if (client == NULL ||
        astra_query_abi(NULL, NULL, &thread) != ASTRA_SYSCALL_OK ||
        thread == 0u || client->port_thread_states == NULL ||
        client->port_thread_capacity != ASTRA_PROCESS_THREAD_COUNT_MAX)
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
    if (lane->direct_address != NULL)
        (void)astra_rt_area_unmap(lane->direct_address);
    if (lane->direct_area != 0u)
        (void)astra_close(lane->direct_area);
    memset(lane, 0, sizeof(*lane));
}

static AstraVfsPortLane *
port_lane(AstraVfsClient *client)
{
    uint32_t owners[ASTRA_PROCESS_THREAD_COUNT_MAX];
    uint32_t thread = port_state(client)->owner_thread;

    test_port_lane_lookup(client);

    for (;;) {
        if (astra_mutex_lock(&client->port_lane_lock) != ASTRA_SYSCALL_OK)
            astra_assert_failed(__FILE__, __LINE__,
                                "VFS lane lock failed");
        for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_COUNT_MAX;
             ++index) {
            if (client->port_lanes[index].owner_thread == thread) {
                (void)astra_mutex_unlock(&client->port_lane_lock);
                return &client->port_lanes[index];
            }
        }
        for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_COUNT_MAX;
             ++index) {
            if (client->port_lanes[index].owner_thread == 0u) {
                client->port_lanes[index].owner_thread = thread;
                client->port_lanes[index].session =
                    ASTRA_VFS_SESSION_INVALID;
                (void)astra_mutex_unlock(&client->port_lane_lock);
                return &client->port_lanes[index];
            }
        }
        for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_COUNT_MAX;
             ++index)
            owners[index] = client->port_lanes[index].owner_thread;
        (void)astra_mutex_unlock(&client->port_lane_lock);

        for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_COUNT_MAX;
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

int
astra_vfs_state_lock_acquire(void *context)
{
    return context != NULL &&
           astra_mutex_lock((volatile uint32_t *)context) ==
               ASTRA_SYSCALL_OK;
}

void
astra_vfs_state_lock_release(void *context)
{
    if (context != NULL)
        (void)astra_mutex_unlock((volatile uint32_t *)context);
}

int
astra_vfs_state_futex_wait(void *context, volatile uint32_t *sequence,
                           uint32_t expected)
{
    uint32_t status;

    if (context == NULL || sequence == NULL)
        return 0;
    if (astra_mutex_unlock((volatile uint32_t *)context) != ASTRA_SYSCALL_OK)
        return 0;
    status = astra_futex_wait(sequence, expected, ASTRA_DEADLINE_FOREVER);
    if (astra_mutex_lock((volatile uint32_t *)context) != ASTRA_SYSCALL_OK)
        return 0;
    return status == ASTRA_SYSCALL_OK || status == ASTRA_SYSCALL_WOULD_BLOCK;
}

void
astra_vfs_state_futex_wake(void *context, volatile uint32_t *sequence)
{
    (void)context;
    if (sequence != NULL)
        (void)astra_futex_wake(sequence, UINT32_MAX, NULL);
}

/*
 * One reply at a time, one message deep. The protocol is synchronous, so a
 * deeper reply port would be capacity nothing can use.
 */
#define VFS_PORT_REPLY_MESSAGES 1u

int
astra_vfs_port_quota_storage(uint32_t element_size, void **storage,
                             uint32_t *capacity)
{
    uint32_t handle = 0u;
    uint32_t span = 0u;
    uint32_t count;
    void *address = NULL;

    if (element_size == 0u || storage == NULL || capacity == NULL)
        return 0;
    *storage = NULL;
    *capacity = 0u;
    if (astra_rt_area_create_flagged(
            ASTRA_AREA_SIZE_MAX,
            ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP,
            ASTRA_AREA_CREATE_RESERVED, &handle) != ASTRA_SYSCALL_OK)
        return 0;
    if (astra_rt_area_map(handle,
                          ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                          &address, &span) != ASTRA_SYSCALL_OK ||
        address == NULL) {
        (void)astra_close(handle);
        return 0;
    }
    count = span / element_size;
    if (count > ASTRA_VFS_FILE_HANDLE_MAX)
        count = ASTRA_VFS_FILE_HANDLE_MAX;
    if (count == 0u) {
        (void)astra_rt_area_unmap(address);
        (void)astra_close(handle);
        return 0;
    }
    /* The service owns the mapping for its lifetime; process teardown closes
     * the retained capability and mapping together. */
    *storage = address;
    *capacity = count;
    return 1;
}

static void
fill_header(AstraMessageHeader *header, uint32_t operation, uint32_t size,
            uint32_t transaction)
{
    astra_message_header_set(header, size, ASTRA_VFS_PROTOCOL,
                             ASTRA_VFS_VERSION, operation, transaction);
}

static void
clear_service_reply(AstraVfsReply *reply, uint16_t version, uint32_t session)
{
    memset(reply, 0, sizeof(*reply));
    reply->size = ASTRA_VFS_REPLY_SIZE;
    reply->version = version;
    reply->session = session;
    reply->status = ASTRA_VFS_ERR_PROTOCOL;
}

static void
port_client_handles_reset(AstraVfsClient *client)
{
    for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_COUNT_MAX;
         ++index)
        lane_resources_close(&client->port_lanes[index]);
    if (client->port_connect_lock != 0u)
        (void)astra_close(client->port_connect_lock);
    client->port_area_capable = 0u;
    client->port_direct_detached = 0u;
    client->port_connect_lock = 0u;
    client->port_lane_lock = 0u;
}

enum {
    PORT_CLIENT_OPEN = 0u,
    PORT_CLIENT_DRAINING,
    PORT_CLIENT_FAILED,
    PORT_CLIENT_CLEANING,
    PORT_CLIENT_CLOSED
};

static uint32_t
port_client_session(const AstraVfsClient *client)
{
    return atomic_load_explicit(&client->session, memory_order_acquire);
}

static void
port_client_publish_session(AstraVfsClient *client, uint32_t session,
                            uint16_t version)
{
    client->version = version;
    atomic_store_explicit(&client->session, session, memory_order_release);
}

static void
port_client_finish(AstraVfsClient *client, int abandon)
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
    if (client->port_accelerator_ops != NULL) {
        if (abandon)
            client->port_accelerator_ops->abandon(client);
        else
            client->port_accelerator_ops->disconnect(client);
    }
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
    if (__atomic_load_n(&client->port_lifecycle, __ATOMIC_ACQUIRE) !=
        PORT_CLIENT_OPEN)
        return ASTRA_VFS_ERR_PEER;
    (void)__atomic_add_fetch(&client->port_inflight, 1u, __ATOMIC_ACQ_REL);
    if (__atomic_load_n(&client->port_lifecycle, __ATOMIC_ACQUIRE) ==
        PORT_CLIENT_OPEN)
        return ASTRA_VFS_OK;
    if (__atomic_sub_fetch(&client->port_inflight, 1u, __ATOMIC_ACQ_REL) == 0u &&
        __atomic_load_n(&client->port_inflight_waiters,
                        __ATOMIC_ACQUIRE) != 0u)
        (void)astra_futex_wake(&client->port_inflight, UINT32_MAX, NULL);
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
        port_client_finish(client, 0);
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
    uint32_t expected = PORT_CLIENT_OPEN;

    if (!__atomic_compare_exchange_n(
            &client->port_lifecycle, &expected, PORT_CLIENT_DRAINING, 0,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return 0;
    port_client_wait_idle(client);
    return 1;
}

static void
port_client_fail(AstraVfsClient *client)
{
    uint32_t state = __atomic_load_n(&client->port_lifecycle,
                                     __ATOMIC_ACQUIRE);

    while (state == PORT_CLIENT_OPEN || state == PORT_CLIENT_DRAINING) {
        if (__atomic_compare_exchange_n(
                &client->port_lifecycle, &state, PORT_CLIENT_FAILED, 0,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            break;
    }
    if (state != PORT_CLIENT_OPEN && state != PORT_CLIENT_DRAINING)
        return;
    /* Closing receive capabilities wakes every lane blocked on a dead peer.
     * Mappings remain valid until the last in-flight caller leaves. */
    for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_COUNT_MAX;
         ++index)
        if (client->port_lanes[index].reply_receive != 0u)
            (void)astra_close(client->port_lanes[index].reply_receive);
    __atomic_store_n(&client->port_connecting, 0u, __ATOMIC_RELEASE);
    (void)astra_futex_wake(&client->port_connecting, UINT32_MAX, NULL);
}

static void
port_client_close(AstraVfsClient *client, int abandon)
{
    uint32_t state;

    if (port_client_begin_drain(client)) {
        port_client_finish(client, abandon);
        return;
    }
    state = __atomic_load_n(&client->port_lifecycle, __ATOMIC_ACQUIRE);
    while (state == PORT_CLIENT_FAILED || state == PORT_CLIENT_CLEANING) {
        if (state == PORT_CLIENT_FAILED) {
            port_client_wait_idle(client);
            port_client_finish(client, abandon);
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
    port_client_close(client, 1);
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
    return astra_vfs_port_connect_with_accelerator(client, service, NULL);
}

uint32_t
astra_vfs_port_connect_with_accelerator(
    AstraVfsClient *client, uint32_t service,
    const AstraVfsPortAcceleratorOps *accelerator)
{
    uint32_t status;

    if (client == NULL || service == 0u ||
        (accelerator != NULL &&
         (accelerator->connect == NULL || accelerator->disconnect == NULL ||
          accelerator->abandon == NULL || accelerator->transport == NULL ||
          accelerator->bulk == NULL)))
        return ASTRA_VFS_ERR_INVALID;
    client->port_service = service;
    memset(client->port_lanes, 0, sizeof(client->port_lanes));
    client->port_lane_lock = 0u;
    client->port_direct_address = NULL;
    client->port_direct_area = 0u;
    client->port_direct_device = 0u;
    client->port_direct_session = ASTRA_VFS_SESSION_INVALID;
    client->port_direct_lock = 0u;
    client->direct_backend_ops = NULL;
    client->direct_backend_context = NULL;
    client->direct_backend_enter = NULL;
    client->direct_backend_leave = NULL;
    client->port_connect_lock = 0u;
    client->port_connecting = 0u;
    client->port_inflight = 0u;
    client->port_inflight_waiters = 0u;
    client->port_lifecycle = PORT_CLIENT_OPEN;
    client->port_accelerator_ops = accelerator;
    if (astra_rt_semaphore_create(
            1u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
            &client->port_connect_lock) != ASTRA_SYSCALL_OK)
        return ASTRA_VFS_ERR_LIMIT;
    status = astra_vfs_connect(client, astra_vfs_port_transport, client);
    if (status != ASTRA_VFS_OK) {
        port_client_close(client, 0);
    } else {
        client->port_area_capable = 1u;
        client->area_payload = astra_vfs_port_call_area;
        client->call_acquire = astra_vfs_port_call_acquire;
    }
    return status;
}

uint32_t
astra_vfs_port_connect_lazy(AstraVfsClient *client, uint32_t service)
{
    return astra_vfs_port_connect_lazy_with_accelerator(client, service,
                                                        NULL);
}

uint32_t
astra_vfs_port_connect_lazy_with_accelerator(
    AstraVfsClient *client, uint32_t service,
    const AstraVfsPortAcceleratorOps *accelerator)
{
    if (client == NULL || service == 0u ||
        (accelerator != NULL &&
         (accelerator->connect == NULL || accelerator->disconnect == NULL ||
          accelerator->abandon == NULL || accelerator->transport == NULL ||
          accelerator->bulk == NULL)))
        return ASTRA_VFS_ERR_INVALID;
    client->transport = astra_vfs_port_transport;
    client->context = client;
    atomic_store_explicit(&client->session, ASTRA_VFS_SESSION_INVALID,
                          memory_order_relaxed);
    client->version = ASTRA_VFS_VERSION;
    client->port_area_capable = 1u;
    client->port_direct_detached = 0u;
    client->port_service = service;
    memset(client->port_lanes, 0, sizeof(client->port_lanes));
    client->port_lane_lock = 0u;
    client->port_direct_address = NULL;
    client->port_direct_area = 0u;
    client->port_direct_device = 0u;
    client->port_direct_session = ASTRA_VFS_SESSION_INVALID;
    client->port_direct_lock = 0u;
    client->direct_backend_ops = NULL;
    client->direct_backend_context = NULL;
    client->direct_backend_enter = NULL;
    client->direct_backend_leave = NULL;
    client->port_connect_lock = 0u;
    client->port_connecting = 0u;
    client->port_inflight = 0u;
    client->port_inflight_waiters = 0u;
    client->port_lifecycle = PORT_CLIENT_OPEN;
    client->port_accelerator_ops = accelerator;
    if (astra_rt_semaphore_create(
            1u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
            &client->port_connect_lock) != ASTRA_SYSCALL_OK)
        return ASTRA_VFS_ERR_LIMIT;
    client->area_payload = astra_vfs_port_call_area;
    client->call_acquire = astra_vfs_port_call_acquire;
    return ASTRA_VFS_OK;
}

static int
first_operation(uint32_t operation)
{
    return operation == ASTRA_VFS_OP_OPEN ||
           operation == ASTRA_VFS_OP_STAT ||
           operation == ASTRA_VFS_OP_READDIR ||
           operation == ASTRA_VFS_OP_MKDIR ||
           operation == ASTRA_VFS_OP_UNLINK ||
           operation == ASTRA_VFS_OP_CHMOD ||
           operation == ASTRA_VFS_OP_READLINK ||
           operation == ASTRA_VFS_OP_SYMLINK_TARGET ||
           operation == ASTRA_VFS_OP_READDIR_BATCH ||
           operation == ASTRA_VFS_OP_READ_PATH;
}

static uint32_t ensure_area_size(AstraVfsClient *client, uint32_t needed);

static uint32_t
ensure_thread_lane(AstraVfsClient *client, AstraVfsPortLane *lane)
{
    AstraVfsPortCallState *call = port_state(client);
    uint32_t status;

    if (lane->session == port_client_session(client))
        return ASTRA_VFS_OK;
    if (client->version < UINT16_C(20))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    memset(&call->area_request, 0, sizeof(call->area_request));
    call->area_request.size = ASTRA_VFS_REQUEST_SIZE;
    call->area_request.version = client->version;
    call->area_request.session = port_client_session(client);
    call->area_request.activity = client->activity;
    status = astra_vfs_port_transport(client, ASTRA_VFS_OP_BIND_LANE,
                                      &call->area_request, &call->reply);
    if (status != ASTRA_VFS_OK)
        return status;
    if (call->reply.status != ASTRA_VFS_OK)
        return call->reply.status;
    lane->session = port_client_session(client);
    return ASTRA_VFS_OK;
}

static uint32_t
ensure_accelerated_connection(AstraVfsClient *client)
{
    uint32_t status;

    if (port_client_session(client) != ASTRA_VFS_SESSION_INVALID)
        return ASTRA_VFS_OK;
    if (client->port_connect_lock == 0u ||
        astra_wait_one(client->port_connect_lock, ASTRA_DEADLINE_FOREVER,
                       NULL) != ASTRA_SYSCALL_OK)
        return ASTRA_VFS_ERR_PEER;
    if (port_client_session(client) == ASTRA_VFS_SESSION_INVALID) {
        status = astra_vfs_connect(client, astra_vfs_port_transport, client);
        client->port_area_capable = status == ASTRA_VFS_OK;
        client->area_payload = astra_vfs_port_call_area;
        client->call_acquire = astra_vfs_port_call_acquire;
    } else {
        status = ASTRA_VFS_OK;
    }
    if (astra_rt_signal(client->port_connect_lock, 1u, NULL) !=
        ASTRA_SYSCALL_OK)
        return ASTRA_VFS_ERR_PEER;
    return status;
}

static uint32_t
ensure_direct_area(AstraVfsClient *client, uint32_t needed)
{
    AstraVfsPortLane *lane = port_lane(client);
    void *address = NULL;
    uint32_t area = 0u;
    uint32_t size = 0u;
    uint32_t status;

    if (needed == 0u || needed > ASTRA_VFS_BULK_MAX)
        return ASTRA_VFS_ERR_LIMIT;
    if (lane->direct_address != NULL && lane->direct_size >= needed)
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
    if (lane->direct_address != NULL)
        (void)astra_rt_area_unmap(lane->direct_address);
    if (lane->direct_area != 0u)
        (void)astra_close(lane->direct_area);
    lane->direct_area = area;
    lane->direct_address = address;
    lane->direct_size = size;
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
    if (client->port_direct_address != NULL) {
        if (capacity != NULL)
            *capacity = lane->direct_size;
        return lane->direct_address;
    }
    if (capacity != NULL)
        *capacity = lane->area_size;
    return lane->area_address;
}

static uint32_t
request_message_size(uint32_t operation, const AstraVfsRequest *request)
{
    if (operation == ASTRA_VFS_OP_RENAME &&
        request->size == ASTRA_VFS_RENAME_REQUEST_SIZE)
        return (uint32_t)sizeof(AstraVfsRenameRequestMessage);
    if (operation != ASTRA_VFS_OP_RENAME &&
        request->size == ASTRA_VFS_REQUEST_SIZE)
        return (uint32_t)sizeof(AstraVfsRequestMessage);
    return 0u;
}

static uint32_t
vfs_port_transport_call(void *context, uint32_t operation,
                        const AstraVfsRequest *request, AstraVfsReply *reply,
                        AstraVfsPortLane *selected_lane);

static AstraVfsPortLane *
port_session_lane(AstraVfsClient *client)
{
    AstraVfsPortLane *lane = NULL;

    if (astra_mutex_lock(&client->port_lane_lock) != ASTRA_SYSCALL_OK)
        return NULL;
    for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_COUNT_MAX;
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

uint32_t
astra_vfs_port_transport(void *context, uint32_t operation,
                         const AstraVfsRequest *request, AstraVfsReply *reply)
{
    AstraVfsClient *client = context;
    int connecting = 0;
    uint32_t status;

    if (client == NULL || request == NULL || reply == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (operation == ASTRA_VFS_OP_BYE) {
        AstraVfsPortLane *lane;

        if (!port_client_begin_drain(client))
            return ASTRA_VFS_ERR_PEER;
        if (client->port_direct_detached != 0u &&
            client->port_accelerator_ops != NULL) {
            status = client->port_accelerator_ops->transport(
                client, operation, request, reply);
            port_client_finish(client, 0);
            return status;
        }
        lane = port_session_lane(client);
        status = lane == NULL ? ASTRA_VFS_ERR_BAD_HANDLE :
            vfs_port_transport_call(context, operation, request, reply, lane);
        port_client_finish(client, 0);
        return status;
    }
    status = astra_vfs_port_client_enter(client);
    if (status != ASTRA_VFS_OK)
        return status;
    if (operation != ASTRA_VFS_OP_HELLO) {
        while (port_client_session(client) == ASTRA_VFS_SESSION_INVALID) {
            uint32_t expected = 0u;

            if (__atomic_load_n(&client->port_lifecycle,
                                __ATOMIC_ACQUIRE) != PORT_CLIENT_OPEN) {
                status = ASTRA_VFS_ERR_PEER;
                goto done;
            }
            if (__atomic_compare_exchange_n(
                    &client->port_connecting, &expected, 1u, 0,
                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                connecting = 1;
                break;
            }
            (void)astra_futex_wait(&client->port_connecting, 1u,
                                   ASTRA_DEADLINE_FOREVER);
        }
        if (!connecting &&
            request->session == ASTRA_VFS_SESSION_INVALID) {
            AstraVfsRequest *retry;

            if (request->size == ASTRA_VFS_RENAME_REQUEST_SIZE) {
                port_call.outgoing.rename.request =
                    *(const AstraVfsRenameRequest *)request;
                retry = &port_call.outgoing.rename.request.request;
            } else {
                port_call.outgoing.standard.request = *request;
                retry = &port_call.outgoing.standard.request;
            }
            retry->session = port_client_session(client);
            retry->version = client->version;
            request = retry;
        }
    }
    status = vfs_port_transport_call(context, operation, request, reply,
                                     NULL);
done:
    if (connecting) {
        __atomic_store_n(&client->port_connecting, 0u, __ATOMIC_RELEASE);
        (void)astra_futex_wake(&client->port_connecting, UINT32_MAX, NULL);
    }
    astra_vfs_port_client_leave(client);
    return status;
}

static uint32_t
vfs_port_transport_call(void *context, uint32_t operation,
                        const AstraVfsRequest *request, AstraVfsReply *reply,
                        AstraVfsPortLane *selected_lane)
{
    AstraVfsClient *client = context;
    AstraVfsPortLane *lane;
    AstraVfsRequestMessageBuffer *outgoing;
    AstraVfsRequest *wire_request;
    AstraMessageHeader *wire_header;
    AstraVfsReplyMessage *incoming;
    AstraVfsRequest *area_request;
    uint32_t handles[1] = {0u};
    uint32_t handle_count = 0u;
    uint32_t reply_handles[1] = {0u};
    uint32_t reply_handle_count = 0u;
    uint32_t size = 0u;
    uint32_t status;
    uint32_t wire_operation = operation;
    uint32_t message_size;
    int fused_hello;

    if (client == NULL || client->port_service == 0u || request == NULL ||
        reply == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    lane = selected_lane;
    if (operation != ASTRA_VFS_OP_HELLO &&
        port_client_session(client) == ASTRA_VFS_SESSION_INVALID &&
        client->port_accelerator_ops != NULL) {
        AstraVfsRequest *retry;

        status = ensure_accelerated_connection(client);
        if (status != ASTRA_VFS_OK)
            return status;
        if (request->size == ASTRA_VFS_RENAME_REQUEST_SIZE) {
            port_call.outgoing.rename.request =
                *(const AstraVfsRenameRequest *)request;
            retry = &port_call.outgoing.rename.request.request;
        } else {
            port_call.outgoing.standard.request = *request;
            retry = &port_call.outgoing.standard.request;
        }
        retry->session = port_client_session(client);
        retry->version = client->version;
        return astra_vfs_port_transport(client, operation, retry, reply);
    }
    outgoing = &port_call.outgoing;
    incoming = &port_call.incoming;
    area_request = &port_call.area_request;
    message_size = request_message_size(operation, request);
    if (message_size == 0u)
        return ASTRA_VFS_ERR_INVALID;
    fused_hello = port_client_session(client) ==
                      ASTRA_VFS_SESSION_INVALID &&
                  client->version >= UINT16_C(8) &&
                  first_operation(operation);
    if (operation != ASTRA_VFS_OP_HELLO &&
        port_client_session(client) == ASTRA_VFS_SESSION_INVALID &&
        !fused_hello) {
        AstraVfsRequest *retry;

        status = astra_vfs_connect(client, astra_vfs_port_transport, client);
        if (status != ASTRA_VFS_OK)
            return status;
        client->port_area_capable = 1u;
        if (request->size == ASTRA_VFS_RENAME_REQUEST_SIZE) {
            outgoing->rename.request =
                *(const AstraVfsRenameRequest *)request;
            retry = &outgoing->rename.request.request;
        } else {
            outgoing->standard.request = *request;
            retry = &outgoing->standard.request;
        }
        retry->session = port_client_session(client);
        retry->version = client->version;
        return astra_vfs_port_transport(client, operation, retry, reply);
    }
    if (operation != ASTRA_VFS_OP_HELLO &&
        operation != ASTRA_VFS_OP_BIND_LANE &&
        client->port_direct_address == NULL) {
        if (lane == NULL)
            lane = port_lane(client);
        status = ensure_thread_lane(client, lane);
        if (status != ASTRA_VFS_OK)
            return status;
    }
    if (operation == ASTRA_VFS_OP_READDIR_AREA) {
        uint32_t record_max = ASTRA_VFS_DIRENT_HEADER +
                              ASTRA_VFS_NAME_MAX - 1u;

        if (client->version < UINT16_C(10))
            return ASTRA_VFS_ERR_UNSUPPORTED;
        if (request->length == 0u ||
            request->length > ASTRA_VFS_BULK_MAX / record_max)
            return ASTRA_VFS_ERR_INVALID;
        *area_request = *request;
        status = client->port_direct_address != NULL ?
            ensure_direct_area(client, request->length * record_max) :
            ensure_area_size(client, request->length * record_max);
        if (status != ASTRA_VFS_OK)
            return status;
        area_request->session = port_client_session(client);
        area_request->version = client->version;
        request = area_request;
    }
    if (fused_hello)
        wire_operation = ASTRA_VFS_OP_HELLO;
    if (client->port_direct_address != NULL &&
        client->port_accelerator_ops != NULL &&
        operation != ASTRA_VFS_OP_BYE) {
        if (operation == ASTRA_VFS_OP_READDIR_AREA ||
            operation == ASTRA_VFS_OP_BIND_AREA)
            lane = port_lane(client);
        status = operation == ASTRA_VFS_OP_READDIR_AREA ?
            client->port_accelerator_ops->bulk(
                client, operation, request, lane->direct_address,
                lane->direct_size, reply) :
            client->port_accelerator_ops->transport(
                client, operation, request, reply);
        if (operation == ASTRA_VFS_OP_BIND_AREA &&
            lane->area_send != 0u) {
            (void)astra_close(lane->area_send);
            lane->area_send = 0u;
        }
        return status;
    }
    if (lane == NULL)
        lane = port_lane(client);
    /*
     * Version 3 moves the send handle once, during HELLO. If the peer negotiates
     * version 2, the reply below closes this pair and the next request creates
     * another one, preserving the old transport during a rolling update.
     */
    if (operation == ASTRA_VFS_OP_BIND_AREA) {
        if (lane->area_send == 0u)
            return ASTRA_VFS_ERR_BAD_HANDLE;
        handles[0] = lane->area_send;
        handle_count = 1u;
    } else if (wire_operation == ASTRA_VFS_OP_HELLO ||
               wire_operation == ASTRA_VFS_OP_BIND_LANE ||
               client->version < UINT16_C(3)) {
        status = duplicate_reply_send(lane, &lane->reply_send);
        if (status != ASTRA_SYSCALL_OK)
            return status == ASTRA_SYSCALL_RESOURCE_LIMIT ?
                ASTRA_VFS_ERR_LIMIT : ASTRA_VFS_ERR_BAD_HANDLE;
        handles[0] = lane->reply_send;
        handle_count = 1u;
    }
    if (request->size == ASTRA_VFS_RENAME_REQUEST_SIZE) {
        outgoing->rename.request = *(const AstraVfsRenameRequest *)request;
        wire_header = &outgoing->rename.header;
        wire_request = &outgoing->rename.request.request;
    } else {
        outgoing->standard.request = *request;
        wire_header = &outgoing->standard.header;
        wire_request = &outgoing->standard.request;
    }
    fill_header(wire_header, wire_operation, message_size,
                client->version >= UINT16_C(20) ? lane->owner_thread :
                                                  request->activity);
    if (fused_hello)
        wire_request->file = operation;

    status = astra_port_send(client->port_service, outgoing,
                             message_size, handles, handle_count);
    if (status != ASTRA_SYSCALL_OK) {
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
    status = astra_wait_one(lane->reply_receive,
                            ASTRA_DEADLINE_FOREVER, NULL);
    if (status == ASTRA_SYSCALL_OK) {
        status = astra_port_receive(lane->reply_receive, incoming,
                                    sizeof(*incoming), reply_handles, 1u,
                                    &size, &reply_handle_count);
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
    if (size != sizeof(*incoming) || reply_handle_count > 1u ||
        (reply_handle_count != 0u &&
         (wire_operation != ASTRA_VFS_OP_HELLO ||
          incoming->reply.version < UINT16_C(18))) ||
        incoming->header.protocol != ASTRA_VFS_PROTOCOL ||
        incoming->header.operation != wire_operation ||
        (client->version >= UINT16_C(20) &&
         incoming->header.transaction_id != lane->owner_thread) ||
        incoming->reply.size != ASTRA_VFS_REPLY_SIZE) {
        port_client_fail(client);
        if (reply_handle_count != 0u)
            (void)astra_close(reply_handles[0]);
        return ASTRA_VFS_ERR_PROTOCOL;
    }
    *reply = incoming->reply;
    if (reply_handle_count == 1u) {
        status = client->port_accelerator_ops != NULL ?
            client->port_accelerator_ops->connect(client,
                                                  reply_handles[0]) :
            ASTRA_VFS_ERR_UNSUPPORTED;
        if (status != ASTRA_VFS_OK)
            (void)astra_close(reply_handles[0]);
    }
    if (fused_hello && reply->session != ASTRA_VFS_SESSION_INVALID) {
        port_client_publish_session(client, reply->session, reply->version);
        lane->session = reply->session;
        if (reply_handle_count == 1u) {
            AstraVfsRequest retry = *request;

            retry.session = port_client_session(client);
            retry.version = client->version;
            if (client->port_direct_address != NULL)
                return client->port_accelerator_ops->transport(
                    client, operation, &retry, reply);
            return astra_vfs_port_transport(client, operation, &retry, reply);
        }
        if (reply->version < UINT16_C(8) && reply->status == ASTRA_VFS_OK) {
            AstraVfsRequest retry = *request;

            retry.session = port_client_session(client);
            retry.version = client->version;
            return astra_vfs_port_transport(client, operation, &retry, reply);
        }
    }
    if (operation == ASTRA_VFS_OP_HELLO &&
        reply->session != ASTRA_VFS_SESSION_INVALID) {
        port_client_publish_session(client, reply->session, reply->version);
        lane->session = reply->session;
    }
    return ASTRA_VFS_OK;
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
     * a bulk path read, which reaches here before the ordinary operation has
     * fused HELLO with it.  Establish the session first: otherwise the nested
     * HELLO overwrites client->request, and the retried bind publishes a
     * zero-length area.
     */
    if (port_client_session(client) == ASTRA_VFS_SESSION_INVALID) {
        status = astra_vfs_connect(client, astra_vfs_port_transport, client);
        if (status != ASTRA_VFS_OK)
            return status;
        client->port_area_capable = 1u;
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
    if (status != ASTRA_SYSCALL_OK)
        goto fail;
    status = astra_rt_handle_duplicate(
        lane->area,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
            ASTRA_RIGHT_TRANSFER,
        &lane->area_send);
    if (status != ASTRA_SYSCALL_OK)
        goto fail;
    lane->area_address = address;
    lane->area_size = size;
    prepare_request(client, ASTRA_VFS_OP_BIND_AREA);
    port_call.request.length = size;
    status = astra_vfs_port_transport(client, ASTRA_VFS_OP_BIND_AREA,
                                      &port_call.request, &port_call.reply);
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
    return status == ASTRA_SYSCALL_RESOURCE_LIMIT ? ASTRA_VFS_ERR_LIMIT :
                                                    ASTRA_VFS_ERR_IO;
}

/* The client's own path copy; vfs_client.c keeps its set_path to itself. */
static int port_set_path(AstraVfsRequest *request, const char *path)
{
    uint32_t at = 0u;

    while (path[at] != '\0') {
        if (at + 1u >= ASTRA_VFS_PATH_MAX)
            return 0;
        request->body.path[at] = (uint8_t)path[at];
        ++at;
    }
    request->body.path[at] = 0u;
    return 1;
}

static int
direct_accelerated(const AstraVfsClient *client)
{
    return client->port_direct_address != NULL &&
           client->port_accelerator_ops != NULL;
}

static uint32_t
port_read_path_call(AstraVfsClient *client, const char *path,
                    const uint8_t **bytes, uint32_t *moved,
                    uint64_t *node_size)
{
    AstraVfsPortLane *lane;
    uint32_t status;

    if (client == NULL || path == NULL || bytes == NULL || moved == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *bytes = NULL;
    *moved = 0u;
    if (node_size != NULL)
        *node_size = 0u;
    if (client->version < UINT16_C(5))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    status = direct_accelerated(client) ?
        ensure_direct_area(client, VFS_PORT_AREA_INITIAL) :
        ensure_area_size(client, VFS_PORT_AREA_INITIAL);
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
        if (!port_set_path(&port_call.request, path))
            return ASTRA_VFS_ERR_INVALID;
        port_call.request.length = direct_accelerated(client) ?
            lane->direct_size : lane->area_size;
        status = direct_accelerated(client) ?
            client->port_accelerator_ops->bulk(
                client, ASTRA_VFS_OP_READ_PATH, &port_call.request,
                lane->direct_address, lane->direct_size,
                &port_call.reply) :
            astra_vfs_port_transport(client, ASTRA_VFS_OP_READ_PATH,
                                     &port_call.request, &port_call.reply);
        if (status != ASTRA_VFS_OK)
            return status;
        if (node_size != NULL)
            *node_size = port_call.reply.node_size;
        if (port_call.reply.status != ASTRA_VFS_ERR_LIMIT ||
            attempt != 0u ||
            port_call.reply.node_size <= port_call.request.length ||
            port_call.reply.node_size > ASTRA_VFS_BULK_MAX)
            break;
        status = direct_accelerated(client) ?
            ensure_direct_area(client,
                               (uint32_t)port_call.reply.node_size) :
            ensure_area_size(client, (uint32_t)port_call.reply.node_size);
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
    status = astra_vfs_port_client_enter(client);
    if (status == ASTRA_VFS_OK) {
        status = port_read_path_call(client, path, bytes, moved, node_size);
        astra_vfs_port_client_leave(client);
    }
    return status;
}

uint32_t
astra_vfs_port_read_path_inline(AstraVfsClient *client, const char *path,
                                const uint8_t **bytes, uint32_t *moved,
                                uint64_t *node_size)
{
    uint32_t status;

    if (client == NULL || path == NULL || bytes == NULL || moved == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *bytes = NULL;
    *moved = 0u;
    if (node_size != NULL)
        *node_size = 0u;
    if (client->version < UINT16_C(7))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    prepare_request(client, ASTRA_VFS_OP_READ_PATH);
    if (!port_set_path(&port_call.request, path))
        return ASTRA_VFS_ERR_INVALID;
    port_call.request.length = ASTRA_VFS_IO_MAX;
    status = astra_vfs_port_transport(client, ASTRA_VFS_OP_READ_PATH,
                                      &port_call.request, &port_call.reply);
    if (status != ASTRA_VFS_OK)
        return status;
    if (node_size != NULL)
        *node_size = port_call.reply.node_size;
    if (port_call.reply.status != ASTRA_VFS_OK)
        return port_call.reply.status;
    if (port_call.reply.count > ASTRA_VFS_IO_MAX)
        return ASTRA_VFS_ERR_PROTOCOL;
    *bytes = port_call.reply.payload;
    *moved = port_call.reply.count;
    return ASTRA_VFS_OK;
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
    if (client->version < UINT16_C(3))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    if (client->direct_backend_ops != NULL) {
        AstraVfsPortLane *direct_lane;

        status = ensure_direct_area(client, length);
        if (status != ASTRA_VFS_OK)
            return status;
        direct_lane = port_lane(client);
        status = client->direct_backend_ops->read(
            client->direct_backend_context, file, offset,
            direct_lane->direct_address, length, moved);
        if (status != ASTRA_VFS_OK)
            return status;
        if (*moved > length)
            return ASTRA_VFS_ERR_PROTOCOL;
        *bytes = direct_lane->direct_address;
        return ASTRA_VFS_OK;
    }
    status = direct_accelerated(client) ? ensure_direct_area(client, length) :
                                          ensure_area_size(client, length);
    if (status != ASTRA_VFS_OK)
        return status;
    lane = port_lane(client);
    if (length > (direct_accelerated(client) ? lane->direct_size :
                                               lane->area_size))
        return ASTRA_VFS_ERR_LIMIT;
    prepare_request(client, ASTRA_VFS_OP_READ_AREA);
    port_call.request.file = file;
    port_call.request.offset = offset;
    port_call.request.length = length;
    status = direct_accelerated(client) ?
        client->port_accelerator_ops->bulk(
            client, ASTRA_VFS_OP_READ_AREA, &port_call.request,
            lane->direct_address, lane->direct_size,
            &port_call.reply) :
        astra_vfs_port_transport(client, ASTRA_VFS_OP_READ_AREA,
                                 &port_call.request, &port_call.reply);
    if (status != ASTRA_VFS_OK)
        return status;
    if (port_call.reply.status != ASTRA_VFS_OK)
        return port_call.reply.status;
    if (port_call.reply.count > length)
        return ASTRA_VFS_ERR_PROTOCOL;
    *bytes = astra_vfs_port_call_area(client, NULL);
    *moved = port_call.reply.count;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_port_read_borrow(AstraVfsClient *client, AstraVfsFile file,
                           uint64_t offset, uint32_t length,
                           const uint8_t **bytes, uint32_t *moved)
{
    uint32_t status;

    if (client == NULL || bytes == NULL || moved == NULL || length == 0u)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_vfs_port_client_enter(client);
    if (status == ASTRA_VFS_OK) {
        status = port_read_borrow_call(client, file, offset, length, bytes,
                                       moved);
        astra_vfs_port_client_leave(client);
    }
    return status;
}

static uint32_t
port_read_bulk_call(AstraVfsClient *client, AstraVfsFile file,
                    uint64_t offset, void *buffer, uint32_t length,
                    uint32_t *moved)
{
    AstraVfsPortLane *lane;
    uint8_t *out = buffer;
    const uint8_t *shared;
    uint32_t status;
    uint32_t index;

    if (client == NULL || buffer == NULL || moved == NULL || length == 0u)
        return ASTRA_VFS_ERR_INVALID;
    *moved = 0u;
    if (client->version < UINT16_C(3))
        return astra_vfs_read(client, file, offset, buffer,
                              length < ASTRA_VFS_IO_MAX ? length :
                                                          ASTRA_VFS_IO_MAX,
                              moved);
    if (client->direct_backend_ops != NULL) {
        status = client->direct_backend_ops->read(
            client->direct_backend_context, file, offset, buffer, length,
            moved);
        return status == ASTRA_VFS_OK && *moved > length ?
            ASTRA_VFS_ERR_PROTOCOL : status;
    }
    if (direct_accelerated(client)) {
        prepare_request(client, ASTRA_VFS_OP_READ_AREA);
        port_call.request.file = file;
        port_call.request.offset = offset;
        port_call.request.length = length;
        status = client->port_accelerator_ops->bulk(
            client, ASTRA_VFS_OP_READ_AREA, &port_call.request, buffer,
            length, &port_call.reply);
        if (status != ASTRA_VFS_OK)
            return status;
        if (port_call.reply.status != ASTRA_VFS_OK)
            return port_call.reply.status;
        if (port_call.reply.count > length)
            return ASTRA_VFS_ERR_PROTOCOL;
        *moved = port_call.reply.count;
        return ASTRA_VFS_OK;
    }
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
    status = astra_vfs_port_transport(client, ASTRA_VFS_OP_READ_AREA,
                                      &port_call.request, &port_call.reply);
    if (status != ASTRA_VFS_OK)
        return status;
    if (port_call.reply.status != ASTRA_VFS_OK)
        return port_call.reply.status;
    if (port_call.reply.count > length)
        return ASTRA_VFS_ERR_PROTOCOL;
    shared = lane->area_address;
    (void)index;
    memcpy(out, shared, port_call.reply.count);
    *moved = port_call.reply.count;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_port_read_bulk(AstraVfsClient *client, AstraVfsFile file,
                         uint64_t offset, void *buffer, uint32_t length,
                         uint32_t *moved)
{
    uint32_t status;

    if (client == NULL || buffer == NULL || moved == NULL || length == 0u)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_vfs_port_client_enter(client);
    if (status == ASTRA_VFS_OK) {
        status = port_read_bulk_call(client, file, offset, buffer, length,
                                     moved);
        astra_vfs_port_client_leave(client);
    }
    return status;
}

static uint32_t
port_write_bulk_call(AstraVfsClient *client, AstraVfsFile file,
                     uint64_t offset, uint32_t flags, const void *buffer,
                     uint32_t length, uint32_t *moved, uint64_t *position)
{
    AstraVfsPortLane *lane;
    uint32_t status;

    if (client == NULL || buffer == NULL || moved == NULL ||
        position == NULL || length == 0u ||
        (flags & ~ASTRA_VFS_OPEN_APPEND) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    *moved = 0u;
    *position = offset;
    if ((flags & ASTRA_VFS_OPEN_APPEND) != 0u &&
        client->version < UINT16_C(21))
        return astra_vfs_write_position(
            client, file, offset, buffer,
            length < ASTRA_VFS_IO_MAX ? length : ASTRA_VFS_IO_MAX,
            moved, position);
    if (client->transport != astra_vfs_port_transport ||
        client->version < UINT16_C(9))
        return astra_vfs_write_position(
            client, file, offset, buffer,
            length < ASTRA_VFS_IO_MAX ? length : ASTRA_VFS_IO_MAX,
            moved, position);
    if (client->direct_backend_ops != NULL) {
        status = client->direct_backend_ops->write(
            client->direct_backend_context, file, offset, flags, buffer,
            length, moved, position);
        return status == ASTRA_VFS_OK && *moved > length ?
            ASTRA_VFS_ERR_PROTOCOL : status;
    }
    if (direct_accelerated(client)) {
        prepare_request(client, ASTRA_VFS_OP_WRITE_AREA);
        port_call.request.file = file;
        port_call.request.offset = offset;
        port_call.request.length = length;
        port_call.request.flags = flags;
        status = client->port_accelerator_ops->bulk(
            client, ASTRA_VFS_OP_WRITE_AREA, &port_call.request,
            (void *)buffer, length, &port_call.reply);
        if (status != ASTRA_VFS_OK)
            return status;
        if (port_call.reply.status != ASTRA_VFS_OK)
            return port_call.reply.status;
        if (port_call.reply.count > length)
            return ASTRA_VFS_ERR_PROTOCOL;
        *moved = port_call.reply.count;
        *position = port_call.reply.node_size;
        return ASTRA_VFS_OK;
    }
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
    status = astra_vfs_port_transport(client, ASTRA_VFS_OP_WRITE_AREA,
                                      &port_call.request, &port_call.reply);
    if (status != ASTRA_VFS_OK)
        return status;
    if (port_call.reply.status != ASTRA_VFS_OK)
        return port_call.reply.status;
    if (port_call.reply.count > length)
        return ASTRA_VFS_ERR_PROTOCOL;
    *moved = port_call.reply.count;
    *position = client->version >= UINT16_C(21) ?
                    port_call.reply.node_size : offset + *moved;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_port_write_bulk(AstraVfsClient *client, AstraVfsFile file,
                          uint64_t offset, const void *buffer, uint32_t length,
                          uint32_t *moved)
{
    uint64_t position = offset;

    return astra_vfs_port_write_bulk_position(
        client, file, offset, 0u, buffer, length, moved, &position);
}

uint32_t
astra_vfs_port_write_bulk_position(
    AstraVfsClient *client, AstraVfsFile file, uint64_t offset,
    uint32_t flags, const void *buffer, uint32_t length, uint32_t *moved,
    uint64_t *position)
{
    uint32_t status;

    if (client == NULL || buffer == NULL || moved == NULL ||
        position == NULL || length == 0u)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_vfs_port_client_enter(client);
    if (status == ASTRA_VFS_OK) {
        status = port_write_bulk_call(client, file, offset, flags, buffer,
                                      length, moved, position);
        astra_vfs_port_client_leave(client);
    }
    return status;
}

static int
port_state_acquire(const AstraVfsPortService *host)
{
    return host->state_acquire == NULL ||
           host->state_acquire(host->state_lock_context);
}

static void
port_state_release(const AstraVfsPortService *host)
{
    if (host->state_release != NULL)
        host->state_release(host->state_lock_context);
}

static void
port_refuse(AstraVfsPortService *host)
{
    if (port_state_acquire(host)) {
        ++host->refused;
        port_state_release(host);
    }
}

typedef struct AstraVfsReplyLease {
    int slot;
    uint32_t session;
    uint32_t lane;
    uint32_t handle;
    uint8_t *area;
    uint32_t area_size;
    int active;
} AstraVfsReplyLease;

typedef struct AstraVfsReplyResources {
    uint32_t session;
    uint32_t reply_handle;
    uint32_t area_handle;
    uint8_t *area_address;
} AstraVfsReplyResources;

static int
reply_acquire(AstraVfsPortService *host, uint32_t session, uint32_t lane,
              AstraVfsReplyLease *lease)
{
    uint32_t index;

    lease->slot = -1;
    lease->session = session;
    lease->lane = lane;
    lease->handle = 0u;
    lease->area = NULL;
    lease->area_size = 0u;
    lease->active = 0;
    if (session == ASTRA_VFS_SESSION_INVALID || !port_state_acquire(host))
        return 0;
    for (index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index) {
        if (host->reply_sessions[index] != session ||
            host->reply_lanes[index] != lane)
            continue;
        ++host->reply_references[index];
        lease->slot = (int)index;
        lease->handle = host->reply_handles[index];
        lease->area = host->area_addresses[index];
        lease->area_size = host->area_sizes[index];
        if (host->reply_references[index] == 1u)
            lease->active = 1;
        break;
    }
    port_state_release(host);
    return lease->slot >= 0;
}

static int
reply_bind_acquire(AstraVfsPortService *host, uint32_t session,
                   uint32_t lane, uint32_t handle,
                   AstraVfsReplyLease *lease)
{
    uint32_t index;

    lease->slot = -1;
    lease->session = session;
    lease->lane = lane;
    lease->handle = 0u;
    lease->area = NULL;
    lease->area_size = 0u;
    lease->active = 0;
    if (!port_state_acquire(host))
        return -1;
    for (index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index) {
        if (host->reply_sessions[index] == session &&
            host->reply_lanes[index] == lane) {
            port_state_release(host);
            return -1;
        }
    }
    for (index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index) {
        if (host->reply_sessions[index] == 0u) {
            host->reply_sessions[index] = session;
            host->reply_lanes[index] = lane;
            host->reply_handles[index] = handle;
            host->reply_references[index] = 1u;
            host->reply_closing[index] = 0u;
            lease->slot = (int)index;
            lease->handle = handle;
            lease->active = 1;
            port_state_release(host);
            return (int)index;
        }
    }
    port_state_release(host);
    return -1;
}

static void
reply_resources_close(const AstraVfsReplyResources *resources)
{
    if (resources->area_address != NULL)
        (void)astra_rt_area_unmap(resources->area_address);
    if (resources->area_handle != 0u)
        (void)astra_close(resources->area_handle);
    if (resources->reply_handle != 0u)
        (void)astra_close(resources->reply_handle);
}

static void
reply_detach_locked(AstraVfsPortService *host, uint32_t index,
                    AstraVfsReplyResources *resources)
{
    resources->session = host->reply_sessions[index];
    resources->reply_handle = host->reply_handles[index];
    resources->area_handle = host->area_handles[index];
    resources->area_address = host->area_addresses[index];
    host->reply_sessions[index] = 0u;
    host->reply_lanes[index] = 0u;
    host->reply_handles[index] = 0u;
    host->area_handles[index] = 0u;
    host->area_addresses[index] = NULL;
    host->area_sizes[index] = 0u;
    host->reply_references[index] = 0u;
    host->reply_closing[index] = 0u;
}

static void
reply_finish(AstraVfsPortService *host, const AstraVfsReplyLease *lease,
             int close_lane)
{
    AstraVfsReplyResources resources = {0};
    int last = 0;
    uint32_t index;

    if (lease->slot < 0 || !port_state_acquire(host))
        return;
    index = (uint32_t)lease->slot;
    if (host->reply_sessions[index] == lease->session &&
        host->reply_lanes[index] == lease->lane) {
        if (host->reply_references[index] != 0u)
            --host->reply_references[index];
        if (close_lane)
            host->reply_closing[index] = 1u;
        if (host->reply_closing[index] != 0u &&
            host->reply_references[index] == 0u) {
            reply_detach_locked(host, index, &resources);
            last = 1;
            for (uint32_t at = 0u; at < ASTRA_VFS_SESSION_MAX; ++at)
                if (host->reply_sessions[at] == lease->session) {
                    last = 0;
                    break;
                }
        }
    }
    port_state_release(host);
    reply_resources_close(&resources);
    if (last)
        astra_vfs_service_release_session(host->service, resources.session);
}

static void
reply_session_close(AstraVfsPortService *host, uint32_t session)
{
    for (;;) {
        AstraVfsReplyResources resources = {0};

        if (!port_state_acquire(host))
            return;
        for (uint32_t index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index) {
            if (host->reply_sessions[index] != session)
                continue;
            host->reply_closing[index] = 1u;
            if (host->reply_references[index] == 0u) {
                reply_detach_locked(host, index, &resources);
                break;
            }
        }
        port_state_release(host);
        reply_resources_close(&resources);
        if (resources.session == 0u)
            break;
    }
    astra_vfs_service_release_session(host->service, session);
}

static void
reply_reap_dead(AstraVfsPortService *host)
{
    for (uint32_t index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index) {
        AstraVfsReplyResources resources = {0};
        uint32_t handle;
        uint32_t session;
        uint32_t status;

        if (!port_state_acquire(host))
            return;
        session = host->reply_sessions[index];
        handle = host->reply_handles[index];
        if (host->reply_references[index] != 0u) {
            port_state_release(host);
            continue;
        }
        port_state_release(host);
        if (session == ASTRA_VFS_SESSION_INVALID)
            continue;
        status = astra_wait_one(handle, 0u, NULL);
        if (status != ASTRA_SYSCALL_PEER_DEAD &&
            status != ASTRA_SYSCALL_CLOSED)
            continue;
        if (!port_state_acquire(host))
            return;
        if (host->reply_sessions[index] == session &&
            host->reply_handles[index] == handle &&
            host->reply_references[index] == 0u)
            reply_detach_locked(host, index, &resources);
        port_state_release(host);
        reply_resources_close(&resources);
        if (resources.session != 0u) {
            int last = 1;

            if (port_state_acquire(host)) {
                for (uint32_t at = 0u; at < ASTRA_VFS_SESSION_MAX; ++at)
                    if (host->reply_sessions[at] == session) {
                        last = 0;
                        break;
                    }
                port_state_release(host);
            } else {
                last = 0;
            }
            if (last)
                astra_vfs_service_release_session(host->service, session);
        }
    }
}

int
astra_vfs_port_service_init(AstraVfsPortService *host, uint32_t receive,
                            AstraVfsService *service)
{
    if (host == NULL || receive == 0u || service == NULL) {
        return 0;
    }
    host->receive = receive;
    host->service = service;
    host->requests = 0u;
    host->refused = 0u;
    host->dropped = 0u;
    host->stalled = 0u;
    host->accelerator = 0u;
    host->state_acquire = NULL;
    host->state_release = NULL;
    host->state_lock_context = NULL;
    for (uint32_t index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index) {
        host->reply_sessions[index] = 0u;
        host->reply_lanes[index] = 0u;
        host->reply_handles[index] = 0u;
        host->area_handles[index] = 0u;
        host->area_addresses[index] = NULL;
        host->area_sizes[index] = 0u;
        host->reply_references[index] = 0u;
        host->reply_closing[index] = 0u;
    }
    return 1;
}

int
astra_vfs_port_service_set_state_lock(AstraVfsPortService *host,
                                      AstraVfsStateAcquire acquire,
                                      AstraVfsStateRelease release,
                                      void *context)
{
    if (host == NULL || acquire == NULL || release == NULL ||
        host->state_acquire != NULL || host->state_release != NULL)
        return 0;
    host->state_acquire = acquire;
    host->state_release = release;
    host->state_lock_context = context;
    return 1;
}

int
astra_vfs_port_service_set_accelerator(AstraVfsPortService *host,
                                       uint32_t accelerator)
{
    if (host == NULL || accelerator == 0u || host->accelerator != 0u)
        return 0;
    host->accelerator = accelerator;
    return 1;
}

uint32_t
astra_vfs_port_service_worker_pump(AstraVfsPortService *host,
                                   AstraVfsPortWorker *worker,
                                   uint32_t budget)
{
    AstraVfsRequestMessage *incoming;
    AstraVfsReplyMessage *outgoing;
    uint32_t answered = 0u;

    if (host == NULL || worker == NULL || host->receive == 0u ||
        host->service == NULL) {
        return 0u;
    }
    incoming = &worker->incoming.standard;
    outgoing = &worker->outgoing;
    while (answered < budget) {
        AstraVfsReplyLease lease = {.slot = -1};
        uint32_t handles[1] = {0u};
        uint32_t outgoing_handles[1] = {0u};
        uint32_t handle_count = 0u;
        uint32_t outgoing_handle_count = 0u;
        uint32_t size = 0u;
        uint32_t sender = 0u;
        uint32_t previous;
        uint32_t reply_handle;
        uint32_t operation;
        uint32_t wire_operation;
        uint32_t lane_id;
        int fused_hello;
        int accelerated = 0;
        int adopted = 0;
        int sent;
        int slot = -1;
        uint32_t status = astra_port_receive_from(
            host->receive, incoming, sizeof(worker->incoming), handles, 1u,
            &size, &handle_count, &sender);

        if (status == ASTRA_SYSCALL_RESOURCE_LIMIT) {
            reply_reap_dead(host);
            status = astra_port_receive_from(
                host->receive, incoming, sizeof(worker->incoming), handles, 1u,
                &size, &handle_count, &sender);
        }

        if (status != ASTRA_SYSCALL_OK) {
            /* WOULD_BLOCK is an empty port and the ordinary way out. */
            if (status != ASTRA_SYSCALL_WOULD_BLOCK) {
                if (port_state_acquire(host)) {
                    host->stalled = status;
                    port_state_release(host);
                }
            }
            break;
        }
        /*
         * No reply handle, no reply. A request that did not say where the
         * answer goes cannot be answered, and there is nothing to close.
         */
        uint32_t expected_size = incoming->header.operation ==
                                     ASTRA_VFS_OP_RENAME ?
            (uint32_t)sizeof(AstraVfsRenameRequestMessage) :
            (uint32_t)sizeof(AstraVfsRequestMessage);
        uint16_t expected_request_size = incoming->header.operation ==
                                             ASTRA_VFS_OP_RENAME ?
            (uint16_t)ASTRA_VFS_RENAME_REQUEST_SIZE :
            (uint16_t)ASTRA_VFS_REQUEST_SIZE;
        if (size != expected_size || incoming->header.total_size != size ||
            incoming->header.protocol != ASTRA_VFS_PROTOCOL ||
            incoming->header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
            incoming->request.size != expected_request_size ||
            handle_count > 1u) {
            port_refuse(host);
            if (handle_count == 1u) {
                (void)astra_close(handles[0]);
            }
            continue;
        }

        wire_operation = incoming->header.operation;
        operation = wire_operation;
        lane_id = incoming->request.version >= UINT16_C(20) ?
            incoming->header.transaction_id : 0u;
        if (incoming->request.version >= UINT16_C(20) && lane_id == 0u) {
            port_refuse(host);
            if (handle_count != 0u)
                (void)astra_close(handles[0]);
            continue;
        }
        fused_hello = wire_operation == ASTRA_VFS_OP_HELLO &&
                      incoming->request.version >= UINT16_C(8) &&
                      first_operation(incoming->request.file);
        int persistent_hello =
            wire_operation == ASTRA_VFS_OP_HELLO && !fused_hello &&
            incoming->request.version >= UINT16_C(3);
        int session_owned = wire_operation == ASTRA_VFS_OP_HELLO ||
            astra_vfs_service_session_owned(
                host->service, incoming->request.session, sender);

        if (!session_owned) {
            port_refuse(host);
            if (handle_count != 0u)
                (void)astra_close(handles[0]);
            continue;
        }

        if (wire_operation != ASTRA_VFS_OP_HELLO &&
            wire_operation != ASTRA_VFS_OP_BIND_LANE &&
            reply_acquire(host, incoming->request.session, lane_id, &lease))
            slot = lease.slot;

        if (wire_operation == ASTRA_VFS_OP_HELLO) {
            if (handle_count != 1u) {
                port_refuse(host);
                continue;
            }
            /*
             * A dead client can strand both a reply handle and a mapped area.
             * Waiting for this service's session table to fill is too late:
             * areas are a kernel-wide pool shared by every VFS service, so
             * several individually non-full services can exhaust it first.
             * A new session is the common boundary at which every backend can
             * retire dead peers before admitting more resource ownership.
             */
            reply_reap_dead(host);
            reply_handle = handles[0];
        } else if (wire_operation == ASTRA_VFS_OP_BIND_LANE) {
            if (handle_count != 1u) {
                port_refuse(host);
                continue;
            }
            reply_handle = handles[0];
        } else if (slot >= 0) {
            uint32_t expected = operation ==
                ASTRA_VFS_OP_BIND_AREA ? 1u : 0u;

            if (handle_count != expected) {
                port_refuse(host);
                if (handle_count != 0u)
                    (void)astra_close(handles[0]);
                reply_finish(host, &lease, 0);
                continue;
            }
            reply_handle = lease.handle;
        } else {
            /* Version 2 transfers a fresh reply handle with every request. */
            if (handle_count != 1u) {
                port_refuse(host);
                continue;
            }
            reply_handle = handles[0];
        }

        if (wire_operation == ASTRA_VFS_OP_HELLO &&
            incoming->request.version >= UINT16_C(18) &&
            host->accelerator != 0u &&
            astra_rt_handle_duplicate(
                host->accelerator,
                ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                    ASTRA_RIGHT_TRANSFER,
                &outgoing_handles[0]) == ASTRA_SYSCALL_OK) {
            outgoing_handle_count = 1u;
            accelerated = 1;
        }

        clear_service_reply(&outgoing->reply, incoming->request.version,
                            incoming->request.session);
        if (slot >= 0 && !lease.active) {
            if (handle_count != 0u)
                (void)astra_close(handles[0]);
            outgoing->reply.status = ASTRA_VFS_ERR_BUSY;
            goto answer_ready;
        }

        /*
         * The service adopts the caller's activity for as long as it is
         * handling the request, so every event the service, the backend and
         * lwext4 emit underneath belongs to the story the caller began. This
         * is what astra_activity_adopt has existed unused for since the
         * activity landed: it was written for a boundary that did not exist
         * until now.
         */
        if (incoming->request.activity != 0u) {
            (void)astra_activity_exchange(incoming->request.activity,
                                          &previous);
            adopted = 1;
        }
        if (fused_hello) {
            AstraVfsRequest hello = incoming->request;

            hello.file = ASTRA_VFS_FILE_INVALID;
            astra_vfs_service_dispatch_from(
                host->service, sender, ASTRA_VFS_OP_HELLO, &hello,
                &outgoing->reply);
            if (outgoing->reply.status == ASTRA_VFS_OK) {
                slot = reply_bind_acquire(
                    host, outgoing->reply.session, lane_id, reply_handle,
                    &lease);
                if (slot < 0) {
                    astra_vfs_service_release_session(
                        host->service, outgoing->reply.session);
                    outgoing->reply.session = ASTRA_VFS_SESSION_INVALID;
                    outgoing->reply.status = ASTRA_VFS_ERR_LIMIT;
                } else {
                    operation = incoming->request.file;
                    incoming->request.file = ASTRA_VFS_FILE_INVALID;
                    incoming->request.session = outgoing->reply.session;
                }
            }
        }
        if (fused_hello && slot < 0) {
            /* HELLO already produced the refusal to return. */
        } else if (fused_hello && accelerated) {
            /* The client retries the fused operation on its local data plane. */
        } else if (operation == ASTRA_VFS_OP_BIND_LANE) {
            if (incoming->request.version < UINT16_C(20)) {
                outgoing->reply.status = ASTRA_VFS_ERR_UNSUPPORTED;
            } else {
                slot = reply_bind_acquire(
                    host, incoming->request.session, lane_id, reply_handle,
                    &lease);
                if (slot < 0) {
                    outgoing->reply.status = ASTRA_VFS_ERR_LIMIT;
                } else {
                    outgoing->reply.version = incoming->request.version;
                    outgoing->reply.session = incoming->request.session;
                    outgoing->reply.status = ASTRA_VFS_OK;
                }
            }
        } else if (operation == ASTRA_VFS_OP_BIND_AREA && slot >= 0) {
            void *address = NULL;
            void *old_address = NULL;
            uint32_t old_handle = 0u;
            uint32_t mapped = 0u;
            uint32_t map_status;
            /*
             * A rebind replaces what was there. A client grows its transfer
             * area when it first reads something large, and refusing the
             * second bind left it able only to fail.
             */
            if (port_state_acquire(host)) {
                old_address = host->area_addresses[(uint32_t)slot];
                old_handle = host->area_handles[(uint32_t)slot];
                host->area_addresses[(uint32_t)slot] = NULL;
                host->area_handles[(uint32_t)slot] = 0u;
                host->area_sizes[(uint32_t)slot] = 0u;
                port_state_release(host);
            }
            if (old_address != NULL)
                (void)astra_rt_area_unmap(old_address);
            if (old_handle != 0u)
                (void)astra_close(old_handle);
            {
                map_status = astra_rt_area_map(
                    handles[0], ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                    &address, &mapped);
                if (map_status == ASTRA_SYSCALL_OK &&
                    mapped >= incoming->request.length &&
                    incoming->request.length <= ASTRA_VFS_BULK_MAX) {
                    if (port_state_acquire(host)) {
                        host->area_handles[(uint32_t)slot] = handles[0];
                        host->area_addresses[(uint32_t)slot] = address;
                        host->area_sizes[(uint32_t)slot] =
                            incoming->request.length;
                        port_state_release(host);
                        outgoing->reply.status = ASTRA_VFS_OK;
                    } else {
                        (void)astra_rt_area_unmap(address);
                        (void)astra_close(handles[0]);
                        outgoing->reply.status = ASTRA_VFS_ERR_IO;
                    }
                } else {
                    if (address != NULL)
                        (void)astra_rt_area_unmap(address);
                    (void)astra_close(handles[0]);
                    outgoing->reply.status =
                        map_status == ASTRA_SYSCALL_RESOURCE_LIMIT ?
                            ASTRA_VFS_ERR_LIMIT : ASTRA_VFS_ERR_INVALID;
                }
            }
        } else if (operation == ASTRA_VFS_OP_READDIR_AREA && slot >= 0) {
            uint32_t used = 0u;
            uint64_t next = incoming->request.offset;
            if (incoming->request.version < UINT16_C(10)) {
                outgoing->reply.status = ASTRA_VFS_ERR_UNSUPPORTED;
            } else if (lease.area == NULL ||
                       incoming->request.length == 0u) {
                outgoing->reply.status = ASTRA_VFS_ERR_INVALID;
            } else {
                outgoing->reply.status =
                    incoming->request.version >= UINT16_C(17) &&
                    incoming->request.file != ASTRA_VFS_FILE_INVALID ?
                    astra_vfs_service_readdir_file_into(
                        host->service, incoming->request.session,
                        incoming->request.file,
                        (const char *)incoming->request.body.path,
                        incoming->request.offset, incoming->request.length,
                        lease.area, lease.area_size, &used, &next) :
                    astra_vfs_service_readdir_into(
                        host->service,
                        (const char *)incoming->request.body.path,
                        incoming->request.offset, incoming->request.length,
                        lease.area, lease.area_size, &used, &next);
                if (outgoing->reply.status == ASTRA_VFS_OK) {
                    outgoing->reply.count = used;
                    outgoing->reply.cursor = next;
                }
            }
        } else if (operation == ASTRA_VFS_OP_READ_PATH &&
                   slot >= 0) {
            uint64_t node_size = 0u;
            uint32_t got = 0u;
            void *target = lease.area;
            uint32_t capacity = lease.area_size;

            if (target == NULL && incoming->request.version >= UINT16_C(7)) {
                target = outgoing->reply.payload;
                capacity = ASTRA_VFS_IO_MAX;
            }
            if (target == NULL) {
                outgoing->reply.status = ASTRA_VFS_ERR_INVALID;
                outgoing->reply.count = 0u;
            } else {
                outgoing->reply.status = astra_vfs_service_read_path(
                    host->service, (const char *)incoming->request.body.path,
                    target, capacity, &got, &node_size);
                outgoing->reply.count =
                    outgoing->reply.status == ASTRA_VFS_OK ? got : 0u;
                outgoing->reply.node_size = node_size;
            }
        } else if (operation == ASTRA_VFS_OP_READ_AREA &&
                   slot >= 0) {
            AstraVfsRequest piece = incoming->request;
            uint32_t total = 0u;

            if (lease.area == NULL ||
                incoming->request.length == 0u ||
                incoming->request.length > lease.area_size) {
                astra_vfs_service_dispatch(host->service,
                                           ASTRA_VFS_OP_READ,
                                           &piece, &outgoing->reply);
                outgoing->reply.status = ASTRA_VFS_ERR_INVALID;
                outgoing->reply.count = 0u;
            } else {
                /*
                 * Straight into the shared area, in one backend read. This
                 * loop used to drive the inline path, so a 16 KiB transfer was
                 * 86 dispatches and 32 KiB of byte-at-a-time copying; the area
                 * saved the messages and none of the work underneath them.
                 */
                outgoing->reply.status = astra_vfs_service_read_into(
                    host->service, incoming->request.session,
                    incoming->request.file, incoming->request.offset,
                    lease.area,
                    incoming->request.length, &total);
                outgoing->reply.count =
                    outgoing->reply.status == ASTRA_VFS_OK ? total : 0u;
                (void)piece;
            }
        } else if (operation == ASTRA_VFS_OP_WRITE_AREA && slot >= 0) {
            uint32_t total = 0u;
            uint64_t position = incoming->request.offset;

            if (incoming->request.version < UINT16_C(9)) {
                outgoing->reply.status = ASTRA_VFS_ERR_UNSUPPORTED;
                outgoing->reply.count = 0u;
            } else if (lease.area == NULL ||
                       incoming->request.length == 0u ||
                       incoming->request.length > lease.area_size) {
                outgoing->reply.status = ASTRA_VFS_ERR_INVALID;
                outgoing->reply.count = 0u;
            } else {
                outgoing->reply.status =
                    astra_vfs_service_write_position_from(
                    host->service, incoming->request.session,
                    incoming->request.file, incoming->request.offset,
                    lease.area,
                    incoming->request.length, &total, &position);
                outgoing->reply.count =
                    outgoing->reply.status == ASTRA_VFS_OK ? total : 0u;
                if (outgoing->reply.status == ASTRA_VFS_OK &&
                    incoming->request.version >= UINT16_C(21))
                    outgoing->reply.node_size = position;
            }
        } else {
            astra_vfs_service_dispatch_from(
                host->service, sender, operation, &incoming->request,
                &outgoing->reply);
        }
answer_ready:
        /*
         * Restored before the reply goes out, so the next thing this thread
         * does -- including serving somebody else -- is not still inside the
         * last caller's story.
         */
        if (adopted)
            (void)astra_activity_adopt(previous);

        fill_header(&outgoing->header, wire_operation,
                    (uint32_t)sizeof(*outgoing),
                    incoming->header.transaction_id);
        if (persistent_hello && outgoing->reply.status == ASTRA_VFS_OK) {
            slot = reply_bind_acquire(host, outgoing->reply.session, lane_id,
                                      reply_handle, &lease);
            if (slot < 0) {
                astra_vfs_service_release_session(host->service,
                                                  outgoing->reply.session);
                outgoing->reply.session = ASTRA_VFS_SESSION_INVALID;
                outgoing->reply.status = ASTRA_VFS_ERR_LIMIT;
            }
        }
        if (outgoing->reply.status != ASTRA_VFS_OK &&
            outgoing_handle_count != 0u) {
            (void)astra_close(outgoing_handles[0]);
            outgoing_handles[0] = 0u;
            outgoing_handle_count = 0u;
        }
        sent = astra_port_send(reply_handle, outgoing, sizeof(*outgoing),
                               outgoing_handles,
                               outgoing_handle_count) == ASTRA_SYSCALL_OK;
        if (!sent && outgoing_handle_count != 0u)
            (void)astra_close(outgoing_handles[0]);
        if (sent) {
            if (port_state_acquire(host)) {
                ++host->requests;
                port_state_release(host);
            }
            ++answered;
        } else {
            /*
             * The work was done and the answer had nowhere to go: the caller
             * closed its reply port, or died holding it. Counted rather than
             * retried -- there is no second address to try.
             */
            if (port_state_acquire(host)) {
                ++host->dropped;
                port_state_release(host);
            }
        }
        if (lease.slot >= 0) {
            reply_finish(host, &lease, !sent);
        } else {
            (void)astra_close(reply_handle);
        }
        if (operation == ASTRA_VFS_OP_BYE)
            reply_session_close(host, incoming->request.session);
    }
    return answered;
}

uint32_t
astra_vfs_port_service_pump(AstraVfsPortService *host, uint32_t budget)
{
    return host == NULL ? 0u :
        astra_vfs_port_service_worker_pump(host, &host->adapter_worker,
                                           budget);
}
