#include <astra/interface_kit.h>

#include <string.h>

/* Copy a TextSurface grid selection as the system's canonical UTF-8 type. */
AstraResult astra_example_copy_text(
    const AstraInterfaceLibraryV2 *interface, AstraHandle clipboard,
    const AstraTextSurface *text_surface, const AstraTextCell *cells,
    uint32_t stride, uint32_t columns, uint32_t rows,
    const AstraTextGridSelection *selection, char *scratch,
    uint32_t scratch_bytes)
{
    AstraClipboardRepresentation representation =
        ASTRA_CLIPBOARD_REPRESENTATION_INIT;
    uint32_t bytes = 0u;
    AstraResult result;

    if (!astra_interface_library_supports(
            interface, 4u, ASTRA_INTERFACE_LIBRARY_2_4_SIZE))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = interface->text_surface_copy_grid_selection(
        text_surface, cells, stride, columns, rows, selection, scratch,
        scratch_bytes, &bytes);
    if (result != ASTRA_OK) return result;
    representation.type = ASTRA_CLIPBOARD_TYPE_UTF8;
    representation.type_length = sizeof(ASTRA_CLIPBOARD_TYPE_UTF8) - 1u;
    representation.data = scratch;
    representation.data_length = bytes;
    return interface->clipboard_write(clipboard, &representation, 1u, 0);
}

/* Read a stable clipboard snapshot and copy its UTF-8 representation. */
AstraResult astra_example_paste_text(
    const AstraInterfaceLibraryV2 *interface, AstraHandle clipboard,
    char *output, uint32_t capacity, uint32_t *bytes)
{
    AstraClipboardItem item = ASTRA_CLIPBOARD_ITEM_INIT;
    const void *data = 0;
    uint32_t length = 0u;
    AstraResult result;
    AstraResult close_result;

    if (bytes == 0 || (output == 0 && capacity != 0u) ||
        !astra_interface_library_supports(
            interface, 4u, ASTRA_INTERFACE_LIBRARY_2_4_SIZE))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = interface->clipboard_read(clipboard, &item);
    if (result != ASTRA_OK) return result;
    result = interface->clipboard_item_find(
        &item, ASTRA_CLIPBOARD_TYPE_UTF8,
        sizeof(ASTRA_CLIPBOARD_TYPE_UTF8) - 1u, &data, &length);
    if (result == ASTRA_OK) {
        *bytes = length;
        if (length > capacity || (length != 0u && output == 0))
            result = ASTRA_ERROR_BUFFER_TOO_SMALL;
        else if (length != 0u)
            memcpy(output, data, length);
    }
    close_result = interface->clipboard_item_close(&item);
    return result == ASTRA_OK ? close_result : result;
}
