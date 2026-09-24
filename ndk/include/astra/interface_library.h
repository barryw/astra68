/** @file interface_library.h @brief Interface Kit direct-symbol API. */
#ifndef ASTRA_INTERFACE_LIBRARY_H
#define ASTRA_INTERFACE_LIBRARY_H

#include <astra/clipboard.h>
#include <astra/command.h>
#include <astra/control.h>
#include <astra/interface.h>
#include <astra/scroll.h>
#include <astra/text_model.h>
#include <astra/text_surface.h>
#include <astra/undo.h>
#include <astra/window.h>

/** Breaking Interface Kit ABI generation carried by interface.library.5. */
#define ASTRA_INTERFACE_LIBRARY_ABI_MAJOR 5u
/** Current backward-compatible Interface Kit revision. */
#define ASTRA_INTERFACE_LIBRARY_ABI_MINOR 2u

/** Display a modal alert. @param gui GUI capability. @param info Alert description. @return ASTRA_OK or a specific error. */
AstraResult astra_interface_show_alert(AstraHandle gui,
                                       const AstraAlertInfo *info);

/** Initialize a retained label.
 * @param control Caller-owned control to initialize.
 * @param info Label description whose referenced text remains caller-owned.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_label_init(AstraControl *control,
                                       const AstraLabelInfo *info);
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
/** Initialize a tab strip.
 * @param control Caller-owned control to initialize.
 * @param info Choice description whose item array remains caller-owned.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_tab_init(AstraControl *control,
                                     const AstraChoiceInfo *info);
/** Initialize a single-line UTF-8 field.
 * @param control Caller-owned control to initialize.
 * @param info Field description; its text model remains caller-owned.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_field_init(AstraControl *control,
                                       const AstraFieldInfo *info);
/** Revalidate and damage a field after an external model mutation.
 * @param context UI context owning @p control.
 * @param control Field whose caller-owned model changed.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_field_refresh(AstraUIContext *context,
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
/** Replace application-owned semantic control state.
 * @param context UI context owning @p control.
 * @param control Control whose application-owned state is replaced.
 * @param state Combination of supported ASTRA_CONTROL_* semantic-state bits.
 * @return ASTRA_OK or a validation error.
 */
AstraResult astra_interface_ui_set_state(
    AstraUIContext *context, AstraControl *control, uint32_t state);
/** Render damaged controls.
 * @param context UI context containing layout, state, and damage.
 * @param surface Destination client surface.
 * @return ASTRA_OK or a rendering or validation error.
 */
AstraResult astra_interface_ui_render(AstraUIContext *context,
                                      AstraSurfaceView *surface);
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
