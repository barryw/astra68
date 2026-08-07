#include "allocation.h"
#include "irq.h"
#include "performance.h"
#include "trace.h"

#include <astra/syscall.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct FakeController {
    char log[1024];
    uint32_t log_count;
    uint32_t configured;
    uint32_t enabled;
    uint32_t configure_calls;
    uint32_t mask_calls;
    uint32_t enable_calls;
    uint32_t acknowledge_calls;
    uint8_t fail_configure;
    uint8_t fail_mask;
    uint8_t fail_enable;
    uint8_t fail_acknowledge;
} FakeController;

typedef struct FakeDevice {
    FakeController *controller;
    uint32_t status;
    uint32_t captures;
    uint32_t completions;
    uint32_t quiesces;
    uint32_t internal_services;
    uint8_t fail_capture;
    uint8_t fail_complete;
    uint8_t fail_quiesce;
} FakeDevice;

static KernelHandle next_test_handle;
static uint32_t trace_event_count[KERNEL_TRACE_EVENT_MONITOR_DROP + 1u];

static bool record_trace(KernelTraceEvent event)
{
    assert(event > 0 && event <= KERNEL_TRACE_EVENT_MONITOR_DROP);
    ++trace_event_count[event];
    return true;
}

bool kernel_trace_write(KernelTraceEvent event, uint16_t flags,
                        uint32_t argument0, uint32_t argument1,
                        uint32_t argument2, uint32_t argument3)
{
    (void)flags;
    (void)argument0;
    (void)argument1;
    (void)argument2;
    (void)argument3;
    return record_trace(event);
}

bool kernel_trace_write_at(KernelTraceEvent event, uint16_t flags,
                           uint64_t timestamp, uint32_t argument0,
                           uint32_t argument1, uint32_t argument2,
                           uint32_t argument3)
{
    (void)timestamp;
    return kernel_trace_write(event, flags, argument0, argument1,
                              argument2, argument3);
}

bool kernel_trace_stage_at(KernelTraceEvent event, uint16_t flags,
                           uint64_t timestamp, uint32_t argument0,
                           uint32_t argument1, uint32_t argument2,
                           uint32_t argument3)
{
    return kernel_trace_write_at(event, flags, timestamp, argument0,
                                 argument1, argument2, argument3);
}

static void log_operation(FakeController *controller, char operation)
{
    assert(controller != NULL);
    assert(controller->log_count + 1u < sizeof(controller->log));
    controller->log[controller->log_count++] = operation;
    controller->log[controller->log_count] = '\0';
}

static bool fail_once(uint8_t *counter)
{
    if (*counter == 0u)
        return false;
    --*counter;
    return true;
}

static bool fake_configure(uint8_t source, uint8_t trigger, uint8_t ipl,
                           uint8_t vector, void *context)
{
    FakeController *controller = context;

    assert(source < KERNEL_IRQ_SOURCE_COUNT);
    assert(trigger == KERNEL_IRQ_TRIGGER_LEVEL ||
           trigger == KERNEL_IRQ_TRIGGER_EDGE);
    assert(ipl != 0u && ipl <= 7u);
    assert(vector == KERNEL_IRQ_COMMON_VECTOR);
    log_operation(controller, 'C');
    ++controller->configure_calls;
    if (fail_once(&controller->fail_configure))
        return false;
    controller->configured |= 1u << source;
    return true;
}

static bool fake_mask(uint8_t source, void *context)
{
    FakeController *controller = context;

    assert(source < KERNEL_IRQ_SOURCE_COUNT);
    log_operation(controller, 'M');
    ++controller->mask_calls;
    if (fail_once(&controller->fail_mask))
        return false;
    controller->enabled &= ~(1u << source);
    return true;
}

static bool fake_enable(uint8_t source, void *context)
{
    FakeController *controller = context;

    assert(source < KERNEL_IRQ_SOURCE_COUNT);
    log_operation(controller, 'E');
    ++controller->enable_calls;
    if (fail_once(&controller->fail_enable))
        return false;
    assert((controller->configured & (1u << source)) != 0u);
    controller->enabled |= 1u << source;
    return true;
}

static bool fake_acknowledge(uint8_t source, void *context)
{
    FakeController *controller = context;

    assert(source < KERNEL_IRQ_SOURCE_COUNT);
    log_operation(controller, 'A');
    ++controller->acknowledge_calls;
    return !fail_once(&controller->fail_acknowledge);
}

static bool fake_capture(uint8_t source, uint32_t *status, void *context)
{
    FakeDevice *device = context;

    assert(source < KERNEL_IRQ_SOURCE_COUNT);
    assert(status != NULL);
    log_operation(device->controller, 'R');
    ++device->captures;
    if (fail_once(&device->fail_capture))
        return false;
    *status = device->status | source;
    return true;
}

static bool fake_complete(uint8_t source, const KernelIrqRecord *record,
                          void *context)
{
    FakeDevice *device = context;

    assert(source < KERNEL_IRQ_SOURCE_COUNT);
    assert(record != NULL && record->sequence != 0u);
    log_operation(device->controller, 'D');
    ++device->completions;
    return !fail_once(&device->fail_complete);
}

static bool fake_quiesce(uint8_t source, void *context)
{
    FakeDevice *device = context;

    assert(source < KERNEL_IRQ_SOURCE_COUNT);
    log_operation(device->controller, 'Q');
    ++device->quiesces;
    return !fail_once(&device->fail_quiesce);
}

static bool fake_internal_service(uint8_t source, uint64_t timestamp,
                                  void *context)
{
    FakeDevice *device = context;

    assert(source < KERNEL_IRQ_SOURCE_COUNT);
    assert(timestamp != UINT64_MAX);
    log_operation(device->controller, 'I');
    ++device->internal_services;
    return !fail_once(&device->fail_capture);
}

static void clear_log(FakeController *controller)
{
    controller->log_count = 0u;
    controller->log[0] = '\0';
}

static void expect_log(const FakeController *controller, const char *expected)
{
    assert(strcmp(controller->log, expected) == 0);
}

static void initialize_test(FakeController *controller)
{
    KernelIrqControllerOps ops;

    memset(controller, 0, sizeof(*controller));
    kernel_allocation_test_clear_failure();
    kernel_performance_init();
    kernel_thread_pool_init();
    ops.configure = fake_configure;
    ops.mask = fake_mask;
    ops.enable = fake_enable;
    ops.acknowledge = fake_acknowledge;
    ops.context = controller;
    assert(kernel_irq_pool_init(&ops));
    assert(kernel_irq_pool_valid());
    next_test_handle = 0x00000101u;
    memset(trace_event_count, 0, sizeof(trace_event_count));
}

static KernelIrqEndpoint *bind_endpoint(FakeController *controller,
                                        FakeDevice *device, uint32_t owner,
                                        uint8_t source, uint8_t trigger)
{
    KernelIrqBinding binding;
    KernelIrqEndpoint *endpoint;

    memset(device, 0, sizeof(*device));
    device->controller = controller;
    device->status = 0xa5000000u;
    binding.capture = fake_capture;
    binding.complete = fake_complete;
    binding.quiesce = fake_quiesce;
    binding.context = device;
    binding.source = source;
    binding.trigger = trigger;
    binding.ipl = 3u;
    binding.vector = KERNEL_IRQ_COMMON_VECTOR;
    assert(kernel_irq_bind(owner, &binding, &endpoint) == KERNEL_IRQ_OK);
    assert(endpoint != NULL);
    return endpoint;
}

static uint32_t find_endpoint_slot(uint32_t owner, uint8_t source)
{
    KernelIrqSnapshot snapshot;

    for (uint32_t slot = 0u; slot < KERNEL_IRQ_ENDPOINT_MAX; ++slot) {
        assert(kernel_irq_snapshot(slot, &snapshot));
        if (snapshot.state != KERNEL_IRQ_FREE && snapshot.owner == owner &&
            snapshot.source == source)
            return slot;
    }
    assert(false);
    return UINT32_MAX;
}

static KernelThread *allocate_running_thread(void)
{
    KernelThread *thread;

    assert(kernel_thread_allocate(
               0u, 0x10000001u, 0u, 0x00100000u, 0x70001000u, 0u,
               KERNEL_THREAD_PRIORITY_NORMAL, &thread) == KERNEL_THREAD_OK);
    assert(kernel_thread_attach_handle(thread, next_test_handle) ==
           KERNEL_THREAD_OK);
    next_test_handle += 0x00000100u;
    assert(kernel_thread_publish(thread) == KERNEL_THREAD_OK);
    assert(kernel_thread_take_next(&thread) == KERNEL_THREAD_OK);
    return thread;
}

static void assert_endpoint_free(uint32_t slot)
{
    KernelIrqSnapshot snapshot;

    assert(kernel_irq_snapshot(slot, &snapshot));
    assert(snapshot.state == KERNEL_IRQ_FREE);
    assert(snapshot.owner == 0u);
    assert(snapshot.references == 0u);
    assert(kernel_irq_pool_valid());
}

static uint32_t service_all_revocations(void)
{
    uint32_t total = 0u;

    while (kernel_irq_revocation_pending()) {
        uint32_t completed = 0u;

        assert(kernel_irq_service_revocations(1u, &completed) ==
               KERNEL_IRQ_OK);
        total += completed;
    }
    return total;
}

static void release_and_service(KernelIrqEndpoint *endpoint, uint32_t slot)
{
    kernel_irq_handle_release(endpoint, NULL);
    assert(service_all_revocations() == 1u);
    assert_endpoint_free(slot);
}

static void test_edge_order_and_manual_mask(void)
{
    FakeController controller;
    FakeDevice device;
    KernelIrqEndpoint *endpoint;
    KernelIrqRecord record;
    KernelIrqPoolStats stats;
    KernelIrqSnapshot snapshot;
    uint32_t event_flags;
    uint32_t slot;
    uint32_t woken;

    initialize_test(&controller);
    endpoint = bind_endpoint(&controller, &device, 1u, 2u,
                             KERNEL_IRQ_TRIGGER_EDGE);
    slot = find_endpoint_slot(1u, 2u);
    expect_log(&controller, "CM");
    clear_log(&controller);
    assert(kernel_irq_arm(endpoint) == KERNEL_IRQ_OK);
    expect_log(&controller, "E");

    clear_log(&controller);
    assert(kernel_irq_dispatch(2u, KERNEL_IRQ_COMMON_VECTOR,
                               0x100000002ull, &woken) == KERNEL_IRQ_OK);
    assert(woken == 0u);
    expect_log(&controller, "RA");
    assert(kernel_irq_read(endpoint, &record, &event_flags) == KERNEL_IRQ_OK);
    assert(event_flags == 0u);
    assert(record.timestamp_high == 1u && record.timestamp_low == 2u);
    assert(record.status == 0xa5000002u && record.sequence != 0u);
    assert(kernel_irq_ack(endpoint, record.sequence + 1u) ==
           KERNEL_IRQ_SEQUENCE_MISMATCH);
    clear_log(&controller);
    assert(kernel_irq_ack(endpoint, record.sequence) == KERNEL_IRQ_OK);
    expect_log(&controller, "D");

    clear_log(&controller);
    assert(kernel_irq_dispatch(2u, KERNEL_IRQ_COMMON_VECTOR, 10u, &woken) ==
           KERNEL_IRQ_OK);
    assert(kernel_irq_read(endpoint, &record, &event_flags) == KERNEL_IRQ_OK);
    assert(kernel_irq_mask(endpoint) == KERNEL_IRQ_OK);
    expect_log(&controller, "RAM");
    clear_log(&controller);
    assert(kernel_irq_ack(endpoint, record.sequence) == KERNEL_IRQ_OK);
    expect_log(&controller, "D");
    assert(kernel_irq_snapshot(slot, &snapshot));
    assert(snapshot.state == KERNEL_IRQ_MASKED);
    assert(kernel_irq_arm(endpoint) == KERNEL_IRQ_OK);

    clear_log(&controller);
    assert(kernel_irq_mask(endpoint) == KERNEL_IRQ_OK);
    assert(kernel_irq_dispatch(2u, KERNEL_IRQ_COMMON_VECTOR, 11u, &woken) ==
           KERNEL_IRQ_INVALID_STATE);
    expect_log(&controller, "MMA");
    assert(kernel_irq_pool_stats(&stats));
    assert(stats.masked_interrupts == 1u);
    release_and_service(endpoint, slot);
}

static void test_internal_route_uses_common_order(void)
{
    FakeController controller;
    FakeDevice device;
    KernelIrqInternalBinding binding;
    KernelIrqPoolStats stats;
    uint32_t woken;

    initialize_test(&controller);
    memset(&device, 0, sizeof(device));
    device.controller = &controller;
    binding.service = fake_internal_service;
    binding.context = &device;
    binding.source = 0u;
    binding.trigger = KERNEL_IRQ_TRIGGER_LEVEL;
    binding.ipl = 4u;
    binding.vector = KERNEL_IRQ_COMMON_VECTOR;
    assert(kernel_irq_bind_internal(&binding) == KERNEL_IRQ_OK);
    expect_log(&controller, "CM");
    clear_log(&controller);
    assert(kernel_irq_arm_internal(0u) == KERNEL_IRQ_OK);
    expect_log(&controller, "E");
    clear_log(&controller);
    assert(kernel_irq_dispatch(0u, KERNEL_IRQ_COMMON_VECTOR, 123u, &woken) ==
           KERNEL_IRQ_OK);
    assert(woken == 0u);
    expect_log(&controller, "MIAE");
    assert(device.internal_services == 1u);

    clear_log(&controller);
    assert(kernel_irq_mask_internal(0u) == KERNEL_IRQ_OK);
    assert(kernel_irq_dispatch(0u, KERNEL_IRQ_COMMON_VECTOR, 124u, &woken) ==
           KERNEL_IRQ_INVALID_STATE);
    expect_log(&controller, "MMA");
    assert(kernel_irq_arm_internal(0u) == KERNEL_IRQ_OK);
    device.fail_capture = 1u;
    clear_log(&controller);
    assert(kernel_irq_dispatch(0u, KERNEL_IRQ_COMMON_VECTOR, 125u, &woken) ==
           KERNEL_IRQ_DEVICE_ERROR);
    expect_log(&controller, "MIMA");
    assert(kernel_irq_pool_stats(&stats));
    assert(stats.internal_routes == 1u);
    assert(stats.internal_deliveries == 1u);
    assert(stats.masked_interrupts == 1u);
    assert(stats.device_failures == 1u);
    assert(kernel_irq_pool_valid());
}

static void test_internal_edge_route_avoids_level_mask_cycle(void)
{
    FakeController controller;
    FakeDevice device;
    KernelIrqInternalBinding binding;
    KernelIrqPoolStats stats;
    uint32_t woken;

    initialize_test(&controller);
    memset(&device, 0, sizeof(device));
    device.controller = &controller;
    binding.service = fake_internal_service;
    binding.context = &device;
    binding.source = 0u;
    binding.trigger = KERNEL_IRQ_TRIGGER_EDGE;
    binding.ipl = 4u;
    binding.vector = KERNEL_IRQ_COMMON_VECTOR;
    assert(kernel_irq_bind_internal(&binding) == KERNEL_IRQ_OK);
    expect_log(&controller, "CM");
    clear_log(&controller);
    assert(kernel_irq_arm_internal(0u) == KERNEL_IRQ_OK);
    expect_log(&controller, "E");
    clear_log(&controller);

    assert(kernel_irq_dispatch(0u, KERNEL_IRQ_COMMON_VECTOR, 123u, &woken) ==
           KERNEL_IRQ_OK);
    assert(woken == 0u);
    expect_log(&controller, "IA");
    assert(device.internal_services == 1u);
    assert(kernel_irq_pool_stats(&stats));
    assert(stats.internal_deliveries == 1u);
    assert(kernel_irq_pool_valid());
}

static void test_level_acknowledgement_order_and_retry(void)
{
    FakeController controller;
    FakeDevice device;
    KernelIrqEndpoint *endpoint;
    KernelIrqRecord record;
    uint32_t event_flags;
    uint32_t slot;
    uint32_t woken;

    initialize_test(&controller);
    endpoint = bind_endpoint(&controller, &device, 2u, 3u,
                             KERNEL_IRQ_TRIGGER_LEVEL);
    slot = find_endpoint_slot(2u, 3u);
    assert(kernel_irq_arm(endpoint) == KERNEL_IRQ_OK);
    clear_log(&controller);
    assert(kernel_irq_dispatch(3u, KERNEL_IRQ_COMMON_VECTOR, 20u, &woken) ==
           KERNEL_IRQ_OK);
    expect_log(&controller, "MR");
    assert(kernel_irq_read(endpoint, &record, &event_flags) == KERNEL_IRQ_OK);
    clear_log(&controller);
    assert(kernel_irq_ack(endpoint, record.sequence) == KERNEL_IRQ_OK);
    expect_log(&controller, "DAE");

    assert(kernel_irq_dispatch(3u, KERNEL_IRQ_COMMON_VECTOR, 21u, &woken) ==
           KERNEL_IRQ_OK);
    assert(kernel_irq_read(endpoint, &record, &event_flags) == KERNEL_IRQ_OK);
    controller.fail_acknowledge = 1u;
    clear_log(&controller);
    assert(kernel_irq_ack(endpoint, record.sequence) ==
           KERNEL_IRQ_DEVICE_ERROR);
    expect_log(&controller, "DA");
    assert(device.completions == 2u);
    clear_log(&controller);
    assert(kernel_irq_ack(endpoint, record.sequence) ==
           KERNEL_IRQ_DEVICE_ERROR);
    expect_log(&controller, "A");
    assert(device.completions == 2u);
    assert(kernel_irq_recover(endpoint) == KERNEL_IRQ_OK);

    clear_log(&controller);
    kernel_irq_handle_release(endpoint, NULL);
    assert(service_all_revocations() == 1u);
    expect_log(&controller, "MMQA");
    assert_endpoint_free(slot);
}

/*
 * An interrupt with nothing behind it must not cost the device.
 *
 * **A capture that finds nothing pending is not a fault.** Every capture in
 * sw/kernel/platform.c returns false for exactly one reason -- the source's
 * status register shows no work -- and not one of them has any way to report a
 * hardware failure, because none of them is given one. Dispatch nevertheless
 * read false as a device failure and set the endpoint's DEVICE_ERROR flag,
 * which is sticky: kernel_irq_read answers with it forever afterwards, so one
 * spurious interrupt ends the device for the rest of the boot.
 *
 * It is reachable in ordinary operation, and it was reached. A caller polling
 * astra_block_lease_collect takes the completion out of the queue itself, and
 * it can win that race against the CPU taking the interrupt the device raised
 * for the same completion -- so the handler runs with the queue already empty.
 * On the machine this killed storage partway through a session: every transfer
 * after it was refused ASTRA_BLOCK_IO_ERROR and the volume never came back,
 * from one interrupt that had simply arrived a moment late.
 *
 * A spurious interrupt is a normal event on real hardware. It is counted and
 * acknowledged, and the endpoint goes on serving.
 */
static void test_a_capture_with_nothing_pending_is_not_a_failure(void)
{
    FakeController controller;
    FakeDevice device;
    KernelIrqEndpoint *endpoint;
    KernelIrqRecord record;
    KernelIrqPoolStats stats;
    uint32_t event_flags;
    uint32_t slot;
    uint32_t woken;

    initialize_test(&controller);
    endpoint = bind_endpoint(&controller, &device, 3u, 4u,
                             KERNEL_IRQ_TRIGGER_EDGE);
    slot = find_endpoint_slot(3u, 4u);
    assert(kernel_irq_arm(endpoint) == KERNEL_IRQ_OK);

    /* The completion was already taken: the handler finds an empty queue. */
    device.fail_capture = 1u;
    clear_log(&controller);
    assert(kernel_irq_dispatch(4u, KERNEL_IRQ_COMMON_VECTOR, 30u, &woken) ==
           KERNEL_IRQ_UNCLAIMED);
    assert(woken == 0u);
    /* Acknowledged at the controller and left enabled, never masked. */
    expect_log(&controller, "RA");
    assert(kernel_irq_pool_stats(&stats));
    assert(stats.unclaimed_interrupts == 1u);
    assert(stats.device_failures == 0u);

    /* No record, and -- the whole point -- no sticky error behind it. */
    assert(kernel_irq_read(endpoint, &record, &event_flags) ==
           KERNEL_IRQ_WOULD_BLOCK);

    /* The next real interrupt is delivered as though nothing had happened. */
    clear_log(&controller);
    assert(kernel_irq_dispatch(4u, KERNEL_IRQ_COMMON_VECTOR, 31u, &woken) ==
           KERNEL_IRQ_OK);
    assert(kernel_irq_read(endpoint, &record, &event_flags) == KERNEL_IRQ_OK);
    assert(kernel_irq_ack(endpoint, record.sequence) == KERNEL_IRQ_OK);

    /*
     * And the negative half: a device that refuses its own completion is a
     * real fault and must still quarantine. `complete` is the callback a
     * device answers with, so it is the one that can say so.
     */
    assert(kernel_irq_dispatch(4u, KERNEL_IRQ_COMMON_VECTOR, 32u, &woken) ==
           KERNEL_IRQ_OK);
    assert(kernel_irq_read(endpoint, &record, &event_flags) == KERNEL_IRQ_OK);
    device.fail_complete = 1u;
    assert(kernel_irq_ack(endpoint, record.sequence) ==
           KERNEL_IRQ_DEVICE_ERROR);
    assert(kernel_irq_pool_stats(&stats));
    assert(stats.device_failures == 1u);
    /*
     * The refused acknowledgement leaves the record where it was, so the
     * sticky flag is only visible once it drains -- which is why it takes a
     * second acknowledgement to see the state the device is now in.
     */
    assert(kernel_irq_ack(endpoint, record.sequence) ==
           KERNEL_IRQ_DEVICE_ERROR);
    assert(kernel_irq_read(endpoint, &record, &event_flags) ==
           KERNEL_IRQ_DEVICE_ERROR);

    assert(kernel_irq_recover(endpoint) == KERNEL_IRQ_OK);
    release_and_service(endpoint, slot);
}

/*
 * The same, level-triggered.
 *
 * A level source is masked before its capture runs and unmasked by the
 * acknowledgement that follows, so a capture that finds nothing has to put the
 * mask back itself. Getting this wrong is worse than the failure it replaces:
 * the endpoint survives, still believing it is armed, while the controller has
 * the source masked -- and the next interrupt never comes at all.
 */
static void test_a_level_capture_with_nothing_pending_re_enables(void)
{
    FakeController controller;
    FakeDevice device;
    KernelIrqEndpoint *endpoint;
    KernelIrqRecord record;
    uint32_t event_flags;
    uint32_t slot;
    uint32_t woken;

    initialize_test(&controller);
    endpoint = bind_endpoint(&controller, &device, 2u, 3u,
                             KERNEL_IRQ_TRIGGER_LEVEL);
    slot = find_endpoint_slot(2u, 3u);
    assert(kernel_irq_arm(endpoint) == KERNEL_IRQ_OK);

    device.fail_capture = 1u;
    clear_log(&controller);
    assert(kernel_irq_dispatch(3u, KERNEL_IRQ_COMMON_VECTOR, 20u, &woken) ==
           KERNEL_IRQ_UNCLAIMED);
    /* Masked for the capture, acknowledged, and enabled again on the way out. */
    expect_log(&controller, "MRAE");

    /* Still armed, so a real interrupt still arrives and still lands. */
    clear_log(&controller);
    assert(kernel_irq_dispatch(3u, KERNEL_IRQ_COMMON_VECTOR, 21u, &woken) ==
           KERNEL_IRQ_OK);
    assert(kernel_irq_read(endpoint, &record, &event_flags) == KERNEL_IRQ_OK);
    assert(kernel_irq_ack(endpoint, record.sequence) == KERNEL_IRQ_OK);
    release_and_service(endpoint, slot);
}

static void test_wait_race_and_wakeup(void)
{
    FakeController controller;
    FakeDevice device;
    KernelIrqEndpoint *endpoint;
    KernelIrqRecord record;
    KernelThreadWaitSpec spec;
    KernelThread *thread;
    uint32_t event_flags;
    uint32_t slot;
    uint32_t woken;

    initialize_test(&controller);
    endpoint = bind_endpoint(&controller, &device, 3u, 4u,
                             KERNEL_IRQ_TRIGGER_EDGE);
    slot = find_endpoint_slot(3u, 4u);
    assert(kernel_irq_arm(endpoint) == KERNEL_IRQ_OK);
    thread = allocate_running_thread();

    assert(kernel_irq_prepare_wait(endpoint, &spec) == KERNEL_IRQ_WOULD_BLOCK);
    assert(kernel_irq_dispatch(4u, KERNEL_IRQ_COMMON_VECTOR, 30u, &woken) ==
           KERNEL_IRQ_OK);
    assert(woken == 0u);
    assert(kernel_thread_block(thread, spec.queue, spec.sequence) ==
           KERNEL_THREAD_CONDITION_CHANGED);
    assert(kernel_irq_read(endpoint, &record, &event_flags) == KERNEL_IRQ_OK);
    assert(kernel_irq_ack(endpoint, record.sequence) == KERNEL_IRQ_OK);

    assert(kernel_irq_prepare_wait(endpoint, &spec) == KERNEL_IRQ_WOULD_BLOCK);
    assert(kernel_thread_block(thread, spec.queue, spec.sequence) ==
           KERNEL_THREAD_OK);
    assert(kernel_irq_commit_wait(endpoint) == KERNEL_IRQ_OK);
    assert(kernel_irq_dispatch(4u, KERNEL_IRQ_COMMON_VECTOR, 31u, &woken) ==
           KERNEL_IRQ_OK);
    assert(woken == 1u);
    assert(thread->state == KERNEL_THREAD_READY);
    assert(thread->context.data[0] == ASTRA_SYSCALL_OK);
    assert(kernel_irq_read(endpoint, &record, &event_flags) == KERNEL_IRQ_OK);
    assert(kernel_irq_ack(endpoint, record.sequence) == KERNEL_IRQ_OK);
    release_and_service(endpoint, slot);
}

static void test_overflow_and_storm_quarantine(void)
{
    FakeController controller;
    FakeDevice device;
    KernelIrqEndpoint *endpoint;
    KernelIrqPoolStats stats;
    KernelIrqRecord record;
    uint32_t event_flags;
    uint32_t slot;
    uint32_t woken;

    initialize_test(&controller);
    endpoint = bind_endpoint(&controller, &device, 4u, 5u,
                             KERNEL_IRQ_TRIGGER_EDGE);
    slot = find_endpoint_slot(4u, 5u);
    assert(kernel_irq_arm(endpoint) == KERNEL_IRQ_OK);
    for (uint32_t index = 0u; index < KERNEL_IRQ_RECORD_DEPTH; ++index) {
        assert(kernel_irq_dispatch(5u, KERNEL_IRQ_COMMON_VECTOR,
                                   100u + index, &woken) == KERNEL_IRQ_OK);
    }
    assert(kernel_irq_dispatch(5u, KERNEL_IRQ_COMMON_VECTOR, 104u, &woken) ==
           KERNEL_IRQ_OVERFLOW);
    for (uint32_t index = 0u; index < KERNEL_IRQ_RECORD_DEPTH; ++index) {
        assert(kernel_irq_read(endpoint, &record, &event_flags) ==
               KERNEL_IRQ_OK);
        KernelIrqStatus status = kernel_irq_ack(endpoint, record.sequence);

        assert(status == (index + 1u == KERNEL_IRQ_RECORD_DEPTH ?
                              KERNEL_IRQ_OVERFLOW : KERNEL_IRQ_OK));
    }
    assert(kernel_irq_read(endpoint, &record, &event_flags) ==
           KERNEL_IRQ_OVERFLOW);
    assert(event_flags == KERNEL_IRQ_EVENT_OVERFLOW);
    assert(kernel_irq_recover(endpoint) == KERNEL_IRQ_OK);

    /*
     * A storm is interrupts nobody retires, so these are not one: spurious
     * ones, which carry no work and leave nothing to acknowledge. The budget
     * is spent here rather than on a device that is merely busy.
     */
    device.fail_capture = KERNEL_IRQ_STORM_BUDGET;
    for (uint32_t index = 0u; index < KERNEL_IRQ_STORM_BUDGET; ++index) {
        KernelIrqStatus dispatch_status = kernel_irq_dispatch(
            5u, KERNEL_IRQ_COMMON_VECTOR, 1000u + index, &woken);

        assert(dispatch_status ==
               (index + 1u == KERNEL_IRQ_STORM_BUDGET ?
                    KERNEL_IRQ_STORM : KERNEL_IRQ_UNCLAIMED));
    }
    assert(kernel_irq_read(endpoint, &record, &event_flags) ==
           KERNEL_IRQ_STORM);
    assert(event_flags == KERNEL_IRQ_EVENT_STORM);
    assert(kernel_irq_recover(endpoint) == KERNEL_IRQ_OK);
    assert(kernel_irq_pool_stats(&stats));
    assert(stats.dropped_records == 1u);
    assert(stats.overflow_quarantines == 1u);
    assert(stats.storm_quarantines == 1u);
    assert(stats.max_pending_records == KERNEL_IRQ_RECORD_DEPTH);
    release_and_service(endpoint, slot);
}

/*
 * A busy device is not a storm.
 *
 * This is the one that cost a session. The storm counter counted every
 * delivery in its window and was cleared by nothing but kernel_irq_recover, so
 * a device answering as fast as it was asked -- every interrupt read and
 * acknowledged the moment it arrived -- was quarantined at sixty-four exactly
 * as if nobody had been listening. The flag is sticky, so kernel_irq_read
 * answered KERNEL_IRQ_STORM for the rest of the boot: on the machine, one
 * burst of sequential transfers during a journal flush ended storage, and
 * every later read came back ASTRA_BLOCK_IO_ERROR from a volume that was
 * perfectly healthy.
 *
 * Many times the budget, all of them serviced, and the endpoint is still
 * serving at the end of it.
 */
static void test_a_serviced_device_is_never_a_storm(void)
{
    FakeController controller;
    FakeDevice device;
    KernelIrqEndpoint *endpoint;
    KernelIrqPoolStats stats;
    KernelIrqRecord record;
    uint32_t event_flags;
    uint32_t slot;
    uint32_t woken;

    initialize_test(&controller);
    endpoint = bind_endpoint(&controller, &device, 4u, 5u,
                             KERNEL_IRQ_TRIGGER_LEVEL);
    slot = find_endpoint_slot(4u, 5u);
    assert(kernel_irq_arm(endpoint) == KERNEL_IRQ_OK);

    for (uint32_t index = 0u; index < (KERNEL_IRQ_STORM_BUDGET * 4u);
         ++index) {
        /*
         * Every one of them inside the storm window, which is the case the
         * old counter could not tell from a wedged line: the timestamp never
         * advances, so nothing here is saved by the window expiring.
         */
        clear_log(&controller);   /* the fake's log is not the subject here */
        assert(kernel_irq_dispatch(5u, KERNEL_IRQ_COMMON_VECTOR, 200u,
                                   &woken) == KERNEL_IRQ_OK);
        assert(kernel_irq_read(endpoint, &record, &event_flags) ==
               KERNEL_IRQ_OK);
        assert(event_flags == 0u);
        assert(kernel_irq_ack(endpoint, record.sequence) == KERNEL_IRQ_OK);
    }

    /* Still armed, still clean, and no quarantine was ever taken. */
    assert(kernel_irq_read(endpoint, &record, &event_flags) ==
           KERNEL_IRQ_WOULD_BLOCK);
    assert(event_flags == 0u);
    assert(kernel_irq_pool_stats(&stats));
    assert(stats.storm_quarantines == 0u);
    assert(stats.overflow_quarantines == 0u);
    release_and_service(endpoint, slot);
}

static void test_reference_owner_death_and_quiesce_retry(void)
{
    FakeController controller;
    FakeDevice device;
    KernelIrqEndpoint *endpoint;
    KernelIrqSnapshot snapshot;
    KernelThreadWaitSpec spec;
    KernelThread *thread;
    uint32_t completed;
    uint32_t revoked;
    uint32_t slot;
    uint32_t woken;

    initialize_test(&controller);
    endpoint = bind_endpoint(&controller, &device, 5u, 6u,
                             KERNEL_IRQ_TRIGGER_LEVEL);
    slot = find_endpoint_slot(5u, 6u);
    assert(kernel_irq_handle_retain(endpoint, NULL));
    kernel_irq_handle_release(endpoint, NULL);
    assert(kernel_irq_snapshot(slot, &snapshot));
    assert(snapshot.references == 1u && snapshot.state == KERNEL_IRQ_MASKED);

    assert(kernel_irq_arm(endpoint) == KERNEL_IRQ_OK);
    thread = allocate_running_thread();
    assert(kernel_irq_prepare_wait(endpoint, &spec) == KERNEL_IRQ_WOULD_BLOCK);
    assert(kernel_thread_block(thread, spec.queue, spec.sequence) ==
           KERNEL_THREAD_OK);
    assert(kernel_irq_commit_wait(endpoint) == KERNEL_IRQ_OK);
    assert(kernel_irq_handle_retain(endpoint, NULL));
    clear_log(&controller);
    assert(kernel_irq_owner_died(5u, &revoked, &woken) == KERNEL_IRQ_OK);
    assert(revoked == 1u && woken == 1u);
    expect_log(&controller, "M");
    assert(thread->context.data[0] == ASTRA_SYSCALL_PEER_DEAD);
    assert(kernel_irq_prepare_wait(endpoint, &spec) == KERNEL_IRQ_CLOSED);
    kernel_irq_handle_release(endpoint, NULL);
    assert(kernel_irq_snapshot(slot, &snapshot));
    assert(snapshot.state == KERNEL_IRQ_REVOKING && snapshot.references == 1u);
    kernel_irq_handle_release(endpoint, NULL);
    clear_log(&controller);
    assert(service_all_revocations() == 1u);
    expect_log(&controller, "MQA");
    assert_endpoint_free(slot);

    endpoint = bind_endpoint(&controller, &device, 6u, 7u,
                             KERNEL_IRQ_TRIGGER_EDGE);
    slot = find_endpoint_slot(6u, 7u);
    device.fail_quiesce = 1u;
    clear_log(&controller);
    kernel_irq_handle_release(endpoint, NULL);
    expect_log(&controller, "M");
    assert(kernel_irq_snapshot(slot, &snapshot));
    assert(snapshot.state == KERNEL_IRQ_REVOKING && snapshot.references == 0u);
    clear_log(&controller);
    assert(kernel_irq_service_revocations(1u, &completed) ==
           KERNEL_IRQ_DEVICE_ERROR);
    assert(completed == 0u);
    expect_log(&controller, "MQ");
    controller.fail_acknowledge = 1u;
    clear_log(&controller);
    assert(kernel_irq_service_revocations(1u, &completed) ==
           KERNEL_IRQ_DEVICE_ERROR);
    assert(completed == 0u);
    expect_log(&controller, "MQA");
    assert(kernel_irq_revocation_pending());
    clear_log(&controller);
    assert(kernel_irq_service_revocations(1u, &completed) == KERNEL_IRQ_OK);
    assert(completed == 1u);
    expect_log(&controller, "MQA");
    assert_endpoint_free(slot);
}

static void test_revocation_service_is_batch_bounded(void)
{
    FakeController controller;
    FakeDevice first_device;
    FakeDevice second_device;
    KernelIrqEndpoint *first;
    KernelIrqEndpoint *second;
    uint32_t completed;
    uint32_t first_slot;
    uint32_t second_slot;

    initialize_test(&controller);
    first = bind_endpoint(&controller, &first_device, 13u, 13u,
                          KERNEL_IRQ_TRIGGER_LEVEL);
    second = bind_endpoint(&controller, &second_device, 13u, 14u,
                           KERNEL_IRQ_TRIGGER_LEVEL);
    first_slot = find_endpoint_slot(13u, 13u);
    second_slot = find_endpoint_slot(13u, 14u);

    kernel_irq_handle_release(first, NULL);
    kernel_irq_handle_release(second, NULL);
    assert(kernel_irq_revocation_pending());
    assert(first_device.quiesces == 0u && second_device.quiesces == 0u);

    assert(kernel_irq_service_revocations(1u, &completed) == KERNEL_IRQ_OK);
    assert(completed == 1u);
    assert(first_device.quiesces + second_device.quiesces == 1u);
    assert(kernel_irq_revocation_pending());

    assert(kernel_irq_service_revocations(1u, &completed) == KERNEL_IRQ_OK);
    assert(completed == 1u);
    assert(first_device.quiesces == 1u && second_device.quiesces == 1u);
    assert(!kernel_irq_revocation_pending());
    assert_endpoint_free(first_slot);
    assert_endpoint_free(second_slot);
}

static void test_limits_failures_and_diagnostics(void)
{
    FakeController controller;
    FakeDevice devices[KERNEL_IRQ_OWNER_MAX];
    KernelIrqEndpoint *endpoints_for_owner[KERNEL_IRQ_OWNER_MAX];
    KernelIrqEndpoint *endpoint = (KernelIrqEndpoint *)(uintptr_t)1u;
    KernelIrqBinding binding;
    KernelIrqPoolStats stats;
    KernelAllocationStats allocation;
    uint32_t woken;

    initialize_test(&controller);
    memset(&binding, 0, sizeof(binding));
    binding.source = 1u;
    binding.trigger = KERNEL_IRQ_TRIGGER_EDGE;
    binding.ipl = 3u;
    binding.vector = KERNEL_IRQ_COMMON_VECTOR;
    kernel_allocation_test_fail_site(KERNEL_ALLOCATION_SITE_IRQ_ENDPOINT, 1u);
    assert(kernel_irq_bind(7u, &binding, &endpoint) == KERNEL_IRQ_NO_SLOT);
    assert(endpoint == NULL);
    kernel_allocation_test_clear_failure();
    assert(kernel_allocation_site_stats(KERNEL_ALLOCATION_SITE_IRQ_ENDPOINT,
                                        &allocation));
    assert(allocation.current_units == 0u &&
           allocation.injected_failures == 1u);

    for (uint32_t index = 0u; index < KERNEL_IRQ_OWNER_MAX; ++index) {
        endpoints_for_owner[index] = bind_endpoint(
            &controller, &devices[index], 8u, (uint8_t)(8u + index),
            KERNEL_IRQ_TRIGGER_EDGE);
    }
    binding.source = 20u;
    assert(kernel_irq_bind(8u, &binding, &endpoint) ==
           KERNEL_IRQ_QUOTA_EXCEEDED);
    binding.source = 8u;
    assert(kernel_irq_bind(9u, &binding, &endpoint) ==
           KERNEL_IRQ_SOURCE_BUSY);

    clear_log(&controller);
    assert(kernel_irq_dispatch(31u, KERNEL_IRQ_COMMON_VECTOR, 1u, &woken) ==
           KERNEL_IRQ_UNCLAIMED);
    expect_log(&controller, "MA");
    assert(kernel_irq_arm(endpoints_for_owner[0]) == KERNEL_IRQ_OK);
    clear_log(&controller);
    assert(kernel_irq_dispatch(8u, KERNEL_IRQ_COMMON_VECTOR + 1u, 2u,
                               &woken) == KERNEL_IRQ_DEVICE_ERROR);
    expect_log(&controller, "MA");
    assert(kernel_irq_pool_stats(&stats));
    assert(stats.allocation_failures == 1u);
    assert(stats.quota_failures == 1u);
    assert(stats.source_busy_failures == 1u);
    assert(stats.unclaimed_interrupts == 1u);
    assert(stats.bad_vector_interrupts == 1u);

    for (uint32_t index = 0u; index < KERNEL_IRQ_OWNER_MAX; ++index)
        kernel_irq_handle_release(endpoints_for_owner[index], NULL);
    assert(service_all_revocations() == KERNEL_IRQ_OWNER_MAX);
    assert(kernel_irq_pool_valid());

    initialize_test(&controller);
    controller.fail_configure = 1u;
    endpoint = (KernelIrqEndpoint *)(uintptr_t)1u;
    assert(kernel_irq_bind(10u, &binding, &endpoint) ==
           KERNEL_IRQ_DEVICE_ERROR);
    assert(endpoint == NULL);
    expect_log(&controller, "CM");
    assert(kernel_irq_pool_stats(&stats));
    assert(stats.controller_failures == 1u);
    assert(stats.live_endpoints == 0u);
    assert(kernel_irq_pool_valid());
}

static void test_publication_rollback_and_generation_reuse(void)
{
    FakeController controller;
    FakeDevice device;
    KernelIrqEndpoint *endpoint;
    KernelIrqPoolStats stats;
    KernelIrqSnapshot snapshot;
    uint32_t old_generation;
    uint32_t slot;

    initialize_test(&controller);
    endpoint = bind_endpoint(&controller, &device, 11u, 1u,
                             KERNEL_IRQ_TRIGGER_EDGE);
    slot = find_endpoint_slot(11u, 1u);
    assert(kernel_irq_snapshot(slot, &snapshot));
    old_generation = snapshot.generation;
    kernel_irq_abandon_unpublished(endpoint);
    assert(service_all_revocations() == 1u);
    assert_endpoint_free(slot);

    endpoint = bind_endpoint(&controller, &device, 11u, 1u,
                             KERNEL_IRQ_TRIGGER_EDGE);
    assert(find_endpoint_slot(11u, 1u) == slot);
    assert(kernel_irq_snapshot(slot, &snapshot));
    assert(snapshot.generation != old_generation);
    kernel_irq_handle_release(endpoint, NULL);
    assert(service_all_revocations() == 1u);
    assert(kernel_irq_pool_stats(&stats));
    assert(stats.publication_rollbacks == 1u);
    assert(kernel_irq_pool_valid());
}

static void test_trace_covers_endpoint_lifecycle(void)
{
    FakeController controller;
    FakeDevice device;
    KernelIrqEndpoint *endpoint;
    KernelIrqRecord record;
    uint32_t event_flags;
    uint32_t woken;

    initialize_test(&controller);
    endpoint = bind_endpoint(&controller, &device, 12u, 12u,
                             KERNEL_IRQ_TRIGGER_EDGE);
    assert(trace_event_count[KERNEL_TRACE_EVENT_IRQ_BIND] == 1u);
    /*
     * Arming, delivery and acknowledgement are the per-transfer stream: two
     * records for every interrupt a device raises, which is what a debug build
     * is for and what a release must not be paying. The lifecycle records
     * around them -- bind, revoke, reset, quarantine -- are rare and explain a
     * machine that misbehaved, so they are kept either way. This asserts that
     * split rather than a fixed count, because the count is the build's to
     * decide.
     */
    const uint32_t chatty = KERNEL_TRACE_KEEPS(KERNEL_TRACE_LEVEL_DEBUG) ?
        1u : 0u;

    assert(kernel_irq_arm(endpoint) == KERNEL_IRQ_OK);
    assert(trace_event_count[KERNEL_TRACE_EVENT_IRQ_ARM] == chatty);
    assert(kernel_irq_dispatch(12u, KERNEL_IRQ_COMMON_VECTOR, 1234u,
                               &woken) == KERNEL_IRQ_OK);
    assert(trace_event_count[KERNEL_TRACE_EVENT_IRQ_DELIVER] == chatty);
    assert(kernel_irq_read(endpoint, &record, &event_flags) == KERNEL_IRQ_OK);
    assert(kernel_irq_ack(endpoint, record.sequence) == KERNEL_IRQ_OK);
    assert(trace_event_count[KERNEL_TRACE_EVENT_IRQ_ACK] == chatty);
    kernel_irq_handle_release(endpoint, NULL);
    assert(trace_event_count[KERNEL_TRACE_EVENT_IRQ_REVOKE] == 1u);
    assert(service_all_revocations() == 1u);
    assert(trace_event_count[KERNEL_TRACE_EVENT_DEVICE_RESET] == 1u);
    assert(kernel_irq_dispatch(31u, KERNEL_IRQ_COMMON_VECTOR, 1235u,
                               &woken) == KERNEL_IRQ_UNCLAIMED);
    assert(trace_event_count[KERNEL_TRACE_EVENT_IRQ_QUARANTINE] == 1u);
    assert(kernel_irq_pool_valid());
}

int main(void)
{
    test_edge_order_and_manual_mask();
    test_internal_route_uses_common_order();
    test_internal_edge_route_avoids_level_mask_cycle();
    test_level_acknowledgement_order_and_retry();
    test_a_capture_with_nothing_pending_is_not_a_failure();
    test_a_level_capture_with_nothing_pending_re_enables();
    test_wait_race_and_wakeup();
    test_overflow_and_storm_quarantine();
    test_a_serviced_device_is_never_a_storm();
    test_reference_owner_death_and_quiesce_retry();
    test_revocation_service_is_batch_bounded();
    test_limits_failures_and_diagnostics();
    test_publication_rollback_and_generation_reuse();
    test_trace_covers_endpoint_lifecycle();
    puts("kernel IRQ endpoint tests passed");
    return 0;
}
