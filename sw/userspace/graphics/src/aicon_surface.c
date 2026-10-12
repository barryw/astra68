#include <astra/graphics_library.h>
#include <astra/runtime.h>

#include <stddef.h>

AstraResult astra_surface_from_aicon(AstraDisplay *display, const void *bytes,
    uint32_t length, uint16_t pixel_size, AstraSurface *surface)
{
    AstraAicon icon;
    AstraAiconStrike strike;
    AstraSurfaceCreateInfo create = ASTRA_SURFACE_CREATE_INFO_INIT;
    uint32_t *pixels;
    uint32_t count;
    AstraResult result;

    if (display == NULL || surface == NULL ||
        surface->_private_handle != ASTRA_INVALID_HANDLE ||
        astra_aicon_open(bytes, length, &icon) != ASTRA_BUNDLE_OK ||
        astra_aicon_strike(&icon, pixel_size, &strike) != ASTRA_BUNDLE_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    count = (uint32_t)strike.width * strike.height;
    pixels = astra_runtime_allocate((size_t)count * sizeof(*pixels));
    if (pixels == NULL)
        return ASTRA_ERROR_NO_RESOURCES;
    if (astra_aicon_strike_argb(&icon, &strike, pixels, strike.width) !=
        ASTRA_BUNDLE_OK) {
        astra_runtime_deallocate(pixels);
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    create.flags = ASTRA_SURFACE_DRAW_SOURCE | ASTRA_SURFACE_CPU_WRITE;
    create.width = strike.width;
    create.height = strike.height;
    create.format = ASTRA_PIXEL_FORMAT_ARGB8888;
    result = astra_surface_create(display, &create, surface);
    if (result == ASTRA_OK)
        result = astra_surface_write(display, surface,
            &(AstraRectI32){0, 0, strike.width, strike.height}, pixels,
            (uint32_t)strike.width * sizeof(*pixels));
    astra_runtime_deallocate(pixels);
    if (result != ASTRA_OK &&
        surface->_private_handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_surface_close(surface);
        (void)ignored;
    }
    return result;
}
