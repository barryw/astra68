#ifndef ASTRA_WINDOW_H
#define ASTRA_WINDOW_H

/**
 * @file window.h
 * @brief Capability-owned display-service windows.
 */

#include <stdint.h>

#include <astra/attributes.h>
#include <astra/resource.h>
#include <astra/theme.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

#define ASTRA_WINDOW_TITLE_MAX UINT32_C(48)

/** Window chrome recipes. */
enum {
    ASTRA_WINDOW_STANDARD = 1,
    ASTRA_WINDOW_UTILITY = 2,
    ASTRA_WINDOW_DIALOG = 3,
    ASTRA_WINDOW_POPOVER = 4,
    ASTRA_WINDOW_FULLSCREEN = 5
};

/** Workspace state owned by the window server. */
enum {
    ASTRA_WINDOW_STATE_NORMAL = 0,
    ASTRA_WINDOW_STATE_MINIMIZED = 1,
    ASTRA_WINDOW_STATE_MAXIMIZED = 2
};

/** Window content representations accepted by the display service. */
enum {
    ASTRA_WINDOW_CONTENT_RGB565 = 1,
    ASTRA_WINDOW_CONTENT_DRAW_LIST = 2
};

/** Window behavior and presentation flags. */
enum {
    ASTRA_WINDOW_RESIZABLE = 1u << 0,
    ASTRA_WINDOW_MODAL = 1u << 1,
    ASTRA_WINDOW_ACTIVE = 1u << 2
};

/** Optional titlebar gadgets. */
enum {
    ASTRA_WINDOW_GADGET_CLOSE = 1u << 0,
    ASTRA_WINDOW_GADGET_MINIMIZE = 1u << 1,
    ASTRA_WINDOW_GADGET_MAXIMIZE = 1u << 2
};

/** Visual states used by the style-preview milestone. */
enum {
    ASTRA_GADGET_NORMAL = 0,
    ASTRA_GADGET_HOVER = 1,
    ASTRA_GADGET_PRESSED = 2,
    ASTRA_GADGET_FOCUSED = 3,
    ASTRA_GADGET_DISABLED = 4
};

/** Opaque identity returned by the window server. */
typedef struct AstraWindow {
    uint32_t _private_control;
    uint32_t _private_events;
    uint32_t _private_id;
    uint32_t _private_generation;
} AstraWindow;

#define ASTRA_WINDOW_INIT { 0, 0, 0, 0 }

/** Window content frame in workspace coordinates. */
typedef struct AstraWindowFrame {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} AstraWindowFrame;

/** Current server-owned placement and state. */
typedef struct AstraWindowInfo {
    uint32_t size;
    AstraWindowFrame frame;
    uint32_t flags;
    uint32_t state;
    uint32_t z_order;
    uint32_t generation;
    uint32_t reserved[4];
} AstraWindowInfo;

#define ASTRA_WINDOW_EVENT_VERSION 2u

/** Events delivered by the compositor to the window owner. */
enum {
    ASTRA_WINDOW_EVENT_POINTER_MOTION = 1,
    ASTRA_WINDOW_EVENT_POINTER_BUTTON = 2,
    ASTRA_WINDOW_EVENT_POINTER_WHEEL = 3,
    ASTRA_WINDOW_EVENT_FOCUS = 4,
    ASTRA_WINDOW_EVENT_FRAME = 5,
    ASTRA_WINDOW_EVENT_CLOSE_REQUEST = 6,
    ASTRA_WINDOW_EVENT_STATE_RESET = 7,
    ASTRA_WINDOW_EVENT_KEY = 8,
    ASTRA_WINDOW_EVENT_TEXT = 9
};

enum {
    ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION = 1u << 0,
    ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON = 1u << 1,
    ASTRA_WINDOW_SUBSCRIBE_POINTER_WHEEL = 1u << 2,
    ASTRA_WINDOW_SUBSCRIBE_FOCUS = 1u << 3,
    ASTRA_WINDOW_SUBSCRIBE_FRAME = 1u << 4,
    ASTRA_WINDOW_SUBSCRIBE_CLOSE_REQUEST = 1u << 5,
    ASTRA_WINDOW_SUBSCRIBE_KEY = 1u << 6,
    ASTRA_WINDOW_SUBSCRIBE_TEXT = 1u << 7
};

#define ASTRA_WINDOW_SUBSCRIBE_DEFAULT \
    (ASTRA_WINDOW_SUBSCRIBE_FOCUS | ASTRA_WINDOW_SUBSCRIBE_FRAME | \
     ASTRA_WINDOW_SUBSCRIBE_CLOSE_REQUEST)
#define ASTRA_WINDOW_SUBSCRIBE_ALL \
    (ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION | \
     ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON | \
     ASTRA_WINDOW_SUBSCRIBE_POINTER_WHEEL | ASTRA_WINDOW_SUBSCRIBE_DEFAULT | \
     ASTRA_WINDOW_SUBSCRIBE_KEY | ASTRA_WINDOW_SUBSCRIBE_TEXT)

/** Event-state flags. */
enum {
    ASTRA_WINDOW_EVENT_DOWN = 1u << 0,
    ASTRA_WINDOW_EVENT_FOCUSED = 1u << 1,
    ASTRA_WINDOW_EVENT_CAPTURED = 1u << 2,
    ASTRA_WINDOW_EVENT_LOSS = 1u << 3,
    ASTRA_WINDOW_EVENT_REPEAT = 1u << 4,
    ASTRA_WINDOW_EVENT_SYNTHETIC = 1u << 5
};

/** One bounded compositor event. Pointer coordinates are content-relative. */
typedef struct AstraWindowEvent {
    uint32_t size;
    uint16_t version;
    uint16_t type;
    uint32_t flags;
    uint32_t timestamp_ms;
    uint32_t sequence;
    uint32_t generation;
    union {
        struct {
            int32_t x;
            int32_t y;
            int32_t screen_x;
            int32_t screen_y;
            uint32_t button;
            uint32_t reserved;
        } pointer;
        struct {
            int32_t x;
            int32_t y;
            int32_t screen_x;
            int32_t screen_y;
            int32_t delta_x;
            int32_t delta_y;
        } wheel;
        struct {
            AstraWindowFrame frame;
            uint32_t state;
            uint32_t z_order;
            uint32_t reserved[2];
        } frame;
        struct {
            uint32_t usage;
            uint32_t modifiers;
            uint32_t reserved[4];
        } key;
        struct {
            uint32_t codepoint;
            uint32_t modifiers;
            uint32_t reserved[4];
        } text;
        uint32_t reserved[6];
    } data;
} AstraWindowEvent;

_Static_assert(sizeof(AstraWindowEvent) == 48u,
               "window event ABI changed");

#define ASTRA_WINDOW_INFO_INIT { \
    sizeof(AstraWindowInfo), { 0, 0, 0, 0 }, 0, \
    ASTRA_WINDOW_STATE_NORMAL, 0, 0, { 0, 0, 0, 0 } \
}

/** Attributes copied by ::astra_window_create. */
typedef struct AstraWindowCreateInfo {
    uint32_t size;
    uint32_t flags;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint32_t pitch;
    uint32_t gadgets;
    uint8_t type;
    uint8_t close_state;
    uint8_t minimize_state;
    uint8_t maximize_state;
    const char *title;
    uint16_t title_length;
    uint16_t content_format;
    uint32_t event_mask;
    uint32_t reserved[3];
} AstraWindowCreateInfo;

#define ASTRA_WINDOW_CREATE_INFO_INIT { \
    sizeof(AstraWindowCreateInfo), ASTRA_WINDOW_RESIZABLE, \
    0, 0, 0, 0, 0, \
    ASTRA_WINDOW_GADGET_CLOSE | ASTRA_WINDOW_GADGET_MINIMIZE | \
        ASTRA_WINDOW_GADGET_MAXIMIZE, \
    ASTRA_WINDOW_STANDARD, ASTRA_GADGET_NORMAL, ASTRA_GADGET_NORMAL, \
    ASTRA_GADGET_NORMAL, 0, 0, ASTRA_WINDOW_CONTENT_RGB565, \
    ASTRA_WINDOW_SUBSCRIBE_DEFAULT, { 0, 0, 0 } \
}

/**
 * Create one window backed by a transferable shared-area handle.
 * The frame origin is @c x,@c y; width and height describe content, while
 * pitch applies to RGB565 surfaces and is zero for draw lists.
 */
ASTRA_NODISCARD AstraResult astra_window_create(
    uint32_t gui_endpoint,
    uint32_t content_area,
    const AstraWindowCreateInfo *create_info,
    AstraWindow *window);

ASTRA_NODISCARD AstraResult astra_window_get_info(
    AstraWindow *window, AstraWindowInfo *info);
ASTRA_NODISCARD AstraResult astra_window_set_frame(
    AstraWindow *window, const AstraWindowFrame *frame);
ASTRA_NODISCARD AstraResult astra_window_move(
    AstraWindow *window, uint16_t x, uint16_t y);
ASTRA_NODISCARD AstraResult astra_window_resize(
    AstraWindow *window, uint16_t width, uint16_t height);
ASTRA_NODISCARD AstraResult astra_window_raise(AstraWindow *window);
ASTRA_NODISCARD AstraResult astra_window_lower(AstraWindow *window);
ASTRA_NODISCARD AstraResult astra_window_activate(AstraWindow *window);
ASTRA_NODISCARD AstraResult astra_window_deactivate(AstraWindow *window);
ASTRA_NODISCARD AstraResult astra_window_minimize(AstraWindow *window);
ASTRA_NODISCARD AstraResult astra_window_maximize(AstraWindow *window);
ASTRA_NODISCARD AstraResult astra_window_restore(AstraWindow *window);
ASTRA_NODISCARD AstraResult astra_window_set_title(
    AstraWindow *window, const char *title, uint16_t title_length);
ASTRA_NODISCARD AstraResult astra_window_set_event_mask(
    AstraWindow *window, uint32_t event_mask);
/** Publish the draw-list or pixel content currently in the shared area. */
ASTRA_NODISCARD AstraResult astra_window_present(AstraWindow *window);
ASTRA_NODISCARD AstraResult astra_window_close(AstraWindow *window);
ASTRA_NODISCARD AstraResult astra_window_event_try(
    AstraWindow *window, AstraWindowEvent *event);
ASTRA_NODISCARD AstraResult astra_window_event_wait(
    AstraWindow *window, AstraWindowEvent *event,
    AstraMonotonicDeadline deadline_ns);
/** Borrow the receive handle used to wait for this window's event port. */
ASTRA_NODISCARD AstraHandle astra_window_event_wait_handle(
    const AstraWindow *window);

ASTRA_EXTERN_C_END

#endif
