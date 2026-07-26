#include "object_cache.h"

#include <stddef.h>

static bool bit_test(const KernelObjectCache *cache, uint16_t slot)
{
    return (cache->bitmap[slot >> 5] &
            (1u << (slot & 31u))) != 0u;
}

static void bit_set(KernelObjectCache *cache, uint16_t slot, bool value)
{
    uint32_t mask = 1u << (slot & 31u);

    if (value)
        cache->bitmap[slot >> 5] |= mask;
    else
        cache->bitmap[slot >> 5] &= ~mask;
}

static bool cache_shape_valid(const KernelObjectCache *cache)
{
    return cache != NULL && cache->initialized != 0u &&
           cache->storage != NULL && cache->bitmap != NULL &&
           cache->object_size != 0u && cache->capacity != 0u &&
           cache->object_size <= UINTPTR_MAX / cache->capacity &&
           cache->bitmap_words ==
               KERNEL_OBJECT_CACHE_BITMAP_WORDS(cache->capacity) &&
           cache->next_hint < cache->capacity &&
           cache->site > KERNEL_ALLOCATION_SITE_INVALID &&
           cache->site < KERNEL_ALLOCATION_SITE_COUNT;
}

static bool object_slot(const KernelObjectCache *cache, const void *object,
                        uint16_t *slot)
{
    uintptr_t address;
    uintptr_t first;
    uintptr_t bytes;
    uintptr_t offset;

    if (!cache_shape_valid(cache) || object == NULL || slot == NULL)
        return false;
    address = (uintptr_t)object;
    first = (uintptr_t)cache->storage;
    bytes = (uintptr_t)cache->object_size * cache->capacity;
    if (address < first || address - first >= bytes)
        return false;
    offset = address - first;
    if (offset % cache->object_size != 0u)
        return false;
    *slot = (uint16_t)(offset / cache->object_size);
    return true;
}

bool kernel_object_cache_init(KernelObjectCache *cache, void *storage,
                              uint32_t object_size, uint16_t capacity,
                              uint32_t *bitmap, uint16_t bitmap_words,
                              KernelAllocationSite site)
{
    uint32_t expected_words = KERNEL_OBJECT_CACHE_BITMAP_WORDS(capacity);

    if (cache == NULL || storage == NULL || object_size == 0u ||
        capacity == 0u || bitmap == NULL || bitmap_words == 0u ||
        object_size > UINTPTR_MAX / capacity ||
        bitmap_words != expected_words ||
        site <= KERNEL_ALLOCATION_SITE_INVALID ||
        site >= KERNEL_ALLOCATION_SITE_COUNT)
        return false;
    if (!kernel_allocation_initialized())
        kernel_allocation_init();
    if (cache->initialized != 0u && cache->live != 0u) {
        uint32_t bytes = (uint32_t)cache->live * cache->object_size;
        KernelAllocationStats stats;

        if (!kernel_allocation_site_stats(
                (KernelAllocationSite)cache->site, &stats))
            return false;
        if (stats.current_units == cache->live &&
            stats.current_bytes == bytes) {
            if (!kernel_allocation_release(
                    (KernelAllocationSite)cache->site, cache->live, bytes))
                return false;
        } else if (stats.current_units != 0u || stats.current_bytes != 0u) {
            return false;
        }
    }
    cache->storage = storage;
    cache->bitmap = bitmap;
    cache->object_size = object_size;
    cache->capacity = capacity;
    cache->bitmap_words = bitmap_words;
    cache->next_hint = 0u;
    cache->live = 0u;
    cache->high_water = 0u;
    cache->site = (uint8_t)site;
    cache->initialized = 1u;
    cache->corrupt = 0u;
    cache->reserved = 0u;
    for (uint16_t word = 0u; word < bitmap_words; ++word)
        bitmap[word] = 0u;
    return true;
}

KernelObjectCacheStatus kernel_object_cache_claim(KernelObjectCache *cache,
                                                  uint32_t owner,
                                                  void **object,
                                                  uint16_t *slot)
{
    uint16_t selected = UINT16_MAX;

    if (!cache_shape_valid(cache) || object == NULL || slot == NULL)
        return KERNEL_OBJECT_CACHE_INVALID_ARGUMENT;
    *object = NULL;
    *slot = UINT16_MAX;
    if (cache->corrupt != 0u)
        return KERNEL_OBJECT_CACHE_CORRUPT;
    if (!kernel_allocation_attempt((KernelAllocationSite)cache->site,
                                   owner))
        return KERNEL_OBJECT_CACHE_UNAVAILABLE;
    for (uint16_t offset = 0u; offset < cache->capacity; ++offset) {
        uint32_t candidate = (uint32_t)cache->next_hint + offset;

        if (candidate >= cache->capacity)
            candidate -= cache->capacity;
        if (!bit_test(cache, (uint16_t)candidate)) {
            selected = (uint16_t)candidate;
            break;
        }
    }
    if (selected == UINT16_MAX) {
        kernel_allocation_fail((KernelAllocationSite)cache->site, owner);
        return KERNEL_OBJECT_CACHE_UNAVAILABLE;
    }
    bit_set(cache, selected, true);
    ++cache->live;
    if (cache->live > cache->high_water)
        cache->high_water = cache->live;
    cache->next_hint = (uint16_t)(selected + 1u);
    if (cache->next_hint == cache->capacity)
        cache->next_hint = 0u;
    if (!kernel_allocation_commit((KernelAllocationSite)cache->site, 1u,
                                  cache->object_size, owner)) {
        bit_set(cache, selected, false);
        --cache->live;
        cache->corrupt = 1u;
        return KERNEL_OBJECT_CACHE_CORRUPT;
    }
    *object = cache->storage + (uint32_t)selected * cache->object_size;
    *slot = selected;
    return KERNEL_OBJECT_CACHE_OK;
}

KernelObjectCacheStatus kernel_object_cache_release(KernelObjectCache *cache,
                                                    void *object)
{
    uint16_t slot;

    if (!object_slot(cache, object, &slot))
        return KERNEL_OBJECT_CACHE_INVALID_ARGUMENT;
    if (cache->corrupt != 0u || cache->live == 0u || !bit_test(cache, slot))
        return KERNEL_OBJECT_CACHE_CORRUPT;
    if (!kernel_allocation_release((KernelAllocationSite)cache->site, 1u,
                                   cache->object_size)) {
        cache->corrupt = 1u;
        return KERNEL_OBJECT_CACHE_CORRUPT;
    }
    bit_set(cache, slot, false);
    --cache->live;
    if (slot < cache->next_hint)
        cache->next_hint = slot;
    return KERNEL_OBJECT_CACHE_OK;
}

bool kernel_object_cache_contains(const KernelObjectCache *cache,
                                  const void *object)
{
    uint16_t slot;

    return object_slot(cache, object, &slot);
}

bool kernel_object_cache_is_claimed(const KernelObjectCache *cache,
                                    const void *object)
{
    uint16_t slot;

    return object_slot(cache, object, &slot) && bit_test(cache, slot);
}

bool kernel_object_cache_slot_claimed(const KernelObjectCache *cache,
                                      uint16_t slot)
{
    return cache_shape_valid(cache) && slot < cache->capacity &&
           bit_test(cache, slot);
}

bool kernel_object_cache_stats(const KernelObjectCache *cache,
                               KernelObjectCacheStats *stats)
{
    if (!cache_shape_valid(cache) || stats == NULL)
        return false;
    stats->object_size = cache->object_size;
    stats->capacity = cache->capacity;
    stats->live = cache->live;
    stats->high_water = cache->high_water;
    stats->next_hint = cache->next_hint;
    stats->site = cache->site;
    stats->healthy = cache->corrupt == 0u ? 1u : 0u;
    return true;
}

bool kernel_object_cache_valid(const KernelObjectCache *cache)
{
    uint16_t live = 0u;
    KernelAllocationStats stats;

    if (!cache_shape_valid(cache) || cache->corrupt != 0u ||
        cache->live > cache->capacity || cache->high_water < cache->live ||
        cache->high_water > cache->capacity)
        return false;
    for (uint16_t slot = 0u; slot < cache->capacity; ++slot) {
        if (bit_test(cache, slot))
            ++live;
    }
    if (live != cache->live ||
        !kernel_allocation_site_stats((KernelAllocationSite)cache->site,
                                      &stats) ||
        stats.current_units != cache->live ||
        stats.current_bytes != (uint32_t)cache->live * cache->object_size)
        return false;
    return true;
}
