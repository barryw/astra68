#ifndef ASTRA_MENU_H
#define ASTRA_MENU_H

/** Standard menu row height in logical pixels. */
#define ASTRA_MENU_ROW_HEIGHT 28u
/** Menu surface inset around rows in logical pixels. */
#define ASTRA_MENU_PADDING 4u

/** @file menu.h @brief Shared transient menu control and command-backed context menus. */
#include <astra/command.h>
#include <astra/control.h>
#include <astra/surface.h>

ASTRA_EXTERN_C_BEGIN

/** Presentation state for a menu row. */
enum {
    ASTRA_MENU_DISABLED = 1u << 0,
    ASTRA_MENU_CHECKED = 1u << 1,
    ASTRA_MENU_SEPARATOR = 1u << 2,
    ASTRA_MENU_SUBMENU = 1u << 3,
};
/** High-bit shift for a child item's one-based parent row. */
#define ASTRA_MENU_PARENT_SHIFT 16u
/** Mask containing a child item's one-based parent row. */
#define ASTRA_MENU_PARENT_MASK UINT32_C(0xffff0000)
/** Encode a child item's one-based parent row; zero means top-level. */
#define ASTRA_MENU_PARENT(row) ((uint32_t)(row) << ASTRA_MENU_PARENT_SHIFT)
/** Menu placement/interaction policy. */
enum {
    ASTRA_MENU_ATTACHED_TOP = 1u << 0,
    ASTRA_MENU_TRACK_OPEN_PRESS = 1u << 1,
    ASTRA_MENU_KEEP_OPEN = 1u << 2, /**< Row activation keeps the menu active. */
};
/** Immutable presentation row, borrowed for the control's lifetime. */
typedef struct AstraMenuItem {
    const char *label; /**< Counted single-line UTF-8; NULL for a separator. */
    uint32_t label_length; /**< Label byte count, excluding any terminator. */
    uint32_t flags; /**< ASTRA_MENU_* presentation and optional parent row. */
} AstraMenuItem;

/** Caller-owned menu control, independent of an ordinary client layout. */
typedef struct AstraMenu {
    /** @cond ASTRA_INTERNAL */
    uint32_t _private_size;
    const AstraMenuItem *_private_items;
    uint32_t _private_count;
    AstraControlFrame _private_frame;
    uint32_t _private_content_height;
    uint32_t _private_scroll;
    uint32_t _private_selected;
    uint32_t _private_pressed;
    uint32_t _private_flags;
    uint32_t _private_active;
    uint32_t _private_revision;
    /** @endcond */
} AstraMenu;
/** Empty caller-owned menu initializer. */
#define ASTRA_MENU_INIT { sizeof(AstraMenu), 0, 0, {0,0,0,0}, 0,0, UINT32_MAX,UINT32_MAX,0,0,0 }

/** Initialize a transient menu, fitted to the supplied work area.
 * @param menu Caller-owned control.
 * @param items Immutable rows retained until dismissal.
 * @param count Number of rows; no fixed row-count ceiling.
 * @param work Available screen rectangle.
 * @param x Requested screen anchor x.
 * @param y Requested screen anchor y.
 * @param minimum_width Minimum preferred width, capped to @p work.
 * @param flags ASTRA_MENU_ATTACHED_TOP/TRACK_OPEN_PRESS.
 * @return ASTRA_OK or a validation/capacity error; output unchanged on error.
 */
AstraResult astra_interface_menu_init(AstraMenu *menu,
    const AstraMenuItem *items, uint32_t count, const AstraControlFrame *work,
    int32_t x, int32_t y, uint32_t minimum_width, uint32_t flags);
/** Current geometry and interaction state of a transient menu. */
typedef struct AstraMenuState {
    AstraControlFrame frame; /**< Fitted screen bounds. */
    uint32_t selection; /**< Highlighted one-based row, or zero. */
    uint32_t scroll; /**< Content offset in pixels. */
    uint32_t revision; /**< Changes whenever interaction changes presentation. */
    uint32_t active; /**< Nonzero while the control is open. */
} AstraMenuState;
/** Read current menu state. @param menu Initialized control. @param state Receives geometry and state. @return ASTRA_OK or a validation error. */
AstraResult astra_interface_menu_get_state(const AstraMenu *menu, AstraMenuState *state);
/** Locate an enabled row at screen coordinates. @param menu Initialized control. @param x Screen x. @param y Screen y. @return One-based row index, or zero. */
uint32_t astra_interface_menu_hit(const AstraMenu *menu, int32_t x, int32_t y);
/** Screen bounds of a one-based row, including current scroll offset.
 * @param menu Initialized control.
 * @param row One-based row index.
 * @param frame Receives screen bounds.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_menu_row_frame(const AstraMenu *menu,
    uint32_t row, AstraControlFrame *frame);
/** Handle one event using screen pointer coordinates.
 * @param menu Initialized control, updated in place.
 * @param event Pointer, wheel, key or state-reset event.
 * @param action ACTIVATE with one-based row control_id, DISMISS, or NONE.
 * @return ASTRA_OK or a validation error. Selection closes the control unless
 * ASTRA_MENU_KEEP_OPEN was set; dismissal always closes it.
 */
AstraResult astra_interface_menu_handle_event(AstraMenu *menu,
    const AstraWindowEvent *event, AstraUIAction *action);
/** Paint through the common raster/draw-list surface framework.
 * @param menu Initialized control.
 * @param surface Destination matching fitted menu width/height.
 * @return ASTRA_OK or a validation/capacity error.
 */
AstraResult astra_interface_menu_render(const AstraMenu *menu, AstraSurfaceView *surface);

/** Command reference for a context row; a zero reference is a separator. */
typedef struct AstraContextMenuItem {
    const char *command; /**< Stable command identifier, borrowed. */
    uint32_t command_length; /**< Counted command identifier bytes. */
    const AstraCommandArguments *arguments; /**< Borrowed invocation arguments. */
} AstraContextMenuItem;
/** One command-backed popup session per window; references remain caller-owned. */
typedef struct AstraContextMenu {
    /** @cond ASTRA_INTERNAL */
    uint32_t _private_size;
    uint32_t _private_serial;
    uint32_t _private_request;
    AstraWindow *_private_window;
    const AstraCommandScope *_private_scope;
    const AstraContextMenuItem *_private_items;
    uint32_t _private_count;
    /** @endcond */
} AstraContextMenu;
/** Empty command-backed popup session initializer. */
#define ASTRA_CONTEXT_MENU_INIT { sizeof(AstraContextMenu),0,0,0,0,0,0 }
/** Resolve current command state and show a copied menu snapshot.
 * @param menu Caller-owned session; one per window.
 * @param window Owning live window.
 * @param scope Responder chain retained until terminal menu event.
 * @param items Command references retained until terminal menu event.
 * @param count Number of references.
 * @param x Client-local anchor x.
 * @param y Client-local anchor y.
 * @return ASTRA_OK or a specific error; existing session retained on refusal.
 */
AstraResult astra_interface_context_menu_open(AstraContextMenu *menu,
    AstraWindow *window, const AstraCommandScope *scope,
    const AstraContextMenuItem *items, uint32_t count, int32_t x, int32_t y);
/** Open a menu with one level of hover-activated child rows.
 * @param menu Caller-owned session; one per window.
 * @param window Owning live window.
 * @param scope Responder chain retained until terminal menu event.
 * @param items Command references retained until terminal menu event.
 * @param parents Borrowed one-based top-level row indices; zero for top-level rows.
 * Top-level rows precede children, and children for each parent are contiguous.
 * @param count Number of references.
 * @param x Client-local anchor x.
 * @param y Client-local anchor y.
 * @return ASTRA_OK or a specific error; existing session retained on refusal.
 */
AstraResult astra_interface_context_menu_open_tree(AstraContextMenu *menu,
    AstraWindow *window, const AstraCommandScope *scope,
    const AstraContextMenuItem *items, const uint16_t *parents,
    uint32_t count, int32_t x, int32_t y);
/** Consume a terminal menu event and invoke its still-enabled command.
 * @param menu Caller-owned session.
 * @param event Event from its window; stale results are consumed without invocation.
 * @param handled Receives nonzero for a menu result, zero otherwise.
 * @return ASTRA_OK, a command result, or a validation error.
 */
AstraResult astra_interface_context_menu_handle_event(AstraContextMenu *menu,
    const AstraWindowEvent *event, int *handled);
/** Dismiss only this session's current menu. @param menu Caller-owned session. @return ASTRA_OK or a window error. */
AstraResult astra_interface_context_menu_close(AstraContextMenu *menu);

/** A caller-owned session connecting a retained Popup to a copied menu. */
typedef struct AstraPopup {
    /** @cond ASTRA_INTERNAL */
    uint32_t _private_size;
    uint32_t _private_serial;
    uint32_t _private_request;
    AstraWindow *_private_window;
    AstraUIContext *_private_context;
    AstraControl *_private_control;
    uint32_t _private_count;
    /** @endcond */
} AstraPopup;
/** Empty Popup session. */
#define ASTRA_POPUP_INIT { sizeof(AstraPopup), 0, 0, 0, 0, 0, 0 }

/** Open a Popup after its retained control emits ACTIVATE. Selection is
 * aligned to the field when screen room permits. The display copies rows.
 * @param popup Caller-owned Popup session.
 * @param context UI context owning control.
 * @param control Retained Popup control.
 * @param window Window receiving the menu result.
 * @return ASTRA_OK or a validation, capacity, or window error.
 */
AstraResult astra_interface_popup_open(AstraPopup *popup,
    AstraUIContext *context, AstraControl *control, AstraWindow *window);
/** Consume a correlated result and update the retained choice. On a changed
 * selection, action is VALUE_CHANGED. Other menu results are left untouched.
 * @param popup Active Popup session.
 * @param event Window event to inspect.
 * @param action Receives value-change action on selection.
 * @param handled Receives nonzero when the event belongs to this Popup.
 * @return ASTRA_OK or a validation or window error.
 */
AstraResult astra_interface_popup_handle_event(AstraPopup *popup,
    const AstraWindowEvent *event, AstraUIAction *action, int *handled);
/** Close this Popup's pending menu.
 * @param popup Active Popup session.
 * @return ASTRA_OK or a validation or window error.
 */
AstraResult astra_interface_popup_close(AstraPopup *popup);

ASTRA_EXTERN_C_END
#endif
