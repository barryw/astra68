#include <astra/surface.h>

#include <stddef.h>

static uint16_t *row(AstraSurfaceView *surface, uint32_t y)
{
    return (uint16_t *)((uint8_t *)surface->pixels + y * surface->pitch);
}

static const uint16_t *const_row(const AstraSurfaceView *surface, uint32_t y)
{
    return (const uint16_t *)((const uint8_t *)surface->pixels +
                             y * surface->pitch);
}

int astra_surface_view_init(AstraSurfaceView *surface, void *pixels,
                            uint32_t byte_size, uint16_t width,
                            uint16_t height, uint32_t pitch)
{
    uint32_t minimum_pitch;

    if (surface == NULL || pixels == NULL || width == 0u || height == 0u)
        return 0;
    minimum_pitch = (uint32_t)width * sizeof(uint16_t);
    if (pitch < minimum_pitch || (pitch & 1u) != 0u ||
        (uint64_t)pitch * height > byte_size)
        return 0;
    surface->pixels = pixels;
    surface->byte_size = byte_size;
    surface->pitch = pitch;
    surface->width = width;
    surface->height = height;
    return 1;
}

uint16_t astra_surface_rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint16_t)(((uint16_t)(red & 0xf8u) << 8u) |
                      ((uint16_t)(green & 0xfcu) << 3u) |
                      ((uint16_t)blue >> 3u));
}

void astra_surface_fill(AstraSurfaceView *surface, int32_t x, int32_t y,
                        uint32_t width, uint32_t height, uint16_t color)
{
    int64_t right;
    int64_t bottom;
    uint32_t first_x;
    uint32_t first_y;
    uint32_t last_x;
    uint32_t last_y;

    if (surface == NULL || surface->pixels == NULL || width == 0u ||
        height == 0u)
        return;
    right = (int64_t)x + width;
    bottom = (int64_t)y + height;
    if (right <= 0 || bottom <= 0 || x >= surface->width ||
        y >= surface->height)
        return;
    first_x = x < 0 ? 0u : (uint32_t)x;
    first_y = y < 0 ? 0u : (uint32_t)y;
    last_x = right > surface->width ? surface->width : (uint32_t)right;
    last_y = bottom > surface->height ? surface->height : (uint32_t)bottom;
    for (uint32_t at_y = first_y; at_y < last_y; ++at_y) {
        uint16_t *pixels = row(surface, at_y);

        for (uint32_t at_x = first_x; at_x < last_x; ++at_x)
            pixels[at_x] = color;
    }
}

void astra_surface_clear(AstraSurfaceView *surface, uint16_t color)
{
    if (surface != NULL)
        astra_surface_fill(surface, 0, 0, surface->width, surface->height,
                           color);
}

static uint32_t rounded_inset(uint32_t at_y, uint32_t height,
                              uint32_t radius)
{
    uint32_t corner_y;
    uint32_t diameter;
    uint32_t inset = 0u;

    if (radius == 0u || (at_y >= radius && at_y < height - radius))
        return 0u;
    corner_y = at_y < radius ? at_y : height - at_y - 1u;
    diameter = radius * 2u;
    while (inset < radius) {
        uint32_t dx = diameter - inset * 2u - 1u;
        uint32_t dy = diameter - corner_y * 2u - 1u;

        if (dx * dx + dy * dy <= diameter * diameter)
            break;
        ++inset;
    }
    return inset;
}

void astra_surface_fill_round(AstraSurfaceView *surface, int32_t x, int32_t y,
                              uint32_t width, uint32_t height,
                              uint16_t radius, uint16_t color)
{
    uint32_t bounded_radius = radius;

    if (width == 0u || height == 0u)
        return;
    if (bounded_radius > width / 2u)
        bounded_radius = width / 2u;
    if (bounded_radius > height / 2u)
        bounded_radius = height / 2u;
    for (uint32_t at_y = 0u; at_y < height; ++at_y) {
        uint32_t inset = rounded_inset(at_y, height, bounded_radius);

        astra_surface_fill(surface, x + (int32_t)inset, y + (int32_t)at_y,
                           width - inset * 2u, 1u, color);
    }
}

void astra_surface_blit(AstraSurfaceView *destination, int32_t x, int32_t y,
                        const AstraSurfaceView *source)
{
    uint32_t source_x = x < 0 ? (uint32_t)(-(int64_t)x) : 0u;
    uint32_t source_y = y < 0 ? (uint32_t)(-(int64_t)y) : 0u;
    uint32_t destination_x = x < 0 ? 0u : (uint32_t)x;
    uint32_t destination_y = y < 0 ? 0u : (uint32_t)y;
    uint32_t width;
    uint32_t height;

    if (destination == NULL || source == NULL || destination->pixels == NULL ||
        source->pixels == NULL || destination_x >= destination->width ||
        destination_y >= destination->height || source_x >= source->width ||
        source_y >= source->height)
        return;
    width = source->width - source_x;
    height = source->height - source_y;
    if (width > destination->width - destination_x)
        width = destination->width - destination_x;
    if (height > destination->height - destination_y)
        height = destination->height - destination_y;
    for (uint32_t at_y = 0u; at_y < height; ++at_y) {
        uint16_t *out = row(destination, destination_y + at_y) + destination_x;
        const uint16_t *in = const_row(source, source_y + at_y) + source_x;

        for (uint32_t at_x = 0u; at_x < width; ++at_x)
            out[at_x] = in[at_x];
    }
}

void astra_surface_blit_round_bottom(AstraSurfaceView *destination, int32_t x,
                                     int32_t y,
                                     const AstraSurfaceView *source,
                                     uint16_t radius)
{
    uint32_t bounded_radius = radius;

    if (destination == NULL || source == NULL || destination->pixels == NULL ||
        source->pixels == NULL)
        return;
    if (bounded_radius > source->width / 2u)
        bounded_radius = source->width / 2u;
    if (bounded_radius > source->height / 2u)
        bounded_radius = source->height / 2u;
    for (uint32_t source_y = 0u; source_y < source->height; ++source_y) {
        int64_t destination_y = (int64_t)y + source_y;
        uint32_t inset = source_y < source->height - bounded_radius ? 0u :
                         rounded_inset(source_y, source->height,
                                       bounded_radius);
        const uint16_t *in = const_row(source, source_y);
        uint16_t *out;

        if (destination_y < 0 || destination_y >= destination->height)
            continue;
        out = row(destination, (uint32_t)destination_y);
        for (uint32_t source_x = inset;
             source_x < (uint32_t)source->width - inset; ++source_x) {
            int64_t destination_x = (int64_t)x + source_x;

            if (destination_x >= 0 && destination_x < destination->width)
                out[destination_x] = in[source_x];
        }
    }
}

void astra_surface_glyph8x8(AstraSurfaceView *surface, int32_t x, int32_t y,
                            const uint8_t rows[8], uint16_t color)
{
    if (surface == NULL || rows == NULL)
        return;
    for (uint32_t glyph_y = 0u; glyph_y < 8u; ++glyph_y) {
        uint8_t bits = rows[glyph_y];

        for (uint32_t glyph_x = 0u; glyph_x < 8u; ++glyph_x) {
            if ((bits & (uint8_t)(0x80u >> glyph_x)) != 0u)
                astra_surface_fill(surface, x + (int32_t)glyph_x,
                                   y + (int32_t)glyph_y, 1u, 1u, color);
        }
    }
}
