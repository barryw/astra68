#ifndef ASTRA_KERNEL_TRACE_H
#define ASTRA_KERNEL_TRACE_H

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_TRACE_MAGIC 0x41545243u /* "ATRC" */
#define KERNEL_TRACE_ABI_VERSION 1u
#define KERNEL_TRACE_STORAGE_SIZE 0x00010000u
#define KERNEL_TRACE_HEADER_SIZE 32u
#define KERNEL_TRACE_RECORD_SIZE 32u
#define KERNEL_TRACE_CAPACITY 2047u
#define KERNEL_TRACE_STAGE_CAPACITY 32u

typedef enum KernelTraceEvent {
    KERNEL_TRACE_EVENT_BOOT = 1,
    KERNEL_TRACE_EVENT_PANIC,
    KERNEL_TRACE_EVENT_EXCEPTION_ENTRY,
    KERNEL_TRACE_EVENT_EXCEPTION_EXIT,
    KERNEL_TRACE_EVENT_IRQ_ENTRY,
    KERNEL_TRACE_EVENT_IRQ_EXIT,
    KERNEL_TRACE_EVENT_IRQ_BIND,
    KERNEL_TRACE_EVENT_IRQ_ARM,
    KERNEL_TRACE_EVENT_IRQ_DELIVER,
    KERNEL_TRACE_EVENT_IRQ_ACK,
    KERNEL_TRACE_EVENT_IRQ_QUARANTINE,
    KERNEL_TRACE_EVENT_IRQ_REVOKE,
    KERNEL_TRACE_EVENT_DEVICE_RESET,
    KERNEL_TRACE_EVENT_CONTEXT_SWITCH,
    KERNEL_TRACE_EVENT_THREAD_WAKE,
    KERNEL_TRACE_EVENT_SYSCALL_ENTRY,
    KERNEL_TRACE_EVENT_SYSCALL_EXIT,
    KERNEL_TRACE_EVENT_HANDLE_INSTALL,
    KERNEL_TRACE_EVENT_HANDLE_CLOSE,
    KERNEL_TRACE_EVENT_IPC_QUEUE,
    KERNEL_TRACE_EVENT_VM_MAP,
    KERNEL_TRACE_EVENT_VM_UNMAP,
    KERNEL_TRACE_EVENT_PMMU_FAULT,
    KERNEL_TRACE_EVENT_PHYSICAL_FAULT,
    KERNEL_TRACE_EVENT_ALLOCATION_FAILURE,
    KERNEL_TRACE_EVENT_MONITOR_COMMAND,
    KERNEL_TRACE_EVENT_MONITOR_DROP
} KernelTraceEvent;

typedef struct KernelTraceHeader {
    uint32_t magic;
    uint16_t abi_version;
    uint16_t record_size;
    uint32_t capacity;
    uint32_t next_sequence;
    uint32_t write_index;
    uint32_t wrap_count;
    uint32_t dropped_count;
    uint32_t reserved;
} KernelTraceHeader;

typedef struct KernelTraceRecord {
    uint32_t commit_sequence;
    uint32_t timestamp_high;
    uint32_t timestamp_low;
    uint16_t event;
    uint16_t flags;
    uint32_t argument[4];
} KernelTraceRecord;

typedef struct KernelTraceStageStats {
    uint32_t pending;
    uint32_t maximum_pending;
    uint32_t staged;
    uint32_t flushed;
    uint32_t dropped;
} KernelTraceStageStats;

bool kernel_trace_init(void);
bool kernel_trace_valid(void);
bool kernel_trace_write(KernelTraceEvent event, uint16_t flags,
                        uint32_t argument0, uint32_t argument1,
                        uint32_t argument2, uint32_t argument3);
bool kernel_trace_write_at(KernelTraceEvent event, uint16_t flags,
                           uint64_t timestamp, uint32_t argument0,
                           uint32_t argument1, uint32_t argument2,
                           uint32_t argument3);
/* Single-producer hard-IRQ path; the worker drains records to retained RAM. */
bool kernel_trace_stage_at(KernelTraceEvent event, uint16_t flags,
                           uint64_t timestamp, uint32_t argument0,
                           uint32_t argument1, uint32_t argument2,
                           uint32_t argument3);
uint32_t kernel_trace_flush_staged(uint32_t batch_limit);
bool kernel_trace_staged_pending(void);
bool kernel_trace_stage_stats(KernelTraceStageStats *stats);
bool kernel_trace_header(KernelTraceHeader *header);
bool kernel_trace_read_slot(uint32_t slot, KernelTraceRecord *record);
bool kernel_trace_read_recent(uint32_t newest_offset,
                              KernelTraceRecord *record);
uint32_t kernel_trace_copy_recent(KernelTraceRecord *records,
                                  uint32_t capacity);

#if defined(KERNEL_TRACE_HOST_TEST)
void kernel_trace_test_inject_torn_read(uint32_t slot);
void kernel_trace_test_invalidate(uint32_t stale_commit_sequence);
#endif

#endif
