#include <astra/image_view.h>

static int overlaps(const AstraRectI32 *a, const AstraRectI32 *b)
{
    return (int64_t)a->x < (int64_t)b->x + b->width &&
        (int64_t)b->x < (int64_t)a->x + a->width &&
        (int64_t)a->y < (int64_t)b->y + b->height &&
        (int64_t)b->y < (int64_t)a->y + a->height;
}

AstraResult astra_interface_image_view_render(AstraDrawList *list,
    const AstraImageViewInfo *info)
{
    AstraBlitOptions blend = ASTRA_BLIT_OPTIONS_INIT;
    AstraResult result;

    if (list == NULL || info == NULL || info->size < sizeof(*info) ||
        info->image == NULL ||
        info->image->_private_handle == ASTRA_INVALID_HANDLE ||
        info->frame.width == 0u || info->frame.height == 0u ||
        info->clip.width == 0u || info->clip.height == 0u ||
        info->source_rect.width == 0u || info->source_rect.height == 0u ||
        info->reserved[0] != 0u || info->reserved[1] != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!overlaps(&info->frame, &info->clip))
        return ASTRA_OK;
    result = astra_draw_list_set_clip(list, &info->clip);
    if (result != ASTRA_OK)
        return result;
    blend.blend = ASTRA_BLEND_ALPHA;
    return astra_draw_blit(list, info->image, &info->source_rect,
        &info->frame, &blend);
}
