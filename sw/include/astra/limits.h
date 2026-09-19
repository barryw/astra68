#ifndef ASTRA_LIMITS_H
#define ASTRA_LIMITS_H

/* The page size shared by the kernel VM and the userspace ABI. */
#define ASTRA_MEMORY_PAGE_SHIFT 12u
#define ASTRA_MEMORY_PAGE_SIZE  (1u << ASTRA_MEMORY_PAGE_SHIFT)

/* One process cannot own or wait on more objects than its handle table. */
#define ASTRA_HANDLE_COUNT_MAX 128u

/* The per-process thread resource exposed by the kernel ABI. */
#define ASTRA_PROCESS_THREAD_COUNT_MAX 16u

#endif
