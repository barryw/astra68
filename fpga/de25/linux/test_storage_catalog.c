#include "astra_storage_catalog.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static uint64_t source(AstraStorageCatalog *catalog, uint32_t class_id,
                        uint32_t format, const char *uuid, const char *label)
{
    uint64_t device;
    uint64_t medium;
    uint64_t result;
    assert(astra_storage_device(catalog, class_id, &device) == 0);
    assert(astra_storage_medium(catalog, device, 1u, &medium) == 0);
    assert(astra_storage_source(catalog, medium, 1u, &result) == 0);
    assert(astra_storage_probe_result(catalog, result, 1u, format,
                                      uuid, label, 0, 0) == 0);
    return result;
}

static void lifecycle(void)
{
    AstraStorageCatalog catalog;
    uint64_t data;
    uint64_t mounted;
    uint64_t other = 123u;
    assert(astra_storage_catalog_init(&catalog, 1024u * 1024u) == 0);
    data = source(&catalog, ASTRA_VOLUME_USB, ASTRA_VOLUME_EXT4,
                   "data-uuid", "Files");
    assert(astra_storage_mount_begin(&catalog, data, 1u, 0u, 0u, 0u,
                                     &mounted) == 0);
    assert(astra_storage_mount_begin(&catalog, data, 1u, 0u, 0u, 0u,
                                     &other) == EBUSY && other == 0u);
    assert(astra_storage_quiesce(&catalog, mounted, 1u) == EBUSY);
    assert(astra_storage_mount_result(&catalog, mounted, 1u, 0u, 0) == EINVAL);
    assert(astra_storage_mount_result(&catalog, mounted, 1u, 321u, 0) == 0);
    assert(astra_storage_mount_result(&catalog, mounted, 1u, 321u, 0) == ESTALE);
    assert(astra_storage_find(&catalog, mounted)->state == ASTRA_VOLUME_MOUNTED);
    assert(astra_storage_quiesce(&catalog, mounted, 9u) == ESTALE);
    assert(astra_storage_quiesce(&catalog, mounted, 1u) == 0);
    assert(astra_storage_unmount_result(&catalog, mounted, 1u, EAGAIN, 0, 0) == EAGAIN);
    assert(astra_storage_find(&catalog, mounted)->state == ASTRA_VOLUME_FAILED);
    assert(astra_storage_quiesce(&catalog, mounted, 1u) == EBUSY);
    assert((astra_storage_find(&catalog, mounted)->flags & ASTRA_VOLUME_CLEANUP_PENDING) != 0u);
    assert(astra_storage_forget(&catalog, mounted) == EBUSY);
    assert(astra_storage_unmount_result(&catalog, mounted, 1u, 0, 0, 0) == 0);
    assert((astra_storage_find(&catalog, mounted)->flags & ASTRA_VOLUME_CLEANUP_PENDING) == 0u);
    assert(astra_storage_forget(&catalog, mounted) == 0);
    assert(astra_storage_mount_begin(&catalog, data, 1u, 0u, 0u, 0u, &mounted) == 0);
    assert(astra_storage_mount_result(&catalog, mounted, 1u, 321u, 0) == 0);
    assert(astra_storage_quiesce(&catalog, mounted, 1u) == 0);
    assert(astra_storage_quiesce(&catalog, mounted, 1u) == EBUSY);
    assert(astra_storage_unmount_result(&catalog, mounted, 1u, EBUSY, 1, 1) == EBUSY);
    assert(astra_storage_find(&catalog, mounted)->state == ASTRA_VOLUME_MOUNTED);
    assert(astra_storage_find(&catalog, mounted)->linux_mount_id == 321u);
    assert(astra_storage_quiesce(&catalog, mounted, 1u) == 0);
    assert(astra_storage_unmount_result(&catalog, mounted, 1u, EIO, 1, 1) == EIO);
    assert(astra_storage_find(&catalog, mounted)->state == ASTRA_VOLUME_MOUNTED);
    assert(astra_storage_quiesce(&catalog, mounted, 1u) == 0);
    assert(astra_storage_unmount_result(&catalog, mounted, 1u, 0, 1, 1) == EPROTO);
    assert(astra_storage_find(&catalog, mounted)->state == ASTRA_VOLUME_QUIESCING);
    assert(astra_storage_unmount_result(&catalog, mounted, 1u, 0, 0, 0) == 0);
    assert(astra_storage_find(&catalog, mounted)->state == ASTRA_VOLUME_UNMOUNTED);
    assert(astra_storage_find(&catalog, mounted)->linux_mount_id == 0u);
    assert(astra_storage_forget(&catalog, mounted) == 0);
    assert(astra_storage_mount_begin(&catalog, data, 1u, 0u, 0u, 0u,
                                     &other) == 0 && other > mounted);
    assert(astra_storage_mount_result(&catalog, other, 1u, 0u, EIO) == EIO);
    assert(astra_storage_find(&catalog, other)->state == ASTRA_VOLUME_FAILED);
    assert(astra_storage_forget(&catalog, other) == 0);
    assert(astra_storage_mount_begin(&catalog, data, 1u, 0u, 0u, 0u,
                                     &other) == 0);
    assert(astra_storage_mount_result(&catalog, other, 1u, 322u, EIO) == EIO);
    assert((astra_storage_find(&catalog, other)->flags & ASTRA_VOLUME_CLEANUP_PENDING) != 0u);
    assert(astra_storage_unmount_result(&catalog, other, 1u, EBUSY, 1, 1) == EBUSY);
    assert(astra_storage_find(&catalog, other)->state == ASTRA_VOLUME_FAILED);
    assert(astra_storage_forget(&catalog, other) == EBUSY);
    assert(astra_storage_unmount_result(&catalog, other, 1u, 0, 0, 0) == 0);
    assert(astra_storage_forget(&catalog, other) == 0);
    astra_storage_catalog_destroy(&catalog);
    assert(catalog.bytes == 0u && catalog.entries == NULL);
}

static void loss_and_stale_completion(void)
{
    AstraStorageCatalog catalog;
    uint64_t device;
    uint64_t medium;
    uint64_t data;
    uint64_t mounted;
    uint64_t replacement;
    size_t charge;
    assert(astra_storage_catalog_init(&catalog, 1024u * 1024u) == 0);
    assert(astra_storage_device(&catalog, ASTRA_VOLUME_USB, &device) == 0);
    charge = catalog.bytes;
    assert(astra_storage_medium(&catalog, device, 1u, &medium) == 0);
    assert(astra_storage_medium(&catalog, device, 1u, &replacement) == EBUSY);
    assert(astra_storage_source(&catalog, medium, 1u, &data) == 0);
    assert(astra_storage_probe_result(&catalog, data, 1u, ASTRA_VOLUME_EXT4,
                                      "uuid", "Original", 0, 0) == 0);
    assert(astra_storage_mount_begin(&catalog, data, 1u, 0u, 0u, 0u,
                                     &mounted) == 0);
    assert(astra_storage_loss(&catalog, medium, 1u) == 0);
    assert(astra_storage_find(&catalog, data)->generation == 2u);
    assert(astra_storage_find(&catalog, mounted)->generation == 2u);
    assert(astra_storage_probe_result(&catalog, data, 1u, ASTRA_VOLUME_EXT4,
                                      "uuid", "Late", 0, 0) == ESTALE);
    assert(astra_storage_forget(&catalog, mounted) == EBUSY);
    /* Linux success after removal stays charged for actual rollback. */
    assert(astra_storage_mount_result(&catalog, mounted, 1u, 99u, 0) == ENODEV);
    assert(astra_storage_find(&catalog, mounted)->linux_mount_id == 99u);
    assert(astra_storage_find(&catalog, mounted)->state == ASTRA_VOLUME_FAILED);
    assert(astra_storage_forget(&catalog, mounted) == EBUSY);
    uint64_t sequence = catalog.sequence;
    assert(astra_storage_quiesce(&catalog, mounted, 1u) == ESTALE);
    assert(catalog.sequence == sequence);
    assert(astra_storage_unmount_result(&catalog, mounted, 1u, EBUSY, 1, 1) == ESTALE);
    assert(astra_storage_find(&catalog, mounted)->state == ASTRA_VOLUME_FAILED);
    assert(astra_storage_unmount_result(&catalog, mounted, 2u, EBUSY, 1, 1) == EBUSY);
    assert(astra_storage_find(&catalog, mounted)->state == ASTRA_VOLUME_FAILED);
    assert(astra_storage_unmount_result(&catalog, mounted, 2u, 0, 0, 0) == 0);
    assert(astra_storage_forget(&catalog, medium) == EBUSY);
    assert(astra_storage_forget(&catalog, mounted) == 0);
    assert(astra_storage_forget(&catalog, data) == 0);
    assert(astra_storage_forget(&catalog, medium) == 0);
    assert(catalog.bytes == charge);
    assert(astra_storage_medium(&catalog, device, 1u, &replacement) == 0);
    assert(replacement > mounted);
    assert(astra_storage_source(&catalog, medium, 1u, &data) == ESTALE);
    assert(astra_storage_loss(&catalog, device, 1u) == 0);
    assert(astra_storage_medium(&catalog, device, 1u, &medium) == ESTALE);
    astra_storage_catalog_destroy(&catalog);
}

static void policy_and_metadata(void)
{
    AstraStorageCatalog catalog;
    uint64_t first;
    uint64_t second;
    uint64_t optical;
    uint64_t mounted;
    uint64_t ram;
    assert(astra_storage_catalog_init(&catalog, 1024u * 1024u) == 0);
    first = source(&catalog, ASTRA_VOLUME_USB, ASTRA_VOLUME_FAT32,
                    "same-uuid", "same-label");
    second = source(&catalog, ASTRA_VOLUME_USB, ASTRA_VOLUME_EXFAT,
                     "same-uuid", "same-label");
    assert((astra_storage_find(&catalog, first)->flags &
            ASTRA_VOLUME_DUPLICATE_UUID) != 0u);
    assert((astra_storage_find(&catalog, second)->flags &
            ASTRA_VOLUME_DUPLICATE_UUID) != 0u);
    assert(astra_storage_loss(&catalog, second, 1u) == 0);
    assert((astra_storage_find(&catalog, first)->flags &
            ASTRA_VOLUME_DUPLICATE_UUID) == 0u);
    optical = source(&catalog, ASTRA_VOLUME_OPTICAL, ASTRA_VOLUME_ISO9660,
                      "", "Disc");
    assert(astra_storage_mount_begin(&catalog, optical, 1u, 0u, 0u, 0u,
                                     &mounted) == EROFS);
    assert(astra_storage_mount_begin(&catalog, optical, 1u,
        ASTRA_VOLUME_READ_ONLY, 0u, 0u, &mounted) == 0);
    assert(astra_storage_mount_result(&catalog, mounted, 1u, 23u, 0) == 0);
    assert((astra_storage_find(&catalog, mounted)->flags &
            ASTRA_VOLUME_READ_ONLY) != 0u);
    first = source(&catalog, ASTRA_VOLUME_USB, ASTRA_VOLUME_EXT4,
                    "", "broken\nlabel");
    assert(astra_storage_find(&catalog, first)->label[0] == '\0');
    assert((astra_storage_find(&catalog, first)->flags &
            ASTRA_VOLUME_LABEL_INVALID) != 0u);
    second = source(&catalog, ASTRA_VOLUME_USB, ASTRA_VOLUME_EXT4,
                     "", "\xf0\x9f\x93\x81 Documents");
    assert(strcmp(astra_storage_find(&catalog, second)->label,
                   "\xf0\x9f\x93\x81 Documents") == 0);
    first = source(&catalog, ASTRA_VOLUME_FIXED, ASTRA_VOLUME_EXT4, "sys", "DH0");
    assert(astra_storage_mount_begin(&catalog, first, 1u, ASTRA_VOLUME_SYSTEM,
                                     0u, 0u, &mounted) == 0);
    assert(astra_storage_mount_result(&catalog, mounted, 1u, 12u, 0) == 0);
    assert(astra_storage_quiesce(&catalog, mounted, 1u) == EPERM);
    assert(astra_storage_device(&catalog, ASTRA_VOLUME_RAM, &ram) == EINVAL);
    assert(astra_storage_ram_source(&catalog, 0u, &ram) == 0);
    assert(astra_storage_find(&catalog, ram)->parent == 0u);
    assert(astra_storage_mount_begin(&catalog, ram, 1u, 0u, 0u, 0u,
                                     &mounted) == EINVAL);
    assert(astra_storage_mount_begin(&catalog, ram, 1u, 0u,
                                     4096u, 1u, &mounted) == 0);
    assert(astra_storage_find(&catalog, mounted)->byte_limit == 4096u);
    assert(astra_storage_find(&catalog, mounted)->inode_limit == 1u);
    assert((astra_storage_find(&catalog, mounted)->flags &
            ASTRA_VOLUME_VOLATILE) != 0u);
    astra_storage_catalog_destroy(&catalog);
}

static void snapshots_and_budget(void)
{
    AstraStorageCatalog catalog;
    AstraStorageRecord records[2];
    uint64_t id;
    uint64_t next;
    uint64_t sequence;
    uint64_t current;
    size_t count;
    assert(astra_storage_catalog_init(&catalog, 0u) == EINVAL);
    assert(astra_storage_catalog_init(&catalog, 1024u * 1024u) == 0);
    (void)source(&catalog, ASTRA_VOLUME_USB, ASTRA_VOLUME_EXT4, "", "");
    assert(astra_storage_snapshot(&catalog, 0u, 0u, records, 2u,
                                   &count, &next, &sequence) == 0);
    assert(count == 2u && next == records[1].id);
    assert(astra_storage_snapshot(&catalog, sequence, next, records, 2u,
                                   &count, &next, &current) == 0);
    assert(count == 1u && next == 0u && current == sequence);
    assert(astra_storage_device(&catalog, ASTRA_VOLUME_FIXED, &id) == 0);
    assert(astra_storage_snapshot(&catalog, sequence, 2u, records, 2u,
                                   &count, &next, &current) == EAGAIN);
    assert(count == 0u && next == 0u);
    catalog.byte_limit = catalog.bytes;
    assert(astra_storage_device(&catalog, ASTRA_VOLUME_USB, &next) == ENOSPC);
    assert(astra_storage_loss(&catalog, id, 1u) == 0);
    assert(astra_storage_forget(&catalog, id) == 0);
    assert(astra_storage_device(&catalog, ASTRA_VOLUME_USB, &next) == 0);
    assert(next > id);
    catalog.next_id = UINT64_MAX;
    assert(astra_storage_device(&catalog, ASTRA_VOLUME_USB, &next) == ENOSPC);
    astra_storage_catalog_destroy(&catalog);
}

static void image_dependencies(void)
{
    AstraStorageCatalog catalog;
    uint64_t data;
    uint64_t backing;
    uint64_t image;
    uint64_t image_mount;
    uint64_t nested;
    uint64_t nested_mount;
    assert(astra_storage_catalog_init(&catalog, 1024u * 1024u) == 0);
    data = source(&catalog, ASTRA_VOLUME_USB, ASTRA_VOLUME_EXT4, "usb", "Files");
    assert(astra_storage_mount_begin(&catalog, data, 1u, 0u, 0u, 0u, &backing) == 0);
    assert(astra_storage_mount_result(&catalog, backing, 1u, 500u, 0) == 0);
    assert(astra_storage_image_source(&catalog, backing, 1u, 0u, &image) == EINVAL);
    assert(astra_storage_image_source(&catalog, backing, 1u, 812u, &image) == 0);
    assert(astra_storage_find(&catalog, image)->origin == ASTRA_VOLUME_ORIGIN_IMAGE);
    assert(astra_storage_find(&catalog, image)->backing_object == 812u);
    assert(astra_storage_probe_result(&catalog, image, 1u,
                                      ASTRA_VOLUME_ISO9660, "", "Install", 0, 0) == 0);
    assert(astra_storage_mount_begin(&catalog, image, 1u, 0u, 0u, 0u,
                                     &image_mount) == EROFS);
    assert(astra_storage_mount_begin(&catalog, image, 1u, ASTRA_VOLUME_READ_ONLY,
                                     0u, 0u, &image_mount) == 0);
    assert(astra_storage_mount_result(&catalog, image_mount, 1u, 501u, 0) == 0);
    assert(astra_storage_image_source(&catalog, image_mount, 1u, 813u, &nested) == 0);
    assert(astra_storage_probe_result(&catalog, nested, 1u,
                                      ASTRA_VOLUME_UDF, "", "Nested", 0, 0) == 0);
    assert(astra_storage_mount_begin(&catalog, nested, 1u, ASTRA_VOLUME_READ_ONLY,
                                     0u, 0u, &nested_mount) == 0);
    assert(astra_storage_mount_result(&catalog, nested_mount, 1u, 502u, 0) == 0);
    assert(astra_storage_quiesce(&catalog, backing, 1u) == 0);
    assert(astra_storage_image_source(&catalog, backing, 1u, 814u, &data) == EBUSY);
    assert(astra_storage_unmount_result(&catalog, backing, 1u, EBUSY, 1, 1) == EBUSY);
    assert(astra_storage_find(&catalog, backing)->state == ASTRA_VOLUME_MOUNTED);
    assert(astra_storage_loss(&catalog, backing, 1u) == 0);
    assert(astra_storage_find(&catalog, image_mount)->state == ASTRA_VOLUME_LOST);
    assert(astra_storage_find(&catalog, nested_mount)->state == ASTRA_VOLUME_LOST);
    assert(astra_storage_find(&catalog, nested_mount)->generation == 2u);
    assert(astra_storage_forget(&catalog, backing) == EBUSY);
    assert(astra_storage_quiesce(&catalog, image_mount, 1u) == ESTALE);
    astra_storage_catalog_destroy(&catalog);
}

static void names_and_bindings(void)
{
    AstraStorageCatalog catalog;
    uint64_t fixed, mounted, binding, ram, ram_mount, temporary;
    assert(astra_storage_catalog_init(&catalog, 1024u * 1024u) == 0);
    fixed = source(&catalog, ASTRA_VOLUME_FIXED, ASTRA_VOLUME_EXT4, "system", "Astra");
    assert(astra_storage_mount_begin(&catalog, fixed, 1u, ASTRA_VOLUME_SYSTEM,
                                     0u, 0u, &mounted) == 0);
    assert(strcmp(astra_storage_find(&catalog, mounted)->name, "/dh0") == 0);
    assert(astra_storage_mount_result(&catalog, mounted, 1u, 100u, 0) == 0);
    assert(astra_storage_binding(&catalog, mounted, 1u, 100u,
                                  "/home", 0u, &binding) == 0);
    assert(astra_storage_find(&catalog, binding)->kind == ASTRA_VOLUME_BINDING);
    assert(astra_storage_binding(&catalog, 0u, 0u, 200u,
                                  "/home", 0u, &ram) == EEXIST);
    assert(astra_storage_binding(&catalog, 0u, 0u, 200u,
                                  "/../host", 0u, &ram) == EINVAL);
    assert(astra_storage_binding(&catalog, 0u, 0u, 200u,
                                  "/dh0", 0u, &ram) == EINVAL);
    assert(astra_storage_ram_source(&catalog, 0u, &ram) == 0);
    assert(astra_storage_mount_begin(&catalog, ram, 1u, 0u,
                                     4096u, 16u, &ram_mount) == 0);
    assert(strcmp(astra_storage_find(&catalog, ram_mount)->name, "/ram0") == 0);
    assert(astra_storage_mount_result(&catalog, ram_mount, 1u, 101u, 0) == 0);
    assert(astra_storage_ram_source(&catalog, 0u, &ram) == 0);
    assert(astra_storage_mount_begin(&catalog, ram, 1u, ASTRA_VOLUME_TEMPORARY,
                                     4096u, 16u, &temporary) == 0);
    assert(strcmp(astra_storage_find(&catalog, temporary)->name, "/tmp") == 0);
    assert(astra_storage_mount_result(&catalog, temporary, 1u, 102u, 0) == 0);
    assert(astra_storage_quiesce(&catalog, temporary, 1u) == EPERM);
    assert(astra_storage_ram_source(&catalog, 0u, &ram) == 0);
    assert(astra_storage_mount_begin(&catalog, ram, 1u, ASTRA_VOLUME_TEMPORARY,
                                     4096u, 16u, &temporary) == EBUSY);
    assert(astra_storage_mount_begin(&catalog, ram, 1u, 0u,
                                     4096u, 16u, &temporary) == 0);
    assert(strcmp(astra_storage_find(&catalog, temporary)->name, "/ram1") == 0);
    assert(astra_storage_loss(&catalog, mounted, 1u) == 0);
    assert(astra_storage_find(&catalog, binding)->state == ASTRA_VOLUME_LOST);
    assert(astra_storage_forget(&catalog, binding) == EBUSY);
    assert(astra_storage_unmount_result(&catalog, binding, 2u, 0, 0, 0) == 0);
    assert(astra_storage_forget(&catalog, binding) == 0);
    astra_storage_catalog_destroy(&catalog);
}

static void refused_authority_restore(void)
{
    AstraStorageCatalog catalog;
    uint64_t data, mounted;
    assert(astra_storage_catalog_init(&catalog, 1024u * 1024u) == 0);
    data = source(&catalog, ASTRA_VOLUME_USB, ASTRA_VOLUME_EXT4, "data", "Files");
    assert(astra_storage_mount_begin(&catalog, data, 1u, 0u, 0u, 0u, &mounted) == 0);
    assert(astra_storage_mount_result(&catalog, mounted, 1u, 101u, 0) == 0);
    assert(astra_storage_quiesce(&catalog, mounted, 1u) == 0);
    assert(astra_storage_unmount_result(&catalog, mounted, 1u, EIO, 1, 0) == EIO);
    assert(astra_storage_find(&catalog, mounted)->state == ASTRA_VOLUME_FAILED);
    assert(astra_storage_forget(&catalog, mounted) == EBUSY);
    uint64_t sequence = catalog.sequence;
    assert(astra_storage_quiesce(&catalog, mounted, 1u) == EBUSY);
    assert(catalog.sequence == sequence);
    assert(astra_storage_unmount_result(&catalog, mounted, 1u, EBUSY, 1, 1) == EBUSY);
    assert(astra_storage_find(&catalog, mounted)->state == ASTRA_VOLUME_FAILED);
    assert(astra_storage_unmount_result(&catalog, mounted, 1u, 0, 0, 0) == 0);
    assert(astra_storage_forget(&catalog, mounted) == 0);
    astra_storage_catalog_destroy(&catalog);
}

static void optical_eject(void)
{
    AstraStorageCatalog catalog;
    uint64_t optical, mount;
    assert(astra_storage_catalog_init(&catalog, 1024u * 1024u) == 0);
    optical = source(&catalog, ASTRA_VOLUME_OPTICAL, ASTRA_VOLUME_ISO9660, "disc", "Disc");
    uint64_t medium = astra_storage_find(&catalog, optical)->parent;
    assert(astra_storage_eject_begin(&catalog, optical, 2u) == ESTALE);
    assert(astra_storage_mount_begin(&catalog, optical, 1u, ASTRA_VOLUME_READ_ONLY, 0u, 0u, &mount) == 0);
    assert(astra_storage_eject_begin(&catalog, optical, 1u) == EBUSY);
    assert(astra_storage_mount_result(&catalog, mount, 1u, 72u, 0) == 0);
    assert(astra_storage_eject_begin(&catalog, optical, 1u) == EBUSY);
    assert(astra_storage_quiesce(&catalog, mount, 1u) == 0);
    assert(astra_storage_eject_begin(&catalog, optical, 1u) == EBUSY);
    assert(astra_storage_unmount_result(&catalog, mount, 1u, 0, 0, 0) == 0);
    assert(astra_storage_eject_begin(&catalog, optical, 1u) == 0);
    assert(astra_storage_find(&catalog, optical)->state == ASTRA_VOLUME_QUIESCING);
    assert(astra_storage_eject_result(&catalog, optical, 1u, ENOTTY) == 0);
    assert(astra_storage_find(&catalog, optical)->state == ASTRA_VOLUME_UNMOUNTED &&
        astra_storage_find(&catalog, optical)->error == ENOTTY);
    assert(astra_storage_eject_begin(&catalog, optical, 1u) == 0);
    assert(astra_storage_eject_result(&catalog, optical, 1u, 0) == 0);
    assert(astra_storage_find(&catalog, medium)->state == ASTRA_VOLUME_LOST);
    assert(astra_storage_find(&catalog, optical)->state == ASTRA_VOLUME_LOST &&
        astra_storage_find(&catalog, optical)->generation == 2u);
    assert(astra_storage_eject_result(&catalog, optical, 1u, EIO) == ESTALE);
    uint64_t ram;
    assert(astra_storage_ram_source(&catalog, 0u, &ram) == 0);
    assert(astra_storage_eject_begin(&catalog, ram, 1u) == ENOTSUP);
    astra_storage_catalog_destroy(&catalog);
}

static void reusable_mount_names(void)
{
    AstraStorageCatalog catalog;
    uint64_t source_id, first, next;
    assert(astra_storage_catalog_init(&catalog, 1024u * 1024u) == 0);
    source_id = source(&catalog, ASTRA_VOLUME_USB, ASTRA_VOLUME_EXT4, "usb", "Files");
    assert(astra_storage_mount_begin(&catalog, source_id, 1u, 0u, 0u, 0u, &first) == 0);
    assert(strcmp(astra_storage_find(&catalog, first)->name, "/usb0") == 0);
    assert(astra_storage_mount_result(&catalog, first, 1u, 82u, 0) == 0);
    assert(astra_storage_quiesce(&catalog, first, 1u) == 0);
    assert(astra_storage_unmount_result(&catalog, first, 1u, 0, 0, 0) == 0);
    assert(astra_storage_mount_begin(&catalog, source_id, 1u, 0u, 0u, 0u, &next) == 0);
    assert(next != first && strcmp(astra_storage_find(&catalog, next)->name, "/usb0") == 0);
    assert(astra_storage_mount_result(&catalog, next, 1u, 83u, 0) == 0);
    uint64_t another = source(&catalog, ASTRA_VOLUME_USB, ASTRA_VOLUME_EXT4, "other", "Other");
    assert(astra_storage_loss(&catalog, next, 1u) == 0);
    uint64_t second;
    assert(astra_storage_mount_begin(&catalog, another, 1u, 0u, 0u, 0u, &second) == 0);
    assert(strcmp(astra_storage_find(&catalog, second)->name, "/usb1") == 0);
    assert(astra_storage_unmount_result(&catalog, next, 2u, 0, 0, 0) == 0);
    assert(astra_storage_mount_result(&catalog, second, 1u, 84u, 0) == 0);
    assert(astra_storage_quiesce(&catalog, second, 1u) == 0);
    assert(astra_storage_unmount_result(&catalog, second, 1u, 0, 0, 0) == 0);
    for (unsigned repeat = 0u; repeat < 100u; ++repeat) {
        uint64_t mount;
        assert(astra_storage_mount_begin(&catalog, another, 1u, 0u, 0u, 0u, &mount) == 0);
        assert(strcmp(astra_storage_find(&catalog, mount)->name, "/usb0") == 0);
        assert(astra_storage_mount_result(&catalog, mount, 1u, 100u + repeat, 0) == 0);
        assert(astra_storage_quiesce(&catalog, mount, 1u) == 0);
        assert(astra_storage_unmount_result(&catalog, mount, 1u, 0, 0, 0) == 0);
        assert(astra_storage_forget(&catalog, mount) == 0);
    }
    astra_storage_catalog_destroy(&catalog);
}

static void configured_ram_source(void)
{
    AstraStorageCatalog catalog;
    uint64_t configured, duplicate, other, mount;
    assert(astra_storage_catalog_init(&catalog, 1024u * 1024u) == 0);
    assert(astra_storage_ram_source(&catalog, ASTRA_VOLUME_CONFIGURED_RAM, &configured) == 0);
    assert(astra_storage_ram_source(&catalog, ASTRA_VOLUME_CONFIGURED_RAM, &duplicate) == EEXIST);
    assert(astra_storage_ram_source(&catalog, ASTRA_VOLUME_SYSTEM, &other) == EINVAL);
    assert(astra_storage_ram_source(&catalog, 0u, &other) == 0);
    assert(astra_storage_find(&catalog, other)->flags == 0u);
    assert(astra_storage_mount_begin(&catalog, configured, 1u, 0u, 1024u, 16u, &mount) == 0);
    assert(astra_storage_mount_result(&catalog, mount, 1u, 85u, 0) == 0);
    assert(astra_storage_quiesce(&catalog, mount, 1u) == 0);
    assert(astra_storage_unmount_result(&catalog, mount, 1u, 0, 0, 0) == 0);
    const AstraStorageRecord *source_record = astra_storage_find(&catalog, configured);
    assert(source_record->generation == 1u && source_record->kind == ASTRA_VOLUME_SOURCE);
    assert((source_record->flags & ASTRA_VOLUME_CONFIGURED_RAM) != 0u);
    assert(source_record->origin == ASTRA_VOLUME_ORIGIN_RAM);
    assert(astra_storage_ram_source(&catalog, ASTRA_VOLUME_CONFIGURED_RAM, &duplicate) == EEXIST);
    astra_storage_catalog_destroy(&catalog);
}

static void retained_release(void)
{
    AstraStorageCatalog catalog;
    uint64_t ram;
    assert(astra_storage_catalog_init(&catalog, 1024u * 1024u) == 0);
    assert(astra_storage_ram_source(&catalog, 0u, &ram) == 0);
    assert(astra_storage_release_result(&catalog, ram, 1u, EIO) == 0);
    assert(astra_storage_loss(&catalog, ram, 1u) == 0);
    assert(astra_storage_release_result(&catalog, ram, 1u, EIO) == ESTALE);
    assert(astra_storage_release_result(&catalog, ram, 2u, EIO) == 0);
    uint64_t sequence = catalog.sequence;
    assert(astra_storage_release_result(&catalog, ram, 2u, EIO) == 0 && catalog.sequence == sequence);
    assert(astra_storage_find(&catalog, ram)->error == EIO);
    assert((astra_storage_find(&catalog, ram)->flags & ASTRA_VOLUME_CLEANUP_PENDING) != 0u);
    assert(astra_storage_can_forget(&catalog, ram) == EBUSY);
    assert(astra_storage_can_release(&catalog, ram) == 0);
    assert(astra_storage_release_result(&catalog, ram, 2u, 0) == 0);
    assert(astra_storage_can_forget(&catalog, ram) == 0);
    assert(astra_storage_forget(&catalog, ram) == 0);
    astra_storage_catalog_destroy(&catalog);
}

int main(void)
{
    lifecycle();
    loss_and_stale_completion();
    policy_and_metadata();
    snapshots_and_budget();
    image_dependencies();
    names_and_bindings();
    refused_authority_restore();
    optical_eject();
    reusable_mount_names();
    configured_ram_source();
    retained_release();
    puts("storage catalog: lifecycle, loss, stale completion, metadata, snapshots, budget PASS");
    return 0;
}
