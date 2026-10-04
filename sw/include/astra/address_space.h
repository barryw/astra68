#ifndef ASTRA_ADDRESS_SPACE_H
#define ASTRA_ADDRESS_SPACE_H

#include <astra/compiler.h>
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
/** First address of the complete user half of the address space. */
#define ASTRA_USER_ADDRESS_START             0x00000000u
/** One past the last address of the user half; the supervisor half begins
 *  here. */
#define ASTRA_USER_ADDRESS_END               0x80000000u

/** First address of the null-guard region: no process may map here. */
#define ASTRA_NULL_GUARD_START               0x00000000u
/** One past the last address of the null-guard region. */
#define ASTRA_NULL_GUARD_END                 0x00010000u

/** First address of the kernel's process-startup region. */
#define ASTRA_STARTUP_ADDRESS_START          0x00010000u
/** One past the last address of the process-startup region. */
#define ASTRA_STARTUP_ADDRESS_END            0x00011000u

/** First address of the program-image region; follows the startup region. */
#define ASTRA_EXECUTABLE_ADDRESS_START       ASTRA_STARTUP_ADDRESS_END
/** Fixed link address used for a program image. */
#define ASTRA_EXECUTABLE_LINK_ADDRESS        0x00100000u
/** One past the last address of the program-image region. */
#define ASTRA_EXECUTABLE_ADDRESS_END         0x20000000u

/** First address of the runtime loader's dynamic-image region. */
#define ASTRA_DYNAMIC_IMAGE_BASE             ASTRA_EXECUTABLE_ADDRESS_END
/** One past the last address of the dynamic-image region. */
#define ASTRA_DYNAMIC_IMAGE_END              0x40000000u

/** First address of the kernel shared-area region. */
#define ASTRA_SHARED_AREA_ADDRESS_START      ASTRA_DYNAMIC_IMAGE_END
/** Byte size of one shared-area slot. */
#define ASTRA_SHARED_AREA_SLOT_SIZE           0x00400000u
/** Number of shared-area slots the region holds. */
#define ASTRA_SHARED_AREA_SLOT_COUNT          32u
/** One past the last address of the shared-area region. */
#define ASTRA_SHARED_AREA_ADDRESS_END         0x48000000u

/** First address of the range reserved for future ABI growth. */
#define ASTRA_RESERVED_ADDRESS_START          ASTRA_SHARED_AREA_ADDRESS_END
/** One past the last address of the reserved range. */
#define ASTRA_RESERVED_ADDRESS_END            0x4ff00000u

/** First address of the kernel host-transport-channel region. */
#define ASTRA_HOST_CHANNEL_ADDRESS_START      ASTRA_RESERVED_ADDRESS_END
/** One past the last address of the host-transport-channel region. */
#define ASTRA_HOST_CHANNEL_ADDRESS_END        0x50000000u

/** First address of the kernel DMA region. */
#define ASTRA_DMA_ADDRESS_START               ASTRA_HOST_CHANNEL_ADDRESS_END
/** One past the last address of the DMA region. */
#define ASTRA_DMA_ADDRESS_END                 0x52000000u
/** Byte size of one DMA slot. */
#define ASTRA_DMA_SLOT_SIZE                    0x00800000u
/** Number of DMA slots the region holds. */
#define ASTRA_DMA_SLOT_COUNT \
    ((ASTRA_DMA_ADDRESS_END - ASTRA_DMA_ADDRESS_START) / ASTRA_DMA_SLOT_SIZE)

/** First address of the kernel private-VM region. */
#define ASTRA_PRIVATE_ADDRESS_START           ASTRA_DMA_ADDRESS_END
/** One past the last address of the private-VM region. */
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
/** First address of the process heap's allocator-state root slot (not an
 *  allocation arena). */
#define ASTRA_PROCESS_HEAP_CONTROL_START       ASTRA_PRIVATE_ADDRESS_START
/** One past the last address of the heap allocator-state root slot. */
#define ASTRA_PROCESS_HEAP_CONTROL_END         0x52400000u
/** First address of the process heap's allocatable range. */
#define ASTRA_PROCESS_HEAP_ADDRESS_START       ASTRA_PROCESS_HEAP_CONTROL_END
/** One past the last address of the process heap's allocatable range. */
#define ASTRA_PROCESS_HEAP_ADDRESS_END         ASTRA_PRIVATE_ADDRESS_END

/** First address of the thread-stack region. */
#define ASTRA_THREAD_STACK_ADDRESS_START      ASTRA_PRIVATE_ADDRESS_END
/** One past the last address of the thread-stack region. */
#define ASTRA_THREAD_STACK_ADDRESS_END        0x78000000u
/** Byte size reserved per thread: its stack plus one guard page. */
#define ASTRA_THREAD_STACK_RESERVATION_BYTES  0x00800000u
/** Number of thread-stack reservation slots the region holds. */
#define ASTRA_PROCESS_THREAD_SLOT_COUNT \
    ((ASTRA_THREAD_STACK_ADDRESS_END - ASTRA_THREAD_STACK_ADDRESS_START) / \
     ASTRA_THREAD_STACK_RESERVATION_BYTES)
/** Byte size of the unmapped guard page at the floor of each stack
 *  reservation, below the stack's usable range. */
#define ASTRA_THREAD_STACK_GUARD_BYTES ASTRA_MEMORY_PAGE_SIZE
/** Largest usable stack size within one reservation, after its guard page. */
#define ASTRA_THREAD_STACK_BYTES_MAX \
    (ASTRA_THREAD_STACK_RESERVATION_BYTES - ASTRA_THREAD_STACK_GUARD_BYTES)

/** First address of the thread-local-storage region. */
#define ASTRA_THREAD_TLS_ADDRESS_START        ASTRA_THREAD_STACK_ADDRESS_END
/** One past the last address of the thread-local-storage region; the end
 *  of the user address space. */
#define ASTRA_THREAD_TLS_ADDRESS_END          ASTRA_USER_ADDRESS_END

/*
 * id, name, start, end, owner. Tests, diagnostics, and the kernel's mapping
 * admission checks consume this list directly; do not maintain a second
 * table by hand.
 */
/**
 * X-macro listing every owned region of the user address space, in address
 * order.
 *
 * Invoking this with an `X(id, name, start, end, owner)` callback macro
 * expands to one call per region: `ASTRA_ADDRESS_REGION_NULL_GUARD`
 * (`null_guard`), `ASTRA_ADDRESS_REGION_STARTUP` (`startup`),
 * `ASTRA_ADDRESS_REGION_EXECUTABLE` (`executable`),
 * `ASTRA_ADDRESS_REGION_DYNAMIC_IMAGES` (`dynamic_images`),
 * `ASTRA_ADDRESS_REGION_SHARED_AREAS` (`shared_areas`),
 * `ASTRA_ADDRESS_REGION_RESERVED` (`reserved`),
 * `ASTRA_ADDRESS_REGION_HOST_CHANNELS` (`host_channels`),
 * `ASTRA_ADDRESS_REGION_DMA` (`dma`),
 * `ASTRA_ADDRESS_REGION_PRIVATE_MEMORY` (`private_memory`),
 * `ASTRA_ADDRESS_REGION_THREAD_STACKS` (`thread_stacks`), and
 * `ASTRA_ADDRESS_REGION_THREAD_TLS` (`thread_tls`). This is the single
 * source consumed to build both the ::AstraAddressRegion enum below and
 * the kernel's own admission table, so the two cannot drift apart.
 *
 * @param X Callback macro invoked once per region as
 *          `X(id, name, start, end, owner)`.
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

/**
 * Canonical owner of one process-virtual address interval.
 *
 * Every enumerator but the last two is generated from
 * ::ASTRA_USER_ADDRESS_REGIONS, one per listed region, in address order.
 */
typedef enum AstraAddressRegion {
/** @cond ASTRA_INTERNAL */
#define ASTRA_ADDRESS_REGION_ENUM(id, name, start, end, owner) id,
    ASTRA_USER_ADDRESS_REGIONS(ASTRA_ADDRESS_REGION_ENUM)
#undef ASTRA_ADDRESS_REGION_ENUM
/** @endcond */
    /** Count of defined regions; one past the last valid region id. */
    ASTRA_ADDRESS_REGION_COUNT,
    /** Not a valid region: no owned range matched the query. */
    ASTRA_ADDRESS_REGION_INVALID = -1
} AstraAddressRegion;

/**
 * Classify a complete byte range by its sole canonical owner.
 *
 * A zero-sized, overflowing, cross-boundary, or supervisor-half range is
 * invalid. Keeping this classifier beside the constants gives every mapping
 * path the same answer about ownership.
 *
 * @param address First byte of the range to classify.
 * @param byte_size Size of the range in bytes.
 * @return The region that owns the complete range, or
 *         ::ASTRA_ADDRESS_REGION_INVALID if none does.
 */
static inline AstraAddressRegion
astra_address_region(uint32_t address, uint32_t byte_size)
{
    if (byte_size == 0u || byte_size - 1u > UINT32_MAX - address)
        return ASTRA_ADDRESS_REGION_INVALID;
/** @cond ASTRA_INTERNAL */
#define ASTRA_ADDRESS_REGION_MATCH(id, name, start, finish, owner)          \
    do {                                                                    \
        uint32_t offset = address - (start);                               \
        uint32_t extent = (finish) - (start);                              \
        if (offset < extent && byte_size <= extent - offset)               \
            return (id);                                                    \
    } while (0);
    ASTRA_USER_ADDRESS_REGIONS(ASTRA_ADDRESS_REGION_MATCH)
#undef ASTRA_ADDRESS_REGION_MATCH
/** @endcond */
    return ASTRA_ADDRESS_REGION_INVALID;
}

/** @cond ASTRA_INTERNAL */
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
_Static_assert(ASTRA_DMA_ADDRESS_END - ASTRA_DMA_ADDRESS_START ==
                   ASTRA_DMA_SLOT_SIZE * ASTRA_DMA_SLOT_COUNT,
               "DMA slot geometry does not fill its ABI range");
_Static_assert(ASTRA_THREAD_STACK_ADDRESS_END -
                       ASTRA_THREAD_STACK_ADDRESS_START ==
                   ASTRA_THREAD_STACK_RESERVATION_BYTES *
                       ASTRA_PROCESS_THREAD_SLOT_COUNT,
               "thread stack reservations do not fill their ABI range");
/** @endcond */
#endif

/** @} */

#endif
