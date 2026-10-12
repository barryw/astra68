#include <bootstrap_host.h>

#include <astra/event_emit.h>
#include <astra/host.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <astra/vfs_host_backend.h>
#include <astra/vfs_host_transport.h>

#include <string.h>

/* The bootstrap reader uses HostFS's canonical backend before services exist. */
static AstraVfsHostTransport transport;
static AstraVfsHostBackend backend;

static uint32_t compare(uintptr_t node, uint64_t size,
                        const uint8_t *expected, uint32_t length)
{
    const AstraVfsBackendOps *ops = astra_vfs_host_ops();
    uint8_t bytes[ASTRA_MEMORY_PAGE_SIZE];
    uint32_t offset = 0u;

    if (size != length || (expected == NULL && length != 0u))
        return ASTRA_STATUS_INVALID;
    while (offset < length) {
        uint32_t want = length - offset;
        uint32_t moved = 0u;
        uint32_t status;

        if (want > sizeof(bytes))
            want = sizeof(bytes);
        status = ops->read(&backend, node, offset, bytes, want, &moved);
        if (status != ASTRA_STATUS_OK)
            return status;
        if (moved != want || memcmp(bytes, expected + offset, want) != 0)
            return ASTRA_STATUS_INVALID;
        offset += moved;
    }
    return ASTRA_STATUS_OK;
}

uint32_t supervisor_bootstrap_probe(const AstraStartupInfo *startup,
                                    const void *expected, uint32_t length)
{
    const AstraStartupCapability *device =
        astra_startup_capability(startup, ASTRA_CAPABILITY_HOST_DEVICE);
    const AstraVfsBackendOps *ops = astra_vfs_host_ops();
    AstraVfsNodeInfo info = {0};
    uintptr_t node = 0u;
    uint32_t session = 0u;
    uint32_t status = ASTRA_STATUS_UNSUPPORTED;
    uint32_t cleanup = ASTRA_STATUS_OK;

    if (device != NULL && astra_vfs_host_transport_init(&transport,
            device->handle, NULL, NULL, NULL)) {
        status = astra_vfs_host_claim(&transport, transport.generation, 0, NULL);
        if (status == ASTRA_STATUS_OK) {
            status = astra_vfs_host_session_open(&transport, transport.generation,
                0u, ASTRA_HOST_FS_VOLUME_SYSTEM, "/",
                ASTRA_HOST_FS_SESSION_READ_ONLY, &session);
            if (status == ASTRA_STATUS_OK) {
                if (!astra_vfs_host_init(&backend, &transport, transport.generation, session))
                    status = ASTRA_STATUS_INVALID;
                else
                    status = ops->open(&backend, "/startup/system", ASTRA_VFS_OPEN_READ,
                        ASTRA_VFS_MODE_DEFAULT, &node, &info);
                if (status == ASTRA_STATUS_OK) {
                    status = compare(node, info.size, expected, length);
                    cleanup = ops->close(&backend, node);
                }
                {
                    uint32_t closed = astra_vfs_host_session_close(&transport,
                        transport.generation, session);
                    if (cleanup == ASTRA_STATUS_OK)
                        cleanup = closed;
                }
            }
            {
                uint32_t released = astra_vfs_host_claim(&transport,
                    transport.generation, 1, NULL);
                if (cleanup == ASTRA_STATUS_OK)
                    cleanup = released;
            }
        }
        astra_vfs_host_transport_destroy(&transport);
    }
    if (status == ASTRA_STATUS_OK)
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR, ASTRA_EVENT_LEVEL_NOTICE,
            "bootstrap-provider match bytes=%u", length);
    else if (status == ASTRA_STATUS_INVALID)
        ASTRA_EVENT2(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR, ASTRA_EVENT_LEVEL_WARNING,
            "bootstrap-provider mismatch provider=%u volume=%u", (uint32_t)info.size, length);
    else
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR, ASTRA_EVENT_LEVEL_NOTICE,
            "bootstrap-provider absent status=%u", status);
    if (cleanup != ASTRA_STATUS_OK)
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR, ASTRA_EVENT_LEVEL_ERROR,
            "bootstrap-provider cleanup failed status=%u", cleanup);
    return cleanup;
}
