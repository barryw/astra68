#include <astra/alloc.h>

#define ASTRA_ALLOC_END 0xFFFFFFFFu

/*
 * The allocator sits below any C library, so it clears storage itself rather
 * than depending on <string.h> being present in a freestanding target build.
 */
static void
fill_zero(void *destination, size_t count)
{
    unsigned char *bytes = destination;

    while (count-- != 0u) {
        *bytes++ = 0u;
    }
}

static size_t
align_up(size_t value, size_t alignment)
{
    return (value + (alignment - 1u)) & ~(alignment - 1u);
}

static uint32_t
class_stride(uint32_t size)
{
    size_t stride = align_up(size, ASTRA_ALLOC_ALIGNMENT);

    /* A free slot carries the next free index in its first bytes. */
    if (stride < sizeof(uint32_t)) {
        stride = sizeof(uint32_t);
    }
    return (uint32_t)stride;
}

static uint32_t
bitmap_words(uint32_t count)
{
    return (count + 31u) / 32u;
}

static int
classes_valid(const AstraAllocClass *classes, uint32_t class_count)
{
    uint32_t index;

    if (classes == NULL || class_count == 0u ||
        class_count > ASTRA_ALLOC_CLASS_MAX) {
        return 0;
    }
    for (index = 0u; index < class_count; ++index) {
        if (classes[index].size == 0u || classes[index].count == 0u) {
            return 0;
        }
        if (index != 0u && classes[index].size <= classes[index - 1u].size) {
            return 0;
        }
    }
    return 1;
}

/*
 * Single layout walk shared by the sizing query and initialisation so the two
 * can never disagree. Emits nothing when allocator is NULL.
 */
static size_t
layout(AstraAllocator *allocator, const AstraAllocClass *classes,
       uint32_t class_count, uint8_t *arena)
{
    size_t cursor = 0u;
    uint32_t index;

    for (index = 0u; index < class_count; ++index) {
        uint32_t count = classes[index].count;
        uint32_t stride = class_stride(classes[index].size);
        size_t bitmap_offset = align_up(cursor, _Alignof(uint32_t));
        size_t blocks_offset =
            align_up(bitmap_offset + (size_t)bitmap_words(count) * 4u,
                     ASTRA_ALLOC_ALIGNMENT);

        if (allocator != NULL) {
            AstraAllocPool *pool = &allocator->pool[index];

            pool->bitmap = (uint32_t *)(void *)(arena + bitmap_offset);
            pool->base = arena + blocks_offset;
            pool->stride = stride;
            pool->count = count;
            pool->usable = classes[index].size;
        }
        cursor = blocks_offset + (size_t)stride * count;
    }
    return cursor;
}

size_t
astra_alloc_arena_bytes(const AstraAllocClass *classes, uint32_t class_count)
{
    if (!classes_valid(classes, class_count)) {
        return 0u;
    }
    return layout(NULL, classes, class_count, NULL);
}

AstraAllocStatus
astra_alloc_init(AstraAllocator *allocator, const AstraAllocClass *classes,
                 uint32_t class_count, void *arena, size_t arena_bytes)
{
    size_t required;
    uint32_t index;

    if (allocator == NULL || arena == NULL) {
        return ASTRA_ALLOC_INVALID_ARGUMENT;
    }
    if (((uintptr_t)arena & (ASTRA_ALLOC_ALIGNMENT - 1u)) != 0u) {
        return ASTRA_ALLOC_INVALID_ARGUMENT;
    }
    if (!classes_valid(classes, class_count)) {
        return ASTRA_ALLOC_CLASS_INVALID;
    }

    required = layout(NULL, classes, class_count, NULL);
    if (required > arena_bytes) {
        return ASTRA_ALLOC_ARENA_TOO_SMALL;
    }

    fill_zero(allocator, sizeof(*allocator));
    fill_zero(arena, required);
    (void)layout(allocator, classes, class_count, arena);
    allocator->pool_count = class_count;

    for (index = 0u; index < class_count; ++index) {
        AstraAllocPool *pool = &allocator->pool[index];
        uint32_t slot = pool->count;

        pool->free_head = ASTRA_ALLOC_END;
        while (slot-- != 0u) {
            uint8_t *block = pool->base + (size_t)slot * pool->stride;

            *(uint32_t *)(void *)block = pool->free_head;
            pool->free_head = slot;
        }
        pool->free_count = pool->count;
    }
    return ASTRA_ALLOC_OK;
}

static void
bitmap_set(uint32_t *bitmap, uint32_t index)
{
    bitmap[index / 32u] |= 1u << (index % 32u);
}

static void
bitmap_clear(uint32_t *bitmap, uint32_t index)
{
    bitmap[index / 32u] &= ~(1u << (index % 32u));
}

static int
bitmap_test(const uint32_t *bitmap, uint32_t index)
{
    return (bitmap[index / 32u] & (1u << (index % 32u))) != 0u;
}

static AstraAllocPool *
pool_for_size(AstraAllocator *allocator, size_t bytes, uint32_t *class_index)
{
    uint32_t index;

    for (index = 0u; index < allocator->pool_count; ++index) {
        if ((size_t)allocator->pool[index].usable >= bytes) {
            *class_index = index;
            return &allocator->pool[index];
        }
    }
    return NULL;
}

static int
injection_fires(uint32_t *nth)
{
    if (*nth == 0u) {
        return 0;
    }
    --*nth;
    if (*nth != 0u) {
        return 0;
    }
    return 1;
}

static void
record_failure(AstraAllocator *allocator, AstraAllocClassMetrics *per_class,
               AstraAllocStatus status)
{
    ++allocator->metrics.failures;
    if (per_class != NULL) {
        ++per_class->failures;
    }
    if (status == ASTRA_ALLOC_INJECTED) {
        ++allocator->metrics.injected;
    }
    allocator->last_status = status;
}

void *
astra_alloc(AstraAllocator *allocator, size_t bytes)
{
    AstraAllocClassMetrics *per_class;
    AstraAllocPool *pool;
    uint32_t class_index = 0u;
    uint32_t slot;
    uint8_t *block;

    if (allocator == NULL) {
        return NULL;
    }
    if (allocator->pool_count == 0u) {
        allocator->last_status = ASTRA_ALLOC_INVALID_ARGUMENT;
        return NULL;
    }

    /* A zero-byte request still returns a distinct freeable block. */
    if (bytes == 0u) {
        bytes = 1u;
    }

    pool = pool_for_size(allocator, bytes, &class_index);
    if (pool == NULL) {
        record_failure(allocator, NULL, ASTRA_ALLOC_TOO_LARGE);
        return NULL;
    }
    per_class = &allocator->metrics.per_class[class_index];

    if (injection_fires(&allocator->injection_nth) ||
        injection_fires(&pool->injection_nth)) {
        record_failure(allocator, per_class, ASTRA_ALLOC_INJECTED);
        return NULL;
    }

    if (pool->free_head == ASTRA_ALLOC_END) {
        record_failure(allocator, per_class, ASTRA_ALLOC_EXHAUSTED);
        return NULL;
    }

    slot = pool->free_head;
    block = pool->base + (size_t)slot * pool->stride;
    pool->free_head = *(const uint32_t *)(const void *)block;
    --pool->free_count;
    bitmap_set(pool->bitmap, slot);

    ++allocator->metrics.allocations;
    ++allocator->metrics.live_blocks;
    allocator->metrics.charged_bytes += pool->stride;
    if (allocator->metrics.live_blocks > allocator->metrics.peak_live_blocks) {
        allocator->metrics.peak_live_blocks = allocator->metrics.live_blocks;
    }
    if (allocator->metrics.charged_bytes >
        allocator->metrics.peak_charged_bytes) {
        allocator->metrics.peak_charged_bytes =
            allocator->metrics.charged_bytes;
    }

    ++per_class->allocations;
    ++per_class->live;
    if (per_class->live > per_class->peak_live) {
        per_class->peak_live = per_class->live;
    }

    allocator->last_status = ASTRA_ALLOC_OK;
    return block;
}

AstraAllocStatus
astra_alloc_free(AstraAllocator *allocator, void *pointer)
{
    uint32_t index;

    if (allocator == NULL) {
        return ASTRA_ALLOC_INVALID_ARGUMENT;
    }
    if (pointer == NULL) {
        allocator->last_status = ASTRA_ALLOC_OK;
        return ASTRA_ALLOC_OK;
    }

    for (index = 0u; index < allocator->pool_count; ++index) {
        AstraAllocPool *pool = &allocator->pool[index];
        size_t span = (size_t)pool->stride * pool->count;
        uint8_t *block = pointer;
        size_t offset;
        uint32_t slot;

        if (block < pool->base || block >= pool->base + span) {
            continue;
        }
        offset = (size_t)(block - pool->base);
        if (offset % pool->stride != 0u) {
            ++allocator->metrics.rejections;
            allocator->last_status = ASTRA_ALLOC_MISALIGNED_POINTER;
            return ASTRA_ALLOC_MISALIGNED_POINTER;
        }
        slot = (uint32_t)(offset / pool->stride);
        if (!bitmap_test(pool->bitmap, slot)) {
            ++allocator->metrics.rejections;
            allocator->last_status = ASTRA_ALLOC_DOUBLE_FREE;
            return ASTRA_ALLOC_DOUBLE_FREE;
        }

        bitmap_clear(pool->bitmap, slot);
        *(uint32_t *)(void *)block = pool->free_head;
        pool->free_head = slot;
        ++pool->free_count;

        ++allocator->metrics.frees;
        --allocator->metrics.live_blocks;
        allocator->metrics.charged_bytes -= pool->stride;
        ++allocator->metrics.per_class[index].frees;
        --allocator->metrics.per_class[index].live;

        allocator->last_status = ASTRA_ALLOC_OK;
        return ASTRA_ALLOC_OK;
    }

    ++allocator->metrics.rejections;
    allocator->last_status = ASTRA_ALLOC_FOREIGN_POINTER;
    return ASTRA_ALLOC_FOREIGN_POINTER;
}

const AstraAllocMetrics *
astra_alloc_metrics(const AstraAllocator *allocator)
{
    return allocator != NULL ? &allocator->metrics : NULL;
}

AstraAllocStatus
astra_alloc_last_status(const AstraAllocator *allocator)
{
    return allocator != NULL ? allocator->last_status
                             : ASTRA_ALLOC_INVALID_ARGUMENT;
}

void
astra_alloc_inject(AstraAllocator *allocator, uint32_t nth)
{
    if (allocator != NULL) {
        allocator->injection_nth = nth;
    }
}

AstraAllocStatus
astra_alloc_inject_class(AstraAllocator *allocator, uint32_t class_index,
                         uint32_t nth)
{
    if (allocator == NULL || class_index >= allocator->pool_count) {
        return ASTRA_ALLOC_INVALID_ARGUMENT;
    }
    allocator->pool[class_index].injection_nth = nth;
    return ASTRA_ALLOC_OK;
}

int
astra_alloc_valid(const AstraAllocator *allocator)
{
    uint32_t index;
    uint32_t live_total = 0u;

    if (allocator == NULL || allocator->pool_count == 0u ||
        allocator->pool_count > ASTRA_ALLOC_CLASS_MAX) {
        return 0;
    }

    for (index = 0u; index < allocator->pool_count; ++index) {
        const AstraAllocPool *pool = &allocator->pool[index];
        const AstraAllocClassMetrics *per_class =
            &allocator->metrics.per_class[index];
        uint32_t walked = 0u;
        uint32_t cursor = pool->free_head;
        uint32_t occupied = 0u;
        uint32_t slot;

        for (slot = 0u; slot < pool->count; ++slot) {
            if (bitmap_test(pool->bitmap, slot)) {
                ++occupied;
            }
        }
        if (occupied != per_class->live) {
            return 0;
        }
        if (occupied + pool->free_count != pool->count) {
            return 0;
        }

        while (cursor != ASTRA_ALLOC_END) {
            const uint8_t *block;

            if (cursor >= pool->count) {
                return 0;
            }
            if (bitmap_test(pool->bitmap, cursor)) {
                return 0;
            }
            if (++walked > pool->count) {
                return 0;
            }
            block = pool->base + (size_t)cursor * pool->stride;
            cursor = *(const uint32_t *)(const void *)block;
        }
        if (walked != pool->free_count) {
            return 0;
        }

        live_total += per_class->live;
    }

    return live_total == allocator->metrics.live_blocks;
}

/*
 * Per-class rows are named by index rather than by size: a service's class
 * table is part of its published budget, and the sizes are already reported by
 * whoever configured it.
 */
static const char *const class_live_names[ASTRA_ALLOC_CLASS_MAX] = {
    "class0.live", "class1.live", "class2.live", "class3.live",
    "class4.live", "class5.live", "class6.live", "class7.live",
};

static const char *const class_peak_names[ASTRA_ALLOC_CLASS_MAX] = {
    "class0.peak_live", "class1.peak_live", "class2.peak_live",
    "class3.peak_live", "class4.peak_live", "class5.peak_live",
    "class6.peak_live", "class7.peak_live",
};

static const char *const class_failure_names[ASTRA_ALLOC_CLASS_MAX] = {
    "class0.failures", "class1.failures", "class2.failures",
    "class3.failures", "class4.failures", "class5.failures",
    "class6.failures", "class7.failures",
};

uint32_t
astra_alloc_sampler(void *context, AstraMetricSample *out, uint32_t capacity)
{
    const AstraAllocator *allocator = context;
    const AstraAllocMetrics *metrics;
    uint32_t written = 0u;
    uint32_t index;

    if (allocator == NULL || out == NULL || capacity < 10u) {
        return 0u;
    }
    metrics = &allocator->metrics;

    out[written].name = "allocations";
    out[written++].value = metrics->allocations;
    out[written].name = "frees";
    out[written++].value = metrics->frees;
    out[written].name = "failures";
    out[written++].value = metrics->failures;
    out[written].name = "rejections";
    out[written++].value = metrics->rejections;
    out[written].name = "injected";
    out[written++].value = metrics->injected;
    out[written].name = "live_blocks";
    out[written++].value = metrics->live_blocks;
    out[written].name = "peak_live_blocks";
    out[written++].value = metrics->peak_live_blocks;
    out[written].name = "charged_bytes";
    out[written++].value = metrics->charged_bytes;
    out[written].name = "peak_charged_bytes";
    out[written++].value = metrics->peak_charged_bytes;
    out[written].name = "classes";
    out[written++].value = allocator->pool_count;

    for (index = 0u; index < allocator->pool_count; ++index) {
        if (capacity - written < 3u) {
            return written;
        }
        out[written].name = class_live_names[index];
        out[written++].value = metrics->per_class[index].live;
        out[written].name = class_peak_names[index];
        out[written++].value = metrics->per_class[index].peak_live;
        out[written].name = class_failure_names[index];
        out[written++].value = metrics->per_class[index].failures;
    }
    return written;
}
