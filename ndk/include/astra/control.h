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
#include <astra/types.h>
#include <astra/window.h>

ASTRA_EXTERN_C_BEGIN

#define ASTRA_UI_ANIMATION_INTERVAL_NS UINT64_C(50000000)

/** Control kinds implemented by Interface Kit ABI 2.0. */
enum {
    ASTRA_CONTROL_LABEL = 1u,
    ASTRA_CONTROL_BUTTON = 2u,
    ASTRA_CONTROL_CHECKBOX = 3u,
    ASTRA_CONTROL_RADIO = 4u,
    ASTRA_CONTROL_SWITCH = 5u,
    ASTRA_CONTROL_SLIDER = 6u,
    ASTRA_CONTROL_PROGRESS = 7u,
    ASTRA_CONTROL_CONTAINER = 8u
};

/** Semantic state owned by an application. */
enum {
    ASTRA_CONTROL_DISABLED = UINT32_C(1) << 0,
    ASTRA_CONTROL_SELECTED = UINT32_C(1) << 1,
    ASTRA_CONTROL_ERROR = UINT32_C(1) << 2
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
    ASTRA_TEXT_CLIENT_MUTED = 4u
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
    ASTRA_FLEX_BREAK_BEFORE = UINT32_C(1) << 0,
    ASTRA_FLEX_BREAK_AFTER = UINT32_C(1) << 1
};

#define ASTRA_FLEX_AUTO UINT32_MAX

/** Per-control sizing and flow constraints. */
typedef struct AstraFlexItem {
    uint32_t size;
    uint32_t grow;
    uint32_t shrink;
    uint32_t basis;
    uint32_t minimum_width;
    uint32_t minimum_height;
    uint32_t maximum_width;
    uint32_t maximum_height;
    uint32_t align_self;
    uint32_t flags;
    uint32_t parent_id;
    uint32_t reserved[3];
} AstraFlexItem;

#define ASTRA_FLEX_ITEM_INIT { \
    sizeof(AstraFlexItem), 0, 1, ASTRA_FLEX_AUTO, 0, 0, \
    UINT32_MAX, UINT32_MAX, ASTRA_FLEX_ALIGN_AUTO, 0, 0, { 0, 0, 0 } \
}

/** One deterministic integer flex container filling its UI context. */
typedef struct AstraFlexLayout {
    uint32_t size;
    uint32_t direction;
    uint32_t flags;
    uint32_t justify;
    uint32_t align_items;
    uint32_t padding_left;
    uint32_t padding_top;
    uint32_t padding_right;
    uint32_t padding_bottom;
    uint32_t main_gap;
    uint32_t cross_gap;
    uint32_t reserved[4];
} AstraFlexLayout;

#define ASTRA_FLEX_LAYOUT_INIT { \
    sizeof(AstraFlexLayout), ASTRA_FLEX_ROW, 0, \
    ASTRA_FLEX_JUSTIFY_START, ASTRA_FLEX_ALIGN_START, \
    0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 } \
}

/** Creation data copied into a nonvisual nested flex container. */
typedef struct AstraContainerInfo {
    uint32_t size;
    uint32_t id;
    AstraFlexLayout layout;
    uint32_t reserved[4];
} AstraContainerInfo;

#define ASTRA_CONTAINER_INFO_INIT { \
    sizeof(AstraContainerInfo), 0, ASTRA_FLEX_LAYOUT_INIT, { 0, 0, 0, 0 } \
}

/** Signed client origin and nonnegative extent. */
typedef struct AstraControlFrame {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} AstraControlFrame;

/** Preferred control extent produced by measurement. */
typedef struct AstraControlSize {
    uint32_t width;
    uint32_t height;
} AstraControlSize;

/** Creation data copied into a retained label. */
typedef struct AstraLabelInfo {
    uint32_t size;
    uint32_t id;
    const char *text;
    uint32_t text_length;
    uint32_t text_role;
    uint32_t reserved[4];
} AstraLabelInfo;

#define ASTRA_LABEL_INFO_INIT { \
    sizeof(AstraLabelInfo), 0, 0, 0, ASTRA_TEXT_CLIENT_PRIMARY, \
    { 0, 0, 0, 0 } \
}

/** Creation data copied into a retained button. */
typedef struct AstraButtonInfo {
    uint32_t size;
    uint32_t id;
    const char *text;
    uint32_t text_length;
    uint32_t variant;
    uint32_t state;
    uint32_t preview_state;
    uint32_t reserved[4];
} AstraButtonInfo;

#define ASTRA_BUTTON_INFO_INIT { \
    sizeof(AstraButtonInfo), 0, 0, 0, ASTRA_BUTTON_STANDARD, 0, 0, \
    { 0, 0, 0, 0 } \
}

/** Creation data copied into a retained checkbox, radio, or switch. */
typedef struct AstraToggleInfo {
    uint32_t size;
    uint32_t id;
    const char *text;
    uint32_t text_length;
    uint32_t state;
    uint32_t group_id;
    uint32_t reserved[4];
} AstraToggleInfo;

#define ASTRA_TOGGLE_INFO_INIT { \
    sizeof(AstraToggleInfo), 0, 0, 0, 0, 0, { 0, 0, 0, 0 } \
}

enum {
    ASTRA_ORIENTATION_HORIZONTAL = 0u,
    ASTRA_ORIENTATION_VERTICAL = 1u
};

/** Creation data copied into a retained slider. */
typedef struct AstraRangeInfo {
    uint32_t size;
    uint32_t id;
    int32_t value;
    int32_t minimum;
    int32_t maximum;
    int32_t step;
    uint32_t orientation;
    uint32_t state;
    uint32_t reserved[4];
} AstraRangeInfo;

#define ASTRA_RANGE_INFO_INIT { \
    sizeof(AstraRangeInfo), 0, 0, 0, 100, 1, \
    ASTRA_ORIENTATION_HORIZONTAL, 0, { 0, 0, 0, 0 } \
}

/** Zero maximum selects indeterminate progress. */
typedef struct AstraProgressInfo {
    uint32_t size;
    uint32_t id;
    uint32_t value;
    uint32_t maximum;
    uint32_t state;
    uint32_t reserved[4];
} AstraProgressInfo;

#define ASTRA_PROGRESS_INFO_INIT { \
    sizeof(AstraProgressInfo), 0, 0, 0, 0, { 0, 0, 0, 0 } \
}

/** Caller-owned retained control.  Private fields must not be modified. */
typedef struct AstraControl {
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
    int32_t _private_value;
    int32_t _private_minimum_value;
    int32_t _private_maximum_value;
    int32_t _private_value_step;
    uint32_t _private_parent_id;
    uint32_t _private_parent;
    uint32_t _private_first_child;
    uint32_t _private_last_child;
    uint32_t _private_next_sibling;
    AstraControlFrame _private_clip;
    AstraFlexLayout _private_child_layout;
} AstraControl;

#define ASTRA_CONTROL_INIT { \
    sizeof(AstraControl), 0, 0, 0, 0, 0, 0, 0, 0, \
    { 0, 0, 0, 0 }, 0, 0, 0, 0, 1, ASTRA_FLEX_AUTO, 0, 0, \
    UINT32_MAX, UINT32_MAX, ASTRA_FLEX_ALIGN_AUTO, 0, 0, 0, 0, 0, 0, \
    0, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, { 0, 0, 0, 0 }, \
    ASTRA_FLEX_LAYOUT_INIT \
}

/** Caller-owned retained interaction and damage state. */
typedef struct AstraUIContext {
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
    uint32_t _private_reserved;
} AstraUIContext;

#define ASTRA_UI_CONTEXT_INIT { \
    sizeof(AstraUIContext), 0, 0, 0, 0, UINT32_MAX, UINT32_MAX, \
    UINT32_MAX, UINT32_MAX, { 0, 0, 0, 0 }, 0, ASTRA_FLEX_LAYOUT_INIT, 0, \
    0, 0, 0, UINT32_MAX, UINT32_MAX, 0 \
}

enum {
    ASTRA_UI_ACTION_NONE = 0u,
    ASTRA_UI_ACTION_ACTIVATE = 1u,
    ASTRA_UI_ACTION_VALUE_CHANGED = 2u
};

/** Semantic result emitted by a handled window event. */
typedef struct AstraUIAction {
    uint32_t size;
    uint32_t type;
    uint32_t control_id;
    int32_t value;
    uint32_t reserved[3];
} AstraUIAction;

#define ASTRA_UI_ACTION_INIT { \
    sizeof(AstraUIAction), ASTRA_UI_ACTION_NONE, 0, 0, { 0, 0, 0 } \
}

ASTRA_EXTERN_C_END

#endif
