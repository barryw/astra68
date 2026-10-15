/* Read-only bootstrap through the same provider backend HostFS owns later.
 * Linux has already mounted and validated the system volume. No guest block
 * lease or second filesystem engine participates in boot. */
#include <volume.h>

#include <astra/host.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <astra/vfs_host_backend.h>
#include <astra/vfs_host_transport.h>

#include <string.h>

static AstraVfsHostTransport transport;
static AstraVfsHostBackend backend;
static AstraVolumeCatalogPage catalog;
static uint32_t session;
static int claimed;
static int initialized;
static uintptr_t source;
static uint32_t source_size;
static uint8_t source_bytes[ASTRA_MEMORY_PAGE_SIZE];

uint32_t supervisor_volume_unmount(void)
{
    uint32_t status = ASTRA_VFS_OK;
    uint32_t released;

    if (source != 0u)
        return ASTRA_VFS_ERR_INVALID;
    if (session != 0u) {
        status = astra_vfs_host_session_close(&transport, transport.generation, session);
        session = 0u;
    }
    if (claimed) {
        released = astra_vfs_host_claim(&transport, transport.generation, 1, NULL);
        if (status == ASTRA_VFS_OK)
            status = released;
        claimed = 0;
    }
    if (initialized) {
        astra_vfs_host_transport_destroy(&transport);
        initialized = 0;
    }
    return status;
}

uint32_t supervisor_volume_open(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *device;
    AstraVolumeReference volume;
    uint32_t status;

    if (initialized)
        return ASTRA_VFS_ERR_INVALID;
    device = astra_startup_capability(startup, ASTRA_CAPABILITY_HOST_DEVICE);
    if (device == NULL || device->handle == 0u)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (!astra_vfs_host_transport_init(&transport, device->handle, NULL, NULL, NULL))
        return ASTRA_VFS_ERR_IO;
    initialized = 1;
    status = astra_vfs_host_claim(&transport, transport.generation, 0, NULL);
    if (status == ASTRA_VFS_OK) {
        claimed = 1;
        status = astra_vfs_host_lookup_mount(&transport, transport.generation,
            "/dh0", ASTRA_VOLUME_SYSTEM, &catalog, &volume);
    }
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_host_session_open(&transport, transport.generation,
            0u, &volume, "/", ASTRA_HOST_FS_SESSION_READ_ONLY, 0u, &session);
    if (status == ASTRA_VFS_OK &&
        !astra_vfs_host_init(&backend, &transport, transport.generation, session))
        status = ASTRA_VFS_ERR_INVALID;
    if (status != ASTRA_VFS_OK) {
        uint32_t cleanup = supervisor_volume_unmount();
        return cleanup == ASTRA_VFS_OK ? status : cleanup;
    }
    return ASTRA_VFS_OK;
}

uint32_t supervisor_volume_source_open(const char *path, uint32_t *length)
{
    AstraVfsNodeInfo info = {0};
    uintptr_t node = 0u;
    uint32_t status;

    if (session == 0u || source != 0u || path == NULL || length == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *length = 0u;
    status = astra_vfs_host_ops()->open(&backend, path, ASTRA_VFS_OPEN_READ,
        ASTRA_VFS_MODE_DEFAULT, &node, &info);
    if (status != ASTRA_VFS_OK)
        return status;
    if (info.kind != ASTRA_VFS_KIND_FILE || info.size > UINT32_MAX) {
        uint32_t closed = astra_vfs_host_ops()->close(&backend, node);
        return closed != ASTRA_VFS_OK ? closed :
            info.kind != ASTRA_VFS_KIND_FILE ? ASTRA_VFS_ERR_INVALID : ASTRA_VFS_ERR_LIMIT;
    }
    source = node;
    source_size = (uint32_t)info.size;
    *length = source_size;
    return ASTRA_VFS_OK;
}

uint32_t supervisor_volume_source_read_at(void *context, uint32_t offset,
    uint32_t length, const uint8_t **bytes, uint32_t *moved)
{
    uint32_t status;
    uint32_t want = length < sizeof(source_bytes) ? length : sizeof(source_bytes);

    (void)context;
    if (source == 0u || bytes == NULL || moved == NULL || length == 0u ||
        offset > source_size || length > source_size - offset)
        return ASTRA_VFS_ERR_INVALID;
    *bytes = NULL;
    *moved = 0u;
    status = astra_vfs_host_ops()->read(&backend, source, offset, source_bytes, want, moved);
    if (status != ASTRA_VFS_OK)
        return status;
    if (*moved == 0u || *moved > want)
        return ASTRA_VFS_ERR_IO;
    *bytes = source_bytes;
    return ASTRA_VFS_OK;
}

uint32_t supervisor_volume_source_close(void *context)
{
    uintptr_t node = source;

    (void)context;
    if (node == 0u)
        return ASTRA_VFS_OK;
    source = 0u;
    source_size = 0u;
    return astra_vfs_host_ops()->close(&backend, node);
}

uint32_t supervisor_volume_read_alloc(const char *path, void **buffer, uint32_t *length)
{
    uint32_t size;
    uint32_t done = 0u;
    uint8_t *out = NULL;
    uint32_t status;
    uint32_t closed;

    if (buffer == NULL || length == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *buffer = NULL;
    *length = 0u;
    status = supervisor_volume_source_open(path, &size);
    if (status != ASTRA_VFS_OK)
        return status;
    if (size == UINT32_MAX)
        status = ASTRA_VFS_ERR_LIMIT;
    else {
        out = astra_runtime_allocate((size_t)size + 1u);
        if (out == NULL)
            status = ASTRA_VFS_ERR_LIMIT;
    }
    while (status == ASTRA_VFS_OK && done < size) {
        const uint8_t *bytes;
        uint32_t moved;
        status = supervisor_volume_source_read_at(NULL, done, size - done, &bytes, &moved);
        if (status == ASTRA_VFS_OK) {
            memcpy(out + done, bytes, moved);
            done += moved;
        }
    }
    closed = supervisor_volume_source_close(NULL);
    if (status == ASTRA_VFS_OK)
        status = closed;
    if (status != ASTRA_VFS_OK) {
        astra_runtime_deallocate(out);
        return status;
    }
    out[size] = '\0';
    *buffer = out;
    *length = size;
    return ASTRA_VFS_OK;
}
