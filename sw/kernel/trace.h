#ifndef ASTRA_KERNEL_TRACE_H
#define ASTRA_KERNEL_TRACE_H

#include <stdbool.h>
#include <stdint.h>

#include <astra/event.h>

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
 * The levels and flags are astra/event.h's, spelled the way the ring spells
 * things. One definition, so a program's call and the record it lands in can
 * never disagree about what a bit means.
 */
#define KERNEL_TRACE_LEVEL_DEBUG   ASTRA_EVENT_LEVEL_DEBUG
#define KERNEL_TRACE_LEVEL_INFO    ASTRA_EVENT_LEVEL_INFO
#define KERNEL_TRACE_LEVEL_NOTICE  ASTRA_EVENT_LEVEL_NOTICE
#define KERNEL_TRACE_LEVEL_WARNING ASTRA_EVENT_LEVEL_WARNING
#define KERNEL_TRACE_LEVEL_ERROR   ASTRA_EVENT_LEVEL_ERROR

#define KERNEL_TRACE_LEVEL_MASK    ASTRA_EVENT_LEVEL_MASK
#define KERNEL_TRACE_LEVEL_OF(flags) \
    ((uint32_t)((flags) & KERNEL_TRACE_LEVEL_MASK))
#define KERNEL_TRACE_FLAG_PRESENTED     ASTRA_EVENT_FLAG_PRESENTED
#define KERNEL_TRACE_FLAG_INLINE_STRING ASTRA_EVENT_FLAG_INLINE_STRING
#define KERNEL_TRACE_FLAG_CONTINUED     ASTRA_EVENT_FLAG_CONTINUED
#define KERNEL_TRACE_FLAG_MASK          ASTRA_EVENT_FLAG_MASK

#define KERNEL_TRACE_ARGUMENT_BYTES ASTRA_EVENT_ARGUMENT_MAX

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
                             uint16_t thread, uint32_t activity,
                             uint16_t flags, const void *payload,
                             uint32_t payload_length);

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

/*
 * Copies the user events after `after_sequence`, oldest first, and returns how
 * many. `after_sequence` of zero means everything the ring still holds, which
 * is what a reader that has never read before asks for.
 *
 * `*cursor` becomes the sequence to pass next time -- the last event returned,
 * or `after_sequence` unchanged when there was nothing. A caller that stops
 * early because its buffer filled resumes exactly where it stopped, so a drain
 * is a bounded page and a cursor like every other enumeration in this system.
 *
 * `*lost` counts what the caller will never see: the slots the ring displaced
 * while it was away, plus any event found here whose arguments a wrap ate. It
 * is a count of ring slots, not of events -- an event with arguments occupies
 * two -- and it is reported rather than rounded, because the one thing a reader
 * must not be told about a gap is nothing.
 *
 * Kernel events are skipped, not counted as loss. They are in the same ring on
 * purpose, and the plan that turns their enum into descriptors is what makes
 * them drainable; until then they are simply not this reader's records.
 */
uint32_t kernel_trace_drain_user(uint32_t after_sequence,
                                 AstraEventDrained *events, uint32_t capacity,
                                 uint32_t *cursor, uint32_t *lost);

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
