#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <astra/gui.h>
#include <astra/render_batch.h>
#include <astra/runtime.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <astra_render_protocol.h>
#pragma GCC diagnostic pop

static AstraGuiWindowEvent delivered;
static uint32_t delivered_count;

uint32_t astra_port_send(uint32_t handle, const void *message, uint32_t size,
                         const uint32_t *handles, uint32_t handle_count)
{
    (void)handles;
    assert(handle == 0x500u && size == sizeof(delivered) &&
           handle_count == 0u);
    memcpy(&delivered, message, sizeof(delivered));
    ++delivered_count;
    return ASTRA_SYSCALL_OK;
}

#define astra_main astra_display_service_main
#include "../main.c"
#undef astra_main

static uint8_t lists[DISPLAY_WINDOW_MAX][ASTRA_DRAW_LIST_AREA_BYTES];
static uint8_t batch[ASTRA_RENDER_BUILDER_BYTES];

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static int batch_has_surface_fill(uint16_t width, uint16_t height,
                                  uint16_t value)
{
    uint32_t count = read_be32(batch + 12u);
    uint32_t commands = ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                        ASTRA_RENDER_BATCH_ARENA_OFFSET;

    for (uint32_t index = 0u; index < count; ++index) {
        const uint8_t *command = batch + commands +
                                 index * ASTRA_RENDER_COMMAND_BYTES;
        uint32_t descriptor = read_be32(command + 32u);
        uint32_t descriptor_offset;

        if ((read_be32(command + 4u) >> 16) != ASTRA_RENDER_OP_FILL ||
            descriptor < ASTRA_RENDER_BATCH_ARENA_OFFSET)
            continue;
        descriptor_offset = descriptor - ASTRA_RENDER_BATCH_ARENA_OFFSET;
        if (descriptor_offset + ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES >
            sizeof(batch))
            continue;
        if (read_be32(batch + descriptor_offset + 20u) ==
                ((uint32_t)width << 16 | height) &&
            read_be32(command + 48u) == 0u &&
            read_be32(command + 56u) ==
                ((uint32_t)width << 16 | height) &&
            (uint16_t)read_be32(command + 60u) == value)
            return 1;
    }
    return 0;
}

static void label(AstraSurfaceView *surface, int32_t x, int32_t y,
                  const char *text, uint32_t length, uint16_t color)
{
    astra_surface_ui_text(surface, x, y, text, length,
                          ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT, color);
}

static void paint_gallery(AstraSurfaceView *surface, uint8_t type,
                          const AstraTheme *theme)
{
    uint16_t client = color(theme->client);
    uint16_t primary = color(theme->title_active);
    uint16_t muted = color(theme->text_muted);
    uint16_t accent = color(theme->accent);

    astra_surface_clear(surface, client);
    label(surface, 18, 18, "GALLERY STATE", 13u, primary);
    astra_surface_fill(surface, 18, 36, surface->width - 36u, 2u, accent);
    if (type == ASTRA_WINDOW_STANDARD) {
        astra_surface_fill_round(surface, 18, 58, 238u, 92u,
                                 theme->card_radius, 0xffffu);
        astra_surface_fill_round(surface, 276, 58, 256u, 92u,
                                 theme->card_radius, 0xffffu);
        label(surface, 34, 76, "NORMAL", 6u, muted);
        label(surface, 292, 76, "HOVER / PRESSED", 15u, muted);
        astra_surface_fill_round(surface, 18, 174, 118u, 34u,
                                 theme->control_radius, accent);
        astra_surface_fill_round(surface, 148, 174, 118u, 34u,
                                 theme->control_radius,
                                 color(theme->control));
        label(surface, 36, 186, "PRIMARY", 7u, 0xffffu);
        label(surface, 164, 186, "SECONDARY", 9u, primary);
    } else if (type == ASTRA_WINDOW_UTILITY) {
        astra_surface_fill_round(surface, 18, 58, surface->width - 36u, 48u,
                                 theme->control_radius, 0xffffu);
        label(surface, 34, 77, "COMPACT TOOL WINDOW", 19u, muted);
    } else if (type == ASTRA_WINDOW_DIALOG) {
        label(surface, 18, 62, "CHANGES ARE READY TO SAVE.", 26u, muted);
        astra_surface_fill_round(surface, 128, 116, 112u, 34u,
                                 theme->control_radius, accent);
        astra_surface_fill_round(surface, 252, 116, 112u, 34u,
                                 theme->control_radius,
                                 color(theme->control));
        label(surface, 164, 128, "SAVE", 4u, 0xffffu);
        label(surface, 280, 128, "CANCEL", 6u, primary);
    } else {
        label(surface, 18, 58, "NO TITLEBAR", 11u, primary);
        label(surface, 18, 78, "CONTEXTUAL CHROME", 17u, muted);
    }
}

static void add_window(DisplayState *state, uint32_t index, uint8_t type,
                       uint16_t x, uint16_t y, uint16_t width,
                       uint16_t height, uint32_t flags, uint32_t gadgets)
{
    DisplayWindow *window = &state->windows[index];

    assert(astra_draw_list_view_init(&window->surface.view, lists[index],
                                     sizeof(lists[index]), width, height));
    {
        AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;

        paint_gallery(&window->surface.view, type, &theme);
    }
    window->request.x = x;
    window->request.y = y;
    window->request.width = width;
    window->request.height = height;
    window->request.flags = flags;
    window->request.gadgets = gadgets;
    window->request.event_mask = ASTRA_WINDOW_SUBSCRIBE_ALL;
    window->request.type = type;
    window->request.title_length = 4u;
    window->request.title[0] = 'T';
    window->request.title[1] = 'E';
    window->request.title[2] = 'S';
    window->request.title[3] = 'T';
    window->id = index + 1u;
    window->generation = 1u;
    window->cache_slot = index;
    window->cache_dirty = 1u;
    reset_content(window);
    window->event_send = 0x500u;
    ++state->count;
}

static void rendered(DisplayWindow *window)
{
    window->cache_dirty = 0u;
    window->content_dirty = 0u;
    window->content_initialized = 1u;
    window->content_damage = (DamageRect){0};
}

int main(void)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    DisplayState state = {
        .damage = {
            { 0, 0, ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT, 1u },
            { 0, 0, ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT, 1u }
        }
    };
    AstraGuiWindowCommand move = {
        .window = 4u,
        .action = ASTRA_GUI_WINDOW_MOVE,
        .x = 120u,
        .y = 110u
    };
    AstraGuiWindowCommand resize = {
        .window = 4u,
        .action = ASTRA_GUI_WINDOW_RESIZE,
        .width = 580u,
        .height = 300u
    };
    AstraLogicalInputEvent motion = {
        .type = ASTRA_INPUT_EVENT_POINTER_MOTION,
        .timestamp_ms = 99u,
        .value_x = 210,
        .value_y = 170,
    };
    DisplayWindow closed;
    uint32_t effects = 0u;
    uint32_t frame_window = 0u;
    uint32_t frame_timestamp = 0u;
    uint32_t error;
    int changed;

    {
        DisplayState fair = {.count = 2u};
        uint32_t waits[DISPLAY_WINDOW_MAX + 2u];
        uint32_t sources[DISPLAY_WINDOW_MAX + 2u];

        fair.windows[0].control_receive = 0x30u;
        fair.windows[1].control_receive = 0x40u;
        assert(display_wait_handles(&fair, 0x10u, 0x20u, 2u,
                                    waits, sources) == 4u);
        assert(waits[0] == 0x30u && sources[0] == 2u);
        assert(waits[1] == 0x40u && sources[1] == 3u);
        assert(waits[2] == 0x10u && sources[2] == 0u);
        assert(waits[3] == 0x20u && sources[3] == 1u);
    }

    add_window(&state, 0u, ASTRA_WINDOW_POPOVER, 300u, 380u,
               250u, 125u, 0u, 0u);
    assert(compose(batch, 1u, &state, &error) == ASTRA_RENDER_BUILDER_BYTES);
    rendered(&state.windows[0]);
    state.damage[1] = (DamageRect){0};
    add_window(&state, 1u, ASTRA_WINDOW_UTILITY, 600u, 80u,
               360u, 145u, 0u, ASTRA_WINDOW_GADGET_CLOSE);
    damage_window(&state, &theme, &state.windows[1]);
    assert(compose(batch, 2u, &state, &error) == ASTRA_RENDER_BUILDER_BYTES);
    rendered(&state.windows[1]);
    state.damage[0] = (DamageRect){0};
    add_window(&state, 2u, ASTRA_WINDOW_DIALOG, 520u, 300u,
               400u, 190u, ASTRA_WINDOW_MODAL, ASTRA_WINDOW_GADGET_CLOSE);
    damage_window(&state, &theme, &state.windows[2]);
    assert(compose(batch, 3u, &state, &error) == ASTRA_RENDER_BUILDER_BYTES);
    rendered(&state.windows[2]);
    state.damage[1] = (DamageRect){0};
    add_window(&state, 3u, ASTRA_WINDOW_STANDARD, 100u, 100u,
               550u, 280u, ASTRA_WINDOW_ACTIVE | ASTRA_WINDOW_RESIZABLE,
               ASTRA_WINDOW_GADGET_CLOSE | ASTRA_WINDOW_GADGET_MINIMIZE |
                   ASTRA_WINDOW_GADGET_MAXIMIZE);
    damage_window(&state, &theme, &state.windows[3]);
    assert(compose(batch, 4u, &state, &error) == ASTRA_RENDER_BUILDER_BYTES);
    rendered(&state.windows[3]);
    state.damage[0] = (DamageRect){0};
    assert(apply_command(&state, &theme, &resize, &closed, &changed) ==
           ASTRA_STATUS_OK && changed);
    assert(compose(batch, 5u, &state, &error) == ASTRA_RENDER_BUILDER_BYTES);
    assert(batch_has_surface_fill(580u, 300u, color(theme.client)));
    rendered(&state.windows[3]);
    assert(apply_command(&state, &theme, &move, &closed, &changed) ==
           ASTRA_STATUS_OK && changed);
    assert(compose(batch, 6u, &state, &error) == ASTRA_RENDER_BUILDER_BYTES);
    state.capture_window = 4u;
    state.capture_region = HIT_TITLE;
    state.capture_dx = 10;
    state.capture_dy = 10;
    assert(handle_pointer(&state, &motion, &effects, &frame_window,
                          &frame_timestamp) == ASTRA_STATUS_OK);
    motion.timestamp_ms = 100u;
    motion.value_x = 220;
    motion.value_y = 175;
    assert(handle_pointer(&state, &motion, &effects, &frame_window,
                          &frame_timestamp) == ASTRA_STATUS_OK);
    assert(state.windows[3].request.x == 210u &&
           state.windows[3].request.y == 165u);
    assert((effects & (DISPLAY_POINTER_CURSOR | DISPLAY_POINTER_RENDER |
                       DISPLAY_POINTER_FRAME)) ==
           (DISPLAY_POINTER_CURSOR | DISPLAY_POINTER_RENDER |
            DISPLAY_POINTER_FRAME));
    assert(frame_window == 4u && frame_timestamp == 100u);
    assert((ASTRA_DISPLAY_CURSOR_VISIBLE |
            ASTRA_DISPLAY_CURSOR_DEFER_COMMIT) == 3u);
    state.capture_window = 0u;
    state.capture_region = HIT_NONE;
    pointer_event(&state.windows[3], &theme,
                  ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u, 77u,
                  180, 190, 0u);
    assert(delivered.event.type == ASTRA_WINDOW_EVENT_POINTER_MOTION);
    assert(delivered.event.data.pointer.screen_x == 180 &&
           delivered.event.data.pointer.screen_y == 190);
    assert(delivered.event.data.pointer.x ==
           180 - state.windows[3].request.x -
                 frame_width(&theme, state.windows[3].request.type));
    assert(delivered.event.data.pointer.y ==
           190 - state.windows[3].request.y -
                 frame_width(&theme, state.windows[3].request.type) -
                 title_height(&theme, state.windows[3].request.type) -
                 theme.signal_height);

    {
        DisplayWindow *window = &state.windows[3];
        AstraLogicalInputEvent resize_down = {
            .type = ASTRA_INPUT_EVENT_POINTER_BUTTON,
            .flags = ASTRA_INPUT_LOGICAL_DOWN,
            .timestamp_ms = 101u,
            .code = ASTRA_INPUT_BUTTON_LEFT,
        };
        AstraLogicalInputEvent resize_motion = {
            .type = ASTRA_INPUT_EVENT_POINTER_MOTION,
            .timestamp_ms = 102u,
        };
        AstraLogicalInputEvent resize_up = {
            .type = ASTRA_INPUT_EVENT_POINTER_BUTTON,
            .timestamp_ms = 103u,
            .code = ASTRA_INPUT_BUTTON_LEFT,
        };
        uint16_t old_width = window->request.width;
        uint16_t old_height = window->request.height;

        state.pointer_x = window->request.x +
                          (int32_t)outer_width(&theme, window) -
                          theme.resize_hit;
        state.pointer_y = window->request.y +
                          (int32_t)outer_height(&theme, window) -
                          theme.resize_hit;
        assert(hit_region(&theme, window, state.pointer_x,
                          state.pointer_y) == HIT_RESIZE_SE);
        assert(handle_pointer(&state, &resize_down, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(state.capture_window == window->id &&
               state.capture_region == HIT_RESIZE_SE);
        resize_motion.value_x = state.pointer_x + 12;
        resize_motion.value_y = state.pointer_y + 7;
        assert(handle_pointer(&state, &resize_motion, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(window->request.width == old_width + 12u &&
               window->request.height == old_height + 7u);
        assert(handle_pointer(&state, &resize_up, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(state.capture_window == 0u);
    }

    {
        DisplayWindow *window = &state.windows[3];
        uint32_t width = outer_width(&theme, window);
        uint32_t height = outer_height(&theme, window);
        const struct {
            int32_t x;
            int32_t y;
            uint32_t region;
        } edges[] = {
            { (int32_t)width / 2, 1, HIT_RESIZE_N },
            { (int32_t)width - theme.resize_hit, theme.resize_hit - 1,
              HIT_RESIZE_NE },
            { (int32_t)width - 1, (int32_t)height / 2, HIT_RESIZE_E },
            { (int32_t)width - theme.resize_hit,
              (int32_t)height - theme.resize_hit, HIT_RESIZE_SE },
            { (int32_t)width / 2, (int32_t)height - 1, HIT_RESIZE_S },
            { theme.resize_hit - 1, (int32_t)height - theme.resize_hit,
              HIT_RESIZE_SW },
            { 1, (int32_t)height / 2, HIT_RESIZE_W },
            { theme.resize_hit - 1, theme.resize_hit - 1, HIT_RESIZE_NW },
        };

        for (uint32_t index = 0u;
             index < sizeof(edges) / sizeof(edges[0]); ++index)
            assert(hit_region(&theme, window,
                              window->request.x + edges[index].x,
                              window->request.y + edges[index].y) ==
                   edges[index].region);
    }

    {
        DisplayWindow *window = &state.windows[3];
        AstraLogicalInputEvent wheel = {
            .type = ASTRA_INPUT_EVENT_POINTER_BUTTON,
            .flags = ASTRA_INPUT_LOGICAL_DOWN,
            .timestamp_ms = 103u,
            .code = ASTRA_INPUT_BUTTON_WHEEL_UP,
        };
        uint32_t before = delivered_count;

        state.pointer_x = window->request.x + 40;
        state.pointer_y = window->request.y + 80;
        assert(handle_pointer(&state, &wheel, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(delivered_count == before + 1u &&
               delivered.event.type == ASTRA_WINDOW_EVENT_POINTER_WHEEL &&
               delivered.event.data.wheel.delta_x == 0 &&
               delivered.event.data.wheel.delta_y == 1 &&
               delivered.event.data.wheel.screen_x == state.pointer_x &&
               delivered.event.data.wheel.screen_y == state.pointer_y);
    }

    {
        AstraLogicalInputEvent key = {
            .type = ASTRA_INPUT_EVENT_KEY,
            .flags = ASTRA_INPUT_LOGICAL_DOWN | ASTRA_INPUT_LOGICAL_REPEAT,
            .timestamp_ms = 104u,
            .code = 0x04u,
            .value_x = ASTRA_INPUT_MOD_LEFT_SHIFT,
        };
        AstraLogicalInputEvent text_event = {
            .type = ASTRA_INPUT_EVENT_TEXT,
            .timestamp_ms = 105u,
            .code = 'A',
            .value_x = ASTRA_INPUT_MOD_LEFT_SHIFT,
        };
        uint32_t before = delivered_count;

        assert(handle_pointer(&state, &key, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(delivered_count == before + 1u &&
               delivered.event.type == ASTRA_WINDOW_EVENT_KEY &&
               delivered.event.data.key.usage == 0x04u &&
               delivered.event.data.key.modifiers ==
                   ASTRA_INPUT_MOD_LEFT_SHIFT &&
               (delivered.event.flags & (ASTRA_WINDOW_EVENT_DOWN |
                                          ASTRA_WINDOW_EVENT_REPEAT)) ==
                   (ASTRA_WINDOW_EVENT_DOWN | ASTRA_WINDOW_EVENT_REPEAT));
        assert(handle_pointer(&state, &text_event, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(delivered_count == before + 2u &&
               delivered.event.type == ASTRA_WINDOW_EVENT_TEXT &&
               delivered.event.data.text.codepoint == 'A' &&
               delivered.event.data.text.modifiers ==
                   ASTRA_INPUT_MOD_LEFT_SHIFT);
    }
    {
        AstraGuiWindowCommand present = {
            .header = {
                .total_size = sizeof(AstraGuiWindowCommand),
                .header_size = ASTRA_MESSAGE_HEADER_SIZE,
                .protocol = ASTRA_GUI_PROTOCOL,
                .protocol_version = ASTRA_GUI_VERSION,
                .operation = ASTRA_GUI_WINDOW_COMMAND,
                .transaction_id = 1u,
            },
            .window = state.windows[3].id,
            .generation = state.windows[3].generation,
            .action = ASTRA_GUI_WINDOW_PRESENT,
        };
        uint32_t generation = state.windows[3].generation;

        state.windows[3].cache_dirty = 0u;
        assert(apply_command(&state, &theme, &present, &closed, &changed) ==
               ASTRA_STATUS_OK && changed);
        assert(state.windows[3].cache_dirty == 0u &&
               state.windows[3].content_dirty != 0u &&
               state.windows[3].content_damage.left == 0 &&
               state.windows[3].content_damage.top == 0 &&
               state.windows[3].content_damage.right ==
                   state.windows[3].request.width &&
               state.windows[3].content_damage.bottom ==
                   state.windows[3].request.height &&
               state.windows[3].generation == generation + 1u);
    }
    {
        AstraGuiWindowCommand present = {
            .header = {
                .total_size = sizeof(AstraGuiWindowCommand),
                .header_size = ASTRA_MESSAGE_HEADER_SIZE,
                .protocol = ASTRA_GUI_PROTOCOL,
                .protocol_version = ASTRA_GUI_VERSION,
                .operation = ASTRA_GUI_WINDOW_COMMAND,
                .transaction_id = 1u,
            },
            .window = state.windows[3].id,
            .generation = state.windows[3].generation,
            .action = ASTRA_GUI_WINDOW_PRESENT,
            .x = 12u,
            .y = 18u,
            .width = 20u,
            .height = 14u,
        };

        state.windows[3].content_dirty = 0u;
        state.windows[3].content_damage = (DamageRect){0};
        assert(valid_command(&present, sizeof(present), 1u,
                             state.windows[3].id,
                             state.windows[3].request.width,
                             state.windows[3].request.height));
        assert(apply_command(&state, &theme, &present, &closed, &changed) ==
               ASTRA_STATUS_OK && changed);
        assert(state.windows[3].content_damage.left == 12 &&
               state.windows[3].content_damage.top == 18 &&
               state.windows[3].content_damage.right == 32 &&
               state.windows[3].content_damage.bottom == 32);
        present.x = state.windows[3].request.width - 10u;
        assert(!valid_command(&present, sizeof(present), 1u,
                              state.windows[3].id,
                              state.windows[3].request.width,
                              state.windows[3].request.height));
    }
    {
        DisplayWindow *window = &state.windows[3];
        AstraLogicalInputEvent button_down = {
            .type = ASTRA_INPUT_EVENT_POINTER_BUTTON,
            .flags = ASTRA_INPUT_LOGICAL_DOWN,
            .timestamp_ms = 106u,
            .code = ASTRA_INPUT_BUTTON_LEFT,
        };
        AstraLogicalInputEvent button_up = {
            .type = ASTRA_INPUT_EVENT_POINTER_BUTTON,
            .timestamp_ms = 107u,
            .code = ASTRA_INPUT_BUTTON_LEFT,
        };
        uint16_t restore_x = window->request.x;
        uint16_t restore_y = window->request.y;
        uint16_t restore_width = window->request.width;
        uint16_t restore_height = window->request.height;

        state.pointer_x = window->request.x + frame_width(&theme,
            window->request.type) + window->request.width -
            theme.spacing_unit - theme.gadget_extent * 3u +
            theme.gadget_extent + theme.gadget_extent / 2u;
        state.pointer_y = window->request.y + frame_width(&theme,
            window->request.type) +
            (title_height(&theme, window->request.type) -
             theme.gadget_extent) / 2u + theme.gadget_extent / 2u;
        assert(hit_region(&theme, window, state.pointer_x,
                          state.pointer_y) == HIT_MAXIMIZE);
        assert(handle_pointer(&state, &button_down, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(window->request.maximize_state == ASTRA_GADGET_PRESSED);
        assert(handle_pointer(&state, &button_up, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(window->state == ASTRA_WINDOW_STATE_MAXIMIZED &&
               window->request.maximize_state == ASTRA_GADGET_NORMAL);

        state.pointer_x = window->request.x + frame_width(&theme,
            window->request.type) + window->request.width -
            theme.spacing_unit - theme.gadget_extent * 3u +
            theme.gadget_extent + theme.gadget_extent / 2u;
        state.pointer_y = window->request.y + frame_width(&theme,
            window->request.type) +
            (title_height(&theme, window->request.type) -
             theme.gadget_extent) / 2u + theme.gadget_extent / 2u;
        assert(handle_pointer(&state, &button_down, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(handle_pointer(&state, &button_up, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(window->state == ASTRA_WINDOW_STATE_NORMAL &&
               window->request.x == restore_x &&
               window->request.y == restore_y &&
               window->request.width == restore_width &&
               window->request.height == restore_height);

        state.pointer_x = window->request.x + frame_width(&theme,
            window->request.type) + window->request.width -
            theme.spacing_unit - theme.gadget_extent * 3u +
            theme.gadget_extent / 2u;
        state.pointer_y = window->request.y + frame_width(&theme,
            window->request.type) +
            (title_height(&theme, window->request.type) -
             theme.gadget_extent) / 2u + theme.gadget_extent / 2u;
        assert(handle_pointer(&state, &button_down, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(handle_pointer(&state, &button_up, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(window->state == ASTRA_WINDOW_STATE_MINIMIZED);
        {
            AstraGuiWindowCommand activate = {
                .window = window->id,
                .generation = window->generation,
                .action = ASTRA_GUI_WINDOW_ACTIVATE,
            };

            assert(apply_command(&state, &theme, &activate, &closed,
                                 &changed) == ASTRA_STATUS_OK && changed);
            assert(window->state == ASTRA_WINDOW_STATE_NORMAL &&
                   (window->request.flags & ASTRA_WINDOW_ACTIVE) != 0u);
        }

        state.pointer_x = window->request.x + frame_width(&theme,
            window->request.type) + window->request.width -
            theme.spacing_unit - theme.gadget_extent / 2u;
        state.pointer_y = window->request.y + frame_width(&theme,
            window->request.type) +
            (title_height(&theme, window->request.type) -
             theme.gadget_extent) / 2u + theme.gadget_extent / 2u;
        {
            uint32_t before = delivered_count;

            assert(handle_pointer(&state, &button_down, &effects,
                                  &frame_window, &frame_timestamp) ==
                   ASTRA_STATUS_OK);
            assert(handle_pointer(&state, &button_up, &effects,
                                  &frame_window, &frame_timestamp) ==
                   ASTRA_STATUS_OK);
            assert(delivered_count == before + 1u &&
                   delivered.event.type ==
                       ASTRA_WINDOW_EVENT_CLOSE_REQUEST);
        }
    }
    {
        DisplayState focus = {0};
        uint32_t before = delivered_count;

        add_window(&focus, 0u, ASTRA_WINDOW_STANDARD, 100u, 100u,
                   200u, 120u, ASTRA_WINDOW_ACTIVE, 0u);
        add_window(&focus, 1u, ASTRA_WINDOW_STANDARD, 160u, 140u,
                   200u, 120u, 0u, 0u);
        activate(&focus, &theme, focus.windows[1].id, 1, 108u);
        assert(delivered_count == before + 2u &&
               delivered.event.type == ASTRA_WINDOW_EVENT_FOCUS &&
               (delivered.event.flags & ASTRA_WINDOW_EVENT_FOCUSED) != 0u &&
               (focus.windows[1].request.flags & ASTRA_WINDOW_ACTIVE) != 0u);
    }
    puts("display compositor tests passed");
    return 0;
}
