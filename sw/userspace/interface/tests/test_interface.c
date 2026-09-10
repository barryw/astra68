#include <astra/interface.h>
#include <astra/control.h>
#include <astra/draw_list.h>
#include <astra/input_modifiers.h>
#include <astra/surface.h>
#include <astra/theme.h>

#include <assert.h>
#include <string.h>

#include "../src/control_internal.h"

int astra_interface_test_valid(const AstraAlertInfo *info);
void astra_interface_test_paint(AstraSurfaceView *surface,
                                const AstraAlertInfo *info);
void astra_interface_test_layout(void);

static uint16_t first_text_color;
static uint16_t last_text_color;
static uint16_t fill_colors[32];
static int32_t fill_x[32];
static int32_t fill_y[32];
static uint32_t fill_width[32];
static uint32_t fill_height[32];
static uint16_t render_pixels[320u * 200u];
static uint32_t fill_calls;
static uint32_t text_calls;

static uint16_t theme_color(AstraColorRGBA8 value)
{
    return astra_surface_rgb565(value.red, value.green, value.blue);
}

int astra_surface_view_init(AstraSurfaceView *surface, void *pixels,
                            uint32_t byte_size, uint16_t width,
                            uint16_t height, uint32_t pitch)
{
    if (surface == NULL || pixels == NULL || width == 0u || height == 0u ||
        pitch < (uint32_t)width * sizeof(uint16_t) ||
        (uint64_t)pitch * height > byte_size)
        return 0;
    return 1;
}

int astra_surface_clip(AstraSurfaceView *surface, int32_t x, int32_t y,
                       uint32_t width, uint32_t height)
{
    int64_t right = (int64_t)x + width;
    int64_t bottom = (int64_t)y + height;

    if (surface == NULL || surface->clip_left > surface->clip_right ||
        surface->clip_right > surface->width ||
        surface->clip_top > surface->clip_bottom ||
        surface->clip_bottom > surface->height)
        return 0;
    if (x < surface->clip_left) x = surface->clip_left;
    if (y < surface->clip_top) y = surface->clip_top;
    if (x > surface->clip_right) x = surface->clip_right;
    if (y > surface->clip_bottom) y = surface->clip_bottom;
    if (right > surface->clip_right) right = surface->clip_right;
    if (bottom > surface->clip_bottom) bottom = surface->clip_bottom;
    if (right < x) right = x;
    if (bottom < y) bottom = y;
    surface->clip_left = (uint16_t)x;
    surface->clip_top = (uint16_t)y;
    surface->clip_right = (uint16_t)right;
    surface->clip_bottom = (uint16_t)bottom;
    return 1;
}

int astra_draw_list_view_adopt(AstraSurfaceView *surface, void *storage,
                               uint32_t byte_size, uint16_t width,
                               uint16_t height)
{
    const AstraDrawListHeader *header = storage;

    if (surface == NULL || storage == NULL ||
        byte_size < ASTRA_DRAW_LIST_AREA_BYTES)
        return 0;
    return header->magic == ASTRA_DRAW_LIST_MAGIC &&
           header->version == ASTRA_DRAW_LIST_VERSION_1_1 &&
           header->total_bytes == ASTRA_DRAW_LIST_AREA_BYTES &&
           header->width == width && header->height == height &&
           header->command_count <= ASTRA_DRAW_LIST_COMMAND_MAX &&
           header->payload_bytes <= ASTRA_DRAW_LIST_PAYLOAD_BYTES;
}

uint16_t astra_surface_rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint16_t)(((uint16_t)(red & 0xf8u) << 8) |
                      ((uint16_t)(green & 0xfcu) << 3) | (blue >> 3));
}

void astra_surface_clear(AstraSurfaceView *surface, uint16_t color)
{
    (void)surface;
    (void)color;
}

void astra_surface_fill(AstraSurfaceView *surface, int32_t x, int32_t y,
                        uint32_t width, uint32_t height, uint16_t color)
{
    (void)surface;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    if (fill_calls < sizeof(fill_colors) / sizeof(fill_colors[0])) {
        fill_colors[fill_calls] = color;
        fill_x[fill_calls] = x;
        fill_y[fill_calls] = y;
        fill_width[fill_calls] = width;
        fill_height[fill_calls] = height;
    }
    ++fill_calls;
}

void astra_surface_fill_round(AstraSurfaceView *surface, int32_t x, int32_t y,
                              uint32_t width, uint32_t height,
                              uint16_t radius, uint16_t color)
{
    (void)surface;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)radius;
    if (fill_calls < sizeof(fill_colors) / sizeof(fill_colors[0])) {
        fill_colors[fill_calls] = color;
        fill_x[fill_calls] = x;
        fill_y[fill_calls] = y;
        fill_width[fill_calls] = width;
        fill_height[fill_calls] = height;
    }
    ++fill_calls;
}

uint32_t astra_surface_ui_text_width(const char *text, uint32_t length,
                                     uint16_t height)
{
    (void)text;
    (void)height;
    return length;
}

uint32_t astra_surface_ui_text_fit(const char *text, uint32_t length,
                                   uint16_t height, uint32_t maximum_width)
{
    (void)text;
    (void)height;
    (void)maximum_width;
    return length;
}

void astra_surface_ui_text(AstraSurfaceView *surface, int32_t x, int32_t y,
                           const char *text, uint32_t length,
                           uint16_t height, uint16_t color)
{
    (void)surface;
    (void)x;
    (void)y;
    (void)text;
    (void)length;
    (void)height;
    if (text_calls++ == 0u) first_text_color = color;
    last_text_color = color;
}

static AstraWindowEvent pointer_event(uint16_t type, uint32_t flags,
                                      int32_t x, int32_t y)
{
    AstraWindowEvent event = {0};

    event.size = sizeof(event);
    event.version = ASTRA_WINDOW_EVENT_VERSION;
    event.type = type;
    event.flags = flags;
    event.data.pointer.x = x;
    event.data.pointer.y = y;
    event.data.pointer.button = 1u;
    return event;
}

static AstraWindowEvent key_event(uint32_t flags, uint32_t usage,
                                  uint32_t modifiers)
{
    AstraWindowEvent event = {0};

    event.size = sizeof(event);
    event.version = ASTRA_WINDOW_EVENT_VERSION;
    event.type = ASTRA_WINDOW_EVENT_KEY;
    event.flags = flags;
    event.data.key.usage = usage;
    event.data.key.modifiers = modifiers;
    return event;
}

static AstraWindowEvent frame_event(uint16_t width, uint16_t height)
{
    AstraWindowEvent event = {0};

    event.size = sizeof(event);
    event.version = ASTRA_WINDOW_EVENT_VERSION;
    event.type = ASTRA_WINDOW_EVENT_FRAME;
    event.data.frame.frame.width = width;
    event.data.frame.frame.height = height;
    return event;
}

static void reset_render_calls(void)
{
    first_text_color = 0u;
    last_text_color = 0u;
    fill_calls = 0u;
    text_calls = 0u;
}

static void test_controls(void)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraControl controls[3] = {
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraButtonInfo first = ASTRA_BUTTON_INFO_INIT;
    AstraButtonInfo second = ASTRA_BUTTON_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraControlSize measured = {0};
    AstraControlFrame damage = {0};
    AstraSurfaceView surface = {
        render_pixels, sizeof(render_pixels), 320u * sizeof(uint16_t),
        320u, 200u, ASTRA_SURFACE_VIEW_RGB565, 0u, 0u, 320u, 200u};
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent event;

    assert(theme.generation == 5u);
    assert(theme.scale == 1u);
    assert(theme.spacing_unit == 4u * theme.scale);
    assert(theme.control_height == 28u * theme.scale);
    assert(theme.control_padding_x == 14u * theme.scale);
    assert(theme.focus_width == 2u * theme.scale);
    assert(theme.text_secondary.red == 201u);
    assert(theme.text_tertiary.red == 151u);
    assert(theme.client_text.red == 26u);
    assert(theme.accent_text.green == 34u);

    label.id = 10u;
    label.text = "Controls";
    label.text_length = 8u;
    label.text_role = ASTRA_TEXT_CLIENT_PRIMARY;
    assert(astra_interface_label_init(&controls[0], &label) == ASTRA_OK);
    item.flags = ASTRA_FLEX_BREAK_AFTER;
    assert(astra_interface_control_set_flex(&controls[0], &item) == ASTRA_OK);

    first.id = 20u;
    first.text = "Apply";
    first.text_length = 5u;
    first.variant = ASTRA_BUTTON_PRIMARY;
    assert(astra_interface_button_init(&controls[1], &first) == ASTRA_OK);

    second.id = 30u;
    second.text = "Erase";
    second.text_length = 5u;
    second.variant = ASTRA_BUTTON_DESTRUCTIVE;
    assert(astra_interface_button_init(&controls[2], &second) == ASTRA_OK);

    assert(astra_interface_ui_init(&context, controls, 3u, 320u, 200u) ==
           ASTRA_OK);
    assert(astra_interface_ui_measure(&context, &controls[1], &measured) ==
           ASTRA_OK);
    assert(measured.width == 33u && measured.height == 28u);
    layout.flags = ASTRA_FLEX_WRAP;
    layout.padding_left = 8u;
    layout.padding_top = 8u;
    layout.padding_right = 8u;
    layout.padding_bottom = 8u;
    layout.main_gap = 8u;
    layout.cross_gap = 12u;
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(controls[0]._private_frame.x == 8 &&
           controls[0]._private_frame.y == 8 &&
           controls[0]._private_frame.width == 8u &&
           controls[0]._private_frame.height == 11u);
    assert(controls[1]._private_frame.x == 8 &&
           controls[1]._private_frame.y == 31 &&
           controls[1]._private_frame.width == 33u &&
           controls[1]._private_frame.height == 28u);
    assert(controls[2]._private_frame.x == 49 &&
           controls[2]._private_frame.y == 31 &&
           controls[2]._private_frame.width == 33u &&
           controls[2]._private_frame.height == 28u);
    assert(astra_interface_ui_damage(&context, &damage) == ASTRA_OK);
    assert(damage.x == 8 && damage.y == 8 && damage.width == 74u &&
           damage.height == 51u);
    astra_interface_ui_damage_clear(&context);
    assert(astra_interface_ui_damage(&context, &damage) ==
           ASTRA_ERROR_WOULD_BLOCK);

    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(text_calls == 3u && fill_calls == 2u);
    assert(first_text_color == astra_surface_rgb565(
        theme.client_text.red, theme.client_text.green, theme.client_text.blue));
    assert(fill_colors[0] == astra_surface_rgb565(
        theme.accent.red, theme.accent.green, theme.accent.blue));
    assert(last_text_color == astra_surface_rgb565(
        theme.text_primary.red, theme.text_primary.green,
        theme.text_primary.blue));

    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u, 20, 44);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_NONE);
    assert(astra_interface_ui_damage(&context, &damage) == ASTRA_OK);
    astra_interface_ui_damage_clear(&context);

    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN, 20, 44);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u, 200, 100);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON, 0u, 200, 100);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_NONE);

    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN, 20, 44);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON, 0u, 20, 44);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_ACTIVATE && action.control_id == 20u);

    assert(astra_interface_ui_set_state(
               &context, &controls[1], ASTRA_CONTROL_DISABLED) == ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN, 20, 44);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON, 0u, 20, 44);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_NONE);
    assert(astra_interface_ui_set_state(&context, &controls[1], 0u) ==
           ASTRA_OK);

    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x2bu,
                      ASTRA_INPUT_MOD_LEFT_SHIFT);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x28u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = key_event(0u, 0x28u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_ACTIVATE && action.control_id == 30u);

    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x2bu,
                      ASTRA_INPUT_MOD_RIGHT_SHIFT);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x28u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = key_event(0u, 0x28u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_ACTIVATE && action.control_id == 20u);

    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x2bu,
                      ASTRA_INPUT_MOD_LEFT_CTRL);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x28u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = key_event(0u, 0x28u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_ACTIVATE && action.control_id == 30u);

    event = (AstraWindowEvent){0};
    event.size = sizeof(event);
    event.version = ASTRA_WINDOW_EVENT_VERSION;
    event.type = ASTRA_WINDOW_EVENT_STATE_RESET;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(astra_interface_ui_damage(&context, &damage) == ASTRA_OK);
}

static void test_button_preview_states(void)
{
    enum { BUTTON_COUNT = 10 };
    static const char *const names[BUTTON_COUNT] = {
        "Normal", "Hover", "Pressed", "Focused", "Disabled",
        "Selected", "Error", "Primary", "Warning", "Destructive"};
    static const uint32_t semantic[BUTTON_COUNT] = {
        0u, 0u, 0u, 0u, ASTRA_CONTROL_DISABLED,
        ASTRA_CONTROL_SELECTED, ASTRA_CONTROL_ERROR, 0u, 0u, 0u};
    static const uint32_t preview[BUTTON_COUNT] = {
        0u, ASTRA_CONTROL_HOVERED, ASTRA_CONTROL_PRESSED,
        ASTRA_CONTROL_FOCUSED, 0u, 0u, 0u, 0u, 0u, 0u};
    static const uint32_t variant[BUTTON_COUNT] = {
        ASTRA_BUTTON_STANDARD, ASTRA_BUTTON_STANDARD, ASTRA_BUTTON_STANDARD,
        ASTRA_BUTTON_STANDARD, ASTRA_BUTTON_STANDARD, ASTRA_BUTTON_STANDARD,
        ASTRA_BUTTON_STANDARD, ASTRA_BUTTON_PRIMARY, ASTRA_BUTTON_WARNING,
        ASTRA_BUTTON_DESTRUCTIVE};
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraControl controls[BUTTON_COUNT];
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraSurfaceView surface = {
        render_pixels, sizeof(render_pixels), 320u * sizeof(uint16_t),
        320u, 200u, ASTRA_SURFACE_VIEW_RGB565, 0u, 0u, 320u, 200u};

    for (uint32_t at = 0u; at < BUTTON_COUNT; ++at) {
        AstraButtonInfo info = ASTRA_BUTTON_INFO_INIT;

        controls[at] = (AstraControl)ASTRA_CONTROL_INIT;
        info.id = at + 1u;
        info.text = names[at];
        info.text_length = (uint32_t)strlen(names[at]);
        info.variant = variant[at];
        info.state = semantic[at];
        info.preview_state = preview[at];
        assert(astra_interface_button_init(&controls[at], &info) == ASTRA_OK);
    }
    assert(astra_interface_ui_init(&context, controls, BUTTON_COUNT,
                                   320u, 200u) == ASTRA_OK);
    layout.flags = ASTRA_FLEX_WRAP;
    layout.padding_left = 8u;
    layout.padding_top = 8u;
    layout.padding_right = 8u;
    layout.padding_bottom = 8u;
    layout.main_gap = 6u;
    layout.cross_gap = 12u;
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(text_calls == BUTTON_COUNT && fill_calls == 12u);
    assert(fill_colors[0] == theme_color(theme.control));
    assert(fill_colors[1] == theme_color(theme.control_hover));
    assert(fill_colors[2] == theme_color(theme.control_pressed));
    assert(fill_colors[3] == theme_color(theme.control_focus));
    assert(fill_colors[5] == theme_color(theme.control_disabled));
    assert(fill_colors[6] == theme_color(theme.control_selected));
    assert(fill_colors[7] == theme_color(theme.control_error));
    assert(fill_colors[9] == theme_color(theme.accent));
    assert(fill_colors[10] == theme_color(theme.warning));
    assert(fill_colors[11] == theme_color(theme.fault));
}

static void test_flex_growth(void)
{
    AstraControl controls[2] = {ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraButtonInfo info = ASTRA_BUTTON_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraControlFrame damage;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent event;

    info.id = 1u;
    info.text = "AA";
    info.text_length = 2u;
    assert(astra_interface_button_init(&controls[0], &info) == ASTRA_OK);
    info.id = 2u;
    assert(astra_interface_button_init(&controls[1], &info) == ASTRA_OK);
    item.grow = 1u;
    assert(astra_interface_control_set_flex(&controls[0], &item) == ASTRA_OK);
    assert(astra_interface_control_set_flex(&controls[1], &item) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 2u, 100u, 50u) ==
           ASTRA_OK);
    layout.padding_left = 10u;
    layout.padding_top = 5u;
    layout.padding_right = 10u;
    layout.padding_bottom = 5u;
    layout.main_gap = 4u;
    layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(controls[0]._private_frame.x == 10 &&
           controls[0]._private_frame.y == 5 &&
           controls[0]._private_frame.width == 38u &&
           controls[0]._private_frame.height == 40u);
    assert(controls[1]._private_frame.x == 52 &&
           controls[1]._private_frame.y == 5 &&
           controls[1]._private_frame.width == 38u &&
           controls[1]._private_frame.height == 40u);
    astra_interface_ui_damage_clear(&context);
    event = frame_event(120u, 80u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(controls[0]._private_frame.x == 10 &&
           controls[0]._private_frame.y == 5 &&
           controls[0]._private_frame.width == 48u &&
           controls[0]._private_frame.height == 70u);
    assert(controls[1]._private_frame.x == 62 &&
           controls[1]._private_frame.y == 5 &&
           controls[1]._private_frame.width == 48u &&
           controls[1]._private_frame.height == 70u);
    assert(astra_interface_ui_damage(&context, &damage) == ASTRA_OK);
    assert(damage.x == 0 && damage.y == 0 && damage.width == 120u &&
           damage.height == 80u);
}

static void test_control_rejections(void)
{
    static const char malformed_utf8[] = {(char)0xc0, (char)0x80};
    AstraControl controls[2] = {ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraButtonInfo info = ASTRA_BUTTON_INFO_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraSurfaceView surface = {
        render_pixels, sizeof(render_pixels), 100u * sizeof(uint16_t),
        100u, 100u, ASTRA_SURFACE_VIEW_RGB565, 0u, 0u, 100u, 100u};

    info.id = 1u;
    info.text = "A";
    info.text_length = 1u;
    assert(astra_interface_button_init(&controls[0], &info) == ASTRA_OK);
    assert(astra_interface_button_init(&controls[1], &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 2u, 100u, 100u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    info.preview_state = ASTRA_CONTROL_DISABLED;
    assert(astra_interface_button_init(&controls[0], &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);

    info.preview_state = 0u;
    info.text = malformed_utf8;
    info.text_length = sizeof(malformed_utf8);
    assert(astra_interface_button_init(&controls[0], &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    info.text = "A";
    info.text_length = 1u;
    assert(astra_interface_button_init(&controls[0], &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 1u, 100u, 100u) ==
           ASTRA_OK);
    controls[0]._private_control_kind = 0u;
    assert(astra_interface_ui_render(&context, &surface) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    controls[0]._private_control_kind = ASTRA_CONTROL_BUTTON;
    surface.byte_size = 1u;
    assert(astra_interface_ui_render(&context, &surface) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
}

static void test_toggle_controls(void)
{
    AstraControl controls[4] = {
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT,
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraToggleInfo info = ASTRA_TOGGLE_INFO_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent event;

    info.id = 1u;
    info.text = "Snap to grid";
    info.text_length = 12u;
    assert(astra_interface_checkbox_init(&controls[0], &info) == ASTRA_OK);
    info.id = 2u;
    info.text = "RGB565";
    info.text_length = 6u;
    info.group_id = 7u;
    info.state = ASTRA_CONTROL_SELECTED;
    assert(astra_interface_radio_init(&controls[1], &info) == ASTRA_OK);
    info.id = 3u;
    info.text = "XRGB8888";
    info.text_length = 8u;
    info.state = 0u;
    assert(astra_interface_radio_init(&controls[2], &info) == ASTRA_OK);
    info.id = 4u;
    info.text = "Tear-free";
    info.text_length = 9u;
    info.group_id = 0u;
    assert(astra_interface_switch_init(&controls[3], &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 4u, 240u, 140u) ==
           ASTRA_OK);
    layout.direction = ASTRA_FLEX_COLUMN;
    layout.main_gap = 4u;
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(astra_interface_ui_set_state(
               &context, &controls[2], ASTRA_CONTROL_SELECTED) == ASTRA_OK);
    assert((controls[1]._private_state & ASTRA_CONTROL_SELECTED) == 0u);
    assert((controls[2]._private_state & ASTRA_CONTROL_SELECTED) != 0u);
    assert(astra_interface_ui_set_state(
               &context, &controls[1], ASTRA_CONTROL_SELECTED) == ASTRA_OK);
    assert((controls[1]._private_state & ASTRA_CONTROL_SELECTED) != 0u);
    assert((controls[2]._private_state & ASTRA_CONTROL_SELECTED) == 0u);

    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN,
                          controls[0]._private_frame.x + 1,
                          controls[0]._private_frame.y + 1);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event.flags = 0u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.control_id == 1u && action.value == 1);
    assert((controls[0]._private_state & ASTRA_CONTROL_SELECTED) != 0u);

    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN,
                          controls[2]._private_frame.x + 1,
                          controls[2]._private_frame.y + 1);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event.flags = 0u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.control_id == 3u && action.value == 1);
    assert((controls[1]._private_state & ASTRA_CONTROL_SELECTED) == 0u);
    assert((controls[2]._private_state & ASTRA_CONTROL_SELECTED) != 0u);

    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x2bu, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x28u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = key_event(0u, 0x28u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_VALUE_CHANGED);

    info.id = 5u;
    info.group_id = 0u;
    assert(astra_interface_radio_init(&controls[0], &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    info.group_id = 1u;
    assert(astra_interface_checkbox_init(&controls[0], &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);

    {
        AstraControl duplicates[2] = {
            ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
        AstraUIContext duplicate_context = ASTRA_UI_CONTEXT_INIT;

        info.group_id = 9u;
        info.state = ASTRA_CONTROL_SELECTED;
        info.id = 6u;
        assert(astra_interface_radio_init(&duplicates[0], &info) == ASTRA_OK);
        info.id = 7u;
        assert(astra_interface_radio_init(&duplicates[1], &info) == ASTRA_OK);
        assert(astra_interface_ui_init(&duplicate_context, duplicates, 2u,
                                       100u, 100u) ==
               ASTRA_ERROR_INVALID_ARGUMENT);
    }
}

static void test_checkbox_mark_geometry(void)
{
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraToggleInfo info = ASTRA_TOGGLE_INFO_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraSurfaceView surface = {
        render_pixels, sizeof(render_pixels), 100u * sizeof(uint16_t),
        100u, 40u, ASTRA_SURFACE_VIEW_RGB565, 0u, 0u, 100u, 40u};
    static const int32_t expected_x[5] = {3, 5, 7, 9, 11};
    static const int32_t expected_y[5] = {7, 9, 7, 5, 3};

    info.id = 11u;
    info.text = "Checked";
    info.text_length = 7u;
    info.state = ASTRA_CONTROL_SELECTED;
    assert(astra_interface_checkbox_init(&control, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &control, 1u, 100u, 40u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(fill_calls == 7u);
    for (uint32_t at = 0u; at < 5u; ++at) {
        assert(fill_x[at + 2u] == expected_x[at]);
        assert(fill_y[at + 2u] == expected_y[at] + 6);
        assert(fill_width[at + 2u] == 3u);
        assert(fill_height[at + 2u] == 3u);
    }
}

static void test_switch_off_geometry(void)
{
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraToggleInfo info = ASTRA_TOGGLE_INFO_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraSurfaceView surface = {
        render_pixels, sizeof(render_pixels), 100u * sizeof(uint16_t),
        100u, 40u, ASTRA_SURFACE_VIEW_RGB565, 0u, 0u, 100u, 40u};

    info.id = 12u;
    info.text = "Off";
    info.text_length = 3u;
    assert(astra_interface_switch_init(&control, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &control, 1u, 100u, 40u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(fill_calls == 3u);
    assert(fill_x[1] == 2 && fill_y[1] == 7 &&
           fill_width[1] == 14u && fill_height[1] == 14u);
    assert(fill_x[2] == 4 && fill_y[2] == 9 &&
           fill_width[2] == 10u && fill_height[2] == 10u);
}

static void test_control_text_update(void)
{
    AstraControl controls[2] = {ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraLabelInfo info = ASTRA_LABEL_INFO_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraControlFrame damage;

    info.id = 1u;
    info.text = "A";
    info.text_length = 1u;
    assert(astra_interface_label_init(&controls[0], &info) == ASTRA_OK);
    info.id = 2u;
    assert(astra_interface_label_init(&controls[1], &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 2u, 100u, 40u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(controls[1]._private_frame.x == 1);
    astra_interface_ui_damage_clear(&context);

    assert(astra_interface_control_set_text(&context, &controls[0],
                                            "Long", 4u) == ASTRA_OK);
    assert(controls[0]._private_measured_width == 4u);
    assert(controls[1]._private_frame.x == 4);
    assert(astra_interface_ui_damage(&context, &damage) == ASTRA_OK);
    assert(damage.x == 0 && damage.y == 0 && damage.width == 5u);
    astra_interface_ui_damage_clear(&context);
    assert(astra_interface_control_set_text(&context, &controls[0],
                                            "Long", 4u) == ASTRA_OK);
    assert(astra_interface_ui_damage(&context, &damage) == ASTRA_OK);
    assert(astra_interface_control_set_text(&context, &controls[0],
                                            NULL, 0u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
}

static void test_slider_control(void)
{
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraRangeInfo info = ASTRA_RANGE_INFO_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent event;
    int32_t value = 0;

    info.id = 21u;
    info.minimum = -50;
    info.maximum = 50;
    info.value = 0;
    info.step = 10;
    assert(astra_interface_slider_init(&control, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &control, 1u, 200u, 40u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN,
                          control._private_frame.x +
                              (int32_t)control._private_frame.width - 1,
                          control._private_frame.y + 10);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.control_id == 21u && action.value == 50);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u,
                          control._private_frame.x - 100,
                          control._private_frame.y + 10);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_VALUE_CHANGED && action.value == -50);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON, 0u,
                          control._private_frame.x - 100,
                          control._private_frame.y + 10);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(astra_interface_control_set_value(&context, &control, 47) ==
           ASTRA_OK);
    assert(astra_interface_control_get_value(&control, &value) == ASTRA_OK &&
           value == 50);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x50u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_VALUE_CHANGED && action.value == 40);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4au, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.value == -50);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4du, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.value == 50);

    info.maximum = info.minimum;
    assert(astra_interface_slider_init(&control, &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    info.maximum = 50;
    info.step = 0;
    assert(astra_interface_slider_init(&control, &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    info.step = 10;
    info.value = 51;
    assert(astra_interface_slider_init(&control, &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
}

static void test_progress_control(void)
{
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraProgressInfo info = ASTRA_PROGRESS_INFO_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraControlFrame damage;
    AstraSurfaceView surface = {
        render_pixels, sizeof(render_pixels), 200u * sizeof(uint16_t),
        200u, 40u, ASTRA_SURFACE_VIEW_RGB565, 0u, 0u, 200u, 40u};

    info.id = 31u;
    assert(astra_interface_progress_init(&control, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &control, 1u, 200u, 40u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(fill_calls == 2u);
    astra_interface_ui_damage_clear(&context);
    assert(astra_interface_ui_tick(&context, 1u) == ASTRA_OK);
    assert(astra_interface_ui_damage(&context, &damage) ==
           ASTRA_ERROR_WOULD_BLOCK);
    assert(astra_interface_ui_tick(&context, 50000001u) == ASTRA_OK);
    assert(astra_interface_ui_damage(&context, &damage) == ASTRA_OK);
    astra_interface_ui_damage_clear(&context);
    assert(astra_interface_ui_tick(&context, 75000001u) == ASTRA_OK);
    assert(astra_interface_ui_damage(&context, &damage) ==
           ASTRA_ERROR_WOULD_BLOCK);
    assert(astra_interface_ui_tick(&context, 100000001u) == ASTRA_OK);
    assert(astra_interface_ui_damage(&context, &damage) == ASTRA_OK);
    astra_interface_ui_damage_clear(&context);
    assert(astra_interface_ui_set_state(
               &context, &control, ASTRA_CONTROL_DISABLED) == ASTRA_OK);
    astra_interface_ui_damage_clear(&context);
    assert(astra_interface_ui_tick(&context, 150000001u) == ASTRA_OK);
    assert(astra_interface_ui_damage(&context, &damage) ==
           ASTRA_ERROR_WOULD_BLOCK);
    assert(astra_interface_ui_set_state(&context, &control, 0u) == ASTRA_OK);
    astra_interface_ui_damage_clear(&context);
    assert(astra_interface_progress_set(&context, &control, 68u, 100u) ==
           ASTRA_OK);
    assert(control._private_value == 68 &&
           control._private_maximum_value == 100);
    assert(astra_interface_progress_set(&context, &control, 101u, 100u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_interface_progress_set(&context, &control, 100u, 100u) ==
           ASTRA_OK);
    info.value = 1u;
    info.maximum = 0u;
    assert(astra_interface_progress_init(&control, &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    info.maximum = UINT32_MAX;
    assert(astra_interface_progress_init(&control, &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
}

int main(void)
{
    static const char malformed_utf8[] = {(char)0xed, (char)0xa0, (char)0x80};
    AstraAlertInfo info = ASTRA_ALERT_INFO_INIT;

    info.title = "Error";
    info.title_length = 5u;
    info.message = "Could not launch application.";
    info.message_length = 29u;
    info.button = "OK";
    info.button_length = 2u;
    assert(astra_interface_test_valid(&info));
    info.kind = 0u;
    assert(!astra_interface_test_valid(&info));
    info.kind = ASTRA_ALERT_ERROR;
    info.reserved[0] = 1u;
    assert(!astra_interface_test_valid(&info));
    info.reserved[0] = 0u;
    info.message = malformed_utf8;
    info.message_length = sizeof(malformed_utf8);
    assert(!astra_interface_test_valid(&info));
    info.message = "Could not launch application.";
    info.message_length = 29u;
    {
        AstraSurfaceView surface = {
            render_pixels, sizeof(render_pixels), 420u * sizeof(uint16_t),
            420u, 150u, ASTRA_SURFACE_VIEW_RGB565, 0u, 0u, 420u, 150u};
        AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;

        astra_interface_test_paint(&surface, &info);
        assert(text_calls == 2u);
        assert(first_text_color == astra_surface_rgb565(
            theme.client_text.red, theme.client_text.green,
            theme.client_text.blue));
        assert(first_text_color != astra_surface_rgb565(
            theme.client.red, theme.client.green, theme.client.blue));
    }
    test_controls();
    test_button_preview_states();
    test_flex_growth();
    test_control_rejections();
    test_toggle_controls();
    test_checkbox_mark_geometry();
    test_switch_off_geometry();
    test_control_text_update();
    test_slider_control();
    test_progress_control();
    astra_interface_test_layout();
    return 0;
}
