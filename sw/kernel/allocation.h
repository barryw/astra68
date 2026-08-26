#ifndef ASTRA_KERNEL_ALLOCATION_H
#define ASTRA_KERNEL_ALLOCATION_H

#include <stdbool.h>
#include <stdint.h>

typedef enum KernelAllocationTag {
    KERNEL_ALLOCATION_TAG_INVALID = 0,
    KERNEL_ALLOCATION_TAG_BOOT = 1,
    KERNEL_ALLOCATION_TAG_VM = 2,
    KERNEL_ALLOCATION_TAG_PROCESS = 3,
    KERNEL_ALLOCATION_TAG_THREAD = 4,
    KERNEL_ALLOCATION_TAG_HANDLE = 5,
    KERNEL_ALLOCATION_TAG_SYNC = 6,
    KERNEL_ALLOCATION_TAG_IPC = 7,
    KERNEL_ALLOCATION_TAG_AREA = 8,
    KERNEL_ALLOCATION_TAG_RING = 9,
    KERNEL_ALLOCATION_TAG_DMA = 10,
    KERNEL_ALLOCATION_TAG_BLOCK = 11,
    KERNEL_ALLOCATION_TAG_EMERGENCY = 12,
    KERNEL_ALLOCATION_TAG_MEMORY = 13,
    KERNEL_ALLOCATION_TAG_DEVICE = 14,
    KERNEL_ALLOCATION_TAG_COUNT = 15
} KernelAllocationTag;

typedef enum KernelAllocationSite {
    KERNEL_ALLOCATION_SITE_INVALID = 0,
    KERNEL_ALLOCATION_SITE_BOOT_SELFTEST_PAGE = 1,
    KERNEL_ALLOCATION_SITE_VM_PAGE_TABLE = 2,
    KERNEL_ALLOCATION_SITE_PROCESS_RECORD = 3,
    KERNEL_ALLOCATION_SITE_PROCESS_CODE_PAGE = 4,
    KERNEL_ALLOCATION_SITE_THREAD_RECORD = 5,
    KERNEL_ALLOCATION_SITE_THREAD_STACK_PAGE = 6,
    KERNEL_ALLOCATION_SITE_HANDLE_SLOT = 7,
    KERNEL_ALLOCATION_SITE_DETACHED_HANDLE = 8,
    KERNEL_ALLOCATION_SITE_SYNC_OBJECT = 9,
    KERNEL_ALLOCATION_SITE_PORT_OBJECT = 10,
    KERNEL_ALLOCATION_SITE_PORT_MESSAGE = 11,
    KERNEL_ALLOCATION_SITE_AREA_OBJECT = 12,
    KERNEL_ALLOCATION_SITE_AREA_PAGES = 13,
    KERNEL_ALLOCATION_SITE_AREA_MAPPING = 14,
    KERNEL_ALLOCATION_SITE_RING_OBJECT = 15,
    KERNEL_ALLOCATION_SITE_DMA_OBJECT = 16,
    KERNEL_ALLOCATION_SITE_DMA_PAGES = 17,
    KERNEL_ALLOCATION_SITE_BLOCK_REQUEST = 18,
    KERNEL_ALLOCATION_SITE_EMERGENCY_FAULT_PAGE = 19,
    KERNEL_ALLOCATION_SITE_EMERGENCY_CLEANUP_PAGE = 20,
    KERNEL_ALLOCATION_SITE_EMERGENCY_LOG_PAGE = 21,
    KERNEL_ALLOCATION_SITE_EMERGENCY_RESERVE = 22,
    KERNEL_ALLOCATION_SITE_MEMORY_GENERIC = 23,
    KERNEL_ALLOCATION_SITE_IRQ_ENDPOINT = 24,
    KERNEL_ALLOCATION_SITE_DEVICE_LEASE = 25,
    KERNEL_ALLOCATION_SITE_LIBRARY_PAGE = 26,
    KERNEL_ALLOCATION_SITE_PROCESS_PRIVATE_PAGE = 27,
    KERNEL_ALLOCATION_SITE_PROCESS_LOAD_RECORD = 28,
    KERNEL_ALLOCATION_SITE_COUNT = 29
} KernelAllocationSite;

typedef enum KernelAllocationPhase {
    KERNEL_ALLOCATION_PHASE_BOOT = 0,
    KERNEL_ALLOCATION_PHASE_RUNTIME = 1
} KernelAllocationPhase;

typedef struct KernelAllocationStats {
    uint32_t attempts;
    uint32_t successes;
    uint32_t failures;
    uint32_t injected_failures;
    uint32_t releases;
    uint32_t current_units;
    uint32_t peak_units;
    uint32_t current_bytes;
    uint32_t peak_bytes;
    uint32_t last_owner;
} KernelAllocationStats;

typedef struct KernelAllocationSiteInfo {
    const char *name;
    uint8_t tag;
    uint8_t boot_only;
    uint8_t injectable;
    uint8_t reserved;
} KernelAllocationSiteInfo;

void kernel_allocation_init(void);
bool kernel_allocation_initialized(void);
KernelAllocationPhase kernel_allocation_phase(void);
bool kernel_allocation_retire_boot(void);

bool kernel_allocation_attempt(KernelAllocationSite site, uint32_t owner);
bool kernel_allocation_commit(KernelAllocationSite site, uint32_t units,
                              uint32_t bytes, uint32_t owner);
void kernel_allocation_fail(KernelAllocationSite site, uint32_t owner);
bool kernel_allocation_release(KernelAllocationSite site, uint32_t units,
                               uint32_t bytes);
bool kernel_allocation_seed(KernelAllocationSite site, uint32_t units,
                            uint32_t bytes);

bool kernel_allocation_site_info(KernelAllocationSite site,
                                 KernelAllocationSiteInfo *info);
bool kernel_allocation_site_stats(KernelAllocationSite site,
                                  KernelAllocationStats *stats);
bool kernel_allocation_tag_stats(KernelAllocationTag tag,
                                 KernelAllocationStats *stats);
bool kernel_allocation_valid(void);

void kernel_allocation_test_fail_global(uint32_t attempt);
void kernel_allocation_test_fail_site(KernelAllocationSite site,
                                      uint32_t attempt);
void kernel_allocation_test_clear_failure(void);

#endif
