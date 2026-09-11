#ifndef ASTRA_WINDOW_H
#define ASTRA_WINDOW_H

/**
 * @file window.h
 * @brief Capability-owned display-service windows.
 */

#include <stdint.h>

#include <astra/attributes.h>
#include <astra/gui.h>
#include <astra/input_modifiers.h>
#include <astra/resource.h>
#include <astra/theme.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/** Opaque identity returned by the window server. */
typedef struct AstraWindow {
    /** @cond ASTRA_INTERNAL */
    uint32_t _private_control;
    uint32_t _private_events;
    uint32_t _private_id;
    uint32_t _private_generation;
    /** @endcond */
} AstraWindow;

/** Empty window-handle initializer. */
#define ASTRA_WINDOW_INIT { 0, 0, 0, 0 }

/** Current server-owned placement and state. */
typedef struct AstraWindowInfo {
    uint32_t size; /**< Caller-supplied structure size. */
    AstraWindowFrame frame; /**< Current content frame. */
    uint32_t flags; /**< ASTRA_WINDOW_* flags. */
    uint32_t state; /**< ASTRA_WINDOW_STATE_* value. */
    uint32_t z_order; /**< Current compositor stacking position. */
    uint32_t generation; /**< State generation for change detection. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraWindowInfo;

/** Default window-information initializer. */
#define ASTRA_WINDOW_INFO_INIT { \
    sizeof(AstraWindowInfo), { 0, 0, 0, 0 }, 0, \
    ASTRA_WINDOW_STATE_NORMAL, 0, 0, { 0, 0, 0, 0 } \
}

/** Attributes copied by ::astra_window_create. */
typedef struct AstraWindowCreateInfo {
    uint32_t size; /**< Caller-supplied structure size. */
    uint32_t flags; /**< ASTRA_WINDOW_* creation flags. */
    uint16_t x; /**< Initial content origin x coordinate. */
    uint16_t y; /**< Initial content origin y coordinate. */
    uint16_t width; /**< Initial content width. */
    uint16_t height; /**< Initial content height. */
    uint32_t pitch; /**< RGB565 row stride, or zero for draw lists. */
    uint32_t gadgets; /**< ASTRA_WINDOW_GADGET_* mask. */
    uint8_t type; /**< ASTRA_WINDOW_* type value. */
    uint8_t close_state; /**< Initial close-gadget state. */
    uint8_t minimize_state; /**< Initial minimize-gadget state. */
    uint8_t maximize_state; /**< Initial maximize-gadget state. */
    const char *title; /**< UTF-8 title bytes copied during creation. */
    uint16_t title_length; /**< Bytes in @p title. */
    uint16_t content_format; /**< ASTRA_WINDOW_CONTENT_* format. */
    uint32_t event_mask; /**< Initial ASTRA_WINDOW_SUBSCRIBE_* mask. */
    AstraHandle title_icon_area; /**< Optional transferred AICON area. */
    uint32_t title_icon_length; /**< AICON bytes in title_icon_area. */
    uint32_t reserved; /**< Must be zero. */
} AstraWindowCreateInfo;

/** Default standard, resizable window creation initializer. */
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
 * @param gui_endpoint GUI service endpoint capability.
 * @param content_area Shared content-area capability transferred to the GUI.
 * @param create_info Validated creation attributes.
 * @param window Receives the open window.
 * @return ASTRA_OK on success or an AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_window_create(
    uint32_t gui_endpoint,
    uint32_t content_area,
    const AstraWindowCreateInfo *create_info,
    AstraWindow *window);

/** Read current server-owned window state.
 * @param window Open window.
 * @param info Receives current state.
 * @return ASTRA_OK on success or an AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_window_get_info(
    AstraWindow *window, AstraWindowInfo *info);
/** Set the content frame atomically.
 * @param window Open window.
 * @param frame Requested content frame.
 * @return ASTRA_OK on success or an AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_window_set_frame(
    AstraWindow *window, const AstraWindowFrame *frame);
/** Move a window without resizing it. @param window Open window. @param x New x coordinate. @param y New y coordinate. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_window_move(
    AstraWindow *window, uint16_t x, uint16_t y);
/** Resize a window without moving it. @param window Open window. @param width New content width. @param height New content height. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_window_resize(
    AstraWindow *window, uint16_t width, uint16_t height);
/** Raise in z-order. @param window Open window. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_window_raise(AstraWindow *window);
/** Lower in z-order. @param window Open window. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_window_lower(AstraWindow *window);
/** Make active. @param window Open window. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_window_activate(AstraWindow *window);
/** Make inactive. @param window Open window. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_window_deactivate(AstraWindow *window);
/** Minimize. @param window Open window. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_window_minimize(AstraWindow *window);
/** Maximize. @param window Open window. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_window_maximize(AstraWindow *window);
/** Restore normal state. @param window Open window. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_window_restore(AstraWindow *window);
/** Replace the UTF-8 title. @param window Open window. @param title UTF-8 bytes. @param title_length Byte length. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_window_set_title(
    AstraWindow *window, const char *title, uint16_t title_length);
/** Replace event subscriptions. @param window Open window. @param event_mask ASTRA_WINDOW_SUBSCRIBE_* mask. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_window_set_event_mask(
    AstraWindow *window, uint32_t event_mask);
/** Publish the draw-list or pixel content currently in the shared area.
 * @param window Open window.
 * @return ASTRA_OK on success or an AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_window_present(AstraWindow *window);
/** Publish only the changed content rectangle.
 * @param window Open window.
 * @param damage Changed content-space rectangle.
 * @return ASTRA_OK on success or an AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_window_present_region(
    AstraWindow *window, const AstraWindowFrame *damage);
/** Close a window. @param window Open window. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_window_close(AstraWindow *window);
/** Poll one event. @param window Open window. @param event Receives the event. @return ASTRA_OK, ASTRA_ERR_WOULD_BLOCK, or an error. */
ASTRA_NODISCARD AstraResult astra_window_event_try(
    AstraWindow *window, AstraWindowEvent *event);
/** Wait for one event. @param window Open window. @param event Receives the event. @param deadline_ns Absolute monotonic deadline. @return ASTRA_OK, ASTRA_ERR_TIMED_OUT, or an error. */
ASTRA_NODISCARD AstraResult astra_window_event_wait(
    AstraWindow *window, AstraWindowEvent *event,
    AstraMonotonicDeadline deadline_ns);
/** Borrow the receive handle used to wait for this window's event port.
 * @param window Open window.
 * @return Borrowed receive handle, or ASTRA_INVALID_HANDLE.
 */
ASTRA_NODISCARD AstraHandle astra_window_event_wait_handle(
    const AstraWindow *window);

ASTRA_EXTERN_C_END

#endif
