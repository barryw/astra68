#ifndef ASTRA_KERNEL_OBJECT_CACHE_H
#define ASTRA_KERNEL_OBJECT_CACHE_H

#include "allocation.h"

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_OBJECT_CACHE_BITMAP_WORDS(capacity) (((capacity) + 31u) / 32u)

typedef enum KernelObjectCacheStatus {
    KERNEL_OBJECT_CACHE_OK = 0,
    KERNEL_OBJECT_CACHE_INVALID_ARGUMENT,
    KERNEL_OBJECT_CACHE_UNAVAILABLE,
    KERNEL_OBJECT_CACHE_CORRUPT
} KernelObjectCacheStatus;

typedef struct KernelObjectCache {
    uint8_t *storage;
    uint32_t *bitmap;
    uint32_t object_size;
    uint16_t capacity;
    uint16_t bitmap_words;
    uint16_t next_hint;
    uint16_t live;
    uint16_t high_water;
    uint8_t site;
    uint8_t initialized;
    uint8_t corrupt;
    uint8_t reserved;
} KernelObjectCache;

typedef struct KernelObjectCacheStats {
    uint32_t object_size;
    uint16_t capacity;
    uint16_t live;
    uint16_t high_water;
    uint16_t next_hint;
    uint8_t site;
    uint8_t healthy;
} KernelObjectCacheStats;

bool kernel_object_cache_init(KernelObjectCache *cache, void *storage,
                              uint32_t object_size, uint16_t capacity,
                              uint32_t *bitmap, uint16_t bitmap_words,
                              KernelAllocationSite site);
KernelObjectCacheStatus kernel_object_cache_claim(KernelObjectCache *cache,
                                                  uint32_t owner,
                                                  void **object,
                                                  uint16_t *slot);
KernelObjectCacheStatus kernel_object_cache_release(KernelObjectCache *cache,
                                                    void *object);
bool kernel_object_cache_contains(const KernelObjectCache *cache,
                                  const void *object);
bool kernel_object_cache_is_claimed(const KernelObjectCache *cache,
                                    const void *object);
bool kernel_object_cache_slot_claimed(const KernelObjectCache *cache,
                                      uint16_t slot);
bool kernel_object_cache_stats(const KernelObjectCache *cache,
                               KernelObjectCacheStats *stats);
bool kernel_object_cache_valid(const KernelObjectCache *cache);

#endif
