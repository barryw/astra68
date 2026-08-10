#ifndef ASTRA_KERNEL_AREA_H
#define ASTRA_KERNEL_AREA_H

#include "handle.h"
#include "memory.h"
#include "vm.h"

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_AREA_MAX KERNEL_VM_AREA_SLOT_COUNT
#define KERNEL_AREA_OWNER_MAX 4u
#define KERNEL_AREA_PAGE_MAX \
    (KERNEL_VM_AREA_SLOT_SIZE / KERNEL_PAGE_SIZE)
#define KERNEL_AREA_OWNER_PAGE_MAX KERNEL_AREA_PAGE_MAX
#define KERNEL_AREA_SYSTEM_PAGE_MAX (KERNEL_AREA_MAX * KERNEL_AREA_PAGE_MAX)
#define KERNEL_AREA_MAPPING_MAX \
    (KERNEL_AREA_MAX * KERNEL_VM_SHARED_ALIAS_MAX)
#define KERNEL_AREA_PROCESS_MAPPING_MAX 4u

#define KERNEL_AREA_RIGHTS \
    ((1u << 0) | (1u << 1) | (1u << 2) | (1u << 5) | (1u << 6))

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
    uint8_t state;
    uint8_t frames_released;
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
} KernelAreaPoolStats;

void kernel_area_pool_init(void);
KernelAreaStatus kernel_area_create(uint32_t creator, uint32_t byte_size,
                                    KernelArea **area);
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
