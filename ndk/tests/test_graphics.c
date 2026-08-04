#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <astra/graphics.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

_Static_assert(sizeof(AstraDisplay) == 4, "AstraDisplay ABI");
_Static_assert(sizeof(AstraSurface) == 4, "AstraSurface ABI");
_Static_assert(sizeof(AstraDrawList) == 4, "AstraDrawList ABI");
_Static_assert(sizeof(AstraPalette) == 4, "AstraPalette ABI");
_Static_assert(sizeof(AstraTileLayers) == 4, "AstraTileLayers ABI");
_Static_assert(sizeof(AstraSpriteSet) == 4, "AstraSpriteSet ABI");
_Static_assert(sizeof(AstraRasterProgram) == 4, "AstraRasterProgram ABI");
_Static_assert(sizeof(AstraFence) == 4, "AstraFence ABI");
_Static_assert(sizeof(AstraPointI32) == 8, "AstraPointI32 ABI");
_Static_assert(sizeof(AstraRectI32) == 16, "AstraRectI32 ABI");
_Static_assert(sizeof(AstraGraphicsInfo) == 44, "AstraGraphicsInfo ABI");
_Static_assert(sizeof(AstraSurfaceCreateInfo) == 40,
               "AstraSurfaceCreateInfo ABI");
_Static_assert(sizeof(AstraSurfaceInfo) == 40, "AstraSurfaceInfo ABI");
_Static_assert(sizeof(AstraDrawPaint) == 32, "AstraDrawPaint ABI");
_Static_assert(sizeof(AstraPattern8) == 16, "AstraPattern8 ABI");
_Static_assert(sizeof(AstraRasterChange) == 12, "AstraRasterChange ABI");
_Static_assert(sizeof(AstraDisplayStatus) == 32, "AstraDisplayStatus ABI");
_Static_assert(ASTRA_GRAPHICS_SPRITE_COUNT == 64, "sprite descriptor count");
_Static_assert(ASTRA_SPRITE_SOURCE_WIDTH_MAX == 128,
               "sprite source width");
_Static_assert(ASTRA_SPRITE_SOURCE_HEIGHT_MAX == 128,
               "sprite source height");
_Static_assert(ASTRA_SPRITE_DESTINATION_EXTENT_MAX == 1024,
               "sprite destination extent");
_Static_assert(ASTRA_SPRITE_PIXELS_PER_LINE == 8192,
               "sprite pixel budget");

static void test_initializers(void)
{
    AstraDisplay display = ASTRA_DISPLAY_INIT;
    AstraSurface surface = ASTRA_SURFACE_INIT;
    AstraDrawList list = ASTRA_DRAW_LIST_INIT;
    AstraPalette palette = ASTRA_PALETTE_INIT;
    AstraTileLayers tile_layers = ASTRA_TILE_LAYERS_INIT;
    AstraFence fence = ASTRA_FENCE_INIT;
    AstraGraphicsInfo info = ASTRA_GRAPHICS_INFO_INIT;
    AstraSurfaceCreateInfo create_info = ASTRA_SURFACE_CREATE_INFO_INIT;
    AstraSurfaceInfo surface_info = ASTRA_SURFACE_INFO_INIT;
    AstraDrawPaint paint = ASTRA_DRAW_PAINT_INIT;
    AstraTileLayerUpdate tile = ASTRA_TILE_LAYER_UPDATE_INIT;
    AstraDisplayStatus display_status = ASTRA_DISPLAY_STATUS_INIT;
    AstraPresentOptions present = ASTRA_PRESENT_OPTIONS_INIT;

    CHECK(display._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(surface._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(list._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(palette._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(tile_layers._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(fence._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(info.size == sizeof(info));
    CHECK(info.max_sprite_width == 0);
    CHECK(info.max_sprite_height == 0);
    CHECK(info.max_sprite_pixels_per_line == 0);
    CHECK(info.sprite_palette_bank_count == 0);
    CHECK(create_info.size == sizeof(create_info));
    CHECK(surface_info.size == sizeof(surface_info));
    CHECK(paint.size == sizeof(paint) && paint.foreground.alpha == 255);
    CHECK(tile.size == sizeof(tile) && tile.tile_size == 8);
    CHECK(display_status.size == sizeof(display_status));
    CHECK(present.size == sizeof(present));
    CHECK(present.palette == 0 && present.tile_layers == 0);
}

static void test_unavailable_objects(void)
{
    AstraDisplay display = ASTRA_DISPLAY_INIT;
    AstraSurface surface = ASTRA_SURFACE_INIT;
    AstraDrawList list = ASTRA_DRAW_LIST_INIT;
    AstraFence fence = ASTRA_FENCE_INIT;
    AstraGraphicsInfo info = ASTRA_GRAPHICS_INFO_INIT;
    AstraSurfaceCreateInfo create_info = ASTRA_SURFACE_CREATE_INFO_INIT;
    AstraSurfaceInfo surface_info = ASTRA_SURFACE_INFO_INIT;
    AstraRectI32 clip = { 0, 0, 320, 200 };
    AstraRectI32 rectangle = { 3, 4, 20, 10 };
    AstraDrawPaint paint = ASTRA_DRAW_PAINT_INIT;
    AstraPattern8 pattern = { UINT64_C(0xaa55aa55aa55aa55), 0, 0 };
    AstraPointI32 p0 = { 0, 0 };
    AstraPointI32 p1 = { 10, 7 };
    int signaled = 0;
    AstraResult completion = ASTRA_OK;

    CHECK(!astra_graphics_present());
    CHECK(astra_graphics_get_info(&info) == ASTRA_ERROR_NOT_PRESENT);
    info.size = 0;
    CHECK(astra_graphics_get_info(&info) == ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_display_open(&display) == ASTRA_ERROR_NOT_PRESENT);
    CHECK(astra_display_open(0) == ASTRA_ERROR_INVALID_ARGUMENT);

    create_info.flags = ASTRA_SURFACE_DRAW_TARGET;
    create_info.width = 320;
    create_info.height = 200;
    create_info.format = ASTRA_PIXEL_FORMAT_INDEX8;
    CHECK(astra_surface_create(&display, &create_info, &surface) ==
          ASTRA_ERROR_INVALID_HANDLE);
    create_info.format = 0;
    CHECK(astra_surface_create(&display, &create_info, &surface) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_surface_get_info(&surface, &surface_info) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_draw_list_create(&surface, &clip, &list) ==
          ASTRA_ERROR_INVALID_HANDLE);

    CHECK(astra_draw_line(&list, p0, p1, &paint) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_draw_rectangle(&list, &rectangle, 1, &paint) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_draw_circle(&list, p0, 12, 0, &paint) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_draw_ellipse(&list, p0, 12, 7, 1, &paint) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_draw_pattern_fill(&list, &rectangle, &pattern, &paint) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_draw_flood_fill(&list, p0, &paint) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_draw_circle(&list, p0, 32768u, 0, &paint) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_draw_rectangle(&list, &rectangle, 2, &paint) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    paint.reserved[0] = 1;
    CHECK(astra_draw_line(&list, p0, p1, &paint) ==
          ASTRA_ERROR_INVALID_ARGUMENT);

    CHECK(astra_draw_submit(&list, &fence) == ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_fence_poll(&fence, &signaled, &completion) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_fence_wait(&fence, 0, &completion) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_fence_poll(0, &signaled, &completion) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
}

static void test_sprite_and_raster_validation(void)
{
    AstraDisplay display = ASTRA_DISPLAY_INIT;
    AstraSurface source = ASTRA_SURFACE_INIT;
    AstraSpriteSet sprites = ASTRA_SPRITE_SET_INIT;
    AstraRasterProgram program = ASTRA_RASTER_PROGRAM_INIT;
    AstraSpriteUpdate update = ASTRA_SPRITE_UPDATE_INIT;
    AstraRasterChange changes[2] = {
        { 20, 0, ASTRA_RASTER_TARGET_BACKDROP, 0, 0x1234 },
        { 40, 0, ASTRA_RASTER_TARGET_TILE0_SCROLL, 0, 0x00010002 }
    };
    uint32_t extent;

    update.source = &source;
    update.source_rect = (AstraRectI32){ 0, 0, 16, 16 };
    update.destination = (AstraPointI32){ 4, 5 };
    update.destination_width = 16;
    update.destination_height = 16;
    update.flags = ASTRA_SPRITE_VISIBLE;
    update.priority = 3;
    update.palette_bank = 2;

    CHECK(astra_sprite_set_create(&display, &sprites) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_sprite_set_update(&sprites, 0, &update) ==
          ASTRA_ERROR_INVALID_HANDLE);
    for (extent = 1; extent <= 128; ++extent) {
        update.source_rect.width = extent;
        update.source_rect.height = 129u - extent;
        CHECK(astra_sprite_set_update(&sprites, 63, &update) ==
              ASTRA_ERROR_INVALID_HANDLE);
    }
    update.source_rect = (AstraRectI32){ 0, 0, 0, 1 };
    CHECK(astra_sprite_set_update(&sprites, 0, &update) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    update.source_rect = (AstraRectI32){ 0, 0, 129, 1 };
    CHECK(astra_sprite_set_update(&sprites, 0, &update) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    update.source_rect = (AstraRectI32){ 0, 0, 1, 0 };
    CHECK(astra_sprite_set_update(&sprites, 0, &update) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    update.source_rect = (AstraRectI32){ 0, 0, 1, 129 };
    CHECK(astra_sprite_set_update(&sprites, 0, &update) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    update.source_rect = (AstraRectI32){ 0, 0, 128, 128 };
    update.destination_width = 1024;
    update.destination_height = 1024;
    update.priority = 255;
    update.transparent_index = 255;
    CHECK(astra_sprite_set_update(&sprites, 63, &update) ==
          ASTRA_ERROR_INVALID_HANDLE);
    update.destination_width = 0;
    CHECK(astra_sprite_set_update(&sprites, 0, &update) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    update.destination_width = 1025;
    CHECK(astra_sprite_set_update(&sprites, 0, &update) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    update.destination_width = 16;
    update.destination_height = 1025;
    CHECK(astra_sprite_set_update(&sprites, 0, &update) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    update.destination_height = 16;
    update.palette_bank = 16;
    CHECK(astra_sprite_set_update(&sprites, 0, &update) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    update.palette_bank = 0;
    CHECK(astra_sprite_set_update(&sprites, 64, 0) ==
          ASTRA_ERROR_INVALID_ARGUMENT);

    CHECK(astra_raster_program_create(&display, changes, 2, &program) ==
          ASTRA_ERROR_INVALID_HANDLE);
    changes[1].beam_y = 10;
    CHECK(astra_raster_program_create(&display, changes, 2, &program) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
}

static void test_palette_tile_and_status_validation(void)
{
    AstraDisplay display = ASTRA_DISPLAY_INIT;
    AstraSurface map = ASTRA_SURFACE_INIT;
    AstraSurface tiles = ASTRA_SURFACE_INIT;
    AstraPalette palette = ASTRA_PALETTE_INIT;
    AstraTileLayers layers = ASTRA_TILE_LAYERS_INIT;
    AstraColorRGBA8 colors[2] = {
        { 0, 0, 0, 255 }, { 255, 128, 64, 255 }
    };
    AstraTileLayerUpdate update = ASTRA_TILE_LAYER_UPDATE_INIT;
    AstraDisplayStatus status = ASTRA_DISPLAY_STATUS_INIT;

    CHECK(astra_palette_create(&display, colors, 2, &palette) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_palette_update(&palette, 254, colors, 2) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_palette_update(&palette, 255, colors, 2) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    colors[1].alpha = 128;
    CHECK(astra_palette_create(&display, colors, 2, &palette) ==
          ASTRA_ERROR_UNSUPPORTED);
    colors[1].alpha = 255;

    CHECK(astra_tile_layers_create(&display, &layers) ==
          ASTRA_ERROR_INVALID_HANDLE);
    update.map = &map;
    update.tiles = &tiles;
    update.flags = ASTRA_TILE_LAYER_VISIBLE | ASTRA_TILE_LAYER_WRAP_X;
    update.tile_size = 16;
    update.transparent_index = 3;
    CHECK(astra_tile_layers_update(&layers, 1, &update) ==
          ASTRA_ERROR_INVALID_HANDLE);
    update.tile_size = 12;
    CHECK(astra_tile_layers_update(&layers, 1, &update) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_tile_layers_update(&layers, 2, 0) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_display_get_status(&display, &status) ==
          ASTRA_ERROR_INVALID_HANDLE);
    status.size = 0;
    CHECK(astra_display_get_status(&display, &status) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
}

static void test_empty_cleanup(void)
{
    ASTRA_AUTO_DISPLAY(display);
    ASTRA_AUTO_SURFACE(surface);
    ASTRA_AUTO_DRAW_LIST(list);
    ASTRA_AUTO_PALETTE(palette);
    ASTRA_AUTO_TILE_LAYERS(tile_layers);
    ASTRA_AUTO_SPRITE_SET(sprites);
    ASTRA_AUTO_RASTER_PROGRAM(program);
    ASTRA_AUTO_FENCE(fence);

    CHECK(display._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(surface._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(list._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(palette._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(tile_layers._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(sprites._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(program._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(fence._private_handle == ASTRA_INVALID_HANDLE);
}

int main(void)
{
    test_initializers();
    test_unavailable_objects();
    test_sprite_and_raster_validation();
    test_palette_tile_and_status_validation();
    test_empty_cleanup();
    puts("PASS Astra NDK graphics contract and unavailable backend");
    return 0;
}
