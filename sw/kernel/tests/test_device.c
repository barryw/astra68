#include "allocation.h"
#include "device.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct FakeDevice {
    uint32_t quiesces;
    uint32_t resets;
    bool quiesce_ok;
    bool reset_ok;
} FakeDevice;

static bool quiesce(uint32_t id, uint32_t generation, void *context)
{
    FakeDevice *device = context;

    assert(id != 0u && generation != 0u);
    ++device->quiesces;
    return device->quiesce_ok;
}

static bool reset(uint32_t id, uint32_t generation, void *context)
{
    FakeDevice *device = context;

    assert(id != 0u && generation != 0u);
    ++device->resets;
    return device->reset_ok;
}

static KernelDeviceDefinition define(uint32_t id, uint32_t class_id,
                                     FakeDevice *device)
{
    KernelDeviceDefinition value = {
        quiesce, reset, device, id, class_id, id << 8
    };

    return value;
}

static void initialize(void)
{
    kernel_allocation_init();
    assert(kernel_device_init());
}

static void test_lifecycle(void)
{
    FakeDevice fake = {0u, 0u, true, true};
    KernelDeviceDefinition definition = define(1u, 10u, &fake);
    KernelDeviceLease *lease = NULL;
    KernelDeviceLease *contender = NULL;
    KernelDeviceSnapshot snapshot;
    KernelDeviceStats stats;

    initialize();
    assert(kernel_device_register(&definition) == KERNEL_DEVICE_OK);
    assert(kernel_device_register(&definition) == KERNEL_DEVICE_BUSY);
    assert(kernel_device_seal_registry());
    assert(kernel_device_acquire(7u, 1u, &lease) == KERNEL_DEVICE_OK);
    assert(kernel_device_acquire(8u, 1u, &contender) == KERNEL_DEVICE_BUSY);
    assert(contender == NULL);
    assert(kernel_device_query(lease, &snapshot) == KERNEL_DEVICE_OK);
    assert(snapshot.device_id == 1u && snapshot.class_id == 10u);
    assert(snapshot.capabilities == 0x100u && snapshot.owner == 7u);
    assert(snapshot.generation == 1u && snapshot.references == 1u);
    assert(kernel_device_handle_retain(lease, NULL));
    kernel_device_handle_release(lease, NULL);
    assert(kernel_device_reset(lease) == KERNEL_DEVICE_OK);
    assert(fake.resets == 1u);
    assert(kernel_device_query(lease, &snapshot) == KERNEL_DEVICE_OK);
    assert(snapshot.generation == 2u);
    assert(kernel_device_revoke(lease) == KERNEL_DEVICE_OK);
    assert(fake.quiesces == 1u && fake.resets == 2u);
    assert(kernel_device_query(lease, &snapshot) == KERNEL_DEVICE_REVOKED);
    kernel_device_handle_release(lease, NULL);
    assert(kernel_device_acquire(8u, 1u, &lease) == KERNEL_DEVICE_OK);
    assert(kernel_device_query(lease, &snapshot) == KERNEL_DEVICE_OK);
    assert(snapshot.generation == 3u);
    kernel_device_handle_release(lease, NULL);
    assert(kernel_device_stats(&stats));
    assert(stats.live_leases == 0u && stats.max_live_leases == 1u);
    assert(stats.acquisitions == 2u && stats.busy_failures == 1u);
    assert(stats.revocations == 2u);
    assert(kernel_device_pool_valid());
}

static void test_quota_and_owner_death(void)
{
    FakeDevice fake[3] = {
        {0u, 0u, true, true}, {0u, 0u, true, true},
        {0u, 0u, true, true}
    };
    KernelDeviceLease *lease[3] = {NULL, NULL, NULL};
    uint32_t revoked = 0u;

    initialize();
    for (uint32_t index = 0u; index < 3u; ++index) {
        KernelDeviceDefinition value = define(index + 1u, 20u, &fake[index]);

        assert(kernel_device_register(&value) == KERNEL_DEVICE_OK);
    }
    assert(kernel_device_seal_registry());
    assert(kernel_device_acquire(4u, 1u, &lease[0]) == KERNEL_DEVICE_OK);
    assert(kernel_device_acquire(4u, 2u, &lease[1]) == KERNEL_DEVICE_OK);
    assert(kernel_device_acquire(4u, 3u, &lease[2]) ==
           KERNEL_DEVICE_QUOTA_EXCEEDED);
    assert(kernel_device_owner_died(4u, &revoked) == KERNEL_DEVICE_OK);
    assert(revoked == 2u);
    for (uint32_t index = 0u; index < 2u; ++index) {
        assert(fake[index].quiesces == 1u && fake[index].resets == 1u);
        kernel_device_handle_release(lease[index], NULL);
    }
    assert(kernel_device_pool_valid());
}

static void test_failures(void)
{
    FakeDevice fake = {0u, 0u, false, true};
    KernelDeviceDefinition definition = define(1u, 30u, &fake);
    KernelDeviceLease *lease = NULL;
    KernelDeviceStats stats;

    initialize();
    assert(kernel_device_register(&definition) == KERNEL_DEVICE_OK);
    assert(kernel_device_seal_registry());
    kernel_allocation_test_fail_site(KERNEL_ALLOCATION_SITE_DEVICE_LEASE, 1u);
    assert(kernel_device_acquire(9u, 1u, &lease) == KERNEL_DEVICE_NO_SLOT);
    kernel_allocation_test_clear_failure();
    assert(kernel_device_acquire(9u, 1u, &lease) == KERNEL_DEVICE_OK);
    assert(kernel_device_revoke(lease) == KERNEL_DEVICE_QUIESCE_FAILED);
    assert(fake.quiesces == 1u && fake.resets == 0u);
    kernel_device_handle_release(lease, NULL);
    assert(kernel_device_acquire(10u, 1u, &lease) == KERNEL_DEVICE_BUSY);
    assert(kernel_device_stats(&stats));
    assert(stats.allocation_failures == 1u);
    assert(stats.quiesce_failures == 1u && stats.revocations == 1u);
    assert(kernel_device_pool_valid());
}

static void test_reset_failure_is_contained(void)
{
    FakeDevice fake = {0u, 0u, true, false};
    KernelDeviceDefinition definition = define(1u, 40u, &fake);
    KernelDeviceLease *lease = NULL;
    KernelDeviceStats stats;

    initialize();
    assert(kernel_device_register(&definition) == KERNEL_DEVICE_OK);
    assert(kernel_device_seal_registry());
    assert(kernel_device_acquire(11u, 1u, &lease) == KERNEL_DEVICE_OK);
    assert(kernel_device_reset(lease) == KERNEL_DEVICE_RESET_FAILED);
    assert(fake.resets == 1u);
    assert(kernel_device_pool_valid());
    assert(kernel_device_owner_died(11u, NULL) ==
           KERNEL_DEVICE_RESET_FAILED);
    assert(fake.quiesces == 1u && fake.resets == 2u);
    assert(kernel_device_pool_valid());
    kernel_device_handle_release(lease, NULL);
    assert(kernel_device_stats(&stats));
    assert(stats.live_leases == 0u && stats.owner_deaths == 1u);
    assert(stats.reset_failures == 2u);
    assert(kernel_device_pool_valid());
}

int main(void)
{
    test_lifecycle();
    test_quota_and_owner_death();
    test_failures();
    test_reset_failure_is_contained();
    puts("device lease tests passed");
    return 0;
}
