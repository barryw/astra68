#include <astra/graphics_library.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t creates, writes, closes, live_allocations;
static int fail_allocate, fail_create, fail_write;

void *astra_runtime_allocate(size_t length)
{
    void *bytes;
    if (fail_allocate)
        return NULL;
    bytes = malloc(length);
    live_allocations += bytes != NULL;
    return bytes;
}
void astra_runtime_deallocate(void *bytes)
{
    if (bytes != NULL)
        --live_allocations;
    free(bytes);
}
AstraResult astra_surface_create(const AstraDisplay *display,
    const AstraSurfaceCreateInfo *info, AstraSurface *surface)
{
    (void)display;
    assert(info->format == ASTRA_PIXEL_FORMAT_ARGB8888);
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
           rect->height == 64u && pitch == 256u && pixels != NULL);
    ++writes;
    return fail_write ? ASTRA_ERROR_IO : ASTRA_OK;
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

    assert(argc == 2);
    file = fopen(argv[1], "rb");
    assert(file != NULL && fseek(file, 0, SEEK_END) == 0);
    length = ftell(file);
    assert(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    bytes = malloc((size_t)length);
    assert(bytes != NULL && fread(bytes, 1, (size_t)length, file) == (size_t)length);
    assert(fclose(file) == 0);
    assert(astra_surface_from_aicon(&display, bytes, (uint32_t)length, 64u,
                                     &surface) == ASTRA_OK);
    assert(creates == 1u && writes == 1u && live_allocations == 0u);
    assert(astra_surface_close(&surface) == ASTRA_OK);
    assert(astra_surface_from_aicon(&display, bytes, 3u, 64u, &surface) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_surface_from_aicon(&display, bytes, (uint32_t)length, 17u,
                                     &surface) == ASTRA_ERROR_INVALID_ARGUMENT);
    fail_allocate = 1;
    assert(astra_surface_from_aicon(&display, bytes, (uint32_t)length, 64u,
                                     &surface) == ASTRA_ERROR_NO_RESOURCES);
    fail_allocate = 0; fail_create = 1;
    assert(astra_surface_from_aicon(&display, bytes, (uint32_t)length, 64u,
                                     &surface) == ASTRA_ERROR_NO_RESOURCES);
    assert(surface._private_handle == ASTRA_INVALID_HANDLE && live_allocations == 0u);
    fail_create = 0; fail_write = 1;
    assert(astra_surface_from_aicon(&display, bytes, (uint32_t)length, 64u,
                                     &surface) == ASTRA_ERROR_IO);
    assert(surface._private_handle == ASTRA_INVALID_HANDLE && live_allocations == 0u && closes == 2u);
    free(bytes);
    puts("AICON SURFACE PASS");
    return 0;
}
