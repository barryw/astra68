#include <volume.h>
#include <astra/host.h>
#include <astra/runtime.h>
#include <astra/vfs_host_backend.h>
#include <astra/vfs_host_transport.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static uint8_t payload[300u * 1024u + 7u];
static uint64_t payload_size;
static uint16_t kind;
static int present, initialized, claimed, session_live, file_live, allocation_failed;
static uint32_t claim_status, lookup_status, open_status, read_status, close_status;
static uint32_t session_status, session_close_status, release_status, read_mode;
static uint32_t closes, releases, destroys;
static AstraStartupCapability device = {.handle = 7u};

void *astra_runtime_allocate(size_t size) { return allocation_failed ? NULL : malloc(size); }
void astra_runtime_deallocate(void *pointer) { free(pointer); }
const AstraStartupCapability *astra_startup_capability(const AstraStartupInfo *startup, const char *name)
{
    (void)startup;
    assert(strcmp(name, ASTRA_CAPABILITY_HOST_DEVICE) == 0);
    return present ? &device : NULL;
}
int astra_vfs_host_transport_init(AstraVfsHostTransport *transport, uint32_t handle,
    AstraVfsStateAcquire acquire, AstraVfsStateRelease release, void *context)
{
    assert(handle == 7u && acquire == NULL && release == NULL && context == NULL);
    memset(transport, 0, sizeof(*transport));
    transport->generation = 9u;
    return initialized;
}
void astra_vfs_host_transport_destroy(AstraVfsHostTransport *transport)
{
    ++destroys;
    memset(transport, 0, sizeof(*transport));
}
uint32_t astra_vfs_host_claim(AstraVfsHostTransport *transport, uint32_t generation,
    int release, uint32_t *epoch)
{
    (void)transport;
    assert(generation == 9u && epoch == NULL);
    if (!release) {
        assert(!claimed);
        claimed = claim_status == ASTRA_VFS_OK;
        return claim_status;
    }
    ++releases;
    claimed = file_live = session_live = 0;
    return release_status;
}
uint32_t astra_vfs_host_lookup_mount(AstraVfsHostTransport *transport, uint32_t generation,
    const char *name, uint32_t flags, AstraVolumeCatalogPage *scratch, AstraVolumeReference *volume)
{
    (void)transport; (void)scratch;
    assert(claimed && generation == 9u && strcmp(name, "/dh0") == 0 && flags == ASTRA_VOLUME_SYSTEM);
    *volume = (AstraVolumeReference){1u, 2u, 1u};
    return lookup_status;
}
uint32_t astra_vfs_host_session_open(AstraVfsHostTransport *transport, uint32_t generation,
    uint32_t parent, const AstraVolumeReference *volume, const char *root,
    uint16_t flags, uint32_t requester, uint32_t *session)
{
    (void)transport;
    assert(claimed && generation == 9u && parent == 0u && volume->id == 2u &&
        strcmp(root, "/") == 0 && flags == ASTRA_HOST_FS_SESSION_READ_ONLY && requester == 0u);
    if (session_status == ASTRA_VFS_OK) { *session = 3u; session_live = 1; }
    return session_status;
}
uint32_t astra_vfs_host_session_close(AstraVfsHostTransport *transport, uint32_t generation, uint32_t session)
{
    (void)transport;
    assert(generation == 9u && session == 3u);
    session_live = file_live = 0;
    return session_close_status;
}
int astra_vfs_host_init(AstraVfsHostBackend *backend, AstraVfsHostTransport *transport,
    uint32_t generation, uint32_t session)
{
    (void)backend; (void)transport;
    assert(generation == 9u && session == 3u);
    return 1;
}
static uint32_t open_node(void *context, const char *path, uint32_t flags, uint16_t mode,
    uintptr_t *node, AstraVfsNodeInfo *info)
{
    (void)context;
    assert(session_live && strcmp(path, "/startup/system") == 0 &&
        flags == ASTRA_VFS_OPEN_READ && mode == ASTRA_VFS_MODE_DEFAULT);
    if (open_status == ASTRA_VFS_OK) {
        *node = 1u; info->size = payload_size; info->kind = kind; file_live = 1;
    }
    return open_status;
}
static uint32_t read_node(void *context, uintptr_t node, uint64_t offset,
    void *bytes, uint32_t length, uint32_t *moved)
{
    (void)context;
    assert(file_live && node == 1u && offset + length <= payload_size);
    *moved = read_mode == 1u ? 0u : read_mode == 2u ? length + 1u :
        length > 4093u ? 4093u : length;
    if (*moved <= length) memcpy(bytes, payload + offset, *moved);
    return read_status;
}
static uint32_t close_node(void *context, uintptr_t node)
{
    (void)context;
    assert(file_live && node == 1u);
    ++closes; file_live = 0;
    return close_status;
}
const AstraVfsBackendOps *astra_vfs_host_ops(void)
{
    static const AstraVfsBackendOps ops = {.open = open_node, .read = read_node, .close = close_node};
    return &ops;
}
static void reset(void)
{
    present = initialized = 1;
    claimed = session_live = file_live = allocation_failed = 0;
    claim_status = lookup_status = open_status = read_status = close_status = ASTRA_VFS_OK;
    session_status = session_close_status = release_status = ASTRA_VFS_OK;
    read_mode = closes = releases = destroys = 0u;
    kind = ASTRA_VFS_KIND_FILE;
    payload_size = sizeof(payload);
    for (uint32_t at = 0u; at < sizeof(payload); ++at) payload[at] = (uint8_t)(at * 17u);
}
static void read_case(uint32_t expected)
{
    void *bytes = (void *)1;
    uint32_t length = 99u;
    assert(supervisor_volume_open(NULL) == ASTRA_VFS_OK);
    assert(supervisor_volume_read_alloc("/startup/system", &bytes, &length) == expected);
    if (expected == ASTRA_VFS_OK) {
        assert(length == sizeof(payload) && memcmp(bytes, payload, length) == 0);
        assert(((uint8_t *)bytes)[length] == 0u);
        free(bytes);
    } else assert(bytes == NULL && length == 0u);
    assert(supervisor_volume_unmount() == ASTRA_VFS_OK);
    assert(!claimed && !session_live && !file_live && releases == 1u && destroys == 1u);
}
int main(void)
{
    reset(); read_case(ASTRA_VFS_OK);
    reset(); allocation_failed = 1; read_case(ASTRA_VFS_ERR_LIMIT);
    reset(); read_mode = 1u; read_case(ASTRA_VFS_ERR_IO);
    reset(); read_mode = 2u; read_case(ASTRA_VFS_ERR_IO);
    reset(); read_status = ASTRA_VFS_ERR_LOST; read_case(ASTRA_VFS_ERR_LOST);
    reset(); close_status = ASTRA_VFS_ERR_IO; read_case(ASTRA_VFS_ERR_IO);
    reset(); kind = ASTRA_VFS_KIND_DIRECTORY; read_case(ASTRA_VFS_ERR_INVALID);
    reset(); payload_size = UINT64_C(1) << 32; read_case(ASTRA_VFS_ERR_LIMIT);
    reset(); present = 0; assert(supervisor_volume_open(NULL) == ASTRA_VFS_ERR_BAD_HANDLE);
    reset(); initialized = 0; assert(supervisor_volume_open(NULL) == ASTRA_VFS_ERR_IO);
    reset(); claim_status = ASTRA_VFS_ERR_ACCESS;
    assert(supervisor_volume_open(NULL) == ASTRA_VFS_ERR_ACCESS && destroys == 1u && releases == 0u);
    reset(); lookup_status = ASTRA_VFS_ERR_NOT_FOUND;
    assert(supervisor_volume_open(NULL) == ASTRA_VFS_ERR_NOT_FOUND && releases == 1u && !claimed);
    reset(); session_status = ASTRA_VFS_ERR_LOST;
    assert(supervisor_volume_open(NULL) == ASTRA_VFS_ERR_LOST && releases == 1u && !claimed);
    reset(); assert(supervisor_volume_open(NULL) == ASTRA_VFS_OK);
    uint32_t length; const uint8_t *bytes; uint32_t moved;
    assert(supervisor_volume_source_open("/startup/system", &length) == ASTRA_VFS_OK);
    assert(supervisor_volume_unmount() == ASTRA_VFS_ERR_INVALID && claimed && file_live);
    assert(supervisor_volume_source_read_at(NULL, length, 1u, &bytes, &moved) == ASTRA_VFS_ERR_INVALID);
    assert(supervisor_volume_source_read_at(NULL, 7u, 100u, &bytes, &moved) == ASTRA_VFS_OK);
    assert(moved == 100u && memcmp(bytes, payload + 7u, 100u) == 0);
    assert(supervisor_volume_source_close(NULL) == ASTRA_VFS_OK);
    assert(supervisor_volume_source_close(NULL) == ASTRA_VFS_OK && closes == 1u);
    session_close_status = ASTRA_VFS_ERR_IO;
    assert(supervisor_volume_unmount() == ASTRA_VFS_ERR_IO && releases == 1u && !claimed);
    reset(); assert(supervisor_volume_open(NULL) == ASTRA_VFS_OK); release_status = ASTRA_VFS_ERR_IO;
    assert(supervisor_volume_unmount() == ASTRA_VFS_ERR_IO && destroys == 1u);
    reset(); read_case(ASTRA_VFS_OK); /* A released bootstrap claim can be reacquired. */
    return 0;
}
