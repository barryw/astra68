#include <astra/vfs_clipboard.h>
#include <astra/vfs_path.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

uint32_t astra_vfs_clipboard_selection_size(uint32_t count)
{
    const uint32_t header = (uint32_t)offsetof(
        AstraVfsClipboardSelection, entries);
    if (count == 0u || count > (UINT32_MAX - header) /
            sizeof(AstraVfsClipboardEntry))
        return 0u;
    return header + count * sizeof(AstraVfsClipboardEntry);
}

AstraResult astra_vfs_clipboard_selection_validate(const void *data,
    uint32_t bytes)
{
    const AstraVfsClipboardSelection *selection = data;
    if (data == NULL || bytes < offsetof(AstraVfsClipboardSelection, entries))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (selection->version != ASTRA_VFS_CLIPBOARD_SELECTION_VERSION ||
        (selection->operation != ASTRA_VFS_CLIPBOARD_COPY &&
         selection->operation != ASTRA_VFS_CLIPBOARD_MOVE) ||
        selection->size != bytes ||
        astra_vfs_clipboard_selection_size(selection->count) != bytes)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < selection->count; ++at) {
        const AstraVfsClipboardEntry *entry = &selection->entries[at];
        if (memchr(entry->path, '\0', sizeof(entry->path)) == NULL ||
            entry->path[0] != '/' || entry->path[1] == '\0' ||
            astra_path_requires_walk(entry->path) ||
            (entry->volume.id == 0u &&
             (entry->volume.epoch != 0u || entry->volume.generation != 0u)))
            return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    return ASTRA_OK;
}
