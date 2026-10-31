#include <astra/ndk.h>
#include <astra/vfs_clipboard.h>

static_assert(sizeof(AstraSyscallResult) == 20u,
              "installed NDK lost the runtime contract");
static_assert(sizeof(AstraVfsClipboardSelection) == 16u,
              "C++ clipboard header differs from the serialized wire header");

int astra_ndk_cxx_header_contract()
{
    AstraVfsClipboardSelection selection = {};
    const AstraVfsClipboardSelection *read_only = &selection;
    return ASTRA_FILESYSTEM_LIBRARY_VERSION == 15u &&
        astra_vfs_clipboard_selection_entries_mutable(&selection) ==
            astra_vfs_clipboard_selection_entries(read_only) ? 0 : 1;
}
