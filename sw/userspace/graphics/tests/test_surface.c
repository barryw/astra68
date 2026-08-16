#include <astra/draw_list.h>
#include <astra/render_batch.h>
#include <astra/render_builder.h>
#include <astra/surface.h>
#include <astra/theme.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <astra_render_protocol.h>
#pragma GCC diagnostic pop

static uint32_t be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

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
    astra_surface_blit_round(&destination, 2, 1, &rounded, 2u);
    assert(destination_pixels[1u * 8u + 2u] == 0u);
    assert(destination_pixels[1u * 8u + 3u] == 7u);
    assert(destination_pixels[4u * 8u + 2u] == 0u);
    assert(destination_pixels[4u * 8u + 3u] == 7u);

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

static void test_text(void)
{
    uint16_t pixels[20u * 20u] = {0};
    AstraSurfaceView surface;

    assert(astra_surface_view_init(&surface, pixels, sizeof(pixels), 20u, 20u,
                                   20u * sizeof(uint16_t)));
    astra_surface_text8x8(&surface, 1, 1, "A", 1u, 2u, 0x55aau);
    assert(pixels[1u * 20u + 7u] == 0x55aau);
    assert(pixels[3u * 20u + 3u] == 0x55aau);
    assert(pixels[9u * 20u + 15u] == 0u);
}

static void test_proportional_utf8_text(void)
{
    static const char a_ogonek[] = "A\xc4\x84" "B";
    static const char replacement[] = "\xef\xbf\xbd";
    uint16_t pixels[80u * 20u] = {0};
    AstraSurfaceView surface;
    uint32_t narrow = astra_surface_ui_text_width(
        "III", 3u, ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT);
    uint32_t wide = astra_surface_ui_text_width(
        "WWW", 3u, ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT);
    uint32_t first_two = astra_surface_ui_text_width(
        a_ogonek, 3u, ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT);

    assert(narrow < wide);
    assert(astra_surface_ui_text_width("\xff", 1u,
                                      ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT) ==
           astra_surface_ui_text_width(replacement, 3u,
                                      ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT));
    assert(astra_surface_ui_text_fit(a_ogonek, 4u,
                                     ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT,
                                     first_two) == 3u);
    assert(astra_surface_view_init(&surface, pixels, sizeof(pixels),
                                   80u, 20u, 80u * sizeof(uint16_t)));
    astra_surface_ui_text(&surface, 1, 1, a_ogonek, 4u,
                          ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT, 0x77aau);
    for (uint32_t index = 0u; index < 80u * 20u; ++index)
        if (pixels[index] == 0x77aau)
            return;
    assert(!"UTF-8 UI text drew no pixels");
}

static void test_hardware_draw_list_batch(void)
{
    static uint8_t draw_storage[ASTRA_DRAW_LIST_AREA_BYTES];
    static uint8_t batch_storage[ASTRA_RENDER_BUILDER_BYTES];
    AstraSurfaceView surface;
    AstraRenderBuilder builder;
    const AstraDrawListHeader *header;
    uint32_t frame;
    uint32_t offscreen;
    uint32_t cached;
    uint32_t masked_commands = 0u;
    uint32_t masked_pixels = 0u;

    assert(astra_draw_list_view_init(&surface, draw_storage,
                                     sizeof(draw_storage), 160u, 80u));
    astra_surface_clear(&surface, 0x1234u);
    astra_surface_fill_round(&surface, 4, 5, 40u, 30u, 6u, 0x5678u);
    astra_surface_ui_text(&surface, 8, 10, "AÄ", 3u,
                          ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT, 0xffffu);
    header = (const AstraDrawListHeader *)(const void *)draw_storage;
    assert(header->command_count == 3u && header->payload_bytes == 3u);

    assert(astra_render_builder_init(&builder, batch_storage,
                                     sizeof(batch_storage), 7u));
    frame = astra_render_builder_frame(&builder);
    offscreen = astra_render_builder_surface(&builder, 160u, 80u);
    cached = astra_render_builder_surface_at(
        &builder, 0x01000000u, 320u, 200u);
    assert(frame != 0u && offscreen != 0u && cached != 0u);
    assert(astra_render_builder_replay(&builder, offscreen, header));
    assert(astra_render_builder_rounded(&builder, offscreen, 60, 10,
                                        14u, 14u, 7u, 0x2222u));
    assert(astra_render_builder_blit_clipped(
        &builder, frame, offscreen, 12, 20, 160u, 80u, 8u, 1,
        17, 24, 120, 76));
    assert(astra_render_builder_finish(&builder) ==
           ASTRA_RENDER_BUILDER_BYTES);
    assert(be32(batch_storage) == ASTRA_RENDER_BATCH_MAGIC);
    for (uint32_t index = 0u; index < be32(batch_storage + 12u); ++index) {
        const uint8_t *command = batch_storage +
            ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
            ASTRA_RENDER_BATCH_ARENA_OFFSET +
            index * ASTRA_RENDER_COMMAND_BYTES;

        if ((be32(command + 4u) >> 16) == ASTRA_RENDER_OP_BLIT &&
            (be32(command + 4u) & ASTRA_RENDER_FLAG_BLIT_MASK1) != 0u) {
            uint32_t extent = be32(command + 52u);

            ++masked_commands;
            masked_pixels += (extent >> 16) * (extent & 0xffffu);
        }
    }
    assert(masked_commands == 4u);
    assert(masked_pixels <= 4u * 8u * 8u);
    assert(be32(batch_storage + 28u) ==
           ASTRA_RENDER_BATCH_SCANOUT1_OFFSET);
    assert(be32(batch_storage +
                ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                ASTRA_RENDER_BATCH_ARENA_OFFSET + 4u) >> 16 ==
           1u);
    assert(be32(batch_storage +
                ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                ASTRA_RENDER_BATCH_ARENA_OFFSET + 12u * 64u + 24u) ==
           ((uint32_t)17u << 16 | 24u));
    assert(be32(batch_storage +
                ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                ASTRA_RENDER_BATCH_ARENA_OFFSET + 12u * 64u + 28u) ==
           ((uint32_t)120u << 16 | 76u));
}

static void test_mono_draw_list(void)
{
    uint8_t storage[ASTRA_DRAW_LIST_AREA_BYTES];
    AstraSurfaceView surface;
    const AstraDrawListHeader *header;
    const AstraDrawListCommand *command;

    assert(astra_draw_list_view_init(&surface, storage, sizeof(storage),
                                     80u, 20u));
    assert(astra_surface_mono_cell_width(
               ASTRA_THEME_SYSTEM_MONO_FONT_HEIGHT) == 8u);
    astra_draw_list_mono_text(
        &surface, 2, 3, "IW", 2u, ASTRA_THEME_SYSTEM_MONO_FONT_HEIGHT,
        ASTRA_THEME_SYSTEM_MONO_CELL_WIDTH, 0xffffu);
    header = (const AstraDrawListHeader *)(const void *)storage;
    command = (const AstraDrawListCommand *)(const void *)(
        storage + sizeof(*header));
    assert(header->command_count == 1u && header->payload_bytes == 2u &&
           command->operation == ASTRA_DRAW_LIST_MONO_TEXT &&
           command->font_height == ASTRA_THEME_SYSTEM_MONO_FONT_HEIGHT &&
           command->width == ASTRA_THEME_SYSTEM_MONO_CELL_WIDTH);
}

int main(void)
{
    test_clipped_drawing_and_blit();
    test_color_and_glyph();
    test_text();
    test_proportional_utf8_text();
    test_hardware_draw_list_batch();
    test_mono_draw_list();
    puts("surface drawing tests passed");
    return 0;
}
