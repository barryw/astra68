#include <astra/image_view.h>

#include <limits.h>

static int overlaps(const AstraRectI32 *a, const AstraRectI32 *b)
{
    return (int64_t)a->x < (int64_t)b->x + b->width &&
        (int64_t)b->x < (int64_t)a->x + a->width &&
        (int64_t)a->y < (int64_t)b->y + b->height &&
        (int64_t)b->y < (int64_t)a->y + a->height;
}

static AstraResult image_view_draw(void *context,
    const AstraControlFrame *frame, AstraSurfaceView *surface,
    uint64_t *commands, uint64_t *payload)
{
    const AstraImageViewModel *model = context;
    AstraRectI32 source;
    AstraRectI32 destination;
    AstraBlitOptions options = ASTRA_BLIT_OPTIONS_INIT;
    uint64_t width, height;

    if (model == NULL || frame == NULL || model->image == NULL ||
        model->image->_private_handle == ASTRA_INVALID_HANDLE ||
        model->image->_private_window == 0u ||
        model->image->_private_id == 0u ||
        (model->image->_private_flags & ASTRA_SURFACE_DRAW_SOURCE) == 0u ||
        model->mode > ASTRA_IMAGE_VIEW_PIXEL4 ||
        model->reserved[0] != 0u || model->reserved[1] != 0u ||
        model->source_rect.width == 0u ||
        model->source_rect.height == 0u ||
        model->source_rect.x < 0 || model->source_rect.y < 0 ||
        (uint64_t)model->source_rect.x + model->source_rect.width >
            model->image->_private_width ||
        (uint64_t)model->source_rect.y + model->source_rect.height >
            model->image->_private_height)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (frame->width == 0u || frame->height == 0u)
        return ASTRA_OK;
    if (surface == NULL) {
        if (commands == NULL || payload == NULL || *commands == UINT64_MAX)
            return ASTRA_ERROR_NO_RESOURCES;
        ++*commands;
        return ASTRA_OK;
    }
    source = model->source_rect;
    destination = (AstraRectI32){frame->x, frame->y,
                                  frame->width, frame->height};
    if (model->mode == ASTRA_IMAGE_VIEW_FIT) {
        width = source.width;
        height = source.height;
        if (width * frame->height > height * frame->width) {
            height = height * frame->width / width;
            width = frame->width;
        } else {
            width = width * frame->height / height;
            height = frame->height;
        }
        if (width == 0u) width = 1u;
        if (height == 0u) height = 1u;
        destination.x += (frame->width - (uint32_t)width) / 2;
        destination.y += (frame->height - (uint32_t)height) / 2;
        destination.width = (uint32_t)width;
        destination.height = (uint32_t)height;
    } else if (model->mode == ASTRA_IMAGE_VIEW_FILL) {
        if ((uint64_t)source.width * frame->height >
            (uint64_t)source.height * frame->width) {
            uint32_t crop = (uint32_t)((uint64_t)source.height *
                                        frame->width / frame->height);
            if (crop == 0u) crop = 1u;
            source.x += (source.width - crop) / 2;
            source.width = crop;
        } else {
            uint32_t crop = (uint32_t)((uint64_t)source.width *
                                        frame->height / frame->width);
            if (crop == 0u) crop = 1u;
            source.y += (source.height - crop) / 2;
            source.height = crop;
        }
    } else {
        uint32_t scale = model->mode == ASTRA_IMAGE_VIEW_PIXEL4 ? 4u : 1u;
        width = (uint64_t)source.width * scale;
        height = (uint64_t)source.height * scale;
        if (width > INT32_MAX || height > INT32_MAX)
            return ASTRA_ERROR_NO_RESOURCES;
        destination.x += ((int64_t)frame->width - (int64_t)width) / 2;
        destination.y += ((int64_t)frame->height - (int64_t)height) / 2;
        destination.width = (uint32_t)width;
        destination.height = (uint32_t)height;
    }
    options.blend = ASTRA_BLEND_ALPHA;
    return astra_surface_draw_blit(surface, model->image, &source,
                                  &destination, &options) ? ASTRA_OK :
                                         ASTRA_ERROR_NO_RESOURCES;
}

AstraResult astra_interface_image_view_model_init(
    AstraImageViewModel *model, const AstraSurface *image,
    const AstraRectI32 *source_rect, uint32_t mode)
{
    if (model == NULL || image == NULL || source_rect == NULL ||
        mode > ASTRA_IMAGE_VIEW_PIXEL4 ||
        image->_private_handle == ASTRA_INVALID_HANDLE ||
        image->_private_window == 0u || image->_private_id == 0u ||
        (image->_private_flags & ASTRA_SURFACE_DRAW_SOURCE) == 0u ||
        source_rect->x < 0 || source_rect->y < 0 ||
        source_rect->x > INT16_MAX || source_rect->y > INT16_MAX ||
        source_rect->width == 0u || source_rect->height == 0u ||
        (uint64_t)source_rect->x + source_rect->width >
            image->_private_width ||
        (uint64_t)source_rect->y + source_rect->height >
            image->_private_height)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *model = (AstraImageViewModel){0};
    model->view = (AstraViewModel)ASTRA_VIEW_MODEL_INIT;
    model->view.intrinsic_width = source_rect->width *
        (mode == ASTRA_IMAGE_VIEW_PIXEL4 ? 4u : 1u);
    model->view.intrinsic_height = source_rect->height *
        (mode == ASTRA_IMAGE_VIEW_PIXEL4 ? 4u : 1u);
    model->view.draw = image_view_draw;
    model->view.context = model;
    model->image = image;
    model->source_rect = *source_rect;
    model->mode = mode;
    return ASTRA_OK;
}

AstraResult astra_interface_image_view_render(AstraDrawList *list,
    const AstraImageViewInfo *info)
{
    AstraBlitOptions blend = ASTRA_BLIT_OPTIONS_INIT;
    AstraResult result;

    if (list == NULL || info == NULL || info->size < sizeof(*info) ||
        info->image == NULL ||
        info->image->_private_handle == ASTRA_INVALID_HANDLE ||
        (info->image->_private_flags & ASTRA_SURFACE_DRAW_SOURCE) == 0u ||
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
