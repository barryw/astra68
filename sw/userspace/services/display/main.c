#include <astra/display.h>
#include <astra/gui.h>
#include <astra/input_service.h>
#include <astra/program.h>
#include <astra/render_builder.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/surface.h>
#include <astra/theme.h>
#include <astra/window.h>

#define DISPLAY_WINDOW_MAX 4u
#define DISPLAY_WORK_TOP 34u
#define DISPLAY_WORK_BOTTOM 678u
#define WINDOW_CACHE_BASE UINT32_C(0x01000000)
#define WINDOW_CACHE_STRIDE UINT32_C(0x00200000)
#define WINDOW_CONTENT_BASE UINT32_C(0x02000000)
#define WINDOW_CONTENT_STRIDE UINT32_C(0x00200000)
#define DISPLAY_IRQ_DRAIN_MAX 8u
#define DISPLAY_INPUT_QUEUE 8u

typedef struct DamageRect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
    uint32_t valid;
} DamageRect;

typedef struct DisplayWindow {
    AstraGuiOpenWindow request;
    AstraSharedSurface surface;
    uint32_t id;
    uint32_t generation;
    uint32_t control_receive;
    uint32_t event_send;
    uint32_t event_sequence;
    uint32_t cache_slot;
    uint16_t restore_x;
    uint16_t restore_y;
    uint16_t restore_width;
    uint16_t restore_height;
    uint8_t state;
    uint8_t restore_state;
    uint8_t cache_dirty;
    uint8_t content_dirty;
    uint8_t content_initialized;
    uint8_t event_lost;
    uint8_t reserved8[2];
    DamageRect content_damage;
} DisplayWindow;

typedef struct DisplayState {
    DisplayWindow windows[DISPLAY_WINDOW_MAX];
    DamageRect damage[2];
    uint32_t count;
    uint32_t next_id;
    int32_t pointer_x;
    int32_t pointer_y;
    uint32_t capture_window;
    uint32_t capture_region;
    int32_t capture_dx;
    int32_t capture_dy;
    uint16_t capture_x;
    uint16_t capture_y;
    uint16_t capture_width;
    uint16_t capture_height;
} DisplayState;

enum {
    HIT_NONE = 0u,
    HIT_CONTENT,
    HIT_TITLE,
    HIT_MINIMIZE,
    HIT_MAXIMIZE,
    HIT_CLOSE,
    HIT_RESIZE_N,
    HIT_RESIZE_NE,
    HIT_RESIZE_E,
    HIT_RESIZE_SE,
    HIT_RESIZE_S,
    HIT_RESIZE_SW,
    HIT_RESIZE_W,
    HIT_RESIZE_NW,
};

enum {
    DISPLAY_POINTER_CURSOR = 1u << 0,
    DISPLAY_POINTER_RENDER = 1u << 1,
    DISPLAY_POINTER_FRAME = 1u << 2,
};

enum {
    DISPLAY_FAIL_ARM = ASTRA_STATUS_PROGRAM_FIRST,
    DISPLAY_FAIL_SUBMIT,
    DISPLAY_FAIL_WAIT,
    DISPLAY_FAIL_IRQ,
    DISPLAY_FAIL_COMPLETION,
    DISPLAY_FAIL_PROTOCOL,
};

ASTRA_PROGRAM("display", 0, 3, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static const AstraStartupCapability *
capability(const AstraStartupInfo *startup, const char *name)
{
    const AstraStartupCapability *entries =
        (const AstraStartupCapability *)(uintptr_t)
            startup->capabilities_address;

    for (uint32_t index = 0u; index < startup->capability_count; ++index)
        if (astra_capability_name_equal(entries[index].name, name))
            return &entries[index];
    return NULL;
}

static uint32_t ready(uint32_t bootstrap, uint32_t status, uint32_t gui)
{
    AstraServiceReady message = {0};

    message.header.total_size = sizeof(message);
    message.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    message.header.protocol = ASTRA_SERVICE_PROTOCOL;
    message.header.protocol_version = ASTRA_SERVICE_VERSION;
    message.header.operation = ASTRA_SERVICE_READY;
    message.status = status;
    return astra_port_send(bootstrap, &message, sizeof(message),
                           status == ASTRA_STATUS_OK ? &gui : NULL,
                           status == ASTRA_STATUS_OK ? 1u : 0u);
}

static uint16_t color(AstraColorRGBA8 value)
{
    return astra_surface_rgb565(value.red, value.green, value.blue);
}

static uint32_t service_status(uint32_t status)
{
    switch (status) {
    case ASTRA_SYSCALL_OK: return ASTRA_STATUS_OK;
    case ASTRA_SYSCALL_INVALID_ARGUMENT:
    case ASTRA_SYSCALL_BAD_ADDRESS: return ASTRA_STATUS_INVALID;
    case ASTRA_SYSCALL_INVALID_HANDLE: return ASTRA_STATUS_BAD_HANDLE;
    case ASTRA_SYSCALL_ACCESS_DENIED: return ASTRA_STATUS_ACCESS;
    case ASTRA_SYSCALL_RESOURCE_LIMIT:
    case ASTRA_SYSCALL_OUT_OF_MEMORY: return ASTRA_STATUS_LIMIT;
    case ASTRA_SYSCALL_PEER_DEAD:
    case ASTRA_SYSCALL_CLOSED:
    case ASTRA_SYSCALL_CANCELLED: return ASTRA_STATUS_PEER_DEAD;
    case ASTRA_SYSCALL_BUFFER_TOO_SMALL: return ASTRA_STATUS_BUFFER_TOO_SMALL;
    default: return ASTRA_STATUS_IO;
    }
}

static uint16_t title_height(const AstraTheme *theme, uint8_t type)
{
    if (type == ASTRA_WINDOW_POPOVER || type == ASTRA_WINDOW_FULLSCREEN)
        return 0u;
    return type == ASTRA_WINDOW_UTILITY ? theme->utility_titlebar_height :
                                         theme->titlebar_height;
}

static uint16_t frame_width(const AstraTheme *theme, uint8_t type)
{
    return type == ASTRA_WINDOW_FULLSCREEN ? 0u : theme->frame_width;
}

static uint16_t window_radius(const AstraTheme *theme,
                              const DisplayWindow *window)
{
    return window->state == ASTRA_WINDOW_STATE_MAXIMIZED ||
           window->request.type == ASTRA_WINDOW_FULLSCREEN ?
           0u : theme->window_radius;
}

static uint32_t outer_width(const AstraTheme *theme,
                            const DisplayWindow *window)
{
    return (uint32_t)window->request.width +
           frame_width(theme, window->request.type) * 2u;
}

static uint32_t outer_height(const AstraTheme *theme,
                             const DisplayWindow *window)
{
    uint16_t title = title_height(theme, window->request.type);
    uint16_t signal = title == 0u ? 0u : theme->signal_height;

    return (uint32_t)window->request.height + title + signal +
           frame_width(theme, window->request.type) * 2u;
}

static DamageRect bounds(const AstraTheme *theme,
                         const DisplayWindow *window)
{
    return (DamageRect){
        window->request.x, window->request.y,
        (int32_t)window->request.x + (int32_t)outer_width(theme, window),
        (int32_t)window->request.y + (int32_t)outer_height(theme, window),
        1u
    };
}

static int overlaps(const DamageRect *left, const DamageRect *right)
{
    return left->valid != 0u && right->valid != 0u &&
           left->left < right->right && left->right > right->left &&
           left->top < right->bottom && left->bottom > right->top;
}

static void damage_add(DamageRect *damage, DamageRect add)
{
    if (add.left < 0)
        add.left = 0;
    if (add.top < 0)
        add.top = 0;
    if (add.right > (int32_t)ASTRA_DISPLAY_WIDTH)
        add.right = ASTRA_DISPLAY_WIDTH;
    if (add.bottom > (int32_t)ASTRA_DISPLAY_HEIGHT)
        add.bottom = ASTRA_DISPLAY_HEIGHT;
    if (add.left >= add.right || add.top >= add.bottom)
        return;
    if (damage->valid == 0u) {
        *damage = add;
        damage->valid = 1u;
        return;
    }
    if (add.left < damage->left)
        damage->left = add.left;
    if (add.top < damage->top)
        damage->top = add.top;
    if (add.right > damage->right)
        damage->right = add.right;
    if (add.bottom > damage->bottom)
        damage->bottom = add.bottom;
}

static void damage_both(DisplayState *state, DamageRect add)
{
    damage_add(&state->damage[0], add);
    damage_add(&state->damage[1], add);
}

static void damage_window(DisplayState *state, const AstraTheme *theme,
                          const DisplayWindow *window)
{
    if (window->state != ASTRA_WINDOW_STATE_MINIMIZED)
        damage_both(state, bounds(theme, window));
}

static void damage_content(DisplayState *state, const AstraTheme *theme,
                           DisplayWindow *window, DamageRect damage)
{
    uint16_t frame = frame_width(theme, window->request.type);
    uint16_t title = title_height(theme, window->request.type);
    uint16_t signal = title == 0u ? 0u : theme->signal_height;
    DamageRect screen = damage;

    damage_add(&window->content_damage, damage);
    screen.left += window->request.x + frame;
    screen.right += window->request.x + frame;
    screen.top += window->request.y + frame + title + signal;
    screen.bottom += window->request.y + frame + title + signal;
    damage_both(state, screen);
}

static void reset_content(DisplayWindow *window)
{
    window->content_initialized = 0u;
    window->content_dirty = 1u;
    window->content_damage = (DamageRect){
        0, 0, window->request.width, window->request.height, 1u
    };
}

static void damage_scene(DisplayState *state, const AstraTheme *theme)
{
    for (uint32_t index = 0u; index < state->count; ++index)
        damage_window(state, theme, &state->windows[index]);
}

static void next_generation(DisplayWindow *window)
{
    if (++window->generation == 0u)
        window->generation = 1u;
}

static uint32_t send_event(DisplayWindow *window, AstraWindowEvent *event)
{
    AstraGuiWindowEvent message = {0};
    uint32_t status;
    uint32_t subscription =
        event->type == ASTRA_WINDOW_EVENT_POINTER_MOTION ?
            ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION :
        event->type == ASTRA_WINDOW_EVENT_POINTER_BUTTON ?
            ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON :
        event->type == ASTRA_WINDOW_EVENT_POINTER_WHEEL ?
            ASTRA_WINDOW_SUBSCRIBE_POINTER_WHEEL :
        event->type == ASTRA_WINDOW_EVENT_FOCUS ?
            ASTRA_WINDOW_SUBSCRIBE_FOCUS :
        event->type == ASTRA_WINDOW_EVENT_FRAME ?
            ASTRA_WINDOW_SUBSCRIBE_FRAME :
        event->type == ASTRA_WINDOW_EVENT_CLOSE_REQUEST ?
            ASTRA_WINDOW_SUBSCRIBE_CLOSE_REQUEST :
        event->type == ASTRA_WINDOW_EVENT_KEY ?
            ASTRA_WINDOW_SUBSCRIBE_KEY :
        event->type == ASTRA_WINDOW_EVENT_TEXT ?
            ASTRA_WINDOW_SUBSCRIBE_TEXT : 0u;

    if (subscription != 0u &&
        (window->request.event_mask & subscription) == 0u)
        return ASTRA_SYSCALL_OK;
    if (++window->event_sequence == 0u)
        ++window->event_sequence;
    event->size = sizeof(*event);
    event->version = ASTRA_WINDOW_EVENT_VERSION;
    event->sequence = window->event_sequence;
    event->generation = window->generation;
    if (window->event_lost != 0u)
        event->flags |= ASTRA_WINDOW_EVENT_LOSS;
    message.header.total_size = sizeof(message);
    message.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    message.header.protocol = ASTRA_GUI_PROTOCOL;
    message.header.protocol_version = ASTRA_GUI_VERSION;
    message.header.operation = ASTRA_GUI_WINDOW_EVENT;
    message.header.transaction_id = event->sequence;
    message.event = *event;
    status = astra_port_send(window->event_send, &message, sizeof(message),
                             NULL, 0u);
    if (status == ASTRA_SYSCALL_OK)
        window->event_lost = 0u;
    else
        window->event_lost = 1u;
    return status;
}

static void focus_event(DisplayWindow *window, uint32_t timestamp_ms,
                        uint32_t focused)
{
    AstraWindowEvent event = {
        .type = ASTRA_WINDOW_EVENT_FOCUS,
        .flags = focused != 0u ? ASTRA_WINDOW_EVENT_FOCUSED : 0u,
        .timestamp_ms = timestamp_ms,
    };

    (void)send_event(window, &event);
}

static void frame_event(DisplayWindow *window, uint32_t timestamp_ms,
                        uint32_t z_order)
{
    AstraWindowEvent event = {
        .type = ASTRA_WINDOW_EVENT_FRAME,
        .timestamp_ms = timestamp_ms,
    };

    event.data.frame.frame = (AstraWindowFrame){
        window->request.x, window->request.y,
        window->request.width, window->request.height
    };
    event.data.frame.state = window->state;
    event.data.frame.z_order = z_order;
    (void)send_event(window, &event);
}

static uint32_t find_id(const DisplayState *state, uint32_t id)
{
    for (uint32_t index = 0u; index < state->count; ++index)
        if (state->windows[index].id == id)
            return index;
    return state->count;
}

static uint32_t top_visible(const DisplayState *state)
{
    for (uint32_t index = state->count; index != 0u; --index)
        if (state->windows[index - 1u].state !=
            ASTRA_WINDOW_STATE_MINIMIZED)
            return state->windows[index - 1u].id;
    return 0u;
}

static void reorder(DisplayState *state, const AstraTheme *theme,
                    uint32_t from, uint32_t to)
{
    DisplayWindow moved;
    uint32_t first;
    uint32_t last;

    if (from == to)
        return;
    damage_scene(state, theme);
    moved = state->windows[from];
    if (from < to)
        for (uint32_t index = from; index < to; ++index)
            state->windows[index] = state->windows[index + 1u];
    else
        for (uint32_t index = from; index > to; --index)
            state->windows[index] = state->windows[index - 1u];
    state->windows[to] = moved;
    first = from < to ? from : to;
    last = from > to ? from : to;
    for (uint32_t index = first; index <= last; ++index)
        next_generation(&state->windows[index]);
    damage_scene(state, theme);
}

static void activate(DisplayState *state, const AstraTheme *theme,
                     uint32_t id, int raise, uint32_t timestamp_ms)
{
    uint32_t index = find_id(state, id);

    if (id != 0u && index == state->count)
        return;
    if (id != 0u && raise) {
        reorder(state, theme, index, state->count - 1u);
        index = state->count - 1u;
    }
    for (uint32_t at = 0u; at < state->count; ++at) {
        DisplayWindow *window = &state->windows[at];
        uint32_t active = window->id == id &&
                          window->state != ASTRA_WINDOW_STATE_MINIMIZED;

        if (((window->request.flags & ASTRA_WINDOW_ACTIVE) != 0u) == active)
            continue;
        damage_window(state, theme, window);
        if (active)
            window->request.flags |= ASTRA_WINDOW_ACTIVE;
        else
            window->request.flags &= ~ASTRA_WINDOW_ACTIVE;
        window->cache_dirty = 1u;
        next_generation(window);
        focus_event(window, timestamp_ms, active);
        damage_window(state, theme, window);
    }
}

static int frame_valid(const AstraTheme *theme,
                       const DisplayWindow *window, uint16_t x, uint16_t y,
                       uint16_t width, uint16_t height)
{
    DisplayWindow candidate = *window;
    uint32_t right;
    uint32_t bottom;

    candidate.request.x = x;
    candidate.request.y = y;
    candidate.request.width = width;
    candidate.request.height = height;
    right = (uint32_t)x + outer_width(theme, &candidate);
    bottom = (uint32_t)y + outer_height(theme, &candidate);
    return width != 0u && height != 0u && x < ASTRA_DISPLAY_WIDTH &&
           y >= DISPLAY_WORK_TOP && right <= ASTRA_DISPLAY_WIDTH &&
           bottom <= DISPLAY_WORK_BOTTOM;
}

static int rounded_contains(uint32_t width, uint32_t height, uint16_t radius,
                            int32_t x, int32_t y)
{
    int32_t center_x;
    int32_t center_y;
    int32_t dx;
    int32_t dy;

    if (x < 0 || y < 0 || x >= (int32_t)width || y >= (int32_t)height)
        return 0;
    if (radius == 0u ||
        (x >= radius && x < (int32_t)width - radius) ||
        (y >= radius && y < (int32_t)height - radius))
        return 1;
    center_x = x < radius ? (int32_t)radius - 1 :
                            (int32_t)width - radius;
    center_y = y < radius ? (int32_t)radius - 1 :
                            (int32_t)height - radius;
    dx = x - center_x;
    dy = y - center_y;
    return dx * dx + dy * dy < (int32_t)radius * radius;
}

static uint32_t hit_region(const AstraTheme *theme,
                           const DisplayWindow *window,
                           int32_t screen_x, int32_t screen_y)
{
    uint16_t frame = frame_width(theme, window->request.type);
    uint16_t title = title_height(theme, window->request.type);
    uint16_t signal = title == 0u ? 0u : theme->signal_height;
    int32_t x = screen_x - window->request.x;
    int32_t y = screen_y - window->request.y;
    uint32_t gadget_count = 0u;
    int32_t gadget_x;
    int32_t gadget_y;
    int32_t width = (int32_t)outer_width(theme, window);
    int32_t height = (int32_t)outer_height(theme, window);
    int left;
    int right;
    int top;
    int bottom;

    if (window->state == ASTRA_WINDOW_STATE_MINIMIZED ||
        !rounded_contains((uint32_t)width, (uint32_t)height,
                          window_radius(theme, window), x, y))
        return HIT_NONE;
    if ((window->request.flags & ASTRA_WINDOW_RESIZABLE) != 0u &&
        window->state == ASTRA_WINDOW_STATE_NORMAL) {
        left = x < theme->resize_hit;
        right = x >= width - theme->resize_hit;
        top = y < theme->resize_hit;
        bottom = y >= height - theme->resize_hit;
        if (top && left)
            return HIT_RESIZE_NW;
        if (top && right)
            return HIT_RESIZE_NE;
        if (bottom && left)
            return HIT_RESIZE_SW;
        if (bottom && right)
            return HIT_RESIZE_SE;
        if (top)
            return HIT_RESIZE_N;
        if (right)
            return HIT_RESIZE_E;
        if (bottom)
            return HIT_RESIZE_S;
        if (left)
            return HIT_RESIZE_W;
    }
    if (title == 0u)
        return HIT_CONTENT;
    if (y < frame || y >= (int32_t)frame + title)
        return y >= (int32_t)frame + title + signal ?
               HIT_CONTENT : HIT_NONE;
    if ((window->request.gadgets & ASTRA_WINDOW_GADGET_MINIMIZE) != 0u)
        ++gadget_count;
    if ((window->request.gadgets & ASTRA_WINDOW_GADGET_MAXIMIZE) != 0u)
        ++gadget_count;
    if ((window->request.gadgets & ASTRA_WINDOW_GADGET_CLOSE) != 0u)
        ++gadget_count;
    gadget_x = frame + window->request.width - theme->spacing_unit -
               (int32_t)gadget_count * theme->gadget_extent;
    gadget_y = frame + ((int32_t)title - theme->gadget_extent) / 2;
#define HIT_GADGET(flag, state, region) do { \
        if ((window->request.gadgets & (flag)) != 0u) { \
            if (x >= gadget_x && x < gadget_x + theme->gadget_extent && \
                y >= gadget_y && y < gadget_y + theme->gadget_extent && \
                (state) != ASTRA_GADGET_DISABLED) \
                return (region); \
            gadget_x += theme->gadget_extent; \
        } \
    } while (0)
    HIT_GADGET(ASTRA_WINDOW_GADGET_MINIMIZE,
               window->request.minimize_state, HIT_MINIMIZE);
    HIT_GADGET(ASTRA_WINDOW_GADGET_MAXIMIZE,
               window->request.maximize_state, HIT_MAXIMIZE);
    HIT_GADGET(ASTRA_WINDOW_GADGET_CLOSE,
               window->request.close_state, HIT_CLOSE);
#undef HIT_GADGET
    return HIT_TITLE;
}

static uint32_t hit_test(const DisplayState *state, const AstraTheme *theme,
                         int32_t x, int32_t y, uint32_t *region)
{
    for (uint32_t index = state->count; index != 0u; --index) {
        uint32_t found = hit_region(theme, &state->windows[index - 1u], x, y);

        if (found != HIT_NONE) {
            *region = found;
            return index - 1u;
        }
    }
    *region = HIT_NONE;
    return state->count;
}

static uint8_t *gadget_state(DisplayWindow *window, uint32_t region)
{
    if (region == HIT_MINIMIZE)
        return &window->request.minimize_state;
    if (region == HIT_MAXIMIZE)
        return &window->request.maximize_state;
    if (region == HIT_CLOSE)
        return &window->request.close_state;
    return NULL;
}

static int set_gadget_visual(DisplayState *state, const AstraTheme *theme,
                             DisplayWindow *window, uint32_t region,
                             uint8_t visual)
{
    uint8_t *current = gadget_state(window, region);

    if (current == NULL || *current == ASTRA_GADGET_DISABLED ||
        *current == visual)
        return 0;
    damage_window(state, theme, window);
    *current = visual;
    window->cache_dirty = 1u;
    damage_window(state, theme, window);
    return 1;
}

static int update_hover(DisplayState *state, const AstraTheme *theme,
                        uint32_t window_id, uint32_t region)
{
    int changed = 0;

    for (uint32_t index = 0u; index < state->count; ++index) {
        DisplayWindow *window = &state->windows[index];

        for (uint32_t gadget = HIT_MINIMIZE; gadget <= HIT_CLOSE; ++gadget)
            changed |= set_gadget_visual(
                state, theme, window, gadget,
                window->id == window_id && gadget == region ?
                    ASTRA_GADGET_HOVER : ASTRA_GADGET_NORMAL);
    }
    return changed;
}

static int move_captured_window(DisplayState *state, const AstraTheme *theme,
                                DisplayWindow *window,
                                int32_t pointer_x, int32_t pointer_y)
{
    int32_t maximum_x = (int32_t)ASTRA_DISPLAY_WIDTH -
                        (int32_t)outer_width(theme, window);
    int32_t maximum_y = (int32_t)DISPLAY_WORK_BOTTOM -
                        (int32_t)outer_height(theme, window);
    int32_t x = pointer_x - state->capture_dx;
    int32_t y = pointer_y - state->capture_dy;

    if (window->state != ASTRA_WINDOW_STATE_NORMAL)
        return 0;
    if (x < 0)
        x = 0;
    if (x > maximum_x)
        x = maximum_x;
    if (y < (int32_t)DISPLAY_WORK_TOP)
        y = DISPLAY_WORK_TOP;
    if (y > maximum_y)
        y = maximum_y;
    if (window->request.x == (uint16_t)x &&
        window->request.y == (uint16_t)y)
        return 0;
    damage_window(state, theme, window);
    window->request.x = (uint16_t)x;
    window->request.y = (uint16_t)y;
    next_generation(window);
    damage_window(state, theme, window);
    return 1;
}

static int resize_left(uint32_t region)
{
    return region == HIT_RESIZE_W || region == HIT_RESIZE_NW ||
           region == HIT_RESIZE_SW;
}

static int resize_right(uint32_t region)
{
    return region == HIT_RESIZE_E || region == HIT_RESIZE_NE ||
           region == HIT_RESIZE_SE;
}

static int resize_top(uint32_t region)
{
    return region == HIT_RESIZE_N || region == HIT_RESIZE_NW ||
           region == HIT_RESIZE_NE;
}

static int resize_bottom(uint32_t region)
{
    return region == HIT_RESIZE_S || region == HIT_RESIZE_SW ||
           region == HIT_RESIZE_SE;
}

static int resize_region(uint32_t region)
{
    return resize_left(region) || resize_right(region) ||
           resize_top(region) || resize_bottom(region);
}

static int resize_captured_window(DisplayState *state,
                                  const AstraTheme *theme,
                                  DisplayWindow *window,
                                  int32_t pointer_x, int32_t pointer_y)
{
    uint16_t frame = frame_width(theme, window->request.type);
    uint16_t title = title_height(theme, window->request.type);
    int32_t chrome_width = (int32_t)frame * 2;
    int32_t chrome_height = (int32_t)frame * 2 + title +
                            (title == 0u ? 0 : theme->signal_height);
    int32_t minimum_width = title == 0u ? 1 : 96;
    int32_t minimum_height = 1;
    int32_t x = state->capture_x;
    int32_t y = state->capture_y;
    int32_t width = state->capture_width;
    int32_t height = state->capture_height;
    int32_t dx = pointer_x - state->capture_dx;
    int32_t dy = pointer_y - state->capture_dy;

    if (window->state != ASTRA_WINDOW_STATE_NORMAL)
        return 0;
    if (resize_left(state->capture_region)) {
        if (dx < -(int32_t)state->capture_x)
            dx = -(int32_t)state->capture_x;
        if (dx > (int32_t)state->capture_width - minimum_width)
            dx = (int32_t)state->capture_width - minimum_width;
        x += dx;
        width -= dx;
    } else if (resize_right(state->capture_region)) {
        width += dx;
        if (width < minimum_width)
            width = minimum_width;
        if (x + width + chrome_width > (int32_t)ASTRA_DISPLAY_WIDTH)
            width = (int32_t)ASTRA_DISPLAY_WIDTH - x - chrome_width;
    }
    if (resize_top(state->capture_region)) {
        if (dy < (int32_t)DISPLAY_WORK_TOP - (int32_t)state->capture_y)
            dy = (int32_t)DISPLAY_WORK_TOP - (int32_t)state->capture_y;
        if (dy > (int32_t)state->capture_height - minimum_height)
            dy = (int32_t)state->capture_height - minimum_height;
        y += dy;
        height -= dy;
    } else if (resize_bottom(state->capture_region)) {
        height += dy;
        if (height < minimum_height)
            height = minimum_height;
        if (y + height + chrome_height > (int32_t)DISPLAY_WORK_BOTTOM)
            height = (int32_t)DISPLAY_WORK_BOTTOM - y - chrome_height;
    }
    if (window->request.x == (uint16_t)x &&
        window->request.y == (uint16_t)y &&
        window->request.width == (uint16_t)width &&
        window->request.height == (uint16_t)height)
        return 0;
    damage_window(state, theme, window);
    window->request.x = (uint16_t)x;
    window->request.y = (uint16_t)y;
    window->request.width = (uint16_t)width;
    window->request.height = (uint16_t)height;
    window->cache_dirty = 1u;
    reset_content(window);
    next_generation(window);
    damage_window(state, theme, window);
    return 1;
}

static void pointer_event(DisplayWindow *window, const AstraTheme *theme,
                          uint16_t type, uint32_t flags,
                          uint32_t timestamp_ms, int32_t screen_x,
                          int32_t screen_y, uint32_t button)
{
    uint16_t frame = frame_width(theme, window->request.type);
    uint16_t title = title_height(theme, window->request.type);
    uint16_t signal = title == 0u ? 0u : theme->signal_height;
    AstraWindowEvent event = {
        .type = type,
        .flags = flags,
        .timestamp_ms = timestamp_ms,
    };

    event.data.pointer.x = screen_x - window->request.x - frame;
    event.data.pointer.y = screen_y - window->request.y - frame - title -
                           signal;
    event.data.pointer.screen_x = screen_x;
    event.data.pointer.screen_y = screen_y;
    event.data.pointer.button = button;
    (void)send_event(window, &event);
}

static void wheel_event(DisplayWindow *window, const AstraTheme *theme,
                        uint32_t timestamp_ms, int32_t screen_x,
                        int32_t screen_y, int32_t dx, int32_t dy)
{
    uint16_t frame = frame_width(theme, window->request.type);
    uint16_t title = title_height(theme, window->request.type);
    uint16_t signal = title == 0u ? 0u : theme->signal_height;
    AstraWindowEvent event = {
        .type = ASTRA_WINDOW_EVENT_POINTER_WHEEL,
        .timestamp_ms = timestamp_ms,
    };

    event.data.wheel.x = screen_x - window->request.x - frame;
    event.data.wheel.y = screen_y - window->request.y - frame - title -
                         signal;
    event.data.wheel.screen_x = screen_x;
    event.data.wheel.screen_y = screen_y;
    event.data.wheel.delta_x = dx;
    event.data.wheel.delta_y = dy;
    (void)send_event(window, &event);
}

static uint32_t active_window(const DisplayState *state)
{
    for (uint32_t index = state->count; index != 0u; --index)
        if ((state->windows[index - 1u].request.flags &
             ASTRA_WINDOW_ACTIVE) != 0u &&
            state->windows[index - 1u].state !=
                ASTRA_WINDOW_STATE_MINIMIZED)
            return index - 1u;
    return state->count;
}

static void key_event(DisplayWindow *window,
                      const AstraLogicalInputEvent *input)
{
    uint32_t flags =
        (input->flags & ASTRA_INPUT_LOGICAL_DOWN) != 0u ?
            ASTRA_WINDOW_EVENT_DOWN : 0u;
    AstraWindowEvent event = {
        .type = input->type == ASTRA_INPUT_EVENT_KEY ?
                    ASTRA_WINDOW_EVENT_KEY : ASTRA_WINDOW_EVENT_TEXT,
        .timestamp_ms = input->timestamp_ms,
    };

    if ((input->flags & ASTRA_INPUT_LOGICAL_REPEAT) != 0u)
        flags |= ASTRA_WINDOW_EVENT_REPEAT;
    if ((input->flags & ASTRA_INPUT_LOGICAL_SYNTHETIC) != 0u)
        flags |= ASTRA_WINDOW_EVENT_SYNTHETIC;
    event.flags = flags;
    if (input->type == ASTRA_INPUT_EVENT_KEY) {
        event.data.key.usage = input->code;
        event.data.key.modifiers = (uint32_t)input->value_x;
    } else {
        event.data.text.codepoint = input->code;
        event.data.text.modifiers = (uint32_t)input->value_x;
    }
    (void)send_event(window, &event);
}

static void gadget_glyph(AstraRenderBuilder *builder, uint32_t destination,
                         int32_t x, int32_t y, uint16_t extent,
                         uint16_t size, uint32_t gadget, uint16_t glyph)
{
    int32_t left = x + ((int32_t)extent - size) / 2;
    int32_t top = y + ((int32_t)extent - size) / 2;

    if (gadget == ASTRA_WINDOW_GADGET_MINIMIZE) {
        (void)astra_render_builder_fill(builder, destination, left,
                                        top + size - 2, size, 2u, glyph);
    } else if (gadget == ASTRA_WINDOW_GADGET_MAXIMIZE) {
        (void)astra_render_builder_fill(builder, destination, left, top,
                                        size, 2u, glyph);
        (void)astra_render_builder_fill(builder, destination, left,
                                        top + size - 2, size, 2u, glyph);
        (void)astra_render_builder_fill(builder, destination, left, top + 2,
                                        2u, size - 4u, glyph);
        (void)astra_render_builder_fill(builder, destination, left + size - 2,
                                        top + 2, 2u, size - 4u, glyph);
    } else {
        for (uint32_t at = 0u; at + 2u <= size; ++at) {
            (void)astra_render_builder_fill(
                builder, destination, left + (int32_t)at,
                top + (int32_t)at, 2u, 2u, glyph);
            (void)astra_render_builder_fill(
                builder, destination, left + size - 2 - (int32_t)at,
                top + (int32_t)at, 2u, 2u, glyph);
        }
    }
}

static void draw_gadget(AstraRenderBuilder *builder, uint32_t destination,
                        const AstraTheme *theme, int32_t x, int32_t y,
                        uint32_t gadget, uint8_t state)
{
    AstraColorRGBA8 semantic = gadget == ASTRA_WINDOW_GADGET_CLOSE ?
        theme->fault : (gadget == ASTRA_WINDOW_GADGET_MINIMIZE ?
                        theme->warning : theme->accent);
    AstraColorRGBA8 background = theme->control;
    AstraColorRGBA8 glyph = theme->text_primary;

    if (state == ASTRA_GADGET_HOVER)
        background = semantic;
    else if (state == ASTRA_GADGET_PRESSED) {
        background = theme->control_pressed;
        glyph = semantic;
    } else if (state == ASTRA_GADGET_DISABLED) {
        background = theme->title_inactive;
        glyph = theme->text_muted;
    }
    if (state == ASTRA_GADGET_FOCUSED)
        (void)astra_render_builder_rounded(
            builder, destination, x + 1, y + 1,
            theme->gadget_extent - 2u, theme->gadget_extent - 2u,
            (theme->gadget_extent - 2u) / 2u, color(theme->accent));
    (void)astra_render_builder_rounded(
        builder, destination, x + 3, y + 3,
        theme->gadget_extent - 6u, theme->gadget_extent - 6u,
        (theme->gadget_extent - 6u) / 2u, color(background));
    gadget_glyph(builder, destination, x, y, theme->gadget_extent,
                 theme->gadget_glyph, gadget, color(glyph));
}

static int build_cache(AstraRenderBuilder *builder, uint32_t cache,
                       uint32_t content, const AstraTheme *theme,
                       const DisplayWindow *window)
{
    const AstraGuiOpenWindow *request = &window->request;
    uint16_t frame = frame_width(theme, request->type);
    uint16_t title = title_height(theme, request->type);
    uint16_t signal = title == 0u ? 0u : theme->signal_height;
    uint16_t radius = window_radius(theme, window);
    int32_t client_x = frame;
    int32_t client_y = frame;

    if (content == 0u)
        return 0;
    if (request->type == ASTRA_WINDOW_FULLSCREEN)
        return astra_render_builder_blit(
            builder, cache, content, 0, 0, request->width,
            request->height, 0u, 0);
    if (!astra_render_builder_rounded(
            builder, cache, 0, 0, outer_width(theme, window),
            outer_height(theme, window), radius, color(theme->frame)))
        return 0;
    if (title != 0u) {
        uint16_t title_color = color(
            (request->flags & ASTRA_WINDOW_ACTIVE) != 0u ?
            theme->title_active : theme->title_inactive);
        uint32_t text_capacity;
        uint32_t text_length;
        uint32_t gadget_count = 0u;
        int32_t gadget_x;
        int32_t gadget_y;

        (void)astra_render_builder_rounded(
            builder, cache, client_x, client_y, request->width,
            title + radius,
            radius > frame ? (uint16_t)(radius - frame) : 0u,
            title_color);
        if (radius != 0u)
            (void)astra_render_builder_fill(
                builder, cache, client_x, client_y + title - radius,
                request->width, radius, title_color);
        if ((request->flags & ASTRA_WINDOW_ACTIVE) != 0u)
            (void)astra_render_builder_fill(
                builder, cache, client_x, client_y + title,
                request->width, signal, color(theme->accent));
        if ((request->gadgets & ASTRA_WINDOW_GADGET_MINIMIZE) != 0u)
            ++gadget_count;
        if ((request->gadgets & ASTRA_WINDOW_GADGET_MAXIMIZE) != 0u)
            ++gadget_count;
        if ((request->gadgets & ASTRA_WINDOW_GADGET_CLOSE) != 0u)
            ++gadget_count;
        text_capacity = theme->spacing_unit * 3u +
                        gadget_count * theme->gadget_extent;
        text_capacity = request->width > text_capacity ?
                        request->width - text_capacity : 0u;
        text_length = astra_surface_ui_text_fit(
            request->title, request->title_length,
            ASTRA_THEME_SYSTEM_TITLE_FONT_HEIGHT, text_capacity);
        (void)astra_render_builder_text(
            builder, cache, client_x + theme->spacing_unit * 2,
            client_y + ((int32_t)title -
                        ASTRA_THEME_SYSTEM_TITLE_FONT_HEIGHT) / 2,
            request->title, text_length,
            ASTRA_THEME_SYSTEM_TITLE_FONT_HEIGHT,
            color(theme->text_primary));
        gadget_x = client_x + request->width - theme->spacing_unit -
                   (int32_t)gadget_count * theme->gadget_extent;
        gadget_y = client_y +
                   ((int32_t)title - theme->gadget_extent) / 2;
        if ((request->gadgets & ASTRA_WINDOW_GADGET_MINIMIZE) != 0u) {
            draw_gadget(builder, cache, theme, gadget_x, gadget_y,
                        ASTRA_WINDOW_GADGET_MINIMIZE,
                        request->minimize_state);
            gadget_x += theme->gadget_extent;
        }
        if ((request->gadgets & ASTRA_WINDOW_GADGET_MAXIMIZE) != 0u) {
            draw_gadget(builder, cache, theme, gadget_x, gadget_y,
                        ASTRA_WINDOW_GADGET_MAXIMIZE,
                        request->maximize_state);
            gadget_x += theme->gadget_extent;
        }
        if ((request->gadgets & ASTRA_WINDOW_GADGET_CLOSE) != 0u)
            draw_gadget(builder, cache, theme, gadget_x, gadget_y,
                        ASTRA_WINDOW_GADGET_CLOSE, request->close_state);
        client_y += title + signal;
    }
    return astra_render_builder_blit(
        builder, cache, content, client_x, client_y,
        request->width, request->height,
        radius > frame ? (uint16_t)(radius - frame) : 0u, title == 0u);
}

static int update_cache_content(AstraRenderBuilder *builder, uint32_t cache,
                                uint32_t content, const AstraTheme *theme,
                                const DisplayWindow *window)
{
    const DamageRect *damage = &window->content_damage;
    uint16_t frame = frame_width(theme, window->request.type);
    uint16_t title = title_height(theme, window->request.type);
    uint16_t signal = title == 0u ? 0u : theme->signal_height;
    uint16_t radius = window_radius(theme, window);
    int32_t client_x = frame;
    int32_t client_y = frame + title + signal;
    uint16_t client_radius = radius > frame ?
        (uint16_t)(radius - frame) : 0u;
    int touches_rounding = client_radius != 0u &&
        (damage->bottom > (int32_t)window->request.height - client_radius ||
         (title == 0u && damage->top < client_radius));

    if (!touches_rounding)
        return astra_render_builder_blit_region(
            builder, cache, content, damage->left, damage->top,
            client_x + damage->left, client_y + damage->top,
            (uint16_t)(damage->right - damage->left),
            (uint16_t)(damage->bottom - damage->top));
    return astra_render_builder_blit_clipped(
        builder, cache, content, client_x, client_y,
        window->request.width, window->request.height, client_radius,
        title == 0u, client_x + damage->left, client_y + damage->top,
        client_x + damage->right, client_y + damage->bottom);
}

static void desktop_damage(AstraRenderBuilder *builder, uint32_t framebuffer,
                           const AstraTheme *theme, const DamageRect *damage)
{
    DamageRect top = { 0, 0, ASTRA_DISPLAY_WIDTH, DISPLAY_WORK_TOP, 1u };
    DamageRect bottom = { 0, DISPLAY_WORK_BOTTOM, ASTRA_DISPLAY_WIDTH,
                          ASTRA_DISPLAY_HEIGHT, 1u };

    (void)astra_render_builder_fill(
        builder, framebuffer, damage->left, damage->top,
        (uint32_t)(damage->right - damage->left),
        (uint32_t)(damage->bottom - damage->top), color(theme->canvas));
    if (overlaps(damage, &top)) {
        int32_t y0 = damage->top > 0 ? damage->top : 0;
        int32_t y1 = damage->bottom < (int32_t)DISPLAY_WORK_TOP ?
                     damage->bottom : (int32_t)DISPLAY_WORK_TOP;

        (void)astra_render_builder_fill(
            builder, framebuffer, damage->left, y0,
            (uint32_t)(damage->right - damage->left),
            (uint32_t)(y1 - y0), color(theme->system_bar));
        (void)astra_render_builder_fill(
            builder, framebuffer, 20, 32, 124u, 2u, color(theme->accent));
        (void)astra_render_builder_text(
            builder, framebuffer, 20, 10, "ASTRA", 5u,
            ASTRA_THEME_SYSTEM_TITLE_FONT_HEIGHT,
            color(theme->text_primary));
    }
    if (overlaps(damage, &bottom)) {
        int32_t y0 = damage->top > (int32_t)DISPLAY_WORK_BOTTOM ?
                     damage->top : (int32_t)DISPLAY_WORK_BOTTOM;

        (void)astra_render_builder_fill(
            builder, framebuffer, damage->left, y0,
            (uint32_t)(damage->right - damage->left),
            (uint32_t)(damage->bottom - y0), color(theme->system_bar));
    }
}

static uint32_t compose(void *storage, uint32_t fence,
                        DisplayState *state, uint32_t *error)
{
    AstraRenderBuilder builder;
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    uint32_t cache[DISPLAY_WINDOW_MAX] = {0};
    uint32_t content[DISPLAY_WINDOW_MAX] = {0};
    uint32_t framebuffer;
    uint32_t buffer = (fence & 1u) != 0u ? 1u : 0u;
    DamageRect *damage = &state->damage[buffer];

    if (error == NULL)
        return 0u;
    *error = ASTRA_STATUS_OK;
    if (damage->valid == 0u || !astra_render_builder_init(
            &builder, storage, ASTRA_RENDER_BUILDER_BYTES, fence)) {
        *error = ASTRA_STATUS_INVALID;
        return 0u;
    }
    framebuffer = astra_render_builder_frame(&builder);
    for (uint32_t index = 0u; index < state->count; ++index) {
        DisplayWindow *window = &state->windows[index];

        cache[index] = astra_render_builder_surface_at(
            &builder, WINDOW_CACHE_BASE +
                window->cache_slot * WINDOW_CACHE_STRIDE,
            (uint16_t)outer_width(&theme, window),
            (uint16_t)outer_height(&theme, window));
        if (cache[index] == 0u) {
            *error = ASTRA_STATUS_LIMIT;
            return 0u;
        }
        content[index] = astra_render_builder_surface_at(
            &builder, WINDOW_CONTENT_BASE +
                window->cache_slot * WINDOW_CONTENT_STRIDE,
            window->request.width, window->request.height);
        if (content[index] == 0u ||
            (window->content_initialized == 0u &&
             !astra_render_builder_fill(
                 &builder, content[index], 0, 0, window->request.width,
                 window->request.height, color(theme.client))) ||
            (window->content_dirty != 0u &&
             !astra_render_builder_replay(
                 &builder, content[index],
                 (const AstraDrawListHeader *)(const void *)
                     window->surface.view.pixels))) {
            *error = builder.failed != 0u ? ASTRA_STATUS_LIMIT :
                                             ASTRA_STATUS_PROTOCOL;
            return 0u;
        }
        if (window->cache_dirty != 0u &&
            !build_cache(&builder, cache[index], content[index],
                         &theme, window)) {
            *error = builder.failed != 0u ? ASTRA_STATUS_LIMIT :
                                             ASTRA_STATUS_PROTOCOL;
            return 0u;
        }
        if (window->cache_dirty == 0u && window->content_dirty != 0u &&
            !update_cache_content(&builder, cache[index], content[index],
                                  &theme, window)) {
            *error = builder.failed != 0u ? ASTRA_STATUS_LIMIT :
                                             ASTRA_STATUS_PROTOCOL;
            return 0u;
        }
    }
    desktop_damage(&builder, framebuffer, &theme, damage);
    for (uint32_t index = 0u; index < state->count; ++index) {
        const DisplayWindow *window = &state->windows[index];
        DamageRect window_bounds;

        if (window->state == ASTRA_WINDOW_STATE_MINIMIZED)
            continue;
        window_bounds = bounds(&theme, window);
        if (overlaps(damage, &window_bounds) &&
            !astra_render_builder_blit_clipped(
                &builder, framebuffer, cache[index],
                window->request.x, window->request.y,
                (uint16_t)outer_width(&theme, window),
                (uint16_t)outer_height(&theme, window),
                window_radius(&theme, window), 1,
                damage->left, damage->top, damage->right, damage->bottom))
            return *error = ASTRA_STATUS_LIMIT, 0u;
    }
    framebuffer = astra_render_builder_finish(&builder);
    if (framebuffer == 0u)
        *error = ASTRA_STATUS_LIMIT;
    return framebuffer;
}

static uint32_t submit_request(uint32_t device, uint32_t irq,
                               const AstraDisplayFrameRequest *request,
                               uint32_t *armed)
{
    AstraDisplayFrameCompletion completion;
    uint32_t status;

    if (*armed == 0u) {
        if (astra_irq_arm(irq) != ASTRA_SYSCALL_OK)
            return DISPLAY_FAIL_ARM;
        *armed = 1u;
    }
    if (astra_display_submit(device, request) != ASTRA_SYSCALL_OK)
        return DISPLAY_FAIL_SUBMIT;
    for (;;) {
        if (astra_wait_one(irq, ASTRA_DEADLINE_FOREVER, NULL) !=
            ASTRA_SYSCALL_OK)
            return DISPLAY_FAIL_WAIT;
        status = astra_display_collect(device, &completion);
        if (status == ASTRA_SYSCALL_OK)
            break;
        if (status != ASTRA_SYSCALL_WOULD_BLOCK)
            return DISPLAY_FAIL_COMPLETION;
    }
    for (uint32_t drained = 0u; drained < DISPLAY_IRQ_DRAIN_MAX; ++drained) {
        AstraIrqRecord record;

        status = astra_irq_read(irq, &record, NULL);
        if (status == ASTRA_SYSCALL_WOULD_BLOCK)
            break;
        if (status != ASTRA_SYSCALL_OK ||
            astra_irq_ack(irq, record.sequence) != ASTRA_SYSCALL_OK)
            return DISPLAY_FAIL_IRQ;
        if (drained + 1u == DISPLAY_IRQ_DRAIN_MAX)
            return DISPLAY_FAIL_IRQ;
    }
    if (completion.size != ASTRA_DISPLAY_FRAME_COMPLETION_SIZE ||
        completion.fence != request->fence ||
        completion.status != ASTRA_DISPLAY_COMPLETION_OK ||
        completion.generation == 0u || completion.reserved != 0u)
        return DISPLAY_FAIL_COMPLETION;
    return ASTRA_STATUS_OK;
}

static uint32_t present(uint32_t device, uint32_t irq,
                        const AstraDmaBufferInfo *buffer, uint32_t byte_size,
                        uint32_t fence, uint32_t *armed)
{
    AstraDisplayFrameRequest request = {
        .size = ASTRA_DISPLAY_FRAME_REQUEST_SIZE,
        .operation = ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH,
        .fence = fence,
        .source = buffer->handle,
        .pitch = 0u,
        .byte_size = byte_size,
    };

    return submit_request(device, irq, &request, armed);
}

static uint32_t update_cursor(uint32_t device, uint32_t irq,
                              int32_t x, int32_t y, uint32_t visible,
                              uint32_t defer_commit,
                              uint32_t *fence, uint32_t *armed)
{
    AstraDisplayFrameRequest request = {
        .size = ASTRA_DISPLAY_FRAME_REQUEST_SIZE,
        .operation = ASTRA_DISPLAY_CURSOR_UPDATE,
        .fence = *fence,
        .source = (uint32_t)x,
        .pitch = (uint32_t)y,
        .byte_size = (visible != 0u ? ASTRA_DISPLAY_CURSOR_VISIBLE : 0u) |
                     (defer_commit != 0u ?
                          ASTRA_DISPLAY_CURSOR_DEFER_COMMIT : 0u),
    };
    uint32_t status = submit_request(device, irq, &request, armed);

    if (status == ASTRA_STATUS_OK && ++*fence == 0u)
        *fence = 1u;
    return status;
}

static uint32_t render(uint32_t device, uint32_t irq,
                       AstraDmaBufferInfo *framebuffer, DisplayState *state,
                       uint32_t *next_fence, uint32_t *armed)
{
    uint32_t buffer = (*next_fence & 1u) != 0u ? 1u : 0u;
    uint32_t compose_status = ASTRA_STATUS_OK;
    uint32_t bytes = compose((void *)(uintptr_t)framebuffer->virtual_base,
                             *next_fence, state, &compose_status);
    uint32_t status = bytes == 0u ? compose_status :
        present(device, irq, framebuffer, bytes, *next_fence, armed);

    if (status == ASTRA_STATUS_OK) {
        ++*next_fence;
        state->damage[buffer] = (DamageRect){0};
        for (uint32_t index = 0u; index < state->count; ++index) {
            state->windows[index].cache_dirty = 0u;
            state->windows[index].content_dirty = 0u;
            state->windows[index].content_initialized = 1u;
            state->windows[index].content_damage = (DamageRect){0};
        }
    }
    return status;
}

static int valid_open(const AstraGuiOpenWindow *request, uint32_t size,
                      uint32_t handles)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    DisplayWindow candidate = { .request = *request };
    uint16_t title;

    if (size != sizeof(*request) || handles != 3u)
        return 0;
    title = title_height(&theme, request->type);
    return request->header.total_size == sizeof(*request) &&
           request->header.header_size == ASTRA_MESSAGE_HEADER_SIZE &&
           request->header.flags == 0u &&
           request->header.protocol == ASTRA_GUI_PROTOCOL &&
           request->header.protocol_version == ASTRA_GUI_VERSION &&
           request->header.reserved == 0u &&
           request->header.operation == ASTRA_GUI_OPEN_WINDOW &&
           request->header.transaction_id != 0u &&
           (request->event_mask & ~ASTRA_WINDOW_SUBSCRIBE_ALL) == 0u &&
           request->content_format == ASTRA_GUI_CONTENT_DRAW_LIST &&
           request->type >= ASTRA_WINDOW_STANDARD &&
           request->type <= ASTRA_WINDOW_FULLSCREEN &&
           (request->flags & ~(ASTRA_WINDOW_RESIZABLE | ASTRA_WINDOW_MODAL |
                               ASTRA_WINDOW_ACTIVE)) == 0u &&
           (request->gadgets & ~(ASTRA_WINDOW_GADGET_CLOSE |
                                 ASTRA_WINDOW_GADGET_MINIMIZE |
                                 ASTRA_WINDOW_GADGET_MAXIMIZE)) == 0u &&
           request->close_state <= ASTRA_GADGET_DISABLED &&
           request->minimize_state <= ASTRA_GADGET_DISABLED &&
           request->maximize_state <= ASTRA_GADGET_DISABLED &&
           request->title_length <= ASTRA_WINDOW_TITLE_MAX &&
           (title == 0u || request->width >= 96u) &&
           request->pitch == 0u && frame_valid(
               &theme, &candidate, request->x, request->y,
               request->width, request->height);
}

static int valid_command(const AstraGuiWindowCommand *request, uint32_t size,
                         uint32_t handles, uint32_t id,
                         uint16_t content_width, uint16_t content_height)
{
    int frame_zero = request->x == 0u && request->y == 0u &&
                     request->width == 0u && request->height == 0u;

    if (size != sizeof(*request) || handles != 1u ||
        request->header.total_size != sizeof(*request) ||
        request->header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        request->header.flags != 0u ||
        request->header.protocol != ASTRA_GUI_PROTOCOL ||
        request->header.protocol_version != ASTRA_GUI_VERSION ||
        request->header.reserved != 0u ||
        request->header.operation != ASTRA_GUI_WINDOW_COMMAND ||
        request->header.transaction_id == 0u || request->window != id ||
        request->generation == 0u ||
        request->reserved16 != 0u || request->reserved != 0u ||
        request->action < ASTRA_GUI_WINDOW_QUERY ||
        request->action > ASTRA_GUI_WINDOW_PRESENT ||
        request->title_length > ASTRA_WINDOW_TITLE_MAX)
        return 0;
    if (request->action == ASTRA_GUI_WINDOW_SET_FRAME)
        return request->width != 0u && request->height != 0u &&
               request->title_length == 0u && request->flags == 0u;
    if (request->action == ASTRA_GUI_WINDOW_MOVE)
        return request->width == 0u && request->height == 0u &&
               request->title_length == 0u && request->flags == 0u;
    if (request->action == ASTRA_GUI_WINDOW_RESIZE)
        return request->x == 0u && request->y == 0u &&
               request->width != 0u && request->height != 0u &&
               request->title_length == 0u && request->flags == 0u;
    if (request->action == ASTRA_GUI_WINDOW_SET_TITLE)
        return frame_zero && request->flags == 0u;
    if (request->action == ASTRA_GUI_WINDOW_SET_EVENT_MASK)
        return frame_zero && request->title_length == 0u &&
               (request->flags & ~ASTRA_WINDOW_SUBSCRIBE_ALL) == 0u;
    if (request->action == ASTRA_GUI_WINDOW_PRESENT)
        return request->title_length == 0u && request->flags == 0u &&
               (frame_zero ||
                (request->width != 0u && request->height != 0u &&
                 (uint32_t)request->x + request->width <= content_width &&
                 (uint32_t)request->y + request->height <= content_height));
    return frame_zero && request->title_length == 0u &&
           request->flags == 0u;
}

static void state_reply(AstraGuiWindowState *reply, uint32_t transaction,
                        uint32_t status, const DisplayWindow *window,
                        uint32_t z_order)
{
    *reply = (AstraGuiWindowState){0};
    reply->header.total_size = sizeof(*reply);
    reply->header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    reply->header.protocol = ASTRA_GUI_PROTOCOL;
    reply->header.protocol_version = ASTRA_GUI_VERSION;
    reply->header.operation = ASTRA_GUI_WINDOW_STATE;
    reply->header.transaction_id = transaction;
    reply->status = status;
    reply->window = window->id;
    reply->generation = window->generation;
    reply->x = window->request.x;
    reply->y = window->request.y;
    reply->width = window->request.width;
    reply->height = window->request.height;
    reply->flags = window->request.flags;
    reply->state = window->state;
    reply->z_order = z_order;
}

static uint32_t apply_command(DisplayState *state, const AstraTheme *theme,
                              const AstraGuiWindowCommand *command,
                              DisplayWindow *closed, int *changed)
{
    uint32_t index = find_id(state, command->window);
    DisplayWindow *window;

    *changed = 0;
    *closed = (DisplayWindow){0};
    if (index == state->count)
        return ASTRA_STATUS_NOT_FOUND;
    window = &state->windows[index];
    switch (command->action) {
    case ASTRA_GUI_WINDOW_QUERY:
        return ASTRA_STATUS_OK;
    case ASTRA_GUI_WINDOW_SET_FRAME:
        if (!frame_valid(theme, window, command->x, command->y,
                         command->width, command->height))
            return ASTRA_STATUS_INVALID;
        damage_window(state, theme, window);
        window->request.x = command->x;
        window->request.y = command->y;
        window->request.width = command->width;
        window->request.height = command->height;
        window->state = ASTRA_WINDOW_STATE_NORMAL;
        window->cache_dirty = 1u;
        reset_content(window);
        next_generation(window);
        damage_window(state, theme, window);
        *changed = 1;
        return ASTRA_STATUS_OK;
    case ASTRA_GUI_WINDOW_MOVE:
        if (window->state != ASTRA_WINDOW_STATE_NORMAL ||
            !frame_valid(theme, window, command->x, command->y,
                         window->request.width, window->request.height))
            return ASTRA_STATUS_INVALID;
        damage_window(state, theme, window);
        window->request.x = command->x;
        window->request.y = command->y;
        next_generation(window);
        damage_window(state, theme, window);
        *changed = 1;
        return ASTRA_STATUS_OK;
    case ASTRA_GUI_WINDOW_RESIZE:
        if ((window->request.flags & ASTRA_WINDOW_RESIZABLE) == 0u)
            return ASTRA_STATUS_UNSUPPORTED;
        if (window->state != ASTRA_WINDOW_STATE_NORMAL ||
            !frame_valid(theme, window, window->request.x, window->request.y,
                         command->width, command->height))
            return ASTRA_STATUS_INVALID;
        damage_window(state, theme, window);
        window->request.width = command->width;
        window->request.height = command->height;
        window->cache_dirty = 1u;
        reset_content(window);
        next_generation(window);
        damage_window(state, theme, window);
        *changed = 1;
        return ASTRA_STATUS_OK;
    case ASTRA_GUI_WINDOW_RAISE:
        if (index != state->count - 1u) {
            reorder(state, theme, index, state->count - 1u);
            *changed = 1;
        }
        return ASTRA_STATUS_OK;
    case ASTRA_GUI_WINDOW_LOWER:
        if (index != 0u) {
            uint32_t was_active = window->request.flags & ASTRA_WINDOW_ACTIVE;

            reorder(state, theme, index, 0u);
            if (was_active != 0u)
                activate(state, theme, top_visible(state), 0, 0u);
            *changed = 1;
        }
        return ASTRA_STATUS_OK;
    case ASTRA_GUI_WINDOW_ACTIVATE:
        if (window->state == ASTRA_WINDOW_STATE_MINIMIZED) {
            window->state = window->restore_state;
            window->cache_dirty = 1u;
            next_generation(window);
            damage_window(state, theme, window);
        }
        activate(state, theme, window->id, 1, 0u);
        *changed = 1;
        return ASTRA_STATUS_OK;
    case ASTRA_GUI_WINDOW_DEACTIVATE:
        if ((window->request.flags & ASTRA_WINDOW_ACTIVE) != 0u) {
            activate(state, theme, 0u, 0, 0u);
            *changed = 1;
        }
        return ASTRA_STATUS_OK;
    case ASTRA_GUI_WINDOW_MINIMIZE:
        if (window->state != ASTRA_WINDOW_STATE_MINIMIZED) {
            uint32_t was_active = window->request.flags & ASTRA_WINDOW_ACTIVE;

            damage_window(state, theme, window);
            window->restore_state = window->state;
            window->state = ASTRA_WINDOW_STATE_MINIMIZED;
            window->request.flags &= ~ASTRA_WINDOW_ACTIVE;
            next_generation(window);
            if (was_active != 0u)
                activate(state, theme, top_visible(state), 0, 0u);
            *changed = 1;
        }
        return ASTRA_STATUS_OK;
    case ASTRA_GUI_WINDOW_MAXIMIZE:
        if ((window->request.flags & ASTRA_WINDOW_RESIZABLE) == 0u)
            return ASTRA_STATUS_UNSUPPORTED;
        if (window->state == ASTRA_WINDOW_STATE_MINIMIZED)
            window->state = window->restore_state;
        if (window->state != ASTRA_WINDOW_STATE_MAXIMIZED) {
            if (window->state == ASTRA_WINDOW_STATE_NORMAL) {
                window->restore_x = window->request.x;
                window->restore_y = window->request.y;
                window->restore_width = window->request.width;
                window->restore_height = window->request.height;
            }
            damage_window(state, theme, window);
            window->request.x = 0u;
            window->request.y = DISPLAY_WORK_TOP;
            window->request.width = ASTRA_DISPLAY_WIDTH -
                frame_width(theme, window->request.type) * 2u;
            window->request.height = DISPLAY_WORK_BOTTOM - DISPLAY_WORK_TOP -
                title_height(theme, window->request.type) -
                (title_height(theme, window->request.type) == 0u ?
                 0u : theme->signal_height) -
                frame_width(theme, window->request.type) * 2u;
            window->state = ASTRA_WINDOW_STATE_MAXIMIZED;
            window->cache_dirty = 1u;
            reset_content(window);
            next_generation(window);
            damage_window(state, theme, window);
            *changed = 1;
        }
        return ASTRA_STATUS_OK;
    case ASTRA_GUI_WINDOW_RESTORE:
        if (window->state == ASTRA_WINDOW_STATE_MINIMIZED) {
            window->state = window->restore_state;
            window->cache_dirty = 1u;
            next_generation(window);
            damage_window(state, theme, window);
            *changed = 1;
        } else if (window->state == ASTRA_WINDOW_STATE_MAXIMIZED) {
            damage_window(state, theme, window);
            window->request.x = window->restore_x;
            window->request.y = window->restore_y;
            window->request.width = window->restore_width;
            window->request.height = window->restore_height;
            window->state = ASTRA_WINDOW_STATE_NORMAL;
            window->cache_dirty = 1u;
            reset_content(window);
            next_generation(window);
            damage_window(state, theme, window);
            *changed = 1;
        }
        return ASTRA_STATUS_OK;
    case ASTRA_GUI_WINDOW_SET_TITLE:
        if (window->request.title_length != command->title_length) {
            *changed = 1;
        } else {
            for (uint32_t at = 0u; at < command->title_length; ++at)
                if (window->request.title[at] != command->title[at]) {
                    *changed = 1;
                    break;
                }
        }
        if (*changed != 0) {
            damage_window(state, theme, window);
            window->request.title_length = command->title_length;
            for (uint32_t at = 0u; at < ASTRA_WINDOW_TITLE_MAX; ++at)
                window->request.title[at] =
                    at < command->title_length ? command->title[at] : 0;
            window->cache_dirty = 1u;
            next_generation(window);
            damage_window(state, theme, window);
        }
        return ASTRA_STATUS_OK;
    case ASTRA_GUI_WINDOW_CLOSE:
        damage_window(state, theme, window);
        next_generation(window);
        *closed = *window;
        for (uint32_t at = index; at + 1u < state->count; ++at)
            state->windows[at] = state->windows[at + 1u];
        --state->count;
        state->windows[state->count] = (DisplayWindow){0};
        if ((closed->request.flags & ASTRA_WINDOW_ACTIVE) != 0u)
            activate(state, theme, top_visible(state), 0, 0u);
        *changed = 1;
        return ASTRA_STATUS_OK;
    case ASTRA_GUI_WINDOW_SET_EVENT_MASK:
        window->request.event_mask = command->flags;
        return ASTRA_STATUS_OK;
    case ASTRA_GUI_WINDOW_PRESENT:
        window->content_dirty = 1u;
        damage_content(state, theme, window,
                       command->width == 0u ?
                           (DamageRect){0, 0, window->request.width,
                                        window->request.height, 1u} :
                           (DamageRect){command->x, command->y,
                                        command->x + command->width,
                                        command->y + command->height, 1u});
        next_generation(window);
        *changed = 1;
        return ASTRA_STATUS_OK;
    default:
        return ASTRA_STATUS_UNSUPPORTED;
    }
}

static uint32_t pointer_target(DisplayState *state, const AstraTheme *theme,
                               uint32_t *region)
{
    uint32_t index;

    if (state->capture_window != 0u) {
        index = find_id(state, state->capture_window);
        if (index != state->count) {
            *region = state->capture_region;
            return index;
        }
        state->capture_window = 0u;
        state->capture_region = HIT_NONE;
    }
    return hit_test(state, theme, state->pointer_x, state->pointer_y, region);
}

static uint32_t handle_pointer(DisplayState *state,
                               const AstraLogicalInputEvent *input,
                               uint32_t *effects, uint32_t *frame_window,
                               uint32_t *frame_timestamp)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    uint32_t region = HIT_NONE;
    uint32_t index;
    int changed = 0;

    if (input->type == ASTRA_INPUT_EVENT_KEY ||
        input->type == ASTRA_INPUT_EVENT_TEXT) {
        index = active_window(state);
        if (index != state->count)
            key_event(&state->windows[index], input);
        return ASTRA_STATUS_OK;
    }
    if (input->type == ASTRA_INPUT_EVENT_STATE_RESET) {
        index = active_window(state);
        state->capture_window = 0u;
        state->capture_region = HIT_NONE;
        changed = update_hover(state, &theme, 0u, HIT_NONE);
        if (index != state->count) {
            AstraWindowEvent event = {
                .type = ASTRA_WINDOW_EVENT_STATE_RESET,
                .flags = ASTRA_WINDOW_EVENT_LOSS,
                .timestamp_ms = input->timestamp_ms,
            };

            (void)send_event(&state->windows[index], &event);
        }
        if (changed)
            *effects |= DISPLAY_POINTER_RENDER;
        return ASTRA_STATUS_OK;
    }

    if (input->type == ASTRA_INPUT_EVENT_POINTER_MOTION) {
        state->pointer_x = input->value_x;
        state->pointer_y = input->value_y;
        index = pointer_target(state, &theme, &region);
        if (index != state->count) {
            DisplayWindow *window = &state->windows[index];

            if (state->capture_region == HIT_TITLE) {
                changed = move_captured_window(
                    state, &theme, window,
                    state->pointer_x, state->pointer_y);
            } else if (resize_region(state->capture_region)) {
                changed = resize_captured_window(
                    state, &theme, window,
                    state->pointer_x, state->pointer_y);
            } else if (state->capture_region >= HIT_MINIMIZE &&
                       state->capture_region <= HIT_CLOSE) {
                uint32_t under = hit_region(
                    &theme, window, state->pointer_x, state->pointer_y);

                changed = set_gadget_visual(
                    state, &theme, window, state->capture_region,
                    under == state->capture_region ?
                        ASTRA_GADGET_PRESSED : ASTRA_GADGET_NORMAL);
            } else {
                pointer_event(
                    window, &theme, ASTRA_WINDOW_EVENT_POINTER_MOTION,
                    state->capture_window != 0u ?
                        ASTRA_WINDOW_EVENT_CAPTURED : 0u,
                    input->timestamp_ms, state->pointer_x,
                    state->pointer_y, 0u);
                if (state->capture_window == 0u)
                    changed = update_hover(state, &theme,
                                           window->id, region);
            }
        } else if (state->capture_window == 0u) {
            changed = update_hover(state, &theme, 0u, HIT_NONE);
        }
        *effects |= DISPLAY_POINTER_CURSOR;
        if (changed) {
            *effects |= DISPLAY_POINTER_RENDER;
            index = find_id(state, state->capture_window);
            if ((state->capture_region == HIT_TITLE ||
                 resize_region(state->capture_region)) &&
                index != state->count) {
                *effects |= DISPLAY_POINTER_FRAME;
                *frame_window = state->windows[index].id;
                *frame_timestamp = input->timestamp_ms;
            }
        }
        return ASTRA_STATUS_OK;
    }
    if (input->type != ASTRA_INPUT_EVENT_POINTER_BUTTON)
        return ASTRA_STATUS_OK;

    index = pointer_target(state, &theme, &region);
    if (input->code >= ASTRA_INPUT_BUTTON_WHEEL_UP &&
        input->code <= ASTRA_INPUT_BUTTON_WHEEL_RIGHT) {
        if ((input->flags & ASTRA_INPUT_LOGICAL_DOWN) != 0u &&
            index != state->count) {
            int32_t dx = input->code == ASTRA_INPUT_BUTTON_WHEEL_LEFT ? -1 :
                         input->code == ASTRA_INPUT_BUTTON_WHEEL_RIGHT ? 1 : 0;
            int32_t dy = input->code == ASTRA_INPUT_BUTTON_WHEEL_UP ? 1 :
                         input->code == ASTRA_INPUT_BUTTON_WHEEL_DOWN ? -1 : 0;

            wheel_event(&state->windows[index], &theme, input->timestamp_ms,
                        state->pointer_x, state->pointer_y, dx, dy);
        }
        return ASTRA_STATUS_OK;
    }
    if ((input->flags & ASTRA_INPUT_LOGICAL_DOWN) != 0u) {
        uint32_t id;

        if (index == state->count)
            return ASTRA_STATUS_OK;
        id = state->windows[index].id;
        if (input->code == ASTRA_INPUT_BUTTON_LEFT) {
            changed |= index != state->count - 1u ||
                       (state->windows[index].request.flags &
                        ASTRA_WINDOW_ACTIVE) == 0u;
            activate(state, &theme, id, 1, input->timestamp_ms);
            index = find_id(state, id);
            region = hit_region(&theme, &state->windows[index],
                                state->pointer_x, state->pointer_y);
            state->capture_window = id;
            state->capture_region = region;
            state->capture_x = state->windows[index].request.x;
            state->capture_y = state->windows[index].request.y;
            state->capture_width = state->windows[index].request.width;
            state->capture_height = state->windows[index].request.height;
            if (resize_region(region)) {
                state->capture_dx = state->pointer_x;
                state->capture_dy = state->pointer_y;
            } else {
                state->capture_dx = state->pointer_x -
                                    state->windows[index].request.x;
                state->capture_dy = state->pointer_y -
                                    state->windows[index].request.y;
            }
            if (region >= HIT_MINIMIZE && region <= HIT_CLOSE)
                changed |= set_gadget_visual(
                    state, &theme, &state->windows[index], region,
                    ASTRA_GADGET_PRESSED);
            else if (region == HIT_CONTENT)
                pointer_event(
                    &state->windows[index], &theme,
                    ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                    ASTRA_WINDOW_EVENT_DOWN | ASTRA_WINDOW_EVENT_CAPTURED,
                    input->timestamp_ms, state->pointer_x,
                    state->pointer_y, input->code);
        } else {
            pointer_event(&state->windows[index], &theme,
                          ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN, input->timestamp_ms,
                          state->pointer_x, state->pointer_y, input->code);
        }
    } else if (state->capture_window != 0u) {
        uint32_t captured_id = state->capture_window;
        uint32_t captured_region = state->capture_region;
        uint32_t under;

        index = find_id(state, captured_id);
        state->capture_window = 0u;
        state->capture_region = HIT_NONE;
        if (index == state->count)
            return ASTRA_STATUS_OK;
        under = hit_region(&theme, &state->windows[index],
                           state->pointer_x, state->pointer_y);
        if (captured_region >= HIT_MINIMIZE && captured_region <= HIT_CLOSE) {
            AstraGuiWindowCommand command = {
                .window = captured_id,
                .generation = state->windows[index].generation,
                .action = captured_region == HIT_MINIMIZE ?
                    ASTRA_GUI_WINDOW_MINIMIZE :
                    captured_region == HIT_MAXIMIZE ?
                    (state->windows[index].state ==
                         ASTRA_WINDOW_STATE_MAXIMIZED ?
                         ASTRA_GUI_WINDOW_RESTORE :
                         ASTRA_GUI_WINDOW_MAXIMIZE) :
                    ASTRA_GUI_WINDOW_QUERY,
            };
            DisplayWindow closed = {0};
            int command_changed = 0;

            changed |= set_gadget_visual(state, &theme,
                                         &state->windows[index],
                                         captured_region,
                                         under == captured_region ?
                                         ASTRA_GADGET_HOVER :
                                         ASTRA_GADGET_NORMAL);
            if (under == captured_region && captured_region == HIT_CLOSE) {
                AstraWindowEvent event = {
                    .type = ASTRA_WINDOW_EVENT_CLOSE_REQUEST,
                    .timestamp_ms = input->timestamp_ms,
                };

                (void)send_event(&state->windows[index], &event);
            } else if (under == captured_region) {
                uint32_t status = apply_command(
                    state, &theme, &command, &closed, &command_changed);

                if (status != ASTRA_STATUS_OK)
                    return status;
                changed |= command_changed;
                if (command_changed) {
                    index = find_id(state, captured_id);
                    under = index == state->count ? HIT_NONE :
                        hit_region(&theme, &state->windows[index],
                                   state->pointer_x, state->pointer_y);
                    changed |= update_hover(state, &theme, captured_id,
                                            under);
                }
            }
        } else if (captured_region == HIT_CONTENT) {
            pointer_event(
                &state->windows[index], &theme,
                ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                ASTRA_WINDOW_EVENT_CAPTURED,
                input->timestamp_ms, state->pointer_x,
                state->pointer_y, input->code);
        }
    } else if (index != state->count) {
        pointer_event(&state->windows[index], &theme,
                      ASTRA_WINDOW_EVENT_POINTER_BUTTON, 0u,
                      input->timestamp_ms, state->pointer_x,
                      state->pointer_y, input->code);
    }
    if (changed)
        *effects |= DISPLAY_POINTER_RENDER;
    return ASTRA_STATUS_OK;
}

static uint32_t cache_slot(const DisplayState *state)
{
    for (uint32_t slot = 0u; slot < DISPLAY_WINDOW_MAX; ++slot) {
        uint32_t used = 0u;

        for (uint32_t index = 0u; index < state->count; ++index)
            used |= state->windows[index].cache_slot == slot;
        if (used == 0u)
            return slot;
    }
    return DISPLAY_WINDOW_MAX;
}

static uint32_t allocate_id(DisplayState *state)
{
    do {
        if (++state->next_id == 0u)
            state->next_id = 1u;
    } while (find_id(state, state->next_id) != state->count);
    return state->next_id;
}

static uint32_t reply_open(uint32_t handle, uint32_t transaction,
                           uint32_t status, const DisplayWindow *window,
                           uint32_t control_send)
{
    AstraGuiWindowOpened message = {0};

    message.header.total_size = sizeof(message);
    message.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    message.header.protocol = ASTRA_GUI_PROTOCOL;
    message.header.protocol_version = ASTRA_GUI_VERSION;
    message.header.operation = ASTRA_GUI_WINDOW_OPENED;
    message.header.transaction_id = transaction;
    message.status = status;
    if (status == ASTRA_STATUS_OK) {
        message.window = window->id;
        message.generation = window->generation;
    }
    return astra_port_send(handle, &message, sizeof(message),
                           status == ASTRA_STATUS_OK ? &control_send : NULL,
                           status == ASTRA_STATUS_OK ? 1u : 0u);
}

static void close_window(DisplayWindow *window)
{
    if (window->control_receive != 0u)
        (void)astra_close(window->control_receive);
    if (window->event_send != 0u)
        (void)astra_close(window->event_send);
    if (window->surface.area != 0u)
        (void)astra_shared_surface_close(&window->surface);
    *window = (DisplayWindow){0};
}

static void receive_open(uint32_t device, uint32_t irq,
                         AstraDmaBufferInfo *framebuffer,
                         DisplayState *state, uint32_t gui_receive,
                         uint32_t *next_fence, uint32_t *armed)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraGuiOpenWindow request = {0};
    DisplayWindow candidate = {0};
    uint32_t handles[ASTRA_MESSAGE_HANDLES_MAX] = {0};
    uint32_t handle_count = 0u;
    uint32_t size = 0u;
    uint32_t control_send = 0u;
    uint32_t status = astra_port_receive(
        gui_receive, &request, sizeof(request), handles,
        ASTRA_MESSAGE_HANDLES_MAX, &size, &handle_count);
    int added = 0;

    if (status != ASTRA_SYSCALL_OK)
        return;
    status = valid_open(&request, size, handle_count) ?
        (state->count < DISPLAY_WINDOW_MAX ? ASTRA_STATUS_OK :
                                             ASTRA_STATUS_LIMIT) :
        DISPLAY_FAIL_PROTOCOL;
    if (status == ASTRA_STATUS_OK)
        status = service_status(astra_shared_draw_list_adopt(
            &candidate.surface, handles[0], request.width, request.height,
            ASTRA_AREA_MAP_READ));
    if (status == ASTRA_STATUS_OK) {
        handles[0] = 0u;
        status = service_status(astra_rt_port_create(
            1u, ASTRA_GUI_WINDOW_COMMAND_SIZE, &candidate.control_receive,
            &control_send));
    }
    if (status == ASTRA_STATUS_OK) {
        candidate.request = request;
        candidate.request.flags &= ~ASTRA_WINDOW_ACTIVE;
        candidate.id = allocate_id(state);
        candidate.generation = 1u;
        candidate.event_send = handles[1];
        handles[1] = 0u;
        candidate.cache_slot = cache_slot(state);
        candidate.cache_dirty = 1u;
        reset_content(&candidate);
        state->windows[state->count++] = candidate;
        added = 1;
        candidate = (DisplayWindow){0};
        damage_window(state, &theme, &state->windows[state->count - 1u]);
        if ((request.flags & ASTRA_WINDOW_ACTIVE) != 0u)
            activate(state, &theme, state->windows[state->count - 1u].id,
                     1, 0u);
        status = render(device, irq, framebuffer, state,
                        next_fence, armed);
    }
    if (handle_count == 3u) {
        const DisplayWindow *window = status == ASTRA_STATUS_OK ?
            &state->windows[state->count - 1u] : &candidate;
        uint32_t sent = reply_open(
            handles[2], request.header.transaction_id, status,
            window, control_send);

        if (sent == ASTRA_SYSCALL_OK && status == ASTRA_STATUS_OK)
            control_send = 0u;
        else if (status == ASTRA_STATUS_OK)
            status = ASTRA_STATUS_PEER_DEAD;
    }
    if (status != ASTRA_STATUS_OK && added) {
        DisplayWindow failed = {0};
        AstraGuiWindowCommand close = {
            .window = state->windows[state->count - 1u].id,
            .action = ASTRA_GUI_WINDOW_CLOSE
        };
        int changed = 0;

        (void)apply_command(state, &theme, &close, &failed, &changed);
        if (changed && render(device, irq, framebuffer, state,
                              next_fence, armed) != ASTRA_STATUS_OK)
            astra_process_exit(DISPLAY_FAIL_COMPLETION);
        close_window(&failed);
    }
    if (control_send != 0u)
        (void)astra_close(control_send);
    close_window(&candidate);
    for (uint32_t index = 0u; index < handle_count; ++index)
        if (handles[index] != 0u)
            (void)astra_close(handles[index]);
}

static void receive_command(uint32_t device, uint32_t irq,
                            AstraDmaBufferInfo *framebuffer,
                            DisplayState *state, uint32_t window_index,
                            uint32_t *next_fence, uint32_t *armed)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraGuiWindowCommand command = {0};
    AstraGuiWindowState reply;
    DisplayWindow closed = {0};
    uint32_t handles[ASTRA_MESSAGE_HANDLES_MAX] = {0};
    uint32_t handle_count = 0u;
    uint32_t size = 0u;
    uint32_t id = state->windows[window_index].id;
    uint32_t receive = state->windows[window_index].control_receive;
    uint32_t status = astra_port_receive(
        receive, &command, sizeof(command), handles,
        ASTRA_MESSAGE_HANDLES_MAX, &size, &handle_count);
    int changed = 0;

    if (status == ASTRA_SYSCALL_PEER_DEAD) {
        AstraGuiWindowCommand close = { .window = id,
                                       .action = ASTRA_GUI_WINDOW_CLOSE };

        (void)apply_command(state, &theme, &close, &closed, &changed);
        if (changed && render(device, irq, framebuffer, state,
                              next_fence, armed) != ASTRA_STATUS_OK)
            astra_process_exit(DISPLAY_FAIL_COMPLETION);
        close_window(&closed);
        return;
    }
    if (status != ASTRA_SYSCALL_OK)
        return;
    status = valid_command(&command, size, handle_count, id,
                           state->windows[window_index].request.width,
                           state->windows[window_index].request.height) ?
             apply_command(state, &theme, &command, &closed, &changed) :
             DISPLAY_FAIL_PROTOCOL;
    if (status == ASTRA_STATUS_OK && changed &&
        render(device, irq, framebuffer, state,
               next_fence, armed) != ASTRA_STATUS_OK)
        astra_process_exit(DISPLAY_FAIL_COMPLETION);
    if (status == ASTRA_STATUS_OK && changed && closed.id == 0u) {
        uint32_t current = find_id(state, id);

        if (current != state->count)
            frame_event(&state->windows[current], 0u, current);
    }
    if (closed.id != 0u)
        state_reply(&reply, command.header.transaction_id, status,
                    &closed, window_index);
    else {
        uint32_t current = find_id(state, id);

        if (current == state->count)
            state_reply(&reply, command.header.transaction_id,
                        ASTRA_STATUS_NOT_FOUND,
                        &state->windows[window_index], window_index);
        else
            state_reply(&reply, command.header.transaction_id, status,
                        &state->windows[current], current);
    }
    if (handle_count == 1u)
        (void)astra_port_send(handles[0], &reply, sizeof(reply), NULL, 0u);
    close_window(&closed);
    for (uint32_t index = 0u; index < handle_count; ++index)
        if (handles[index] != 0u)
            (void)astra_close(handles[index]);
}

static uint32_t connect_input(uint32_t service, uint32_t *receive_out)
{
    AstraInputConnect request = {0};
    AstraInputConnected reply = {0};
    uint32_t event_receive = 0u;
    uint32_t event_send = 0u;
    uint32_t reply_receive = 0u;
    uint32_t reply_send = 0u;
    uint32_t handles[2];
    uint32_t size = 0u;
    uint32_t handle_count = 0u;
    uint32_t status;

    status = astra_rt_port_create(
        DISPLAY_INPUT_QUEUE,
        DISPLAY_INPUT_QUEUE * sizeof(AstraInputEventMessage),
        &event_receive, &event_send);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    status = astra_rt_port_create(1u, sizeof(reply),
                                  &reply_receive, &reply_send);
    if (status != ASTRA_SYSCALL_OK)
        goto done;
    request.header.total_size = sizeof(request);
    request.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    request.header.protocol = ASTRA_INPUT_SERVICE_PROTOCOL;
    request.header.protocol_version = ASTRA_INPUT_SERVICE_VERSION;
    request.header.operation = ASTRA_INPUT_OPERATION_CONNECT;
    request.header.transaction_id = 1u;
    request.subscriptions = ASTRA_INPUT_SUBSCRIBE_ALL;
    request.flags = ASTRA_INPUT_CONNECT_SEAT_OWNER;
    handles[0] = event_send;
    handles[1] = reply_send;
    status = astra_port_send(service, &request, sizeof(request), handles, 2u);
    if (status != ASTRA_SYSCALL_OK)
        goto done;
    event_send = 0u;
    reply_send = 0u;
    status = astra_wait_one(reply_receive, ASTRA_DEADLINE_FOREVER, NULL);
    if (status != ASTRA_SYSCALL_OK)
        goto done;
    status = astra_port_receive(reply_receive, &reply, sizeof(reply), NULL,
                                0u, &size, &handle_count);
    if (status != ASTRA_SYSCALL_OK)
        goto done;
    if (size != sizeof(reply) || handle_count != 0u ||
        reply.header.total_size != sizeof(reply) ||
        reply.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        reply.header.flags != 0u ||
        reply.header.protocol != ASTRA_INPUT_SERVICE_PROTOCOL ||
        reply.header.protocol_version != ASTRA_INPUT_SERVICE_VERSION ||
        reply.header.reserved != 0u ||
        reply.header.operation != ASTRA_INPUT_OPERATION_CONNECTED ||
        reply.header.transaction_id != request.header.transaction_id ||
        reply.status != ASTRA_STATUS_OK || reply.client == 0u ||
        reply.generation == 0u) {
        status = DISPLAY_FAIL_PROTOCOL;
        goto done;
    }
    *receive_out = event_receive;
    event_receive = 0u;

done:
    if (event_receive != 0u)
        (void)astra_close(event_receive);
    if (event_send != 0u)
        (void)astra_close(event_send);
    if (reply_receive != 0u)
        (void)astra_close(reply_receive);
    if (reply_send != 0u)
        (void)astra_close(reply_send);
    return status;
}

static uint32_t receive_input(uint32_t receive,
                              AstraLogicalInputEvent *event)
{
    AstraInputEventMessage message = {0};
    uint32_t size = 0u;
    uint32_t handles = 0u;
    uint32_t status = astra_port_receive(
        receive, &message, sizeof(message), NULL, 0u, &size, &handles);

    if (status != ASTRA_SYSCALL_OK)
        return status;
    if (size != sizeof(message) || handles != 0u ||
        message.header.total_size != sizeof(message) ||
        message.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        message.header.flags != 0u ||
        message.header.protocol != ASTRA_INPUT_SERVICE_PROTOCOL ||
        message.header.protocol_version != ASTRA_INPUT_SERVICE_VERSION ||
        message.header.reserved != 0u ||
        message.header.operation != ASTRA_INPUT_OPERATION_EVENT ||
        message.event.size != sizeof(message.event) ||
        message.event.version != ASTRA_INPUT_SERVICE_VERSION ||
        message.event.type < ASTRA_INPUT_EVENT_KEY ||
        message.event.type > ASTRA_INPUT_EVENT_STATE_RESET)
        return DISPLAY_FAIL_PROTOCOL;
    *event = message.event;
    return ASTRA_STATUS_OK;
}

static uint32_t display_wait_handles(const DisplayState *state,
                                     uint32_t gui_receive,
                                     uint32_t input_receive,
                                     uint32_t first,
                                     uint32_t *waits,
                                     uint32_t *sources)
{
    uint32_t count = state->count + 2u;

    first %= count;
    for (uint32_t slot = 0u; slot < count; ++slot) {
        uint32_t source = first + slot;

        if (source >= count)
            source -= count;
        sources[slot] = source;
        waits[slot] = source == 0u ? gui_receive :
                      source == 1u ? input_receive :
                      state->windows[source - 2u].control_receive;
    }
    return count;
}

static void serve_windows(uint32_t device, uint32_t irq,
                          AstraDmaBufferInfo *framebuffer,
                          uint32_t gui_receive, uint32_t input_receive)
{
    DisplayState state = {
        .damage = {
            { 0, 0, ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT, 1u },
            { 0, 0, ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT, 1u }
        }
    };
    uint32_t next_fence = 1u;
    uint32_t cursor_fence = UINT32_C(0x80000001);
    uint32_t armed = 0u;
    uint32_t first_wait = 0u;

    for (;;) {
        uint32_t waits[DISPLAY_WINDOW_MAX + 2u];
        uint32_t sources[DISPLAY_WINDOW_MAX + 2u];
        uint32_t wait_count = display_wait_handles(
            &state, gui_receive, input_receive, first_wait, waits, sources);
        uint32_t selected = 0u;
        uint32_t status;

        status = astra_wait_multiple(waits, wait_count,
                                     ASTRA_DEADLINE_FOREVER,
                                     &selected, NULL);
        if (status != ASTRA_SYSCALL_OK || selected >= wait_count)
            astra_process_exit(DISPLAY_FAIL_WAIT);
        selected = sources[selected];
        first_wait = (selected + 1u) % wait_count;
        if (selected == 0u)
            receive_open(device, irq, framebuffer, &state, gui_receive,
                         &next_fence, &armed);
        else if (selected == 1u) {
            uint32_t effects = 0u;
            uint32_t frame_window = 0u;
            uint32_t frame_timestamp = 0u;

            for (uint32_t drained = 0u; drained < DISPLAY_INPUT_QUEUE;
                 ++drained) {
                AstraLogicalInputEvent event;

                status = receive_input(input_receive, &event);
                if (drained != 0u && status == ASTRA_SYSCALL_WOULD_BLOCK)
                    break;
                if (status != ASTRA_STATUS_OK ||
                    handle_pointer(&state, &event, &effects, &frame_window,
                                   &frame_timestamp) != ASTRA_STATUS_OK)
                    astra_process_exit(DISPLAY_FAIL_PROTOCOL);
            }
            if ((effects & DISPLAY_POINTER_CURSOR) != 0u &&
                update_cursor(device, irq, state.pointer_x, state.pointer_y,
                              1u, 0u,
                              &cursor_fence, &armed) != ASTRA_STATUS_OK)
                astra_process_exit(DISPLAY_FAIL_COMPLETION);
            if ((effects & DISPLAY_POINTER_RENDER) != 0u &&
                render(device, irq, framebuffer, &state,
                       &next_fence, &armed) != ASTRA_STATUS_OK)
                astra_process_exit(DISPLAY_FAIL_COMPLETION);
            if ((effects & DISPLAY_POINTER_FRAME) != 0u) {
                uint32_t index = find_id(&state, frame_window);

                if (index != state.count)
                    frame_event(&state.windows[index], frame_timestamp,
                                index);
            }
        } else
            receive_command(device, irq, framebuffer, &state, selected - 2u,
                            &next_fence, &armed);
    }
}

int astra_main(const AstraStartupInfo *startup)
{
    AstraDmaBufferInfo framebuffer;
    const AstraStartupCapability *bootstrap;
    const AstraStartupCapability *device;
    const AstraStartupCapability *irq;
    const AstraStartupCapability *input_service;
    uint32_t gui_receive = 0u;
    uint32_t gui_send = 0u;
    uint32_t input_receive = 0u;
    uint32_t status;

    if (!astra_startup_validate(startup) ||
        startup->capabilities_address == 0u)
        return ASTRA_STATUS_INVALID;
    bootstrap = capability(startup, ASTRA_CAPABILITY_SERVICE_READY);
    device = capability(startup, ASTRA_CAPABILITY_DISPLAY_DEVICE);
    irq = capability(startup, ASTRA_CAPABILITY_DISPLAY_IRQ);
    input_service = capability(startup, ASTRA_CAPABILITY_INPUT_SERVICE);
    if (bootstrap == NULL || device == NULL || irq == NULL ||
        input_service == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    status = astra_dma_create(ASTRA_RENDER_BUILDER_BYTES, &framebuffer);
    if (status == ASTRA_SYSCALL_OK)
        status = connect_input(input_service->handle, &input_receive);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_rt_port_create(4u, 4u * ASTRA_GUI_OPEN_WINDOW_SIZE,
                                   &gui_receive, &gui_send);
    if (status == ASTRA_SYSCALL_OK)
        status = ready(bootstrap->handle, ASTRA_STATUS_OK, gui_send);
    else
        (void)ready(bootstrap->handle, status, 0u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_SYSCALL_OK) {
        if (gui_send != 0u)
            (void)astra_close(gui_send);
        if (gui_receive != 0u)
            (void)astra_close(gui_receive);
        if (input_receive != 0u)
            (void)astra_close(input_receive);
        (void)astra_device_reset(device->handle);
        return (int)status;
    }
    serve_windows(device->handle, irq->handle, &framebuffer, gui_receive,
                  input_receive);
    return ASTRA_STATUS_OK;
}
