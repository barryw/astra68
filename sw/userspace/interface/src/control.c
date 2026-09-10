#include "control_internal.h"

#include <astra/bytes.h>
#include <astra/draw_list.h>
#include <astra/input.h>
#include <astra/input_modifiers.h>
#include <astra/surface.h>
#include <astra/theme.h>

#include <limits.h>
#include <stddef.h>

#define CONTROL_NONE UINT32_MAX
#define CONTROL_SEMANTIC_STATES \
    (ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_SELECTED | ASTRA_CONTROL_ERROR)
#define CONTROL_PREVIEW_STATES \
    (ASTRA_CONTROL_HOVERED | ASTRA_CONTROL_PRESSED | ASTRA_CONTROL_FOCUSED)
#define FLEX_ITEM_FLAGS (ASTRA_FLEX_BREAK_BEFORE | ASTRA_FLEX_BREAK_AFTER)
#define TOGGLE_KINDS(kind) \
    ((kind) == ASTRA_CONTROL_CHECKBOX || (kind) == ASTRA_CONTROL_RADIO || \
     (kind) == ASTRA_CONTROL_SWITCH)

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
static void activate_control(AstraUIContext *context, uint32_t index,
                             AstraUIAction *action);

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
        (control->_private_dynamic_state & ~CONTROL_PREVIEW_STATES) != 0u ||
        control->_private_minimum_width > control->_private_maximum_width ||
        control->_private_minimum_height > control->_private_maximum_height ||
        control->_private_align_self > ASTRA_FLEX_ALIGN_AUTO ||
        (control->_private_flex_flags & ~FLEX_ITEM_FLAGS) != 0u)
        return 0;
    if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER)
        return control->_private_text == NULL &&
               control->_private_text_length == 0u &&
               control->_private_state == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_dynamic_state == 0u &&
               control->_private_style == 0u &&
               control->_private_value == 0 &&
               control->_private_minimum_value == 0 &&
               control->_private_maximum_value == 0 &&
               control->_private_value_step == 0 &&
               layout_valid(&control->_private_child_layout);
    if (control->_private_control_kind == ASTRA_CONTROL_LABEL)
        return control->_private_text != NULL &&
               control->_private_text_length != 0u &&
               control->_private_style >= ASTRA_TEXT_CLIENT_PRIMARY &&
               control->_private_style <= ASTRA_TEXT_CLIENT_MUTED &&
               (state & ~(uint32_t)ASTRA_CONTROL_DISABLED) == 0u &&
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
               (state & ~(ASTRA_CONTROL_DISABLED |
                          ASTRA_CONTROL_ERROR)) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_minimum_value <
                   control->_private_maximum_value &&
               control->_private_value >= control->_private_minimum_value &&
               control->_private_value <= control->_private_maximum_value &&
               control->_private_value_step > 0;
    if (control->_private_control_kind == ASTRA_CONTROL_PROGRESS)
        return control->_private_text == NULL &&
               control->_private_text_length == 0u &&
               control->_private_style == 0u &&
               (state & ~(ASTRA_CONTROL_DISABLED |
                          ASTRA_CONTROL_ERROR)) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_minimum_value == 0 &&
               control->_private_value_step == 0 &&
               control->_private_value >= 0 &&
               control->_private_maximum_value >= 0 &&
               control->_private_value <= control->_private_maximum_value;
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
           context->_private_reserved == 0u;
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

static uint32_t effective_state(const AstraControl *control)
{
    return control->_private_state | control->_private_preview_state |
           control->_private_dynamic_state;
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
    if ((state & ASTRA_CONTROL_FOCUSED) != 0u) {
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
    if (context->_private_hover == index)
        return;
    dynamic_state(context, context->_private_hover,
                  ASTRA_CONTROL_HOVERED, 0);
    context->_private_hover = index;
    dynamic_state(context, index, ASTRA_CONTROL_HOVERED, 1);
}

static void set_focus(AstraUIContext *context, uint32_t index)
{
    if (context->_private_focus == index)
        return;
    dynamic_state(context, context->_private_focus,
                  ASTRA_CONTROL_FOCUSED, 0);
    context->_private_focus = index;
    dynamic_state(context, index, ASTRA_CONTROL_FOCUSED, 1);
}

static void clear_capture(AstraUIContext *context)
{
    dynamic_state(context, context->_private_capture,
                  ASTRA_CONTROL_PRESSED, 0);
    context->_private_capture = CONTROL_NONE;
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
        set_focus(context, CONTROL_NONE);
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

        if ((control->_private_control_kind == ASTRA_CONTROL_BUTTON ||
             TOGGLE_KINDS(control->_private_control_kind) ||
             control->_private_control_kind == ASTRA_CONTROL_SLIDER) &&
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

        if ((control->_private_control_kind == ASTRA_CONTROL_BUTTON ||
             TOGGLE_KINDS(control->_private_control_kind) ||
             control->_private_control_kind == ASTRA_CONTROL_SLIDER) &&
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
        info->id == 0u || info->text == NULL || info->text_length == 0u ||
        info->text_role < ASTRA_TEXT_CLIENT_PRIMARY ||
        info->text_role > ASTRA_TEXT_CLIENT_MUTED ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_LABEL;
    control->_private_id = info->id;
    control->_private_style = info->text_role;
    control->_private_text = info->text;
    control->_private_text_length = info->text_length;
    return ASTRA_OK;
}

AstraResult astra_interface_button_init(AstraControl *control,
                                        const AstraButtonInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->text == NULL || info->text_length == 0u ||
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

AstraResult astra_interface_container_init(AstraControl *control,
                                           const AstraContainerInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || !layout_valid(&info->layout) ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_CONTAINER;
    control->_private_id = info->id;
    control->_private_child_layout = info->layout;
    return ASTRA_OK;
}

static AstraResult toggle_init(AstraControl *control,
                               const AstraToggleInfo *info, uint32_t kind)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->text == NULL || info->text_length == 0u ||
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
        info->orientation > ASTRA_ORIENTATION_VERTICAL ||
        (info->state & ~(ASTRA_CONTROL_DISABLED |
                         ASTRA_CONTROL_ERROR)) != 0u ||
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

AstraResult astra_interface_progress_init(AstraControl *control,
                                          const AstraProgressInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->value > info->maximum ||
        info->maximum > INT32_MAX ||
        (info->maximum == 0u && info->value != 0u) ||
        (info->state & ~(ASTRA_CONTROL_DISABLED |
                         ASTRA_CONTROL_ERROR)) != 0u ||
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

    if (index == CONTROL_NONE || text == NULL || text_length == 0u ||
        (control->_private_control_kind != ASTRA_CONTROL_LABEL &&
         control->_private_control_kind != ASTRA_CONTROL_BUTTON &&
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
        if (control->_private_parent_id != 0u) {
            for (uint32_t before = 0u; before < at; ++before)
                if (controls[before]._private_id ==
                    control->_private_parent_id) {
                    parent = before;
                    break;
                }
            if (parent == CONTROL_NONE ||
                controls[parent]._private_control_kind !=
                    ASTRA_CONTROL_CONTAINER)
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
    if (control->_private_control_kind == ASTRA_CONTROL_PROGRESS) {
        size->width = theme.spacing_unit * 32u;
        size->height = theme.spacing_unit * 2u;
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
    uint32_t maximum = row ? control->_private_maximum_width :
                             control->_private_maximum_height;
    uint32_t minimum = axis_minimum(control, row);

    return maximum < minimum ? minimum : maximum;
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
    for (uint32_t at = container->_private_first_child;
         at != CONTROL_NONE;
         at = context->_private_controls[at]._private_next_sibling) {
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
         at = context->_private_controls[at]._private_next_sibling)
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
             at = context->_private_controls[at]._private_next_sibling) {
            uint64_t weight = item_weight(
                &context->_private_controls[at], row, grow);

            if (UINT64_MAX - total < weight)
                return 0;
            total += weight;
        }
        if (total == 0u || remaining > UINT64_MAX / total)
            return total == 0u;
        for (uint32_t at = first; at != after;
             at = context->_private_controls[at]._private_next_sibling) {
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

static AstraResult layout_children(AstraUIContext *context, uint32_t child,
                                   const AstraFlexLayout *layout,
                                   const AstraControlFrame *bounds,
                                   const AstraControlFrame *clip)
{
    uint32_t inner_main;
    uint32_t inner_cross;
    uint64_t cross_position;
    int row;

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
         at = context->_private_controls[at]._private_next_sibling)
        context->_private_controls[at]._private_flex_target =
            axis_base(&context->_private_controls[at], row);
    cross_position = (uint64_t)(row ? bounds->y : bounds->x) +
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
        uint64_t used;
        uint64_t main_position;

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
            after = control->_private_next_sibling;
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
                 at = context->_private_controls[at]._private_next_sibling) {
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
        main_position = (uint64_t)(row ? bounds->x : bounds->y) +
                        (row ? layout->padding_left : layout->padding_top) +
                        justify_offset;
        for (uint32_t at = first; at != after;) {
            AstraControl *control = &context->_private_controls[at];
            uint32_t next = control->_private_next_sibling;
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
            if (main_position > INT32_MAX ||
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
        cross_position += line_cross;
        if (after != CONTROL_NONE) cross_position += layout->cross_gap;
        first = after;
    }
    return ASTRA_OK;
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

        if (container->_private_control_kind != ASTRA_CONTROL_CONTAINER ||
            container->_private_laid_out == 0u)
            continue;
        clip = frame_intersection(&container->_private_frame,
                                  &container->_private_clip);
        result = layout_children(context, container->_private_first_child,
                                 &container->_private_child_layout,
                                 &container->_private_frame, &clip);
        if (result != ASTRA_OK) return result;
    }
    context->_private_layout = *layout;
    context->_private_has_layout = 1u;
    return ASTRA_OK;
}

AstraResult astra_interface_ui_set_state(AstraUIContext *context,
                                         AstraControl *control,
                                         uint32_t state)
{
    uint32_t index = control_index(context, control);
    uint32_t allowed;

    if (index == CONTROL_NONE ||
        control->_private_control_kind == ASTRA_CONTROL_CONTAINER)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (control->_private_control_kind == ASTRA_CONTROL_LABEL)
        allowed = ASTRA_CONTROL_DISABLED;
    else if (control->_private_control_kind == ASTRA_CONTROL_SLIDER ||
             control->_private_control_kind == ASTRA_CONTROL_PROGRESS)
        allowed = ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_ERROR;
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
    control_damage(context, index);
    control->_private_state = state;
    if ((state & ASTRA_CONTROL_DISABLED) != 0u) {
        if (context->_private_hover == index) set_hover(context, CONTROL_NONE);
        if (context->_private_capture == index) clear_capture(context);
        if (context->_private_keyboard == index) clear_keyboard(context);
        if (context->_private_focus == index) set_focus(context, CONTROL_NONE);
    }
    control_damage(context, index);
    return ASTRA_OK;
}

static int32_t range_snap(const AstraControl *control, int64_t value)
{
    int64_t minimum = control->_private_minimum_value;
    int64_t maximum = control->_private_maximum_value;
    int64_t step = control->_private_value_step;
    int64_t offset;
    int64_t snapped;

    if (value <= minimum) return (int32_t)minimum;
    if (value >= maximum) return (int32_t)maximum;
    offset = value - minimum;
    snapped = minimum + ((offset + step / 2) / step) * step;
    if (snapped > maximum) snapped = maximum;
    return (int32_t)snapped;
}

AstraResult astra_interface_control_set_value(AstraUIContext *context,
                                              AstraControl *control,
                                              int32_t value)
{
    uint32_t index = control_index(context, control);
    int32_t snapped;

    if (index == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_SLIDER)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    snapped = range_snap(control, value);
    if (snapped == control->_private_value)
        return ASTRA_OK;
    control_damage(context, index);
    control->_private_value = snapped;
    control_damage(context, index);
    return ASTRA_OK;
}

AstraResult astra_interface_control_get_value(const AstraControl *control,
                                              int32_t *value)
{
    if (!control_valid(control) || value == NULL ||
        (control->_private_control_kind != ASTRA_CONTROL_SLIDER &&
         control->_private_control_kind != ASTRA_CONTROL_PROGRESS))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *value = control->_private_value;
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
        if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER)
            continue;
        if (control->_private_clip.width == 0u ||
            control->_private_clip.height == 0u)
            continue;
        state = effective_state(control);
        if (control->_private_control_kind == ASTRA_CONTROL_LABEL)
            *commands += 1u;
        else if (control->_private_control_kind == ASTRA_CONTROL_BUTTON)
            *commands += 2u;
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
        else if (control->_private_control_kind == ASTRA_CONTROL_PROGRESS)
            *commands += 2u;
        else
            *commands += 3u +
                ((control->_private_control_kind == ASTRA_CONTROL_SWITCH &&
                  ((state & ASTRA_CONTROL_SELECTED) == 0u ||
                   (state & ASTRA_CONTROL_DISABLED) != 0u)) ? 1u : 0u);
        if (control->_private_control_kind == ASTRA_CONTROL_BUTTON &&
            (state & ASTRA_CONTROL_FOCUSED) != 0u)
            ++*commands;
        if (control->_private_control_kind == ASTRA_CONTROL_BUTTON &&
            (state & ASTRA_CONTROL_ERROR) != 0u)
            ++*commands;
        if (TOGGLE_KINDS(control->_private_control_kind) &&
            (state & ASTRA_CONTROL_FOCUSED) != 0u)
            ++*commands;
        if (control->_private_control_kind == ASTRA_CONTROL_SLIDER &&
            (state & ASTRA_CONTROL_FOCUSED) != 0u)
            ++*commands;
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

    if ((state & ASTRA_CONTROL_FOCUSED) != 0u)
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

    if ((state & ASTRA_CONTROL_FOCUSED) != 0u)
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

static void render_slider(const AstraTheme *theme,
                          const AstraControl *control,
                          AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t state = effective_state(control);
    uint64_t range = (uint64_t)((int64_t)control->_private_maximum_value -
                                control->_private_minimum_value);
    uint64_t offset = (uint64_t)((int64_t)control->_private_value -
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
        uint32_t position = (uint32_t)(offset * travel / range);
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
        uint32_t position = (uint32_t)(offset * travel / range);
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
    if ((state & ASTRA_CONTROL_FOCUSED) != 0u)
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

static void render_progress(const AstraUIContext *context,
                            const AstraTheme *theme,
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
        uint32_t phase = context->_private_animation_phase & 63u;
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

static void slider_action(AstraUIContext *context, uint32_t index,
                          int64_t value, AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    int32_t before = control->_private_value;

    (void)astra_interface_control_set_value(
        context, control,
        value < INT32_MIN ? INT32_MIN :
        (value > INT32_MAX ? INT32_MAX : (int32_t)value));
    if (control->_private_value != before) {
        action->type = ASTRA_UI_ACTION_VALUE_CHANGED;
        action->control_id = control->_private_id;
        action->value = control->_private_value;
    }
}

static void slider_pointer(AstraUIContext *context, uint32_t index,
                           int32_t x, int32_t y, AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t travel;
    int64_t position;
    int64_t range = (int64_t)control->_private_maximum_value -
                    control->_private_minimum_value;
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
    value = (int64_t)control->_private_minimum_value +
            position * range / travel;
    slider_action(context, index, value, action);
}

AstraResult astra_interface_ui_render(const AstraUIContext *context,
                                      AstraSurfaceView *surface)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraSurfaceView validated;
    uint64_t commands;
    uint64_t payload;

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
    if (surface->kind == ASTRA_SURFACE_VIEW_DRAW_LIST) {
        if (!draw_list_has_room(surface, commands, payload))
            return ASTRA_ERROR_NO_RESOURCES;
    } else if (!astra_surface_view_init(
                   &validated, surface->pixels, surface->byte_size,
                   surface->width, surface->height, surface->pitch)) {
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    validated = *surface;
    for (uint32_t at = 0u; at < context->_private_control_count; ++at) {
        const AstraControl *control = &context->_private_controls[at];
        AstraSurfaceView clipped;

        if (control->_private_laid_out == 0u ||
            control->_private_control_kind == ASTRA_CONTROL_CONTAINER ||
            control->_private_clip.width == 0u ||
            control->_private_clip.height == 0u)
            continue;
        clipped = validated;
        if (!astra_surface_clip(&clipped, control->_private_clip.x,
                                control->_private_clip.y,
                                control->_private_clip.width,
                                control->_private_clip.height))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if (control->_private_control_kind == ASTRA_CONTROL_LABEL)
            render_label(&theme, control, &clipped);
        else if (control->_private_control_kind == ASTRA_CONTROL_BUTTON)
            render_button(&theme, control, &clipped);
        else if (control->_private_control_kind == ASTRA_CONTROL_SLIDER)
            render_slider(&theme, control, &clipped);
        else if (control->_private_control_kind == ASTRA_CONTROL_PROGRESS)
            render_progress(context, &theme, control, &clipped);
        else
            render_toggle(&theme, control, &clipped);
    }
    return ASTRA_OK;
}

static void pointer_event(AstraUIContext *context,
                          const AstraWindowEvent *event,
                          AstraUIAction *action)
{
    uint32_t hit = hit_test(context, event->data.pointer.x,
                            event->data.pointer.y);

    set_hover(context, hit);
    if (event->type == ASTRA_WINDOW_EVENT_POINTER_MOTION) {
        if (context->_private_capture != CONTROL_NONE) {
            dynamic_state(context, context->_private_capture,
                          ASTRA_CONTROL_PRESSED,
                          hit == context->_private_capture);
            if (context->_private_controls[context->_private_capture]
                    ._private_control_kind == ASTRA_CONTROL_SLIDER)
                slider_pointer(context, context->_private_capture,
                               event->data.pointer.x,
                               event->data.pointer.y, action);
        }
        return;
    }
    if (event->data.pointer.button != ASTRA_INPUT_BUTTON_LEFT)
        return;
    if ((event->flags & ASTRA_WINDOW_EVENT_DOWN) != 0u) {
        clear_keyboard(context);
        clear_capture(context);
        set_focus(context, hit);
        context->_private_capture = hit;
        dynamic_state(context, hit, ASTRA_CONTROL_PRESSED, 1);
        if (hit != CONTROL_NONE &&
            context->_private_controls[hit]._private_control_kind ==
                ASTRA_CONTROL_SLIDER)
            slider_pointer(context, hit, event->data.pointer.x,
                           event->data.pointer.y, action);
        return;
    }
    if (context->_private_capture != CONTROL_NONE) {
        uint32_t captured = context->_private_capture;
        int slider = context->_private_controls[captured]
                         ._private_control_kind == ASTRA_CONTROL_SLIDER;

        if (slider)
            slider_pointer(context, captured, event->data.pointer.x,
                           event->data.pointer.y, action);

        clear_capture(context);
        if (!slider && hit == captured &&
            (context->_private_controls[captured]._private_state &
             ASTRA_CONTROL_DISABLED) == 0u)
            activate_control(context, captured, action);
    }
}

static int activation_key(uint32_t usage)
{
    return usage == 0x28u || usage == 0x2cu || usage == 0x58u;
}

static void activate_control(AstraUIContext *context, uint32_t index,
                             AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];

    if (control->_private_control_kind == ASTRA_CONTROL_SLIDER)
        return;
    action->control_id = control->_private_id;
    if (!TOGGLE_KINDS(control->_private_control_kind)) {
        action->type = ASTRA_UI_ACTION_ACTIVATE;
        return;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_RADIO) {
        if ((control->_private_state & ASTRA_CONTROL_SELECTED) != 0u)
            return;
        (void)astra_interface_ui_set_state(
            context, control,
            control->_private_state | ASTRA_CONTROL_SELECTED);
    } else {
        control_damage(context, index);
        control->_private_state ^= ASTRA_CONTROL_SELECTED;
        control_damage(context, index);
    }
    action->type = ASTRA_UI_ACTION_VALUE_CHANGED;
    action->value = (control->_private_state & ASTRA_CONTROL_SELECTED) != 0u;
}

static void key_event(AstraUIContext *context,
                      const AstraWindowEvent *event,
                      AstraUIAction *action)
{
    int down = (event->flags & ASTRA_WINDOW_EVENT_DOWN) != 0u;

    if (event->data.key.usage == 0x2bu) {
        if (down && (event->flags & ASTRA_WINDOW_EVENT_REPEAT) == 0u)
            set_focus(context, next_focus(
                context,
                (event->data.key.modifiers & ASTRA_INPUT_MOD_SHIFT) != 0u));
        return;
    }
    if (down && context->_private_focus != CONTROL_NONE &&
        context->_private_controls[context->_private_focus]
                ._private_control_kind == ASTRA_CONTROL_SLIDER) {
        AstraControl *control =
            &context->_private_controls[context->_private_focus];
        int64_t value = control->_private_value;

        if (event->data.key.usage == 0x50u ||
            event->data.key.usage == 0x51u)
            value -= control->_private_value_step;
        else if (event->data.key.usage == 0x4fu ||
                 event->data.key.usage == 0x52u)
            value += control->_private_value_step;
        else if (event->data.key.usage == 0x4au)
            value = control->_private_minimum_value;
        else if (event->data.key.usage == 0x4du)
            value = control->_private_maximum_value;
        else
            value = INT64_MIN;
        if (value != INT64_MIN) {
            slider_action(context, context->_private_focus, value, action);
            return;
        }
    }
    if (!activation_key(event->data.key.usage))
        return;
    if (down) {
        if ((event->flags & ASTRA_WINDOW_EVENT_REPEAT) != 0u ||
            context->_private_focus == CONTROL_NONE)
            return;
        clear_capture(context);
        clear_keyboard(context);
        context->_private_keyboard = context->_private_focus;
        dynamic_state(context, context->_private_keyboard,
                      ASTRA_CONTROL_PRESSED, 1);
        return;
    }
    if (context->_private_keyboard != CONTROL_NONE) {
        uint32_t pressed = context->_private_keyboard;

        clear_keyboard(context);
        if (pressed == context->_private_focus &&
            (context->_private_controls[pressed]._private_state &
             ASTRA_CONTROL_DISABLED) == 0u)
            activate_control(context, pressed, action);
    }
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
    if (event->type == ASTRA_WINDOW_EVENT_POINTER_MOTION ||
        event->type == ASTRA_WINDOW_EVENT_POINTER_BUTTON)
        pointer_event(context, event, action);
    else if (event->type == ASTRA_WINDOW_EVENT_KEY)
        key_event(context, event, action);
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
}

AstraResult astra_interface_ui_tick(AstraUIContext *context, uint64_t now_ns)
{
    uint64_t before;
    uint64_t elapsed;
    uint64_t steps;

    if (!context_valid(context))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    before = ((uint64_t)context->_private_animation_time_high << 32) |
             context->_private_animation_time_low;
    if (before == 0u) {
        context->_private_animation_time_low = (uint32_t)now_ns;
        context->_private_animation_time_high = (uint32_t)(now_ns >> 32);
        return ASTRA_OK;
    }
    if (now_ns < before)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    elapsed = now_ns - before;
    if (elapsed < ASTRA_UI_ANIMATION_INTERVAL_NS)
        return ASTRA_OK;
    steps = elapsed / ASTRA_UI_ANIMATION_INTERVAL_NS;
    context->_private_animation_phase =
        (context->_private_animation_phase + (uint32_t)(steps & 63u)) & 63u;
    before += steps * ASTRA_UI_ANIMATION_INTERVAL_NS;
    context->_private_animation_time_low = (uint32_t)before;
    context->_private_animation_time_high = (uint32_t)(before >> 32);
    for (uint32_t at = 0u; at < context->_private_control_count; ++at)
        if (context->_private_controls[at]._private_control_kind ==
                ASTRA_CONTROL_PROGRESS &&
            context->_private_controls[at]._private_maximum_value == 0 &&
            (context->_private_controls[at]._private_state &
             ASTRA_CONTROL_DISABLED) == 0u)
            control_damage(context, at);
    return ASTRA_OK;
}
