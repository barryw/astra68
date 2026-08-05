#ifndef ASTRA_ALLOC_H
#define ASTRA_ALLOC_H

#include <stddef.h>
#include <stdint.h>

#include <astra/metrics.h>

/*
 * Bounded userspace allocator.
 *
 * Astra services must not depend on an unbounded heap. This allocator owns a
 * caller-supplied arena divided into fixed-size classes with a hard block count
 * each, so every service publishes an exact memory budget and exhaustion is a
 * reportable status rather than a growth event.
 *
 * The arena is supplied at initialisation and its origin is deliberately not
 * this module's concern: static storage today, a mapped region once virtual
 * memory growth exists. The contract does not change when that happens.
 *
 * A request is served from the smallest class that can hold it. A full class
 * fails; it does not spill into a larger class. Spilling would make a service's
 * budget unpredictable and hide the exhaustion the budget exists to expose.
 */

#define ASTRA_ALLOC_CLASS_MAX 8u

/* Widest scalar an allocation may hold; fixes slot alignment per target. */
typedef union AstraAllocScalar {
    void *pointer;
    uint64_t wide;
    double real;
} AstraAllocScalar;

#define ASTRA_ALLOC_ALIGNMENT (_Alignof(AstraAllocScalar))

typedef enum AstraAllocStatus {
    ASTRA_ALLOC_OK = 0,
    ASTRA_ALLOC_INVALID_ARGUMENT = 1,
    ASTRA_ALLOC_ARENA_TOO_SMALL = 2,
    ASTRA_ALLOC_CLASS_INVALID = 3,
    ASTRA_ALLOC_EXHAUSTED = 4,
    ASTRA_ALLOC_TOO_LARGE = 5,
    ASTRA_ALLOC_INJECTED = 6,
    ASTRA_ALLOC_FOREIGN_POINTER = 7,
    ASTRA_ALLOC_MISALIGNED_POINTER = 8,
    ASTRA_ALLOC_DOUBLE_FREE = 9
} AstraAllocStatus;

typedef struct AstraAllocClass {
    uint32_t size;  /* usable bytes per block, rounded up to the alignment */
    uint32_t count; /* hard block count for this class */
} AstraAllocClass;

typedef struct AstraAllocClassMetrics {
    uint32_t allocations;
    uint32_t frees;
    uint32_t failures;
    uint32_t live;
    uint32_t peak_live;
} AstraAllocClassMetrics;

typedef struct AstraAllocMetrics {
    uint32_t allocations;
    uint32_t frees;
    uint32_t failures;   /* exhausted, too large, or injected */
    uint32_t rejections; /* free of a foreign, misaligned, or freed pointer */
    uint32_t injected;
    uint32_t live_blocks;
    uint32_t peak_live_blocks;
    /*
     * Charged bytes are whole slots. A block does not record the size that
     * was asked for, and the arena has to cover whole slots regardless, so
     * slot bytes are the only budget figure worth reporting.
     */
    size_t charged_bytes;
    size_t peak_charged_bytes;
    AstraAllocClassMetrics per_class[ASTRA_ALLOC_CLASS_MAX];
} AstraAllocMetrics;

typedef struct AstraAllocPool {
    uint8_t *base;
    uint32_t *bitmap;
    uint32_t stride;
    uint32_t count;
    uint32_t usable;
    uint32_t free_head;
    uint32_t free_count;
    uint32_t injection_nth;
} AstraAllocPool;

typedef struct AstraAllocator {
    AstraAllocPool pool[ASTRA_ALLOC_CLASS_MAX];
    uint32_t pool_count;
    uint32_t injection_nth;
    AstraAllocMetrics metrics;
    AstraAllocStatus last_status;
} AstraAllocator;

/*
 * Arena bytes required for a class table, including bitmap and alignment
 * padding. Returns 0 for an invalid table.
 */
size_t astra_alloc_arena_bytes(const AstraAllocClass *classes,
                               uint32_t class_count);

/*
 * Classes must be non-empty, ascending by size, and fit the arena. The arena
 * must be aligned to ASTRA_ALLOC_ALIGNMENT and outlive the allocator.
 */
AstraAllocStatus astra_alloc_init(AstraAllocator *allocator,
                                  const AstraAllocClass *classes,
                                  uint32_t class_count, void *arena,
                                  size_t arena_bytes);

void *astra_alloc(AstraAllocator *allocator, size_t bytes);
AstraAllocStatus astra_alloc_free(AstraAllocator *allocator, void *pointer);

const AstraAllocMetrics *astra_alloc_metrics(const AstraAllocator *allocator);
AstraAllocStatus astra_alloc_last_status(const AstraAllocator *allocator);

/*
 * Fail the nth subsequent allocation, counted across every class
 * (astra_alloc_inject) or within one class (astra_alloc_inject_class).
 * An nth of 0 disables the selector. Both selectors may be armed at once.
 */
void astra_alloc_inject(AstraAllocator *allocator, uint32_t nth);
AstraAllocStatus astra_alloc_inject_class(AstraAllocator *allocator,
                                          uint32_t class_index, uint32_t nth);

/*
 * Free lists, occupancy bitmaps, and live counters agree, and every free-list
 * entry is an unallocated slot of its own class.
 */
int astra_alloc_valid(const AstraAllocator *allocator);

/* Publishes the allocator through <astra/metrics.h>; context is the allocator. */
uint32_t astra_alloc_sampler(void *context, AstraMetricSample *out,
                             uint32_t capacity);

#endif
