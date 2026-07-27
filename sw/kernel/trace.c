#include "trace.h"

#include "bytes.h"
#include "platform.h"

#include <stddef.h>

typedef struct KernelTraceStorage {
    KernelTraceHeader header;
    KernelTraceRecord records[KERNEL_TRACE_CAPACITY];
} KernelTraceStorage;

typedef struct KernelTraceStagedRecord {
    uint32_t timestamp_high;
    uint32_t timestamp_low;
    uint16_t event;
    uint16_t flags;
    uint32_t argument[4];
} KernelTraceStagedRecord;

static KernelTraceStorage trace_storage
    __attribute__((section(".kernel_trace"), aligned(16), used));
static KernelTraceStagedRecord staged_records[KERNEL_TRACE_STAGE_CAPACITY];
static volatile uint32_t staged_write_sequence;
static volatile uint32_t staged_read_sequence;
static uint32_t staged_maximum_pending;
static uint32_t staged_total;
static uint32_t staged_flushed;
static uint32_t staged_dropped;
static uint8_t trace_initialized;

#if defined(KERNEL_TRACE_HOST_TEST)
static uint32_t torn_read_slot = UINT32_MAX;
#endif

_Static_assert(sizeof(KernelTraceHeader) == KERNEL_TRACE_HEADER_SIZE,
               "trace header size changed");
_Static_assert(sizeof(KernelTraceRecord) == KERNEL_TRACE_RECORD_SIZE,
               "trace record size changed");
_Static_assert(sizeof(KernelTraceStorage) == KERNEL_TRACE_STORAGE_SIZE,
               "trace storage must occupy exactly 64 KiB");
_Static_assert(sizeof(KernelTraceStagedRecord) == 28u,
               "staged trace record size changed");

static void trace_barrier(void)
{
    __asm__ volatile ("" ::: "memory");
}

static uint32_t next_sequence(uint32_t sequence)
{
    ++sequence;
    return sequence == 0u ? 1u : sequence;
}

static void increment_saturating(uint32_t *value)
{
    if (*value != UINT32_MAX)
        ++*value;
}

static bool header_valid(const KernelTraceHeader *header)
{
    return header->magic == KERNEL_TRACE_MAGIC &&
           header->abi_version == KERNEL_TRACE_ABI_VERSION &&
           header->record_size == KERNEL_TRACE_RECORD_SIZE &&
           header->capacity == KERNEL_TRACE_CAPACITY &&
           header->next_sequence != 0u &&
           header->write_index < KERNEL_TRACE_CAPACITY &&
           header->reserved == 0u;
}

static void copy_header(KernelTraceHeader *destination,
                        const KernelTraceHeader *source)
{
    destination->magic = source->magic;
    destination->abi_version = source->abi_version;
    destination->record_size = source->record_size;
    destination->capacity = source->capacity;
    destination->next_sequence = source->next_sequence;
    destination->write_index = source->write_index;
    destination->wrap_count = source->wrap_count;
    destination->dropped_count = source->dropped_count;
    destination->reserved = source->reserved;
}

static void initialize_header(void)
{
    kernel_bytes_clear(&trace_storage.header, sizeof(trace_storage.header));
    trace_storage.header.magic = KERNEL_TRACE_MAGIC;
    trace_storage.header.abi_version = KERNEL_TRACE_ABI_VERSION;
    trace_storage.header.record_size = KERNEL_TRACE_RECORD_SIZE;
    trace_storage.header.capacity = KERNEL_TRACE_CAPACITY;
    trace_storage.header.next_sequence = 1u;
}

bool kernel_trace_init(void)
{
    uint16_t saved_status = kernel_interrupt_save_disable();

    if (!header_valid(&trace_storage.header))
        initialize_header();
    staged_write_sequence = 0u;
    staged_read_sequence = 0u;
    staged_maximum_pending = 0u;
    staged_total = 0u;
    staged_flushed = 0u;
    staged_dropped = 0u;
    trace_initialized = 1u;
    kernel_interrupt_restore(saved_status);
    return header_valid(&trace_storage.header);
}

bool kernel_trace_valid(void)
{
    return trace_initialized != 0u && header_valid(&trace_storage.header);
}

bool kernel_trace_write(KernelTraceEvent event, uint16_t flags,
                        uint32_t argument0, uint32_t argument1,
                        uint32_t argument2, uint32_t argument3)
{
    KernelPlatformCycleCount cycles;

    kernel_platform_cpu_cycles(&cycles);
    return kernel_trace_write_at(
        event, flags, ((uint64_t)cycles.high << 32) | cycles.low,
        argument0, argument1, argument2, argument3);
}

bool kernel_trace_write_at(KernelTraceEvent event, uint16_t flags,
                           uint64_t timestamp, uint32_t argument0,
                           uint32_t argument1, uint32_t argument2,
                           uint32_t argument3)
{
    KernelTraceHeader *header;
    KernelTraceRecord *record;
    uint32_t sequence;
    uint32_t index;
    uint16_t saved_status;

    if ((uint32_t)event == 0u || (uint32_t)event > UINT16_MAX)
        return false;
    saved_status = kernel_interrupt_save_disable();
    if (!kernel_trace_valid()) {
        kernel_interrupt_restore(saved_status);
        return false;
    }
    header = &trace_storage.header;
    index = header->write_index;
    sequence = header->next_sequence;
    record = &trace_storage.records[index];
    if (header->wrap_count != 0u && record->commit_sequence != 0u)
        increment_saturating(&header->dropped_count);

    record->commit_sequence = 0u;
    trace_barrier();
    record->timestamp_high = (uint32_t)(timestamp >> 32);
    record->timestamp_low = (uint32_t)timestamp;
    record->event = (uint16_t)event;
    record->flags = flags;
    record->argument[0] = argument0;
    record->argument[1] = argument1;
    record->argument[2] = argument2;
    record->argument[3] = argument3;
    trace_barrier();
    record->commit_sequence = sequence;
    trace_barrier();

    header->next_sequence = next_sequence(sequence);
    ++index;
    if (index == KERNEL_TRACE_CAPACITY) {
        index = 0u;
        increment_saturating(&header->wrap_count);
    }
    header->write_index = index;
    kernel_interrupt_restore(saved_status);
    return true;
}

bool kernel_trace_stage_at(KernelTraceEvent event, uint16_t flags,
                           uint64_t timestamp, uint32_t argument0,
                           uint32_t argument1, uint32_t argument2,
                           uint32_t argument3)
{
    KernelTraceStagedRecord *record;
    uint32_t pending;
    uint32_t write_sequence;

    if ((uint32_t)event == 0u || (uint32_t)event > UINT16_MAX ||
        trace_initialized == 0u)
        return false;
    write_sequence = staged_write_sequence;
    pending = write_sequence - staged_read_sequence;
    if (pending >= KERNEL_TRACE_STAGE_CAPACITY) {
        increment_saturating(&staged_dropped);
        return false;
    }
    record = &staged_records[
        write_sequence % KERNEL_TRACE_STAGE_CAPACITY];
    record->timestamp_high = (uint32_t)(timestamp >> 32);
    record->timestamp_low = (uint32_t)timestamp;
    record->event = (uint16_t)event;
    record->flags = flags;
    record->argument[0] = argument0;
    record->argument[1] = argument1;
    record->argument[2] = argument2;
    record->argument[3] = argument3;
    trace_barrier();
    staged_write_sequence = write_sequence + 1u;
    increment_saturating(&staged_total);
    ++pending;
    if (pending > staged_maximum_pending)
        staged_maximum_pending = pending;
    return true;
}

uint32_t kernel_trace_flush_staged(uint32_t batch_limit)
{
    uint32_t flushed = 0u;

    while (flushed < batch_limit) {
        KernelTraceStagedRecord record;
        const KernelTraceStagedRecord *source;
        uint32_t read_sequence = staged_read_sequence;

        if (read_sequence == staged_write_sequence)
            break;
        trace_barrier();
        source = &staged_records[
            read_sequence % KERNEL_TRACE_STAGE_CAPACITY];
        record.timestamp_high = source->timestamp_high;
        record.timestamp_low = source->timestamp_low;
        record.event = source->event;
        record.flags = source->flags;
        for (uint32_t index = 0u; index < 4u; ++index)
            record.argument[index] = source->argument[index];
        trace_barrier();
        if (!kernel_trace_write_at(
                (KernelTraceEvent)record.event, record.flags,
                ((uint64_t)record.timestamp_high << 32) |
                    record.timestamp_low,
                record.argument[0], record.argument[1],
                record.argument[2], record.argument[3]))
            break;
        staged_read_sequence = read_sequence + 1u;
        increment_saturating(&staged_flushed);
        ++flushed;
    }
    return flushed;
}

bool kernel_trace_staged_pending(void)
{
    return staged_read_sequence != staged_write_sequence;
}

bool kernel_trace_stage_stats(KernelTraceStageStats *stats)
{
    uint16_t saved_status;

    if (stats == NULL)
        return false;
    saved_status = kernel_interrupt_save_disable();
    stats->pending = staged_write_sequence - staged_read_sequence;
    stats->maximum_pending = staged_maximum_pending;
    stats->staged = staged_total;
    stats->flushed = staged_flushed;
    stats->dropped = staged_dropped;
    kernel_interrupt_restore(saved_status);
    return stats->pending <= KERNEL_TRACE_STAGE_CAPACITY;
}

bool kernel_trace_header(KernelTraceHeader *header)
{
    uint16_t saved_status;

    if (header == NULL)
        return false;
    saved_status = kernel_interrupt_save_disable();
    if (!kernel_trace_valid()) {
        kernel_interrupt_restore(saved_status);
        return false;
    }
    copy_header(header, &trace_storage.header);
    kernel_interrupt_restore(saved_status);
    return header_valid(header);
}

bool kernel_trace_read_slot(uint32_t slot, KernelTraceRecord *record)
{
    volatile KernelTraceRecord *source;
    uint32_t first_commit;
    uint32_t second_commit;

    if (!kernel_trace_valid() || slot >= KERNEL_TRACE_CAPACITY ||
        record == NULL)
        return false;
    source = &trace_storage.records[slot];
    first_commit = source->commit_sequence;
    if (first_commit == 0u)
        return false;
    trace_barrier();
    record->commit_sequence = first_commit;
    record->timestamp_high = source->timestamp_high;
    record->timestamp_low = source->timestamp_low;
    record->event = source->event;
    record->flags = source->flags;
    for (uint32_t index = 0u; index < 4u; ++index)
        record->argument[index] = source->argument[index];
#if defined(KERNEL_TRACE_HOST_TEST)
    if (torn_read_slot == slot) {
        torn_read_slot = UINT32_MAX;
        source->commit_sequence = 0u;
    }
#endif
    trace_barrier();
    second_commit = source->commit_sequence;
    return first_commit == second_commit && second_commit != 0u;
}

static uint32_t committed_count(const KernelTraceHeader *header)
{
    return header->wrap_count == 0u ? header->write_index :
                                      KERNEL_TRACE_CAPACITY;
}

static uint32_t sequence_before(uint32_t next, uint32_t distance)
{
    if (next > distance)
        return next - distance;
    return UINT32_MAX - (distance - next);
}

static bool read_recent_from_header(const KernelTraceHeader *header,
                                    uint32_t newest_offset,
                                    KernelTraceRecord *record)
{
    uint32_t expected_sequence;
    uint32_t slot;

    if (newest_offset >= committed_count(header))
        return false;
    slot = (header->write_index + KERNEL_TRACE_CAPACITY - 1u -
            newest_offset) % KERNEL_TRACE_CAPACITY;
    expected_sequence = sequence_before(header->next_sequence,
                                        newest_offset + 1u);
    return kernel_trace_read_slot(slot, record) &&
           record->commit_sequence == expected_sequence;
}

bool kernel_trace_read_recent(uint32_t newest_offset,
                              KernelTraceRecord *record)
{
    KernelTraceHeader header;

    if (!kernel_trace_header(&header))
        return false;
    return read_recent_from_header(&header, newest_offset, record);
}

uint32_t kernel_trace_copy_recent(KernelTraceRecord *records,
                                  uint32_t capacity)
{
    KernelTraceHeader header;
    uint32_t copied = 0u;
    uint32_t available;

    if (records == NULL || !kernel_trace_header(&header))
        return 0u;
    available = committed_count(&header);
    for (uint32_t offset = 0u;
         offset < available && copied < capacity; ++offset) {
        if (read_recent_from_header(&header, offset, &records[copied]))
            ++copied;
    }
    return copied;
}

#if defined(KERNEL_TRACE_HOST_TEST)
void kernel_trace_test_inject_torn_read(uint32_t slot)
{
    torn_read_slot = slot < KERNEL_TRACE_CAPACITY ? slot : UINT32_MAX;
}

void kernel_trace_test_invalidate(uint32_t stale_commit_sequence)
{
    trace_storage.header.magic = 0u;
    trace_storage.records[0].commit_sequence = stale_commit_sequence;
    trace_initialized = 0u;
}
#endif
