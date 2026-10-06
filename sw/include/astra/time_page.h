#ifndef ASTRA_TIME_PAGE_H
#define ASTRA_TIME_PAGE_H

/** @file time_page.h
 * @brief The kernel time page: monotonic and wall time without a syscall.
 *
 * Haiku's real_time_data (src/system/kernel/real_time_clock.cpp:108, read by
 * src/system/libroot/os/time.cpp:29): one kernel page mapped read-only into
 * every process. The kernel writes the copy readers are not using and then
 * advances `sequence`; a reader takes the copy `sequence` names and retries
 * if `sequence` moved while it read. The values are as of the last timer tick
 * or context switch, so they trail the clock registers by at most one quantum
 * (5 ms). Callers that need the register's 80 ns resolution keep using the
 * clock syscalls.
 */

#include <stdint.h>

#include <astra/address_space.h>

#define ASTRA_TIME_PAGE_ADDRESS ASTRA_TIME_PAGE_ADDRESS_START
#define ASTRA_TIME_PAGE_MAGIC   0x54494D45u /* "TIME" */
#define ASTRA_TIME_PAGE_VERSION 1u
/** Set in `flags` when the wall-clock fields are valid (RTC_STATUS valid). */
#define ASTRA_TIME_PAGE_WALL_VALID 0x1u

typedef struct AstraTimePageCopy {
    uint32_t monotonic_high;   /* ns since boot */
    uint32_t monotonic_low;
    uint32_t wall_high;        /* ns since 1970-01-01T00:00:00Z */
    uint32_t wall_low;
    int32_t  utc_offset;       /* seconds east of UTC */
    uint32_t zone;             /* packed 4-character zone name */
    uint32_t flags;            /* ASTRA_TIME_PAGE_WALL_VALID */
    uint32_t reserved;
} AstraTimePageCopy;

typedef struct AstraTimePage {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;         /* copy[sequence & 1] is current */
    uint32_t reserved;
    AstraTimePageCopy copy[2];
} AstraTimePage;

_Static_assert(sizeof(AstraTimePageCopy) == 32u, "time page copy ABI");
_Static_assert(sizeof(AstraTimePage) == 80u, "time page ABI");

typedef struct AstraTimeSample {
    uint64_t monotonic_ns;
    uint64_t wall_ns;
    int32_t  utc_offset;
    uint32_t zone;
    uint32_t wall_valid;
} AstraTimeSample;

/**
 * Read one consistent copy. Returns 1, or 0 when the page is absent or not
 * this ABI (the caller falls back to the clock syscalls).
 */
static inline int
astra_time_page_read(const volatile AstraTimePage *page,
                     AstraTimeSample *sample)
{
    uint32_t before;
    uint32_t after;
    AstraTimePageCopy copy;

    if (page == 0 || sample == 0 || page->magic != ASTRA_TIME_PAGE_MAGIC ||
        page->version != ASTRA_TIME_PAGE_VERSION)
        return 0;
    do {
        const volatile AstraTimePageCopy *current;

        before = __atomic_load_n(&page->sequence, __ATOMIC_ACQUIRE);
        current = &page->copy[before & 1u];
        copy.monotonic_high = current->monotonic_high;
        copy.monotonic_low = current->monotonic_low;
        copy.wall_high = current->wall_high;
        copy.wall_low = current->wall_low;
        copy.utc_offset = current->utc_offset;
        copy.zone = current->zone;
        copy.flags = current->flags;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        after = __atomic_load_n(&page->sequence, __ATOMIC_RELAXED);
    } while (before != after);
    sample->monotonic_ns = ((uint64_t)copy.monotonic_high << 32) |
                           copy.monotonic_low;
    sample->wall_ns = ((uint64_t)copy.wall_high << 32) | copy.wall_low;
    sample->utc_offset = copy.utc_offset;
    sample->zone = copy.zone;
    sample->wall_valid = (copy.flags & ASTRA_TIME_PAGE_WALL_VALID) != 0u;
    return 1;
}

/**
 * The writer half, shared by the kernel and the host test so both exercise
 * one implementation. Writes the copy readers are not using, then publishes.
 */
static inline void
astra_time_page_publish(volatile AstraTimePage *page,
                        const AstraTimePageCopy *value)
{
    uint32_t next = page->sequence + 1u;
    volatile AstraTimePageCopy *target = &page->copy[next & 1u];

    target->monotonic_high = value->monotonic_high;
    target->monotonic_low = value->monotonic_low;
    target->wall_high = value->wall_high;
    target->wall_low = value->wall_low;
    target->utc_offset = value->utc_offset;
    target->zone = value->zone;
    target->flags = value->flags;
    target->reserved = 0u;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&page->sequence, next, __ATOMIC_RELEASE);
}

#endif
