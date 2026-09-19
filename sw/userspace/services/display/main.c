#include <astra/bundle.h>
#include <astra/display.h>
#include <astra/gui.h>
#include <astra/input_service.h>
#include <astra/program.h>
#include <astra/render_batch.h>
#include <astra/render_builder.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/surface.h>
#include <astra/theme.h>
#include <astra/utf8.h>
#include <astra/window.h>
#include <astra/window_scene.h>

#define DISPLAY_WORK_TOP 34u
#define DISPLAY_WORK_BOTTOM (ASTRA_DISPLAY_HEIGHT - 42u)
#define DISPLAY_MEDIA_BASE ASTRA_RENDER_BATCH_WORKSPACE_LIMIT
#define DISPLAY_MEDIA_LIMIT ASTRA_RENDER_BATCH_MEDIA_LIMIT
#define DISPLAY_MEDIA_ALIGNMENT UINT32_C(64)
#define WINDOW_SURFACE_MAX_BYTES \
    (ASTRA_DISPLAY_WIDTH * (DISPLAY_WORK_BOTTOM - DISPLAY_WORK_TOP) * 2u)
#define DISPLAY_IRQ_DRAIN_MAX 8u
#define DISPLAY_INPUT_QUEUE 8u
#define DISPLAY_DOUBLE_CLICK_MS 500u
#define DISPLAY_DOUBLE_CLICK_DISTANCE 4

_Static_assert(ASTRA_RENDER_BATCH_SCANOUT1_OFFSET +
                       ASTRA_RENDER_BATCH_SCANOUT_BYTES <=
                   ASTRA_RENDER_BATCH_ARENA_OFFSET,
               "scanouts overlap the render arena");
_Static_assert(ASTRA_RENDER_BATCH_ARENA_OFFSET +
                       ASTRA_RENDER_BUILDER_BYTES <= DISPLAY_MEDIA_BASE,
               "render arena overlaps persistent media");
_Static_assert(WINDOW_SURFACE_MAX_BYTES <=
                   DISPLAY_MEDIA_LIMIT - DISPLAY_MEDIA_BASE,
               "window surface does not fit Media RAM");
_Static_assert(ASTRA_DISPLAY_CURSOR_IMAGE_WIDTH ==
                   ASTRA_HARDWARE_POINTER_WIDTH &&
                   ASTRA_DISPLAY_CURSOR_IMAGE_HEIGHT ==
                       ASTRA_HARDWARE_POINTER_HEIGHT,
               "NDK and display pointer dimensions disagree");

typedef struct DamageRect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
    uint32_t valid;
} DamageRect;

typedef struct TitleIconRun {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint8_t matched;
} TitleIconRun;

#define TITLE_ICON_RUN_MAX ((ASTRA_AICON_STRIKE_WIDTH_MAX + 1u) / 2u)

typedef struct DisplayWindow {
    AstraGuiOpenWindow request;
    AstraSharedSurface surface;
    uint32_t title_icon_area;
    void *title_icon_bytes;
    AstraAicon title_icon;
    AstraAiconStrike title_icon_strike;
    uint32_t id;
    uint32_t generation;
    uint32_t control_receive;
    uint32_t event_send;
    uint32_t vblank_signal;
    uint32_t event_sequence;
    uint32_t cache_offset[2];
    uint32_t cache_bytes;
    uint32_t content_offset;
    uint32_t content_bytes;
    uint32_t cache_resource[2];
    uint32_t content_resource;
    uint16_t restore_x;
    uint16_t restore_y;
    uint16_t restore_width;
    uint16_t restore_height;
    uint8_t state;
    uint8_t restore_state;
    uint8_t cache_dirty[2];
    uint8_t cache_active;
    uint8_t cache_valid;
    uint8_t cache_pending;
    uint8_t content_pending;
    uint8_t content_dirty;
    uint8_t content_initialized;
    uint8_t event_lost;
    uint8_t pending_close;
    uint32_t pending_close_timestamp_ms;
    uint32_t pointer_shape;
    uint32_t pointer_image_area;
    const AstraColorRGBA8 *pointer_image;
    uint32_t pointer_image_bytes;
    uint16_t pointer_hot_x;
    uint16_t pointer_hot_y;
    uint32_t pointer_image_generation;
    DamageRect content_damage;
    DamageRect cache_damage[2];
} DisplayWindow;

typedef struct DisplayState {
    DisplayWindow *windows;
    uint32_t windows_area;
    uint32_t capacity;
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
    uint32_t last_click_window;
    uint32_t last_click_button;
    uint32_t last_click_timestamp;
    int32_t last_click_x;
    int32_t last_click_y;
    uint32_t click_count;
    uint32_t loaded_pointer_shape;
    uint32_t loaded_pointer_window;
    uint32_t loaded_pointer_generation;
    uint32_t system_offset[2];
    uint32_t system_capacity[2];
    uint32_t system_resource[2];
    uint32_t scene_offset[2];
    uint32_t scene_capacity[2];
    uint8_t scene_active;
    uint8_t scene_pending;
    uint8_t scene_valid;
    uint8_t system_initialized;
    uint8_t system_pending;
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
    DISPLAY_POINTER_RESIZE = 1u << 3,
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

static uint32_t display_windows_reserve(DisplayState *state,
                                        uint32_t minimum)
{
    DisplayWindow *replacement = NULL;
    uint32_t replacement_area = 0u;
    uint32_t mapped_bytes = 0u;
    uint32_t requested_bytes;
    uint32_t status;

    if (state == NULL || minimum < state->count)
        return ASTRA_STATUS_INVALID;
    if (minimum <= state->capacity)
        return ASTRA_STATUS_OK;
    if (minimum > UINT32_MAX / (uint32_t)sizeof(DisplayWindow))
        return ASTRA_STATUS_LIMIT;
    requested_bytes = minimum * (uint32_t)sizeof(DisplayWindow);
    status = astra_rt_area_create(
        requested_bytes,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP,
        &replacement_area);
    if (status != ASTRA_SYSCALL_OK)
        return service_status(status);
    status = astra_rt_area_map(
        replacement_area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
        (void **)&replacement, &mapped_bytes);
    if (status != ASTRA_SYSCALL_OK ||
        mapped_bytes / sizeof(DisplayWindow) < minimum) {
        if (replacement != NULL)
            (void)astra_rt_area_unmap(replacement);
        (void)astra_close(replacement_area);
        return status == ASTRA_SYSCALL_OK ? ASTRA_STATUS_LIMIT :
                                           service_status(status);
    }
    for (uint32_t index = 0u; index < state->count; ++index)
        replacement[index] = state->windows[index];
    for (uint32_t index = state->count;
         index < mapped_bytes / sizeof(DisplayWindow); ++index)
        replacement[index] = (DisplayWindow){0};
    if (state->windows != NULL) {
        status = astra_rt_area_unmap(state->windows);
        if (status != ASTRA_SYSCALL_OK) {
            (void)astra_rt_area_unmap(replacement);
            (void)astra_close(replacement_area);
            return service_status(status);
        }
    }
    if (state->windows_area != 0u) {
        status = astra_close(state->windows_area);
        if (status != ASTRA_SYSCALL_OK) {
            state->windows = replacement;
            state->windows_area = replacement_area;
            state->capacity = mapped_bytes / sizeof(DisplayWindow);
            return service_status(status);
        }
    }
    state->windows = replacement;
    state->windows_area = replacement_area;
    state->capacity = mapped_bytes / sizeof(DisplayWindow);
    return ASTRA_STATUS_OK;
}

static uint32_t align_media_bytes(uint32_t bytes)
{
    return (bytes + DISPLAY_MEDIA_ALIGNMENT - 1u) &
           ~(DISPLAY_MEDIA_ALIGNMENT - 1u);
}

static int media_extents_overlap(uint32_t left_offset, uint32_t left_bytes,
                                 uint32_t right_offset,
                                 uint32_t right_bytes)
{
    return left_bytes != 0u && right_bytes != 0u &&
           left_offset < right_offset + right_bytes &&
           right_offset < left_offset + left_bytes;
}

static uint32_t media_advance_extent(uint32_t offset, uint32_t bytes,
                                     uint32_t extent_offset,
                                     uint32_t extent_bytes, uint32_t next)
{
    if (media_extents_overlap(offset, bytes, extent_offset, extent_bytes) &&
        extent_offset + extent_bytes > next)
        next = extent_offset + extent_bytes;
    return next;
}

static uint32_t media_advance_past(const DisplayWindow *window,
                                   uint32_t offset, uint32_t bytes,
                                   uint32_t next)
{
    next = media_advance_extent(offset, bytes, window->content_offset,
                                window->content_bytes, next);
    for (uint32_t bank = 0u; bank < 2u; ++bank)
        next = media_advance_extent(offset, bytes,
                                    window->cache_offset[bank],
                                    window->cache_bytes, next);
    return next;
}

static uint32_t display_media_allocate(const DisplayState *state,
                                       const DisplayWindow *pending,
                                       uint32_t excluded_scene,
                                       uint32_t bytes)
{
    uint32_t offset = DISPLAY_MEDIA_BASE;

    bytes = align_media_bytes(bytes);
    if (state == NULL || bytes == 0u ||
        bytes > DISPLAY_MEDIA_LIMIT - DISPLAY_MEDIA_BASE)
        return 0u;
    while (offset <= DISPLAY_MEDIA_LIMIT - bytes) {
        uint32_t next = offset;

        for (uint32_t index = 0u; index < state->count; ++index)
            next = media_advance_past(&state->windows[index], offset,
                                      bytes, next);
        if (pending != NULL)
            next = media_advance_past(pending, offset, bytes, next);
        for (uint32_t index = 0u; index < 2u; ++index) {
            next = media_advance_extent(offset, bytes,
                                        state->system_offset[index],
                                        state->system_capacity[index], next);
            if (index != excluded_scene)
                next = media_advance_extent(offset, bytes,
                                            state->scene_offset[index],
                                            state->scene_capacity[index],
                                            next);
        }
        if (next == offset)
            return offset;
        offset = align_media_bytes(next);
    }
    return 0u;
}

static uint16_t title_height(const AstraTheme *theme, uint8_t type)
{
    if (type == ASTRA_WINDOW_POPOVER || type == ASTRA_WINDOW_FULLSCREEN ||
        type == ASTRA_WINDOW_DESKTOP)
        return 0u;
    return type == ASTRA_WINDOW_UTILITY ? theme->utility_titlebar_height :
                                         theme->titlebar_height;
}

static uint16_t frame_width(const AstraTheme *theme, uint8_t type)
{
    return type == ASTRA_WINDOW_FULLSCREEN || type == ASTRA_WINDOW_DESKTOP ?
        0u : theme->frame_width;
}

static uint16_t window_radius(const AstraTheme *theme,
                              const DisplayWindow *window)
{
    return window->state == ASTRA_WINDOW_STATE_MAXIMIZED ||
           window->request.type == ASTRA_WINDOW_FULLSCREEN ||
           window->request.type == ASTRA_WINDOW_DESKTOP ?
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

static int decorated(const DisplayWindow *window)
{
    return window->request.type != ASTRA_WINDOW_FULLSCREEN &&
           window->request.type != ASTRA_WINDOW_DESKTOP;
}

static uint32_t display_window_media_prepare(const DisplayState *state,
                                             DisplayWindow *candidate)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    uint32_t content_bytes;
    uint32_t cache_bytes;

    if (state == NULL || candidate == NULL ||
        candidate->request.width == 0u || candidate->request.height == 0u)
        return ASTRA_STATUS_INVALID;
    content_bytes = align_media_bytes(
        (uint32_t)candidate->request.width * candidate->request.height * 2u);
    cache_bytes = align_media_bytes(
        outer_width(&theme, candidate) * outer_height(&theme, candidate) *
        2u);
    if (content_bytes > candidate->content_bytes) {
        candidate->content_offset = 0u;
        candidate->content_bytes = 0u;
        candidate->content_offset = display_media_allocate(
            state, candidate, UINT32_MAX, content_bytes);
        if (candidate->content_offset == 0u)
            return ASTRA_STATUS_LIMIT;
    }
    candidate->content_bytes = content_bytes;
    if (cache_bytes > candidate->cache_bytes) {
        candidate->cache_offset[0] = 0u;
        candidate->cache_offset[1] = 0u;
        candidate->cache_bytes = 0u;
        candidate->cache_bytes = cache_bytes;
        for (uint32_t bank = 0u; bank < 2u; ++bank) {
            candidate->cache_offset[bank] = display_media_allocate(
                state, candidate, UINT32_MAX, cache_bytes);
            if (candidate->cache_offset[bank] == 0u)
                return ASTRA_STATUS_LIMIT;
        }
    }
    candidate->cache_bytes = cache_bytes;
    return ASTRA_STATUS_OK;
}

static uint32_t display_system_media_prepare(DisplayState *state)
{
    const uint32_t heights[2] = {
        DISPLAY_WORK_TOP, ASTRA_DISPLAY_HEIGHT - DISPLAY_WORK_BOTTOM
    };

    for (uint32_t index = 0u; index < 2u; ++index) {
        uint32_t bytes = align_media_bytes(
            ASTRA_DISPLAY_WIDTH * heights[index] * 2u);

        if (state->system_capacity[index] >= bytes)
            continue;
        state->system_offset[index] = display_media_allocate(
            state, NULL, UINT32_MAX, bytes);
        if (state->system_offset[index] == 0u)
            return ASTRA_STATUS_LIMIT;
        state->system_capacity[index] = bytes;
    }
    return ASTRA_STATUS_OK;
}

static uint32_t display_scene_prepare(DisplayState *state,
                                      uint32_t layer_count)
{
    uint32_t bank = state->scene_valid != 0u ?
        (state->scene_active ^ 1u) : 0u;
    uint32_t required = astra_window_scene_compiled_capacity(
        ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT, layer_count);

    if (required == 0u)
        return ASTRA_STATUS_LIMIT;
    if (state->scene_capacity[bank] < required) {
        uint32_t offset = display_media_allocate(
            state, NULL, bank, required);

        if (offset == 0u)
            return ASTRA_STATUS_LIMIT;
        state->scene_offset[bank] = offset;
        state->scene_capacity[bank] = required;
    }
    state->scene_pending = (uint8_t)bank;
    return ASTRA_STATUS_OK;
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
    damage_add(&window->cache_damage[0], damage);
    damage_add(&window->cache_damage[1], damage);
    screen.left += window->request.x + frame;
    screen.right += window->request.x + frame;
    screen.top += window->request.y + frame + title + signal;
    screen.bottom += window->request.y + frame + title + signal;
    damage_both(state, screen);
}

static void dirty_cache(DisplayWindow *window)
{
    window->cache_dirty[0] = 1u;
    window->cache_dirty[1] = 1u;
    window->cache_damage[0] = (DamageRect){0};
    window->cache_damage[1] = (DamageRect){0};
}

static void reset_content(DisplayWindow *window)
{
    window->content_initialized = 0u;
    window->content_dirty = 1u;
    window->content_damage = (DamageRect){
        0, 0, window->request.width, window->request.height, 1u
    };
    dirty_cache(window);
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
        event->type == ASTRA_WINDOW_EVENT_STATE ?
            ASTRA_WINDOW_SUBSCRIBE_STATE :
        event->type == ASTRA_WINDOW_EVENT_RESIZE ?
            ASTRA_WINDOW_SUBSCRIBE_RESIZE :
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
    astra_message_header_set(&message.header, sizeof(message),
                             ASTRA_GUI_PROTOCOL, ASTRA_GUI_VERSION,
                             ASTRA_GUI_WINDOW_EVENT, event->sequence);
    message.event = *event;
    status = astra_port_send(window->event_send, &message, sizeof(message),
                             NULL, 0u);
    if (status == ASTRA_SYSCALL_OK) {
        window->event_lost = 0u;
        if (event->type == ASTRA_WINDOW_EVENT_CLOSE_REQUEST)
            window->pending_close = 0u;
    } else {
        window->event_lost = 1u;
        if (event->type == ASTRA_WINDOW_EVENT_CLOSE_REQUEST) {
            window->pending_close = 1u;
            window->pending_close_timestamp_ms = event->timestamp_ms;
        }
    }
    return status;
}

static void state_event(DisplayWindow *window, uint32_t timestamp_ms,
                        uint32_t z_order)
{
    AstraWindowEvent event = {
        .type = ASTRA_WINDOW_EVENT_STATE,
        .timestamp_ms = timestamp_ms,
    };

    event.data.state.frame = (AstraWindowFrame){
        window->request.x, window->request.y,
        window->request.width, window->request.height
    };
    event.data.state.state = window->state;
    event.data.state.flags = window->request.flags;
    event.data.state.z_order = z_order;
    (void)send_event(window, &event);
}

static void resize_event(DisplayWindow *window, uint32_t timestamp_ms)
{
    AstraWindowEvent event = {
        .type = ASTRA_WINDOW_EVENT_RESIZE,
        .timestamp_ms = timestamp_ms,
    };

    event.data.resize.width = window->request.width;
    event.data.resize.height = window->request.height;
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

static int activate(DisplayState *state, const AstraTheme *theme,
                    uint32_t id, int raise, uint32_t timestamp_ms)
{
    uint32_t index = find_id(state, id);
    int changed = 0;

    if (id != 0u && index == state->count)
        return 0;
    if (id != 0u &&
        state->windows[index].request.type == ASTRA_WINDOW_DESKTOP) {
        id = 0u;
        raise = 0;
    }
    if (id != 0u && raise) {
        changed |= index != state->count - 1u;
        reorder(state, theme, index, state->count - 1u);
        index = state->count - 1u;
    }
    for (uint32_t at = 0u; at < state->count; ++at) {
        DisplayWindow *window = &state->windows[at];
        uint32_t active = window->id == id &&
                          window->state != ASTRA_WINDOW_STATE_MINIMIZED;

        if (((window->request.flags & ASTRA_WINDOW_ACTIVE) != 0u) == active)
            continue;
        /* An undecorated window draws the same either way, so flipping its
           focus must not cost a repaint of everything it covers. */
        if (decorated(window)) {
            damage_window(state, theme, window);
            dirty_cache(window);
        }
        if (active)
            window->request.flags |= ASTRA_WINDOW_ACTIVE;
        else
            window->request.flags &= ~ASTRA_WINDOW_ACTIVE;
        next_generation(window);
        state_event(window, timestamp_ms, at);
        if (decorated(window))
            damage_window(state, theme, window);
        changed = 1;
    }
    return changed;
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

static uint32_t prepare_geometry(const DisplayState *state,
                                 const DisplayWindow *window,
                                 uint16_t x, uint16_t y,
                                 uint16_t width, uint16_t height,
                                 uint8_t window_state,
                                 DisplayWindow *prepared)
{
    *prepared = *window;
    prepared->request.x = x;
    prepared->request.y = y;
    prepared->request.width = width;
    prepared->request.height = height;
    prepared->state = window_state;
    return display_window_media_prepare(state, prepared);
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
    dirty_cache(window);
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
    DisplayWindow prepared;

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
    if (prepare_geometry(state, window, (uint16_t)x, (uint16_t)y,
                         (uint16_t)width, (uint16_t)height,
                         ASTRA_WINDOW_STATE_NORMAL, &prepared) !=
        ASTRA_STATUS_OK)
        return 0;
    damage_window(state, theme, window);
    *window = prepared;
    dirty_cache(window);
    reset_content(window);
    next_generation(window);
    damage_window(state, theme, window);
    return 1;
}

static void pointer_event(DisplayWindow *window, const AstraTheme *theme,
                          uint16_t type, uint32_t flags,
                          uint32_t timestamp_ms, int32_t screen_x,
                          int32_t screen_y, uint32_t button,
                          uint32_t click_count, uint32_t modifiers)
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
    event.data.pointer.click_count = click_count;
    event.data.pointer.modifiers = modifiers;
    (void)send_event(window, &event);
}

static uint32_t register_click(DisplayState *state, uint32_t window,
                               uint32_t button, uint32_t timestamp_ms)
{
    int32_t dx = state->pointer_x - state->last_click_x;
    int32_t dy = state->pointer_y - state->last_click_y;
    uint32_t elapsed = timestamp_ms - state->last_click_timestamp;

    if (state->last_click_window == window &&
        state->last_click_button == button &&
        elapsed <= DISPLAY_DOUBLE_CLICK_MS &&
        dx >= -DISPLAY_DOUBLE_CLICK_DISTANCE &&
        dx <= DISPLAY_DOUBLE_CLICK_DISTANCE &&
        dy >= -DISPLAY_DOUBLE_CLICK_DISTANCE &&
        dy <= DISPLAY_DOUBLE_CLICK_DISTANCE)
        ++state->click_count;
    else
        state->click_count = 1u;
    state->last_click_window = window;
    state->last_click_button = button;
    state->last_click_timestamp = timestamp_ms;
    state->last_click_x = state->pointer_x;
    state->last_click_y = state->pointer_y;
    return state->click_count;
}

static void wheel_event(DisplayWindow *window, const AstraTheme *theme,
                        uint32_t timestamp_ms, int32_t screen_x,
                        int32_t screen_y, int32_t dx, int32_t dy,
                        uint32_t modifiers)
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
    event.data.wheel.modifiers = modifiers;
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
        event.data.key.modifiers = input->modifiers;
    } else {
        event.data.text.codepoint = input->code;
        event.data.text.modifiers = input->modifiers;
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

static int draw_title_icon(AstraRenderBuilder *builder, uint32_t destination,
                           const DisplayWindow *window, int32_t x, int32_t y)
{
    const AstraAiconStrike *strike = &window->title_icon_strike;

    if (strike->width > ASTRA_AICON_STRIKE_WIDTH_MAX)
        return 0;
    for (uint16_t color_index = 1u;
         color_index < window->title_icon.palette_count; ++color_index) {
        TitleIconRun active[TITLE_ICON_RUN_MAX];
        uint32_t active_count = 0u;
        uint8_t rgba[4];
        uint16_t pixel;

        if (astra_aicon_palette(&window->title_icon, color_index, rgba) !=
                ASTRA_BUNDLE_OK || rgba[3] == 0u)
            continue;
        pixel = astra_surface_rgb565(rgba[0], rgba[1], rgba[2]);
        for (uint16_t row = 0u; row < strike->height; ++row) {
            TitleIconRun current[TITLE_ICON_RUN_MAX];
            uint32_t current_count = 0u;
            uint16_t column = 0u;

            for (uint32_t at = 0u; at < active_count; ++at)
                active[at].matched = 0u;
            while (column < strike->width) {
                uint16_t start;
                uint32_t match = active_count;

                while (column < strike->width && strike->pixels[
                           (uint32_t)row * strike->width + column] !=
                           color_index)
                    ++column;
                start = column;
                while (column < strike->width && strike->pixels[
                           (uint32_t)row * strike->width + column] ==
                           color_index)
                    ++column;
                if (start == column)
                    break;
                for (uint32_t at = 0u; at < active_count; ++at)
                    if (active[at].x == start &&
                        active[at].width == column - start &&
                        active[at].matched == 0u) {
                        match = at;
                        break;
                    }
                if (current_count == sizeof(current) / sizeof(current[0]))
                    return 0;
                if (match != active_count) {
                    active[match].matched = 1u;
                    ++active[match].height;
                    current[current_count++] = active[match];
                } else {
                    current[current_count++] = (TitleIconRun){
                        start, row, (uint16_t)(column - start), 1u, 1u};
                }
            }
            for (uint32_t at = 0u; at < active_count; ++at)
                if (active[at].matched == 0u &&
                    !astra_render_builder_fill(
                        builder, destination, x + active[at].x,
                        y + active[at].y, active[at].width,
                        active[at].height, pixel))
                    return 0;
            active_count = current_count;
            for (uint32_t at = 0u; at < current_count; ++at)
                active[at] = current[at];
        }
        for (uint32_t at = 0u; at < active_count; ++at)
            if (!astra_render_builder_fill(
                    builder, destination, x + active[at].x,
                    y + active[at].y, active[at].width,
                    active[at].height, pixel))
                return 0;
    }
    return 1;
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
    if (request->type == ASTRA_WINDOW_FULLSCREEN ||
        request->type == ASTRA_WINDOW_DESKTOP)
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
        int32_t text_x = client_x + theme->spacing_unit * 2;
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
        if (window->title_icon_area != 0u) {
            int32_t icon_x = client_x + theme->spacing_unit;
            int32_t icon_y = client_y + ((int32_t)title - 16) / 2;

            if (!draw_title_icon(builder, cache, window, icon_x, icon_y))
                return 0;
            text_x = icon_x + 16 + theme->spacing_unit;
            text_capacity += 16u + theme->spacing_unit;
        }
        text_capacity = request->width > text_capacity ?
                        request->width - text_capacity : 0u;
        text_length = astra_surface_ui_text_fit(
            request->title, request->title_length,
            ASTRA_THEME_SYSTEM_TITLE_FONT_HEIGHT, text_capacity);
        (void)astra_render_builder_text(
            builder, cache, text_x,
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
                                const DisplayWindow *window,
                                const DamageRect *damage)
{
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

static int build_system_surfaces(AstraRenderBuilder *builder,
                                 DisplayState *state,
                                 const AstraTheme *theme)
{
    state->system_resource[0] = astra_render_builder_surface_at(
        builder, state->system_offset[0], state->system_capacity[0],
        ASTRA_DISPLAY_WIDTH, DISPLAY_WORK_TOP);
    state->system_resource[1] = astra_render_builder_surface_at(
        builder, state->system_offset[1], state->system_capacity[1],
        ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT - DISPLAY_WORK_BOTTOM);
    if (state->system_resource[0] == 0u ||
        state->system_resource[1] == 0u)
        return 0;
    state->system_pending = state->system_initialized == 0u;
    if (state->system_pending == 0u)
        return 1;
    return astra_render_builder_fill(
               builder, state->system_resource[0], 0, 0,
               ASTRA_DISPLAY_WIDTH, DISPLAY_WORK_TOP,
               color(theme->system_bar)) &&
           astra_render_builder_fill(
               builder, state->system_resource[0], 20, 32, 124u, 2u,
               color(theme->accent)) &&
           astra_render_builder_text(
               builder, state->system_resource[0], 20, 10, "ASTRA", 5u,
               ASTRA_THEME_SYSTEM_TITLE_FONT_HEIGHT,
               color(theme->text_primary)) &&
           astra_render_builder_fill(
               builder, state->system_resource[1], 0, 0,
               ASTRA_DISPLAY_WIDTH,
               ASTRA_DISPLAY_HEIGHT - DISPLAY_WORK_BOTTOM,
               color(theme->system_bar));
}

static uint32_t compose_failed(const AstraRenderBuilder *builder,
                               uint32_t *error, uint32_t *failure,
                               uint32_t status)
{
    *error = status;
    if (failure != NULL)
        *failure = builder->failed;
    return 0u;
}

static uint32_t display_pointer_shape(DisplayState *state,
                                      const AstraTheme *theme);

static uint32_t compose(void *storage, uint32_t fence,
                        DisplayState *state, uint32_t *error,
                        uint32_t *failure, int include_cursor)
{
    AstraRenderBuilder builder = {0};
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    uint32_t buffer = (fence & 1u) != 0u ? 1u : 0u;
    DamageRect *damage = &state->damage[buffer];
    uint32_t layer_count = 2u;
    uint32_t status;

    if (error == NULL)
        return 0u;
    *error = ASTRA_STATUS_OK;
    if (failure != NULL)
        *failure = ASTRA_RENDER_BUILDER_FAILURE_NONE;
    for (uint32_t index = 0u; index < state->count; ++index)
        if (state->windows[index].state != ASTRA_WINDOW_STATE_MINIMIZED)
            ++layer_count;
    status = display_system_media_prepare(state);
    if (status == ASTRA_STATUS_OK)
        status = display_scene_prepare(state, layer_count);
    if (damage->valid == 0u || status != ASTRA_STATUS_OK ||
        !astra_render_builder_init(
            &builder, storage, ASTRA_RENDER_BUILDER_BYTES, fence)) {
        return compose_failed(&builder, error, failure,
                              status != ASTRA_STATUS_OK ? status :
                                                         ASTRA_STATUS_INVALID);
    }
    if (!build_system_surfaces(&builder, state, &theme))
        return compose_failed(&builder, error, failure,
                              ASTRA_STATUS_LIMIT);
    for (uint32_t index = 0u; index < state->count; ++index) {
        DisplayWindow *window = &state->windows[index];
        const AstraDrawListHeader *list =
            (const AstraDrawListHeader *)(const void *)
                window->surface.view.pixels;
        uint32_t cache_bank;

        window->cache_pending = 2u;
        window->content_pending = 0u;

        window->content_resource = astra_render_builder_surface_at(
            &builder, window->content_offset, window->content_bytes,
            window->request.width, window->request.height);
        if (window->content_resource == 0u ||
            (window->content_initialized == 0u &&
             !(window->content_dirty != 0u &&
               astra_draw_list_covers(list, window->request.width,
                                      window->request.height)) &&
             !astra_render_builder_fill(
                 &builder, window->content_resource, 0, 0,
                 window->request.width,
                 window->request.height, color(theme.client))) ||
            (window->content_dirty != 0u &&
             !astra_render_builder_replay(
                 &builder, window->content_resource, list))) {
            return compose_failed(
                &builder, error, failure,
                builder.failed != 0u ? ASTRA_STATUS_LIMIT :
                                       ASTRA_STATUS_PROTOCOL);
        }
        window->content_pending = window->content_dirty;
        if (window->state == ASTRA_WINDOW_STATE_MINIMIZED)
            continue;
        cache_bank = window->cache_valid != 0u &&
                     window->cache_dirty[window->cache_active] == 0u &&
                     window->cache_damage[window->cache_active].valid == 0u ?
                         window->cache_active :
                     window->cache_valid != 0u ?
                         window->cache_active ^ 1u : 0u;
        window->cache_resource[cache_bank] =
            astra_render_builder_surface_at(
                &builder, window->cache_offset[cache_bank],
                window->cache_bytes,
                (uint16_t)outer_width(&theme, window),
                (uint16_t)outer_height(&theme, window));
        if (window->cache_resource[cache_bank] == 0u)
            return compose_failed(&builder, error, failure,
                                  ASTRA_STATUS_LIMIT);
        if ((window->cache_dirty[cache_bank] != 0u ||
             (window->cache_valid & (1u << cache_bank)) == 0u) &&
            !build_cache(&builder, window->cache_resource[cache_bank],
                         window->content_resource, &theme, window)) {
            return compose_failed(
                &builder, error, failure,
                builder.failed != 0u ? ASTRA_STATUS_LIMIT :
                                       ASTRA_STATUS_PROTOCOL);
        }
        if (window->cache_dirty[cache_bank] == 0u &&
            window->cache_damage[cache_bank].valid != 0u &&
            !update_cache_content(&builder,
                                  window->cache_resource[cache_bank],
                                  window->content_resource,
                                  &theme, window,
                                  &window->cache_damage[cache_bank])) {
            return compose_failed(
                &builder, error, failure,
                builder.failed != 0u ? ASTRA_STATUS_LIMIT :
                                       ASTRA_STATUS_PROTOCOL);
        }
        window->cache_pending = (uint8_t)cache_bank;
    }
    if (!astra_render_builder_window_scene(
            &builder, state->scene_offset[state->scene_pending],
            state->scene_capacity[state->scene_pending],
            color(theme.canvas)) ||
        !astra_render_builder_window_scene_layer(
            &builder, state->system_resource[0], 0, 0, 0u, 1) ||
        !astra_render_builder_window_scene_layer(
            &builder, state->system_resource[1], 0, DISPLAY_WORK_BOTTOM,
            0u, 1))
        return compose_failed(&builder, error, failure,
                              ASTRA_STATUS_LIMIT);
    for (uint32_t index = 0u; index < state->count; ++index) {
        DisplayWindow *window = &state->windows[index];

        if (window->state == ASTRA_WINDOW_STATE_MINIMIZED)
            continue;
        if (!astra_render_builder_window_scene_layer(
                &builder, window->cache_resource[window->cache_pending],
                window->request.x, window->request.y,
                window_radius(&theme, window), 1))
            return compose_failed(&builder, error, failure,
                                  ASTRA_STATUS_LIMIT);
    }
    if (include_cursor && !astra_render_builder_cursor(
            &builder, (uint32_t)state->pointer_x,
            (uint32_t)state->pointer_y,
            ASTRA_DISPLAY_CURSOR_VISIBLE |
                ASTRA_DISPLAY_CURSOR_SHAPE(
                    display_pointer_shape(state, &theme))))
        return compose_failed(&builder, error, failure,
                              ASTRA_STATUS_INVALID);
    status = astra_render_builder_finish(&builder);
    if (status == 0u)
        return compose_failed(&builder, error, failure,
                              ASTRA_STATUS_LIMIT);
    return status;
}

static void log_builder_failure(uint32_t failure)
{
    static const char *const messages[] = {
        "display render builder failed without a reason",
        "display render data arena is full",
        "display render descriptor table is full",
        "display render command has an invalid destination",
        "display render command ring is full",
        "display render surface geometry is invalid",
        "display render glyph table is full",
        "display render presentation state is invalid",
        "display window scene is invalid",
    };

    if (failure >= sizeof(messages) / sizeof(messages[0]))
        failure = ASTRA_RENDER_BUILDER_FAILURE_NONE;
    (void)astra_log(messages[failure]);
}

static uint32_t last_builder_failure;

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
                              int32_t x, int32_t y, uint32_t flags,
                              uint32_t *fence, uint32_t *armed)
{
    AstraDisplayFrameRequest request = {
        .size = ASTRA_DISPLAY_FRAME_REQUEST_SIZE,
        .operation = ASTRA_DISPLAY_CURSOR_UPDATE,
        .fence = *fence,
        .source = (uint32_t)x,
        .pitch = (uint32_t)y,
        .byte_size = flags,
    };
    uint32_t status = submit_request(device, irq, &request, armed);

    if (status == ASTRA_STATUS_OK && ++*fence == 0u)
        *fence = 1u;
    return status;
}

static uint32_t update_cursor_image(
    uint32_t device, uint32_t irq, AstraDmaBufferInfo *buffer,
    const AstraColorRGBA8 *pixels, uint16_t hot_x, uint16_t hot_y,
    uint32_t *fence, uint32_t *armed)
{
    AstraDisplayCursorImage *image;
    AstraDisplayFrameRequest request;
    uint32_t status;

    if (buffer == NULL || pixels == NULL ||
        buffer->byte_size < sizeof(*image) || hot_x >= 32u || hot_y >= 32u)
        return ASTRA_STATUS_INVALID;
    image = (AstraDisplayCursorImage *)(uintptr_t)buffer->virtual_base;
    image->magic = ASTRA_DISPLAY_CURSOR_IMAGE_MAGIC;
    image->version = ASTRA_DISPLAY_CURSOR_IMAGE_VERSION;
    image->hotspot = ((uint32_t)hot_y << 16) | hot_x;
    image->reserved = 0u;
    for (uint32_t at = 0u; at < ASTRA_DISPLAY_CURSOR_IMAGE_PIXELS; ++at) {
        AstraColorRGBA8 pixel = pixels[at];

        image->argb[at] = ((uint32_t)pixel.alpha << 24) |
                          ((uint32_t)pixel.red << 16) |
                          ((uint32_t)pixel.green << 8) | pixel.blue;
    }
    request = (AstraDisplayFrameRequest){
        .size = ASTRA_DISPLAY_FRAME_REQUEST_SIZE,
        .operation = ASTRA_DISPLAY_CURSOR_IMAGE_UPDATE,
        .fence = *fence,
        .source = buffer->handle,
        .pitch = 0u,
        .byte_size = sizeof(*image),
    };
    status = submit_request(device, irq, &request, armed);
    if (status == ASTRA_STATUS_OK && ++*fence == 0u)
        *fence = 1u;
    return status;
}

static void commit_render_state(DisplayState *state)
{
    state->damage[0] = (DamageRect){0};
    state->damage[1] = (DamageRect){0};
    state->scene_active = state->scene_pending;
    state->scene_valid = 1u;
    if (state->system_pending != 0u)
        state->system_initialized = 1u;
    for (uint32_t index = 0u; index < state->count; ++index) {
        DisplayWindow *window = &state->windows[index];

        if (window->content_pending != 0u) {
            window->content_dirty = 0u;
            window->content_initialized = 1u;
            window->content_damage = (DamageRect){0};
            window->content_pending = 0u;
        }
        if (window->cache_pending < 2u) {
            uint32_t bank = window->cache_pending;

            window->cache_active = (uint8_t)bank;
            window->cache_valid |= (uint8_t)(1u << bank);
            window->cache_dirty[bank] = 0u;
            window->cache_damage[bank] = (DamageRect){0};
            window->cache_pending = 2u;
        }
    }
}

static uint32_t render(uint32_t device, uint32_t irq,
                       AstraDmaBufferInfo *framebuffer, DisplayState *state,
                       uint32_t *next_fence, uint32_t *armed,
                       int include_cursor)
{
    uint32_t compose_status = ASTRA_STATUS_OK;

    last_builder_failure = ASTRA_RENDER_BUILDER_FAILURE_NONE;
    uint32_t bytes = compose((void *)(uintptr_t)framebuffer->virtual_base,
                             *next_fence, state, &compose_status,
                             &last_builder_failure, include_cursor);
    uint32_t status = bytes == 0u ? compose_status :
        present(device, irq, framebuffer, bytes, *next_fence, armed);

    if (status == ASTRA_STATUS_OK) {
        ++*next_fence;
        commit_render_state(state);
    }
    return status;
}

static void log_render_failure(const char *phase, uint32_t status)
{
    (void)astra_log(phase);
    if (last_builder_failure != ASTRA_RENDER_BUILDER_FAILURE_NONE)
        log_builder_failure(last_builder_failure);
    else if (status == ASTRA_STATUS_LIMIT)
        (void)astra_log("display render exhausted its command resources");
    else if (status == ASTRA_STATUS_PROTOCOL)
        (void)astra_log("display rejected a window draw list");
    else if (status == ASTRA_STATUS_INVALID)
        (void)astra_log("display render had no valid damage or batch");
    else
        (void)astra_log("display hardware submission or completion failed");
}

static void render_failure(const char *phase, uint32_t status)
{
    log_render_failure(phase, status);
    astra_process_exit(DISPLAY_FAIL_COMPLETION);
}

static int valid_open(const AstraGuiOpenWindow *request, uint32_t size,
                      uint32_t handles)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    DisplayWindow candidate = { .request = *request };
    uint16_t title;

    if (size != sizeof(*request) ||
        handles != (request->title_icon_length != 0u ? 4u : 3u))
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
           request->content_format == ASTRA_WINDOW_CONTENT_DRAW_LIST &&
           request->type >= ASTRA_WINDOW_STANDARD &&
           request->type <= ASTRA_WINDOW_DESKTOP &&
           (request->flags & ~(ASTRA_WINDOW_RESIZABLE | ASTRA_WINDOW_MODAL |
                               ASTRA_WINDOW_ACTIVE)) == 0u &&
           (request->gadgets & ~(ASTRA_WINDOW_GADGET_CLOSE |
                                 ASTRA_WINDOW_GADGET_MINIMIZE |
                                 ASTRA_WINDOW_GADGET_MAXIMIZE)) == 0u &&
           request->close_state <= ASTRA_GADGET_DISABLED &&
           request->minimize_state <= ASTRA_GADGET_DISABLED &&
           request->maximize_state <= ASTRA_GADGET_DISABLED &&
           request->title_length <= ASTRA_WINDOW_TITLE_MAX &&
           astra_utf8_validate(request->title, request->title_length, 0u) &&
           request->title_icon_length <=
               ASTRA_WINDOW_TITLE_ICON_BYTES_MAX &&
           (title == 0u || request->width >= 96u) &&
           request->pitch == 0u &&
           (request->type != ASTRA_WINDOW_DESKTOP ||
            (request->x == 0u && request->y == DISPLAY_WORK_TOP &&
             request->width == ASTRA_DISPLAY_WIDTH &&
             request->height == DISPLAY_WORK_BOTTOM - DISPLAY_WORK_TOP &&
             request->flags == 0u && request->gadgets == 0u &&
             request->title_length == 0u &&
             request->title_icon_length == 0u)) && frame_valid(
               &theme, &candidate, request->x, request->y,
               request->width, request->height);
}

static int valid_command(const AstraGuiWindowCommand *request, uint32_t size,
                         uint32_t handles, uint32_t id,
                         uint16_t content_width, uint16_t content_height)
{
    int frame_zero = request->x == 0u && request->y == 0u &&
                     request->width == 0u && request->height == 0u;

    if (size != sizeof(*request) ||
        handles != (request->action == ASTRA_GUI_WINDOW_CLOSE ? 0u :
                   request->action == ASTRA_GUI_WINDOW_SET_POINTER_IMAGE ?
                       2u : 1u) ||
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
        request->action > ASTRA_GUI_WINDOW_SET_POINTER_IMAGE ||
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
        return frame_zero && request->flags == 0u &&
               astra_utf8_validate(request->title, request->title_length, 0u);
    if (request->action == ASTRA_GUI_WINDOW_SET_EVENT_MASK)
        return frame_zero && request->title_length == 0u &&
               (request->flags & ~ASTRA_WINDOW_SUBSCRIBE_ALL) == 0u;
    if (request->action == ASTRA_GUI_WINDOW_PRESENT)
        return request->title_length == 0u && request->flags == 0u &&
               (frame_zero ||
                (request->width != 0u && request->height != 0u &&
                 (uint32_t)request->x + request->width <= content_width &&
                 (uint32_t)request->y + request->height <= content_height));
    if (request->action == ASTRA_GUI_WINDOW_SET_POINTER_SHAPE)
        return frame_zero && request->title_length == 0u &&
               request->flags < ASTRA_POINTER_SHAPE_COUNT;
    if (request->action == ASTRA_GUI_WINDOW_SET_POINTER_IMAGE)
        return request->title_length == 0u && request->width != 0u &&
               request->width <= ASTRA_DISPLAY_CURSOR_IMAGE_WIDTH &&
               request->height != 0u &&
               request->height <= ASTRA_DISPLAY_CURSOR_IMAGE_HEIGHT &&
               request->x < request->width && request->y < request->height &&
               request->flags ==
                   ASTRA_DISPLAY_CURSOR_IMAGE_WIDTH *
                       sizeof(AstraColorRGBA8);
    return frame_zero && request->title_length == 0u &&
           request->flags == 0u;
}

static void state_reply(AstraGuiWindowState *reply, uint32_t transaction,
                        uint32_t status, const DisplayWindow *window,
                        uint32_t z_order)
{
    *reply = (AstraGuiWindowState){0};
    astra_message_header_set(&reply->header, sizeof(*reply),
                             ASTRA_GUI_PROTOCOL, ASTRA_GUI_VERSION,
                             ASTRA_GUI_WINDOW_STATE, transaction);
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
    DisplayWindow prepared;
    uint32_t status;

    *changed = 0;
    *closed = (DisplayWindow){0};
    if (index == state->count)
        return ASTRA_STATUS_NOT_FOUND;
    window = &state->windows[index];
    if (window->request.type == ASTRA_WINDOW_DESKTOP &&
        command->action != ASTRA_GUI_WINDOW_QUERY &&
        command->action != ASTRA_GUI_WINDOW_PRESENT &&
        command->action != ASTRA_GUI_WINDOW_SET_EVENT_MASK &&
        command->action != ASTRA_GUI_WINDOW_SET_POINTER_SHAPE &&
        command->action != ASTRA_GUI_WINDOW_SET_POINTER_IMAGE &&
        command->action != ASTRA_GUI_WINDOW_CLOSE)
        return ASTRA_STATUS_UNSUPPORTED;
    switch (command->action) {
    case ASTRA_GUI_WINDOW_QUERY:
        return ASTRA_STATUS_OK;
    case ASTRA_GUI_WINDOW_SET_FRAME:
        if (!frame_valid(theme, window, command->x, command->y,
                         command->width, command->height))
            return ASTRA_STATUS_INVALID;
        status = prepare_geometry(
            state, window, command->x, command->y,
            command->width, command->height, ASTRA_WINDOW_STATE_NORMAL,
            &prepared);
        if (status != ASTRA_STATUS_OK)
            return status;
        damage_window(state, theme, window);
        *window = prepared;
        dirty_cache(window);
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
        status = prepare_geometry(
            state, window, window->request.x, window->request.y,
            command->width, command->height, ASTRA_WINDOW_STATE_NORMAL,
            &prepared);
        if (status != ASTRA_STATUS_OK)
            return status;
        damage_window(state, theme, window);
        *window = prepared;
        dirty_cache(window);
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
            dirty_cache(window);
            next_generation(window);
            damage_window(state, theme, window);
            *changed = 1;
        }
        *changed |= activate(state, theme, window->id, 1, 0u);
        return ASTRA_STATUS_OK;
    case ASTRA_GUI_WINDOW_DEACTIVATE:
        if ((window->request.flags & ASTRA_WINDOW_ACTIVE) != 0u)
            *changed = activate(state, theme, 0u, 0, 0u);
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
        {
            uint8_t restored_state =
                window->state == ASTRA_WINDOW_STATE_MINIMIZED ?
                    window->restore_state : window->state;

            if (restored_state == ASTRA_WINDOW_STATE_MAXIMIZED)
                return ASTRA_STATUS_OK;
            status = prepare_geometry(
                state, window, 0u, DISPLAY_WORK_TOP,
                ASTRA_DISPLAY_WIDTH -
                    frame_width(theme, window->request.type) * 2u,
                DISPLAY_WORK_BOTTOM - DISPLAY_WORK_TOP -
                    title_height(theme, window->request.type) -
                    (title_height(theme, window->request.type) == 0u ?
                     0u : theme->signal_height) -
                    frame_width(theme, window->request.type) * 2u,
                ASTRA_WINDOW_STATE_MAXIMIZED, &prepared);
            if (status != ASTRA_STATUS_OK)
                return status;
            if (restored_state == ASTRA_WINDOW_STATE_NORMAL) {
                prepared.restore_x = window->request.x;
                prepared.restore_y = window->request.y;
                prepared.restore_width = window->request.width;
                prepared.restore_height = window->request.height;
            }
            damage_window(state, theme, window);
            *window = prepared;
            dirty_cache(window);
            reset_content(window);
            next_generation(window);
            damage_window(state, theme, window);
            *changed = 1;
        }
        return ASTRA_STATUS_OK;
    case ASTRA_GUI_WINDOW_RESTORE:
        if (window->state == ASTRA_WINDOW_STATE_MINIMIZED) {
            window->state = window->restore_state;
            dirty_cache(window);
            next_generation(window);
            damage_window(state, theme, window);
            *changed = 1;
        } else if (window->state == ASTRA_WINDOW_STATE_MAXIMIZED) {
            status = prepare_geometry(
                state, window, window->restore_x, window->restore_y,
                window->restore_width, window->restore_height,
                ASTRA_WINDOW_STATE_NORMAL, &prepared);
            if (status != ASTRA_STATUS_OK)
                return status;
            damage_window(state, theme, window);
            *window = prepared;
            dirty_cache(window);
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
            dirty_cache(window);
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
    case ASTRA_GUI_WINDOW_SET_POINTER_SHAPE:
        if (command->flags == ASTRA_POINTER_SHAPE_CUSTOM &&
            window->pointer_image == NULL)
            return ASTRA_STATUS_NOT_FOUND;
        window->pointer_shape = command->flags;
        return ASTRA_STATUS_OK;
    case ASTRA_GUI_WINDOW_SET_POINTER_IMAGE:
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

static uint32_t display_pointer_shape(DisplayState *state,
                                      const AstraTheme *theme)
{
    uint32_t region = HIT_NONE;
    uint32_t index = pointer_target(state, theme, &region);

    if (index == state->count)
        return ASTRA_POINTER_SHAPE_DEFAULT;
    if (region == HIT_CONTENT)
        return state->windows[index].pointer_shape;
    if (region == HIT_RESIZE_E || region == HIT_RESIZE_W)
        return ASTRA_POINTER_SHAPE_RESIZE_HORIZONTAL;
    if (region == HIT_RESIZE_N || region == HIT_RESIZE_S)
        return ASTRA_POINTER_SHAPE_RESIZE_VERTICAL;
    return ASTRA_POINTER_SHAPE_DEFAULT;
}

static uint32_t prepare_pointer_image(
    uint32_t device, uint32_t irq, AstraDmaBufferInfo *buffer,
    DisplayState *state, const AstraTheme *theme, uint32_t *fence,
    uint32_t *armed)
{
    uint32_t region = HIT_NONE;
    uint32_t index = pointer_target(state, theme, &region);
    DisplayWindow *window;

    if (index == state->count || region != HIT_CONTENT ||
        state->windows[index].pointer_shape != ASTRA_POINTER_SHAPE_CUSTOM)
        return ASTRA_STATUS_OK;
    window = &state->windows[index];
    if (window->pointer_image == NULL)
        return ASTRA_STATUS_INVALID;
    if (state->loaded_pointer_shape == ASTRA_POINTER_SHAPE_CUSTOM &&
        state->loaded_pointer_window == window->id &&
        state->loaded_pointer_generation == window->pointer_image_generation)
        return ASTRA_STATUS_OK;
    {
        uint32_t status = update_cursor_image(
            device, irq, buffer, window->pointer_image,
            window->pointer_hot_x, window->pointer_hot_y, fence, armed);

        if (status != ASTRA_STATUS_OK)
            return status;
    }
    state->loaded_pointer_shape = ASTRA_POINTER_SHAPE_CUSTOM;
    state->loaded_pointer_window = window->id;
    state->loaded_pointer_generation = window->pointer_image_generation;
    return ASTRA_STATUS_OK;
}

static void pointer_shape_presented(DisplayState *state,
                                    const AstraTheme *theme)
{
    uint32_t shape = display_pointer_shape(state, theme);

    state->loaded_pointer_shape = shape;
    if (shape != ASTRA_POINTER_SHAPE_CUSTOM) {
        state->loaded_pointer_window = 0u;
        state->loaded_pointer_generation = 0u;
    }
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
        if (input->type == ASTRA_INPUT_EVENT_TEXT &&
            !astra_unicode_scalar_valid(input->code))
            return ASTRA_STATUS_INVALID;
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
                if (changed)
                    *effects |= DISPLAY_POINTER_RESIZE;
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
                    state->pointer_y, 0u, 0u, input->modifiers);
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
                        state->pointer_x, state->pointer_y, dx, dy,
                        input->modifiers);
        }
        return ASTRA_STATUS_OK;
    }
    if ((input->flags & ASTRA_INPUT_LOGICAL_DOWN) != 0u) {
        uint32_t id;
        uint32_t click_count;

        if (index == state->count)
            return ASTRA_STATUS_OK;
        id = state->windows[index].id;
        click_count = register_click(state, id, input->code,
                                     input->timestamp_ms);
        if (input->code == ASTRA_INPUT_BUTTON_LEFT) {
            changed |= activate(state, &theme, id, 1,
                                input->timestamp_ms);
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
                    state->pointer_y, input->code, click_count,
                    input->modifiers);
        } else {
            pointer_event(&state->windows[index], &theme,
                          ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN, input->timestamp_ms,
                          state->pointer_x, state->pointer_y, input->code,
                          click_count, input->modifiers);
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
                state->pointer_y, input->code, state->click_count,
                input->modifiers);
        }
    } else if (index != state->count) {
        pointer_event(&state->windows[index], &theme,
                      ASTRA_WINDOW_EVENT_POINTER_BUTTON, 0u,
                      input->timestamp_ms, state->pointer_x,
                      state->pointer_y, input->code, state->click_count,
                      input->modifiers);
    }
    if (changed)
        *effects |= DISPLAY_POINTER_RENDER;
    return ASTRA_STATUS_OK;
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
                           uint32_t control_send, uint32_t vblank_wait)
{
    AstraGuiWindowOpened message = {0};
    uint32_t handles[2] = {control_send, vblank_wait};

    astra_message_header_set(&message.header, sizeof(message),
                             ASTRA_GUI_PROTOCOL, ASTRA_GUI_VERSION,
                             ASTRA_GUI_WINDOW_OPENED, transaction);
    message.status = status;
    if (status == ASTRA_STATUS_OK) {
        message.window = window->id;
        message.generation = window->generation;
    }
    return astra_port_send(handle, &message, sizeof(message),
                           status == ASTRA_STATUS_OK ? handles : NULL,
                           status == ASTRA_STATUS_OK ? 2u : 0u);
}

static void close_window(DisplayWindow *window)
{
    if (window->control_receive != 0u)
        (void)astra_close(window->control_receive);
    if (window->event_send != 0u)
        (void)astra_close(window->event_send);
    if (window->vblank_signal != 0u)
        (void)astra_close(window->vblank_signal);
    if (window->surface.area != 0u)
        (void)astra_shared_surface_close(&window->surface);
    if (window->title_icon_bytes != NULL)
        (void)astra_rt_area_unmap(window->title_icon_bytes);
    if (window->title_icon_area != 0u)
        (void)astra_close(window->title_icon_area);
    if (window->pointer_image != NULL)
        (void)astra_rt_area_unmap((void *)(uintptr_t)window->pointer_image);
    if (window->pointer_image_area != 0u)
        (void)astra_close(window->pointer_image_area);
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
    uint32_t vblank_wait = 0u;
    uint32_t status = astra_port_receive(
        gui_receive, &request, sizeof(request), handles,
        ASTRA_MESSAGE_HANDLES_MAX, &size, &handle_count);
    int added = 0;

    if (status != ASTRA_SYSCALL_OK)
        return;
    status = valid_open(&request, size, handle_count) &&
             (request.type != ASTRA_WINDOW_DESKTOP || state->count == 0u) ?
        ASTRA_STATUS_OK : DISPLAY_FAIL_PROTOCOL;
    if (status == ASTRA_STATUS_OK &&
        state->count >= ASTRA_WAIT_MULTIPLE_MAX - 3u)
        status = ASTRA_STATUS_LIMIT;
    if (status == ASTRA_STATUS_OK)
        status = display_windows_reserve(state, state->count + 1u);
    if (status == ASTRA_STATUS_OK) {
        candidate.request = request;
        status = display_window_media_prepare(state, &candidate);
    }
    if (status == ASTRA_STATUS_OK)
        status = service_status(astra_shared_draw_list_adopt(
            &candidate.surface, handles[0], request.width, request.height,
            ASTRA_AREA_MAP_READ));
    if (status == ASTRA_STATUS_OK) {
        handles[0] = 0u;
        if (request.title_icon_length != 0u) {
            uint32_t mapped_size = 0u;

            status = service_status(astra_rt_area_map(
                handles[3], ASTRA_AREA_MAP_READ,
                &candidate.title_icon_bytes, &mapped_size));
            if (status == ASTRA_STATUS_OK) {
                candidate.title_icon_area = handles[3];
                handles[3] = 0u;
                if (mapped_size < request.title_icon_length ||
                    astra_aicon_open(candidate.title_icon_bytes,
                                     request.title_icon_length,
                                     &candidate.title_icon) !=
                        ASTRA_BUNDLE_OK ||
                    astra_aicon_strike(&candidate.title_icon, 16u,
                                       &candidate.title_icon_strike) !=
                        ASTRA_BUNDLE_OK)
                    status = DISPLAY_FAIL_PROTOCOL;
            }
        }
    }
    if (status == ASTRA_STATUS_OK) {
        uint32_t port_status = astra_rt_port_create(
            1u, ASTRA_GUI_WINDOW_COMMAND_SIZE, &candidate.control_receive,
            &control_send);

        status = service_status(port_status);
    }
    if (status == ASTRA_STATUS_OK) {
        uint32_t event_status = astra_rt_event_create(
            0u, ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT |
                    ASTRA_RIGHT_TRANSFER,
            &candidate.vblank_signal);

        status = service_status(event_status);
    }
    if (status == ASTRA_STATUS_OK)
        status = service_status(astra_rt_handle_duplicate(
            candidate.vblank_signal,
            ASTRA_RIGHT_WAIT | ASTRA_RIGHT_TRANSFER, &vblank_wait));
    if (status == ASTRA_STATUS_OK) {
        candidate.request.flags &= ~ASTRA_WINDOW_ACTIVE;
        candidate.id = allocate_id(state);
        candidate.generation = 1u;
        candidate.event_send = handles[1];
        handles[1] = 0u;
        dirty_cache(&candidate);
        reset_content(&candidate);
        state->windows[state->count++] = candidate;
        added = 1;
        candidate = (DisplayWindow){0};
        damage_window(state, &theme, &state->windows[state->count - 1u]);
        if ((request.flags & ASTRA_WINDOW_ACTIVE) != 0u)
            activate(state, &theme, state->windows[state->count - 1u].id,
                     1, 0u);
        status = render(device, irq, framebuffer, state,
                        next_fence, armed, 0);
        if (status != ASTRA_STATUS_OK)
            log_render_failure("display window-open render failed", status);
    }
    if (handle_count == 3u || handle_count == 4u) {
        const DisplayWindow *window = status == ASTRA_STATUS_OK ?
            &state->windows[state->count - 1u] : &candidate;
        uint32_t sent = reply_open(
            handles[2], request.header.transaction_id, status,
            window, control_send, vblank_wait);

        if (sent == ASTRA_SYSCALL_OK && status == ASTRA_STATUS_OK) {
            control_send = 0u;
            vblank_wait = 0u;
        }
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
        if (changed) {
            status = render(device, irq, framebuffer, state,
                            next_fence, armed, 0);
            if (status != ASTRA_STATUS_OK)
                render_failure("display close render failed", status);
        }
        close_window(&failed);
    }
    if (control_send != 0u)
        (void)astra_close(control_send);
    if (vblank_wait != 0u)
        (void)astra_close(vblank_wait);
    close_window(&candidate);
    for (uint32_t index = 0u; index < handle_count; ++index)
        if (handles[index] != 0u)
            (void)astra_close(handles[index]);
}

static void receive_command(uint32_t device, uint32_t irq,
                            AstraDmaBufferInfo *framebuffer,
                            AstraDmaBufferInfo *pointer_buffer,
                            DisplayState *state, uint32_t window_index,
                            uint32_t *next_fence, uint32_t *cursor_fence,
                            uint32_t *armed)
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
    const AstraColorRGBA8 *pointer_image = NULL;
    uint32_t pointer_image_bytes = 0u;
    const AstraColorRGBA8 *old_pointer_image = NULL;
    uint32_t old_pointer_image_area = 0u;
    uint32_t old_pointer_image_bytes = 0u;
    uint16_t old_pointer_hot_x = 0u;
    uint16_t old_pointer_hot_y = 0u;
    uint32_t old_pointer_generation = 0u;
    uint32_t old_pointer_shape = ASTRA_POINTER_SHAPE_DEFAULT;
    uint32_t old_loaded_shape = state->loaded_pointer_shape;
    uint32_t old_loaded_window = state->loaded_pointer_window;
    uint32_t old_loaded_generation = state->loaded_pointer_generation;
    uint16_t old_width = state->windows[window_index].request.width;
    uint16_t old_height = state->windows[window_index].request.height;
    int pointer_image_pending = 0;
    uint32_t status = astra_port_receive(
        receive, &command, sizeof(command), handles,
        ASTRA_MESSAGE_HANDLES_MAX, &size, &handle_count);
    int changed = 0;

    if (status == ASTRA_SYSCALL_PEER_DEAD) {
        AstraGuiWindowCommand close = { .window = id,
                                       .action = ASTRA_GUI_WINDOW_CLOSE };

        (void)apply_command(state, &theme, &close, &closed, &changed);
        if (changed && render(device, irq, framebuffer, state,
                              next_fence, armed, 0) != ASTRA_STATUS_OK)
            astra_process_exit(DISPLAY_FAIL_COMPLETION);
        close_window(&closed);
        return;
    }
    if (status != ASTRA_SYSCALL_OK)
        return;
    status = valid_command(&command, size, handle_count, id,
                           state->windows[window_index].request.width,
                           state->windows[window_index].request.height) ?
             ASTRA_STATUS_OK : DISPLAY_FAIL_PROTOCOL;
    if (status == ASTRA_STATUS_OK &&
        command.action == ASTRA_GUI_WINDOW_SET_POINTER_IMAGE) {
        void *mapping = NULL;

        status = service_status(astra_rt_area_map(
            handles[1], ASTRA_AREA_MAP_READ, &mapping,
            &pointer_image_bytes));
        if (status == ASTRA_STATUS_OK &&
            pointer_image_bytes < ASTRA_DISPLAY_CURSOR_IMAGE_PIXELS *
                                      sizeof(AstraColorRGBA8)) {
            (void)astra_rt_area_unmap(mapping);
            mapping = NULL;
            status = DISPLAY_FAIL_PROTOCOL;
        }
        pointer_image = mapping;
    }
    if (status == ASTRA_STATUS_OK)
        status = apply_command(state, &theme, &command, &closed, &changed);
    if (status == ASTRA_STATUS_OK &&
        command.action == ASTRA_GUI_WINDOW_SET_POINTER_IMAGE) {
        DisplayWindow *window = &state->windows[window_index];

        old_pointer_image = window->pointer_image;
        old_pointer_image_area = window->pointer_image_area;
        old_pointer_image_bytes = window->pointer_image_bytes;
        old_pointer_hot_x = window->pointer_hot_x;
        old_pointer_hot_y = window->pointer_hot_y;
        old_pointer_generation = window->pointer_image_generation;
        old_pointer_shape = window->pointer_shape;
        window->pointer_image = pointer_image;
        window->pointer_image_bytes = pointer_image_bytes;
        window->pointer_image_area = handles[1];
        window->pointer_hot_x = command.x;
        window->pointer_hot_y = command.y;
        if (++window->pointer_image_generation == 0u)
            ++window->pointer_image_generation;
        window->pointer_shape = ASTRA_POINTER_SHAPE_CUSTOM;
        pointer_image_pending = 1;
    }
    if (status == ASTRA_STATUS_OK &&
        (command.action == ASTRA_GUI_WINDOW_SET_POINTER_SHAPE ||
         command.action == ASTRA_GUI_WINDOW_SET_POINTER_IMAGE))
        status = prepare_pointer_image(device, irq, pointer_buffer, state,
                                       &theme, cursor_fence, armed);
    if (status == ASTRA_STATUS_OK &&
        command.action == ASTRA_GUI_WINDOW_SET_POINTER_SHAPE &&
        update_cursor(device, irq, state->pointer_x, state->pointer_y,
                      ASTRA_DISPLAY_CURSOR_VISIBLE |
                          ASTRA_DISPLAY_CURSOR_SHAPE(
                              display_pointer_shape(state, &theme)),
                      cursor_fence, armed) != ASTRA_STATUS_OK)
        status = DISPLAY_FAIL_COMPLETION;
    if (pointer_image_pending && status != ASTRA_STATUS_OK) {
        DisplayWindow *window = &state->windows[window_index];

        window->pointer_image = old_pointer_image;
        window->pointer_image_area = old_pointer_image_area;
        window->pointer_image_bytes = old_pointer_image_bytes;
        window->pointer_hot_x = old_pointer_hot_x;
        window->pointer_hot_y = old_pointer_hot_y;
        window->pointer_image_generation = old_pointer_generation;
        window->pointer_shape = old_pointer_shape;
        state->loaded_pointer_shape = old_loaded_shape;
        state->loaded_pointer_window = old_loaded_window;
        state->loaded_pointer_generation = old_loaded_generation;
        pointer_image_pending = 0;
    }
    if (pointer_image_pending) {
        handles[1] = 0u;
        pointer_image = NULL;
        if (old_pointer_image != NULL)
            (void)astra_rt_area_unmap(
                (void *)(uintptr_t)old_pointer_image);
        if (old_pointer_image_area != 0u)
            (void)astra_close(old_pointer_image_area);
    }
    if (status == ASTRA_STATUS_OK &&
        (command.action == ASTRA_GUI_WINDOW_SET_POINTER_SHAPE ||
         command.action == ASTRA_GUI_WINDOW_SET_POINTER_IMAGE))
        pointer_shape_presented(state, &theme);
    if (status == ASTRA_STATUS_OK && changed) {
        status = render(device, irq, framebuffer, state,
                        next_fence, armed, 0);
        if (status != ASTRA_STATUS_OK)
            render_failure("display window-command render failed", status);
    }
    if (status == ASTRA_STATUS_OK && changed && closed.id == 0u) {
        uint32_t current = find_id(state, id);

        if (current != state->count)
            state_event(&state->windows[current], 0u, current);
        if (current != state->count &&
            (state->windows[current].request.width != old_width ||
             state->windows[current].request.height != old_height))
            resize_event(&state->windows[current], 0u);
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
    if (handle_count != 0u && handles[0] != 0u)
        (void)astra_port_send(handles[0], &reply, sizeof(reply), NULL, 0u);
    close_window(&closed);
    if (pointer_image != NULL)
        (void)astra_rt_area_unmap((void *)(uintptr_t)pointer_image);
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
    astra_message_header_set(&request.header, sizeof(request),
                             ASTRA_INPUT_SERVICE_PROTOCOL,
                             ASTRA_INPUT_SERVICE_VERSION,
                             ASTRA_INPUT_OPERATION_CONNECT, 1u);
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
        message.event.type > ASTRA_INPUT_EVENT_STATE_RESET ||
        (message.event.modifiers & ~ASTRA_INPUT_MOD_ALL) != 0u)
        return DISPLAY_FAIL_PROTOCOL;
    *event = message.event;
    return ASTRA_STATUS_OK;
}

/* Apply every input event already queued, then present the resulting state
 * once.  Pointer motion is absolute, so replaying intermediate frames only
 * adds latency; button, key, and text events still retain queue order. */
static uint32_t drain_input(uint32_t receive, DisplayState *state,
                            uint32_t *effects, uint32_t *frame_window,
                            uint32_t *frame_timestamp)
{
    for (;;) {
        AstraLogicalInputEvent event;
        uint32_t status = receive_input(receive, &event);

        if (status == ASTRA_SYSCALL_WOULD_BLOCK)
            return ASTRA_STATUS_OK;
        if (status != ASTRA_STATUS_OK)
            return status;
        status = handle_pointer(state, &event, effects, frame_window,
                                frame_timestamp);
        if (status != ASTRA_STATUS_OK)
            return status;
    }
}

static uint32_t display_wait_handles(const DisplayState *state,
                                     uint32_t gui_receive,
                                     uint32_t input_receive,
                                     uint32_t vblank_irq,
                                     uint32_t first,
                                     uint32_t *waits,
                                     uint32_t *sources)
{
    uint32_t count;

    if (state == NULL || waits == NULL || sources == NULL ||
        state->count > ASTRA_WAIT_MULTIPLE_MAX - 3u)
        return 0u;
    count = state->count + 3u;

    first %= count;
    for (uint32_t slot = 0u; slot < count; ++slot) {
        uint32_t source = first + slot;

        if (source >= count)
            source -= count;
        sources[slot] = source;
        waits[slot] = source == 0u ? gui_receive :
                      source == 1u ? input_receive :
                      source == 2u ? vblank_irq :
                      state->windows[source - 3u].control_receive;
    }
    return count;
}

static uint32_t display_window_wait_index(uint32_t source,
                                          uint32_t window_count)
{
    if (source < 3u || source - 3u >= window_count)
        return window_count;
    return source - 3u;
}

static uint32_t signal_vblank(DisplayState *state)
{
    for (uint32_t index = 0u; index < state->count; ++index) {
        if (state->windows[index].pending_close != 0u) {
            AstraWindowEvent event = {
                .type = ASTRA_WINDOW_EVENT_CLOSE_REQUEST,
                .timestamp_ms =
                    state->windows[index].pending_close_timestamp_ms,
            };

            (void)send_event(&state->windows[index], &event);
        }
        if ((state->windows[index].request.event_mask &
             ASTRA_WINDOW_SUBSCRIBE_VBLANK) != 0u &&
            astra_rt_signal(state->windows[index].vblank_signal, 1u,
                            NULL) != ASTRA_SYSCALL_OK)
            return DISPLAY_FAIL_WAIT;
    }
    return ASTRA_STATUS_OK;
}

static uint32_t dispatch_vblank(uint32_t irq, DisplayState *state)
{
    AstraIrqRecord record;
    uint32_t status = astra_irq_read(irq, &record, NULL);

    if (status != ASTRA_SYSCALL_OK ||
        astra_irq_ack(irq, record.sequence) != ASTRA_SYSCALL_OK)
        return DISPLAY_FAIL_IRQ;
    return signal_vblank(state);
}

static void serve_windows(uint32_t device, uint32_t irq,
                          uint32_t vblank_irq,
                          AstraDmaBufferInfo *framebuffer,
                          AstraDmaBufferInfo *pointer_buffer,
                          uint32_t gui_receive, uint32_t input_receive)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    DisplayState state = {
        .damage = {
            { 0, 0, ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT, 1u },
            { 0, 0, ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT, 1u }
        },
        .loaded_pointer_shape = ASTRA_POINTER_SHAPE_DEFAULT,
    };
    uint32_t next_fence = 1u;
    uint32_t cursor_fence = UINT32_C(0x80000001);
    uint32_t armed = 0u;
    uint32_t first_wait = 0u;

    if (astra_irq_arm(vblank_irq) != ASTRA_SYSCALL_OK)
        astra_process_exit(DISPLAY_FAIL_ARM);
    for (;;) {
        uint32_t waits[ASTRA_WAIT_MULTIPLE_MAX];
        uint32_t sources[ASTRA_WAIT_MULTIPLE_MAX];
        uint32_t wait_count = display_wait_handles(
            &state, gui_receive, input_receive, vblank_irq, first_wait,
            waits, sources);
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
            status = drain_input(input_receive, &state, &effects,
                                 &frame_window, &frame_timestamp);
            if (status != ASTRA_STATUS_OK)
                astra_process_exit(DISPLAY_FAIL_PROTOCOL);
            if ((effects & DISPLAY_POINTER_CURSOR) != 0u &&
                prepare_pointer_image(device, irq, pointer_buffer, &state,
                                      &theme, &cursor_fence, &armed) !=
                    ASTRA_STATUS_OK)
                astra_process_exit(DISPLAY_FAIL_COMPLETION);
            if ((effects & DISPLAY_POINTER_CURSOR) != 0u &&
                (effects & DISPLAY_POINTER_RENDER) == 0u &&
                update_cursor(device, irq, state.pointer_x, state.pointer_y,
                              ASTRA_DISPLAY_CURSOR_VISIBLE |
                                  ASTRA_DISPLAY_CURSOR_SHAPE(
                                      display_pointer_shape(&state, &theme)),
                              &cursor_fence, &armed) != ASTRA_STATUS_OK)
                astra_process_exit(DISPLAY_FAIL_COMPLETION);
            if ((effects & DISPLAY_POINTER_CURSOR) != 0u &&
                (effects & DISPLAY_POINTER_RENDER) == 0u)
                pointer_shape_presented(&state, &theme);
            if ((effects & DISPLAY_POINTER_RENDER) != 0u) {
                status = render(device, irq, framebuffer, &state,
                                &next_fence, &armed,
                                (effects & DISPLAY_POINTER_CURSOR) != 0u);
                if (status != ASTRA_STATUS_OK)
                    render_failure("display pointer render failed", status);
                if ((effects & DISPLAY_POINTER_CURSOR) != 0u)
                    pointer_shape_presented(&state, &theme);
            }
            if ((effects & DISPLAY_POINTER_FRAME) != 0u) {
                uint32_t index = find_id(&state, frame_window);

                if (index != state.count)
                    state_event(&state.windows[index], frame_timestamp,
                                index);
                if (index != state.count &&
                    (effects & DISPLAY_POINTER_RESIZE) != 0u)
                    resize_event(&state.windows[index], frame_timestamp);
            }
        } else if (selected == 2u) {
            if (dispatch_vblank(vblank_irq, &state) != ASTRA_STATUS_OK)
                astra_process_exit(DISPLAY_FAIL_IRQ);
        } else {
            uint32_t window_index = display_window_wait_index(
                selected, state.count);

            if (window_index != state.count)
                receive_command(device, irq, framebuffer, pointer_buffer,
                                &state, window_index, &next_fence,
                                &cursor_fence,
                                &armed);
        }
    }
}

int astra_main(const AstraStartupInfo *startup)
{
    AstraDmaBufferInfo framebuffer;
    AstraDmaBufferInfo pointer_buffer;
    const AstraStartupCapability *bootstrap;
    const AstraStartupCapability *device;
    const AstraStartupCapability *irq;
    const AstraStartupCapability *vblank_irq;
    const AstraStartupCapability *input_service;
    uint32_t gui_receive = 0u;
    uint32_t gui_send = 0u;
    uint32_t input_receive = 0u;
    uint32_t status;

    if (!astra_startup_validate(startup) ||
        startup->capabilities_address == 0u)
        return ASTRA_STATUS_INVALID;
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    device = astra_startup_capability(startup,
                                      ASTRA_CAPABILITY_DISPLAY_DEVICE);
    irq = astra_startup_capability(startup, ASTRA_CAPABILITY_DISPLAY_IRQ);
    vblank_irq = astra_startup_capability(
        startup, ASTRA_CAPABILITY_DISPLAY_VBLANK_IRQ);
    input_service = astra_startup_capability(
        startup, ASTRA_CAPABILITY_INPUT_SERVICE);
    if (bootstrap == NULL || device == NULL || irq == NULL ||
        vblank_irq == NULL ||
        input_service == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    status = astra_dma_create(ASTRA_RENDER_BUILDER_BYTES, &framebuffer);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_dma_create(ASTRA_DISPLAY_CURSOR_IMAGE_BYTES,
                                  &pointer_buffer);
    if (status == ASTRA_SYSCALL_OK)
        status = connect_input(input_service->handle, &input_receive);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_rt_port_create(4u, 4u * ASTRA_GUI_OPEN_WINDOW_SIZE,
                                   &gui_receive, &gui_send);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_service_ready(bootstrap->handle, ASTRA_STATUS_OK,
                                     &gui_send, 1u);
    else {
        status = service_status(status);
        (void)astra_service_ready(bootstrap->handle, status, NULL, 0u);
    }
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
    serve_windows(device->handle, irq->handle, vblank_irq->handle,
                  &framebuffer, &pointer_buffer, gui_receive, input_receive);
    return ASTRA_STATUS_OK;
}
