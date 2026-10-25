#include <astra/graphics_library.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t creates, writes, palettes, closes;
static int fail_create, fail_write, fail_staging, fail_palette;
static uint8_t staging[1024];
static const uint8_t *expected_pixels;
static uint16_t expected_palette_count;
AstraResult astra_surface_create(const AstraDisplay *display,
    const AstraSurfaceCreateInfo *info, AstraSurface *surface)
{
    (void)display;
    assert(info->format == ASTRA_PIXEL_FORMAT_INDEX8);
    assert(info->width == 64u && info->height == 64u);
    ++creates;
    if (fail_create)
        return ASTRA_ERROR_NO_RESOURCES;
    surface->_private_handle = 7u;
    return ASTRA_OK;
}
AstraResult astra_surface_write(AstraDisplay *display,
    const AstraSurface *surface, const AstraRectI32 *rect, const void *pixels,
    uint32_t pitch)
{
    (void)display;
    assert(surface->_private_handle == 7u && rect->width == 64u &&
           rect->height == 64u && pitch == 64u && pixels == expected_pixels);
    ++writes;
    return fail_write ? ASTRA_ERROR_IO : ASTRA_OK;
}
AstraResult astra_display_staging(AstraDisplay *display,
    uint32_t minimum_bytes, void **pixels, uint32_t *bytes)
{
    (void)display;
    assert(minimum_bytes == expected_palette_count * 4u);
    if (fail_staging)
        return ASTRA_ERROR_NO_RESOURCES;
    *pixels = staging;
    *bytes = sizeof(staging);
    return ASTRA_OK;
}
AstraResult astra_surface_palette_write_staged(const AstraSurface *surface,
    uint16_t first, uint16_t count, uint32_t offset)
{
    assert(surface->_private_handle == 7u && first == 0u &&
           count == expected_palette_count && offset == 0u);
    ++palettes;
    return fail_palette ? ASTRA_ERROR_IO : ASTRA_OK;
}
AstraResult astra_surface_close(AstraSurface *surface)
{
    ++closes;
    *surface = (AstraSurface)ASTRA_SURFACE_INIT;
    return ASTRA_OK;
}

int main(int argc, char **argv)
{
    FILE *file;
    long length;
    uint8_t *bytes;
    AstraDisplay display = ASTRA_DISPLAY_INIT;
    AstraSurface surface = ASTRA_SURFACE_INIT;
    AstraAicon icon;
    AstraAiconStrike strike;
    uint8_t rgba[4];

    assert(argc == 2);
    file = fopen(argv[1], "rb");
    assert(file != NULL && fseek(file, 0, SEEK_END) == 0);
    length = ftell(file);
    assert(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    bytes = malloc((size_t)length);
    assert(bytes != NULL && fread(bytes, 1, (size_t)length, file) == (size_t)length);
    assert(fclose(file) == 0);
    assert(astra_aicon_open(bytes, (uint32_t)length, &icon) == ASTRA_BUNDLE_OK);
    assert(astra_aicon_strike(&icon, 64u, &strike) == ASTRA_BUNDLE_OK);
    expected_pixels = strike.pixels;
    expected_palette_count = icon.palette_count;
    assert(astra_surface_from_aicon(&display, bytes, (uint32_t)length, 64u,
                                     &surface) == ASTRA_OK);
    assert(creates == 1u && writes == 1u && palettes == 1u);
    assert(astra_aicon_palette(&icon, 1u, rgba) == ASTRA_BUNDLE_OK);
    assert(staging[4u] == rgba[3] && staging[5u] == rgba[0] &&
           staging[6u] == rgba[1] && staging[7u] == rgba[2]);
    assert(astra_surface_close(&surface) == ASTRA_OK);
    assert(astra_surface_from_aicon(&display, bytes, 3u, 64u, &surface) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_surface_from_aicon(&display, bytes, (uint32_t)length, 17u,
                                     &surface) == ASTRA_ERROR_INVALID_ARGUMENT);
    fail_create = 1;
    assert(astra_surface_from_aicon(&display, bytes, (uint32_t)length, 64u,
                                     &surface) == ASTRA_ERROR_NO_RESOURCES);
    assert(surface._private_handle == ASTRA_INVALID_HANDLE);
    fail_create = 0; fail_write = 1;
    assert(astra_surface_from_aicon(&display, bytes, (uint32_t)length, 64u,
                                     &surface) == ASTRA_ERROR_IO);
    assert(surface._private_handle == ASTRA_INVALID_HANDLE && closes == 2u);
    fail_write = 0; fail_staging = 1;
    assert(astra_surface_from_aicon(&display, bytes, (uint32_t)length, 64u,
                                     &surface) == ASTRA_ERROR_NO_RESOURCES);
    assert(surface._private_handle == ASTRA_INVALID_HANDLE && closes == 3u);
    fail_staging = 0; fail_palette = 1;
    assert(astra_surface_from_aicon(&display, bytes, (uint32_t)length, 64u,
                                     &surface) == ASTRA_ERROR_IO);
    assert(surface._private_handle == ASTRA_INVALID_HANDLE && closes == 4u);
    free(bytes);
    puts("AICON SURFACE PASS");
    return 0;
}
