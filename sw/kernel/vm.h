#ifndef ASTRA_KERNEL_VM_H
#define ASTRA_KERNEL_VM_H

#include <stdbool.h>
#include <stdint.h>

#include <astra/block.h>
#include <astra/address_space.h>
#include <astra/host.h>
#include <astra/library.h>
#include <astra/render_batch.h>

#define KERNEL_VM_USER_MIN ASTRA_NULL_GUARD_END
#define KERNEL_VM_USER_MAX (ASTRA_USER_ADDRESS_END - 1u)
/*
 * Relocatable executable images are packed into this address-space arena by
 * their validated extent. It is an address-map boundary, not a library-count
 * or per-image-size quota; committed pages remain limited by physical RAM.
 */
#define KERNEL_VM_DYNAMIC_BASE ASTRA_DYNAMIC_IMAGE_BASE
#define KERNEL_VM_DYNAMIC_END  ASTRA_DYNAMIC_IMAGE_END
#define KERNEL_VM_AREA_BASE ASTRA_SHARED_AREA_ADDRESS_START
#define KERNEL_VM_AREA_SLOT_SIZE ASTRA_SHARED_AREA_SLOT_SIZE
/*
 * Sixteen was a machine whose graphical half was one program. A desktop is
 * seven: every window is a surface its client created, and every mount a
 * program reads through wants a transfer area of its own. A 4 MiB slot spans
 * sixteen MC68040 page tables; the VM transaction owns their publication and
 * rollback. Reserved areas commit no RAM until touched.
 */
#define KERNEL_VM_AREA_SLOT_COUNT ASTRA_SHARED_AREA_SLOT_COUNT
/*
 * Transfer memory lands in its own window. Each buffer is process-private, so
 * the slot index is per address space rather than global.
 */
#define KERNEL_VM_DMA_BASE ASTRA_DMA_ADDRESS_START
#define KERNEL_VM_DMA_SLOT_SIZE ASTRA_DMA_SLOT_SIZE
#define KERNEL_VM_DMA_SLOT_COUNT ASTRA_DMA_SLOT_COUNT

_Static_assert(ASTRA_RENDER_BATCH_BUFFER_BYTES <= KERNEL_VM_DMA_SLOT_SIZE,
               "largest DMA client exceeds a process DMA slot");

/*
 * One process-private page per thread exposes only that thread's AstraHost
 * doorbell. Mapping the whole device would let one process submit work as
 * another. The virtual window and physical aperture both cover the system's
 * complete thread pool.
 */
#define KERNEL_VM_HOST_CHANNEL_BASE ASTRA_HOST_CHANNEL_ADDRESS_START
#define KERNEL_VM_HOST_CHANNEL_PHYSICAL_BASE ASTRA_HOST_CHANNEL_PHYSICAL_BASE
#define KERNEL_VM_HOST_CHANNEL_PAGE_COUNT ASTRA_HOST_CHANNEL_COUNT

/*
 * Clone-private anonymous memory occupies every whole 4 MiB PMMU root slot
 * between transfer buffers and user stacks. Reserving a slot consumes no
 * frame; first touch commits ordinary process-owned pages through the normal
 * quota. The extent is therefore the address-map boundary, not a heap limit.
 */
#define KERNEL_VM_PRIVATE_BASE ASTRA_PRIVATE_ADDRESS_START
#define KERNEL_VM_PRIVATE_END ASTRA_PRIVATE_ADDRESS_END
#define KERNEL_VM_PRIVATE_SLOT_SIZE 0x00400000u
#define KERNEL_VM_PRIVATE_SLOT_COUNT \
    ((KERNEL_VM_PRIVATE_END - KERNEL_VM_PRIVATE_BASE) / \
     KERNEL_VM_PRIVATE_SLOT_SIZE)
#define KERNEL_VM_PRIVATE_BITMAP_WORDS \
    ((KERNEL_VM_PRIVATE_SLOT_COUNT + 31u) / 32u)
#define KERNEL_VM_PROCESS_HEAP_CONTROL_BASE \
    ASTRA_PROCESS_HEAP_CONTROL_START
#define KERNEL_VM_PROCESS_HEAP_CONTROL_END \
    ASTRA_PROCESS_HEAP_CONTROL_END

_Static_assert(KERNEL_VM_PROCESS_HEAP_CONTROL_BASE == KERNEL_VM_PRIVATE_BASE,
               "process heap control must begin the private VM window");
_Static_assert(KERNEL_VM_PROCESS_HEAP_CONTROL_END -
                   KERNEL_VM_PROCESS_HEAP_CONTROL_BASE ==
                   KERNEL_VM_PRIVATE_SLOT_SIZE,
               "process heap control must occupy exactly one root slot");

_Static_assert(KERNEL_VM_AREA_BASE +
                   KERNEL_VM_AREA_SLOT_SIZE * KERNEL_VM_AREA_SLOT_COUNT ==
                   ASTRA_SHARED_AREA_ADDRESS_END,
               "shared-area implementation disagrees with address ABI");
_Static_assert(KERNEL_VM_HOST_CHANNEL_BASE +
                   ASTRA_HOST_CHANNEL_COUNT *
                       ASTRA_HOST_CHANNEL_PAGE_SIZE ==
                   ASTRA_HOST_CHANNEL_ADDRESS_END,
               "host-channel implementation disagrees with address ABI");
_Static_assert(KERNEL_VM_DMA_BASE +
                   KERNEL_VM_DMA_SLOT_SIZE * KERNEL_VM_DMA_SLOT_COUNT ==
                   ASTRA_DMA_ADDRESS_END,
               "DMA implementation disagrees with address ABI");

#define KERNEL_VM_READ  (1u << 0)
#define KERNEL_VM_WRITE (1u << 1)
#define KERNEL_VM_EXEC  (1u << 2)

typedef enum KernelVmStatus {
    KERNEL_VM_OK = 0,
    KERNEL_VM_INVALID_ARGUMENT,
    KERNEL_VM_OUT_OF_MEMORY,
    KERNEL_VM_ALREADY_MAPPED,
    KERNEL_VM_NOT_MAPPED,
    KERNEL_VM_NOT_OWNED,
    KERNEL_VM_CACHE_ALIAS,
    KERNEL_VM_BUSY,
    KERNEL_VM_CORRUPT
} KernelVmStatus;

typedef enum KernelVmMapping {
    KERNEL_VM_MAPPING_UNKNOWN = 0,
    KERNEL_VM_MAPPING_UNMAPPED,
    KERNEL_VM_MAPPING_READ_ONLY,
    KERNEL_VM_MAPPING_READ_WRITE
} KernelVmMapping;

/** One page-aligned range used by an atomic VM rights transition. */
typedef struct KernelVmPageRange {
    uint32_t virtual_address;
    uint32_t page_count;
} KernelVmPageRange;

typedef struct KernelAddressSpace {
    struct KernelAddressSpace *registry_next;
    uint32_t owner;
    uint32_t root_physical;
    uint32_t mapped_pages;
    uint32_t table_pages;
    uint32_t private_reserved[KERNEL_VM_PRIVATE_BITMAP_WORDS];
    uint32_t private_writable[KERNEL_VM_PRIVATE_BITMAP_WORDS];
    uint8_t initialized;
    uint8_t reserved[3];
} KernelAddressSpace;

typedef struct KernelVmStats {
    uint32_t kernel_root_physical;
    uint32_t empty_root_physical;
    uint32_t kernel_stack_guard;
    uint32_t kernel_worker_stack_guard;
    uint32_t kernel_thread_stack_arena;
    uint32_t kernel_thread_stack_arena_end;
    uint32_t kernel_thread_stack_guards;
    uint32_t supervisor_table_pages;
    uint32_t address_spaces;
    uint32_t user_mappings;
    uint32_t user_table_pages;
    uint32_t flushes;
    uint32_t cache_invalidations;
    uint32_t switches;
} KernelVmStats;

typedef struct KernelVmControlState {
    uint32_t srp_table_address;
    uint32_t urp_table_address;
    uint32_t translation_control;
    uint32_t cache_control;
    uint8_t translation_enabled;
    uint8_t reserved[3];
} KernelVmControlState;

KernelVmStatus kernel_vm_init(void);
KernelVmStatus kernel_vm_enable(void);
bool kernel_vm_enabled(void);
KernelVmStatus kernel_vm_create_address_space(uint32_t owner,
                                              KernelAddressSpace *space);
KernelVmStatus kernel_vm_clone_address_space(
    KernelAddressSpace *source, uint32_t owner,
    KernelAddressSpace *destination);
KernelVmStatus kernel_vm_exchange_address_spaces(KernelAddressSpace *left,
                                                  KernelAddressSpace *right);
KernelVmStatus kernel_vm_destroy_address_space(KernelAddressSpace *space);
KernelVmStatus kernel_vm_map_page(KernelAddressSpace *space,
                                  uint32_t virtual_address,
                                  uint32_t physical_address,
                                  uint32_t permissions);
/*
 * Maps a newly allocated process page by transferring the caller's existing
 * frame reference to the mapping. Failure leaves that reference untouched.
 */
KernelVmStatus kernel_vm_adopt_page(KernelAddressSpace *space,
                                    uint32_t virtual_address,
                                    uint32_t physical_address,
                                    uint32_t permissions);
KernelVmStatus kernel_vm_map_shared_page(KernelAddressSpace *space,
                                         uint32_t virtual_address,
                                         uint32_t physical_address,
                                         uint32_t frame_owner,
                                         uint32_t permissions);
KernelVmStatus kernel_vm_map_cow_page(KernelAddressSpace *space,
                                      uint32_t virtual_address,
                                      uint32_t physical_address,
                                      uint32_t frame_owner,
                                      bool writable);
KernelVmStatus kernel_vm_promote_page_to_cow(KernelAddressSpace *space,
                                             uint32_t virtual_address);
KernelVmStatus kernel_vm_cow_make_private(KernelAddressSpace *space,
                                          uint32_t virtual_address);
KernelVmStatus kernel_vm_cow_fault(KernelAddressSpace *space,
                                   uint32_t virtual_address);
KernelVmStatus kernel_vm_unmap_page(KernelAddressSpace *space,
                                    uint32_t virtual_address);
KernelVmStatus kernel_vm_map_supervisor_stack(uint16_t slot,
                                              uint32_t physical_address);
KernelVmStatus kernel_vm_unmap_supervisor_stack(uint16_t slot);
KernelVmStatus kernel_vm_private_reserve(KernelAddressSpace *space,
                                         uint32_t byte_size,
                                         uint32_t permissions,
                                         uint32_t *virtual_base,
                                         uint32_t *mapped_span);
KernelVmStatus kernel_vm_private_reserve_largest(
    KernelAddressSpace *space, uint32_t minimum_byte_size,
    uint32_t permissions, uint32_t *virtual_base, uint32_t *mapped_span);
KernelVmStatus kernel_vm_private_fault(KernelAddressSpace *space,
                                       uint32_t virtual_address,
                                       bool write);
KernelVmStatus kernel_vm_private_commit_range(KernelAddressSpace *space,
                                              uint32_t virtual_address,
                                              uint32_t byte_size,
                                              bool write);
KernelVmStatus kernel_vm_private_decommit(KernelAddressSpace *space,
                                          uint32_t virtual_address,
                                          uint32_t byte_size,
                                          uint32_t *released_pages);
/*
 * Maps a DMA frame the process owns, cache-inhibited: the device writes these
 * pages behind the data cache's back.
 */
KernelVmStatus kernel_vm_map_transfer_page(KernelAddressSpace *space,
                                           uint32_t virtual_address,
                                           uint32_t physical_address,
                                           uint32_t permissions);
KernelVmStatus kernel_vm_map_host_channel_page(
    KernelAddressSpace *space, uint32_t virtual_address,
    uint32_t physical_address);
KernelVmStatus kernel_vm_map_shared_range(
    KernelAddressSpace *space, uint32_t virtual_address,
    const uint32_t *physical_pages, uint32_t page_count,
    uint32_t frame_owner, uint32_t permissions);
/*
 * Permanently remove write access from process-owned pages. The complete
 * range is validated before any descriptor changes, so an unmapped, shared,
 * device, or foreign page leaves every earlier page unchanged.
 */
KernelVmStatus kernel_vm_protect_read_only(KernelAddressSpace *space,
                                           uint32_t virtual_address,
                                           uint32_t page_count);
/*
 * The multi-range form validates every page in every range before changing
 * any descriptor. It is used when one logical commit seals disjoint metadata
 * regions and partial success would leave a process in an unusable state.
 */
KernelVmStatus kernel_vm_protect_read_only_ranges(
    KernelAddressSpace *space, const KernelVmPageRange *ranges,
    uint32_t range_count);
KernelVmStatus kernel_vm_unmap_shared_range(
    KernelAddressSpace *space, uint32_t virtual_address,
    const uint32_t *physical_pages, uint32_t page_count,
    uint32_t frame_owner);
KernelVmStatus kernel_vm_switch(const KernelAddressSpace *space);
KernelVmStatus kernel_vm_switch_to_empty(void);
KernelVmStatus kernel_vm_deactivate(const KernelAddressSpace *space);
KernelVmStatus kernel_vm_sync_shared_aliases(void);
bool kernel_vm_stats(KernelVmStats *stats);
bool kernel_vm_control_state(KernelVmControlState *state);
KernelVmMapping kernel_vm_probe_current(uint32_t virtual_address,
                                        bool supervisor,
                                        uint32_t *physical_address);
KernelVmMapping kernel_vm_probe_address_space(
    const KernelAddressSpace *space, uint32_t virtual_address,
    uint32_t *physical_address);
KernelVmStatus kernel_vm_read(const KernelAddressSpace *space,
                              uint32_t virtual_address,
                              void *destination, uint32_t byte_size);
KernelVmStatus kernel_vm_write(KernelAddressSpace *space,
                               uint32_t virtual_address,
                               const void *source, uint32_t byte_size);

#if defined(KERNEL_VM_HOST_TEST)
typedef enum KernelVmSharedMapFault {
    KERNEL_VM_SHARED_MAP_FAULT_NONE = 0,
    KERNEL_VM_SHARED_MAP_FAULT_AFTER_TABLE_ALLOCATE,
    KERNEL_VM_SHARED_MAP_FAULT_AFTER_FRAME_RETAIN,
    KERNEL_VM_SHARED_MAP_FAULT_AFTER_MAPPING_METADATA,
    KERNEL_VM_SHARED_MAP_FAULT_AFTER_DESCRIPTOR_PUBLISH,
    KERNEL_VM_SHARED_MAP_FAULT_AFTER_ROOT_PUBLISH,
    KERNEL_VM_SHARED_MAP_FAULT_COUNT
} KernelVmSharedMapFault;

void kernel_vm_test_bind_physical_memory(uint8_t *memory, uint32_t base,
                                         uint32_t size);
bool kernel_vm_test_translate_current(uint32_t virtual_address, bool write,
                                      uint32_t *physical_address);
void kernel_vm_test_fail_next_shared_map(KernelVmSharedMapFault fault);
#endif

#endif
