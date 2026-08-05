#include <astra/lease_block.h>

#include <astra/bytes.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

#define LEASE_BLOCK_DEFAULT_TIMEOUT_NS 2000000000u

/*
 * A bound on the drain loop, not the kernel's record ring depth, which is not
 * part of the syscall ABI. The loop ends when the endpoint reports it has no
 * record left; this only stops a kernel that never says so from spinning here.
 */
#define LEASE_BLOCK_DRAIN_LIMIT 8u

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

static uint32_t
timeout_of(const AstraLeaseBlock *lease)
{
    return lease->timeout_ns != 0u ? lease->timeout_ns :
        LEASE_BLOCK_DEFAULT_TIMEOUT_NS;
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
 * It may only be called with nothing in flight. The transport refuses to
 * complete its interrupt while a completion is still queued, and a failed
 * acknowledgement does not merely fail: it marks the endpoint with a device
 * error and masks it, which no arm can undo.
 */
static uint32_t
drain(AstraLeaseBlock *lease)
{
    AstraIrqRecord record;
    uint32_t events = 0u;
    uint32_t status;
    uint32_t taken;

    for (taken = 0u; taken < LEASE_BLOCK_DRAIN_LIMIT; ++taken) {
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
    return ASTRA_SYSCALL_OK;
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
static AstraBlockStatus
run_request(AstraLeaseBlock *lease, uint32_t operation, uint64_t lba,
            uint32_t sector_count)
{
    AstraBlockRequest request;
    AstraBlockCompletion completion;
    uint64_t deadline;
    uint32_t block_request = 0u;
    uint32_t status;

    /*
     * Nothing is in flight at this point, so the transport has no completion
     * queued and a record left over from the last request can be safely
     * acknowledged. Doing it before the arm matters twice over: the kernel
     * refuses to arm an endpoint that still holds a record.
     */
    if (drain(lease) != ASTRA_SYSCALL_OK) {
        return ASTRA_BLOCK_IO_ERROR;
    }
    if (arm(lease) != ASTRA_SYSCALL_OK) {
        return ASTRA_BLOCK_IO_ERROR;
    }

    (void)memset(&request, 0, sizeof(request));
    request.size = ASTRA_BLOCK_REQUEST_SIZE;
    request.operation = operation;
    request.buffer = operation == ASTRA_BLOCK_OP_FLUSH ? 0u : lease->buffer;
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
        return ASTRA_BLOCK_IO_ERROR;
    }

    deadline = astra_clock_monotonic() + timeout_of(lease);
    for (;;) {
        (void)memset(&completion, 0, sizeof(completion));
        status = astra_block_lease_collect(lease->device, block_request,
                                           &completion);
        if (status == ASTRA_SYSCALL_OK) {
            /*
             * The records are drained here and nowhere else in this loop.
             * Collecting is what pops the completion out of the transport,
             * and the transport refuses to complete its interrupt while one
             * is still queued. Only once this request has been collected is
             * nothing in flight, and only then can no further completion
             * appear between the check and the acknowledgement.
             *
             * Acknowledging after a collection that returned nothing is the
             * shape that fails: it races the device, which queues the
             * completion in the gap, and the acknowledgement then quarantines
             * the endpoint instead of merely failing.
             */
            if (drain(lease) != ASTRA_SYSCALL_OK) {
                return ASTRA_BLOCK_IO_ERROR;
            }
            if (completion.status == ASTRA_BLOCK_COMPLETION_OK &&
                completion.sectors != sector_count &&
                operation != ASTRA_BLOCK_OP_FLUSH) {
                /* A short transfer reported as success is still short. */
                return ASTRA_BLOCK_IO_ERROR;
            }
            return astra_lease_block_status(completion.status);
        }
        if (status != ASTRA_SYSCALL_WOULD_BLOCK) {
            return ASTRA_BLOCK_IO_ERROR;
        }

        status = astra_wait_one(lease->irq, deadline, NULL);
        if (status == ASTRA_SYSCALL_TIMED_OUT) {
            break;
        }
        if (status != ASTRA_SYSCALL_OK) {
            return ASTRA_BLOCK_IO_ERROR;
        }
    }

    if (astra_device_reset(lease->device) != ASTRA_SYSCALL_OK) {
        return ASTRA_BLOCK_IO_ERROR;
    }
    (void)astra_block_lease_collect(lease->device, block_request,
                                    &completion);
    return ASTRA_BLOCK_TIMED_OUT;
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
    AstraBlockStatus status;

    (void)deadline;
    if (lease == NULL || buffer == NULL || sector_count == 0u) {
        return ASTRA_BLOCK_INVALID_ARGUMENT;
    }
    if (sector_count > lease->max_transfer_sectors ||
        sector_count * lease->sector_bytes > lease->buffer_bytes) {
        return ASTRA_BLOCK_TRANSFER_TOO_LARGE;
    }
    status = run_request(lease, ASTRA_BLOCK_OP_READ, lba, sector_count);
    if (status != ASTRA_BLOCK_OK) {
        return status;
    }
    /* The device wrote transfer memory; the caller's buffer is ordinary. */
    (void)memcpy(buffer, (const void *)(uintptr_t)lease->buffer_base,
                 sector_count * lease->sector_bytes);
    return ASTRA_BLOCK_OK;
}

static AstraBlockStatus
lease_write(void *context, uint64_t lba, uint32_t sector_count,
            const void *buffer, uint64_t deadline)
{
    AstraLeaseBlock *lease = context;

    (void)deadline;
    if (lease == NULL || buffer == NULL || sector_count == 0u) {
        return ASTRA_BLOCK_INVALID_ARGUMENT;
    }
    if (sector_count > lease->max_transfer_sectors ||
        sector_count * lease->sector_bytes > lease->buffer_bytes) {
        return ASTRA_BLOCK_TRANSFER_TOO_LARGE;
    }
    (void)memcpy((void *)(uintptr_t)lease->buffer_base, buffer,
                 sector_count * lease->sector_bytes);
    return run_request(lease, ASTRA_BLOCK_OP_WRITE, lba, sector_count);
}

static AstraBlockStatus
lease_flush(void *context, uint64_t deadline)
{
    AstraLeaseBlock *lease = context;

    (void)deadline;
    if (lease == NULL) {
        return ASTRA_BLOCK_INVALID_ARGUMENT;
    }
    return run_request(lease, ASTRA_BLOCK_OP_FLUSH, 0u, 0u);
}

static const AstraBlockBackend lease_backend = {
    lease_query,
    lease_read,
    lease_write,
    lease_flush
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
    AstraDmaBufferInfo buffer;
    uint32_t transfer_bytes;

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
        info.max_transfer_sectors == 0u) {
        return ASTRA_BLOCK_CORRUPT;
    }
    if ((info.state_flags & ASTRA_BLOCK_STATE_MEDIA_PRESENT) == 0u) {
        return ASTRA_BLOCK_NO_MEDIA;
    }

    transfer_bytes = info.max_transfer_sectors * info.sector_bytes;
    (void)memset(&buffer, 0, sizeof(buffer));
    if (astra_dma_create(transfer_bytes, &buffer) != ASTRA_SYSCALL_OK ||
        buffer.size != ASTRA_DMA_BUFFER_INFO_SIZE || buffer.handle == 0u ||
        buffer.byte_size < transfer_bytes) {
        return ASTRA_BLOCK_IO_ERROR;
    }

    lease->device = device_handle;
    lease->irq = irq_handle;
    lease->buffer = buffer.handle;
    lease->buffer_base = buffer.virtual_base;
    lease->buffer_bytes = buffer.byte_size;
    lease->sector_bytes = info.sector_bytes;
    lease->max_transfer_sectors = info.max_transfer_sectors;
    lease->media_generation = info.media_generation;
    return ASTRA_BLOCK_OK;
}

void
astra_lease_block_detach(AstraLeaseBlock *lease)
{
    if (lease == NULL || lease->buffer == 0u) {
        return;
    }
    (void)astra_close(lease->buffer);
    lease->buffer = 0u;
    lease->buffer_base = 0u;
    lease->buffer_bytes = 0u;
}
