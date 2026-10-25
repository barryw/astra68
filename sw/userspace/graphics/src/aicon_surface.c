#include <astra/graphics_library.h>
#include <stddef.h>

AstraResult astra_surface_from_aicon(AstraDisplay *display, const void *bytes,
    uint32_t length, uint16_t pixel_size, AstraSurface *surface)
{
    AstraAicon icon;
    AstraAiconStrike strike;
    AstraSurfaceCreateInfo create = ASTRA_SURFACE_CREATE_INFO_INIT;
    uint8_t *palette = NULL;
    uint32_t staging_bytes = 0u;
    AstraResult result;

    if (display == NULL || surface == NULL ||
        surface->_private_handle != ASTRA_INVALID_HANDLE ||
        astra_aicon_open(bytes, length, &icon) != ASTRA_BUNDLE_OK ||
        astra_aicon_strike(&icon, pixel_size, &strike) != ASTRA_BUNDLE_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    create.flags = ASTRA_SURFACE_DRAW_SOURCE | ASTRA_SURFACE_CPU_WRITE;
    create.width = strike.width;
    create.height = strike.height;
    create.format = ASTRA_PIXEL_FORMAT_INDEX8;
    result = astra_surface_create(display, &create, surface);
    if (result == ASTRA_OK)
        result = astra_surface_write(display, surface,
            &(AstraRectI32){0, 0, strike.width, strike.height},
            strike.pixels, strike.width);
    if (result == ASTRA_OK)
        result = astra_display_staging(display, icon.palette_count * 4u,
                                       (void **)&palette, &staging_bytes);
    if (result == ASTRA_OK) {
        for (uint16_t at = 0u; at < icon.palette_count; ++at) {
            uint8_t rgba[4];

            if (astra_aicon_palette(&icon, at, rgba) != ASTRA_BUNDLE_OK) {
                result = ASTRA_ERROR_INVALID_ARGUMENT;
                break;
            }
            palette[at * 4u] = rgba[3];
            palette[at * 4u + 1u] = rgba[0];
            palette[at * 4u + 2u] = rgba[1];
            palette[at * 4u + 3u] = rgba[2];
        }
    }
    if (result == ASTRA_OK)
        result = astra_surface_palette_write_staged(
            surface, 0u, icon.palette_count, 0u);
    if (result != ASTRA_OK &&
        surface->_private_handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_surface_close(surface);
        (void)ignored;
    }
    return result;
}
