#ifndef ASTRA_SCROLL_H
#define ASTRA_SCROLL_H

/**
 * @file scroll.h
 * @brief Caller-owned scrolling state shared by views and scrollbars.
 */

#include <stdint.h>

#include <astra/result.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/** Scroll-model representation version. */
#define ASTRA_SCROLL_MODEL_VERSION UINT32_C(1)

/** Semantic scrollbar mark styles. */
enum {
    ASTRA_SCROLL_MARK_INFORMATION = 1u,
    ASTRA_SCROLL_MARK_WARNING = 2u,
    ASTRA_SCROLL_MARK_FAULT = 3u
};

/** One application-owned marker in two-dimensional content coordinates. */
typedef struct AstraScrollMark {
    uint32_t x; /**< Horizontal content coordinate. */
    uint32_t y; /**< Vertical content coordinate. */
    uint32_t kind; /**< ASTRA_SCROLL_MARK_* semantic style. */
    uint32_t reserved; /**< Must be zero. */
} AstraScrollMark;

/** Scroll-model creation data. Borrowed marks remain application-owned. */
typedef struct AstraScrollModelInfo {
    uint32_t size; /**< Structure size for source-compatible extension. */
    uint32_t content_width; /**< Complete content width in logical pixels. */
    uint32_t content_height; /**< Complete content height in logical pixels. */
    uint32_t viewport_width; /**< Visible width in logical pixels. */
    uint32_t viewport_height; /**< Visible height in logical pixels. */
    uint32_t offset_x; /**< Initial horizontal offset. */
    uint32_t offset_y; /**< Initial vertical offset. */
    uint32_t line_width; /**< Horizontal keyboard/wheel step. */
    uint32_t line_height; /**< Vertical keyboard/wheel step. */
    const AstraScrollMark *marks; /**< Borrowed marker array, or NULL. */
    uint32_t mark_count; /**< Marker count; storage is the only ceiling. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraScrollModelInfo;

/** Empty one-pixel-step scroll model. */
#define ASTRA_SCROLL_MODEL_INFO_INIT { \
    sizeof(AstraScrollModelInfo), 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, \
    { 0, 0, 0, 0 } \
}

/** Caller-owned scroll model. Private fields must not be modified. */
typedef struct AstraScrollModel {
    /** @cond ASTRA_INTERNAL */
    uint32_t _private_structure_size;
    uint32_t _private_version;
    uint32_t _private_generation;
    uint32_t _private_content_width;
    uint32_t _private_content_height;
    uint32_t _private_viewport_width;
    uint32_t _private_viewport_height;
    uint32_t _private_offset_x;
    uint32_t _private_offset_y;
    uint32_t _private_line_width;
    uint32_t _private_line_height;
    const AstraScrollMark *_private_marks;
    uint32_t _private_mark_count;
    uint32_t _private_reserved[3];
    /** @endcond */
} AstraScrollModel;

/** Empty caller-owned scroll model. */
#define ASTRA_SCROLL_MODEL_INIT { \
    sizeof(AstraScrollModel), ASTRA_SCROLL_MODEL_VERSION, 0, \
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0 } \
}

/** Immutable snapshot of one scroll model. */
typedef struct AstraScrollState {
    uint32_t size; /**< Structure size for source-compatible extension. */
    uint32_t generation; /**< Nonzero generation changed by mutations. */
    uint32_t content_width; /**< Complete content width. */
    uint32_t content_height; /**< Complete content height. */
    uint32_t viewport_width; /**< Visible viewport width. */
    uint32_t viewport_height; /**< Visible viewport height. */
    uint32_t offset_x; /**< Current clamped horizontal offset. */
    uint32_t offset_y; /**< Current clamped vertical offset. */
    uint32_t maximum_x; /**< Largest valid horizontal offset. */
    uint32_t maximum_y; /**< Largest valid vertical offset. */
    uint32_t line_width; /**< Horizontal keyboard and wheel step. */
    uint32_t line_height; /**< Vertical keyboard and wheel step. */
    const AstraScrollMark *marks; /**< Borrowed marker array. */
    uint32_t mark_count; /**< Number of entries in @ref marks. */
    uint32_t reserved[3]; /**< Must be zero. */
} AstraScrollState;

/** Empty scroll-state output. */
#define ASTRA_SCROLL_STATE_INIT { \
    sizeof(AstraScrollState), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
    { 0, 0, 0 } \
}

/** Initialize a caller-owned scroll model. @param model Destination model. @param info Valid creation data copied into the model. @return ::ASTRA_OK or a negative ::AstraResult error. */
AstraResult astra_scroll_init(AstraScrollModel *model,
                              const AstraScrollModelInfo *info);
/** Read a coherent scroll-model snapshot. @param model Initialized model. @param state Receives the snapshot. @return ::ASTRA_OK or a negative ::AstraResult error. */
AstraResult astra_scroll_get_state(const AstraScrollModel *model,
                                   AstraScrollState *state);
/** Replace extents and clamp existing offsets. @param model Initialized model. @param content_width Complete content width. @param content_height Complete content height. @param viewport_width Visible width. @param viewport_height Visible height. @return ::ASTRA_OK or a negative ::AstraResult error. */
AstraResult astra_scroll_set_extents(AstraScrollModel *model,
                                     uint32_t content_width,
                                     uint32_t content_height,
                                     uint32_t viewport_width,
                                     uint32_t viewport_height);
/** Set absolute offsets, clamped to range. @param model Initialized model. @param offset_x Horizontal offset. @param offset_y Vertical offset. @return ::ASTRA_OK or a negative ::AstraResult error. */
AstraResult astra_scroll_set_offset(AstraScrollModel *model,
                                    uint32_t offset_x, uint32_t offset_y);
/** Add signed pixel deltas, saturating at each edge. @param model Initialized model. @param delta_x Signed horizontal delta. @param delta_y Signed vertical delta. @return ::ASTRA_OK or a negative ::AstraResult error. */
AstraResult astra_scroll_by(AstraScrollModel *model,
                            int32_t delta_x, int32_t delta_y);
/** Atomically replace borrowed marks. @param model Initialized model. @param marks Borrowed marker array, or NULL when count is zero. @param mark_count Number of markers. @return ::ASTRA_OK or a negative ::AstraResult error. */
AstraResult astra_scroll_set_marks(AstraScrollModel *model,
                                   const AstraScrollMark *marks,
                                   uint32_t mark_count);

ASTRA_EXTERN_C_END

#endif
