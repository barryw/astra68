#include "control_internal.h"

#include <astra/bytes.h>
#include <astra/draw_list.h>
#include <astra/input.h>
#include <astra/input_modifiers.h>
#include <astra/surface.h>
#include <astra/theme.h>
#include <astra/utf8.h>

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define CONTROL_NONE UINT32_MAX
#define CONTROL_SEMANTIC_STATES \
    (ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_SELECTED | ASTRA_CONTROL_ERROR | \
     ASTRA_CONTROL_COLLAPSED)
#define CONTROL_VALUE_STATES \
    (ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_ERROR | ASTRA_CONTROL_COLLAPSED)
#define CONTROL_PREVIEW_STATES \
    (ASTRA_CONTROL_HOVERED | ASTRA_CONTROL_PRESSED | ASTRA_CONTROL_FOCUSED)
#define CONTROL_FOCUS_VISIBLE (UINT32_C(1) << 31)
#define CONTROL_DYNAMIC_STATES \
    (CONTROL_PREVIEW_STATES | CONTROL_FOCUS_VISIBLE)
#define FIELD_FLAGS ASTRA_FIELD_READ_ONLY
#define FLEX_ITEM_FLAGS (ASTRA_FLEX_BREAK_BEFORE | ASTRA_FLEX_BREAK_AFTER)
#define VALUE_INPUT_EDITING UINT32_C(1)
#define VALUE_INPUT_NEGATIVE UINT32_C(2)
#define VALUE_INPUT_FLAGS (VALUE_INPUT_EDITING | VALUE_INPUT_NEGATIVE)
#define STEPPER_PART_VALUE 0u
#define STEPPER_PART_UP 1u
#define STEPPER_PART_DOWN 2u
#define SCROLLBAR_PART_NONE 0u
#define SCROLLBAR_PART_THUMB 1u
#define SCROLLBAR_PART_BEFORE 2u
#define SCROLLBAR_PART_AFTER 3u
#define STEPPER_BUTTON_WIDTH 20u
#define STEPPER_REPEAT_DELAY 24u
#define STEPPER_REPEAT_INTERVAL 5u
#define TOGGLE_KINDS(kind) \
    ((kind) == ASTRA_CONTROL_CHECKBOX || (kind) == ASTRA_CONTROL_RADIO || \
     (kind) == ASTRA_CONTROL_SWITCH)
#define CHOICE_KINDS(kind) \
    ((kind) == ASTRA_CONTROL_SEGMENTED || (kind) == ASTRA_CONTROL_TAB)
#define CONTAINER_KINDS(kind) \
    ((kind) == ASTRA_CONTROL_CONTAINER || \
     (kind) == ASTRA_CONTROL_SCROLL_VIEW)
#define SCROLLBAR_STYLE_ORIENTATION UINT32_C(1)
#define SCROLLBAR_STYLE_OVERLAY UINT32_C(2)
#define SCROLLBAR_STYLE_MASK \
    (SCROLLBAR_STYLE_ORIENTATION | SCROLLBAR_STYLE_OVERLAY)
#define SCROLLBAR_FADE_FRAMES 48u
#define SCROLL_DAMAGE_RECORDING UINT32_C(0x80000000)
#define SCROLL_DAMAGE_INDEX_MASK UINT32_C(0x7fffffff)
#define SCROLL_DAMAGE_INVALID UINT32_MAX

static uint16_t color(AstraColorRGBA8 value)
{
    return astra_surface_rgb565(value.red, value.green, value.blue);
}

static int layout_valid(const AstraFlexLayout *layout);
static AstraResult measure_control(const AstraControl *control,
                                   AstraControlSize *size);
static AstraResult measure_container(const AstraUIContext *context,
                                     uint32_t index,
                                     AstraControlSize *size);
static int range_value_representable(int64_t value, int64_t minimum,
                                     int64_t maximum, int64_t step);
static AstraResult activate_control(AstraUIContext *context, uint32_t index,
                                    AstraUIAction *action);
static void animation_sync(AstraUIContext *context, uint32_t index);
static AstraResult splitter_set_position(AstraUIContext *context,
                                         uint32_t index, int64_t position,
                                         AstraUIAction *action);
static int splitter_neighbors(const AstraUIContext *context, uint32_t index,
                              const AstraFlexLayout *root_layout,
                              uint32_t *before, uint32_t *after, int *row);

static AstraTextModel *field_model(AstraControl *control)
{
    return (AstraTextModel *)(void *)control->_private_text;
}

static const AstraTextModel *const_field_model(const AstraControl *control)
{
    return (const AstraTextModel *)(const void *)control->_private_text;
}

static const AstraChoiceItem *choice_items(const AstraControl *control)
{
    return (const AstraChoiceItem *)(const void *)control->_private_text;
}

static AstraScrollModel *scroll_model(AstraControl *control)
{
    return (AstraScrollModel *)(void *)control->_private_text;
}

static const AstraScrollModel *const_scroll_model(const AstraControl *control)
{
    return (const AstraScrollModel *)(const void *)control->_private_text;
}

static uint32_t private_u32(const int64_t *value)
{
    return (uint32_t)*value;
}

static void private_u32_set(int64_t *value, uint32_t bits)
{
    *value = bits;
}

static int field_text_valid(const char *text, uint32_t bytes)
{
    uint32_t at = 0u;

    if (!astra_utf8_validate(text, bytes, 0u))
        return 0;
    while (at < bytes) {
        uint32_t consumed = 0u;
        uint32_t scalar = astra_utf8_decode(text + at, bytes - at,
                                            &consumed);

        if (scalar < 0x20u || (scalar >= 0x7fu && scalar <= 0x9fu) ||
            scalar == 0x2028u || scalar == 0x2029u)
            return 0;
        at += consumed;
    }
    return 1;
}

static int field_content_valid(const AstraTextModel *model)
{
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    uint32_t offset = 0u;

    if (astra_text_model_get_state(model, &state) != ASTRA_OK ||
        state.line_count != 1u)
        return 0;
    while (offset < state.text_bytes) {
        const char *text;
        uint32_t bytes;
        if (astra_text_model_read(model, offset, &text, &bytes) != ASTRA_OK ||
            bytes == 0u || !field_text_valid(text, bytes))
            return 0;
        offset += bytes;
    }
    return 1;
}

static int control_valid(const AstraControl *control)
{
    uint32_t state;

    if (control == NULL ||
        control->_private_structure_size != sizeof(*control) ||
        control->_private_id == 0u)
        return 0;
    state = control->_private_state;
    if ((state & ~CONTROL_SEMANTIC_STATES) != 0u ||
        (control->_private_preview_state & ~CONTROL_PREVIEW_STATES) != 0u ||
        (control->_private_dynamic_state & ~CONTROL_DYNAMIC_STATES) != 0u ||
        control->_private_minimum_width > control->_private_maximum_width ||
        control->_private_minimum_height > control->_private_maximum_height ||
        control->_private_align_self > ASTRA_FLEX_ALIGN_AUTO ||
        (control->_private_flex_flags & ~FLEX_ITEM_FLAGS) != 0u ||
        (control->_private_control_kind != ASTRA_CONTROL_DIAL &&
         (control->_private_reset_value != 0 ||
          control->_private_decimal_places != 0u)))
        return 0;
    if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER)
        return control->_private_text == NULL &&
               control->_private_text_length == 0u &&
               (control->_private_state &
                ~ASTRA_CONTROL_COLLAPSED) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_dynamic_state == 0u &&
               control->_private_style == 0u &&
               control->_private_value == 0 &&
               control->_private_minimum_value == 0 &&
               control->_private_maximum_value == 0 &&
               control->_private_value_step == 0 &&
               layout_valid(&control->_private_child_layout);
    if (control->_private_control_kind == ASTRA_CONTROL_SCROLL_VIEW) {
        AstraScrollState scroll = ASTRA_SCROLL_STATE_INIT;

        return control->_private_text != NULL &&
               control->_private_text_length != 0u &&
               control->_private_style != 0u &&
               (control->_private_state & ~ASTRA_CONTROL_COLLAPSED) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_dynamic_state == 0u &&
               control->_private_value_step >= 0 &&
               control->_private_value_step <= 1 &&
               astra_scroll_get_state(
                   const_scroll_model(control), &scroll) == ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_SCROLLBAR) {
        AstraScrollState scroll = ASTRA_SCROLL_STATE_INIT;

        return control->_private_text != NULL &&
               control->_private_text_length == 0u &&
               (control->_private_style & ~SCROLLBAR_STYLE_MASK) == 0u &&
               (state & ~(ASTRA_CONTROL_DISABLED |
                          ASTRA_CONTROL_COLLAPSED)) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_minimum_value == 0 &&
               control->_private_maximum_value == 0 &&
               control->_private_value_step == 0 &&
               astra_scroll_get_state(
                   const_scroll_model(control), &scroll) == ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_SPLITTER)
        return control->_private_text == NULL &&
               control->_private_text_length == 0u &&
               control->_private_style <= ASTRA_ORIENTATION_VERTICAL &&
               (state & ~(ASTRA_CONTROL_DISABLED |
                          ASTRA_CONTROL_COLLAPSED)) == 0u &&
               control->_private_preview_state == 0u;
    if (control->_private_control_kind == ASTRA_CONTROL_LABEL)
        return control->_private_text != NULL &&
               control->_private_text_length != 0u &&
               control->_private_style >= ASTRA_TEXT_CLIENT_PRIMARY &&
               control->_private_style <= ASTRA_TEXT_CLIENT_FAULT &&
               (state & ~(ASTRA_CONTROL_DISABLED |
                          ASTRA_CONTROL_COLLAPSED)) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_dynamic_state == 0u &&
               control->_private_value == 0 &&
               control->_private_minimum_value == 0 &&
               control->_private_maximum_value == 0 &&
               control->_private_value_step == 0;
    if (control->_private_control_kind == ASTRA_CONTROL_BUTTON)
        return control->_private_text != NULL &&
               control->_private_text_length != 0u &&
               control->_private_style <= ASTRA_BUTTON_DESTRUCTIVE &&
               control->_private_value == 0 &&
               control->_private_minimum_value == 0 &&
               control->_private_maximum_value == 0 &&
               control->_private_value_step == 0;
    if (control->_private_control_kind == ASTRA_CONTROL_DISCLOSURE)
        return control->_private_text != NULL &&
               control->_private_text_length != 0u &&
               control->_private_style != 0u &&
               (state & ~(ASTRA_CONTROL_DISABLED |
                          ASTRA_CONTROL_COLLAPSED)) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_value == 0 &&
               control->_private_minimum_value == 0 &&
               control->_private_maximum_value == 0 &&
               control->_private_value_step == 0;
    if (TOGGLE_KINDS(control->_private_control_kind))
        return control->_private_text != NULL &&
               control->_private_text_length != 0u &&
               (control->_private_control_kind == ASTRA_CONTROL_RADIO ?
                    control->_private_style != 0u :
                    control->_private_style == 0u) &&
               control->_private_preview_state == 0u &&
               control->_private_value == 0 &&
               control->_private_minimum_value == 0 &&
               control->_private_maximum_value == 0 &&
               control->_private_value_step == 0;
    if (control->_private_control_kind == ASTRA_CONTROL_SLIDER)
        return control->_private_text == NULL &&
               control->_private_text_length == 0u &&
               control->_private_style <= ASTRA_ORIENTATION_VERTICAL &&
               (state & ~CONTROL_VALUE_STATES) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_minimum_value <
                   control->_private_maximum_value &&
               control->_private_value >= control->_private_minimum_value &&
               control->_private_value <= control->_private_maximum_value &&
               control->_private_value_step > 0 &&
               range_value_representable(
                   control->_private_value,
                   control->_private_minimum_value,
                   control->_private_maximum_value,
                   control->_private_value_step);
    if (control->_private_control_kind == ASTRA_CONTROL_DIAL) {
        return control->_private_text == NULL &&
               control->_private_text_length == 0u &&
               control->_private_style == 0u &&
               (state & ~CONTROL_VALUE_STATES) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_minimum_value <
                   control->_private_maximum_value &&
               control->_private_value >= control->_private_minimum_value &&
               control->_private_value <= control->_private_maximum_value &&
               control->_private_value_step > 0 &&
               control->_private_reset_value >=
                   control->_private_minimum_value &&
               control->_private_reset_value <=
                   control->_private_maximum_value &&
               range_value_representable(
                   control->_private_value,
                   control->_private_minimum_value,
                   control->_private_maximum_value,
                   control->_private_value_step) &&
               range_value_representable(
                   control->_private_reset_value,
                   control->_private_minimum_value,
                   control->_private_maximum_value,
                   control->_private_value_step);
    }
    if (control->_private_control_kind == ASTRA_CONTROL_STEPPER)
        return control->_private_text == NULL &&
               control->_private_text_length == 0u &&
               control->_private_style == 0u &&
               (state & ~CONTROL_VALUE_STATES) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_minimum_value <
                   control->_private_maximum_value &&
               control->_private_value >= control->_private_minimum_value &&
               control->_private_value <= control->_private_maximum_value &&
               control->_private_value_step > 0 &&
               range_value_representable(
                   control->_private_value,
                   control->_private_minimum_value,
                   control->_private_maximum_value,
                   control->_private_value_step);
    if (control->_private_control_kind == ASTRA_CONTROL_PROGRESS)
        return control->_private_text == NULL &&
               control->_private_text_length == 0u &&
               control->_private_style == 0u &&
               (state & ~CONTROL_VALUE_STATES) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_minimum_value == 0 &&
               control->_private_value >= 0 &&
               control->_private_maximum_value >= 0 &&
               control->_private_value <= control->_private_maximum_value &&
               control->_private_value_step == 0;
    if (CHOICE_KINDS(control->_private_control_kind))
        return control->_private_text != NULL &&
               control->_private_text_length != 0u &&
               control->_private_style == 0u &&
               (state & ~CONTROL_VALUE_STATES) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_value >= 0 &&
               (uint32_t)control->_private_value <
                   control->_private_text_length &&
               control->_private_minimum_value >= -1 &&
               (uint32_t)(control->_private_minimum_value + 1) <=
                   control->_private_text_length &&
               control->_private_maximum_value ==
                   (int32_t)(control->_private_text_length - 1u) &&
               control->_private_value_step == 0;
    if (control->_private_control_kind == ASTRA_CONTROL_FIELD) {
        AstraTextModelState model_state = ASTRA_TEXT_MODEL_STATE_INIT;

        return control->_private_text != NULL &&
               control->_private_text_length != 0u &&
               (control->_private_style & ~FIELD_FLAGS) == 0u &&
               (state & ~CONTROL_VALUE_STATES) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_minimum_value == 0 &&
               control->_private_value_step == 0 &&
               astra_text_model_get_state(
                   const_field_model(control), &model_state) == ASTRA_OK &&
               model_state.line_count == 1u;
    }
    return 0;
}

static int context_valid(const AstraUIContext *context)
{
    return context != NULL &&
           context->_private_structure_size == sizeof(*context) &&
           context->_private_width != 0u && context->_private_height != 0u &&
           (context->_private_control_count == 0u ||
            context->_private_controls != NULL) &&
           context->_private_has_layout <= 1u &&
           (context->_private_has_layout == 0u ||
            layout_valid(&context->_private_layout)) &&
           (context->_private_first_child == CONTROL_NONE ||
            context->_private_first_child < context->_private_control_count) &&
           (context->_private_last_child == CONTROL_NONE ||
            context->_private_last_child < context->_private_control_count) &&
           (context->_private_animation_first == CONTROL_NONE ||
            context->_private_animation_first <
                context->_private_control_count) &&
           (context->_private_reserved == 0u ||
            context->_private_reserved == SCROLL_DAMAGE_INVALID ||
            (((context->_private_reserved & SCROLL_DAMAGE_INDEX_MASK) != 0u) &&
             (context->_private_reserved & SCROLL_DAMAGE_INDEX_MASK) - 1u <
                 context->_private_control_count)) &&
           (context->_private_value_input_digits <= 19u ||
            (context->_private_capture < context->_private_control_count &&
             context->_private_controls[context->_private_capture]
                     ._private_control_kind == ASTRA_CONTROL_DIAL)) &&
           (context->_private_value_input_flags & ~VALUE_INPUT_FLAGS) == 0u &&
           context->_private_hover_part <= SCROLLBAR_PART_AFTER &&
           context->_private_capture_part <= SCROLLBAR_PART_AFTER;
}

static uint32_t control_index(const AstraUIContext *context,
                              const AstraControl *control)
{
    if (!context_valid(context) || control == NULL)
        return CONTROL_NONE;
    for (uint32_t at = 0u; at < context->_private_control_count; ++at)
        if (&context->_private_controls[at] == control)
            return at;
    return CONTROL_NONE;
}

static uint32_t control_id_index(const AstraUIContext *context, uint32_t id)
{
    for (uint32_t at = 0u; at < context->_private_control_count; ++at)
        if (context->_private_controls[at]._private_id == id)
            return at;
    return CONTROL_NONE;
}

static uint32_t effective_state(const AstraControl *control)
{
    uint32_t state = control->_private_state | control->_private_preview_state |
                     control->_private_dynamic_state;

    if ((control->_private_preview_state & ASTRA_CONTROL_FOCUSED) != 0u)
        state |= CONTROL_FOCUS_VISIBLE;
    return state;
}

static void damage_add(AstraUIContext *context,
                       const AstraControlFrame *frame, uint32_t state,
                       const AstraControlFrame *clip)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
    int64_t old_right;
    int64_t old_bottom;

    if (frame->width == 0u || frame->height == 0u)
        return;
    left = frame->x;
    top = frame->y;
    right = left + frame->width;
    bottom = top + frame->height;
    if ((state & CONTROL_FOCUS_VISIBLE) != 0u) {
        left -= theme.focus_width;
        top -= theme.focus_width;
        right += theme.focus_width;
        bottom += theme.focus_width;
    }
    if (left < clip->x) left = clip->x;
    if (top < clip->y) top = clip->y;
    if (right > (int64_t)clip->x + clip->width)
        right = (int64_t)clip->x + clip->width;
    if (bottom > (int64_t)clip->y + clip->height)
        bottom = (int64_t)clip->y + clip->height;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > context->_private_width) right = context->_private_width;
    if (bottom > context->_private_height) bottom = context->_private_height;
    if (left >= right || top >= bottom)
        return;
    if (context->_private_has_damage == 0u) {
        context->_private_damage = (AstraControlFrame){
            (int32_t)left, (int32_t)top,
            (uint32_t)(right - left), (uint32_t)(bottom - top)};
        context->_private_has_damage = 1u;
        return;
    }
    old_right = (int64_t)context->_private_damage.x +
                context->_private_damage.width;
    old_bottom = (int64_t)context->_private_damage.y +
                 context->_private_damage.height;
    if (left > context->_private_damage.x)
        left = context->_private_damage.x;
    if (top > context->_private_damage.y)
        top = context->_private_damage.y;
    if (right < old_right) right = old_right;
    if (bottom < old_bottom) bottom = old_bottom;
    context->_private_damage = (AstraControlFrame){
        (int32_t)left, (int32_t)top,
        (uint32_t)(right - left), (uint32_t)(bottom - top)};
}

static void control_damage(AstraUIContext *context, uint32_t index)
{
    AstraControl *control;

    if (context->_private_reserved != 0u &&
        (context->_private_reserved & SCROLL_DAMAGE_RECORDING) == 0u)
        context->_private_reserved = SCROLL_DAMAGE_INVALID;

    if (context->_private_has_damage != 0u &&
        context->_private_damage.x == 0 &&
        context->_private_damage.y == 0 &&
        context->_private_damage.width == context->_private_width &&
        context->_private_damage.height == context->_private_height)
        return;
    if (index == CONTROL_NONE || index >= context->_private_control_count)
        return;
    control = &context->_private_controls[index];
    if (control->_private_laid_out != 0u)
        damage_add(context, &control->_private_frame,
                   effective_state(control), &control->_private_clip);
}

static void disclosure_target_damage(AstraUIContext *context,
                                     uint32_t target_id)
{
    for (uint32_t at = 0u; at < context->_private_control_count; ++at)
        if (context->_private_controls[at]._private_control_kind ==
                ASTRA_CONTROL_DISCLOSURE &&
            context->_private_controls[at]._private_style == target_id)
            control_damage(context, at);
}

static void dynamic_state(AstraUIContext *context, uint32_t index,
                          uint32_t mask, int enabled)
{
    AstraControl *control;
    uint32_t before;

    if (index == CONTROL_NONE || index >= context->_private_control_count)
        return;
    control = &context->_private_controls[index];
    before = effective_state(control);
    if (enabled)
        control->_private_dynamic_state |= mask;
    else
        control->_private_dynamic_state &= ~mask;
    if (before != effective_state(control)) {
        damage_add(context, &control->_private_frame, before,
                   &control->_private_clip);
        control_damage(context, index);
    }
}

static void set_hover(AstraUIContext *context, uint32_t index)
{
    uint32_t previous = context->_private_hover;

    if (context->_private_hover == index)
        return;
    dynamic_state(context, previous, ASTRA_CONTROL_HOVERED, 0);
    if (previous != CONTROL_NONE &&
        CHOICE_KINDS(context->_private_controls[previous]
                         ._private_control_kind)) {
        context->_private_controls[previous]._private_minimum_value = -1;
        control_damage(context, previous);
    }
    context->_private_hover = index;
    dynamic_state(context, index, ASTRA_CONTROL_HOVERED, 1);
    if (previous != CONTROL_NONE &&
        context->_private_controls[previous]._private_control_kind ==
            ASTRA_CONTROL_SCROLLBAR)
        animation_sync(context, previous);
    if (index != CONTROL_NONE &&
        context->_private_controls[index]._private_control_kind ==
            ASTRA_CONTROL_SCROLLBAR) {
        context->_private_controls[index]._private_animation_phase = 0u;
        animation_sync(context, index);
    }
}

static int control_animates(const AstraUIContext *context, uint32_t index)
{
    const AstraControl *control;

    if (index >= context->_private_control_count)
        return 0;
    control = &context->_private_controls[index];
    if ((control->_private_state & ASTRA_CONTROL_DISABLED) != 0u)
        return 0;
    if (context->_private_has_layout != 0u &&
        control->_private_laid_out == 0u)
        return 0;
    return (control->_private_control_kind == ASTRA_CONTROL_PROGRESS &&
            control->_private_maximum_value == 0) ||
           (control->_private_control_kind == ASTRA_CONTROL_FIELD &&
            context->_private_focus == index) ||
           (control->_private_control_kind == ASTRA_CONTROL_STEPPER &&
            context->_private_capture == index &&
            (control->_private_dynamic_state & ASTRA_CONTROL_PRESSED) != 0u &&
            context->_private_capture_part != STEPPER_PART_VALUE) ||
           (control->_private_control_kind == ASTRA_CONTROL_SCROLLBAR &&
            (control->_private_style & SCROLLBAR_STYLE_OVERLAY) != 0u &&
            (((effective_state(control) &
               (ASTRA_CONTROL_HOVERED | ASTRA_CONTROL_PRESSED)) != 0u) ||
             control->_private_animation_phase < SCROLLBAR_FADE_FRAMES));
}

static void animation_sync(AstraUIContext *context, uint32_t index)
{
    uint32_t previous = CONTROL_NONE;
    uint32_t at = context->_private_animation_first;

    if (index == CONTROL_NONE || index >= context->_private_control_count)
        return;
    while (at != CONTROL_NONE && at != index) {
        if (at >= context->_private_control_count)
            return;
        previous = at;
        at = context->_private_controls[at]._private_animation_next;
    }
    if (!control_animates(context, index)) {
        if (at == index) {
            uint32_t next =
                context->_private_controls[index]._private_animation_next;

            if (previous == CONTROL_NONE)
                context->_private_animation_first = next;
            else
                context->_private_controls[previous]
                    ._private_animation_next = next;
            context->_private_controls[index]._private_animation_next =
                CONTROL_NONE;
        }
        return;
    }
    if (at == index)
        return;
    if (context->_private_animation_first == CONTROL_NONE) {
        context->_private_animation_time_low = 0u;
        context->_private_animation_time_high = 0u;
        context->_private_animation_phase = 0u;
    }
    context->_private_controls[index]._private_animation_phase = 0u;
    context->_private_controls[index]._private_animation_next =
        context->_private_animation_first;
    context->_private_animation_first = index;
}

static void set_focus(AstraUIContext *context, uint32_t index, int visible)
{
    uint32_t before = context->_private_focus;

    if (context->_private_focus == index) {
        dynamic_state(context, index, CONTROL_FOCUS_VISIBLE, visible);
        return;
    }
    context->_private_value_input = 0u;
    context->_private_value_input_digits = 0u;
    context->_private_value_input_flags = 0u;
    dynamic_state(context, before,
                  ASTRA_CONTROL_FOCUSED | CONTROL_FOCUS_VISIBLE, 0);
    context->_private_focus = index;
    animation_sync(context, before);
    animation_sync(context, index);
    dynamic_state(context, index, ASTRA_CONTROL_FOCUSED, 1);
    dynamic_state(context, index, CONTROL_FOCUS_VISIBLE, visible);
}

static void clear_capture(AstraUIContext *context)
{
    uint32_t captured = context->_private_capture;

    dynamic_state(context, captured,
                  ASTRA_CONTROL_PRESSED, 0);
    if (captured < context->_private_control_count &&
        context->_private_controls[captured]._private_control_kind ==
            ASTRA_CONTROL_DIAL) {
        context->_private_value_input = 0u;
        context->_private_value_input_digits = 0u;
        context->_private_value_input_flags = 0u;
    }
    context->_private_capture = CONTROL_NONE;
    context->_private_capture_part = STEPPER_PART_VALUE;
    animation_sync(context, captured);
}

static void clear_keyboard(AstraUIContext *context)
{
    dynamic_state(context, context->_private_keyboard,
                  ASTRA_CONTROL_PRESSED, 0);
    context->_private_keyboard = CONTROL_NONE;
}

static void reset_interaction(AstraUIContext *context, int clear_focus)
{
    set_hover(context, CONTROL_NONE);
    clear_capture(context);
    clear_keyboard(context);
    if (clear_focus)
        set_focus(context, CONTROL_NONE, 0);
}

static int focusable(const AstraControl *control)
{
    return control->_private_control_kind == ASTRA_CONTROL_BUTTON ||
           control->_private_control_kind == ASTRA_CONTROL_DISCLOSURE ||
           TOGGLE_KINDS(control->_private_control_kind) ||
           control->_private_control_kind == ASTRA_CONTROL_SLIDER ||
           control->_private_control_kind == ASTRA_CONTROL_DIAL ||
           control->_private_control_kind == ASTRA_CONTROL_STEPPER ||
           CHOICE_KINDS(control->_private_control_kind) ||
           control->_private_control_kind == ASTRA_CONTROL_FIELD ||
           control->_private_control_kind == ASTRA_CONTROL_SPLITTER;
}

static int interactive(const AstraControl *control)
{
    return focusable(control) ||
           control->_private_control_kind == ASTRA_CONTROL_SCROLLBAR;
}

static uint32_t hit_test(const AstraUIContext *context, int32_t x, int32_t y)
{
    for (uint32_t count = context->_private_control_count; count != 0u;
         --count) {
        const AstraControl *control =
            &context->_private_controls[count - 1u];
        const AstraControlFrame *frame = &control->_private_frame;
        const AstraControlFrame *clip = &control->_private_clip;
        int64_t right = (int64_t)frame->x + frame->width;
        int64_t bottom = (int64_t)frame->y + frame->height;
        int64_t clip_right = (int64_t)clip->x + clip->width;
        int64_t clip_bottom = (int64_t)clip->y + clip->height;

        if (interactive(control) &&
            control->_private_laid_out != 0u &&
            (control->_private_state & ASTRA_CONTROL_DISABLED) == 0u &&
            x >= frame->x && (int64_t)x < right &&
            y >= frame->y && (int64_t)y < bottom &&
            x >= clip->x && (int64_t)x < clip_right &&
            y >= clip->y && (int64_t)y < clip_bottom)
            return count - 1u;
    }
    return CONTROL_NONE;
}

static uint32_t next_focus(const AstraUIContext *context, int backwards)
{
    uint32_t count = context->_private_control_count;
    uint32_t start;

    if (count == 0u)
        return CONTROL_NONE;
    start = context->_private_focus == CONTROL_NONE ?
        (backwards ? 0u : count - 1u) : context->_private_focus;
    for (uint32_t step = 1u; step <= count; ++step) {
        uint32_t index = backwards ?
            (start + count - (step % count)) % count :
            (start + step) % count;
        const AstraControl *control = &context->_private_controls[index];

        if (focusable(control) &&
            control->_private_laid_out != 0u &&
            (control->_private_state & ASTRA_CONTROL_DISABLED) == 0u)
            return index;
    }
    return CONTROL_NONE;
}

AstraResult astra_interface_label_init(AstraControl *control,
                                       const AstraLabelInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->text_length == 0u ||
        !astra_utf8_validate(info->text, info->text_length, 0u) ||
        info->text_role < ASTRA_TEXT_CLIENT_PRIMARY ||
        info->text_role > ASTRA_TEXT_CLIENT_FAULT ||
        (info->state & ~(ASTRA_CONTROL_DISABLED |
                         ASTRA_CONTROL_COLLAPSED)) != 0u ||
        !astra_words_zero(info->reserved, 3u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_LABEL;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_style = info->text_role;
    control->_private_text = info->text;
    control->_private_text_length = info->text_length;
    return ASTRA_OK;
}

AstraResult astra_interface_button_init(AstraControl *control,
                                        const AstraButtonInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->text_length == 0u ||
        !astra_utf8_validate(info->text, info->text_length, 0u) ||
        info->variant > ASTRA_BUTTON_DESTRUCTIVE ||
        (info->state & ~CONTROL_SEMANTIC_STATES) != 0u ||
        (info->preview_state & ~CONTROL_PREVIEW_STATES) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_BUTTON;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_preview_state = info->preview_state;
    control->_private_style = info->variant;
    control->_private_text = info->text;
    control->_private_text_length = info->text_length;
    return ASTRA_OK;
}

AstraResult astra_interface_disclosure_init(
    AstraControl *control, const AstraDisclosureInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->target_id == 0u ||
        info->id == info->target_id || info->text_length == 0u ||
        !astra_utf8_validate(info->text, info->text_length, 0u) ||
        (info->state & ~(ASTRA_CONTROL_DISABLED |
                         ASTRA_CONTROL_COLLAPSED)) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_DISCLOSURE;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_style = info->target_id;
    control->_private_text = info->text;
    control->_private_text_length = info->text_length;
    return ASTRA_OK;
}

AstraResult astra_interface_container_init(AstraControl *control,
                                           const AstraContainerInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || !layout_valid(&info->layout) ||
        (info->state & ~ASTRA_CONTROL_COLLAPSED) != 0u ||
        !astra_words_zero(info->reserved, 3u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_CONTAINER;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_child_layout = info->layout;
    return ASTRA_OK;
}

AstraResult astra_interface_scroll_view_init(
    AstraControl *control, const AstraScrollViewInfo *info)
{
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;

    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->model == NULL ||
        info->preferred_width == 0u || info->preferred_height == 0u ||
        (info->state & ~ASTRA_CONTROL_COLLAPSED) != 0u ||
        !astra_words_zero(info->reserved, 4u) ||
        astra_scroll_get_state(info->model, &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_SCROLL_VIEW;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_text = (const char *)(const void *)info->model;
    control->_private_text_length = info->preferred_width;
    control->_private_style = info->preferred_height;
    return ASTRA_OK;
}

AstraResult astra_interface_scrollbar_init(
    AstraControl *control, const AstraScrollbarInfo *info)
{
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;

    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->model == NULL ||
        info->orientation > ASTRA_ORIENTATION_VERTICAL ||
        (info->flags & ~ASTRA_SCROLLBAR_OVERLAY) != 0u ||
        (info->state & ~(ASTRA_CONTROL_DISABLED |
                         ASTRA_CONTROL_COLLAPSED)) != 0u ||
        !astra_words_zero(info->reserved, 4u) ||
        astra_scroll_get_state(info->model, &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_SCROLLBAR;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_text = (const char *)(const void *)info->model;
    control->_private_style = info->orientation |
        ((info->flags & ASTRA_SCROLLBAR_OVERLAY) != 0u ?
             SCROLLBAR_STYLE_OVERLAY : 0u);
    return ASTRA_OK;
}

AstraResult astra_interface_splitter_init(
    AstraControl *control, const AstraSplitterInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u ||
        info->orientation > ASTRA_ORIENTATION_VERTICAL ||
        (info->state & ~(ASTRA_CONTROL_DISABLED |
                         ASTRA_CONTROL_COLLAPSED)) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_SPLITTER;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_style = info->orientation;
    return ASTRA_OK;
}

static AstraResult toggle_init(AstraControl *control,
                               const AstraToggleInfo *info, uint32_t kind)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->text_length == 0u ||
        !astra_utf8_validate(info->text, info->text_length, 0u) ||
        (info->state & ~CONTROL_SEMANTIC_STATES) != 0u ||
        (kind == ASTRA_CONTROL_RADIO ? info->group_id == 0u :
                                      info->group_id != 0u) ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = kind;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_style = info->group_id;
    control->_private_text = info->text;
    control->_private_text_length = info->text_length;
    return ASTRA_OK;
}

AstraResult astra_interface_checkbox_init(AstraControl *control,
                                          const AstraToggleInfo *info)
{
    return toggle_init(control, info, ASTRA_CONTROL_CHECKBOX);
}

AstraResult astra_interface_radio_init(AstraControl *control,
                                       const AstraToggleInfo *info)
{
    return toggle_init(control, info, ASTRA_CONTROL_RADIO);
}

AstraResult astra_interface_switch_init(AstraControl *control,
                                        const AstraToggleInfo *info)
{
    return toggle_init(control, info, ASTRA_CONTROL_SWITCH);
}

AstraResult astra_interface_slider_init(AstraControl *control,
                                        const AstraRangeInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->minimum >= info->maximum || info->step <= 0 ||
        info->value < info->minimum || info->value > info->maximum ||
        !range_value_representable(info->value, info->minimum,
                                   info->maximum, info->step) ||
        info->orientation > ASTRA_ORIENTATION_VERTICAL ||
        (info->state & ~CONTROL_VALUE_STATES) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_SLIDER;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_style = info->orientation;
    control->_private_value = info->value;
    control->_private_minimum_value = info->minimum;
    control->_private_maximum_value = info->maximum;
    control->_private_value_step = info->step;
    return ASTRA_OK;
}

static uint64_t range_offset(int64_t value, int64_t minimum)
{
    return (uint64_t)value - (uint64_t)minimum;
}

static int64_t range_value_at(int64_t minimum, uint64_t offset)
{
    uint64_t bits = (uint64_t)minimum + offset;
    int64_t value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int range_value_representable(int64_t value, int64_t minimum,
                                     int64_t maximum, int64_t step)
{
    return step > 0 && value >= minimum && value <= maximum &&
           (value == maximum || range_offset(value, minimum) %
            (uint64_t)step == 0u);
}

AstraResult astra_interface_dial_init(AstraControl *control,
                                      const AstraDialInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->minimum >= info->maximum || info->step <= 0 ||
        info->value < info->minimum || info->value > info->maximum ||
        info->reset_value < info->minimum ||
        info->reset_value > info->maximum ||
        !range_value_representable(info->value, info->minimum,
                                   info->maximum, info->step) ||
        !range_value_representable(info->reset_value, info->minimum,
                                   info->maximum, info->step) ||
        (info->state & ~CONTROL_VALUE_STATES) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_DIAL;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_value = info->value;
    control->_private_minimum_value = info->minimum;
    control->_private_maximum_value = info->maximum;
    control->_private_value_step = info->step;
    control->_private_reset_value = info->reset_value;
    control->_private_decimal_places = info->decimal_places;
    return ASTRA_OK;
}

AstraResult astra_interface_stepper_init(AstraControl *control,
                                         const AstraStepperInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->minimum >= info->maximum || info->step <= 0 ||
        info->value < info->minimum || info->value > info->maximum ||
        !range_value_representable(info->value, info->minimum,
                                   info->maximum, info->step) ||
        (info->state & ~CONTROL_VALUE_STATES) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_STEPPER;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_value = info->value;
    control->_private_minimum_value = info->minimum;
    control->_private_maximum_value = info->maximum;
    control->_private_value_step = info->step;
    return ASTRA_OK;
}

AstraResult astra_interface_progress_init(AstraControl *control,
                                          const AstraProgressInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->value > info->maximum ||
        info->maximum > INT32_MAX ||
        (info->maximum == 0u && info->value != 0u) ||
        (info->state & ~CONTROL_VALUE_STATES) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_PROGRESS;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_value = (int32_t)info->value;
    control->_private_maximum_value = (int32_t)info->maximum;
    return ASTRA_OK;
}

static AstraResult choice_init(AstraControl *control,
                               const AstraChoiceInfo *info, uint32_t kind)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->items == NULL || info->item_count == 0u ||
        info->item_count > (uint32_t)INT32_MAX ||
        info->selected >= info->item_count ||
        (info->state & ~CONTROL_VALUE_STATES) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < info->item_count; ++at) {
        const AstraChoiceItem *item = &info->items[at];

        if (item->text_length == 0u ||
            !astra_utf8_validate(item->text, item->text_length, 0u) ||
            !astra_words_zero(item->reserved, 2u))
            return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = kind;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_text = (const char *)(const void *)info->items;
    control->_private_text_length = info->item_count;
    control->_private_value = (int32_t)info->selected;
    control->_private_minimum_value = -1;
    control->_private_maximum_value = (int32_t)(info->item_count - 1u);
    return ASTRA_OK;
}

AstraResult astra_interface_segmented_init(AstraControl *control,
                                           const AstraChoiceInfo *info)
{
    return choice_init(control, info, ASTRA_CONTROL_SEGMENTED);
}

AstraResult astra_interface_tab_init(AstraControl *control,
                                     const AstraChoiceInfo *info)
{
    return choice_init(control, info, ASTRA_CONTROL_TAB);
}

AstraResult astra_interface_field_init(AstraControl *control,
                                       const AstraFieldInfo *info)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    uint64_t width;

    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->model == NULL ||
        info->preferred_columns == 0u ||
        (info->flags & ~FIELD_FLAGS) != 0u ||
        (info->state & ~(ASTRA_CONTROL_DISABLED |
                         ASTRA_CONTROL_ERROR)) != 0u ||
        !astra_words_zero(info->reserved, 4u) ||
        astra_text_model_get_state(info->model, &state) != ASTRA_OK ||
        state.line_count != 1u || !field_content_valid(info->model))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    width = (uint64_t)info->preferred_columns * theme.mono_cell_width +
            (uint32_t)theme.control_padding_x * 2u;
    if (width > UINT32_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_FIELD;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_style = info->flags;
    control->_private_text = (const char *)(const void *)info->model;
    control->_private_text_length = info->preferred_columns;
    private_u32_set(&control->_private_maximum_value, state.generation);
    return ASTRA_OK;
}

AstraResult astra_interface_field_refresh(AstraUIContext *context,
                                          AstraControl *control)
{
    uint32_t index = control_index(context, control);
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;

    if (index == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_FIELD ||
        astra_text_model_get_state(field_model(control), &state) != ASTRA_OK ||
        state.line_count != 1u || !field_content_valid(field_model(control)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (private_u32(&control->_private_maximum_value) != state.generation) {
        control_damage(context, index);
        private_u32_set(&control->_private_maximum_value, state.generation);
        control_damage(context, index);
    }
    return ASTRA_OK;
}

AstraResult astra_interface_control_set_flex(AstraControl *control,
                                             const AstraFlexItem *item)
{
    if (!control_valid(control) || item == NULL || item->size < sizeof(*item) ||
        item->minimum_width > item->maximum_width ||
        item->minimum_height > item->maximum_height ||
        item->align_self > ASTRA_FLEX_ALIGN_AUTO ||
        (item->flags & ~FLEX_ITEM_FLAGS) != 0u ||
        !astra_words_zero(item->reserved, 3u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    control->_private_flex_grow = item->grow;
    control->_private_flex_shrink = item->shrink;
    control->_private_flex_basis = item->basis;
    control->_private_minimum_width = item->minimum_width;
    control->_private_minimum_height = item->minimum_height;
    control->_private_maximum_width = item->maximum_width;
    control->_private_maximum_height = item->maximum_height;
    control->_private_align_self = item->align_self;
    control->_private_flex_flags = item->flags;
    control->_private_parent_id = item->parent_id;
    return ASTRA_OK;
}

AstraResult astra_interface_control_set_text(AstraUIContext *context,
                                             AstraControl *control,
                                             const char *text,
                                             uint32_t text_length)
{
    uint32_t index = control_index(context, control);
    AstraControl before;
    AstraControl measured;
    AstraControlSize size;
    AstraResult result;

    if (index == CONTROL_NONE || text_length == 0u ||
        !astra_utf8_validate(text, text_length, 0u) ||
        (control->_private_control_kind != ASTRA_CONTROL_LABEL &&
         control->_private_control_kind != ASTRA_CONTROL_BUTTON &&
         control->_private_control_kind != ASTRA_CONTROL_DISCLOSURE &&
         !TOGGLE_KINDS(control->_private_control_kind)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    before = *control;
    measured = before;
    measured._private_text = text;
    measured._private_text_length = text_length;
    result = measure_control(&measured, &size);
    if (result != ASTRA_OK)
        return result;

    control_damage(context, index);
    control->_private_text = text;
    control->_private_text_length = text_length;
    control->_private_measured_width = size.width;
    control->_private_measured_height = size.height;
    if ((size.width != before._private_measured_width ||
         size.height != before._private_measured_height) &&
        context->_private_has_layout != 0u) {
        result = astra_interface_ui_layout(context,
                                           &context->_private_layout);
        if (result != ASTRA_OK) {
            *control = before;
            (void)astra_interface_ui_layout(context,
                                            &context->_private_layout);
            return result;
        }
    } else {
        control_damage(context, index);
    }
    return ASTRA_OK;
}

AstraResult astra_interface_ui_init(AstraUIContext *context,
                                    AstraControl *controls,
                                    uint32_t control_count,
                                    uint16_t width, uint16_t height)
{
    if (context == NULL || width == 0u || height == 0u ||
        (control_count != 0u && controls == NULL))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < control_count; ++at) {
        if (!control_valid(&controls[at]))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        for (uint32_t before = 0u; before < at; ++before)
            if (controls[before]._private_id == controls[at]._private_id)
                return ASTRA_ERROR_INVALID_ARGUMENT;
            else if (controls[at]._private_control_kind ==
                         ASTRA_CONTROL_RADIO &&
                     (controls[at]._private_state &
                      ASTRA_CONTROL_SELECTED) != 0u &&
                     controls[before]._private_control_kind ==
                         ASTRA_CONTROL_RADIO &&
                     controls[before]._private_style ==
                         controls[at]._private_style &&
                     (controls[before]._private_state &
                      ASTRA_CONTROL_SELECTED) != 0u)
                return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    *context = (AstraUIContext)ASTRA_UI_CONTEXT_INIT;
    context->_private_controls = controls;
    context->_private_control_count = control_count;
    context->_private_width = width;
    context->_private_height = height;
    for (uint32_t at = 0u; at < control_count; ++at) {
        AstraControl *control = &controls[at];
        uint32_t parent = CONTROL_NONE;

        control->_private_dynamic_state = 0u;
        control->_private_laid_out = 0u;
        control->_private_parent = CONTROL_NONE;
        control->_private_first_child = CONTROL_NONE;
        control->_private_last_child = CONTROL_NONE;
        control->_private_next_sibling = CONTROL_NONE;
        control->_private_animation_next = CONTROL_NONE;
        control->_private_animation_phase = 0u;
        if (control->_private_parent_id != 0u) {
            for (uint32_t before = 0u; before < at; ++before)
                if (controls[before]._private_id ==
                    control->_private_parent_id) {
                    parent = before;
                    break;
                }
            if (parent == CONTROL_NONE ||
                !CONTAINER_KINDS(controls[parent]._private_control_kind))
                return ASTRA_ERROR_INVALID_ARGUMENT;
        }
        control->_private_parent = parent;
        if (parent == CONTROL_NONE) {
            if (context->_private_last_child == CONTROL_NONE)
                context->_private_first_child = at;
            else
                controls[context->_private_last_child]
                    ._private_next_sibling = at;
            context->_private_last_child = at;
        } else {
            AstraControl *owner = &controls[parent];

            if (owner->_private_last_child == CONTROL_NONE)
                owner->_private_first_child = at;
            else
                controls[owner->_private_last_child]
                    ._private_next_sibling = at;
            owner->_private_last_child = at;
        }
    }
    for (uint32_t at = 0u; at < control_count; ++at)
        if (controls[at]._private_control_kind == ASTRA_CONTROL_SCROLL_VIEW &&
            (controls[at]._private_first_child == CONTROL_NONE ||
             controls[at]._private_first_child !=
                 controls[at]._private_last_child))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        else if (controls[at]._private_control_kind ==
                     ASTRA_CONTROL_DISCLOSURE) {
            uint32_t target = control_id_index(
                context, controls[at]._private_style);

            if (target == CONTROL_NONE ||
                controls[target]._private_control_kind !=
                    ASTRA_CONTROL_CONTAINER ||
                controls[target]._private_parent !=
                    controls[at]._private_parent)
                return ASTRA_ERROR_INVALID_ARGUMENT;
        }
    for (uint32_t count = control_count; count != 0u; --count) {
        uint32_t at = count - 1u;
        AstraControlSize measured;
        AstraResult result = controls[at]._private_control_kind ==
                ASTRA_CONTROL_CONTAINER ?
            measure_container(context, at, &measured) :
            measure_control(&controls[at], &measured);

        if (result != ASTRA_OK)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        controls[at]._private_measured_width = measured.width;
        controls[at]._private_measured_height = measured.height;
    }
    for (uint32_t at = 0u; at < control_count; ++at)
        animation_sync(context, at);
    return ASTRA_OK;
}

static AstraResult measure_control(const AstraControl *control,
                                   AstraControlSize *size)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    uint32_t width;

    if (size == NULL || !control_valid(control))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (control->_private_control_kind == ASTRA_CONTROL_LABEL) {
        size->width = astra_surface_ui_text_width(
            control->_private_text, control->_private_text_length,
            theme.body_font_height);
        size->height = theme.body_font_height;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_SCROLL_VIEW) {
        size->width = control->_private_text_length;
        size->height = control->_private_style;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_SCROLLBAR) {
        if ((control->_private_style & SCROLLBAR_STYLE_ORIENTATION) ==
            ASTRA_ORIENTATION_HORIZONTAL) {
            size->width = theme.spacing_unit * 32u;
            size->height = 10u;
        } else {
            size->width = 10u;
            size->height = theme.spacing_unit * 32u;
        }
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_SPLITTER) {
        if (control->_private_style == ASTRA_ORIENTATION_VERTICAL) {
            size->width = theme.spacing_unit * 2u;
            size->height = theme.spacing_unit * 8u;
        } else {
            size->width = theme.spacing_unit * 8u;
            size->height = theme.spacing_unit * 2u;
        }
        return ASTRA_OK;
    }
    if (TOGGLE_KINDS(control->_private_control_kind)) {
        uint32_t indicator = control->_private_control_kind ==
            ASTRA_CONTROL_SWITCH ? 34u : 16u;

        width = astra_surface_ui_text_width(
            control->_private_text, control->_private_text_length,
            theme.control_font_height);
        if (width > UINT32_MAX - indicator - theme.spacing_unit * 2u)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        size->width = indicator + theme.spacing_unit * 2u + width;
        size->height = theme.control_height;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_DISCLOSURE) {
        width = astra_surface_ui_text_width(
            control->_private_text, control->_private_text_length,
            theme.control_font_height);
        if (width > UINT32_MAX - 10u - theme.spacing_unit * 2u -
                        (uint32_t)theme.control_padding_x * 2u)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        size->width = width + 10u + theme.spacing_unit * 2u +
                      (uint32_t)theme.control_padding_x * 2u;
        size->height = theme.control_height;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_SLIDER) {
        if (control->_private_style == ASTRA_ORIENTATION_HORIZONTAL) {
            size->width = theme.spacing_unit * 32u;
            size->height = theme.control_height;
        } else {
            size->width = theme.control_height;
            size->height = theme.spacing_unit * 32u;
        }
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_DIAL) {
        size->width = 56u;
        size->height = 56u;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_STEPPER) {
        uint64_t minimum_magnitude =
            control->_private_minimum_value < 0 ?
                0u - (uint64_t)control->_private_minimum_value :
                (uint64_t)control->_private_minimum_value;
        uint64_t maximum_magnitude =
            control->_private_maximum_value < 0 ?
                0u - (uint64_t)control->_private_maximum_value :
                (uint64_t)control->_private_maximum_value;
        uint32_t minimum_length = control->_private_minimum_value < 0 ? 1u : 0u;
        uint32_t maximum_length = control->_private_maximum_value < 0 ? 1u : 0u;
        uint32_t value_width;

        do {
            ++minimum_length;
            minimum_magnitude /= 10u;
        } while (minimum_magnitude != 0u);
        do {
            ++maximum_length;
            maximum_magnitude /= 10u;
        } while (maximum_magnitude != 0u);
        width = minimum_length > maximum_length ? minimum_length :
                                                   maximum_length;
        value_width = width * theme.mono_cell_width +
                      theme.spacing_unit * 2u;
        if (value_width < 56u) value_width = 56u;
        if (value_width > UINT32_MAX - STEPPER_BUTTON_WIDTH)
            return ASTRA_ERROR_NO_RESOURCES;
        size->width = value_width + STEPPER_BUTTON_WIDTH;
        size->height = theme.control_height;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_PROGRESS) {
        size->width = theme.spacing_unit * 32u;
        size->height = theme.spacing_unit * 2u;
        return ASTRA_OK;
    }
    if (CHOICE_KINDS(control->_private_control_kind)) {
        const AstraChoiceItem *items = choice_items(control);
        uint32_t widest = 0u;
        uint64_t total_width = 0u;

        for (uint32_t at = 0u; at < control->_private_text_length; ++at) {
            uint32_t item_width = astra_surface_ui_text_width(
                items[at].text, items[at].text_length,
                theme.control_font_height);

            if (item_width > widest) widest = item_width;
            if (control->_private_control_kind == ASTRA_CONTROL_TAB)
                total_width += item_width + theme.spacing_unit * 6u;
        }
        if (control->_private_control_kind == ASTRA_CONTROL_SEGMENTED)
            total_width = ((uint64_t)widest + theme.spacing_unit * 4u) *
                          control->_private_text_length;
        if (total_width > UINT32_MAX)
            return ASTRA_ERROR_NO_RESOURCES;
        size->width = (uint32_t)total_width;
        size->height = theme.control_height;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_FIELD) {
        uint64_t field_width =
            (uint64_t)control->_private_text_length * theme.mono_cell_width +
            (uint32_t)theme.control_padding_x * 2u;

        if (field_width > UINT32_MAX)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        size->width = (uint32_t)field_width;
        size->height = theme.control_height;
        return ASTRA_OK;
    }
    width = astra_surface_ui_text_width(
        control->_private_text, control->_private_text_length,
        theme.control_font_height);
    if (width > UINT32_MAX - (uint32_t)theme.control_padding_x * 2u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    size->width = width + (uint32_t)theme.control_padding_x * 2u;
    size->height = theme.control_height;
    return ASTRA_OK;
}

AstraResult astra_interface_ui_measure(const AstraUIContext *context,
                                       const AstraControl *control,
                                       AstraControlSize *size)
{
    if (!context_valid(context) ||
        control_index(context, control) == CONTROL_NONE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (size == NULL || !control_valid(control))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    size->width = control->_private_measured_width;
    size->height = control->_private_measured_height;
    return ASTRA_OK;
}

static uint32_t axis_measured(const AstraControl *control, int row)
{
    return row ? control->_private_measured_width :
                 control->_private_measured_height;
}

static uint32_t axis_minimum(const AstraControl *control, int row)
{
    uint32_t configured = row ? control->_private_minimum_width :
                                control->_private_minimum_height;
    uint32_t measured = axis_measured(control, row);

    return configured > measured ? configured : measured;
}

static uint32_t axis_maximum(const AstraControl *control, int row)
{
    return row ? control->_private_maximum_width :
                 control->_private_maximum_height;
}

static uint32_t axis_base(const AstraControl *control, int row)
{
    uint32_t value = control->_private_flex_basis == ASTRA_FLEX_AUTO ?
        axis_measured(control, row) : control->_private_flex_basis;
    uint32_t minimum = axis_minimum(control, row);
    uint32_t maximum = axis_maximum(control, row);

    if (value < minimum) value = minimum;
    if (value > maximum) value = maximum;
    return value;
}

static uint32_t cross_base(const AstraControl *control, int row)
{
    uint32_t value = axis_measured(control, !row);
    uint32_t minimum = axis_minimum(control, !row);
    uint32_t maximum = axis_maximum(control, !row);

    if (value < minimum) value = minimum;
    if (value > maximum) value = maximum;
    return value;
}

static uint32_t next_layout_child(const AstraUIContext *context,
                                  uint32_t index)
{
    while (index != CONTROL_NONE &&
           (context->_private_controls[index]._private_state &
            ASTRA_CONTROL_COLLAPSED) != 0u)
        index = context->_private_controls[index]._private_next_sibling;
    return index;
}

static AstraResult measure_container(const AstraUIContext *context,
                                     uint32_t index,
                                     AstraControlSize *size)
{
    const AstraControl *container = &context->_private_controls[index];
    const AstraFlexLayout *layout = &container->_private_child_layout;
    uint64_t main = layout->direction == ASTRA_FLEX_ROW ?
        (uint64_t)layout->padding_left + layout->padding_right :
        (uint64_t)layout->padding_top + layout->padding_bottom;
    uint64_t cross_padding = layout->direction == ASTRA_FLEX_ROW ?
        (uint64_t)layout->padding_top + layout->padding_bottom :
        (uint64_t)layout->padding_left + layout->padding_right;
    uint32_t cross = 0u;
    uint32_t count = 0u;
    int row = layout->direction == ASTRA_FLEX_ROW;

    if (size == NULL || !layout_valid(layout))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = next_layout_child(
             context, container->_private_first_child);
         at != CONTROL_NONE;
         at = next_layout_child(
             context,
             context->_private_controls[at]._private_next_sibling)) {
        const AstraControl *child = &context->_private_controls[at];
        uint32_t child_cross = cross_base(child, row);

        main += axis_base(child, row);
        if (child_cross > cross) cross = child_cross;
        ++count;
    }
    if (count > 1u)
        main += (uint64_t)(count - 1u) * layout->main_gap;
    if (main > UINT32_MAX || cross_padding + cross > UINT32_MAX)
        return ASTRA_ERROR_NO_RESOURCES;
    if (row) {
        size->width = (uint32_t)main;
        size->height = (uint32_t)(cross_padding + cross);
    } else {
        size->width = (uint32_t)(cross_padding + cross);
        size->height = (uint32_t)main;
    }
    return ASTRA_OK;
}

static uint64_t line_used(const AstraUIContext *context, uint32_t first,
                          uint32_t after, uint32_t count, uint32_t gap)
{
    uint64_t used = count > 1u ? (uint64_t)(count - 1u) * gap : 0u;

    for (uint32_t at = first; at != after;
         at = next_layout_child(
             context,
             context->_private_controls[at]._private_next_sibling))
        used += context->_private_controls[at]._private_flex_target;
    return used;
}

static uint64_t item_weight(const AstraControl *control, int row, int grow)
{
    if (grow)
        return control->_private_flex_target < axis_maximum(control, row) ?
            control->_private_flex_grow : 0u;
    if (control->_private_flex_target <= axis_minimum(control, row))
        return 0u;
    return (uint64_t)control->_private_flex_shrink *
           control->_private_flex_target;
}

static int flex_line(AstraUIContext *context, uint32_t first, uint32_t after,
                     uint32_t count, uint32_t inner_main, uint32_t gap,
                     int row)
{
    uint64_t used = line_used(context, first, after, count, gap);
    int grow = used < inner_main;
    uint64_t remaining = grow ? inner_main - used : used - inner_main;

    /* ponytail: constraint redistribution can scan a line repeatedly; replace
       it with a scratch-backed active set only if measured UI sizes warrant it. */
    while (remaining != 0u) {
        uint64_t total = 0u;
        uint64_t seen = 0u;
        uint64_t apportioned = 0u;
        uint64_t applied = 0u;

        for (uint32_t at = first; at != after;
             at = next_layout_child(
                 context,
                 context->_private_controls[at]._private_next_sibling)) {
            uint64_t weight = item_weight(
                &context->_private_controls[at], row, grow);

            if (UINT64_MAX - total < weight)
                return 0;
            total += weight;
        }
        if (total == 0u || remaining > UINT64_MAX / total)
            return total == 0u;
        for (uint32_t at = first; at != after;
             at = next_layout_child(
                 context,
                 context->_private_controls[at]._private_next_sibling)) {
            AstraControl *control = &context->_private_controls[at];
            uint64_t weight = item_weight(control, row, grow);
            uint64_t entitled;
            uint64_t share;
            uint32_t room;

            if (weight == 0u) continue;
            seen += weight;
            entitled = remaining * seen / total;
            share = entitled - apportioned;
            apportioned = entitled;
            room = grow ? axis_maximum(control, row) -
                              control->_private_flex_target :
                          control->_private_flex_target -
                              axis_minimum(control, row);
            if (share > room) share = room;
            if (grow)
                control->_private_flex_target += (uint32_t)share;
            else
                control->_private_flex_target -= (uint32_t)share;
            applied += share;
        }
        if (applied == 0u)
            return 0;
        remaining -= applied;
    }
    return 1;
}

static int layout_valid(const AstraFlexLayout *layout)
{
    return layout != NULL && layout->size >= sizeof(*layout) &&
           layout->direction <= ASTRA_FLEX_COLUMN &&
           (layout->flags & ~ASTRA_FLEX_WRAP) == 0u &&
           layout->justify <= ASTRA_FLEX_JUSTIFY_SPACE_BETWEEN &&
           layout->align_items <= ASTRA_FLEX_ALIGN_STRETCH &&
           astra_words_zero(layout->reserved, 4u);
}

static AstraControlFrame frame_intersection(const AstraControlFrame *first,
                                            const AstraControlFrame *second)
{
    int64_t left = first->x > second->x ? first->x : second->x;
    int64_t top = first->y > second->y ? first->y : second->y;
    int64_t first_right = (int64_t)first->x + first->width;
    int64_t second_right = (int64_t)second->x + second->width;
    int64_t first_bottom = (int64_t)first->y + first->height;
    int64_t second_bottom = (int64_t)second->y + second->height;
    int64_t right = first_right < second_right ? first_right : second_right;
    int64_t bottom = first_bottom < second_bottom ? first_bottom : second_bottom;

    if (right < left) right = left;
    if (bottom < top) bottom = top;
    return (AstraControlFrame){
        (int32_t)left, (int32_t)top,
        (uint32_t)(right - left), (uint32_t)(bottom - top)};
}

static uint32_t scroll_content_extent(const AstraControl *control, int width)
{
    uint32_t measured = width ? control->_private_measured_width :
                                control->_private_measured_height;
    uint32_t minimum = width ? control->_private_minimum_width :
                               control->_private_minimum_height;

    return measured > minimum ? measured : minimum;
}

static AstraResult layout_scroll_view(AstraUIContext *context,
                                      uint32_t index)
{
    AstraControl *view = &context->_private_controls[index];
    AstraControl *child;
    AstraControlFrame clip;
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
    int64_t x;
    int64_t y;
    AstraResult result;

    if (view->_private_first_child == CONTROL_NONE ||
        view->_private_first_child != view->_private_last_child)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    child = &context->_private_controls[view->_private_first_child];
    result = astra_scroll_set_extents(
        scroll_model(view), scroll_content_extent(child, 1),
        scroll_content_extent(child, 0), view->_private_frame.width,
        view->_private_frame.height);
    if (result != ASTRA_OK ||
        astra_scroll_get_state(scroll_model(view), &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    x = (int64_t)view->_private_frame.x - state.offset_x;
    y = (int64_t)view->_private_frame.y - state.offset_y;
    if (x < INT32_MIN || x > INT32_MAX ||
        y < INT32_MIN || y > INT32_MAX)
        return ASTRA_ERROR_NO_RESOURCES;
    clip = frame_intersection(&view->_private_frame, &view->_private_clip);
    child->_private_frame = (AstraControlFrame){
        (int32_t)x, (int32_t)y,
        state.content_width, state.content_height};
    child->_private_clip = clip;
    child->_private_laid_out = 1u;
    control_damage(context, index);
    control_damage(context, view->_private_first_child);
    return ASTRA_OK;
}

static AstraResult layout_children(AstraUIContext *context, uint32_t child,
                                   const AstraFlexLayout *layout,
                                   const AstraControlFrame *bounds,
                                   const AstraControlFrame *clip)
{
    uint32_t inner_main;
    uint32_t inner_cross;
    int64_t cross_position;
    int row;

    child = next_layout_child(context, child);
    row = layout->direction == ASTRA_FLEX_ROW;
    inner_main = row ? bounds->width : bounds->height;
    inner_cross = row ? bounds->height : bounds->width;
    {
        uint64_t main_padding = row ?
            (uint64_t)layout->padding_left + layout->padding_right :
            (uint64_t)layout->padding_top + layout->padding_bottom;
        uint64_t cross_padding = row ?
            (uint64_t)layout->padding_top + layout->padding_bottom :
            (uint64_t)layout->padding_left + layout->padding_right;

        inner_main = main_padding < inner_main ?
            inner_main - (uint32_t)main_padding : 0u;
        inner_cross = cross_padding < inner_cross ?
            inner_cross - (uint32_t)cross_padding : 0u;
    }
    for (uint32_t at = child; at != CONTROL_NONE;
         at = next_layout_child(
             context,
             context->_private_controls[at]._private_next_sibling))
        context->_private_controls[at]._private_flex_target =
            axis_base(&context->_private_controls[at], row);
    cross_position = (int64_t)(row ? bounds->y : bounds->x) +
                     (row ? layout->padding_top : layout->padding_left);
    for (uint32_t first = child; first != CONTROL_NONE;) {
        uint32_t after = first;
        uint32_t line_count = 0u;
        uint64_t natural = 0u;
        uint32_t line_cross = 0u;
        uint32_t main_gap = layout->main_gap;
        uint32_t justify_offset = 0u;
        uint32_t gap_extra = 0u;
        uint32_t gap_remainder = 0u;
        uint32_t previous = CONTROL_NONE;
        uint64_t used;
        int64_t main_position;

        while (after != CONTROL_NONE) {
            AstraControl *control = &context->_private_controls[after];
            uint64_t addition = control->_private_flex_target;

            if (line_count != 0u) addition += layout->main_gap;
            if (line_count != 0u &&
                ((control->_private_flex_flags & ASTRA_FLEX_BREAK_BEFORE) != 0u ||
                 ((layout->flags & ASTRA_FLEX_WRAP) != 0u &&
                  natural + addition > inner_main)))
                break;
            natural += addition;
            ++line_count;
            after = next_layout_child(context,
                                      control->_private_next_sibling);
            if ((control->_private_flex_flags & ASTRA_FLEX_BREAK_AFTER) != 0u)
                break;
        }
        if (!flex_line(context, first, after, line_count, inner_main,
                       layout->main_gap, row))
            return ASTRA_ERROR_NO_RESOURCES;
        used = line_used(context, first, after, line_count, layout->main_gap);
        if ((layout->flags & ASTRA_FLEX_WRAP) == 0u) {
            line_cross = inner_cross;
        } else {
            for (uint32_t at = first; at != after;
                 at = next_layout_child(
                     context,
                     context->_private_controls[at]._private_next_sibling)) {
                uint32_t value = cross_base(
                    &context->_private_controls[at], row);
                if (value > line_cross) line_cross = value;
            }
        }
        if (used < inner_main) {
            uint32_t spare = inner_main - (uint32_t)used;

            if (layout->justify == ASTRA_FLEX_JUSTIFY_CENTER)
                justify_offset = spare / 2u;
            else if (layout->justify == ASTRA_FLEX_JUSTIFY_END)
                justify_offset = spare;
            else if (layout->justify == ASTRA_FLEX_JUSTIFY_SPACE_BETWEEN &&
                     line_count > 1u) {
                uint32_t spaces = line_count - 1u;
                gap_extra = spare / spaces;
                gap_remainder = spare % spaces;
            }
        }
        main_position = (int64_t)(row ? bounds->x : bounds->y) +
                        (row ? layout->padding_left : layout->padding_top) +
                        justify_offset;
        for (uint32_t at = first; at != after;) {
            AstraControl *control = &context->_private_controls[at];
            uint32_t next = next_layout_child(
                context, control->_private_next_sibling);
            uint32_t align = control->_private_align_self ==
                    ASTRA_FLEX_ALIGN_AUTO ? layout->align_items :
                                           control->_private_align_self;
            uint32_t cross = cross_base(control, row);
            uint32_t cross_offset = 0u;
            AstraControlFrame frame;

            if (align == ASTRA_FLEX_ALIGN_STRETCH) {
                uint32_t maximum = axis_maximum(control, !row);
                cross = line_cross < maximum ? line_cross : maximum;
            } else if (line_cross > cross && align == ASTRA_FLEX_ALIGN_CENTER) {
                cross_offset = (line_cross - cross) / 2u;
            } else if (line_cross > cross && align == ASTRA_FLEX_ALIGN_END) {
                cross_offset = line_cross - cross;
            }
            if (main_position < INT32_MIN || main_position > INT32_MAX ||
                cross_position + cross_offset < INT32_MIN ||
                cross_position + cross_offset > INT32_MAX)
                return ASTRA_ERROR_NO_RESOURCES;
            frame = row ? (AstraControlFrame){
                (int32_t)main_position,
                (int32_t)(cross_position + cross_offset),
                control->_private_flex_target, cross} :
                (AstraControlFrame){
                (int32_t)(cross_position + cross_offset),
                (int32_t)main_position,
                cross, control->_private_flex_target};
            control->_private_frame = frame;
            control->_private_clip = *clip;
            control->_private_laid_out = 1u;
            control_damage(context, at);
            if (previous != CONTROL_NONE &&
                context->_private_controls[previous]._private_control_kind ==
                    ASTRA_CONTROL_SPLITTER) {
                AstraControl *splitter =
                    &context->_private_controls[previous];
                uint32_t before;
                uint32_t after_splitter;
                int splitter_row;

                if (!splitter_neighbors(context, previous, layout, &before,
                                        &after_splitter, &splitter_row) ||
                    after_splitter != at)
                    return ASTRA_ERROR_INVALID_ARGUMENT;
                splitter->_private_value = (int32_t)(splitter_row ?
                    context->_private_controls[before]._private_frame.width :
                    context->_private_controls[before]._private_frame.height);
            }
            previous = at;
            main_position += control->_private_flex_target;
            if (next != after) {
                main_position += main_gap + gap_extra;
                if (gap_remainder != 0u) {
                    ++main_position;
                    --gap_remainder;
                }
            }
            at = next;
        }
        if (previous != CONTROL_NONE &&
            context->_private_controls[previous]._private_control_kind ==
                ASTRA_CONTROL_SPLITTER)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        cross_position += line_cross;
        if (after != CONTROL_NONE) cross_position += layout->cross_gap;
        first = after;
    }
    return ASTRA_OK;
}

static int splitter_neighbors(const AstraUIContext *context, uint32_t index,
                              const AstraFlexLayout *root_layout,
                              uint32_t *before, uint32_t *after, int *row)
{
    const AstraControl *splitter;
    const AstraFlexLayout *layout;
    uint32_t first;
    uint32_t previous = CONTROL_NONE;
    uint32_t at;

    if (index >= context->_private_control_count || before == NULL ||
        after == NULL || row == NULL)
        return 0;
    splitter = &context->_private_controls[index];
    if (splitter->_private_control_kind != ASTRA_CONTROL_SPLITTER ||
        splitter->_private_laid_out == 0u)
        return 0;
    if (splitter->_private_parent == CONTROL_NONE) {
        layout = root_layout;
        first = context->_private_first_child;
    } else {
        const AstraControl *parent =
            &context->_private_controls[splitter->_private_parent];

        if (parent->_private_control_kind != ASTRA_CONTROL_CONTAINER)
            return 0;
        layout = &parent->_private_child_layout;
        first = parent->_private_first_child;
    }
    if (!layout_valid(layout) || (layout->flags & ASTRA_FLEX_WRAP) != 0u)
        return 0;
    *row = layout->direction == ASTRA_FLEX_ROW;
    if (splitter->_private_style != (*row ? ASTRA_ORIENTATION_VERTICAL :
                                            ASTRA_ORIENTATION_HORIZONTAL))
        return 0;
    for (at = first; at != CONTROL_NONE && at != index;
         at = context->_private_controls[at]._private_next_sibling)
        previous = at;
    if (at != index || previous == CONTROL_NONE)
        return 0;
    at = splitter->_private_next_sibling;
    if (at == CONTROL_NONE ||
        context->_private_controls[previous]._private_laid_out == 0u ||
        context->_private_controls[at]._private_laid_out == 0u ||
        context->_private_controls[previous]._private_control_kind ==
            ASTRA_CONTROL_SPLITTER ||
        context->_private_controls[at]._private_control_kind ==
            ASTRA_CONTROL_SPLITTER)
        return 0;
    *before = previous;
    *after = at;
    return 1;
}

AstraResult astra_interface_ui_layout(AstraUIContext *context,
                                      const AstraFlexLayout *layout)
{
    AstraControlFrame root;
    AstraResult result;

    if (!context_valid(context) || !layout_valid(layout))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t count = context->_private_control_count; count != 0u;
         --count) {
        uint32_t at = count - 1u;
        AstraControl *control = &context->_private_controls[at];

        if (!control_valid(control))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER) {
            AstraControlSize measured;

            result = measure_container(context, at, &measured);
            if (result != ASTRA_OK) return result;
            control->_private_measured_width = measured.width;
            control->_private_measured_height = measured.height;
        } else if (control->_private_measured_width == 0u ||
                   control->_private_measured_height == 0u) {
            return ASTRA_ERROR_INVALID_ARGUMENT;
        }
    }
    for (uint32_t at = 0u; at < context->_private_control_count; ++at) {
        control_damage(context, at);
        context->_private_controls[at]._private_laid_out = 0u;
        context->_private_controls[at]._private_clip = (AstraControlFrame){0};
    }
    root = (AstraControlFrame){
        0, 0, context->_private_width, context->_private_height};
    result = layout_children(context, context->_private_first_child, layout,
                             &root, &root);
    if (result != ASTRA_OK) return result;
    for (uint32_t at = 0u; at < context->_private_control_count; ++at) {
        AstraControl *container = &context->_private_controls[at];
        AstraControlFrame clip;

        if (!CONTAINER_KINDS(container->_private_control_kind) ||
            container->_private_laid_out == 0u)
            continue;
        if (container->_private_control_kind == ASTRA_CONTROL_SCROLL_VIEW) {
            result = layout_scroll_view(context, at);
            if (result != ASTRA_OK) return result;
            continue;
        }
        clip = frame_intersection(&container->_private_frame,
                                  &container->_private_clip);
        result = layout_children(context, container->_private_first_child,
                                 &container->_private_child_layout,
                                 &container->_private_frame, &clip);
        if (result != ASTRA_OK) return result;
    }
    context->_private_layout = *layout;
    context->_private_has_layout = 1u;
    for (uint32_t at = 0u; at < context->_private_control_count; ++at)
        animation_sync(context, at);
    return ASTRA_OK;
}

static AstraResult splitter_set_position(AstraUIContext *context,
                                         uint32_t index, int64_t position,
                                         AstraUIAction *action)
{
    AstraControl *splitter;
    AstraControl *before_control;
    AstraControl *after_control;
    uint32_t before;
    uint32_t after;
    uint32_t before_size;
    uint32_t after_size;
    uint32_t old_before_basis;
    uint32_t old_after_basis;
    int64_t delta;
    int64_t minimum_delta;
    int64_t maximum_delta;
    int64_t bound;
    int row;
    AstraResult result;

    if (!context_valid(context) || context->_private_has_layout == 0u ||
        !splitter_neighbors(context, index, &context->_private_layout,
                            &before, &after, &row))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    splitter = &context->_private_controls[index];
    before_control = &context->_private_controls[before];
    after_control = &context->_private_controls[after];
    before_size = row ? before_control->_private_frame.width :
                        before_control->_private_frame.height;
    after_size = row ? after_control->_private_frame.width :
                       after_control->_private_frame.height;
    minimum_delta = (int64_t)axis_minimum(before_control, row) - before_size;
    bound = (int64_t)after_size - axis_maximum(after_control, row);
    if (bound > minimum_delta) minimum_delta = bound;
    maximum_delta = (int64_t)axis_maximum(before_control, row) - before_size;
    bound = (int64_t)after_size - axis_minimum(after_control, row);
    if (bound < maximum_delta) maximum_delta = bound;
    if (minimum_delta > maximum_delta)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (position <= (int64_t)before_size + minimum_delta)
        delta = minimum_delta;
    else if (position >= (int64_t)before_size + maximum_delta)
        delta = maximum_delta;
    else
        delta = position - before_size;
    if (delta == 0)
        return ASTRA_OK;

    old_before_basis = before_control->_private_flex_basis;
    old_after_basis = after_control->_private_flex_basis;
    before_control->_private_flex_basis =
        (uint32_t)((int64_t)before_size + delta);
    after_control->_private_flex_basis =
        (uint32_t)((int64_t)after_size - delta);
    result = astra_interface_ui_layout(context, &context->_private_layout);
    if (result != ASTRA_OK) {
        before_control->_private_flex_basis = old_before_basis;
        after_control->_private_flex_basis = old_after_basis;
        (void)astra_interface_ui_layout(context, &context->_private_layout);
        return result;
    }
    if (action != NULL) {
        action->type = ASTRA_UI_ACTION_VALUE_CHANGED;
        action->control_id = splitter->_private_id;
        action->value = splitter->_private_value;
        action->decimal_places = 0u;
    }
    return ASTRA_OK;
}

static int control_descends_from(const AstraUIContext *context,
                                 uint32_t index, uint32_t ancestor)
{
    uint32_t at = context->_private_controls[index]._private_parent;

    while (at != CONTROL_NONE) {
        if (at == ancestor) return 1;
        at = context->_private_controls[at]._private_parent;
    }
    return 0;
}

static AstraResult relayout_scroll_model(AstraUIContext *context,
                                         const AstraScrollModel *model)
{
    int found = 0;

    for (uint32_t at = 0u; at < context->_private_control_count; ++at) {
        AstraControl *view = &context->_private_controls[at];
        AstraResult result;

        if (view->_private_control_kind != ASTRA_CONTROL_SCROLL_VIEW ||
            const_scroll_model(view) != model ||
            view->_private_laid_out == 0u)
            continue;
        found = 1;
        control_damage(context, at);
        result = layout_scroll_view(context, at);
        if (result != ASTRA_OK) return result;
        for (uint32_t child = at + 1u;
             child < context->_private_control_count; ++child) {
            AstraControl *container = &context->_private_controls[child];
            AstraControlFrame clip;

            if (!control_descends_from(context, child, at) ||
                !CONTAINER_KINDS(container->_private_control_kind) ||
                container->_private_laid_out == 0u)
                continue;
            if (container->_private_control_kind ==
                ASTRA_CONTROL_SCROLL_VIEW) {
                result = layout_scroll_view(context, child);
            } else {
                clip = frame_intersection(&container->_private_frame,
                                          &container->_private_clip);
                result = layout_children(
                    context, container->_private_first_child,
                    &container->_private_child_layout,
                    &container->_private_frame, &clip);
            }
            if (result != ASTRA_OK) return result;
        }
    }
    for (uint32_t at = 0u; at < context->_private_control_count; ++at)
        if (context->_private_controls[at]._private_control_kind ==
                ASTRA_CONTROL_SCROLLBAR &&
            const_scroll_model(&context->_private_controls[at]) == model)
            control_damage(context, at);
    return found ? ASTRA_OK : ASTRA_ERROR_INVALID_ARGUMENT;
}

AstraResult astra_interface_ui_set_state(AstraUIContext *context,
                                         AstraControl *control,
                                         uint32_t state)
{
    uint32_t index = control_index(context, control);
    uint32_t allowed;
    uint32_t before;
    AstraResult result;

    if (index == CONTROL_NONE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (CONTAINER_KINDS(control->_private_control_kind))
        allowed = ASTRA_CONTROL_COLLAPSED;
    else if (control->_private_control_kind == ASTRA_CONTROL_DISCLOSURE)
        allowed = ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_COLLAPSED;
    else if (control->_private_control_kind == ASTRA_CONTROL_SCROLLBAR ||
             control->_private_control_kind == ASTRA_CONTROL_SPLITTER)
        allowed = ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_COLLAPSED;
    else if (control->_private_control_kind == ASTRA_CONTROL_LABEL)
        allowed = ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_COLLAPSED;
    else if (control->_private_control_kind == ASTRA_CONTROL_SLIDER ||
             control->_private_control_kind == ASTRA_CONTROL_DIAL ||
             control->_private_control_kind == ASTRA_CONTROL_STEPPER ||
             control->_private_control_kind == ASTRA_CONTROL_PROGRESS ||
             CHOICE_KINDS(control->_private_control_kind) ||
             control->_private_control_kind == ASTRA_CONTROL_FIELD)
        allowed = CONTROL_VALUE_STATES;
    else
        allowed = CONTROL_SEMANTIC_STATES;
    if ((state & ~allowed) != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (control->_private_control_kind == ASTRA_CONTROL_RADIO &&
        (state & ASTRA_CONTROL_SELECTED) != 0u) {
        for (uint32_t at = 0u; at < context->_private_control_count; ++at) {
            AstraControl *peer = &context->_private_controls[at];

            if (at != index &&
                peer->_private_control_kind == ASTRA_CONTROL_RADIO &&
                peer->_private_style == control->_private_style &&
                (peer->_private_state & ASTRA_CONTROL_SELECTED) != 0u) {
                control_damage(context, at);
                peer->_private_state &= ~ASTRA_CONTROL_SELECTED;
                control_damage(context, at);
            }
        }
    }
    if (control->_private_state == state)
        return ASTRA_OK;
    before = control->_private_state;
    if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER)
        disclosure_target_damage(context, control->_private_id);
    control_damage(context, index);
    control->_private_state = state;
    if ((state & ASTRA_CONTROL_DISABLED) != 0u) {
        if (context->_private_hover == index) set_hover(context, CONTROL_NONE);
        if (context->_private_capture == index) clear_capture(context);
        if (context->_private_keyboard == index) clear_keyboard(context);
        if (context->_private_focus == index)
            set_focus(context, CONTROL_NONE, 0);
    }
    if (((before ^ state) & ASTRA_CONTROL_COLLAPSED) != 0u &&
        context->_private_has_layout != 0u) {
        result = astra_interface_ui_layout(context,
                                           &context->_private_layout);
        if (result != ASTRA_OK) {
            control->_private_state = before;
            (void)astra_interface_ui_layout(context,
                                            &context->_private_layout);
            if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER)
                disclosure_target_damage(context, control->_private_id);
            return result;
        }
        if (context->_private_hover != CONTROL_NONE &&
            context->_private_controls[context->_private_hover]
                    ._private_laid_out == 0u)
            set_hover(context, CONTROL_NONE);
        if (context->_private_capture != CONTROL_NONE &&
            context->_private_controls[context->_private_capture]
                    ._private_laid_out == 0u)
            clear_capture(context);
        if (context->_private_keyboard != CONTROL_NONE &&
            context->_private_controls[context->_private_keyboard]
                    ._private_laid_out == 0u)
            clear_keyboard(context);
        if (context->_private_focus != CONTROL_NONE &&
            context->_private_controls[context->_private_focus]
                    ._private_laid_out == 0u)
            set_focus(context, CONTROL_NONE, 0);
        if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER)
            disclosure_target_damage(context, control->_private_id);
        return ASTRA_OK;
    }
    animation_sync(context, index);
    control_damage(context, index);
    if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER)
        disclosure_target_damage(context, control->_private_id);
    return ASTRA_OK;
}

static uint64_t range_offset_at(uint64_t span, uint32_t position,
                                uint32_t extent)
{
    uint64_t quotient;
    uint64_t remainder;

    if (extent == 0u) return 0u;
    quotient = span / extent;
    remainder = span % extent;
    return quotient * position + remainder * position / extent;
}

static uint32_t range_position(uint64_t offset, uint64_t span,
                               uint32_t extent)
{
    uint32_t low = 0u;
    uint32_t high = extent;

    while (low < high) {
        uint32_t middle = low + (high - low + 1u) / 2u;

        if (range_offset_at(span, middle, extent) <= offset)
            low = middle;
        else
            high = middle - 1u;
    }
    return low;
}

static int64_t range_snap(const AstraControl *control, int64_t value)
{
    int64_t minimum = control->_private_minimum_value;
    int64_t maximum = control->_private_maximum_value;
    uint64_t step = (uint64_t)control->_private_value_step;
    uint64_t span;
    uint64_t offset;
    uint64_t lower;
    uint64_t upper;

    if (value <= minimum) return minimum;
    if (value >= maximum) return maximum;
    span = range_offset(maximum, minimum);
    offset = range_offset(value, minimum);
    lower = offset / step * step;
    upper = step <= span - lower ? lower + step : span;
    return range_value_at(
        minimum,
        offset - lower < upper - offset ? lower : upper);
}

static int64_t range_move_steps(const AstraControl *control, int64_t value,
                                int64_t steps)
{
    uint64_t offset = range_offset(value, control->_private_minimum_value);
    uint64_t span = range_offset(control->_private_maximum_value,
                                 control->_private_minimum_value);
    uint64_t step = (uint64_t)control->_private_value_step;
    uint64_t count;

    if (steps >= 0) {
        count = (uint64_t)steps;
        if (count > (span - offset) / step)
            return control->_private_maximum_value;
        offset += count * step;
    } else {
        count = 0u - (uint64_t)steps;
        if (count > offset / step)
            return control->_private_minimum_value;
        offset -= count * step;
    }
    return range_value_at(control->_private_minimum_value, offset);
}

static int stepper_draft_value(const AstraUIContext *context,
                               int64_t *value)
{
    uint64_t magnitude = context->_private_value_input;

    if ((context->_private_value_input_flags & VALUE_INPUT_EDITING) == 0u ||
        context->_private_value_input_digits == 0u)
        return 0;
    if ((context->_private_value_input_flags & VALUE_INPUT_NEGATIVE) != 0u) {
        if (magnitude > UINT64_C(9223372036854775808)) return 0;
        *value = magnitude == UINT64_C(9223372036854775808) ? INT64_MIN :
                 -(int64_t)magnitude;
    } else {
        if (magnitude > INT64_MAX) return 0;
        *value = (int64_t)magnitude;
    }
    return 1;
}

static uint32_t stepper_format(const AstraUIContext *context,
                               uint32_t index, char text[22])
{
    const AstraControl *control = &context->_private_controls[index];
    char reversed[20];
    uint64_t magnitude;
    uint32_t length = 0u;
    uint32_t digits = 0u;
    int negative;

    if (context->_private_focus == index &&
        (context->_private_value_input_flags & VALUE_INPUT_EDITING) != 0u) {
        magnitude = context->_private_value_input;
        negative = (context->_private_value_input_flags &
                    VALUE_INPUT_NEGATIVE) != 0u;
        if (negative) text[length++] = '-';
        if (context->_private_value_input_digits == 0u)
            return length;
    } else {
        int64_t value = control->_private_value;

        negative = value < 0;
        magnitude = negative ? 0u - (uint64_t)value : (uint64_t)value;
        if (negative) text[length++] = '-';
    }
    do {
        reversed[digits++] = (char)('0' + magnitude % 10u);
        magnitude /= 10u;
    } while (magnitude != 0u);
    while (digits != 0u) text[length++] = reversed[--digits];
    return length;
}

AstraResult astra_interface_control_set_value(AstraUIContext *context,
                                              AstraControl *control,
                                              int64_t value)
{
    uint32_t index = control_index(context, control);
    int64_t snapped;

    if (index == CONTROL_NONE ||
        (control->_private_control_kind != ASTRA_CONTROL_SLIDER &&
         control->_private_control_kind != ASTRA_CONTROL_DIAL &&
         control->_private_control_kind != ASTRA_CONTROL_STEPPER &&
         control->_private_control_kind != ASTRA_CONTROL_SPLITTER &&
         !CHOICE_KINDS(control->_private_control_kind)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (control->_private_control_kind == ASTRA_CONTROL_SPLITTER)
        return splitter_set_position(context, index, value, NULL);
    if (CHOICE_KINDS(control->_private_control_kind)) {
        if (value < 0 || value > control->_private_maximum_value)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        snapped = value;
    } else {
        snapped = range_snap(control, value);
    }
    if (snapped == control->_private_value)
        return ASTRA_OK;
    control_damage(context, index);
    control->_private_value = snapped;
    control_damage(context, index);
    return ASTRA_OK;
}

AstraResult astra_interface_control_get_value(const AstraControl *control,
                                              int64_t *value,
                                              uint32_t *decimal_places)
{
    if (!control_valid(control) || value == NULL ||
        (control->_private_control_kind != ASTRA_CONTROL_SLIDER &&
         control->_private_control_kind != ASTRA_CONTROL_DIAL &&
         control->_private_control_kind != ASTRA_CONTROL_STEPPER &&
         control->_private_control_kind != ASTRA_CONTROL_PROGRESS &&
         control->_private_control_kind != ASTRA_CONTROL_SPLITTER &&
         !CHOICE_KINDS(control->_private_control_kind)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *value = control->_private_value;
    if (decimal_places != NULL)
        *decimal_places = control->_private_decimal_places;
    return ASTRA_OK;
}

AstraResult astra_interface_progress_set(AstraUIContext *context,
                                         AstraControl *control,
                                         uint32_t value, uint32_t maximum)
{
    uint32_t index = control_index(context, control);

    if (index == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_PROGRESS ||
        value > maximum || maximum > INT32_MAX ||
        (maximum == 0u && value != 0u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (control->_private_value == (int32_t)value &&
        control->_private_maximum_value == (int32_t)maximum)
        return ASTRA_OK;
    control_damage(context, index);
    control->_private_value = (int32_t)value;
    control->_private_maximum_value = (int32_t)maximum;
    animation_sync(context, index);
    control_damage(context, index);
    return ASTRA_OK;
}

static AstraColorRGBA8 label_color(const AstraTheme *theme,
                                   const AstraControl *control)
{
    if ((effective_state(control) & ASTRA_CONTROL_DISABLED) != 0u)
        return theme->text_muted;
    switch (control->_private_style) {
    case ASTRA_TEXT_CLIENT_SECONDARY: return theme->client_text_secondary;
    case ASTRA_TEXT_CLIENT_TERTIARY: return theme->text_tertiary;
    case ASTRA_TEXT_CLIENT_MUTED: return theme->text_muted;
    case ASTRA_TEXT_CLIENT_FAULT: return theme->fault;
    default: return theme->client_text;
    }
}

static AstraColorRGBA8 button_fill(const AstraTheme *theme,
                                   const AstraControl *control)
{
    uint32_t state = effective_state(control);

    if ((state & ASTRA_CONTROL_DISABLED) != 0u)
        return theme->control_disabled;
    if ((state & ASTRA_CONTROL_PRESSED) != 0u)
        return theme->control_pressed;
    if ((state & ASTRA_CONTROL_SELECTED) != 0u)
        return theme->control_selected;
    if (control->_private_style == ASTRA_BUTTON_PRIMARY)
        return theme->accent;
    if (control->_private_style == ASTRA_BUTTON_WARNING)
        return theme->warning;
    if (control->_private_style == ASTRA_BUTTON_DESTRUCTIVE)
        return theme->fault;
    if ((state & ASTRA_CONTROL_HOVERED) != 0u)
        return theme->control_hover;
    return theme->control;
}

static AstraColorRGBA8 button_text(const AstraTheme *theme,
                                   const AstraControl *control)
{
    uint32_t state = effective_state(control);

    if ((state & ASTRA_CONTROL_DISABLED) != 0u)
        return theme->text_tertiary;
    if ((state & ASTRA_CONTROL_PRESSED) != 0u)
        return theme->text_secondary;
    if ((state & ASTRA_CONTROL_SELECTED) != 0u ||
        control->_private_style == ASTRA_BUTTON_PRIMARY ||
        control->_private_style == ASTRA_BUTTON_WARNING)
        return theme->accent_text;
    return theme->text_primary;
}

static AstraColorRGBA8 toggle_text(const AstraTheme *theme,
                                   const AstraControl *control)
{
    return (effective_state(control) & ASTRA_CONTROL_DISABLED) != 0u ?
        theme->text_tertiary : theme->client_text;
}

typedef struct FieldView {
    AstraTextModelState state;
    uint32_t start;
    uint32_t end;
    uint32_t columns;
    uint32_t caret_column;
    uint32_t selection_first;
    uint32_t selection_last;
} FieldView;

static int model_scalar_count(const AstraTextModel *model, uint32_t start,
                              uint32_t end, uint32_t *count)
{
    uint32_t total = 0u;

    while (start < end) {
        const char *text;
        uint32_t bytes;
        uint32_t limit;
        uint32_t at = 0u;

        if (astra_text_model_read(model, start, &text, &bytes) != ASTRA_OK ||
            bytes == 0u)
            return 0;
        limit = bytes < end - start ? bytes : end - start;
        while (at < limit) {
            uint32_t before = at;

            if (!astra_utf8_scalar_advance(text, limit, &at) || at <= before)
                return 0;
            ++total;
        }
        start += limit;
    }
    *count = total;
    return 1;
}

static int model_advance_scalars(const AstraTextModel *model,
                                 uint32_t start, uint32_t maximum,
                                 uint32_t *end)
{
    uint32_t moved = 0u;

    while (moved < maximum) {
        const char *text;
        uint32_t bytes;
        uint32_t at = 0u;

        if (astra_text_model_read(model, start, &text, &bytes) != ASTRA_OK)
            return 0;
        if (bytes == 0u)
            break;
        while (at < bytes && moved < maximum) {
            uint32_t before = at;

            if (!astra_utf8_scalar_advance(text, bytes, &at) || at <= before)
                return 0;
            ++moved;
        }
        start += at;
    }
    *end = start;
    return 1;
}

static int field_view(const AstraControl *control, FieldView *view)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    const AstraTextModel *model = const_field_model(control);
    uint32_t inner_width;
    uint32_t scroll = private_u32(&control->_private_value);
    uint32_t distance = 0u;
    uint32_t selection_start;
    uint32_t selection_end;
    const char *ignored_text;
    uint32_t ignored_bytes;

    *view = (FieldView){.state = ASTRA_TEXT_MODEL_STATE_INIT};
    if (astra_text_model_get_state(model, &view->state) != ASTRA_OK)
        return 0;
    inner_width = control->_private_frame.width >
                      (uint32_t)theme.control_padding_x * 2u ?
        control->_private_frame.width -
            (uint32_t)theme.control_padding_x * 2u : 0u;
    view->columns = inner_width / theme.mono_cell_width;
    if (scroll > view->state.text_bytes ||
        astra_text_model_read(model, scroll, &ignored_text,
                              &ignored_bytes) != ASTRA_OK)
        scroll = 0u;
    if (view->state.selection.focus < scroll ||
        !model_scalar_count(model, scroll, view->state.selection.focus,
                            &distance) ||
        (view->columns != 0u && distance >= view->columns)) {
        uint32_t remaining = view->columns == 0u ? 0u : view->columns - 1u;

        scroll = view->state.selection.focus;
        while (remaining-- != 0u &&
               astra_text_model_scalar_retreat(model, &scroll) == ASTRA_OK)
            ;
    }
    view->start = scroll;
    if (!model_advance_scalars(model, view->start, view->columns, &view->end) ||
        !model_scalar_count(model, view->start,
                            view->state.selection.focus,
                            &view->caret_column))
        return 0;
    selection_start = view->state.selection.anchor <
                              view->state.selection.focus ?
        view->state.selection.anchor : view->state.selection.focus;
    selection_end = view->state.selection.anchor >
                            view->state.selection.focus ?
        view->state.selection.anchor : view->state.selection.focus;
    if (selection_start < view->start) selection_start = view->start;
    if (selection_end > view->end) selection_end = view->end;
    if (selection_start < selection_end &&
        (!model_scalar_count(model, view->start, selection_start,
                             &view->selection_first) ||
         !model_scalar_count(model, view->start, selection_end,
                             &view->selection_last)))
        return 0;
    return 1;
}

static int field_caret_visible_at(uint32_t phase)
{
    return phase < 30u || ((phase / 30u) & 1u) == 0u;
}

static int field_caret_visible(const AstraUIContext *context,
                               const AstraControl *control)
{
    (void)context;
    return field_caret_visible_at(control->_private_animation_phase);
}

static int field_render_requirements(const AstraUIContext *context,
                                     const AstraControl *control,
                                     uint64_t *commands, uint64_t *payload)
{
    FieldView view;
    uint32_t offset;
    uint32_t state = effective_state(control);

    if (!field_view(control, &view))
        return 0;
    *commands += 2u;
    if ((state & CONTROL_FOCUS_VISIBLE) != 0u) ++*commands;
    if (view.selection_first < view.selection_last) ++*commands;
    if ((state & (ASTRA_CONTROL_FOCUSED | ASTRA_CONTROL_DISABLED)) ==
            ASTRA_CONTROL_FOCUSED && field_caret_visible(context, control))
        ++*commands;
    offset = view.start;
    while (offset < view.end) {
        const char *text;
        uint32_t bytes;
        uint32_t take;

        if (astra_text_model_read(const_field_model(control), offset,
                                  &text, &bytes) != ASTRA_OK || bytes == 0u)
            return 0;
        take = bytes < view.end - offset ? bytes : view.end - offset;
        ++*commands;
        *payload += take;
        offset += take;
    }
    return 1;
}

static int render_requirements(const AstraUIContext *context,
                               uint64_t *commands, uint64_t *payload)
{
    *commands = 0u;
    *payload = 0u;
    for (uint32_t at = 0u; at < context->_private_control_count; ++at) {
        const AstraControl *control = &context->_private_controls[at];
        uint32_t state;

        if (!control_valid(control))
            return 0;
        if (control->_private_laid_out == 0u)
            continue;
        if (CONTAINER_KINDS(control->_private_control_kind))
            continue;
        if (control->_private_clip.width == 0u ||
            control->_private_clip.height == 0u)
            continue;
        state = effective_state(control);
        if (control->_private_control_kind == ASTRA_CONTROL_FIELD) {
            if (!field_render_requirements(context, control,
                                           commands, payload))
                return 0;
            continue;
        }
        if (control->_private_control_kind == ASTRA_CONTROL_SCROLLBAR) {
            AstraScrollState scroll = ASTRA_SCROLL_STATE_INIT;

            if ((control->_private_style & SCROLLBAR_STYLE_OVERLAY) != 0u &&
                (state & (ASTRA_CONTROL_HOVERED |
                          ASTRA_CONTROL_PRESSED)) == 0u &&
                control->_private_animation_phase >= SCROLLBAR_FADE_FRAMES)
                continue;

            if (astra_scroll_get_state(
                    const_scroll_model(control), &scroll) != ASTRA_OK)
                return 0;
            *commands += 2u + scroll.mark_count;
            continue;
        }
        if (control->_private_control_kind == ASTRA_CONTROL_SPLITTER) {
            ++*commands;
            continue;
        }
        if (CHOICE_KINDS(control->_private_control_kind)) {
            const AstraChoiceItem *items = choice_items(control);

            if (control->_private_control_kind == ASTRA_CONTROL_SEGMENTED) {
                *commands += 2u +
                    (uint64_t)control->_private_text_length * 2u;
                if (control->_private_minimum_value >= 0 &&
                    control->_private_minimum_value != control->_private_value)
                    ++*commands;
                if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
                    ++*commands;
            } else {
                *commands += 2u + control->_private_text_length;
                if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
                    *commands += 2u;
            }
            for (uint32_t item = 0u;
                 item < control->_private_text_length; ++item)
                *payload += items[item].text_length;
            continue;
        }
        if (control->_private_control_kind == ASTRA_CONTROL_LABEL)
            *commands += 1u;
        else if (control->_private_control_kind == ASTRA_CONTROL_BUTTON)
            *commands += 2u;
        else if (control->_private_control_kind == ASTRA_CONTROL_DISCLOSURE)
            *commands += 7u;
        else if (control->_private_control_kind == ASTRA_CONTROL_CHECKBOX)
            *commands += 3u +
                (((state & ASTRA_CONTROL_SELECTED) != 0u) ? 5u : 0u);
        else if (control->_private_control_kind == ASTRA_CONTROL_RADIO)
            *commands += 3u +
                (((state & ASTRA_CONTROL_SELECTED) != 0u) ? 1u : 0u);
        else if (control->_private_control_kind == ASTRA_CONTROL_SLIDER)
            *commands += 2u +
                ((control->_private_value >
                  control->_private_minimum_value) ? 1u : 0u);
        else if (control->_private_control_kind == ASTRA_CONTROL_DIAL)
            *commands += 3u +
                (((state & CONTROL_FOCUS_VISIBLE) != 0u) ? 1u : 0u);
        else if (control->_private_control_kind == ASTRA_CONTROL_STEPPER) {
            char text[22];

            *commands += 15u;
            if ((state & CONTROL_FOCUS_VISIBLE) != 0u) ++*commands;
            if (context->_private_hover == at &&
                context->_private_hover_part != STEPPER_PART_VALUE)
                ++*commands;
            *payload += stepper_format(context, at, text);
        }
        else if (control->_private_control_kind == ASTRA_CONTROL_PROGRESS)
            *commands += 2u;
        else
            *commands += 3u +
                ((control->_private_control_kind == ASTRA_CONTROL_SWITCH &&
                  ((state & ASTRA_CONTROL_SELECTED) == 0u ||
                   (state & ASTRA_CONTROL_DISABLED) != 0u)) ? 1u : 0u);
        if (control->_private_control_kind == ASTRA_CONTROL_BUTTON &&
            (state & CONTROL_FOCUS_VISIBLE) != 0u)
            ++*commands;
        if (control->_private_control_kind == ASTRA_CONTROL_DISCLOSURE &&
            (state & CONTROL_FOCUS_VISIBLE) != 0u)
            ++*commands;
        if (control->_private_control_kind == ASTRA_CONTROL_BUTTON &&
            (state & ASTRA_CONTROL_ERROR) != 0u)
            ++*commands;
        if (TOGGLE_KINDS(control->_private_control_kind) &&
            (state & CONTROL_FOCUS_VISIBLE) != 0u)
            ++*commands;
        if (control->_private_control_kind == ASTRA_CONTROL_SLIDER &&
            (state & CONTROL_FOCUS_VISIBLE) != 0u)
            ++*commands;
        if (control->_private_control_kind != ASTRA_CONTROL_STEPPER)
            *payload += control->_private_text_length;
    }
    return 1;
}

static int draw_list_has_room(const AstraSurfaceView *surface,
                              uint64_t commands, uint64_t payload)
{
    AstraSurfaceView adopted;
    const AstraDrawListHeader *header =
        (const AstraDrawListHeader *)(const void *)surface->pixels;

    return astra_draw_list_view_adopt(
               &adopted, surface->pixels, surface->byte_size,
               surface->width, surface->height) &&
           commands <= ASTRA_DRAW_LIST_COMMAND_MAX - header->command_count &&
           payload <= ASTRA_DRAW_LIST_PAYLOAD_BYTES - header->payload_bytes;
}

static void render_label(const AstraTheme *theme,
                         const AstraControl *control,
                         AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t length = astra_surface_ui_text_fit(
        control->_private_text, control->_private_text_length,
        theme->body_font_height, frame->width);
    int32_t y = frame->y +
        (int32_t)(frame->height - theme->body_font_height) / 2;

    astra_surface_ui_text(surface, frame->x, y, control->_private_text,
                          length, theme->body_font_height,
                          color(label_color(theme, control)));
}

static void render_button(const AstraTheme *theme,
                          const AstraControl *control,
                          AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t state = effective_state(control);
    int32_t x = frame->x;
    int32_t y = frame->y;
    uint32_t width = frame->width;
    uint32_t height = frame->height;
    uint32_t inset = 0u;
    uint32_t text_width;
    int32_t text_x;
    int32_t text_y;

    if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
        astra_surface_fill_round(
            surface, x - theme->focus_width, y - theme->focus_width,
            width + (uint32_t)theme->focus_width * 2u,
            height + (uint32_t)theme->focus_width * 2u,
            (uint16_t)(theme->control_radius + theme->focus_width),
            color(theme->control_focus));
    if ((state & ASTRA_CONTROL_ERROR) != 0u) {
        astra_surface_fill_round(surface, x, y, width, height,
                                 theme->control_radius,
                                 color(theme->control_error));
        inset = theme->control_border_width;
    }
    astra_surface_fill_round(
        surface, x + (int32_t)inset, y + (int32_t)inset,
        width - inset * 2u, height - inset * 2u,
        theme->control_radius > inset ?
            (uint16_t)(theme->control_radius - inset) : 0u,
        color(button_fill(theme, control)));
    text_width = astra_surface_ui_text_width(
        control->_private_text, control->_private_text_length,
        theme->control_font_height);
    text_x = x + (int32_t)(width - text_width) / 2;
    text_y = y + (int32_t)(height - theme->control_font_height) / 2;
    astra_surface_ui_text(surface, text_x, text_y, control->_private_text,
                          control->_private_text_length,
                          theme->control_font_height,
                          color(button_text(theme, control)));
}

static void render_toggle(const AstraTheme *theme,
                          const AstraControl *control,
                          AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t state = effective_state(control);
    uint32_t indicator_width = control->_private_control_kind ==
        ASTRA_CONTROL_SWITCH ? 34u : 16u;
    uint32_t indicator_height = control->_private_control_kind ==
        ASTRA_CONTROL_SWITCH ? 18u : 16u;
    int32_t x = frame->x;
    int32_t y = frame->y + (int32_t)(frame->height - indicator_height) / 2;
    AstraColorRGBA8 border = (state & ASTRA_CONTROL_ERROR) != 0u ?
        theme->control_error : theme->client_border;
    AstraColorRGBA8 fill = (state & ASTRA_CONTROL_DISABLED) != 0u ?
        theme->control_disabled : theme->client;

    if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
        astra_surface_fill_round(
            surface, x - theme->focus_width, y - theme->focus_width,
            indicator_width + (uint32_t)theme->focus_width * 2u,
            indicator_height + (uint32_t)theme->focus_width * 2u,
            (uint16_t)(indicator_height / 2u + theme->focus_width),
            color(theme->control_focus));
    if (control->_private_control_kind == ASTRA_CONTROL_SWITCH) {
        int disabled = (state & ASTRA_CONTROL_DISABLED) != 0u;
        int selected = (state & ASTRA_CONTROL_SELECTED) != 0u;
        AstraColorRGBA8 track = disabled ? theme->control_disabled :
            (selected ? theme->accent : theme->surface_inset);
        int32_t thumb_x = x + ((state & ASTRA_CONTROL_SELECTED) != 0u ?
                               18 : 2);

        astra_surface_fill_round(surface, x, y, indicator_width,
                                 indicator_height, indicator_height / 2u,
                                 color(track));
        if (!selected || disabled) {
            astra_surface_fill_round(
                surface, thumb_x, y + 2, 14u, 14u, 7u,
                color(disabled ? theme->text_tertiary :
                                 theme->client_border));
            astra_surface_fill_round(surface, thumb_x + 2, y + 4,
                                     10u, 10u, 5u, color(track));
        } else {
            astra_surface_fill_round(surface, thumb_x, y + 2,
                                     14u, 14u, 7u, color(theme->client));
        }
    } else {
        uint16_t radius = control->_private_control_kind ==
            ASTRA_CONTROL_RADIO ? 8u : 4u;

        if ((state & ASTRA_CONTROL_SELECTED) != 0u)
            border = (state & ASTRA_CONTROL_DISABLED) != 0u ?
                theme->text_tertiary : theme->accent;
        astra_surface_fill_round(surface, x, y, 16u, 16u, radius,
                                 color(border));
        astra_surface_fill_round(surface, x + 2, y + 2, 12u, 12u,
                                 radius > 2u ? (uint16_t)(radius - 2u) : 0u,
                                 color((state & ASTRA_CONTROL_SELECTED) != 0u ?
                                       border : fill));
        if ((state & ASTRA_CONTROL_SELECTED) != 0u &&
            control->_private_control_kind == ASTRA_CONTROL_RADIO)
            astra_surface_fill_round(surface, x + 5, y + 5, 6u, 6u, 3u,
                                     color(theme->accent_text));
        if ((state & ASTRA_CONTROL_SELECTED) != 0u &&
            control->_private_control_kind == ASTRA_CONTROL_CHECKBOX) {
            AstraColorRGBA8 mark = (state & ASTRA_CONTROL_DISABLED) != 0u ?
                theme->client : theme->accent_text;

            astra_surface_fill(surface, x + 3, y + 7, 3u, 3u, color(mark));
            astra_surface_fill(surface, x + 5, y + 9, 3u, 3u, color(mark));
            astra_surface_fill(surface, x + 7, y + 7, 3u, 3u, color(mark));
            astra_surface_fill(surface, x + 9, y + 5, 3u, 3u, color(mark));
            astra_surface_fill(surface, x + 11, y + 3, 3u, 3u,
                               color(mark));
        }
    }
    astra_surface_ui_text(
        surface, x + (int32_t)indicator_width + theme->spacing_unit * 2,
        frame->y + (int32_t)(frame->height - theme->control_font_height) / 2,
        control->_private_text, control->_private_text_length,
        theme->control_font_height, color(toggle_text(theme, control)));
}

static AstraColorRGBA8 blend(AstraColorRGBA8 background,
                             AstraColorRGBA8 foreground, uint32_t alpha)
{
    uint32_t inverse = 255u - alpha;

    return (AstraColorRGBA8){
        (uint8_t)(((uint32_t)background.red * inverse +
                   (uint32_t)foreground.red * alpha + 127u) / 255u),
        (uint8_t)(((uint32_t)background.green * inverse +
                   (uint32_t)foreground.green * alpha + 127u) / 255u),
        (uint8_t)(((uint32_t)background.blue * inverse +
                   (uint32_t)foreground.blue * alpha + 127u) / 255u),
        255u};
}

static AstraResult render_field(const AstraUIContext *context,
                                const AstraTheme *theme,
                                const AstraControl *control,
                                AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t state = effective_state(control);
    AstraColorRGBA8 background =
        (state & ASTRA_CONTROL_DISABLED) != 0u ?
            theme->control_disabled :
            ((state & ASTRA_CONTROL_FOCUSED) != 0u ?
                 theme->client : theme->surface_inset);
    AstraColorRGBA8 border = (state & ASTRA_CONTROL_ERROR) != 0u ?
        theme->control_error :
        ((state & ASTRA_CONTROL_FOCUSED) != 0u ?
             theme->control_focus : theme->client_border);
    FieldView view;
    AstraSurfaceView clipped = *surface;
    int32_t text_x = frame->x + theme->control_padding_x;
    int32_t text_y = frame->y +
        (int32_t)(frame->height - theme->mono_font_height) / 2;
    uint32_t offset;
    uint32_t column = 0u;

    if (!field_view(control, &view))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
        astra_surface_fill_round(
            surface, frame->x - theme->focus_width,
            frame->y - theme->focus_width,
            frame->width + (uint32_t)theme->focus_width * 2u,
            frame->height + (uint32_t)theme->focus_width * 2u,
            (uint16_t)(theme->control_radius + theme->focus_width),
            color(theme->control_focus));
    astra_surface_fill_round(surface, frame->x, frame->y,
                             frame->width, frame->height,
                             theme->control_radius, color(border));
    astra_surface_fill_round(
        surface, frame->x + theme->control_border_width,
        frame->y + theme->control_border_width,
        frame->width - (uint32_t)theme->control_border_width * 2u,
        frame->height - (uint32_t)theme->control_border_width * 2u,
        theme->control_radius > theme->control_border_width ?
            (uint16_t)(theme->control_radius -
                       theme->control_border_width) : 0u,
        color(background));
    if (!astra_surface_clip(
            &clipped, text_x, frame->y + theme->control_border_width,
            frame->width > (uint32_t)theme->control_padding_x * 2u ?
                frame->width - (uint32_t)theme->control_padding_x * 2u : 0u,
            frame->height - (uint32_t)theme->control_border_width * 2u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (view.selection_first < view.selection_last) {
        AstraColorRGBA8 selection = blend(background, theme->accent, 71u);

        astra_surface_fill(
            &clipped,
            text_x + (int32_t)(view.selection_first *
                               theme->mono_cell_width),
            text_y,
            (view.selection_last - view.selection_first) *
                theme->mono_cell_width,
            theme->mono_font_height, color(selection));
    }
    offset = view.start;
    while (offset < view.end) {
        const char *text;
        uint32_t bytes;
        uint32_t take;
        uint32_t scalars;

        if (astra_text_model_read(const_field_model(control), offset,
                                  &text, &bytes) != ASTRA_OK || bytes == 0u)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        take = bytes < view.end - offset ? bytes : view.end - offset;
        if (!model_scalar_count(const_field_model(control), offset,
                                offset + take, &scalars) ||
            !astra_surface_mono_text_styled(
                &clipped,
                text_x + (int32_t)(column * theme->mono_cell_width),
                text_y, text, take, theme->mono_font_height,
                theme->mono_cell_width,
                color((state & ASTRA_CONTROL_DISABLED) != 0u ?
                      theme->text_tertiary : theme->client_text), 0u))
            return ASTRA_ERROR_IO;
        column += scalars;
        offset += take;
    }
    if ((state & (ASTRA_CONTROL_FOCUSED | ASTRA_CONTROL_DISABLED)) ==
            ASTRA_CONTROL_FOCUSED && field_caret_visible(context, control))
        astra_surface_fill(
            &clipped,
            text_x + (int32_t)(view.caret_column *
                               theme->mono_cell_width),
            text_y, 2u, theme->mono_font_height, color(theme->accent));
    return ASTRA_OK;
}

static void render_slider(const AstraTheme *theme,
                          const AstraControl *control,
                          AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t state = effective_state(control);
    uint64_t range = range_offset(control->_private_maximum_value,
                                  control->_private_minimum_value);
    uint64_t offset = range_offset(control->_private_value,
                                   control->_private_minimum_value);
    AstraColorRGBA8 track = (state & ASTRA_CONTROL_ERROR) != 0u ?
        theme->control_error : theme->surface_inset;
    AstraColorRGBA8 active = (state & ASTRA_CONTROL_DISABLED) != 0u ?
        theme->text_tertiary : theme->accent;
    AstraColorRGBA8 thumb = (state & ASTRA_CONTROL_DISABLED) != 0u ?
        theme->control_disabled : theme->client_border;
    int32_t thumb_x;
    int32_t thumb_y;

    if (control->_private_style == ASTRA_ORIENTATION_HORIZONTAL) {
        uint32_t travel = frame->width - 14u;
        uint32_t position = range_position(offset, range, travel);
        int32_t center_y = frame->y + (int32_t)frame->height / 2;

        astra_surface_fill_round(surface, frame->x + 7, center_y - 2,
                                 travel, 4u, 2u, color(track));
        if (offset != 0u)
            astra_surface_fill_round(surface, frame->x + 7, center_y - 2,
                                     position, 4u, 2u, color(active));
        thumb_x = frame->x + (int32_t)position;
        thumb_y = center_y - 9;
    } else {
        uint32_t travel = frame->height - 14u;
        uint32_t position = range_position(offset, range, travel);
        int32_t center_x = frame->x + (int32_t)frame->width / 2;

        astra_surface_fill_round(surface, center_x - 2, frame->y + 7,
                                 4u, travel, 2u, color(track));
        if (offset != 0u)
            astra_surface_fill_round(
                surface, center_x - 2,
                frame->y + 7 + (int32_t)(travel - position),
                4u, position, 2u, color(active));
        thumb_x = center_x - 7;
        thumb_y = frame->y + (int32_t)(travel - position);
    }
    if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
        astra_surface_fill_round(
            surface, thumb_x - theme->focus_width,
            thumb_y - theme->focus_width,
            14u + (uint32_t)theme->focus_width * 2u,
            18u + (uint32_t)theme->focus_width * 2u,
            (uint16_t)(7u + theme->focus_width),
            color(theme->control_focus));
    astra_surface_fill_round(surface, thumb_x, thumb_y, 14u, 18u, 7u,
                             color(thumb));
}

static void render_dial(const AstraTheme *theme,
                        const AstraControl *control,
                        AstraSurfaceView *surface)
{
    static const int8_t hand[33][2] = {
        {-13, 13}, {-14, 11}, {-16, 8}, {-17, 6}, {-18, 4}, {-18, 1},
        {-18, -2}, {-17, -4}, {-17, -7}, {-15, -9}, {-14, -11},
        {-12, -13}, {-10, -15}, {-8, -16}, {-5, -17}, {-3, -18},
        {0, -18}, {3, -18}, {5, -17}, {8, -16}, {10, -15},
        {12, -13}, {14, -11}, {15, -9}, {17, -7}, {17, -4},
        {18, -2}, {18, 1}, {18, 4}, {17, 6}, {16, 8}, {14, 11},
        {13, 13}
    };
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t state = effective_state(control);
    uint64_t range = range_offset(control->_private_maximum_value,
                                  control->_private_minimum_value);
    uint64_t offset = range_offset(control->_private_value,
                                   control->_private_minimum_value);
    uint32_t index = range_position(offset, range, 32u);
    int32_t center_x = frame->x + (int32_t)frame->width / 2;
    int32_t center_y = frame->y + (int32_t)frame->height / 2;
    AstraColorRGBA8 border = (state & ASTRA_CONTROL_ERROR) != 0u ?
        theme->control_error : theme->client_border;
    AstraColorRGBA8 face = (state & ASTRA_CONTROL_DISABLED) != 0u ?
        theme->control_disabled : theme->surface_inset;
    AstraColorRGBA8 ink = (state & ASTRA_CONTROL_DISABLED) != 0u ?
        theme->text_tertiary : theme->accent;

    if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
        astra_surface_fill_round(surface, center_x - 27, center_y - 27,
                                 54u, 54u, 27u,
                                 color(theme->control_focus));
    astra_surface_fill_round(surface, center_x - 24, center_y - 24,
                             48u, 48u, 24u, color(border));
    astra_surface_fill_round(surface, center_x - 22, center_y - 22,
                             44u, 44u, 22u, color(face));
    (void)astra_surface_line(surface, center_x, center_y,
                             center_x + hand[index][0],
                             center_y + hand[index][1], color(ink));
}

typedef struct ScrollbarGeometry {
    uint32_t content;
    uint32_t viewport;
    uint32_t offset;
    uint32_t maximum;
    uint32_t track_length;
    uint32_t thumb_position;
    uint32_t thumb_length;
    int vertical;
} ScrollbarGeometry;

static int scrollbar_geometry(const AstraControl *control,
                              ScrollbarGeometry *geometry)
{
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
    uint32_t travel;

    if (astra_scroll_get_state(
            const_scroll_model(control), &state) != ASTRA_OK)
        return 0;
    geometry->vertical =
        (control->_private_style & SCROLLBAR_STYLE_ORIENTATION) ==
        ASTRA_ORIENTATION_VERTICAL;
    geometry->content = geometry->vertical ? state.content_height :
                                             state.content_width;
    geometry->viewport = geometry->vertical ? state.viewport_height :
                                              state.viewport_width;
    geometry->offset = geometry->vertical ? state.offset_y : state.offset_x;
    geometry->maximum = geometry->vertical ? state.maximum_y : state.maximum_x;
    geometry->track_length = geometry->vertical ?
        control->_private_frame.height : control->_private_frame.width;
    geometry->thumb_length = geometry->track_length;
    geometry->thumb_position = 0u;
    if (geometry->maximum == 0u || geometry->track_length == 0u)
        return 1;
    geometry->thumb_length = (uint32_t)(
        (uint64_t)geometry->viewport * geometry->track_length /
        geometry->content);
    if (geometry->thumb_length < 24u) geometry->thumb_length = 24u;
    if (geometry->thumb_length > geometry->track_length)
        geometry->thumb_length = geometry->track_length;
    travel = geometry->track_length - geometry->thumb_length;
    geometry->thumb_position = (uint32_t)(
        (uint64_t)geometry->offset * travel / geometry->maximum);
    return 1;
}

static uint32_t scrollbar_part(const AstraControl *control,
                               int32_t x, int32_t y)
{
    ScrollbarGeometry geometry;
    int64_t coordinate;
    int64_t first;
    int64_t last;

    if (!scrollbar_geometry(control, &geometry) || geometry.maximum == 0u)
        return SCROLLBAR_PART_NONE;
    coordinate = geometry.vertical ? y : x;
    first = (geometry.vertical ? control->_private_frame.y :
                                 control->_private_frame.x) +
            geometry.thumb_position;
    last = first + geometry.thumb_length;
    if (coordinate < first) return SCROLLBAR_PART_BEFORE;
    if (coordinate >= last) return SCROLLBAR_PART_AFTER;
    return SCROLLBAR_PART_THUMB;
}

static void scrollbar_drag_save(AstraUIContext *context, int32_t pointer,
                                 uint32_t offset)
{
    context->_private_value_input =
        ((uint64_t)(uint32_t)pointer << 32) | offset;
}

static int32_t scrollbar_drag_pointer(const AstraUIContext *context)
{
    return (int32_t)(uint32_t)(context->_private_value_input >> 32);
}

static uint32_t scrollbar_drag_offset(const AstraUIContext *context)
{
    return (uint32_t)context->_private_value_input;
}

static uint32_t scrollbar_offset(const ScrollbarGeometry *geometry,
                                 int64_t value)
{
    if (value < 0) return 0u;
    if ((uint64_t)value > geometry->maximum) return geometry->maximum;
    return (uint32_t)value;
}

static void scroll_activity(AstraUIContext *context,
                            const AstraScrollModel *model)
{
    for (uint32_t at = 0u; at < context->_private_control_count; ++at) {
        AstraControl *control = &context->_private_controls[at];

        if (control->_private_control_kind != ASTRA_CONTROL_SCROLLBAR ||
            const_scroll_model(control) != model ||
            (control->_private_style & SCROLLBAR_STYLE_OVERLAY) == 0u)
            continue;
        control_damage(context, at);
        control->_private_animation_phase = 0u;
        animation_sync(context, at);
        control_damage(context, at);
    }
}

static AstraResult scroll_apply(AstraUIContext *context,
                                AstraScrollModel *model,
                                uint32_t x, uint32_t y,
                                uint32_t control_id,
                                AstraUIAction *action)
{
    AstraScrollState before = ASTRA_SCROLL_STATE_INIT;
    uint32_t previous_scroll_damage = context->_private_reserved;
    uint32_t scroll_view = CONTROL_NONE;
    uint32_t scroll_view_count = 0u;
    AstraResult result;

    if (astra_scroll_get_state(model, &before) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_scroll_set_offset(model, x, y);
    if (result != ASTRA_OK)
        return result;
    {
        AstraScrollState after = ASTRA_SCROLL_STATE_INIT;

        if (astra_scroll_get_state(model, &after) != ASTRA_OK)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if (after.generation == before.generation)
            return ASTRA_OK;
        for (uint32_t at = 0u; at < context->_private_control_count; ++at) {
            const AstraControl *control = &context->_private_controls[at];

            if (control->_private_control_kind == ASTRA_CONTROL_SCROLL_VIEW &&
                control->_private_laid_out != 0u &&
                const_scroll_model(control) == model) {
                scroll_view = at;
                ++scroll_view_count;
            }
        }
        if (scroll_view_count == 1u &&
            ((context->_private_has_damage == 0u &&
              previous_scroll_damage == 0u) ||
             (previous_scroll_damage != SCROLL_DAMAGE_INVALID &&
              (previous_scroll_damage & SCROLL_DAMAGE_INDEX_MASK) ==
                  scroll_view + 1u))) {
            context->_private_reserved =
                SCROLL_DAMAGE_RECORDING | (scroll_view + 1u);
        } else {
            context->_private_reserved = SCROLL_DAMAGE_INVALID;
        }
        result = relayout_scroll_model(context, model);
        if (result != ASTRA_OK) {
            (void)astra_scroll_set_offset(model, before.offset_x,
                                          before.offset_y);
            (void)relayout_scroll_model(context, model);
            context->_private_reserved = previous_scroll_damage;
            return result;
        }
        action->type = ASTRA_UI_ACTION_SCROLL_CHANGED;
        action->control_id = control_id;
        action->value = 0;
        scroll_activity(context, model);
        if (context->_private_reserved != SCROLL_DAMAGE_INVALID)
            context->_private_reserved &= ~SCROLL_DAMAGE_RECORDING;
    }
    return ASTRA_OK;
}

static AstraResult scrollbar_set(AstraUIContext *context, uint32_t index,
                                 uint32_t value, AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;

    if (astra_scroll_get_state(scroll_model(control), &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return (control->_private_style & SCROLLBAR_STYLE_ORIENTATION) ==
            ASTRA_ORIENTATION_VERTICAL ?
        scroll_apply(context, scroll_model(control), state.offset_x, value,
                     control->_private_id, action) :
        scroll_apply(context, scroll_model(control), value, state.offset_y,
                     control->_private_id, action);
}

static AstraResult scrollbar_pointer(AstraUIContext *context,
                                     uint32_t index,
                                     const AstraWindowEvent *event,
                                     AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    ScrollbarGeometry geometry;
    int32_t pointer;
    int64_t value;

    if (!scrollbar_geometry(control, &geometry) || geometry.maximum == 0u)
        return ASTRA_OK;
    pointer = geometry.vertical ? event->data.pointer.y :
                                  event->data.pointer.x;
    if (event->type == ASTRA_WINDOW_EVENT_POINTER_MOTION) {
        int64_t delta = (int64_t)pointer -
                        scrollbar_drag_pointer(context);

        if ((event->data.pointer.modifiers & ASTRA_INPUT_MOD_SHIFT) != 0u) {
            value = (int64_t)scrollbar_drag_offset(context) + delta;
        } else {
            uint32_t travel = geometry.track_length - geometry.thumb_length;

            value = scrollbar_drag_offset(context);
            if (travel != 0u)
                value += delta * geometry.maximum / travel;
        }
        return scrollbar_set(context, index,
                             scrollbar_offset(&geometry, value), action);
    }
    if ((event->flags & ASTRA_WINDOW_EVENT_DOWN) == 0u)
        return ASTRA_OK;
    if (context->_private_capture_part == SCROLLBAR_PART_THUMB) {
        scrollbar_drag_save(context, pointer, geometry.offset);
        return ASTRA_OK;
    }
    if ((event->data.pointer.modifiers & ASTRA_INPUT_MOD_ALT) != 0u) {
        int64_t relative = (int64_t)pointer -
            (geometry.vertical ? control->_private_frame.y :
                                 control->_private_frame.x) -
            geometry.thumb_length / 2u;
        uint32_t travel = geometry.track_length - geometry.thumb_length;

        value = travel == 0u ? 0 : relative * geometry.maximum / travel;
    } else if (context->_private_capture_part == SCROLLBAR_PART_BEFORE) {
        value = (int64_t)geometry.offset - geometry.viewport;
    } else {
        value = (int64_t)geometry.offset + geometry.viewport;
    }
    return scrollbar_set(context, index,
                         scrollbar_offset(&geometry, value), action);
}

static AstraColorRGBA8 scroll_mark_color(const AstraTheme *theme,
                                         uint32_t kind)
{
    if (kind == ASTRA_SCROLL_MARK_WARNING) return theme->warning;
    if (kind == ASTRA_SCROLL_MARK_FAULT) return theme->fault;
    return theme->accent;
}

static void render_scrollbar(const AstraTheme *theme,
                             const AstraControl *control,
                             AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
    ScrollbarGeometry geometry;
    uint32_t state_flags = effective_state(control);
    AstraColorRGBA8 thumb;
    AstraColorRGBA8 track;
    uint32_t alpha = 255u;
    int32_t thumb_x;
    int32_t thumb_y;
    uint32_t thumb_width;
    uint32_t thumb_height;

    if (!scrollbar_geometry(control, &geometry) ||
        astra_scroll_get_state(
            const_scroll_model(control), &state) != ASTRA_OK)
        return;
    if ((control->_private_style & SCROLLBAR_STYLE_OVERLAY) != 0u &&
        (state_flags & (ASTRA_CONTROL_HOVERED |
                        ASTRA_CONTROL_PRESSED)) == 0u) {
        if (control->_private_animation_phase >= SCROLLBAR_FADE_FRAMES)
            return;
        alpha = (SCROLLBAR_FADE_FRAMES -
                 control->_private_animation_phase) * 255u /
                SCROLLBAR_FADE_FRAMES;
    }
    track = (control->_private_style & SCROLLBAR_STYLE_OVERLAY) != 0u ?
        blend(theme->frame, theme->surface_inset, alpha / 3u) :
        theme->surface_inset;
    astra_surface_fill_round(surface, frame->x, frame->y,
                             frame->width, frame->height, 5u,
                             color(track));
    thumb = geometry.maximum == 0u ||
            (state_flags & ASTRA_CONTROL_DISABLED) != 0u ?
        theme->control_disabled :
        ((state_flags & ASTRA_CONTROL_PRESSED) != 0u ? theme->accent :
         ((state_flags & ASTRA_CONTROL_HOVERED) != 0u ?
              theme->control_hover : theme->text_tertiary));
    if ((control->_private_style & SCROLLBAR_STYLE_OVERLAY) != 0u)
        thumb = blend(theme->frame, thumb, alpha);
    thumb_x = frame->x + (geometry.vertical ?
        (int32_t)(frame->width > 6u ? (frame->width - 6u) / 2u : 0u) :
        (int32_t)geometry.thumb_position);
    thumb_y = frame->y + (geometry.vertical ?
        (int32_t)geometry.thumb_position :
        (int32_t)(frame->height > 6u ? (frame->height - 6u) / 2u : 0u));
    thumb_width = geometry.vertical ?
        (frame->width < 6u ? frame->width : 6u) : geometry.thumb_length;
    thumb_height = geometry.vertical ? geometry.thumb_length :
        (frame->height < 6u ? frame->height : 6u);
    astra_surface_fill_round(surface, thumb_x, thumb_y,
                             thumb_width, thumb_height, 3u, color(thumb));
    if (geometry.maximum == 0u)
        return;
    for (uint32_t at = 0u; at < state.mark_count; ++at) {
        uint32_t coordinate = geometry.vertical ? state.marks[at].y :
                                                  state.marks[at].x;
        uint32_t position = geometry.content == 0u ? 0u : (uint32_t)(
            (uint64_t)coordinate * geometry.track_length /
            geometry.content);

        if (position >= geometry.track_length && geometry.track_length != 0u)
            position = geometry.track_length - 1u;
        if (geometry.vertical)
            astra_surface_fill(surface, frame->x, frame->y + position,
                               frame->width, 2u,
                               color(scroll_mark_color(
                                   theme, state.marks[at].kind)));
        else
            astra_surface_fill(surface, frame->x + position, frame->y,
                               2u, frame->height,
                               color(scroll_mark_color(
                                   theme, state.marks[at].kind)));
    }
}

static void render_stepper_arrow(AstraSurfaceView *surface, int32_t center_x,
                                 int32_t center_y, int up, uint16_t ink)
{
    int32_t direction = up ? 1 : -1;

    astra_surface_fill(surface, center_x - 4, center_y + direction * 2,
                       2u, 2u, ink);
    astra_surface_fill(surface, center_x - 2, center_y,
                       2u, 2u, ink);
    astra_surface_fill(surface, center_x, center_y - direction * 2,
                       2u, 2u, ink);
    astra_surface_fill(surface, center_x + 2, center_y,
                       2u, 2u, ink);
    astra_surface_fill(surface, center_x + 4, center_y + direction * 2,
                       2u, 2u, ink);
}

static void render_disclosure(const AstraUIContext *context,
                              const AstraTheme *theme,
                              const AstraControl *control,
                              AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t target = control_id_index(context, control->_private_style);
    uint32_t state = effective_state(control);
    int expanded = target != CONTROL_NONE &&
        (context->_private_controls[target]._private_state &
         ASTRA_CONTROL_COLLAPSED) == 0u;
    AstraColorRGBA8 background = theme->surface_inset;
    AstraColorRGBA8 ink = theme->client_text;
    int32_t chevron_x = frame->x + theme->control_padding_x + 5;
    int32_t center_y = frame->y + (int32_t)frame->height / 2;
    int32_t text_x = chevron_x + 5 + theme->spacing_unit * 2;
    uint32_t available = text_x < frame->x + (int32_t)frame->width ?
        (uint32_t)((int64_t)frame->x + frame->width - text_x) : 0u;
    uint32_t length;

    if ((state & ASTRA_CONTROL_DISABLED) != 0u) {
        background = theme->control_disabled;
        ink = theme->text_tertiary;
    } else if ((state & ASTRA_CONTROL_PRESSED) != 0u) {
        background = theme->control_pressed;
    } else if ((state & ASTRA_CONTROL_HOVERED) != 0u) {
        background = theme->control_hover;
    }
    if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
        astra_surface_fill_round(
            surface, frame->x - theme->focus_width,
            frame->y - theme->focus_width,
            frame->width + (uint32_t)theme->focus_width * 2u,
            frame->height + (uint32_t)theme->focus_width * 2u,
            (uint16_t)(theme->control_radius + theme->focus_width),
            color(theme->control_focus));
    astra_surface_fill_round(surface, frame->x, frame->y,
                             frame->width, frame->height,
                             theme->control_radius, color(background));
    if (expanded) {
        render_stepper_arrow(surface, chevron_x, center_y, 0, color(ink));
    } else {
        astra_surface_fill(surface, chevron_x - 2, center_y - 4,
                           2u, 2u, color(ink));
        astra_surface_fill(surface, chevron_x, center_y - 2,
                           2u, 2u, color(ink));
        astra_surface_fill(surface, chevron_x + 2, center_y,
                           2u, 2u, color(ink));
        astra_surface_fill(surface, chevron_x, center_y + 2,
                           2u, 2u, color(ink));
        astra_surface_fill(surface, chevron_x - 2, center_y + 4,
                           2u, 2u, color(ink));
    }
    length = astra_surface_ui_text_fit(
        control->_private_text, control->_private_text_length,
        theme->control_font_height, available);
    astra_surface_ui_text(
        surface, text_x,
        frame->y + (int32_t)(frame->height - theme->control_font_height) / 2,
        control->_private_text, length, theme->control_font_height,
        color(ink));
}

static AstraResult render_stepper(const AstraUIContext *context,
                                  uint32_t index, const AstraTheme *theme,
                                  const AstraControl *control,
                                  AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t state = effective_state(control);
    uint32_t half = frame->height / 2u;
    int32_t button_x = frame->x + (int32_t)frame->width -
                       STEPPER_BUTTON_WIDTH;
    int32_t button_center = button_x + STEPPER_BUTTON_WIDTH / 2;
    AstraColorRGBA8 background =
        (state & ASTRA_CONTROL_DISABLED) != 0u ?
            theme->control_disabled : theme->client;
    AstraColorRGBA8 border = (state & ASTRA_CONTROL_ERROR) != 0u ?
        theme->control_error : theme->client_border;
    char text[22];
    uint32_t text_length = stepper_format(context, index, text);
    uint32_t text_width = text_length * theme->mono_cell_width;
    uint16_t up_ink = color(
        (state & ASTRA_CONTROL_DISABLED) != 0u ||
        control->_private_value == control->_private_maximum_value ?
            theme->text_muted : theme->text_secondary);
    uint16_t down_ink = color(
        (state & ASTRA_CONTROL_DISABLED) != 0u ||
        control->_private_value == control->_private_minimum_value ?
            theme->text_muted : theme->text_secondary);

    if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
        astra_surface_fill_round(
            surface, frame->x - theme->focus_width,
            frame->y - theme->focus_width,
            frame->width + (uint32_t)theme->focus_width * 2u,
            frame->height + (uint32_t)theme->focus_width * 2u,
            (uint16_t)(theme->control_radius + theme->focus_width),
            color(theme->control_focus));
    astra_surface_fill_round(surface, frame->x, frame->y,
                             frame->width, frame->height,
                             theme->control_radius, color(border));
    astra_surface_fill_round(
        surface, frame->x + theme->control_border_width,
        frame->y + theme->control_border_width,
        frame->width - (uint32_t)theme->control_border_width * 2u,
        frame->height - (uint32_t)theme->control_border_width * 2u,
        theme->control_radius > theme->control_border_width ?
            (uint16_t)(theme->control_radius -
                       theme->control_border_width) : 0u,
        color(background));
    if (context->_private_hover == index &&
        context->_private_hover_part != STEPPER_PART_VALUE) {
        uint32_t part = context->_private_hover_part;
        int pressed = context->_private_capture == index &&
                      context->_private_capture_part == part &&
                      (state & ASTRA_CONTROL_PRESSED) != 0u;

        astra_surface_fill(
            surface, button_x + theme->control_border_width,
            frame->y + (part == STEPPER_PART_DOWN ? (int32_t)half :
                                                       theme->control_border_width),
            STEPPER_BUTTON_WIDTH - theme->control_border_width * 2u,
            half - theme->control_border_width,
            color(pressed ? theme->control_pressed : theme->control_hover));
    }
    astra_surface_fill(surface, button_x, frame->y, 1u, frame->height,
                       color(theme->border_soft));
    astra_surface_fill(surface, button_x, frame->y + (int32_t)half,
                       STEPPER_BUTTON_WIDTH, 1u,
                       color(theme->border_soft));
    render_stepper_arrow(surface, button_center - 1,
                         frame->y + (int32_t)half / 2, 1, up_ink);
    render_stepper_arrow(surface, button_center - 1,
                         frame->y + (int32_t)half +
                             (int32_t)(frame->height - half) / 2,
                         0, down_ink);
    if (!astra_surface_mono_text_styled(
            surface,
            button_x - (int32_t)theme->spacing_unit * 2 -
                (int32_t)text_width,
            frame->y +
                (int32_t)(frame->height - theme->mono_font_height) / 2,
            text, text_length, theme->mono_font_height,
            theme->mono_cell_width,
            color((state & ASTRA_CONTROL_DISABLED) != 0u ?
                  theme->text_tertiary : theme->client_text), 0u))
        return ASTRA_ERROR_IO;
    return ASTRA_OK;
}

static void render_progress(const AstraTheme *theme,
                            const AstraControl *control,
                            AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t state = effective_state(control);
    uint32_t width;
    uint32_t offset;
    AstraColorRGBA8 fill;

    astra_surface_fill_round(surface, frame->x, frame->y, frame->width,
                             frame->height, frame->height / 2u,
                             color(theme->surface_inset));
    if (control->_private_maximum_value == 0) {
        uint32_t phase = control->_private_animation_phase & 63u;
        uint32_t triangle = phase <= 32u ? phase : 64u - phase;

        width = frame->width / 3u;
        offset = triangle * (frame->width - width) / 32u;
    } else {
        width = (uint32_t)((uint64_t)frame->width *
                           (uint32_t)control->_private_value /
                           (uint32_t)control->_private_maximum_value);
        offset = 0u;
    }
    if ((state & ASTRA_CONTROL_ERROR) != 0u)
        fill = theme->fault;
    else if ((state & ASTRA_CONTROL_DISABLED) != 0u ||
             (control->_private_maximum_value != 0 &&
              control->_private_value == control->_private_maximum_value))
        fill = theme->text_tertiary;
    else
        fill = theme->accent;
    astra_surface_fill_round(surface, frame->x + (int32_t)offset, frame->y,
                             width, frame->height, frame->height / 2u,
                             color(fill));
}

static void render_segmented(const AstraTheme *theme,
                             const AstraControl *control,
                             AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    const AstraChoiceItem *items = choice_items(control);
    uint32_t state = effective_state(control);
    uint32_t border_width = theme->control_border_width;
    uint32_t inner_width = frame->width - border_width * 2u;
    uint32_t inner_height = frame->height - border_width * 2u;
    uint32_t count = control->_private_text_length;

    if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
        astra_surface_fill_round(
            surface, frame->x - theme->focus_width,
            frame->y - theme->focus_width,
            frame->width + (uint32_t)theme->focus_width * 2u,
            frame->height + (uint32_t)theme->focus_width * 2u,
            (uint16_t)(theme->control_radius + theme->focus_width),
            color(theme->control_focus));
    astra_surface_fill_round(
        surface, frame->x, frame->y, frame->width, frame->height,
        theme->control_radius,
        color((state & ASTRA_CONTROL_ERROR) != 0u ?
              theme->control_error : theme->client_border));
    astra_surface_fill_round(
        surface, frame->x + (int32_t)border_width,
        frame->y + (int32_t)border_width, inner_width, inner_height,
        theme->control_radius > border_width ?
            (uint16_t)(theme->control_radius - border_width) : 0u,
        color(theme->surface_inset));
    for (uint32_t at = 0u; at < count; ++at) {
        uint32_t left = (uint32_t)((uint64_t)inner_width * at / count);
        uint32_t right = (uint32_t)((uint64_t)inner_width * (at + 1u) /
                                    count);
        int selected = control->_private_value == (int32_t)at;
        int hot = control->_private_minimum_value == (int32_t)at;
        AstraColorRGBA8 text_color =
            (state & ASTRA_CONTROL_DISABLED) != 0u ?
                theme->text_tertiary : theme->client_text_secondary;
        uint32_t fitted;
        uint32_t text_width;
        int32_t text_x;
        int32_t text_y;

        if ((state & ASTRA_CONTROL_PRESSED) != 0u && hot) {
            astra_surface_fill_round(
                surface, frame->x + (int32_t)border_width + (int32_t)left,
                frame->y + (int32_t)border_width, right - left,
                inner_height,
                theme->control_radius > border_width ?
                    (uint16_t)(theme->control_radius - border_width) : 0u,
                color(theme->control_pressed));
            text_color = theme->text_primary;
        } else if (selected ||
                   ((state & ASTRA_CONTROL_HOVERED) != 0u && hot)) {
            AstraColorRGBA8 fill = selected ? theme->surface_raised :
                                              theme->client;

            if ((state & ASTRA_CONTROL_DISABLED) != 0u)
                fill = theme->control_disabled;
            astra_surface_fill_round(
                surface, frame->x + (int32_t)border_width + (int32_t)left,
                frame->y + (int32_t)border_width, right - left,
                inner_height,
                theme->control_radius > border_width ?
                    (uint16_t)(theme->control_radius - border_width) : 0u,
                color(fill));
            if ((state & ASTRA_CONTROL_DISABLED) == 0u)
                text_color = theme->client_text;
        }
        if (at != 0u)
            astra_surface_fill(
                surface,
                frame->x + (int32_t)border_width + (int32_t)left,
                frame->y + (int32_t)border_width, 1u, inner_height,
                color(theme->client_border));
        fitted = astra_surface_ui_text_fit(
            items[at].text, items[at].text_length,
            theme->control_font_height, right - left);
        text_width = astra_surface_ui_text_width(
            items[at].text, fitted, theme->control_font_height);
        text_x = frame->x + (int32_t)border_width + (int32_t)left +
                 (int32_t)(right - left - text_width) / 2;
        text_y = frame->y +
                 (int32_t)(frame->height - theme->control_font_height) / 2;
        astra_surface_ui_text(surface, text_x, text_y, items[at].text,
                              fitted, theme->control_font_height,
                              color(text_color));
    }
}

static uint32_t tab_item_width(const AstraTheme *theme,
                               const AstraChoiceItem *item)
{
    return astra_surface_ui_text_width(item->text, item->text_length,
                                       theme->control_font_height) +
           theme->spacing_unit * 6u;
}

static uint64_t tab_total_width(const AstraTheme *theme,
                                const AstraControl *control)
{
    const AstraChoiceItem *items = choice_items(control);
    uint64_t total = 0u;

    for (uint32_t at = 0u; at < control->_private_text_length; ++at)
        total += tab_item_width(theme, &items[at]);
    return total;
}

static void tab_item_bounds(uint32_t frame_width, uint64_t total,
                            uint64_t before, uint32_t width,
                            uint32_t *left, uint32_t *right)
{
    if (total > frame_width) {
        *left = (uint32_t)(before * frame_width / total);
        *right = (uint32_t)((before + width) * frame_width / total);
    } else {
        *left = (uint32_t)before;
        *right = *left + width;
    }
}

static void render_tab(const AstraTheme *theme,
                       const AstraControl *control,
                       AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    const AstraChoiceItem *items = choice_items(control);
    uint32_t state = effective_state(control);
    uint64_t total = tab_total_width(theme, control);
    uint64_t before = 0u;
    uint16_t rail = color((state & ASTRA_CONTROL_ERROR) != 0u ?
                          theme->control_error : theme->client_border);

    if ((state & CONTROL_FOCUS_VISIBLE) != 0u) {
        astra_surface_fill_round(surface, frame->x, frame->y,
                                 frame->width, frame->height,
                                 theme->control_radius,
                                 color(theme->control_focus));
        astra_surface_fill_round(
            surface, frame->x + theme->control_border_width,
            frame->y + theme->control_border_width,
            frame->width - (uint32_t)theme->control_border_width * 2u,
            frame->height - (uint32_t)theme->control_border_width * 2u,
            theme->control_radius > theme->control_border_width ?
                (uint16_t)(theme->control_radius -
                           theme->control_border_width) : 0u,
            color(theme->client));
    }
    astra_surface_fill(surface, frame->x,
                       frame->y + (int32_t)frame->height - 1,
                       frame->width, 1u, rail);
    for (uint32_t at = 0u; at < control->_private_text_length; ++at) {
        uint32_t left;
        uint32_t right;
        uint32_t fitted;
        uint32_t text_width;
        uint32_t item_width = tab_item_width(theme, &items[at]);
        AstraColorRGBA8 text_color = theme->client_text_secondary;

        tab_item_bounds(frame->width, total, before, item_width,
                        &left, &right);
        before += item_width;
        if ((state & ASTRA_CONTROL_DISABLED) != 0u)
            text_color = theme->text_tertiary;
        else if (control->_private_value == (int32_t)at ||
                 control->_private_minimum_value == (int32_t)at)
            text_color = theme->client_text;
        fitted = astra_surface_ui_text_fit(
            items[at].text, items[at].text_length,
            theme->control_font_height, right - left);
        text_width = astra_surface_ui_text_width(
            items[at].text, fitted, theme->control_font_height);
        astra_surface_ui_text(
            surface,
            frame->x + (int32_t)left +
                (int32_t)(right - left - text_width) / 2,
            frame->y +
                (int32_t)(frame->height - theme->control_font_height) / 2,
            items[at].text, fitted, theme->control_font_height,
            color(text_color));
        if (control->_private_value == (int32_t)at)
            astra_surface_fill(surface, frame->x + (int32_t)left,
                               frame->y + (int32_t)frame->height - 2,
                               right - left, 2u,
                               color((state & ASTRA_CONTROL_DISABLED) != 0u ?
                                     theme->text_tertiary : theme->accent));
    }
}

static int32_t choice_index_at(const AstraControl *control, int32_t x)
{
    if (control->_private_control_kind == ASTRA_CONTROL_TAB) {
        AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
        const AstraChoiceItem *items = choice_items(control);
        uint64_t total = tab_total_width(&theme, control);
        uint64_t before = 0u;
        uint32_t relative = (uint32_t)((int64_t)x -
                                       control->_private_frame.x);

        for (uint32_t at = 0u; at < control->_private_text_length; ++at) {
            uint32_t left;
            uint32_t right;
            uint32_t item_width = tab_item_width(&theme, &items[at]);

            tab_item_bounds(control->_private_frame.width, total, before,
                            item_width, &left, &right);
            before += item_width;
            if (relative >= left && relative < right)
                return (int32_t)at;
        }
        return -1;
    }
    uint64_t relative = (uint64_t)((int64_t)x -
                                   control->_private_frame.x);
    uint32_t count = control->_private_text_length;
    uint32_t index = (uint32_t)(relative * count /
                                control->_private_frame.width);

    return (int32_t)(index < count ? index : count - 1u);
}

static void choice_hot(AstraUIContext *context, uint32_t index, int32_t x)
{
    AstraControl *control = &context->_private_controls[index];
    int32_t hot = choice_index_at(control, x);

    if (hot == control->_private_minimum_value)
        return;
    control_damage(context, index);
    control->_private_minimum_value = hot;
    control_damage(context, index);
}

static void choice_action(AstraUIContext *context, uint32_t index,
                          int32_t value, AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    int64_t before = control->_private_value;

    if (astra_interface_control_set_value(context, control, value) !=
            ASTRA_OK || control->_private_value == before)
        return;
    action->type = ASTRA_UI_ACTION_VALUE_CHANGED;
    action->control_id = control->_private_id;
    action->value = control->_private_value;
    action->decimal_places = 0u;
}

static void range_action(AstraUIContext *context, uint32_t index,
                         int64_t value, AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    int64_t before = control->_private_value;

    (void)astra_interface_control_set_value(context, control, value);
    if (control->_private_value != before) {
        action->type = ASTRA_UI_ACTION_VALUE_CHANGED;
        action->control_id = control->_private_id;
        action->value = control->_private_value;
        action->decimal_places = control->_private_decimal_places;
    }
}

static AstraResult splitter_pointer(AstraUIContext *context, uint32_t index,
                                    int32_t x, int32_t y,
                                    AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    int32_t pointer = control->_private_style == ASTRA_ORIENTATION_VERTICAL ?
        x : y;
    int64_t position = (int64_t)control->_private_maximum_value + pointer -
                       control->_private_minimum_value;

    return splitter_set_position(context, index, position, action);
}

static void slider_pointer(AstraUIContext *context, uint32_t index,
                           int32_t x, int32_t y, AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t travel;
    int64_t position;
    uint64_t range = range_offset(control->_private_maximum_value,
                                  control->_private_minimum_value);
    int64_t value;

    if (control->_private_style == ASTRA_ORIENTATION_HORIZONTAL) {
        travel = frame->width - 14u;
        position = (int64_t)x - frame->x - 7;
    } else {
        travel = frame->height - 14u;
        position = (int64_t)frame->y + 7 + travel - y;
    }
    if (position < 0) position = 0;
    if ((uint64_t)position > travel) position = travel;
    value = range_value_at(
        control->_private_minimum_value,
        range_offset_at(range, (uint32_t)position, travel));
    range_action(context, index, value, action);
}

static void dial_drag_save(AstraUIContext *context, int32_t pointer,
                           int64_t value)
{
    memcpy(&context->_private_value_input, &value, sizeof(value));
    context->_private_value_input_digits = (uint32_t)pointer;
}

static void dial_pointer(AstraUIContext *context, uint32_t index,
                         const AstraWindowEvent *event,
                         AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    int32_t anchor_y = (int32_t)context->_private_value_input_digits;
    int64_t anchor_value;
    int64_t pixels = (int64_t)anchor_y - event->data.pointer.y;
    int64_t value;
    int64_t snapped;

    memcpy(&anchor_value, &context->_private_value_input,
           sizeof(anchor_value));
    if ((event->data.pointer.modifiers & ASTRA_INPUT_MOD_ALT) != 0u)
        pixels /= 4;
    value = range_move_steps(control, anchor_value, pixels);
    snapped = range_snap(control, value);
    if (control->_private_minimum_value < 0 &&
        control->_private_maximum_value > 0 &&
        range_value_representable(
            0, control->_private_minimum_value,
            control->_private_maximum_value,
            control->_private_value_step) &&
        snapped >= -control->_private_value_step &&
        snapped <= control->_private_value_step)
        value = 0;
    range_action(context, index, value, action);
}

static uint32_t stepper_part(const AstraControl *control, int32_t x,
                             int32_t y)
{
    const AstraControlFrame *frame = &control->_private_frame;
    int64_t button_left = (int64_t)frame->x + frame->width -
                          STEPPER_BUTTON_WIDTH;

    if (x < button_left || x >= (int64_t)frame->x + frame->width ||
        y < frame->y || y >= (int64_t)frame->y + frame->height)
        return STEPPER_PART_VALUE;
    return y < frame->y + (int32_t)frame->height / 2 ?
        STEPPER_PART_UP : STEPPER_PART_DOWN;
}

static void stepper_step(AstraUIContext *context, uint32_t index,
                         uint32_t part, uint32_t count,
                         AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    int64_t steps;

    if (part == STEPPER_PART_UP)
        steps = count;
    else if (part == STEPPER_PART_DOWN)
        steps = -(int64_t)count;
    else
        return;
    range_action(context, index,
                 range_move_steps(control, control->_private_value, steps),
                 action);
}

static void stepper_finish_input(AstraUIContext *context, uint32_t index,
                                 int commit, AstraUIAction *action)
{
    int64_t value;

    if ((context->_private_value_input_flags & VALUE_INPUT_EDITING) == 0u)
        return;
    if (commit && stepper_draft_value(context, &value))
        range_action(context, index, value, action);
    control_damage(context, index);
    context->_private_value_input = 0u;
    context->_private_value_input_digits = 0u;
    context->_private_value_input_flags = 0u;
    control_damage(context, index);
}

static void stepper_text(AstraUIContext *context, uint32_t index,
                         uint32_t scalar, AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    uint32_t digit;
    int64_t value;

    if (scalar == '-' && context->_private_value_input_digits == 0u &&
        control->_private_minimum_value < 0) {
        control_damage(context, index);
        context->_private_value_input = 0u;
        context->_private_value_input_flags =
            VALUE_INPUT_EDITING | VALUE_INPUT_NEGATIVE;
        control_damage(context, index);
        return;
    }
    if (scalar == '+' && context->_private_value_input_digits == 0u) {
        control_damage(context, index);
        context->_private_value_input = 0u;
        context->_private_value_input_flags = VALUE_INPUT_EDITING;
        control_damage(context, index);
        return;
    }
    if (scalar < '0' || scalar > '9') return;
    digit = scalar - '0';
    if ((context->_private_value_input_flags & VALUE_INPUT_EDITING) == 0u) {
        control_damage(context, index);
        context->_private_value_input = 0u;
        context->_private_value_input_digits = 0u;
        context->_private_value_input_flags = VALUE_INPUT_EDITING;
    }
    if (context->_private_value_input == 0u &&
        context->_private_value_input_digits != 0u) {
        context->_private_value_input_digits = 0u;
    }
    {
        uint64_t limit =
            (context->_private_value_input_flags & VALUE_INPUT_NEGATIVE) != 0u ?
                UINT64_C(9223372036854775808) : (uint64_t)INT64_MAX;

        if (context->_private_value_input_digits >= 19u ||
            context->_private_value_input > (limit - digit) / 10u)
            return;
    }
    context->_private_value_input =
        context->_private_value_input * 10u + digit;
    ++context->_private_value_input_digits;
    control_damage(context, index);
    if (stepper_draft_value(context, &value) &&
        value >= control->_private_minimum_value &&
        value <= control->_private_maximum_value &&
        range_snap(control, value) == value)
        range_action(context, index, value, action);
}

static AstraResult field_position(const AstraControl *control, int32_t x,
                                  uint32_t *position)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    FieldView view;
    int64_t frame_right = (int64_t)control->_private_frame.x +
                          control->_private_frame.width;
    int64_t relative;
    uint32_t columns;

    if (position == NULL || !field_view(control, &view))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (x < control->_private_frame.x) {
        *position = 0u;
        return ASTRA_OK;
    }
    if ((int64_t)x >= frame_right) {
        *position = view.state.text_bytes;
        return ASTRA_OK;
    }
    relative = (int64_t)x - control->_private_frame.x -
               theme.control_padding_x;
    columns = relative <= 0 ? 0u :
        (uint32_t)((relative + theme.mono_cell_width / 2u) /
                   theme.mono_cell_width);
    if (columns > view.columns) columns = view.columns;
    if (!model_advance_scalars(const_field_model(control), view.start,
                               columns, position))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_OK;
}

static AstraResult field_selection(AstraUIContext *context, uint32_t index,
                                   uint32_t anchor, uint32_t focus,
                                   AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraTextSelection selection = ASTRA_TEXT_SELECTION_INIT;
    AstraResult result;

    if (astra_text_model_get_state(field_model(control), &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (state.selection.anchor == anchor && state.selection.focus == focus)
        return ASTRA_OK;
    selection.anchor = anchor;
    selection.focus = focus;
    result = astra_text_model_set_selection(field_model(control), &selection);
    if (result != ASTRA_OK)
        return result;
    if (astra_text_model_get_state(field_model(control), &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    private_u32_set(&control->_private_maximum_value, state.generation);
    control->_private_animation_phase = 0u;
    control_damage(context, index);
    action->type = ASTRA_UI_ACTION_SELECTION_CHANGED;
    action->control_id = control->_private_id;
    return ASTRA_OK;
}

static AstraResult field_replace_selection_internal(
    AstraUIContext *context, uint32_t index, const char *replacement,
    uint32_t replacement_bytes, AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    uint32_t start;
    uint32_t end;
    AstraResult result;

    if ((control->_private_style & ASTRA_FIELD_READ_ONLY) != 0u)
        return ASTRA_OK;
    if (astra_text_model_get_state(field_model(control), &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    start = state.selection.anchor < state.selection.focus ?
        state.selection.anchor : state.selection.focus;
    end = state.selection.anchor > state.selection.focus ?
        state.selection.anchor : state.selection.focus;
    result = astra_text_model_replace(field_model(control), start, end,
                                      replacement, replacement_bytes);
    if (result != ASTRA_OK)
        return result;
    if (astra_text_model_get_state(field_model(control), &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    private_u32_set(&control->_private_maximum_value, state.generation);
    control->_private_animation_phase = 0u;
    control_damage(context, index);
    action->type = ASTRA_UI_ACTION_TEXT_CHANGED;
    action->control_id = control->_private_id;
    return ASTRA_OK;
}

AstraResult astra_interface_field_replace_selection(
    AstraUIContext *context, AstraControl *control,
    const char *replacement, uint32_t replacement_bytes)
{
    uint32_t index = control_index(context, control);
    AstraUIAction ignored = ASTRA_UI_ACTION_INIT;

    if (index == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_FIELD ||
        !field_text_valid(replacement, replacement_bytes))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if ((control->_private_style & ASTRA_FIELD_READ_ONLY) != 0u)
        return ASTRA_ERROR_PERMISSION;
    return field_replace_selection_internal(
        context, index, replacement, replacement_bytes, &ignored);
}

static AstraResult field_delete(AstraUIContext *context, uint32_t index,
                                int backwards, AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraTextSelection selection = ASTRA_TEXT_SELECTION_INIT;
    uint32_t start;
    uint32_t end;
    AstraResult result;

    if ((control->_private_style & ASTRA_FIELD_READ_ONLY) != 0u)
        return ASTRA_OK;
    if (astra_text_model_get_state(field_model(control), &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    start = state.selection.anchor < state.selection.focus ?
        state.selection.anchor : state.selection.focus;
    end = state.selection.anchor > state.selection.focus ?
        state.selection.anchor : state.selection.focus;
    if (start == end) {
        if (backwards) {
            result = astra_text_model_scalar_retreat(field_model(control),
                                                     &start);
        } else {
            result = astra_text_model_scalar_advance(field_model(control),
                                                     &end);
        }
        if (result == ASTRA_ERROR_NOT_PRESENT)
            return ASTRA_OK;
        if (result != ASTRA_OK)
            return result;
        selection.anchor = start;
        selection.focus = end;
        result = astra_text_model_set_selection(field_model(control),
                                                &selection);
        if (result != ASTRA_OK)
            return result;
    }
    return field_replace_selection_internal(
        context, index, NULL, 0u, action);
}

static AstraResult field_key(AstraUIContext *context, uint32_t index,
                             const AstraWindowEvent *event,
                             AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    uint32_t usage = event->data.key.usage;
    uint32_t modifiers = event->data.key.modifiers;
    uint32_t anchor;
    uint32_t focus;
    int shift = (modifiers & ASTRA_INPUT_MOD_SHIFT) != 0u;
    AstraResult result;

    if ((event->flags & ASTRA_WINDOW_EVENT_DOWN) == 0u)
        return ASTRA_OK;
    if (astra_text_model_get_state(field_model(control), &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if ((modifiers & ASTRA_INPUT_MOD_META) != 0u &&
        (modifiers & ASTRA_INPUT_MOD_CTRL) == 0u) {
        if (usage == 0x04u)
            return field_selection(context, index, 0u, state.text_bytes,
                                   action);
        if (usage == 0x06u &&
            state.selection.anchor != state.selection.focus) {
            action->type = ASTRA_UI_ACTION_COPY;
            action->control_id = control->_private_id;
        } else if (usage == 0x1bu &&
                   state.selection.anchor != state.selection.focus &&
                   (control->_private_style & ASTRA_FIELD_READ_ONLY) == 0u) {
            action->type = ASTRA_UI_ACTION_CUT;
            action->control_id = control->_private_id;
        } else if (usage == 0x19u &&
                   (control->_private_style & ASTRA_FIELD_READ_ONLY) == 0u) {
            action->type = ASTRA_UI_ACTION_PASTE;
            action->control_id = control->_private_id;
        }
        return ASTRA_OK;
    }
    if (usage == 0x28u || usage == 0x58u) {
        if ((event->flags & ASTRA_WINDOW_EVENT_REPEAT) == 0u) {
            action->type = ASTRA_UI_ACTION_ACTIVATE;
            action->control_id = control->_private_id;
        }
        return ASTRA_OK;
    }
    if (usage == 0x2au)
        return field_delete(context, index, 1, action);
    if (usage == 0x4cu)
        return field_delete(context, index, 0, action);
    anchor = state.selection.anchor;
    focus = state.selection.focus;
    if (usage == 0x4au)
        focus = 0u;
    else if (usage == 0x4du)
        focus = state.text_bytes;
    else if (usage == 0x50u) {
        if (!shift && anchor != focus)
            focus = anchor < focus ? anchor : focus;
        else {
            result = astra_text_model_scalar_retreat(field_model(control),
                                                     &focus);
            if (result != ASTRA_OK && result != ASTRA_ERROR_NOT_PRESENT)
                return result;
        }
    } else if (usage == 0x4fu) {
        if (!shift && anchor != focus)
            focus = anchor > focus ? anchor : focus;
        else {
            result = astra_text_model_scalar_advance(field_model(control),
                                                     &focus);
            if (result != ASTRA_OK && result != ASTRA_ERROR_NOT_PRESENT)
                return result;
        }
    } else {
        return ASTRA_OK;
    }
    return field_selection(context, index, shift ? anchor : focus,
                           focus, action);
}

static AstraResult field_text(AstraUIContext *context, uint32_t index,
                              const AstraWindowEvent *event,
                              AstraUIAction *action)
{
    uint32_t scalar = event->data.text.codepoint;
    char encoded[4];
    uint32_t bytes;

    if (!astra_unicode_scalar_valid(scalar))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (scalar < 0x20u || (scalar >= 0x7fu && scalar <= 0x9fu) ||
        scalar == 0x2028u || scalar == 0x2029u)
        return ASTRA_OK;
    bytes = astra_utf8_encode(scalar, encoded);
    return field_replace_selection_internal(
        context, index, encoded, bytes, action);
}

typedef struct ScrollCopy {
    uint32_t view;
    AstraControlFrame viewport;
    AstraControlFrame exposed[2];
    uint32_t exposed_count;
    uint32_t source_x;
    uint32_t source_y;
    uint32_t destination_x;
    uint32_t destination_y;
    uint32_t width;
    uint32_t height;
} ScrollCopy;

static int scroll_copy_prepare(const AstraUIContext *context,
                               const AstraSurfaceView *surface,
                               ScrollCopy *copy)
{
    const AstraDrawListHeader *header;
    const AstraControl *view;
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
    uint32_t encoded = context->_private_reserved &
                       SCROLL_DAMAGE_INDEX_MASK;
    uint32_t old_x;
    uint32_t old_y;
    int64_t dx;
    int64_t dy;
    uint32_t shift_x;
    uint32_t shift_y;

    *copy = (ScrollCopy){0};
    if (surface->kind != ASTRA_SURFACE_VIEW_DRAW_LIST || encoded == 0u ||
        context->_private_reserved == SCROLL_DAMAGE_INVALID ||
        (context->_private_reserved & SCROLL_DAMAGE_RECORDING) != 0u)
        return 0;
    header = (const AstraDrawListHeader *)(const void *)surface->pixels;
    if (header->command_count != 0u)
        return 0;
    copy->view = encoded - 1u;
    if (copy->view >= context->_private_control_count)
        return 0;
    view = &context->_private_controls[copy->view];
    if (view->_private_control_kind != ASTRA_CONTROL_SCROLL_VIEW ||
        view->_private_laid_out == 0u || view->_private_value_step == 0 ||
        astra_scroll_get_state(const_scroll_model(view), &state) != ASTRA_OK)
        return 0;
    old_x = private_u32(&view->_private_minimum_value);
    old_y = private_u32(&view->_private_maximum_value);
    dx = (int64_t)state.offset_x - old_x;
    dy = (int64_t)state.offset_y - old_y;
    if (dx == 0 && dy == 0)
        return 0;
    copy->viewport = frame_intersection(&view->_private_frame,
                                        &view->_private_clip);
    shift_x = dx < 0 ? (uint32_t)-dx : (uint32_t)dx;
    shift_y = dy < 0 ? (uint32_t)-dy : (uint32_t)dy;
    if (shift_x >= copy->viewport.width ||
        shift_y >= copy->viewport.height)
        return 0;
    copy->source_x = (uint32_t)copy->viewport.x + (dx > 0 ? shift_x : 0u);
    copy->source_y = (uint32_t)copy->viewport.y + (dy > 0 ? shift_y : 0u);
    copy->destination_x = (uint32_t)copy->viewport.x +
                          (dx < 0 ? shift_x : 0u);
    copy->destination_y = (uint32_t)copy->viewport.y +
                          (dy < 0 ? shift_y : 0u);
    copy->width = copy->viewport.width - shift_x;
    copy->height = copy->viewport.height - shift_y;
    if (shift_x != 0u) {
        copy->exposed[copy->exposed_count++] = (AstraControlFrame){
            copy->viewport.x + (dx > 0 ?
                (int32_t)(copy->viewport.width - shift_x) : 0),
            copy->viewport.y, shift_x, copy->viewport.height};
    }
    if (shift_y != 0u) {
        copy->exposed[copy->exposed_count++] = (AstraControlFrame){
            copy->viewport.x,
            copy->viewport.y + (dy > 0 ?
                (int32_t)(copy->viewport.height - shift_y) : 0),
            copy->viewport.width, shift_y};
    }
    return 1;
}

static void render_splitter(const AstraTheme *theme,
                            const AstraControl *control,
                            AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t state = effective_state(control);
    uint32_t thickness =
        (state & (ASTRA_CONTROL_PRESSED | CONTROL_FOCUS_VISIBLE)) != 0u ?
            2u : 1u;
    AstraColorRGBA8 line = theme->client_border;

    if ((state & ASTRA_CONTROL_DISABLED) != 0u)
        line = theme->control_disabled;
    else if ((state & (ASTRA_CONTROL_PRESSED |
                       CONTROL_FOCUS_VISIBLE)) != 0u)
        line = theme->accent;
    else if ((state & ASTRA_CONTROL_HOVERED) != 0u)
        line = theme->border_emphasis;
    if (control->_private_style == ASTRA_ORIENTATION_VERTICAL)
        astra_surface_fill(surface,
                           frame->x + (int32_t)(frame->width - thickness) / 2,
                           frame->y, thickness, frame->height, color(line));
    else
        astra_surface_fill(surface, frame->x,
                           frame->y +
                               (int32_t)(frame->height - thickness) / 2,
                           frame->width, thickness, color(line));
}

static AstraResult render_one_control(const AstraUIContext *context,
                                      uint32_t index,
                                      const AstraTheme *theme,
                                      const AstraControl *control,
                                      AstraSurfaceView *surface)
{
    if (control->_private_control_kind == ASTRA_CONTROL_FIELD)
        return render_field(context, theme, control, surface);
    if (control->_private_control_kind == ASTRA_CONTROL_LABEL)
        render_label(theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_BUTTON)
        render_button(theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_DISCLOSURE)
        render_disclosure(context, theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_SLIDER)
        render_slider(theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_DIAL)
        render_dial(theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_SCROLLBAR)
        render_scrollbar(theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_SPLITTER)
        render_splitter(theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_STEPPER)
        return render_stepper(context, index, theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_PROGRESS)
        render_progress(theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_SEGMENTED)
        render_segmented(theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_TAB)
        render_tab(theme, control, surface);
    else
        render_toggle(theme, control, surface);
    return ASTRA_OK;
}

AstraResult astra_interface_ui_render(AstraUIContext *context,
                                      AstraSurfaceView *surface)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraSurfaceView validated;
    uint64_t commands;
    uint64_t payload;
    ScrollCopy copy;
    int accelerated;

    if (!context_valid(context) || surface == NULL || surface->pixels == NULL ||
        surface->width != context->_private_width ||
        surface->height != context->_private_height ||
        surface->clip_left > surface->clip_right ||
        surface->clip_right > surface->width ||
        surface->clip_top > surface->clip_bottom ||
        surface->clip_bottom > surface->height ||
        (surface->kind != ASTRA_SURFACE_VIEW_RGB565 &&
         surface->kind != ASTRA_SURFACE_VIEW_DRAW_LIST) ||
        !render_requirements(context, &commands, &payload))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    accelerated = scroll_copy_prepare(context, surface, &copy);
    if (surface->kind == ASTRA_SURFACE_VIEW_DRAW_LIST) {
        if (!draw_list_has_room(surface,
                                commands + (accelerated ?
                                    1u + copy.exposed_count : 0u), payload))
            return ASTRA_ERROR_NO_RESOURCES;
    } else if (!astra_surface_view_init(
                   &validated, surface->pixels, surface->byte_size,
                   surface->width, surface->height, surface->pitch)) {
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    validated = *surface;
    if (accelerated) {
        AstraTheme background = ASTRA_THEME_SYSTEM_INIT;

        if (!astra_draw_list_copy(&validated, copy.source_x, copy.source_y,
                                  copy.destination_x, copy.destination_y,
                                  copy.width, copy.height))
            return ASTRA_ERROR_NO_RESOURCES;
        for (uint32_t at = 0u; at < copy.exposed_count; ++at)
            astra_surface_fill(&validated, copy.exposed[at].x,
                               copy.exposed[at].y,
                               copy.exposed[at].width,
                               copy.exposed[at].height,
                               color(background.client));
    }
    for (uint32_t at = 0u; at < context->_private_control_count; ++at) {
        const AstraControl *control = &context->_private_controls[at];
        AstraSurfaceView clipped;
        AstraResult result;

        if (control->_private_laid_out == 0u ||
            CONTAINER_KINDS(control->_private_control_kind) ||
            control->_private_clip.width == 0u ||
            control->_private_clip.height == 0u)
            continue;
        if (accelerated &&
            control->_private_control_kind == ASTRA_CONTROL_SCROLLBAR &&
            const_scroll_model(control) ==
                const_scroll_model(&context->_private_controls[copy.view])) {
            clipped = validated;
            if (!astra_surface_clip(&clipped, control->_private_clip.x,
                                    control->_private_clip.y,
                                    control->_private_clip.width,
                                    control->_private_clip.height))
                return ASTRA_ERROR_INVALID_ARGUMENT;
            result = render_one_control(context, at, &theme, control,
                                        &clipped);
            if (result != ASTRA_OK) return result;
            continue;
        }
        if (accelerated) {
            if (!control_descends_from(context, at, copy.view))
                continue;
            for (uint32_t strip = 0u; strip < copy.exposed_count; ++strip) {
                AstraControlFrame visible = frame_intersection(
                    &control->_private_clip, &copy.exposed[strip]);

                if (visible.width == 0u || visible.height == 0u)
                    continue;
                clipped = validated;
                if (!astra_surface_clip(&clipped, visible.x, visible.y,
                                        visible.width, visible.height))
                    return ASTRA_ERROR_INVALID_ARGUMENT;
                result = render_one_control(context, at, &theme, control,
                                            &clipped);
                if (result != ASTRA_OK) return result;
            }
            continue;
        }
        clipped = validated;
        if (!astra_surface_clip(&clipped, control->_private_clip.x,
                                control->_private_clip.y,
                                control->_private_clip.width,
                                control->_private_clip.height))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        result = render_one_control(context, at, &theme, control, &clipped);
        if (result != ASTRA_OK) return result;
    }
    for (uint32_t at = 0u; at < context->_private_control_count; ++at) {
        AstraControl *control = &context->_private_controls[at];
        AstraScrollState state = ASTRA_SCROLL_STATE_INIT;

        if (control->_private_control_kind != ASTRA_CONTROL_SCROLL_VIEW ||
            astra_scroll_get_state(scroll_model(control), &state) != ASTRA_OK)
            continue;
        private_u32_set(&control->_private_minimum_value, state.offset_x);
        private_u32_set(&control->_private_maximum_value, state.offset_y);
        control->_private_value_step = 1;
    }
    context->_private_reserved = 0u;
    return ASTRA_OK;
}

static AstraResult pointer_event(AstraUIContext *context,
                                 const AstraWindowEvent *event,
                                 AstraUIAction *action)
{
    uint32_t hit = hit_test(context, event->data.pointer.x,
                            event->data.pointer.y);

    set_hover(context, hit);
    {
        uint32_t part = STEPPER_PART_VALUE;

        if (hit != CONTROL_NONE &&
            context->_private_controls[hit]._private_control_kind ==
                ASTRA_CONTROL_STEPPER)
            part = stepper_part(&context->_private_controls[hit],
                                event->data.pointer.x,
                                event->data.pointer.y);
        else if (hit != CONTROL_NONE &&
                 context->_private_controls[hit]._private_control_kind ==
                     ASTRA_CONTROL_SCROLLBAR)
            part = scrollbar_part(&context->_private_controls[hit],
                                  event->data.pointer.x,
                                  event->data.pointer.y);
        if (context->_private_hover_part != part) {
            control_damage(context, hit);
            context->_private_hover_part = part;
            control_damage(context, hit);
        }
    }
    if (hit != CONTROL_NONE &&
        CHOICE_KINDS(context->_private_controls[hit]
                         ._private_control_kind))
        choice_hot(context, hit, event->data.pointer.x);
    if (event->type == ASTRA_WINDOW_EVENT_POINTER_MOTION) {
        if (context->_private_capture != CONTROL_NONE) {
            uint32_t captured = context->_private_capture;
            AstraControl *captured_control =
                &context->_private_controls[captured];
            int stepper = captured_control->_private_control_kind ==
                          ASTRA_CONTROL_STEPPER;
            int scrollbar = captured_control->_private_control_kind ==
                            ASTRA_CONTROL_SCROLLBAR;
            int splitter = captured_control->_private_control_kind ==
                           ASTRA_CONTROL_SPLITTER;
            int dial = captured_control->_private_control_kind ==
                       ASTRA_CONTROL_DIAL;
            int pressed = scrollbar || splitter || dial || hit == captured;

            if (stepper)
                pressed = pressed &&
                          context->_private_hover_part ==
                              context->_private_capture_part &&
                          context->_private_capture_part !=
                              STEPPER_PART_VALUE;
            dynamic_state(context, captured, ASTRA_CONTROL_PRESSED, pressed);
            if (stepper) animation_sync(context, captured);
            if (captured_control->_private_control_kind ==
                    ASTRA_CONTROL_SLIDER)
                slider_pointer(context, context->_private_capture,
                               event->data.pointer.x,
                               event->data.pointer.y, action);
            else if (dial)
                dial_pointer(context, context->_private_capture,
                             event, action);
            else if (scrollbar) {
                AstraResult result = scrollbar_pointer(
                    context, context->_private_capture, event, action);

                if (result != ASTRA_OK) return result;
            }
            else if (splitter) {
                AstraResult result = splitter_pointer(
                    context, context->_private_capture,
                    event->data.pointer.x, event->data.pointer.y, action);

                if (result != ASTRA_OK) return result;
            }
            else if (captured_control->_private_control_kind ==
                         ASTRA_CONTROL_FIELD) {
                AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
                uint32_t position;
                AstraResult result = field_position(
                    &context->_private_controls[context->_private_capture],
                    event->data.pointer.x, &position);

                if (result != ASTRA_OK)
                    return result;
                if (astra_text_model_get_state(
                        field_model(&context->_private_controls[
                            context->_private_capture]), &state) != ASTRA_OK)
                    return ASTRA_ERROR_INVALID_ARGUMENT;
                result = field_selection(
                    context, context->_private_capture,
                    state.selection.anchor, position, action);
                if (result != ASTRA_OK)
                    return result;
            }
        }
        return ASTRA_OK;
    }
    if (event->data.pointer.button != ASTRA_INPUT_BUTTON_LEFT)
        return ASTRA_OK;
    if ((event->flags & ASTRA_WINDOW_EVENT_DOWN) != 0u) {
        clear_keyboard(context);
        clear_capture(context);
        if (hit == CONTROL_NONE || focusable(&context->_private_controls[hit]))
            set_focus(context, hit, 0);
        context->_private_capture = hit;
        context->_private_capture_part = STEPPER_PART_VALUE;
        if (hit != CONTROL_NONE &&
            context->_private_controls[hit]._private_control_kind ==
                ASTRA_CONTROL_STEPPER)
            context->_private_capture_part = stepper_part(
                &context->_private_controls[hit], event->data.pointer.x,
                event->data.pointer.y);
        else if (hit != CONTROL_NONE &&
                 context->_private_controls[hit]._private_control_kind ==
                     ASTRA_CONTROL_SCROLLBAR)
            context->_private_capture_part = scrollbar_part(
                &context->_private_controls[hit], event->data.pointer.x,
                event->data.pointer.y);
        dynamic_state(context, hit, ASTRA_CONTROL_PRESSED,
                      hit == CONTROL_NONE ||
                      context->_private_controls[hit]._private_control_kind !=
                          ASTRA_CONTROL_STEPPER ||
                      context->_private_capture_part != STEPPER_PART_VALUE);
        if (hit != CONTROL_NONE &&
            context->_private_controls[hit]._private_control_kind ==
                ASTRA_CONTROL_SPLITTER) {
            AstraControl *splitter = &context->_private_controls[hit];

            splitter->_private_minimum_value =
                splitter->_private_style == ASTRA_ORIENTATION_VERTICAL ?
                    event->data.pointer.x : event->data.pointer.y;
            splitter->_private_maximum_value = splitter->_private_value;
        }
        if (hit != CONTROL_NONE &&
            context->_private_controls[hit]._private_control_kind ==
                ASTRA_CONTROL_DIAL) {
            AstraControl *dial = &context->_private_controls[hit];

            if (event->data.pointer.click_count >= 2u)
                range_action(context, hit, dial->_private_reset_value,
                             action);
            dial_drag_save(context, event->data.pointer.y,
                           dial->_private_value);
        }
        if (hit != CONTROL_NONE &&
            context->_private_controls[hit]._private_control_kind ==
                ASTRA_CONTROL_SLIDER)
            slider_pointer(context, hit, event->data.pointer.x,
                           event->data.pointer.y, action);
        else if (hit != CONTROL_NONE &&
                 context->_private_controls[hit]._private_control_kind ==
                     ASTRA_CONTROL_SCROLLBAR) {
            AstraResult result = scrollbar_pointer(
                context, hit, event, action);

            if (result != ASTRA_OK) return result;
        }
        else if (hit != CONTROL_NONE &&
                 context->_private_controls[hit]._private_control_kind ==
                     ASTRA_CONTROL_FIELD) {
            uint32_t position;
            AstraResult result = field_position(
                &context->_private_controls[hit], event->data.pointer.x,
                &position);

            if (result != ASTRA_OK)
                return result;
            result = field_selection(context, hit, position, position,
                                     action);
            if (result != ASTRA_OK)
                return result;
        } else if (hit != CONTROL_NONE &&
                   context->_private_controls[hit]._private_control_kind ==
                       ASTRA_CONTROL_STEPPER) {
            stepper_finish_input(context, hit, 0, action);
            stepper_step(context, hit, context->_private_capture_part, 1u,
                         action);
            animation_sync(context, hit);
        }
        return ASTRA_OK;
    }
    if (context->_private_capture != CONTROL_NONE) {
        uint32_t captured = context->_private_capture;
        int slider = context->_private_controls[captured]
                         ._private_control_kind == ASTRA_CONTROL_SLIDER;
        int choice = CHOICE_KINDS(context->_private_controls[captured]
                                     ._private_control_kind);
        int stepper = context->_private_controls[captured]
                          ._private_control_kind == ASTRA_CONTROL_STEPPER;
        int scrollbar = context->_private_controls[captured]
                            ._private_control_kind == ASTRA_CONTROL_SCROLLBAR;
        int splitter = context->_private_controls[captured]
                           ._private_control_kind == ASTRA_CONTROL_SPLITTER;
        int dial = context->_private_controls[captured]
                       ._private_control_kind == ASTRA_CONTROL_DIAL;

        if (slider)
            slider_pointer(context, captured, event->data.pointer.x,
                           event->data.pointer.y, action);
        else if (choice && hit == captured)
            choice_action(
                context, captured,
                context->_private_controls[captured]._private_minimum_value,
                action);
        else if (splitter) {
            AstraResult result = splitter_pointer(
                context, captured, event->data.pointer.x,
                event->data.pointer.y, action);

            if (result != ASTRA_OK) return result;
        }

        clear_capture(context);
        if (!slider && !dial && !choice && !stepper && !scrollbar && !splitter &&
            hit == captured &&
            context->_private_controls[captured]._private_control_kind !=
                ASTRA_CONTROL_FIELD &&
            (context->_private_controls[captured]._private_state &
             ASTRA_CONTROL_DISABLED) == 0u)
            return activate_control(context, captured, action);
    }
    return ASTRA_OK;
}

static int point_in_control(const AstraControl *control, int32_t x, int32_t y)
{
    const AstraControlFrame *frame = &control->_private_frame;
    const AstraControlFrame *clip = &control->_private_clip;

    return control->_private_laid_out != 0u &&
           x >= frame->x && (int64_t)x < (int64_t)frame->x + frame->width &&
           y >= frame->y && (int64_t)y < (int64_t)frame->y + frame->height &&
           x >= clip->x && (int64_t)x < (int64_t)clip->x + clip->width &&
           y >= clip->y && (int64_t)y < (int64_t)clip->y + clip->height;
}

static uint32_t scroll_delta(uint32_t offset, uint32_t maximum,
                             int32_t notches, uint32_t step)
{
    int64_t value = (int64_t)offset - (int64_t)notches * step;

    if (value < 0) return 0u;
    if ((uint64_t)value > maximum) return maximum;
    return (uint32_t)value;
}

static AstraResult wheel_event(AstraUIContext *context,
                               const AstraWindowEvent *event,
                               AstraUIAction *action)
{
    for (uint32_t count = context->_private_control_count; count != 0u;
         --count) {
        uint32_t at = count - 1u;
        AstraControl *view = &context->_private_controls[at];
        AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
        uint32_t x;
        uint32_t y;
        AstraResult result;

        if (!point_in_control(view, event->data.wheel.x,
                              event->data.wheel.y) ||
            (view->_private_control_kind != ASTRA_CONTROL_SCROLL_VIEW &&
             view->_private_control_kind != ASTRA_CONTROL_SCROLLBAR) ||
            astra_scroll_get_state(
                scroll_model(view), &state) != ASTRA_OK)
            continue;
        x = scroll_delta(state.offset_x, state.maximum_x,
                         event->data.wheel.delta_x, state.line_width);
        y = scroll_delta(state.offset_y, state.maximum_y,
                         event->data.wheel.delta_y, state.line_height);
        if (x == state.offset_x && y == state.offset_y)
            continue;
        result = scroll_apply(context, scroll_model(view), x, y,
                              view->_private_id, action);
        return result;
    }
    return ASTRA_OK;
}

static AstraResult scroll_key(AstraUIContext *context,
                              const AstraWindowEvent *event,
                              AstraUIAction *action)
{
    uint32_t at;

    if ((event->flags & ASTRA_WINDOW_EVENT_DOWN) == 0u ||
        context->_private_focus == CONTROL_NONE)
        return ASTRA_OK;
    at = context->_private_controls[context->_private_focus]._private_parent;
    while (at != CONTROL_NONE) {
        AstraControl *view = &context->_private_controls[at];

        if (view->_private_control_kind == ASTRA_CONTROL_SCROLL_VIEW) {
            AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
            uint32_t x;
            uint32_t y;
            int handled = 1;

            if (astra_scroll_get_state(
                    scroll_model(view), &state) != ASTRA_OK)
                return ASTRA_ERROR_INVALID_ARGUMENT;
            x = state.offset_x;
            y = state.offset_y;
            if (event->data.key.usage == 0x50u)
                x = scroll_delta(x, state.maximum_x, 1,
                                 state.line_width);
            else if (event->data.key.usage == 0x4fu)
                x = scroll_delta(x, state.maximum_x, -1,
                                 state.line_width);
            else if (event->data.key.usage == 0x52u)
                y = scroll_delta(y, state.maximum_y, 1,
                                 state.line_height);
            else if (event->data.key.usage == 0x51u)
                y = scroll_delta(y, state.maximum_y, -1,
                                 state.line_height);
            else if (event->data.key.usage == 0x4bu)
                y = state.offset_y > state.viewport_height ?
                    state.offset_y - state.viewport_height : 0u;
            else if (event->data.key.usage == 0x4eu)
                y = state.maximum_y - state.offset_y < state.viewport_height ?
                    state.maximum_y : state.offset_y + state.viewport_height;
            else if (event->data.key.usage == 0x4au) {
                if (state.maximum_y != 0u) y = 0u;
                else x = 0u;
            } else if (event->data.key.usage == 0x4du) {
                if (state.maximum_y != 0u) y = state.maximum_y;
                else x = state.maximum_x;
            } else {
                handled = 0;
            }
            if (handled && (x != state.offset_x || y != state.offset_y))
                return scroll_apply(context, scroll_model(view), x, y,
                                    view->_private_id, action);
            if (handled)
                return ASTRA_OK;
        }
        at = view->_private_parent;
    }
    return ASTRA_OK;
}

static int activation_key(uint32_t usage)
{
    return usage == 0x28u || usage == 0x2cu || usage == 0x58u;
}

static AstraResult disclosure_set(AstraUIContext *context, uint32_t index,
                                  int expanded, AstraUIAction *action)
{
    AstraControl *disclosure = &context->_private_controls[index];
    uint32_t target_index = control_id_index(
        context, disclosure->_private_style);
    AstraControl *target;
    uint32_t state;
    AstraResult result;

    if (target_index == CONTROL_NONE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    target = &context->_private_controls[target_index];
    state = target->_private_state;
    if (((state & ASTRA_CONTROL_COLLAPSED) == 0u) == (expanded != 0))
        return ASTRA_OK;
    if (expanded)
        state &= ~ASTRA_CONTROL_COLLAPSED;
    else
        state |= ASTRA_CONTROL_COLLAPSED;
    result = astra_interface_ui_set_state(context, target, state);
    if (result != ASTRA_OK)
        return result;
    action->type = ASTRA_UI_ACTION_VALUE_CHANGED;
    action->control_id = disclosure->_private_id;
    action->value = expanded != 0;
    action->decimal_places = 0u;
    return ASTRA_OK;
}

static AstraResult activate_control(AstraUIContext *context, uint32_t index,
                                    AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];

    if (control->_private_control_kind == ASTRA_CONTROL_SLIDER ||
        control->_private_control_kind == ASTRA_CONTROL_DIAL ||
        control->_private_control_kind == ASTRA_CONTROL_STEPPER ||
        control->_private_control_kind == ASTRA_CONTROL_SPLITTER ||
        CHOICE_KINDS(control->_private_control_kind))
        return ASTRA_OK;
    if (control->_private_control_kind == ASTRA_CONTROL_DISCLOSURE) {
        uint32_t target = control_id_index(context, control->_private_style);

        if (target == CONTROL_NONE)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        return disclosure_set(
            context, index,
            (context->_private_controls[target]._private_state &
             ASTRA_CONTROL_COLLAPSED) != 0u,
            action);
    }
    action->control_id = control->_private_id;
    if (!TOGGLE_KINDS(control->_private_control_kind)) {
        action->type = ASTRA_UI_ACTION_ACTIVATE;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_RADIO) {
        AstraResult result;

        if ((control->_private_state & ASTRA_CONTROL_SELECTED) != 0u)
            return ASTRA_OK;
        result = astra_interface_ui_set_state(
            context, control,
            control->_private_state | ASTRA_CONTROL_SELECTED);
        if (result != ASTRA_OK)
            return result;
    } else {
        control_damage(context, index);
        control->_private_state ^= ASTRA_CONTROL_SELECTED;
        control_damage(context, index);
    }
    action->type = ASTRA_UI_ACTION_VALUE_CHANGED;
    action->value = (control->_private_state & ASTRA_CONTROL_SELECTED) != 0u;
    action->decimal_places = 0u;
    return ASTRA_OK;
}

static AstraResult key_event(AstraUIContext *context,
                             const AstraWindowEvent *event,
                             AstraUIAction *action)
{
    int down = (event->flags & ASTRA_WINDOW_EVENT_DOWN) != 0u;

    if (event->data.key.usage == 0x2bu) {
        if (down && (event->flags & ASTRA_WINDOW_EVENT_REPEAT) == 0u)
            set_focus(context, next_focus(
                context,
                (event->data.key.modifiers & ASTRA_INPUT_MOD_SHIFT) != 0u),
                1);
        return ASTRA_OK;
    }
    if (down && context->_private_focus != CONTROL_NONE)
        dynamic_state(context, context->_private_focus,
                      CONTROL_FOCUS_VISIBLE, 1);
    if (context->_private_focus != CONTROL_NONE &&
        context->_private_controls[context->_private_focus]
                ._private_control_kind == ASTRA_CONTROL_FIELD)
        return field_key(context, context->_private_focus, event, action);
    if (down && context->_private_focus != CONTROL_NONE &&
        context->_private_controls[context->_private_focus]
                ._private_control_kind == ASTRA_CONTROL_DISCLOSURE &&
        (event->data.key.usage == 0x50u ||
         event->data.key.usage == 0x4fu))
        return disclosure_set(context, context->_private_focus,
                              event->data.key.usage == 0x4fu, action);
    if (down && context->_private_focus != CONTROL_NONE &&
        CHOICE_KINDS(context->_private_controls[context->_private_focus]
                         ._private_control_kind)) {
        AstraControl *control =
            &context->_private_controls[context->_private_focus];
        int32_t value = control->_private_value;
        int handled = 1;

        if (event->data.key.usage == 0x50u ||
            event->data.key.usage == 0x52u) {
            if (value != 0) --value;
        } else if (event->data.key.usage == 0x4fu ||
                   event->data.key.usage == 0x51u) {
            if (value != control->_private_maximum_value) ++value;
        } else if (event->data.key.usage == 0x4au) {
            value = 0;
        } else if (event->data.key.usage == 0x4du) {
            value = control->_private_maximum_value;
        } else {
            handled = 0;
        }
        if (handled) {
            choice_action(context, context->_private_focus, value, action);
            return ASTRA_OK;
        }
    }
    if (context->_private_focus != CONTROL_NONE &&
        context->_private_controls[context->_private_focus]
                ._private_control_kind == ASTRA_CONTROL_STEPPER) {
        uint32_t index = context->_private_focus;
        AstraControl *control = &context->_private_controls[index];

        if (!down) return ASTRA_OK;
        if (event->data.key.usage == 0x29u) {
            stepper_finish_input(context, index, 0, action);
            return ASTRA_OK;
        }
        if (event->data.key.usage == 0x28u ||
            event->data.key.usage == 0x58u) {
            stepper_finish_input(context, index, 1, action);
            return ASTRA_OK;
        }
        if (event->data.key.usage == 0x2au &&
            (context->_private_value_input_flags &
             VALUE_INPUT_EDITING) != 0u) {
            int64_t value;

            control_damage(context, index);
            if (context->_private_value_input_digits != 0u) {
                context->_private_value_input /= 10u;
                --context->_private_value_input_digits;
            } else {
                context->_private_value_input_flags = 0u;
            }
            control_damage(context, index);
            if (stepper_draft_value(context, &value) &&
                value >= control->_private_minimum_value &&
                value <= control->_private_maximum_value &&
                range_snap(control, value) == value)
                range_action(context, index, value, action);
            return ASTRA_OK;
        }
        if (event->data.key.usage == 0x50u ||
            event->data.key.usage == 0x51u ||
            event->data.key.usage == 0x4fu ||
            event->data.key.usage == 0x52u ||
            event->data.key.usage == 0x4au ||
            event->data.key.usage == 0x4du) {
            uint32_t part;
            uint32_t count = 1u;

            stepper_finish_input(context, index, 0, action);
            if (event->data.key.usage == 0x4au) {
                range_action(context, index,
                             control->_private_minimum_value, action);
                return ASTRA_OK;
            }
            if (event->data.key.usage == 0x4du) {
                range_action(context, index,
                             control->_private_maximum_value, action);
                return ASTRA_OK;
            }
            part = event->data.key.usage == 0x4fu ||
                   event->data.key.usage == 0x52u ?
                STEPPER_PART_UP : STEPPER_PART_DOWN;
            stepper_step(context, index, part, count, action);
            return ASTRA_OK;
        }
    }
    if (down && context->_private_focus != CONTROL_NONE &&
        context->_private_controls[context->_private_focus]
                ._private_control_kind == ASTRA_CONTROL_SPLITTER) {
        AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
        uint32_t index = context->_private_focus;
        AstraControl *control = &context->_private_controls[index];
        int64_t position = control->_private_value;
        int32_t step =
            (event->data.key.modifiers & ASTRA_INPUT_MOD_SHIFT) != 0u ?
                1 : (int32_t)theme.spacing_unit;
        int handled = 1;

        if (event->data.key.usage == 0x4au)
            position = INT64_MIN;
        else if (event->data.key.usage == 0x4du)
            position = INT64_MAX;
        else if (control->_private_style == ASTRA_ORIENTATION_VERTICAL &&
                 event->data.key.usage == 0x50u)
            position -= step;
        else if (control->_private_style == ASTRA_ORIENTATION_VERTICAL &&
                 event->data.key.usage == 0x4fu)
            position += step;
        else if (control->_private_style == ASTRA_ORIENTATION_HORIZONTAL &&
                 event->data.key.usage == 0x52u)
            position -= step;
        else if (control->_private_style == ASTRA_ORIENTATION_HORIZONTAL &&
                 event->data.key.usage == 0x51u)
            position += step;
        else
            handled = 0;
        if (handled)
            return splitter_set_position(context, index, position, action);
    }
    if (down && context->_private_focus != CONTROL_NONE &&
        (context->_private_controls[context->_private_focus]
                 ._private_control_kind == ASTRA_CONTROL_SLIDER ||
         context->_private_controls[context->_private_focus]
                 ._private_control_kind == ASTRA_CONTROL_DIAL)) {
        AstraControl *control =
            &context->_private_controls[context->_private_focus];
        int64_t value = control->_private_value;
        int handled = 1;

        if (event->data.key.usage == 0x50u ||
            event->data.key.usage == 0x51u)
            value = range_move_steps(control, value, -1);
        else if (event->data.key.usage == 0x4fu ||
                 event->data.key.usage == 0x52u)
            value = range_move_steps(control, value, 1);
        else if (event->data.key.usage == 0x4au)
            value = control->_private_minimum_value;
        else if (event->data.key.usage == 0x4du)
            value = control->_private_maximum_value;
        else
            handled = 0;
        if (handled) {
            range_action(context, context->_private_focus, value, action);
            return ASTRA_OK;
        }
    }
    {
        AstraResult result = scroll_key(context, event, action);

        if (result != ASTRA_OK || action->type != ASTRA_UI_ACTION_NONE)
            return result;
    }
    if (!activation_key(event->data.key.usage))
        return ASTRA_OK;
    if (down) {
        if ((event->flags & ASTRA_WINDOW_EVENT_REPEAT) != 0u ||
            context->_private_focus == CONTROL_NONE)
            return ASTRA_OK;
        clear_capture(context);
        clear_keyboard(context);
        context->_private_keyboard = context->_private_focus;
        dynamic_state(context, context->_private_keyboard,
                      ASTRA_CONTROL_PRESSED, 1);
        return ASTRA_OK;
    }
    if (context->_private_keyboard != CONTROL_NONE) {
        uint32_t pressed = context->_private_keyboard;

        clear_keyboard(context);
        if (pressed == context->_private_focus &&
            (context->_private_controls[pressed]._private_state &
             ASTRA_CONTROL_DISABLED) == 0u)
            return activate_control(context, pressed, action);
    }
    return ASTRA_OK;
}

AstraResult astra_interface_ui_handle_event(AstraUIContext *context,
                                            const AstraWindowEvent *event,
                                            AstraUIAction *action)
{
    if (!context_valid(context) || event == NULL || action == NULL ||
        event->size < sizeof(*event) ||
        event->version != ASTRA_WINDOW_EVENT_VERSION ||
        action->size < sizeof(*action))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    action->type = ASTRA_UI_ACTION_NONE;
    action->control_id = 0u;
    action->value = 0;
    action->decimal_places = 0u;
    if (event->type == ASTRA_WINDOW_EVENT_POINTER_MOTION ||
        event->type == ASTRA_WINDOW_EVENT_POINTER_BUTTON)
        return pointer_event(context, event, action);
    else if (event->type == ASTRA_WINDOW_EVENT_POINTER_WHEEL)
        return wheel_event(context, event, action);
    else if (event->type == ASTRA_WINDOW_EVENT_KEY)
        return key_event(context, event, action);
    else if (event->type == ASTRA_WINDOW_EVENT_TEXT &&
             context->_private_focus != CONTROL_NONE) {
        uint32_t index = context->_private_focus;
        uint32_t kind = context->_private_controls[index]
                            ._private_control_kind;

        if (kind == ASTRA_CONTROL_FIELD)
            return field_text(context, index, event, action);
        if (kind == ASTRA_CONTROL_STEPPER) {
            if (!astra_unicode_scalar_valid(event->data.text.codepoint))
                return ASTRA_ERROR_INVALID_ARGUMENT;
            stepper_text(context, index, event->data.text.codepoint, action);
        }
    }
    else if (event->type == ASTRA_WINDOW_EVENT_FRAME &&
             event->data.frame.frame.width != 0u &&
             event->data.frame.frame.height != 0u) {
        if (context->_private_width == event->data.frame.frame.width &&
            context->_private_height == event->data.frame.frame.height)
            return ASTRA_OK;
        context->_private_width = event->data.frame.frame.width;
        context->_private_height = event->data.frame.frame.height;
        context->_private_damage = (AstraControlFrame){
            0, 0, context->_private_width, context->_private_height};
        context->_private_has_damage = 1u;
        if (context->_private_has_layout != 0u)
            return astra_interface_ui_layout(
                context, &context->_private_layout);
    }
    else if (event->type == ASTRA_WINDOW_EVENT_STATE_RESET)
        reset_interaction(context, 1);
    else if (event->type == ASTRA_WINDOW_EVENT_FOCUS &&
             ((event->flags & ASTRA_WINDOW_EVENT_FOCUSED) == 0u ||
              (event->flags & ASTRA_WINDOW_EVENT_LOSS) != 0u))
        reset_interaction(context, 1);
    return ASTRA_OK;
}

AstraResult astra_interface_ui_pointer_shape(const AstraUIContext *context,
                                             uint32_t *shape)
{
    uint32_t index;
    const AstraControl *control;

    if (!context_valid(context) || shape == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    index = context->_private_capture != CONTROL_NONE ?
        context->_private_capture : context->_private_hover;
    if (index == CONTROL_NONE) {
        *shape = ASTRA_POINTER_SHAPE_DEFAULT;
        return ASTRA_OK;
    }
    control = &context->_private_controls[index];
    if (control->_private_control_kind == ASTRA_CONTROL_FIELD)
        *shape = ASTRA_POINTER_SHAPE_TEXT;
    else if (control->_private_control_kind == ASTRA_CONTROL_SPLITTER)
        *shape = control->_private_style == ASTRA_ORIENTATION_VERTICAL ?
            ASTRA_POINTER_SHAPE_RESIZE_HORIZONTAL :
            ASTRA_POINTER_SHAPE_RESIZE_VERTICAL;
    else
        *shape = ASTRA_POINTER_SHAPE_DEFAULT;
    return ASTRA_OK;
}

AstraResult astra_interface_ui_update_pointer(const AstraUIContext *context,
                                              AstraWindow *window,
                                              uint32_t requested_shape,
                                              uint32_t *applied_shape)
{
    uint32_t shape;
    AstraResult result;

    if ((requested_shape != ASTRA_POINTER_SHAPE_AUTOMATIC &&
         requested_shape >= ASTRA_POINTER_SHAPE_COUNT) ||
        applied_shape == NULL ||
        (*applied_shape != UINT32_MAX &&
         *applied_shape >= ASTRA_POINTER_SHAPE_COUNT))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (requested_shape == ASTRA_POINTER_SHAPE_AUTOMATIC) {
        result = astra_interface_ui_pointer_shape(context, &shape);
        if (result != ASTRA_OK)
            return result;
    } else {
        if (!context_valid(context))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        shape = requested_shape;
    }
    if (shape == *applied_shape)
        return ASTRA_OK;
    result = astra_window_set_pointer_shape(window, (AstraPointerShape)shape);
    if (result == ASTRA_OK)
        *applied_shape = shape;
    return result;
}

AstraResult astra_interface_ui_damage(const AstraUIContext *context,
                                      AstraControlFrame *damage)
{
    if (!context_valid(context) || damage == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (context->_private_has_damage == 0u)
        return ASTRA_ERROR_WOULD_BLOCK;
    *damage = context->_private_damage;
    return ASTRA_OK;
}

void astra_interface_ui_damage_clear(AstraUIContext *context)
{
    if (!context_valid(context))
        return;
    context->_private_damage = (AstraControlFrame){0};
    context->_private_has_damage = 0u;
    context->_private_reserved = 0u;
}

AstraResult astra_interface_ui_vblank(AstraUIContext *context,
                                      uint64_t now_ns,
                                      AstraUIAction *action)
{
    uint64_t before;
    uint64_t phase64;
    uint32_t phase_delta;
    uint32_t at;
    uint32_t visited = 0u;

    if (!context_valid(context) || action == NULL ||
        action->size < sizeof(*action))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    action->type = ASTRA_UI_ACTION_NONE;
    action->control_id = 0u;
    action->value = 0;
    action->decimal_places = 0u;
    before = ((uint64_t)context->_private_animation_time_high << 32) |
             context->_private_animation_time_low;
    if (before != 0u && now_ns < before)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    phase64 = (now_ns / UINT64_C(1000000000)) * ASTRA_UI_VBLANK_HZ +
              ((now_ns % UINT64_C(1000000000)) * ASTRA_UI_VBLANK_HZ) /
                  UINT64_C(1000000000);
    phase_delta = before == 0u ? 1u :
        (uint32_t)phase64 - context->_private_animation_phase;
    context->_private_animation_phase = (uint32_t)phase64;
    at = context->_private_animation_first;
    while (at != CONTROL_NONE) {
        AstraControl *control;
        uint32_t next;

        if (at >= context->_private_control_count ||
            ++visited > context->_private_control_count)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        control = &context->_private_controls[at];
        next = control->_private_animation_next;
        if (control->_private_control_kind == ASTRA_CONTROL_PROGRESS) {
            control->_private_animation_phase += phase_delta;
            if (phase_delta != 0u)
                control_damage(context, at);
        } else if (control->_private_control_kind == ASTRA_CONTROL_FIELD) {
            int before_visible = field_caret_visible_at(
                control->_private_animation_phase);

            control->_private_animation_phase += phase_delta;
            if (before_visible != field_caret_visible_at(
                    control->_private_animation_phase))
                control_damage(context, at);
        } else if (control->_private_control_kind == ASTRA_CONTROL_STEPPER) {
            uint32_t before_phase = control->_private_animation_phase;
            uint32_t after_phase = before_phase + phase_delta;
            uint32_t before_repeats = before_phase < STEPPER_REPEAT_DELAY ?
                0u : 1u + (before_phase - STEPPER_REPEAT_DELAY) /
                                STEPPER_REPEAT_INTERVAL;
            uint32_t after_repeats = after_phase < STEPPER_REPEAT_DELAY ?
                0u : 1u + (after_phase - STEPPER_REPEAT_DELAY) /
                                STEPPER_REPEAT_INTERVAL;

            control->_private_animation_phase = after_phase;
            if (after_repeats > before_repeats)
                stepper_step(context, at, context->_private_capture_part,
                             after_repeats - before_repeats, action);
        } else if (control->_private_control_kind ==
                   ASTRA_CONTROL_SCROLLBAR) {
            uint32_t before_phase = control->_private_animation_phase;

            if (before_phase < SCROLLBAR_FADE_FRAMES) {
                uint32_t remaining = SCROLLBAR_FADE_FRAMES - before_phase;

                control->_private_animation_phase +=
                    phase_delta < remaining ? phase_delta : remaining;
                if (control->_private_animation_phase != before_phase)
                    control_damage(context, at);
            }
            animation_sync(context, at);
        }
        at = next;
    }
    context->_private_animation_time_low = (uint32_t)now_ns;
    context->_private_animation_time_high = (uint32_t)(now_ns >> 32);
    return ASTRA_OK;
}

int astra_interface_ui_animations_active(const AstraUIContext *context)
{
    return context_valid(context) &&
           context->_private_animation_first != CONTROL_NONE;
}
