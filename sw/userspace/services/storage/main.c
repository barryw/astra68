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
#define STORAGE_WORKER_MAX (ASTRA_BLOCK_MAX_REQUESTS_PER_SERVICE + 1u)

enum {
    STORAGE_FAIL_ATTACH = ASTRA_STATUS_PROGRAM_FIRST,
    STORAGE_FAIL_QUERY,
    STORAGE_FAIL_MBR,
    STORAGE_FAIL_ALLOCATOR,
    STORAGE_FAIL_PORT,
    STORAGE_FAIL_REGISTER,
    STORAGE_FAIL_MOUNT,
    STORAGE_FAIL_RECOVER,
    STORAGE_FAIL_JOURNAL,
    STORAGE_FAIL_LOCK,
    STORAGE_FAIL_THREAD,
    STORAGE_FAIL_READY
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
static AstraVfsPortWorker workers[STORAGE_WORKER_MAX];
static uint32_t worker_handles[STORAGE_WORKER_MAX - 1u];
static AstraThreadStart worker_starts[STORAGE_WORKER_MAX - 1u];
static uint32_t worker_count = 2u;
static uint32_t state_lock;
static uint32_t mount_lock;
static uint32_t mount_reader_lock;
static uint32_t cache_lock;
static uint32_t fill_lock;
static uint32_t backend_table_lock;
static uint32_t backend_scan_lock;
static uint32_t mount_readers;

static int vfs_state_acquire(void *context)
{
    uint32_t handle = *(const uint32_t *)context;

    return astra_wait_one(handle, ASTRA_DEADLINE_FOREVER, NULL) ==
           ASTRA_SYSCALL_OK;
}

static void vfs_state_release(void *context)
{
    uint32_t handle = *(const uint32_t *)context;

    if (astra_rt_signal(handle, 1u, NULL) != ASTRA_SYSCALL_OK)
        astra_process_exit(STORAGE_FAIL_LOCK);
}

static void ext4_lock(void)
{
    if (astra_wait_one(mount_lock, ASTRA_DEADLINE_FOREVER, NULL) !=
        ASTRA_SYSCALL_OK)
        astra_process_exit(STORAGE_FAIL_LOCK);
}

static void ext4_unlock(void)
{
    if (astra_rt_signal(mount_lock, 1u, NULL) != ASTRA_SYSCALL_OK)
        astra_process_exit(STORAGE_FAIL_LOCK);
}

static void ext4_read_lock(void)
{
    if (astra_wait_one(mount_reader_lock, ASTRA_DEADLINE_FOREVER, NULL) !=
        ASTRA_SYSCALL_OK)
        astra_process_exit(STORAGE_FAIL_LOCK);
    if (mount_readers == 0u &&
        astra_wait_one(mount_lock, ASTRA_DEADLINE_FOREVER, NULL) !=
            ASTRA_SYSCALL_OK)
        astra_process_exit(STORAGE_FAIL_LOCK);
    ++mount_readers;
    if (astra_rt_signal(mount_reader_lock, 1u, NULL) != ASTRA_SYSCALL_OK)
        astra_process_exit(STORAGE_FAIL_LOCK);
}

static void ext4_read_unlock(void)
{
    if (astra_wait_one(mount_reader_lock, ASTRA_DEADLINE_FOREVER, NULL) !=
        ASTRA_SYSCALL_OK || mount_readers == 0u)
        astra_process_exit(STORAGE_FAIL_LOCK);
    --mount_readers;
    if (mount_readers == 0u &&
        astra_rt_signal(mount_lock, 1u, NULL) != ASTRA_SYSCALL_OK)
        astra_process_exit(STORAGE_FAIL_LOCK);
    if (astra_rt_signal(mount_reader_lock, 1u, NULL) != ASTRA_SYSCALL_OK)
        astra_process_exit(STORAGE_FAIL_LOCK);
}

static void ext4_cache_lock(void)
{
    if (astra_wait_one(cache_lock, ASTRA_DEADLINE_FOREVER, NULL) !=
        ASTRA_SYSCALL_OK)
        astra_process_exit(STORAGE_FAIL_LOCK);
}

static void ext4_cache_unlock(void)
{
    if (astra_rt_signal(cache_lock, 1u, NULL) != ASTRA_SYSCALL_OK)
        astra_process_exit(STORAGE_FAIL_LOCK);
}

static void ext4_fill_lock(void)
{
    if (astra_wait_one(fill_lock, ASTRA_DEADLINE_FOREVER, NULL) !=
        ASTRA_SYSCALL_OK)
        astra_process_exit(STORAGE_FAIL_LOCK);
}

static void ext4_fill_unlock(void)
{
    if (astra_rt_signal(fill_lock, 1u, NULL) != ASTRA_SYSCALL_OK)
        astra_process_exit(STORAGE_FAIL_LOCK);
}

static const struct ext4_lock ext4_locks = {
    .lock = ext4_lock,
    .unlock = ext4_unlock,
    .read_lock = ext4_read_lock,
    .read_unlock = ext4_read_unlock,
    .cache_lock = ext4_cache_lock,
    .cache_unlock = ext4_cache_unlock,
    .fill_lock = ext4_fill_lock,
    .fill_unlock = ext4_fill_unlock,
};

static uint64_t service_clock(void *context)
{
    (void)context;
    return astra_clock_monotonic();
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
    if (geometry.queue_depth == 0u ||
        geometry.queue_depth > ASTRA_BLOCK_MAX_REQUESTS_PER_SERVICE)
        return STORAGE_FAIL_QUERY;
    worker_count = geometry.queue_depth + 1u;
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
    if (ext4_mount_setup_locks(MOUNT_POINT, &ext4_locks) != EOK)
        return STORAGE_FAIL_LOCK;
    rc = ext4_recover(MOUNT_POINT);
    if (rc != EOK && rc != ENOTSUP)
        return STORAGE_FAIL_RECOVER;
    if (ext4_journal_start(MOUNT_POINT) != EOK)
        return STORAGE_FAIL_JOURNAL;
    return ASTRA_STATUS_OK;
}

static void storage_worker(uint32_t index) __attribute__((noreturn));

static void storage_worker(uint32_t index)
{
    if (index >= worker_count)
        astra_thread_exit(ASTRA_STATUS_INVALID);
    for (;;) {
        uint32_t status = astra_wait_one(port.receive,
                                         ASTRA_DEADLINE_FOREVER, NULL);

        if (status != ASTRA_SYSCALL_OK) {
            (void)astra_log_failure("storage receive wait", status);
            astra_thread_exit(status);
        }
        (void)astra_vfs_port_service_worker_pump(&port, &workers[index], 1u);
    }
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
    device = astra_startup_capability(startup,
                                      ASTRA_CAPABILITY_BLOCK_DEVICE);
    irq = astra_startup_capability(startup, ASTRA_CAPABILITY_BLOCK_IRQ);
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    if (device == NULL || irq == NULL || bootstrap == NULL)
        return ASTRA_STATUS_BAD_HANDLE;

    if (astra_rt_semaphore_create(
            1u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL, &state_lock) !=
            ASTRA_SYSCALL_OK ||
        astra_rt_semaphore_create(
            1u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL, &mount_lock) !=
            ASTRA_SYSCALL_OK ||
        astra_rt_semaphore_create(
            1u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
            &mount_reader_lock) != ASTRA_SYSCALL_OK ||
        astra_rt_semaphore_create(
            1u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL, &cache_lock) !=
            ASTRA_SYSCALL_OK ||
        astra_rt_semaphore_create(
            1u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL, &fill_lock) !=
            ASTRA_SYSCALL_OK ||
        astra_rt_semaphore_create(
            1u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
            &backend_table_lock) != ASTRA_SYSCALL_OK ||
        astra_rt_semaphore_create(
            1u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
            &backend_scan_lock) !=
            ASTRA_SYSCALL_OK)
        status = STORAGE_FAIL_LOCK;
    else
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
                   !astra_vfs_ext4_set_locks(
                       &backend, vfs_state_acquire, vfs_state_release,
                       &backend_table_lock, vfs_state_acquire,
                       vfs_state_release, &backend_scan_lock) ||
                   !astra_vfs_service_init(
                       &service, astra_vfs_ext4_ops(), &backend,
                       service_storage, service_capacity)) {
            status = ASTRA_STATUS_IO;
        }
    }
    if (status == ASTRA_STATUS_OK &&
        !astra_vfs_service_set_state_lock(
             &service, vfs_state_acquire, vfs_state_release, &state_lock))
        status = STORAGE_FAIL_LOCK;
    if (status == ASTRA_STATUS_OK &&
        (astra_rt_port_create(ASTRA_PORT_MESSAGES_MAX,
                           ASTRA_PORT_MESSAGES_MAX *
                               (uint32_t)sizeof(AstraVfsRequestMessage),
                           &receive, &send) != ASTRA_SYSCALL_OK ||
         !astra_vfs_port_service_init(&port, receive, &service)))
        status = ASTRA_STATUS_LIMIT;
    if (status == ASTRA_STATUS_OK &&
        !astra_vfs_port_service_set_state_lock(
            &port, vfs_state_acquire, vfs_state_release, &state_lock))
        status = STORAGE_FAIL_LOCK;

    if (status == ASTRA_STATUS_OK) {
        AstraProcessInfo info = {0};
        uint32_t process_handle = 0u;

        if (astra_query_abi(NULL, &process_handle, NULL) != ASTRA_SYSCALL_OK ||
            astra_process_info(process_handle, &info) != ASTRA_SYSCALL_OK)
            status = STORAGE_FAIL_THREAD;
        for (uint32_t index = 1u;
             status == ASTRA_STATUS_OK && index < worker_count; ++index) {
            worker_starts[index - 1u].entry = storage_worker;
            worker_starts[index - 1u].argument = index;
            if (astra_rt_thread_create(
                    &worker_starts[index - 1u], info.default_priority,
                    ASTRA_RIGHT_READ | ASTRA_RIGHT_WAIT,
                    &worker_handles[index - 1u], NULL) != ASTRA_SYSCALL_OK)
                status = STORAGE_FAIL_THREAD;
        }
    }

    {
        uint32_t ready_status = astra_service_ready(bootstrap->handle, status,
                                                    &send, 1u);

        if (ready_status != ASTRA_SYSCALL_OK) {
            (void)astra_log_failure("storage ready send", ready_status);
            if (status == ASTRA_STATUS_OK)
                status = STORAGE_FAIL_READY;
        }
    }
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK)
        return (int)status;

    storage_worker(0u);
}
