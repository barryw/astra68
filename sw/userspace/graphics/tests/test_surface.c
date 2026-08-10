#include <astra/surface.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_clipped_drawing_and_blit(void)
{
    uint16_t destination_pixels[8u * 6u] = {0};
    uint16_t source_pixels[3u * 2u] = {
        1u, 2u, 3u,
        4u, 5u, 6u
    };
    uint16_t rounded_pixels[4u * 4u] = {
        7u, 7u, 7u, 7u, 7u, 7u, 7u, 7u,
        7u, 7u, 7u, 7u, 7u, 7u, 7u, 7u
    };
    AstraSurfaceView destination;
    AstraSurfaceView source;
    AstraSurfaceView rounded;

    assert(astra_surface_view_init(&destination, destination_pixels,
                                   sizeof(destination_pixels), 8u, 6u,
                                   8u * sizeof(uint16_t)));
    assert(astra_surface_view_init(&source, source_pixels,
                                   sizeof(source_pixels), 3u, 2u,
                                   3u * sizeof(uint16_t)));
    assert(astra_surface_view_init(&rounded, rounded_pixels,
                                   sizeof(rounded_pixels), 4u, 4u,
                                   4u * sizeof(uint16_t)));
    astra_surface_clear(&destination, 0x1234u);
    astra_surface_fill(&destination, -2, 1, 4u, 3u, 0xabcdu);
    assert(destination_pixels[0u] == 0x1234u);
    assert(destination_pixels[1u * 8u + 0u] == 0xabcdu);
    assert(destination_pixels[3u * 8u + 1u] == 0xabcdu);
    assert(destination_pixels[3u * 8u + 2u] == 0x1234u);

    astra_surface_blit(&destination, 6, 4, &source);
    assert(destination_pixels[4u * 8u + 6u] == 1u);
    assert(destination_pixels[4u * 8u + 7u] == 2u);
    assert(destination_pixels[5u * 8u + 6u] == 4u);
    assert(destination_pixels[5u * 8u + 7u] == 5u);

    astra_surface_clear(&destination, 0u);
    astra_surface_fill_round(&destination, 0, 0, 8u, 6u, 2u, 0x55aau);
    assert(destination_pixels[0u] == 0u);
    assert(destination_pixels[1u] == 0x55aau);
    assert(destination_pixels[2u * 8u] == 0x55aau);

    astra_surface_clear(&destination, 0u);
    astra_surface_blit_round_bottom(&destination, 2, 1, &rounded, 2u);
    assert(destination_pixels[1u * 8u + 2u] == 7u);
    assert(destination_pixels[2u * 8u + 2u] == 7u);
    assert(destination_pixels[4u * 8u + 2u] == 0u);
    assert(destination_pixels[4u * 8u + 3u] == 7u);
}

static void test_color_and_glyph(void)
{
    static const uint8_t glyph[8] = {
        0x18u, 0x24u, 0x42u, 0x7eu, 0x42u, 0x42u, 0x42u, 0x00u
    };
    uint16_t pixels[10u * 10u] = {0};
    AstraSurfaceView surface;
    uint16_t white = astra_surface_rgb565(255u, 255u, 255u);

    assert(white == 0xffffu);
    assert(astra_surface_rgb565(255u, 0u, 0u) == 0xf800u);
    assert(astra_surface_view_init(&surface, pixels, sizeof(pixels), 10u, 10u,
                                   10u * sizeof(uint16_t)));
    astra_surface_glyph8x8(&surface, 1, 1, glyph, white);
    assert(pixels[1u * 10u + 4u] == white);
    assert(pixels[4u * 10u + 1u] == 0u);
    assert(pixels[4u * 10u + 2u] == white);
    assert(pixels[4u * 10u + 7u] == white);
}

int main(void)
{
    test_clipped_drawing_and_blit();
    test_color_and_glyph();
    puts("surface drawing tests passed");
    return 0;
}
