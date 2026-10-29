#include "picture.h"

#include <SDL.h>
#include <SDL_image.h>

#include <limits.h>
#include <stdint.h>
#include <string.h>

#define PNG_TAG UINT32_C(0x504e4720)
#define JPEG_TAG UINT32_C(0x4a504547)
#define RECT_TAG UINT32_C(0x52656374)

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
        ((uint32_t)p[2] << 8) | p[3];
}

static SDL_Surface *decode(const AstraZResource *resource)
{
    if (!resource || !resource->bytes || resource->length == 0u ||
        resource->length > INT_MAX) return NULL;
    if (resource->format != PNG_TAG && resource->format != JPEG_TAG)
        return NULL;
    SDL_RWops *stream = SDL_RWFromConstMem(resource->bytes,
                                            (int)resource->length);
    if (!stream) return NULL;
    SDL_Surface *image = resource->format == PNG_TAG ?
        IMG_LoadPNG_RW(stream) : IMG_LoadJPG_RW(stream);
    SDL_RWclose(stream);
    return image;
}

int astraz_picture_size(const AstraZResource *resource, int *height,
                        int *width)
{
    if (!resource || !height || !width || !resource->bytes) return 0;
    *height = *width = 0;
    if (resource->format == RECT_TAG) {
        if (resource->length != 8u) return 0;
        uint32_t w = be32(resource->bytes), h = be32(resource->bytes + 4u);
        if (w > INT_MAX || h > INT_MAX) return 0;
        *height = (int)h;
        *width = (int)w;
        return 1;
    }
    SDL_Surface *decoded = decode(resource);
    if (!decoded) return 0;
    *height = decoded->h;
    *width = decoded->w;
    SDL_FreeSurface(decoded);
    return *height > 0 && *width > 0;
}

AstraResult astraz_picture_upload(AstraDisplay *display,
    const AstraZResource *resource, AstraSurface *surface,
    int *height, int *width)
{
    if (!display || !resource || !surface || !height || !width ||
        surface->_private_handle != ASTRA_INVALID_HANDLE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    SDL_Surface *decoded = decode(resource);
    if (!decoded) return ASTRA_ERROR_INVALID_ARGUMENT;
    if (decoded->w <= 0 || decoded->h <= 0 ||
        decoded->w > UINT16_MAX || decoded->h > UINT16_MAX) {
        SDL_FreeSurface(decoded);
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(decoded,
        SDL_PIXELFORMAT_ARGB8888, 0u);
    SDL_FreeSurface(decoded);
    if (!rgba) return ASTRA_ERROR_OUT_OF_MEMORY;
    AstraSurfaceCreateInfo info = ASTRA_SURFACE_CREATE_INFO_INIT;
    info.flags = ASTRA_SURFACE_DRAW_SOURCE | ASTRA_SURFACE_CPU_WRITE;
    info.width = (uint16_t)rgba->w;
    info.height = (uint16_t)rgba->h;
    info.format = ASTRA_PIXEL_FORMAT_ARGB8888;
    AstraResult result = astra_surface_create(display, &info, surface);
    if (result == ASTRA_OK)
        result = astra_surface_write(display, surface,
            &(AstraRectI32){0, 0, (uint32_t)rgba->w, (uint32_t)rgba->h},
            rgba->pixels, (uint32_t)rgba->pitch);
    if (result != ASTRA_OK &&
        surface->_private_handle != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_surface_close(surface);
        (void)closed;
    }
    if (result == ASTRA_OK) {
        *height = rgba->h;
        *width = rgba->w;
    }
    SDL_FreeSurface(rgba);
    return result;
}
