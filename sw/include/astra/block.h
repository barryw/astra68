#ifndef ASTRA_BLOCK_H
#define ASTRA_BLOCK_H

/*
 * The block admission ABI: what a protected block service may ask of Axiom.
 *
 * The kernel keeps the transport and the DMA engine. A service holds a device
 * lease and a completion IRQ endpoint, owns its transfer memory, and never
 * names a physical address. Requirements are recorded in
 * `docs/STORAGE_AND_VFS.md`.
 */

#define ASTRA_DEVICE_CLASS_BLOCK UINT32_C(0x424c4b53) /* BLKS */
#define ASTRA_DEVICE_ID_BLOCK0   UINT32_C(0x424c0001)

/* Capability-table names published in the initial image's startup block. */
#define ASTRA_CAPABILITY_BLOCK_DEVICE UINT32_C(0x424c4b44) /* BLKD */
#define ASTRA_CAPABILITY_BLOCK_IRQ    UINT32_C(0x424c4b49) /* BLKI */

/*
 * The transport is fixed at 512-byte sectors today. It is reported rather than
 * assumed so a service written now keeps working when it is not.
 */
#define ASTRA_BLOCK_SECTOR_BYTES 512u

#define ASTRA_BLOCK_CAP_READ  (1u << 0)
#define ASTRA_BLOCK_CAP_WRITE (1u << 1)
#define ASTRA_BLOCK_CAP_FLUSH (1u << 2)

#define ASTRA_BLOCK_STATE_LINK_UP       (1u << 0)
#define ASTRA_BLOCK_STATE_MEDIA_PRESENT (1u << 1)
#define ASTRA_BLOCK_STATE_WRITE_ENABLE  (1u << 2)

#define ASTRA_BLOCK_OP_READ  1u
#define ASTRA_BLOCK_OP_WRITE 2u
#define ASTRA_BLOCK_OP_FLUSH 3u

/*
 * Completion outcomes are distinct because a filesystem must treat them
 * differently: a late completion is not a failure of the request being waited
 * on, and a media change invalidates cached state that a device error does not.
 */
#define ASTRA_BLOCK_COMPLETION_OK            0u
#define ASTRA_BLOCK_COMPLETION_DEVICE_ERROR  1u
#define ASTRA_BLOCK_COMPLETION_LATE          2u
#define ASTRA_BLOCK_COMPLETION_CANCELLED     3u
#define ASTRA_BLOCK_COMPLETION_RESET         4u
#define ASTRA_BLOCK_COMPLETION_MEDIA_CHANGED 5u

#define ASTRA_BLOCK_GEOMETRY_SIZE   40u
#define ASTRA_BLOCK_REQUEST_SIZE    32u
#define ASTRA_BLOCK_COMPLETION_SIZE 32u

/* Hard per-service ceilings. Exceeding one is a rejection, never a stall. */
#define ASTRA_BLOCK_MAX_REQUESTS_PER_SERVICE 4u
#define ASTRA_DMA_MAX_BUFFERS_PER_SERVICE    4u
#define ASTRA_DMA_MAX_PAGES_PER_SERVICE      16u

#ifndef __ASSEMBLER__

#include <stdint.h>

typedef struct AstraBlockGeometry {
    uint32_t size;
    uint32_t sector_bytes;
    uint32_t max_transfer_sectors;
    uint32_t capabilities;
    uint32_t state_flags;
    uint32_t media_generation;
    uint32_t host_generation;
    uint32_t reserved;
    uint64_t sector_count;
} AstraBlockGeometry;

typedef struct AstraBlockRequest {
    uint32_t size;
    uint32_t operation;
    uint32_t buffer;        /* transfer-memory handle, never an address */
    uint32_t buffer_offset;
    uint32_t sectors;
    uint32_t reserved;
    uint64_t lba;
} AstraBlockRequest;

typedef struct AstraBlockCompletion {
    uint32_t size;
    uint32_t request;
    uint32_t status;
    uint32_t detail;
    uint32_t sectors;
    uint32_t media_generation;
    uint32_t host_generation;
    uint32_t flags;
} AstraBlockCompletion;

_Static_assert(sizeof(AstraBlockGeometry) == ASTRA_BLOCK_GEOMETRY_SIZE,
               "block geometry ABI size changed");
_Static_assert(sizeof(AstraBlockRequest) == ASTRA_BLOCK_REQUEST_SIZE,
               "block request ABI size changed");
_Static_assert(sizeof(AstraBlockCompletion) == ASTRA_BLOCK_COMPLETION_SIZE,
               "block completion ABI size changed");

#endif

#endif
