#include <astra/bytes.h>
#include <astra/interface_kit.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/stream.h>

/* Copy a TextSurface grid selection as the system's canonical UTF-8 type. */
AstraResult astra_example_copy_text(
    AstraHandle clipboard, const AstraTextSurface *text_surface,
    const AstraTextCell *cells,
    uint32_t stride, uint32_t columns, uint32_t rows,
    const AstraTextGridSelection *selection, char *scratch,
    uint32_t scratch_bytes)
{
    AstraClipboardRepresentation representation =
        ASTRA_CLIPBOARD_REPRESENTATION_INIT;
    uint32_t bytes = 0u;
    AstraResult result;

    result = astra_text_surface_copy_grid_selection(
        text_surface, cells, stride, columns, rows, selection, scratch,
        scratch_bytes, &bytes);
    if (result != ASTRA_OK) return result;
    representation.type = ASTRA_CLIPBOARD_TYPE_UTF8;
    representation.type_length = sizeof(ASTRA_CLIPBOARD_TYPE_UTF8) - 1u;
    representation.data = scratch;
    representation.data_length = bytes;
    return astra_clipboard_write(clipboard, &representation, 1u, 0);
}

/* Read a stable clipboard snapshot and copy its UTF-8 representation. */
AstraResult astra_example_paste_text(
    AstraHandle clipboard, char *output, uint32_t capacity, uint32_t *bytes)
{
    AstraClipboardItem item = ASTRA_CLIPBOARD_ITEM_INIT;
    const void *data = 0;
    uint32_t length = 0u;
    AstraResult result;
    AstraResult close_result;

    if (bytes == 0 || (output == 0 && capacity != 0u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_clipboard_read(clipboard, &item);
    if (result != ASTRA_OK) return result;
    result = astra_clipboard_item_find(
        &item, ASTRA_CLIPBOARD_TYPE_UTF8,
        sizeof(ASTRA_CLIPBOARD_TYPE_UTF8) - 1u, &data, &length);
    if (result == ASTRA_OK) {
        *bytes = length;
        if (length > capacity || (length != 0u && output == 0))
            result = ASTRA_ERROR_BUFFER_TOO_SMALL;
        else if (length != 0u)
            memcpy(output, data, length);
    }
    close_result = astra_clipboard_item_close(&item);
    return result == ASTRA_OK ? close_result : result;
}

ASTRA_PROGRAM("text_clipboard", 1, 0, 0, "Your Name",
              "Copyright 2026 Your Name");

enum { GRID_COLUMNS = 8u, GRID_ROWS = 2u };

static void fill_row(AstraTextCell *row, const char *text)
{
    for (uint32_t column = 0u; column < GRID_COLUMNS; ++column) {
        row[column] = (AstraTextCell){0};
        row[column].codepoint = (uint8_t)text[column];
        row[column].foreground = ASTRA_TEXT_COLOR_DEFAULT;
        row[column].background = ASTRA_TEXT_COLOR_DEFAULT;
        row[column].width = 1u;
    }
}

/* Copy the first word of a two-row grid to the system clipboard, then read
   it back the way any other application would. */
int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *output =
        astra_startup_capability(startup, "STDOUT");
    const AstraStartupCapability *clipboard =
        astra_startup_capability(startup, ASTRA_CAPABILITY_CLIPBOARD);
    static char surface_scratch[256];
    AstraTextSurfaceInfo info = ASTRA_TEXT_SURFACE_INFO_INIT;
    AstraTextSurface text_surface = ASTRA_TEXT_SURFACE_INIT;
    AstraTextGridSelection selection = ASTRA_TEXT_GRID_SELECTION_INIT;
    AstraTextCell cells[GRID_COLUMNS * GRID_ROWS];
    char scratch[32];
    char pasted[33];
    uint32_t bytes = 0u;

    if (output == 0 || clipboard == 0)
        return 1;
    info.font_height = 16u;
    info.cell_width = 8u;
    info.line_height = 20u;
    info.scratch = surface_scratch;
    info.scratch_bytes = sizeof(surface_scratch);
    if (astra_text_surface_init(&text_surface, &info) != ASTRA_OK)
        return 2;
    fill_row(cells, "Astra 68");
    fill_row(cells + GRID_COLUMNS, "clipping");
    selection.anchor.row = 0u;
    selection.anchor.column = 0u;
    selection.focus.row = 0u;
    selection.focus.column = 5u;
    if (astra_example_copy_text(clipboard->handle, &text_surface, cells,
                                GRID_COLUMNS, GRID_COLUMNS, GRID_ROWS,
                                &selection, scratch, sizeof(scratch)) !=
            ASTRA_OK ||
        astra_example_paste_text(clipboard->handle, pasted,
                                 sizeof(pasted) - 1u, &bytes) != ASTRA_OK)
        return 3;
    pasted[bytes] = '\0';
    (void)astra_print(output->handle, "text_clipboard: copied and pasted \"");
    (void)astra_print(output->handle, pasted);
    (void)astra_print(output->handle, "\"\n");
    return 0;
}
