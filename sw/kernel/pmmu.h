#ifndef ASTRA_KERNEL_PMMU_H
#define ASTRA_KERNEL_PMMU_H

#include <stdint.h>

#define KERNEL_PMMU_TC_4K 0x00008000u

void kernel_pmmu_load_tc(uint32_t value);
void kernel_pmmu_load_srp(uint32_t root);
void kernel_pmmu_load_urp(uint32_t root);
void kernel_pmmu_read_tc(uint32_t *value);
void kernel_pmmu_read_srp(uint32_t *root);
void kernel_pmmu_read_urp(uint32_t *root);
void kernel_pmmu_flush_all(void);
void kernel_pmmu_flush_non_global(void);
/*
 * One ATC entry, by address, for every function code. PFLUSHA discards every
 * cached translation, so using it for a one-page change makes every other
 * translation in the machine pay a table walk again -- and on an emulated
 * target it also discards the host's translation cache.
 */
void kernel_pmmu_flush_page(uint32_t virtual_address);
void kernel_pmmu_disable_transparent_translation(void);
void kernel_pmmu_set_user_function_codes(void);
void kernel_cache_invalidate_all(void);
uint32_t kernel_cache_read_control(void);

#endif
