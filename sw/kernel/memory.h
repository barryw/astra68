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
/*
 * Frames set aside at boot that only DMA may take.
 *
 * With an MMU, physical fragmentation almost never matters: any frame will do
 * for an ordinary allocation because the page tables supply the contiguity.
 * It bites in exactly one place, and `test_memory.c` demonstrates it rather
 * than assuming it -- comb the frame map by freeing every other frame and half
 * the machine is free while the largest run in it is one frame, at which point
 * a two-frame block transfer is refused and a framebuffer certainly is.
 *
 * A buddy allocator does not fix that. It makes finding a run a list pop
 * instead of a scan, and coalesces on free, but it cannot manufacture
 * contiguity while the alternating frames are still held -- it would fail the
 * same test. It also has one customer: every other allocation in this kernel
 * asks for a single frame and does not care where it lands.
 *
 * So the answer is the cheap and predictable one. 128 frames is 512 KiB, which
 * is 0.4% of the machine, and it is the measured demand rather than a round
 * number: 64 for the display's framebuffer -- ASTRA_RENDER_BUILDER_BYTES, the
 * only large contiguous request on the machine -- and two for each of the
 * KERNEL_DMA_MAX_BUFFERS block transfer slots. DMA takes from here first and
 * falls back to the general pool, so nothing that works today stops working;
 * what changes is that a display service restarting at hour six finds its
 * framebuffer where it left it.
 */
#define KERNEL_DMA_ZONE_FRAMES 128u

/*
 * The top of what the kernel can touch directly.
 *
 * The supervisor identity-maps SDRAM from 0x02000000 through root index 15,
 * which is 32 MiB, and nothing above that has a supervisor translation. A
 * frame beyond it can still be handed to a process -- the process reaches it
 * through its own page tables -- but the kernel cannot zero it, poison it or
 * copy through it, and trying is a PMMU translation fault in supervisor mode,
 * which is a panic.
 *
 * The general allocator never meets this, because it searches upward from the
 * bottom and a 128 MB machine never gets that far. The DMA zone did meet it
 * immediately, by deliberately taking the highest frames it could find, and
 * booted straight into `Class: PMMU translation, Fault: 0x09F60000` -- which
 * is exactly the top of RAM less the emergency reserve less this zone.
 *
 * So the zone is placed below this, and the limit is written down here rather
 * than left as a fact about vm.c that the next person rediscovers the same
 * way. That the machine believes it has 128 MB while the kernel can directly
 * reach 32 MB of it is a real limit and not this file's to fix.
 */
#define KERNEL_DIRECT_MAP_LIMIT 0x04000000u

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
    uint32_t dma_zone_first;
    uint32_t dma_zone_frames;
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
