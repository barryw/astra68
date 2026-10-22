#ifndef ASTRA_STORAGE_CATALOG_H
#define ASTRA_STORAGE_CATALOG_H

#include <stddef.h>
#include <stdint.h>
#include <astra/storage_provider.h>

static inline int astra_storage_format_read_only(uint32_t format)
{
    return format == ASTRA_VOLUME_FAT12 || format == ASTRA_VOLUME_FAT16 ||
           format == ASTRA_VOLUME_ISO9660 || format == ASTRA_VOLUME_UDF;
}

/* Linux provider state, not an application ABI. The event loop owns mutations.
 * QEMU continues to own file descriptions and their usage accounting. */
typedef struct AstraStorageEntry AstraStorageEntry;
typedef struct AstraStorageCatalog {
    AstraStorageEntry *entries;
    size_t byte_limit;
    size_t bytes;
    uint64_t next_id;
    uint64_t sequence;
} AstraStorageCatalog;

int astra_storage_catalog_init(AstraStorageCatalog *catalog, size_t byte_limit);
void astra_storage_catalog_destroy(AstraStorageCatalog *catalog);
const AstraStorageRecord *astra_storage_find(const AstraStorageCatalog *catalog,
                                            uint64_t id);
/* Approved directory exports are bindings, not fabricated disks. */
int astra_storage_binding(AstraStorageCatalog *catalog, uint64_t mount,
                          uint64_t generation, uint64_t linux_mount_id,
                          const char *name, uint32_t flags, uint64_t *id);
int astra_storage_device(AstraStorageCatalog *catalog, uint32_t class_id,
                         uint64_t *id);
int astra_storage_medium(AstraStorageCatalog *catalog, uint64_t device,
                         uint64_t generation, uint64_t *id);
int astra_storage_source(AstraStorageCatalog *catalog, uint64_t medium,
                         uint64_t generation, uint64_t *id);
/* Configured tmpfs is a source, not a fabricated physical disk or medium. */
int astra_storage_ram_source(AstraStorageCatalog *catalog, uint32_t flags, uint64_t *id);
/* Trusted launcher selection, not a fabricated physical device/medium. */
int astra_storage_system_source(AstraStorageCatalog *catalog, uint64_t *id);
/* An image pins an exact file description and its backing mount. */
int astra_storage_image_source(AstraStorageCatalog *catalog, uint64_t mount,
                               uint64_t generation, uint64_t backing_object,
                               uint64_t *id);
int astra_storage_probe_result(AstraStorageCatalog *catalog, uint64_t source,
                               uint64_t generation, uint32_t format,
                               const char *uuid, const char *label,
                               int ambiguous, int error);
int astra_storage_mount_begin(AstraStorageCatalog *catalog, uint64_t source,
                              uint64_t generation, uint32_t flags,
                              uint64_t byte_limit, uint64_t inode_limit,
                              uint64_t *mount);
/* A nonzero Linux mount ID is retained even when publication is refused:
 * the caller must roll that real mount back before discarding its record. */
int astra_storage_mount_result(AstraStorageCatalog *catalog, uint64_t mount,
                               uint64_t generation, uint64_t linux_mount_id,
                               int error);
int astra_storage_quiesce(AstraStorageCatalog *catalog, uint64_t mount,
                          uint64_t generation);
/* Eject fences a physical optical source after every mount actually retired.
 * Failed ioctl restores admission; success retires the whole inserted medium. */
int astra_storage_eject_begin(AstraStorageCatalog *catalog, uint64_t source,
    uint64_t generation);
int astra_storage_eject_result(AstraStorageCatalog *catalog, uint64_t source,
    uint64_t generation, int error);
/* Includes an admitted mount worker and retained real cleanup, not just
 * publicly usable mounts. Used by source release and optical eject. */
int astra_storage_no_mounted_children(const AstraStorageCatalog *catalog, uint64_t source);
/* Called after real Linux unmount or refusal, never for a lazy detach. */
int astra_storage_unmount_result(AstraStorageCatalog *catalog, uint64_t mount,
                                 uint64_t generation, int error,
                                 int linux_mount_present, int authority_intact);
int astra_storage_loss(AstraStorageCatalog *catalog, uint64_t id,
                       uint64_t generation);
/* Removes terminal records only after all child records have been released. */
int astra_storage_forget(AstraStorageCatalog *catalog, uint64_t id);
int astra_storage_can_forget(AstraStorageCatalog *catalog, uint64_t id);
/* The provider may retry real cleanup while its pending flag is still set.
 * This checks the same terminal state and child graph, excluding that flag. */
int astra_storage_can_release(AstraStorageCatalog *catalog, uint64_t id);
/* Publish an actual resource-release failure, or clear it after real cleanup. */
int astra_storage_release_result(AstraStorageCatalog *catalog, uint64_t id,
    uint64_t generation, int error);
/* Sequence-checked keyset pagination; sequence zero starts a new snapshot. */
int astra_storage_snapshot(const AstraStorageCatalog *catalog,
                           uint64_t sequence, uint64_t after,
                           AstraStorageRecord *records, size_t capacity,
                           size_t *count, uint64_t *next, uint64_t *current);

#endif
