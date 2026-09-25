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

#include "shutdown.h"

#define MOUNT_POINT "/vol/"
#define DEVICE_NAME "astra"
#define STORAGE_WORKER_MAX (ASTRA_BLOCK_MAX_REQUESTS_PER_SERVICE + 1u)
#define JOURNAL_COMMIT_NS (UINT64_C(5) * UINT64_C(1000000000))

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
    STORAGE_FAIL_WRITEBACK,
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
static AstraVfsSessionSlot service_sessions[ASTRA_VFS_SESSION_MAX];
static AstraVfsPortService port;
static uint8_t sector[ASTRA_BLOCK_SECTOR_BYTES];
static AstraVfsPortWorker workers[STORAGE_WORKER_MAX];
static uint32_t worker_handles[STORAGE_WORKER_MAX - 1u];
static AstraThreadStart worker_starts[STORAGE_WORKER_MAX - 1u];
static uint32_t worker_count = 2u;
static _Alignas(4) uint32_t state_lock;
static _Alignas(4) uint32_t mount_lock;
static _Alignas(4) uint32_t mount_reader_lock;
static _Alignas(4) uint32_t cache_lock;
static _Alignas(4) uint32_t fill_lock;
static _Alignas(4) uint32_t backend_table_lock;
static _Alignas(4) uint32_t backend_scan_lock;
static uint32_t journal_timer;
static uint32_t shutdown_event;
static uint32_t shutdown_receive;
static uint32_t shutdown_reply;
static uint32_t shutdown_transaction;
static uint32_t journal_thread_handle;
static AstraThreadStart journal_thread_start;
static uint32_t mount_readers;
static volatile uint32_t shutdown_requested;
static uint32_t worker_failed;
static _Alignas(16) uint8_t signal_stack[4096u];

static void storage_signal(int number)
{
    if (number == (int)ASTRA_SIGNAL_TERMINATE)
        shutdown_requested = 1u;
}

static void storage_lock_failure(const char *operation, uint32_t status)
{
    (void)astra_log_failure(operation, status);
    astra_process_exit(STORAGE_FAIL_LOCK);
}

static void ext4_lock(void)
{
    uint32_t status = astra_mutex_lock(&mount_lock);

    if (status != ASTRA_SYSCALL_OK)
        storage_lock_failure("storage mount lock", status);
}

static void ext4_unlock(void)
{
    uint32_t status = astra_mutex_unlock(&mount_lock);

    if (status != ASTRA_SYSCALL_OK)
        storage_lock_failure("storage mount unlock", status);
}

static void ext4_read_lock(void)
{
    uint32_t status = astra_mutex_lock(&mount_reader_lock);

    if (status != ASTRA_SYSCALL_OK)
        storage_lock_failure("storage reader lock", status);
    if (mount_readers == 0u &&
        (status = astra_mutex_lock(&mount_lock)) != ASTRA_SYSCALL_OK)
        storage_lock_failure("storage first reader", status);
    ++mount_readers;
    status = astra_mutex_unlock(&mount_reader_lock);
    if (status != ASTRA_SYSCALL_OK)
        storage_lock_failure("storage reader unlock", status);
}

static void ext4_read_unlock(void)
{
    uint32_t status = astra_mutex_lock(&mount_reader_lock);

    if (status != ASTRA_SYSCALL_OK)
        storage_lock_failure("storage reader release lock", status);
    if (mount_readers == 0u)
        storage_lock_failure("storage reader underflow", 0u);
    --mount_readers;
    if (mount_readers == 0u &&
        (status = astra_mutex_unlock(&mount_lock)) != ASTRA_SYSCALL_OK)
        storage_lock_failure("storage last reader", status);
    status = astra_mutex_unlock(&mount_reader_lock);
    if (status != ASTRA_SYSCALL_OK)
        storage_lock_failure("storage reader release unlock", status);
}

static void ext4_cache_lock(void)
{
    uint32_t status = astra_mutex_lock(&cache_lock);

    if (status != ASTRA_SYSCALL_OK)
        storage_lock_failure("storage cache lock", status);
}

static void ext4_cache_unlock(void)
{
    uint32_t status = astra_mutex_unlock(&cache_lock);

    if (status != ASTRA_SYSCALL_OK)
        storage_lock_failure("storage cache unlock", status);
}

static void ext4_fill_lock(void)
{
    uint32_t status = astra_mutex_lock(&fill_lock);

    if (status != ASTRA_SYSCALL_OK)
        storage_lock_failure("storage fill lock", status);
}

static void ext4_fill_unlock(void)
{
    uint32_t status = astra_mutex_unlock(&fill_lock);

    if (status != ASTRA_SYSCALL_OK)
        storage_lock_failure("storage fill unlock", status);
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
    /* Keep checkpoint buffers dirty across requests. Journal commits remain
     * durable, fsync drains them explicitly, and cache/journal pressure makes
     * forward progress without turning every mutation into write-through I/O. */
    if (ext4_cache_write_back(MOUNT_POINT, true) != EOK)
        return STORAGE_FAIL_WRITEBACK;
    return ASTRA_STATUS_OK;
}

static void journal_worker(uint32_t unused) __attribute__((noreturn));

static void journal_worker(uint32_t unused)
{
    (void)unused;
    for (;;) {
        uint64_t deadline = astra_clock_monotonic() + JOURNAL_COMMIT_NS;
        uint32_t handles[2] = {shutdown_event, journal_timer};
        uint32_t index = ASTRA_WAIT_INDEX_NONE;
        uint32_t status = astra_wait_multiple(handles, 2u, deadline,
                                              &index, NULL);

        if (status == ASTRA_SYSCALL_OK && index == 0u)
            astra_thread_exit(ASTRA_STATUS_OK);

        if (status != ASTRA_SYSCALL_TIMED_OUT) {
            (void)astra_log_failure("storage journal timer", status);
            __atomic_store_n(&worker_failed, 1u, __ATOMIC_RELEASE);
            astra_thread_exit(status);
        }
        status = (uint32_t)ext4_journal_commit(MOUNT_POINT);
        if (status != EOK) {
            (void)astra_log_failure("storage journal commit", status);
            __atomic_store_n(&worker_failed, 1u, __ATOMIC_RELEASE);
            astra_thread_exit(STORAGE_FAIL_JOURNAL);
        }
    }
}

static void storage_worker(uint32_t index)
{
    if (index >= worker_count)
        astra_thread_exit(ASTRA_STATUS_INVALID);
    for (;;) {
        uint32_t handles[3] = {shutdown_event, port.receive,
                               shutdown_receive};
        uint32_t ready = ASTRA_WAIT_INDEX_NONE;
        uint32_t status = astra_wait_multiple(
            handles, index == 0u && shutdown_receive != 0u ? 3u : 2u,
            ASTRA_DEADLINE_FOREVER, &ready, NULL);

        if (status == ASTRA_SYSCALL_OK && ready == 0u)
            break;
        if (status == ASTRA_SYSCALL_OK && ready == 2u) {
            AstraShutdownRequest request = {0};

            status = astra_shutdown_receive(shutdown_receive, &request,
                                             &shutdown_reply);
            if (status != ASTRA_SYSCALL_OK) {
                __atomic_store_n(&worker_failed, 1u, __ATOMIC_RELEASE);
                break;
            }
            shutdown_transaction = request.header.transaction_id;
            shutdown_requested = 1u;
            break;
        }
        if (index == 0u && shutdown_requested != 0u)
            break;
        if (status == ASTRA_SYSCALL_CANCELLED)
            continue;
        if (status != ASTRA_SYSCALL_OK) {
            (void)astra_log_failure("storage receive wait", status);
            __atomic_store_n(&worker_failed, 1u, __ATOMIC_RELEASE);
            astra_thread_exit(status);
        }
        (void)astra_vfs_port_service_worker_pump(&port, &workers[index], 1u);
    }
    if (index != 0u)
        astra_thread_exit(ASTRA_STATUS_OK);
}

static uint32_t finish_storage(void)
{
    const AstraVfsMountOps mount = {
        storage_flush_volume, storage_unmount_volume
    };
    uint32_t status;

    status = astra_rt_signal(shutdown_event, 1u, NULL);
    if (status != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_IO;
    for (uint32_t index = 1u; index < worker_count; ++index) {
        status = astra_wait_one(worker_handles[index - 1u],
                                ASTRA_DEADLINE_FOREVER, NULL);
        if (status != ASTRA_SYSCALL_OK)
            return ASTRA_STATUS_IO;
        (void)astra_close(worker_handles[index - 1u]);
    }
    status = astra_wait_one(journal_thread_handle,
                            ASTRA_DEADLINE_FOREVER, NULL);
    if (status != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_IO;
    (void)astra_close(journal_thread_handle);
    if (__atomic_load_n(&worker_failed, __ATOMIC_ACQUIRE) != 0u)
        return ASTRA_STATUS_IO;
    while (astra_vfs_port_service_worker_pump(&port, &workers[0], 1u) != 0u) {}
    if (port.stalled != 0u)
        return ASTRA_STATUS_IO;
    return astra_vfs_service_shutdown(&service, &mount, &block);
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *device;
    const AstraStartupCapability *irq;
    const AstraStartupCapability *bootstrap;
    uint32_t receive = 0u;
    uint32_t send = 0u;
    uint32_t shutdown_send = 0u;
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

    if (astra_rt_semaphore_create(0u, 1u, ASTRA_RIGHT_WAIT,
                                  &journal_timer) != ASTRA_SYSCALL_OK ||
        astra_rt_event_create(ASTRA_EVENT_MANUAL_RESET,
                              ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
                              &shutdown_event) != ASTRA_SYSCALL_OK)
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
                       &backend, astra_vfs_state_lock_acquire,
                       astra_vfs_state_lock_release, &backend_table_lock,
                       astra_vfs_state_lock_acquire,
                       astra_vfs_state_lock_release, &backend_scan_lock) ||
                   !astra_vfs_service_init(
                       &service, astra_vfs_ext4_ops(), &backend,
                       service_sessions, ASTRA_VFS_SESSION_MAX,
                       service_storage, service_capacity)) {
            status = ASTRA_STATUS_IO;
        }
    }
    if (status == ASTRA_STATUS_OK &&
        !astra_vfs_service_set_state_lock(
             &service, astra_vfs_state_lock_acquire,
             astra_vfs_state_lock_release, &state_lock))
        status = STORAGE_FAIL_LOCK;
    if (status == ASTRA_STATUS_OK &&
        !astra_vfs_service_set_state_wait(
            &service, astra_vfs_state_futex_wait,
            astra_vfs_state_futex_wake))
        status = STORAGE_FAIL_LOCK;
    if (status == ASTRA_STATUS_OK &&
        (astra_rt_port_create(ASTRA_PORT_MESSAGES_MAX,
                           ASTRA_PORT_MESSAGES_MAX *
                               (uint32_t)sizeof(AstraVfsRenameRequestMessage),
                           &receive, &send) != ASTRA_SYSCALL_OK ||
         !astra_vfs_port_service_init(&port, receive, &service)))
        status = ASTRA_STATUS_LIMIT;
    if (status == ASTRA_STATUS_OK &&
        !astra_vfs_port_service_set_state_lock(
            &port, astra_vfs_state_lock_acquire,
            astra_vfs_state_lock_release, &state_lock))
        status = STORAGE_FAIL_LOCK;
    if (status == ASTRA_STATUS_OK &&
        (status = astra_rt_signal_configure(
            storage_signal, signal_stack + sizeof(signal_stack),
            0u, NULL, NULL)) != ASTRA_SYSCALL_OK) {
        (void)astra_log_failure("storage signal configure", status);
        status = STORAGE_FAIL_THREAD;
    }

    if (status == ASTRA_STATUS_OK) {
        AstraProcessInfo info = {0};
        uint32_t process_handle = 0u;

        uint32_t query = astra_query_abi(NULL, &process_handle, NULL);

        if (query == ASTRA_SYSCALL_OK)
            query = astra_process_info(process_handle, &info);
        if (query != ASTRA_SYSCALL_OK) {
            (void)astra_log_failure("storage thread process info", query);
            status = STORAGE_FAIL_THREAD;
        }
        for (uint32_t index = 1u;
             status == ASTRA_STATUS_OK && index < worker_count; ++index) {
            worker_starts[index - 1u].entry = storage_worker;
            worker_starts[index - 1u].argument = index;
            uint32_t created = astra_rt_thread_create(
                    &worker_starts[index - 1u], info.default_priority,
                    ASTRA_RIGHT_READ | ASTRA_RIGHT_WAIT,
                    &worker_handles[index - 1u], NULL);
            if (created != ASTRA_SYSCALL_OK) {
                (void)astra_log_failure("storage worker thread", created);
                status = STORAGE_FAIL_THREAD;
            }
        }
        if (status == ASTRA_STATUS_OK) {
            journal_thread_start.entry = journal_worker;
            journal_thread_start.argument = 0u;
            uint32_t created = astra_rt_thread_create(
                    &journal_thread_start, info.default_priority,
                    ASTRA_RIGHT_READ | ASTRA_RIGHT_WAIT,
                    &journal_thread_handle, NULL);
            if (created != ASTRA_SYSCALL_OK) {
                (void)astra_log_failure("storage journal thread", created);
                status = STORAGE_FAIL_THREAD;
            }
        }
    }

    if (status == ASTRA_STATUS_OK &&
        astra_rt_port_create(1u, sizeof(AstraShutdownRequest),
                             &shutdown_receive, &shutdown_send) !=
            ASTRA_SYSCALL_OK)
        status = STORAGE_FAIL_PORT;
    {
        uint32_t ready_status = astra_service_ready_managed(
            bootstrap->handle, status, &send, 1u, shutdown_send);

        if (ready_status != ASTRA_SYSCALL_OK) {
            (void)astra_log_failure("storage ready send", ready_status);
            if (status == ASTRA_STATUS_OK)
                status = STORAGE_FAIL_READY;
        }
    }
    if (shutdown_send != 0u)
        (void)astra_close(shutdown_send);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK)
        return (int)status;

    storage_worker(0u);
    status = finish_storage();
    if (shutdown_reply != 0u)
        (void)astra_shutdown_respond(
            shutdown_reply, shutdown_transaction,
            status == ASTRA_STATUS_OK ? ASTRA_SHUTDOWN_READY :
                                        ASTRA_SHUTDOWN_CANCEL,
            status);
    if (status != ASTRA_STATUS_OK)
        (void)astra_log_failure("storage shutdown", status);
    return (int)status;
}
