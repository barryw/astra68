#include <astra/font_library.h>
#include <astra/library.h>

ASTRA_LIBRARY("font.library", 1, 0, 0,
              ASTRA_FONT_LIBRARY_ABI_MAJOR, ASTRA_FONT_LIBRARY_ABI_MINOR,
              "Barry Walker", "Copyright 2026 Barry Walker");

const AstraFontLibraryV1 astra_library_exports ASTRA_LIBRARY_EXPORTS = {
    ASTRA_FONT_LIBRARY_ABI_MAJOR,
    ASTRA_FONT_LIBRARY_ABI_MINOR,
    sizeof(AstraFontLibraryV1),
    astra_surface_ui_text_width,
    astra_surface_ui_text_fit,
    astra_surface_mono_cell_width,
    astra_draw_list_mono_text,
};
