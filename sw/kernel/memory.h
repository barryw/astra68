#ifndef ASTRA_KERNEL_MEMORY_H
#define ASTRA_KERNEL_MEMORY_H

#include <astra/boot.h>

#include "allocation.h"

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_PAGE_SHIFT 12u
#define KERNEL_PAGE_SIZE  (1u << KERNEL_PAGE_SHIFT)
#define KERNEL_MAX_FRAMES (ASTRA_RAM_SIZE_ARTY_GUEST / KERNEL_PAGE_SIZE)
#define KERNEL_MAX_FRAME_OWNERS 64u
#define KERNEL_OWNER_NONE 0u
#define KERNEL_EMERGENCY_RESERVE_FRAMES 32u

typedef enum KernelFrameState {
    KERNEL_FRAME_UNCLASSIFIED = 0,
    KERNEL_FRAME_FREE,
    KERNEL_FRAME_FIRMWARE,
    KERNEL_FRAME_EARLY_LOG,
    KERNEL_FRAME_KERNEL,
    KERNEL_FRAME_ROM_BACKING,
    KERNEL_FRAME_PAGE_TABLE,
    KERNEL_FRAME_PROCESS,
    KERNEL_FRAME_SHARED,
    KERNEL_FRAME_DMA,
    KERNEL_FRAME_DEVICE,
    KERNEL_FRAME_EMERGENCY_RESERVED
} KernelFrameState;

typedef enum KernelMemoryStatus {
    KERNEL_MEMORY_OK = 0,
    KERNEL_MEMORY_INVALID_ARGUMENT,
    KERNEL_MEMORY_INVALID_MAP,
    KERNEL_MEMORY_OUT_OF_MEMORY,
    KERNEL_MEMORY_NOT_OWNED,
    KERNEL_MEMORY_BUSY,
    KERNEL_MEMORY_OVERFLOW
} KernelMemoryStatus;

typedef struct KernelFrameInfo {
    uint32_t owner;
    uint16_t references;
    uint8_t pins;
    uint8_t state;
} KernelFrameInfo;

typedef struct KernelMemoryStats {
    uint32_t ram_base;
    uint32_t total_frames;
    uint32_t free_frames;
    uint32_t high_water_frames;
    uint32_t allocation_failures;
    uint32_t owner_slots_used;
    uint32_t owner_release_operations;
    uint32_t owner_release_frame_visits;
    uint32_t emergency_total_frames;
    uint32_t emergency_available_frames;
    uint32_t emergency_acquisitions;
    uint32_t emergency_failures;
} KernelMemoryStats;

KernelMemoryStatus kernel_memory_init(const AstraBootInfo *info);
KernelMemoryStatus kernel_memory_alloc(uint32_t frame_count,
                                       uint32_t alignment_frames,
                                       KernelFrameState state,
                                       uint32_t owner,
                                       uint32_t *physical_base);
KernelMemoryStatus kernel_memory_alloc_tagged(
    KernelAllocationSite site, uint32_t frame_count,
    uint32_t alignment_frames, KernelFrameState state, uint32_t owner,
    uint32_t *physical_base);
KernelMemoryStatus kernel_memory_alloc_zeroed(uint32_t frame_count,
                                              uint32_t alignment_frames,
                                              KernelFrameState state,
                                              uint32_t owner,
                                              uint32_t *physical_base);
KernelMemoryStatus kernel_memory_alloc_zeroed_tagged(
    KernelAllocationSite site, uint32_t frame_count,
    uint32_t alignment_frames, KernelFrameState state, uint32_t owner,
    uint32_t *physical_base);
KernelMemoryStatus kernel_memory_alloc_pages_zeroed(
    uint32_t frame_count, KernelFrameState state, uint32_t owner,
    uint32_t *physical_pages);
KernelMemoryStatus kernel_memory_alloc_pages_zeroed_tagged(
    KernelAllocationSite site, uint32_t frame_count, KernelFrameState state,
    uint32_t owner, uint32_t *physical_pages);
KernelMemoryStatus kernel_memory_emergency_acquire(
    KernelAllocationSite site, KernelFrameState state, uint32_t owner,
    uint32_t *physical_base);
KernelMemoryStatus kernel_memory_retain(uint32_t physical_base,
                                        uint32_t frame_count,
                                        uint32_t owner);
KernelMemoryStatus kernel_memory_release(uint32_t physical_base,
                                         uint32_t frame_count,
                                         uint32_t owner);
KernelMemoryStatus kernel_memory_pin(uint32_t physical_base,
                                     uint32_t frame_count,
                                     uint32_t owner);
KernelMemoryStatus kernel_memory_unpin(uint32_t physical_base,
                                       uint32_t frame_count,
                                       uint32_t owner);
KernelMemoryStatus kernel_memory_release_owner(uint32_t owner,
                                               uint32_t *released_frames);
bool kernel_memory_range_owned(uint32_t physical_base, uint32_t byte_count,
                               uint32_t owner, KernelFrameState state,
                               bool require_pinned);
bool kernel_memory_frame_info(uint32_t physical_address,
                              KernelFrameInfo *info);
bool kernel_memory_frame_allocation_site(uint32_t physical_address,
                                         KernelAllocationSite *site);
bool kernel_memory_owner_frames(uint32_t owner, uint32_t *frame_count);
bool kernel_memory_stats(KernelMemoryStats *stats);

#if defined(KERNEL_MEMORY_HOST_TEST)
void kernel_memory_test_bind_physical_memory(uint8_t *memory, uint32_t base,
                                             uint32_t size);
#endif

#endif
