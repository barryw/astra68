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
    /** Northwest/southeast diagonal resize pointer. */
    ASTRA_POINTER_SHAPE_RESIZE_NW_SE = 5,
    /** Northeast/southwest diagonal resize pointer. */
    ASTRA_POINTER_SHAPE_RESIZE_NE_SW = 6,
    /** Precise selection: a thin cross. */
    ASTRA_POINTER_SHAPE_CROSSHAIR = 7,
    /** A link or other pressable target: a pointing hand. */
    ASTRA_POINTER_SHAPE_HAND = 8,
    /** The action under the pointer is refused: a slashed circle. */
    ASTRA_POINTER_SHAPE_NOT_ALLOWED = 9,
    /** Move in any direction: four arrows. */
    ASTRA_POINTER_SHAPE_MOVE = 10,
    /** Working, and still usable: the arrow with a small hourglass. */
    ASTRA_POINTER_SHAPE_PROGRESS = 11,
    /** No pointer is shown over the window's content. */
    ASTRA_POINTER_SHAPE_NONE = 12,
    /** Window-owned image installed through the window API. */
    ASTRA_POINTER_SHAPE_CUSTOM = 13
} AstraPointerShape;

/** Number of valid ::AstraPointerShape values. */
#define ASTRA_POINTER_SHAPE_COUNT 14u

/** Ask Interface Kit to derive the shape from current hover/capture state. */
#define ASTRA_POINTER_SHAPE_AUTOMATIC UINT32_MAX

#endif
