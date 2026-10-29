#ifndef ASTRA_IMAGE_VIEW_H
#define ASTRA_IMAGE_VIEW_H

/** @file image_view.h @brief Shared hardware-drawn image view. */

#include <stddef.h>

#include <astra/graphics.h>

ASTRA_EXTERN_C_BEGIN

/** Caller-owned placement of one persistent Graphics Kit image surface. */
typedef struct AstraImageViewInfo {
    uint32_t size; /**< Structure size in bytes. */
    AstraRectI32 frame; /**< Image bounds in destination coordinates. */
    AstraRectI32 clip; /**< Visible viewport in destination coordinates. */
    AstraRectI32 source_rect; /**< Image pixels to draw; scaling uses frame. */
    const AstraSurface *image; /**< Borrowed draw-source surface. */
    uint32_t reserved[2]; /**< Must be zero. */
} AstraImageViewInfo;

/** Empty image view initializer; set the frame, clip, source and image. */
#define ASTRA_IMAGE_VIEW_INFO_INIT { \
    sizeof(AstraImageViewInfo), {0, 0, 0, 0}, {0, 0, 0, 0}, \
    {0, 0, 0, 0}, NULL, {0u, 0u} \
}

/** Append one clipped, alpha-aware hardware blit to the window draw list.
 * The caller owns image upload and keeps its surface live until referencing
 * draw lists retire. Layout and scrolling belong to existing controls.
 * @param list Destination hardware draw list.
 * @param info Image placement and borrowed source surface.
 * @return ASTRA_OK, including when outside clip, or validation/draw error. */
AstraResult astra_interface_image_view_render(AstraDrawList *list,
    const AstraImageViewInfo *info);

ASTRA_EXTERN_C_END

#endif
