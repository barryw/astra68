#ifndef ASTRA_LEASE_BLOCK_H
#define ASTRA_LEASE_BLOCK_H

#include <stdint.h>

#include <astra/block.h>
#include <astra/block_device.h>

/*
 * The block facade backed by an Axiom device lease.
 *
 * The facade is what filesystems talk to and it is deliberately transport
 * ignorant: `sw/userspace/storage` already carries a deterministic memory
 * backend for tests. This is the same interface over the real device, so a
 * filesystem written against the memory backend runs on hardware unchanged.
 *
 * One transfer buffer is owned for the life of the attachment rather than
 * created per request: transfer memory is a hard-capped resource, and a
 * service that allocates per request would fail under exactly the load it
 * needs to survive.
 */
typedef struct AstraLeaseBlock {
    uint32_t device;        /* device lease handle */
    uint32_t irq;           /* completion endpoint handle */
    uint32_t buffer;        /* transfer-memory handle */
    uint32_t buffer_base;   /* mapped address of that memory */
    uint32_t buffer_bytes;
    uint32_t sector_bytes;
    uint32_t max_transfer_sectors;
    uint32_t media_generation;
    uint32_t timeout_ns;    /* per-request deadline, 0 for the default */
} AstraLeaseBlock;

/*
 * Claims transfer memory sized for one maximum transfer and records the
 * device's geometry. Returns an ASTRA_BLOCK_* status; on failure nothing is
 * left claimed.
 */
AstraBlockStatus astra_lease_block_attach(AstraLeaseBlock *lease,
                                          uint32_t device_handle,
                                          uint32_t irq_handle);
void astra_lease_block_detach(AstraLeaseBlock *lease);

/* The backend vtable to hand to astra_block_device_init(). */
const AstraBlockBackend *astra_lease_block_backend(void);

/*
 * Pure translations, separated so they can be tested where syscalls cannot
 * run: a completion status becomes a facade status, and the lease's view of
 * the device becomes the facade's geometry.
 */
AstraBlockStatus astra_lease_block_status(uint32_t completion_status);
void astra_lease_block_geometry(const AstraBlockLeaseInfo *info,
                                AstraBlockGeometry *geometry);

#endif
