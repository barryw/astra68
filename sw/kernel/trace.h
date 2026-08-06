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

/*
 * User events share the ring with the kernel's own, and are discriminated by
 * the event field rather than by a second ring. One ordered stream with one
 * set of sequence numbers is the point -- see the layout spec's section 6 --
 * and two rings would be two timelines a reader could not merge, because it
 * could not know which write happened first.
 *
 * These values sit above the KernelTraceEvent enum's range so the enum can
 * keep growing. The plan after next turns that enum into descriptors too, and
 * then every record here is this shape and the discrimination goes away.
 */
#define KERNEL_TRACE_EVENT_USER           0xE000u
#define KERNEL_TRACE_EVENT_USER_ARGUMENTS 0xE001u

/*
 * Five levels, ordered, because filtering by severity is the one thing every
 * reader of a log wants. This is where severity lives on this machine; a
 * status never carries it, and astra/status.h says why.
 */
#define KERNEL_TRACE_LEVEL_DEBUG   0u
#define KERNEL_TRACE_LEVEL_INFO    1u
#define KERNEL_TRACE_LEVEL_NOTICE  2u
#define KERNEL_TRACE_LEVEL_WARNING 3u
#define KERNEL_TRACE_LEVEL_ERROR   4u

#define KERNEL_TRACE_LEVEL_MASK    0x0007u
#define KERNEL_TRACE_LEVEL_OF(flags) \
    ((uint32_t)((flags) & KERNEL_TRACE_LEVEL_MASK))
/* The person was shown this. What makes an event notification history. */
#define KERNEL_TRACE_FLAG_PRESENTED     0x0008u
/* The payload is text rather than typed arguments. */
#define KERNEL_TRACE_FLAG_INLINE_STRING 0x0010u
#define KERNEL_TRACE_FLAG_MASK (KERNEL_TRACE_LEVEL_MASK | \
                                KERNEL_TRACE_FLAG_PRESENTED | \
                                KERNEL_TRACE_FLAG_INLINE_STRING)

/*
 * 24 rather than the design's 32: eight bytes of the argument slot pay for its
 * own commit sequence and its discriminator, without which a slot stops being
 * self-describing and kernel_trace_read_slot cannot answer for one in
 * isolation -- which is the API the monitor and every existing test are built
 * on. Four u32 arguments or three u64 ones fit.
 */
#define KERNEL_TRACE_ARGUMENT_BYTES 24u

typedef struct KernelTraceUserRecord {
    uint32_t commit_sequence;
    uint32_t timestamp_high;
    uint32_t timestamp_low;
    uint16_t event;            /* KERNEL_TRACE_EVENT_USER */
    uint16_t flags;            /* level, presented, inline string */
    uint32_t process;          /* generation-tagged, per OBSERVABILITY.md */
    uint32_t message;          /* the message id; a descriptor address later */
    uint32_t activity;         /* zero until the Kit fills it in */
    uint16_t thread;
    uint16_t payload_length;   /* 0..KERNEL_TRACE_ARGUMENT_BYTES */
} KernelTraceUserRecord;

typedef struct KernelTraceArgumentRecord {
    uint32_t commit_sequence;
    uint16_t event;            /* KERNEL_TRACE_EVENT_USER_ARGUMENTS */
    uint16_t reserved;
    uint8_t  payload[KERNEL_TRACE_ARGUMENT_BYTES];
} KernelTraceArgumentRecord;

/*
 * One slot, or two when the event carries arguments. The two are written
 * inside one interrupt-disabled window, header first: a wrapping writer
 * reaches the header before the arguments, so a header whose successor still
 * carries the sequence it expects proves both survived.
 */
typedef union KernelTraceSlot {
    KernelTraceRecord record;
    KernelTraceUserRecord user;
    KernelTraceArgumentRecord arguments;
} KernelTraceSlot;

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
/*
 * Appends one event, and its arguments in the following slot when it has any.
 * Refuses rather than truncates: a shortened argument is a wrong value, and a
 * log that carries one is worse than a log that carries none.
 */
bool kernel_trace_write_user(uint32_t message, uint32_t process,
                             uint16_t thread, uint16_t flags,
                             const void *payload, uint32_t payload_length);

/*
 * Reads the user event at `slot`. Returns false when the slot is not a user
 * event, when it was torn, or when its arguments were lost to a wrap -- the
 * caller learns nothing rather than something plausible and wrong.
 *
 * An event carrying arguments needs somewhere to put them, and is refused
 * whole when `capacity` cannot hold them: the header on its own is the one
 * answer a caller could not tell from an event that had no arguments.
 */
bool kernel_trace_read_user(uint32_t slot, KernelTraceUserRecord *record,
                            void *payload, uint32_t capacity,
                            uint32_t *payload_length);

bool kernel_trace_header(KernelTraceHeader *header);
bool kernel_trace_read_slot(uint32_t slot, KernelTraceRecord *record);
bool kernel_trace_read_recent(uint32_t newest_offset,
                              KernelTraceRecord *record);
uint32_t kernel_trace_copy_recent(KernelTraceRecord *records,
                                  uint32_t capacity);

#if defined(KERNEL_TRACE_HOST_TEST)
void kernel_trace_test_inject_torn_read(uint32_t slot);
void kernel_trace_test_invalidate(uint32_t stale_commit_sequence);
void kernel_trace_test_overwrite_argument_slot(uint32_t slot);
#endif

#endif
