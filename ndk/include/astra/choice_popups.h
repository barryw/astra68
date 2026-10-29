#ifndef ASTRA_CHOICE_POPUPS_H
#define ASTRA_CHOICE_POPUPS_H

/** @file choice_popups.h @brief Editable and multiple-choice menu compositions. */
#include <astra/command.h>
#include <astra/control.h>
#include <astra/menu.h>
#include <astra/text_model.h>

ASTRA_EXTERN_C_BEGIN

/** Caller-owned construction data for a Field plus dropdown Button. */
typedef struct AstraComboBoxInfo {
    uint32_t size; /**< Structure size. */
    uint32_t first_id; /**< Container ID; Field and Button use the next IDs. */
    AstraTextModel *model; /**< Borrowed editable single-line TextModel. */
    const AstraChoiceItem *items; /**< Borrowed suggestions. */
    uint32_t item_count; /**< Suggestion count. */
    uint32_t preferred_columns; /**< Field's intrinsic width. */
    uint32_t state; /**< Disabled/error state for the Field and Button. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraComboBoxInfo;
/** Empty Combo Box construction data. */
#define ASTRA_COMBO_BOX_INFO_INIT { \
    sizeof(AstraComboBoxInfo), 0, 0, 0, 0, 20u, 0, {0,0,0,0} }

/** Caller-owned Combo Box controller; three controls remain caller-owned. */
typedef struct AstraComboBox {
    /** @cond ASTRA_INTERNAL */
    uint32_t _private_size;
    uint32_t _private_serial;
    uint32_t _private_request;
    AstraControl *_private_controls;
    AstraUIContext *_private_context;
    AstraWindow *_private_window;
    AstraTextModel *_private_model;
    const AstraChoiceItem *_private_items;
    uint32_t _private_item_count;
    uint32_t *_private_visible;
    uint32_t _private_visible_count;
    /** @endcond */
} AstraComboBox;
/** Empty Combo Box controller. */
#define ASTRA_COMBO_BOX_INIT { sizeof(AstraComboBox),0,0,0,0,0,0,0,0,0,0 }

/** Initialize a Container, editable Field, and menu Button in three slots.
 * @param combo Empty caller-owned controller.
 * @param controls Three consecutive caller-owned control slots.
 * @param info Borrowed model and suggestion descriptions.
 * @return ASTRA_OK or a validation/initialization error.
 */
AstraResult astra_interface_combo_box_init(AstraComboBox *combo,
    AstraControl *controls, const AstraComboBoxInfo *info);
/** Open suggestions whose labels begin with the Field's current text.
 * Matching is ASCII case-insensitive and preserves declaration order.
 * @param combo Initialized controller.
 * @param context UI context retaining the three controls.
 * @param window Window receiving the menu result.
 * @return ASTRA_ERROR_NOT_PRESENT when no suggestions match.
 */
AstraResult astra_interface_combo_box_open(AstraComboBox *combo,
    AstraUIContext *context, AstraWindow *window);
/** Consume a correlated menu result. Selection replaces the entire Field
 * model and emits VALUE_CHANGED with the selected source item index.
 * @param combo Initialized controller.
 * @param event Window event to inspect.
 * @param action Receives VALUE_CHANGED on selection.
 * @param handled Receives nonzero for this Combo Box's result.
 * @return ASTRA_OK or a model/window error.
 */
AstraResult astra_interface_combo_box_handle_event(AstraComboBox *combo,
    const AstraWindowEvent *event, AstraUIAction *action, int *handled);
/** Close the pending suggestions menu and release its index map.
 * @param combo Initialized controller.
 * @return ASTRA_OK or a window error.
 */
AstraResult astra_interface_combo_box_close(AstraComboBox *combo);

/** Command-backed persistent multiple-choice dropdown trigger. Each command
 * query supplies its label, enabled, and checked state. Invocation must toggle
 * checked state when enabled.
 */
typedef struct AstraMultiPopupInfo {
    uint32_t size; /**< Structure size. */
    uint32_t id; /**< Trigger Button ID. */
    const char *label; /**< Borrowed trigger label. */
    uint32_t label_length; /**< Counted label bytes. */
    const AstraContextMenuItem *items; /**< Borrowed command references. */
    uint32_t item_count; /**< Number of toggle commands. */
    const AstraCommandScope *scope; /**< Borrowed responder chain. */
    uint32_t state; /**< Disabled/collapsed Button state. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraMultiPopupInfo;
/** Empty multi-select Popup construction data. */
#define ASTRA_MULTI_POPUP_INFO_INIT { \
    sizeof(AstraMultiPopupInfo),0,0,0,0,0,0,0,{0,0,0,0} }

typedef struct AstraMultiPopup {
    /** @cond ASTRA_INTERNAL */
    uint32_t _private_size;
    uint32_t _private_serial;
    uint32_t _private_request;
    AstraControl *_private_control;
    AstraUIContext *_private_context;
    AstraWindow *_private_window;
    const AstraContextMenuItem *_private_items;
    uint32_t _private_item_count;
    const AstraCommandScope *_private_scope;
    /** @endcond */
} AstraMultiPopup;
/** Empty multi-select Popup controller. */
#define ASTRA_MULTI_POPUP_INIT { sizeof(AstraMultiPopup),0,0,0,0,0,0,0,0 }

/** Initialize the retained trigger Button; caller retains the controller.
 * @param popup Empty caller-owned controller.
 * @param control Caller-owned Button slot.
 * @param info Borrowed command scope and item descriptions.
 * @return ASTRA_OK or a validation/initialization error.
 */
AstraResult astra_interface_multi_popup_init(AstraMultiPopup *popup,
    AstraControl *control, const AstraMultiPopupInfo *info);
/** Query all commands, update trigger selection, and return checked count.
 * @param popup Initialized controller.
 * @param context UI context retaining the Button.
 * @param checked_count Optional checked-count result.
 * @return ASTRA_OK or a command/control error.
 */
AstraResult astra_interface_multi_popup_refresh(AstraMultiPopup *popup,
    AstraUIContext *context, uint32_t *checked_count);
/** Open a persistent menu using current command states.
 * @param popup Initialized controller.
 * @param context UI context retaining the Button.
 * @param window Window receiving continued and terminal menu results.
 * @return ASTRA_OK or a command/window error.
 */
AstraResult astra_interface_multi_popup_open(AstraMultiPopup *popup,
    AstraUIContext *context, AstraWindow *window);
/** Invoke a continued selection while retaining the menu; terminal dismissal
 * clears the session. Emits VALUE_CHANGED with the new checked count.
 * @param popup Initialized controller.
 * @param event Window event to inspect.
 * @param action Receives VALUE_CHANGED for a continued selection.
 * @param handled Receives nonzero for this Popup's result.
 * @return ASTRA_OK or a command/window error.
 */
AstraResult astra_interface_multi_popup_handle_event(AstraMultiPopup *popup,
    const AstraWindowEvent *event, AstraUIAction *action, int *handled);
/** Close the pending menu, if any.
 * @param popup Initialized controller.
 * @return ASTRA_OK or a window error.
 */
AstraResult astra_interface_multi_popup_close(AstraMultiPopup *popup);

ASTRA_EXTERN_C_END
#endif
