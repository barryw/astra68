#include <astra/draw_list.h>
#include <astra/display.h>
#include <astra/render_batch.h>
#include <astra/render_builder.h>
#include <astra/surface.h>
#include <astra/theme.h>
#include <astra/ui_font.h>

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

static int32_t high_s16(uint32_t value)
{
    return (int16_t)(value >> 16);
}

static int32_t low_s16(uint32_t value)
{
    return (int16_t)value;
}

static uint32_t finish_batch(AstraRenderBuilder *builder,
                             const uint8_t *storage)
{
    uint32_t bytes = astra_render_builder_finish(builder);

    assert(bytes >= ASTRA_RENDER_BATCH_MIN_BYTES);
    assert(bytes <= ASTRA_RENDER_BUILDER_BYTES);
    assert(be32(storage + 8u) == bytes);
    return bytes;
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

static void test_font_advance_and_baseline(void)
{
    static uint8_t batch[ASTRA_RENDER_BUILDER_BYTES];
    AstraRenderBuilder builder;
    const AstraUiStrike *strike;
    const AstraUiGlyph *first;
    const AstraUiGlyph *second;
    const uint8_t *records = batch + ASTRA_RENDER_BATCH_GLYPH_OFFSET -
                             ASTRA_RENDER_BATCH_ARENA_OFFSET;
    uint32_t destination;
    uint32_t first_position;
    uint32_t second_position;

    {
        const AstraUiGlyph fractional = {
            .bearing_x = 32,
            .bearing_y = 32,
            .advance_x = 96,
        };

        assert(astra_ui_glyph_x(0, &fractional) == 0);
        assert(astra_ui_glyph_x(96, &fractional) == 2);
        assert(astra_ui_glyph_x(-96, &fractional) == -1);
        assert(astra_ui_glyph_y(0, &fractional) == -1);
    }

    for (uint16_t height = 11u; height <= 13u; height += 2u) {
        strike = astra_ui_font_strike(height);
        first = astra_ui_font_glyph(strike, 'I');
        second = astra_ui_font_glyph(strike, 'W');
        assert(first->advance_x < second->advance_x);
        assert(astra_render_builder_init(&builder, batch, sizeof(batch), 1u));
        destination = astra_render_builder_surface(&builder, 160u, 80u);
        assert(astra_render_builder_text(
            &builder, destination, 11, 7, "IW", 2u, height, 0xffffu));
        first_position = be32(records + 8u);
        second_position = be32(
            records + ASTRA_RENDER_GLYPH_DESCRIPTOR_BYTES + 8u);
        assert(high_s16(first_position) == astra_ui_glyph_x(11 * 64, first));
        assert(high_s16(second_position) == astra_ui_glyph_x(
            11 * 64 + astra_ui_glyph_advance(first, 0u), second));
        assert(low_s16(first_position) + first->bearing_y / 64 ==
               7 + strike->ascent);
        assert(low_s16(second_position) + second->bearing_y / 64 ==
               7 + strike->ascent);
    }

    strike = astra_mono_font_strike(16u);
    first = astra_mono_font_glyph(strike, 'I');
    second = astra_mono_font_glyph(strike, 'W');
    assert(astra_render_builder_init(&builder, batch, sizeof(batch), 2u));
    destination = astra_render_builder_surface(&builder, 160u, 80u);
    assert(astra_render_builder_mono_text(
        &builder, destination, 5, 3, "IW", 2u, 16u, 14u, 0xffffu));
    first_position = be32(records + 8u);
    second_position = be32(
        records + ASTRA_RENDER_GLYPH_DESCRIPTOR_BYTES + 8u);
    assert(high_s16(second_position) - high_s16(first_position) == 14);
    assert(low_s16(first_position) + first->bearing_y / 64 ==
           3 + strike->ascent);
    assert(low_s16(second_position) + second->bearing_y / 64 ==
           3 + strike->ascent);
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
    uint32_t clipped_blits = 0u;

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
        &builder, 0x01000000u, ASTRA_RENDER_BATCH_SCANOUT_STRIDE,
        320u, 200u);
    assert(frame != 0u && offscreen != 0u && cached != 0u);
    assert(be32(batch_storage + offscreen - ASTRA_RENDER_BATCH_ARENA_OFFSET +
                8u) >= ASTRA_RENDER_BATCH_ARENA_OFFSET +
                           ASTRA_RENDER_BUILDER_BYTES);
    assert(astra_render_builder_replay(&builder, offscreen, header));
    assert(astra_render_builder_rounded(&builder, offscreen, 60, 10,
                                        14u, 14u, 7u, 0x2222u));
    assert(astra_render_builder_blit_clipped(
        &builder, frame, offscreen, 12, 20, 160u, 80u, 8u, 1,
        17, 24, 120, 76));
    assert(finish_batch(&builder, batch_storage) != 0u);
    assert(be32(batch_storage) == ASTRA_RENDER_BATCH_MAGIC);
    for (uint32_t index = 0u; index < be32(batch_storage + 12u); ++index) {
        const uint8_t *command = batch_storage +
            ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
            ASTRA_RENDER_BATCH_ARENA_OFFSET +
            index * ASTRA_RENDER_COMMAND_BYTES;

        if ((be32(command + 4u) >> 16) == ASTRA_RENDER_OP_BLIT &&
            be32(command + 32u) == frame) {
            assert((be32(command + 4u) &
                    ASTRA_RENDER_FLAG_BLIT_MASK1) == 0u);
            assert(be32(command + 24u) == ((uint32_t)17u << 16 | 24u));
            assert(be32(command + 28u) == ((uint32_t)120u << 16 | 76u));
            ++clipped_blits;
        }
    }
    assert(clipped_blits != 0u);
    assert(be32(batch_storage + 28u) ==
           ASTRA_RENDER_BATCH_SCANOUT1_OFFSET);
    assert(be32(batch_storage +
                ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                ASTRA_RENDER_BATCH_ARENA_OFFSET + 4u) >> 16 ==
           1u);

    assert(astra_render_builder_init(&builder, batch_storage,
                                     sizeof(batch_storage), 8u));
    frame = astra_render_builder_frame(&builder);
    cached = astra_render_builder_surface_at(
        &builder, 0x01000000u, ASTRA_RENDER_BATCH_SCANOUT_STRIDE,
        1280u, 720u);
    {
        uint32_t data_cursor = builder.data_cursor;

        assert(astra_render_builder_blit(&builder, frame, cached,
                                         0, 0, 944u, 578u, 12u, 1));
        assert(astra_render_builder_blit(&builder, frame, cached,
                                         2, 30, 940u, 548u, 10u, 0));
        assert(builder.data_cursor == data_cursor);
    }
    assert(finish_batch(&builder, batch_storage) != 0u);

    assert(astra_render_builder_init(&builder, batch_storage,
                                     sizeof(batch_storage), 9u));
    assert(astra_render_builder_surface_at(
               &builder, 0x01000000u, 4095u, 64u, 32u) == 0u);
    assert(builder.failed == ASTRA_RENDER_BUILDER_FAILURE_SURFACE);
}

static void test_scanout_source_is_explicit(void)
{
    uint8_t batch_storage[ASTRA_RENDER_BUILDER_BYTES];
    AstraRenderBuilder builder;
    uint32_t frame;
    uint32_t scanout;

    assert(astra_render_builder_init(&builder, batch_storage,
                                     sizeof(batch_storage), 1u));
    frame = astra_render_builder_frame(&builder);
    scanout = astra_render_builder_scanout(
        &builder, ASTRA_RENDER_BATCH_SCANOUT0_OFFSET);
    assert(frame != 0u && scanout != 0u && scanout != frame);
    assert(astra_render_builder_blit_region(
        &builder, frame, scanout, 0, 0, 0, 0,
        ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT));
    assert(finish_batch(&builder, batch_storage) != 0u);

    assert(astra_render_builder_init(&builder, batch_storage,
                                     sizeof(batch_storage), 1u));
    assert(astra_render_builder_scanout(&builder, UINT32_C(0x00600000)) ==
           0u);
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

static void test_draw_list_copy_is_a_hardware_self_blit(void)
{
    uint8_t draw_storage[ASTRA_DRAW_LIST_AREA_BYTES];
    uint8_t batch_storage[ASTRA_RENDER_BUILDER_BYTES];
    AstraSurfaceView surface;
    AstraRenderBuilder builder;
    const AstraDrawListHeader *header;
    const AstraDrawListCommand *command;
    uint32_t destination;
    const uint8_t *render;

    assert(astra_draw_list_view_init(&surface, draw_storage,
                                     sizeof(draw_storage), 80u, 40u));
    assert(!astra_draw_list_copy(&surface, 0u, 1u, 0u, 0u, 81u, 20u));
    assert(astra_draw_list_copy(&surface, 0u, 10u, 0u, 0u, 80u, 30u));
    header = (const AstraDrawListHeader *)(const void *)draw_storage;
    command = (const AstraDrawListCommand *)(const void *)(header + 1);
    assert(header->command_count == 1u &&
           command->operation == ASTRA_DRAW_LIST_COPY &&
           command->foreground == 0u && command->background == 10u &&
           command->x == 0 && command->y == 0 &&
           command->width == 80u && command->height == 30u);

    assert(astra_render_builder_init(&builder, batch_storage,
                                     sizeof(batch_storage), 11u));
    destination = astra_render_builder_surface(&builder, 80u, 40u);
    assert(destination != 0u);
    assert(astra_render_builder_replay(&builder, destination, header));
    assert(finish_batch(&builder, batch_storage) != 0u);
    render = batch_storage + ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
             ASTRA_RENDER_BATCH_ARENA_OFFSET;
    assert(be32(render + 4u) >> 16 == ASTRA_RENDER_OP_BLIT);
    assert(be32(render + 32u) == destination);
    assert(be32(render + 36u) == destination);
}

static void test_text_box_scroll_uses_overlap_safe_copy(void)
{
    uint8_t storage[ASTRA_DRAW_LIST_AREA_BYTES];
    AstraSurfaceView surface;
    AstraTextBox text_box;
    const AstraDrawListHeader *header;
    const AstraDrawListCommand *command;

    assert(astra_draw_list_view_init(&surface, storage, sizeof(storage),
                                     100u, 80u));
    text_box = (AstraTextBox){&surface, 10u, 12u, 70u, 50u};
    assert(astra_text_box_scroll(&text_box, 8));
    header = (const AstraDrawListHeader *)(const void *)storage;
    command = (const AstraDrawListCommand *)(const void *)(header + 1);
    assert(header->command_count == 1u &&
           command->operation == ASTRA_DRAW_LIST_COPY &&
           command->foreground == 10u && command->background == 20u &&
           command->x == 10 && command->y == 12 &&
           command->width == 70u && command->height == 42u);

    assert(astra_draw_list_view_init(&surface, storage, sizeof(storage),
                                     100u, 80u));
    assert(astra_text_box_scroll(&text_box, -8));
    header = (const AstraDrawListHeader *)(const void *)storage;
    command = (const AstraDrawListCommand *)(const void *)(header + 1);
    assert(header->command_count == 1u && command->foreground == 10u &&
           command->background == 12u && command->x == 10 &&
           command->y == 20 && command->width == 70u &&
           command->height == 42u);
}

static void test_rounded_fill_has_no_overlap(void)
{
    /* A rounded fill must not cover any pixel twice: its bands used to
       overlap, and one repainted band is milliseconds on real hardware. */
    static uint8_t batch_storage[ASTRA_RENDER_BUILDER_BYTES];
    AstraRenderBuilder solid;
    uint32_t surface;
    uint32_t filled = 0u;

    assert(astra_render_builder_init(&solid, batch_storage,
                                     sizeof(batch_storage), 9u));
    surface = astra_render_builder_surface(&solid, 100u, 60u);
    assert(surface != 0u);
    assert(astra_render_builder_rounded(&solid, surface, 0, 0,
                                        100u, 60u, 12u, 0x1234u));
    assert(finish_batch(&solid, batch_storage) != 0u);
    for (uint32_t index = 0u; index < be32(batch_storage + 12u); ++index) {
        const uint8_t *item = batch_storage +
            ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
            ASTRA_RENDER_BATCH_ARENA_OFFSET +
            index * ASTRA_RENDER_COMMAND_BYTES;
        uint32_t extent = be32(item + 56u);

        if ((be32(item + 4u) >> 16) == ASTRA_RENDER_OP_FILL)
            filled += (extent >> 16) * (extent & 0xffffu);
    }
    assert(filled == 100u * 60u - 4u * 12u * 12u);
}

int main(void)
{
    test_clipped_drawing_and_blit();
    test_color_and_glyph();
    test_text();
    test_proportional_utf8_text();
    test_font_advance_and_baseline();
    test_hardware_draw_list_batch();
    test_scanout_source_is_explicit();
    test_mono_draw_list();
    test_draw_list_copy_is_a_hardware_self_blit();
    test_text_box_scroll_uses_overlap_safe_copy();
    test_rounded_fill_has_no_overlap();
    puts("surface drawing tests passed");
    return 0;
}
