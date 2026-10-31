#include <astra/interface_library.h>
#include <astra/runtime.h>
#include <astra/theme.h>
#include <astra/bytes.h>

#include "control_internal.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static int valid(const AstraToolbarActions *toolbar)
{
    return toolbar != NULL && toolbar->_private_size == sizeof(*toolbar);
}

static int context_contains(const AstraUIContext *context,
    const AstraControl *controls, uint32_t count)
{
    if (context == NULL || controls == NULL ||
        context->_private_controls == NULL || count == 0u ||
        count > context->_private_control_count) return 0;
    for (uint32_t at = 0u;
         at <= context->_private_control_count - count; ++at)
        if (&context->_private_controls[at] == controls) return 1;
    return 0;
}

static int actions_valid(const AstraToolbarCommand *actions, uint32_t count)
{
    if (actions == NULL || count == 0u) return 0;
    for (uint32_t at = 0u; at < count; ++at) {
        const AstraToolbarCommand *action = &actions[at];

        if (action->command_length == 0u ||
            !astra_interface_single_line_text_valid(
                action->command, action->command_length) ||
            action->label_length == 0u ||
            !astra_interface_single_line_text_valid(
                action->label, action->label_length) ||
            action->icon == ASTRA_TOOLBAR_ICON_NONE ||
            action->icon == ASTRA_TOOLBAR_ICON_MORE ||
            action->icon > ASTRA_TOOLBAR_ICON_NEW_FOLDER ||
            !astra_words_zero(action->reserved, 2u)) return 0;
    }
    return 1;
}

static uint32_t group_index(const AstraToolbarActions *toolbar, uint32_t group)
{
    return 1u + group + toolbar->_private_groups[group].first_action;
}

static uint32_t overflow_index(const AstraToolbarActions *toolbar)
{
    return 1u + toolbar->_private_group_count +
           toolbar->_private_action_count;
}

static int info_valid(const AstraToolbarActionsInfo *info)
{
    uint32_t next = 0u;
    uint64_t controls;

    if (info == NULL || info->size < sizeof(*info) ||
        info->first_id == 0u || info->groups == NULL ||
        info->group_count == 0u || info->actions == NULL ||
        info->action_count == 0u || info->scope == NULL ||
        !astra_words_zero(info->reserved, 4u)) return 0;
    controls = (uint64_t)info->group_count + info->action_count + 2u;
    if (controls > UINT32_MAX ||
        (uint64_t)info->first_id + controls - 1u > UINT32_MAX)
        return 0;
    for (uint32_t at = 0u; at < info->group_count; ++at) {
        const AstraToolbarGroup *group = &info->groups[at];

        if (group->first_action != next || group->action_count == 0u ||
            group->action_count > info->action_count - next ||
            group->title_length == 0u ||
            !astra_interface_single_line_text_valid(
                group->title, group->title_length) ||
            (group->flags & ~ASTRA_TOOLBAR_GROUP_PINNED) != 0u ||
            !astra_words_zero(group->reserved, 2u)) return 0;
        next += group->action_count;
    }
    if (next != info->action_count) return 0;
    return actions_valid(info->actions, info->action_count);
}

static int button_group_valid(const AstraButtonGroup *group)
{
    return group != NULL && group->_private_size == sizeof(*group);
}

AstraResult astra_interface_button_group_init(AstraButtonGroup *group,
    AstraControl *controls, const AstraButtonGroupInfo *info)
{
    AstraContainerInfo row = ASTRA_CONTAINER_INFO_INIT;
    AstraResult result;

    if (!button_group_valid(group) || group->_private_controls != NULL ||
        controls == NULL || info == NULL || info->size < sizeof(*info) ||
        info->first_id == 0u || info->scope == NULL ||
        !astra_words_zero(info->reserved, 4u) ||
        !actions_valid(info->actions, info->action_count) ||
        info->first_id > UINT32_MAX - info->action_count)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    row.id = info->first_id;
    row.layout.align_items = ASTRA_FLEX_ALIGN_CENTER;
    row.layout.main_gap = 2u;
    result = astra_interface_container_init(&controls[0], &row);
    if (result != ASTRA_OK) return result;
    for (uint32_t at = 0u; at < info->action_count; ++at) {
        AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
        AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;

        button.id = info->first_id + at + 1u;
        button.text = info->actions[at].label;
        button.text_length = info->actions[at].label_length;
        button.variant = ASTRA_BUTTON_TOOLBAR;
        result = astra_interface_button_init(&controls[at + 1u], &button);
        if (result != ASTRA_OK) return result;
        controls[at + 1u]._private_style |=
            info->actions[at].icon << ASTRA_BUTTON_ICON_SHIFT;
        item.parent_id = row.id;
        item.shrink = 0u;
        item.maximum_height = 22u;
        result = astra_interface_control_set_flex(&controls[at + 1u], &item);
        if (result != ASTRA_OK) return result;
    }
    group->_private_controls = controls;
    group->_private_actions = info->actions;
    group->_private_action_count = info->action_count;
    group->_private_scope = info->scope;
    group->_private_first_id = info->first_id;
    return ASTRA_OK;
}

AstraResult astra_interface_button_group_refresh(AstraButtonGroup *group,
    AstraUIContext *context)
{
    if (!button_group_valid(group) || group->_private_controls == NULL ||
        !context_contains(context, group->_private_controls,
                          group->_private_action_count + 1u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < group->_private_action_count; ++at) {
        AstraControl *button = &group->_private_controls[at + 1u];
        const AstraToolbarCommand *action = &group->_private_actions[at];
        AstraCommandState state = {0};
        AstraResult result = astra_interface_command_query(
            group->_private_scope, action->command, action->command_length,
            &state);
        uint32_t semantic;

        if (result != ASTRA_OK) return result;
        semantic = (state.enabled ? 0u : ASTRA_CONTROL_DISABLED) |
            (state.checked ? ASTRA_CONTROL_SELECTED : 0u);
        if (semantic != button->_private_state) {
            result = astra_interface_ui_set_state(context, button, semantic);
            if (result != ASTRA_OK) return result;
        }
    }
    return ASTRA_OK;
}

AstraResult astra_interface_button_group_handle_action(
    AstraButtonGroup *group, const AstraUIAction *action, int *handled)
{
    uint32_t index;

    if (handled != NULL) *handled = 0;
    if (!button_group_valid(group) || group->_private_controls == NULL ||
        action == NULL || action->size < sizeof(*action) || handled == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (action->type != ASTRA_UI_ACTION_ACTIVATE ||
        action->control_id <= group->_private_first_id) return ASTRA_OK;
    index = action->control_id - group->_private_first_id - 1u;
    if (index >= group->_private_action_count) return ASTRA_OK;
    *handled = 1;
    return astra_interface_command_invoke(group->_private_scope,
        group->_private_actions[index].command,
        group->_private_actions[index].command_length, NULL);
}

uint32_t astra_interface_toolbar_actions_control_count(
    const AstraToolbarActionsInfo *info)
{
    return info_valid(info) ?
        2u + info->group_count + info->action_count : 0u;
}

AstraResult astra_interface_toolbar_actions_init(AstraToolbarActions *toolbar,
    AstraControl *controls, const AstraToolbarActionsInfo *info)
{
    static const char more[] = "More tools";
    AstraContainerInfo root = ASTRA_CONTAINER_INFO_INIT;
    AstraResult result;

    if (!valid(toolbar) || controls == NULL || !info_valid(info) ||
        toolbar->_private_controls != NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    root.id = info->first_id;
    root.layout.align_items = ASTRA_FLEX_ALIGN_CENTER;
    root.layout.padding_left = root.layout.padding_right =
        ASTRA_THEME_TOOLBAR_INSET;
    root.layout.padding_top = root.layout.padding_bottom =
        ASTRA_THEME_TOOLBAR_INSET;
    root.layout.main_gap = 8u;
    result = astra_interface_container_init(&controls[0], &root);
    if (result != ASTRA_OK) return result;
    controls[0]._private_style = ASTRA_CONTAINER_STYLE_TOOLBAR;
    for (uint32_t group = 0u; group < info->group_count; ++group) {
        uint32_t group_at = 1u + group + info->groups[group].first_action;
        AstraContainerInfo row = ASTRA_CONTAINER_INFO_INIT;
        AstraFlexItem group_flex = ASTRA_FLEX_ITEM_INIT;

        row.id = info->first_id + group_at;
        row.layout.align_items = ASTRA_FLEX_ALIGN_CENTER;
        row.layout.main_gap = 2u;
        result = astra_interface_container_init(&controls[group_at], &row);
        if (result != ASTRA_OK) return result;
        group_flex.parent_id = root.id;
        group_flex.shrink = 0u;
        result = astra_interface_control_set_flex(
            &controls[group_at], &group_flex);
        if (result != ASTRA_OK) return result;
        for (uint32_t local = 0u;
             local < info->groups[group].action_count; ++local) {
            uint32_t action = info->groups[group].first_action + local;
            uint32_t at = group_at + local + 1u;
            AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
            AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;

            button.id = info->first_id + at;
            button.text = info->actions[action].label;
            button.text_length = info->actions[action].label_length;
            button.variant = ASTRA_BUTTON_TOOLBAR;
            result = astra_interface_button_init(&controls[at], &button);
            if (result != ASTRA_OK) return result;
            controls[at]._private_style |=
                info->actions[action].icon << ASTRA_BUTTON_ICON_SHIFT;
            item.parent_id = row.id;
            item.shrink = 0u;
            item.maximum_height = ASTRA_THEME_TOOLBAR_BUTTON_HEIGHT;
            result = astra_interface_control_set_flex(&controls[at], &item);
            if (result != ASTRA_OK) return result;
        }
    }
    {
        uint32_t at = 1u + info->group_count + info->action_count;
        AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
        AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;

        button.id = info->first_id + at;
        button.text = more;
        button.text_length = sizeof(more) - 1u;
        button.variant = ASTRA_BUTTON_TOOLBAR;
        button.state = ASTRA_CONTROL_COLLAPSED;
        result = astra_interface_button_init(&controls[at], &button);
        if (result != ASTRA_OK) return result;
        controls[at]._private_style |= ASTRA_BUTTON_ICON_ONLY |
            (ASTRA_TOOLBAR_ICON_MORE << ASTRA_BUTTON_ICON_SHIFT);
        item.parent_id = root.id;
        item.shrink = 0u;
        item.maximum_height = ASTRA_THEME_TOOLBAR_BUTTON_HEIGHT;
        result = astra_interface_control_set_flex(&controls[at], &item);
        if (result != ASTRA_OK) return result;
    }
    toolbar->_private_controls = controls;
    toolbar->_private_groups = info->groups;
    toolbar->_private_group_count = info->group_count;
    toolbar->_private_actions = info->actions;
    toolbar->_private_action_count = info->action_count;
    toolbar->_private_scope = info->scope;
    toolbar->_private_first_id = info->first_id;
    return ASTRA_OK;
}

AstraResult astra_interface_toolbar_actions_refresh(AstraToolbarActions *toolbar,
    AstraUIContext *context)
{
    if (!valid(toolbar) || toolbar->_private_controls == NULL ||
        !context_contains(context, toolbar->_private_controls,
                          overflow_index(toolbar) + 1u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t group = 0u; group < toolbar->_private_group_count; ++group) {
        uint32_t start = toolbar->_private_groups[group].first_action;
        for (uint32_t local = 0u;
             local < toolbar->_private_groups[group].action_count; ++local) {
            uint32_t action = start + local;
            uint32_t at = group_index(toolbar, group) + local + 1u;
            AstraControl *button = &toolbar->_private_controls[at];
            AstraCommandState state = {0};
            uint32_t semantic;
            AstraResult result = astra_interface_command_query(
                toolbar->_private_scope,
                toolbar->_private_actions[action].command,
                toolbar->_private_actions[action].command_length, &state);

            if (result != ASTRA_OK) return result;
            semantic = (state.enabled ? 0u : ASTRA_CONTROL_DISABLED) |
                (state.checked ? ASTRA_CONTROL_SELECTED : 0u);
            if (semantic != button->_private_state) {
                result = astra_interface_ui_set_state(context, button,
                                                       semantic);
                if (result != ASTRA_OK) return result;
            }
        }
    }
    return ASTRA_OK;
}

static AstraResult measure_groups(AstraToolbarActions *toolbar,
    uint32_t mode, uint32_t *widths)
{
    for (uint32_t group = 0u; group < toolbar->_private_group_count; ++group) {
        uint64_t width = 0u;

        for (uint32_t local = 0u;
             local < toolbar->_private_groups[group].action_count; ++local) {
            uint32_t at = group_index(toolbar, group) + local + 1u;
            AstraControl *button = &toolbar->_private_controls[at];
            AstraControlSize size = {0};
            AstraResult result;

            result = astra_interface_toolbar_button_measure(
                button, mode == ASTRA_TOOLBAR_ICONS_ONLY, &size);
            if (result != ASTRA_OK) return result;
            width += size.width + (local != 0u ? 2u : 0u);
        }
        if (width > UINT32_MAX) return ASTRA_ERROR_NO_RESOURCES;
        widths[group] = (uint32_t)width;
    }
    return ASTRA_OK;
}

static uint64_t fitted_width(const AstraToolbarActions *toolbar,
    const uint32_t *widths, const uint8_t *folded, uint32_t hidden)
{
    uint64_t width = 2u * ASTRA_THEME_TOOLBAR_INSET;
    uint32_t members = 0u;

    for (uint32_t at = 0u; at < toolbar->_private_group_count; ++at) {
        if (folded[at]) continue;
        width += widths[at] + (members != 0u ? 8u : 0u);
        ++members;
    }
    if (hidden != 0u)
        width += ASTRA_THEME_TOOLBAR_ICON_BUTTON_WIDTH +
            (members != 0u ? 8u : 0u);
    return width;
}

AstraResult astra_interface_toolbar_actions_fit(AstraToolbarActions *toolbar,
    AstraUIContext *context, uint32_t available_width, uint32_t preferred_mode,
    AstraToolbarFit *fit)
{
    uint32_t *widths;
    uint8_t *folded;
    uint64_t minimum = 2u * ASTRA_THEME_TOOLBAR_INSET;
    uint32_t pinned = 0u, unpinned = 0u, hidden = 0u;
    uint32_t mode;
    uint64_t required;
    AstraResult result;

    if (!valid(toolbar) || toolbar->_private_controls == NULL ||
        !context_contains(context, toolbar->_private_controls,
                          overflow_index(toolbar) + 1u) ||
        fit == NULL || fit->size < sizeof(*fit) ||
        preferred_mode > ASTRA_TOOLBAR_ICONS_ONLY)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (toolbar->_private_group_count > UINT32_MAX / sizeof(*widths))
        return ASTRA_ERROR_NO_RESOURCES;
    widths = astra_runtime_allocate(
        (size_t)toolbar->_private_group_count * sizeof(*widths));
    folded = astra_runtime_allocate(toolbar->_private_group_count);
    if (widths == NULL || folded == NULL) {
        astra_runtime_deallocate(widths);
        astra_runtime_deallocate(folded);
        return ASTRA_ERROR_NO_RESOURCES;
    }
    memset(folded, 0, toolbar->_private_group_count);
    for (uint32_t at = 0u; at < toolbar->_private_group_count; ++at) {
        const AstraToolbarGroup *group = &toolbar->_private_groups[at];

        if ((group->flags & ASTRA_TOOLBAR_GROUP_PINNED) != 0u) {
            minimum += (uint64_t)group->action_count *
                    ASTRA_THEME_TOOLBAR_ICON_BUTTON_WIDTH +
                (uint64_t)(group->action_count - 1u) * 2u +
                (pinned != 0u ? 8u : 0u);
            ++pinned;
        } else ++unpinned;
    }
    if (unpinned != 0u)
        minimum += ASTRA_THEME_TOOLBAR_ICON_BUTTON_WIDTH +
            (pinned != 0u ? 8u : 0u);
    if (minimum > UINT32_MAX) minimum = UINT32_MAX;
    fit->minimum_width = (uint32_t)minimum;
    if (available_width < minimum) {
        astra_runtime_deallocate(widths);
        astra_runtime_deallocate(folded);
        return ASTRA_ERROR_NO_RESOURCES;
    }
    mode = preferred_mode;
    result = measure_groups(toolbar, mode, widths);
    if (result != ASTRA_OK) goto done;
    required = fitted_width(toolbar, widths, folded, 0u);
    if (mode == ASTRA_TOOLBAR_LABELS && required > available_width) {
        mode = ASTRA_TOOLBAR_ICONS_ONLY;
        result = measure_groups(toolbar, mode, widths);
        if (result != ASTRA_OK) goto done;
        required = fitted_width(toolbar, widths, folded, 0u);
    }
    while (required > available_width) {
        uint32_t candidate = UINT32_MAX;

        for (uint32_t at = 0u; at < toolbar->_private_group_count; ++at) {
            if (folded[at] ||
                (toolbar->_private_groups[at].flags &
                 ASTRA_TOOLBAR_GROUP_PINNED) != 0u) continue;
            if (candidate == UINT32_MAX ||
                toolbar->_private_groups[at].priority >
                    toolbar->_private_groups[candidate].priority ||
                (toolbar->_private_groups[at].priority ==
                    toolbar->_private_groups[candidate].priority &&
                 at > candidate)) candidate = at;
        }
        if (candidate == UINT32_MAX) {
            result = ASTRA_ERROR_NO_RESOURCES;
            goto done;
        }
        folded[candidate] = 1u;
        ++hidden;
        required = fitted_width(toolbar, widths, folded, hidden);
    }
    if (toolbar->_private_overflow_items != NULL) {
        result = astra_interface_toolbar_actions_close(toolbar);
        if (result != ASTRA_OK) goto done;
    }
    for (uint32_t at = 0u; at < toolbar->_private_group_count; ++at) {
        AstraControl *group =
            &toolbar->_private_controls[group_index(toolbar, at)];
        group->_private_state = folded[at] ? ASTRA_CONTROL_COLLAPSED : 0u;
    }
    for (uint32_t group = 0u; group < toolbar->_private_group_count; ++group)
        for (uint32_t local = 0u;
             local < toolbar->_private_groups[group].action_count; ++local) {
            uint32_t at = group_index(toolbar, group) + local + 1u;
            AstraControl *button = &toolbar->_private_controls[at];
            AstraControlSize size = {0};

            result = astra_interface_toolbar_button_measure(
                button, mode == ASTRA_TOOLBAR_ICONS_ONLY, &size);
            if (result != ASTRA_OK) goto done;
            if (mode == ASTRA_TOOLBAR_ICONS_ONLY)
                button->_private_style |= ASTRA_BUTTON_ICON_ONLY;
            else
                button->_private_style &= ~ASTRA_BUTTON_ICON_ONLY;
            button->_private_measured_width = size.width;
            button->_private_measured_height = size.height;
        }
    toolbar->_private_controls[overflow_index(toolbar)]._private_state =
        hidden != 0u ? 0u : ASTRA_CONTROL_COLLAPSED;
    result = astra_interface_ui_layout(context, &context->_private_layout);
    if (result == ASTRA_OK) {
        toolbar->_private_mode = mode;
        fit->mode = mode;
        fit->hidden_groups = hidden;
        fit->required_width = (uint32_t)required;
    }
done:
    astra_runtime_deallocate(widths);
    astra_runtime_deallocate(folded);
    return result;
}

static AstraResult open_overflow(AstraToolbarActions *toolbar,
    AstraWindow *window)
{
    AstraContextMenuItem *items;
    uint32_t count = 0u, groups = 0u;
    AstraResult result;
    AstraControl *button = &toolbar->_private_controls[
        overflow_index(toolbar)];

    if (window == NULL ||
        (button->_private_state & ASTRA_CONTROL_COLLAPSED) != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (toolbar->_private_overflow_items != NULL)
        return ASTRA_ERROR_BUSY;
    for (uint32_t group = 0u; group < toolbar->_private_group_count; ++group)
        if ((toolbar->_private_controls[group_index(toolbar, group)]
            ._private_state & ASTRA_CONTROL_COLLAPSED) != 0u) {
            count += toolbar->_private_groups[group].action_count;
            ++groups;
        }
    if (groups == 0u ||
        (uint64_t)count + groups - 1u >
            UINT32_MAX / sizeof(*items))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    count += groups - 1u;
    items = astra_runtime_allocate((size_t)count * sizeof(*items));
    if (items == NULL) return ASTRA_ERROR_NO_RESOURCES;
    count = 0u;
    groups = 0u;
    for (uint32_t group = 0u; group < toolbar->_private_group_count; ++group) {
        const AstraToolbarGroup *description =
            &toolbar->_private_groups[group];

        if ((toolbar->_private_controls[group_index(toolbar, group)]
            ._private_state & ASTRA_CONTROL_COLLAPSED) == 0u) continue;
        if (groups++ != 0u)
            items[count++] = (AstraContextMenuItem){0};
        for (uint32_t at = 0u; at < description->action_count; ++at) {
            const AstraToolbarCommand *action =
                &toolbar->_private_actions[description->first_action + at];

            items[count++] = (AstraContextMenuItem){
                action->command, action->command_length, NULL};
        }
    }
    result = astra_interface_context_menu_open(
        &toolbar->_private_overflow, window, toolbar->_private_scope,
        items, count, button->_private_frame.x,
        button->_private_frame.y + (int32_t)button->_private_frame.height);
    if (result != ASTRA_OK) {
        astra_runtime_deallocate(items);
        return result;
    }
    toolbar->_private_overflow_items = items;
    toolbar->_private_overflow_count = count;
    return ASTRA_OK;
}

AstraResult astra_interface_toolbar_actions_handle_action(
    AstraToolbarActions *toolbar, AstraWindow *window,
    const AstraUIAction *action, int *handled)
{
    if (handled != NULL) *handled = 0;
    if (!valid(toolbar) || toolbar->_private_controls == NULL ||
        action == NULL || action->size < sizeof(*action) || handled == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (action->type != ASTRA_UI_ACTION_ACTIVATE) return ASTRA_OK;
    if (action->control_id ==
            toolbar->_private_first_id + overflow_index(toolbar)) {
        *handled = 1;
        return open_overflow(toolbar, window);
    }
    for (uint32_t group = 0u; group < toolbar->_private_group_count; ++group) {
        uint32_t start = group_index(toolbar, group) + 1u;
        uint32_t end = start + toolbar->_private_groups[group].action_count;
        uint32_t index = action->control_id - toolbar->_private_first_id;

        if (index < start || index >= end) continue;
        *handled = 1;
        index = toolbar->_private_groups[group].first_action + index - start;
        return astra_interface_command_invoke(toolbar->_private_scope,
            toolbar->_private_actions[index].command,
            toolbar->_private_actions[index].command_length, NULL);
    }
    return ASTRA_OK;
}

AstraResult astra_interface_toolbar_actions_handle_event(
    AstraToolbarActions *toolbar, const AstraWindowEvent *event, int *handled)
{
    AstraResult result;

    if (handled != NULL) *handled = 0;
    if (!valid(toolbar) || toolbar->_private_controls == NULL ||
        event == NULL || handled == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    if (event->type != ASTRA_WINDOW_EVENT_MENU_RESULT ||
        toolbar->_private_overflow._private_request == 0u ||
        event->data.menu_result.request !=
            toolbar->_private_overflow._private_request)
        return ASTRA_OK;
    result = astra_interface_context_menu_handle_event(
        &toolbar->_private_overflow, event, handled);
    if (toolbar->_private_overflow._private_request == 0u) {
        astra_runtime_deallocate(toolbar->_private_overflow_items);
        toolbar->_private_overflow_items = NULL;
        toolbar->_private_overflow_count = 0u;
    }
    return result;
}

AstraResult astra_interface_toolbar_actions_close(AstraToolbarActions *toolbar)
{
    AstraResult result;

    if (!valid(toolbar)) return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_interface_context_menu_close(&toolbar->_private_overflow);
    if (result != ASTRA_OK) return result;
    astra_runtime_deallocate(toolbar->_private_overflow_items);
    toolbar->_private_overflow_items = NULL;
    toolbar->_private_overflow_count = 0u;
    return ASTRA_OK;
}
