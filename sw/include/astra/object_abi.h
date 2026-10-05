#ifndef ASTRA_OBJECT_ABI_H
#define ASTRA_OBJECT_ABI_H

/**
 * @file object_abi.h
 * @brief Handle rights, shared-area flags, and the bulk-ring shared header:
 *        the kernel object ABI that both the raw system calls (%syscall.h)
 *        and the NDK's resource, area, and bulk-ring APIs use.
 *
 * One definition, included by both: a copy in each drifts.
 */

#include <astra/address_space.h>

/** @defgroup astra_object_abi Kernel object ABI
 *  @brief Rights and layouts the kernel checks on every handle operation.
 *  @{
 */

/** Observe resource state or read data. */
#define ASTRA_RIGHT_READ       (1u << 0)
/** Modify resource state or write data. */
#define ASTRA_RIGHT_WRITE      (1u << 1)
/** Map the resource into the process address space. */
#define ASTRA_RIGHT_MAP        (1u << 2)
/** Signal an event, semaphore, or other waitable object. */
#define ASTRA_RIGHT_SIGNAL     (1u << 3)
/** Wait for the resource to become signaled or ready. */
#define ASTRA_RIGHT_WAIT       (1u << 4)
/** Transfer the capability to another process or subsystem. */
#define ASTRA_RIGHT_TRANSFER   (1u << 5)
/** Change resource configuration or lifecycle state. */
#define ASTRA_RIGHT_ADMINISTER (1u << 6)
/** Use privileged diagnostics associated with the resource. */
#define ASTRA_RIGHT_DEBUG      (1u << 7)
/** On a host device handle: open audio streams (%audio_stream.h), and
 * nothing else the device offers. */
#define ASTRA_RIGHT_AUDIO_STREAM (1u << 8)

/** Maximum size of one shared or reserved area. */
#define ASTRA_AREA_SIZE_MAX \
    (ASTRA_SHARED_AREA_ADDRESS_END - ASTRA_SHARED_AREA_ADDRESS_START)
/** Map readable pages. Every mapping must include this flag. */
#define ASTRA_AREA_MAP_READ  (1u << 0)
/** Map writable pages; requires write rights on the area handle. */
#define ASTRA_AREA_MAP_WRITE (1u << 1)
/**
 * Create flag: take the address range and commit nothing. Pages arrive as
 * they are touched, a cluster at a time, and are charged to the owner then.
 */
#define ASTRA_AREA_CREATE_RESERVED (1u << 0)

/** Native-big-endian `ARIN` bulk-ring header signature. */
#define ASTRA_BULK_RING_MAGIC 0x4152494eu
/** Current bulk-ring shared-header ABI revision. */
#define ASTRA_BULK_RING_ABI_VERSION 1u
/** Fixed shared-header size and payload offset. */
#define ASTRA_BULK_RING_HEADER_SIZE 64u
/** Required alignment for each ring's area offset. */
#define ASTRA_BULK_RING_OFFSET_ALIGNMENT 64u
/** Smallest fixed element size. */
#define ASTRA_BULK_RING_ELEMENT_SIZE_MIN 4u
/** Smallest power-of-two element capacity. */
#define ASTRA_BULK_RING_CAPACITY_MIN 2u
/** Notification flag that closes a ring after detected shared corruption. */
#define ASTRA_BULK_RING_NOTIFY_CORRUPT (1u << 0)
/** Create flag: the kernel copies payloads between private buffers. */
#define ASTRA_BULK_RING_CREATE_KERNEL_COPY (1u << 0)
/** Every valid bulk-ring create flag. */
#define ASTRA_BULK_RING_CREATE_FLAG_MASK ASTRA_BULK_RING_CREATE_KERNEL_COPY
/** Write flag: the element is written whole or not at all. */
#define ASTRA_BULK_RING_WRITE_ATOMIC (1u << 0)
/** Every valid bulk-ring write flag. */
#define ASTRA_BULK_RING_WRITE_FLAG_MASK ASTRA_BULK_RING_WRITE_ATOMIC
/** Producer endpoint role supplied to attach and notification operations. */
#define ASTRA_BULK_RING_PRODUCER 1u
/** Consumer endpoint role supplied to attach and notification operations. */
#define ASTRA_BULK_RING_CONSUMER 2u

#ifndef __ASSEMBLER__

#include <stdint.h>

#include <astra/compiler.h>

/** Shared native-big-endian bulk-ring header. */
typedef struct AstraBulkRingHeader {
    /** Immutable ::ASTRA_BULK_RING_MAGIC signature. */
    uint32_t magic;
    /** Immutable ::ASTRA_BULK_RING_ABI_VERSION. */
    uint16_t version;
    /** Immutable ::ASTRA_BULK_RING_HEADER_SIZE. */
    uint16_t header_size;
    /** Immutable flags; currently zero. */
    uint32_t flags;
    /** Immutable fixed element size in bytes. */
    uint32_t element_size;
    /** Immutable power-of-two element count. */
    uint32_t capacity;
    /** Immutable payload offset; currently 64 bytes. */
    uint32_t data_offset;
    /** Immutable complete header-plus-payload byte count. */
    uint32_t total_size;
    /** Immutable nonzero generation assigned by the kernel. */
    uint32_t generation;
    /** Monotonic element count written only by the producer. */
    uint32_t producer_position;
    /** Immutable zero fields reserved for producer-side growth. */
    uint32_t producer_reserved[3];
    /** Monotonic element count written only by the consumer. */
    uint32_t consumer_position;
    /** Immutable zero fields reserved for consumer-side growth. */
    uint32_t consumer_reserved[3];
} AstraBulkRingHeader;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraBulkRingHeader) == ASTRA_BULK_RING_HEADER_SIZE,
               "bulk-ring ABI header size changed");
/** @endcond */

#endif /* __ASSEMBLER__ */

/** @} */

#endif
