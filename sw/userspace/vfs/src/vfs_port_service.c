/* Native VFS service pump; client transport remains independently linkable. */
#include <astra/vfs_port_transport.h>
#include <astra/vfs_port_receive.h>
#include <astra/secret_memory.h>
#include <astra/vfs_backend.h>
#include <astra/vfs_path_payload.h>
#include <astra/vfs_reply.h>
#include <astra/vfs_path_storage.h>
#include <astra/bytes.h>
#include <astra/runtime.h>
#include <astra/utf8.h>
#include <stddef.h>
#include <string.h>

#ifdef ASTRA_VFS_TEST_HOOKS
uint32_t astra_vfs_test_mutex_lock(volatile uint32_t *state);
uint32_t astra_vfs_test_mutex_unlock(volatile uint32_t *state);
#define astra_mutex_lock(state) astra_vfs_test_mutex_lock(state)
#define astra_mutex_unlock(state) astra_vfs_test_mutex_unlock(state)
#endif

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

int
astra_vfs_port_quota_storage(uint32_t element_size, AstraArea *storage,
                             uint32_t *capacity)
{
    uint32_t handle = 0u;
    uint32_t bytes;
    uint32_t span = 0u;
    uint32_t count;
    void *address = NULL;

    if (storage == NULL || capacity == NULL)
        return 0;
    *capacity = 0u;
    if (element_size == 0u || storage->handle != ASTRA_INVALID_HANDLE || storage->address != NULL)
        return 0;
    bytes = element_size >
                    ASTRA_AREA_SIZE_MAX / ASTRA_VFS_FILE_HANDLE_MAX ?
        ASTRA_AREA_SIZE_MAX : element_size * ASTRA_VFS_FILE_HANDLE_MAX;
    uint32_t status = astra_rt_area_create_flagged(
            bytes,
            ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP,
            ASTRA_AREA_CREATE_RESERVED, &handle);
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_log_failure("VFS file storage reserve", status);
        return 0;
    }
    status = astra_rt_area_map(handle, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                              &address, &span);
    if (status != ASTRA_SYSCALL_OK || address == NULL) {
        (void)astra_log_failure("VFS file storage map", status == ASTRA_SYSCALL_OK ?
            ASTRA_SYSCALL_BAD_ADDRESS : status);
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
    /* The owner retains both resources, and may retire them before exit. */
    storage->handle = handle;
    storage->address = address;
    storage->size = span;
    storage->map_flags = ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE;
    *capacity = count;
    return 1;
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
    uint32_t sessions[ASTRA_VFS_SESSION_MAX];
    uint32_t handles[ASTRA_VFS_SESSION_MAX];

    /* Snapshot admission once; probes run unlocked and retirement revalidates. */
    if (!port_state_acquire(host))
        return;
    for (uint32_t index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index) {
        sessions[index] = host->reply_references[index] == 0u ?
            host->reply_sessions[index] : ASTRA_VFS_SESSION_INVALID;
        handles[index] = host->reply_handles[index];
    }
    port_state_release(host);
    for (uint32_t index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index) {
        AstraVfsReplyResources resources = {0};
        uint32_t session = sessions[index];
        uint32_t handle = handles[index];
        uint32_t status;

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
    host->derive = NULL;
    host->derive_context = NULL;
    host->config_crypt = NULL;
    host->config_crypt_context = NULL;
    host->file_area = NULL;
    host->file_area_context = NULL;
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
astra_vfs_port_service_set_derive(AstraVfsPortService *host,
                                  AstraVfsPortDerive derive, void *context)
{
    if (host == NULL || derive == NULL || host->derive != NULL)
        return 0;
    host->derive = derive;
    host->derive_context = context;
    return 1;
}

int
astra_vfs_port_service_set_config_crypt(AstraVfsPortService *host,
    AstraVfsPortConfigCrypt crypt, void *context)
{
    if (host == NULL || crypt == NULL || host->config_crypt != NULL)
        return 0;
    host->config_crypt = crypt;
    host->config_crypt_context = context;
    return 1;
}

int
astra_vfs_port_service_set_file_area(AstraVfsPortService *host,
    AstraVfsPortFileArea file_area, void *context)
{
    if (host == NULL || file_area == NULL || host->file_area != NULL)
        return 0;
    host->file_area = file_area;
    host->file_area_context = context;
    return 1;
}


static uint32_t
reply_send(uint32_t reply_handle, const void *message, uint32_t size,
           const uint32_t *handles, uint32_t count, uint32_t rights)
{
    AstraHandleDisposition dispositions[1];

    if (rights == 0u || count == 0u)
        return astra_port_send(reply_handle, message, size, handles, count);
    if (count > 1u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    dispositions[0].handle = handles[0];
    dispositions[0].rights = rights;
    return astra_port_send_rights(reply_handle, message, size, dispositions,
                                  count);
}

static void receive_reclaim(void *context)
{
    AstraVfsPortService *host = context;
    reply_reap_dead(host);
    if (host->service->backend.ops->cache_reclaim != NULL)
        host->service->backend.ops->cache_reclaim(host->service->backend.context);
}

/*
 * The receive-dispatch-reply pump, for the host's own port (`grant` NULL)
 * or for one grant's port. A grant's port is control only, and every
 * session it opens is bound to that grant (FS15): the kernel stamps the
 * sender and the receiving port, so neither can be forged from the request.
 */
static uint32_t
pump(AstraVfsPortService *host, AstraVfsPortWorker *worker, uint32_t receive,
     const AstraVfsPortGrant *grant, uint32_t budget)
{
    AstraVfsRequestMessage *incoming;
    AstraVfsReplyMessage *outgoing;
    uint32_t answered = 0u;
    const uint32_t grant_id = grant != NULL ? grant->id : 0u;
    const uint32_t features = grant != NULL ?
        ASTRA_VFS_FEATURE_GRANTS | grant->features : 0u;

    if (host == NULL || worker == NULL || receive == 0u ||
        host->service == NULL) {
        return 0u;
    }
    incoming = &worker->incoming;
    outgoing = &worker->outgoing;
    while (answered < budget) {
        AstraVfsReplyLease lease = {.slot = -1};
        AstraVfsPathStorage path_snapshot ASTRA_AUTO_VFS_PATH_STORAGE = {0};
        AstraVfsTransfer transfer = {0};
        AstraVfsPathOperands paths = {0};
        uint32_t handles[1] = {0u};
        uint32_t outgoing_handles[1] = {0u};
        uint32_t outgoing_rights = 0u;   /* 0: the handle's own rights */
        uint32_t handle_count = 0u;
        uint32_t outgoing_handle_count = 0u;
        uint32_t size = 0u;
        uint32_t sender = 0u;
        uint32_t previous;
        uint32_t reply_handle;
        uint32_t operation;
        uint32_t wire_operation;
        uint32_t lane_id;

        int adopted = 0;
        int sent;
        int slot = -1;
        uint32_t status = astra_vfs_port_receive_reclaim(
            receive, incoming, sizeof(worker->incoming), handles, 1u,
            &size, &handle_count, &sender, receive_reclaim, host);

        if (status != ASTRA_SYSCALL_OK) {
            /* An empty port or drained peer does not fail the shared service. */
            if (status != ASTRA_SYSCALL_WOULD_BLOCK &&
                status != ASTRA_SYSCALL_PEER_DEAD) {
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
        uint32_t expected_size = sizeof(AstraVfsRequestMessage);
        uint16_t expected_request_size = ASTRA_VFS_REQUEST_SIZE;
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
        lane_id = incoming->header.transaction_id;
        /* Below ASTRA_VFS_VERSION_MIN only HELLO is answered, and the
         * service core answers it ASTRA_VFS_ERR_PROTOCOL: no older request
         * layout is served. */
        if (lane_id == 0u ||
            (incoming->request.version < ASTRA_VFS_VERSION_MIN &&
             wire_operation != ASTRA_VFS_OP_HELLO)) {
            port_refuse(host);
            if (handle_count != 0u)
                (void)astra_close(handles[0]);
            continue;
        }
        int persistent_hello = wire_operation == ASTRA_VFS_OP_HELLO;
        int session_owned = wire_operation == ASTRA_VFS_OP_HELLO ||
            astra_vfs_service_session_bound(
                host->service, incoming->request.session, sender, grant_id);

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
            /* Every request but HELLO and BIND_LANE rides a bound lane. */
            port_refuse(host);
            if (handle_count != 0u)
                (void)astra_close(handles[0]);
            continue;
        }

        astra_vfs_reply_init(&outgoing->reply, incoming->request.version,
                            incoming->request.session);
        if (incoming->header.protocol_version != incoming->request.version ||
            (operation != ASTRA_VFS_OP_HELLO && incoming->request.version != ASTRA_VFS_VERSION)) {
            outgoing->reply.status = ASTRA_VFS_ERR_PROTOCOL;
            goto answer_ready;
        }
        if (grant != NULL && grant->lost != 0u) {
            outgoing->reply.status = ASTRA_VFS_ERR_LOST;
            goto answer_ready;
        }
        if (slot >= 0 && !lease.active) {
            if (handle_count != 0u)
                (void)astra_close(handles[0]);
            outgoing->reply.status = ASTRA_VFS_ERR_BUSY;
            goto answer_ready;
        }

        transfer.output = lease.area;
        transfer.output_capacity = lease.area_size;
        uint32_t operand_count = astra_vfs_path_operand_count(operation, &incoming->request);
        if (operand_count != 0u) {
            const AstraVfsPathFields *fields = &incoming->request.body.paths;
            uint32_t input_size = fields->first_length;
            if (fields->second_length > UINT32_MAX - input_size) {
                outgoing->reply.status = ASTRA_VFS_ERR_PROTOCOL;
                goto answer_ready;
            }
            input_size += fields->second_length;
            if (input_size == 0u || input_size > 2u * ASTRA_VFS_PATH_MAX ||
                input_size > lease.area_size || lease.area == NULL) {
                outgoing->reply.status = ASTRA_VFS_ERR_PROTOCOL;
                goto answer_ready;
            }
            path_snapshot = astra_vfs_path_storage_open(2u);
            if (path_snapshot.bytes == NULL) {
                outgoing->reply.status = ASTRA_VFS_ERR_LIMIT;
                goto answer_ready;
            }
            memcpy(path_snapshot.bytes, lease.area, input_size);
            transfer.input = path_snapshot.bytes;
            transfer.input_size = input_size;
            outgoing->reply.status = astra_vfs_path_unpack(fields, transfer.input,
                input_size, operand_count, &paths);
            if (outgoing->reply.status != ASTRA_VFS_OK)
                goto answer_ready;
        }
        outgoing->reply.status = operation == ASTRA_VFS_OP_CONFIG_CRYPT ?
            ASTRA_VFS_OK : astra_vfs_path_request(operation,
                &incoming->request, &transfer, &paths);
        if (outgoing->reply.status != ASTRA_VFS_OK)
            goto answer_ready;
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
        if (operation == ASTRA_VFS_OP_BIND_LANE) {
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
            if (lease.area == NULL ||
                       incoming->request.length == 0u) {
                outgoing->reply.status = ASTRA_VFS_ERR_INVALID;
            } else {
                outgoing->reply.status =
                    incoming->request.file != ASTRA_VFS_FILE_INVALID ?
                    astra_vfs_service_readdir_file_into(
                        host->service, incoming->request.session,
                        incoming->request.file,
                        paths.first,
                        incoming->request.offset, incoming->request.length,
                        lease.area, lease.area_size, &used, &next) :
                    astra_vfs_service_readdir_into(
                        host->service, incoming->request.session,
                        paths.first,
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
            void *target = NULL;
            uint32_t capacity = 0u;

            if (lease.area != NULL && incoming->request.length != 0u &&
                incoming->request.length <= lease.area_size) {
                target = lease.area;
                capacity = incoming->request.length;
            }
            if (target == NULL) {
                outgoing->reply.status = ASTRA_VFS_ERR_INVALID;
                outgoing->reply.count = 0u;
            } else {
                outgoing->reply.status = astra_vfs_service_read_path(
                    host->service, incoming->request.session, paths.first,
                    target, capacity, &got, &node_size);
                outgoing->reply.count =
                    outgoing->reply.status == ASTRA_VFS_OK ? got : 0u;
                outgoing->reply.node_size = node_size;
            }
        } else if (operation == ASTRA_VFS_OP_READ_AREA &&
                   slot >= 0) {
            uint32_t total = 0u;
            uint64_t position = incoming->request.offset;

            if (lease.area == NULL ||
                incoming->request.length == 0u ||
                incoming->request.length > lease.area_size ||
                (incoming->request.flags & ~ASTRA_VFS_IO_SEQUENTIAL) != 0u) {
                outgoing->reply.status = ASTRA_VFS_ERR_INVALID;
                outgoing->reply.count = 0u;
            } else {
                /*
                 * Straight into the shared area, in one backend read. This
                 * loop used to drive the inline path, so a 16 KiB transfer was
                 * 86 dispatches and 32 KiB of byte-at-a-time copying; the area
                 * saved the messages and none of the work underneath them.
                 */
                outgoing->reply.status = astra_vfs_service_read_position_into(
                    host->service, incoming->request.session,
                    incoming->request.file, incoming->request.offset,
                    incoming->request.flags,
                    lease.area,
                    incoming->request.length, &total, &position);
                outgoing->reply.count = total;
                outgoing->reply.position = position;
            }
        } else if (operation == ASTRA_VFS_OP_WRITE_AREA && slot >= 0) {
            uint32_t total = 0u;
            uint64_t position = incoming->request.offset;

            if (lease.area == NULL ||
                       incoming->request.length == 0u ||
                       incoming->request.length > lease.area_size) {
                outgoing->reply.status = ASTRA_VFS_ERR_INVALID;
                outgoing->reply.count = 0u;
            } else {
                outgoing->reply.status =
                    astra_vfs_service_write_position_from(
                    host->service, incoming->request.session,
                    incoming->request.file, incoming->request.offset,
                    incoming->request.flags, incoming->request.write_limit, lease.area,
                    incoming->request.length, &total, &position);
                outgoing->reply.count = total;
                outgoing->reply.position = position;
            }
        } else if (operation == ASTRA_VFS_OP_CONFIG_CRYPT) {
            uint32_t key_bytes = (uint32_t)incoming->request.offset;
            uint32_t input_length = incoming->request.length;
            uint32_t output_capacity = (uint32_t)incoming->request.write_limit;
            uint32_t output_length = 0u;
            const uint8_t *bytes = lease.area;
            if (grant == NULL || host->config_crypt == NULL ||
                (grant->features & ASTRA_VFS_FEATURE_CONFIG_CRYPT) == 0u)
                outgoing->reply.status = ASTRA_VFS_ERR_UNSUPPORTED;
            else if (slot < 0 || lease.area == NULL ||
                     incoming->request.offset > UINT32_MAX ||
                     incoming->request.write_limit > UINT32_MAX ||
                     key_bytes < 2u ||
                     key_bytes > 128u || input_length == 0u ||
                     key_bytes > lease.area_size ||
                     input_length > lease.area_size - key_bytes ||
                     output_capacity == 0u ||
                     output_capacity > lease.area_size ||
                     bytes[key_bytes - 1u] != 0u ||
                     memchr(bytes, 0, key_bytes - 1u) != NULL ||
                     incoming->request.body.paths.first_length != 0u ||
                     incoming->request.body.paths.second_length != 0u)
                outgoing->reply.status = ASTRA_VFS_ERR_INVALID;
            else {
                outgoing->reply.status = host->config_crypt(
                    host->config_crypt_context, grant, incoming->request.flags,
                    (const char *)bytes, bytes + key_bytes, input_length,
                    lease.area, output_capacity, &output_length);
                if (outgoing->reply.status == ASTRA_VFS_OK)
                    outgoing->reply.count = output_length;
            }
        } else if (operation == ASTRA_VFS_OP_FILE_AREA) {
            uint32_t area = 0u;
            uint64_t file_size = 0u;

            if (grant == NULL || host->file_area == NULL ||
                (grant->features & ASTRA_VFS_FEATURE_FILE_AREAS) == 0u)
                outgoing->reply.status = ASTRA_VFS_ERR_UNSUPPORTED;
            else if ((grant->rights & ASTRA_VFS_GRANT_READ) == 0u)
                outgoing->reply.status = ASTRA_VFS_ERR_ACCESS;
            else if (incoming->request.file == ASTRA_VFS_FILE_INVALID ||
                     incoming->request.flags != 0u ||
                     incoming->request.offset != 0u ||
                     incoming->request.length == 0u ||
                     incoming->request.length > ASTRA_AREA_SIZE_MAX ||
                     incoming->request.length > ASTRA_AREA_SIZE_MAX -
                         incoming->request.write_limit % ASTRA_AREA_SIZE_MAX ||
                     (incoming->request.length & (ASTRA_MEMORY_PAGE_SIZE - 1u)) != 0u ||
                     (incoming->request.write_limit & (ASTRA_MEMORY_PAGE_SIZE - 1u)) != 0u ||
                     incoming->request.write_limit > UINT64_MAX - incoming->request.length)
                outgoing->reply.status = ASTRA_VFS_ERR_INVALID;
            else {
                outgoing->reply.status = host->file_area(
                    host->file_area_context, grant, sender,
                    incoming->request.session, incoming->request.file,
                    incoming->request.write_limit,
                    incoming->request.length, &area, &file_size);
                if (outgoing->reply.status == ASTRA_VFS_OK && area != 0u) {
                    outgoing_handles[0] = area;
                    outgoing_handle_count = 1u;
                    outgoing_rights = ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP |
                        ((grant->rights & ASTRA_VFS_GRANT_WRITE) != 0u ? ASTRA_RIGHT_WRITE : 0u);
                    outgoing->reply.node_size = file_size;
                } else {
                    if (area != 0u) (void)astra_close(area);
                    if (outgoing->reply.status == ASTRA_VFS_OK)
                        outgoing->reply.status = ASTRA_VFS_ERR_IO;
                }
            }
        } else if (operation == ASTRA_VFS_OP_CACHE_RECLAIM) {
            if (grant == NULL ||
                (grant->features & ASTRA_VFS_FEATURE_CACHE_RECLAIM) == 0u ||
                host->service->backend.ops->cache_reclaim == NULL)
                outgoing->reply.status = ASTRA_VFS_ERR_UNSUPPORTED;
            else if ((grant->rights & ASTRA_VFS_GRANT_READ) == 0u)
                outgoing->reply.status = ASTRA_VFS_ERR_ACCESS;
            else if (incoming->request.file != ASTRA_VFS_FILE_INVALID ||
                     incoming->request.flags != 0u || incoming->request.offset != 0u ||
                     incoming->request.length != 0u || incoming->request.write_limit != 0u ||
                     incoming->request.body.paths.first_length != 0u ||
                     incoming->request.body.paths.second_length != 0u)
                outgoing->reply.status = ASTRA_VFS_ERR_INVALID;
            else {
                host->service->backend.ops->cache_reclaim(host->service->backend.context);
                outgoing->reply.status = ASTRA_VFS_OK;
            }
        } else if (operation == ASTRA_VFS_OP_GRANT_DERIVE) {
            const char *root = paths.first;
            uint32_t root_length = 0u;
            uint32_t derived = 0u;

            while (root_length < ASTRA_VFS_PATH_MAX &&
                   root[root_length] != '\0')
                ++root_length;

            /* Never wider: the new rights are a subset of this grant's, and
             * every grant reads. The root is resolved by whoever derives,
             * beneath this grant's root. */
            if (grant == NULL ||
                host->derive == NULL)
                outgoing->reply.status = ASTRA_VFS_ERR_UNSUPPORTED;
            else if ((incoming->request.flags & ~grant->rights) != 0u ||
                     (incoming->request.flags & ASTRA_VFS_GRANT_READ) == 0u)
                outgoing->reply.status = ASTRA_VFS_ERR_ACCESS;
            else if (root_length == ASTRA_VFS_PATH_MAX ||
                     !astra_utf8_validate(root, root_length, 0u))
                outgoing->reply.status = ASTRA_VFS_ERR_INVALID;
            else {
                const AstraVfsPortDeriveRequest request = {
                    .parent = grant,
                    .root = root,
                    .rights = incoming->request.flags,
                    .deriver = sender,
                    .session = incoming->request.session,
                };

                outgoing->reply.status = host->derive(
                    host->derive_context, &request, &derived);
                if (outgoing->reply.status == ASTRA_VFS_OK && derived != 0u) {
                    outgoing_handles[0] = derived;
                    outgoing_handle_count = 1u;
                    /* A port derived from a non-delegable grant arrives
                     * without TRANSFER: it cannot travel on (C1). */
                    if ((grant->rights & ASTRA_VFS_GRANT_DELEGATE) == 0u)
                        outgoing_rights = ~ASTRA_RIGHT_TRANSFER;
                } else if (outgoing->reply.status == ASTRA_VFS_OK) {
                    outgoing->reply.status = ASTRA_VFS_ERR_IO;
                } else if (derived != 0u) {
                    (void)astra_close(derived);
                }
            }
        } else {
            astra_vfs_service_dispatch_grant(
                host->service, sender, grant_id, features, operation,
                &incoming->request, &transfer, &outgoing->reply);
        }
answer_ready:
        /*
         * Restored before the reply goes out, so the next thing this thread
         * does -- including serving somebody else -- is not still inside the
         * last caller's story.
         */
        if (adopted)
            (void)astra_activity_adopt(previous);

        astra_message_header_set(&outgoing->header, sizeof(*outgoing),
            ASTRA_VFS_PROTOCOL, ASTRA_VFS_VERSION, wire_operation,
            incoming->header.transaction_id);
        if (persistent_hello && outgoing->reply.status == ASTRA_VFS_OK) {
            if (incoming->request.session != ASTRA_VFS_SESSION_INVALID &&
                reply_acquire(host, outgoing->reply.session, lane_id, &lease)) {
                slot = lease.slot;
                (void)astra_close(reply_handle);
                reply_handle = lease.handle;
                if (!lease.active)
                    outgoing->reply.status = ASTRA_VFS_ERR_BUSY;
            } else {
                slot = reply_bind_acquire(host, outgoing->reply.session, lane_id,
                                          reply_handle, &lease);
            }
            if (slot < 0) {
                if (incoming->request.session == ASTRA_VFS_SESSION_INVALID)
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
        sent = reply_send(reply_handle, outgoing, sizeof(*outgoing),
                          outgoing_handles, outgoing_handle_count,
                          outgoing_rights) == ASTRA_SYSCALL_OK;
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
astra_vfs_port_service_worker_pump(AstraVfsPortService *host,
                                   AstraVfsPortWorker *worker,
                                   uint32_t budget)
{
    return host == NULL ? 0u : pump(host, worker, host->receive, NULL, budget);
}

uint32_t
astra_vfs_port_service_grant_pump(AstraVfsPortService *host,
                                  AstraVfsPortWorker *worker,
                                  const AstraVfsPortGrant *grant,
                                  uint32_t budget)
{
    if (grant == NULL || grant->receive == 0u || grant->id == 0u)
        return 0u;
    return pump(host, worker, grant->receive, grant, budget);
}

void
astra_vfs_port_service_release_grant(AstraVfsPortService *host,
                                     uint32_t grant)
{
    if (host == NULL || host->service == NULL || grant == 0u)
        return;
    for (uint32_t index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index) {
        uint32_t session = 0u;

        if (!port_state_acquire(host))
            return;
        session = host->reply_sessions[index];
        port_state_release(host);
        if (session != ASTRA_VFS_SESSION_INVALID &&
            astra_vfs_service_session_grant(host->service, session) == grant)
            reply_session_close(host, session);
    }
    /* Sessions that never bound a reply lane. */
    astra_vfs_service_release_grant(host->service, grant);
}

void
astra_vfs_port_service_reap(AstraVfsPortService *host)
{
    if (host != NULL && host->service != NULL) {
        reply_reap_dead(host);
        astra_vfs_service_reap(host->service);
    }
}

uint32_t
astra_vfs_port_service_pump(AstraVfsPortService *host, uint32_t budget)
{
    return host == NULL ? 0u :
        astra_vfs_port_service_worker_pump(host, &host->adapter_worker,
                                           budget);
}
