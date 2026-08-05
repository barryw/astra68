#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <astra/lease_block.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

/*
 * The lease backend against a modelled kernel.
 *
 * The interesting half of this backend is not the data path, it is the order
 * it drives the interrupt endpoint in: arm before submit, drain before
 * re-arm, reset on deadline. Every one of those was wrong at least once, and
 * none of them fail visibly in emulation when the device answers instantly.
 * The model below refuses the wrong order rather than tolerating it.
 */

#define MOCK_DEVICE 0x100u
#define MOCK_IRQ 0x200u
#define MOCK_BUFFER 0x300u
#define MOCK_REQUEST 0x400u

/*
 * The endpoint states kernel_irq_arm() and kernel_irq_ack() move through, not a
 * boolean. A boolean was the reason a defect shipped: it modelled an
 * acknowledgement as disarming the endpoint, which is the opposite of what the
 * kernel does, so the second transfer on one attachment passed here and failed
 * on the device.
 */
#define MOCK_IRQ_MASKED 0
#define MOCK_IRQ_ARMED 1
#define MOCK_IRQ_PENDING 2

static int mock_irq_state;
static uint32_t mock_arm_refusals;
static uint32_t mock_ack_before_collect;
static int mock_answers_before_collect;
static int mock_answers_during_collect;
static int mock_completion_queued;   /* the transport's completion-valid bit */
static int mock_record_pending;
static int mock_request_active;
static int mock_request_complete;
static uint32_t mock_completion_status;
static uint32_t mock_completion_sectors;
static uint32_t mock_reset_calls;
static uint32_t mock_arm_calls;
static uint32_t mock_ack_calls;
static uint32_t mock_wait_calls;
static uint32_t mock_submit_calls;
static uint64_t mock_now_ns;
static int mock_device_answers;      /* 0 models a device that never replies */
static uint32_t mock_sequence;
static uint32_t mock_submitted_operation;
static int mock_submitted_while_armed;

static void
mock_reset(void)
{
    mock_irq_state = MOCK_IRQ_MASKED;
    mock_arm_refusals = 0u;
    mock_ack_before_collect = 0u;
    mock_answers_before_collect = 0;
    mock_answers_during_collect = 0;
    mock_completion_queued = 0;
    mock_record_pending = 0;
    mock_request_active = 0;
    mock_request_complete = 0;
    mock_completion_status = ASTRA_BLOCK_COMPLETION_OK;
    mock_completion_sectors = 0u;
    mock_reset_calls = 0u;
    mock_arm_calls = 0u;
    mock_ack_calls = 0u;
    mock_wait_calls = 0u;
    mock_submit_calls = 0u;
    mock_now_ns = 1000u;
    mock_device_answers = 1;
    mock_sequence = 1u;
    mock_submitted_operation = 0u;
    mock_submitted_while_armed = 0;
}

static void
fill_lease_info(AstraBlockLeaseInfo *info)
{
    memset(info, 0, sizeof(*info));
    info->size = ASTRA_BLOCK_LEASE_INFO_SIZE;
    info->sector_bytes = ASTRA_BLOCK_SECTOR_BYTES;
    info->max_transfer_sectors = 8u;
    info->capabilities = ASTRA_BLOCK_CAP_READ | ASTRA_BLOCK_CAP_WRITE |
                         ASTRA_BLOCK_CAP_FLUSH;
    info->state_flags = ASTRA_BLOCK_STATE_LINK_UP |
                        ASTRA_BLOCK_STATE_MEDIA_PRESENT |
                        ASTRA_BLOCK_STATE_WRITE_ENABLE;
    info->media_generation = 4u;
    info->sector_count = 1024u;
}

/*
 * The mock replaces the syscall wrappers rather than the trap beneath them:
 * the wrappers pass user pointers as 32-bit arguments, which a 64-bit host
 * cannot represent. The logic under test is this file's ordering, not the
 * wrappers, which the target build exercises.
 */
uint32_t
astra_block_lease_query(uint32_t device, AstraBlockLeaseInfo *info)
{
    assert(device == MOCK_DEVICE);
    fill_lease_info(info);
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_dma_create(uint32_t byte_size, AstraDmaBufferInfo *info)
{
    static uint8_t transfer_memory[8u * ASTRA_BLOCK_SECTOR_BYTES];

    assert(byte_size <= sizeof(transfer_memory));
    memset(info, 0, sizeof(*info));
    info->size = ASTRA_DMA_BUFFER_INFO_SIZE;
    info->handle = MOCK_BUFFER;
    info->virtual_base = 0x50000000u;
    info->byte_size = (uint32_t)sizeof(transfer_memory);
    info->page_count = 2u;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_block_lease_submit(uint32_t device, const AstraBlockRequest *request,
                         uint32_t *block_request)
{
    assert(device == MOCK_DEVICE);
    ++mock_submit_calls;
    mock_submitted_operation = request->operation;
    /* The endpoint must already be armed: the event cannot be missed. */
    mock_submitted_while_armed = mock_irq_state != MOCK_IRQ_MASKED;
    mock_request_active = 1;
    mock_request_complete = 0;
    *block_request = MOCK_REQUEST;
    if (mock_answers_before_collect) {
        /*
         * What the device model actually does: the completion and its record
         * are both queued before the service ever polls, so the first collect
         * succeeds and the request never waits. That is the path every boot
         * takes, and the one the wait-driven tests below never reach.
         */
        mock_request_complete = 1;
        mock_completion_queued = 1;
        mock_record_pending = 1;
        mock_irq_state = MOCK_IRQ_PENDING;
    }
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_block_lease_collect(uint32_t device, uint32_t block_request,
                          AstraBlockCompletion *completion)
{
    assert(device == MOCK_DEVICE);
    assert(block_request == MOCK_REQUEST);
    /*
     * Collection drains the transport before it looks anything up, which is
     * what clears the completion the interrupt is still pointing at. It
     * happens on every call, including the ones that find nothing to return.
     */
    mock_completion_queued = 0;
    if (!mock_request_active) {
        return ASTRA_SYSCALL_INVALID_HANDLE;
    }
    if (!mock_request_complete) {
        if (mock_answers_during_collect) {
            /*
             * The race a real device wins about one request in fifteen: this
             * collection found nothing, and the completion and its record land
             * in the gap before the caller does anything else. Anything that
             * acknowledges here is acknowledging a completion still sitting in
             * the transport.
             */
            mock_request_complete = 1;
            mock_completion_queued = 1;
            mock_record_pending = 1;
            mock_irq_state = MOCK_IRQ_PENDING;
        }
        return ASTRA_SYSCALL_WOULD_BLOCK;
    }
    memset(completion, 0, sizeof(*completion));
    completion->size = ASTRA_BLOCK_COMPLETION_SIZE;
    completion->request = MOCK_REQUEST;
    completion->status = mock_completion_status;
    completion->sectors = mock_completion_sectors;
    mock_request_active = 0;
    return ASTRA_SYSCALL_OK;
}

/*
 * kernel_irq_arm() takes a masked endpoint with nothing queued and nothing
 * flagged. Anything else is KERNEL_IRQ_INVALID_STATE, which reaches user mode
 * as ASTRA_SYSCALL_INVALID_ARGUMENT. Arming an armed endpoint is therefore a
 * refusal, not a no-op, and arming one with a record still queued is what
 * strands a service.
 */
uint32_t
astra_irq_arm(uint32_t handle)
{
    assert(handle == MOCK_IRQ);
    ++mock_arm_calls;
    if (mock_irq_state != MOCK_IRQ_MASKED || mock_record_pending) {
        ++mock_arm_refusals;
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    mock_irq_state = MOCK_IRQ_ARMED;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_irq_read(uint32_t handle, AstraIrqRecord *record, uint32_t *events)
{
    assert(handle == MOCK_IRQ);
    if (events != NULL) {
        *events = 0u;
    }
    if (!mock_record_pending) {
        return ASTRA_SYSCALL_WOULD_BLOCK;
    }
    memset(record, 0, sizeof(*record));
    record->sequence = mock_sequence;
    return ASTRA_SYSCALL_OK;
}

/*
 * Acknowledging the last record re-enables the source and leaves the endpoint
 * armed. Nothing has to arm it again, and anything that tries is refused.
 *
 * The transport refuses to complete its interrupt while a completion is still
 * queued behind it, and collecting is what clears that. Acknowledging first is
 * a device error, not a harmless reordering.
 */
uint32_t
astra_irq_ack(uint32_t handle, uint32_t sequence)
{
    assert(handle == MOCK_IRQ);
    if (!mock_record_pending) {
        return ASTRA_SYSCALL_WOULD_BLOCK;
    }
    if (mock_completion_queued) {
        ++mock_ack_before_collect;
        return ASTRA_SYSCALL_IO_ERROR;
    }
    assert(sequence == mock_sequence);
    ++mock_ack_calls;
    ++mock_sequence;
    mock_record_pending = 0;
    mock_irq_state = MOCK_IRQ_ARMED;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_wait_one(uint32_t handle, uint64_t deadline_ns, uint32_t *detail)
{
    assert(handle == MOCK_IRQ);
    assert(deadline_ns > mock_now_ns);
    (void)detail;
    ++mock_wait_calls;
    /* A completion cannot be delivered into a masked source. */
    assert(mock_irq_state != MOCK_IRQ_MASKED);
    if (!mock_device_answers) {
        mock_now_ns += 1000000000u;
        return ASTRA_SYSCALL_TIMED_OUT;
    }
    /* The device answers: a completion and its interrupt arrive. */
    mock_request_complete = 1;
    mock_completion_queued = 1;
    mock_record_pending = 1;
    mock_irq_state = MOCK_IRQ_PENDING;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_device_reset(uint32_t handle)
{
    assert(handle == MOCK_DEVICE);
    ++mock_reset_calls;
    mock_completion_status = ASTRA_BLOCK_COMPLETION_RESET;
    mock_request_complete = 1;
    mock_completion_queued = 1;
    mock_request_active = 1;
    return ASTRA_SYSCALL_OK;
}

uint64_t
astra_clock_monotonic(void)
{
    return mock_now_ns;
}

uint32_t
astra_close(uint32_t handle)
{
    assert(handle == MOCK_BUFFER);
    return ASTRA_SYSCALL_OK;
}

static AstraBlockStatus
attach(AstraLeaseBlock *lease)
{
    mock_reset();
    return astra_lease_block_attach(lease, MOCK_DEVICE, MOCK_IRQ);
}

static void
test_attach_claims_one_transfer(void)
{
    AstraLeaseBlock lease;

    assert(attach(&lease) == ASTRA_BLOCK_OK);
    assert(lease.device == MOCK_DEVICE);
    assert(lease.irq == MOCK_IRQ);
    assert(lease.buffer == MOCK_BUFFER);
    assert(lease.sector_bytes == ASTRA_BLOCK_SECTOR_BYTES);
    assert(lease.max_transfer_sectors == 8u);
    /* Sized for one maximum transfer, claimed once, not per request. */
    assert(lease.buffer_bytes >= 8u * ASTRA_BLOCK_SECTOR_BYTES);
    astra_lease_block_detach(&lease);
    assert(lease.buffer == 0u);
}

static void
test_request_order(void)
{
    AstraLeaseBlock lease;

    assert(attach(&lease) == ASTRA_BLOCK_OK);
    assert(astra_lease_block_backend()->flush(&lease, 0u) == ASTRA_BLOCK_OK);

    /* Armed before the request was submitted, never after. */
    assert(mock_submit_calls == 1u);
    assert(mock_submitted_while_armed);
    assert(mock_submitted_operation == ASTRA_BLOCK_OP_FLUSH);
    /* It waited on the endpoint rather than spinning. */
    assert(mock_wait_calls == 1u);
    /* Every delivered record was acknowledged. */
    assert(mock_ack_calls == 1u);
    assert(!mock_record_pending);
    /* And acknowledged after its completion was collected, never before. */
    assert(mock_ack_before_collect == 0u);
    assert(mock_reset_calls == 0u);
}

/*
 * Two transfers on one attachment.
 *
 * Every earlier gate issued exactly one, and one transfer cannot see what the
 * endpoint was left in. A filesystem issues thousands, and the second one is
 * where it stopped: the acknowledgement had already re-armed the endpoint, and
 * arming it again was refused, so the transfer returned IO_ERROR before it
 * reached the device.
 *
 * These drive flush rather than read. Every operation runs the same
 * run_request(), which is where the endpoint is sequenced; read and write add
 * only a copy to and from transfer memory, and transfer memory is addressed by
 * a uint32_t the host cannot make point at anything real.
 */
static void
test_second_transfer_on_one_attachment(void)
{
    AstraLeaseBlock lease;

    assert(attach(&lease) == ASTRA_BLOCK_OK);
    mock_answers_before_collect = 1;

    assert(astra_lease_block_backend()->flush(&lease, 0u) == ASTRA_BLOCK_OK);
    /* The partition table read: the first transfer a filesystem asks for. */
    assert(astra_lease_block_backend()->flush(&lease, 0u) == ASTRA_BLOCK_OK);
    /* And it keeps going, rather than working once more and then stopping. */
    assert(astra_lease_block_backend()->flush(&lease, 0u) == ASTRA_BLOCK_OK);

    /* Each completion was consumed, and nothing armed an armed endpoint. */
    assert(mock_submit_calls == 3u);
    assert(mock_ack_calls == 3u);
    assert(mock_arm_refusals == 0u);
    assert(mock_ack_before_collect == 0u);
    assert(!mock_record_pending);
    /* It never waited: the device had already answered. */
    assert(mock_wait_calls == 0u);
    assert(mock_reset_calls == 0u);
}

/* The same two transfers down the path that does wait for its interrupt. */
static void
test_second_transfer_through_the_wait_path(void)
{
    AstraLeaseBlock lease;

    assert(attach(&lease) == ASTRA_BLOCK_OK);

    assert(astra_lease_block_backend()->flush(&lease, 0u) == ASTRA_BLOCK_OK);
    assert(astra_lease_block_backend()->flush(&lease, 0u) == ASTRA_BLOCK_OK);
    assert(mock_wait_calls == 2u);
    assert(mock_ack_calls == 2u);
    assert(mock_arm_refusals == 0u);
    /* The waiting path is where the acknowledgement used to come first. */
    assert(mock_ack_before_collect == 0u);
    assert(!mock_record_pending);
}

/*
 * The endpoint is armed once. The kernel keeps it armed across every
 * acknowledgement, so a service that arms per request is asking for a state
 * transition that is not available to it.
 */
static void
test_endpoint_is_armed_once(void)
{
    AstraLeaseBlock lease;

    assert(attach(&lease) == ASTRA_BLOCK_OK);
    mock_answers_before_collect = 1;

    assert(astra_lease_block_backend()->flush(&lease, 0u) == ASTRA_BLOCK_OK);
    assert(astra_lease_block_backend()->flush(&lease, 0u) == ASTRA_BLOCK_OK);
    assert(astra_lease_block_backend()->flush(&lease, 0u) == ASTRA_BLOCK_OK);
    assert(mock_arm_calls == 1u);
}

/*
 * The device completes in the gap after a collection returned nothing.
 *
 * This is the shape that survived the first two fixes and still stopped the
 * mount, roughly one request in fifteen. Acknowledging at that moment reaches
 * the transport while the completion is still queued, which does not merely
 * fail: it marks the endpoint with a device error and masks it, and no arm
 * recovers that. The request must collect again instead.
 */
static void
test_completion_that_lands_after_an_empty_collect(void)
{
    AstraLeaseBlock lease;
    uint32_t index;

    assert(attach(&lease) == ASTRA_BLOCK_OK);
    mock_answers_during_collect = 1;

    for (index = 0u; index < 20u; ++index) {
        assert(astra_lease_block_backend()->flush(&lease, 0u) ==
               ASTRA_BLOCK_OK);
    }
    assert(mock_submit_calls == 20u);
    assert(mock_ack_calls == 20u);
    assert(mock_ack_before_collect == 0u);
    assert(mock_arm_refusals == 0u);
    assert(!mock_record_pending);
    assert(mock_reset_calls == 0u);
}

static void
test_device_that_never_answers(void)
{
    AstraLeaseBlock lease;

    assert(attach(&lease) == ASTRA_BLOCK_OK);
    mock_device_answers = 0;

    /* Bounded: the deadline expires, the device is reset, and it reports. */
    assert(astra_lease_block_backend()->flush(&lease, 0u) ==
           ASTRA_BLOCK_TIMED_OUT);
    assert(mock_reset_calls == 1u);
    assert(mock_wait_calls >= 1u);
}

static void
test_completion_statuses_reach_the_caller(void)
{
    AstraLeaseBlock lease;

    assert(attach(&lease) == ASTRA_BLOCK_OK);
    mock_completion_status = ASTRA_BLOCK_COMPLETION_DEVICE_ERROR;
    assert(astra_lease_block_backend()->flush(&lease, 0u) ==
           ASTRA_BLOCK_IO_ERROR);

    assert(attach(&lease) == ASTRA_BLOCK_OK);
    mock_completion_status = ASTRA_BLOCK_COMPLETION_MEDIA_CHANGED;
    assert(astra_lease_block_backend()->flush(&lease, 0u) ==
           ASTRA_BLOCK_MEDIA_CHANGED);
}

static void
test_transfer_ceilings(void)
{
    AstraLeaseBlock lease;
    uint8_t buffer[ASTRA_BLOCK_SECTOR_BYTES];

    assert(attach(&lease) == ASTRA_BLOCK_OK);
    /* More sectors than one transfer holds is refused, not truncated. */
    assert(astra_lease_block_backend()->read(&lease, 0u, 9u, buffer, 0u) ==
           ASTRA_BLOCK_TRANSFER_TOO_LARGE);
    assert(astra_lease_block_backend()->write(&lease, 0u, 9u, buffer, 0u) ==
           ASTRA_BLOCK_TRANSFER_TOO_LARGE);
    assert(astra_lease_block_backend()->read(&lease, 0u, 0u, buffer, 0u) ==
           ASTRA_BLOCK_INVALID_ARGUMENT);
    assert(astra_lease_block_backend()->read(&lease, 0u, 1u, NULL, 0u) ==
           ASTRA_BLOCK_INVALID_ARGUMENT);
    /* Nothing reached the device. */
    assert(mock_submit_calls == 0u);
}

static void
test_attach_rejections(void)
{
    AstraLeaseBlock lease;

    mock_reset();
    assert(astra_lease_block_attach(NULL, MOCK_DEVICE, MOCK_IRQ) ==
           ASTRA_BLOCK_INVALID_ARGUMENT);
    assert(astra_lease_block_attach(&lease, 0u, MOCK_IRQ) ==
           ASTRA_BLOCK_INVALID_ARGUMENT);
    assert(astra_lease_block_attach(&lease, MOCK_DEVICE, 0u) ==
           ASTRA_BLOCK_INVALID_ARGUMENT);
}

static void
test_status_translation(void)
{
    assert(astra_lease_block_status(ASTRA_BLOCK_COMPLETION_OK) ==
           ASTRA_BLOCK_OK);
    assert(astra_lease_block_status(ASTRA_BLOCK_COMPLETION_DEVICE_ERROR) ==
           ASTRA_BLOCK_IO_ERROR);
    assert(astra_lease_block_status(ASTRA_BLOCK_COMPLETION_MEDIA_CHANGED) ==
           ASTRA_BLOCK_MEDIA_CHANGED);

    /* A reset and a cancellation both mean the request did not happen. */
    assert(astra_lease_block_status(ASTRA_BLOCK_COMPLETION_RESET) ==
           ASTRA_BLOCK_CANCELLED);
    assert(astra_lease_block_status(ASTRA_BLOCK_COMPLETION_CANCELLED) ==
           ASTRA_BLOCK_CANCELLED);

    /*
     * A late completion answers a request nobody is waiting on. Reporting it
     * as this request's result would claim a transfer that did not happen.
     */
    assert(astra_lease_block_status(ASTRA_BLOCK_COMPLETION_LATE) ==
           ASTRA_BLOCK_CORRUPT);

    /* Anything the ABI has not defined is an error, never a success. */
    assert(astra_lease_block_status(0x5a5a5a5au) == ASTRA_BLOCK_IO_ERROR);
}

static void
test_geometry_translation(void)
{
    AstraBlockLeaseInfo info;
    AstraBlockGeometry geometry;

    fill_lease_info(&info);
    astra_lease_block_geometry(&info, &geometry);
    assert(geometry.sector_count == 1024u);
    assert(geometry.sector_size == ASTRA_BLOCK_SECTOR_BYTES);
    assert(geometry.max_transfer_sectors == 8u);
    assert(geometry.media_generation == 4u);
    assert((geometry.flags & ASTRA_BLOCK_FLAG_PRESENT) != 0u);
    assert((geometry.flags & ASTRA_BLOCK_FLAG_READ_ONLY) == 0u);

    /* Write protection at the transport is read-only to a filesystem. */
    fill_lease_info(&info);
    info.state_flags &= ~(uint32_t)ASTRA_BLOCK_STATE_WRITE_ENABLE;
    astra_lease_block_geometry(&info, &geometry);
    assert((geometry.flags & ASTRA_BLOCK_FLAG_READ_ONLY) != 0u);

    /* So is a device that cannot write at all. */
    fill_lease_info(&info);
    info.capabilities &= ~(uint32_t)ASTRA_BLOCK_CAP_WRITE;
    astra_lease_block_geometry(&info, &geometry);
    assert((geometry.flags & ASTRA_BLOCK_FLAG_READ_ONLY) != 0u);

    /* Absent media must not look present. */
    fill_lease_info(&info);
    info.state_flags &= ~(uint32_t)ASTRA_BLOCK_STATE_MEDIA_PRESENT;
    astra_lease_block_geometry(&info, &geometry);
    assert((geometry.flags & ASTRA_BLOCK_FLAG_PRESENT) == 0u);

    /* Null arguments are refused rather than half-writing the caller's. */
    astra_lease_block_geometry(NULL, &geometry);
    astra_lease_block_geometry(&info, NULL);
}

int
main(void)
{
    test_status_translation();
    test_geometry_translation();
    test_attach_claims_one_transfer();
    test_attach_rejections();
    test_request_order();
    test_second_transfer_on_one_attachment();
    test_second_transfer_through_the_wait_path();
    test_endpoint_is_armed_once();
    test_completion_that_lands_after_an_empty_collect();
    test_device_that_never_answers();
    test_completion_statuses_reach_the_caller();
    test_transfer_ceilings();
    puts("astra lease block: PASS");
    return 0;
}
