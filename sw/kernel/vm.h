#ifndef ASTRA_KERNEL_VM_H
#define ASTRA_KERNEL_VM_H

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_VM_USER_MIN 0x00010000u
#define KERNEL_VM_USER_MAX 0x7fffffffu
#define KERNEL_VM_AREA_BASE 0x40000000u
#define KERNEL_VM_AREA_SLOT_SIZE 0x00200000u
#define KERNEL_VM_AREA_SLOT_COUNT 8u
#define KERNEL_VM_SHARED_ALIAS_MAX 7u
/*
 * Transfer memory lands in its own window. Each buffer is process-private, so
 * the slot index is per address space rather than global.
 */
#define KERNEL_VM_DMA_BASE 0x50000000u
#define KERNEL_VM_DMA_SLOT_SIZE 0x00200000u
#define KERNEL_VM_DMA_SLOT_COUNT 4u

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

typedef struct KernelAddressSpace {
    uint32_t owner;
    uint32_t root_physical;
    uint32_t mapped_pages;
    uint32_t table_pages;
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
    uint32_t srp_limit_descriptor;
    uint32_t srp_table_address;
    uint32_t crp_limit_descriptor;
    uint32_t crp_table_address;
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
KernelVmStatus kernel_vm_destroy_address_space(KernelAddressSpace *space);
KernelVmStatus kernel_vm_map_page(KernelAddressSpace *space,
                                  uint32_t virtual_address,
                                  uint32_t physical_address,
                                  uint32_t permissions);
KernelVmStatus kernel_vm_unmap_page(KernelAddressSpace *space,
                                    uint32_t virtual_address);
/*
 * Maps a DMA frame the process owns, cache-inhibited: the device writes these
 * pages behind the data cache's back.
 */
KernelVmStatus kernel_vm_map_transfer_page(KernelAddressSpace *space,
                                           uint32_t virtual_address,
                                           uint32_t physical_address,
                                           uint32_t permissions);
KernelVmStatus kernel_vm_map_shared_range(
    KernelAddressSpace *space, uint32_t virtual_address,
    const uint32_t *physical_pages, uint32_t page_count,
    uint32_t frame_owner, uint32_t permissions);
KernelVmStatus kernel_vm_unmap_shared_range(
    KernelAddressSpace *space, uint32_t virtual_address,
    const uint32_t *physical_pages, uint32_t page_count,
    uint32_t frame_owner);
KernelVmStatus kernel_vm_switch(const KernelAddressSpace *space);
KernelVmStatus kernel_vm_switch_to_empty(void);
KernelVmStatus kernel_vm_sync_shared_aliases(void);
bool kernel_vm_stats(KernelVmStats *stats);
bool kernel_vm_control_state(KernelVmControlState *state);
KernelVmMapping kernel_vm_probe_current(uint32_t virtual_address,
                                        bool supervisor,
                                        uint32_t *physical_address);

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
