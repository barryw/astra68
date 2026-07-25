#ifndef ASTRA_KERNEL_VM_H
#define ASTRA_KERNEL_VM_H

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_VM_USER_MIN 0x00010000u
#define KERNEL_VM_USER_MAX 0x7fffffffu

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
KernelVmStatus kernel_vm_switch(const KernelAddressSpace *space);
KernelVmStatus kernel_vm_switch_to_empty(void);
bool kernel_vm_stats(KernelVmStats *stats);

#if defined(KERNEL_VM_HOST_TEST)
void kernel_vm_test_bind_physical_memory(uint8_t *memory, uint32_t base,
                                         uint32_t size);
bool kernel_vm_test_translate_current(uint32_t virtual_address, bool write,
                                      uint32_t *physical_address);
#endif

#endif
