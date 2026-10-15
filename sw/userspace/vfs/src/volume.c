#include <astra/volume_port.h>

int astra_volume_control_valid(const AstraVolumeControl *control)
{
    if (control == NULL) return 0;
    if (control->operation == ASTRA_VOLUME_IMAGE)
        return control->reference.epoch == 0u && control->reference.id == 0u &&
            control->reference.generation == 0u && control->flags == 0u &&
            control->byte_limit == 0u && control->inode_limit == 0u && control->image_share != 0u;
    if (control->reference.epoch == 0u || control->reference.id == 0u ||
        control->reference.generation == 0u || control->image_share != 0u) return 0;
    if (control->operation == ASTRA_VOLUME_MOUNT_SOURCE)
        return (control->flags & ~ASTRA_VOLUME_READ_ONLY) == 0u;
    return (control->operation == ASTRA_VOLUME_UNMOUNT || control->operation == ASTRA_VOLUME_EJECT ||
        control->operation == ASTRA_VOLUME_RELEASE_IMAGE_SOURCE) && control->flags == 0u &&
        control->byte_limit == 0u && control->inode_limit == 0u;
}
#include <astra/utf8.h>
#include <string.h>

int astra_volume_usage_query_valid(const AstraVolumeUsageQuery *query)
{
    return query != NULL && query->version == ASTRA_VOLUME_USAGE_VERSION &&
        query->size == sizeof(*query) &&
        ((query->volume.id == 0u) == (query->volume.generation == 0u)) &&
        (query->volume.id == 0u || query->volume.epoch != 0u) &&
        ((query->sequence == 0u) == (query->catalog_sequence == 0u)) &&
        ((query->after == 0u) == (query->after_kind == 0u)) &&
        (query->after != 0u || query->after_process == 0u) &&
        query->after_kind <= ASTRA_VOLUME_USAGE_IMAGE &&
        (query->volume.epoch != 0u || query->sequence == 0u) &&
        (query->sequence != 0u || query->after == 0u);
}

int astra_volume_usage_page_valid(const AstraVolumeUsageQuery *query,
    const AstraVolumeUsagePage *page)
{
    if (!astra_volume_usage_query_valid(query) || page == NULL ||
        page->version != ASTRA_VOLUME_USAGE_VERSION || page->size != sizeof(*page) ||
        page->volume.epoch == 0u || page->volume.id != query->volume.id ||
        page->volume.generation != query->volume.generation ||
        (query->volume.epoch != 0u && page->volume.epoch != query->volume.epoch) ||
        page->sequence == 0u || page->catalog_sequence == 0u ||
        (query->sequence != 0u && page->sequence != query->sequence) ||
        (query->catalog_sequence != 0u && page->catalog_sequence != query->catalog_sequence) ||
        page->count > ASTRA_VOLUME_USAGE_PAGE_RECORDS || page->reserved != 0u) return 0;
    uint64_t handle = query->after;
    uint32_t kind = query->after_kind;
    uint32_t process = query->after_process;
    for (uint32_t at = 0u; at < page->count; ++at) {
        const AstraVolumeUsageRecord *row = &page->records[at];
        if (row->volume.epoch != page->volume.epoch || row->volume.id == 0u ||
            row->volume.generation == 0u || row->handle == 0u ||
            (page->volume.id != 0u && (row->volume.id != page->volume.id ||
                row->volume.generation != page->volume.generation)) ||
            row->kind < ASTRA_VOLUME_USAGE_FILE || row->kind > ASTRA_VOLUME_USAGE_IMAGE ||
            astra_volume_usage_key_compare(row->kind, row->handle, row->process, kind, handle, process) <= 0 ||
            (row->rights & ~(ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE)) != 0u ||
            (row->flags & ~(ASTRA_VOLUME_USAGE_LOST | ASTRA_VOLUME_USAGE_UNNAMED |
                ASTRA_VOLUME_USAGE_ACTIVE | ASTRA_VOLUME_USAGE_CLEANUP_PENDING |
                ASTRA_VOLUME_USAGE_INVALID_NAME)) != 0u || row->reserved != 0u ||
            row->path_length >= sizeof(row->path) || row->path[row->path_length] != '\0' ||
            memchr(row->path, '\0', row->path_length) != NULL ||
            ((row->flags & ASTRA_VOLUME_USAGE_UNNAMED) != 0u ? row->path_length != 0u :
                row->path_length == 0u || row->path[0] != '/') ||
            !astra_utf8_validate(row->path, row->path_length, 0u)) return 0;
        kind = row->kind; handle = row->handle; process = row->process;
    }
    return (page->next == 0u && page->next_kind == 0u && page->next_process == 0u) ||
        (page->count != 0u && page->next == handle && page->next_kind == kind && page->next_process == process);
}

int astra_volume_query_valid(const AstraVolumeCatalogQuery *query)
{
    return query != NULL && query->version == ASTRA_VOLUME_CATALOG_VERSION &&
        query->size == sizeof(*query) &&
        (query->epoch != 0u || (query->sequence == 0u && query->after == 0u)) &&
        (query->sequence != 0u || query->after == 0u);
}

int astra_volume_page_valid(const AstraVolumeCatalogQuery *query,
                            const AstraVolumeCatalogPage *page)
{
    if (!astra_volume_query_valid(query) || page == NULL ||
        page->version != ASTRA_VOLUME_CATALOG_VERSION || page->size != sizeof(*page) ||
        page->epoch == 0u || page->sequence == 0u || page->reserved != 0u ||
        page->count > ASTRA_VOLUME_PAGE_RECORDS ||
        (query->epoch != 0u && query->epoch != page->epoch) ||
        (query->sequence != 0u && query->sequence != page->sequence))
        return 0;
    for (uint32_t at = 0u; at < page->count; ++at) {
        const AstraVolumeRecord *row = &page->records[at];
        if (row->id == 0u || row->generation == 0u || row->reserved != 0u ||
            row->kind < ASTRA_VOLUME_DEVICE || row->kind > ASTRA_VOLUME_BINDING ||
            row->state < ASTRA_VOLUME_DISCOVERED || row->state > ASTRA_VOLUME_FAILED ||
            row->class_id > ASTRA_VOLUME_RAM || row->format > ASTRA_VOLUME_TMPFS ||
            row->origin > ASTRA_VOLUME_ORIGIN_SYSTEM_IMAGE ||
            (row->flags & ~(ASTRA_VOLUME_READ_ONLY | ASTRA_VOLUME_SYSTEM | ASTRA_VOLUME_VOLATILE |
                ASTRA_VOLUME_DUPLICATE_UUID | ASTRA_VOLUME_AMBIGUOUS_FORMAT | ASTRA_VOLUME_LABEL_INVALID |
                ASTRA_VOLUME_TEMPORARY | ASTRA_VOLUME_CLEANUP_PENDING | ASTRA_VOLUME_CONFIGURED_RAM)) != 0u ||
            ((row->flags & ASTRA_VOLUME_CONFIGURED_RAM) != 0u &&
                (row->kind != ASTRA_VOLUME_SOURCE || row->class_id != ASTRA_VOLUME_RAM ||
                    row->origin != ASTRA_VOLUME_ORIGIN_RAM)) ||
            ((row->parent == 0u) != (row->parent_generation == 0u)) ||
            memchr(row->name, '\0', sizeof(row->name)) == NULL ||
            memchr(row->uuid, '\0', sizeof(row->uuid)) == NULL ||
            memchr(row->label, '\0', sizeof(row->label)) == NULL ||
            row->id <= (at == 0u ? query->after : page->records[at - 1u].id))
            return 0;
        if (!astra_utf8_validate(row->name, (uint32_t)strlen(row->name), 0u) ||
            !astra_utf8_validate(row->uuid, (uint32_t)strlen(row->uuid), 0u) ||
            !astra_utf8_validate(row->label, (uint32_t)strlen(row->label), 0u))
            return 0;
    }
    return page->next == 0u || (page->count != 0u &&
        page->next == page->records[page->count - 1u].id);
}
