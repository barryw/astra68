#ifndef ASTRA_KERNEL_DEVICE_H
#define ASTRA_KERNEL_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_DEVICE_MAX 16u
#define KERNEL_DEVICE_LEASE_MAX 16u
/*
 * Leases one process may hold at once. Two was enough while a service owned a
 * single device; the initial image holds the block device, the keyboard and
 * the screen until those are separate services, which is three. Four bounds it
 * at the number of device classes that exist rather than at what today needs,
 * and costs nothing: leases come from the shared pool above, so this is a
 * policy ceiling and not an array size.
 */
#define KERNEL_DEVICE_LEASE_OWNER_MAX 4u

#define KERNEL_DEVICE_RIGHT_QUERY      (1u << 0)
#define KERNEL_DEVICE_RIGHT_TRANSFER   (1u << 5)
#define KERNEL_DEVICE_RIGHT_ADMINISTER (1u << 6)
#define KERNEL_DEVICE_RIGHTS \
    (KERNEL_DEVICE_RIGHT_QUERY | KERNEL_DEVICE_RIGHT_TRANSFER | \
     KERNEL_DEVICE_RIGHT_ADMINISTER)

typedef enum KernelDeviceState {
    KERNEL_DEVICE_UNREGISTERED = 0,
    KERNEL_DEVICE_READY,
    KERNEL_DEVICE_LEASED,
    KERNEL_DEVICE_QUIESCING,
    KERNEL_DEVICE_RESETTING,
    KERNEL_DEVICE_FAILED
} KernelDeviceState;

typedef enum KernelDeviceLeaseState {
    KERNEL_DEVICE_LEASE_FREE = 0,
    KERNEL_DEVICE_LEASE_ACTIVE,
    KERNEL_DEVICE_LEASE_REVOKING,
    KERNEL_DEVICE_LEASE_REVOKED
} KernelDeviceLeaseState;

typedef enum KernelDeviceStatus {
    KERNEL_DEVICE_OK = 0,
    KERNEL_DEVICE_INVALID_ARGUMENT,
    KERNEL_DEVICE_NOT_FOUND,
    KERNEL_DEVICE_BUSY,
    KERNEL_DEVICE_NO_SLOT,
    KERNEL_DEVICE_QUOTA_EXCEEDED,
    KERNEL_DEVICE_REVOKED,
    KERNEL_DEVICE_QUIESCE_FAILED,
    KERNEL_DEVICE_RESET_FAILED,
    KERNEL_DEVICE_CORRUPT
} KernelDeviceStatus;

typedef struct KernelDeviceLease KernelDeviceLease;

typedef bool (*KernelDeviceQuiesce)(uint32_t device_id, uint32_t generation,
                                    void *context);
typedef bool (*KernelDeviceReset)(uint32_t device_id, uint32_t generation,
                                  void *context);

typedef struct KernelDeviceDefinition {
    KernelDeviceQuiesce quiesce;
    KernelDeviceReset reset;
    void *context;
    uint32_t device_id;
    uint32_t class_id;
    uint32_t capabilities;
} KernelDeviceDefinition;

typedef struct KernelDeviceSnapshot {
    uint32_t device_id;
    uint32_t class_id;
    uint32_t capabilities;
    uint32_t generation;
    uint32_t owner;
    uint16_t references;
    uint8_t device_state;
    uint8_t lease_state;
} KernelDeviceSnapshot;

typedef struct KernelDeviceStats {
    uint32_t registered_devices;
    uint32_t live_leases;
    uint32_t max_live_leases;
    uint32_t acquisitions;
    uint32_t busy_failures;
    uint32_t quota_failures;
    uint32_t allocation_failures;
    uint32_t revocations;
    uint32_t owner_deaths;
    uint32_t quiesce_failures;
    uint32_t reset_failures;
} KernelDeviceStats;

bool kernel_device_init(void);
KernelDeviceStatus kernel_device_register(
    const KernelDeviceDefinition *definition);
bool kernel_device_seal_registry(void);
KernelDeviceStatus kernel_device_acquire(uint32_t owner, uint32_t device_id,
                                         KernelDeviceLease **lease);
bool kernel_device_handle_retain(void *object, void *context);
void kernel_device_handle_release(void *object, void *context);
void kernel_device_abandon_unpublished(KernelDeviceLease *lease);
KernelDeviceStatus kernel_device_query(const KernelDeviceLease *lease,
                                       KernelDeviceSnapshot *snapshot);
KernelDeviceStatus kernel_device_reset(KernelDeviceLease *lease);
KernelDeviceStatus kernel_device_revoke(KernelDeviceLease *lease);
KernelDeviceStatus kernel_device_owner_died(uint32_t owner,
                                            uint32_t *revoked_leases);
bool kernel_device_stats(KernelDeviceStats *stats);
bool kernel_device_pool_valid(void);

#endif
