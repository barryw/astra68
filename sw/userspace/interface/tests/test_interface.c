#include <astra/interface.h>
#include <astra/interface_library.h>
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

_Static_assert(sizeof(AstraControl) == (sizeof(void *) == 4u ? 252u : 264u),
               "AstraControl ABI 5 layout changed; bump the ABI major");
_Static_assert(sizeof(AstraUIContext) ==
                   (sizeof(void *) == 4u ? 168u : 176u),
               "AstraUIContext ABI 5 layout changed; bump the ABI major");

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
static uint16_t text_clip_top;
static uint16_t text_clip_bottom;
static uint32_t mono_text_calls;
static uint32_t copy_calls;
static uint32_t copy_source_x;
static uint32_t copy_source_y;
static uint32_t copy_destination_x;
static uint32_t copy_destination_y;
static uint32_t copy_width;
static uint32_t copy_height;
static uint32_t line_calls;
static uint32_t pointer_shape_calls;
static uint32_t last_pointer_shape;

AstraResult astra_window_set_pointer_shape(AstraWindow *window,
                                           AstraPointerShape shape)
{
    assert(window != NULL && (uint32_t)shape < ASTRA_POINTER_SHAPE_COUNT);
    ++pointer_shape_calls;
    last_pointer_shape = (uint32_t)shape;
    return ASTRA_OK;
}

AstraResult astra_window_set_pointer_image(
    AstraWindow *window, const AstraHardwarePointerImage *image)
{
    (void)window;
    (void)image;
    return ASTRA_ERROR_UNSUPPORTED;
}

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
           header->version == ASTRA_DRAW_LIST_VERSION_1_2 &&
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

int astra_surface_line(AstraSurfaceView *surface, int32_t x0, int32_t y0,
                       int32_t x1, int32_t y1, uint16_t color)
{
    (void)surface;
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    (void)color;
    ++line_calls;
    return 1;
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
    text_clip_top = surface->clip_top;
    text_clip_bottom = surface->clip_bottom;
}

int astra_surface_mono_text_styled(AstraSurfaceView *surface, int32_t x,
                                   int32_t y, const char *utf8,
                                   uint32_t length, uint16_t pixel_height,
                                   uint16_t cell_width, uint16_t color,
                                   uint32_t style_flags)
{
    (void)surface;
    if (mono_text_calls < sizeof(fill_x) / sizeof(fill_x[0]))
        fill_x[mono_text_calls] = x;
    (void)y;
    (void)utf8;
    (void)length;
    (void)pixel_height;
    (void)cell_width;
    (void)color;
    (void)style_flags;
    ++mono_text_calls;
    return 1;
}

int astra_text_box_scroll(AstraTextBox *text_box, int32_t pixels)
{
    (void)text_box;
    (void)pixels;
    return 1;
}

int astra_draw_list_copy(AstraSurfaceView *surface, uint32_t source_x,
                         uint32_t source_y, uint32_t destination_x,
                         uint32_t destination_y, uint32_t width,
                         uint32_t height)
{
    (void)surface;
    ++copy_calls;
    copy_source_x = source_x;
    copy_source_y = source_y;
    copy_destination_x = destination_x;
    copy_destination_y = destination_y;
    copy_width = width;
    copy_height = height;
    return 1;
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

static AstraWindowEvent wheel_event(int32_t x, int32_t y,
                                    int32_t delta_x, int32_t delta_y,
                                    uint32_t modifiers)
{
    AstraWindowEvent event = {0};

    event.size = sizeof(event);
    event.version = ASTRA_WINDOW_EVENT_VERSION;
    event.type = ASTRA_WINDOW_EVENT_POINTER_WHEEL;
    event.data.wheel.x = x;
    event.data.wheel.y = y;
    event.data.wheel.delta_x = delta_x;
    event.data.wheel.delta_y = delta_y;
    event.data.wheel.modifiers = modifiers;
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
    text_clip_top = 0u;
    text_clip_bottom = 0u;
    mono_text_calls = 0u;
    copy_calls = 0u;
    line_calls = 0u;
}

static AstraWindowEvent text_event(uint32_t codepoint)
{
    AstraWindowEvent event = {0};

    event.size = sizeof(event);
    event.version = ASTRA_WINDOW_EVENT_VERSION;
    event.type = ASTRA_WINDOW_EVENT_TEXT;
    event.data.text.codepoint = codepoint;
    return event;
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

static void test_fault_label(void)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraLabelInfo info = ASTRA_LABEL_INFO_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraSurfaceView surface = {
        render_pixels, sizeof(render_pixels), 100u * sizeof(uint16_t),
        100u, 40u, ASTRA_SURFACE_VIEW_RGB565, 0u, 0u, 100u, 40u};

    info.id = 11u;
    info.text = "Fault";
    info.text_length = 5u;
    info.text_role = ASTRA_TEXT_CLIENT_FAULT;
    assert(astra_interface_label_init(&control, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &control, 1u, 100u, 40u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(text_calls == 1u && first_text_color == theme_color(theme.fault));
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
    int64_t value = 0;

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
    assert(astra_interface_control_get_value(&control, &value, NULL) == ASTRA_OK &&
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

static void test_dial_control(void)
{
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraControl wide_control = ASTRA_CONTROL_INIT;
    AstraDialInfo info = ASTRA_DIAL_INFO_INIT;
    AstraDialInfo wide_info = ASTRA_DIAL_INFO_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraUIContext wide_context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraControlSize measured = {0};
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent event;
    AstraSurfaceView surface = {
        render_pixels, sizeof(render_pixels), 120u * sizeof(uint16_t),
        120u, 80u, ASTRA_SURFACE_VIEW_RGB565, 0u, 0u, 120u, 80u};
    int64_t value = 0;
    uint32_t decimal_places = 0u;

    info.id = 22u;
    info.minimum = -100;
    info.maximum = 100;
    info.value = 0;
    info.step = 5;
    info.reset_value = 0;
    assert(astra_interface_dial_init(&control, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &control, 1u, 120u, 80u) ==
           ASTRA_OK);
    assert(astra_interface_ui_measure(&context, &control, &measured) ==
           ASTRA_OK);
    assert(measured.width == 56u && measured.height == 56u);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);

    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN, 28, 28);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_NONE);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u, 28, 18);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.control_id == 22u && action.value == 50 &&
           action.decimal_places == 0u);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON, 0u, 28, 18);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);

    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN, 28, 28);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u, 28, 24);
    event.data.pointer.modifiers = ASTRA_INPUT_MOD_LEFT_ALT;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.value == 55);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON, 0u, 28, 24);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);

    assert(astra_interface_control_set_value(&context, &control, -10) ==
           ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN, 28, 28);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u, 28, 27);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.value == 0);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON, 0u, 28, 27);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);

    assert(astra_interface_control_set_value(&context, &control, 75) ==
           ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN, 28, 28);
    event.data.pointer.click_count = 2u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.value == 0);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON, 0u, 28, 28);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);

    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x52u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.value == 5);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4au, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.value == -100);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4du, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.value == 100);
    assert(astra_interface_control_set_value(&context, &control, 17) ==
           ASTRA_OK);
    assert(astra_interface_control_get_value(&control, &value, NULL) == ASTRA_OK &&
           value == 15);

    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(fill_calls == 3u && line_calls == 1u);

    info.reset_value = 3;
    assert(astra_interface_dial_init(&control, &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    info.reset_value = 0;
    info.step = 0;
    assert(astra_interface_dial_init(&control, &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);

    wide_info.id = 23u;
    wide_info.minimum = INT64_C(-3286);
    wide_info.maximum = INT64_C(732145632765400);
    wide_info.value = INT64_C(-3286);
    wide_info.step = INT64_C(1);
    wide_info.reset_value = INT64_C(1250);
    wide_info.decimal_places = 2u;
    assert(astra_interface_dial_init(&wide_control, &wide_info) == ASTRA_OK);
    assert(astra_interface_ui_init(
               &wide_context, &wide_control, 1u, 120u, 80u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&wide_context, &layout) == ASTRA_OK);
    assert(astra_interface_control_set_value(
               &wide_context, &wide_control,
               INT64_C(732145632765399)) == ASTRA_OK);
    assert(astra_interface_control_get_value(
               &wide_control, &value, &decimal_places) ==
               ASTRA_OK &&
           value == INT64_C(732145632765399) && decimal_places == 2u);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN, 28, 28);
    assert(astra_interface_ui_handle_event(
               &wide_context, &event, &action) == ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u, 28, 27);
    assert(astra_interface_ui_handle_event(
               &wide_context, &event, &action) == ASTRA_OK &&
           action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.value == INT64_C(732145632765400) &&
           action.decimal_places == 2u);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON, 0u, 28, 27);
    assert(astra_interface_ui_handle_event(
               &wide_context, &event, &action) == ASTRA_OK);
    reset_render_calls();
    assert(astra_interface_ui_render(&wide_context, &surface) == ASTRA_OK);
    assert(fill_calls == 2u && line_calls == 1u);

    wide_info.minimum = INT64_MIN;
    wide_info.maximum = INT64_MAX;
    wide_info.value = 0;
    wide_info.step = 1;
    wide_info.reset_value = 0;
    wide_info.decimal_places = 0u;
    assert(astra_interface_dial_init(&wide_control, &wide_info) == ASTRA_OK);
    assert(astra_interface_ui_init(
               &wide_context, &wide_control, 1u, 120u, 80u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&wide_context, &layout) == ASTRA_OK);
    assert(astra_interface_control_set_value(
               &wide_context, &wide_control, INT64_MAX - 1) == ASTRA_OK);
    reset_render_calls();
    assert(astra_interface_ui_render(&wide_context, &surface) == ASTRA_OK);
    assert(fill_calls == 2u && line_calls == 1u);
}

static void test_progress_control(void)
{
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraProgressInfo info = ASTRA_PROGRESS_INFO_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
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
    assert(astra_interface_ui_animations_active(&context));
    assert(control._private_animation_next == UINT32_MAX &&
           control._private_animation_phase == 0u);
    assert(astra_interface_ui_vblank(&context, 1u, &action) == ASTRA_OK);
    assert(control._private_animation_phase == 1u);
    assert(astra_interface_ui_damage(&context, &damage) == ASTRA_OK);
    astra_interface_ui_damage_clear(&context);
    assert(astra_interface_ui_vblank(&context, 16666668u, &action) ==
           ASTRA_OK);
    assert(control._private_animation_phase == 2u);
    assert(astra_interface_ui_damage(&context, &damage) == ASTRA_OK);
    astra_interface_ui_damage_clear(&context);
    assert(astra_interface_ui_set_state(
               &context, &control, ASTRA_CONTROL_DISABLED) == ASTRA_OK);
    astra_interface_ui_damage_clear(&context);
    assert(!astra_interface_ui_animations_active(&context));
    assert(astra_interface_ui_vblank(&context, 33333335u, &action) ==
           ASTRA_OK);
    assert(astra_interface_ui_damage(&context, &damage) ==
           ASTRA_ERROR_WOULD_BLOCK);
    assert(astra_interface_ui_set_state(&context, &control, 0u) == ASTRA_OK);
    astra_interface_ui_damage_clear(&context);
    assert(astra_interface_progress_set(&context, &control, 68u, 100u) ==
           ASTRA_OK);
    assert(!astra_interface_ui_animations_active(&context));
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

static void test_segmented_control(void)
{
    static const AstraChoiceItem items[] = {
        {"Windows", 7u, {0u, 0u}},
        {"Scenes", 6u, {0u, 0u}},
        {"Ports", 5u, {0u, 0u}}
    };
    static const char malformed[] = {(char)0xed, (char)0xa0, (char)0x80};
    AstraChoiceInfo info = ASTRA_CHOICE_INFO_INIT;
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent event;
    AstraSurfaceView surface = {
        render_pixels, sizeof(render_pixels), 240u * sizeof(uint16_t),
        240u, 40u, ASTRA_SURFACE_VIEW_RGB565, 0u, 0u, 240u, 40u};
    int64_t value = -1;

    info.id = 51u;
    info.items = items;
    info.item_count = 3u;
    info.selected = 0u;
    assert(astra_interface_segmented_init(&control, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &control, 1u, 240u, 40u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);

    event = pointer_event(
        ASTRA_WINDOW_EVENT_POINTER_BUTTON, ASTRA_WINDOW_EVENT_DOWN,
        control._private_frame.x +
            (int32_t)(control._private_frame.width * 5u / 6u),
        control._private_frame.y + 10);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_NONE);
    event.flags = 0u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.control_id == 51u && action.value == 2);
    assert(astra_interface_control_get_value(&control, &value, NULL) == ASTRA_OK &&
           value == 2);

    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x50u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.value == 1);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4au, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.value == 0);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4du, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.value == 2);
    assert(astra_interface_control_set_value(&context, &control, 1) ==
           ASTRA_OK);
    assert(astra_interface_control_set_value(&context, &control, 3) ==
           ASTRA_ERROR_INVALID_ARGUMENT);

    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN,
                          control._private_frame.x + 1,
                          control._private_frame.y + 10);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u,
                          control._private_frame.x - 1,
                          control._private_frame.y + 10);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event.type = ASTRA_WINDOW_EVENT_POINTER_BUTTON;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_NONE);
    assert(astra_interface_control_get_value(&control, &value, NULL) == ASTRA_OK &&
           value == 1);
    event = pointer_event(
        ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u,
        control._private_frame.x +
            (int32_t)(control._private_frame.width * 5u / 6u),
        control._private_frame.y + 10);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);

    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(text_calls == 3u && fill_calls == 6u);
    assert(astra_interface_ui_set_state(
               &context, &control, ASTRA_CONTROL_DISABLED) == ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN,
                          control._private_frame.x + 1,
                          control._private_frame.y + 1);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_NONE);
    assert(astra_interface_control_get_value(&control, &value, NULL) == ASTRA_OK &&
           value == 1);

    info.item_count = 0u;
    assert(astra_interface_segmented_init(&control, &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    info.item_count = 3u;
    info.selected = 3u;
    assert(astra_interface_segmented_init(&control, &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    info.selected = 0u;
    {
        AstraChoiceItem bad = {malformed, sizeof(malformed), {0u, 0u}};

        info.items = &bad;
        info.item_count = 1u;
        assert(astra_interface_segmented_init(&control, &info) ==
               ASTRA_ERROR_INVALID_ARGUMENT);
    }
}

static void test_tab_control_and_collapsed_pages(void)
{
    static const AstraChoiceItem items[] = {
        {"Input", 5u, {0u, 0u}},
        {"Choice", 6u, {0u, 0u}},
        {"Value", 5u, {0u, 0u}}
    };
    AstraChoiceInfo info = ASTRA_CHOICE_INFO_INIT;
    AstraContainerInfo first_page = ASTRA_CONTAINER_INFO_INIT;
    AstraContainerInfo second_page = ASTRA_CONTAINER_INFO_INIT;
    AstraButtonInfo first_button = ASTRA_BUTTON_INFO_INIT;
    AstraButtonInfo second_button = ASTRA_BUTTON_INFO_INIT;
    AstraControl controls[5] = {
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT,
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent event;
    uint32_t pointer_fill_calls;
    AstraSurfaceView surface = {
        render_pixels, sizeof(render_pixels), 320u * sizeof(uint16_t),
        320u, 120u, ASTRA_SURFACE_VIEW_RGB565, 0u, 0u, 320u, 120u};

    info.id = 60u;
    info.items = items;
    info.item_count = 3u;
    assert(astra_interface_tab_init(&controls[0], &info) == ASTRA_OK);

    first_page.id = 61u;
    first_page.layout.direction = ASTRA_FLEX_COLUMN;
    assert(astra_interface_container_init(&controls[1], &first_page) ==
           ASTRA_OK);
    item.parent_id = 61u;
    assert(astra_interface_control_set_flex(&controls[1],
                                             &(AstraFlexItem)
                                             ASTRA_FLEX_ITEM_INIT) ==
           ASTRA_OK);
    first_button.id = 62u;
    first_button.text = "First";
    first_button.text_length = 5u;
    assert(astra_interface_button_init(&controls[2], &first_button) ==
           ASTRA_OK);
    assert(astra_interface_control_set_flex(&controls[2], &item) == ASTRA_OK);

    second_page.id = 63u;
    second_page.state = ASTRA_CONTROL_COLLAPSED;
    second_page.layout.direction = ASTRA_FLEX_COLUMN;
    assert(astra_interface_container_init(&controls[3], &second_page) ==
           ASTRA_OK);
    second_button.id = 64u;
    second_button.text = "Second";
    second_button.text_length = 6u;
    assert(astra_interface_button_init(&controls[4], &second_button) ==
           ASTRA_OK);
    item.parent_id = 63u;
    assert(astra_interface_control_set_flex(&controls[4], &item) == ASTRA_OK);

    layout.direction = ASTRA_FLEX_COLUMN;
    layout.main_gap = 8u;
    assert(astra_interface_ui_init(&context, controls, 5u, 320u, 120u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(controls[0]._private_laid_out != 0u);
    assert(controls[1]._private_laid_out != 0u);
    assert(controls[2]._private_laid_out != 0u);
    assert(controls[3]._private_laid_out == 0u);
    assert(controls[4]._private_laid_out == 0u);
    assert(controls[1]._private_frame.x == controls[0]._private_frame.x);
    assert(controls[1]._private_frame.y ==
           controls[0]._private_frame.y +
               (int32_t)controls[0]._private_frame.height + 8);

    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN,
                          controls[2]._private_frame.x + 1,
                          controls[2]._private_frame.y + 1);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event.flags = 0u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_ACTIVATE);
    assert(context._private_focus == 2u);

    assert(astra_interface_ui_set_state(
               &context, &controls[1], ASTRA_CONTROL_COLLAPSED) == ASTRA_OK);
    assert(astra_interface_ui_set_state(&context, &controls[3], 0u) ==
           ASTRA_OK);
    assert(context._private_focus == UINT32_MAX);
    assert(controls[1]._private_laid_out == 0u);
    assert(controls[2]._private_laid_out == 0u);
    assert(controls[3]._private_laid_out != 0u);
    assert(controls[4]._private_laid_out != 0u);

    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    pointer_fill_calls = fill_calls;

    event = pointer_event(
        ASTRA_WINDOW_EVENT_POINTER_BUTTON, ASTRA_WINDOW_EVENT_DOWN,
        controls[0]._private_frame.x +
            (int32_t)(controls[0]._private_frame.width / 2u),
        controls[0]._private_frame.y + 8);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_NONE);
    event.flags = 0u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.control_id == 60u && action.value == 1);
    assert((controls[0]._private_dynamic_state & ASTRA_CONTROL_FOCUSED) != 0u);

    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(fill_calls == pointer_fill_calls);
    assert(text_calls == 4u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4du, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.value == 2);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(fill_calls == pointer_fill_calls + 2u);
}

static void test_stepper_control(void)
{
    AstraStepperInfo info = ASTRA_STEPPER_INFO_INIT;
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent event;
    AstraSurfaceView surface = {
        render_pixels, sizeof(render_pixels), 160u * sizeof(uint16_t),
        160u, 40u, ASTRA_SURFACE_VIEW_RGB565, 0u, 0u, 160u, 40u};
    int64_t value = 0;

    info.id = 52u;
    info.value = 36;
    info.minimum = 24;
    info.maximum = 60;
    info.step = 12;
    assert(astra_interface_stepper_init(&control, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &control, 1u, 160u, 40u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);

    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN,
                          control._private_frame.x +
                              (int32_t)control._private_frame.width - 4,
                          control._private_frame.y + 4);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.control_id == 52u && action.value == 48);
    assert(astra_interface_ui_animations_active(&context));

    action = (AstraUIAction)ASTRA_UI_ACTION_INIT;
    assert(astra_interface_ui_vblank(&context, 1u, &action) == ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_NONE);
    assert(astra_interface_ui_vblank(&context, UINT64_C(400000000),
                                     &action) == ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.value == 60);

    event.flags = 0u;
    action = (AstraUIAction)ASTRA_UI_ACTION_INIT;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(!astra_interface_ui_animations_active(&context));

    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x51u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.value == 48);

    event = text_event('6');
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_NONE);
    event = text_event('0');
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.value == 60);
    assert(astra_interface_control_get_value(&control, &value, NULL) == ASTRA_OK &&
           value == 60);

    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(mono_text_calls == 1u && fill_calls >= 9u);

    assert(astra_interface_ui_set_state(
               &context, &control, ASTRA_CONTROL_DISABLED) == ASTRA_OK);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x51u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_NONE);
    assert(astra_interface_control_get_value(&control, &value, NULL) == ASTRA_OK &&
           value == 60);

    info.value = 25;
    assert(astra_interface_stepper_init(&control, &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    info.value = 36;
    info.reserved[0] = 1u;
    assert(astra_interface_stepper_init(&control, &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    info.reserved[0] = 0u;
    info.value = 0;
    info.minimum = -60;
    info.maximum = 60;
    {
        AstraUIContext signed_context = ASTRA_UI_CONTEXT_INIT;

        assert(astra_interface_stepper_init(&control, &info) == ASTRA_OK);
        assert(astra_interface_ui_init(
                   &signed_context, &control, 1u, 160u, 40u) == ASTRA_OK);
        assert(astra_interface_ui_layout(&signed_context, &layout) ==
               ASTRA_OK);
        event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                              ASTRA_WINDOW_EVENT_DOWN,
                              control._private_frame.x + 2,
                              control._private_frame.y + 2);
        assert(astra_interface_ui_handle_event(
                   &signed_context, &event, &action) == ASTRA_OK);
        event.flags = 0u;
        assert(astra_interface_ui_handle_event(
                   &signed_context, &event, &action) == ASTRA_OK);
        event = text_event('-');
        assert(astra_interface_ui_handle_event(
                   &signed_context, &event, &action) == ASTRA_OK &&
               action.type == ASTRA_UI_ACTION_NONE);
        event = text_event('2');
        assert(astra_interface_ui_handle_event(
                   &signed_context, &event, &action) == ASTRA_OK &&
               action.type == ASTRA_UI_ACTION_NONE);
        event = text_event('4');
        assert(astra_interface_ui_handle_event(
                   &signed_context, &event, &action) == ASTRA_OK &&
               action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
               action.value == -24);
    }
    info.value = INT64_MIN;
    info.minimum = INT64_MIN;
    info.maximum = INT64_MAX;
    info.step = 1;
    assert(astra_interface_stepper_init(&control, &info) == ASTRA_OK);
    context = (AstraUIContext)ASTRA_UI_CONTEXT_INIT;
    assert(astra_interface_ui_init(&context, &control, 1u, 160u, 40u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(mono_text_calls == 1u);
    assert(astra_interface_control_set_value(
               &context, &control, INT64_MAX) == ASTRA_OK);
    assert(astra_interface_control_get_value(
               &control, &value, NULL) == ASTRA_OK && value == INT64_MAX);
}

static void test_animation_ownership(void)
{
    AstraControl controls[3] = {
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraProgressInfo info = ASTRA_PROGRESS_INFO_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;

    for (uint32_t index = 0u; index < 3u; ++index) {
        info.id = index + 1u;
        assert(astra_interface_progress_init(&controls[index], &info) ==
               ASTRA_OK);
    }
    assert(astra_interface_ui_init(&context, controls, 3u, 100u, 20u) ==
           ASTRA_OK);
    assert(context._private_animation_first == 2u &&
           controls[2]._private_animation_next == 1u &&
           controls[1]._private_animation_next == 0u &&
           controls[0]._private_animation_next == UINT32_MAX);
    assert(astra_interface_ui_vblank(&context, 1u, &action) == ASTRA_OK);
    assert(controls[0]._private_animation_phase == 1u &&
           controls[1]._private_animation_phase == 1u &&
           controls[2]._private_animation_phase == 1u);
    assert(astra_interface_ui_set_state(
               &context, &controls[1], ASTRA_CONTROL_DISABLED) == ASTRA_OK);
    assert(controls[2]._private_animation_next == 0u &&
           controls[1]._private_animation_next == UINT32_MAX);
    assert(astra_interface_ui_vblank(&context, 16666668u, &action) ==
           ASTRA_OK);
    assert(controls[0]._private_animation_phase == 2u &&
           controls[1]._private_animation_phase == 1u &&
           controls[2]._private_animation_phase == 2u);
    assert(astra_interface_ui_set_state(&context, &controls[1], 0u) ==
           ASTRA_OK);
    assert(context._private_animation_first == 1u &&
           controls[1]._private_animation_phase == 0u &&
           controls[1]._private_animation_next == 2u);
}

static void test_text_field(void)
{
    uint8_t content[128] = {0};
    _Alignas(4) uint8_t metadata[256] = {0};
    AstraTextModel model = ASTRA_TEXT_MODEL_INIT;
    AstraTextModelInfo model_info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraTextModelState model_state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraControl field = ASTRA_CONTROL_INIT;
    AstraFieldInfo info = ASTRA_FIELD_INFO_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraSurfaceView surface = {
        render_pixels, sizeof(render_pixels), 160u * sizeof(uint16_t),
        160u, 40u, ASTRA_SURFACE_VIEW_RGB565, 0u, 0u, 160u, 40u};
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent event;
    char copied[32];
    uint32_t copied_bytes;

    model_info.text = "A\xce\xbb" "B";
    model_info.text_bytes = 4u;
    model_info.content_arena = content;
    model_info.content_arena_bytes = sizeof(content);
    model_info.metadata_arena = metadata;
    model_info.metadata_arena_bytes = sizeof(metadata);
    model_info.selection.anchor = 4u;
    model_info.selection.focus = 4u;
    assert(astra_text_model_init(&model, &model_info) == ASTRA_OK);
    info.id = 41u;
    info.model = &model;
    info.preferred_columns = 8u;
    assert(astra_interface_field_init(&field, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &field, 1u, 160u, 40u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(field._private_frame.width == 92u &&
           field._private_frame.height == 28u);

    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN,
                          field._private_frame.x + 14 + 8,
                          field._private_frame.y + 10);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_SELECTION_CHANGED &&
           action.control_id == 41u);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK);
    assert(model_state.selection.anchor == 1u &&
           model_state.selection.focus == 1u);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u,
                          field._private_frame.x + 14 + 24,
                          field._private_frame.y + 10);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_SELECTION_CHANGED);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK);
    assert(model_state.selection.anchor == 1u &&
           model_state.selection.focus == 4u);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON, 0u,
                          field._private_frame.x + 14 + 24,
                          field._private_frame.y + 10);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);

    event = text_event(0x4e16u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_TEXT_CHANGED &&
           action.control_id == 41u);
    assert(astra_text_model_copy(&model, 0u, 4u, copied, sizeof(copied),
                                 &copied_bytes) == ASTRA_OK);
    assert(copied_bytes == 4u && memcmp(copied, "A\xe4\xb8\x96", 4u) == 0);

    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x50u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_SELECTION_CHANGED);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK);
    assert(model_state.selection.anchor == 1u &&
           model_state.selection.focus == 1u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4fu,
                      ASTRA_INPUT_MOD_LEFT_SHIFT);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK);
    assert(model_state.selection.anchor == 1u &&
           model_state.selection.focus == 4u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x2au, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_TEXT_CHANGED);
    assert(astra_text_model_copy(&model, 0u, 1u, copied, sizeof(copied),
                                 &copied_bytes) == ASTRA_OK);
    assert(copied_bytes == 1u && copied[0] == 'A');

    event = text_event(0x03bbu);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_TEXT_CHANGED);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4au, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_SELECTION_CHANGED);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4cu, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_TEXT_CHANGED);
    assert(astra_text_model_copy(&model, 0u, 2u, copied, sizeof(copied),
                                 &copied_bytes) == ASTRA_OK);
    assert(copied_bytes == 2u && memcmp(copied, "\xce\xbb", 2u) == 0);

    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4du, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x2au, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_TEXT_CHANGED);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK);
    assert(model_state.text_bytes == 0u);
    event = text_event('\n');
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_NONE);

    event = text_event('x');
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x04u,
                      ASTRA_INPUT_MOD_META);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_SELECTION_CHANGED);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x06u,
                      ASTRA_INPUT_MOD_META);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_COPY);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x1bu,
                      ASTRA_INPUT_MOD_META);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_CUT);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x19u,
                      ASTRA_INPUT_MOD_META);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_PASTE);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x28u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_ACTIVATE);

    assert(astra_interface_field_replace_selection(
               &context, &field, "ok", 2u) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK);
    assert(model_state.text_bytes == 2u);
    assert(astra_interface_field_replace_selection(
               &context, &field, "bad\n", 4u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK &&
           model_state.text_bytes == 2u);

    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(fill_calls >= 4u && mono_text_calls == 1u);
    astra_interface_ui_damage_clear(&context);
    assert(astra_text_model_replace(&model, 1u, 1u, "yz", 2u) == ASTRA_OK);
    assert(astra_interface_field_refresh(&context, &field) == ASTRA_OK);
    {
        AstraControlFrame damage;

        assert(astra_interface_ui_damage(&context, &damage) == ASTRA_OK);
    }
    assert(astra_interface_ui_set_state(
               &context, &field,
               ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_ERROR) == ASTRA_OK);
}

static void test_text_field_rejections(void)
{
    uint8_t content[16] = {0};
    _Alignas(4) uint8_t metadata[64] = {0};
    AstraTextModel model = ASTRA_TEXT_MODEL_INIT;
    AstraTextModelInfo model_info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraFieldInfo info = ASTRA_FIELD_INFO_INIT;
    AstraControl field = ASTRA_CONTROL_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent event;

    model_info.text = "a\nb";
    model_info.text_bytes = 3u;
    model_info.content_arena = content;
    model_info.content_arena_bytes = sizeof(content);
    model_info.metadata_arena = metadata;
    model_info.metadata_arena_bytes = sizeof(metadata);
    assert(astra_text_model_init(&model, &model_info) == ASTRA_OK);
    info.id = 42u;
    info.model = &model;
    assert(astra_interface_field_init(&field, &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    astra_text_model_dispose(&model);

    model_info.text = NULL;
    model_info.text_bytes = 0u;
    model_info.content_arena_bytes = 1u;
    assert(astra_text_model_init(&model, &model_info) == ASTRA_OK);
    info.model = &model;
    assert(astra_interface_field_init(&field, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &field, 1u, 80u, 32u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN, 1, 1);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = text_event(0x4e16u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_ERROR_BUFFER_TOO_SMALL);
    assert(astra_text_model_validate(&model) == ASTRA_OK);

    info.flags = ASTRA_FIELD_READ_ONLY;
    field = (AstraControl)ASTRA_CONTROL_INIT;
    assert(astra_interface_field_init(&field, &info) == ASTRA_OK);
    context = (AstraUIContext)ASTRA_UI_CONTEXT_INIT;
    assert(astra_interface_ui_init(&context, &field, 1u, 80u, 32u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN, 1, 1);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = text_event('x');
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_NONE);
    assert(astra_interface_field_replace_selection(
               &context, &field, "x", 1u) == ASTRA_ERROR_PERMISSION);
}

static void test_scroll_view_layout_and_wheel(void)
{
    static const char content[] =
        "This content is deliberately wider than its viewport.";
    AstraScrollModel model = ASTRA_SCROLL_MODEL_INIT;
    AstraScrollModelInfo model_info = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraScrollViewInfo view_info = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraScrollbarInfo bar_info = ASTRA_SCROLLBAR_INFO_INIT;
    AstraContainerInfo content_info = ASTRA_CONTAINER_INFO_INIT;
    AstraLabelInfo label_info = ASTRA_LABEL_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout root = ASTRA_FLEX_LAYOUT_INIT;
    AstraControl controls[4] = {
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT,
        ASTRA_CONTROL_INIT};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
    AstraScrollMark marks[2] = {
        { 0u, 95u, ASTRA_SCROLL_MARK_WARNING, 0u },
        { 0u, 190u, ASTRA_SCROLL_MARK_FAULT, 0u }};
    AstraWindowEvent event;
    AstraDrawListHeader list = {
        .magic = ASTRA_DRAW_LIST_MAGIC,
        .version = ASTRA_DRAW_LIST_VERSION_1_2,
        .total_bytes = ASTRA_DRAW_LIST_AREA_BYTES,
        .width = 130u,
        .height = 60u};
    AstraSurfaceView draw_list = {
        (uint16_t *)(void *)&list, ASTRA_DRAW_LIST_AREA_BYTES, 0u,
        130u, 60u, ASTRA_SURFACE_VIEW_DRAW_LIST, 0u, 0u, 130u, 60u};

    model_info.line_width = 8u;
    model_info.line_height = 16u;
    assert(astra_scroll_init(&model, &model_info) == ASTRA_OK);
    view_info.id = 100u;
    view_info.model = &model;
    view_info.preferred_width = 120u;
    view_info.preferred_height = 60u;
    assert(astra_interface_scroll_view_init(&controls[0], &view_info) ==
           ASTRA_OK);
    item.minimum_width = 120u;
    item.maximum_width = 120u;
    item.minimum_height = 60u;
    item.maximum_height = 60u;
    assert(astra_interface_control_set_flex(&controls[0], &item) == ASTRA_OK);

    content_info.id = 101u;
    content_info.layout.direction = ASTRA_FLEX_COLUMN;
    content_info.layout.padding_bottom = 180u;
    assert(astra_interface_container_init(&controls[1], &content_info) ==
           ASTRA_OK);
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = 100u;
    assert(astra_interface_control_set_flex(&controls[1], &item) == ASTRA_OK);

    label_info.id = 102u;
    label_info.text = content;
    label_info.text_length = sizeof(content) - 1u;
    assert(astra_interface_label_init(&controls[2], &label_info) == ASTRA_OK);
    item.parent_id = 101u;
    assert(astra_interface_control_set_flex(&controls[2], &item) == ASTRA_OK);

    bar_info.id = 103u;
    bar_info.model = &model;
    bar_info.orientation = ASTRA_ORIENTATION_VERTICAL;
    bar_info.flags = ASTRA_SCROLLBAR_OVERLAY;
    assert(astra_interface_scrollbar_init(&controls[3], &bar_info) ==
           ASTRA_OK);
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.minimum_width = 10u;
    item.maximum_width = 10u;
    item.minimum_height = 60u;
    item.maximum_height = 60u;
    assert(astra_interface_control_set_flex(&controls[3], &item) == ASTRA_OK);

    assert(astra_interface_ui_init(&context, controls, 4u, 130u, 60u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &root) == ASTRA_OK);
    assert(astra_scroll_get_state(&model, &state) == ASTRA_OK);
    assert(state.content_width == sizeof(content) - 1u &&
           state.content_height == 191u &&
           state.viewport_width == 120u && state.viewport_height == 60u);
    assert(controls[1]._private_frame.x == 0 &&
           controls[1]._private_frame.y == 0 &&
           controls[1]._private_clip.x == 0 &&
           controls[1]._private_clip.y == 0 &&
           controls[1]._private_clip.width == 120u &&
           controls[1]._private_clip.height == 60u &&
           controls[2]._private_clip.width == state.content_width &&
           controls[2]._private_clip.height == 60u);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &(AstraSurfaceView){
               render_pixels, sizeof(render_pixels),
               320u * sizeof(uint16_t), 130u, 60u,
               ASTRA_SURFACE_VIEW_RGB565, 0u, 0u, 130u, 60u}) == ASTRA_OK);
    assert(fill_calls == 2u);
    assert(fill_x[0] == 120 && fill_y[0] == 0 &&
           fill_width[0] == 10u && fill_height[0] == 60u);
    assert(fill_x[1] == 122 && fill_y[1] == 0 &&
           fill_width[1] == 6u && fill_height[1] == 24u);
    assert(astra_scroll_set_marks(&model, marks, 2u) == ASTRA_OK);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &(AstraSurfaceView){
               render_pixels, sizeof(render_pixels),
               320u * sizeof(uint16_t), 130u, 60u,
               ASTRA_SURFACE_VIEW_RGB565, 0u, 0u, 130u, 60u}) == ASTRA_OK);
    assert(fill_calls == 4u && fill_height[2] == 2u &&
           fill_height[3] == 2u);
    astra_interface_ui_damage_clear(&context);

    event = wheel_event(20, 20, 0, -1, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_SCROLL_CHANGED &&
           action.control_id == 100u);
    assert(astra_scroll_get_state(&model, &state) == ASTRA_OK);
    assert(state.offset_y == 16u && controls[1]._private_frame.y == -16);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &draw_list) == ASTRA_OK);
    assert(copy_calls == 1u && copy_source_x == 0u && copy_source_y == 16u &&
           copy_destination_x == 0u && copy_destination_y == 0u &&
           copy_width == 120u && copy_height == 44u);
    assert(fill_x[0] == 0 && fill_y[0] == 44 &&
           fill_width[0] == 120u && fill_height[0] == 16u &&
           text_calls == 1u && text_clip_top == 44u &&
           text_clip_bottom == 60u);

    event = wheel_event(20, 20, 0, 1, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(state.offset_y == 16u);
    assert(astra_scroll_get_state(&model, &state) == ASTRA_OK);
    assert(state.offset_y == 0u && controls[1]._private_frame.y == 0);

    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN, 125, 50);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_SCROLL_CHANGED &&
           action.control_id == 103u);
    assert(astra_scroll_get_state(&model, &state) == ASTRA_OK);
    assert(state.offset_y == 60u);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON, 0u, 125, 50);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);

    assert(astra_scroll_set_offset(&model, 0u, 0u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &root) == ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN, 125, 5);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u, 125, 15);
    event.data.pointer.modifiers = ASTRA_INPUT_MOD_LEFT_SHIFT;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_SCROLL_CHANGED &&
           action.control_id == 103u);
    assert(astra_scroll_get_state(&model, &state) == ASTRA_OK);
    assert(state.offset_y == 10u);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON, 0u, 125, 15);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);

    assert(astra_scroll_set_offset(&model, 0u, 0u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &root) == ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN, 125, 48);
    event.data.pointer.modifiers = ASTRA_INPUT_MOD_LEFT_ALT;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(astra_scroll_get_state(&model, &state) == ASTRA_OK);
    assert(state.offset_y == 131u);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON, 0u, 125, 48);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u, 0, 0);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    for (uint32_t frame = 0u; frame < 48u; ++frame)
        assert(astra_interface_ui_vblank(
                   &context,
                   UINT64_C(1000000000) +
                       (uint64_t)frame * UINT64_C(16666667),
                   &action) == ASTRA_OK);
    assert(!astra_interface_ui_animations_active(&context));
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &(AstraSurfaceView){
               render_pixels, sizeof(render_pixels),
               320u * sizeof(uint16_t), 130u, 60u,
               ASTRA_SURFACE_VIEW_RGB565, 0u, 0u, 130u, 60u}) == ASTRA_OK);
    assert(fill_calls == 0u);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u, 125, 30);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(astra_interface_ui_animations_active(&context));
    event = wheel_event(125, 30, 0, 1, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_SCROLL_CHANGED &&
           action.control_id == 103u);
    assert(astra_scroll_get_state(&model, &state) == ASTRA_OK);
    assert(state.offset_y == 115u);
}

static void test_scroll_view_keyboard_listener(void)
{
    AstraScrollModel model = ASTRA_SCROLL_MODEL_INIT;
    AstraScrollModelInfo model_info = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraScrollViewInfo view_info = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraContainerInfo content_info = ASTRA_CONTAINER_INFO_INIT;
    AstraButtonInfo button_info = ASTRA_BUTTON_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout root = ASTRA_FLEX_LAYOUT_INIT;
    AstraControl controls[3] = {
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
    AstraWindowEvent event;

    model_info.line_width = 8u;
    model_info.line_height = 16u;
    assert(astra_scroll_init(&model, &model_info) == ASTRA_OK);
    view_info.id = 200u;
    view_info.model = &model;
    view_info.preferred_width = 120u;
    view_info.preferred_height = 60u;
    assert(astra_interface_scroll_view_init(&controls[0], &view_info) ==
           ASTRA_OK);
    content_info.id = 201u;
    content_info.layout.padding_bottom = 180u;
    assert(astra_interface_container_init(&controls[1], &content_info) ==
           ASTRA_OK);
    item.parent_id = 200u;
    assert(astra_interface_control_set_flex(&controls[1], &item) == ASTRA_OK);
    button_info.id = 202u;
    button_info.text = "Inside";
    button_info.text_length = 6u;
    assert(astra_interface_button_init(&controls[2], &button_info) ==
           ASTRA_OK);
    item.parent_id = 201u;
    assert(astra_interface_control_set_flex(&controls[2], &item) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 3u, 120u, 60u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &root) == ASTRA_OK);

    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x2bu, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert((controls[2]._private_dynamic_state & ASTRA_CONTROL_FOCUSED) != 0u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x51u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_SCROLL_CHANGED &&
           action.control_id == 200u);
    assert(astra_scroll_get_state(&model, &state) == ASTRA_OK);
    assert(state.offset_y == 16u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4eu, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(astra_scroll_get_state(&model, &state) == ASTRA_OK);
    assert(state.offset_y == 76u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4du, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(astra_scroll_get_state(&model, &state) == ASTRA_OK);
    assert(state.offset_y == state.maximum_y);
}

static void test_nested_scroll_chaining(void)
{
    AstraScrollModel outer_model = ASTRA_SCROLL_MODEL_INIT;
    AstraScrollModel inner_model = ASTRA_SCROLL_MODEL_INIT;
    AstraScrollModelInfo model_info = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraScrollViewInfo view_info = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraContainerInfo container_info = ASTRA_CONTAINER_INFO_INIT;
    AstraButtonInfo button_info = ASTRA_BUTTON_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout root = ASTRA_FLEX_LAYOUT_INIT;
    AstraControl controls[5] = {
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT,
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
    AstraWindowEvent event;

    model_info.line_width = 8u;
    model_info.line_height = 10u;
    assert(astra_scroll_init(&outer_model, &model_info) == ASTRA_OK);
    assert(astra_scroll_init(&inner_model, &model_info) == ASTRA_OK);
    view_info.id = 300u;
    view_info.model = &outer_model;
    view_info.preferred_width = 120u;
    view_info.preferred_height = 60u;
    assert(astra_interface_scroll_view_init(&controls[0], &view_info) ==
           ASTRA_OK);
    container_info.id = 301u;
    container_info.layout.direction = ASTRA_FLEX_COLUMN;
    container_info.layout.padding_bottom = 200u;
    assert(astra_interface_container_init(&controls[1], &container_info) ==
           ASTRA_OK);
    item.parent_id = 300u;
    assert(astra_interface_control_set_flex(&controls[1], &item) == ASTRA_OK);
    view_info.id = 302u;
    view_info.model = &inner_model;
    view_info.preferred_width = 80u;
    view_info.preferred_height = 40u;
    assert(astra_interface_scroll_view_init(&controls[2], &view_info) ==
           ASTRA_OK);
    item.parent_id = 301u;
    assert(astra_interface_control_set_flex(&controls[2], &item) == ASTRA_OK);
    container_info.id = 303u;
    container_info.layout.padding_bottom = 100u;
    assert(astra_interface_container_init(&controls[3], &container_info) ==
           ASTRA_OK);
    item.parent_id = 302u;
    assert(astra_interface_control_set_flex(&controls[3], &item) == ASTRA_OK);
    button_info.id = 304u;
    button_info.text = "Nested";
    button_info.text_length = 6u;
    assert(astra_interface_button_init(&controls[4], &button_info) ==
           ASTRA_OK);
    item.parent_id = 303u;
    assert(astra_interface_control_set_flex(&controls[4], &item) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 5u, 120u, 60u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &root) == ASTRA_OK);

    event = wheel_event(10, 10, 0, -1, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.control_id == 302u);
    assert(astra_scroll_get_state(&inner_model, &state) == ASTRA_OK);
    assert(state.offset_y == 10u);
    assert(astra_scroll_set_offset(&inner_model, 0u, state.maximum_y) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &root) == ASTRA_OK);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.control_id == 300u);
    assert(astra_scroll_get_state(&outer_model, &state) == ASTRA_OK);
    assert(state.offset_y == 10u);
}

static void test_scroll_view_rejects_non_single_child(void)
{
    AstraScrollModel model = ASTRA_SCROLL_MODEL_INIT;
    AstraScrollModelInfo model_info = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraScrollViewInfo view_info = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraLabelInfo label_info = ASTRA_LABEL_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraControl controls[3] = {
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;

    model_info.line_width = 1u;
    model_info.line_height = 1u;
    assert(astra_scroll_init(&model, &model_info) == ASTRA_OK);
    view_info.id = 400u;
    view_info.model = &model;
    view_info.preferred_width = 10u;
    view_info.preferred_height = 10u;
    assert(astra_interface_scroll_view_init(&controls[0], &view_info) ==
           ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 1u, 10u, 10u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    label_info.text = "x";
    label_info.text_length = 1u;
    label_info.id = 401u;
    assert(astra_interface_label_init(&controls[1], &label_info) == ASTRA_OK);
    item.parent_id = 400u;
    assert(astra_interface_control_set_flex(&controls[1], &item) == ASTRA_OK);
    label_info.id = 402u;
    assert(astra_interface_label_init(&controls[2], &label_info) == ASTRA_OK);
    assert(astra_interface_control_set_flex(&controls[2], &item) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 3u, 10u, 10u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
}

static void test_splitter_composition(void)
{
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraSplitterInfo splitter = ASTRA_SPLITTER_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout root = ASTRA_FLEX_LAYOUT_INIT;
    AstraControl controls[3] = {
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent event;
    AstraWindow window = ASTRA_WINDOW_INIT;
    uint32_t applied_shape = UINT32_MAX;
    uint32_t pointer_shape = UINT32_MAX;
    int64_t value = -1;

    label.id = 500u;
    label.text = "L";
    label.text_length = 1u;
    assert(astra_interface_label_init(&controls[0], &label) == ASTRA_OK);
    item.basis = 80u;
    item.minimum_width = 40u;
    item.maximum_width = 140u;
    assert(astra_interface_control_set_flex(&controls[0], &item) == ASTRA_OK);

    splitter.id = 501u;
    splitter.orientation = ASTRA_ORIENTATION_VERTICAL;
    assert(astra_interface_splitter_init(&controls[1], &splitter) == ASTRA_OK);
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.minimum_width = 8u;
    item.maximum_width = 8u;
    assert(astra_interface_control_set_flex(&controls[1], &item) == ASTRA_OK);

    label.id = 502u;
    label.text = "R";
    assert(astra_interface_label_init(&controls[2], &label) == ASTRA_OK);
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.basis = 112u;
    item.minimum_width = 60u;
    item.maximum_width = 180u;
    assert(astra_interface_control_set_flex(&controls[2], &item) == ASTRA_OK);

    assert(astra_interface_ui_init(&context, controls, 3u, 200u, 80u) ==
           ASTRA_OK);
    root.direction = ASTRA_FLEX_ROW;
    root.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    assert(astra_interface_ui_layout(&context, &root) == ASTRA_OK);
    assert(controls[0]._private_frame.width == 80u);
    assert(controls[1]._private_frame.x == 80);
    assert(controls[1]._private_frame.width == 8u);
    assert(controls[2]._private_frame.x == 88);
    assert(controls[2]._private_frame.width == 112u);
    assert(astra_interface_control_get_value(&controls[1], &value, NULL) ==
           ASTRA_OK && value == 80);

    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u, 84, 20);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(astra_interface_ui_pointer_shape(&context, &pointer_shape) ==
           ASTRA_OK);
    assert(pointer_shape == ASTRA_POINTER_SHAPE_RESIZE_HORIZONTAL);
    assert(astra_interface_ui_update_pointer(
               &context, &window, ASTRA_POINTER_SHAPE_AUTOMATIC,
               &applied_shape) == ASTRA_OK);
    assert(pointer_shape_calls == 1u &&
           last_pointer_shape == ASTRA_POINTER_SHAPE_RESIZE_HORIZONTAL);
    assert(astra_interface_ui_update_pointer(
               &context, &window, ASTRA_POINTER_SHAPE_AUTOMATIC,
               &applied_shape) == ASTRA_OK);
    assert(pointer_shape_calls == 1u);
    assert(astra_interface_ui_update_pointer(
               &context, &window, ASTRA_POINTER_SHAPE_WAIT,
               &applied_shape) == ASTRA_OK);
    assert(pointer_shape_calls == 2u &&
           last_pointer_shape == ASTRA_POINTER_SHAPE_WAIT);
    assert(astra_interface_ui_update_pointer(
               &context, &window, ASTRA_POINTER_SHAPE_CUSTOM,
               &applied_shape) == ASTRA_OK);
    assert(pointer_shape_calls == 3u &&
           last_pointer_shape == ASTRA_POINTER_SHAPE_CUSTOM);
    assert(astra_interface_ui_update_pointer(
               &context, &window, ASTRA_POINTER_SHAPE_COUNT,
               &applied_shape) == ASTRA_ERROR_INVALID_ARGUMENT);
    assert(pointer_shape_calls == 3u);

    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN, 84, 20);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(astra_interface_ui_pointer_shape(&context, &pointer_shape) ==
           ASTRA_OK);
    assert(pointer_shape == ASTRA_POINTER_SHAPE_RESIZE_HORIZONTAL);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u, 104, 20);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.control_id == 501u && action.value == 100);
    assert(controls[0]._private_frame.width == 100u);
    assert(controls[1]._private_frame.x == 100);
    assert(controls[2]._private_frame.width == 92u);

    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u, -100, 20);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.value == 40);
    assert(controls[0]._private_frame.width == 40u);
    assert(controls[2]._private_frame.width == 152u);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u, 1000, 20);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.value == 132);
    assert(controls[0]._private_frame.width == 132u);
    assert(controls[2]._private_frame.width == 60u);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON, 0u, 1000, 20);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(astra_interface_ui_pointer_shape(&context, &pointer_shape) ==
           ASTRA_OK);
    assert(pointer_shape == ASTRA_POINTER_SHAPE_DEFAULT);

    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x50u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.value == 128);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x50u,
                      ASTRA_INPUT_MOD_SHIFT);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.value == 127);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4au, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.value == 40);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4du, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.value == 132);

    assert(astra_interface_control_set_value(&context, &controls[1], 90) ==
           ASTRA_OK);
    assert(astra_interface_control_get_value(&controls[1], &value, NULL) ==
           ASTRA_OK && value == 90);
    assert(controls[0]._private_frame.width == 90u);
    assert(controls[2]._private_frame.width == 102u);

    fill_calls = 0u;
    {
        AstraSurfaceView surface = {
            render_pixels, sizeof(render_pixels), 200u * sizeof(uint16_t),
            200u, 80u, ASTRA_SURFACE_VIEW_RGB565, 0u, 0u, 200u, 80u};
        assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    }
    assert(fill_calls == 1u);
    assert(fill_x[0] == 93 && fill_y[0] == 0 &&
           fill_width[0] == 2u && fill_height[0] == 80u);
}

static void test_splitter_rejects_invalid_composition(void)
{
    AstraSplitterInfo splitter = ASTRA_SPLITTER_INFO_INIT;
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraFlexLayout root = ASTRA_FLEX_LAYOUT_INIT;
    AstraControl controls[2] = {ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;

    splitter.id = 510u;
    splitter.orientation = ASTRA_ORIENTATION_VERTICAL;
    assert(astra_interface_splitter_init(&controls[0], &splitter) == ASTRA_OK);
    label.id = 511u;
    label.text = "pane";
    label.text_length = 4u;
    assert(astra_interface_label_init(&controls[1], &label) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 2u, 100u, 50u) ==
           ASTRA_OK);
    root.direction = ASTRA_FLEX_ROW;
    assert(astra_interface_ui_layout(&context, &root) ==
           ASTRA_ERROR_INVALID_ARGUMENT);

    controls[0] = (AstraControl)ASTRA_CONTROL_INIT;
    controls[1] = (AstraControl)ASTRA_CONTROL_INIT;
    label.id = 512u;
    assert(astra_interface_label_init(&controls[0], &label) == ASTRA_OK);
    splitter.id = 513u;
    assert(astra_interface_splitter_init(&controls[1], &splitter) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 2u, 100u, 50u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &root) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
}

static void test_horizontal_splitter(void)
{
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraSplitterInfo splitter = ASTRA_SPLITTER_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout root = ASTRA_FLEX_LAYOUT_INIT;
    AstraControl controls[3] = {
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent event;
    uint32_t pointer_shape = UINT32_MAX;

    label.id = 520u;
    label.text = "T";
    label.text_length = 1u;
    assert(astra_interface_label_init(&controls[0], &label) == ASTRA_OK);
    item.basis = 40u;
    item.minimum_height = 20u;
    item.maximum_height = 70u;
    assert(astra_interface_control_set_flex(&controls[0], &item) == ASTRA_OK);
    splitter.id = 521u;
    splitter.orientation = ASTRA_ORIENTATION_HORIZONTAL;
    assert(astra_interface_splitter_init(&controls[1], &splitter) == ASTRA_OK);
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.minimum_height = 8u;
    item.maximum_height = 8u;
    assert(astra_interface_control_set_flex(&controls[1], &item) == ASTRA_OK);
    label.id = 522u;
    label.text = "B";
    assert(astra_interface_label_init(&controls[2], &label) == ASTRA_OK);
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.basis = 52u;
    item.minimum_height = 30u;
    item.maximum_height = 80u;
    assert(astra_interface_control_set_flex(&controls[2], &item) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 3u, 120u, 100u) ==
           ASTRA_OK);
    root.direction = ASTRA_FLEX_COLUMN;
    root.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    assert(astra_interface_ui_layout(&context, &root) == ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u, 40, 44);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(astra_interface_ui_pointer_shape(&context, &pointer_shape) ==
           ASTRA_OK);
    assert(pointer_shape == ASTRA_POINTER_SHAPE_RESIZE_VERTICAL);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN, 40, 44);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u, 40, 54);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.value == 50);
    assert(controls[0]._private_frame.height == 50u &&
           controls[2]._private_frame.height == 42u);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON, 0u, 40, 54);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x52u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.value == 46);

    splitter.orientation = ASTRA_ORIENTATION_VERTICAL;
    assert(astra_interface_splitter_init(&controls[1], &splitter) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 3u, 120u, 100u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &root) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
}

static void test_disclosure_control(void)
{
    AstraDisclosureInfo disclosure = ASTRA_DISCLOSURE_INFO_INIT;
    AstraContainerInfo body = ASTRA_CONTAINER_INFO_INIT;
    AstraToggleInfo option = ASTRA_TOGGLE_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout root = ASTRA_FLEX_LAYOUT_INIT;
    AstraControl controls[3] = {
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraSurfaceView surface = {
        render_pixels, sizeof(render_pixels), 240u * sizeof(uint16_t),
        240u, 100u, ASTRA_SURFACE_VIEW_RGB565, 0u, 0u, 240u, 100u};
    AstraWindowEvent event;

    disclosure.id = 530u;
    disclosure.text = "Advanced";
    disclosure.text_length = 8u;
    disclosure.target_id = 531u;
    assert(astra_interface_disclosure_init(&controls[0], &disclosure) ==
           ASTRA_OK);
    body.id = 531u;
    body.state = ASTRA_CONTROL_COLLAPSED;
    body.layout.direction = ASTRA_FLEX_COLUMN;
    assert(astra_interface_container_init(&controls[1], &body) == ASTRA_OK);
    option.id = 532u;
    option.text = "Reject late generations";
    option.text_length = 23u;
    assert(astra_interface_checkbox_init(&controls[2], &option) == ASTRA_OK);
    item.parent_id = 531u;
    assert(astra_interface_control_set_flex(&controls[2], &item) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 3u, 240u, 100u) ==
           ASTRA_OK);
    root.direction = ASTRA_FLEX_COLUMN;
    root.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    assert(astra_interface_ui_layout(&context, &root) == ASTRA_OK);
    assert(controls[0]._private_laid_out != 0u);
    assert(controls[1]._private_laid_out == 0u);
    assert(controls[2]._private_laid_out == 0u);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(text_calls == 1u && fill_calls == 6u);
    assert(fill_x[3] > fill_x[2] && fill_x[3] > fill_x[4] &&
           fill_x[1] == fill_x[5]);

    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN, 20, 10);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON, 0u, 20, 10);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.control_id == 530u && action.value == 1);
    assert((controls[1]._private_state & ASTRA_CONTROL_COLLAPSED) == 0u);
    assert(controls[1]._private_laid_out != 0u);
    assert(controls[2]._private_laid_out != 0u);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(text_calls == 2u);
    assert(fill_y[3] > fill_y[2] && fill_y[3] > fill_y[4] &&
           fill_y[1] == fill_y[5]);

    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x50u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.control_id == 530u && action.value == 0);
    assert((controls[1]._private_state & ASTRA_CONTROL_COLLAPSED) != 0u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4fu, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_VALUE_CHANGED && action.value == 1);

    disclosure.target_id = 999u;
    assert(astra_interface_disclosure_init(&controls[0], &disclosure) ==
           ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 3u, 240u, 100u) ==
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
    info.button = "Continue with this operation";
    info.button_length = 28u;
    assert(astra_interface_test_valid(&info));
    info.button = malformed_utf8;
    info.button_length = sizeof(malformed_utf8);
    assert(!astra_interface_test_valid(&info));
    info.button = "OK";
    info.button_length = 2u;
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
    test_fault_label();
    test_button_preview_states();
    test_flex_growth();
    test_control_rejections();
    test_toggle_controls();
    test_checkbox_mark_geometry();
    test_switch_off_geometry();
    test_control_text_update();
    test_slider_control();
    test_dial_control();
    test_progress_control();
    test_segmented_control();
    test_tab_control_and_collapsed_pages();
    test_stepper_control();
    test_animation_ownership();
    test_text_field();
    test_text_field_rejections();
    test_scroll_view_layout_and_wheel();
    test_scroll_view_keyboard_listener();
    test_nested_scroll_chaining();
    test_scroll_view_rejects_non_single_child();
    test_splitter_composition();
    test_splitter_rejects_invalid_composition();
    test_horizontal_splitter();
    test_disclosure_control();
    astra_interface_test_layout();
    return 0;
}
