#ifndef ASTRA_TOOLBAR_ACTIONS_H
#define ASTRA_TOOLBAR_ACTIONS_H

/** @file toolbar_actions.h @brief Command-backed, width-fitted toolbar groups. */
#include <astra/command.h>
#include <astra/control.h>
#include <astra/menu.h>

ASTRA_EXTERN_C_BEGIN

enum {
    ASTRA_TOOLBAR_GROUP_PINNED = 1u << 0, /**< Keep this group visible. */
    ASTRA_TOOLBAR_LABELS = 0u, /**< Show glyph and label on each action. */
    ASTRA_TOOLBAR_ICONS_ONLY = 1u /**< Show glyphs with labels retained as names. */
};

/** One stable action declaration. Labels stay borrowed for the control lifetime. */
typedef struct AstraToolbarCommand {
    const char *command; /**< Borrowed command ID. */
    uint32_t command_length; /**< Counted command ID bytes. */
    const char *label; /**< Borrowed stable display label. */
    uint32_t label_length; /**< Counted label bytes. */
    uint32_t icon; /**< ASTRA_TOOLBAR_ICON_*; required for icon mode. */
    uint32_t reserved[2]; /**< Must be zero. */
} AstraToolbarCommand;

/** Construction data for a standalone command-backed Button Group. */
typedef struct AstraButtonGroupInfo {
    uint32_t size; /**< Structure size. */
    uint32_t first_id; /**< Group Container ID; Button IDs follow. */
    const AstraToolbarCommand *actions; /**< Borrowed action descriptions. */
    uint32_t action_count; /**< Number of Buttons in the group. */
    const AstraCommandScope *scope; /**< Borrowed command responder chain. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraButtonGroupInfo;
/** Empty Button Group construction data. */
#define ASTRA_BUTTON_GROUP_INFO_INIT { \
    sizeof(AstraButtonGroupInfo), 0, 0, 0, 0, {0,0,0,0} }

/** Caller-owned standalone Button Group controller. */
typedef struct AstraButtonGroup {
    /** @cond ASTRA_INTERNAL */
    uint32_t _private_size;
    AstraControl *_private_controls;
    const AstraToolbarCommand *_private_actions;
    uint32_t _private_action_count;
    const AstraCommandScope *_private_scope;
    uint32_t _private_first_id;
    /** @endcond */
} AstraButtonGroup;
/** Empty standalone Button Group. */
#define ASTRA_BUTTON_GROUP_INIT { sizeof(AstraButtonGroup), 0, 0, 0, 0, 0 }

/** Compose a row Container and consecutive Buttons.
 * @param group Empty caller-owned group controller.
 * @param controls Array of action_count + 1 caller-owned control slots.
 * @param info Valid group construction data; descriptions remain borrowed.
 * @return ASTRA_OK or a validation/initialization error.
 */
AstraResult astra_interface_button_group_init(AstraButtonGroup *group,
    AstraControl *controls, const AstraButtonGroupInfo *info);
/** Synchronize Button state with command enabled and checked state.
 * @param group Initialized Button Group.
 * @param context UI context retaining its controls.
 * @return ASTRA_OK or a query/control error.
 */
AstraResult astra_interface_button_group_refresh(AstraButtonGroup *group,
    AstraUIContext *context);
/** Invoke the group's command for a Button activation.
 * @param group Initialized Button Group.
 * @param action UI action from the retained context.
 * @param handled Receives nonzero for this group's Button action.
 * @return ASTRA_OK or a command error.
 */
AstraResult astra_interface_button_group_handle_action(
    AstraButtonGroup *group, const AstraUIAction *action, int *handled);

/** Contiguous actions that fit or fold as a unit. Higher priority folds first. */
typedef struct AstraToolbarGroup {
    const char *title; /**< Borrowed group name. */
    uint32_t title_length; /**< Counted group name bytes. */
    uint32_t first_action; /**< First action in the contiguous action array. */
    uint32_t action_count; /**< Number of actions in this group. */
    uint32_t priority; /**< Larger values enter overflow first. */
    uint32_t flags; /**< ASTRA_TOOLBAR_GROUP_PINNED or zero. */
    uint32_t reserved[2]; /**< Must be zero. */
} AstraToolbarGroup;

/** Construction data for one composed, command-backed toolbar. */
typedef struct AstraToolbarActionsInfo {
    uint32_t size; /**< Structure size. */
    uint32_t first_id; /**< Root toolbar Container ID. */
    const AstraToolbarGroup *groups; /**< Borrowed ordered groups. */
    uint32_t group_count; /**< Number of groups. */
    const AstraToolbarCommand *actions; /**< Borrowed ordered actions. */
    uint32_t action_count; /**< Number of actions. */
    const AstraCommandScope *scope; /**< Borrowed responder chain. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraToolbarActionsInfo;
/** Empty toolbar construction data. */
#define ASTRA_TOOLBAR_ACTIONS_INFO_INIT { \
    sizeof(AstraToolbarActionsInfo), 0, 0, 0, 0, 0, 0, {0,0,0,0} }

/** Caller-owned controller; controls and declarations remain caller-owned. */
typedef struct AstraToolbarActions {
    /** @cond ASTRA_INTERNAL */
    uint32_t _private_size;
    AstraControl *_private_controls;
    const AstraToolbarGroup *_private_groups;
    uint32_t _private_group_count;
    const AstraToolbarCommand *_private_actions;
    uint32_t _private_action_count;
    const AstraCommandScope *_private_scope;
    uint32_t _private_first_id;
    uint32_t _private_mode;
    AstraContextMenu _private_overflow;
    AstraContextMenuItem *_private_overflow_items;
    uint32_t _private_overflow_count;
    /** @endcond */
} AstraToolbarActions;
/** Empty caller-owned toolbar controller. */
#define ASTRA_TOOLBAR_ACTIONS_INIT { \
    sizeof(AstraToolbarActions), 0, 0, 0, 0, 0, 0, 0, ASTRA_TOOLBAR_LABELS, \
    ASTRA_CONTEXT_MENU_INIT, 0, 0 }

/** Result of width fitting. */
typedef struct AstraToolbarFit {
    uint32_t size; /**< Structure size. */
    uint32_t mode; /**< Actual ASTRA_TOOLBAR_LABELS/ICONS_ONLY mode. */
    uint32_t hidden_groups; /**< Entire groups in overflow. */
    uint32_t required_width; /**< Width of the resulting toolbar. */
    uint32_t minimum_width; /**< Pinned groups plus overflow. */
    uint32_t reserved[3]; /**< Must be zero. */
} AstraToolbarFit;
/** Empty width-fit result. */
#define ASTRA_TOOLBAR_FIT_INIT { sizeof(AstraToolbarFit), 0, 0, 0, 0, {0,0,0} }

/** Count required controls: root, one Container per group, one Button per
 * action, and the overflow Button.
 * @param info Valid toolbar construction data.
 * @return Required control count, or zero for invalid data.
 */
uint32_t astra_interface_toolbar_actions_control_count(
    const AstraToolbarActionsInfo *info);
/** Compose the caller-owned controls without opening a window.
 * @param toolbar Empty caller-owned controller.
 * @param controls Array of control_count slots, retained by the controller.
 * @param info Valid construction data; descriptions remain borrowed.
 * @return ASTRA_OK or a validation/initialization error.
 */
AstraResult astra_interface_toolbar_actions_init(AstraToolbarActions *toolbar,
    AstraControl *controls, const AstraToolbarActionsInfo *info);
/** Query command state and update Button enabled/selected state.
 * @param toolbar Initialized controller.
 * @param context UI context retaining its controls.
 * @return ASTRA_OK or a query/control error.
 */
AstraResult astra_interface_toolbar_actions_refresh(AstraToolbarActions *toolbar,
    AstraUIContext *context);
/** Fit labels, then icons, then whole groups. Ties fold rightmost first.
 * @param toolbar Initialized controller.
 * @param context UI context retaining its controls.
 * @param available_width Current toolbar width in logical pixels.
 * @param preferred_mode ASTRA_TOOLBAR_LABELS or ASTRA_TOOLBAR_ICONS_ONLY.
 * @param fit Receives actual mode, hidden count, and required/minimum width.
 * @return ASTRA_OK or ASTRA_ERROR_NO_RESOURCES when below minimum width;
 * fit.minimum_width is supplied on that error.
 */
AstraResult astra_interface_toolbar_actions_fit(AstraToolbarActions *toolbar,
    AstraUIContext *context, uint32_t available_width, uint32_t preferred_mode,
    AstraToolbarFit *fit);
/** Invoke an action or open the overflow menu when its Button activates.
 * @param toolbar Initialized controller.
 * @param window Owning window, required for overflow.
 * @param action UI action from the retained control context.
 * @param handled Receives nonzero when the action belongs to this toolbar.
 * @return ASTRA_OK or a command/menu error.
 */
AstraResult astra_interface_toolbar_actions_handle_action(
    AstraToolbarActions *toolbar, AstraWindow *window,
    const AstraUIAction *action, int *handled);
/** Pass correlated results through the shared Context Menu session.
 * @param toolbar Initialized controller.
 * @param event Window event to consider.
 * @param handled Receives nonzero for this toolbar's result.
 * @return ASTRA_OK or a command/menu error.
 */
AstraResult astra_interface_toolbar_actions_handle_event(
    AstraToolbarActions *toolbar, const AstraWindowEvent *event, int *handled);
/** Close the pending overflow menu, if any.
 * @param toolbar Initialized or empty controller.
 * @return ASTRA_OK or a window/menu error.
 */
AstraResult astra_interface_toolbar_actions_close(AstraToolbarActions *toolbar);

ASTRA_EXTERN_C_END
#endif
