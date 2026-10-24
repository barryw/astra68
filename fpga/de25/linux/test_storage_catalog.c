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
    assert(astra_storage_device(catalog, class_id, 0u, 0u, &device) == 0);
    assert(astra_storage_medium(catalog, device, 1u, &medium) == 0);
    assert(astra_storage_source(catalog, medium, 1u, 0u, 0u, 0u, 0u, &result) == 0);
    assert(astra_storage_probe_result(catalog, result, 1u, format,
                                      uuid, label, "", 0, 0) == 0);
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
    assert(astra_storage_device(&catalog, ASTRA_VOLUME_USB, 0u, 0u, &device) == 0);
    charge = catalog.bytes;
    assert(astra_storage_medium(&catalog, device, 1u, &medium) == 0);
    assert(astra_storage_medium(&catalog, device, 1u, &replacement) == EBUSY);
    assert(astra_storage_source(&catalog, medium, 1u, 0u, 0u, 0u, 0u, &data) == 0);
    assert(astra_storage_probe_result(&catalog, data, 1u, ASTRA_VOLUME_EXT4,
                                      "uuid", "Original", "", 0, 0) == 0);
    assert(astra_storage_mount_begin(&catalog, data, 1u, 0u, 0u, 0u,
                                     &mounted) == 0);
    assert(astra_storage_loss(&catalog, medium, 1u) == 0);
    assert(astra_storage_find(&catalog, data)->generation == 2u);
    assert(astra_storage_find(&catalog, mounted)->generation == 2u);
    assert(astra_storage_probe_result(&catalog, data, 1u, ASTRA_VOLUME_EXT4,
                                      "uuid", "Late", "", 0, 0) == ESTALE);
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
    assert(astra_storage_source(&catalog, medium, 1u, 0u, 0u, 0u, 0u, &data) == ESTALE);
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
    assert((astra_storage_find(&catalog, optical)->flags & ASTRA_VOLUME_READ_ONLY) != 0u);
    assert(astra_storage_mount_begin(&catalog, optical, 1u, 0u, 0u, 0u,
                                     &mounted) == EROFS);
    assert(astra_storage_mount_begin(&catalog, optical, 1u,
        ASTRA_VOLUME_READ_ONLY, 0u, 0u, &mounted) == 0);
    assert(astra_storage_mount_result(&catalog, mounted, 1u, 23u, 0) == 0);
    assert((astra_storage_find(&catalog, mounted)->flags &
            ASTRA_VOLUME_READ_ONLY) != 0u);
    for (uint32_t format = ASTRA_VOLUME_FAT12; format <= ASTRA_VOLUME_FAT16; ++format) {
        uint64_t fat = source(&catalog, ASTRA_VOLUME_USB, format, "", "Legacy FAT");
        assert(astra_storage_mount_begin(&catalog, fat, 1u, 0u, 0u, 0u,
                                         &mounted) == EROFS);
        assert(astra_storage_mount_begin(&catalog, fat, 1u,
            ASTRA_VOLUME_READ_ONLY, 0u, 0u, &mounted) == 0);
    }
    uint64_t usb_disc = source(&catalog, ASTRA_VOLUME_USB, ASTRA_VOLUME_ISO9660,
                               "", "Installer");
    assert((astra_storage_find(&catalog, usb_disc)->flags & ASTRA_VOLUME_READ_ONLY) != 0u);
    assert(astra_storage_mount_begin(&catalog, usb_disc, 1u, 0u, 0u, 0u,
                                     &mounted) == EROFS);
    assert(astra_storage_mount_begin(&catalog, usb_disc, 1u,
        ASTRA_VOLUME_READ_ONLY, 0u, 0u, &mounted) == 0);
    uint64_t ntfs = source(&catalog, ASTRA_VOLUME_USB, ASTRA_VOLUME_NTFS,
                           "", "Windows Data");
    assert(astra_storage_mount_begin(&catalog, ntfs, 1u, 0u, 0u, 0u,
                                     &mounted) == EROFS);
    assert(astra_storage_mount_begin(&catalog, ntfs, 1u,
        ASTRA_VOLUME_READ_ONLY, 0u, 0u, &mounted) == 0);
    uint64_t unsupported_device, unsupported_medium, unsupported_source;
    assert(astra_storage_device(&catalog, ASTRA_VOLUME_USB,
        ASTRA_VOLUME_PARTITION_MAP_GPT, UINT64_C(16) << 30, &unsupported_device) == 0);
    assert(astra_storage_medium(&catalog, unsupported_device, 1u, &unsupported_medium) == 0);
    assert(astra_storage_source(&catalog, unsupported_medium, 1u,
        ASTRA_VOLUME_PARTITION_MAP_GPT, 1u, UINT64_MAX, 4096u,
        &unsupported_source) == EINVAL);
    uint64_t before_geometry = catalog.sequence;
    assert(astra_storage_source(&catalog, unsupported_medium, 1u,
        ASTRA_VOLUME_PARTITION_MAP_GPT, 1u, (UINT64_C(16) << 30) - 4096u,
        8192u, &unsupported_source) == EINVAL);
    assert(catalog.sequence == before_geometry);
    assert(astra_storage_source(&catalog, unsupported_medium, 1u,
        ASTRA_VOLUME_PARTITION_MAP_GPT, 1u, (UINT64_C(16) << 30) - 4096u,
        4096u, &unsupported_source) == 0);
    assert(astra_storage_source(&catalog, unsupported_medium, 1u,
        ASTRA_VOLUME_PARTITION_MAP_GPT, 1u, 1048576u, 4096u,
        &unsupported_source) == 0);
    assert(astra_storage_device_geometry(&catalog, unsupported_device, 1u,
        ASTRA_VOLUME_PARTITION_MAP_GPT, UINT64_C(16) << 30) == 0);
    assert(astra_storage_probe_result(&catalog, unsupported_source, 1u,
        ASTRA_VOLUME_UNSUPPORTED, "", "Archive", "btrfs", 0, 0) == 0);
    assert(strcmp(astra_storage_find(&catalog, unsupported_source)->detected_type, "btrfs") == 0);
    assert(astra_storage_mount_begin(&catalog, unsupported_source, 1u,
        ASTRA_VOLUME_READ_ONLY, 0u, 0u, &mounted) == ENOTSUP);
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
    uint64_t fixed_ntfs = source(&catalog, ASTRA_VOLUME_FIXED, ASTRA_VOLUME_NTFS,
                                 "", "Other disk");
    assert(astra_storage_mount_begin(&catalog, fixed_ntfs, 1u,
        ASTRA_VOLUME_SYSTEM | ASTRA_VOLUME_READ_ONLY, 0u, 0u, &mounted) == EINVAL);
    assert(astra_storage_mount_begin(&catalog, fixed_ntfs, 1u,
        ASTRA_VOLUME_READ_ONLY, 0u, 0u, &mounted) == 0);
    assert(astra_storage_device(&catalog, ASTRA_VOLUME_RAM, 0u, 0u, &ram) == EINVAL);
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
    assert(astra_storage_device(&catalog, ASTRA_VOLUME_FIXED, 0u, 0u, &id) == 0);
    assert(astra_storage_snapshot(&catalog, sequence, 2u, records, 2u,
                                   &count, &next, &current) == EAGAIN);
    assert(count == 0u && next == 0u);
    catalog.byte_limit = catalog.bytes;
    assert(astra_storage_device(&catalog, ASTRA_VOLUME_USB, 0u, 0u, &next) == ENOSPC);
    assert(astra_storage_loss(&catalog, id, 1u) == 0);
    assert(astra_storage_forget(&catalog, id) == 0);
    assert(astra_storage_device(&catalog, ASTRA_VOLUME_USB, 0u, 0u, &next) == 0);
    assert(next > id);
    catalog.next_id = UINT64_MAX;
    assert(astra_storage_device(&catalog, ASTRA_VOLUME_USB, 0u, 0u, &next) == ENOSPC);
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
                                      ASTRA_VOLUME_ISO9660, "", "Install", "iso9660", 0, 0) == 0);
    assert(astra_storage_mount_begin(&catalog, image, 1u, 0u, 0u, 0u,
                                     &image_mount) == EROFS);
    assert(astra_storage_mount_begin(&catalog, image, 1u, ASTRA_VOLUME_READ_ONLY,
                                     0u, 0u, &image_mount) == 0);
    assert(astra_storage_mount_result(&catalog, image_mount, 1u, 501u, 0) == 0);
    assert(astra_storage_image_source(&catalog, image_mount, 1u, 813u, &nested) == 0);
    assert(astra_storage_probe_result(&catalog, nested, 1u,
                                      ASTRA_VOLUME_UDF, "", "Nested", "udf", 0, 0) == 0);
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
    uint64_t optical, mount, sibling, sibling_mount;
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
    assert(astra_storage_source(&catalog, medium, 1u,
        ASTRA_VOLUME_PARTITION_MAP_GPT, 2u, 1048576u, 1048576u, &sibling) == 0);
    assert(astra_storage_probe_result(&catalog, sibling, 1u,
        ASTRA_VOLUME_ISO9660, "sibling", "Other Disc", "", 0, 0) == 0);
    assert(astra_storage_mount_begin(&catalog, sibling, 1u,
        ASTRA_VOLUME_READ_ONLY, 0u, 0u, &sibling_mount) == 0);
    assert(astra_storage_mount_result(&catalog, sibling_mount, 1u, 73u, 0) == 0);
    assert(astra_storage_eject_begin(&catalog, optical, 1u) == EBUSY);
    assert(astra_storage_find(&catalog, optical)->state == ASTRA_VOLUME_UNMOUNTED);
    assert(astra_storage_find(&catalog, sibling_mount)->state == ASTRA_VOLUME_MOUNTED);
    assert(astra_storage_quiesce(&catalog, sibling_mount, 1u) == 0);
    assert(astra_storage_unmount_result(&catalog, sibling_mount, 1u, 0, 0, 0) == 0);
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

static void multiple_partitions_one_device(void)
{
    AstraStorageCatalog catalog;
    uint64_t device, medium, data, installer, overlap, unknown, first, second, blocked;
    assert(astra_storage_catalog_init(&catalog, 1024u * 1024u) == 0);
    assert(astra_storage_device(&catalog, ASTRA_VOLUME_USB,
        ASTRA_VOLUME_PARTITION_MAP_GPT, UINT64_C(64) << 30, &device) == 0);
    assert(astra_storage_medium(&catalog, device, 1u, &medium) == 0);
    assert(astra_storage_source(&catalog, medium, 1u,
        ASTRA_VOLUME_PARTITION_MAP_GPT, 1u, UINT64_C(1) << 20,
        UINT64_C(1) << 30, &data) == 0);
    assert(astra_storage_source(&catalog, medium, 1u,
        ASTRA_VOLUME_PARTITION_MAP_GPT, 2u, UINT64_C(2) << 30,
        UINT64_C(1) << 30, &installer) == 0);
    assert(astra_storage_source(&catalog, medium, 1u,
        ASTRA_VOLUME_PARTITION_MAP_GPT, 3u, UINT64_C(1) << 30,
        UINT64_C(1) << 30, &overlap) == 0);
    assert(astra_storage_source(&catalog, medium, 1u,
        ASTRA_VOLUME_PARTITION_MAP_GPT, 0u, 0u, 0u, &unknown) == 0);
    assert(astra_storage_probe_result(&catalog, data, 1u, ASTRA_VOLUME_EXT4,
                                      "data", "Data", "ext4", 0, 0) == 0);
    assert(astra_storage_probe_result(&catalog, installer, 1u,
                                      ASTRA_VOLUME_ISO9660, "", "Installer", "iso9660", 0, 0) == 0);
    assert(astra_storage_probe_result(&catalog, overlap, 1u,
                                      ASTRA_VOLUME_EXT4, "", "Overlap", "ext4", 0, 0) == 0);
    assert(astra_storage_probe_result(&catalog, unknown, 1u,
                                      ASTRA_VOLUME_EXT4, "", "Unknown", "ext4", 0, 0) == 0);
    assert(astra_storage_mount_begin(&catalog, data, 1u, 0u, 0u, 0u, &first) == 0);
    blocked = 99u;
    assert(astra_storage_mount_begin(&catalog, overlap, 1u, 0u, 0u, 0u, &blocked) == EBUSY);
    assert(blocked == 0u);
    assert(astra_storage_mount_begin(&catalog, unknown, 1u, 0u, 0u, 0u, &blocked) == EBUSY);
    assert(astra_storage_mount_result(&catalog, first, 1u, 20u, 0) == 0);
    assert(astra_storage_mount_begin(&catalog, installer, 1u,
                                     ASTRA_VOLUME_READ_ONLY, 0u, 0u, &second) == 0);
    assert(astra_storage_mount_result(&catalog, second, 1u, 21u, 0) == 0);
    assert(strcmp(astra_storage_find(&catalog, first)->name, "/usb0") == 0);
    assert(strcmp(astra_storage_find(&catalog, second)->name, "/usb1") == 0);
    assert(astra_storage_find(&catalog, first)->parent == data);
    assert(astra_storage_find(&catalog, second)->parent == installer);
    assert(astra_storage_quiesce(&catalog, first, 1u) == 0);
    assert(astra_storage_unmount_result(&catalog, first, 1u, 0, 0, 0) == 0);
    assert(astra_storage_mount_begin(&catalog, overlap, 1u, 0u, 0u, 0u, &blocked) == 0);
    assert(blocked != 0u);
    astra_storage_catalog_destroy(&catalog);
}

static void physical_write_protection(void)
{
    AstraStorageCatalog catalog;
    uint64_t device, medium, data, mount = 99u;
    assert(astra_storage_catalog_init(&catalog, 1024u * 1024u) == 0);
    assert(astra_storage_device(&catalog, ASTRA_VOLUME_USB, 0u, 0u, &device) == 0);
    assert(astra_storage_physical_read_only(&catalog, device, 1u, 1) == 0);
    assert((astra_storage_find(&catalog, device)->flags & ASTRA_VOLUME_READ_ONLY) != 0u);
    assert(astra_storage_medium(&catalog, device, 1u, &medium) == 0);
    assert(astra_storage_source(&catalog, medium, 1u, 0u, 0u, 0u, 0u, &data) == 0);
    assert(astra_storage_physical_read_only(&catalog, data, 1u, 1) == 0);
    assert(astra_storage_probe_result(&catalog, data, 1u, ASTRA_VOLUME_EXT4,
        "", "Protected", "ext4", 0, 0) == 0);
    assert(astra_storage_mount_begin(&catalog, data, 1u, 0u, 0u, 0u, &mount) == EROFS);
    assert(mount == 0u);
    assert(astra_storage_mount_begin(&catalog, data, 1u,
        ASTRA_VOLUME_READ_ONLY, 0u, 0u, &mount) == 0);
    assert(astra_storage_physical_read_only(&catalog, medium, 1u, 1) == EINVAL);
    assert(astra_storage_physical_read_only(&catalog, data, 2u, 1) == ESTALE);
    assert(astra_storage_physical_read_only(&catalog, device, 1u, 2) == EINVAL);
    catalog.sequence = UINT64_MAX;
    assert(astra_storage_physical_read_only(&catalog, data, 1u, 0) == EOVERFLOW);
    assert((astra_storage_find(&catalog, data)->flags & ASTRA_VOLUME_READ_ONLY) != 0u);
    astra_storage_catalog_destroy(&catalog);
}

static void retained_mount_blocks_new_medium(void)
{
    AstraStorageCatalog catalog;
    uint64_t device, old_medium, old_source, old_mount, new_medium, new_source, admitted;
    assert(astra_storage_catalog_init(&catalog, 1024u * 1024u) == 0);
    assert(astra_storage_device(&catalog, ASTRA_VOLUME_USB,
        ASTRA_VOLUME_PARTITION_MAP_GPT, UINT64_C(8) << 30, &device) == 0);
    assert(astra_storage_medium(&catalog, device, 1u, &old_medium) == 0);
    assert(astra_storage_source(&catalog, old_medium, 1u,
        ASTRA_VOLUME_PARTITION_MAP_GPT, 1u, UINT64_C(1) << 20,
        UINT64_C(1) << 30, &old_source) == 0);
    assert(astra_storage_probe_result(&catalog, old_source, 1u,
        ASTRA_VOLUME_EXT4, "", "Old", "ext4", 0, 0) == 0);
    assert(astra_storage_mount_begin(&catalog, old_source, 1u, 0u, 0u, 0u,
        &old_mount) == 0);
    assert(astra_storage_mount_result(&catalog, old_mount, 1u, 77u, 0) == 0);
    assert(astra_storage_loss(&catalog, old_medium, 1u) == 0);
    assert(astra_storage_medium(&catalog, device, 1u, &new_medium) == 0);
    assert(astra_storage_source(&catalog, new_medium, 1u,
        ASTRA_VOLUME_PARTITION_MAP_GPT, 1u, UINT64_C(1) << 20,
        UINT64_C(1) << 30, &new_source) == 0);
    assert(astra_storage_probe_result(&catalog, new_source, 1u,
        ASTRA_VOLUME_EXT4, "", "New", "ext4", 0, 0) == 0);
    admitted = 99u;
    assert(astra_storage_mount_begin(&catalog, new_source, 1u, 0u, 0u, 0u,
        &admitted) == EBUSY);
    assert(admitted == 0u);
    assert(astra_storage_unmount_result(&catalog, old_mount, 2u, 0, 0, 0) == 0);
    assert(astra_storage_mount_begin(&catalog, new_source, 1u, 0u, 0u, 0u,
        &admitted) == 0);
    astra_storage_catalog_destroy(&catalog);
}

static void format_lifecycle(void)
{
    AstraStorageCatalog catalog;
    uint64_t disk, medium, legacy, other, mounted, fixed, candidate;
    assert(astra_storage_catalog_init(&catalog, 1024u * 1024u) == 0);
    assert(astra_storage_device(&catalog, ASTRA_VOLUME_USB, ASTRA_VOLUME_PARTITION_MAP_MBR,
        1024u * 1024u * 1024u, &disk) == 0);
    assert(astra_storage_medium(&catalog, disk, 1u, &medium) == 0);
    assert(astra_storage_source(&catalog, medium, 1u, ASTRA_VOLUME_PARTITION_MAP_MBR,
        1u, 1024u * 1024u, 128u * 1024u * 1024u, &legacy) == 0);
    assert(astra_storage_source(&catalog, medium, 1u, ASTRA_VOLUME_PARTITION_MAP_MBR,
        2u, 129u * 1024u * 1024u, 128u * 1024u * 1024u, &other) == 0);
    assert(astra_storage_probe_result(&catalog, legacy, 1u, ASTRA_VOLUME_FAT16,
        "old-id", "Old", "", 0, 0) == 0);
    assert(astra_storage_probe_result(&catalog, other, 1u, ASTRA_VOLUME_EXFAT,
        "other-id", "Other", "", 0, 0) == 0);
    assert((astra_storage_find(&catalog, legacy)->flags & ASTRA_VOLUME_READ_ONLY) != 0u);
    assert((astra_storage_find(&catalog, legacy)->flags & ASTRA_VOLUME_PHYSICAL_READ_ONLY) == 0u);
    assert(astra_storage_mount_begin(&catalog, other, 1u, 0u, 0u, 0u, &mounted) == 0);
    assert(astra_storage_mount_result(&catalog, mounted, 1u, 501u, 0) == 0);
    assert(astra_storage_no_mounted_children(&catalog, disk) == EBUSY);
    assert(astra_storage_partition_can_edit(&catalog, disk, 1u) == EBUSY);
    assert(astra_storage_format_begin(&catalog, legacy, 1u) == EBUSY);
    assert(astra_storage_quiesce(&catalog, mounted, 1u) == 0);
    assert(astra_storage_unmount_result(&catalog, mounted, 1u, 0, 0, 0) == 0);
    assert(astra_storage_forget(&catalog, mounted) == 0);
    assert(astra_storage_partition_can_edit(&catalog, disk, 2u) == ESTALE);
    assert(astra_storage_partition_can_edit(&catalog, disk, 1u) == 0);
    assert(astra_storage_format_begin(&catalog, legacy, 1u) == 0);
    assert(astra_storage_partition_can_edit(&catalog, disk, 1u) == EBUSY);
    assert(astra_storage_find(&catalog, legacy)->state == ASTRA_VOLUME_FORMATTING);
    assert(astra_storage_format_begin(&catalog, other, 1u) == EBUSY);
    assert(astra_storage_mount_begin(&catalog, other, 1u, 0u, 0u, 0u, &candidate) == EBUSY);
    assert(astra_storage_format_abort(&catalog, legacy, 2u) == ESTALE);
    assert(astra_storage_format_abort(&catalog, legacy, 1u) == 0);
    assert(astra_storage_partition_can_edit(&catalog, disk, 1u) == 0);
    assert(astra_storage_find(&catalog, legacy)->state == ASTRA_VOLUME_UNMOUNTED);
    assert(strcmp(astra_storage_find(&catalog, legacy)->label, "Old") == 0);
    assert(astra_storage_format_begin(&catalog, legacy, 1u) == 0);
    assert(astra_storage_mount_begin(&catalog, legacy, 1u, ASTRA_VOLUME_READ_ONLY,
        0u, 0u, &candidate) == EBUSY);
    assert(astra_storage_format_complete(&catalog, legacy, 2u) == ESTALE);
    assert(astra_storage_format_complete(&catalog, legacy, 1u) == 0);
    const AstraStorageRecord *record = astra_storage_find(&catalog, legacy);
    assert(record->generation == 2u && record->state == ASTRA_VOLUME_PROBING);
    assert(record->format == ASTRA_VOLUME_FORMAT_UNKNOWN && record->uuid[0] == '\0' &&
        record->label[0] == '\0' && (record->flags & ASTRA_VOLUME_READ_ONLY) == 0u);
    assert(astra_storage_probe_result(&catalog, legacy, 1u, ASTRA_VOLUME_FAT32,
        "new-id", "New", "", 0, 0) == ESTALE);
    assert(astra_storage_probe_result(&catalog, legacy, 2u, ASTRA_VOLUME_FAT32,
        "new-id", "New", "", 0, 0) == 0);
    assert(astra_storage_physical_read_only(&catalog, legacy, 2u, 1) == 0);
    assert((astra_storage_find(&catalog, legacy)->flags &
        (ASTRA_VOLUME_READ_ONLY | ASTRA_VOLUME_PHYSICAL_READ_ONLY)) ==
        (ASTRA_VOLUME_READ_ONLY | ASTRA_VOLUME_PHYSICAL_READ_ONLY));
    assert(astra_storage_format_begin(&catalog, legacy, 2u) == EROFS);
    assert(astra_storage_physical_read_only(&catalog, legacy, 2u, 0) == 0);
    assert((astra_storage_find(&catalog, legacy)->flags & ASTRA_VOLUME_READ_ONLY) == 0u);
    assert(astra_storage_physical_read_only(&catalog, disk, 1u, 1) == 0);
    assert(astra_storage_partition_can_edit(&catalog, disk, 1u) == EROFS);
    assert(astra_storage_format_begin(&catalog, legacy, 2u) == EROFS);
    assert(astra_storage_physical_read_only(&catalog, disk, 1u, 0) == 0);
    assert(astra_storage_partition_can_edit(&catalog, disk, 1u) == 0);
    assert(astra_storage_format_begin(&catalog, legacy, 2u) == 0);
    assert(astra_storage_format_complete(&catalog, legacy, 2u) == 0);
    fixed = source(&catalog, ASTRA_VOLUME_FIXED, ASTRA_VOLUME_FAT32, "fixed-id", "Fixed");
    assert(astra_storage_mount_begin(&catalog, fixed, 1u, 0u, 0u, 0u, &mounted) == 0);
    uint64_t damaged;
    assert(astra_storage_source(&catalog, medium, 1u, ASTRA_VOLUME_PARTITION_MAP_MBR,
        3u, 257u * 1024u * 1024u, 128u * 1024u * 1024u, &damaged) == 0);
    assert(astra_storage_probe_result(&catalog, damaged, 1u, ASTRA_VOLUME_EXFAT,
        "bad-id", "Bad", "exfat", 0, EUCLEAN) == 0);
    assert(astra_storage_find(&catalog, damaged)->state == ASTRA_VOLUME_FAILED);
    assert(astra_storage_format_begin(&catalog, damaged, 1u) == 0);
    assert(astra_storage_format_complete(&catalog, damaged, 1u) == 0);
    astra_storage_catalog_destroy(&catalog);
}

static void partition_lifecycle(void)
{
    AstraStorageCatalog catalog;
    uint64_t disk, medium, source, rejected = 99u;
    assert(astra_storage_catalog_init(&catalog, 1024u * 1024u) == 0);
    assert(astra_storage_device(&catalog, ASTRA_VOLUME_USB,
        ASTRA_VOLUME_PARTITION_MAP_MBR, UINT64_C(128) << 20, &disk) == 0);
    assert(astra_storage_medium(&catalog, disk, 1u, &medium) == 0);
    assert(astra_storage_source(&catalog, medium, 1u,
        ASTRA_VOLUME_PARTITION_MAP_MBR, 1u, UINT64_C(1) << 20,
        UINT64_C(32) << 20, &source) == 0);
    assert(astra_storage_probe_result(&catalog, source, 1u,
        ASTRA_VOLUME_FAT32, "", "Old", "vfat", 0, 0) == 0);
    assert(astra_storage_partition_begin(&catalog, disk, 2u) == ESTALE);
    assert(astra_storage_partition_begin(&catalog, disk, 1u) == 0);
    assert(astra_storage_find(&catalog, disk)->state == ASTRA_VOLUME_PARTITIONING);
    assert(astra_storage_partition_can_edit(&catalog, disk, 1u) == EBUSY);
    assert(astra_storage_medium(&catalog, disk, 1u, &rejected) == EBUSY);
    assert(rejected == 0u);
    assert(astra_storage_mount_begin(&catalog, source, 1u, 0u, 0u, 0u,
        &rejected) == EBUSY);
    assert(astra_storage_format_begin(&catalog, source, 1u) == EBUSY);
    assert(astra_storage_partition_abort(&catalog, disk, 2u) == ESTALE);
    assert(astra_storage_partition_abort(&catalog, disk, 1u) == 0);
    assert(astra_storage_find(&catalog, disk)->state == ASTRA_VOLUME_DISCOVERED);
    assert(astra_storage_find(&catalog, source)->state == ASTRA_VOLUME_UNMOUNTED);
    assert(astra_storage_partition_begin(&catalog, disk, 1u) == 0);
    assert(astra_storage_partition_complete(&catalog, disk, 2u, EIO) == ESTALE);
    assert(astra_storage_partition_complete(&catalog, disk, 1u, EIO) == 0);
    const AstraStorageRecord *record = astra_storage_find(&catalog, disk);
    assert(record->generation == 2u && record->state == ASTRA_VOLUME_DISCOVERED &&
        record->partition_map == ASTRA_VOLUME_PARTITION_MAP_OTHER && record->error == EIO);
    assert(astra_storage_find(&catalog, medium)->state == ASTRA_VOLUME_LOST);
    assert(astra_storage_find(&catalog, source)->state == ASTRA_VOLUME_LOST);
    assert(astra_storage_mount_begin(&catalog, source, 1u, 0u, 0u, 0u,
        &rejected) == ESTALE);
    assert(astra_storage_medium(&catalog, disk, 2u, &rejected) == 0);
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
    multiple_partitions_one_device();
    retained_mount_blocks_new_medium();
    physical_write_protection();
    format_lifecycle();
    partition_lifecycle();
    puts("storage catalog: lifecycle, loss, stale completion, metadata, snapshots, budget PASS");
    return 0;
}
