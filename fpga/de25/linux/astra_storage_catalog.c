#include "astra_storage_catalog.h"

#include <astra/utf8.h>

#include <errno.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct AstraStorageEntry {
    AstraStorageRecord record;
    uint64_t mounting_generation; /* retained accepted Linux mount attempt */
    struct AstraStorageEntry *next;
};

static AstraStorageEntry *find_entry(AstraStorageCatalog *catalog, uint64_t id)
{
    for (AstraStorageEntry *entry = catalog->entries; entry != NULL;
         entry = entry->next)
        if (entry->record.id == id)
            return entry;
    return NULL;
}

static AstraStorageRecord *find(AstraStorageCatalog *catalog, uint64_t id)
{
    AstraStorageEntry *entry = find_entry(catalog, id);
    return entry == NULL ? NULL : &entry->record;
}

const AstraStorageRecord *astra_storage_find(const AstraStorageCatalog *catalog,
                                            uint64_t id)
{
    if (catalog == NULL)
        return NULL;
    for (const AstraStorageEntry *entry = catalog->entries; entry != NULL;
         entry = entry->next)
        if (entry->record.id == id)
            return &entry->record;
    return NULL;
}

static bool live(const AstraStorageRecord *record)
{
    return record != NULL && record->state != ASTRA_VOLUME_LOST;
}

static int changed(AstraStorageCatalog *catalog)
{
    if (catalog->sequence == UINT64_MAX)
        return EOVERFLOW;
    ++catalog->sequence;
    return 0;
}

int astra_storage_catalog_init(AstraStorageCatalog *catalog, size_t byte_limit)
{
    if (catalog == NULL || byte_limit < sizeof(AstraStorageEntry))
        return EINVAL;
    *catalog = (AstraStorageCatalog){.byte_limit = byte_limit, .sequence = 1u};
    return 0;
}

void astra_storage_catalog_destroy(AstraStorageCatalog *catalog)
{
    if (catalog == NULL)
        return;
    while (catalog->entries != NULL) {
        AstraStorageEntry *entry = catalog->entries;
        catalog->entries = entry->next;
        free(entry);
    }
    *catalog = (AstraStorageCatalog){0};
}

static int add(AstraStorageCatalog *catalog, uint32_t kind,
                const AstraStorageRecord *parent, uint32_t class_id,
                uint32_t state, uint64_t *id)
{
    AstraStorageEntry *entry;
    AstraStorageEntry **tail;

    if (catalog == NULL || id == NULL || catalog->sequence == UINT64_MAX)
        return EINVAL;
    *id = 0u;
    if (catalog->next_id == UINT64_MAX ||
        catalog->bytes > catalog->byte_limit ||
        sizeof(*entry) > catalog->byte_limit - catalog->bytes)
        return ENOSPC;
    entry = calloc(1u, sizeof(*entry));
    if (entry == NULL)
        return ENOMEM;
    entry->record = (AstraStorageRecord){
        .id = ++catalog->next_id, .generation = 1u,
        .parent = parent == NULL ? 0u : parent->id,
        .parent_generation = parent == NULL ? 0u : parent->generation,
        .kind = kind, .class_id = class_id, .state = state,
        .origin = parent == NULL ? ASTRA_VOLUME_ORIGIN_DEVICE : parent->origin
    };
    /* Append order is ID order, so pagination survives record retirement. */
    for (tail = &catalog->entries; *tail != NULL; tail = &(*tail)->next) {}
    *tail = entry;
    catalog->bytes += sizeof(*entry);
    *id = entry->record.id;
    return changed(catalog);
}

int astra_storage_binding(AstraStorageCatalog *catalog, uint64_t mount,
                          uint64_t generation, uint64_t linux_mount_id,
                          const char *name, uint32_t flags, uint64_t *id)
{
    const AstraStorageRecord *parent = astra_storage_find(catalog, mount);
    size_t length;
    int error;
    if (catalog == NULL || name == NULL || id == NULL ||
        flags & ~ASTRA_VOLUME_READ_ONLY ||
        (mount != 0u && (parent == NULL || parent->kind != ASTRA_VOLUME_MOUNT ||
            parent->generation != generation || parent->state != ASTRA_VOLUME_MOUNTED)))
        return EINVAL;
    length = strnlen(name, sizeof(((AstraStorageRecord *)0)->name));
    if (length < 2u || length == sizeof(((AstraStorageRecord *)0)->name) || name[0] != '/')
        return EINVAL;
    for (size_t at = 1u; at < length; ++at)
        if ((name[at] < 'a' || name[at] > 'z') &&
            (name[at] < '0' || name[at] > '9') && name[at] != '-')
            return EINVAL;
    const char *digits = NULL;
    if (strncmp(name, "/dh", 3u) == 0 || strncmp(name, "/cd", 3u) == 0)
        digits = name + 3u;
    else if (strncmp(name, "/usb", 4u) == 0 || strncmp(name, "/ram", 4u) == 0)
        digits = name + 4u;
    if (strcmp(name, "/tmp") == 0 || (digits != NULL && digits[0] >= '0' && digits[0] <= '9'))
        return EINVAL;
    for (AstraStorageEntry *entry = catalog->entries; entry != NULL; entry = entry->next)
        if (live(&entry->record) && strcmp(entry->record.name, name) == 0)
            return EEXIST;
    error = add(catalog, ASTRA_VOLUME_BINDING, parent, 0u,
                 linux_mount_id == 0u ? ASTRA_VOLUME_MOUNTING : ASTRA_VOLUME_MOUNTED, id);
    if (error == 0) {
        AstraStorageRecord *record = find(catalog, *id);
        record->linux_mount_id = linux_mount_id;
        record->origin = ASTRA_VOLUME_ORIGIN_EXPORT;
        record->flags = flags;
        memcpy(record->name, name, length + 1u);
        if (linux_mount_id == 0u)
            find_entry(catalog, *id)->mounting_generation = 1u;
    }
    return error;
}

int astra_storage_device(AstraStorageCatalog *catalog, uint32_t class_id,
                         uint32_t partition_map, uint64_t capacity_bytes,
                         uint64_t *id)
{
    if (class_id < ASTRA_VOLUME_FIXED || class_id > ASTRA_VOLUME_OPTICAL ||
        partition_map > ASTRA_VOLUME_PARTITION_MAP_OTHER)
        return EINVAL;
    int error = add(catalog, ASTRA_VOLUME_DEVICE, NULL, class_id,
                    ASTRA_VOLUME_DISCOVERED, id);
    if (error == 0) {
        AstraStorageRecord *record = find(catalog, *id);
        record->partition_map = partition_map;
        record->capacity_bytes = capacity_bytes;
    }
    return error;
}

static int child(AstraStorageCatalog *catalog, uint64_t parent_id,
                  uint64_t generation, uint32_t parent_kind,
                  uint32_t kind, uint32_t state, uint64_t *id)
{
    const AstraStorageRecord *parent;

    if (catalog == NULL || id == NULL)
        return EINVAL;
    *id = 0u;
    parent = find(catalog, parent_id);
    if (!live(parent) || parent->generation != generation)
        return ESTALE;
    if (parent->kind != parent_kind)
        return EINVAL;
    if (kind == ASTRA_VOLUME_MEDIUM &&
        parent->state == ASTRA_VOLUME_PARTITIONING)
        return EBUSY;
    if (kind == ASTRA_VOLUME_MEDIUM)
        for (AstraStorageEntry *entry = catalog->entries; entry != NULL;
             entry = entry->next)
            if (entry->record.kind == kind &&
                entry->record.parent == parent_id && live(&entry->record))
                return EBUSY;
    return add(catalog, kind, parent, parent->class_id, state, id);
}

int astra_storage_medium(AstraStorageCatalog *catalog, uint64_t device,
                         uint64_t generation, uint64_t *id)
{
    return child(catalog, device, generation, ASTRA_VOLUME_DEVICE,
                 ASTRA_VOLUME_MEDIUM, ASTRA_VOLUME_DISCOVERED, id);
}

int astra_storage_source(AstraStorageCatalog *catalog, uint64_t medium,
                         uint64_t generation, uint32_t partition_map,
                         uint32_t partition_number, uint64_t offset_bytes,
                         uint64_t capacity_bytes, uint64_t *id)
{
    if (partition_map > ASTRA_VOLUME_PARTITION_MAP_OTHER ||
        (partition_number == 0u && offset_bytes != 0u) ||
        (partition_number != 0u && (capacity_bytes == 0u ||
            offset_bytes > UINT64_MAX - capacity_bytes)))
        return EINVAL;
    if (catalog != NULL) {
        const AstraStorageRecord *parent = find(catalog, medium);
        if (live(parent) && parent->generation == generation &&
            parent->kind == ASTRA_VOLUME_MEDIUM) {
            const AstraStorageRecord *device = find(catalog, parent->parent);
            if (!live(device) || device->generation != parent->parent_generation ||
                device->kind != ASTRA_VOLUME_DEVICE)
                return ESTALE;
            if (device->state == ASTRA_VOLUME_PARTITIONING)
                return EBUSY;
            if (partition_number != 0u && device->capacity_bytes != 0u &&
                (offset_bytes > device->capacity_bytes ||
                 capacity_bytes > device->capacity_bytes - offset_bytes))
                return EINVAL;
        }
    }
    int error = child(catalog, medium, generation, ASTRA_VOLUME_MEDIUM,
                      ASTRA_VOLUME_SOURCE, ASTRA_VOLUME_PROBING, id);
    if (error == 0) {
        AstraStorageRecord *record = find(catalog, *id);
        record->partition_map = partition_map;
        record->partition_number = partition_number;
        record->offset_bytes = offset_bytes;
        record->capacity_bytes = capacity_bytes;
    }
    return error;
}

int astra_storage_device_geometry(AstraStorageCatalog *catalog, uint64_t id,
                                  uint64_t generation, uint32_t partition_map,
                                  uint64_t capacity_bytes)
{
    if (catalog == NULL)
        return EINVAL;
    AstraStorageRecord *record = find(catalog, id);
    if (!live(record) || record->generation != generation)
        return ESTALE;
    if (catalog->sequence == UINT64_MAX ||
        partition_map > ASTRA_VOLUME_PARTITION_MAP_OTHER ||
        record->kind != ASTRA_VOLUME_DEVICE)
        return EINVAL;
    if (record->state == ASTRA_VOLUME_PARTITIONING)
        return EBUSY;
    if (record->partition_map == partition_map &&
        record->capacity_bytes == capacity_bytes)
        return 0;
    record->partition_map = partition_map;
    record->capacity_bytes = capacity_bytes;
    return changed(catalog);
}

int astra_storage_physical_read_only(AstraStorageCatalog *catalog, uint64_t id,
                                     uint64_t generation, int read_only)
{
    if (catalog == NULL || (read_only != 0 && read_only != 1))
        return EINVAL;
    AstraStorageRecord *record = find(catalog, id);
    if (!live(record) || record->generation != generation)
        return ESTALE;
    if ((record->kind != ASTRA_VOLUME_DEVICE && record->kind != ASTRA_VOLUME_SOURCE) ||
        (record->kind == ASTRA_VOLUME_SOURCE &&
            record->origin != ASTRA_VOLUME_ORIGIN_DEVICE))
        return EINVAL;
    uint32_t flags = read_only ?
        record->flags | ASTRA_VOLUME_READ_ONLY | ASTRA_VOLUME_PHYSICAL_READ_ONLY :
        record->flags & ~ASTRA_VOLUME_PHYSICAL_READ_ONLY;
    if (!read_only && record->class_id != ASTRA_VOLUME_OPTICAL &&
        !astra_storage_format_read_only(record->format))
        flags &= ~ASTRA_VOLUME_READ_ONLY;
    if (record->flags == flags)
        return 0;
    if (catalog->sequence == UINT64_MAX)
        return EOVERFLOW;
    record->flags = flags;
    return changed(catalog);
}

int astra_storage_ram_source(AstraStorageCatalog *catalog, uint32_t flags, uint64_t *id)
{
    if (catalog == NULL || id == NULL || (flags & ~ASTRA_VOLUME_CONFIGURED_RAM) != 0u)
        return EINVAL;
    if ((flags & ASTRA_VOLUME_CONFIGURED_RAM) != 0u)
        for (AstraStorageEntry *entry = catalog->entries; entry != NULL; entry = entry->next)
            if (entry->record.kind == ASTRA_VOLUME_SOURCE && live(&entry->record) &&
                (entry->record.flags & ASTRA_VOLUME_CONFIGURED_RAM) != 0u)
                return EEXIST;
    int error = add(catalog, ASTRA_VOLUME_SOURCE, NULL, ASTRA_VOLUME_RAM,
                     ASTRA_VOLUME_UNMOUNTED, id);
    if (error == 0)
        *find(catalog, *id) = (AstraStorageRecord){
            .id = *id, .generation = 1u, .kind = ASTRA_VOLUME_SOURCE,
            .class_id = ASTRA_VOLUME_RAM, .state = ASTRA_VOLUME_UNMOUNTED,
            .format = ASTRA_VOLUME_TMPFS, .origin = ASTRA_VOLUME_ORIGIN_RAM, .flags = flags
        };
    return error;
}

int astra_storage_system_source(AstraStorageCatalog *catalog, uint64_t *id)
{
    int error = add(catalog, ASTRA_VOLUME_SOURCE, NULL, ASTRA_VOLUME_FIXED,
                     ASTRA_VOLUME_PROBING, id);
    if (error == 0)
        find(catalog, *id)->origin = ASTRA_VOLUME_ORIGIN_SYSTEM_IMAGE;
    return error;
}

int astra_storage_image_source(AstraStorageCatalog *catalog, uint64_t mount,
                               uint64_t generation, uint64_t backing_object,
                               uint64_t *id)
{
    const AstraStorageRecord *parent;
    AstraStorageRecord *image;
    int error;

    if (catalog == NULL || id == NULL || backing_object == 0u)
        return EINVAL;
    *id = 0u;
    parent = find(catalog, mount);
    if (!live(parent) || parent->generation != generation)
        return ESTALE;
    if (parent->kind != ASTRA_VOLUME_MOUNT && parent->kind != ASTRA_VOLUME_BINDING)
        return EINVAL;
    if (parent->state != ASTRA_VOLUME_MOUNTED)
        return EBUSY;
    error = add(catalog, ASTRA_VOLUME_SOURCE, parent, ASTRA_VOLUME_OPTICAL,
                 ASTRA_VOLUME_PROBING, id);
    if (error != 0)
        return error;
    image = find(catalog, *id);
    image->origin = ASTRA_VOLUME_ORIGIN_IMAGE;
    image->backing_object = backing_object;
    return 0;
}

static void duplicate_uuids(AstraStorageCatalog *catalog)
{
    for (AstraStorageEntry *entry = catalog->entries; entry != NULL;
         entry = entry->next) {
        AstraStorageRecord *record = &entry->record;
        record->flags &= ~ASTRA_VOLUME_DUPLICATE_UUID;
        if (record->kind != ASTRA_VOLUME_SOURCE || !live(record) ||
            record->uuid[0] == '\0')
            continue;
        for (AstraStorageEntry *other = catalog->entries; other != NULL;
             other = other->next)
            if (other != entry && other->record.kind == ASTRA_VOLUME_SOURCE &&
                live(&other->record) &&
                strcmp(record->uuid, other->record.uuid) == 0) {
                record->flags |= ASTRA_VOLUME_DUPLICATE_UUID;
                break;
            }
    }
}

static bool display_label(const char *label, size_t length)
{
    uint32_t offset = 0u;

    if (!astra_utf8_validate(label, (uint32_t)length, 0u))
        return false;
    while (offset < length) {
        uint32_t width = 0u;
        uint32_t scalar = astra_utf8_decode(label + offset,
                                           (uint32_t)length - offset, &width);
        if (scalar < 0x20u || (scalar >= 0x7fu && scalar <= 0x9fu))
            return false;
        offset += width;
    }
    return true;
}

int astra_storage_probe_result(AstraStorageCatalog *catalog, uint64_t source,
                               uint64_t generation, uint32_t format,
                               const char *uuid, const char *label,
                               const char *detected_type,
                               int ambiguous, int error)
{
    AstraStorageRecord *record;
    size_t uuid_length;
    size_t label_length;
    size_t type_length;

    if (catalog == NULL || uuid == NULL || label == NULL || detected_type == NULL || error < 0 ||
        format > ASTRA_VOLUME_UNSUPPORTED || catalog->sequence == UINT64_MAX)
        return EINVAL;
    record = find(catalog, source);
    if (!live(record) || record->generation != generation)
        return ESTALE;
    if (record->kind != ASTRA_VOLUME_SOURCE ||
        record->state != ASTRA_VOLUME_PROBING)
        return EINVAL;
    uuid_length = strnlen(uuid, sizeof(record->uuid));
    label_length = strnlen(label, sizeof(record->label));
    type_length = strnlen(detected_type, sizeof(record->detected_type));
    if (uuid_length == sizeof(record->uuid) ||
        label_length == sizeof(record->label) ||
        type_length == sizeof(record->detected_type))
        return ENAMETOOLONG;
    if (!display_label(uuid, uuid_length) || !display_label(detected_type, type_length) ||
        (format == ASTRA_VOLUME_UNSUPPORTED && type_length == 0u))
        return EILSEQ;
    record->flags &= ~(ASTRA_VOLUME_AMBIGUOUS_FORMAT | ASTRA_VOLUME_LABEL_INVALID |
                       ASTRA_VOLUME_READ_ONLY);
    record->label[0] = '\0';
    memcpy(record->uuid, uuid, uuid_length + 1u);
    memcpy(record->detected_type, detected_type, type_length + 1u);
    if (display_label(label, label_length))
        memcpy(record->label, label, label_length + 1u);
    else
        record->flags |= ASTRA_VOLUME_LABEL_INVALID;
    if (ambiguous) record->flags |= ASTRA_VOLUME_AMBIGUOUS_FORMAT;
    if (record->class_id == ASTRA_VOLUME_OPTICAL ||
        astra_storage_format_read_only(format) ||
        (record->flags & ASTRA_VOLUME_PHYSICAL_READ_ONLY) != 0u)
        record->flags |= ASTRA_VOLUME_READ_ONLY;
    record->format = format;
    record->error = error;
    record->state = error == 0 ? ASTRA_VOLUME_UNMOUNTED : ASTRA_VOLUME_FAILED;
    duplicate_uuids(catalog);
    return changed(catalog);
}

static bool supported(uint32_t class_id, uint32_t format)
{
    switch (class_id) {
    case ASTRA_VOLUME_FIXED:
    case ASTRA_VOLUME_USB:
        return format == ASTRA_VOLUME_EXT4 || format == ASTRA_VOLUME_FAT32 ||
               format == ASTRA_VOLUME_EXFAT || format == ASTRA_VOLUME_FAT12 ||
               format == ASTRA_VOLUME_FAT16 || format == ASTRA_VOLUME_ISO9660 ||
               format == ASTRA_VOLUME_UDF || format == ASTRA_VOLUME_NTFS;
    case ASTRA_VOLUME_OPTICAL:
        return format == ASTRA_VOLUME_ISO9660 || format == ASTRA_VOLUME_UDF;
    case ASTRA_VOLUME_RAM: return format == ASTRA_VOLUME_TMPFS;
    default: return false;
    }
}

/* A lost mount still reserves its name while Linux or an admitted worker
 * retains it. Persisted USB reservations live in the provider name map. */
static int name_reserved(const AstraStorageCatalog *catalog, const char *name)
{
    for (const AstraStorageEntry *entry = catalog->entries; entry != NULL; entry = entry->next)
        if (strcmp(entry->record.name, name) == 0 &&
            (entry->record.linux_mount_id != 0u || entry->mounting_generation != 0u ||
             entry->record.state == ASTRA_VOLUME_MOUNTING ||
             entry->record.state == ASTRA_VOLUME_MOUNTED ||
             entry->record.state == ASTRA_VOLUME_QUIESCING ||
             (entry->record.flags & ASTRA_VOLUME_CLEANUP_PENDING) != 0u))
            return 1;
    return 0;
}

int astra_storage_usb_slot_reserved(const AstraStorageCatalog *catalog, uint32_t slot)
{
    if (catalog == NULL || slot == UINT32_MAX) return 1;
    char name[32];
    int length = snprintf(name, sizeof(name), "/usb%" PRIu32, slot);
    return length < 0 || (size_t)length >= sizeof(name) || name_reserved(catalog, name);
}

static int mount_name(const AstraStorageCatalog *catalog, uint32_t class_id,
    uint32_t flags, uint32_t usb_slot, char name[32])
{
    if (flags & ASTRA_VOLUME_SYSTEM) { strcpy(name, "/dh0"); return 0; }
    if (flags & ASTRA_VOLUME_TEMPORARY) { strcpy(name, "/tmp"); return 0; }
    static const char *const prefixes[] = {"dh", "usb", "cd", "ram"};
    if (class_id < ASTRA_VOLUME_FIXED || class_id > ASTRA_VOLUME_RAM) return EINVAL;
    if (class_id != ASTRA_VOLUME_USB && usb_slot != UINT32_MAX) return EINVAL;
    uint64_t number = usb_slot != UINT32_MAX ? usb_slot :
        class_id == ASTRA_VOLUME_FIXED ? 1u : 0u;
    for (;;) {
        int length = snprintf(name, 32u, "/%s%" PRIu64, prefixes[class_id - 1u], number);
        if (length < 0 || length >= 32) return EOVERFLOW;
        if (!name_reserved(catalog, name)) return 0;
        if (usb_slot != UINT32_MAX) return EBUSY;
        if (number == UINT64_MAX) return EOVERFLOW;
        ++number;
    }
}

int astra_storage_mount_begin(AstraStorageCatalog *catalog, uint64_t source,
                              uint64_t generation, uint32_t flags,
                              uint64_t byte_limit, uint64_t inode_limit,
                              uint32_t usb_slot, uint64_t *mount)
{
    const AstraStorageRecord *record;
    AstraStorageRecord *mounted;
    char name[32];
    int error;

    if (catalog == NULL || mount == NULL ||
        (flags & ~(ASTRA_VOLUME_READ_ONLY | ASTRA_VOLUME_SYSTEM |
                   ASTRA_VOLUME_TEMPORARY)) != 0u)
        return EINVAL;
    *mount = 0u;
    record = find(catalog, source);
    if (!live(record) || record->generation != generation)
        return ESTALE;
    if (record->kind != ASTRA_VOLUME_SOURCE)
        return EINVAL;
    if (record->origin == ASTRA_VOLUME_ORIGIN_DEVICE) {
        const AstraStorageRecord *medium = find(catalog, record->parent);
        const AstraStorageRecord *device = medium == NULL ? NULL : find(catalog, medium->parent);
        if (device != NULL && device->state == ASTRA_VOLUME_PARTITIONING)
            return EBUSY;
    }
    if (record->state != ASTRA_VOLUME_UNMOUNTED)
        return record->state == ASTRA_VOLUME_PROBING ? EAGAIN :
            record->state == ASTRA_VOLUME_FORMATTING ? EBUSY : EINVAL;
    if (!supported(record->class_id, record->format) ||
        (record->flags & ASTRA_VOLUME_AMBIGUOUS_FORMAT) != 0u)
        return ENOTSUP;
    if ((record->flags & ASTRA_VOLUME_READ_ONLY) != 0u &&
        (flags & ASTRA_VOLUME_READ_ONLY) == 0u)
        return EROFS;
    if ((flags & ASTRA_VOLUME_SYSTEM) != 0u &&
        (record->class_id != ASTRA_VOLUME_FIXED || record->format != ASTRA_VOLUME_EXT4))
        return EINVAL;
    if ((flags & ASTRA_VOLUME_TEMPORARY) != 0u && record->class_id != ASTRA_VOLUME_RAM)
        return EINVAL;
    if (record->class_id == ASTRA_VOLUME_RAM ?
        byte_limit == 0u || inode_limit == 0u :
        byte_limit != 0u || inode_limit != 0u)
        return EINVAL;
    error = astra_storage_no_mounted_children(catalog, source);
    if (error != 0) return error;
    if (record->origin == ASTRA_VOLUME_ORIGIN_DEVICE) {
        const AstraStorageRecord *medium = find(catalog, record->parent);
        if (medium == NULL || medium->kind != ASTRA_VOLUME_MEDIUM)
            return ESTALE;
        for (AstraStorageEntry *entry = catalog->entries; entry != NULL;
             entry = entry->next) {
            const AstraStorageRecord *sibling = &entry->record;
            if (sibling->id == source || sibling->kind != ASTRA_VOLUME_SOURCE ||
                sibling->origin != ASTRA_VOLUME_ORIGIN_DEVICE)
                continue;
            const AstraStorageRecord *sibling_medium = find(catalog, sibling->parent);
            if (sibling_medium == NULL || sibling_medium->kind != ASTRA_VOLUME_MEDIUM ||
                sibling_medium->parent != medium->parent)
                continue;
            if (sibling->state == ASTRA_VOLUME_FORMATTING)
                return EBUSY;
            if (record->capacity_bytes == 0u || sibling->capacity_bytes == 0u ||
                (record->offset_bytes < sibling->offset_bytes + sibling->capacity_bytes &&
                 sibling->offset_bytes < record->offset_bytes + record->capacity_bytes)) {
                error = astra_storage_no_mounted_children(catalog, sibling->id);
                if (error != 0) return error;
            }
        }
    }
    for (AstraStorageEntry *entry = catalog->entries; entry != NULL;
         entry = entry->next)
        if ((flags & ASTRA_VOLUME_TEMPORARY) != 0u &&
            (entry->record.flags & ASTRA_VOLUME_TEMPORARY) != 0u &&
            (entry->record.state == ASTRA_VOLUME_MOUNTING ||
             entry->record.state == ASTRA_VOLUME_MOUNTED ||
             entry->record.state == ASTRA_VOLUME_QUIESCING ||
             entry->record.linux_mount_id != 0u))
            return EBUSY;
    for (AstraStorageEntry *entry = catalog->entries; entry != NULL;
         entry = entry->next)
        if (entry->record.kind == ASTRA_VOLUME_MOUNT &&
            ((flags & entry->record.flags & ASTRA_VOLUME_SYSTEM) != 0u &&
              (entry->record.state == ASTRA_VOLUME_MOUNTING ||
               entry->record.state == ASTRA_VOLUME_MOUNTED ||
               entry->record.state == ASTRA_VOLUME_QUIESCING ||
               entry->record.linux_mount_id != 0u ||
               entry->mounting_generation != 0u ||
               (entry->record.flags & ASTRA_VOLUME_CLEANUP_PENDING) != 0u)))
            return EBUSY;
    error = mount_name(catalog, record->class_id, flags, usb_slot, name);
    if (error != 0) return error;
    error = add(catalog, ASTRA_VOLUME_MOUNT, record, record->class_id,
                 ASTRA_VOLUME_MOUNTING, mount);
    if (error != 0)
        return error;
    mounted = find(catalog, *mount);
    find_entry(catalog, *mount)->mounting_generation = mounted->generation;
    mounted->format = record->format;
    memcpy(mounted->name, name, (size_t)strlen(name) + 1u);
    mounted->backing_object = record->backing_object;
    mounted->flags = flags | (record->flags & ASTRA_VOLUME_LABEL_INVALID);
    if (record->class_id == ASTRA_VOLUME_RAM)
        mounted->flags |= ASTRA_VOLUME_VOLATILE;
    mounted->byte_limit = byte_limit;
    mounted->inode_limit = inode_limit;
    memcpy(mounted->uuid, record->uuid, sizeof(mounted->uuid));
    memcpy(mounted->label, record->label, sizeof(mounted->label));
    return 0;
}

static bool parent_live(const AstraStorageCatalog *catalog,
                         const AstraStorageRecord *record)
{
    const AstraStorageRecord *parent = astra_storage_find(catalog, record->parent);
    if (record->parent == 0u &&
        (record->class_id == ASTRA_VOLUME_RAM || record->kind == ASTRA_VOLUME_BINDING))
        return true;
    return live(parent) && parent->generation == record->parent_generation;
}

int astra_storage_mount_result(AstraStorageCatalog *catalog, uint64_t mount,
                               uint64_t generation, uint64_t linux_mount_id,
                               int error)
{
    AstraStorageRecord *record;
    AstraStorageEntry *entry;

    if (catalog == NULL || error < 0 || catalog->sequence == UINT64_MAX)
        return EINVAL;
    entry = find_entry(catalog, mount);
    if (entry == NULL || entry->mounting_generation != generation ||
        generation == 0u)
        return ESTALE;
    record = &entry->record;
    /* Loss while Linux was mounting cannot orphan the late successful mount. */
    if ((record->kind != ASTRA_VOLUME_MOUNT && record->kind != ASTRA_VOLUME_BINDING) ||
        (record->state != ASTRA_VOLUME_MOUNTING &&
         record->state != ASTRA_VOLUME_LOST))
        return EINVAL;
    if (error == 0 && linux_mount_id == 0u)
        return EINVAL;
    entry->mounting_generation = 0u;
    record->linux_mount_id = linux_mount_id;
    record->error = parent_live(catalog, record) ? error : ENODEV;
    record->state = record->error == 0 ? ASTRA_VOLUME_MOUNTED : ASTRA_VOLUME_FAILED;
    if (record->error != 0 && linux_mount_id != 0u)
        record->flags |= ASTRA_VOLUME_CLEANUP_PENDING;
    (void)changed(catalog);
    return record->error;
}

int astra_storage_quiesce(AstraStorageCatalog *catalog, uint64_t mount,
                          uint64_t generation)
{
    AstraStorageRecord *record;

    if (catalog == NULL || catalog->sequence == UINT64_MAX)
        return EINVAL;
    record = find(catalog, mount);
    if (!live(record) || record->generation != generation)
        return ESTALE;
    if (record->kind != ASTRA_VOLUME_MOUNT)
        return EINVAL;
    if ((record->flags & (ASTRA_VOLUME_SYSTEM | ASTRA_VOLUME_TEMPORARY)) != 0u)
        return EPERM;
    if (record->state != ASTRA_VOLUME_MOUNTED)
        return EBUSY;
    record->state = ASTRA_VOLUME_QUIESCING;
    record->error = 0;
    return changed(catalog);
}

int astra_storage_unmount_result(AstraStorageCatalog *catalog, uint64_t mount,
                                 uint64_t generation, int error,
                                 int linux_mount_present, int authority_intact)
{
    AstraStorageRecord *record;

    if (catalog == NULL || error < 0 || catalog->sequence == UINT64_MAX ||
        (linux_mount_present != 0 && linux_mount_present != 1) ||
        (authority_intact != 0 && authority_intact != 1) ||
        (!linux_mount_present && authority_intact))
        return EINVAL;
    record = find(catalog, mount);
    if (record == NULL || record->generation != generation)
        return ESTALE;
    if ((record->kind != ASTRA_VOLUME_MOUNT && record->kind != ASTRA_VOLUME_BINDING) ||
        (record->state != ASTRA_VOLUME_QUIESCING &&
         record->state != ASTRA_VOLUME_FAILED &&
         record->state != ASTRA_VOLUME_LOST))
        return EINVAL;
    if (error == 0 && linux_mount_present)
        return EPROTO;
    record->error = error;
    if (!linux_mount_present) {
        record->linux_mount_id = 0u;
        record->flags = error == 0 ? record->flags & ~ASTRA_VOLUME_CLEANUP_PENDING :
                                    record->flags | ASTRA_VOLUME_CLEANUP_PENDING;
        if (error == 0)
            record->state = ASTRA_VOLUME_UNMOUNTED;
        else if (record->state == ASTRA_VOLUME_QUIESCING)
            record->state = ASTRA_VOLUME_FAILED;
        else if (record->state != ASTRA_VOLUME_FAILED)
            record->state = ASTRA_VOLUME_LOST;
    } else {
        /* Only intact quiescence can restore usable authority. A failed mount
         * or lost generation remains quarantined even if Linux stays busy. */
        record->state = record->state == ASTRA_VOLUME_QUIESCING &&
            authority_intact && parent_live(catalog, record) ?
            ASTRA_VOLUME_MOUNTED : ASTRA_VOLUME_FAILED;
        if (record->state == ASTRA_VOLUME_FAILED)
            record->flags |= ASTRA_VOLUME_CLEANUP_PENDING;
    }
    (void)changed(catalog);
    return error;
}

static void lose_children(AstraStorageCatalog *catalog, uint64_t parent)
{
    for (AstraStorageEntry *entry = catalog->entries; entry != NULL;
         entry = entry->next)
        if (entry->record.parent == parent) {
            if (entry->record.state != ASTRA_VOLUME_LOST) {
                ++entry->record.generation;
                entry->record.state = ASTRA_VOLUME_LOST;
                entry->record.error = ENODEV;
            }
            lose_children(catalog, entry->record.id);
        }
}

int astra_storage_loss(AstraStorageCatalog *catalog, uint64_t id,
                       uint64_t generation)
{
    AstraStorageRecord *record;

    if (catalog == NULL || catalog->sequence == UINT64_MAX)
        return EINVAL;
    record = find(catalog, id);
    if (record == NULL || record->generation != generation)
        return ESTALE;
    if (record->state == ASTRA_VOLUME_LOST)
        return 0;
    for (AstraStorageEntry *entry = catalog->entries; entry != NULL;
         entry = entry->next)
        if (entry->record.generation == UINT64_MAX)
            return EOVERFLOW;
    ++record->generation;
    record->state = ASTRA_VOLUME_LOST;
    record->error = ENODEV;
    lose_children(catalog, id);
    duplicate_uuids(catalog);
    return changed(catalog);
}

int astra_storage_no_mounted_children(const AstraStorageCatalog *catalog, uint64_t source)
{
    if (catalog == NULL) return EINVAL;
    if (astra_storage_find(catalog, source) == NULL) return ESTALE;
    for (AstraStorageEntry *entry = catalog->entries; entry != NULL; entry = entry->next) {
        const AstraStorageRecord *ancestor = &entry->record;
        while (ancestor != NULL && ancestor->parent != 0u && ancestor->parent != source)
            ancestor = astra_storage_find(catalog, ancestor->parent);
        if (ancestor != NULL && ancestor->parent == source &&
            (entry->record.linux_mount_id != 0u ||
            entry->record.state == ASTRA_VOLUME_MOUNTING || entry->record.state == ASTRA_VOLUME_MOUNTED ||
            entry->record.state == ASTRA_VOLUME_QUIESCING || entry->mounting_generation != 0u ||
            (entry->record.flags & ASTRA_VOLUME_CLEANUP_PENDING) != 0u))
            return EBUSY;
    }
    return 0;
}

int astra_storage_partition_can_edit(const AstraStorageCatalog *catalog,
    uint64_t device, uint64_t generation)
{
    if (catalog == NULL) return EINVAL;
    const AstraStorageRecord *record = astra_storage_find(catalog, device);
    if (!live(record) || record->generation != generation)
        return ESTALE;
    if (record->kind != ASTRA_VOLUME_DEVICE ||
        (record->class_id != ASTRA_VOLUME_FIXED &&
         record->class_id != ASTRA_VOLUME_USB))
        return ENOTSUP;
    if ((record->flags & ASTRA_VOLUME_SYSTEM) != 0u) return EACCES;
    if ((record->flags & ASTRA_VOLUME_PHYSICAL_READ_ONLY) != 0u) return EROFS;
    if (record->state != ASTRA_VOLUME_DISCOVERED || record->capacity_bytes == 0u)
        return EBUSY;
    int error = astra_storage_no_mounted_children(catalog, device);
    if (error != 0) return error;
    for (AstraStorageEntry *entry = catalog->entries; entry != NULL;
         entry = entry->next) {
        const AstraStorageRecord *child = &entry->record;
        if (child->kind != ASTRA_VOLUME_SOURCE ||
            child->state != ASTRA_VOLUME_FORMATTING)
            continue;
        const AstraStorageRecord *medium = astra_storage_find(catalog, child->parent);
        if (medium != NULL && medium->parent == device)
            return EBUSY;
    }
    return 0;
}

int astra_storage_partition_begin(AstraStorageCatalog *catalog,
    uint64_t device, uint64_t generation)
{
    int error = astra_storage_partition_can_edit(catalog, device, generation);
    if (error != 0) return error;
    AstraStorageRecord *record = find(catalog, device);
    if (catalog->sequence == UINT64_MAX || record->generation == UINT64_MAX)
        return EOVERFLOW;
    record->state = ASTRA_VOLUME_PARTITIONING;
    record->error = 0;
    return changed(catalog);
}

int astra_storage_partition_abort(AstraStorageCatalog *catalog,
    uint64_t device, uint64_t generation)
{
    if (catalog == NULL || catalog->sequence == UINT64_MAX) return EINVAL;
    AstraStorageRecord *record = find(catalog, device);
    if (!live(record) || record->generation != generation) return ESTALE;
    if (record->kind != ASTRA_VOLUME_DEVICE ||
        record->state != ASTRA_VOLUME_PARTITIONING) return EINVAL;
    record->state = ASTRA_VOLUME_DISCOVERED;
    return changed(catalog);
}

int astra_storage_partition_complete(AstraStorageCatalog *catalog,
    uint64_t device, uint64_t generation, int result)
{
    if (catalog == NULL || result < 0 || result > 4095 ||
        catalog->sequence == UINT64_MAX) return EINVAL;
    AstraStorageRecord *record = find(catalog, device);
    if (!live(record) || record->generation != generation) return ESTALE;
    if (record->kind != ASTRA_VOLUME_DEVICE ||
        record->state != ASTRA_VOLUME_PARTITIONING) return EINVAL;
    for (AstraStorageEntry *entry = catalog->entries; entry != NULL;
         entry = entry->next)
        if (entry->record.generation == UINT64_MAX)
            return EOVERFLOW;
    int error = astra_storage_no_mounted_children(catalog, device);
    if (error != 0) return error;
    lose_children(catalog, device);
    ++record->generation;
    record->state = ASTRA_VOLUME_DISCOVERED;
    record->partition_map = ASTRA_VOLUME_PARTITION_MAP_OTHER;
    record->error = result;
    duplicate_uuids(catalog);
    return changed(catalog);
}

int astra_storage_format_begin(AstraStorageCatalog *catalog, uint64_t source,
    uint64_t generation)
{
    if (catalog == NULL || catalog->sequence == UINT64_MAX) return EINVAL;
    AstraStorageRecord *record = find(catalog, source);
    if (!live(record) || record->generation != generation) return ESTALE;
    if (record->kind != ASTRA_VOLUME_SOURCE ||
        record->origin != ASTRA_VOLUME_ORIGIN_DEVICE ||
        (record->class_id != ASTRA_VOLUME_USB && record->class_id != ASTRA_VOLUME_FIXED))
        return ENOTSUP;
    if (record->generation == UINT64_MAX) return EOVERFLOW;
    if (record->state != ASTRA_VOLUME_UNMOUNTED &&
        record->state != ASTRA_VOLUME_FAILED) return EBUSY;
    const AstraStorageRecord *medium = find(catalog, record->parent);
    const AstraStorageRecord *device = medium == NULL ? NULL : find(catalog, medium->parent);
    if (!live(medium) || medium->kind != ASTRA_VOLUME_MEDIUM ||
        medium->generation != record->parent_generation ||
        !live(device) || device->kind != ASTRA_VOLUME_DEVICE ||
        device->generation != medium->parent_generation)
        return ESTALE;
    if (device->state == ASTRA_VOLUME_PARTITIONING)
        return EBUSY;
    if ((record->flags | device->flags) & ASTRA_VOLUME_PHYSICAL_READ_ONLY)
        return EROFS;
    if ((record->flags | device->flags) & ASTRA_VOLUME_SYSTEM)
        return EACCES;
    int error = astra_storage_no_mounted_children(catalog, device->id);
    if (error != 0) return error;
    for (AstraStorageEntry *entry = catalog->entries; entry != NULL; entry = entry->next) {
        const AstraStorageRecord *sibling = &entry->record;
        if (sibling->state != ASTRA_VOLUME_FORMATTING) continue;
        const AstraStorageRecord *parent = find(catalog, sibling->parent);
        if (parent != NULL && parent->parent == device->id) return EBUSY;
    }
    record->state = ASTRA_VOLUME_FORMATTING;
    record->error = 0;
    return changed(catalog);
}

int astra_storage_format_complete(AstraStorageCatalog *catalog, uint64_t source,
    uint64_t generation)
{
    if (catalog == NULL || catalog->sequence == UINT64_MAX) return EINVAL;
    AstraStorageRecord *record = find(catalog, source);
    if (!live(record) || record->generation != generation) return ESTALE;
    if (record->kind != ASTRA_VOLUME_SOURCE || record->state != ASTRA_VOLUME_FORMATTING)
        return EINVAL;
    if (record->generation == UINT64_MAX) return EOVERFLOW;
    ++record->generation;
    record->state = ASTRA_VOLUME_PROBING;
    record->format = ASTRA_VOLUME_FORMAT_UNKNOWN;
    record->error = 0;
    record->uuid[0] = record->label[0] = record->detected_type[0] = '\0';
    record->flags &= ~(ASTRA_VOLUME_AMBIGUOUS_FORMAT | ASTRA_VOLUME_LABEL_INVALID |
                       ASTRA_VOLUME_DUPLICATE_UUID);
    if ((record->flags & ASTRA_VOLUME_PHYSICAL_READ_ONLY) == 0u)
        record->flags &= ~ASTRA_VOLUME_READ_ONLY;
    duplicate_uuids(catalog);
    return changed(catalog);
}

int astra_storage_format_abort(AstraStorageCatalog *catalog, uint64_t source,
    uint64_t generation)
{
    if (catalog == NULL || catalog->sequence == UINT64_MAX) return EINVAL;
    AstraStorageRecord *record = find(catalog, source);
    if (!live(record) || record->generation != generation) return ESTALE;
    if (record->kind != ASTRA_VOLUME_SOURCE || record->state != ASTRA_VOLUME_FORMATTING)
        return EINVAL;
    record->state = ASTRA_VOLUME_UNMOUNTED;
    return changed(catalog);
}

int astra_storage_eject_begin(AstraStorageCatalog *catalog, uint64_t source,
    uint64_t generation)
{
    if (catalog == NULL || catalog->sequence == UINT64_MAX) return EINVAL;
    AstraStorageRecord *record = find(catalog, source);
    if (record == NULL || record->generation != generation || record->state == ASTRA_VOLUME_LOST)
        return ESTALE;
    if (record->kind != ASTRA_VOLUME_SOURCE || record->class_id != ASTRA_VOLUME_OPTICAL ||
        record->origin != ASTRA_VOLUME_ORIGIN_DEVICE) return ENOTSUP;
    if (record->state != ASTRA_VOLUME_UNMOUNTED) return EBUSY;
    int error = astra_storage_no_mounted_children(catalog, record->parent);
    if (error != 0) return error;
    record->state = ASTRA_VOLUME_QUIESCING;
    record->error = 0;
    return changed(catalog);
}

int astra_storage_eject_result(AstraStorageCatalog *catalog, uint64_t source,
    uint64_t generation, int error)
{
    if (catalog == NULL || error < 0 || error > 4095 || catalog->sequence == UINT64_MAX) return EINVAL;
    AstraStorageRecord *record = find(catalog, source);
    if (record == NULL || record->generation != generation || record->state == ASTRA_VOLUME_LOST)
        return ESTALE;
    if (record->kind != ASTRA_VOLUME_SOURCE || record->class_id != ASTRA_VOLUME_OPTICAL ||
        record->origin != ASTRA_VOLUME_ORIGIN_DEVICE || record->state != ASTRA_VOLUME_QUIESCING)
        return EINVAL;
    if (error == 0)
        return astra_storage_loss(catalog, record->parent, record->parent_generation);
    record->state = ASTRA_VOLUME_UNMOUNTED;
    record->error = error;
    return changed(catalog);
}

static int can_retire(AstraStorageCatalog *catalog, uint64_t id, int require_cleanup)
{
    AstraStorageRecord *record;

    if (catalog == NULL || catalog->sequence == UINT64_MAX)
        return EINVAL;
    record = find(catalog, id);
    if (record == NULL)
        return ESTALE;
    if ((record->state != ASTRA_VOLUME_LOST &&
         record->state != ASTRA_VOLUME_FAILED &&
         !((record->kind == ASTRA_VOLUME_MOUNT || record->kind == ASTRA_VOLUME_BINDING) &&
           record->state == ASTRA_VOLUME_UNMOUNTED)) ||
        record->linux_mount_id != 0u || (require_cleanup && (record->flags & ASTRA_VOLUME_CLEANUP_PENDING) != 0u))
        return EBUSY;
    if (find_entry(catalog, id)->mounting_generation != 0u)
        return EBUSY;
    for (AstraStorageEntry *entry = catalog->entries; entry != NULL;
         entry = entry->next)
        if (entry->record.parent == id)
            return EBUSY;
    return 0;
}

int astra_storage_can_forget(AstraStorageCatalog *catalog, uint64_t id)
{
    return can_retire(catalog, id, 1);
}

int astra_storage_can_release(AstraStorageCatalog *catalog, uint64_t id)
{
    return can_retire(catalog, id, 0);
}

int astra_storage_release_result(AstraStorageCatalog *catalog, uint64_t id,
    uint64_t generation, int error)
{
    if (catalog == NULL || error < 0 || catalog->sequence == UINT64_MAX) return EINVAL;
    AstraStorageRecord *record = find(catalog, id);
    if (record == NULL || record->generation != generation) return ESTALE;
    if (record->state != ASTRA_VOLUME_LOST && record->state != ASTRA_VOLUME_FAILED &&
        record->state != ASTRA_VOLUME_UNMOUNTED) return EBUSY;
    uint32_t flags = error == 0 ? record->flags & ~ASTRA_VOLUME_CLEANUP_PENDING :
        record->flags | ASTRA_VOLUME_CLEANUP_PENDING;
    if (flags != record->flags || record->error != error) {
        record->flags = flags;
        record->error = error;
        return changed(catalog);
    }
    return 0;
}

int astra_storage_forget(AstraStorageCatalog *catalog, uint64_t id)
{
    AstraStorageEntry **link;
    int error = astra_storage_can_forget(catalog, id);
    if (error != 0)
        return error;
    for (link = &catalog->entries; *link != NULL; link = &(*link)->next)
        if ((*link)->record.id == id) {
            AstraStorageEntry *entry = *link;
            *link = entry->next;
            catalog->bytes -= sizeof(*entry);
            free(entry);
            break;
        }
    duplicate_uuids(catalog);
    return changed(catalog);
}

int astra_storage_snapshot(const AstraStorageCatalog *catalog,
                           uint64_t sequence, uint64_t after,
                           AstraStorageRecord *records, size_t capacity,
                           size_t *count, uint64_t *next, uint64_t *current)
{
    if (catalog == NULL || records == NULL || capacity == 0u || count == NULL ||
        next == NULL || current == NULL)
        return EINVAL;
    *count = 0u;
    *next = 0u;
    *current = catalog->sequence;
    if (sequence != 0u && sequence != catalog->sequence)
        return EAGAIN;
    if (sequence == 0u && after != 0u)
        return EINVAL;
    for (const AstraStorageEntry *entry = catalog->entries; entry != NULL;
         entry = entry->next) {
        if (entry->record.id <= after)
            continue;
        if (*count == capacity) {
            *next = records[*count - 1u].id;
            break;
        }
        records[(*count)++] = entry->record;
    }
    return 0;
}
