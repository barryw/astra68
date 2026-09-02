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
 * One lane is provisioned for every request the device advertises. Each lane
 * owns its transfer buffer and completion event, so synchronous callers may
 * sleep independently while the physical backend keeps the whole queue busy.
 * All resources are claimed at attach time; the request path allocates none.
 */
typedef struct AstraLeaseBlockLane {
    uint32_t buffer;
    uint32_t buffer_base;
    uint32_t buffer_bytes;
    uint32_t event;
    uint32_t request;
    AstraBlockCompletion completion;
    uint32_t collect_status;
    uint8_t in_use;
    uint8_t active;
    uint8_t completed;
    uint8_t reserved;
} AstraLeaseBlockLane;

typedef struct AstraLeaseBlock {
    uint32_t device;        /* device lease handle */
    uint32_t irq;           /* completion endpoint handle */
    uint32_t queue_depth;
    uint32_t available;
    uint32_t state_lock;
    uint32_t completion_lock;
    uint32_t sector_bytes;
    uint32_t max_transfer_sectors;
    uint32_t media_generation;
    /*
     * Whether the completion endpoint is currently armed. A granted endpoint
     * starts masked and must be armed before a request is submitted, but
     * arming one that is already armed is refused. The first request used to
     * leave it armed — its wait loop re-arms, and the collect that follows
     * consumes no record — so the second request on the same attachment failed
     * at its first call. Nothing did two transfers on one lease until a
     * filesystem did.
     */
    uint32_t armed;
    /*
     * Where the last refused request gave up, and with which syscall status.
     *
     * Seven different things in this file answer ASTRA_BLOCK_IO_ERROR, lwext4
     * has one errno for all of them, and a person is shown "I/O error" -- which
     * names the layer that gave up rather than what went wrong. A completion
     * that could not be acknowledged and a device that refused a submission
     * call for entirely different things, and neither is legible from here.
     *
     * The site is an AstraLeaseBlockSite; the status is the syscall's own.
     */
    uint32_t last_site;
    uint32_t last_status;
    AstraLeaseBlockLane lanes[ASTRA_BLOCK_MAX_REQUESTS_PER_SERVICE];
} AstraLeaseBlock;

/* Where in a request's life it was refused. Ordered as the request runs. */
typedef enum AstraLeaseBlockSite {
    ASTRA_LEASE_BLOCK_SITE_NONE = 0,
    ASTRA_LEASE_BLOCK_SITE_DRAIN_BEFORE_ARM = 1,
    ASTRA_LEASE_BLOCK_SITE_ARM = 2,
    ASTRA_LEASE_BLOCK_SITE_SUBMIT = 3,
    ASTRA_LEASE_BLOCK_SITE_COLLECT = 4,
    ASTRA_LEASE_BLOCK_SITE_DRAIN_AFTER_COLLECT = 5,
    ASTRA_LEASE_BLOCK_SITE_SHORT_TRANSFER = 6,
    ASTRA_LEASE_BLOCK_SITE_WAIT = 7,
    ASTRA_LEASE_BLOCK_SITE_RESET = 8
} AstraLeaseBlockSite;

/*
 * Where the last refused request gave up, packed as (site * 256) + status, or
 * zero if none has been. One number because it travels through a terminal line
 * and a person reads it back to this enum.
 */
uint32_t astra_lease_block_last_failure(const AstraLeaseBlock *lease);

/*
 * Claims the advertised lane count, each sized for one maximum transfer, and
 * records the device's geometry. Returns an ASTRA_BLOCK_* status; on failure
 * nothing is left claimed.
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
