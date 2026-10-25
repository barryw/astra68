/* Exercise the provider's canonical cleanup boundary with failed Linux
 * rollback. Unused provider functions are discarded by this focused gate. */
#define astra_storage_linux_cleanup test_linux_cleanup
#include "astra_storage_provider.c"
#undef astra_storage_linux_cleanup

#include <assert.h>

static int cleanup_error;
static unsigned cleanup_calls;

int test_linux_cleanup(AstraStorageMounted *mounted)
{
    ++cleanup_calls;
    if (cleanup_error != 0) return cleanup_error;
    mounted->attached = mounted->directory = 0;
    mounted->root = mounted->loop = mounted->backing = -1;
    return 0;
}

static void failed_rollback(int failed_cleanup)
{
    AstraStorageProvider provider = {0};
    uint64_t source, mount;
    assert(astra_storage_catalog_init(&provider.catalog, 1024u * 1024u) == 0);
    assert(astra_storage_ram_source(&provider.catalog, 0u, &source) == 0);
    assert(astra_storage_mount_begin(&provider.catalog, source, 1u, 0u, 1024u, 16u, UINT32_MAX, &mount) == 0);
    assert(astra_storage_mount_result(&provider.catalog, mount, 1u, 0u, EIO) == EIO);
    AstraStorageResource entry = {.id = mount, .generation = 1u,
        .charged_bytes = 1024u, .charged_inodes = 16u,
        .mounted = {.directory = 1, .attached = failed_cleanup == 2,
            .root = -1, .loop = -1, .backing = -1}};
    provider.ram_bytes = entry.charged_bytes;
    provider.ram_inodes = entry.charged_inodes;
    cleanup_error = failed_cleanup ? ENOTEMPTY : 0;
    assert(cleanup_mount_resource(&provider, &entry, 1) == cleanup_error);
    const AstraStorageRecord *record = astra_storage_find(&provider.catalog, mount);
    if (failed_cleanup) {
        assert(record->state == ASTRA_VOLUME_FAILED);
        assert(record->error == ENOTEMPTY);
        assert((record->flags & ASTRA_VOLUME_CLEANUP_PENDING) != 0u);
        assert(provider.ram_bytes == 1024u && provider.ram_inodes == 16u);
        assert(astra_storage_can_forget(&provider.catalog, mount) == EBUSY);
        assert(astra_storage_no_mounted_children(&provider.catalog, source) == EBUSY);
        uint64_t refused;
        assert(astra_storage_mount_begin(&provider.catalog, source, 1u, 0u, 1024u, 16u, UINT32_MAX, &refused) == EBUSY);
        cleanup_error = 0;
        assert(cleanup_mount_resource(&provider, &entry, 0) == 0);
        record = astra_storage_find(&provider.catalog, mount);
        assert(record->state == ASTRA_VOLUME_UNMOUNTED && record->error == 0);
        assert((record->flags & ASTRA_VOLUME_CLEANUP_PENDING) == 0u);
    } else {
        /* Successful rollback must retain the failed admission's real error. */
        assert(record->state == ASTRA_VOLUME_FAILED && record->error == EIO);
        assert((record->flags & ASTRA_VOLUME_CLEANUP_PENDING) == 0u);
    }
    assert(provider.ram_bytes == 0u && provider.ram_inodes == 0u);
    assert(entry.charged_bytes == 0u && entry.charged_inodes == 0u);
    assert(astra_storage_can_forget(&provider.catalog, mount) == 0);
    assert(cleanup_mount_resource(&provider, &entry, 1) == 0);
    assert(provider.ram_bytes == 0u && provider.ram_inodes == 0u);
    astra_storage_catalog_destroy(&provider.catalog);
}

static void detached_cleanup_retry(void)
{
    AstraStorageProvider provider = {.resource_limit = 1024u * 1024u, .worker_limit = 1u};
    uint64_t source, mount;
    assert(astra_storage_catalog_init(&provider.catalog, 1024u * 1024u) == 0);
    assert(astra_storage_ram_source(&provider.catalog, 0u, &source) == 0);
    assert(astra_storage_mount_begin(&provider.catalog, source, 1u, 0u, 1024u, 16u, UINT32_MAX, &mount) == 0);
    assert(astra_storage_mount_result(&provider.catalog, mount, 1u, 123u, 0) == 0);
    assert(astra_storage_quiesce(&provider.catalog, mount, 1u) == 0);
    AstraStorageResource *entry = reserve(&provider, mount, 1u);
    assert(entry != NULL);
    /* The detach worker removed the Linux mount; directory/loop cleanup failed. */
    entry->mounted.directory = 1;
    entry->charged_bytes = provider.ram_bytes = 1024u;
    entry->charged_inodes = provider.ram_inodes = 16u;
    cleanup_error = EBUSY;
    assert(cleanup_mount_resource(&provider, entry, 0) == EBUSY);
    const AstraStorageRecord *record = astra_storage_find(&provider.catalog, mount);
    assert(record->state == ASTRA_VOLUME_FAILED && record->linux_mount_id == 0u);
    assert((record->flags & ASTRA_VOLUME_CLEANUP_PENDING) != 0u);
    unsigned before = cleanup_calls;
    assert(astra_storage_provider_reap(&provider) == EAGAIN);
    assert(cleanup_calls == before + 1u);
    assert(provider.resources == entry && provider.ram_bytes == 1024u);
    assert(astra_storage_quiesce(&provider.catalog, mount, 1u) == EBUSY);
    cleanup_error = 0;
    assert(astra_storage_provider_reap(&provider) == 0);
    assert(cleanup_calls == before + 2u);
    assert(astra_storage_find(&provider.catalog, mount) == NULL);
    assert(provider.resources == NULL && provider.resource_bytes == 0u);
    assert(provider.ram_bytes == 0u && provider.ram_inodes == 0u);
    assert(astra_storage_find(&provider.catalog, source) != NULL);
    assert(astra_storage_provider_reap(&provider) == 0 && cleanup_calls == before + 2u);
    astra_storage_catalog_destroy(&provider.catalog);
}

int main(void)
{
    failed_rollback(0);
    failed_rollback(1);
    failed_rollback(2);
    detached_cleanup_retry();
    puts("provider failed rollback, visible cleanup, retained quotas and retry PASS");
    return 0;
}
