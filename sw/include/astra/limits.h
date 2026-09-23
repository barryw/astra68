#ifndef ASTRA_LIMITS_H
#define ASTRA_LIMITS_H

/* The page size shared by the kernel VM and the userspace ABI. */
#define ASTRA_MEMORY_PAGE_SHIFT 12u
#define ASTRA_MEMORY_PAGE_SIZE  (1u << ASTRA_MEMORY_PAGE_SHIFT)

/*
 * Handle zero is invalid and the low eight ABI bits encode slot+1. Every
 * representable nonzero slot is usable; there is no smaller deployment cap.
 */
#define ASTRA_HANDLE_COUNT_MAX 255u

#endif
