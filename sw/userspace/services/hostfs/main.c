#include <astra/host.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/vfs_host_backend.h>
#include <astra/vfs_host_transport.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_service_core.h>

ASTRA_PROGRAM("hostfs", 0, 1, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

enum {
    HOSTFS_FAIL_DEVICE = ASTRA_STATUS_PROGRAM_FIRST,
    HOSTFS_FAIL_LOCK,
    HOSTFS_FAIL_PORT,
    HOSTFS_FAIL_READY
};

static uint32_t device_handle;
static uint32_t transport_lock;
static uint32_t state_lock;
static AstraVfsHostTransport transport;
static AstraVfsHostBackend backend;
static AstraVfsService service;
static AstraVfsSessionSlot service_sessions[ASTRA_VFS_SESSION_MAX];
static AstraVfsPortService port;
static AstraVfsPortWorker worker;

static int acquire(void *context)
{
    return astra_wait_one(*(uint32_t *)context, ASTRA_DEADLINE_FOREVER,
                          NULL) == ASTRA_SYSCALL_OK;
}

static void release(void *context)
{
    if (astra_rt_signal(*(uint32_t *)context, 1u, NULL) != ASTRA_SYSCALL_OK)
        astra_process_exit(HOSTFS_FAIL_LOCK);
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *device;
    const AstraStartupCapability *bootstrap;
    void *service_storage = NULL;
    uint32_t service_capacity = 0u;
    uint32_t receive = 0u;
    uint32_t send = 0u;
    uint32_t status = ASTRA_STATUS_OK;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    device = astra_startup_capability(startup,
                                      ASTRA_CAPABILITY_HOST_DEVICE);
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    if (device == NULL || bootstrap == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    device_handle = device->handle;
    if (status == ASTRA_STATUS_OK &&
        (astra_rt_semaphore_create(
             1u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
             &transport_lock) != ASTRA_SYSCALL_OK ||
         astra_rt_semaphore_create(
             1u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
             &state_lock) != ASTRA_SYSCALL_OK))
        status = HOSTFS_FAIL_LOCK;
    if (status == ASTRA_STATUS_OK &&
        !astra_vfs_host_transport_init(&transport, device_handle, acquire,
                                       release, &transport_lock))
        status = HOSTFS_FAIL_DEVICE;
    if (status == ASTRA_STATUS_OK &&
        (!astra_vfs_port_quota_storage(sizeof(AstraVfsOpenFile),
                                       &service_storage,
                                       &service_capacity) ||
         !astra_vfs_host_init(&backend, &transport, transport.generation) ||
         !astra_vfs_service_init(
             &service, astra_vfs_host_ops(), &backend, service_sessions,
             ASTRA_VFS_SESSION_MAX, service_storage, service_capacity) ||
         !astra_vfs_service_set_state_lock(&service, acquire, release,
                                           &state_lock) ||
         !astra_vfs_service_set_state_wait(
             &service, astra_vfs_state_futex_wait,
             astra_vfs_state_futex_wake)))
        status = ASTRA_STATUS_LIMIT;
    if (status == ASTRA_STATUS_OK &&
        (astra_rt_port_create(
             ASTRA_PORT_MESSAGES_MAX,
             ASTRA_PORT_MESSAGES_MAX *
                 (uint32_t)sizeof(AstraVfsRenameRequestMessage),
             &receive, &send) != ASTRA_SYSCALL_OK ||
         !astra_vfs_port_service_init(&port, receive, &service) ||
         !astra_vfs_port_service_set_state_lock(&port, acquire, release,
                                                &state_lock) ||
         !astra_vfs_port_service_set_accelerator(&port, device_handle)))
        status = HOSTFS_FAIL_PORT;
    {
        uint32_t ready = astra_service_ready(bootstrap->handle, status,
                                             &send, 1u);
        if (ready != ASTRA_SYSCALL_OK && status == ASTRA_STATUS_OK)
            status = HOSTFS_FAIL_READY;
    }
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK)
        return (int)status;
    for (;;) {
        status = astra_wait_one(port.receive, ASTRA_DEADLINE_FOREVER, NULL);
        if (status != ASTRA_SYSCALL_OK)
            return (int)status;
        (void)astra_vfs_port_service_worker_pump(&port, &worker, 1u);
    }
}
