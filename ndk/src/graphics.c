#include <astra/graphics.h>
#include <astra/bytes.h>

/* The direct-MMIO NDK deliberately does not expose unsafe graphics MMIO. */

#define SURFACE_FLAGS (ASTRA_SURFACE_SCANOUT | ASTRA_SURFACE_DRAW_TARGET | \
                       ASTRA_SURFACE_DRAW_SOURCE | ASTRA_SURFACE_TILE_MAP | \
                       ASTRA_SURFACE_CPU_READ | ASTRA_SURFACE_CPU_WRITE)
#define DRAW_FLAGS ASTRA_DRAW_OPAQUE_BACKGROUND
#define SPRITE_FLAGS (ASTRA_SPRITE_VISIBLE | ASTRA_SPRITE_FLIP_X | \
                      ASTRA_SPRITE_FLIP_Y | \
                      ASTRA_SPRITE_BEHIND_FRAMEBUFFER | \
                      ASTRA_SPRITE_COLLISION_ENABLE)
#define TILE_FLAGS (ASTRA_TILE_LAYER_VISIBLE | \
                    ASTRA_TILE_LAYER_ABOVE_FRAMEBUFFER | \
                    ASTRA_TILE_LAYER_WRAP_X | ASTRA_TILE_LAYER_WRAP_Y)

static int pixel_format_valid(uint16_t format)
{
    return format >= ASTRA_PIXEL_FORMAT_INDEX4 &&
           format <= ASTRA_SURFACE_FORMAT_TILE16;
}

static int surface_usage_valid(const AstraSurfaceCreateInfo *info)
{
    uint32_t direct_only = ASTRA_SURFACE_SCANOUT |
                           ASTRA_SURFACE_DRAW_TARGET;

    if ((info->flags & direct_only) != 0 &&
        info->format != ASTRA_PIXEL_FORMAT_INDEX8 &&
        info->format != ASTRA_PIXEL_FORMAT_RGB565)
        return 0;
    if ((info->flags & ASTRA_SURFACE_TILE_MAP) != 0)
        return info->format == ASTRA_SURFACE_FORMAT_TILE16;
    return info->format != ASTRA_SURFACE_FORMAT_TILE16;
}

static int rect_valid(const AstraRectI32 *rectangle)
{
    return rectangle != 0 && rectangle->width != 0 &&
           rectangle->height != 0 && rectangle->width <= 32767u &&
           rectangle->height <= 32767u;
}

static int sprite_source_rect_valid(const AstraRectI32 *rectangle)
{
    return rect_valid(rectangle) && rectangle->x >= 0 && rectangle->y >= 0 &&
           rectangle->width <= ASTRA_SPRITE_SOURCE_WIDTH_MAX &&
           rectangle->height <= ASTRA_SPRITE_SOURCE_HEIGHT_MAX;
}

static int paint_valid(const AstraDrawPaint *paint)
{
    return paint != 0 && paint->size >= sizeof(*paint) &&
           (paint->flags & ~DRAW_FLAGS) == 0 &&
           astra_words_zero(paint->reserved, 4);
}

static int text_paint_valid(const AstraTextPaint *paint)
{
    return paint != 0 && paint->size >= sizeof(*paint) &&
           (paint->flags & ~ASTRA_TEXT_PAINT_OPAQUE_BACKGROUND) == 0 &&
           paint->embedded_color_policy <= ASTRA_TEXT_EMBEDDED_COLOR_REJECT &&
           paint->reserved16 == 0 && astra_words_zero(paint->reserved, 5);
}

static int empty_handle(AstraHandle handle)
{
    return handle == ASTRA_INVALID_HANDLE;
}

int astra_graphics_present(void)
{
    return 0;
}

AstraResult astra_graphics_get_info(AstraGraphicsInfo *info)
{
    if (info == 0 || info->size < sizeof(*info))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_NOT_PRESENT;
}

AstraResult astra_display_open(AstraDisplay *display)
{
    if (display == 0 || !empty_handle(display->_private_handle))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_NOT_PRESENT;
}

AstraResult astra_display_close(AstraDisplay *display)
{
    if (display == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_surface_create(const AstraDisplay *display,
                                 const AstraSurfaceCreateInfo *create_info,
                                 AstraSurface *surface)
{
    if (display == 0 || create_info == 0 ||
        create_info->size < sizeof(*create_info) ||
        create_info->width == 0 || create_info->height == 0 ||
        create_info->width > 32767u || create_info->height > 32767u ||
        !pixel_format_valid(create_info->format) ||
        !surface_usage_valid(create_info) ||
        (create_info->flags & ~SURFACE_FLAGS) != 0 ||
        create_info->flags == 0 || create_info->reserved16 != 0 ||
        !astra_words_zero(create_info->reserved, 5) || surface == 0 ||
        !empty_handle(surface->_private_handle))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_surface_get_info(const AstraSurface *surface,
                                   AstraSurfaceInfo *info)
{
    if (surface == 0 || info == 0 || info->size < sizeof(*info))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_surface_close(AstraSurface *surface)
{
    if (surface == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_draw_list_create(const AstraSurface *destination,
                                   const AstraRectI32 *clip,
                                   AstraDrawList *draw_list)
{
    if (destination == 0 || !rect_valid(clip) || draw_list == 0 ||
        !empty_handle(draw_list->_private_handle))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_draw_list_reset(AstraDrawList *draw_list)
{
    if (draw_list == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_draw_list_close(AstraDrawList *draw_list)
{
    if (draw_list == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_draw_line(AstraDrawList *draw_list,
                            AstraPointI32 p0,
                            AstraPointI32 p1,
                            const AstraDrawPaint *paint)
{
    (void)p0;
    (void)p1;
    if (draw_list == 0 || !paint_valid(paint))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_draw_rectangle(AstraDrawList *draw_list,
                                 const AstraRectI32 *rectangle,
                                 int filled,
                                 const AstraDrawPaint *paint)
{
    if (draw_list == 0 || !rect_valid(rectangle) ||
        (filled != 0 && filled != 1) || !paint_valid(paint))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_draw_circle(AstraDrawList *draw_list,
                              AstraPointI32 center,
                              uint32_t radius,
                              int filled,
                              const AstraDrawPaint *paint)
{
    (void)center;
    if (draw_list == 0 || radius > 32767u ||
        (filled != 0 && filled != 1) || !paint_valid(paint))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_draw_ellipse(AstraDrawList *draw_list,
                               AstraPointI32 center,
                               uint32_t radius_x,
                               uint32_t radius_y,
                               int filled,
                               const AstraDrawPaint *paint)
{
    (void)center;
    if (draw_list == 0 || radius_x > 32767u || radius_y > 32767u ||
        (filled != 0 && filled != 1) || !paint_valid(paint))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_draw_pattern_fill(AstraDrawList *draw_list,
                                    const AstraRectI32 *rectangle,
                                    const AstraPattern8 *pattern,
                                    const AstraDrawPaint *paint)
{
    if (draw_list == 0 || !rect_valid(rectangle) || pattern == 0 ||
        !paint_valid(paint))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_draw_flood_fill(AstraDrawList *draw_list,
                                  AstraPointI32 seed,
                                  const AstraDrawPaint *paint)
{
    (void)seed;
    if (draw_list == 0 || !paint_valid(paint))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_draw_text_layout(AstraDrawList *draw_list,
                                   const AstraTextLayout *layout,
                                   AstraPointI32 origin,
                                   const AstraTextPaint *paint)
{
    (void)origin;
    if (draw_list == 0 || layout == 0 || !text_paint_valid(paint))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_draw_submit(AstraDrawList *draw_list, AstraFence *fence)
{
    if (draw_list == 0 || fence == 0 ||
        !empty_handle(fence->_private_handle))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_palette_create(const AstraDisplay *display,
                                 const AstraColorRGBA8 *entries,
                                 uint32_t entry_count,
                                 AstraPalette *palette)
{
    uint32_t index;

    if (display == 0 || entries == 0 || entry_count == 0 ||
        entry_count > 256u || palette == 0 ||
        !empty_handle(palette->_private_handle))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (index = 0; index < entry_count; ++index) {
        if (entries[index].alpha != 255u)
            return ASTRA_ERROR_UNSUPPORTED;
    }
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_palette_update(AstraPalette *palette,
                                 uint32_t first_entry,
                                 const AstraColorRGBA8 *entries,
                                 uint32_t entry_count)
{
    uint32_t index;

    if (palette == 0 || entries == 0 || entry_count == 0 ||
        first_entry >= 256u || entry_count > 256u - first_entry)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (index = 0; index < entry_count; ++index) {
        if (entries[index].alpha != 255u)
            return ASTRA_ERROR_UNSUPPORTED;
    }
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_palette_close(AstraPalette *palette)
{
    if (palette == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_tile_layers_create(const AstraDisplay *display,
                                     AstraTileLayers *tile_layers)
{
    if (display == 0 || tile_layers == 0 ||
        !empty_handle(tile_layers->_private_handle))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_tile_layers_update(AstraTileLayers *tile_layers,
                                     uint32_t index,
                                     const AstraTileLayerUpdate *update)
{
    if (tile_layers == 0 || index >= 2u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (update != 0 &&
        (update->size < sizeof(*update) || update->map == 0 ||
         update->tiles == 0 || (update->flags & ~TILE_FLAGS) != 0 ||
         (update->tile_size != 8u && update->tile_size != 16u) ||
         update->transparent_index > 15u || update->reserved8 != 0 ||
         update->scroll_x < -32768 || update->scroll_x > 32767 ||
         update->scroll_y < -32768 || update->scroll_y > 32767 ||
         !astra_words_zero(update->reserved, 4)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_tile_layers_close(AstraTileLayers *tile_layers)
{
    if (tile_layers == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_sprite_set_create(const AstraDisplay *display,
                                    AstraSpriteSet *sprite_set)
{
    if (display == 0 || sprite_set == 0 ||
        !empty_handle(sprite_set->_private_handle))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_sprite_set_update(AstraSpriteSet *sprite_set,
                                    uint32_t index,
                                    const AstraSpriteUpdate *update)
{
    if (sprite_set == 0 || index >= ASTRA_GRAPHICS_SPRITE_COUNT)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (update != 0 &&
        (update->size < sizeof(*update) || update->source == 0 ||
         !sprite_source_rect_valid(&update->source_rect) ||
         update->destination.x < -32768 ||
         update->destination.x > 32767 ||
         update->destination.y < -32768 ||
         update->destination.y > 32767 ||
         update->destination_width == 0u ||
         update->destination_width > ASTRA_SPRITE_DESTINATION_EXTENT_MAX ||
         update->destination_height == 0u ||
         update->destination_height > ASTRA_SPRITE_DESTINATION_EXTENT_MAX ||
         (update->flags & ~SPRITE_FLAGS) != 0 ||
         update->palette_bank >= ASTRA_SPRITE_PALETTE_BANK_COUNT ||
         !astra_words_zero(update->reserved, 2)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_sprite_set_close(AstraSpriteSet *sprite_set)
{
    if (sprite_set == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_raster_program_create(const AstraDisplay *display,
                                        const AstraRasterChange *changes,
                                        uint32_t change_count,
                                        AstraRasterProgram *program)
{
    uint32_t index;

    if (display == 0 || changes == 0 || change_count == 0 ||
        change_count > 2047u || program == 0 ||
        !empty_handle(program->_private_handle))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (index = 0; index < change_count; ++index) {
        if (changes[index].target < ASTRA_RASTER_TARGET_BACKDROP ||
            changes[index].target > ASTRA_RASTER_TARGET_TILE1_SCROLL ||
            changes[index].beam_x >= 720u || changes[index].beam_y >= 480u)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if (index != 0 &&
            (changes[index].beam_y < changes[index - 1].beam_y ||
             (changes[index].beam_y == changes[index - 1].beam_y &&
              changes[index].beam_x < changes[index - 1].beam_x)))
            return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_raster_program_close(AstraRasterProgram *program)
{
    if (program == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_display_present_surface(const AstraDisplay *display,
                                          const AstraSurface *surface,
                                          const AstraPresentOptions *options,
                                          AstraFence *fence)
{
    if (display == 0 || surface == 0 || fence == 0 ||
        !empty_handle(fence->_private_handle) ||
        (options != 0 &&
         (options->size < sizeof(*options) || options->flags != 0 ||
          !astra_words_zero(options->reserved, 4))))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_display_get_status(const AstraDisplay *display,
                                     AstraDisplayStatus *status)
{
    if (display == 0 || status == 0 || status->size < sizeof(*status))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_fence_poll(const AstraFence *fence,
                             int *signaled,
                             AstraResult *completion_result)
{
    if (fence == 0 || signaled == 0 || completion_result == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_fence_wait(const AstraFence *fence,
                             uint32_t timeout_ms,
                             AstraResult *completion_result)
{
    (void)timeout_ms;
    if (fence == 0 || completion_result == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_fence_close(AstraFence *fence)
{
    if (fence == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

#define DEFINE_CLEANUP(function_name, type_name, close_function) \
    void function_name(type_name *value) \
    { \
        if (value != 0 && value->_private_handle != ASTRA_INVALID_HANDLE) { \
            AstraResult ignored = close_function(value); \
            (void)ignored; \
        } \
    }

DEFINE_CLEANUP(astra_display_cleanup, AstraDisplay, astra_display_close)
DEFINE_CLEANUP(astra_surface_cleanup, AstraSurface, astra_surface_close)
DEFINE_CLEANUP(astra_draw_list_cleanup, AstraDrawList, astra_draw_list_close)
DEFINE_CLEANUP(astra_palette_cleanup, AstraPalette, astra_palette_close)
DEFINE_CLEANUP(astra_tile_layers_cleanup, AstraTileLayers,
               astra_tile_layers_close)
DEFINE_CLEANUP(astra_sprite_set_cleanup, AstraSpriteSet, astra_sprite_set_close)
DEFINE_CLEANUP(astra_raster_program_cleanup, AstraRasterProgram,
               astra_raster_program_close)
DEFINE_CLEANUP(astra_fence_cleanup, AstraFence, astra_fence_close)
