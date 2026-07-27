#include "platform.h"
#include "trace.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint16_t simulated_status = 0x2300u;
static uint64_t simulated_cycles;
static uint32_t save_count;
static uint32_t restore_count;

uint16_t kernel_interrupt_save_disable(void)
{
    uint16_t saved = simulated_status;

    simulated_status |= 0x0700u;
    ++save_count;
    return saved;
}

void kernel_interrupt_restore(uint16_t status_register)
{
    assert((simulated_status & 0x0700u) == 0x0700u);
    simulated_status = status_register;
    ++restore_count;
}

void kernel_platform_cpu_cycles(KernelPlatformCycleCount *cycles)
{
    assert(cycles != NULL);
    cycles->high = (uint32_t)(simulated_cycles >> 32);
    cycles->low = (uint32_t)simulated_cycles;
}

static void test_retained_ring_wrap_and_torn_read(void)
{
    KernelTraceHeader before_reinit;
    KernelTraceHeader header;
    KernelTraceRecord records[4];
    KernelTraceRecord record;
    uint32_t newest_slot;

    assert(kernel_trace_init());
    assert(kernel_trace_valid());
    assert(simulated_status == 0x2300u);
    assert(kernel_trace_header(&header));
    assert(header.magic == KERNEL_TRACE_MAGIC);
    assert(header.abi_version == KERNEL_TRACE_ABI_VERSION);
    assert(header.record_size == KERNEL_TRACE_RECORD_SIZE);
    assert(header.capacity == KERNEL_TRACE_CAPACITY);
    assert(header.next_sequence == 1u);
    assert(header.write_index == 0u);

    assert(kernel_trace_write_at(
        KERNEL_TRACE_EVENT_BOOT, 0x1234u,
        UINT64_C(0x1122334455667788), 1u, 2u, 3u, 4u));
    assert(simulated_status == 0x2300u);
    assert(kernel_trace_read_recent(0u, &record));
    assert(record.commit_sequence == 1u);
    assert(record.timestamp_high == 0x11223344u);
    assert(record.timestamp_low == 0x55667788u);
    assert(record.event == KERNEL_TRACE_EVENT_BOOT);
    assert(record.flags == 0x1234u);
    assert(record.argument[0] == 1u && record.argument[3] == 4u);

    for (uint32_t index = 1u; index < KERNEL_TRACE_CAPACITY + 2u;
         ++index) {
        assert(kernel_trace_write_at(
            KERNEL_TRACE_EVENT_IRQ_DELIVER, 0u, index,
            index, index + 1u, index + 2u, index + 3u));
    }
    assert(kernel_trace_header(&header));
    assert(header.write_index == 2u);
    assert(header.wrap_count == 1u);
    assert(header.dropped_count == 2u);
    assert(header.next_sequence == KERNEL_TRACE_CAPACITY + 3u);
    assert(kernel_trace_read_recent(0u, &record));
    assert(record.argument[0] == KERNEL_TRACE_CAPACITY + 1u);
    assert(kernel_trace_read_recent(KERNEL_TRACE_CAPACITY - 1u, &record));
    assert(record.argument[0] == 2u);

    newest_slot = (header.write_index + KERNEL_TRACE_CAPACITY - 1u) %
                  KERNEL_TRACE_CAPACITY;
    kernel_trace_test_inject_torn_read(newest_slot);
    assert(!kernel_trace_read_recent(0u, &record));
    assert(kernel_trace_copy_recent(records, 4u) == 4u);
    assert(records[0].argument[0] == KERNEL_TRACE_CAPACITY);

    assert(kernel_trace_header(&before_reinit));
    assert(kernel_trace_init());
    assert(kernel_trace_header(&header));
    assert(header.next_sequence == before_reinit.next_sequence);
    assert(header.write_index == before_reinit.write_index);
    assert(header.wrap_count == before_reinit.wrap_count);
    assert(header.dropped_count == before_reinit.dropped_count);
    assert(save_count == restore_count);
    assert(simulated_status == 0x2300u);
}

static void test_invalid_header_hides_stale_records(void)
{
    KernelTraceHeader header;
    KernelTraceRecord record;

    kernel_trace_test_invalidate(0xfeedbeefu);
    assert(kernel_trace_init());
    assert(kernel_trace_header(&header));
    assert(header.next_sequence == 1u);
    assert(header.write_index == 0u);
    assert(header.wrap_count == 0u);
    assert(header.dropped_count == 0u);
    assert(!kernel_trace_read_recent(0u, &record));

    assert(kernel_trace_write_at(
        KERNEL_TRACE_EVENT_BOOT, 0u, 1u, 2u, 3u, 4u, 5u));
    assert(kernel_trace_header(&header));
    assert(header.write_index == 1u);
    assert(header.dropped_count == 0u);
    assert(kernel_trace_read_recent(0u, &record));
    assert(record.commit_sequence == 1u);
    assert(record.argument[0] == 2u && record.argument[3] == 5u);
    assert(save_count == restore_count);
}

static void test_hard_irq_staging_is_bounded_and_deferred(void)
{
    KernelTraceStageStats stats;
    KernelTraceRecord record;

    assert(kernel_trace_init());
    for (uint32_t index = 0u; index < KERNEL_TRACE_STAGE_CAPACITY; ++index) {
        assert(kernel_trace_stage_at(
            KERNEL_TRACE_EVENT_IRQ_EXIT, (uint16_t)index,
            UINT64_C(0x100000000) + index,
            index, index + 1u, index + 2u, index + 3u));
    }
    assert(!kernel_trace_stage_at(
        KERNEL_TRACE_EVENT_IRQ_EXIT, 0u, 0u, 0u, 0u, 0u, 0u));
    assert(kernel_trace_staged_pending());
    assert(kernel_trace_stage_stats(&stats));
    assert(stats.pending == KERNEL_TRACE_STAGE_CAPACITY);
    assert(stats.maximum_pending == KERNEL_TRACE_STAGE_CAPACITY);
    assert(stats.staged == KERNEL_TRACE_STAGE_CAPACITY);
    assert(stats.flushed == 0u);
    assert(stats.dropped == 1u);

    assert(kernel_trace_flush_staged(7u) == 7u);
    assert(kernel_trace_stage_stats(&stats));
    assert(stats.pending == KERNEL_TRACE_STAGE_CAPACITY - 7u);
    assert(stats.flushed == 7u);
    assert(kernel_trace_flush_staged(UINT32_MAX) ==
           KERNEL_TRACE_STAGE_CAPACITY - 7u);
    assert(!kernel_trace_staged_pending());
    assert(kernel_trace_stage_stats(&stats));
    assert(stats.pending == 0u);
    assert(stats.flushed == KERNEL_TRACE_STAGE_CAPACITY);
    assert(kernel_trace_read_recent(0u, &record));
    assert(record.event == KERNEL_TRACE_EVENT_IRQ_EXIT);
    assert(record.argument[0] == KERNEL_TRACE_STAGE_CAPACITY - 1u);
}

int main(void)
{
    test_retained_ring_wrap_and_torn_read();
    test_invalid_header_hides_stale_records();
    test_hard_irq_staging_is_bounded_and_deferred();
    puts("retained trace tests passed");
    return 0;
}
