#ifndef ASTRA_KERNEL_AREA_H
#define ASTRA_KERNEL_AREA_H

#include "handle.h"
#include "memory.h"
#include "vm.h"

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_AREA_MAX KERNEL_VM_AREA_SLOT_COUNT
/*
 * Four was one short of what a terminal in a window needs: its own surface,
 * and a transfer area for each mount it reads through. It ran out between
 * WORK: and EVENTS:, and what a person saw was `cat: limit reached` on a file
 * that was there. Kept at a quarter of the pool, so the quota still means
 * that no one process can spend everybody else's share.
 */
#define KERNEL_AREA_OWNER_MAX 8u
#define KERNEL_AREA_PAGE_MAX \
    (KERNEL_VM_AREA_SLOT_SIZE / KERNEL_PAGE_SIZE)
/*
 * What one owner may have *committed* across all of its areas.
 *
 * This was one slot's worth -- 512 pages -- from when creating an area
 * committed it, so reserving and spending were the same act and one number
 * could bound both. They are different acts now, and the number that bounds
 * reservations is KERNEL_AREA_OWNER_MAX: eight slots of address space, which
 * is free. So this bounds only frames, and it is the owner's whole allowance
 * rather than one slot of it, because a program with a large heap should not
 * thereby lose its ability to hold a surface.
 *
 * Sized against a measurement rather than a feeling. `heapbench` runs
 * editor-shaped churn -- many small objects, mixed lifetimes, occasional
 * large buffers -- and its peak footprint is 169 pages, 688 KiB, for 403 KiB
 * live. At the old 512 that left 343 pages for everything else a program
 * holds, and a 640x480 surface is 75 of them; two windows and a couple of
 * transfer areas and an editor is refused a buffer it should have had. At
 * 4096 the owner's frames are bounded by the address space it was already
 * allowed to name, and what actually refuses is
 * KERNEL_AREA_SYSTEM_PAGE_MAX -- 16384 pages, half the machine -- and then
 * the free frame count, which are the honest limits.
 */
#define KERNEL_AREA_OWNER_PAGE_MAX \
    (KERNEL_AREA_OWNER_MAX * KERNEL_AREA_PAGE_MAX)
#define KERNEL_AREA_SYSTEM_PAGE_MAX (KERNEL_AREA_MAX * KERNEL_AREA_PAGE_MAX)
/*
 * Every area slot, aliased into every process. Bounded by the process count
 * rather than by the frame ledger's alias ceiling: the ledger says what one
 * frame can bear, this says how many mappings can exist at once.
 */
#define KERNEL_AREA_MAPPING_MAX \
    (KERNEL_AREA_MAX * KERNEL_VM_ADDRESS_SPACE_MAX)
/* One address space can use every area slot; the VM layout is the quota. */
#define KERNEL_AREA_PROCESS_MAPPING_MAX KERNEL_AREA_MAX

#define KERNEL_AREA_RIGHTS \
    ((1u << 0) | (1u << 1) | (1u << 2) | (1u << 5) | (1u << 6))

/*
 * Naming memory and owning it are different operations. An ordinary area
 * commits every frame at creation, which is right for something whose whole
 * extent is about to be written -- a surface, a ring. A reserved area takes
 * the address range and commits nothing; its pages arrive when they are
 * touched. A 2 MiB window that half of a program ever reads then costs half
 * of 2 MiB, and the address space it did not use costs nothing at all,
 * because there is 2 GB of that and 128 MB of the other.
 */
#define KERNEL_AREA_CREATE_RESERVED (1u << 0)
#define KERNEL_AREA_CREATE_FLAGS KERNEL_AREA_CREATE_RESERVED

/*
 * How much a fault commits. One page would be correct and slow: on a 30 MHz
 * 68030 the frame push, the handler entry, the table walk and the ATC fill
 * around a fault cost far more than clearing the pages themselves, so the
 * fault is the expensive part and it should be amortised. Sixteen pages is
 * 64 KiB, which is the order of magnitude the argument gives rather than a
 * measured optimum -- it is a knob, and it wants a number from the allocator
 * trace in HANDOVER-memory-and-modernity.md §4.
 *
 * Clusters are aligned to their own size inside the area, so repeated faults
 * walking upward land on distinct clusters and a fault never re-does work.
 */
#define KERNEL_AREA_COMMIT_CLUSTER_PAGES 16u

typedef enum KernelAreaStatus {
    KERNEL_AREA_OK = 0,
    KERNEL_AREA_INVALID_ARGUMENT,
    KERNEL_AREA_INVALID_STATE,
    KERNEL_AREA_NO_SLOT,
    KERNEL_AREA_QUOTA_EXCEEDED,
    KERNEL_AREA_OUT_OF_MEMORY,
    KERNEL_AREA_ALREADY_MAPPED,
    KERNEL_AREA_NOT_MAPPED,
    KERNEL_AREA_ACCESS_DENIED,
    KERNEL_AREA_PEER_DEAD,
    KERNEL_AREA_CORRUPT
} KernelAreaStatus;

typedef struct KernelArea KernelArea;

typedef struct KernelAreaSnapshot {
    uint32_t creator;
    uint32_t frame_owner;
    uint32_t generation;
    uint32_t byte_size;
    uint32_t virtual_base;
    uint32_t terminal_result;
    uint16_t handle_references;
    uint16_t child_references;
    uint16_t mapping_references;
    uint16_t page_count;
    uint16_t committed_pages;
    uint8_t state;
    uint8_t frames_released;
    uint8_t reserved_form;
} KernelAreaSnapshot;

typedef struct KernelAreaPoolStats {
    uint32_t created_areas;
    uint32_t active_areas;
    uint32_t closing_areas;
    uint32_t max_active_areas;
    uint32_t committed_pages;
    uint32_t max_committed_pages;
    uint32_t active_mappings;
    uint32_t max_active_mappings;
    uint32_t allocation_failures;
    uint32_t quota_failures;
    uint32_t map_operations;
    uint32_t map_rollbacks;
    uint32_t unmap_operations;
    uint32_t revoked_mappings;
    uint32_t owner_deaths;
    uint32_t stale_operations;
    uint32_t commit_faults;
    uint32_t commit_pages;
    uint32_t commit_failures;
    uint32_t decommit_operations;
    uint32_t decommit_pages;
} KernelAreaPoolStats;

void kernel_area_pool_init(void);
KernelAreaStatus kernel_area_create(uint32_t creator, uint32_t byte_size,
                                    uint32_t flags, KernelArea **area);
/*
 * Answers a fault inside the area window: commits the cluster containing the
 * address and publishes it into every address space that already has the area
 * mapped. True means the access may be retried.
 *
 * The process must already hold a mapping. A fault from someone who does not
 * is not an area growing, it is a wild pointer that happened to land in the
 * window, and committing memory to it would be answering a bug with a frame.
 */
bool kernel_area_fault(uint32_t process_id, KernelAddressSpace *space,
                       uint32_t address);
/*
 * Drops the committed pages of a reserved area that lie wholly inside the
 * range, and keeps the reservation. This is what makes freeing memory mean
 * something: without it "returns memory to the system" is a sentence rather
 * than a behaviour, and a long-running program ratchets upward until it dies.
 *
 * Only whole pages entirely inside the range go, because a page half of which
 * is still live is a page that is still live. Touching the address again
 * re-faults and gets a fresh zeroed page, which is the same contract
 * MADV_DONTNEED offers and the only one that can be honoured without knowing
 * what the owner meant to keep.
 */
KernelAreaStatus kernel_area_decommit(uint32_t process_id,
                                      KernelAddressSpace *space,
                                      uint32_t address, uint32_t byte_size,
                                      uint32_t *released_pages);
void kernel_area_abandon_unpublished(KernelArea *area);
bool kernel_area_handle_retain(void *object, void *context);
void kernel_area_handle_release(void *object, void *context);
KernelAreaStatus kernel_area_child_retain(KernelArea *area);
KernelAreaStatus kernel_area_child_release(KernelArea *area);
KernelAreaStatus kernel_area_map(KernelArea *area, uint32_t process_id,
                                 KernelAddressSpace *space,
                                 uint32_t permissions,
                                 uint32_t *virtual_base,
                                 uint32_t *byte_size);
KernelAreaStatus kernel_area_unmap(uint32_t process_id,
                                   KernelAddressSpace *space,
                                   uint32_t virtual_base);
KernelAreaStatus kernel_area_process_died(uint32_t process_id,
                                          uint32_t *closed_areas,
                                          uint32_t *revoked_mappings);
KernelAreaStatus kernel_area_write(KernelArea *area, uint32_t offset,
                                   const void *source, uint32_t size);
KernelAreaStatus kernel_area_read(const KernelArea *area, uint32_t offset,
                                  void *destination, uint32_t size);
bool kernel_area_live(const KernelArea *area);
uint32_t kernel_area_creator(const KernelArea *area);
uint32_t kernel_area_generation(const KernelArea *area);
uint32_t kernel_area_size(const KernelArea *area);
bool kernel_area_snapshot(uint32_t slot, KernelAreaSnapshot *snapshot);
bool kernel_area_pool_stats(KernelAreaPoolStats *stats);
bool kernel_area_pool_healthy(void);
bool kernel_area_pool_valid(void);

#if defined(KERNEL_AREA_HOST_TEST)
typedef enum KernelAreaTestFault {
    KERNEL_AREA_TEST_FAULT_NONE = 0,
    KERNEL_AREA_TEST_FAULT_CREATE_AFTER_RESERVE,
    KERNEL_AREA_TEST_FAULT_CREATE_AFTER_FRAME_ALLOCATE,
    KERNEL_AREA_TEST_FAULT_MAP_AFTER_VM_PUBLISH,
    KERNEL_AREA_TEST_FAULT_COUNT
} KernelAreaTestFault;

void kernel_area_test_bind_physical_memory(uint8_t *memory, uint32_t base,
                                           uint32_t size);
void kernel_area_test_fail_next(KernelAreaTestFault fault);
#endif

#endif
