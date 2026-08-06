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


/* A fresh ring, so a test's slot indices mean what they say. */
static void reset_ring(void)
{
    kernel_trace_test_invalidate(0xfeedbeefu);
    assert(kernel_trace_init());
}

static void test_a_user_event_carries_its_arguments(void)
{
    KernelTraceUserRecord user;
    uint8_t payload[KERNEL_TRACE_ARGUMENT_BYTES];
    uint8_t read_back[KERNEL_TRACE_ARGUMENT_BYTES];
    uint32_t length = 0u;

    reset_ring();
    for (uint32_t index = 0u; index < sizeof(payload); ++index)
        payload[index] = (uint8_t)(index + 1u);

    assert(kernel_trace_write_user(0x11223344u, 0x1000u, 7u, 0x5150u,
                                   KERNEL_TRACE_LEVEL_WARNING, payload, 12u));
    assert(kernel_trace_read_user(0u, &user, read_back, sizeof(read_back),
                                  &length));
    assert(user.event == KERNEL_TRACE_EVENT_USER);
    assert(user.process == 0x1000u);
    assert(user.message == 0x11223344u);
    assert(user.thread == 7u);
    assert(KERNEL_TRACE_LEVEL_OF(user.flags) == KERNEL_TRACE_LEVEL_WARNING);
    assert(length == 12u);
    for (uint32_t index = 0u; index < length; ++index)
        assert(read_back[index] == payload[index]);
    /* The activity the emitter was in, carried without a call site saying so. */
    assert(user.activity == 0x5150u);
    /* The arguments are a slot of their own, and it says so. */
    assert(!kernel_trace_read_user(1u, &user, read_back, sizeof(read_back),
                                   &length));
}

static void test_an_event_with_no_arguments_costs_one_slot(void)
{
    KernelTraceHeader header;
    KernelTraceUserRecord user;
    uint8_t read_back[KERNEL_TRACE_ARGUMENT_BYTES];
    uint32_t length = 7u;

    reset_ring();
    assert(kernel_trace_write_user(1u, 0x1000u, 1u, 0u, KERNEL_TRACE_LEVEL_INFO,
                                   NULL, 0u));
    assert(kernel_trace_header(&header));
    assert(header.write_index == 1u);
    assert(kernel_trace_read_user(0u, &user, NULL, 0u, &length));
    assert(length == 0u);

    /* One with arguments costs two, and they are consecutive. */
    assert(kernel_trace_write_user(2u, 0x1000u, 1u, 0u, KERNEL_TRACE_LEVEL_INFO,
                                   "ab", 2u));
    assert(kernel_trace_header(&header));
    assert(header.write_index == 3u);
    assert(kernel_trace_read_user(1u, &user, read_back, sizeof(read_back),
                                  &length));
    assert(user.message == 2u);
    assert(length == 2u);
    /*
     * An event with arguments and nowhere to put them is refused whole. A
     * half-answer -- the header, silently without its arguments -- is the one
     * outcome a reader could not tell from an event that had none.
     */
    assert(!kernel_trace_read_user(1u, &user, NULL, 0u, &length));
    assert(!kernel_trace_read_user(1u, &user, read_back, 1u, &length));
}

static void test_argument_refusals(void)
{
    uint8_t payload[KERNEL_TRACE_ARGUMENT_BYTES + 1u] = {0u};
    KernelTraceHeader header;

    reset_ring();
    /*
     * More than the slot holds is refused, never truncated: a truncated
     * argument is a wrong value rather than a missing one, and a log carrying
     * one is worse than a log carrying neither.
     */
    assert(!kernel_trace_write_user(1u, 0x1000u, 1u, 0u, KERNEL_TRACE_LEVEL_INFO,
                                    payload, sizeof(payload)));
    /* A length with no payload, and a payload with no length. */
    assert(!kernel_trace_write_user(1u, 0x1000u, 1u, 0u, KERNEL_TRACE_LEVEL_INFO,
                                    NULL, 4u));
    assert(!kernel_trace_write_user(1u, 0x1000u, 1u, 0u, KERNEL_TRACE_LEVEL_INFO,
                                    payload, 0u));
    /* A message id of zero names no message. */
    assert(!kernel_trace_write_user(0u, 0x1000u, 1u, 0u, KERNEL_TRACE_LEVEL_INFO,
                                    NULL, 0u));
    /* A level outside the five is a caller defect, not a level. */
    assert(!kernel_trace_write_user(1u, 0x1000u, 1u,
                                    0u,
                                    KERNEL_TRACE_LEVEL_ERROR + 1u, NULL, 0u));
    /*
     * A flag this build does not know is a record it cannot render. Accepting
     * one silently means a reader shows the event as though the bit were
     * clear, which is a wrong answer rather than a refused one.
     */
    assert(!kernel_trace_write_user(1u, 0x1000u, 1u, 0u, 0x8000u, NULL, 0u));

    /* Nothing above reached the ring. */
    assert(kernel_trace_header(&header));
    assert(header.write_index == 0u);
}

static void test_arguments_lost_to_a_wrap_are_reported_as_lost(void)
{
    KernelTraceUserRecord user;
    uint8_t read_back[KERNEL_TRACE_ARGUMENT_BYTES];
    uint32_t length = 0u;

    /*
     * The header slot survives a wrap that ate its arguments, because the
     * writer reaches the header first. A reader must say the arguments are
     * gone rather than render whatever now occupies the slot: a plausible
     * wrong value in a log is worse than an absent one.
     */
    reset_ring();
    assert(kernel_trace_write_user(1u, 0x1000u, 1u, 0u, KERNEL_TRACE_LEVEL_INFO,
                                   "abcd", 4u));
    kernel_trace_test_overwrite_argument_slot(1u);
    assert(!kernel_trace_read_user(0u, &user, read_back, sizeof(read_back),
                                   &length));
}

static void test_a_user_event_shares_the_one_ring(void)
{
    KernelTraceUserRecord user;
    KernelTraceRecord record;
    uint32_t length = 0u;

    /*
     * One stream with one set of sequence numbers. A kernel event and a user
     * event are ordered against each other because they are the same ring;
     * two rings would be two timelines a reader could not merge.
     */
    reset_ring();
    assert(kernel_trace_write_at(KERNEL_TRACE_EVENT_BOOT, 0u, 1u, 0u, 0u, 0u,
                                 0u));
    assert(kernel_trace_write_user(9u, 0x1000u, 1u, 0u, KERNEL_TRACE_LEVEL_NOTICE,
                                   NULL, 0u));
    assert(kernel_trace_write_at(KERNEL_TRACE_EVENT_PANIC, 0u, 2u, 0u, 0u, 0u,
                                 0u));
    assert(kernel_trace_read_slot(0u, &record));
    assert(record.event == KERNEL_TRACE_EVENT_BOOT);
    assert(record.commit_sequence == 1u);
    assert(kernel_trace_read_user(1u, &user, NULL, 0u, &length));
    assert(user.commit_sequence == 2u);
    assert(kernel_trace_read_slot(2u, &record));
    assert(record.event == KERNEL_TRACE_EVENT_PANIC);
    assert(record.commit_sequence == 3u);
    /* A user event read as a kernel one is still a valid slot, and says USER. */
    assert(kernel_trace_read_slot(1u, &record));
    assert(record.event == KERNEL_TRACE_EVENT_USER);
}

int main(void)
{
    test_retained_ring_wrap_and_torn_read();
    test_invalid_header_hides_stale_records();
    test_hard_irq_staging_is_bounded_and_deferred();
    test_a_user_event_carries_its_arguments();
    test_an_event_with_no_arguments_costs_one_slot();
    test_argument_refusals();
    test_arguments_lost_to_a_wrap_are_reported_as_lost();
    test_a_user_event_shares_the_one_ring();
    puts("retained trace tests passed");
    return 0;
}
