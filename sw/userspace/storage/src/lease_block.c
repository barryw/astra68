#include <astra/lease_block.h>

#include <astra/bytes.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

AstraBlockStatus
astra_lease_block_status(uint32_t completion_status)
{
    switch (completion_status) {
    case ASTRA_BLOCK_COMPLETION_OK:
        return ASTRA_BLOCK_OK;
    case ASTRA_BLOCK_COMPLETION_MEDIA_CHANGED:
        return ASTRA_BLOCK_MEDIA_CHANGED;
    case ASTRA_BLOCK_COMPLETION_RESET:
    case ASTRA_BLOCK_COMPLETION_CANCELLED:
        return ASTRA_BLOCK_CANCELLED;
    case ASTRA_BLOCK_COMPLETION_LATE:
        /*
         * A completion for a request nobody is waiting on is not this
         * request's answer, and treating it as one would report a transfer
         * that did not happen.
         */
        return ASTRA_BLOCK_CORRUPT;
    default:
        return ASTRA_BLOCK_IO_ERROR;
    }
}

void
astra_lease_block_geometry(const AstraBlockLeaseInfo *info,
                           AstraBlockGeometry *geometry)
{
    if (info == NULL || geometry == NULL) {
        return;
    }
    (void)memset(geometry, 0, sizeof(*geometry));
    geometry->sector_count = info->sector_count;
    geometry->sector_size = info->sector_bytes;
    geometry->max_transfer_sectors = info->max_transfer_sectors;
    geometry->queue_depth = info->queue_depth == 0u ? 1u : info->queue_depth;
    geometry->media_generation = info->media_generation;
    if ((info->state_flags & ASTRA_BLOCK_STATE_MEDIA_PRESENT) != 0u) {
        geometry->flags |= ASTRA_BLOCK_FLAG_PRESENT;
    }
    if ((info->state_flags & ASTRA_BLOCK_STATE_WRITE_ENABLE) == 0u ||
        (info->capabilities & ASTRA_BLOCK_CAP_WRITE) == 0u) {
        geometry->flags |= ASTRA_BLOCK_FLAG_READ_ONLY;
    }
    /* The transport reports removable media through its media generation. */
    geometry->flags |= ASTRA_BLOCK_FLAG_REMOVABLE;
}

/*
 * Consumes every delivered record.
 *
 * Acknowledging the last record is what re-enables the source: the kernel
 * leaves the endpoint armed rather than returning it to the caller masked, so
 * a successful acknowledgement means the endpoint still needs nothing. Only a
 * failed one leaves it masked with its event flags set, and that state is
 * cleared by a recover, not by an arm.
 *
 * This drains rather than taking one record because the storage interrupt has
 * two causes. A state change and a completion each leave a record, and a
 * record left behind refuses every later arm.
 *
 * Multiqueue storage is level-triggered. A new completion may arrive while a
 * prior record is being retired; the kernel re-enables the source and records
 * that work again. This loop therefore consumes the records that exist rather
 * than imposing a guessed count on a queue whose depth is negotiated.
 */
static uint32_t
drain(AstraLeaseBlock *lease)
{
    AstraIrqRecord record;
    uint32_t events = 0u;
    uint32_t status;

    for (;;) {
        (void)memset(&record, 0, sizeof(record));
        status = astra_irq_read(lease->irq, &record, &events);
        if (status == ASTRA_SYSCALL_WOULD_BLOCK) {
            return ASTRA_SYSCALL_OK;
        }
        if (status != ASTRA_SYSCALL_OK) {
            lease->armed = 0u;
            return status;
        }
        status = astra_irq_ack(lease->irq, record.sequence);
        if (status != ASTRA_SYSCALL_OK) {
            lease->armed = 0u;
            return status;
        }
        lease->armed = 1u;
    }
}

/*
 * Arming is a transition out of the masked state, and the kernel refuses it
 * from any other. The flag records whether the endpoint still needs one, so
 * this asks exactly once per attachment rather than once per request.
 */
static uint32_t
arm(AstraLeaseBlock *lease)
{
    uint32_t status;

    if (lease->armed != 0u) {
        return ASTRA_SYSCALL_OK;
    }
    status = astra_irq_arm(lease->irq);
    if (status == ASTRA_SYSCALL_OK) {
        lease->armed = 1u;
    }
    return status;
}

/*
 * Runs one request to completion. The endpoint is armed before submission,
 * because a granted endpoint starts masked and a completion that fires into a
 * disabled source would be waited for until the deadline. On timeout the
 * device is reset, which ends every in-flight request with a status this can
 * still collect, so a device that stops answering is bounded rather than a
 * hang.
 */
/*
 * Records where a request was refused and returns the status a caller sees.
 *
 * Everything here answers ASTRA_BLOCK_IO_ERROR because that is what lwext4 can
 * take, and lwext4 has one errno for a timeout, a cancellation, a short
 * transfer and a corrupt reply. So the distinction is kept here instead of
 * being thrown away at the boundary: a person shown "I/O error" can still ask
 * what the device actually did.
 */
static uint32_t
semaphore_release(uint32_t handle)
{
    return astra_rt_signal(handle, 1u, NULL);
}

static void
record_failure(AstraLeaseBlock *lease, AstraLeaseBlockSite site,
               uint32_t status)
{
    uint32_t locked = 0u;

    if (lease->state_lock != 0u &&
        astra_wait_one(lease->state_lock, ASTRA_DEADLINE_FOREVER, NULL) ==
            ASTRA_SYSCALL_OK) {
        locked = 1u;
    }
    lease->last_site = (uint32_t)site;
    lease->last_status = status;
    if (locked != 0u) {
        (void)semaphore_release(lease->state_lock);
    }
}

static AstraBlockStatus
refused(AstraLeaseBlock *lease, AstraLeaseBlockSite site, uint32_t status)
{
    record_failure(lease, site, status);
    return ASTRA_BLOCK_IO_ERROR;
}

static uint32_t
lane_lock(AstraLeaseBlock *lease)
{
    return astra_wait_one(lease->state_lock, ASTRA_DEADLINE_FOREVER, NULL);
}

static uint32_t
lane_unlock(AstraLeaseBlock *lease)
{
    return semaphore_release(lease->state_lock);
}

static AstraBlockStatus
claim_lane(AstraLeaseBlock *lease, uint64_t deadline,
           AstraLeaseBlockLane **claimed)
{
    uint32_t status;

    *claimed = NULL;
    if (deadline == 0u) {
        deadline = ASTRA_DEADLINE_FOREVER;
    }
    status = astra_wait_one(lease->available, deadline, NULL);
    if (status == ASTRA_SYSCALL_TIMED_OUT) {
        return ASTRA_BLOCK_TIMED_OUT;
    }
    if (status != ASTRA_SYSCALL_OK) {
        return refused(lease, ASTRA_LEASE_BLOCK_SITE_WAIT, status);
    }
    if (lane_lock(lease) != ASTRA_SYSCALL_OK) {
        (void)semaphore_release(lease->available);
        return refused(lease, ASTRA_LEASE_BLOCK_SITE_WAIT,
                       ASTRA_SYSCALL_IO_ERROR);
    }
    for (uint32_t index = 0u; index < lease->queue_depth; ++index) {
        AstraLeaseBlockLane *lane = &lease->lanes[index];

        if (lane->in_use != 0u) {
            continue;
        }
        lane->in_use = 1u;
        lane->active = 0u;
        lane->completed = 0u;
        lane->request = 0u;
        lane->collect_status = ASTRA_SYSCALL_WOULD_BLOCK;
        (void)memset(&lane->completion, 0, sizeof(lane->completion));
        *claimed = lane;
        break;
    }
    status = lane_unlock(lease);
    if (*claimed != NULL && status == ASTRA_SYSCALL_OK) {
        return ASTRA_BLOCK_OK;
    }
    (void)semaphore_release(lease->available);
    return refused(lease, ASTRA_LEASE_BLOCK_SITE_WAIT,
                   status == ASTRA_SYSCALL_OK ? ASTRA_SYSCALL_IO_ERROR :
                                                status);
}

static AstraBlockStatus
release_lane(AstraLeaseBlock *lease, AstraLeaseBlockLane *lane)
{
    uint32_t status;

    if (lane_lock(lease) != ASTRA_SYSCALL_OK) {
        return refused(lease, ASTRA_LEASE_BLOCK_SITE_WAIT,
                       ASTRA_SYSCALL_IO_ERROR);
    }
    lane->in_use = 0u;
    lane->active = 0u;
    lane->completed = 0u;
    lane->request = 0u;
    status = lane_unlock(lease);
    if (status == ASTRA_SYSCALL_OK) {
        status = semaphore_release(lease->available);
    }
    return status == ASTRA_SYSCALL_OK ? ASTRA_BLOCK_OK :
        refused(lease, ASTRA_LEASE_BLOCK_SITE_WAIT, status);
}

static uint32_t
publish_lane(AstraLeaseBlock *lease, AstraLeaseBlockLane *lane,
             uint32_t request, uint32_t collect_status,
             const AstraBlockCompletion *completion)
{
    uint32_t status;
    uint32_t signal = 0u;

    if (lane_lock(lease) != ASTRA_SYSCALL_OK) {
        return ASTRA_SYSCALL_IO_ERROR;
    }
    if (lane->in_use != 0u && lane->active != 0u &&
        lane->request == request && lane->completed == 0u) {
        lane->collect_status = collect_status;
        if (completion != NULL) {
            (void)memcpy(&lane->completion, completion,
                         sizeof(lane->completion));
        }
        lane->active = 0u;
        lane->completed = 1u;
        signal = 1u;
    }
    status = lane_unlock(lease);
    if (status == ASTRA_SYSCALL_OK && signal != 0u) {
        status = semaphore_release(lane->event);
    }
    return status;
}

/*
 * The first collect drains the hardware completion ring into kernel request
 * slots. A later collect in the same pass can drain a completion for a lane
 * already visited, so the kernel reports that count and the owner repeats
 * until a whole pass drains none. That is work-driven draining, not polling.
 */
static uint32_t
service_completions(AstraLeaseBlock *lease)
{
    uint32_t drained;

    do {
        drained = 0u;
        for (uint32_t index = 0u; index < lease->queue_depth; ++index) {
            AstraLeaseBlockLane *lane = &lease->lanes[index];
            AstraBlockCompletion completion;
            uint32_t request = 0u;
            uint32_t serviced = 0u;
            uint32_t status;

            if (lane_lock(lease) != ASTRA_SYSCALL_OK) {
                return ASTRA_SYSCALL_IO_ERROR;
            }
            if (lane->in_use != 0u && lane->active != 0u &&
                lane->completed == 0u) {
                request = lane->request;
            }
            if (lane_unlock(lease) != ASTRA_SYSCALL_OK) {
                return ASTRA_SYSCALL_IO_ERROR;
            }
            if (request == 0u) {
                continue;
            }
            (void)memset(&completion, 0, sizeof(completion));
            status = astra_block_lease_collect_ex(
                lease->device, request, &completion, &serviced);
            drained += serviced;
            if (status == ASTRA_SYSCALL_WOULD_BLOCK) {
                continue;
            }
            if (publish_lane(lease, lane, request, status,
                             status == ASTRA_SYSCALL_OK ? &completion :
                                                          NULL) !=
                ASTRA_SYSCALL_OK) {
                return ASTRA_SYSCALL_IO_ERROR;
            }
        }
    } while (drained != 0u);
    return ASTRA_SYSCALL_OK;
}

static uint32_t
service_irq(AstraLeaseBlock *lease)
{
    uint32_t status = service_completions(lease);

    if (status != ASTRA_SYSCALL_OK) {
        return status;
    }
    return drain(lease);
}

static int
lane_completed(AstraLeaseBlock *lease, AstraLeaseBlockLane *lane)
{
    int completed;

    if (lane_lock(lease) != ASTRA_SYSCALL_OK) {
        return -1;
    }
    completed = lane->completed != 0u;
    if (lane_unlock(lease) != ASTRA_SYSCALL_OK) {
        return -1;
    }
    return completed;
}

static AstraBlockStatus
reset_timed_out_requests(AstraLeaseBlock *lease,
                         AstraLeaseBlockLane *timed_out)
{
    uint32_t status;
    int completed;

    status = astra_wait_one(lease->completion_lock,
                            ASTRA_DEADLINE_FOREVER, NULL);
    if (status != ASTRA_SYSCALL_OK) {
        return refused(lease, ASTRA_LEASE_BLOCK_SITE_RESET, status);
    }
    completed = lane_completed(lease, timed_out);
    if (completed == 0) {
        status = astra_device_reset(lease->device);
        if (status == ASTRA_SYSCALL_OK) {
            status = service_irq(lease);
        }
    } else if (completed < 0) {
        status = ASTRA_SYSCALL_IO_ERROR;
    } else {
        status = ASTRA_SYSCALL_OK;
    }
    if (semaphore_release(lease->completion_lock) != ASTRA_SYSCALL_OK &&
        status == ASTRA_SYSCALL_OK) {
        status = ASTRA_SYSCALL_IO_ERROR;
    }
    if (status != ASTRA_SYSCALL_OK) {
        return refused(lease, ASTRA_LEASE_BLOCK_SITE_RESET, status);
    }
    return completed == 0 ? ASTRA_BLOCK_TIMED_OUT : ASTRA_BLOCK_OK;
}

static AstraBlockStatus
wait_for_lane(AstraLeaseBlock *lease, AstraLeaseBlockLane *lane,
              uint64_t deadline)
{
    const uint32_t handles[2] = {lane->event, lease->irq};
    int timed_out = 0;

    if (deadline == 0u) {
        deadline = ASTRA_DEADLINE_FOREVER;
    }
    for (;;) {
        uint32_t index = ASTRA_WAIT_INDEX_NONE;
        uint32_t status = astra_wait_multiple(handles, 2u, deadline,
                                              &index, NULL);

        if (status == ASTRA_SYSCALL_TIMED_OUT) {
            AstraBlockStatus reset = reset_timed_out_requests(lease, lane);

            if (reset != ASTRA_BLOCK_TIMED_OUT && reset != ASTRA_BLOCK_OK) {
                return reset;
            }
            timed_out = reset == ASTRA_BLOCK_TIMED_OUT;
            deadline = ASTRA_DEADLINE_FOREVER;
            continue;
        }
        if (status != ASTRA_SYSCALL_OK) {
            return refused(lease, ASTRA_LEASE_BLOCK_SITE_WAIT, status);
        }
        if (index == 0u) {
            AstraBlockCompletion completion;
            uint32_t collect_status;

            if (lane_lock(lease) != ASTRA_SYSCALL_OK) {
                return refused(lease, ASTRA_LEASE_BLOCK_SITE_COLLECT,
                               ASTRA_SYSCALL_IO_ERROR);
            }
            completion = lane->completion;
            collect_status = lane->collect_status;
            status = lane_unlock(lease);
            if (status != ASTRA_SYSCALL_OK) {
                return refused(lease, ASTRA_LEASE_BLOCK_SITE_COLLECT,
                               status);
            }
            if (timed_out != 0) {
                return ASTRA_BLOCK_TIMED_OUT;
            }
            if (collect_status != ASTRA_SYSCALL_OK) {
                return refused(lease, ASTRA_LEASE_BLOCK_SITE_COLLECT,
                               collect_status);
            }
            if (completion.status == ASTRA_BLOCK_COMPLETION_OK &&
                completion.sectors != 0u &&
                completion.sectors > lease->max_transfer_sectors) {
                return refused(lease,
                               ASTRA_LEASE_BLOCK_SITE_SHORT_TRANSFER,
                               completion.sectors);
            }
            return astra_lease_block_status(completion.status);
        }
        if (index != 1u) {
            return refused(lease, ASTRA_LEASE_BLOCK_SITE_WAIT,
                           ASTRA_SYSCALL_IO_ERROR);
        }
        status = astra_wait_one(lease->completion_lock, deadline, NULL);
        if (status == ASTRA_SYSCALL_TIMED_OUT) {
            AstraBlockStatus reset = reset_timed_out_requests(lease, lane);

            if (reset != ASTRA_BLOCK_TIMED_OUT && reset != ASTRA_BLOCK_OK) {
                return reset;
            }
            timed_out = reset == ASTRA_BLOCK_TIMED_OUT;
            deadline = ASTRA_DEADLINE_FOREVER;
            continue;
        }
        if (status != ASTRA_SYSCALL_OK) {
            return refused(lease, ASTRA_LEASE_BLOCK_SITE_WAIT, status);
        }
        status = service_irq(lease);
        if (semaphore_release(lease->completion_lock) != ASTRA_SYSCALL_OK &&
            status == ASTRA_SYSCALL_OK) {
            status = ASTRA_SYSCALL_IO_ERROR;
        }
        if (status != ASTRA_SYSCALL_OK) {
            return refused(lease,
                           ASTRA_LEASE_BLOCK_SITE_DRAIN_AFTER_COLLECT,
                           status);
        }
    }
}

static AstraBlockStatus
run_request(AstraLeaseBlock *lease, AstraLeaseBlockLane *lane,
            uint32_t operation, uint64_t lba, uint32_t sector_count,
            uint64_t deadline)
{
    AstraBlockRequest request;
    uint32_t block_request = 0u;
    uint32_t status;

    (void)memset(&request, 0, sizeof(request));
    request.size = ASTRA_BLOCK_REQUEST_SIZE;
    request.operation = operation;
    request.buffer = operation == ASTRA_BLOCK_OP_FLUSH ? 0u : lane->buffer;
    request.sectors = sector_count;
    request.lba = lba;
    status = astra_block_lease_submit(lease->device, &request,
                                      &block_request);
    if (status == ASTRA_SYSCALL_ACCESS_DENIED) {
        return ASTRA_BLOCK_READ_ONLY;
    }
    if (status == ASTRA_SYSCALL_PEER_DEAD) {
        return ASTRA_BLOCK_NO_MEDIA;
    }
    if (status == ASTRA_SYSCALL_INVALID_ARGUMENT) {
        return ASTRA_BLOCK_INVALID_ARGUMENT;
    }
    if (status != ASTRA_SYSCALL_OK) {
        return refused(lease, ASTRA_LEASE_BLOCK_SITE_SUBMIT, status);
    }
    if (lane_lock(lease) != ASTRA_SYSCALL_OK) {
        return refused(lease, ASTRA_LEASE_BLOCK_SITE_SUBMIT,
                       ASTRA_SYSCALL_IO_ERROR);
    }
    lane->request = block_request;
    lane->active = 1u;
    status = lane_unlock(lease);
    if (status != ASTRA_SYSCALL_OK) {
        return refused(lease, ASTRA_LEASE_BLOCK_SITE_SUBMIT, status);
    }
    status = (uint32_t)wait_for_lane(lease, lane, deadline);
    if (status == ASTRA_BLOCK_OK && operation != ASTRA_BLOCK_OP_FLUSH &&
        lane->completion.sectors != sector_count) {
        return refused(lease, ASTRA_LEASE_BLOCK_SITE_SHORT_TRANSFER,
                       lane->completion.sectors);
    }
    return (AstraBlockStatus)status;
}

static AstraBlockStatus
lease_query(void *context, AstraBlockGeometry *geometry)
{
    AstraLeaseBlock *lease = context;
    AstraBlockLeaseInfo info;

    if (lease == NULL || geometry == NULL) {
        return ASTRA_BLOCK_INVALID_ARGUMENT;
    }
    (void)memset(&info, 0, sizeof(info));
    if (astra_block_lease_query(lease->device, &info) != ASTRA_SYSCALL_OK ||
        info.size != ASTRA_BLOCK_LEASE_INFO_SIZE) {
        return ASTRA_BLOCK_IO_ERROR;
    }
    astra_lease_block_geometry(&info, geometry);
    lease->media_generation = info.media_generation;
    return ASTRA_BLOCK_OK;
}

static AstraBlockStatus
lease_read(void *context, uint64_t lba, uint32_t sector_count, void *buffer,
           uint64_t deadline)
{
    AstraLeaseBlock *lease = context;
    AstraLeaseBlockLane *lane;
    AstraBlockStatus status;

    if (lease == NULL || buffer == NULL || sector_count == 0u) {
        return ASTRA_BLOCK_INVALID_ARGUMENT;
    }
    if (sector_count > lease->max_transfer_sectors ||
        sector_count > UINT32_MAX / lease->sector_bytes) {
        return ASTRA_BLOCK_TRANSFER_TOO_LARGE;
    }
    status = claim_lane(lease, deadline, &lane);
    if (status != ASTRA_BLOCK_OK) {
        return status;
    }
    if (sector_count * lease->sector_bytes > lane->buffer_bytes) {
        status = ASTRA_BLOCK_TRANSFER_TOO_LARGE;
    } else {
        status = run_request(lease, lane, ASTRA_BLOCK_OP_READ, lba,
                             sector_count, deadline);
    }
    if (status == ASTRA_BLOCK_OK) {
        /* The device wrote transfer memory; the caller's buffer is ordinary. */
        (void)memcpy(buffer, (const void *)(uintptr_t)lane->buffer_base,
                     sector_count * lease->sector_bytes);
    }
    {
        AstraBlockStatus released = release_lane(lease, lane);

        return status == ASTRA_BLOCK_OK ? released : status;
    }
}

static AstraBlockStatus
lease_write(void *context, uint64_t lba, uint32_t sector_count,
            const void *buffer, uint64_t deadline)
{
    AstraLeaseBlock *lease = context;
    AstraLeaseBlockLane *lane;
    AstraBlockStatus status;

    if (lease == NULL || buffer == NULL || sector_count == 0u) {
        return ASTRA_BLOCK_INVALID_ARGUMENT;
    }
    if (sector_count > lease->max_transfer_sectors ||
        sector_count > UINT32_MAX / lease->sector_bytes) {
        return ASTRA_BLOCK_TRANSFER_TOO_LARGE;
    }
    status = claim_lane(lease, deadline, &lane);
    if (status != ASTRA_BLOCK_OK) {
        return status;
    }
    if (sector_count * lease->sector_bytes > lane->buffer_bytes) {
        status = ASTRA_BLOCK_TRANSFER_TOO_LARGE;
    } else {
        (void)memcpy((void *)(uintptr_t)lane->buffer_base, buffer,
                     sector_count * lease->sector_bytes);
        status = run_request(lease, lane, ASTRA_BLOCK_OP_WRITE, lba,
                             sector_count, deadline);
    }
    {
        AstraBlockStatus released = release_lane(lease, lane);

        return status == ASTRA_BLOCK_OK ? released : status;
    }
}

static AstraBlockStatus
lease_writev(void *context, uint64_t lba, const AstraBlockVector *vector,
             uint64_t deadline)
{
    AstraLeaseBlock *lease = context;
    AstraLeaseBlockLane *lane;
    AstraBlockStatus status;
    uint64_t total = 0u;
    uint32_t index;
    uint8_t *out;

    if (lease == NULL || vector == NULL || vector->buffers == NULL ||
        vector->sector_counts == NULL || vector->count == 0u) {
        return ASTRA_BLOCK_INVALID_ARGUMENT;
    }
    for (index = 0u; index < vector->count; ++index) {
        if (vector->buffers[index] == NULL ||
            vector->sector_counts[index] == 0u) {
            return ASTRA_BLOCK_INVALID_ARGUMENT;
        }
        total += vector->sector_counts[index];
        if (total > lease->max_transfer_sectors ||
            total > UINT32_MAX / lease->sector_bytes) {
            return ASTRA_BLOCK_TRANSFER_TOO_LARGE;
        }
    }
    status = claim_lane(lease, deadline, &lane);
    if (status != ASTRA_BLOCK_OK) {
        return status;
    }
    if (total * lease->sector_bytes > lane->buffer_bytes) {
        (void)release_lane(lease, lane);
        return ASTRA_BLOCK_TRANSFER_TOO_LARGE;
    }
    out = (void *)(uintptr_t)lane->buffer_base;
    for (index = 0u; index < vector->count; ++index) {
        uint32_t bytes = vector->sector_counts[index] * lease->sector_bytes;

        (void)memcpy(out, vector->buffers[index], bytes);
        out += bytes;
    }
    status = run_request(lease, lane, ASTRA_BLOCK_OP_WRITE, lba,
                         (uint32_t)total, deadline);
    {
        AstraBlockStatus released = release_lane(lease, lane);

        return status == ASTRA_BLOCK_OK ? released : status;
    }
}

static AstraBlockStatus
lease_flush(void *context, uint64_t deadline)
{
    AstraLeaseBlock *lease = context;
    AstraLeaseBlockLane *lane;
    AstraBlockStatus status;

    if (lease == NULL) {
        return ASTRA_BLOCK_INVALID_ARGUMENT;
    }
    status = claim_lane(lease, deadline, &lane);
    if (status != ASTRA_BLOCK_OK) {
        return status;
    }
    status = run_request(lease, lane, ASTRA_BLOCK_OP_FLUSH, 0u, 0u,
                         deadline);
    {
        AstraBlockStatus released = release_lane(lease, lane);

        return status == ASTRA_BLOCK_OK ? released : status;
    }
}

static const AstraBlockBackend lease_backend = {
    .query = lease_query,
    .read = lease_read,
    .write = lease_write,
    .writev = lease_writev,
    .flush = lease_flush,
};

const AstraBlockBackend *
astra_lease_block_backend(void)
{
    return &lease_backend;
}

AstraBlockStatus
astra_lease_block_attach(AstraLeaseBlock *lease, uint32_t device_handle,
                         uint32_t irq_handle)
{
    AstraBlockLeaseInfo info;
    uint32_t transfer_bytes;
    uint32_t status;

    if (lease == NULL || device_handle == 0u || irq_handle == 0u) {
        return ASTRA_BLOCK_INVALID_ARGUMENT;
    }
    (void)memset(lease, 0, sizeof(*lease));
    (void)memset(&info, 0, sizeof(info));
    if (astra_block_lease_query(device_handle, &info) != ASTRA_SYSCALL_OK ||
        info.size != ASTRA_BLOCK_LEASE_INFO_SIZE) {
        return ASTRA_BLOCK_IO_ERROR;
    }
    if (info.sector_bytes < ASTRA_BLOCK_SECTOR_SIZE_MIN ||
        info.sector_bytes > ASTRA_BLOCK_SECTOR_SIZE_MAX ||
        info.max_transfer_sectors == 0u ||
        info.max_transfer_sectors > ASTRA_BLOCK_TRANSFER_SECTORS_MAX ||
        info.max_transfer_sectors > UINT32_MAX / info.sector_bytes ||
        info.queue_depth == 0u ||
        info.queue_depth > ASTRA_BLOCK_MAX_REQUESTS_PER_SERVICE ||
        info.queue_depth > ASTRA_DMA_MAX_BUFFERS_PER_SERVICE) {
        return ASTRA_BLOCK_CORRUPT;
    }
    if ((info.state_flags & ASTRA_BLOCK_STATE_MEDIA_PRESENT) == 0u) {
        return ASTRA_BLOCK_NO_MEDIA;
    }

    transfer_bytes = info.max_transfer_sectors * info.sector_bytes;
    lease->device = device_handle;
    lease->irq = irq_handle;
    lease->queue_depth = info.queue_depth;
    lease->sector_bytes = info.sector_bytes;
    lease->max_transfer_sectors = info.max_transfer_sectors;
    lease->media_generation = info.media_generation;

    if (astra_rt_semaphore_create(
            1u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
            &lease->state_lock) != ASTRA_SYSCALL_OK ||
        astra_rt_semaphore_create(
            1u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
            &lease->completion_lock) != ASTRA_SYSCALL_OK ||
        astra_rt_semaphore_create(
            lease->queue_depth, lease->queue_depth,
            ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
            &lease->available) != ASTRA_SYSCALL_OK) {
        astra_lease_block_detach(lease);
        return ASTRA_BLOCK_IO_ERROR;
    }
    for (uint32_t index = 0u; index < lease->queue_depth; ++index) {
        AstraDmaBufferInfo buffer;
        AstraLeaseBlockLane *lane = &lease->lanes[index];

        (void)memset(&buffer, 0, sizeof(buffer));
        if (astra_dma_create(transfer_bytes, &buffer) != ASTRA_SYSCALL_OK ||
            buffer.size != ASTRA_DMA_BUFFER_INFO_SIZE ||
            buffer.handle == 0u || buffer.virtual_base == 0u ||
            buffer.byte_size < transfer_bytes ||
            astra_rt_semaphore_create(
                0u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
                &lane->event) != ASTRA_SYSCALL_OK) {
            if (buffer.handle != 0u) {
                (void)astra_close(buffer.handle);
            }
            astra_lease_block_detach(lease);
            return ASTRA_BLOCK_IO_ERROR;
        }
        lane->buffer = buffer.handle;
        lane->buffer_base = buffer.virtual_base;
        lane->buffer_bytes = buffer.byte_size;
    }
    status = drain(lease);
    if (status == ASTRA_SYSCALL_OK) {
        status = arm(lease);
    }
    if (status != ASTRA_SYSCALL_OK) {
        astra_lease_block_detach(lease);
        return ASTRA_BLOCK_IO_ERROR;
    }
    return ASTRA_BLOCK_OK;
}

void
astra_lease_block_detach(AstraLeaseBlock *lease)
{
    if (lease == NULL) {
        return;
    }
    for (uint32_t index = 0u;
         index < ASTRA_BLOCK_MAX_REQUESTS_PER_SERVICE; ++index) {
        if (lease->lanes[index].event != 0u) {
            (void)astra_close(lease->lanes[index].event);
        }
        if (lease->lanes[index].buffer != 0u) {
            (void)astra_close(lease->lanes[index].buffer);
        }
    }
    if (lease->available != 0u) {
        (void)astra_close(lease->available);
    }
    if (lease->completion_lock != 0u) {
        (void)astra_close(lease->completion_lock);
    }
    if (lease->state_lock != 0u) {
        (void)astra_close(lease->state_lock);
    }
    (void)memset(lease, 0, sizeof(*lease));
}

uint32_t
astra_lease_block_last_failure(const AstraLeaseBlock *lease)
{
    if (lease == NULL || lease->last_site == ASTRA_LEASE_BLOCK_SITE_NONE) {
        return 0u;
    }
    return (lease->last_site * 256u) + (lease->last_status & 0xffu);
}
