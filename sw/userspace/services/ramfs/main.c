#include <astra/config_library.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_process.h>
#include <astra/vfs_ram_backend.h>
#include <astra/vfs_service_core.h>

ASTRA_PROGRAM("ramfs", 0, 1, 0, "Astra68 contributors",
              "Copyright 2026 Astra68 contributors");

static AstraVfsRamBackend backend;
static AstraVfsService service;
static AstraVfsSessionSlot sessions[ASTRA_VFS_SESSION_MAX];
static AstraVfsPortService port;
static _Alignas(4) uint32_t backend_lock;
static uint32_t receive;
static uint32_t send;

static uint32_t configured_capacity(const AstraStartupInfo *startup,
                                     uint64_t *bytes)
{
    AstraConfig config = ASTRA_CONFIG_INIT;
    AstraConfigError error = {0};
    uint32_t count = 0u;
    uint32_t status;

    if (astra_process_vfs_init(startup) != ASTRA_VFS_OK)
        return ASTRA_STATUS_NOT_FOUND;
    if (astra_config_open(startup, 1u, ASTRA_CONFIG_OPEN_READ,
                          &config, &error) != ASTRA_CONFIG_OK)
        return ASTRA_STATUS_INVALID;
    status = astra_config_count(&config, "max_bytes", &count);
    if (status == ASTRA_CONFIG_OK && count == 1u)
        status = astra_config_get_u64(&config, "max_bytes", 0u, bytes);
    astra_config_close(&config);
    return status == ASTRA_CONFIG_OK && count == 1u && *bytes != 0u ?
        ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
}

static uint32_t start(const AstraStartupInfo *startup)
{
    void *file_storage = NULL;
    uint32_t file_capacity = 0u;
    uint64_t max_bytes = 0u;
    uint32_t status = configured_capacity(startup, &max_bytes);

    if (status != ASTRA_STATUS_OK)
        return status;
    if (!astra_vfs_ram_init(&backend, max_bytes,
                            astra_runtime_allocate,
                            astra_runtime_deallocate,
                            astra_vfs_state_lock_acquire,
                            astra_vfs_state_lock_release,
                            &backend_lock))
        return ASTRA_STATUS_NO_SPACE;
    if (!astra_vfs_port_quota_storage(sizeof(AstraVfsOpenFile),
                                      &file_storage, &file_capacity) ||
        !astra_vfs_service_init(&service, astra_vfs_ram_ops(), &backend,
                                sessions, ASTRA_VFS_SESSION_MAX,
                                file_storage, file_capacity))
        return ASTRA_STATUS_LIMIT;
    if (astra_rt_port_create(1u,
                              (uint32_t)sizeof(AstraVfsRenameRequestMessage),
                              &receive, &send) != ASTRA_SYSCALL_OK ||
        !astra_vfs_port_service_init(&port, receive, &service))
        return ASTRA_STATUS_LIMIT;
    return ASTRA_STATUS_OK;
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *bootstrap;
    uint32_t status;
    uint32_t ready;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    if (bootstrap == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    status = start(startup);
    ready = astra_service_ready(bootstrap->handle, status,
                                status == ASTRA_STATUS_OK ? &send : NULL,
                                status == ASTRA_STATUS_OK ? 1u : 0u);
    (void)astra_close(bootstrap->handle);
    if (ready != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_PEER_DEAD;
    if (status != ASTRA_STATUS_OK)
        return (int)status;
    for (;;) {
        status = astra_wait_one(receive, ASTRA_DEADLINE_FOREVER, NULL);
        if (status != ASTRA_SYSCALL_OK)
            return (int)status;
        (void)astra_vfs_port_service_pump(&port, 1u);
    }
}
