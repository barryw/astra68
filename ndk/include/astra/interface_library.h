/** @file interface_library.h @brief Interface Kit direct-symbol API. */
#ifndef ASTRA_INTERFACE_LIBRARY_H
#define ASTRA_INTERFACE_LIBRARY_H

#include <astra/clipboard.h>
#include <astra/command.h>
#include <astra/choice_popups.h>
#include <astra/control.h>
#include <astra/field_family.h>
#include <astra/icon_tile.h>
#include <astra/image_view.h>
#include <astra/interface.h>
#include <astra/inline_edit.h>
#include <astra/menu.h>
#include <astra/scroll.h>
#include <astra/system_panels.h>
#include <astra/text_model.h>
#include <astra/text_surface.h>
#include <astra/toolbar_actions.h>
#include <astra/undo.h>
#include <astra/window.h>

/** Breaking Interface Kit ABI generation carried by interface.library.12. */
#define ASTRA_INTERFACE_LIBRARY_ABI_MAJOR 12u
/** Current Interface Kit revision. */
#define ASTRA_INTERFACE_LIBRARY_ABI_MINOR 10u

/** Display a modal alert. @param gui GUI capability. @param info Alert description. @return ASTRA_OK or a specific error. */
AstraResult astra_interface_show_alert(AstraHandle gui,
                                       const AstraAlertInfo *info);

/** Show a two-button modal confirmation using shared Label, Container and
 * Button controls. Closing the window or choosing cancel leaves confirmed
 * zero. Only activation of info->button sets it to one.
 * @param gui GUI capability.
 * @param info Alert text and affirmative button label.
 * @param cancel Counted UTF-8 cancellation button label.
 * @param cancel_length Bytes in cancel.
 * @param confirmed Receives one only after affirmative activation.
 * @return ASTRA_OK or a specific error.
 * @since Interface Kit 12.10.
 */
AstraResult astra_interface_confirm(AstraHandle gui,
    const AstraAlertInfo *info, const char *cancel,
    uint16_t cancel_length, uint32_t *confirmed);

/** Initialize a retained label.
 * @param control Caller-owned control to initialize.
 * @param info Label description whose referenced text remains caller-owned.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_label_init(AstraControl *control,
                                       const AstraLabelInfo *info);
/** Change a Label's installed face/size, preserving focus and retained layout.
 * Failure keeps its previous font. Ordinary theme labels use the empty font.
 * @param context Owning UI context.
 * @param control Label to update.
 * @param font Copied descriptor; referenced ID remains caller-owned.
 * @return ASTRA_OK or a validation, metric or layout error. */
AstraResult astra_interface_label_set_font(AstraUIContext *context,
    AstraControl *control, const AstraLabelFont *font);
/** Initialize a retained button.
 * @param control Caller-owned control to initialize.
 * @param info Button description whose referenced text remains caller-owned.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_button_init(AstraControl *control,
                                        const AstraButtonInfo *info);
/** Initialize a disclosure header.
 * @param control Caller-owned control to initialize.
 * @param info Disclosure description retained by value.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_disclosure_init(
    AstraControl *control, const AstraDisclosureInfo *info);
/** Initialize a nonvisual retained container.
 * @param control Caller-owned control to initialize.
 * @param info Container description retained by value.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_container_init(AstraControl *control,
                                           const AstraContainerInfo *info);
/** Initialize a retained bottom-up channel meter; model storage stays caller-owned.
 * @param control Caller-owned control.
 * @param info Borrowed model and control state.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_level_meter_init(AstraControl *control,
    const AstraDisplayInfo *info);
/** Initialize a retained per-sample bar sparkline.
 * @param control Caller-owned control.
 * @param info Borrowed model and control state.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_sparkline_init(AstraControl *control,
    const AstraDisplayInfo *info);
/** Validate changed display model data and mark its frame damaged.
 * @param context Owning UI context.
 * @param control Level Meter or Sparkline control.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_display_refresh(AstraUIContext *context,
    AstraControl *control);
/** Compose a themed status strip from one Container and two Labels.
 * @param controls Three caller-owned consecutive control slots.
 * @param info Status labels and first control ID.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_status_bar_init(AstraControl *controls,
    const AstraStatusBarInfo *info);
/** Compose a shared settings shell from four existing controls.
 * Place these controls and the application's BODY/ACTIONS children in one UI
 * array, then use ordinary ui_init/layout/render/event APIs with
 * ASTRA_SETTINGS_PANE_LAYOUT_INIT. The shell adds no event loop or renderer.
 * @param controls Four caller-owned slots, indexed by ASTRA_SETTINGS_PANE_*.
 * @param info Pane text and reserved consecutive IDs.
 * @return ASTRA_OK or a validation error. Discard controls after failure.
 * @since Interface Kit 7.2.
 */
AstraResult astra_interface_settings_pane_init(AstraControl *controls,
                                               const AstraSettingsPaneInfo *info);
/** Compose a form row from five retained controls. Place an application
 * control under first_id + ASTRA_FORM_ROW_VALUE and the row root under the
 * desired settings BODY container. The message can be hidden or use the
 * shared fault text role.
 * @param controls Five caller-owned consecutive control slots.
 * @param info Row labels, ID, and shared label width.
 * @return ASTRA_OK or a validation error.
 * @since Interface Kit 9.0.
 */
AstraResult astra_interface_form_row_init(AstraControl *controls,
                                          const AstraFormRowInfo *info);
/** Compose a row toolbar from one Container and action_count Buttons.
 * Controls occupy action_count + 1 consecutive slots and IDs. Caller owns
 * labels and handles ordinary button activation.
 * @param controls Caller-owned slots for the Container and Buttons.
 * @param info Toolbar action descriptions.
 * @return ASTRA_OK or a validation error.
 * @since Interface Kit 7.5.
 */
AstraResult astra_interface_toolbar_init(AstraControl *controls,
                                         const AstraToolbarInfo *info);
/** Add shared glyphs to an initialized toolbar's action Buttons. Zero keeps
 * an action text-only. Icons and labels remain independent for future display
 * modes. @param controls Toolbar Container followed by action Buttons.
 * @param icons One ASTRA_TOOLBAR_ICON_* value per action.
 * @param count Number of action Buttons.
 * @return ASTRA_OK or a validation error. @since Interface Kit 7.6. */
AstraResult astra_interface_toolbar_set_icons(AstraControl *controls,
    const uint32_t *icons, uint32_t count);
/** Focus one enabled, visible control by ID after layout.
 * @param context Initialized UI context.
 * @param control_id Target control ID.
 * @return ASTRA_OK or a validation error.
 * @since Interface Kit 7.5.
 */
AstraResult astra_interface_ui_focus_control(AstraUIContext *context,
                                             uint32_t control_id);
/** Query focused control ID. Zero means no control owns keyboard focus.
 * @param context Initialized UI context.
 * @param control_id Receives focused control ID or zero.
 * @return ASTRA_OK or ASTRA_ERROR_INVALID_ARGUMENT.
 * @since Interface Kit 10.0.
 */
AstraResult astra_interface_ui_focused_control(const AstraUIContext *context,
                                              uint32_t *control_id);
/** Initialize a scroll viewport.
 * @param control Caller-owned control to initialize.
 * @param info View description; its scroll model remains caller-owned.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_scroll_view_init(
    AstraControl *control, const AstraScrollViewInfo *info);
/** Initialize a scrollbar.
 * @param control Caller-owned control to initialize.
 * @param info Scrollbar description; its scroll model remains caller-owned.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_scrollbar_init(
    AstraControl *control, const AstraScrollbarInfo *info);
/** Initialize a split-view divider.
 * @param control Caller-owned control to initialize.
 * @param info Splitter description retained by value.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_splitter_init(
    AstraControl *control, const AstraSplitterInfo *info);
/** Initialize a retained checkbox.
 * @param control Caller-owned control to initialize.
 * @param info Toggle description whose referenced text remains caller-owned.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_checkbox_init(AstraControl *control,
                                          const AstraToggleInfo *info);
/** Initialize a retained radio button.
 * @param control Caller-owned control to initialize.
 * @param info Toggle description whose referenced text remains caller-owned.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_radio_init(AstraControl *control,
                                       const AstraToggleInfo *info);
/** Initialize a retained switch.
 * @param control Caller-owned control to initialize.
 * @param info Toggle description whose referenced text remains caller-owned.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_switch_init(AstraControl *control,
                                        const AstraToggleInfo *info);
/** Initialize a retained slider.
 * @param control Caller-owned control to initialize.
 * @param info Range description retained by value.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_slider_init(AstraControl *control,
                                        const AstraRangeInfo *info);
/** Initialize a vertically dragged dial.
 * @param control Caller-owned control to initialize.
 * @param info Dial description retained by value.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_dial_init(AstraControl *control,
                                      const AstraDialInfo *info);
/** Initialize a numeric stepper.
 * @param control Caller-owned control to initialize.
 * @param info Stepper description retained by value.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_stepper_init(AstraControl *control,
                                         const AstraStepperInfo *info);
/** Initialize a progress indicator.
 * @param control Caller-owned control to initialize.
 * @param info Progress description retained by value.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_progress_init(AstraControl *control,
                                          const AstraProgressInfo *info);
/** Initialize a segmented selector.
 * @param control Caller-owned control to initialize.
 * @param info Choice description whose item array remains caller-owned.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_segmented_init(AstraControl *control,
                                           const AstraChoiceInfo *info);
/** Use shared 16 px glyphs in a segmented selector while retaining each
 * choice's text label for its semantic identity. Call before UI attachment;
 * the icon array must remain alive for the control's lifetime.
 * @param control Initialized segmented selector.
 * @param icons One nonzero ASTRA_TOOLBAR_ICON_* value per choice.
 * @param count Number of icons; must match the choice count.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_segmented_set_icons(AstraControl *control,
    const uint32_t *icons, uint32_t count);
/** Initialize a tab strip.
 * @param control Caller-owned control to initialize.
 * @param info Choice description whose item array remains caller-owned.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_tab_init(AstraControl *control,
                                     const AstraChoiceInfo *info);
/** Initialize a collection model with unique, stable item IDs.
 * Item array and labels remain caller-owned. ID zero means no selection.
 * @param model Caller-owned model to initialize.
 * @param items Borrowed item array, or NULL when count is zero.
 * @param count Number of items.
 * @param selected_id Initial selected ID, or zero for none.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_collection_init(AstraCollectionModel *model,
    const AstraCollectionItem *items, uint32_t count, uint64_t selected_id);
/** Prepare caller-owned stable-ID multi-selection storage.
 * @param selection Selection object to initialize.
 * @param ids Caller-owned ID array, or NULL for zero capacity.
 * @param capacity Number of ID slots.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_selection_init(AstraSelectionSet *selection,
    uint64_t *ids, uint32_t capacity);
/** Enable Ctrl/Command toggle and Shift range selection on a collection.
 * Storage capacity must cover every item in the collection. One set may be
 * shared across views of the same items; a second model may start unselected.
 * @param model Collection model to bind.
 * @param selection Initialized caller-owned selection set.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_collection_multi_bind(AstraCollectionModel *model,
    AstraSelectionSet *selection);
/** Test membership in the caller-owned, sorted stable-ID selection.
 * @param selection Selection set to inspect.
 * @param id Stable nonzero item ID.
 * @return One if selected, zero otherwise.
 */
uint32_t astra_interface_selection_contains(const AstraSelectionSet *selection,
    uint64_t id);
/** Initialize a single-selection vertical List over a collection model.
 * Compose with ScrollView for overflow. Model remains caller-owned and must
 * be bound to only one active collection control.
 * @param control Caller-owned control to initialize.
 * @param info List description.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_list_init(AstraControl *control,
                                      const AstraListInfo *info);
/** Replace List items, preserving selection by stable item ID.
 * Selection clears when selected item disappears. Failed updates leave prior
 * model and layout intact.
 * @param context Owning UI context.
 * @param control List bound to one collection model.
 * @param items Replacement borrowed item array, or NULL when count is zero.
 * @param count Number of items.
 * @return ASTRA_OK or a validation or layout error.
 */
AstraResult astra_interface_list_set_items(AstraUIContext *context,
    AstraControl *control, const AstraCollectionItem *items, uint32_t count);
/** Initialize a menu-backed single-choice field with caller-owned items.
 * @param control Caller-owned control.
 * @param info Borrowed choices and initial selection.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_popup_init(AstraControl *control,
                                      const AstraChoiceInfo *info);
/** Compose a toolbar primary action and menu segment. Place adjacent in a
 * zero-gap row; open a shared Context Menu when the menu ID activates.
 * @param primary Caller-owned primary Button slot.
 * @param menu Caller-owned menu Button slot.
 * @param primary_info Primary Button description.
 * @param menu_id Nonzero ID for the menu Button.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_split_button_init(AstraControl *primary,
    AstraControl *menu, const AstraButtonInfo *primary_info, uint32_t menu_id);
/** Compose a column browser from C1 Lists and ScrollViews. Every column uses
 * the canonical collection selection engine and caller-owned scroll model.
 * @param controls Array of at least 1 + 2 * column_count caller-owned slots.
 * @param info Borrowed column descriptions and viewport height.
 * @return ASTRA_OK or validation error; discard controls on failure.
 */
AstraResult astra_interface_columns_init(AstraControl *controls,
    const AstraColumnsInfo *info);
/** Compose Previous/Label/Next controls over one C1 collection page state.
 * Supply four caller-owned control slots. Button activation IDs are
 * first_id+1 and first_id+3; call set_page in response.
 * @param controls Array of four caller-owned control slots.
 * @param info Pagination composition description.
 * @return ASTRA_OK or validation error.
 */
AstraResult astra_interface_pagination_init(AstraControl *controls,
    const AstraPaginationInfo *info);
/** Return the current half-open range and total page count. Empty collections
 * have one empty page. The range is recalculated from the C1 item count.
 * @param model Caller-owned Pagination state.
 * @param first Receives first included item index.
 * @param end Receives first excluded item index.
 * @param page_count Receives current total page count.
 * @return ASTRA_OK or validation error.
 */
AstraResult astra_interface_pagination_range(
    const AstraPaginationModel *model, uint32_t *first, uint32_t *end,
    uint32_t *page_count);
/** Change the current page and update the composed Label and Button states.
 * The model and controls remain unchanged if layout rejects the label update.
 * @param context Owning retained UI context.
 * @param controls Four composed controls from pagination_init.
 * @param model Caller-owned Pagination state.
 * @param page_index New zero-based page index.
 * @return ASTRA_OK or validation/layout error.
 */
AstraResult astra_interface_pagination_set_page(AstraUIContext *context,
    AstraControl *controls, AstraPaginationModel *model, uint32_t page_index);
/** Initialize a retained Table over C1 item IDs and caller-owned columns.
 * Column zero uses each CollectionItem label. The remaining cells are
 * row-major. Compose with ScrollView for large tables.
 * @param control Caller-owned retained control.
 * @param info Table description with borrowed model.
 * @return ASTRA_OK or validation error.
 */
AstraResult astra_interface_table_init(AstraControl *control,
    const AstraTableInfo *info);
/** Atomically replace Table rows and trailing cells, preserving selection by
 * C1 stable ID. Failed updates keep the prior presentation and selection.
 * @param context Owning UI context.
 * @param control Table to update.
 * @param items Borrowed new row identities and labels.
 * @param count Number of rows.
 * @param cells Borrowed trailing cells in row-major order.
 * @param cell_count Number of trailing cells.
 * @return ASTRA_OK or validation/layout error.
 */
AstraResult astra_interface_table_set_rows(AstraUIContext *context,
    AstraControl *control, const AstraCollectionItem *items, uint32_t count,
    const AstraTableCell *cells, uint32_t cell_count);
/** Build a Table with nested, expandable visible rows. Uses the ordinary
 * Table columns and C1 selection. Caller owns full hierarchy, lazy loading,
 * and row replacement after EXPANSION_REQUESTED. Available since 12.6.
 * @param control Caller-owned retained control.
 * @param info Borrowed Table and row metadata.
 * @return ASTRA_OK or validation error.
 */
AstraResult astra_interface_outline_table_init(AstraControl *control,
    const AstraOutlineTableInfo *info);
/** Atomically replace visible Outline Table rows, metadata, and cells.
 * Selection follows stable C1 IDs. On failure, prior rows remain bound.
 * @param context Owning UI context.
 * @param control Outline Table to update.
 * @param items Borrowed visible row identities and labels.
 * @param count Number of visible rows.
 * @param cells Borrowed trailing cells in row-major order.
 * @param cell_count Number of trailing cells.
 * @param rows Borrowed depth and disclosure metadata, one per visible row.
 * @return ASTRA_OK or validation/layout error.
 */
AstraResult astra_interface_outline_table_set_rows(AstraUIContext *context,
    AstraControl *control, const AstraCollectionItem *items, uint32_t count,
    const AstraTableCell *cells, uint32_t cell_count,
    const AstraOutlineTableRow *rows);
/** Show the caller's active sort column and direction in a Table header.
 * This changes presentation only; caller owns row ordering. Pass UINT32_MAX
 * as column to clear the indicator. Available since Interface Kit 12.1.
 * @param context Owning retained UI context.
 * @param control Table to update.
 * @param column Zero-based column or UINT32_MAX to clear.
 * @param reverse Zero for ascending, one for descending.
 * @return ASTRA_OK or validation error.
 */
AstraResult astra_interface_table_set_sort(AstraUIContext *context,
    AstraControl *control, uint32_t column, uint32_t reverse);
/** Initialize a retained Tree over one C1 collection and preorder hierarchy.
 * Computes depth and visible rows in the caller-owned workspace.
 * @param control Caller-owned retained control.
 * @param info Tree description with borrowed model.
 * @return ASTRA_OK or validation error.
 */
AstraResult astra_interface_tree_init(AstraControl *control,
    const AstraTreeInfo *info);
/** Expand or collapse a branch by stable item ID. A hidden selected descendant
 * selects its newly visible ancestor. A leaf returns INVALID_ARGUMENT.
 * @param context Owning UI context.
 * @param control Tree to update.
 * @param item_id Stable C1 item ID of branch.
 * @param expanded Zero to collapse, one to expand.
 * @return ASTRA_OK or validation/layout error.
 */
AstraResult astra_interface_tree_set_expanded(AstraUIContext *context,
    AstraControl *control, uint64_t item_id, uint32_t expanded);
/** Replace the full preorder hierarchy transactionally, preserving the C1
 * selected ID when it remains visible.
 * @param context Owning UI context.
 * @param control Tree to update.
 * @param items Borrowed new C1 items.
 * @param count Item and node count.
 * @param nodes Mutable parallel parent/expanded metadata.
 * @param visible_rows Caller-owned workspace with at least count entries.
 * @param visible_capacity Number of entries in visible_rows.
 * @return ASTRA_OK or validation/layout error.
 */
AstraResult astra_interface_tree_set_nodes(AstraUIContext *context,
    AstraControl *control, const AstraCollectionItem *items, uint32_t count,
    AstraTreeNode *nodes, uint32_t *visible_rows,
    uint32_t visible_capacity);
/** Initialize a retained icon Grid over C1 selection and parallel borrowed
 * 64x64 icon surfaces. Compose with ScrollView for overflow.
 * @param control Caller-owned retained control.
 * @param info Icon Grid description.
 * @return ASTRA_OK or validation error.
 */
AstraResult astra_interface_icon_grid_init(AstraControl *control,
    const AstraIconGridInfo *info);
/** Replace icon Grid items and icons together, preserving selected stable ID.
 * A failed validation or layout leaves the previous presentation intact.
 * @param context Owning retained UI context.
 * @param control Icon Grid control to update.
 * @param items Borrowed new C1 items.
 * @param count Number of items and icons.
 * @param icons Borrowed parallel icon surfaces; entries may be NULL.
 * @return ASTRA_OK or validation/layout error.
 */
AstraResult astra_interface_icon_grid_set_items(AstraUIContext *context,
    AstraControl *control, const AstraCollectionItem *items, uint32_t count,
    const AstraSurface *const *icons);
/** Initialize a single-line UTF-8 field.
 * @param control Caller-owned control to initialize.
 * @param info Field description; its text model remains caller-owned.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_field_init(AstraControl *control,
                                       const AstraFieldInfo *info);
/** Use Field/TextModel editing with masked draw-list glyphs and no copy/cut.
 * @param control Caller-owned control to initialize.
 * @param info Field description with caller-owned model.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_secure_field_init(AstraControl *control,
    const AstraFieldInfo *info);
/** Use Field/TextModel editing with search and clear affordances.
 * Escape or clicking the clear affordance empties the model.
 * @param control Caller-owned control to initialize.
 * @param info Field description with caller-owned model.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_search_field_init(AstraControl *control,
    const AstraFieldInfo *info);
/** Compose Fields and separator Labels over caller-owned segment models.
 * @param controls Array of exactly @p control_count caller-owned controls.
 * @param control_count Twice the number of segments.
 * @param info Borrowed models and separator description.
 * @return ASTRA_OK or a validation error; discard partial controls on failure.
 */
AstraResult astra_interface_segmented_field_init(AstraControl *controls,
    uint32_t control_count, const AstraSegmentedFieldInfo *info);
/** Compose removable tag Buttons and one editable draft Field.
 * @param controls Array of exactly @p control_count caller-owned controls.
 * @param control_count Tag count plus two.
 * @param info Borrowed tags and draft model.
 * @return ASTRA_OK or a validation error; discard partial controls on failure.
 */
AstraResult astra_interface_tag_input_init(AstraControl *controls,
    uint32_t control_count, const AstraTagInputInfo *info);
/** Initialize a retained activatable color swatch.
 * @param control Caller-owned control to initialize.
 * @param info Borrowed label and binding description.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_colour_well_init(AstraControl *control,
    const AstraColourWellInfo *info);
/** Change a Colour Well's binding and resolved color, damaging its frame.
 * @param context UI context owning @p control.
 * @param control Colour Well to change.
 * @param binding One ASTRA_COLOUR_* source.
 * @param color Opaque literal or resolved palette RGB.
 * @param palette_slot Slot 0..255 for palette binding; zero otherwise.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_colour_well_set(AstraUIContext *context,
    AstraControl *control, uint32_t binding, AstraColorRGBA8 color,
    uint32_t palette_slot);
/** Revalidate and damage a field after an external model mutation.
 * @param context UI context owning @p control.
 * @param control Field whose caller-owned model changed.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_field_refresh(AstraUIContext *context,
                                          AstraControl *control);
/** Initialize selectable, read-only multiline text. Compose with ScrollView
 * and Scrollbar controls for overflow. TextModel retains original UTF-8;
 * tabs use eight-cell stops and CRLF draws as one line break.
 * @param control Caller-owned control to initialize.
 * @param info Description retaining the caller-owned TextModel.
 * @return ASTRA_OK or a validation/resource error; failure leaves control intact.
 * @since Interface Kit 7.1.
 */
AstraResult astra_interface_text_view_init(AstraControl *control,
                                          const AstraTextViewInfo *info);
/** Remeasure, relayout and damage Text View after its model changes.
 * @param context UI context owning the control.
 * @param control Text View whose caller-owned model changed.
 * @return ASTRA_OK or a validation/resource error.
 * @since Interface Kit 7.1.
 */
AstraResult astra_interface_text_view_refresh(AstraUIContext *context,
                                             AstraControl *control);
/** Invert a Text View's theme foreground and background, for status lines.
 * @param context UI context owning the control.
 * @param control Text View to update.
 * @param inverted Zero for normal colors, one for inverted colors.
 * @return ASTRA_OK or a validation error.
 * @since Interface Kit 7.8.
 */
AstraResult astra_interface_text_view_set_inverted(AstraUIContext *context,
    AstraControl *control, uint32_t inverted);
/** Wrap Text View at a viewport width; zero restores unwrapped lines.
 * Reflows only presentation and preserves model text and selections.
 * @param context UI context owning the control.
 * @param control Text View to update.
 * @param width Available text width in pixels, or zero to disable wrapping.
 * @return ASTRA_OK or a validation/resource error.
 * @since Interface Kit 8.0.
 */
AstraResult astra_interface_text_view_set_wrap_width(AstraUIContext *context,
    AstraControl *control, uint32_t width);
/** Select fixed-cell code or proportional flow presentation. Grid cells use
 * Grid View; both text modes share the retained TextModel and selection.
 * @param context Owning UI context.
 * @param control Text View to update.
 * @param mode One ASTRA_TEXT_SURFACE_* presentation mode.
 * @return ASTRA_OK or a validation/layout error. */
AstraResult astra_interface_text_view_set_mode(AstraUIContext *context,
    AstraControl *control, uint32_t mode);
/** Enable or disable in-place editing. Text View starts read-only.
 * @param context Owning UI context.
 * @param control Text View to update.
 * @param editable One to allow edits, zero to refuse them.
 * @return ASTRA_OK or a validation error. */
AstraResult astra_interface_text_view_set_editable(AstraUIContext *context,
    AstraControl *control, uint32_t editable);
/** Attach bounded caller-owned Undo Manager history to one editable Field or
 * Text View. This initializes @p manager with the shared text edit dispatcher;
 * @p history_arena and @p scratch must not overlap. The scratch must hold one
 * edit's old and new bytes plus metadata. A Secure Field is rejected because
 * its contents must never enter history. Both arenas and all objects must
 * outlive the binding. Editing fails without mutation when history or scratch
 * cannot hold the transaction. Ordinary typing, deletion, and programmatic
 * replacement then share one undo path; Command+Z and Command+Shift+Z work
 * through normal UI event handling.
 * @param binding Empty caller-owned attachment.
 * @param context Initialized UI context owning @p control.
 * @param control Editable Field or Text View to attach.
 * @param manager Empty caller-owned Undo Manager.
 * @param history_arena Four-byte-aligned caller-owned history storage.
 * @param history_bytes Size of @p history_arena.
 * @param scratch Separate caller-owned edit payload workspace.
 * @param scratch_bytes Size of @p scratch.
 * @return ASTRA_OK or a validation, capacity, or history error.
 * @since Interface Kit 10.0. */
AstraResult astra_interface_text_undo_bind(AstraTextUndoBinding *binding,
    AstraUIContext *context, AstraControl *control, AstraUndoManager *manager,
    void *history_arena, uint32_t history_bytes,
    void *scratch, uint32_t scratch_bytes);
/** Detach history, wipe history and scratch, reset manager to its empty
 * initializer, and release binding ownership. Call before destroying control,
 * UI context, or arenas.
 * @param binding Active attachment to detach.
 * @return ASTRA_OK or a validation/history error. */
AstraResult astra_interface_text_undo_unbind(AstraTextUndoBinding *binding);
/** Apply previous text transaction and refresh retained layout.
 * @param binding Active attachment.
 * @return ASTRA_OK, ASTRA_ERROR_NOT_PRESENT when empty, or an edit error. */
AstraResult astra_interface_text_undo_undo(AstraTextUndoBinding *binding);
/** Reapply next text transaction and refresh retained layout.
 * @param binding Active attachment.
 * @return ASTRA_OK, ASTRA_ERROR_NOT_PRESENT when empty, or an edit error. */
AstraResult astra_interface_text_undo_redo(AstraTextUndoBinding *binding);
/** Show or hide right-aligned logical line numbers in code mode's shared
 * gutter. Flow mode retains the setting but presents no gutter.
 * @param context Owning UI context.
 * @param control Text View to update.
 * @param visible One to show numbers, zero to hide them.
 * @return ASTRA_OK or a validation/layout error. */
AstraResult astra_interface_text_view_set_line_numbers(AstraUIContext *context,
    AstraControl *control, uint32_t visible);
/** Replace a Text View selection with UTF-8 and refresh its ScrollView.
 * @param context Owning UI context.
 * @param control Editable Text View.
 * @param replacement UTF-8 bytes, or NULL for deletion.
 * @param replacement_bytes Counted replacement bytes.
 * @return ASTRA_OK or a validation, capacity, or history error. */
AstraResult astra_interface_text_view_replace_selection(
    AstraUIContext *context, AstraControl *control,
    const char *replacement, uint32_t replacement_bytes);
/** Set the model's half-open byte selection and reveal its focus.
 * @param context Owning UI context.
 * @param control Text View to select in.
 * @param anchor First scalar-boundary byte offset.
 * @param focus Second scalar-boundary byte offset.
 * @return ASTRA_OK or a validation/layout error. */
AstraResult astra_interface_text_view_select(AstraUIContext *context,
    AstraControl *control, uint32_t anchor, uint32_t focus);
/** Reveal an existing scalar-boundary byte position without selecting it.
 * @param context Owning UI context.
 * @param control Text View to scroll.
 * @param byte_offset Scalar-boundary byte offset in its model.
 * @return ASTRA_OK or a validation/layout error. */
AstraResult astra_interface_text_view_reveal(AstraUIContext *context,
    AstraControl *control, uint32_t byte_offset);
/** Find exact UTF-8 text, select the match and reveal it. A miss leaves both
 * the model selection and @p match unchanged.
 * @param context Owning UI context.
 * @param control Text View to search.
 * @param needle Counted UTF-8 search text.
 * @param needle_bytes Byte count of @p needle.
 * @param from Starting scalar-boundary byte offset.
 * @param wrap One to search again from the beginning after reaching the end.
 * @param match Receives match selection on success.
 * @return ASTRA_OK, ASTRA_ERROR_NOT_PRESENT for a miss, or a validation error. */
AstraResult astra_interface_text_view_find(AstraUIContext *context,
    AstraControl *control, const char *needle, uint32_t needle_bytes,
    uint32_t from, uint32_t wrap, AstraTextSelection *match);
/** Initialize a read-only fixed cell grid from caller-owned storage.
 * @param control Caller-owned empty control.
 * @param info Validated grid model and control information.
 * @return ASTRA_OK or a validation/resource error.
 * @since Interface Kit 7.9.
 */
AstraResult astra_interface_grid_view_init(AstraControl *control,
    const AstraGridViewInfo *info);
/** Remeasure, relayout and damage a Grid View after model cells or rows change.
 * @param context UI context owning the control.
 * @param control Grid View whose caller-owned model changed.
 * @return ASTRA_OK or a validation/resource error.
 * @since Interface Kit 7.9.
 */
AstraResult astra_interface_grid_view_refresh(AstraUIContext *context,
    AstraControl *control);
/** Replace the current field selection.
 * @param context UI context owning @p control.
 * @param control Field to edit.
 * @param text Validated UTF-8 replacement bytes, or NULL when @p text_bytes is zero.
 * @param text_bytes Number of replacement bytes, excluding any terminator.
 * @return ASTRA_OK or a specific text-model, capacity, or validation error.
 */
AstraResult astra_interface_field_replace_selection(
    AstraUIContext *context, AstraControl *control, const char *text,
    uint32_t text_bytes);
/** Replace a control's flex constraints.
 * @param control Control whose retained constraints are replaced.
 * @param item Constraints copied into @p control.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_control_set_flex(
    AstraControl *control, const AstraFlexItem *item);
/** Mark one retained control's complete clipped frame for repaint.
 * @param context UI context that owns @p control.
 * @param control Retained control whose frame needs repaint.
 * @return ASTRA_OK or a validation error. */
AstraResult astra_interface_control_invalidate(AstraUIContext *context,
    const AstraControl *control);
/** Compose Drop Target from Container and Label; no new control state machine.
 * @param controls Storage for two consecutive retained controls.
 * @param info Validated caller-owned creation data.
 * @return ASTRA_OK or a validation error. */
AstraResult astra_interface_drop_target_init(AstraControl controls[2],
    const AstraDropTargetInfo *info);
/** Update feedback and precise refusal/operation text; borrowed text stays live.
 * @param context UI context containing both controls.
 * @param controls Drop Target controls created by astra_interface_drop_target_init.
 * @param visual_state One ASTRA_DROP_TARGET_* visual state.
 * @param message Borrowed UTF-8 feedback text.
 * @param message_length Feedback text byte length.
 * @return ASTRA_OK or a validation error. */
AstraResult astra_interface_drop_target_set(AstraUIContext *context,
    AstraControl controls[2], uint32_t visual_state,
    const char *message, uint32_t message_length);
/** Replace a control's borrowed UTF-8 text.
 * @param context UI context owning @p control.
 * @param control Text-bearing control to update.
 * @param text Borrowed validated UTF-8 bytes, or NULL when @p text_length is zero.
 * @param text_length Number of borrowed bytes, excluding any terminator.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_control_set_text(
    AstraUIContext *context, AstraControl *control, const char *text,
    uint32_t text_length);
/** Set a value control and damage it immediately.
 * @param context UI context owning @p control.
 * @param control Value-bearing control to update.
 * @param value New value in the control's declared fixed-point units.
 * @return ASTRA_OK or a range, control-kind, or validation error.
 */
AstraResult astra_interface_control_set_value(
    AstraUIContext *context, AstraControl *control, int64_t value);
/** Read a value control.
 * @param control Value-bearing control to inspect.
 * @param value Receives the current value in declared fixed-point units.
 * @param decimal_places Receives the number of fractional decimal digits.
 * @return ASTRA_OK or a control-kind or validation error.
 */
AstraResult astra_interface_control_get_value(
    const AstraControl *control, int64_t *value, uint32_t *decimal_places);
/** Set progress and indeterminate state.
 * @param context UI context owning @p control.
 * @param control Progress control to update.
 * @param value Current progress value.
 * @param maximum Inclusive completion value, or zero for indeterminate mode.
 * @return ASTRA_OK or a control-kind or validation error.
 */
AstraResult astra_interface_progress_set(
    AstraUIContext *context, AstraControl *control, uint32_t value,
    uint32_t maximum);
/** Initialize a retained UI over caller-owned controls.
 * @param context Caller-owned context to initialize.
 * @param controls Caller-owned control array retained by @p context.
 * @param control_count Number of elements in @p controls.
 * @param width Initial client width in logical pixels.
 * @param height Initial client height in logical pixels.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_ui_init(
    AstraUIContext *context, AstraControl *controls, uint32_t control_count,
    uint16_t width, uint16_t height);
/** Measure one control's intrinsic extent.
 * @param context UI context supplying theme and text metrics.
 * @param control Control to measure.
 * @param size Receives the preferred logical-pixel extent.
 * @return ASTRA_OK or a control-kind or validation error.
 */
AstraResult astra_interface_ui_measure(
    const AstraUIContext *context, const AstraControl *control,
    AstraControlSize *size);
/** Reflow the complete retained hierarchy.
 * @param context UI context to reflow using its current client extent.
 * @param layout Root flex layout applied to top-level controls.
 * @return ASTRA_OK or a hierarchy, range, or validation error.
 */
AstraResult astra_interface_ui_layout(AstraUIContext *context,
                                      const AstraFlexLayout *layout);
/** Read retained geometry for application content drawn through Graphics Kit.
 * @param context Laid-out UI context.
 * @param index Index in the caller-owned control array.
 * @param frame Receives logical content bounds.
 * @param clip Receives the visible clip inherited from parent viewports.
 * @return ASTRA_OK, ASTRA_ERROR_WOULD_BLOCK for an unlaid-out/collapsed control,
 * or a validation error. Outputs remain unchanged on error.
 */
AstraResult astra_interface_ui_geometry(const AstraUIContext *context,
    uint32_t index, AstraControlFrame *frame, AstraControlFrame *clip);
/** Text region for one visible List, Table, or Outline Table row. Shared row
 * metrics include Table header and outline indentation. Useful for inline
 * editors and other overlays. @since Interface Kit 12.7.
 * @param context Laid-out UI context.
 * @param control Retained List or Table control in context.
 * @param row Zero-based visible row.
 * @param text Receives row text bounds.
 * @param clip Receives inherited visible clip.
 * @return ASTRA_OK, WOULD_BLOCK before layout, or validation error. */
AstraResult astra_interface_collection_text_rect(
    const AstraUIContext *context, const AstraControl *control,
    uint32_t row, AstraControlFrame *text, AstraControlFrame *clip);
/** Hit-test a client-local pointer against retained controls. The row is -1
 * for blank collection space or controls without rows. A miss returns ID 0.
 * Uses the same clipped geometry and row mapping as context-menu dispatch.
 * @since Interface Kit 12.4.
 * @param context Retained UI context with current layout.
 * @param x Client-local pointer x coordinate.
 * @param y Client-local pointer y coordinate.
 * @param control_id Receives hit control ID, or zero on a miss.
 * @param row Receives collection row, or -1 outside a row.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_ui_pointer_hit(const AstraUIContext *context,
    int32_t x, int32_t y, uint32_t *control_id, int32_t *row);

/** Shared left-button drag threshold. Feed pointer and reset events before UI
 * dispatch; source_hit matters on button down only. A trigger disarms until
 * next down, leaving document creation to the application.
 * @since Interface Kit 12.4.
 * @param gesture Caller-owned drag threshold state.
 * @param event Pointer or reset event to observe.
 * @param source_hit Nonzero when pointer down began over a drag source.
 * @param triggered Receives one when threshold is crossed, otherwise zero.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_drag_gesture_event(AstraDragGesture *gesture,
    const AstraWindowEvent *event, uint32_t source_hit, uint32_t *triggered);
/** Replace application-owned semantic control state.
 * @param context UI context owning @p control.
 * @param control Control whose application-owned state is replaced.
 * @param state Combination of supported ASTRA_CONTROL_* semantic-state bits.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_ui_set_state(
    AstraUIContext *context, AstraControl *control, uint32_t state);
/** Replace semantic state while retaining layout visibility. Useful when a
 * responsive toolbar temporarily folds a control. @since Interface Kit 12.7.
 * @param context UI context owning the control.
 * @param control Retained control whose semantic state changes.
 * @param state Semantic bits only; ASTRA_CONTROL_COLLAPSED is invalid.
 * @return ASTRA_OK or a validation/layout error.
 */
AstraResult astra_interface_ui_set_semantic_state(
    AstraUIContext *context, AstraControl *control, uint32_t state);
/** Render damaged controls.
 * @param context UI context containing layout, state, and damage.
 * @param surface Destination client surface.
 * @return ASTRA_OK or a rendering or validation error.
 */
AstraResult astra_interface_ui_render(AstraUIContext *context,
                                      AstraSurfaceView *surface);
/** Render contiguous controls into one fixed-size draw-list view. Render
 * ranges in ascending index order and append each view to one session list
 * when a complete UI exceeds the view's command budget. Scroll-copy
 * acceleration is reserved for the whole-UI renderer.
 * @param context UI context with current layout and state.
 * @param surface Fixed-size draw-list view for this range.
 * @param first Index of first control to render.
 * @param count Number of consecutive controls to render.
 * @return ASTRA_OK or validation/resource error.
 */
AstraResult astra_interface_ui_render_range(AstraUIContext *context,
    AstraSurfaceView *surface, uint32_t first, uint32_t count);
/** Append one control to a session draw list. When its commands exceed one
 * fixed-size view, render clipped strips and append each in paint order.
 * This keeps large visible collections within the fixed view command budget.
 * @param context UI context with current layout and state.
 * @param list Open session draw list targeting the same window.
 * @param index Control index.
 * @param scratch Caller-owned aligned draw-list storage, at least 16 KiB.
 * @param scratch_bytes Available storage bytes.
 * @param sources Borrowed image surfaces used by this control, or NULL.
 * @param source_count Number of source entries.
 * @return ASTRA_OK or a validation, rendering, or resource error.
 * @since Interface Kit 12.9.
 */
AstraResult astra_interface_ui_append_control(AstraUIContext *context,
    AstraDrawList *list, uint32_t index, void *scratch, uint32_t scratch_bytes,
    const AstraSurface *const *sources, uint32_t source_count);
/** Dispatch one window event.
 * @param context UI context receiving the event.
 * @param event Window event to dispatch.
 * @param action Receives the resulting semantic action, or no action.
 * @return ASTRA_OK or a dispatch or validation error.
 */
AstraResult astra_interface_ui_handle_event(
    AstraUIContext *context, const AstraWindowEvent *event,
    AstraUIAction *action);
/** Read accumulated UI damage.
 * @param context UI context to inspect.
 * @param damage Receives the bounding logical-pixel damage rectangle.
 * @return ASTRA_OK, ASTRA_ERR_NOT_FOUND when clean, or a validation error.
 */
AstraResult astra_interface_ui_damage(const AstraUIContext *context,
                                      AstraControlFrame *damage);
/** Clear accumulated UI damage.
 * @param context UI context whose damage accumulator is reset.
 */
void astra_interface_ui_damage_clear(AstraUIContext *context);
/** Advance active control animations by one display vblank.
 * @param context UI context whose animations advance.
 * @param timestamp_ns Monotonic timestamp for the current vblank, in nanoseconds.
 * @param action Receives a semantic action emitted by the animation, or no action.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_ui_vblank(
    AstraUIContext *context, uint64_t timestamp_ns, AstraUIAction *action);
/** Report whether any retained control owns an animation.
 * @param context UI context to inspect.
 * @return Nonzero while an animation is active; zero otherwise.
 */
int astra_interface_ui_animations_active(const AstraUIContext *context);
/** Synchronize the pointer image implied by UI hover and capture.
 * @param context UI context providing hover and pointer-capture state.
 * @param window Window whose hardware pointer shape is updated.
 * @param override_shape Explicit ASTRA_POINTER_* shape, or zero for automatic selection.
 * @param current_shape In/out cache of the shape currently installed on @p window.
 * @return ASTRA_OK or a window, pointer, or validation error.
 */
AstraResult astra_interface_ui_update_pointer(
    const AstraUIContext *context, AstraWindow *window,
    uint32_t override_shape, uint32_t *current_shape);

#endif
