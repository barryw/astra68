#ifndef ASTRA_CONTROL_H
#define ASTRA_CONTROL_H

/**
 * @file control.h
 * @brief Retained, themed client controls for Astra application windows.
 *
 * Controls and contexts are caller-owned process memory.  They never cross the
 * display-service boundary; only the draw list and ordinary window events do.
 */

#include <stdint.h>

#include <astra/surface.h>
#include <astra/scroll.h>
#include <astra/text_model.h>
#include <astra/types.h>
#include <astra/window.h>

ASTRA_EXTERN_C_BEGIN

/** Display refresh used to derive deterministic animation phases. */
#define ASTRA_UI_VBLANK_HZ 60u

/** Control kinds implemented by Interface Kit ABI 5.1. */
enum {
    ASTRA_CONTROL_LABEL = 1u,
    ASTRA_CONTROL_BUTTON = 2u,
    ASTRA_CONTROL_CHECKBOX = 3u,
    ASTRA_CONTROL_RADIO = 4u,
    ASTRA_CONTROL_SWITCH = 5u,
    ASTRA_CONTROL_SLIDER = 6u,
    ASTRA_CONTROL_PROGRESS = 7u,
    ASTRA_CONTROL_CONTAINER = 8u,
    ASTRA_CONTROL_FIELD = 9u,
    ASTRA_CONTROL_SEGMENTED = 10u,
    ASTRA_CONTROL_STEPPER = 11u,
    ASTRA_CONTROL_TAB = 12u,
    ASTRA_CONTROL_SCROLL_VIEW = 13u,
    ASTRA_CONTROL_SCROLLBAR = 14u,
    ASTRA_CONTROL_SPLITTER = 15u,
    ASTRA_CONTROL_DIAL = 16u,
    ASTRA_CONTROL_DISCLOSURE = 17u
};

/** Semantic state owned by an application. */
enum {
    ASTRA_CONTROL_DISABLED = UINT32_C(1) << 0,
    ASTRA_CONTROL_SELECTED = UINT32_C(1) << 1,
    ASTRA_CONTROL_ERROR = UINT32_C(1) << 2,
    /** Remove a control and its descendants from layout and interaction. */
    ASTRA_CONTROL_COLLAPSED = UINT32_C(1) << 3
};

/** Interaction states owned by the Interface Kit. */
enum {
    ASTRA_CONTROL_HOVERED = UINT32_C(1) << 8,
    ASTRA_CONTROL_PRESSED = UINT32_C(1) << 9,
    ASTRA_CONTROL_FOCUSED = UINT32_C(1) << 10
};

/** Semantic button variants; arbitrary per-control colors are not supported. */
enum {
    ASTRA_BUTTON_STANDARD = 0u,
    ASTRA_BUTTON_PRIMARY = 1u,
    ASTRA_BUTTON_WARNING = 2u,
    ASTRA_BUTTON_DESTRUCTIVE = 3u
};

/** Text hierarchy on a lunar client surface. */
enum {
    ASTRA_TEXT_CLIENT_PRIMARY = 1u,
    ASTRA_TEXT_CLIENT_SECONDARY = 2u,
    ASTRA_TEXT_CLIENT_TERTIARY = 3u,
    ASTRA_TEXT_CLIENT_MUTED = 4u,
    ASTRA_TEXT_CLIENT_FAULT = 5u
};

/** Main-axis direction and wrapping policy for a flex layout. */
enum {
    ASTRA_FLEX_ROW = 0u,
    ASTRA_FLEX_COLUMN = 1u,
    ASTRA_FLEX_WRAP = UINT32_C(1) << 0
};

/** Main-axis distribution after flexible sizes have been resolved. */
enum {
    ASTRA_FLEX_JUSTIFY_START = 0u,
    ASTRA_FLEX_JUSTIFY_CENTER = 1u,
    ASTRA_FLEX_JUSTIFY_END = 2u,
    ASTRA_FLEX_JUSTIFY_SPACE_BETWEEN = 3u
};

/** Cross-axis alignment.  AUTO is valid only on an item. */
enum {
    ASTRA_FLEX_ALIGN_START = 0u,
    ASTRA_FLEX_ALIGN_CENTER = 1u,
    ASTRA_FLEX_ALIGN_END = 2u,
    ASTRA_FLEX_ALIGN_STRETCH = 3u,
    ASTRA_FLEX_ALIGN_AUTO = 4u
};

enum {
    /** Force this item onto a new flex line. */
    ASTRA_FLEX_BREAK_BEFORE = UINT32_C(1) << 0,
    /** End the current flex line after this item. */
    ASTRA_FLEX_BREAK_AFTER = UINT32_C(1) << 1
};

/** Select intrinsic measurement instead of a fixed flex basis. */
#define ASTRA_FLEX_AUTO UINT32_MAX

/** Per-control sizing and flow constraints. */
typedef struct AstraFlexItem {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** Relative share of positive free space. */
    uint32_t grow;
    /** Relative share of space removed when the line overflows. */
    uint32_t shrink;
    /** Main-axis basis, or ::ASTRA_FLEX_AUTO for intrinsic size. */
    uint32_t basis;
    /** Inclusive minimum width. */
    uint32_t minimum_width;
    /** Inclusive minimum height. */
    uint32_t minimum_height;
    /** Inclusive maximum width. */
    uint32_t maximum_width;
    /** Inclusive maximum height. */
    uint32_t maximum_height;
    /** Per-item cross-axis alignment, or `ASTRA_FLEX_ALIGN_AUTO`. */
    uint32_t align_self;
    /** Combination of ASTRA_FLEX_BREAK_* flags. */
    uint32_t flags;
    /** Parent container ID; zero places the control at the root. */
    uint32_t parent_id;
    /** Must be zero. */
    uint32_t reserved[3];
} AstraFlexItem;

/** Default intrinsic flex-item constraints. */
#define ASTRA_FLEX_ITEM_INIT { \
    sizeof(AstraFlexItem), 0, 1, ASTRA_FLEX_AUTO, 0, 0, \
    UINT32_MAX, UINT32_MAX, ASTRA_FLEX_ALIGN_AUTO, 0, 0, { 0, 0, 0 } \
}

/** One deterministic integer flex container filling its UI context. */
typedef struct AstraFlexLayout {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** `ASTRA_FLEX_ROW` or `ASTRA_FLEX_COLUMN`. */
    uint32_t direction;
    /** Combination of layout flags such as `ASTRA_FLEX_WRAP`. */
    uint32_t flags;
    /** ASTRA_FLEX_JUSTIFY_* main-axis policy. */
    uint32_t justify;
    /** ASTRA_FLEX_ALIGN_* cross-axis policy. */
    uint32_t align_items;
    /** Inner left padding in logical pixels. */
    uint32_t padding_left;
    /** Inner top padding in logical pixels. */
    uint32_t padding_top;
    /** Inner right padding in logical pixels. */
    uint32_t padding_right;
    /** Inner bottom padding in logical pixels. */
    uint32_t padding_bottom;
    /** Gap between items on the main axis. */
    uint32_t main_gap;
    /** Gap between wrapped lines on the cross axis. */
    uint32_t cross_gap;
    /** Must be zero. */
    uint32_t reserved[4];
} AstraFlexLayout;

/** Default row layout without padding, wrapping, or gaps. */
#define ASTRA_FLEX_LAYOUT_INIT { \
    sizeof(AstraFlexLayout), ASTRA_FLEX_ROW, 0, \
    ASTRA_FLEX_JUSTIFY_START, ASTRA_FLEX_ALIGN_START, \
    0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 } \
}

/** Creation data copied into a nonvisual nested flex container. */
typedef struct AstraContainerInfo {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** Nonzero ID unique within the UI context. */
    uint32_t id;
    /** Layout applied to this container's direct children. */
    AstraFlexLayout layout;
    /** Application-owned collapsed state. */
    uint32_t state;
    /** Must be zero. */
    uint32_t reserved[3];
} AstraContainerInfo;

/** Empty container creation data. */
#define ASTRA_CONTAINER_INFO_INIT { \
    sizeof(AstraContainerInfo), 0, ASTRA_FLEX_LAYOUT_INIT, 0, { 0, 0, 0 } \
}

/** Creation data for a viewport that clips and translates one direct child. */
typedef struct AstraScrollViewInfo {
    uint32_t size; /**< Structure size for source-compatible extension. */
    uint32_t id; /**< Nonzero ID unique within the UI context. */
    AstraScrollModel *model; /**< Caller-owned model retained by the view. */
    uint32_t preferred_width; /**< Intrinsic viewport width. */
    uint32_t preferred_height; /**< Intrinsic viewport height. */
    uint32_t state; /**< Application-owned collapsed state. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraScrollViewInfo;

/** Empty scroll viewport with no arbitrary default extent. */
#define ASTRA_SCROLL_VIEW_INFO_INIT { \
    sizeof(AstraScrollViewInfo), 0, 0, 0, 0, 0, { 0, 0, 0, 0 } \
}

/** Signed client origin and nonnegative extent. */
typedef struct AstraControlFrame {
    /** Left edge in logical client coordinates. */
    int32_t x;
    /** Top edge in logical client coordinates. */
    int32_t y;
    /** Width in logical pixels. */
    uint32_t width;
    /** Height in logical pixels. */
    uint32_t height;
} AstraControlFrame;

/** Preferred control extent produced by measurement. */
typedef struct AstraControlSize {
    /** Preferred width in logical pixels. */
    uint32_t width;
    /** Preferred height in logical pixels. */
    uint32_t height;
} AstraControlSize;

/** Label creation data; the retained UTF-8 text span remains caller-owned. */
typedef struct AstraLabelInfo {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** Nonzero ID unique within the UI context. */
    uint32_t id;
    /** Borrowed validated UTF-8 bytes. */
    const char *text;
    /** Counted bytes at @ref text, excluding any terminator. */
    uint32_t text_length;
    /** ASTRA_TEXT_CLIENT_* hierarchy role. */
    uint32_t text_role;
    /** Application-owned disabled or collapsed state. */
    uint32_t state;
    /** Must be zero. */
    uint32_t reserved[3];
} AstraLabelInfo;

/** Empty primary-label creation data. */
#define ASTRA_LABEL_INFO_INIT { \
    sizeof(AstraLabelInfo), 0, 0, 0, ASTRA_TEXT_CLIENT_PRIMARY, 0, \
    { 0, 0, 0 } \
}

/** Button creation data; the retained UTF-8 text span remains caller-owned. */
typedef struct AstraButtonInfo {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** Nonzero ID unique within the UI context. */
    uint32_t id;
    /** Borrowed validated UTF-8 bytes. */
    const char *text;
    /** Counted bytes at @ref text, excluding any terminator. */
    uint32_t text_length;
    /** ASTRA_BUTTON_* semantic variant. */
    uint32_t variant;
    /** Application-owned ASTRA_CONTROL_* semantic state. */
    uint32_t state;
    /** Optional preview state used by design and gallery tooling. */
    uint32_t preview_state;
    /** Must be zero. */
    uint32_t reserved[4];
} AstraButtonInfo;

/** Empty standard-button creation data. */
#define ASTRA_BUTTON_INFO_INIT { \
    sizeof(AstraButtonInfo), 0, 0, 0, ASTRA_BUTTON_STANDARD, 0, 0, \
    { 0, 0, 0, 0 } \
}

/**
 * Creation data for a disclosure header controlling one sibling container.
 *
 * The target container owns the expanded state: a collapsed target is closed,
 * and an ordinary target is open. This avoids duplicated state between the
 * header and its contents.
 */
typedef struct AstraDisclosureInfo {
    uint32_t size; /**< Structure size for source-compatible extension. */
    uint32_t id; /**< Nonzero ID unique within the UI context. */
    const char *text; /**< Borrowed validated UTF-8 label. */
    uint32_t text_length; /**< Counted bytes excluding any terminator. */
    uint32_t target_id; /**< Nonzero ID of the sibling container to toggle. */
    uint32_t state; /**< Disabled or collapsed application-owned state. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraDisclosureInfo;

/** Empty disclosure creation data. */
#define ASTRA_DISCLOSURE_INFO_INIT { \
    sizeof(AstraDisclosureInfo), 0, 0, 0, 0, 0, { 0, 0, 0, 0 } \
}

/** Toggle creation data; the retained UTF-8 text span remains caller-owned. */
typedef struct AstraToggleInfo {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** Nonzero ID unique within the UI context. */
    uint32_t id;
    /** Borrowed validated UTF-8 bytes. */
    const char *text;
    /** Counted bytes at @ref text, excluding any terminator. */
    uint32_t text_length;
    /** Application-owned ASTRA_CONTROL_* semantic state. */
    uint32_t state;
    /** Nonzero mutual-exclusion group for radio controls. */
    uint32_t group_id;
    /** Must be zero. */
    uint32_t reserved[4];
} AstraToggleInfo;

/** Empty toggle creation data. */
#define ASTRA_TOGGLE_INFO_INIT { \
    sizeof(AstraToggleInfo), 0, 0, 0, 0, 0, { 0, 0, 0, 0 } \
}

/** Slider orientation. */
enum {
    ASTRA_ORIENTATION_HORIZONTAL = 0u,
    ASTRA_ORIENTATION_VERTICAL = 1u
};

enum {
    /** Fade the scrollbar after 800 milliseconds without interaction. */
    ASTRA_SCROLLBAR_OVERLAY = UINT32_C(1) << 0
};

/** Creation data for a scrollbar bound to a shared scroll model. */
typedef struct AstraScrollbarInfo {
    uint32_t size; /**< Structure size for source-compatible extension. */
    uint32_t id; /**< Nonzero ID unique within the UI context. */
    AstraScrollModel *model; /**< Caller-owned model retained by the bar. */
    uint32_t orientation; /**< ASTRA_ORIENTATION_* value. */
    uint32_t flags; /**< Combination of ASTRA_SCROLLBAR_* flags. */
    uint32_t state; /**< Application-owned disabled or collapsed state. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraScrollbarInfo;

/** Empty persistent vertical scrollbar. */
#define ASTRA_SCROLLBAR_INFO_INIT { \
    sizeof(AstraScrollbarInfo), 0, 0, ASTRA_ORIENTATION_VERTICAL, 0, 0, \
    { 0, 0, 0, 0 } \
}

/**
 * Creation data for the divider in a flex-composed split view.
 *
 * The splitter must sit between the two panes it resizes. A vertical splitter
 * belongs in a row layout and changes pane widths; a horizontal splitter
 * belongs in a column layout and changes pane heights. The panes' existing
 * flex minimum and maximum constraints govern movement.
 */
typedef struct AstraSplitterInfo {
    uint32_t size; /**< Structure size for source-compatible extension. */
    uint32_t id; /**< Nonzero ID unique within the UI context. */
    uint32_t orientation; /**< ASTRA_ORIENTATION_* divider direction. */
    uint32_t state; /**< Application-owned disabled or collapsed state. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraSplitterInfo;

/** Empty vertical splitter for a row-layout split view. */
#define ASTRA_SPLITTER_INFO_INIT { \
    sizeof(AstraSplitterInfo), 0, ASTRA_ORIENTATION_VERTICAL, 0, \
    { 0, 0, 0, 0 } \
}

/** Creation data copied into a retained slider. */
typedef struct AstraRangeInfo {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** Nonzero ID unique within the UI context. */
    uint32_t id;
    /** Initial representable value. */
    int64_t value;
    /** Inclusive minimum value. */
    int64_t minimum;
    /** Inclusive maximum value. */
    int64_t maximum;
    /** Positive snapping increment. */
    int64_t step;
    /** ASTRA_ORIENTATION_* value. */
    uint32_t orientation;
    /** Application-owned ASTRA_CONTROL_* semantic state. */
    uint32_t state;
    /** Must be zero. */
    uint32_t reserved[4];
} AstraRangeInfo;

/** Empty horizontal slider spanning zero through one hundred. */
#define ASTRA_RANGE_INFO_INIT { \
    sizeof(AstraRangeInfo), 0, 0, 0, 100, 1, \
    ASTRA_ORIENTATION_HORIZONTAL, 0, { 0, 0, 0, 0 } \
}

/** Creation data copied into a retained vertically-dragged dial. */
typedef struct AstraDialInfo {
    uint32_t size; /**< Structure size for source-compatible extension. */
    uint32_t id; /**< Nonzero ID unique within the UI context. */
    int64_t value; /**< Initial representable fixed-point value. */
    int64_t minimum; /**< Inclusive fixed-point minimum. */
    int64_t maximum; /**< Inclusive fixed-point maximum. */
    int64_t step; /**< Positive representable fixed-point increment. */
    int64_t reset_value; /**< Fixed-point value restored by a double click. */
    /** Decimal places shared by every range value. */
    uint32_t decimal_places;
    uint32_t state; /**< Application-owned disabled or error state. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraDialInfo;

/** Empty dial spanning zero through one hundred and resetting to zero. */
#define ASTRA_DIAL_INFO_INIT { \
    sizeof(AstraDialInfo), 0, 0, 0, 100, 1, 0, 0, 0, \
    { 0, 0, 0, 0 } \
}

/** Creation data copied into a retained numeric stepper. */
typedef struct AstraStepperInfo {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** Nonzero ID unique within the UI context. */
    uint32_t id;
    /** Initial representable value. */
    int64_t value;
    /** Inclusive minimum value. */
    int64_t minimum;
    /** Inclusive maximum value. */
    int64_t maximum;
    /** Positive representable increment. */
    int64_t step;
    /** Application-owned disabled or error semantic state. */
    uint32_t state;
    /** Must be zero. */
    uint32_t reserved[4];
} AstraStepperInfo;

/** Empty numeric stepper spanning zero through one hundred. */
#define ASTRA_STEPPER_INFO_INIT {                                      \
    sizeof(AstraStepperInfo), 0, 0, 0, 100, 1, 0,                     \
    { 0, 0, 0, 0 }                                                     \
}

/** Zero maximum selects indeterminate progress. */
typedef struct AstraProgressInfo {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** Nonzero ID unique within the UI context. */
    uint32_t id;
    /** Completed units when @ref maximum is nonzero. */
    uint32_t value;
    /** Total units, or zero for indeterminate progress. */
    uint32_t maximum;
    /** Application-owned ASTRA_CONTROL_* semantic state. */
    uint32_t state;
    /** Must be zero. */
    uint32_t reserved[4];
} AstraProgressInfo;

/** Empty indeterminate-progress creation data. */
#define ASTRA_PROGRESS_INFO_INIT { \
    sizeof(AstraProgressInfo), 0, 0, 0, 0, { 0, 0, 0, 0 } \
}

/** One borrowed UTF-8 choice shared by selection controls. */
typedef struct AstraChoiceItem {
    /** Borrowed validated UTF-8 label. */
    const char *text;
    /** Counted bytes at @ref text, excluding any terminator. */
    uint32_t text_length;
    /** Must be zero. */
    uint32_t reserved[2];
} AstraChoiceItem;

/** Empty selection-control item. */
#define ASTRA_CHOICE_ITEM_INIT { 0, 0, { 0, 0 } }

/** Creation data shared by mutually exclusive selection controls. */
typedef struct AstraChoiceInfo {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** Nonzero ID unique within the UI context. */
    uint32_t id;
    /** Borrowed item array retained for the control lifetime. */
    const AstraChoiceItem *items;
    /** Number of items; storage capacity is the only ceiling. */
    uint32_t item_count;
    /** Initially selected zero-based item. */
    uint32_t selected;
    /** Application-owned disabled or error semantic state. */
    uint32_t state;
    /** Must be zero. */
    uint32_t reserved[4];
} AstraChoiceInfo;

/** Empty selection-control creation data. */
#define ASTRA_CHOICE_INFO_INIT {                                         \
    sizeof(AstraChoiceInfo), 0, 0, 0, 0, 0, { 0, 0, 0, 0 }              \
}

enum {
    /** Permit selection and copying but reject document mutations. */
    ASTRA_FIELD_READ_ONLY = UINT32_C(1) << 0
};

/** Creation data copied into a retained single-line UTF-8 field. */
typedef struct AstraFieldInfo {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** Nonzero ID unique within the UI context. */
    uint32_t id;
    /** Caller-owned piece-table model retained for the control lifetime. */
    AstraTextModel *model;
    /** Preferred visible columns used only for intrinsic layout width. */
    uint32_t preferred_columns;
    /** Combination of ASTRA_FIELD_* behavior flags. */
    uint32_t flags;
    /** Application-owned disabled or error semantic state. */
    uint32_t state;
    /** Must be zero. */
    uint32_t reserved[4];
} AstraFieldInfo;

/** Empty editable field with a twenty-column intrinsic width. */
#define ASTRA_FIELD_INFO_INIT {                                           \
    sizeof(AstraFieldInfo), 0u, 0, 20u, 0u, 0u,                          \
    { 0u, 0u, 0u, 0u }                                                   \
}

/** Caller-owned retained control.  Private fields must not be modified. */
typedef struct AstraControl {
    /** @cond ASTRA_INTERNAL */
    uint32_t _private_structure_size;
    uint32_t _private_control_kind;
    uint32_t _private_id;
    uint32_t _private_state;
    uint32_t _private_preview_state;
    uint32_t _private_dynamic_state;
    uint32_t _private_style;
    const char *_private_text;
    uint32_t _private_text_length;
    AstraControlFrame _private_frame;
    uint32_t _private_laid_out;
    uint32_t _private_measured_width;
    uint32_t _private_measured_height;
    uint32_t _private_flex_grow;
    uint32_t _private_flex_shrink;
    uint32_t _private_flex_basis;
    uint32_t _private_minimum_width;
    uint32_t _private_minimum_height;
    uint32_t _private_maximum_width;
    uint32_t _private_maximum_height;
    uint32_t _private_align_self;
    uint32_t _private_flex_flags;
    uint32_t _private_flex_target;
    int64_t _private_value;
    int64_t _private_minimum_value;
    int64_t _private_maximum_value;
    int64_t _private_value_step;
    int64_t _private_reset_value;
    uint32_t _private_decimal_places;
    uint32_t _private_parent_id;
    uint32_t _private_parent;
    uint32_t _private_first_child;
    uint32_t _private_last_child;
    uint32_t _private_next_sibling;
    AstraControlFrame _private_clip;
    AstraFlexLayout _private_child_layout;
    uint32_t _private_animation_next;
    uint32_t _private_animation_phase;
    /** @endcond */
} AstraControl;

/** Empty retained control state. */
#define ASTRA_CONTROL_INIT { \
    sizeof(AstraControl), 0, 0, 0, 0, 0, 0, 0, 0, \
    { 0, 0, 0, 0 }, 0, 0, 0, 0, 1, ASTRA_FLEX_AUTO, 0, 0, \
    UINT32_MAX, UINT32_MAX, ASTRA_FLEX_ALIGN_AUTO, 0, 0, \
    0, 0, 0, 0, 0, 0, 0, UINT32_MAX, UINT32_MAX, UINT32_MAX, \
    UINT32_MAX, { 0, 0, 0, 0 }, ASTRA_FLEX_LAYOUT_INIT, UINT32_MAX, 0 \
}

/** Caller-owned retained interaction and damage state. */
typedef struct AstraUIContext {
    /** @cond ASTRA_INTERNAL */
    uint32_t _private_structure_size;
    AstraControl *_private_controls;
    uint32_t _private_control_count;
    uint16_t _private_width;
    uint16_t _private_height;
    uint32_t _private_hover;
    uint32_t _private_capture;
    uint32_t _private_focus;
    uint32_t _private_keyboard;
    AstraControlFrame _private_damage;
    uint32_t _private_has_damage;
    AstraFlexLayout _private_layout;
    uint32_t _private_has_layout;
    uint32_t _private_animation_time_low;
    uint32_t _private_animation_time_high;
    uint32_t _private_animation_phase;
    uint32_t _private_first_child;
    uint32_t _private_last_child;
    uint32_t _private_animation_first;
    uint32_t _private_reserved;
    uint64_t _private_value_input;
    uint32_t _private_value_input_digits;
    uint32_t _private_value_input_flags;
    uint32_t _private_hover_part;
    uint32_t _private_capture_part;
    /** @endcond */
} AstraUIContext;

/** Empty UI context state. */
#define ASTRA_UI_CONTEXT_INIT { \
    sizeof(AstraUIContext), 0, 0, 0, 0, UINT32_MAX, UINT32_MAX, \
    UINT32_MAX, UINT32_MAX, { 0, 0, 0, 0 }, 0, ASTRA_FLEX_LAYOUT_INIT, 0, \
    0, 0, 0, UINT32_MAX, UINT32_MAX, UINT32_MAX, 0, 0, 0, 0, 0, 0 \
}

/** Semantic event results returned to application code. */
enum {
    ASTRA_UI_ACTION_NONE = 0u,
    ASTRA_UI_ACTION_ACTIVATE = 1u,
    ASTRA_UI_ACTION_VALUE_CHANGED = 2u,
    ASTRA_UI_ACTION_TEXT_CHANGED = 3u,
    ASTRA_UI_ACTION_SELECTION_CHANGED = 4u,
    ASTRA_UI_ACTION_COPY = 5u,
    ASTRA_UI_ACTION_CUT = 6u,
    ASTRA_UI_ACTION_PASTE = 7u,
    ASTRA_UI_ACTION_SCROLL_CHANGED = 8u
};

/** Semantic result emitted by a handled window event. */
typedef struct AstraUIAction {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** ASTRA_UI_ACTION_* result kind. */
    uint32_t type;
    /** ID of the control that emitted the action. */
    uint32_t control_id;
    /** Current value for `ASTRA_UI_ACTION_VALUE_CHANGED`. */
    int64_t value;
    /** Decimal places for @ref value; zero for nonnumeric actions. */
    uint32_t decimal_places;
    /** Must be zero. */
    uint32_t reserved[2];
} AstraUIAction;

/** Empty semantic action. */
#define ASTRA_UI_ACTION_INIT { \
    sizeof(AstraUIAction), ASTRA_UI_ACTION_NONE, 0, 0, 0, { 0, 0 } \
}

ASTRA_EXTERN_C_END

#endif
