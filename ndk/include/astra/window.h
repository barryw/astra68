#ifndef ASTRA_WINDOW_H
#define ASTRA_WINDOW_H

/**
 * @file window.h
 * @brief Capability-owned display-service windows.
 */

#include <stdint.h>

#include <astra/attributes.h>
#include <astra/gui.h>
#include <astra/resource.h>
#include <astra/theme.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/** Opaque identity returned by the window server. */
typedef struct AstraWindow {
    uint32_t _private_control;
    uint32_t _private_events;
    uint32_t _private_id;
    uint32_t _private_generation;
} AstraWindow;

#define ASTRA_WINDOW_INIT { 0, 0, 0, 0 }

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
    AstraHandle title_icon_area;
    uint32_t title_icon_length;
    uint32_t reserved;
} AstraWindowCreateInfo;

#define ASTRA_WINDOW_CREATE_INFO_INIT { \
    sizeof(AstraWindowCreateInfo), ASTRA_WINDOW_RESIZABLE, \
    0, 0, 0, 0, 0, \
    ASTRA_WINDOW_GADGET_CLOSE | ASTRA_WINDOW_GADGET_MINIMIZE | \
        ASTRA_WINDOW_GADGET_MAXIMIZE, \
    ASTRA_WINDOW_STANDARD, ASTRA_GADGET_NORMAL, ASTRA_GADGET_NORMAL, \
    ASTRA_GADGET_NORMAL, 0, 0, ASTRA_WINDOW_CONTENT_RGB565, \
    ASTRA_WINDOW_SUBSCRIBE_DEFAULT, ASTRA_INVALID_HANDLE, 0, 0 \
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
/** Publish only the changed content rectangle. */
ASTRA_NODISCARD AstraResult astra_window_present_region(
    AstraWindow *window, const AstraWindowFrame *damage);
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
