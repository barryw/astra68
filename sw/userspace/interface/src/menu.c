#include <astra/menu.h>
#include <astra/input.h>
#include <astra/theme.h>

#include "control_internal.h"

#include <limits.h>
#include <stddef.h>

#define NONE UINT32_MAX
#define OPEN_PRESS (UINT32_MAX - 1u)
#define PAD 8u
#define ROW 32u
#define PITCH 40u
#define SEPARATOR 8u

static int valid(const AstraMenu *menu)
{
    return menu != NULL && menu->_private_size == sizeof(*menu) &&
        menu->_private_items != NULL && menu->_private_count != 0u &&
        menu->_private_frame.width >= 64u && menu->_private_frame.width <= UINT16_MAX &&
        menu->_private_frame.height >= ROW + 2u * PAD &&
        menu->_private_frame.height <= UINT16_MAX && menu->_private_active <= 1u &&
        menu->_private_content_height >= menu->_private_frame.height &&
        menu->_private_content_height <= INT32_MAX &&
        menu->_private_scroll <= menu->_private_content_height - menu->_private_frame.height &&
        (menu->_private_selected == NONE || menu->_private_selected < menu->_private_count) &&
        (menu->_private_pressed == NONE || menu->_private_pressed == OPEN_PRESS ||
         menu->_private_pressed < menu->_private_count);
}

static uint32_t pitch(const AstraMenuItem *item)
{
    return (item->flags & ASTRA_MENU_SEPARATOR) != 0u ? SEPARATOR : PITCH;
}

static int enabled(const AstraMenuItem *item)
{
    return (item->flags & (ASTRA_MENU_DISABLED | ASTRA_MENU_SEPARATOR)) == 0u;
}

static int32_t fitted(int32_t point, uint32_t extent, int32_t start, uint32_t size)
{
    int64_t position = point;
    int64_t end = (int64_t)start + size;

    if (position + extent > end)
        position -= extent;
    if (position < start) position = start;
    if (position > end - extent) position = end - extent;
    return (int32_t)position;
}

AstraResult astra_interface_menu_init(AstraMenu *menu,
    const AstraMenuItem *items, uint32_t count, const AstraControlFrame *work,
    int32_t x, int32_t y, uint32_t minimum_width, uint32_t flags)
{
    AstraMenu candidate = ASTRA_MENU_INIT;
    uint64_t height = 2u * PAD - (PITCH - ROW);
    uint64_t width = minimum_width < 64u ? 64u : minimum_width;
    uint32_t rows = 0u;

    if (menu == NULL || items == NULL || count == 0u || work == NULL ||
        count > (UINT32_MAX - 2u * PAD) / PITCH ||
        work->width < 64u || work->width > UINT16_MAX ||
        work->height < ROW + 2u * PAD || work->height > UINT16_MAX ||
        (int64_t)work->x + work->width > INT32_MAX ||
        (int64_t)work->y + work->height > INT32_MAX ||
        (flags & ~(ASTRA_MENU_ATTACHED_TOP | ASTRA_MENU_TRACK_OPEN_PRESS)) != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < count; ++at) {
        const AstraMenuItem *item = &items[at];

        if ((item->flags & ~(ASTRA_MENU_DISABLED | ASTRA_MENU_CHECKED |
                             ASTRA_MENU_SEPARATOR)) != 0u)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if ((item->flags & ASTRA_MENU_SEPARATOR) != 0u) {
            if (item->flags != ASTRA_MENU_SEPARATOR || item->label != NULL ||
                item->label_length != 0u || at == 0u || at + 1u == count ||
                (items[at - 1u].flags & ASTRA_MENU_SEPARATOR) != 0u)
                return ASTRA_ERROR_INVALID_ARGUMENT;
        } else {
            uint64_t preferred;
            if (item->label_length == 0u ||
                !astra_interface_single_line_text_valid(item->label, item->label_length))
                return ASTRA_ERROR_INVALID_ARGUMENT;
            preferred = (uint64_t)astra_surface_ui_text_width(item->label,
                item->label_length, ASTRA_THEME_SYSTEM_MENU_FONT_HEIGHT) + 44u;
            if (preferred > width) width = preferred;
            ++rows;
        }
        height += pitch(item);
    }
    if (rows == 0u || height > INT32_MAX)
        return ASTRA_ERROR_NO_RESOURCES;
    candidate._private_items = items;
    candidate._private_count = count;
    candidate._private_content_height = (uint32_t)height;
    candidate._private_frame.width = (uint32_t)(width > work->width ? work->width : width);
    candidate._private_frame.height = (uint32_t)(height > work->height ? work->height : height);
    candidate._private_frame.x = fitted(x, candidate._private_frame.width, work->x, work->width);
    candidate._private_frame.y = fitted(y, candidate._private_frame.height, work->y, work->height);
    candidate._private_flags = flags;
    candidate._private_pressed = (flags & ASTRA_MENU_TRACK_OPEN_PRESS) != 0u ? OPEN_PRESS : NONE;
    candidate._private_active = 1u;
    candidate._private_revision = 1u;
    *menu = candidate;
    return ASTRA_OK;
}

AstraResult astra_interface_menu_get_state(const AstraMenu *menu, AstraMenuState *state)
{
    if (!valid(menu) || state == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *state = (AstraMenuState){menu->_private_frame,
        menu->_private_selected == NONE ? 0u : menu->_private_selected + 1u,
        menu->_private_scroll, menu->_private_revision, menu->_private_active};
    return ASTRA_OK;
}

uint32_t astra_interface_menu_hit(const AstraMenu *menu, int32_t x, int32_t y)
{
    int64_t local_x, local_y, top;

    if (!valid(menu) || menu->_private_active == 0u)
        return 0u;
    local_x = (int64_t)x - menu->_private_frame.x;
    local_y = (int64_t)y - menu->_private_frame.y;
    if (local_x < PAD || local_x >= menu->_private_frame.width - PAD ||
        local_y < PAD || local_y >= menu->_private_frame.height - PAD)
        return 0u;
    top = (int64_t)PAD - menu->_private_scroll;
    for (uint32_t at = 0u; at < menu->_private_count; ++at) {
        const AstraMenuItem *item = &menu->_private_items[at];
        if (local_y >= top && local_y < top + ROW && enabled(item))
            return at + 1u;
        top += pitch(item);
    }
    return 0u;
}

static void revision(AstraMenu *menu)
{
    if (++menu->_private_revision == 0u) ++menu->_private_revision;
}

static void select_row(AstraMenu *menu, uint32_t index)
{
    uint32_t top = PAD;
    uint32_t scroll = menu->_private_scroll;

    if (index != NONE) {
        for (uint32_t at = 0u; at < index; ++at)
            top += pitch(&menu->_private_items[at]);
        if (top < scroll + PAD)
            scroll = top - PAD;
        else if ((uint64_t)top + ROW > scroll + menu->_private_frame.height - PAD)
            scroll = top + ROW + PAD - menu->_private_frame.height;
    }
    if (menu->_private_selected != index || menu->_private_scroll != scroll) {
        menu->_private_selected = index;
        menu->_private_scroll = scroll;
        revision(menu);
    }
}

static uint32_t next_row(const AstraMenu *menu, uint32_t start, int backwards, int wrap)
{
    uint32_t at = start;
    for (uint32_t visited = 0u; visited < menu->_private_count; ++visited) {
        if (!wrap && at != NONE && (backwards ? at == 0u : at + 1u == menu->_private_count))
            return start;
        at = at == NONE ? (backwards ? menu->_private_count - 1u : 0u) :
            backwards ? (at == 0u ? menu->_private_count - 1u : at - 1u) :
            at + 1u == menu->_private_count ? 0u : at + 1u;
        if (enabled(&menu->_private_items[at])) return at;
    }
    return NONE;
}

static void finish(AstraMenu *menu, AstraUIAction *action, uint32_t selection)
{
    menu->_private_active = 0u;
    menu->_private_pressed = NONE;
    action->type = selection == 0u ? ASTRA_UI_ACTION_DISMISS : ASTRA_UI_ACTION_ACTIVATE;
    action->control_id = selection;
    revision(menu);
}

AstraResult astra_interface_menu_handle_event(AstraMenu *menu,
    const AstraWindowEvent *event, AstraUIAction *action)
{
    uint32_t hit;

    if (!valid(menu) || event == NULL || event->size < sizeof(*event) ||
        event->version != ASTRA_WINDOW_EVENT_VERSION || action == NULL ||
        action->size < sizeof(*action))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *action = (AstraUIAction)ASTRA_UI_ACTION_INIT;
    if (menu->_private_active == 0u) return ASTRA_OK;
    if ((event->flags & ASTRA_WINDOW_EVENT_LOSS) != 0u ||
        event->type == ASTRA_WINDOW_EVENT_STATE_RESET) {
        finish(menu, action, 0u);
        return ASTRA_OK;
    }
    if (event->type == ASTRA_WINDOW_EVENT_KEY) {
        uint32_t usage = event->data.key.usage;
        if ((event->flags & ASTRA_WINDOW_EVENT_DOWN) == 0u) return ASTRA_OK;
        if (usage == 0x29u) finish(menu, action, 0u);
        else if (usage == 0x51u || usage == 0x52u)
            select_row(menu, next_row(menu, menu->_private_selected, usage == 0x52u, 1));
        else if (usage == 0x4au || usage == 0x4du)
            select_row(menu, next_row(menu, NONE, usage == 0x4du, 1));
        else if (usage == 0x4bu || usage == 0x4eu) {
            uint32_t steps = (menu->_private_frame.height - 2u * PAD) / PITCH;
            uint32_t at = menu->_private_selected;
            if (steps == 0u) steps = 1u;
            while (steps-- != 0u) at = next_row(menu, at, usage == 0x4bu, 0);
            select_row(menu, at);
        } else if ((usage == 0x28u || usage == 0x2cu) &&
                   (event->flags & ASTRA_WINDOW_EVENT_REPEAT) == 0u &&
                   menu->_private_selected != NONE)
            finish(menu, action, menu->_private_selected + 1u);
        return ASTRA_OK;
    }
    if (event->type == ASTRA_WINDOW_EVENT_POINTER_WHEEL) {
        int64_t offset = (int64_t)menu->_private_scroll -
                        (int64_t)event->data.wheel.delta_y * PITCH;
        uint32_t maximum = menu->_private_content_height - menu->_private_frame.height;
        if (offset < 0) offset = 0;
        if ((uint64_t)offset > maximum) offset = maximum;
        if ((uint32_t)offset != menu->_private_scroll) {
            menu->_private_scroll = (uint32_t)offset;
            menu->_private_selected = NONE;
            revision(menu);
        }
        return ASTRA_OK;
    }
    if (event->type != ASTRA_WINDOW_EVENT_POINTER_MOTION &&
        event->type != ASTRA_WINDOW_EVENT_POINTER_BUTTON) return ASTRA_OK;
    hit = astra_interface_menu_hit(menu, event->data.pointer.screen_x, event->data.pointer.screen_y);
    if (menu->_private_selected != (hit == 0u ? NONE : hit - 1u)) {
        menu->_private_selected = hit == 0u ? NONE : hit - 1u;
        revision(menu);
    }
    if (event->type == ASTRA_WINDOW_EVENT_POINTER_MOTION) return ASTRA_OK;
    if ((event->flags & ASTRA_WINDOW_EVENT_DOWN) != 0u) {
        int64_t x = (int64_t)event->data.pointer.screen_x - menu->_private_frame.x;
        int64_t y = (int64_t)event->data.pointer.screen_y - menu->_private_frame.y;
        if (x < 0 || y < 0 || x >= menu->_private_frame.width || y >= menu->_private_frame.height)
            finish(menu, action, 0u);
        else if (event->data.pointer.button == ASTRA_INPUT_BUTTON_LEFT)
            menu->_private_pressed = hit == 0u ? NONE : hit - 1u;
    } else if (event->data.pointer.button == ASTRA_INPUT_BUTTON_LEFT) {
        uint32_t pressed = menu->_private_pressed;
        menu->_private_pressed = NONE;
        if (hit != 0u && (pressed == OPEN_PRESS || pressed == hit - 1u))
            finish(menu, action, hit);
    }
    return ASTRA_OK;
}

static uint16_t color(AstraColorRGBA8 value)
{
    return astra_surface_rgb565(value.red, value.green, value.blue);
}

AstraResult astra_interface_menu_render(const AstraMenu *menu, AstraSurfaceView *surface)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraSurfaceView target;
    uint64_t commands = 8u, payload = 0u;
    int64_t top;

    if (!valid(menu) || surface == NULL || surface->pixels == NULL ||
        surface->width != menu->_private_frame.width ||
        surface->height != menu->_private_frame.height ||
        surface->clip_left > surface->clip_right || surface->clip_right > surface->width ||
        surface->clip_top > surface->clip_bottom || surface->clip_bottom > surface->height)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (surface->kind != ASTRA_SURFACE_VIEW_DRAW_LIST)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!astra_draw_list_view_adopt(&target, surface->pixels,
                                    surface->byte_size, surface->width,
                                    surface->height))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    top = (int64_t)PAD - menu->_private_scroll;
    for (uint32_t at = 0u; at < menu->_private_count; ++at) {
        const AstraMenuItem *item = &menu->_private_items[at];
        if (top < surface->height - PAD && top + pitch(item) > PAD) {
            commands += 4u;
            if (item->label_length != 0u)
                payload += astra_surface_ui_text_fit(item->label, item->label_length,
                    ASTRA_THEME_SYSTEM_MENU_FONT_HEIGHT, surface->width - 40u);
        }
        top += pitch(item);
    }
    if (!astra_interface_draw_list_has_room(surface, commands, payload))
        return ASTRA_ERROR_NO_RESOURCES;
    target = *surface;
    astra_surface_fill_round(&target, 0, 0, target.width, target.height,
                            theme.window_radius, color(theme.frame));
    astra_surface_fill_round(&target, 2, 2, target.width - 4u, target.height - 4u,
                            theme.window_radius - 2u, color(theme.title_inactive));
    if ((menu->_private_flags & ASTRA_MENU_ATTACHED_TOP) != 0u) {
        astra_surface_fill(&target, 0, 0, target.width, theme.window_radius, color(theme.frame));
        astra_surface_fill(&target, 2, 2, target.width - 4u,
                           theme.window_radius - 2u, color(theme.title_inactive));
    }
    (void)astra_surface_clip(&target, PAD, PAD, target.width - 2u * PAD, target.height - 2u * PAD);
    top = (int64_t)PAD - menu->_private_scroll;
    for (uint32_t at = 0u; at < menu->_private_count; ++at) {
        const AstraMenuItem *item = &menu->_private_items[at];
        if (top < surface->height - PAD && top + pitch(item) > PAD) {
            int32_t y = (int32_t)top;
            if ((item->flags & ASTRA_MENU_SEPARATOR) != 0u)
                astra_surface_fill(&target, 12, y + 4, target.width - 24u, 1, color(theme.border_soft));
            else {
                int selected = at == menu->_private_selected && enabled(item);
                uint16_t ink = color((item->flags & ASTRA_MENU_DISABLED) != 0u ?
                    theme.text_muted : selected ? theme.accent_text : theme.text_primary);
                uint32_t bytes = astra_surface_ui_text_fit(item->label, item->label_length,
                    ASTRA_THEME_SYSTEM_MENU_FONT_HEIGHT, target.width - 40u);
                if (selected)
                    astra_surface_fill_round(&target, PAD, y, target.width - 2u * PAD,
                                             ROW, theme.control_radius, color(theme.accent));
                if ((item->flags & ASTRA_MENU_CHECKED) != 0u) {
                    if (!astra_surface_line(&target, 12, y + 16, 16, y + 20, ink) ||
                        !astra_surface_line(&target, 16, y + 20, 22, y + 12, ink))
                        return ASTRA_ERROR_NO_RESOURCES;
                }
                astra_surface_ui_text(&target, 28, y + 9, item->label, bytes,
                                      ASTRA_THEME_SYSTEM_MENU_FONT_HEIGHT, ink);
            }
        }
        top += pitch(item);
    }
    return ASTRA_OK;
}
