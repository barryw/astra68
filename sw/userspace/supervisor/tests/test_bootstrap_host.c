#include <bootstrap_host.h>
#include <astra/event_emit.h>
#include <astra/host.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <astra/vfs_host_transport.h>

#include <assert.h>
#include <string.h>

uint8_t astra_event_levels[ASTRA_EVENT_SUBSYSTEM_MAX];
static AstraStartupCapability device;
static uint8_t payload[8193];
static uint32_t payload_size, reads, closes, session_closes, releases, destroys;
static uint32_t open_status, read_status, close_status, session_status, release_status;
static uint32_t session_close_status, claim_status;
static int present, initialized, short_read, claimed, file_live, session_live;
static int matched, mismatched, absent, cleanup_failed;

const AstraStartupCapability *astra_startup_capability(
    const AstraStartupInfo *startup, const char *name)
{
    (void)startup;
    assert(strcmp(name, ASTRA_CAPABILITY_HOST_DEVICE) == 0);
    return present ? &device : NULL;
}

uint32_t astra_event_emit_packed(const AstraEventDescriptor *descriptor,
    uint32_t level, const uint32_t *values, uint32_t count)
{
    (void)level; (void)values; (void)count;
    matched += strstr(descriptor->format, "match bytes") != NULL;
    mismatched += strstr(descriptor->format, "mismatch") != NULL;
    absent += strstr(descriptor->format, "absent") != NULL;
    cleanup_failed += strstr(descriptor->format, "cleanup failed") != NULL;
    return ASTRA_SYSCALL_OK;
}

int astra_vfs_host_transport_init(AstraVfsHostTransport *transport,
    uint32_t handle, AstraVfsStateAcquire acquire, AstraVfsStateRelease release, void *context)
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

uint32_t astra_vfs_host_claim(AstraVfsHostTransport *transport,
    uint32_t generation, int release, uint32_t *epoch)
{
    (void)transport;
    assert(generation == 9u && epoch == NULL);
    if (!release) {
        assert(!claimed);
        claimed = claim_status == ASTRA_STATUS_OK;
        return claim_status;
    }
    ++releases;
    if (release_status == ASTRA_STATUS_OK)
        claimed = file_live = session_live = 0;
    return release_status;
}

uint32_t astra_vfs_host_session_open(AstraVfsHostTransport *transport,
    uint32_t generation, uint32_t parent, uint32_t volume,
    const char *root, uint16_t flags, uint32_t *session)
{
    (void)transport;
    assert(claimed && generation == 9u && parent == 0u &&
        volume == ASTRA_HOST_FS_VOLUME_SYSTEM && strcmp(root, "/") == 0 &&
        flags == ASTRA_HOST_FS_SESSION_READ_ONLY);
    if (session_status == ASTRA_STATUS_OK) {
        *session = 3u;
        session_live = 1;
    }
    return session_status;
}

uint32_t astra_vfs_host_session_close(AstraVfsHostTransport *transport,
    uint32_t generation, uint32_t session)
{
    (void)transport;
    assert(generation == 9u && session == 3u);
    ++session_closes;
    if (session_close_status == ASTRA_STATUS_OK)
        session_live = file_live = 0;
    return session_close_status;
}

int astra_vfs_host_init(AstraVfsHostBackend *backend,
    AstraVfsHostTransport *transport, uint32_t generation, uint32_t session)
{
    (void)backend; (void)transport;
    assert(generation == 9u && session == 3u);
    return 1;
}

static uint32_t open_node(void *context, const char *path, uint32_t flags,
    uint16_t mode, uintptr_t *node, AstraVfsNodeInfo *info)
{
    (void)context;
    assert(session_live && strcmp(path, "/startup/system") == 0 &&
        flags == ASTRA_VFS_OPEN_READ && mode == ASTRA_VFS_MODE_DEFAULT);
    if (open_status == ASTRA_STATUS_OK) {
        *node = 1u;
        info->size = payload_size;
        file_live = 1;
    }
    return open_status;
}

static uint32_t read_node(void *context, uintptr_t node, uint64_t offset,
    void *bytes, uint32_t length, uint32_t *moved)
{
    (void)context;
    assert(file_live && node == 1u && offset + length <= payload_size);
    ++reads;
    *moved = short_read ? length - 1u : length;
    memcpy(bytes, payload + offset, *moved);
    return read_status;
}

static uint32_t close_node(void *context, uintptr_t node)
{
    (void)context;
    assert(file_live && node == 1u);
    ++closes;
    if (close_status == ASTRA_STATUS_OK)
        file_live = 0;
    return close_status;
}

const AstraVfsBackendOps *astra_vfs_host_ops(void)
{
    static const AstraVfsBackendOps ops = {
        .open = open_node, .read = read_node, .close = close_node
    };
    return &ops;
}

static void reset(void)
{
    memset(&device, 0, sizeof(device));
    device.handle = 7u;
    memset(payload, 'x', sizeof(payload));
    payload_size = sizeof(payload);
    reads = closes = session_closes = releases = destroys = 0u;
    open_status = read_status = close_status = session_status = release_status = ASTRA_STATUS_OK;
    session_close_status = claim_status = ASTRA_STATUS_OK;
    present = initialized = 1;
    short_read = claimed = file_live = session_live = 0;
    matched = mismatched = absent = cleanup_failed = 0;
}

int main(void)
{
    uint8_t expected[sizeof(payload)];
    memset(expected, 'x', sizeof(expected));
    reset();
    assert(supervisor_bootstrap_probe(NULL, expected, sizeof(expected)) == ASTRA_STATUS_OK);
    assert(matched == 1 && reads == 3u && closes == 1u && session_closes == 1u &&
        releases == 1u && destroys == 1u && !claimed && !file_live && !session_live);
    reset(); payload[8192] = 'y';
    assert(supervisor_bootstrap_probe(NULL, expected, sizeof(expected)) == ASTRA_STATUS_OK);
    assert(mismatched == 1 && closes == 1u && !claimed);
    reset(); --payload_size;
    assert(supervisor_bootstrap_probe(NULL, expected, sizeof(expected)) == ASTRA_STATUS_OK);
    assert(mismatched == 1 && reads == 0u && !claimed);
    reset(); short_read = 1;
    assert(supervisor_bootstrap_probe(NULL, expected, sizeof(expected)) == ASTRA_STATUS_OK);
    assert(mismatched == 1 && closes == 1u && !claimed);
    reset(); session_status = ASTRA_STATUS_NOT_FOUND;
    assert(supervisor_bootstrap_probe(NULL, expected, sizeof(expected)) == ASTRA_STATUS_OK);
    assert(absent == 1 && releases == 1u && session_closes == 0u && !claimed);
    reset(); open_status = ASTRA_STATUS_NOT_FOUND;
    assert(supervisor_bootstrap_probe(NULL, expected, sizeof(expected)) == ASTRA_STATUS_OK);
    assert(absent == 1 && session_closes == 1u && !claimed);
    reset(); read_status = ASTRA_STATUS_IO;
    assert(supervisor_bootstrap_probe(NULL, expected, sizeof(expected)) == ASTRA_STATUS_OK);
    assert(absent == 1 && closes == 1u && !claimed);
    reset(); close_status = ASTRA_STATUS_IO;
    assert(supervisor_bootstrap_probe(NULL, expected, sizeof(expected)) == ASTRA_STATUS_IO);
    assert(cleanup_failed == 1 && releases == 1u && !file_live && !claimed);
    reset(); session_close_status = ASTRA_STATUS_IO;
    assert(supervisor_bootstrap_probe(NULL, expected, sizeof(expected)) == ASTRA_STATUS_IO);
    assert(cleanup_failed == 1 && releases == 1u && !session_live && !claimed);
    reset(); release_status = ASTRA_STATUS_IO;
    assert(supervisor_bootstrap_probe(NULL, expected, sizeof(expected)) == ASTRA_STATUS_IO);
    assert(cleanup_failed == 1 && claimed && destroys == 1u);
    reset(); present = 0;
    assert(supervisor_bootstrap_probe(NULL, expected, sizeof(expected)) == ASTRA_STATUS_OK);
    assert(absent == 1 && destroys == 0u && !claimed);
    reset(); initialized = 0;
    assert(supervisor_bootstrap_probe(NULL, expected, sizeof(expected)) == ASTRA_STATUS_OK);
    assert(absent == 1 && releases == 0u && !claimed);
    reset(); claim_status = ASTRA_STATUS_ACCESS;
    assert(supervisor_bootstrap_probe(NULL, expected, sizeof(expected)) == ASTRA_STATUS_OK);
    assert(absent == 1 && releases == 0u && destroys == 1u && !claimed);
    return 0;
}
