#ifndef ASTRA_KERNEL_PMMU_H
#define ASTRA_KERNEL_PMMU_H

#include <stdint.h>

#define KERNEL_PMMU_TC_4K_10_10       0x80c0aa00u
#define KERNEL_PMMU_TC_4K_10_10_SRE   0x82c0aa00u
#define KERNEL_PMMU_ROOT_LIMIT_SHORT  0x03ff0002u

typedef struct KernelPmmuRootPointer {
    uint32_t limit_descriptor;
    uint32_t table_address;
} KernelPmmuRootPointer;

void kernel_pmmu_load_tc(const uint32_t *value);
void kernel_pmmu_load_srp(const KernelPmmuRootPointer *root);
void kernel_pmmu_load_crp(const KernelPmmuRootPointer *root);
void kernel_pmmu_load_tt0(const uint32_t *value);
void kernel_pmmu_load_tt1(const uint32_t *value);
void kernel_pmmu_flush_all(void);
void kernel_pmmu_set_user_function_codes(void);
void kernel_cache_invalidate_all(void);

#endif
