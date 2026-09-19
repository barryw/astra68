#ifndef ASTRA_ADDRESS_SPACE_H
#define ASTRA_ADDRESS_SPACE_H

#include <astra/limits.h>

/**
 * @file address_space.h
 * @brief Stable MC68040 process-virtual address ownership map.
 *
 * Stable process-virtual address map shared by the kernel, loader, NDK, and
 * linker contracts.  These are ABI addresses: published ranges do not move.
 * A new owner consumes an explicitly reserved range or requires a deliberate
 * breaking address-map revision; it never shifts an existing range.
 *
 * Every interval is half-open: [START, END).  The registry covers the entire
 * user half of the MC68040 address space so an unowned hole is visible and
 * cannot accidentally become two subsystems' private convention.
 */
/** @defgroup astra_address_space Process address-space ABI
 *  @brief Stable half-open virtual ranges and their canonical owners.
 *  @{
 */
#define ASTRA_USER_ADDRESS_START             0x00000000u
#define ASTRA_USER_ADDRESS_END               0x80000000u

#define ASTRA_NULL_GUARD_START               0x00000000u
#define ASTRA_NULL_GUARD_END                 0x00010000u

#define ASTRA_STARTUP_ADDRESS_START          0x00010000u
#define ASTRA_STARTUP_ADDRESS_END            0x00011000u

#define ASTRA_EXECUTABLE_ADDRESS_START       ASTRA_STARTUP_ADDRESS_END
#define ASTRA_EXECUTABLE_LINK_ADDRESS        0x00100000u
#define ASTRA_EXECUTABLE_ADDRESS_END         0x20000000u

#define ASTRA_DYNAMIC_IMAGE_BASE             ASTRA_EXECUTABLE_ADDRESS_END
#define ASTRA_DYNAMIC_IMAGE_END              0x40000000u

#define ASTRA_SHARED_AREA_ADDRESS_START      ASTRA_DYNAMIC_IMAGE_END
#define ASTRA_SHARED_AREA_SLOT_SIZE           0x00400000u
#define ASTRA_SHARED_AREA_SLOT_COUNT          32u
#define ASTRA_SHARED_AREA_ADDRESS_END         0x48000000u

#define ASTRA_RESERVED_ADDRESS_START          ASTRA_SHARED_AREA_ADDRESS_END
#define ASTRA_RESERVED_ADDRESS_END            0x4ff00000u

#define ASTRA_HOST_CHANNEL_ADDRESS_START      ASTRA_RESERVED_ADDRESS_END
#define ASTRA_HOST_CHANNEL_ADDRESS_END        0x50000000u

#define ASTRA_DMA_ADDRESS_START               ASTRA_HOST_CHANNEL_ADDRESS_END
#define ASTRA_DMA_ADDRESS_END                 0x52000000u

#define ASTRA_PRIVATE_ADDRESS_START           ASTRA_DMA_ADDRESS_END
#define ASTRA_PRIVATE_ADDRESS_END             0x70000000u

/*
 * The first private-VM root slot is process-owned allocator state, not an
 * allocation arena.  Both the bootstrap loader and runtime.library execute
 * in the same process but are distinct ELF images; keeping allocator state in
 * either image's .bss gives the process two contradictory accounts of one
 * heap.  A stable process address makes the state singular across that
 * hand-off.  The complete 4 MiB root slot is named because private-VM
 * reservations have root-slot granularity; only the touched control pages
 * consume physical memory.
 */
#define ASTRA_PROCESS_HEAP_CONTROL_START       ASTRA_PRIVATE_ADDRESS_START
#define ASTRA_PROCESS_HEAP_CONTROL_END         0x52400000u
#define ASTRA_PROCESS_HEAP_ADDRESS_START       ASTRA_PROCESS_HEAP_CONTROL_END
#define ASTRA_PROCESS_HEAP_ADDRESS_END         ASTRA_PRIVATE_ADDRESS_END

#define ASTRA_THREAD_STACK_ADDRESS_START      ASTRA_PRIVATE_ADDRESS_END
#define ASTRA_THREAD_STACK_ADDRESS_END        0x78000000u
#define ASTRA_THREAD_STACK_RESERVATION_BYTES \
    ((ASTRA_THREAD_STACK_ADDRESS_END - ASTRA_THREAD_STACK_ADDRESS_START) / \
     ASTRA_PROCESS_THREAD_COUNT_MAX)
#define ASTRA_THREAD_STACK_GUARD_BYTES ASTRA_MEMORY_PAGE_SIZE
#define ASTRA_THREAD_STACK_BYTES_MAX \
    (ASTRA_THREAD_STACK_RESERVATION_BYTES - ASTRA_THREAD_STACK_GUARD_BYTES)

#define ASTRA_THREAD_TLS_ADDRESS_START        ASTRA_THREAD_STACK_ADDRESS_END
#define ASTRA_THREAD_TLS_ADDRESS_END          ASTRA_USER_ADDRESS_END

/*
 * id, name, start, end, owner. Tests, diagnostics, and the kernel's mapping
 * admission checks consume this list directly; do not maintain a second
 * table by hand.
 */
#define ASTRA_USER_ADDRESS_REGIONS(X)                                      \
    X(ASTRA_ADDRESS_REGION_NULL_GUARD, null_guard,                         \
      ASTRA_NULL_GUARD_START, ASTRA_NULL_GUARD_END, "kernel")             \
    X(ASTRA_ADDRESS_REGION_STARTUP, startup,                               \
      ASTRA_STARTUP_ADDRESS_START, ASTRA_STARTUP_ADDRESS_END,              \
      "kernel process startup")                                           \
    X(ASTRA_ADDRESS_REGION_EXECUTABLE, executable,                         \
      ASTRA_EXECUTABLE_ADDRESS_START,                                      \
      ASTRA_EXECUTABLE_ADDRESS_END, "program images")                     \
    X(ASTRA_ADDRESS_REGION_DYNAMIC_IMAGES, dynamic_images,                 \
      ASTRA_DYNAMIC_IMAGE_BASE, ASTRA_DYNAMIC_IMAGE_END,                   \
      "runtime loader")                                                   \
    X(ASTRA_ADDRESS_REGION_SHARED_AREAS, shared_areas,                     \
      ASTRA_SHARED_AREA_ADDRESS_START,                                     \
      ASTRA_SHARED_AREA_ADDRESS_END, "kernel shared areas")               \
    X(ASTRA_ADDRESS_REGION_RESERVED, reserved,                             \
      ASTRA_RESERVED_ADDRESS_START, ASTRA_RESERVED_ADDRESS_END,            \
      "reserved")                                                         \
    X(ASTRA_ADDRESS_REGION_HOST_CHANNELS, host_channels,                   \
      ASTRA_HOST_CHANNEL_ADDRESS_START,                                    \
      ASTRA_HOST_CHANNEL_ADDRESS_END, "kernel host transport")            \
    X(ASTRA_ADDRESS_REGION_DMA, dma,                                       \
      ASTRA_DMA_ADDRESS_START, ASTRA_DMA_ADDRESS_END,                      \
      "kernel DMA")                                                       \
    X(ASTRA_ADDRESS_REGION_PRIVATE_MEMORY, private_memory,                 \
      ASTRA_PRIVATE_ADDRESS_START,                                         \
      ASTRA_PRIVATE_ADDRESS_END, "kernel private VM")                     \
    X(ASTRA_ADDRESS_REGION_THREAD_STACKS, thread_stacks,                   \
      ASTRA_THREAD_STACK_ADDRESS_START,                                    \
      ASTRA_THREAD_STACK_ADDRESS_END, "kernel thread stacks")             \
    X(ASTRA_ADDRESS_REGION_THREAD_TLS, thread_tls,                         \
      ASTRA_THREAD_TLS_ADDRESS_START,                                      \
      ASTRA_THREAD_TLS_ADDRESS_END, "kernel thread TLS")

#ifndef __ASSEMBLER__
#include <stdint.h>

/** Canonical owner of one process-virtual address interval. */
typedef enum AstraAddressRegion {
#define ASTRA_ADDRESS_REGION_ENUM(id, name, start, end, owner) id,
    ASTRA_USER_ADDRESS_REGIONS(ASTRA_ADDRESS_REGION_ENUM)
#undef ASTRA_ADDRESS_REGION_ENUM
    ASTRA_ADDRESS_REGION_COUNT,
    ASTRA_ADDRESS_REGION_INVALID = -1
} AstraAddressRegion;

/**
 * Classify a complete byte range by its sole canonical owner.
 *
 * A zero-sized, overflowing, cross-boundary, or supervisor-half range is
 * invalid. Keeping this classifier beside the constants gives every mapping
 * path the same answer about ownership.
 */
static inline AstraAddressRegion
astra_address_region(uint32_t address, uint32_t byte_size)
{
    if (byte_size == 0u || byte_size - 1u > UINT32_MAX - address)
        return ASTRA_ADDRESS_REGION_INVALID;
#define ASTRA_ADDRESS_REGION_MATCH(id, name, start, finish, owner)          \
    do {                                                                    \
        uint32_t offset = address - (start);                               \
        uint32_t extent = (finish) - (start);                              \
        if (offset < extent && byte_size <= extent - offset)               \
            return (id);                                                    \
    } while (0);
    ASTRA_USER_ADDRESS_REGIONS(ASTRA_ADDRESS_REGION_MATCH)
#undef ASTRA_ADDRESS_REGION_MATCH
    return ASTRA_ADDRESS_REGION_INVALID;
}

_Static_assert(ASTRA_NULL_GUARD_START == ASTRA_USER_ADDRESS_START,
               "user map does not begin at zero");
_Static_assert(ASTRA_NULL_GUARD_END == ASTRA_STARTUP_ADDRESS_START &&
               ASTRA_STARTUP_ADDRESS_END == ASTRA_EXECUTABLE_ADDRESS_START &&
               ASTRA_EXECUTABLE_ADDRESS_END == ASTRA_DYNAMIC_IMAGE_BASE &&
               ASTRA_DYNAMIC_IMAGE_END == ASTRA_SHARED_AREA_ADDRESS_START &&
               ASTRA_SHARED_AREA_ADDRESS_END == ASTRA_RESERVED_ADDRESS_START &&
               ASTRA_RESERVED_ADDRESS_END == ASTRA_HOST_CHANNEL_ADDRESS_START &&
               ASTRA_HOST_CHANNEL_ADDRESS_END == ASTRA_DMA_ADDRESS_START &&
               ASTRA_DMA_ADDRESS_END == ASTRA_PRIVATE_ADDRESS_START &&
               ASTRA_PRIVATE_ADDRESS_END == ASTRA_THREAD_STACK_ADDRESS_START &&
               ASTRA_THREAD_STACK_ADDRESS_END == ASTRA_THREAD_TLS_ADDRESS_START &&
               ASTRA_THREAD_TLS_ADDRESS_END == ASTRA_USER_ADDRESS_END,
               "user address ranges overlap or leave an unowned hole");
_Static_assert(ASTRA_SHARED_AREA_ADDRESS_END -
                   ASTRA_SHARED_AREA_ADDRESS_START ==
                   ASTRA_SHARED_AREA_SLOT_SIZE * ASTRA_SHARED_AREA_SLOT_COUNT,
               "shared-area geometry does not fill its ABI range");
#endif

/** @} */

#endif
