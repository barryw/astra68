#include <astra/image_view.h>

#include <assert.h>

static AstraRectI32 clip_seen, source_seen, frame_seen;
static const AstraSurface *image_seen;
static unsigned clips, blits;

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
    return 0;
}
