#include <astra/vfs_clipboard.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    assert(astra_vfs_clipboard_selection_size(0u) == 0u);
    assert(astra_vfs_clipboard_selection_size(UINT32_MAX) == 0u);
    uint32_t bytes = astra_vfs_clipboard_selection_size(2u);
    AstraVfsClipboardSelection *selection = malloc(bytes);
    assert(selection != NULL);
    assert(astra_vfs_clipboard_selection_entries_mutable(selection) ==
           selection->entries);
    assert(astra_vfs_clipboard_selection_entries(selection) ==
           selection->entries);
    memset(selection, 0, bytes);
    selection->size = bytes;
    selection->version = ASTRA_VFS_CLIPBOARD_SELECTION_VERSION;
    selection->count = 2u;
    strcpy(selection->entries[0].path, "/dh0/Doom.app");
    strcpy(selection->entries[1].path, "/usb0/Quake.app");
    selection->entries[1].volume = (AstraVolumeReference){12u, 3u, 8u};
    assert(astra_vfs_clipboard_selection_validate(selection, bytes) == ASTRA_OK);
    selection->operation = ASTRA_VFS_CLIPBOARD_MOVE;
    assert(astra_vfs_clipboard_selection_validate(selection, bytes) == ASTRA_OK);
    selection->operation = 2u;
    assert(astra_vfs_clipboard_selection_validate(selection, bytes) != ASTRA_OK);
    selection->operation = ASTRA_VFS_CLIPBOARD_COPY;
    selection->version = 2u;
    assert(astra_vfs_clipboard_selection_validate(selection, bytes) != ASTRA_OK);
    selection->version = ASTRA_VFS_CLIPBOARD_SELECTION_VERSION;
    assert(astra_vfs_clipboard_selection_validate(selection, bytes - 1u) != ASTRA_OK);
    selection->entries[1].path[0] = 'u';
    assert(astra_vfs_clipboard_selection_validate(selection, bytes) != ASTRA_OK);
    strcpy(selection->entries[1].path, "/usb0/../Quake.app");
    assert(astra_vfs_clipboard_selection_validate(selection, bytes) != ASTRA_OK);
    strcpy(selection->entries[1].path, "/usb0/Quake.app");
    selection->entries[0].volume.epoch = 1u;
    assert(astra_vfs_clipboard_selection_validate(selection, bytes) != ASTRA_OK);
    selection->entries[0].volume.epoch = 0u;
    selection->count = UINT32_MAX;
    assert(astra_vfs_clipboard_selection_validate(selection, bytes) != ASTRA_OK);
    free(selection);
    return 0;
}
