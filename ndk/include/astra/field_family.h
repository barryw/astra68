#ifndef ASTRA_FIELD_FAMILY_H
#define ASTRA_FIELD_FAMILY_H

/** @file field_family.h @brief Composed Field controls. */

#include <astra/control.h>

ASTRA_EXTERN_C_BEGIN

/** One row of editable segments separated by ordinary Labels.
 * Reserve 2 * segment_count slots (root, fields, separators). Each segment
 * retains its caller-owned TextModel. Tab traverses fields in visual order.
 */
typedef struct AstraSegmentedFieldInfo {
    uint32_t size; /**< Structure size. */
    uint32_t first_id; /**< Nonzero first control ID. */
    AstraTextModel *const *models; /**< Borrowed segment models. */
    const uint32_t *preferred_columns; /**< Borrowed widths for each segment. */
    uint32_t segment_count; /**< Segment count, 2 through 16. */
    const char *separator; /**< Borrowed UTF-8 separator between segments. */
    uint32_t separator_length; /**< Counted separator bytes. */
    uint32_t state; /**< Disabled or error state for each Field. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraSegmentedFieldInfo;

/** Empty segmented Field description. */
#define ASTRA_SEGMENTED_FIELD_INFO_INIT \
    { sizeof(AstraSegmentedFieldInfo), 0u, 0, 0, 0u, 0, 0u, 0u, \
      { 0u, 0u, 0u, 0u } }

/** Wrapped removable tokens followed by a normal editable draft Field.
 * Reserve tag_count + 2 slots. Button activations with IDs from
 * first_id + 1 to first_id + tag_count request removal of that tag.
 * Field activation requests commit of the draft TextModel as a new tag.
 */
typedef struct AstraTagInputInfo {
    uint32_t size; /**< Structure size. */
    uint32_t first_id; /**< Nonzero first control ID. */
    const AstraChoiceItem *tags; /**< Borrowed token labels. */
    uint32_t tag_count; /**< Number of removable tags, at most 32. */
    AstraTextModel *draft; /**< Borrowed editable draft TextModel. */
    uint32_t preferred_columns; /**< Intrinsic width of draft Field. */
    uint32_t state; /**< Disabled or error state for draft Field. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraTagInputInfo;

/** Empty Tag Input description. */
#define ASTRA_TAG_INPUT_INFO_INIT \
    { sizeof(AstraTagInputInfo), 0u, 0, 0u, 0, 12u, 0u, \
      { 0u, 0u, 0u, 0u } }

/** Source of a Colour Well's swatch. */
enum {
    ASTRA_COLOUR_LITERAL = 0u, /**< Use the explicit RGB value. */
    ASTRA_COLOUR_THEME_ACCENT = 1u, /**< Follow the system accent. */
    ASTRA_COLOUR_THEME_WARNING = 2u, /**< Follow warning role. */
    ASTRA_COLOUR_THEME_FAULT = 3u, /**< Follow fault role. */
    ASTRA_COLOUR_PALETTE_SLOT = 4u /**< Show resolved RGB and slot identity. */
};

/** One activatable swatch. The caller supplies the current resolved color
 * for a palette slot and opens the shared Colour panel on activation. */
typedef struct AstraColourWellInfo {
    uint32_t size; /**< Structure size. */
    uint32_t id; /**< Unique control ID. */
    const char *label; /**< Borrowed binding label. */
    uint32_t label_length; /**< UTF-8 bytes excluding NUL. */
    uint32_t binding; /**< ASTRA_COLOUR_* source. */
    AstraColorRGBA8 color; /**< Literal or resolved palette RGB, opaque. */
    uint32_t palette_slot; /**< Slot 0..255 only for palette binding. */
    uint32_t state; /**< Disabled, error, or collapsed. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraColourWellInfo;

/** Empty literal Colour Well description. */
#define ASTRA_COLOUR_WELL_INFO_INIT \
    { sizeof(AstraColourWellInfo), 0u, 0, 0u, ASTRA_COLOUR_LITERAL, \
      { 0u, 0u, 0u, 255u }, 0u, 0u, { 0u, 0u, 0u, 0u } }

ASTRA_EXTERN_C_END

#endif
