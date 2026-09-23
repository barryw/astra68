#include "device.h"

#include "bytes.h"
#include "generation.h"
#include "memory.h"

#include <stddef.h>
#if !defined(__m68k__)
#include <stdlib.h>
#endif

typedef struct KernelDeviceRecord KernelDeviceRecord;

struct KernelDeviceLease {
    KernelDeviceRecord *device;
    uint32_t owner;
    uint32_t device_generation;
    uint16_t references;
    uint8_t state;
    uint8_t reserved;
};

struct KernelDeviceRecord {
    KernelDeviceDefinition definition;
    KernelDeviceLease lease;
    uint32_t generation;
    uint8_t state;
    uint8_t reserved[3];
};

typedef struct KernelDeviceBlock {
    struct KernelDeviceBlock *next;
    uint32_t physical;
    uint16_t used;
    uint16_t capacity;
    KernelDeviceRecord records[];
} KernelDeviceBlock;

#if defined(__m68k__)
_Static_assert(sizeof(KernelDeviceRecord) == 48u,
               "device record size changed; update the memory budget");
_Static_assert(sizeof(KernelDeviceLease) == 16u,
               "device lease size changed; update the memory budget");
#endif

static KernelDeviceBlock *device_blocks;
static KernelDeviceBlock *device_blocks_tail;
static KernelDeviceStats device_stats;
static uint8_t registry_sealed;
static uint8_t initialized;
static uint8_t corrupt;

static KernelDeviceRecord *find_device(uint32_t device_id)
{
    for (KernelDeviceBlock *block = device_blocks; block != NULL;
         block = block->next) {
        for (uint32_t index = 0u; index < block->used; ++index) {
            if (block->records[index].state != KERNEL_DEVICE_UNREGISTERED &&
                block->records[index].definition.device_id == device_id)
                return &block->records[index];
        }
    }
    return NULL;
}

static bool valid_lease(const KernelDeviceLease *lease)
{
    if (lease == NULL || lease->state == KERNEL_DEVICE_LEASE_FREE ||
        lease->device == NULL || &lease->device->lease != lease)
        return false;
    for (const KernelDeviceBlock *block = device_blocks; block != NULL;
         block = block->next) {
        for (uint32_t index = 0u; index < block->used; ++index) {
            if (&block->records[index] == lease->device)
                return true;
        }
    }
    return false;
}

static bool release_lease(KernelDeviceLease *lease)
{
    lease->device = NULL;
    lease->owner = 0u;
    lease->device_generation = 0u;
    lease->references = 0u;
    lease->state = KERNEL_DEVICE_LEASE_FREE;
    lease->reserved = 0u;
    if (device_stats.live_leases == 0u)
        return false;
    --device_stats.live_leases;
    return kernel_allocation_release(KERNEL_ALLOCATION_SITE_DEVICE_LEASE,
                                     1u, sizeof(*lease));
}

static KernelDeviceStatus revoke_lease(KernelDeviceLease *lease)
{
    KernelDeviceRecord *device;

    if (!valid_lease(lease))
        return KERNEL_DEVICE_INVALID_ARGUMENT;
    if (lease->state == KERNEL_DEVICE_LEASE_REVOKED)
        return KERNEL_DEVICE_REVOKED;
    if (lease->state != KERNEL_DEVICE_LEASE_ACTIVE)
        return KERNEL_DEVICE_BUSY;
    device = lease->device;
    lease->state = KERNEL_DEVICE_LEASE_REVOKING;
    device->state = KERNEL_DEVICE_QUIESCING;
    if (!device->definition.quiesce(
            device->definition.device_id, lease->device_generation,
            device->definition.context)) {
        ++device_stats.quiesce_failures;
        device->state = KERNEL_DEVICE_FAILED;
        lease->state = KERNEL_DEVICE_LEASE_REVOKED;
        device->generation = kernel_generation_next(device->generation);
        ++device_stats.revocations;
        return KERNEL_DEVICE_QUIESCE_FAILED;
    }
    device->state = KERNEL_DEVICE_RESETTING;
    if (!device->definition.reset(
            device->definition.device_id, lease->device_generation,
            device->definition.context)) {
        ++device_stats.reset_failures;
        device->state = KERNEL_DEVICE_FAILED;
        lease->state = KERNEL_DEVICE_LEASE_REVOKED;
        device->generation = kernel_generation_next(device->generation);
        ++device_stats.revocations;
        return KERNEL_DEVICE_RESET_FAILED;
    }
    device->generation = kernel_generation_next(device->generation);
    lease->state = KERNEL_DEVICE_LEASE_REVOKED;
    device->state = KERNEL_DEVICE_READY;
    ++device_stats.revocations;
    return KERNEL_DEVICE_OK;
}

static bool discard_device_blocks(void)
{
#if defined(__m68k__)
    while (device_blocks != NULL) {
        KernelDeviceBlock *block = device_blocks;

        device_blocks = block->next;
        if (kernel_memory_release(block->physical, 1u,
                                  KERNEL_OWNER_CORE) != KERNEL_MEMORY_OK)
            return false;
    }
#else
    uint32_t blocks = 0u;
    KernelAllocationStats stats;

    while (device_blocks != NULL) {
        KernelDeviceBlock *block = device_blocks;

        device_blocks = block->next;
        free(block);
        ++blocks;
    }
    if (blocks != 0u &&
        kernel_allocation_site_stats(
            KERNEL_ALLOCATION_SITE_DEVICE_METADATA, &stats) &&
        stats.current_units != 0u &&
        !kernel_allocation_release(KERNEL_ALLOCATION_SITE_DEVICE_METADATA,
                                   blocks, blocks * KERNEL_PAGE_SIZE))
        return false;
#endif
    device_blocks_tail = NULL;
    return true;
}

static KernelDeviceBlock *allocate_device_block(void)
{
    KernelDeviceBlock *block;

#if defined(__m68k__)
    uint32_t physical;

    if (kernel_memory_alloc_zeroed_tagged(
            KERNEL_ALLOCATION_SITE_DEVICE_METADATA, 1u, 1u,
            KERNEL_FRAME_KERNEL, KERNEL_OWNER_CORE, &physical) !=
        KERNEL_MEMORY_OK)
        return NULL;
    block = kernel_memory_access(physical, KERNEL_PAGE_SIZE);
    if (block == NULL) {
        (void)kernel_memory_release(physical, 1u, KERNEL_OWNER_CORE);
        return NULL;
    }
    block->physical = physical;
#else
    if (!kernel_allocation_attempt(KERNEL_ALLOCATION_SITE_DEVICE_METADATA,
                                   KERNEL_OWNER_CORE))
        return NULL;
    block = calloc(1u, KERNEL_PAGE_SIZE);
    if (block == NULL ||
        !kernel_allocation_commit(KERNEL_ALLOCATION_SITE_DEVICE_METADATA,
                                  1u, KERNEL_PAGE_SIZE,
                                  KERNEL_OWNER_CORE)) {
        free(block);
        return NULL;
    }
#endif
    block->capacity = (uint16_t)((KERNEL_PAGE_SIZE - sizeof(*block)) /
                                 sizeof(block->records[0]));
    if (block->capacity == 0u) {
#if defined(__m68k__)
        (void)kernel_memory_release(block->physical, 1u, KERNEL_OWNER_CORE);
#else
        (void)kernel_allocation_release(
            KERNEL_ALLOCATION_SITE_DEVICE_METADATA, 1u, KERNEL_PAGE_SIZE);
        free(block);
#endif
        return NULL;
    }
    if (device_blocks_tail != NULL)
        device_blocks_tail->next = block;
    else
        device_blocks = block;
    device_blocks_tail = block;
    return block;
}

bool kernel_device_init(void)
{
    initialized = 0u;
    registry_sealed = 0u;
    corrupt = 0u;
    if (!discard_device_blocks())
        return false;
    kernel_bytes_clear(&device_stats, sizeof(device_stats));
    initialized = 1u;
    return true;
}

KernelDeviceStatus kernel_device_register(
    const KernelDeviceDefinition *definition)
{
    KernelDeviceBlock *block;
    KernelDeviceRecord *available;

    if (!initialized || registry_sealed != 0u || definition == NULL ||
        definition->device_id == 0u || definition->class_id == 0u ||
        definition->quiesce == NULL || definition->reset == NULL)
        return KERNEL_DEVICE_INVALID_ARGUMENT;
    if (find_device(definition->device_id) != NULL)
        return KERNEL_DEVICE_BUSY;
    block = device_blocks_tail;
    if (block == NULL || block->used == block->capacity)
        block = allocate_device_block();
    if (block == NULL) {
        ++device_stats.allocation_failures;
        return KERNEL_DEVICE_NO_SLOT;
    }
    available = &block->records[block->used++];
    kernel_bytes_copy(&available->definition, definition,
                      sizeof(available->definition));
    available->lease.device = NULL;
    available->lease.state = KERNEL_DEVICE_LEASE_FREE;
    available->generation = 1u;
    available->state = KERNEL_DEVICE_READY;
    ++device_stats.registered_devices;
    return KERNEL_DEVICE_OK;
}

bool kernel_device_seal_registry(void)
{
    if (!initialized || registry_sealed != 0u)
        return false;
    registry_sealed = 1u;
    return true;
}

KernelDeviceStatus kernel_device_acquire(uint32_t owner, uint32_t device_id,
                                         KernelDeviceLease **lease)
{
    KernelDeviceRecord *device;
    KernelDeviceLease *claimed;

    if (!initialized || registry_sealed == 0u || owner == 0u ||
        device_id == 0u || lease == NULL)
        return KERNEL_DEVICE_INVALID_ARGUMENT;
    *lease = NULL;
    device = find_device(device_id);
    if (device == NULL)
        return KERNEL_DEVICE_NOT_FOUND;
    if (device->state != KERNEL_DEVICE_READY ||
        device->lease.state != KERNEL_DEVICE_LEASE_FREE) {
        ++device_stats.busy_failures;
        return KERNEL_DEVICE_BUSY;
    }
    if (!kernel_allocation_attempt(KERNEL_ALLOCATION_SITE_DEVICE_LEASE,
                                   owner)) {
        ++device_stats.allocation_failures;
        return KERNEL_DEVICE_NO_SLOT;
    }
    if (!kernel_allocation_commit(KERNEL_ALLOCATION_SITE_DEVICE_LEASE,
                                  1u, sizeof(device->lease), owner)) {
        corrupt = 1u;
        return KERNEL_DEVICE_CORRUPT;
    }
    claimed = &device->lease;
    claimed->device = device;
    claimed->owner = owner;
    claimed->device_generation = device->generation;
    claimed->references = 1u;
    claimed->state = KERNEL_DEVICE_LEASE_ACTIVE;
    claimed->reserved = 0u;
    device->state = KERNEL_DEVICE_LEASED;
    ++device_stats.acquisitions;
    ++device_stats.live_leases;
    if (device_stats.live_leases > device_stats.max_live_leases)
        device_stats.max_live_leases = device_stats.live_leases;
    *lease = claimed;
    return KERNEL_DEVICE_OK;
}

bool kernel_device_handle_retain(void *object, void *context)
{
    KernelDeviceLease *lease = object;

    (void)context;
    if (!valid_lease(lease) || lease->references == UINT16_MAX)
        return false;
    ++lease->references;
    return true;
}

void kernel_device_handle_release(void *object, void *context)
{
    KernelDeviceLease *lease = object;
    KernelDeviceStatus status;

    (void)context;
    if (!valid_lease(lease) || lease->references == 0u) {
        corrupt = 1u;
        return;
    }
    --lease->references;
    if (lease->references != 0u)
        return;
    if (lease->state == KERNEL_DEVICE_LEASE_ACTIVE) {
        status = revoke_lease(lease);
        if (status == KERNEL_DEVICE_CORRUPT) {
            corrupt = 1u;
            return;
        }
    }
    if (!release_lease(lease))
        corrupt = 1u;
}

void kernel_device_abandon_unpublished(KernelDeviceLease *lease)
{
    kernel_device_handle_release(lease, NULL);
}

KernelDeviceStatus kernel_device_query(const KernelDeviceLease *lease,
                                       KernelDeviceSnapshot *snapshot)
{
    if (!valid_lease(lease) || snapshot == NULL)
        return KERNEL_DEVICE_INVALID_ARGUMENT;
    snapshot->device_id = lease->device->definition.device_id;
    snapshot->class_id = lease->device->definition.class_id;
    snapshot->capabilities = lease->device->definition.capabilities;
    snapshot->generation = lease->device_generation;
    snapshot->owner = lease->owner;
    snapshot->references = lease->references;
    snapshot->device_state = lease->device->state;
    snapshot->lease_state = lease->state;
    return lease->state == KERNEL_DEVICE_LEASE_REVOKED ?
        KERNEL_DEVICE_REVOKED : KERNEL_DEVICE_OK;
}

KernelDeviceStatus kernel_device_reset(KernelDeviceLease *lease)
{
    KernelDeviceRecord *device;

    if (!valid_lease(lease))
        return KERNEL_DEVICE_INVALID_ARGUMENT;
    if (lease->state != KERNEL_DEVICE_LEASE_ACTIVE)
        return KERNEL_DEVICE_REVOKED;
    device = lease->device;
    device->state = KERNEL_DEVICE_RESETTING;
    if (!device->definition.reset(device->definition.device_id,
                                  lease->device_generation,
                                  device->definition.context)) {
        ++device_stats.reset_failures;
        device->state = KERNEL_DEVICE_FAILED;
        return KERNEL_DEVICE_RESET_FAILED;
    }
    device->generation = kernel_generation_next(device->generation);
    lease->device_generation = device->generation;
    device->state = KERNEL_DEVICE_LEASED;
    return KERNEL_DEVICE_OK;
}

KernelDeviceStatus kernel_device_revoke(KernelDeviceLease *lease)
{
    return revoke_lease(lease);
}

KernelDeviceStatus kernel_device_owner_died(uint32_t owner,
                                            uint32_t *revoked_leases)
{
    uint32_t revoked = 0u;
    KernelDeviceStatus result = KERNEL_DEVICE_OK;
    bool owned = false;

    if (owner == 0u)
        return KERNEL_DEVICE_INVALID_ARGUMENT;
    for (KernelDeviceBlock *block = device_blocks; block != NULL;
         block = block->next) {
        for (uint32_t index = 0u; index < block->used; ++index) {
            KernelDeviceLease *lease = &block->records[index].lease;
            KernelDeviceStatus status;

            if (lease->state != KERNEL_DEVICE_LEASE_ACTIVE ||
                lease->owner != owner)
                continue;
            owned = true;
            if (lease->references != 1u)
                continue;
            status = revoke_lease(lease);
            if (status != KERNEL_DEVICE_OK && result == KERNEL_DEVICE_OK)
                result = status;
            ++revoked;
        }
    }
    if (owned)
        ++device_stats.owner_deaths;
    if (revoked_leases != NULL)
        *revoked_leases = revoked;
    return result;
}

bool kernel_device_stats(KernelDeviceStats *stats)
{
    if (!initialized || stats == NULL)
        return false;
    kernel_bytes_copy(stats, &device_stats, sizeof(*stats));
    return true;
}

bool kernel_device_pool_valid(void)
{
    KernelAllocationStats metadata;
    KernelAllocationStats leases;
    uint32_t blocks = 0u;
    uint32_t registered = 0u;
    uint32_t live = 0u;

    if (!initialized || corrupt != 0u)
        return false;
    for (KernelDeviceBlock *block = device_blocks; block != NULL;
         block = block->next) {
        ++blocks;
        if (block->capacity == 0u || block->used > block->capacity ||
            (block->next == NULL) != (block == device_blocks_tail))
            return false;
        for (uint32_t index = 0u; index < block->used; ++index) {
            KernelDeviceRecord *device = &block->records[index];
            KernelDeviceLease *lease = &device->lease;

            ++registered;
            if (device->state == KERNEL_DEVICE_UNREGISTERED ||
                device->definition.device_id == 0u ||
                device->definition.class_id == 0u ||
                device->definition.quiesce == NULL ||
                device->definition.reset == NULL)
                return false;
            if (lease->state == KERNEL_DEVICE_LEASE_FREE) {
                if (lease->device != NULL || lease->owner != 0u ||
                    lease->references != 0u ||
                    device->state == KERNEL_DEVICE_LEASED ||
                    device->state == KERNEL_DEVICE_QUIESCING ||
                    device->state == KERNEL_DEVICE_RESETTING)
                    return false;
                continue;
            }
            if (!valid_lease(lease) || lease->references == 0u)
                return false;
            ++live;
        }
    }
    return registered == device_stats.registered_devices &&
           live == device_stats.live_leases &&
           kernel_allocation_site_stats(
               KERNEL_ALLOCATION_SITE_DEVICE_METADATA, &metadata) &&
           metadata.current_units == blocks &&
           metadata.current_bytes == blocks * KERNEL_PAGE_SIZE &&
           kernel_allocation_site_stats(
               KERNEL_ALLOCATION_SITE_DEVICE_LEASE, &leases) &&
           leases.current_units == live &&
           leases.current_bytes == live * sizeof(KernelDeviceLease);
}
