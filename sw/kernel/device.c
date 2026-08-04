#include "device.h"

#include "bytes.h"
#include "generation.h"
#include "object_cache.h"

#include <stddef.h>

typedef struct KernelDeviceRecord {
    KernelDeviceDefinition definition;
    KernelDeviceLease *lease;
    uint32_t generation;
    uint8_t state;
    uint8_t reserved[3];
} KernelDeviceRecord;

struct KernelDeviceLease {
    KernelDeviceRecord *device;
    uint32_t owner;
    uint32_t device_generation;
    uint16_t references;
    uint8_t state;
    uint8_t reserved;
};

#if defined(__m68k__)
_Static_assert(sizeof(KernelDeviceRecord) == 36u,
               "device record size changed; update the memory budget");
_Static_assert(sizeof(KernelDeviceLease) == 16u,
               "device lease size changed; update the memory budget");
#endif

static KernelDeviceRecord devices[KERNEL_DEVICE_MAX];
static KernelDeviceLease leases[KERNEL_DEVICE_LEASE_MAX];
static KernelObjectCache lease_cache;
static uint32_t lease_cache_bitmap[
    KERNEL_OBJECT_CACHE_BITMAP_WORDS(KERNEL_DEVICE_LEASE_MAX)];
static KernelDeviceStats device_stats;
static uint8_t registry_sealed;
static uint8_t initialized;
static uint8_t corrupt;

static KernelDeviceRecord *find_device(uint32_t device_id)
{
    for (uint32_t index = 0u; index < KERNEL_DEVICE_MAX; ++index) {
        if (devices[index].state != KERNEL_DEVICE_UNREGISTERED &&
            devices[index].definition.device_id == device_id)
            return &devices[index];
    }
    return NULL;
}

static bool valid_lease(const KernelDeviceLease *lease)
{
    if (lease == NULL || lease < leases ||
        lease >= leases + KERNEL_DEVICE_LEASE_MAX ||
        lease->state == KERNEL_DEVICE_LEASE_FREE || lease->device == NULL)
        return false;
    return lease->device >= devices &&
           lease->device < devices + KERNEL_DEVICE_MAX &&
           lease->device->lease == lease;
}

static uint32_t owner_lease_count(uint32_t owner)
{
    uint32_t count = 0u;

    for (uint32_t index = 0u; index < KERNEL_DEVICE_LEASE_MAX; ++index) {
        if (leases[index].state != KERNEL_DEVICE_LEASE_FREE &&
            leases[index].owner == owner)
            ++count;
    }
    return count;
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
    return kernel_object_cache_release(&lease_cache, lease) ==
           KERNEL_OBJECT_CACHE_OK;
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

bool kernel_device_init(void)
{
    initialized = 0u;
    registry_sealed = 0u;
    corrupt = 0u;
    for (uint32_t index = 0u; index < KERNEL_DEVICE_MAX; ++index) {
        kernel_bytes_clear(&devices[index], sizeof(devices[index]));
    }
    for (uint32_t index = 0u; index < KERNEL_DEVICE_LEASE_MAX; ++index) {
        leases[index].device = NULL;
        leases[index].owner = 0u;
        leases[index].device_generation = 0u;
        leases[index].references = 0u;
        leases[index].state = KERNEL_DEVICE_LEASE_FREE;
        leases[index].reserved = 0u;
    }
    kernel_bytes_clear(&device_stats, sizeof(device_stats));
    if (!kernel_object_cache_init(
            &lease_cache, leases, sizeof(leases[0]), KERNEL_DEVICE_LEASE_MAX,
            lease_cache_bitmap,
            KERNEL_OBJECT_CACHE_BITMAP_WORDS(KERNEL_DEVICE_LEASE_MAX),
            KERNEL_ALLOCATION_SITE_DEVICE_LEASE))
        return false;
    initialized = 1u;
    return true;
}

KernelDeviceStatus kernel_device_register(
    const KernelDeviceDefinition *definition)
{
    KernelDeviceRecord *available = NULL;

    if (!initialized || registry_sealed != 0u || definition == NULL ||
        definition->device_id == 0u || definition->class_id == 0u ||
        definition->quiesce == NULL || definition->reset == NULL)
        return KERNEL_DEVICE_INVALID_ARGUMENT;
    if (find_device(definition->device_id) != NULL)
        return KERNEL_DEVICE_BUSY;
    for (uint32_t index = 0u; index < KERNEL_DEVICE_MAX; ++index) {
        if (devices[index].state == KERNEL_DEVICE_UNREGISTERED) {
            available = &devices[index];
            break;
        }
    }
    if (available == NULL)
        return KERNEL_DEVICE_NO_SLOT;
    kernel_bytes_copy(&available->definition, definition,
                      sizeof(available->definition));
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
    void *raw;
    uint16_t slot;
    KernelObjectCacheStatus status;

    if (!initialized || registry_sealed == 0u || owner == 0u ||
        device_id == 0u || lease == NULL)
        return KERNEL_DEVICE_INVALID_ARGUMENT;
    *lease = NULL;
    device = find_device(device_id);
    if (device == NULL)
        return KERNEL_DEVICE_NOT_FOUND;
    if (device->state != KERNEL_DEVICE_READY || device->lease != NULL) {
        ++device_stats.busy_failures;
        return KERNEL_DEVICE_BUSY;
    }
    if (owner_lease_count(owner) >= KERNEL_DEVICE_LEASE_OWNER_MAX) {
        ++device_stats.quota_failures;
        return KERNEL_DEVICE_QUOTA_EXCEEDED;
    }
    status = kernel_object_cache_claim(&lease_cache, owner, &raw, &slot);
    if (status == KERNEL_OBJECT_CACHE_UNAVAILABLE) {
        ++device_stats.allocation_failures;
        return KERNEL_DEVICE_NO_SLOT;
    }
    if (status != KERNEL_OBJECT_CACHE_OK || raw == NULL ||
        slot >= KERNEL_DEVICE_LEASE_MAX) {
        corrupt = 1u;
        return KERNEL_DEVICE_CORRUPT;
    }
    claimed = raw;
    claimed->device = device;
    claimed->owner = owner;
    claimed->device_generation = device->generation;
    claimed->references = 1u;
    claimed->state = KERNEL_DEVICE_LEASE_ACTIVE;
    claimed->reserved = 0u;
    device->lease = claimed;
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
    KernelDeviceRecord *device;
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
    device = lease->device;
    device->lease = NULL;
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

    if (owner == 0u)
        return KERNEL_DEVICE_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index < KERNEL_DEVICE_LEASE_MAX; ++index) {
        KernelDeviceLease *lease = &leases[index];
        KernelDeviceStatus status;

        if (lease->state != KERNEL_DEVICE_LEASE_ACTIVE ||
            lease->owner != owner)
            continue;
        status = revoke_lease(lease);
        if (status != KERNEL_DEVICE_OK && result == KERNEL_DEVICE_OK)
            result = status;
        ++revoked;
    }
    if (revoked != 0u)
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
    uint32_t registered = 0u;
    uint32_t live = 0u;

    if (!initialized || corrupt != 0u ||
        !kernel_object_cache_valid(&lease_cache))
        return false;
    for (uint32_t index = 0u; index < KERNEL_DEVICE_MAX; ++index) {
        KernelDeviceRecord *device = &devices[index];

        if (device->state == KERNEL_DEVICE_UNREGISTERED)
            continue;
        ++registered;
        if (device->definition.device_id == 0u ||
            device->definition.class_id == 0u ||
            device->definition.quiesce == NULL ||
            device->definition.reset == NULL)
            return false;
        if (device->state == KERNEL_DEVICE_READY && device->lease != NULL)
            return false;
        if ((device->state == KERNEL_DEVICE_LEASED ||
             device->state == KERNEL_DEVICE_QUIESCING ||
             device->state == KERNEL_DEVICE_RESETTING) &&
            device->lease == NULL)
            return false;
    }
    for (uint32_t index = 0u; index < KERNEL_DEVICE_LEASE_MAX; ++index) {
        if (leases[index].state == KERNEL_DEVICE_LEASE_FREE)
            continue;
        if (!valid_lease(&leases[index]) || leases[index].references == 0u)
            return false;
        ++live;
    }
    return registered == device_stats.registered_devices &&
           live == device_stats.live_leases;
}
