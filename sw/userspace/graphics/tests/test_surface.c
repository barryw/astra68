#include <astra/draw_list.h>
#include <astra/display.h>
#include <astra/render_batch.h>
#include <astra/window_scene.h>
#include <astra/render_builder.h>
#include <astra/rounded.h>
#include <astra/surface.h>
#include <astra/texture_reference.h>
#include <astra/theme.h>
#include <astra/ui_font.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <astra_render_protocol.h>
#pragma GCC diagnostic pop

static uint32_t argb_from_rgb565(uint16_t color)
{
    uint32_t red = (color >> 11u) & 31u;
    uint32_t green = (color >> 5u) & 63u;
    uint32_t blue = color & 31u;

    return UINT32_C(0xff000000) |
           (((red << 3u) | (red >> 2u)) << 16u) |
           (((green << 2u) | (green >> 4u)) << 8u) |
           ((blue << 3u) | (blue >> 2u));
}

static uint32_t be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static const uint8_t *batch_command(const uint8_t *batch, uint32_t index)
{
    return batch + ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
           ASTRA_RENDER_BATCH_ARENA_OFFSET +
           index * ASTRA_RENDER_COMMAND_BYTES;
}

static const uint8_t *batch_record(const uint8_t *batch,
                                   uint32_t arena_offset)
{
    assert(arena_offset >= ASTRA_RENDER_BATCH_ARENA_OFFSET);
    return batch + arena_offset - ASTRA_RENDER_BATCH_ARENA_OFFSET;
}

static int32_t high_s16(uint32_t value)
{
    return (int16_t)(value >> 16);
}

static int32_t low_s16(uint32_t value)
{
    return (int16_t)value;
}

static uint16_t blend_rgb565(uint16_t foreground, uint16_t background,
                             uint8_t coverage)
{
    uint32_t inverse = 255u - coverage;
    uint32_t red = ((((foreground >> 11u) & 31u) * coverage) +
                    (((background >> 11u) & 31u) * inverse) + 127u) / 255u;
    uint32_t green = ((((foreground >> 5u) & 63u) * coverage) +
                      (((background >> 5u) & 63u) * inverse) + 127u) / 255u;
    uint32_t blue = (((foreground & 31u) * coverage) +
                     ((background & 31u) * inverse) + 127u) / 255u;

    return (uint16_t)((red << 11u) | (green << 5u) | blue);
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

static void test_batch_header_reserves_presentation_words(void)
{
    static uint8_t storage[ASTRA_RENDER_BUILDER_BYTES];
    AstraRenderBuilder builder;
    uint32_t frame;

    assert(astra_render_builder_init(&builder, storage, sizeof(storage), 1u));
    frame = astra_render_builder_frame(&builder);
    assert(astra_render_builder_fill(&builder, frame, 0, 0, 1u, 1u, 0u));
    assert(finish_batch(&builder, storage) != 0u);
    assert(be32(storage + 4u) == ASTRA_RENDER_BATCH_VERSION_1_2);
    for (uint32_t offset = 32u; offset < ASTRA_RENDER_BATCH_HEADER_BYTES;
         offset += 4u)
        assert(be32(storage + offset) == 0u);
}

static void test_rgb565_upload_uses_hardware_blit(void)
{
    static uint8_t storage[ASTRA_RENDER_BUILDER_BYTES];
    static const uint8_t pixels[] = {
        0x12u, 0x34u, 0x56u, 0x78u, 0xaau, 0xbbu,
        0x9au, 0xbcu, 0xdeu, 0xf0u, 0xccu, 0xddu,
    };
    static const uint8_t expected[] = {
        0x12u, 0x34u, 0x56u, 0x78u,
        0x9au, 0xbcu, 0xdeu, 0xf0u,
    };
    AstraRenderBuilder builder;
    uint32_t uploaded;
    const uint8_t *descriptor;
    const uint8_t *command;

    assert(astra_render_builder_init(&builder, storage, sizeof(storage), 1u));
    uploaded = astra_render_builder_upload_rgb565(
        &builder, pixels, 6u, 2u, 2u);
    assert(uploaded != 0u);
    descriptor = batch_record(storage, uploaded);
    assert(be32(descriptor + 12u) == sizeof(expected));
    assert(be32(descriptor + 16u) == 4u);
    assert(be32(descriptor + 20u) == (2u << 16u | 2u));
    assert(memcmp(batch_record(storage, be32(descriptor + 8u)),
                  expected, sizeof(expected)) == 0);
    assert(astra_render_builder_blit_region(
        &builder, astra_render_builder_frame(&builder), uploaded,
        0, 0, 3, 4, 2u, 2u));
    command = batch_command(storage, 0u);
    assert((be32(command + 4u) >> 16u) == ASTRA_RENDER_OP_BLIT);
    assert(be32(command + 36u) == uploaded);
    assert(finish_batch(&builder, storage) != 0u);

    assert(astra_render_builder_init(&builder, storage, sizeof(storage), 2u));
    assert(astra_render_builder_upload_rgb565(
        &builder, pixels, 3u, 2u, 2u) == 0u);
    assert(builder.failed == ASTRA_RENDER_BUILDER_FAILURE_SURFACE);
}

/* A reserved upload copies nothing: its descriptor keeps the staging pitch
   and names the batch's last data, where the device places the rows. */
static void test_upload_reserve_ends_the_batch_data(void)
{
    static uint8_t storage[ASTRA_RENDER_BUILDER_BYTES];
    AstraRenderBuilder builder;
    const uint8_t *descriptor;
    uint32_t target;
    uint32_t source;
    uint32_t bytes;

    assert(astra_render_builder_init(&builder, storage, sizeof(storage), 1u));
    target = astra_render_builder_frame(&builder);
    source = astra_render_builder_upload_reserve(
        &builder, 64u, 8u, 4u, ASTRA_RENDER_FORMAT_ARGB8888, &bytes);
    assert(source != 0u && target != 0u);
    descriptor = batch_record(storage, source);
    assert(be32(descriptor + 16u) == 64u);
    assert(be32(descriptor + 12u) == 64u * 4u);
    assert(be32(descriptor + 20u) == (8u << 16u | 4u));
    assert(be32(descriptor + 8u) ==
           ASTRA_RENDER_BATCH_ARENA_OFFSET + bytes);
    assert(bytes % 64u == 0u && bytes >= ASTRA_RENDER_BATCH_MIN_BYTES);
    assert(source - ASTRA_RENDER_BATCH_ARENA_OFFSET < bytes);
    assert(astra_render_builder_blit_region(&builder, target, source, 0, 0,
                                            0, 0, 8u, 4u));
    assert(astra_render_builder_finish_render_only(&builder) ==
           bytes + 64u * 4u);

    assert(astra_render_builder_init(&builder, storage, sizeof(storage), 2u));
    assert(astra_render_builder_upload_reserve(
               &builder, 31u, 8u, 4u, ASTRA_RENDER_FORMAT_ARGB8888,
               &bytes) == 0u);
    assert(builder.failed == ASTRA_RENDER_BUILDER_FAILURE_SURFACE);
}

static void test_window_scene_batch(void)
{
    static uint8_t storage[ASTRA_RENDER_BUILDER_BYTES];
    AstraRenderBuilder builder;
    uint32_t surface;
    uint32_t bytes;
    uint32_t scene_offset;
    const uint8_t *scene;
    const uint8_t *layer;

    assert(astra_window_scene_compiled_capacity(1920u, 1080u, 1u) ==
           UINT32_C(112384));
    assert(astra_window_scene_compiled_capacity(
               1920u, 1080u, UINT32_MAX) == UINT32_C(66363904));

    assert(astra_render_builder_init(&builder, storage, sizeof(storage), 3u));
    surface = astra_render_builder_surface_at(
        &builder, 0x02000000u, 320u * 200u * 2u, 320u, 200u);
    assert(surface != 0u);
    assert(astra_render_builder_window_scene(
        &builder, 0x10000000u, 0x00020000u,
        ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT, 0x1234u));
    assert(astra_render_builder_window_scene_layer(
        &builder, surface, -7, 11, 12u, 1));
    bytes = finish_batch(&builder, storage);
    assert(bytes != 0u && be32(storage + 4u) ==
                            ASTRA_RENDER_BATCH_VERSION_1_3);
    assert(be32(storage + 12u) == 0u);
    scene_offset = be32(storage + 48u);
    assert(scene_offset != 0u && be32(storage + 52u) ==
                                    ASTRA_WINDOW_SCENE_HEADER_BYTES +
                                        ASTRA_WINDOW_SCENE_LAYER_BYTES);
    scene = batch_record(storage, scene_offset);
    layer = scene + ASTRA_WINDOW_SCENE_HEADER_BYTES;
    assert(be32(scene + 0u) == ASTRA_WINDOW_SCENE_MAGIC);
    assert(be32(scene + 4u) == ASTRA_WINDOW_SCENE_VERSION);
    assert(be32(scene + 8u) == be32(storage + 52u));
    assert(be32(scene + 12u) == 3u);
    assert(be32(scene + 16u) ==
           (ASTRA_DISPLAY_WIDTH << 16 | ASTRA_DISPLAY_HEIGHT));
    assert(be32(scene + 20u) == 1u);
    assert(be32(scene + 24u) == ASTRA_WINDOW_SCENE_HEADER_BYTES);
    assert(be32(scene + 28u) == 0x10000000u);
    assert(be32(scene + 32u) == 0x00020000u);
    assert(be32(scene + 36u) == 0x1234u);
    assert(be32(layer + 0u) == 0x02000000u);
    assert(be32(layer + 4u) == 320u * 200u * 2u);
    assert(be32(layer + 8u) == 640u);
    assert(be32(layer + 12u) == (320u << 16 | 200u));
    assert(be32(layer + 16u) ==
           ((uint32_t)(uint16_t)-7 << 16 | 11u));
    assert(be32(layer + 20u) ==
           (ASTRA_WINDOW_SCENE_LAYER_VISIBLE | 12u));

    assert(astra_render_builder_init(&builder, storage, sizeof(storage), 4u));
    surface = astra_render_builder_surface_at(
        &builder, 0x02000000u, 320u * 200u * 2u, 320u, 200u);
    assert(surface != 0u);
    assert(astra_render_builder_window_scene(
        &builder, 0x10000000u, 0x00010000u,
        ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT, 0u));
    assert(astra_render_builder_window_scene_layer(
        &builder, surface, 0, 0, 0u, 1));
    assert(astra_render_builder_finish(&builder) == 0u);
    assert(builder.failed == ASTRA_RENDER_BUILDER_FAILURE_SCENE);

    assert(astra_render_builder_init(&builder, storage, sizeof(storage), 5u));
    surface = astra_render_builder_surface_at(
        &builder, 0x02000000u, 320u * 200u * 2u, 320u, 200u);
    assert(surface != 0u);
    assert(astra_render_builder_window_scene(
        &builder, 0x10000000u, 0x00010000u, 320u, 200u, 0u));
    assert(astra_render_builder_window_scene_layer(
        &builder, surface, 0, 0, 0u, 1));
    assert(astra_render_builder_finish(&builder) != 0u);
    scene = batch_record(storage, be32(storage + 48u));
    assert(be32(scene + 16u) == (320u << 16 | 200u));

    assert(astra_render_builder_init(&builder, storage, sizeof(storage), 6u));
    assert(!astra_render_builder_window_scene(
        &builder, 0x10000000u, 0x00010000u, 0u, 200u, 0u));
    assert(builder.failed == ASTRA_RENDER_BUILDER_FAILURE_SCENE);
    assert(astra_render_builder_init(&builder, storage, sizeof(storage), 7u));
    assert(!astra_render_builder_window_scene(
        &builder, 0x10000000u, 0x00010000u,
        ASTRA_DISPLAY_WIDTH + 1u, 200u, 0u));
    assert(builder.failed == ASTRA_RENDER_BUILDER_FAILURE_SCENE);
}

static void test_builder_uses_the_capacity_it_is_given(void)
{
    static uint8_t storage[ASTRA_RENDER_BATCH_MIN_BYTES +
                           ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES];
    AstraRenderBuilder builder;
    uint32_t frame;

    assert(astra_render_builder_init(&builder, storage, sizeof(storage), 1u));
    frame = astra_render_builder_frame(&builder);
    assert(astra_render_builder_fill(&builder, frame, 0, 0, 1u, 1u, 0u));
    assert(finish_batch(&builder, storage) == sizeof(storage));
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

static void test_surface_clip_is_inherited(void)
{
    uint16_t pixels[8u * 6u] = {0};
    AstraSurfaceView surface;

    assert(astra_surface_view_init(&surface, pixels, sizeof(pixels), 8u, 6u,
                                   8u * sizeof(uint16_t)));
    assert(astra_surface_clip(&surface, 2, 1, 4u, 3u));
    assert(astra_surface_clip(&surface, 0, 2, 8u, 3u));
    astra_surface_fill(&surface, 0, 0, 8u, 6u, 0x55aau);
    for (uint32_t y = 0u; y < 6u; ++y)
        for (uint32_t x = 0u; x < 8u; ++x)
            assert(pixels[y * 8u + x] ==
                   ((x >= 2u && x < 6u && y >= 2u && y < 4u) ?
                        0x55aau : 0u));

    assert(astra_surface_clip(&surface, 3, 3, 0u, 0u));
    astra_surface_clear(&surface, 0xffffu);
    for (uint32_t index = 0u; index < 8u * 6u; ++index)
        assert(pixels[index] != 0xffffu);
}

static void test_line_software_and_hardware(void)
{
    uint16_t pixels[8u * 6u] = {0};
    uint8_t draw_storage[ASTRA_DRAW_LIST_AREA_BYTES];
    static uint8_t batch_storage[ASTRA_RENDER_BUILDER_BYTES];
    AstraSurfaceView surface;
    AstraRenderBuilder builder;
    const AstraDrawListHeader *header;
    const AstraDrawListCommand *command;
    const uint8_t *render;
    uint32_t destination;

    assert(astra_surface_view_init(&surface, pixels, sizeof(pixels),
                                   8u, 6u, 8u * sizeof(uint16_t)));
    assert(astra_surface_line(&surface, -10, 2, 10, 2, 0x55aau));
    for (uint32_t x = 0u; x < 8u; ++x)
        assert(pixels[2u * 8u + x] == 0x55aau);
    assert(astra_surface_clip(&surface, 2, 1, 3u, 4u));
    assert(astra_surface_line(&surface, 3, -20, 3, 20, 0x1234u));
    for (uint32_t y = 0u; y < 6u; ++y)
        assert(pixels[y * 8u + 3u] ==
               (y >= 1u && y < 5u ? 0x1234u :
                                    (y == 2u ? 0x55aau : 0u)));

    assert(astra_draw_list_view_init(&surface, draw_storage,
                                     sizeof(draw_storage), 20u, 10u));
    assert(astra_surface_clip(&surface, 2, 1, 16u, 8u));
    assert(astra_surface_line(&surface, 1, 2, 19, 7, 0xabcd));
    header = (const AstraDrawListHeader *)(const void *)draw_storage;
    command = (const AstraDrawListCommand *)(const void *)(header + 1);
    assert(header->command_count == 1u &&
           command->operation == ASTRA_DRAW_LIST_LINE &&
           command->x == 1 && command->y == 2 &&
           (int32_t)command->width == 19 &&
           (int32_t)command->height == 7 &&
           command->color == argb_from_rgb565(0xabcdu));

    assert(astra_render_builder_init(&builder, batch_storage,
                                     sizeof(batch_storage), 1u));
    destination = astra_render_builder_surface(&builder, 20u, 10u);
    assert(destination != 0u &&
           astra_render_builder_replay(&builder, destination, header));
    assert(finish_batch(&builder, batch_storage) != 0u);
    render = batch_command(batch_storage, 0u);
    assert(be32(render + 4u) >> 16 == ASTRA_RENDER_OP_LINE);
    assert(high_s16(be32(render + 44u)) == 1 &&
           low_s16(be32(render + 44u)) == 2 &&
           high_s16(be32(render + 48u)) == 19 &&
           low_s16(be32(render + 48u)) == 7 &&
           be32(render + 60u) == 0xabcdu);
}

static void test_draw_list_clip_reaches_hardware(void)
{
    uint8_t draw_storage[ASTRA_DRAW_LIST_AREA_BYTES];
    static uint8_t batch_storage[ASTRA_RENDER_BUILDER_BYTES];
    AstraSurfaceView surface;
    AstraRenderBuilder builder;
    const AstraDrawListHeader *header;
    AstraDrawListCommand *draw;
    const uint8_t *render;
    uint32_t destination;

    assert(astra_draw_list_view_init(&surface, draw_storage,
                                     sizeof(draw_storage), 20u, 10u));
    assert(astra_surface_clip(&surface, 4, 3, 8u, 5u));
    astra_surface_fill(&surface, 0, 0, 20u, 10u, 0x1234u);
    header = (const AstraDrawListHeader *)(const void *)draw_storage;
    draw = (AstraDrawListCommand *)(void *)(header + 1);
    assert(header->version == ASTRA_DRAW_LIST_VERSION_1_7);
    assert(draw->clip_left == 4u && draw->clip_top == 3u &&
           draw->clip_right == 12u && draw->clip_bottom == 8u);
    assert(!astra_draw_list_covers(header, 20u, 10u));

    assert(astra_render_builder_init(&builder, batch_storage,
                                     sizeof(batch_storage), 1u));
    destination = astra_render_builder_surface(&builder, 20u, 10u);
    assert(destination != 0u);
    assert(astra_render_builder_replay(&builder, destination, header));
    assert(finish_batch(&builder, batch_storage) != 0u);
    render = batch_storage + ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
             ASTRA_RENDER_BATCH_ARENA_OFFSET;
    assert(be32(render + 24u) == ((uint32_t)4u << 16 | 3u));
    assert(be32(render + 28u) == ((uint32_t)12u << 16 | 8u));

    draw->clip_right = 21u;
    assert(astra_render_builder_init(&builder, batch_storage,
                                     sizeof(batch_storage), 2u));
    destination = astra_render_builder_surface(&builder, 20u, 10u);
    assert(destination != 0u &&
           !astra_render_builder_replay(&builder, destination, header));
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
        if (pixels[index] != 0u)
            return;
    assert(!"UTF-8 UI text drew no pixels");
}

static void test_missing_mono_glyph_uses_replacement(void)
{
    const AstraUiStrike *strike = astra_mono_font_strike(
        ASTRA_THEME_SYSTEM_MONO_FONT_HEIGHT);
    const AstraUiGlyph *replacement;
    const uint8_t *bitmap;
    uint8_t ink = 0u;

    assert(strike != NULL);
    replacement = astra_mono_font_glyph(strike, 0xfffdu);
    assert(astra_mono_font_glyph(strike, 0x10fffdu) == replacement);
    bitmap = astra_mono_font_bitmap(replacement);
    for (uint32_t byte = 0u; byte < replacement->bitmap_length; ++byte)
        ink |= bitmap[byte];
    assert(ink != 0u);
}

static void test_font_advance_and_baseline(void)
{
    static uint8_t batch[ASTRA_RENDER_BUILDER_BYTES];
    AstraRenderBuilder builder;
    const AstraUiStrike *strike;
    const AstraUiGlyph *first;
    const AstraUiGlyph *second;
    const uint8_t *records;
    const uint8_t *source_descriptor;
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

    const uint16_t ui_heights[] = {11u, 13u, 16u};

    assert(astra_ui_font_strike(15u) == NULL);
    for (uint32_t index = 0u;
         index < sizeof(ui_heights) / sizeof(ui_heights[0]); ++index) {
        uint16_t height = ui_heights[index];
        strike = astra_ui_font_strike(height);
        assert(strike != NULL &&
               strike->bitmap_format == ASTRA_FONT_BITMAP_A8);
        first = astra_ui_font_glyph(strike, 'I');
        second = astra_ui_font_glyph(strike, 'W');
        assert(first->advance_x < second->advance_x);
        assert(astra_render_builder_init(&builder, batch, sizeof(batch), 1u));
        destination = astra_render_builder_surface(&builder, 160u, 80u);
        assert(astra_render_builder_text(
            &builder, destination, 11, 7, "IW", 2u, height, 0xffffu));
        records = batch_record(batch, be32(batch_command(batch, 0u) + 40u));
        source_descriptor = batch_record(
            batch, be32(batch_command(batch, 0u) + 36u));
        assert((be32(batch_command(batch, 0u) + 36u) &
                (ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES - 1u)) == 0u);
        assert((be32(batch_command(batch, 0u) + 40u) &
                (ASTRA_RENDER_GLYPH_DESCRIPTOR_BYTES - 1u)) == 0u);
        assert(source_descriptor[24u] == ASTRA_RENDER_FORMAT_A8);
        first_position = be32(records + 8u);
        second_position = be32(
            records + ASTRA_RENDER_GLYPH_DESCRIPTOR_BYTES + 8u);
        assert(high_s16(first_position) == astra_ui_glyph_x(11 * 64, first));
        assert(high_s16(second_position) == astra_ui_glyph_x(
            11 * 64 + astra_ui_glyph_advance(first, 0u), second));
        assert(low_s16(first_position) + first->bearing_y / 64 ==
               7 + strike->ascent / 64);
        assert(low_s16(second_position) + second->bearing_y / 64 ==
               7 + strike->ascent / 64);
    }

    strike = astra_mono_font_strike(16u);
    assert(strike != NULL && strike->bitmap_format == ASTRA_FONT_BITMAP_A8);
    first = astra_mono_font_glyph(strike, 'I');
    second = astra_mono_font_glyph(strike, 'W');
    assert(astra_render_builder_init(&builder, batch, sizeof(batch), 2u));
    destination = astra_render_builder_surface(&builder, 160u, 80u);
    assert(astra_render_builder_mono_text(
        &builder, destination, 5, 3, "IW", 2u, 16u, 14u, 0xffffu));
    records = batch_record(batch, be32(batch_command(batch, 0u) + 40u));
    first_position = be32(records + 8u);
    second_position = be32(
        records + ASTRA_RENDER_GLYPH_DESCRIPTOR_BYTES + 8u);
    assert(high_s16(second_position) - high_s16(first_position) == 14);
    assert(low_s16(first_position) + first->bearing_y / 64 ==
           3 + strike->ascent / 64);
    assert(low_s16(second_position) + second->bearing_y / 64 ==
           3 + strike->ascent / 64);
}

static void test_a8_software_oracle_blends_native_rgb565_channels(void)
{
    uint16_t pixels[64u * 32u];
    AstraSurfaceView surface;
    const AstraUiStrike *strike = astra_ui_font_strike(13u);
    const AstraUiGlyph *glyph = astra_ui_font_glyph(strike, 'A');
    const uint8_t *bitmap = astra_ui_font_bitmap(glyph);
    const uint16_t background = 0x1234u;
    const uint16_t foreground = 0xe71cu;
    uint32_t sample = UINT32_MAX;

    assert(strike != NULL && strike->bitmap_format == ASTRA_FONT_BITMAP_A8);
    for (uint32_t index = 0u; index < glyph->bitmap_length; ++index)
        if (bitmap[index] != 0u && bitmap[index] != 255u) {
            sample = index;
            break;
        }
    assert(sample != UINT32_MAX);
    for (uint32_t index = 0u; index < 64u * 32u; ++index)
        pixels[index] = background;
    assert(astra_surface_view_init(&surface, pixels, sizeof(pixels),
                                   64u, 32u, 64u * sizeof(uint16_t)));
    astra_surface_ui_text(&surface, 5, 3, "A", 1u, 13u, foreground);
    {
        uint32_t glyph_row = sample / glyph->pitch;
        uint32_t glyph_column = sample % glyph->pitch;
        int32_t x = astra_ui_glyph_x(5 * 64, glyph) +
                    (int32_t)glyph_column;
        int32_t y = astra_ui_glyph_y(3 * 64 + strike->ascent, glyph) +
                    (int32_t)glyph_row;

        assert(x >= 0 && x < 64 && y >= 0 && y < 32);
        assert(pixels[(uint32_t)y * 64u + (uint32_t)x] ==
               blend_rgb565(foreground, background, bitmap[sample]));
    }
}

static void test_builder_chunks_the_hardware_glyph_limit(void)
{
    static uint8_t batch[ASTRA_RENDER_BUILDER_BYTES];
    static char text[ASTRA_RENDER_MAX_GLYPH_DESCRIPTORS + 1u];
    AstraRenderBuilder builder;
    uint32_t destination;
    const uint8_t *first_command;
    const uint8_t *second_command;
    const uint8_t *source_descriptor;

    for (uint32_t index = 0u; index < sizeof(text); ++index)
        text[index] = 'A';
    assert(astra_render_builder_init(&builder, batch, sizeof(batch), 1u));
    destination = astra_render_builder_surface(&builder, 160u, 80u);
    assert(destination != 0u);
    assert(astra_render_builder_mono_text(
        &builder, destination, 0, 0, text, sizeof(text), 16u, 1u,
        0xffffu));
    assert(builder.glyph_count == sizeof(text));
    assert(builder.command_count == 2u);
    first_command = batch_command(batch, 0u);
    second_command = batch_command(batch, 1u);
    assert((be32(first_command + 36u) &
            (ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES - 1u)) == 0u);
    assert((be32(first_command + 40u) &
            (ASTRA_RENDER_GLYPH_DESCRIPTOR_BYTES - 1u)) == 0u);
    assert((be32(second_command + 36u) &
            (ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES - 1u)) == 0u);
    assert((be32(second_command + 40u) &
            (ASTRA_RENDER_GLYPH_DESCRIPTOR_BYTES - 1u)) == 0u);
    assert(be32(first_command + 44u) ==
           ASTRA_RENDER_MAX_GLYPH_DESCRIPTORS);
    assert(be32(second_command + 44u) == 1u);
    assert(high_s16(be32(batch_record(
               batch, be32(second_command + 40u)) + 8u)) ==
           (int32_t)ASTRA_RENDER_MAX_GLYPH_DESCRIPTORS);
    source_descriptor = batch_record(batch, be32(first_command + 36u));
    assert(be32(source_descriptor + 12u) <= 512u);
    source_descriptor = batch_record(batch, be32(second_command + 36u));
    assert(be32(source_descriptor + 12u) <= 512u);
    assert(finish_batch(&builder, batch) != 0u);
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
                           builder.data_cursor);
    assert(be32(batch_storage + offscreen - ASTRA_RENDER_BATCH_ARENA_OFFSET +
                8u) + 160u * 80u * 2u <=
           ASTRA_RENDER_BATCH_WORKSPACE_LIMIT);
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
    static uint8_t batch_storage[ASTRA_RENDER_BUILDER_BYTES];
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

static void test_command_failure_reason(void)
{
    static uint8_t batch_storage[ASTRA_RENDER_BUILDER_BYTES];
    AstraRenderBuilder builder;
    uint32_t frame;
    uint32_t source;

    assert(astra_render_builder_init(&builder, batch_storage,
                                     sizeof(batch_storage), 1u));
    frame = astra_render_builder_frame(&builder);
    source = astra_render_builder_surface(&builder, 16u, 16u);
    assert(astra_render_builder_blit_clipped(
        &builder, frame, source, 0, 0, 16u, 16u, 0u, 0,
        8, 8, 8, 16));
    assert(builder.failed == ASTRA_RENDER_BUILDER_FAILURE_NONE);
    assert(builder.command_count == 0u);

    assert(astra_render_builder_init(&builder, batch_storage,
                                     sizeof(batch_storage), 2u));
    frame = astra_render_builder_frame(&builder);
    for (uint32_t index = 0u; index < ASTRA_RENDER_RING_ENTRIES; ++index)
        assert(astra_render_builder_fill(&builder, frame, 0, 0, 1u, 1u,
                                         0u));
    assert(!astra_render_builder_fill(&builder, frame, 0, 0, 1u, 1u, 0u));
    assert(builder.failed == ASTRA_RENDER_BUILDER_FAILURE_COMMAND_CAPACITY);
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
           command->flags == 0u &&
           command->font_height == ASTRA_THEME_SYSTEM_MONO_FONT_HEIGHT &&
           command->width == ASTRA_THEME_SYSTEM_MONO_CELL_WIDTH);
}

static void test_styled_text_uses_one_glyph_source(void)
{
    static uint8_t draw_storage[ASTRA_DRAW_LIST_AREA_BYTES];
    static uint8_t batch_storage[ASTRA_RENDER_BUILDER_BYTES];
    AstraSurfaceView surface;
    AstraRenderBuilder builder;
    const AstraDrawListHeader *header;
    AstraDrawListCommand *command;
    uint32_t destination;
    uint32_t regular_source;
    uint32_t styled_source;
    const uint8_t *glyphs;

    assert(astra_draw_list_view_init(&surface, draw_storage,
                                     sizeof(draw_storage), 80u, 24u));
    assert(astra_draw_list_mono_text_styled(
        &surface, 2, 3, "A", 1u, ASTRA_THEME_SYSTEM_MONO_FONT_HEIGHT,
        ASTRA_THEME_SYSTEM_MONO_CELL_WIDTH, 0xffffu,
        ASTRA_TEXT_STYLE_BOLD | ASTRA_TEXT_STYLE_ITALIC |
        ASTRA_TEXT_STYLE_UNDERLINE | ASTRA_TEXT_STYLE_STRIKETHROUGH));
    header = (const AstraDrawListHeader *)(const void *)draw_storage;
    command = (AstraDrawListCommand *)(void *)(header + 1);
    assert(header->command_count == 1u && header->payload_bytes == 1u);
    assert(command->flags == (ASTRA_TEXT_STYLE_BOLD |
                              ASTRA_TEXT_STYLE_ITALIC |
                              ASTRA_TEXT_STYLE_UNDERLINE |
                              ASTRA_TEXT_STYLE_STRIKETHROUGH));
    assert(astra_render_builder_init(&builder, batch_storage,
                                     sizeof(batch_storage), 3u));
    destination = astra_render_builder_surface(&builder, 80u, 24u);
    assert(destination != 0u);
    assert(astra_render_builder_replay(&builder, destination, header));
    assert(builder.glyph_count == 1u);
    glyphs = batch_record(
        batch_storage, be32(batch_command(batch_storage, 0u) + 40u));
    styled_source = be32(glyphs);
    assert(be32(glyphs + 12u) >> 16 >
           astra_mono_font_glyph(astra_mono_font_strike(16u), 'A')->width);
    assert(builder.command_count == 3u);

    assert(astra_draw_list_view_init(&surface, draw_storage,
                                     sizeof(draw_storage), 80u, 24u));
    astra_draw_list_mono_text(&surface, 2, 3, "A", 1u,
                              ASTRA_THEME_SYSTEM_MONO_FONT_HEIGHT,
                              ASTRA_THEME_SYSTEM_MONO_CELL_WIDTH, 0xffffu);
    header = (const AstraDrawListHeader *)(const void *)draw_storage;
    assert(astra_render_builder_init(&builder, batch_storage,
                                     sizeof(batch_storage), 4u));
    destination = astra_render_builder_surface(&builder, 80u, 24u);
    assert(destination != 0u);
    assert(astra_render_builder_replay(&builder, destination, header));
    assert(builder.glyph_count == 1u && builder.command_count == 1u);
    glyphs = batch_record(
        batch_storage, be32(batch_command(batch_storage, 0u) + 40u));
    regular_source = be32(glyphs);
    assert(regular_source == styled_source);

    command = (AstraDrawListCommand *)(void *)(header + 1);
    command->flags = UINT32_C(1) << 31;
    assert(astra_render_builder_init(&builder, batch_storage,
                                     sizeof(batch_storage), 5u));
    destination = astra_render_builder_surface(&builder, 80u, 24u);
    assert(destination != 0u &&
           !astra_render_builder_replay(&builder, destination, header));
}

static void test_direct_styled_text(void)
{
    static uint8_t batch_storage[ASTRA_RENDER_BUILDER_BYTES];
    AstraRenderBuilder builder;
    uint32_t destination;

    assert(astra_render_builder_init(&builder, batch_storage,
                                     sizeof(batch_storage), 7u));
    destination = astra_render_builder_surface(&builder, 80u, 24u);
    assert(destination != 0u);
    assert(astra_render_builder_text_styled(
        &builder, destination, 2, 3, "A", 1u, 13u, 0xffffu,
        ASTRA_TEXT_STYLE_BOLD | ASTRA_TEXT_STYLE_ITALIC |
        ASTRA_TEXT_STYLE_UNDERLINE | ASTRA_TEXT_STYLE_STRIKETHROUGH));
    assert(builder.glyph_count == 1u && builder.command_count == 3u);
    assert(!astra_render_builder_text_styled(
        &builder, destination, 2, 3, "A", 1u, 13u, 0xffffu,
        ASTRA_TEXT_STYLE_HIDDEN));
    assert(builder.glyph_count == 1u && builder.command_count == 3u);
}

static void test_draw_list_copy_is_a_hardware_self_blit(void)
{
    uint8_t draw_storage[ASTRA_DRAW_LIST_AREA_BYTES];
    static uint8_t batch_storage[ASTRA_RENDER_BUILDER_BYTES];
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
           command->operation == ASTRA_DRAW_LIST_BLIT &&
           command->source == ASTRA_DRAW_LIST_SOURCE_DESTINATION &&
           command->source_x == 0 && command->source_y == 10 &&
           command->source_width == 80u && command->source_height == 30u &&
           command->color == ASTRA_DRAW_LIST_COLOR_IDENTITY &&
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
           command->operation == ASTRA_DRAW_LIST_BLIT &&
           command->source_x == 10 && command->source_y == 20 &&
           command->x == 10 && command->y == 12 &&
           command->width == 70u && command->height == 42u);

    assert(astra_draw_list_view_init(&surface, storage, sizeof(storage),
                                     100u, 80u));
    assert(astra_text_box_scroll(&text_box, -8));
    header = (const AstraDrawListHeader *)(const void *)storage;
    command = (const AstraDrawListCommand *)(const void *)(header + 1);
    assert(header->command_count == 1u && command->source_x == 10 &&
           command->source_y == 12 && command->x == 10 &&
           command->y == 20 && command->width == 70u &&
           command->height == 42u);
}

static void test_rounded_fill_has_no_overlap(void)
{
    static uint8_t batch_storage[ASTRA_RENDER_BUILDER_BYTES];
    uint8_t coverage[60u][100u] = {{0}};
    AstraRenderBuilder solid;
    uint32_t surface;

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
        uint32_t position = be32(item + 48u);
        uint32_t extent = be32(item + 56u);
        uint32_t x = position >> 16;
        uint32_t y = position & 0xffffu;
        uint32_t width = extent >> 16;
        uint32_t height = extent & 0xffffu;

        assert((be32(item + 4u) >> 16) == ASTRA_RENDER_OP_FILL);
        assert(x + width <= 100u && y + height <= 60u);
        for (uint32_t row = y; row < y + height; ++row)
            for (uint32_t column = x; column < x + width; ++column)
                assert(++coverage[row][column] == 1u);
    }
    for (uint32_t row = 0u; row < 60u; ++row) {
        uint32_t inset = astra_graphics_rounded_inset(row, 60u, 12u);

        for (uint32_t column = 0u; column < 100u; ++column)
            assert(coverage[row][column] ==
                   (uint8_t)(column >= inset && column < 100u - inset));
    }
}

/* ADLT v1.6 session lists: built by hand the way the NDK writes them. */
enum {
    SESSION_CAPACITY = 1500u,
    /* Room for one command of 4097 triangles. */
    SESSION_PAYLOAD_BYTES = 4097u * 3u * sizeof(AstraDrawListVertex),
};
static uint8_t session_list[ASTRA_DRAW_LIST_HEADER_BYTES +
                            SESSION_CAPACITY * ASTRA_DRAW_LIST_COMMAND_BYTES +
                            SESSION_PAYLOAD_BYTES];

#define BLEND_ALPHA ASTRA_DRAW_LIST_BLEND_FLAGS(ASTRA_DRAW_LIST_BLEND_BLEND)

static AstraDrawListHeader *session_begin(uint16_t width, uint16_t height)
{
    AstraDrawListHeader *header = (AstraDrawListHeader *)(void *)session_list;

    memset(session_list, 0, sizeof(session_list));
    *header = (AstraDrawListHeader){
        .magic = ASTRA_DRAW_LIST_MAGIC,
        .version = ASTRA_DRAW_LIST_VERSION_1_7,
        .total_bytes = sizeof(session_list),
        .width = width,
        .height = height,
        .command_capacity = SESSION_CAPACITY,
    };
    return header;
}

static AstraDrawListCommand *session_add(AstraDrawListHeader *header,
                                         uint32_t operation)
{
    AstraDrawListCommand *command =
        &((AstraDrawListCommand *)(void *)(header + 1))
            [header->command_count++];

    assert(header->command_count <= header->command_capacity);
    *command = (AstraDrawListCommand){
        .operation = operation,
        .clip_right = header->width,
        .clip_bottom = header->height,
    };
    return command;
}

typedef struct TestSurfaces {
    uint32_t sprite_offset;
    uint32_t index_offset;
    uint32_t calls;
} TestSurfaces;

static uint32_t resolve_test_surface(void *context,
                                     AstraRenderBuilder *builder,
                                     uint32_t surface_id)
{
    TestSurfaces *surfaces = context;

    ++surfaces->calls;
    if (surface_id == 7u)
        return astra_render_builder_surface_format_at(
            builder, surfaces->sprite_offset, 64u * 32u * 4u, 64u, 32u,
            64u * 4u, ASTRA_RENDER_FORMAT_ARGB8888,
            ASTRA_RENDER_SURFACE_READ);
    if (surface_id == 9u)
        return astra_render_builder_surface_format_at(
            builder, surfaces->index_offset, 16u * 16u, 16u, 16u, 16u,
            ASTRA_RENDER_FORMAT_INDEX8, ASTRA_RENDER_SURFACE_READ);
    return 0u;
}

static uint32_t replay_session(AstraRenderBuilder *builder,
                               uint8_t *storage, uint32_t generation,
                               const AstraRenderSourceResolver *resolver,
                               uint32_t *destination)
{
    uint32_t next = UINT32_MAX;

    assert(astra_render_builder_init(builder, storage,
                                     ASTRA_RENDER_BUILDER_BYTES, generation));
    *destination = astra_render_builder_surface_at(
        builder, ASTRA_RENDER_BATCH_WORKSPACE_LIMIT, 320u * 200u * 2u,
        320u, 200u);
    assert(*destination != 0u);
    return (uint32_t)astra_render_builder_replay_range(
        builder, *destination,
        (const AstraDrawListHeader *)(const void *)session_list,
        sizeof(session_list), resolver, 0u, &next) == 0u ? UINT32_MAX :
        next;
}

static void test_rgb565_color_round_trip(void)
{
    for (uint32_t value = 0u; value <= UINT16_MAX; ++value)
        assert(astra_render_builder_destination_color(
                   ASTRA_RENDER_FORMAT_RGB565,
                   argb_from_rgb565((uint16_t)value)) == value);
    assert(astra_render_builder_destination_color(
               ASTRA_RENDER_FORMAT_XRGB8888, 0x80123456u) == 0x00123456u);
    assert(astra_render_builder_destination_color(
               ASTRA_RENDER_FORMAT_INDEX8, 0xff0000a5u) == 0xa5u);
}

static void test_session_blit_lowers_to_hardware(void)
{
    static uint8_t storage[ASTRA_RENDER_BUILDER_BYTES];
    TestSurfaces surfaces = {
        ASTRA_RENDER_BATCH_WORKSPACE_LIMIT + 0x40000u,
        ASTRA_RENDER_BATCH_WORKSPACE_LIMIT + 0x80000u, 0u
    };
    AstraRenderSourceResolver resolver = { resolve_test_surface, &surfaces };
    AstraDrawListHeader *header = session_begin(320u, 200u);
    AstraDrawListCommand *blit = session_add(header, ASTRA_DRAW_LIST_BLIT);
    AstraRenderBuilder builder;
    const uint8_t *render;
    const uint8_t *source;
    uint32_t destination;

    /* A half-opaque, flipped, 2x enlargement of a 32x16 ARGB region. */
    blit->flags = BLEND_ALPHA | ASTRA_DRAW_LIST_FLIP_X |
                  ASTRA_DRAW_LIST_FLIP_Y;
    blit->x = -5;
    blit->y = 10;
    blit->width = 64u;
    blit->height = 32u;
    blit->color = 0x80ffffffu;
    blit->source = 7u;
    blit->source_x = 16;
    blit->source_y = 8;
    blit->source_width = 32u;
    blit->source_height = 16u;
    assert(replay_session(&builder, storage, 3u, &resolver, &destination) ==
           1u);
    assert(surfaces.calls == 1u);
    render = batch_command(storage, 0u);
    assert(be32(render + 4u) ==
           ((uint32_t)ASTRA_RENDER_OP_BLIT << 16 |
            ASTRA_RENDER_FLAG_BLIT_REFLECT_X |
            ASTRA_RENDER_FLAG_BLIT_REFLECT_Y |
            ASTRA_RENDER_FLAG_BLIT_ALPHA));
    assert(be32(render + 32u) == destination);
    source = batch_record(storage, be32(render + 36u));
    assert(be32(source + 8u) == surfaces.sprite_offset &&
           be32(source + 24u) >> 24 == ASTRA_RENDER_FORMAT_ARGB8888);
    assert(high_s16(be32(render + 44u)) == 16 &&
           low_s16(be32(render + 44u)) == 8);
    assert(high_s16(be32(render + 48u)) == -5 &&
           low_s16(be32(render + 48u)) == 10);
    assert(be32(render + 52u) == ((uint32_t)32u << 16 | 16u));
    assert(be32(render + 56u) == ((uint32_t)64u << 16 | 32u));
    assert(be32(render + 60u) == UINT32_C(0x80000000));

    /* ARGB without BLEND is a plain converting copy with no opacity. */
    blit->flags = 0u;
    assert(replay_session(&builder, storage, 4u, &resolver, &destination) ==
           1u);
    render = batch_command(storage, 0u);
    assert((be32(render + 4u) & 0xffffu) == 0u && be32(render + 60u) == 0u);

    /* Fully transparent blended source writes nothing. */
    blit->flags = BLEND_ALPHA;
    blit->color = 0x00ffffffu;
    assert(replay_session(&builder, storage, 5u, &resolver, &destination) ==
           1u && builder.command_count == 0u);

    /* Unsupported or unsafe requests reject the whole list. */
    blit->color = ASTRA_DRAW_LIST_COLOR_IDENTITY;
    blit->source_width = 49u; /* 16 + 49 > 64 */
    assert(replay_session(&builder, storage, 7u, &resolver, &destination) ==
           UINT32_MAX);
    blit->source_width = 32u;
    blit->source = 9u; /* INDEX8 needs a palette to reach RGB565 */
    assert(replay_session(&builder, storage, 8u, &resolver, &destination) ==
           UINT32_MAX);
    blit->source = 3u; /* unknown surface */
    assert(replay_session(&builder, storage, 9u, &resolver, &destination) ==
           UINT32_MAX);
    blit->source = ASTRA_DRAW_LIST_SOURCE_DESTINATION; /* scaled self-copy */
    assert(replay_session(&builder, storage, 10u, &resolver, &destination) ==
           UINT32_MAX);
    blit->source_width = 64u;
    blit->source_height = 32u;
    blit->flags = 0u; /* unscaled self-copy is the overlap-safe case */
    assert(replay_session(&builder, storage, 11u, &resolver, &destination) ==
           1u);
    blit->flags = ASTRA_DRAW_LIST_FLIP_X;
    assert(replay_session(&builder, storage, 12u, &resolver, &destination) ==
           UINT32_MAX);
    assert(replay_session(&builder, storage, 13u, NULL, &destination) ==
           UINT32_MAX);
}

static void test_session_blended_fill(void)
{
    static uint8_t storage[ASTRA_RENDER_BUILDER_BYTES];
    AstraDrawListHeader *header = session_begin(320u, 200u);
    AstraDrawListCommand *fill = session_add(header, ASTRA_DRAW_LIST_FILL);
    AstraRenderBuilder builder;
    const uint8_t *render;
    const uint8_t *source;
    uint32_t destination;

    fill->flags = BLEND_ALPHA;
    fill->x = 10;
    fill->y = 20;
    fill->width = 100u;
    fill->height = 50u;
    fill->color = 0x40ff8000u;
    /* A translucent fill is a blended rectangle list of one, its color
       straight ARGB. */
    assert(replay_session(&builder, storage, 3u, NULL, &destination) == 1u);
    render = batch_command(storage, 0u);
    assert(builder.command_count == 1u &&
           be32(render + 4u) == (uint32_t)ASTRA_RENDER_OP_FILL_RECTS << 16 &&
           be32(render + 44u) == 1u &&
           be32(render + 48u) == ASTRA_RENDER_FILL_RECTS_OPTION_BLEND &&
           be32(render + 60u) == 0x40ff8000u);
    source = batch_record(storage, be32(render + 40u));
    assert(be32(source + 0u) == ((uint32_t)10u << 16 | 20u) &&
           be32(source + 4u) == ((uint32_t)100u << 16 | 50u) &&
           be32(source + 8u) == 0u && be32(source + 12u) == 0u);
    /* Rows the list cannot blend (a pitch off eight bytes) take the
       blitter's one-pixel source instead, which it matches bit for bit. */
    {
        uint32_t next;

        assert(astra_render_builder_init(&builder, storage,
                                         ASTRA_RENDER_BUILDER_BYTES, 9u));
        destination = astra_render_builder_surface_format_at(
            &builder, ASTRA_RENDER_BATCH_WORKSPACE_LIMIT, 644u * 200u, 320u,
            200u, 644u, ASTRA_RENDER_FORMAT_RGB565,
            ASTRA_RENDER_SURFACE_READ | ASTRA_RENDER_SURFACE_WRITE);
        assert(astra_render_builder_replay_range(
                   &builder, destination, header, sizeof(session_list), NULL,
                   0u, &next) == ASTRA_RENDER_REPLAY_DONE);
    }
    render = batch_command(storage, 0u);
    assert(be32(render + 4u) ==
           ((uint32_t)ASTRA_RENDER_OP_BLIT << 16 |
            ASTRA_RENDER_FLAG_BLIT_ALPHA));
    source = batch_record(storage, be32(render + 36u));
    assert(be32(source + 20u) == ((uint32_t)1u << 16 | 1u) &&
           be32(source + 24u) >> 24 == ASTRA_RENDER_FORMAT_ARGB8888);
    assert(be32(batch_record(storage, be32(source + 8u))) == 0x40ff8000u);
    assert(be32(render + 52u) == ((uint32_t)1u << 16 | 1u) &&
           be32(render + 56u) == ((uint32_t)100u << 16 | 50u) &&
           be32(render + 60u) == UINT32_C(0xff000000));

    /* Opaque BLEND stays an ordinary fill in the destination format. */
    fill->color = 0xff00ff00u;
    assert(replay_session(&builder, storage, 4u, NULL, &destination) == 1u);
    render = batch_command(storage, 0u);
    assert(be32(render + 4u) >> 16 == ASTRA_RENDER_OP_FILL &&
           be32(render + 60u) == 0x07e0u);
    /* A blended line needs RTL support and is rejected, not drawn opaque. */
    fill->operation = ASTRA_DRAW_LIST_LINE;
    fill->width = 30u;
    fill->height = 40u;
    fill->color = 0x80ffffffu;
    assert(replay_session(&builder, storage, 5u, NULL, &destination) ==
           UINT32_MAX);
}

static void test_session_list_splits_full_batches(void)
{
    static uint8_t storage[ASTRA_RENDER_BUILDER_BYTES];
    AstraDrawListHeader *header = session_begin(320u, 200u);
    AstraRenderBuilder builder;
    uint32_t destination;
    uint32_t next = 0u;
    uint32_t total = 0u;
    uint32_t batches = 0u;

    for (uint32_t index = 0u; index < SESSION_CAPACITY; ++index) {
        AstraDrawListCommand *fill = session_add(header,
                                                 ASTRA_DRAW_LIST_FILL);

        fill->x = (int32_t)(index % 300u);
        fill->y = (int32_t)(index % 190u);
        fill->width = 4u;
        fill->height = 4u;
        fill->color = 0xff000000u | index;
    }
    while (next < header->command_count) {
        uint32_t first = next;
        int result;

        assert(astra_render_builder_init(&builder, storage,
                                         ASTRA_RENDER_BUILDER_BYTES,
                                         10u + batches));
        destination = astra_render_builder_surface_at(
            &builder, ASTRA_RENDER_BATCH_WORKSPACE_LIMIT, 320u * 200u * 2u,
            320u, 200u);
        result = astra_render_builder_replay_range(
            &builder, destination, header, sizeof(session_list), NULL,
            first, &next);
        assert(result == ASTRA_RENDER_REPLAY_DONE ||
               result == ASTRA_RENDER_REPLAY_FULL);
        assert(next > first && builder.failed == 0u);
        assert(builder.command_count == next - first);
        assert(astra_render_builder_finish_render_only(&builder) != 0u);
        assert(be32(storage + 4u) == ASTRA_RENDER_BATCH_VERSION_1_4);
        /* The resumed command is the first one that did not fit. */
        assert(be32(batch_command(storage, 0u) + 60u) ==
               astra_render_builder_destination_color(
                   ASTRA_RENDER_FORMAT_RGB565, 0xff000000u | first));
        total += builder.command_count;
        ++batches;
    }
    assert(total == SESSION_CAPACITY && batches == 2u);
}

/* A TARGET mark ends a segment: what follows draws into the surface it
   names, validated against that surface, in the same batch or the next. */
static void test_session_targets(void)
{
    static uint8_t storage[ASTRA_RENDER_BUILDER_BYTES];
    AstraDrawListHeader *header = session_begin(320u, 200u);
    AstraDrawListCommand *command;
    AstraRenderBuilder builder;
    uint32_t window;
    uint32_t sprite;
    uint32_t next = 0u;

    command = session_add(header, ASTRA_DRAW_LIST_FILL);
    command->width = 320u;
    command->height = 200u;
    command = session_add(header, ASTRA_DRAW_LIST_TARGET);
    command->source = 7u;
    command->width = 64u;
    command->height = 32u;
    command->clip_right = 64u;
    command->clip_bottom = 32u;
    command = session_add(header, ASTRA_DRAW_LIST_FILL);
    command->width = 64u;
    command->height = 32u;
    command->clip_right = 64u;
    command->clip_bottom = 32u;
    assert(astra_render_builder_init(&builder, storage,
                                     ASTRA_RENDER_BUILDER_BYTES, 3u));
    window = astra_render_builder_surface_at(
        &builder, ASTRA_RENDER_BATCH_WORKSPACE_LIMIT, 320u * 200u * 2u,
        320u, 200u);
    assert(astra_render_builder_replay_range(
               &builder, window, header, sizeof(session_list), NULL, 0u,
               &next) == ASTRA_RENDER_REPLAY_TARGET &&
           next == 1u && builder.command_count == 1u);
    sprite = astra_render_builder_surface_format_at(
        &builder, ASTRA_RENDER_BATCH_WORKSPACE_LIMIT + 0x40000u,
        64u * 32u * 4u, 64u, 32u, 64u * 4u, ASTRA_RENDER_FORMAT_ARGB8888,
        ASTRA_RENDER_SURFACE_READ | ASTRA_RENDER_SURFACE_WRITE);
    assert(astra_render_builder_replay_range(
               &builder, sprite, header, sizeof(session_list), NULL,
               next + 1u, &next) == ASTRA_RENDER_REPLAY_DONE &&
           next == 3u && builder.command_count == 2u);
    assert(be32(batch_command(storage, 1u) + 32u) == sprite);

    /* A clip spans the destination or the list: beyond both it is
       invalid. */
    command->clip_right = 321u;
    assert(astra_render_builder_replay_range(
               &builder, sprite, header, sizeof(session_list), NULL, 2u,
               &next) == ASTRA_RENDER_REPLAY_INVALID);
    command->clip_right = 64u;
    /* A TARGET names a surface, never the list's own destination, and its
       clip is that surface. */
    command = &((AstraDrawListCommand *)(void *)(header + 1))[1];
    command->source = ASTRA_DRAW_LIST_SOURCE_DESTINATION;
    assert(astra_render_builder_replay_range(
               &builder, window, header, sizeof(session_list), NULL, 1u,
               &next) == ASTRA_RENDER_REPLAY_INVALID);
    command->source = 7u;
    command->clip_right = 63u;
    assert(astra_render_builder_replay_range(
               &builder, window, header, sizeof(session_list), NULL, 1u,
               &next) == ASTRA_RENDER_REPLAY_INVALID);
    command->clip_right = 64u;

    /* An UPLOAD stops the replay unlowered, like a TARGET: its rows are
       the caller's to attach to a batch of their own. */
    header = session_begin(320u, 200u);
    command = session_add(header, ASTRA_DRAW_LIST_FILL);
    command->width = 320u;
    command->height = 200u;
    command = session_add(header, ASTRA_DRAW_LIST_UPLOAD);
    command->source = 7u;
    command->x = 2;
    command->y = 3;
    command->width = 16u;
    command->height = 4u;
    command->color = 64u;
    command->payload_offset = 4096u;
    command->clip_right = 0u;
    command->clip_bottom = 0u;
    assert(astra_render_builder_init(&builder, storage,
                                     ASTRA_RENDER_BUILDER_BYTES, 4u));
    window = astra_render_builder_surface_at(
        &builder, ASTRA_RENDER_BATCH_WORKSPACE_LIMIT, 320u * 200u * 2u,
        320u, 200u);
    assert(astra_render_builder_replay_range(
               &builder, window, header, sizeof(session_list), NULL, 0u,
               &next) == ASTRA_RENDER_REPLAY_UPLOAD &&
           next == 1u && builder.command_count == 1u);
    /* It names a surface and a pitch, and nothing else. */
    command->source = ASTRA_DRAW_LIST_SOURCE_DESTINATION;
    assert(astra_render_builder_replay_range(
               &builder, window, header, sizeof(session_list), NULL, 1u,
               &next) == ASTRA_RENDER_REPLAY_INVALID);
    command->source = 7u;
    command->color = 0u;
    assert(astra_render_builder_replay_range(
               &builder, window, header, sizeof(session_list), NULL, 1u,
               &next) == ASTRA_RENDER_REPLAY_INVALID);
    command->color = 64u;
    command->clip_right = 16u;
    assert(astra_render_builder_replay_range(
               &builder, window, header, sizeof(session_list), NULL, 1u,
               &next) == ASTRA_RENDER_REPLAY_INVALID);
    command->clip_right = 0u;
    command->x = -1;
    assert(astra_render_builder_replay_range(
               &builder, window, header, sizeof(session_list), NULL, 1u,
               &next) == ASTRA_RENDER_REPLAY_INVALID);
    command->x = 2;
    assert(astra_render_builder_replay_range(
               &builder, window, header, sizeof(session_list), NULL, 1u,
               &next) == ASTRA_RENDER_REPLAY_UPLOAD && next == 1u);

    /* A segment that fills the batch leaves the next one for a new batch:
       its first command is FULL there, not invalid. */
    header = session_begin(320u, 200u);
    for (uint32_t index = 0u; index < ASTRA_RENDER_RING_ENTRIES; ++index) {
        command = session_add(header, ASTRA_DRAW_LIST_FILL);
        command->width = 4u;
        command->height = 4u;
    }
    command = session_add(header, ASTRA_DRAW_LIST_TARGET);
    command->source = 7u;
    command->width = 64u;
    command->height = 32u;
    command->clip_right = 64u;
    command->clip_bottom = 32u;
    command = session_add(header, ASTRA_DRAW_LIST_FILL);
    command->width = 4u;
    command->height = 4u;
    command->clip_right = 64u;
    command->clip_bottom = 32u;
    assert(astra_render_builder_init(&builder, storage,
                                     ASTRA_RENDER_BUILDER_BYTES, 4u));
    window = astra_render_builder_surface_at(
        &builder, ASTRA_RENDER_BATCH_WORKSPACE_LIMIT, 320u * 200u * 2u,
        320u, 200u);
    assert(astra_render_builder_replay_range(
               &builder, window, header, sizeof(session_list), NULL, 0u,
               &next) == ASTRA_RENDER_REPLAY_TARGET &&
           next == ASTRA_RENDER_RING_ENTRIES &&
           builder.command_count == ASTRA_RENDER_RING_ENTRIES);
    sprite = astra_render_builder_surface_format_at(
        &builder, ASTRA_RENDER_BATCH_WORKSPACE_LIMIT + 0x40000u,
        64u * 32u * 4u, 64u, 32u, 64u * 4u, ASTRA_RENDER_FORMAT_ARGB8888,
        ASTRA_RENDER_SURFACE_READ | ASTRA_RENDER_SURFACE_WRITE);
    assert(astra_render_builder_replay_range(
               &builder, sprite, header, sizeof(session_list), NULL,
               next + 1u, &next) == ASTRA_RENDER_REPLAY_FULL &&
           next == ASTRA_RENDER_RING_ENTRIES + 1u &&
           builder.command_count == ASTRA_RENDER_RING_ENTRIES);
}

static void test_session_header_is_bounded(void)
{
    static uint8_t storage[ASTRA_RENDER_BUILDER_BYTES];
    AstraDrawListHeader *header = session_begin(320u, 200u);
    AstraRenderBuilder builder;
    uint32_t destination;
    uint32_t next;
    AstraDrawListCommand *fill = session_add(header, ASTRA_DRAW_LIST_FILL);

    fill->width = 1u;
    fill->height = 1u;
    assert(replay_session(&builder, storage, 3u, NULL, &destination) == 1u);
    assert(astra_render_builder_replay_range(
               &builder, destination, header, header->total_bytes - 1u,
               NULL, 0u, &next) == ASTRA_RENDER_REPLAY_INVALID);
    header->command_capacity = UINT32_MAX / 64u;
    assert(replay_session(&builder, storage, 4u, NULL, &destination) ==
           UINT32_MAX);
    header->command_capacity = SESSION_CAPACITY;
    header->reserved[3] = 1u;
    assert(replay_session(&builder, storage, 5u, NULL, &destination) ==
           UINT32_MAX);
    header->reserved[3] = 0u;
    /* A render-only batch presents nothing: no scene, reserved header. */
    assert(replay_session(&builder, storage, 6u, NULL, &destination) == 1u);
    assert(astra_render_builder_finish_render_only(&builder) != 0u);
    assert(be32(storage + 4u) == ASTRA_RENDER_BATCH_VERSION_1_4);
    assert(be32(storage + 32u) == 0u);
}


/* Append n vertices to the session payload and point command at them. */
static AstraDrawListVertex *session_vertices(AstraDrawListHeader *header,
                                             AstraDrawListCommand *command,
                                             uint32_t count)
{
    uint32_t offset = astra_draw_list_payload_offset(
                          header->command_capacity) + header->payload_bytes;
    uint32_t bytes = count * (uint32_t)sizeof(AstraDrawListVertex);

    assert(header->payload_bytes + bytes <= SESSION_PAYLOAD_BYTES);
    command->payload_offset = offset;
    command->payload_bytes = bytes;
    header->payload_bytes += bytes;
    return (AstraDrawListVertex *)(void *)(session_list + offset);
}

static void expect_vertex(const uint8_t *batch, uint32_t array,
                          uint32_t index, const AstraDrawListVertex *vertex)
{
    const uint8_t *record = batch_record(
        batch, array + index * ASTRA_RENDER_TRIANGLE_VERTEX_BYTES);

    assert((int32_t)be32(record + 0u) == vertex->x &&
           (int32_t)be32(record + 4u) == vertex->y &&
           (int32_t)be32(record + 8u) == vertex->u &&
           (int32_t)be32(record + 12u) == vertex->v &&
           be32(record + 16u) == vertex->color &&
           be32(record + 20u) == 0u && be32(record + 24u) == 0u &&
           be32(record + 28u) == 0u);
}

static uint32_t replay_status(uint8_t *storage, uint32_t generation,
                              const AstraRenderSourceResolver *resolver,
                              AstraRenderBuilder *builder)
{
    uint32_t destination;

    return replay_session(builder, storage, generation, resolver,
                          &destination);
}

static void test_session_triangles_encoding(void)
{
    static uint8_t storage[ASTRA_RENDER_BUILDER_BYTES];
    TestSurfaces surfaces = {
        ASTRA_RENDER_BATCH_WORKSPACE_LIMIT + 0x40000u,
        ASTRA_RENDER_BATCH_WORKSPACE_LIMIT + 0x80000u, 0u
    };
    AstraRenderSourceResolver resolver = { resolve_test_surface, &surfaces };
    AstraDrawListHeader *header = session_begin(320u, 200u);
    AstraDrawListCommand *triangles =
        session_add(header, ASTRA_DRAW_LIST_TRIANGLES);
    AstraDrawListVertex *vertices = session_vertices(header, triangles, 6u);
    AstraRenderBuilder builder;
    const uint8_t *render;
    const uint8_t *source;
    uint32_t destination;
    uint32_t array;

    for (uint32_t index = 0u; index < 6u; ++index)
        vertices[index] = (AstraDrawListVertex){
            (int32_t)(index * 1000u) - 2000, (int32_t)(index * 300u) + 7,
            0, 0, 0x80102030u + index };
    triangles->flags = ASTRA_DRAW_LIST_BLEND_FLAGS(ASTRA_DRAW_LIST_BLEND_ADD);
    triangles->clip_left = 3u;
    triangles->clip_top = 4u;
    triangles->clip_right = 300u;
    triangles->clip_bottom = 150u;
    assert(replay_session(&builder, storage, 3u, &resolver, &destination) ==
           1u);
    assert(builder.command_count == 1u && surfaces.calls == 0u);
    render = batch_command(storage, 0u);
    /* Section 1: opcode 768, zero command flags, words 8-15. */
    assert(be32(render + 4u) == (uint32_t)ASTRA_RENDER_OP_TRIANGLES << 16);
    assert(be32(render + 24u) == ((uint32_t)3u << 16 | 4u) &&
           be32(render + 28u) == ((uint32_t)300u << 16 | 150u));
    assert(be32(render + 32u) == destination && be32(render + 36u) == 0u);
    array = be32(render + 40u);
    assert(array >= ASTRA_RENDER_BATCH_DATA_OFFSET &&
           (array & (ASTRA_RENDER_TRIANGLE_VERTEX_BYTES - 1u)) == 0u);
    assert(be32(render + 44u) == 2u &&
           be32(render + 48u) == ASTRA_RENDER_TRIANGLE_OPTION_BLEND_ADD);
    assert(be32(render + 52u) == 0u && be32(render + 56u) == 0u &&
           be32(render + 60u) == 0u);
    for (uint32_t index = 0u; index < 6u; ++index)
        expect_vertex(storage, array, index, &vertices[index]);

    /* Textured from an ARGB surface, filtered, alpha-blended. */
    triangles->source = 7u;
    triangles->flags = ASTRA_DRAW_LIST_BLEND_FLAGS(
                           ASTRA_DRAW_LIST_BLEND_BLEND) |
                       ASTRA_DRAW_LIST_FILTER_LINEAR;
    vertices[4].u = 64 << 16;
    vertices[4].v = -(3 << 15);
    assert(replay_session(&builder, storage, 4u, &resolver, &destination) ==
           1u && surfaces.calls == 1u);
    render = batch_command(storage, 0u);
    source = batch_record(storage, be32(render + 36u));
    assert(be32(source + 8u) == surfaces.sprite_offset);
    assert(be32(render + 48u) ==
           (ASTRA_RENDER_TRIANGLE_OPTION_BLEND_BLEND |
            ASTRA_RENDER_TRIANGLE_OPTION_FILTER_LINEAR));
    expect_vertex(storage, be32(render + 40u), 4u, &vertices[4]);

    /* A clip outside the destination draws nothing. */
    header->width = 640u;
    triangles->clip_left = 400u;
    triangles->clip_right = 500u;
    assert(replay_session(&builder, storage, 5u, &resolver, &destination) ==
           1u && builder.command_count == 0u);
}

static void test_session_triangles_rejections(void)
{
    static uint8_t storage[ASTRA_RENDER_BUILDER_BYTES];
    TestSurfaces surfaces = {
        ASTRA_RENDER_BATCH_WORKSPACE_LIMIT + 0x40000u,
        ASTRA_RENDER_BATCH_WORKSPACE_LIMIT + 0x80000u, 0u
    };
    AstraRenderSourceResolver resolver = { resolve_test_surface, &surfaces };
    AstraDrawListHeader *header = session_begin(320u, 200u);
    AstraDrawListCommand *triangles =
        session_add(header, ASTRA_DRAW_LIST_TRIANGLES);
    AstraDrawListVertex *vertices = session_vertices(header, triangles, 3u);
    AstraRenderBuilder builder;
    uint32_t destination;
    uint32_t next;

    vertices[0] = (AstraDrawListVertex){ 0, 0, 0, 0, 0xffffffffu };
    vertices[1] = (AstraDrawListVertex){ 100 << 8, 0, 0, 0, 0xffffffffu };
    vertices[2] = (AstraDrawListVertex){ 0, 100 << 8, 0, 0, 0xffffffffu };
    assert(replay_status(storage, 3u, &resolver, &builder) == 1u);
    /* Filtering without a texture. */
    triangles->flags = ASTRA_DRAW_LIST_FILTER_LINEAR;
    assert(replay_status(storage, 4u, &resolver, &builder) == UINT32_MAX);
    /* Blend modes 5-7. */
    triangles->flags = ASTRA_DRAW_LIST_BLEND_FLAGS(5u);
    assert(replay_status(storage, 5u, &resolver, &builder) == UINT32_MAX);
    triangles->flags = 0u;
    /* Untextured vertices carry no texel coordinates. */
    vertices[1].u = 1;
    assert(replay_status(storage, 6u, &resolver, &builder) == UINT32_MAX);
    vertices[1].u = 0;
    /* Vertices beyond +-32768 pixels. */
    vertices[2].y = ASTRA_TEXTURE_COORD_LIMIT;
    assert(replay_status(storage, 7u, &resolver, &builder) == UINT32_MAX);
    vertices[2].y = -ASTRA_TEXTURE_COORD_LIMIT;
    assert(replay_status(storage, 8u, &resolver, &builder) == 1u);
    /* The rectangle, color, and source rectangle stay zero. */
    triangles->color = 1u;
    assert(replay_status(storage, 9u, &resolver, &builder) == UINT32_MAX);
    triangles->color = 0u;
    triangles->source_width = 1u;
    assert(replay_status(storage, 10u, &resolver, &builder) == UINT32_MAX);
    triangles->source_width = 0u;
    /* Payload: whole triangles, aligned, inside the payload. */
    triangles->payload_bytes -= 4u;
    assert(replay_status(storage, 11u, &resolver, &builder) == UINT32_MAX);
    triangles->payload_bytes += 4u;
    header->payload_bytes -= 4u;
    assert(replay_status(storage, 12u, &resolver, &builder) == UINT32_MAX);
    header->payload_bytes += 4u;
    triangles->payload_bytes = 0u;
    assert(replay_status(storage, 13u, &resolver, &builder) == UINT32_MAX);
    triangles->payload_bytes = 3u * (uint32_t)sizeof(AstraDrawListVertex);
    /* INDEX8 sources need a palette; unknown sources are rejected. */
    triangles->source = 9u;
    assert(replay_status(storage, 14u, &resolver, &builder) == UINT32_MAX);
    triangles->source = 3u;
    assert(replay_status(storage, 15u, &resolver, &builder) == UINT32_MAX);
    triangles->source = 7u;
    assert(replay_status(storage, 16u, &resolver, &builder) == 1u);
    /* An INDEX8 destination cannot take triangles. */
    triangles->source = 0u;
    assert(astra_render_builder_init(&builder, storage,
                                     ASTRA_RENDER_BUILDER_BYTES, 17u));
    destination = astra_render_builder_surface_format_at(
        &builder, ASTRA_RENDER_BATCH_WORKSPACE_LIMIT, 320u * 200u, 320u,
        200u, 320u, ASTRA_RENDER_FORMAT_INDEX8,
        ASTRA_RENDER_SURFACE_READ | ASTRA_RENDER_SURFACE_WRITE);
    assert(destination != 0u &&
           astra_render_builder_replay_range(
               &builder, destination, header, sizeof(session_list),
               &resolver, 0u, &next) == ASTRA_RENDER_REPLAY_INVALID);
}

static void test_session_triangles_split(void)
{
    static uint8_t storage[ASTRA_RENDER_BUILDER_BYTES];
    AstraDrawListHeader *header = session_begin(320u, 200u);
    AstraDrawListCommand *first =
        session_add(header, ASTRA_DRAW_LIST_TRIANGLES);
    AstraDrawListVertex *vertices = session_vertices(header, first, 4097u *
                                                                    3u);
    AstraRenderBuilder builder;
    uint32_t destination;
    uint32_t next = 0u;
    uint32_t batches = 0u;
    uint32_t hardware = 0u;
    uint32_t triangles = 0u;

    for (uint32_t index = 0u; index < 4097u * 3u; ++index)
        vertices[index] = (AstraDrawListVertex){
            (int32_t)(index % 3u == 1u ? 256u : 0u),
            (int32_t)(index % 3u == 2u ? 256u : 0u), 0, 0,
            0xff000000u | index };
    /* One command above the 4096-triangle limit becomes two. */
    assert(replay_session(&builder, storage, 3u, NULL, &destination) == 1u);
    assert(builder.command_count == 2u &&
           be32(batch_command(storage, 0u) + 44u) == 4096u &&
           be32(batch_command(storage, 1u) + 44u) == 1u);
    expect_vertex(storage, be32(batch_command(storage, 1u) + 40u), 2u,
                  &vertices[4096u * 3u + 2u]);
    /* Forty commands sharing that payload overflow one batch's data
       arena; the list resumes, whole commands at a time, in the next. */
    for (uint32_t index = 1u; index < 40u; ++index)
        *session_add(header, ASTRA_DRAW_LIST_TRIANGLES) = *first;
    while (next < header->command_count) {
        uint32_t start = next;
        int result;

        assert(astra_render_builder_init(&builder, storage,
                                         ASTRA_RENDER_BUILDER_BYTES,
                                         10u + batches));
        destination = astra_render_builder_surface_at(
            &builder, ASTRA_RENDER_BATCH_WORKSPACE_LIMIT, 320u * 200u * 2u,
            320u, 200u);
        result = astra_render_builder_replay_range(
            &builder, destination, header, sizeof(session_list), NULL,
            start, &next);
        assert(result == ASTRA_RENDER_REPLAY_DONE ||
               result == ASTRA_RENDER_REPLAY_FULL);
        assert(next > start && builder.failed == 0u &&
               builder.command_count == (next - start) * 2u);
        for (uint32_t index = 0u; index < builder.command_count; ++index)
            triangles += be32(batch_command(storage, index) + 44u);
        assert(astra_render_builder_finish_render_only(&builder) != 0u);
        hardware += builder.command_count;
        ++batches;
    }
    assert(batches > 1u && hardware == 80u && triangles == 40u * 4097u);
}

/* Modulated, ADD/MOD/MUL, and filtered BLITs and fills become two
   triangles; plain and alpha copies stay blits. */
static void test_session_engine_lowering(void)
{
    static uint8_t storage[ASTRA_RENDER_BUILDER_BYTES];
    TestSurfaces surfaces = {
        ASTRA_RENDER_BATCH_WORKSPACE_LIMIT + 0x40000u,
        ASTRA_RENDER_BATCH_WORKSPACE_LIMIT + 0x80000u, 0u
    };
    AstraRenderSourceResolver resolver = { resolve_test_surface, &surfaces };
    AstraDrawListHeader *header = session_begin(320u, 200u);
    AstraDrawListCommand *blit = session_add(header, ASTRA_DRAW_LIST_BLIT);
    AstraRenderBuilder builder;
    const uint8_t *render;
    uint32_t destination;
    uint32_t array;

    blit->flags = ASTRA_DRAW_LIST_FLIP_X;
    blit->x = -5;
    blit->y = 10;
    blit->width = 64u;
    blit->height = 32u;
    blit->color = 0xc0ff8040u;
    blit->source = 7u;
    blit->source_x = 16;
    blit->source_y = 8;
    blit->source_width = 32u;
    blit->source_height = 16u;
    assert(replay_session(&builder, storage, 3u, &resolver, &destination) ==
           1u);
    render = batch_command(storage, 0u);
    assert(builder.command_count == 1u &&
           be32(render + 4u) == (uint32_t)ASTRA_RENDER_OP_TRIANGLES << 16 &&
           be32(render + 44u) == 2u &&
           be32(render + 48u) == ASTRA_RENDER_TRIANGLE_OPTION_BLEND_NONE);
    array = be32(render + 40u);
    {
        /* FLIP_X swaps the texel edges; the diagonal is shared. */
        const AstraDrawListVertex expected[6] = {
            { -5 * 256, 10 * 256, 48 << 16, 8 << 16, 0xc0ff8040u },
            { 59 * 256, 10 * 256, 16 << 16, 8 << 16, 0xc0ff8040u },
            { -5 * 256, 42 * 256, 48 << 16, 24 << 16, 0xc0ff8040u },
            { 59 * 256, 10 * 256, 16 << 16, 8 << 16, 0xc0ff8040u },
            { 59 * 256, 42 * 256, 16 << 16, 24 << 16, 0xc0ff8040u },
            { -5 * 256, 42 * 256, 48 << 16, 24 << 16, 0xc0ff8040u },
        };

        for (uint32_t index = 0u; index < 6u; ++index)
            expect_vertex(storage, array, index, &expected[index]);
    }
    /* White modulation with MUL, and with linear filtering. */
    blit->color = ASTRA_DRAW_LIST_COLOR_IDENTITY;
    blit->flags = ASTRA_DRAW_LIST_BLEND_FLAGS(ASTRA_DRAW_LIST_BLEND_MUL);
    assert(replay_session(&builder, storage, 4u, &resolver, &destination) ==
           1u);
    render = batch_command(storage, 0u);
    assert(be32(render + 4u) >> 16 == ASTRA_RENDER_OP_TRIANGLES &&
           be32(render + 48u) == ASTRA_RENDER_TRIANGLE_OPTION_BLEND_MUL);
    blit->flags = ASTRA_DRAW_LIST_FILTER_LINEAR | BLEND_ALPHA;
    assert(replay_session(&builder, storage, 5u, &resolver, &destination) ==
           1u);
    render = batch_command(storage, 0u);
    assert(be32(render + 48u) ==
           (ASTRA_RENDER_TRIANGLE_OPTION_BLEND_BLEND |
            ASTRA_RENDER_TRIANGLE_OPTION_FILTER_LINEAR));
    {
        /* Filtering is kept only where it is affordable: a copy whose
           visible area exceeds the budget, or one that is not scaled at
           all, is drawn by the blitter instead. */
        int32_t x = blit->x;
        int32_t y = blit->y;
        uint32_t width = blit->width;
        uint32_t height = blit->height;

        blit->x = 0;
        blit->y = 0;
        blit->width = 320u;
        blit->height = 200u;
        assert(replay_session(&builder, storage, 20u, &resolver,
                              &destination) == 1u);
        assert(be32(batch_command(storage, 0u) + 4u) >> 16 ==
               ASTRA_RENDER_OP_BLIT);
        /* Clipped to its visible part, the same copy is affordable. */
        blit->clip_right = 100u;
        blit->clip_bottom = 100u;
        assert(replay_session(&builder, storage, 21u, &resolver,
                              &destination) == 1u);
        assert(be32(batch_command(storage, 0u) + 4u) >> 16 ==
               ASTRA_RENDER_OP_TRIANGLES);
        blit->clip_right = 320u;
        blit->clip_bottom = 200u;
        blit->width = blit->source_width;
        blit->height = blit->source_height;
        assert(replay_session(&builder, storage, 22u, &resolver,
                              &destination) == 1u);
        assert(be32(batch_command(storage, 0u) + 4u) >> 16 ==
               ASTRA_RENDER_OP_BLIT);
        blit->x = x;
        blit->y = y;
        blit->width = width;
        blit->height = height;
    }
    /* A plain alpha copy keeps the blitter. */
    blit->flags = BLEND_ALPHA;
    assert(replay_session(&builder, storage, 6u, &resolver, &destination) ==
           1u);
    assert(be32(batch_command(storage, 0u) + 4u) >> 16 ==
           ASTRA_RENDER_OP_BLIT);
    /* The engine never textures a surface from itself. */
    blit->source = ASTRA_DRAW_LIST_SOURCE_DESTINATION;
    blit->source_width = 64u;
    blit->source_height = 32u;
    blit->flags = 0u;
    blit->color = 0xff808080u;
    assert(replay_session(&builder, storage, 7u, &resolver, &destination) ==
           UINT32_MAX);
    /* A rectangle past the engine's vertex range is cut to the clip, with
       the texel edges moved proportionally. */
    blit->source = 7u;
    blit->source_x = 0;
    blit->source_y = 0;
    blit->source_width = 64u;
    blit->source_height = 32u;
    blit->x = -32000;
    blit->y = 0;
    blit->width = 65000u; /* right edge 33000 */
    blit->height = 32u;
    assert(replay_session(&builder, storage, 8u, &resolver, &destination) ==
           1u);
    render = batch_command(storage, 0u);
    array = be32(render + 40u);
    {
        const AstraDrawListVertex left_top = {
            0, 0, (int32_t)(((int64_t)64 << 16) * 32000 / 65000), 0,
            0xff808080u };
        const AstraDrawListVertex right_top = {
            320 * 256, 0,
            (int32_t)(((int64_t)64 << 16) * 32320 / 65000), 0,
            0xff808080u };

        expect_vertex(storage, array, 0u, &left_top);
        expect_vertex(storage, array, 1u, &right_top);
    }

    /* ADD fills are untextured triangles; MOD cannot reach INDEX8. */
    blit->operation = ASTRA_DRAW_LIST_FILL;
    blit->flags = ASTRA_DRAW_LIST_BLEND_FLAGS(ASTRA_DRAW_LIST_BLEND_ADD);
    blit->x = 10;
    blit->width = 20u;
    blit->color = 0x80ff0000u;
    blit->source = 0u;
    blit->source_width = 0u;
    blit->source_height = 0u;
    assert(replay_session(&builder, storage, 9u, &resolver, &destination) ==
           1u);
    render = batch_command(storage, 0u);
    assert(be32(render + 4u) >> 16 == ASTRA_RENDER_OP_TRIANGLES &&
           be32(render + 36u) == 0u &&
           be32(render + 48u) == ASTRA_RENDER_TRIANGLE_OPTION_BLEND_ADD);
    {
        const AstraDrawListVertex corner = { 30 * 256, 32 * 256, 0, 0,
                                             0x80ff0000u };

        expect_vertex(storage, be32(render + 40u), 4u, &corner);
    }
    {
        uint32_t next;

        blit->flags = ASTRA_DRAW_LIST_BLEND_FLAGS(ASTRA_DRAW_LIST_BLEND_MOD);
        assert(astra_render_builder_init(&builder, storage,
                                         ASTRA_RENDER_BUILDER_BYTES, 10u));
        destination = astra_render_builder_surface_format_at(
            &builder, ASTRA_RENDER_BATCH_WORKSPACE_LIMIT, 320u * 200u, 320u,
            200u, 320u, ASTRA_RENDER_FORMAT_INDEX8,
            ASTRA_RENDER_SURFACE_READ | ASTRA_RENDER_SURFACE_WRITE);
        assert(astra_render_builder_replay_range(
                   &builder, destination, header, sizeof(session_list),
                   &resolver, 0u, &next) == ASTRA_RENDER_REPLAY_INVALID);
    }
    /* A blended LINE may only be opaque BLEND. */
    blit->operation = ASTRA_DRAW_LIST_LINE;
    blit->flags = ASTRA_DRAW_LIST_BLEND_FLAGS(ASTRA_DRAW_LIST_BLEND_ADD);
    blit->color = 0xffffffffu;
    assert(replay_session(&builder, storage, 11u, &resolver, &destination) ==
           UINT32_MAX);
}

/*
 * The builder's TRIANGLES output run through the texture engine's reference
 * model (texture_reference.c), decoded the way docs/TEXTURE_ENGINE.md
 * section 1 tells the RTL to: descriptors, clip, vertex array and options
 * from the batch bytes alone. Media RAM past the batch is model_media.
 */
static uint8_t model_media[0x100000];

static uint8_t *model_arena(uint8_t *batch, uint32_t offset, uint32_t bytes)
{
    if (offset >= ASTRA_RENDER_BATCH_WORKSPACE_LIMIT) {
        assert(offset - ASTRA_RENDER_BATCH_WORKSPACE_LIMIT + bytes <=
               sizeof(model_media));
        return model_media + (offset - ASTRA_RENDER_BATCH_WORKSPACE_LIMIT);
    }
    assert(offset >= ASTRA_RENDER_BATCH_ARENA_OFFSET &&
           offset + bytes <= ASTRA_RENDER_BATCH_WORKSPACE_LIMIT);
    return batch + (offset - ASTRA_RENDER_BATCH_ARENA_OFFSET);
}

static AstraTextureSurface model_surface(uint8_t *batch, uint32_t address)
{
    const uint8_t *record = batch_record(batch, address);
    uint32_t size = be32(record + 20u);
    AstraTextureSurface surface = {
        model_arena(batch, be32(record + 8u), be32(record + 12u)), NULL,
        be32(record + 16u), (uint16_t)(size >> 16), (uint16_t)size,
        (uint8_t)(be32(record + 24u) >> 24)
    };

    return surface;
}

/* Executes every TRIANGLES command of a built batch; returns pixels
   written. Each must pass the engine's validation. */
static uint32_t model_execute(uint8_t *batch, uint32_t command_count)
{
    uint32_t written = 0u;

    for (uint32_t index = 0u; index < command_count; ++index) {
        const uint8_t *render = batch_command(batch, index);
        uint32_t source_address = be32(render + 36u);
        uint32_t count = be32(render + 44u);
        const uint8_t *array;
        AstraTextureVertex vertices[6];
        AstraTextureSurface destination;
        AstraTextureSurface source;
        AstraTextureClip clip;

        if (be32(render + 4u) >> 16 != ASTRA_RENDER_OP_TRIANGLES)
            continue;
        assert((be32(render + 4u) & 0xffffu) == 0u);
        assert(count >= 1u && count <= 2u);
        destination = model_surface(batch, be32(render + 32u));
        if (source_address != 0u)
            source = model_surface(batch, source_address);
        clip = (AstraTextureClip){
            (int16_t)high_s16(be32(render + 24u)),
            (int16_t)low_s16(be32(render + 24u)),
            (int16_t)high_s16(be32(render + 28u)),
            (int16_t)low_s16(be32(render + 28u))
        };
        array = batch_record(batch, be32(render + 40u));
        for (uint32_t vertex = 0u; vertex < count * 3u; ++vertex) {
            const uint8_t *record =
                array + vertex * ASTRA_RENDER_TRIANGLE_VERTEX_BYTES;

            vertices[vertex] = (AstraTextureVertex){
                (int32_t)be32(record + 0u), (int32_t)be32(record + 4u),
                (int32_t)be32(record + 8u), (int32_t)be32(record + 12u),
                be32(record + 16u)
            };
        }
        assert(astra_texture_validate(
                   &destination, source_address != 0u ? &source : NULL,
                   be32(render + 48u), vertices, count) ==
               ASTRA_RENDER_STATUS_OK);
        written += astra_texture_draw(
            &destination, source_address != 0u ? &source : NULL,
            be32(render + 48u), clip, vertices, count);
    }
    return written;
}

/* The 64x32 ARGB sprite behind surface id 7: opaque, every texel distinct
   in the low bits so an off-by-one texel shows. */
static uint32_t model_texel(uint32_t x, uint32_t y)
{
    return UINT32_C(0xff000000) | (x * 4u) << 16 | (y * 8u) << 8 |
           ((x ^ y) * 4u);
}

static uint16_t model_rgb565(uint32_t x, uint32_t y)
{
    const uint8_t *p = model_media + y * 320u * 2u + x * 2u;

    return (uint16_t)(p[0] << 8 | p[1]);
}

static void model_reset(uint32_t sprite_offset)
{
    uint8_t *sprite = model_media +
                      (sprite_offset - ASTRA_RENDER_BATCH_WORKSPACE_LIMIT);

    /* Destination 320x200 RGB565 at offset 0: a sentinel colour. */
    for (uint32_t index = 0u; index < 320u * 200u; ++index) {
        model_media[index * 2u] = 0x12;
        model_media[index * 2u + 1u] = 0x34;
    }
    for (uint32_t y = 0u; y < 32u; ++y)
        for (uint32_t x = 0u; x < 64u; ++x) {
            uint32_t texel = model_texel(x, y);
            uint8_t *p = sprite + y * 64u * 4u + x * 4u;

            p[0] = (uint8_t)(texel >> 24);
            p[1] = (uint8_t)(texel >> 16);
            p[2] = (uint8_t)(texel >> 8);
            p[3] = (uint8_t)texel;
        }
}

static void test_session_triangles_on_reference_model(void)
{
    static uint8_t storage[ASTRA_RENDER_BUILDER_BYTES];
    TestSurfaces surfaces = {
        ASTRA_RENDER_BATCH_WORKSPACE_LIMIT + 0x40000u,
        ASTRA_RENDER_BATCH_WORKSPACE_LIMIT + 0x80000u, 0u
    };
    AstraRenderSourceResolver resolver = { resolve_test_surface, &surfaces };
    AstraDrawListHeader *header;
    AstraDrawListCommand *blit;
    AstraRenderBuilder builder;
    uint32_t destination;

    /* A 1:1 bilinear quad lands every texel on its pixel exactly; swapping
       its u edges mirrors it. Only the rectangle is written. (A 1:1 BLIT
       would not reach the engine: unscaled, filtering changes nothing, and
       the blitter draws it.) */
    for (int flip = 0; flip < 2; ++flip) {
        int32_t u0 = (flip ? 48 : 16) << 16;
        int32_t u1 = (flip ? 16 : 48) << 16;
        AstraDrawListCommand *quad;
        AstraDrawListVertex *vertices;

        header = session_begin(320u, 200u);
        quad = session_add(header, ASTRA_DRAW_LIST_TRIANGLES);
        vertices = session_vertices(header, quad, 6u);
        quad->source = 7u;
        quad->flags = ASTRA_DRAW_LIST_FILTER_LINEAR;
        vertices[0] = (AstraDrawListVertex){ 5 * 256, 7 * 256, u0, 8 << 16,
                                             ASTRA_DRAW_LIST_COLOR_IDENTITY };
        vertices[1] = (AstraDrawListVertex){ 37 * 256, 7 * 256, u1, 8 << 16,
                                             ASTRA_DRAW_LIST_COLOR_IDENTITY };
        vertices[2] = (AstraDrawListVertex){ 5 * 256, 23 * 256, u0, 24 << 16,
                                             ASTRA_DRAW_LIST_COLOR_IDENTITY };
        vertices[3] = vertices[1];
        vertices[4] = (AstraDrawListVertex){ 37 * 256, 23 * 256, u1,
                                             24 << 16,
                                             ASTRA_DRAW_LIST_COLOR_IDENTITY };
        vertices[5] = vertices[2];
        model_reset(surfaces.sprite_offset);
        assert(replay_session(&builder, storage, 3u, &resolver,
                              &destination) == 1u);
        assert(model_execute(storage, builder.command_count) == 32u * 16u);
        for (uint32_t y = 0u; y < 200u; ++y)
            for (uint32_t x = 0u; x < 320u; ++x) {
                int inside = x >= 5u && x < 37u && y >= 7u && y < 23u;
                uint32_t sx = flip ? 16u + 31u - (x - 5u) : 16u + x - 5u;
                uint16_t expected = inside ?
                    (uint16_t)astra_texture_pack(
                        ASTRA_RENDER_FORMAT_RGB565,
                        model_texel(sx, 8u + y - 7u)) :
                    0x1234u;

                assert(model_rgb565(x, y) == expected);
            }
    }

    /* A 2x nearest upscale with colour modulation, clipped on the left:
       each texel covers a 2x2 block. */
    header = session_begin(320u, 200u);
    blit = session_add(header, ASTRA_DRAW_LIST_BLIT);
    *blit = (AstraDrawListCommand){
        .operation = ASTRA_DRAW_LIST_BLIT,
        .x = -4, .y = 7, .width = 64u, .height = 32u,
        .color = 0xff80c0ffu, .source = 7u,
        .source_x = 16, .source_y = 8,
        .source_width = 32u, .source_height = 16u,
        .clip_right = 320u, .clip_bottom = 200u,
    };
    model_reset(surfaces.sprite_offset);
    assert(replay_session(&builder, storage, 4u, &resolver, &destination) ==
           1u);
    assert(model_execute(storage, builder.command_count) == 60u * 32u);
    for (uint32_t y = 0u; y < 32u; ++y)
        for (uint32_t x = 0u; x < 60u; ++x) {
            uint32_t texel = model_texel(16u + (x + 4u) / 2u, 8u + y / 2u);
            uint32_t modulated =
                UINT32_C(0xff000000) |
                (uint32_t)astra_texture_mul255((uint8_t)(texel >> 16),
                                               0x80u) << 16 |
                (uint32_t)astra_texture_mul255((uint8_t)(texel >> 8),
                                               0xc0u) << 8 |
                (texel & 0xffu);

            assert(model_rgb565(x, 7u + y) ==
                   astra_texture_pack(ASTRA_RENDER_FORMAT_RGB565,
                                      modulated));
        }

    /* A rectangle past the vertex range is cut to the clip and still
       covers exactly the clipped area. */
    blit->x = -32000;
    blit->y = 0;
    blit->width = 65000u;
    blit->height = 32u;
    blit->source_x = 0;
    blit->source_y = 0;
    blit->source_width = 64u;
    blit->source_height = 32u;
    model_reset(surfaces.sprite_offset);
    assert(replay_session(&builder, storage, 5u, &resolver, &destination) ==
           1u);
    assert(model_execute(storage, builder.command_count) == 320u * 32u);

    /* An ADD fill is an untextured quad: saturating per channel. */
    *blit = (AstraDrawListCommand){
        .operation = ASTRA_DRAW_LIST_FILL,
        .flags = ASTRA_DRAW_LIST_BLEND_FLAGS(ASTRA_DRAW_LIST_BLEND_ADD),
        .x = 10, .y = 20, .width = 30u, .height = 40u,
        .color = 0x80ff0000u,
        .clip_right = 320u, .clip_bottom = 200u,
    };
    model_reset(surfaces.sprite_offset);
    assert(replay_session(&builder, storage, 6u, &resolver, &destination) ==
           1u);
    assert(model_execute(storage, builder.command_count) == 30u * 40u);
    assert(model_rgb565(10u, 20u) ==
           astra_texture_pack(ASTRA_RENDER_FORMAT_RGB565,
                              astra_texture_blend(
                                  ASTRA_RENDER_TRIANGLE_OPTION_BLEND_ADD,
                                  0x80ff0000u,
                                  argb_from_rgb565(0x1234u))) &&
           model_rgb565(9u, 20u) == 0x1234u &&
           model_rgb565(39u, 59u) != 0x1234u &&
           model_rgb565(40u, 59u) == 0x1234u);

    /* Client triangles reach the model unchanged, clip included: a fan
       with a shared edge draws each clipped pixel once. */
    *blit = (AstraDrawListCommand){
        .operation = ASTRA_DRAW_LIST_TRIANGLES,
        .flags = ASTRA_DRAW_LIST_BLEND_FLAGS(ASTRA_DRAW_LIST_BLEND_NONE),
        .source = 7u,
        .clip_left = 2u, .clip_top = 3u,
        .clip_right = 50u, .clip_bottom = 40u,
    };
    header->payload_bytes = 0u;
    {
        AstraDrawListVertex *vertices = session_vertices(header, blit, 6u);
        const AstraDrawListVertex fan[6] = {
            { 0, 0, 0, 0, 0xffffffffu },
            { 64 * 256, 0, 64 << 16, 0, 0xffffffffu },
            { 0, 32 * 256, 0, 32 << 16, 0xffffffffu },
            { 64 * 256, 0, 64 << 16, 0, 0xffffffffu },
            { 64 * 256, 32 * 256, 64 << 16, 32 << 16, 0xffffffffu },
            { 0, 32 * 256, 0, 32 << 16, 0xffffffffu },
        };

        memcpy(vertices, fan, sizeof(fan));
    }
    model_reset(surfaces.sprite_offset);
    assert(replay_session(&builder, storage, 7u, &resolver, &destination) ==
           1u);
    assert(model_execute(storage, builder.command_count) == 48u * 29u);
    for (uint32_t y = 0u; y < 40u; ++y)
        for (uint32_t x = 0u; x < 64u; ++x) {
            int inside = x >= 2u && x < 50u && y >= 3u && y < 32u;

            assert(model_rgb565(x, y) ==
                   (inside ? astra_texture_pack(ASTRA_RENDER_FORMAT_RGB565,
                                                model_texel(x, y)) :
                             0x1234u));
        }
}

/* ARGB8888 is a draw target: fills keep their alpha; text is refused. */
static void test_session_argb_target(void)
{
    static uint8_t storage[ASTRA_RENDER_BUILDER_BYTES];
    static const char text[] = "A";
    AstraDrawListHeader *header = session_begin(64u, 32u);
    AstraDrawListCommand *fill = session_add(header, ASTRA_DRAW_LIST_FILL);
    AstraRenderBuilder builder;
    uint32_t destination;
    uint32_t next;

    fill->width = 8u;
    fill->height = 8u;
    fill->color = 0x40123456u;
    assert(astra_render_builder_init(&builder, storage,
                                     ASTRA_RENDER_BUILDER_BYTES, 3u));
    destination = astra_render_builder_surface_format_at(
        &builder, ASTRA_RENDER_BATCH_WORKSPACE_LIMIT, 64u * 32u * 4u, 64u,
        32u, 64u * 4u, ASTRA_RENDER_FORMAT_ARGB8888,
        ASTRA_RENDER_SURFACE_READ | ASTRA_RENDER_SURFACE_WRITE);
    assert(astra_render_builder_replay_range(
               &builder, destination, header, sizeof(session_list), NULL, 0u,
               &next) == ASTRA_RENDER_REPLAY_DONE);
    assert(be32(batch_command(storage, 0u) + 4u) >> 16 ==
               ASTRA_RENDER_OP_FILL &&
           be32(batch_command(storage, 0u) + 60u) == 0x40123456u);
    fill->operation = ASTRA_DRAW_LIST_TEXT;
    fill->width = 0u;
    fill->height = 0u;
    fill->font_height = 14u;
    fill->payload_offset = astra_draw_list_payload_offset(
        header->command_capacity);
    fill->payload_bytes = 1u;
    memcpy(session_list + fill->payload_offset, text, 1u);
    header->payload_bytes = 1u;
    assert(astra_render_builder_init(&builder, storage,
                                     ASTRA_RENDER_BUILDER_BYTES, 4u));
    destination = astra_render_builder_surface_format_at(
        &builder, ASTRA_RENDER_BATCH_WORKSPACE_LIMIT, 64u * 32u * 4u, 64u,
        32u, 64u * 4u, ASTRA_RENDER_FORMAT_ARGB8888,
        ASTRA_RENDER_SURFACE_READ | ASTRA_RENDER_SURFACE_WRITE);
    assert(astra_render_builder_replay_range(
               &builder, destination, header, sizeof(session_list), NULL, 0u,
               &next) == ASTRA_RENDER_REPLAY_INVALID);
}

/* Append n rectangles to the session payload and point command at them. */
static AstraDrawListRect *session_rects(AstraDrawListHeader *header,
                                        AstraDrawListCommand *command,
                                        uint32_t count)
{
    uint32_t offset = astra_draw_list_payload_offset(
                          header->command_capacity) + header->payload_bytes;
    uint32_t bytes = count * (uint32_t)sizeof(AstraDrawListRect);

    assert(header->payload_bytes + bytes <= SESSION_PAYLOAD_BYTES);
    command->payload_offset = offset;
    command->payload_bytes = bytes;
    header->payload_bytes += bytes;
    return (AstraDrawListRect *)(void *)(session_list + offset);
}

static void expect_fill_rect(const uint8_t *batch, uint32_t array,
                             uint32_t index, int32_t x, int32_t y,
                             uint32_t width, uint32_t height)
{
    const uint8_t *record = batch_record(
        batch, array + index * ASTRA_RENDER_FILL_RECT_BYTES);

    assert(be32(record + 0u) == ((uint32_t)(uint16_t)x << 16 | (uint16_t)y) &&
           be32(record + 4u) == (width << 16 | height) &&
           be32(record + 8u) == 0u && be32(record + 12u) == 0u);
}

/* FILL_RECTS: one hardware rectangle list, empties and culled records
   dropped, in the destination format. */
static void test_session_fill_rects_encoding(void)
{
    static uint8_t storage[ASTRA_RENDER_BUILDER_BYTES];
    AstraDrawListHeader *header = session_begin(320u, 200u);
    AstraDrawListCommand *fill = session_add(header,
                                             ASTRA_DRAW_LIST_FILL_RECTS);
    AstraDrawListRect *rects = session_rects(header, fill, 7u);
    AstraRenderBuilder builder;
    const uint8_t *render;
    uint32_t destination;
    uint32_t array;

    rects[0] = (AstraDrawListRect){ 5, 6, 1u, 1u };
    rects[1] = (AstraDrawListRect){ 7, 8, 0u, 4u };        /* empty */
    rects[2] = (AstraDrawListRect){ -40, 10, 43u, 3u };    /* crosses */
    rects[3] = (AstraDrawListRect){ 300, 150, 1u, 1u };    /* right of */
    rects[4] = (AstraDrawListRect){ 10, -5, 32767u, 5u };    /* above */
    rects[5] = (AstraDrawListRect){ 32767, 32767, 65535u, 65535u };
    rects[6] = (AstraDrawListRect){ 20, 20, 65535u, 2u };
    fill->color = 0x12ff0000u;
    fill->clip_left = 2u;
    fill->clip_right = 300u;
    fill->clip_bottom = 150u;
    assert(replay_session(&builder, storage, 3u, NULL, &destination) == 1u);
    assert(builder.command_count == 1u);
    render = batch_command(storage, 0u);
    /* Opcode 4, flags 0; words 6-7 clip, 8 destination, 9 zero, 10 array,
       11 count, 12 options, 13-14 zero, 15 shared color. */
    assert(be32(render + 4u) == (uint32_t)ASTRA_RENDER_OP_FILL_RECTS << 16);
    assert(be32(render + 24u) == ((uint32_t)2u << 16 | 0u) &&
           be32(render + 28u) == ((uint32_t)300u << 16 | 150u));
    assert(be32(render + 32u) == destination && be32(render + 36u) == 0u);
    array = be32(render + 40u);
    assert(array >= ASTRA_RENDER_BATCH_DATA_OFFSET &&
           (array & (ASTRA_RENDER_FILL_RECT_BYTES - 1u)) == 0u);
    assert(be32(render + 44u) == 3u && be32(render + 48u) == 0u &&
           be32(render + 52u) == 0u && be32(render + 56u) == 0u &&
           be32(render + 60u) == 0xf800u);
    expect_fill_rect(storage, array, 0u, 5, 6, 1u, 1u);
    expect_fill_rect(storage, array, 1u, -40, 10, 43u, 3u);
    expect_fill_rect(storage, array, 2u, 20, 20, 65535u, 2u);
    /* The array ends the data: culled records took no space. */
    assert(builder.data_cursor ==
           array - ASTRA_RENDER_BATCH_ARENA_OFFSET +
               3u * ASTRA_RENDER_FILL_RECT_BYTES);

    /* Opaque BLEND is the same list; nothing visible is no command. */
    fill->flags = BLEND_ALPHA;
    fill->color = 0xff00ff00u;
    assert(replay_session(&builder, storage, 4u, NULL, &destination) == 1u);
    assert(builder.command_count == 1u &&
           be32(batch_command(storage, 0u) + 60u) == 0x07e0u);
    rects[0].width = 0u;
    rects[2].x = -42;
    rects[6].y = 150;
    assert(replay_session(&builder, storage, 5u, NULL, &destination) == 1u);
    assert(builder.command_count == 0u);

    /* A clip wholly outside the destination draws nothing, and gives its
       data back. */
    rects[0].width = 1u;
    header->width = 640u;
    fill->clip_left = 400u;
    fill->clip_right = 500u;
    rects[0].x = 450;
    assert(replay_session(&builder, storage, 6u, NULL, &destination) == 1u);
    assert(builder.command_count == 0u);
}

/* Translucent BLEND rectangles are one blended list; ADD, MOD, and MUL,
   and destinations the list cannot blend, lower each as FILL does. */
static void test_session_fill_rects_blended(void)
{
    static uint8_t storage[ASTRA_RENDER_BUILDER_BYTES];
    AstraDrawListHeader *header = session_begin(320u, 200u);
    AstraDrawListCommand *fill = session_add(header,
                                             ASTRA_DRAW_LIST_FILL_RECTS);
    AstraDrawListRect *rects = session_rects(header, fill, 3u);
    AstraRenderBuilder builder;
    const uint8_t *render;
    uint32_t destination;
    uint32_t next;

    rects[0] = (AstraDrawListRect){ 10, 20, 100u, 50u };
    rects[1] = (AstraDrawListRect){ 1, 2, 0u, 3u };
    rects[2] = (AstraDrawListRect){ 30, 40, 5u, 6u };
    fill->flags = BLEND_ALPHA;
    fill->color = 0x40ff8000u;
    assert(replay_session(&builder, storage, 3u, NULL, &destination) == 1u);
    assert(builder.command_count == 1u);
    render = batch_command(storage, 0u);
    assert(be32(render + 4u) == (uint32_t)ASTRA_RENDER_OP_FILL_RECTS << 16 &&
           be32(render + 44u) == 2u &&
           be32(render + 48u) == ASTRA_RENDER_FILL_RECTS_OPTION_BLEND &&
           be32(render + 60u) == 0x40ff8000u);
    expect_fill_rect(storage, be32(render + 40u), 0u, 10, 20, 100u, 50u);
    expect_fill_rect(storage, be32(render + 40u), 1u, 30, 40, 5u, 6u);

    /* A pitch off eight bytes: one alpha blit per rectangle. */
    assert(astra_render_builder_init(&builder, storage,
                                     ASTRA_RENDER_BUILDER_BYTES, 4u));
    destination = astra_render_builder_surface_format_at(
        &builder, ASTRA_RENDER_BATCH_WORKSPACE_LIMIT, 644u * 200u, 320u,
        200u, 644u, ASTRA_RENDER_FORMAT_RGB565,
        ASTRA_RENDER_SURFACE_READ | ASTRA_RENDER_SURFACE_WRITE);
    assert(astra_render_builder_replay_range(
               &builder, destination, header, sizeof(session_list), NULL, 0u,
               &next) == ASTRA_RENDER_REPLAY_DONE);
    assert(builder.command_count == 2u);
    for (uint32_t index = 0u; index < 2u; ++index) {
        render = batch_command(storage, index);
        assert(be32(render + 4u) ==
               ((uint32_t)ASTRA_RENDER_OP_BLIT << 16 |
                ASTRA_RENDER_FLAG_BLIT_ALPHA));
        assert(be32(batch_record(storage,
                                 be32(batch_record(storage,
                                                   be32(render + 36u)) +
                                      8u))) == 0x40ff8000u);
    }
    assert(be32(batch_command(storage, 0u) + 48u) ==
               ((uint32_t)10u << 16 | 20u) &&
           be32(batch_command(storage, 1u) + 48u) ==
               ((uint32_t)30u << 16 | 40u));
    /* An indexed destination cannot blend at all. */
    assert(astra_render_builder_init(&builder, storage,
                                     ASTRA_RENDER_BUILDER_BYTES, 5u));
    destination = astra_render_builder_surface_format_at(
        &builder, ASTRA_RENDER_BATCH_WORKSPACE_LIMIT, 320u * 200u, 320u,
        200u, 320u, ASTRA_RENDER_FORMAT_INDEX8,
        ASTRA_RENDER_SURFACE_READ | ASTRA_RENDER_SURFACE_WRITE);
    assert(astra_render_builder_replay_range(
               &builder, destination, header, sizeof(session_list), NULL, 0u,
               &next) == ASTRA_RENDER_REPLAY_INVALID);

    /* Transparent draws nothing; ADD is the texture engine, per rectangle. */
    fill->color = 0x00ff8000u;
    assert(replay_session(&builder, storage, 6u, NULL, &destination) == 1u &&
           builder.command_count == 0u);
    fill->flags = ASTRA_DRAW_LIST_BLEND_FLAGS(ASTRA_DRAW_LIST_BLEND_ADD);
    assert(replay_session(&builder, storage, 7u, NULL, &destination) == 1u);
    assert(builder.command_count == 2u &&
           be32(batch_command(storage, 0u) + 4u) >> 16 ==
               ASTRA_RENDER_OP_TRIANGLES &&
           be32(batch_command(storage, 1u) + 4u) >> 16 ==
               ASTRA_RENDER_OP_TRIANGLES);
}

/* LINES: one hardware segment list, segments that miss the clip dropped,
   opaque only. */
static void test_session_lines(void)
{
    static uint8_t storage[ASTRA_RENDER_BUILDER_BYTES];
    enum { SEGMENTS = ASTRA_RENDER_MAX_LINE_SEGMENTS + 3u };
    AstraDrawListHeader *header = session_begin(320u, 200u);
    AstraDrawListCommand *lines = session_add(header, ASTRA_DRAW_LIST_LINES);
    AstraDrawListSegment *segments;
    AstraDrawListCommand good;
    AstraRenderBuilder builder;
    const uint8_t *render;
    const uint8_t *record;
    uint32_t destination;

    segments = (AstraDrawListSegment *)(void *)session_rects(header, lines,
                                                             4u);
    segments[0] = (AstraDrawListSegment){ -5, 7, 30, 9 };
    segments[1] = (AstraDrawListSegment){ -9, 1, -1, 150 };   /* left */
    segments[2] = (AstraDrawListSegment){ 3, 3, 3, 3 };       /* a dot */
    segments[3] = (AstraDrawListSegment){ 400, 0, 300, 5 };   /* right */
    lines->color = 0xff00ff00u;
    lines->clip_right = 300u;
    good = *lines;
    assert(replay_session(&builder, storage, 3u, NULL, &destination) == 1u);
    assert(builder.command_count == 1u);
    render = batch_command(storage, 0u);
    /* Opcode 262, flags 0; words 6-7 clip, 8 destination, 9 zero,
       10 array, 11 count, 12 options, 13-14 zero, 15 LINE color. */
    assert(be32(render + 4u) == (uint32_t)ASTRA_RENDER_OP_LINES << 16);
    assert(be32(render + 24u) == 0u &&
           be32(render + 28u) == ((uint32_t)300u << 16 | 200u) &&
           be32(render + 32u) == destination && be32(render + 36u) == 0u);
    assert((be32(render + 40u) & (ASTRA_RENDER_LINE_SEGMENT_BYTES - 1u)) ==
               0u &&
           be32(render + 44u) == 2u && be32(render + 48u) == 0u &&
           be32(render + 52u) == 0u && be32(render + 56u) == 0u &&
           be32(render + 60u) == 0x07e0u);
    record = batch_record(storage, be32(render + 40u));
    assert(be32(record + 0u) == ((uint32_t)(uint16_t)-5 << 16 | 7u) &&
           be32(record + 4u) == ((uint32_t)30u << 16 | 9u) &&
           be32(record + 8u) == 0u && be32(record + 12u) == 0u &&
           be32(record + 16u) == ((uint32_t)3u << 16 | 3u) &&
           be32(record + 20u) == ((uint32_t)3u << 16 | 3u));
    /* Opaque BLEND is the same list; translucent lines are refused, as
       LINE refuses them. */
    lines->flags = BLEND_ALPHA;
    assert(replay_session(&builder, storage, 4u, NULL, &destination) == 1u &&
           builder.command_count == 1u);
    lines->color = 0x80ffffffu;
    assert(replay_session(&builder, storage, 5u, NULL, &destination) ==
           UINT32_MAX);
    *lines = good;
    lines->flags = ASTRA_DRAW_LIST_BLEND_FLAGS(ASTRA_DRAW_LIST_BLEND_ADD);
    assert(replay_session(&builder, storage, 6u, NULL, &destination) ==
           UINT32_MAX);
    *lines = good;
    lines->payload_bytes = 12u;
    assert(replay_session(&builder, storage, 7u, NULL, &destination) ==
           UINT32_MAX);
    *lines = good;
    lines->width = 1u;
    assert(replay_session(&builder, storage, 8u, NULL, &destination) ==
           UINT32_MAX);
    /* 4096 segments per hardware command. */
    header = session_begin(320u, 200u);
    lines = session_add(header, ASTRA_DRAW_LIST_LINES);
    segments = (AstraDrawListSegment *)(void *)session_rects(header, lines,
                                                             SEGMENTS);
    for (uint32_t index = 0u; index < SEGMENTS; ++index)
        segments[index] = (AstraDrawListSegment){
            (int16_t)(index % 300u), 0, 0, (int16_t)(index % 200u) };
    lines->color = 0xffffffffu;
    assert(replay_session(&builder, storage, 9u, NULL, &destination) == 1u);
    assert(builder.command_count == 2u &&
           be32(batch_command(storage, 0u) + 44u) ==
               ASTRA_RENDER_MAX_LINE_SEGMENTS &&
           be32(batch_command(storage, 1u) + 44u) == 3u);
    record = batch_record(storage, be32(batch_command(storage, 1u) + 40u));
    assert(be32(record + 0u) ==
           (ASTRA_RENDER_MAX_LINE_SEGMENTS % 300u) << 16);
}

static void test_session_fill_rects_rejections(void)
{
    static uint8_t storage[ASTRA_RENDER_BUILDER_BYTES];
    AstraDrawListHeader *header = session_begin(320u, 200u);
    AstraDrawListCommand *fill = session_add(header,
                                             ASTRA_DRAW_LIST_FILL_RECTS);
    AstraDrawListRect *rects = session_rects(header, fill, 2u);
    AstraDrawListCommand good;
    AstraRenderBuilder builder;
    uint32_t destination;

    rects[0] = (AstraDrawListRect){ 1, 1, 1u, 1u };
    rects[1] = (AstraDrawListRect){ 2, 2, 1u, 1u };
    good = *fill;
    assert(replay_session(&builder, storage, 3u, NULL, &destination) == 1u);
    /* Only blend flags, one of the five modes, no rectangle or source of
       its own, a word-aligned whole number of records inside the payload. */
    fill->flags = ASTRA_DRAW_LIST_FLIP_X;
    assert(replay_session(&builder, storage, 4u, NULL, &destination) ==
           UINT32_MAX);
    *fill = good;
    fill->flags = ASTRA_DRAW_LIST_BLEND_FLAGS(ASTRA_DRAW_LIST_BLEND_MUL + 1u);
    assert(replay_session(&builder, storage, 5u, NULL, &destination) ==
           UINT32_MAX);
    *fill = good;
    fill->width = 1u;
    assert(replay_session(&builder, storage, 6u, NULL, &destination) ==
           UINT32_MAX);
    *fill = good;
    fill->source = 7u;
    assert(replay_session(&builder, storage, 7u, NULL, &destination) ==
           UINT32_MAX);
    *fill = good;
    fill->payload_bytes = 12u;
    assert(replay_session(&builder, storage, 8u, NULL, &destination) ==
           UINT32_MAX);
    *fill = good;
    fill->payload_offset += 2u;
    fill->payload_bytes = 8u;
    assert(replay_session(&builder, storage, 9u, NULL, &destination) ==
           UINT32_MAX);
    *fill = good;
    fill->payload_bytes = 0u;
    fill->payload_offset = 0u;
    assert(replay_session(&builder, storage, 10u, NULL, &destination) ==
           UINT32_MAX);
    *fill = good;
    fill->payload_bytes = 24u;
    assert(replay_session(&builder, storage, 11u, NULL, &destination) ==
           UINT32_MAX);
}

/* 4096 records per hardware command; a list that does not fit the ring
   moves whole to the next batch. */
static void test_session_fill_rects_split(void)
{
    static uint8_t storage[ASTRA_RENDER_BUILDER_BYTES];
    enum { RECTS = 2u * ASTRA_RENDER_MAX_FILL_RECTS + 1u };
    AstraDrawListHeader *header = session_begin(320u, 200u);
    AstraDrawListCommand *fill;
    AstraDrawListRect *rects;
    AstraRenderBuilder builder;
    uint32_t destination;
    uint32_t next = 0u;
    int result;

    for (uint32_t index = 0u; index < ASTRA_RENDER_RING_ENTRIES - 1u;
         ++index) {
        fill = session_add(header, ASTRA_DRAW_LIST_FILL);
        fill->width = 1u;
        fill->height = 1u;
    }
    fill = session_add(header, ASTRA_DRAW_LIST_FILL_RECTS);
    rects = session_rects(header, fill, RECTS);
    for (uint32_t index = 0u; index < RECTS; ++index)
        rects[index] = (AstraDrawListRect){ (int16_t)(index % 320u),
                                            (int16_t)(index % 200u), 1u,
                                            1u };
    assert(astra_render_builder_init(&builder, storage,
                                     ASTRA_RENDER_BUILDER_BYTES, 3u));
    destination = astra_render_builder_surface_at(
        &builder, ASTRA_RENDER_BATCH_WORKSPACE_LIMIT, 320u * 200u * 2u,
        320u, 200u);
    result = astra_render_builder_replay_range(
        &builder, destination, header, sizeof(session_list), NULL, 0u,
        &next);
    assert(result == ASTRA_RENDER_REPLAY_FULL &&
           next == ASTRA_RENDER_RING_ENTRIES - 1u &&
           builder.command_count == ASTRA_RENDER_RING_ENTRIES - 1u &&
           builder.failed == 0u);
    assert(astra_render_builder_init(&builder, storage,
                                     ASTRA_RENDER_BUILDER_BYTES, 4u));
    destination = astra_render_builder_surface_at(
        &builder, ASTRA_RENDER_BATCH_WORKSPACE_LIMIT, 320u * 200u * 2u,
        320u, 200u);
    result = astra_render_builder_replay_range(
        &builder, destination, header, sizeof(session_list), NULL, next,
        &next);
    assert(result == ASTRA_RENDER_REPLAY_DONE &&
           next == header->command_count && builder.command_count == 3u);
    for (uint32_t index = 0u; index < 3u; ++index) {
        const uint8_t *render = batch_command(storage, index);

        assert(be32(render + 4u) >> 16 == ASTRA_RENDER_OP_FILL_RECTS &&
               be32(render + 44u) ==
                   (index < 2u ? ASTRA_RENDER_MAX_FILL_RECTS : 1u));
        expect_fill_rect(storage, be32(render + 40u), 0u,
                         (int32_t)((index * ASTRA_RENDER_MAX_FILL_RECTS) %
                                   320u),
                         (int32_t)((index * ASTRA_RENDER_MAX_FILL_RECTS) %
                                   200u), 1u, 1u);
    }
}

int main(void)
{
    test_rgb565_color_round_trip();
    test_session_blit_lowers_to_hardware();
    test_session_blended_fill();
    test_session_list_splits_full_batches();
    test_session_targets();
    test_session_header_is_bounded();
    test_session_triangles_encoding();
    test_session_triangles_rejections();
    test_session_triangles_split();
    test_session_triangles_on_reference_model();
    test_session_engine_lowering();
    test_session_argb_target();
    test_session_fill_rects_encoding();
    test_session_fill_rects_blended();
    test_session_fill_rects_rejections();
    test_session_fill_rects_split();
    test_session_lines();
    test_batch_header_reserves_presentation_words();
    test_rgb565_upload_uses_hardware_blit();
    test_upload_reserve_ends_the_batch_data();
    test_window_scene_batch();
    test_builder_uses_the_capacity_it_is_given();
    test_clipped_drawing_and_blit();
    test_surface_clip_is_inherited();
    test_line_software_and_hardware();
    test_draw_list_clip_reaches_hardware();
    test_color_and_glyph();
    test_text();
    test_proportional_utf8_text();
    test_missing_mono_glyph_uses_replacement();
    test_font_advance_and_baseline();
    test_a8_software_oracle_blends_native_rgb565_channels();
    test_builder_chunks_the_hardware_glyph_limit();
    test_hardware_draw_list_batch();
    test_scanout_source_is_explicit();
    test_command_failure_reason();
    test_mono_draw_list();
    test_styled_text_uses_one_glyph_source();
    test_direct_styled_text();
    test_draw_list_copy_is_a_hardware_self_blit();
    test_text_box_scroll_uses_overlap_safe_copy();
    test_rounded_fill_has_no_overlap();
    puts("surface drawing tests passed");
    return 0;
}
