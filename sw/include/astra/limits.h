#ifndef ASTRA_LIMITS_H
#define ASTRA_LIMITS_H

/**
 * @file limits.h
 * @brief Fixed sizes the kernel VM and the userspace handle ABI both assume.
 *
 * Small and deliberately so: anything that scales with the machine (frame
 * counts, table sizes) is discovered at boot rather than compiled in here.
 */

/** Power-of-two shift giving the MC68040 PMMU's page size. */
#define ASTRA_MEMORY_PAGE_SHIFT 12u
/** Page size in bytes: `1 << ASTRA_MEMORY_PAGE_SHIFT`, 4096 on this PMMU. */
#define ASTRA_MEMORY_PAGE_SIZE  (1u << ASTRA_MEMORY_PAGE_SHIFT)

/**
 * Largest representable handle count.
 *
 * Handle zero is invalid and the low eight ABI bits encode slot+1. Every
 * representable nonzero slot is usable; there is no smaller deployment cap.
 */
#define ASTRA_HANDLE_COUNT_MAX 255u

#endif
