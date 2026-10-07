#ifndef ASTRA_KERNEL_TIME_PAGE_H
#define ASTRA_KERNEL_TIME_PAGE_H

#include <stdbool.h>

#include "process.h"
#include "vm.h"

/*
 * Kernel-owned shared frames live under 0x3xxxxxxx, beside
 * LIBRARY_CACHE_OWNER. Process owners are 0x1xxxxxxx and area frame owners
 * span 0x40000000-0x7fffffff, so an ASCII tag such as "TIME" (0x54494d45)
 * would collide with a reachable area owner.
 */
#define KERNEL_TIME_PAGE_OWNER 0x30000002u

/* Allocate and publish the page. Called once at boot, after the allocator. */
bool kernel_time_page_init(void);
/* Republish monotonic and wall time. Every timer tick and context switch. */
void kernel_time_page_update(void);
/* Map the page read-only at ASTRA_TIME_PAGE_ADDRESS in a new space. */
KernelProcessStatus kernel_time_page_map(KernelAddressSpace *space);

#endif
