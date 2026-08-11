#include <astra/surface.h>

#include <astra/draw_list.h>
#include <astra/runtime.h>

#include <stddef.h>

static void reset(AstraSharedSurface *surface)
{
    surface->view = (AstraSurfaceView){0};
    surface->area = 0u;
    surface->mapping = NULL;
}

uint32_t astra_shared_surface_adopt(AstraSharedSurface *surface,
                                    uint32_t area, uint16_t width,
                                    uint16_t height, uint32_t pitch,
                                    uint32_t map_flags)
{
    void *mapping = NULL;
    uint32_t bytes = 0u;
    uint32_t status;

    if (surface == NULL || surface->area != 0u || surface->mapping != NULL ||
        area == 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    status = astra_rt_area_map(area, map_flags, &mapping, &bytes);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    if (!astra_surface_view_init(&surface->view, mapping, bytes, width, height,
                                 pitch)) {
        (void)astra_rt_area_unmap(mapping);
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    surface->area = area;
    surface->mapping = mapping;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_shared_surface_create(AstraSharedSurface *surface,
                                     uint16_t width, uint16_t height)
{
    uint32_t area = 0u;
    uint32_t pitch = (uint32_t)width * sizeof(uint16_t);
    uint64_t bytes = (uint64_t)pitch * height;
    uint32_t status;

    if (surface == NULL || surface->area != 0u || surface->mapping != NULL ||
        width == 0u || height == 0u || bytes > ASTRA_AREA_SIZE_MAX)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    status = astra_rt_area_create(
        (uint32_t)bytes,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
            ASTRA_RIGHT_TRANSFER,
        &area);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    status = astra_shared_surface_adopt(
        surface, area, width, height, pitch,
        ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE);
    if (status != ASTRA_SYSCALL_OK)
        (void)astra_close(area);
    return status;
}

uint32_t astra_shared_draw_list_adopt(AstraSharedSurface *surface,
                                      uint32_t area, uint16_t width,
                                      uint16_t height, uint32_t map_flags)
{
    void *mapping = NULL;
    uint32_t bytes = 0u;
    uint32_t status;

    if (surface == NULL || surface->area != 0u || surface->mapping != NULL ||
        area == 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    status = astra_rt_area_map(area, map_flags, &mapping, &bytes);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    if (!astra_draw_list_view_adopt(&surface->view, mapping, bytes, width,
                                    height)) {
        (void)astra_rt_area_unmap(mapping);
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    surface->area = area;
    surface->mapping = mapping;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_shared_draw_list_create(AstraSharedSurface *surface,
                                       uint16_t width, uint16_t height)
{
    uint32_t area = 0u;
    uint32_t bytes = 0u;
    void *mapping = NULL;
    uint32_t status;

    if (surface == NULL || surface->area != 0u || surface->mapping != NULL ||
        width == 0u || height == 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    status = astra_rt_area_create(
        ASTRA_DRAW_LIST_AREA_BYTES,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
            ASTRA_RIGHT_TRANSFER,
        &area);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    status = astra_rt_area_map(area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                            &mapping, &bytes);
    if (status == ASTRA_SYSCALL_OK &&
        !astra_draw_list_view_init(&surface->view, mapping, bytes, width,
                                   height))
        status = ASTRA_SYSCALL_INVALID_ARGUMENT;
    if (status == ASTRA_SYSCALL_OK) {
        surface->area = area;
        surface->mapping = mapping;
    } else {
        if (mapping != NULL)
            (void)astra_rt_area_unmap(mapping);
        (void)astra_close(area);
    }
    return status;
}

uint32_t astra_shared_surface_close(AstraSharedSurface *surface)
{
    uint32_t first = ASTRA_SYSCALL_OK;

    if (surface == NULL || surface->area == 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    if (surface->mapping != NULL) {
        first = astra_rt_area_unmap(surface->mapping);
        if (first == ASTRA_SYSCALL_OK)
            surface->mapping = NULL;
    }
    {
        uint32_t status = astra_close(surface->area);

        if (first == ASTRA_SYSCALL_OK)
            first = status;
    }
    reset(surface);
    return first;
}
