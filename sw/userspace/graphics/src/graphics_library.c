#include <astra/graphics_library.h>
#include <astra/library.h>

ASTRA_LIBRARY("graphics.library", 1, 3, 0,
              ASTRA_GRAPHICS_LIBRARY_ABI_MAJOR,
              ASTRA_GRAPHICS_LIBRARY_ABI_MINOR,
              "Barry Walker", "Copyright 2026 Barry Walker");

const AstraGraphicsLibraryV1 astra_library_exports ASTRA_LIBRARY_EXPORTS = {
    ASTRA_GRAPHICS_LIBRARY_ABI_MAJOR,
    ASTRA_GRAPHICS_LIBRARY_ABI_MINOR,
    sizeof(AstraGraphicsLibraryV1),
    astra_surface_view_init,
    astra_draw_list_view_init,
    astra_draw_list_view_adopt,
    astra_surface_rgb565,
    astra_surface_clear,
    astra_surface_fill,
    astra_shared_surface_create,
    astra_shared_draw_list_create,
    astra_shared_surface_close,
    astra_aicon_open,
    astra_aicon_strike,
    astra_aicon_palette,
    astra_draw_list_copy,
    astra_text_box_scroll,
};
