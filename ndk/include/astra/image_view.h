#ifndef ASTRA_IMAGE_VIEW_H
#define ASTRA_IMAGE_VIEW_H

/** @file image_view.h @brief Shared hardware-drawn image view. */

#include <stddef.h>

#include <astra/graphics.h>
#include <astra/control.h>

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

/** Image placement inside a retained View's laid-out frame. */
enum {
    ASTRA_IMAGE_VIEW_FIT = 0u,
    ASTRA_IMAGE_VIEW_FILL = 1u,
    ASTRA_IMAGE_VIEW_ACTUAL = 2u,
    ASTRA_IMAGE_VIEW_PIXEL4 = 3u
};

/** An ImageView composes the shared retained View. The image is a borrowed
 * window-owned Graphics Kit surface; keep it live while visible. */
typedef struct AstraImageViewModel {
    AstraViewModel view; /**< Embedded shared View model. */
    const AstraSurface *image; /**< Borrowed source surface. */
    AstraRectI32 source_rect; /**< Source rectangle in image pixels. */
    uint32_t mode; /**< One ASTRA_IMAGE_VIEW_* placement mode. */
    uint32_t reserved[2]; /**< Must be zero. */
} AstraImageViewModel;

/** Initialize an Image View model for hardware drawing.
 * @param model Empty caller-owned model.
 * @param image Borrowed source surface, kept live through rendering.
 * @param source_rect Optional source rectangle; null selects the full image.
 * @param mode One ASTRA_IMAGE_VIEW_* placement mode.
 * @return ASTRA_OK or a validation error. */
ASTRA_NODISCARD AstraResult astra_interface_image_view_model_init(
    AstraImageViewModel *model, const AstraSurface *image,
    const AstraRectI32 *source_rect, uint32_t mode);

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
