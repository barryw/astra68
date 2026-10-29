#ifndef ASTRA_WINDOW_H
#define ASTRA_WINDOW_H

/**
 * @file window.h
 * @brief Capability-owned display-service windows.
 */

#include <stdint.h>

#include <astra/attributes.h>
#include <astra/graphics.h>
#include <astra/gui.h>
#include <astra/input_modifiers.h>
#include <astra/pointer.h>
#include <astra/resource.h>
#include <astra/theme.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

struct AstraMenuItem;

/** Opaque identity returned by the window server. */
typedef struct AstraWindow {
    /** @cond ASTRA_INTERNAL */
    uint32_t _private_control;
    uint32_t _private_events;
    uint32_t _private_id;
    uint32_t _private_generation;
    uint32_t _private_vblank;
    /** @endcond */
} AstraWindow;

/** Empty window-handle initializer. */
#define ASTRA_WINDOW_INIT { 0, 0, 0, 0, 0 }

/** Show an immutable presentation snapshot owned by this window.
 * Most callers use Interface Kit's command-backed context menu session.
 * @param window Owning live window.
 * @param items Counted presentation rows copied before return.
 * @param count Number of rows.
 * @param x Client-local anchor x.
 * @param y Client-local anchor y.
 * @param request Nonzero caller correlation token.
 * @return ASTRA_OK or a validation, permission or capacity error.
 */
ASTRA_NODISCARD AstraResult astra_window_menu_open(AstraWindow *window,
    const struct AstraMenuItem *items, uint32_t count, int32_t x, int32_t y,
    uint32_t request);
/** Open a flat menu that reports each enabled row selection while it stays
 * open. Each selected MENU_RESULT has ASTRA_WINDOW_EVENT_CONTINUED; dismissal
 * sends a final selection-zero result without that flag. The display owns
 * the copied rows and toggles their checked marks after each selection.
 * @param window Owning active window.
 * @param items Flat counted presentation rows copied before return.
 * @param count Number of rows.
 * @param x Client-local anchor x.
 * @param y Client-local anchor y.
 * @param request Nonzero correlation token.
 * @return ASTRA_OK or a validation, permission, or capacity error.
 */
ASTRA_NODISCARD AstraResult astra_window_menu_open_persistent(
    AstraWindow *window, const struct AstraMenuItem *items, uint32_t count,
    int32_t x, int32_t y, uint32_t request);

/** Show a display-owned Tooltip after the shared hover delay. Its anchor is
 * client-local. Pointer motion, focus loss, owner close, explicit hide, or
 * timeout dismisses it without changing input ownership.
 * @param window Owning live window.
 * @param text Counted single-line UTF-8, at most ASTRA_WINDOW_TITLE_MAX bytes.
 * @param length Text byte count, excluding NUL.
 * @param x Client-local anchor x.
 * @param y Client-local anchor y.
 * @param duration_ms Visible duration, 1000 to 30000; zero selects 5000.
 * @return ASTRA_OK or a validation, permission, or service error.
 */
ASTRA_NODISCARD AstraResult astra_window_tooltip_show(AstraWindow *window,
    const char *text, uint16_t length, uint16_t x, uint16_t y,
    uint32_t duration_ms);
/** Show a display-owned Notification at the work-area edge. It does not
 * capture input. A new Notice replaces the previous one, unless a menu is
 * active. Pointer click, owner close, explicit hide, or timeout dismisses it.
 * @param window Owning live window.
 * @param text Counted single-line UTF-8, at most ASTRA_WINDOW_TITLE_MAX bytes.
 * @param length Text byte count, excluding NUL.
 * @param duration_ms Visible duration, 1000 to 30000; zero selects 7000.
 * @return ASTRA_OK or a validation, permission, or service error.
 */
ASTRA_NODISCARD AstraResult astra_window_notification_show(
    AstraWindow *window, const char *text, uint16_t length,
    uint32_t duration_ms);
/** Dismiss this window's Tooltip or Notification, if present.
 * @param window Owning live window.
 * @return ASTRA_OK or a validation or service error.
 */
ASTRA_NODISCARD AstraResult astra_window_overlay_hide(AstraWindow *window);
/** Dismiss this window's correlated menu. @param window Owning window. @param request Menu correlation token. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_window_menu_close(AstraWindow *window, uint32_t request);

/** Correlation token in MENU_RESULT for a selection from the system menu bar.
 * Selection is the one-based row index in the registered menu document. */
#define ASTRA_WINDOW_MENU_BAR_REQUEST UINT32_C(0xffffffff)
/** Publish headings (root rows) and their grouped child commands for the
 * system menu bar. The display copies all labels. Replaces any prior bar.
 * @param window Owning live window.
 * @param items Root headings followed by grouped child commands.
 * @param count Total number of rows.
 * @return ASTRA_OK or a validation, permission or capacity error.
 */
ASTRA_NODISCARD AstraResult astra_window_menu_bar_set(AstraWindow *window,
    const struct AstraMenuItem *items, uint32_t count);
/** Remove this window's published menu headings.
 * @param window Owning live window.
 * @return ASTRA_OK or a validation or service error.
 */
ASTRA_NODISCARD AstraResult astra_window_menu_bar_clear(AstraWindow *window);

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
    uint32_t gadgets; /**< ASTRA_WINDOW_GADGET_AUTO or explicit mask. */
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
    ASTRA_WINDOW_GADGET_AUTO, \
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

/** Create a parent-attached Sheet using parent-local placement. The display
 * validates that @p parent belongs to the same GUI session and keeps the
 * Sheet attached as the parent moves. While visible it blocks input to that
 * parent, leaving other windows in the session usable. Closing the parent
 * also closes its Sheet. The parent must remain live through this call.
 * @param gui_endpoint GUI session endpoint used to open the parent.
 * @param content_area Shared Sheet content area transferred to the GUI.
 * @param create_info Creation data with type ASTRA_WINDOW_SHEET and no MODAL flag.
 * @param parent Live parent window.
 * @param window Receives the Sheet window.
 * @return ASTRA_OK or a validation, permission, or service error.
 */
ASTRA_NODISCARD AstraResult astra_window_sheet_create(
    uint32_t gui_endpoint, uint32_t content_area,
    const AstraWindowCreateInfo *create_info, const AstraWindow *parent,
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
/** Take the whole display without chrome; astra_window_restore() returns the
 * window's previous state and frame. Standard windows only.
 * @param window Open window.
 * @return ASTRA_OK or an AstraResult error.
 * @since system.library 2.7.
 */
ASTRA_NODISCARD AstraResult astra_window_fullscreen(AstraWindow *window);
/** Replace the UTF-8 title. @param window Open window. @param title UTF-8 bytes. @param title_length Byte length. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_window_set_title(
    AstraWindow *window, const char *title, uint16_t title_length);
/** Set the stable UTF-8 application name shown in the system bar, independent
 * of the mutable window title. Set this before activating a new window.
 * @param window Open window.
 * @param name Counted UTF-8 application name.
 * @param name_length Nonzero byte length, at most ASTRA_WINDOW_TITLE_MAX.
 * @return ASTRA_OK or a validation/service error.
 */
ASTRA_NODISCARD AstraResult astra_window_set_application_name(
    AstraWindow *window, const char *name, uint16_t name_length);
/** Replace event subscriptions. @param window Open window. @param event_mask ASTRA_WINDOW_SUBSCRIBE_* mask. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_window_set_event_mask(
    AstraWindow *window, uint32_t event_mask);
/** Select one server-supplied pointer image for this window's content. @param window Open window. @param shape ASTRA_POINTER_SHAPE_* value other than CUSTOM unless a custom image is installed. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_window_set_pointer_shape(
    AstraWindow *window, AstraPointerShape shape);
/** Copy, install, and select a window-owned custom pointer image. Pixels are normalized to the native 32 by 32 pointer plane and cannot be mutated after return. @param window Open window. @param image Valid RGBA image and hotspot. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_window_set_pointer_image(
    AstraWindow *window, const AstraHardwarePointerImage *image);
/** Grab the pointer for this window while it is active (Haiku's
 * SetEventMask(B_POINTER_EVENTS) and B_LOCK_WINDOW_FOCUS, made explicit).
 * CONFINE keeps the pointer inside @p rectangle of the content, or the
 * whole content when it is NULL; LOCK holds it still, motion arriving only
 * as the device's motion totals; CAPTURE sends the window the pointer's
 * motion and buttons wherever it is. The grab lapses while the window is
 * inactive and resumes when it is active again; zero releases it.
 * @param window Open window.
 * @param flags ASTRA_WINDOW_POINTER_* grab flags, or zero.
 * @param rectangle Content rectangle for CONFINE, or NULL.
 * @return ASTRA_OK or an error.
 * @since system.library 3.2.
 */
ASTRA_NODISCARD AstraResult astra_window_set_pointer_grab(
    AstraWindow *window, uint32_t flags, const AstraWindowFrame *rectangle);
/** Move the pointer to a content position of this window, kept inside the
 * content and any confinement. Only the active window may move it; the
 * window then receives a synthetic motion event at the new position.
 * @param window Active open window.
 * @param x Content x, 0 to 65535.
 * @param y Content y, 0 to 65535.
 * @return ASTRA_OK, ::ASTRA_ERROR_PERMISSION when the window is not active,
 *         or another error.
 * @since system.library 3.2.
 */
ASTRA_NODISCARD AstraResult astra_window_warp_pointer(AstraWindow *window,
                                                      int32_t x, int32_t y);
/** Publish the draw-list or pixel content currently in the shared area.
 * @param window Open window.
 * @return ASTRA_OK on success or an AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_window_present(AstraWindow *window);
/** Publish the whole content surface and give up its pixels: the next
 * frame starts undefined and the client redraws every pixel of it before
 * presenting again, as SDL's renderer already requires. This saves the
 * service copying the frame forward. Only ::ASTRA_WINDOW_CONTENT_SURFACE
 * windows accept it.
 * @param window Open window.
 * @return ASTRA_OK on success or an AstraResult error.
 * @since system.library 2.6.
 */
ASTRA_NODISCARD AstraResult astra_window_present_discard(AstraWindow *window);
/** Publish only the changed content rectangle.
 * @param window Open window.
 * @param damage Changed content-space rectangle.
 * @return ASTRA_OK on success or an AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_window_present_region(
    AstraWindow *window, const AstraWindowFrame *damage);
/** Close a window. @param window Open window. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_window_close(AstraWindow *window);
/**
 * Bind a graphics display to a ::ASTRA_WINDOW_CONTENT_SURFACE or
 * ::ASTRA_WINDOW_CONTENT_DRAW_LIST window. Draw-list windows can create and
 * upload source surfaces, then blit them from their retained commands.
 * The display borrows the window's control channel: close it first.
 * @param window Open window.
 * @param display Empty display that receives the binding.
 * @return ASTRA_OK or an AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_window_display(const AstraWindow *window,
                                                 AstraDisplay *display);
/**
 * Borrow a ::ASTRA_WINDOW_CONTENT_SURFACE window's content as an RGB565
 * draw target and source, sized to the window's current content.
 * @param window Open window.
 * @param surface Empty surface that receives the borrowed content.
 * @return ASTRA_OK or an AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_window_surface(AstraWindow *window,
                                                 AstraSurface *surface);
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
/** Borrow the coalescing display-vblank wait handle owned by this window.
 * The handle becomes signaled only while the window subscribes to
 * ASTRA_WINDOW_SUBSCRIBE_VBLANK.
 * @param window Open window.
 * @return Borrowed wait handle, or ASTRA_INVALID_HANDLE.
 */
ASTRA_NODISCARD AstraHandle astra_window_vblank_wait_handle(
    const AstraWindow *window);
ASTRA_EXTERN_C_END

#endif
