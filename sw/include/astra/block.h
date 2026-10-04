#ifndef ASTRA_BLOCK_H
#define ASTRA_BLOCK_H

/**
 * @file block.h
 * @brief The block admission ABI: what a protected block service may ask
 *        of Axiom.
 *
 * The kernel keeps the transport and the DMA engine. A service holds a
 * device lease and a completion IRQ endpoint, owns its transfer memory, and
 * never names a physical address. Requirements are recorded in
 * `docs/STORAGE_AND_VFS.md`.
 */

#include <astra/compiler.h>
#include <astra/address_space.h>

/** @defgroup astra_block Block admission ABI
 *  @brief The lease, submit, and collect records a block service exchanges
 *  with the kernel's block engine.
 *  @{
 */

/** Native-big-endian `BLKS` block device class signature. */
#define ASTRA_DEVICE_CLASS_BLOCK UINT32_C(0x424c4b53) /* BLKS */
/** The one block device Astra admits leases against today. */
#define ASTRA_DEVICE_ID_BLOCK0   UINT32_C(0x424c0001)

/** Capability-table name published in the initial image's startup block. */
#define ASTRA_CAPABILITY_BLOCK_DEVICE "BLOCK_DEVICE"
/** Capability-table name for the device's completion IRQ endpoint. */
#define ASTRA_CAPABILITY_BLOCK_IRQ    "BLOCK_IRQ"

/*
 * The transport is fixed at 512-byte sectors today. It is reported rather than
 * assumed so a service written now keeps working when it is not.
 */
/** Bytes per sector on the transport. See ::AstraBlockLeaseInfo::sector_bytes. */
#define ASTRA_BLOCK_SECTOR_BYTES 512u

/** Lease capability bit: the device accepts ::ASTRA_BLOCK_OP_READ requests. */
#define ASTRA_BLOCK_CAP_READ  (1u << 0)
/** Lease capability bit: the device accepts ::ASTRA_BLOCK_OP_WRITE requests. */
#define ASTRA_BLOCK_CAP_WRITE (1u << 1)
/** Lease capability bit: the device accepts ::ASTRA_BLOCK_OP_FLUSH requests. */
#define ASTRA_BLOCK_CAP_FLUSH (1u << 2)

/** Lease state bit: the transport link to the device is up. */
#define ASTRA_BLOCK_STATE_LINK_UP       (1u << 0)
/** Lease state bit: removable media is present in the device. */
#define ASTRA_BLOCK_STATE_MEDIA_PRESENT (1u << 1)
/** Lease state bit: the device currently accepts writes. */
#define ASTRA_BLOCK_STATE_WRITE_ENABLE  (1u << 2)

/** ::AstraBlockRequest::operation: read sectors into the transfer buffer. */
#define ASTRA_BLOCK_OP_READ  1u
/** ::AstraBlockRequest::operation: write sectors from the transfer buffer. */
#define ASTRA_BLOCK_OP_WRITE 2u
/** ::AstraBlockRequest::operation: flush the device's write cache. */
#define ASTRA_BLOCK_OP_FLUSH 3u

/*
 * Completion outcomes are distinct because a filesystem must treat them
 * differently: a late completion is not a failure of the request being waited
 * on, and a media change invalidates cached state that a device error does not.
 */
/** ::AstraBlockCompletion::status: the request completed as asked. */
#define ASTRA_BLOCK_COMPLETION_OK            0u
/** ::AstraBlockCompletion::status: the device reported a transfer error. */
#define ASTRA_BLOCK_COMPLETION_DEVICE_ERROR  1u
/**
 * ::AstraBlockCompletion::status: the request finished, but after the
 * waiter already gave up on it -- not itself a failure of the transfer.
 */
#define ASTRA_BLOCK_COMPLETION_LATE          2u
/** ::AstraBlockCompletion::status: the request was cancelled before completion. */
#define ASTRA_BLOCK_COMPLETION_CANCELLED     3u
/** ::AstraBlockCompletion::status: the device or transport was reset in flight. */
#define ASTRA_BLOCK_COMPLETION_RESET         4u
/**
 * ::AstraBlockCompletion::status: the media changed during the request,
 * which invalidates any state a client had cached about it.
 */
#define ASTRA_BLOCK_COMPLETION_MEDIA_CHANGED 5u

/** Encoded byte size of ::AstraBlockLeaseInfo. */
#define ASTRA_BLOCK_LEASE_INFO_SIZE   40u
/** Encoded byte size of ::AstraBlockRequest. */
#define ASTRA_BLOCK_REQUEST_SIZE    32u
/** Encoded byte size of ::AstraBlockCompletion. */
#define ASTRA_BLOCK_COMPLETION_SIZE 32u

/** One in-flight request per process DMA slot; no separate block quota. */
#define ASTRA_BLOCK_MAX_REQUESTS_PER_SERVICE ASTRA_DMA_SLOT_COUNT

#ifndef __ASSEMBLER__

#include <stdint.h>

/* For ASTRA_ABI_ALIGNMENT: these records cross the syscall boundary. */
#include <astra/syscall.h>

/**
 * Device geometry and lease state, as reported by ::ASTRA_SYSCALL_BLOCK_QUERY.
 *
 * Read with the lease's query right; nothing here is a DMA address, so
 * holding it grants no access beyond what the lease handle already does.
 */
typedef struct AstraBlockLeaseInfo {
    /** Structure size; always ::ASTRA_BLOCK_LEASE_INFO_SIZE. */
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    /** Bytes per sector; currently always ::ASTRA_BLOCK_SECTOR_BYTES. */
    uint32_t sector_bytes;
    /** Largest sector count the backend accepts in one request. */
    uint32_t max_transfer_sectors;
    /** ASTRA_BLOCK_CAP_* bits the device currently supports. */
    uint32_t capabilities;
    /** ASTRA_BLOCK_STATE_* bits describing the device's current state. */
    uint32_t state_flags;
    /** Counter that advances whenever the removable media changes. */
    uint32_t media_generation;
    /** Counter that advances whenever the device or transport resets. */
    uint32_t host_generation;
    /** Simultaneous requests the physical backend can actually keep active. */
    uint32_t queue_depth;
    /** Total sectors addressable on the current media. */
    uint64_t sector_count;
} AstraBlockLeaseInfo;

/**
 * One block transfer request, submitted with ::ASTRA_SYSCALL_BLOCK_SUBMIT.
 *
 * Submitting returns a request handle immediately; the transfer itself
 * completes asynchronously and is retrieved with
 * ::ASTRA_SYSCALL_BLOCK_COLLECT via a matching ::AstraBlockCompletion.
 */
typedef struct AstraBlockRequest {
    /** Structure size; must equal ::ASTRA_BLOCK_REQUEST_SIZE. */
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    /** One of ::ASTRA_BLOCK_OP_READ, ::ASTRA_BLOCK_OP_WRITE, ::ASTRA_BLOCK_OP_FLUSH. */
    uint32_t operation;
    /** Transfer-memory handle, never an address. Unused for ::ASTRA_BLOCK_OP_FLUSH. */
    uint32_t buffer;
    /** Byte offset of the transfer within `buffer`. */
    uint32_t buffer_offset;
    /** Sector count to transfer; zero and unused for ::ASTRA_BLOCK_OP_FLUSH. */
    uint32_t sectors;
    /** Must be zero. */
    uint32_t reserved;
    /** Starting logical block address on the device. */
    uint64_t lba;
} AstraBlockRequest;

/**
 * The outcome of one previously submitted ::AstraBlockRequest, retrieved
 * with ::ASTRA_SYSCALL_BLOCK_COLLECT.
 */
typedef struct AstraBlockCompletion {
    /** Structure size; always ::ASTRA_BLOCK_COMPLETION_SIZE. */
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    /** The request handle this completion answers. */
    uint32_t request;
    /** One of the ASTRA_BLOCK_COMPLETION_* outcomes. */
    uint32_t status;
    /** Backend-specific diagnostic detail; meaningful only on a device error. */
    uint32_t detail;
    /** Sectors actually transferred before completion. */
    uint32_t sectors;
    /** The device's media generation at completion time. */
    uint32_t media_generation;
    /** The device's host/transport generation at completion time. */
    uint32_t host_generation;
    /** Reserved; currently always zero. */
    uint32_t flags;
} AstraBlockCompletion;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraBlockLeaseInfo) == ASTRA_BLOCK_LEASE_INFO_SIZE,
               "block geometry ABI size changed");
_Static_assert(sizeof(AstraBlockRequest) == ASTRA_BLOCK_REQUEST_SIZE,
               "block request ABI size changed");
_Static_assert(sizeof(AstraBlockCompletion) == ASTRA_BLOCK_COMPLETION_SIZE,
               "block completion ABI size changed");

_Static_assert(_Alignof(AstraBlockLeaseInfo) % ASTRA_ABI_ALIGNMENT == 0u,
               "block geometry must satisfy the syscall alignment rule");
_Static_assert(_Alignof(AstraBlockRequest) % ASTRA_ABI_ALIGNMENT == 0u,
               "block request must satisfy the syscall alignment rule");
_Static_assert(_Alignof(AstraBlockCompletion) % ASTRA_ABI_ALIGNMENT == 0u,
               "block completion must satisfy the syscall alignment rule");
/** @endcond */

#endif

/** @} */

#endif
