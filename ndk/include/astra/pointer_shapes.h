#ifndef ASTRA_POINTER_SHAPES_H
#define ASTRA_POINTER_SHAPES_H

/** @file pointer_shapes.h @brief Canonical system pointer-image identifiers. */

#include <stdint.h>

/** System pointer images supplied by the window server. */
typedef enum AstraPointerShape {
    /** Ordinary pointing arrow. */
    ASTRA_POINTER_SHAPE_DEFAULT = 0,
    /** Left/right resize pointer used by a vertical divider. */
    ASTRA_POINTER_SHAPE_RESIZE_HORIZONTAL = 1,
    /** Up/down resize pointer used by a horizontal divider. */
    ASTRA_POINTER_SHAPE_RESIZE_VERTICAL = 2,
    /** Text insertion I-beam. */
    ASTRA_POINTER_SHAPE_TEXT = 3,
    /** Interaction is temporarily unavailable. */
    ASTRA_POINTER_SHAPE_WAIT = 4,
    /** Window-owned image installed through the window API. */
    ASTRA_POINTER_SHAPE_CUSTOM = 5
} AstraPointerShape;

/** Number of valid ::AstraPointerShape values. */
#define ASTRA_POINTER_SHAPE_COUNT 6u

/** Ask Interface Kit to derive the shape from current hover/capture state. */
#define ASTRA_POINTER_SHAPE_AUTOMATIC UINT32_MAX

#endif
