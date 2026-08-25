#include <astra/alloc.h>
#include <astra/block.h>
#include <astra/block_device.h>
#include <astra/bytes.h>
#include <astra/ext4_alloc.h>
#include <astra/ext4_port.h>
#include <astra/lease_block.h>
#include <astra/mbr.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/ext4_time.h>
#include <astra/vfs_ext4_backend.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_service_core.h>

#include <ext4.h>

#define MOUNT_POINT "/vol/"
#define DEVICE_NAME "astra"
#define PORT_MESSAGES 2u

enum {
    STORAGE_FAIL_ATTACH = ASTRA_STATUS_PROGRAM_FIRST,
    STORAGE_FAIL_QUERY,
    STORAGE_FAIL_MBR,
    STORAGE_FAIL_ALLOCATOR,
    STORAGE_FAIL_PORT,
    STORAGE_FAIL_REGISTER,
    STORAGE_FAIL_MOUNT,
    STORAGE_FAIL_RECOVER,
    STORAGE_FAIL_JOURNAL
};

ASTRA_PROGRAM("storage", 0, 1, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static AstraLeaseBlock lease;
static AstraBlockDevice block;
static AstraAllocScalar arena[ASTRA_EXT4_ARENA_BYTES /
                              sizeof(AstraAllocScalar)];
static AstraAllocator allocator;
static AstraExt4Port ext4_port;
static AstraVfsExt4Backend backend;
static AstraVfsService service;
static AstraVfsPortService port;
static uint8_t sector[ASTRA_BLOCK_SECTOR_BYTES];

static uint64_t service_clock(void *context)
{
    (void)context;
    return astra_clock_monotonic();
}

static const AstraStartupCapability *
capability(const AstraStartupInfo *startup, const char *name)
{
    const AstraStartupCapability *entries;

    if (startup == NULL || startup->capabilities_address == 0u)
        return NULL;
    entries = (const AstraStartupCapability *)(uintptr_t)
        startup->capabilities_address;
    for (uint32_t index = 0u; index < startup->capability_count; ++index) {
        if (astra_capability_name_equal(entries[index].name, name))
            return &entries[index];
    }
    return NULL;
}

static void ready(uint32_t handle, uint32_t status, uint32_t service_handle)
{
    AstraServiceReady message;
    uint32_t carried[1];

    (void)memset(&message, 0, sizeof(message));
    message.header.total_size = sizeof(message);
    message.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    message.header.protocol = ASTRA_SERVICE_PROTOCOL;
    message.header.protocol_version = ASTRA_SERVICE_VERSION;
    message.header.operation = ASTRA_SERVICE_READY;
    message.status = status;
    carried[0] = service_handle;
    (void)astra_port_send(handle, &message, sizeof(message),
                          status == ASTRA_STATUS_OK ? carried : NULL,
                          status == ASTRA_STATUS_OK ? 1u : 0u);
}

static uint32_t mount_volume(uint32_t device, uint32_t irq)
{
    AstraBlockGeometry geometry;
    AstraMbrTable table;
    const AstraMbrEntry *entry;
    AstraExt4Partition window;
    int rc;

    if (astra_lease_block_attach(&lease, device, irq) != ASTRA_BLOCK_OK)
        return STORAGE_FAIL_ATTACH;
    astra_block_device_init(&block, astra_lease_block_backend(), &lease,
                            service_clock, NULL);
    if (astra_block_query(&block, &geometry) != ASTRA_BLOCK_OK)
        return STORAGE_FAIL_QUERY;
    if (astra_mbr_read(&block, sector, sizeof(sector), &table, 0u) !=
        ASTRA_BLOCK_OK)
        return STORAGE_FAIL_MBR;
    entry = astra_mbr_find(&table, ASTRA_MBR_LINUX);
    if (entry == NULL)
        return ASTRA_STATUS_NOT_FOUND;
    window.first_sector = entry->first_sector;
    window.sector_count = entry->sector_count;
    if (astra_alloc_init(&allocator, astra_ext4_alloc_classes,
                         ASTRA_EXT4_ALLOC_CLASS_COUNT, arena,
                         sizeof(arena)) != ASTRA_ALLOC_OK)
        return STORAGE_FAIL_ALLOCATOR;
    astra_ext4_alloc_bind(&allocator);
    /*
     * The clock, beside the allocator: this is the mount a program's writes
     * reach, so this is the binding that decides whether a file has a date.
     */
    astra_ext4_clock_bind(astra_ext4_clock_machine);
    if (astra_ext4_port_init(&ext4_port, &block, &window, sector,
                             sizeof(sector), 0u) != ASTRA_EXT4_OK)
        return STORAGE_FAIL_PORT;
    if (ext4_device_register(astra_ext4_port_blockdev(&ext4_port),
                             DEVICE_NAME) != EOK)
        return STORAGE_FAIL_REGISTER;
    if (ext4_mount(DEVICE_NAME, MOUNT_POINT, false) != EOK)
        return STORAGE_FAIL_MOUNT;
    rc = ext4_recover(MOUNT_POINT);
    if (rc != EOK && rc != ENOTSUP)
        return STORAGE_FAIL_RECOVER;
    if (ext4_journal_start(MOUNT_POINT) != EOK)
        return STORAGE_FAIL_JOURNAL;
    return ASTRA_STATUS_OK;
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *device;
    const AstraStartupCapability *irq;
    const AstraStartupCapability *bootstrap;
    uint32_t receive = 0u;
    uint32_t send = 0u;
    uint32_t status;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    device = capability(startup, ASTRA_CAPABILITY_BLOCK_DEVICE);
    irq = capability(startup, ASTRA_CAPABILITY_BLOCK_IRQ);
    bootstrap = capability(startup, ASTRA_CAPABILITY_SERVICE_READY);
    if (device == NULL || irq == NULL || bootstrap == NULL)
        return ASTRA_STATUS_BAD_HANDLE;

    status = mount_volume(device->handle, irq->handle);
    if (status == ASTRA_STATUS_OK) {
        void *service_storage = NULL;
        void *backend_storage = NULL;
        uint32_t service_capacity = 0u;
        uint32_t backend_capacity = 0u;

        if (!astra_vfs_port_quota_storage(sizeof(AstraVfsOpenFile),
                                           &service_storage,
                                           &service_capacity) ||
            !astra_vfs_port_quota_storage(sizeof(AstraVfsExt4File),
                                           &backend_storage,
                                           &backend_capacity)) {
            status = ASTRA_STATUS_LIMIT;
        } else if (!astra_vfs_ext4_init(&backend, MOUNT_POINT,
                                        backend_storage, backend_capacity) ||
                   !astra_vfs_service_init(
                       &service, astra_vfs_ext4_ops(), &backend,
                       service_storage, service_capacity)) {
            status = ASTRA_STATUS_IO;
        }
    }
    if (status == ASTRA_STATUS_OK &&
        (astra_rt_port_create(PORT_MESSAGES,
                           PORT_MESSAGES *
                               (uint32_t)sizeof(AstraVfsRequestMessage),
                           &receive, &send) != ASTRA_SYSCALL_OK ||
         !astra_vfs_port_service_init(&port, receive, &service)))
        status = ASTRA_STATUS_LIMIT;

    ready(bootstrap->handle, status, send);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK)
        return (int)status;

    for (;;) {
        if (astra_wait_one(receive, ASTRA_DEADLINE_FOREVER, NULL) !=
            ASTRA_SYSCALL_OK)
            return ASTRA_STATUS_PEER_DEAD;
        (void)astra_vfs_port_service_pump(&port, 1u);
    }
}
