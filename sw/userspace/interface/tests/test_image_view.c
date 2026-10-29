#include <astra/image_view.h>

#include <assert.h>

static AstraRectI32 clip_seen, source_seen, frame_seen;
static const AstraSurface *image_seen;
static unsigned clips, blits;

int astra_surface_draw_blit(AstraSurfaceView *surface,
    const AstraSurface *image, const AstraRectI32 *source,
    const AstraRectI32 *frame, const AstraBlitOptions *options)
{
    assert(surface != NULL && options != NULL);
    assert(options->blend == ASTRA_BLEND_ALPHA);
    image_seen = image;
    source_seen = *source;
    frame_seen = *frame;
    ++blits;
    return 1;
}

AstraResult astra_draw_list_set_clip(AstraDrawList *list,
    const AstraRectI32 *clip)
{
    assert(list != NULL);
    clip_seen = *clip;
    ++clips;
    return ASTRA_OK;
}

AstraResult astra_draw_blit(AstraDrawList *list,
    const AstraSurface *image, const AstraRectI32 *source,
    const AstraRectI32 *frame, const AstraBlitOptions *options)
{
    assert(list != NULL && options != NULL);
    assert(options->blend == ASTRA_BLEND_ALPHA);
    image_seen = image;
    source_seen = *source;
    frame_seen = *frame;
    ++blits;
    return ASTRA_OK;
}

int main(void)
{
    AstraDrawList list = ASTRA_DRAW_LIST_INIT;
    AstraSurface image = ASTRA_SURFACE_INIT;
    AstraImageViewInfo view = ASTRA_IMAGE_VIEW_INFO_INIT;
    image._private_handle = 1u;
    image._private_flags = ASTRA_SURFACE_DRAW_SOURCE;
    view.image = &image;
    view.source_rect = (AstraRectI32){0, 0, 320u, 200u};
    view.frame = (AstraRectI32){32, 48, 320u, 200u};
    view.clip = (AstraRectI32){0, 40, 240u, 240u};
    assert(astra_interface_image_view_render(&list, &view) == ASTRA_OK);
    assert(clips == 1u && blits == 1u && image_seen == &image);
    assert(clip_seen.y == 40 && clip_seen.height == 240u);
    assert(source_seen.width == 320u && frame_seen.x == 32);
    view.clip = (AstraRectI32){400, 400, 20u, 20u};
    assert(astra_interface_image_view_render(&list, &view) == ASTRA_OK);
    assert(clips == 1u && blits == 1u);
    view.frame.width = 0u;
    assert(astra_interface_image_view_render(&list, &view) ==
        ASTRA_ERROR_INVALID_ARGUMENT);
    view.frame.width = 320u;
    view.reserved[0] = 1u;
    assert(astra_interface_image_view_render(&list, &view) ==
        ASTRA_ERROR_INVALID_ARGUMENT);
    {
        AstraImageViewModel retained;
        AstraSurfaceView surface = {0};
        AstraControlFrame bounds = {10, 20, 200u, 100u};
        uint64_t commands = 0u, payload = 0u;

        image._private_window = 7u;
        image._private_id = 2u;
        image._private_flags = ASTRA_SURFACE_DRAW_SOURCE;
        image._private_width = 320u;
        image._private_height = 200u;
        assert(astra_interface_image_view_model_init(&retained, &image,
            &view.source_rect, ASTRA_IMAGE_VIEW_FIT) == ASTRA_OK);
        assert(retained.view.draw(retained.view.context, &bounds, NULL,
            &commands, &payload) == ASTRA_OK);
        assert(commands == 1u && payload == 0u);
        assert(retained.view.draw(retained.view.context, &bounds, &surface,
            NULL, NULL) == ASTRA_OK);
        assert(frame_seen.x == 30 && frame_seen.y == 20 &&
               frame_seen.width == 160u && frame_seen.height == 100u);
        bounds = (AstraControlFrame){10, 20, 320u, 90u};
        assert(retained.view.draw(retained.view.context, &bounds, &surface,
            NULL, NULL) == ASTRA_OK);
        assert(frame_seen.x == 98 && frame_seen.y == 20 &&
               frame_seen.width == 144u && frame_seen.height == 90u);
        image._private_handle = ASTRA_INVALID_HANDLE;
        assert(retained.view.draw(retained.view.context, &bounds, NULL,
            &commands, &payload) == ASTRA_ERROR_INVALID_ARGUMENT);
        image._private_handle = 1u;
        bounds = (AstraControlFrame){10, 20, 200u, 100u};
        retained.mode = ASTRA_IMAGE_VIEW_FILL;
        assert(retained.view.draw(retained.view.context, &bounds, &surface,
            NULL, NULL) == ASTRA_OK);
        assert(source_seen.x == 0 && source_seen.y == 20 &&
               source_seen.width == 320u && source_seen.height == 160u);
        retained.mode = ASTRA_IMAGE_VIEW_ACTUAL;
        assert(retained.view.draw(retained.view.context, &bounds, &surface,
            NULL, NULL) == ASTRA_OK);
        assert(frame_seen.x == -50 && frame_seen.y == -30);
        assert(astra_interface_image_view_model_init(&retained, &image,
            &view.source_rect, ASTRA_IMAGE_VIEW_PIXEL4) == ASTRA_OK);
        assert(retained.view.intrinsic_width == 1280u &&
               retained.view.intrinsic_height == 800u);
        image._private_width = UINT16_MAX;
        assert(astra_interface_image_view_model_init(&retained, &image,
            &(AstraRectI32){INT16_MAX + 1, 0, 1u, 1u},
            ASTRA_IMAGE_VIEW_FIT) == ASTRA_ERROR_INVALID_ARGUMENT);
    }
    return 0;
}
