#include <astra/interface.h>
#include <astra/interface_library.h>
#include <astra/fonts.h>
#include <astra/control.h>
#include <astra/display.h>
#include <astra/draw_list.h>
#include <astra/input_modifiers.h>
#include <astra/surface.h>
#include <astra/theme.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/control_internal.h"

int astra_interface_test_valid(const AstraAlertInfo *info);
void astra_interface_test_paint(AstraSurfaceView *surface,
                                const AstraAlertInfo *info);
void astra_interface_test_layout(void);

_Static_assert(sizeof(AstraControl) == (sizeof(void *) == 4u ? 292u : 320u),
               "Display model slot must be included in Interface ABI 9");
_Static_assert(sizeof(AstraUIContext) ==
                   (sizeof(void *) == 4u ? 168u : 176u),
               "AstraUIContext ABI 8 layout changed; bump the ABI major");

static uint16_t first_text_color;
static char first_mono_text[129];
static uint32_t first_mono_length;
static uint16_t last_text_color;
static uint16_t fill_colors[32];
static int32_t fill_x[32];
static int32_t fill_y[32];
static uint32_t fill_width[32];
static uint32_t fill_height[32];
static _Alignas(4) uint16_t render_pixels[320u * 200u];
static uint32_t icon_blit_calls;

int astra_surface_draw_blit(AstraSurfaceView *view,
    const AstraSurface *source, const AstraRectI32 *source_rect,
    const AstraRectI32 *destination_rect, const AstraBlitOptions *options)
{
    assert(view != NULL && source != NULL && source_rect != NULL &&
           destination_rect != NULL && options != NULL);
    ++icon_blit_calls;
    return 1;
}

static void test_toolbar_composition(void)
{
    static const AstraToolbarAction actions[] = {
        {"Open", 4u, 0u}, {"Save", 4u, ASTRA_CONTROL_DISABLED}
    };
    AstraToolbarInfo info = ASTRA_TOOLBAR_INFO_INIT;
    const uint32_t icons[2] = {
        ASTRA_TOOLBAR_ICON_OPEN, ASTRA_TOOLBAR_ICON_TRASH
    };
    AstraControl controls[3] = {
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT
    };
    info.first_id = 41u;
    info.actions = actions;
    info.action_count = 2u;
    assert(astra_interface_toolbar_init(controls, &info) == ASTRA_OK);
    assert(controls[0]._private_control_kind == ASTRA_CONTROL_CONTAINER);
    assert(controls[0]._private_style == ASTRA_CONTAINER_STYLE_TOOLBAR);
    assert(controls[1]._private_control_kind == ASTRA_CONTROL_BUTTON);
    assert(controls[1]._private_style == ASTRA_BUTTON_TOOLBAR);
    assert(controls[1]._private_parent_id == 41u);
    assert(controls[2]._private_state == ASTRA_CONTROL_DISABLED);
    assert(astra_interface_toolbar_set_icons(controls, icons, 2u) == ASTRA_OK);
    assert(ASTRA_BUTTON_ICON(controls[1]._private_style) ==
           ASTRA_TOOLBAR_ICON_OPEN);
    {
        const uint32_t invalid[2] = {
            ASTRA_TOOLBAR_ICON_BACK, ASTRA_TOOLBAR_ICON_MORE + 1u
        };
        assert(astra_interface_toolbar_set_icons(controls, invalid, 2u) ==
               ASTRA_ERROR_INVALID_ARGUMENT);
        assert(ASTRA_BUTTON_ICON(controls[1]._private_style) ==
               ASTRA_TOOLBAR_ICON_OPEN);
    }
    {
        AstraUIContext ui = ASTRA_UI_CONTEXT_INIT;
        AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
        assert(astra_interface_ui_init(&ui, controls, 3u, 240u, 48u) == ASTRA_OK);
        assert(astra_interface_ui_layout(&ui, &layout) == ASTRA_OK);
        assert(controls[0]._private_frame.height == 36u);
        assert(controls[1]._private_frame.height == 22u);
        assert(astra_interface_ui_focus_control(&ui, 42u) == ASTRA_OK);
        assert(ui._private_focus == 1u);
        assert(astra_interface_ui_focus_control(&ui, 43u) ==
               ASTRA_ERROR_INVALID_ARGUMENT);
    }
    info.first_id = UINT32_MAX;
    assert(astra_interface_toolbar_init(controls, &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
}

static AstraSurfaceView render_view(uint16_t width, uint16_t height)
{
    AstraDrawListHeader *header = (AstraDrawListHeader *)(void *)render_pixels;

    *header = (AstraDrawListHeader){
        .magic = ASTRA_DRAW_LIST_MAGIC,
        .version = ASTRA_DRAW_LIST_VERSION_1_8,
        .total_bytes = ASTRA_DRAW_LIST_AREA_BYTES,
        .width = width,
        .height = height,
        .command_capacity = ASTRA_DRAW_LIST_COMMAND_MAX,
    };
    return (AstraSurfaceView){render_pixels, sizeof(render_pixels), 0u,
        width, height, ASTRA_SURFACE_VIEW_DRAW_LIST, 0u, 0u, width, height};
}

static uint32_t fill_calls;
static uint32_t line_calls;
static int32_t line_x0[16], line_y0[16], line_x1[16], line_y1[16];

static void test_toolbar_render(void)
{
    static const AstraToolbarAction actions[] = {
        {"Back", 4u, 0u}, {"Forward", 7u, 0u}
    };
    static const uint32_t icons[] = {
        ASTRA_TOOLBAR_ICON_BACK, ASTRA_TOOLBAR_ICON_FORWARD
    };
    AstraToolbarInfo info = ASTRA_TOOLBAR_INFO_INIT;
    AstraControl controls[3] = {
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT
    };
    AstraUIContext ui = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraSurfaceView view = render_view(240u, 48u);
    info.first_id = 1u;
    info.actions = actions;
    info.action_count = 2u;
    assert(astra_interface_toolbar_init(controls, &info) == ASTRA_OK);
    assert(astra_interface_toolbar_set_icons(controls, icons, 2u) == ASTRA_OK);
    assert(astra_interface_ui_init(&ui, controls, 3u, 240u, 48u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&ui, &layout) == ASTRA_OK);
    fill_calls = line_calls = 0u;
    assert(astra_interface_ui_render(&ui, &view) == ASTRA_OK);
    assert(fill_calls >= 2u && line_calls >= 6u);
    ((AstraDrawListHeader *)view.pixels)->command_count = 100u;
    assert(astra_interface_ui_render(&ui, &view) == ASTRA_OK);
}
static uint32_t text_calls;
static uint16_t text_clip_top;
static uint16_t text_clip_bottom;
static uint32_t mono_text_calls;
static uint32_t mono_styles[32];
static uint16_t mono_colors[32];
static uint32_t copy_calls;
static uint32_t copy_source_x;
static uint32_t copy_source_y;
static uint32_t copy_destination_x;
static uint32_t copy_destination_y;
static uint32_t copy_width;
static uint32_t copy_height;
static uint32_t pointer_shape_calls;
static uint32_t last_pointer_shape;
static uint8_t alert_draw_storage[ASTRA_DRAW_LIST_AREA_BYTES];
static uint32_t alert_create_calls;
static uint32_t alert_name_calls;
static uint32_t alert_activate_calls;
static uint32_t alert_close_calls;
static uint32_t alert_font_events;
static uint32_t alert_font_refreshes;

AstraResult astra_fonts_use_system(AstraHandle gui, const AstraWindowEvent *change)
{
    assert(gui == 1u && (change == NULL || change->type == ASTRA_WINDOW_EVENT_FONT_CHANGED));
    ++alert_font_refreshes;
    return ASTRA_OK;
}

uint32_t astra_log_failure(const char *operation, uint32_t status)
{
    (void)operation; (void)status;
    return ASTRA_SYSCALL_OK;
}
static AstraResult alert_name_result = ASTRA_OK;

int astra_draw_list_view_init(AstraSurfaceView *surface, void *storage,
                              uint32_t byte_size, uint16_t width,
                              uint16_t height)
{
    AstraDrawListHeader *header = storage;

    memset(storage, 0, byte_size);
    header->magic = ASTRA_DRAW_LIST_MAGIC;
    header->version = ASTRA_DRAW_LIST_VERSION_1_8;
    header->total_bytes = ASTRA_DRAW_LIST_AREA_BYTES;
    header->width = width;
    header->height = height;
    header->command_capacity = ASTRA_DRAW_LIST_COMMAND_MAX;
    *surface = (AstraSurfaceView){
        storage, byte_size, 0u, width, height,
        ASTRA_SURFACE_VIEW_DRAW_LIST, 0u, 0u, width, height};
    return 1;
}

uint32_t astra_shared_draw_list_create(AstraSharedSurface *surface,
                                       uint16_t width, uint16_t height)
{
    surface->area = 2u;
    surface->mapping = alert_draw_storage;
    assert(astra_draw_list_view_init(&surface->view, alert_draw_storage,
                                     sizeof(alert_draw_storage), width,
                                     height));
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_shared_surface_close(AstraSharedSurface *surface)
{
    assert(surface->area == 2u);
    return ASTRA_SYSCALL_OK;
}

AstraResult astra_window_create(uint32_t gui, uint32_t area,
                                const AstraWindowCreateInfo *info,
                                AstraWindow *window)
{
    assert(gui == 1u && area == 2u && window != NULL &&
           (info->flags & ASTRA_WINDOW_ACTIVE) == 0u &&
           info->type == ASTRA_WINDOW_DIALOG &&
           info->gadgets == ASTRA_WINDOW_GADGET_AUTO &&
           info->x == (ASTRA_DISPLAY_WIDTH - 420u) / 2u &&
           info->y == (ASTRA_DISPLAY_HEIGHT - 150u) / 2u);
    ++alert_create_calls;
    return ASTRA_OK;
}

AstraResult astra_window_set_application_name(AstraWindow *window,
                                               const char *name,
                                               uint16_t length)
{
    assert(window != NULL && length == 5u && memcmp(name, "Astra", 5u) == 0);
    ++alert_name_calls;
    return alert_name_result;
}

AstraResult astra_window_activate(AstraWindow *window)
{
    assert(window != NULL);
    ++alert_activate_calls;
    return ASTRA_OK;
}

AstraResult astra_window_event_wait(AstraWindow *window,
                                    AstraWindowEvent *event,
                                    AstraMonotonicDeadline deadline)
{
    assert(window != NULL && event != NULL &&
           deadline == ASTRA_DEADLINE_INFINITE);
    if (alert_font_events != 0u) {
        --alert_font_events;
        *event = (AstraWindowEvent){.size = sizeof(*event),
            .version = ASTRA_WINDOW_EVENT_VERSION, .type = ASTRA_WINDOW_EVENT_FONT_CHANGED};
        event->data.font_changed.generation = 7u;
        event->data.font_changed.window = 7u;
        return ASTRA_OK;
    }
    event->type = ASTRA_WINDOW_EVENT_CLOSE_REQUEST;
    return ASTRA_OK;
}

AstraResult astra_window_present_region(AstraWindow *window,
                                         const AstraWindowFrame *region)
{
    (void)window;
    (void)region;
    return ASTRA_OK;
}

AstraResult astra_window_close(AstraWindow *window)
{
    assert(window != NULL);
    ++alert_close_calls;
    return ASTRA_OK;
}

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
           header->version == ASTRA_DRAW_LIST_VERSION_1_8 &&
           header->total_bytes == ASTRA_DRAW_LIST_AREA_BYTES &&
           header->command_capacity == ASTRA_DRAW_LIST_COMMAND_MAX &&
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
    (void)color;
    if (line_calls < 16u) {
        line_x0[line_calls] = x0;
        line_y0[line_calls] = y0;
        line_x1[line_calls] = x1;
        line_y1[line_calls] = y1;
    }
    ++line_calls;
    return 1;
}

int astra_surface_lines(AstraSurfaceView *surface,
    const AstraSurfaceLine *lines, uint32_t count, uint16_t color)
{
    for (uint32_t at = 0u; at < count; ++at)
        if (!astra_surface_line(surface, lines[at].x0, lines[at].y0,
                                lines[at].x1, lines[at].y1, color))
            return 0;
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

static uint32_t font_advance = 1u;
static uint32_t font_line_height;
static const char *context_font_id = "";
AstraResult astra_fonts_context_id(char id[ASTRA_FONT_ID_MAX])
{
    strcpy(id, context_font_id);
    return ASTRA_OK;
}
static AstraResult preview_result = ASTRA_OK;
static uint32_t preview_draws;
static uint16_t preview_height;

AstraResult astra_fonts_text_measure(AstraHandle gui,
    const char *id, uint32_t id_length, uint16_t height,
    const char *text, uint32_t length, uint32_t maximum_width,
    AstraFontTextMetrics *metrics)
{
    (void)text;
    assert(gui == 1u && id_length == 12u &&
           memcmp(id, "user:preview", 12u) == 0);
    *metrics = (AstraFontTextMetrics)ASTRA_FONT_TEXT_METRICS_INIT;
    if (preview_result != ASTRA_OK) return preview_result;
    metrics->fitted_bytes = maximum_width / height;
    if (metrics->fitted_bytes > length) metrics->fitted_bytes = length;
    metrics->width = metrics->fitted_bytes * height;
    metrics->height = height + 3u;
    return ASTRA_OK;
}

int astra_surface_font_text(AstraSurfaceView *surface,
    const char *id, uint32_t id_length, int32_t x, int32_t y,
    const char *text, uint32_t length, uint16_t height, uint16_t color)
{
    (void)x; (void)y; (void)text; (void)color;
    assert(surface->kind == ASTRA_SURFACE_VIEW_DRAW_LIST &&
        id_length == 12u && memcmp(id, "user:preview", 12u) == 0 && length != 0u);
    ++preview_draws;
    preview_height = height;
    return 1;
}

uint32_t astra_surface_ui_text_width(const char *text, uint32_t length,
                                     uint16_t height)
{
    (void)text;
    (void)height;
    return length * font_advance;
}

uint32_t astra_surface_ui_text_width_styled(const char *text,
    uint32_t length, uint16_t height, uint32_t style_flags)
{
    return astra_surface_ui_text_width(text, length, height) +
        ((style_flags & ASTRA_TEXT_STYLE_BOLD) != 0u ? length : 0u);
}

int astra_surface_ui_text_requirements(uint32_t length,
    uint32_t *commands, uint32_t *payload_bytes)
{
    *commands = length != 0u;
    *payload_bytes = length != 0u ?
        length + (uint32_t)strlen(context_font_id) : 0u;
    return 1;
}

uint32_t astra_surface_ui_text_height(uint16_t pixel_height)
{
    return font_line_height != 0u ? font_line_height : pixel_height;
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

int astra_surface_ui_text_styled(AstraSurfaceView *surface, int32_t x,
    int32_t y, const char *text, uint32_t length, uint16_t height,
    uint16_t color, uint32_t style_flags)
{
    (void)style_flags;
    astra_surface_ui_text(surface, x, y, text, length, height, color);
    return 1;
}

int astra_surface_mono_text_styled(AstraSurfaceView *surface, int32_t x,
                                   int32_t y, const char *utf8,
                                   uint32_t length, uint16_t pixel_height,
                                   uint16_t cell_width, uint16_t color,
                                   uint32_t style_flags)
{
    (void)surface;
    if (mono_text_calls == 0u) {
        first_mono_length = length < sizeof(first_mono_text) - 1u ?
            length : sizeof(first_mono_text) - 1u;
        memcpy(first_mono_text, utf8, first_mono_length);
        first_mono_text[first_mono_length] = '\0';
    }
    if (mono_text_calls < sizeof(fill_x) / sizeof(fill_x[0]))
        fill_x[mono_text_calls] = x;
    if (mono_text_calls < sizeof(mono_styles) / sizeof(mono_styles[0])) {
        mono_styles[mono_text_calls] = style_flags;
        mono_colors[mono_text_calls] = color;
    }
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

static AstraWindowEvent resize_event(uint16_t width, uint16_t height)
{
    AstraWindowEvent event = {0};

    event.size = sizeof(event);
    event.version = ASTRA_WINDOW_EVENT_VERSION;
    event.type = ASTRA_WINDOW_EVENT_RESIZE;
    event.data.resize.width = width;
    event.data.resize.height = height;
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
    first_mono_length = 0u;
    first_mono_text[0] = '\0';
    memset(mono_styles, 0, sizeof(mono_styles));
    memset(mono_colors, 0, sizeof(mono_colors));
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
    AstraSurfaceView surface = render_view(320u, 200u);
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
    surface.kind = ASTRA_SURFACE_VIEW_RGB565;
    assert(astra_interface_ui_render(&context, &surface) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    surface.kind = ASTRA_SURFACE_VIEW_DRAW_LIST;
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
    AstraSurfaceView surface = render_view(100u, 40u);

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

static void test_label_installed_font(void)
{
    AstraLabelInfo info = ASTRA_LABEL_INFO_INIT;
    AstraControl label = ASTRA_CONTROL_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraLabelFont font = ASTRA_LABEL_FONT_INIT;
    AstraSurfaceView surface = render_view(200u, 80u);
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    font_advance = 1u;
    info.id = 1u;
    info.text = "ABC";
    info.text_length = 3u;
    font.gui = 1u;
    font.id = "user:preview";
    font.id_length = 12u;
    font.pixel_height = 20u;
    info.font = font;
    assert(astra_interface_label_init(&label, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &label, 1u, 200u, 80u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(label._private_measured_width == 60u && label._private_measured_height == 23u);
    reset_render_calls();
    AstraDrawListHeader *header = (void *)surface.pixels;
    header->payload_bytes = ASTRA_DRAW_LIST_PAYLOAD_BYTES - 14u;
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_ERROR_NO_RESOURCES);
    assert(preview_draws == 0u);
    header->payload_bytes = 0u;
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(preview_draws == 1u && preview_height == 20u && text_calls == 0u);
    font.pixel_height = 13u;
    assert(astra_interface_label_set_font(&context, &label, &font) == ASTRA_OK);
    assert(label._private_measured_width == 39u && label._private_measured_height == 16u);
    uint32_t focus = context._private_focus;
    preview_result = ASTRA_ERROR_UNSUPPORTED;
    font.pixel_height = 99u;
    assert(astra_interface_label_set_font(&context, &label, &font) == ASTRA_ERROR_UNSUPPORTED);
    assert(label._private_label_font.pixel_height == 13u && label._private_measured_width == 39u &&
           context._private_focus == focus);
    preview_result = ASTRA_OK;
    assert(astra_interface_control_set_text(&context, &label, "ABCD", 4u) == ASTRA_OK);
    assert(label._private_measured_width == 52u);
    font = (AstraLabelFont)ASTRA_LABEL_FONT_INIT;
    assert(astra_interface_label_set_font(&context, &label, &font) == ASTRA_OK);
    assert(label._private_measured_width == 4u &&
        label._private_measured_height == theme.body_font_height);
    font.gui = 1u;
    assert(astra_interface_label_set_font(&context, &label, &font) == ASTRA_ERROR_INVALID_ARGUMENT);
    assert(label._private_label_font.gui == ASTRA_INVALID_HANDLE && context._private_focus == focus);
    context_font_id = "user:preview";
    header->payload_bytes = ASTRA_DRAW_LIST_PAYLOAD_BYTES - 15u;
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_ERROR_NO_RESOURCES);
    header->payload_bytes = 0u;
    context_font_id = "";
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
    AstraSurfaceView surface = render_view(320u, 200u);

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
    event = resize_event(120u, 80u);
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
    AstraSurfaceView surface = render_view(100u, 100u);

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
    AstraSurfaceView surface = render_view(100u, 40u);

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
    assert(fill_calls == 1u && line_calls == 2u);
    assert(fill_colors[0] == theme_color(
        ((AstraTheme)ASTRA_THEME_SYSTEM_INIT).accent));
    assert(line_x0[0] == 3 && line_y0[0] == 14 &&
           line_x1[0] == 6 && line_y1[0] == 17);
    assert(line_x0[1] == 6 && line_y0[1] == 17 &&
           line_x1[1] == 12 && line_y1[1] == 10);
}

static void test_switch_off_geometry(void)
{
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraToggleInfo info = ASTRA_TOGGLE_INFO_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraSurfaceView surface = render_view(100u, 40u);

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
    AstraWindowEvent changed = {.size = sizeof(changed),
        .version = ASTRA_WINDOW_EVENT_VERSION, .type = ASTRA_WINDOW_EVENT_FONT_CHANGED};
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    font_advance = 3u;
    assert(astra_interface_ui_handle_event(&context, &changed, &action) == ASTRA_OK);
    assert(controls[0]._private_measured_width == 12u &&
           controls[1]._private_frame.x == 12);
    font_advance = 1u;
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
    AstraSurfaceView surface = render_view(120u, 80u);
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
    AstraSurfaceView surface = render_view(200u, 40u);

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

static void test_display_widgets(void)
{
    uint32_t values[2] = {62u, 71u};
    uint32_t peaks[2] = {78u, 88u};
    uint32_t samples[4] = {30u, 100u, 32u, 34u};
    AstraLevelMeterModel meter = ASTRA_LEVEL_METER_MODEL_INIT;
    AstraSparklineModel spark = ASTRA_SPARKLINE_MODEL_INIT;
    AstraDisplayInfo info = ASTRA_DISPLAY_INFO_INIT;
    AstraControl controls[2] = {ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraControlFrame damage;
    AstraSurfaceView surface = render_view(160u, 100u);

    meter.values = values;
    meter.peaks = peaks;
    meter.channels = 2u;
    meter.maximum = 100u;
    info.id = 1u;
    info.model = &meter;
    assert(astra_interface_level_meter_init(&controls[0], &info) == ASTRA_OK);
    spark.samples = samples;
    spark.sample_count = 4u;
    spark.maximum = 100u;
    spark.warning_threshold = 90u;
    info.id = 2u;
    info.model = &spark;
    assert(astra_interface_sparkline_init(&controls[1], &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 2u, 160u, 100u) ==
           ASTRA_OK);
    layout.main_gap = 8u;
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(fill_calls == 11u);
    assert(fill_height[1] == 49u);
    assert(fill_y[2] == 18);
    assert(fill_y[8] == controls[1]._private_frame.y);
    {
        AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
        assert(fill_colors[8] == astra_surface_rgb565(
            theme.fault.red, theme.fault.green, theme.fault.blue));
        assert(fill_colors[10] == astra_surface_rgb565(
            theme.accent.red, theme.accent.green, theme.accent.blue));
    }
    astra_interface_ui_damage_clear(&context);
    values[0] = 84u;
    peaks[0] = 84u;
    assert(astra_interface_display_refresh(&context, &controls[0]) ==
           ASTRA_OK);
    assert(astra_interface_ui_damage(&context, &damage) == ASTRA_OK);
    assert(damage.width != 0u && damage.height != 0u);
    astra_interface_ui_damage_clear(&context);
    values[0] = 101u;
    assert(astra_interface_display_refresh(&context, &controls[0]) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    values[0] = 84u;
    samples[1] = 101u;
    assert(astra_interface_display_refresh(&context, &controls[1]) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    samples[1] = 100u;
}

static void test_status_bar(void)
{
    AstraControl controls[ASTRA_STATUS_BAR_CONTROL_COUNT] = {
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT
    };
    AstraStatusBarInfo info = ASTRA_STATUS_BAR_INFO_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraSurfaceView surface = render_view(160u, 40u);

    info.first_id = 11u;
    info.left = "Ready";
    info.left_length = 5u;
    info.right = "14 files";
    info.right_length = 8u;
    assert(astra_interface_status_bar_init(controls, &info) == ASTRA_OK);
    assert(controls[0]._private_style == ASTRA_CONTAINER_STYLE_STATUS_BAR);
    assert(controls[1]._private_parent_id == 11u);
    assert(controls[2]._private_parent_id == 11u);
    assert(astra_interface_ui_init(&context, controls,
        ASTRA_STATUS_BAR_CONTROL_COUNT, 160u, 40u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(fill_calls == 2u);
    assert(text_calls == 2u);
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
    AstraSurfaceView surface = render_view(240u, 40u);
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
    assert(text_calls == 3u && fill_calls == 3u);
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

static void test_list_control(void)
{
    static const AstraCollectionItem items[] = {
        {1u, "Astra Sans", 10u, {0u, 0u}},
        {2u, "Astra Mono", 10u, {0u, 0u}},
        {3u, "Iosevka Fixed", 13u, {0u, 0u}}
    };
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraCollectionModel model = ASTRA_COLLECTION_MODEL_INIT;
    AstraListInfo info = ASTRA_LIST_INFO_INIT;
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraControlSize size = {0};
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent event;
    AstraSurfaceView surface = render_view(240u, 160u);
    int64_t value = 0;

    info.id = 73u;
    assert(astra_interface_collection_init(&model, items, 3u, 0u) == ASTRA_OK);
    info.model = &model;
    assert(astra_interface_list_init(&control, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &control, 1u, 240u, 160u) ==
           ASTRA_OK);
    assert(astra_interface_ui_measure(&context, &control, &size) == ASTRA_OK);
    assert(size.height == 3u * theme.control_height);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(astra_interface_control_get_value(&control, &value, NULL) ==
           ASTRA_OK && value == -1);

    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN,
                          control._private_frame.x + 8,
                          control._private_frame.y +
                              theme.control_height + 4);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_NONE);
    event.flags = 0u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.control_id == 73u && action.value == 1);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN,
                          control._private_frame.x + 8,
                          control._private_frame.y +
                              theme.control_height + 4);
    event.data.pointer.click_count = 2u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_NONE);
    event.flags = 0u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_ACTIVATE &&
           action.control_id == 73u && action.value == 1);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x28u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_NONE);
    event.flags = 0u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_ACTIVATE &&
           action.control_id == 73u && action.value == 1);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN,
                          control._private_frame.x + 8,
                          control._private_frame.y +
                              2u * theme.control_height + 4u);
    event.data.pointer.click_count = 2u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_NONE);
    event.flags = 0u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.value == 2);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x51u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_NONE);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x50u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_NONE);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4au, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.value == 0);
    assert(astra_interface_control_set_value(&context, &control, -1) ==
           ASTRA_OK);
    assert(astra_interface_control_set_value(&context, &control, 3) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(text_calls == 3u && fill_calls >= 5u);

    {
        static const AstraCollectionItem reordered[] = {
            {2u, "Astra Mono", 10u, {0u, 0u}},
            {1u, "Astra Sans", 10u, {0u, 0u}},
            {3u, "Iosevka Fixed", 13u, {0u, 0u}}
        };
        static const AstraCollectionItem duplicate[] = {
            {2u, "A", 1u, {0u, 0u}},
            {2u, "B", 1u, {0u, 0u}}
        };
        assert(astra_interface_control_set_value(&context, &control, 1) ==
               ASTRA_OK && model.selected_id == 2u);
        assert(astra_interface_list_set_items(&context, &control, reordered,
                                              3u) == ASTRA_OK);
        assert(astra_interface_control_get_value(&control, &value, NULL) ==
               ASTRA_OK && value == 0 && model.selected_id == 2u);
        assert(astra_interface_list_set_items(&context, &control, duplicate,
                                              2u) ==
               ASTRA_ERROR_INVALID_ARGUMENT);
        assert(model.items == reordered && model.selected_id == 2u);
        assert(astra_interface_list_set_items(&context, &control, NULL, 0u) ==
               ASTRA_OK && model.selected_id == 0u);
    }

    assert(astra_interface_collection_init(&model, NULL, 0u, 0u) == ASTRA_OK);
    assert(astra_interface_list_init(&control, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &control, 1u, 240u, 160u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(text_calls == 0u);
    assert(astra_interface_control_set_value(&context, &control, 0) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_interface_collection_init(&model, NULL, 0u, 1u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
}

static void test_table_control(void)
{
    static const AstraCollectionItem rows[] = {
        {11u, "Doom", 4u, {0u, 0u}},
        {12u, "Quake", 5u, {0u, 0u}},
        {13u, "AstraZ", 6u, {0u, 0u}}
    };
    static const AstraCollectionItem reordered[] = {
        {13u, "AstraZ", 6u, {0u, 0u}},
        {12u, "Quake", 5u, {0u, 0u}},
        {11u, "Doom", 4u, {0u, 0u}}
    };
    static const AstraTableColumn columns[] = {
        {"Name", 4u, 180u, 0u},
        {"Kind", 4u, 100u, 0u}
    };
    static const AstraTableCell cells[] = {
        {"Game", 4u, 0u}, {"Game", 4u, 0u},
        {"Story", 5u, 0u}
    };
    static const AstraTableCell reordered_cells[] = {
        {"Story", 5u, 0u}, {"Game", 4u, 0u},
        {"Game", 4u, 0u}
    };
    AstraCollectionModel collection = ASTRA_COLLECTION_MODEL_INIT;
    AstraTableModel model = ASTRA_TABLE_MODEL_INIT;
    AstraTableInfo info = ASTRA_TABLE_INFO_INIT;
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent event;
    AstraControlSize size = {0};
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraSurfaceView surface = render_view(400u, 200u);

    assert(astra_interface_collection_init(&collection, rows, 3u, 0u) == ASTRA_OK);
    model.collection = &collection;
    model.columns = columns;
    model.column_count = 2u;
    model.cells = cells;
    model.cell_count = 3u;
    info.id = 91u;
    info.model = &model;
    assert(astra_interface_table_init(&control, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &control, 1u, 400u, 200u) == ASTRA_OK);
    assert(astra_interface_ui_measure(&context, &control, &size) == ASTRA_OK);
    assert(size.width == 280u && size.height == 4u * theme.control_height);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
        ASTRA_WINDOW_EVENT_DOWN, control._private_frame.x + 8,
        control._private_frame.y + 4);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    event.flags = 0u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(collection.selected_id == 0u);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
        ASTRA_WINDOW_EVENT_DOWN, control._private_frame.x + 8,
        control._private_frame.y + 2u * theme.control_height + 4u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    event.flags = 0u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.control_id == 91u && action.value == 1);
    assert(collection.selected_id == 12u);
    assert(astra_interface_table_set_rows(&context, &control,
        reordered, 3u, reordered_cells, 3u) == ASTRA_OK);
    assert(collection.selected_id == 12u && control._private_value == 1);
    assert(astra_interface_table_set_rows(&context, &control,
        rows, 3u, cells, 2u) == ASTRA_ERROR_INVALID_ARGUMENT);
    assert(collection.items == reordered && model.cells == reordered_cells);
}

static void test_tree_control(void)
{
    static const AstraCollectionItem items[] = {
        {11u, "Applications", 12u, {0u, 0u}},
        {12u, "Games", 5u, {0u, 0u}},
        {13u, "Doom", 4u, {0u, 0u}},
        {14u, "Documents", 9u, {0u, 0u}}
    };
    AstraTreeNode nodes[] = {
        {0u, 1u, 0u}, {11u, 1u, 0u},
        {12u, 0u, 0u}, {0u, 0u, 0u}
    };
    AstraTreeNode invalid[] = {
        {0u, 1u, 0u}, {11u, 1u, 0u},
        {14u, 0u, 0u}, {0u, 0u, 0u}
    };
    uint32_t visible[4] = {0};
    uint32_t replacement[4] = {0};
    AstraCollectionModel collection = ASTRA_COLLECTION_MODEL_INIT;
    AstraTreeModel tree = ASTRA_TREE_MODEL_INIT;
    AstraTreeInfo info = ASTRA_TREE_INFO_INIT;
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraControlSize size = {0};
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraSurfaceView surface = render_view(400u, 200u);
    AstraWindowEvent event;

    assert(astra_interface_collection_init(&collection, items, 4u, 13u) == ASTRA_OK);
    tree.collection = &collection;
    tree.nodes = nodes;
    tree.node_count = 4u;
    tree.visible_rows = visible;
    tree.visible_capacity = 4u;
    info.id = 92u;
    info.model = &tree;
    assert(astra_interface_tree_init(&control, &info) == ASTRA_OK);
    assert(tree.visible_count == 4u && nodes[2].depth == 2u);
    assert(astra_interface_ui_init(&context, &control, 1u, 400u, 200u) == ASTRA_OK);
    assert(astra_interface_ui_measure(&context, &control, &size) == ASTRA_OK);
    assert(size.height == 4u * theme.control_height);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(astra_interface_tree_set_expanded(&context, &control, 12u, 0u) == ASTRA_OK);
    assert(tree.visible_count == 3u && collection.selected_id == 12u);
    assert(control._private_value == 1);
    assert(astra_interface_tree_set_expanded(&context, &control, 13u, 1u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_interface_tree_set_expanded(&context, &control, 12u, 1u) == ASTRA_OK);
    assert(tree.visible_count == 4u);
    assert(astra_interface_tree_set_nodes(&context, &control, items, 4u,
        invalid, replacement, 4u) == ASTRA_ERROR_INVALID_ARGUMENT);
    assert(tree.nodes == nodes && collection.items == items);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
        ASTRA_WINDOW_EVENT_DOWN, control._private_frame.x + 4,
        control._private_frame.y + theme.control_height / 2);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    event.flags = 0u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_EXPANSION_CHANGED && action.value == 0);
    assert(tree.visible_count == 2u && collection.selected_id == 11u);
}

static void test_pagination_control(void)
{
    static const AstraCollectionItem items[] = {
        {1u, "One", 3u, {0u, 0u}},
        {2u, "Two", 3u, {0u, 0u}},
        {3u, "Three", 5u, {0u, 0u}},
        {4u, "Four", 4u, {0u, 0u}},
        {5u, "Five", 4u, {0u, 0u}}
    };
    AstraCollectionModel collection = ASTRA_COLLECTION_MODEL_INIT;
    AstraPaginationModel model = ASTRA_PAGINATION_MODEL_INIT;
    AstraPaginationInfo info = ASTRA_PAGINATION_INFO_INIT;
    AstraControl controls[4] = {
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT,
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT
    };
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    uint32_t first, end, pages;

    assert(astra_interface_collection_init(&collection, items, 5u, 3u) == ASTRA_OK);
    model.collection = &collection;
    model.page_size = 2u;
    info.first_id = 300u;
    info.model = &model;
    assert(astra_interface_pagination_init(controls, &info) == ASTRA_OK);
    assert(astra_interface_pagination_range(&model, &first, &end, &pages) == ASTRA_OK);
    assert(first == 0u && end == 2u && pages == 3u);
    assert(controls[1]._private_id == 301u &&
           controls[2]._private_id == 302u &&
           controls[3]._private_id == 303u);
    assert(astra_interface_ui_init(&context, controls, 4u, 400u, 100u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(astra_interface_pagination_set_page(&context, controls, &model, 2u) == ASTRA_OK);
    assert(astra_interface_pagination_range(&model, &first, &end, &pages) == ASTRA_OK);
    assert(first == 4u && end == 5u && pages == 3u);
    assert(model.label_length == 5u &&
           memcmp(model.label[model.active_label], "3 / 3", 5u) == 0);
    assert((controls[3]._private_state & ASTRA_CONTROL_DISABLED) != 0u);
    assert(collection.selected_id == 3u);
    assert(astra_interface_pagination_set_page(&context, controls, &model, 3u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(model.page_index == 2u);
    collection.item_count = 0u;
    collection.selected_id = 0u;
    assert(astra_interface_pagination_set_page(&context, controls, &model, 0u) == ASTRA_OK);
    assert(astra_interface_pagination_range(&model, &first, &end, &pages) == ASTRA_OK);
    assert(first == 0u && end == 0u && pages == 1u);
}

static void test_icon_grid_control(void)
{
    static const AstraCollectionItem items[] = {
        {1u, "One", 3u, {0u, 0u}},
        {2u, "Two", 3u, {0u, 0u}},
        {3u, "Three", 5u, {0u, 0u}},
        {4u, "Four", 4u, {0u, 0u}},
        {5u, "Five", 4u, {0u, 0u}}
    };
    static const AstraCollectionItem reordered[] = {
        {5u, "Five", 4u, {0u, 0u}},
        {4u, "Four", 4u, {0u, 0u}},
        {3u, "Three", 5u, {0u, 0u}},
        {2u, "Two", 3u, {0u, 0u}},
        {1u, "One", 3u, {0u, 0u}}
    };
    AstraSurface icon = ASTRA_SURFACE_INIT;
    AstraSurface bad_icon = ASTRA_SURFACE_INIT;
    const AstraSurface *icons[5] = {&icon, NULL, NULL, NULL, NULL};
    const AstraSurface *bad_icons[5] = {&bad_icon, NULL, NULL, NULL, NULL};
    AstraCollectionModel collection = ASTRA_COLLECTION_MODEL_INIT;
    AstraIconGridModel grid = ASTRA_ICON_GRID_MODEL_INIT;
    AstraIconGridInfo info = ASTRA_ICON_GRID_INFO_INIT;
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraControlSize size = {0};
    AstraSurfaceView surface = render_view(320u, 200u);
    AstraWindowEvent event;

    icon._private_handle = 42u;
    icon._private_window = 1u;
    icon._private_id = 7u;
    icon._private_width = icon._private_height = 64u;
    bad_icon = icon;
    bad_icon._private_width = 32u;
    assert(astra_interface_collection_init(&collection, items, 5u, 0u) == ASTRA_OK);
    grid.collection = &collection;
    grid.icons = icons;
    grid.icon_count = 5u;
    grid.columns = 2u;
    info.id = 93u;
    info.model = &grid;
    assert(astra_interface_icon_grid_init(&control, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &control, 1u, 320u, 200u) == ASTRA_OK);
    assert(astra_interface_ui_measure(&context, &control, &size) == ASTRA_OK);
    assert(size.width == 208u && size.height == 264u);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    icon_blit_calls = 0u;
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(icon_blit_calls == 1u);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
        ASTRA_WINDOW_EVENT_DOWN, control._private_frame.x + 110,
        control._private_frame.y + 10);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    event.flags = 0u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.value == 1 && collection.selected_id == 2u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x51u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(collection.selected_id == 4u && control._private_value == 3);
    assert(astra_interface_icon_grid_set_items(&context, &control,
        reordered, 5u, icons) == ASTRA_OK);
    assert(collection.selected_id == 4u && control._private_value == 1);
    assert(astra_interface_icon_grid_set_items(&context, &control,
        items, 5u, bad_icons) == ASTRA_ERROR_INVALID_ARGUMENT);
    assert(collection.items == reordered && grid.icons == icons);
}

static void test_scrolled_list_control(void)
{
    static const char family[] = "A deliberately wide installed font family name";
    AstraCollectionItem items[100];
    AstraControl controls[2] = {ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraScrollModel model = ASTRA_SCROLL_MODEL_INIT;
    AstraScrollModelInfo model_info = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraScrollViewInfo view_info = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraCollectionModel collection = ASTRA_COLLECTION_MODEL_INIT;
    AstraListInfo list_info = ASTRA_LIST_INFO_INIT;
    AstraFlexItem flex = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraWindowEvent event;
    AstraSurfaceView surface = render_view(240u, 80u);
    AstraDrawListHeader draw_header = {
        .magic = ASTRA_DRAW_LIST_MAGIC,
        .version = ASTRA_DRAW_LIST_VERSION_1_8,
        .total_bytes = ASTRA_DRAW_LIST_AREA_BYTES,
        .width = 240u,
        .height = 80u,
        .command_capacity = ASTRA_DRAW_LIST_COMMAND_MAX};
    AstraSurfaceView draw_list = {
        (uint16_t *)(void *)&draw_header, ASTRA_DRAW_LIST_AREA_BYTES, 0u,
        240u, 80u, ASTRA_SURFACE_VIEW_DRAW_LIST, 0u, 0u, 240u, 80u};

    for (uint32_t at = 0u; at < 100u; ++at) {
        items[at] = (AstraCollectionItem){
            (uint64_t)at + 1u, family, sizeof(family) - 1u, {0u, 0u}};
    }
    model_info.line_height = theme.control_height;
    assert(astra_scroll_init(&model, &model_info) == ASTRA_OK);
    view_info.id = 74u;
    view_info.model = &model;
    view_info.preferred_width = 240u;
    view_info.preferred_height = 80u;
    assert(astra_interface_scroll_view_init(&controls[0], &view_info) ==
           ASTRA_OK);
    flex.minimum_width = flex.maximum_width = 240u;
    flex.minimum_height = flex.maximum_height = 80u;
    assert(astra_interface_control_set_flex(&controls[0], &flex) == ASTRA_OK);
    list_info.id = 75u;
    assert(astra_interface_collection_init(&collection, items, 100u, 0u) ==
           ASTRA_OK);
    list_info.model = &collection;
    assert(astra_interface_list_init(&controls[1], &list_info) == ASTRA_OK);
    flex = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    flex.parent_id = 74u;
    assert(astra_interface_control_set_flex(&controls[1], &flex) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 2u, 240u, 80u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(astra_scroll_get_state(&model, &state) == ASTRA_OK);
    assert(state.content_height == 100u * theme.control_height);
    assert(astra_scroll_set_offset(&model, 0u, 50u * theme.control_height) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &draw_list) == ASTRA_OK);
    assert(text_calls <= 4u && text_calls >= 3u);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN, 8, 8);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event.flags = 0u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.control_id == 75u && action.value == 50);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4du, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
           action.value == 99);
    assert(astra_scroll_get_state(&model, &state) == ASTRA_OK &&
           state.offset_y == state.maximum_y);
    {
        uint64_t offset = state.offset_y;
        uint32_t width = controls[1]._private_measured_width;
        AstraControlFrame damage;
        astra_interface_ui_damage_clear(&context);
        event = (AstraWindowEvent){.size = sizeof(event),
            .version = ASTRA_WINDOW_EVENT_VERSION, .type = ASTRA_WINDOW_EVENT_FONT_CHANGED};
        event.data.font_changed.generation = 7u;
        font_advance = 3u;
        assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
        assert(controls[1]._private_measured_width > width);
        assert(astra_scroll_get_state(&model, &state) == ASTRA_OK && state.offset_y == offset);
        assert(astra_interface_ui_damage(&context, &damage) == ASTRA_OK &&
               damage.width == 240u && damage.height == 80u);
        font_advance = 1u;
        assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    }
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(text_calls <= 4u && text_calls >= 3u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4au, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.value == 0);
    assert(astra_scroll_get_state(&model, &state) == ASTRA_OK &&
           state.offset_y == 0u);
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
    AstraSurfaceView surface = render_view(320u, 120u);

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
    assert(fill_calls == pointer_fill_calls + 3u);
}

static void test_stepper_control(void)
{
    AstraStepperInfo info = ASTRA_STEPPER_INFO_INIT;
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent event;
    AstraSurfaceView surface = render_view(160u, 40u);
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
    AstraSurfaceView surface = render_view(160u, 40u);
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
    assert(field._private_frame.width == 84u &&
           field._private_frame.height == 28u);

    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN,
                          field._private_frame.x + 10 + 8,
                          field._private_frame.y + 10);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_SELECTION_CHANGED &&
           action.control_id == 41u);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK);
    assert(model_state.selection.anchor == 1u &&
           model_state.selection.focus == 1u);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u,
                          field._private_frame.x + 10 + 24,
                          field._private_frame.y + 10);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_SELECTION_CHANGED);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK);
    assert(model_state.selection.anchor == 1u &&
           model_state.selection.focus == 4u);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON, 0u,
                          field._private_frame.x + 10 + 24,
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

static void test_secure_and_search_fields(void)
{
    uint8_t content[128] = {0};
    _Alignas(4) uint8_t metadata[256] = {0};
    AstraTextModel model = ASTRA_TEXT_MODEL_INIT;
    AstraTextModelInfo model_info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraTextSelection selection = ASTRA_TEXT_SELECTION_INIT;
    AstraControl field = ASTRA_CONTROL_INIT;
    AstraFieldInfo info = ASTRA_FIELD_INFO_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraSurfaceView surface = render_view(160u, 40u);
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent event;

    model_info.text = "secret";
    model_info.text_bytes = 6u;
    model_info.content_arena = content;
    model_info.content_arena_bytes = sizeof(content);
    model_info.metadata_arena = metadata;
    model_info.metadata_arena_bytes = sizeof(metadata);
    model_info.selection.anchor = 0u;
    model_info.selection.focus = 6u;
    assert(astra_text_model_init(&model, &model_info) == ASTRA_OK);
    info.id = 90u;
    info.model = &model;
    info.preferred_columns = 8u;
    assert(astra_interface_secure_field_init(&field, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &field, 1u, 160u, 40u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(astra_interface_ui_focus_control(&context, 90u) == ASTRA_OK);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(first_mono_length == 6u);
    assert(memcmp(first_mono_text, "******", 6u) == 0);
    assert(astra_text_model_replace(&model, 0u, 6u,
        "\xc3\xa9\xf0\x9f\x94\x92", 6u) == ASTRA_OK);
    assert(astra_interface_field_refresh(&context, &field) == ASTRA_OK);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(first_mono_length == 2u &&
           memcmp(first_mono_text, "**", 2u) == 0);
    assert(astra_text_model_replace(&model, 0u, 6u, "secret", 6u) == ASTRA_OK);
    selection.focus = 6u;
    assert(astra_text_model_set_selection(&model, &selection) == ASTRA_OK);
    assert(astra_interface_field_refresh(&context, &field) == ASTRA_OK);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x06u, ASTRA_INPUT_MOD_META);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_NONE);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x1bu, ASTRA_INPUT_MOD_META);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_NONE);
    assert(astra_text_model_get_state(&model, &state) == ASTRA_OK &&
           state.text_bytes == 6u);

    info.id = 91u;
    assert(astra_interface_search_field_init(&field, &info) == ASTRA_OK);
    context = (AstraUIContext)ASTRA_UI_CONTEXT_INIT;
    assert(astra_interface_ui_init(&context, &field, 1u, 160u, 40u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(astra_interface_ui_focus_control(&context, 91u) == ASTRA_OK);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(first_mono_length == 6u &&
           memcmp(first_mono_text, "secret", 6u) == 0);
    assert(line_calls == 7u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x29u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(astra_text_model_get_state(&model, &state) == ASTRA_OK &&
           state.text_bytes == 0u);
}

static void test_composed_fields_and_colour_well(void)
{
    uint8_t left_content[64] = {0}, right_content[64] = {0};
    _Alignas(4) uint8_t left_metadata[256] = {0},
        right_metadata[256] = {0};
    AstraTextModel left = ASTRA_TEXT_MODEL_INIT;
    AstraTextModel right = ASTRA_TEXT_MODEL_INIT;
    AstraTextModelInfo model_info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraTextModel *models[2] = {&left, &right};
    uint32_t columns[2] = {3u, 3u};
    AstraControl segmented[4] = {
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT,
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT
    };
    AstraSegmentedFieldInfo segment = ASTRA_SEGMENTED_FIELD_INFO_INIT;
    AstraChoiceItem tags[2] = {
        {"vega", 4u, {0u, 0u}}, {"rtl", 3u, {0u, 0u}}
    };
    AstraControl token_controls[4] = {
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT,
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT
    };
    AstraTagInputInfo tag = ASTRA_TAG_INPUT_INFO_INIT;
    AstraControl well = ASTRA_CONTROL_INIT;
    AstraColourWellInfo color_info = ASTRA_COLOUR_WELL_INFO_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraSurfaceView surface = render_view(160u, 40u);
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent event;

    model_info.text = "1";
    model_info.text_bytes = 1u;
    model_info.content_arena = left_content;
    model_info.content_arena_bytes = sizeof(left_content);
    model_info.metadata_arena = left_metadata;
    model_info.metadata_arena_bytes = sizeof(left_metadata);
    assert(astra_text_model_init(&left, &model_info) == ASTRA_OK);
    model_info.text = "2";
    model_info.content_arena = right_content;
    model_info.content_arena_bytes = sizeof(right_content);
    model_info.metadata_arena = right_metadata;
    model_info.metadata_arena_bytes = sizeof(right_metadata);
    assert(astra_text_model_init(&right, &model_info) == ASTRA_OK);
    segment.first_id = 100u;
    segment.models = models;
    segment.preferred_columns = columns;
    segment.segment_count = 2u;
    segment.separator = ".";
    segment.separator_length = 1u;
    assert(astra_interface_segmented_field_init(segmented, 4u, &segment) ==
           ASTRA_OK);
    assert(segmented[1]._private_parent_id == 100u &&
           segmented[3]._private_parent_id == 100u);
    assert(segmented[2]._private_control_kind == ASTRA_CONTROL_LABEL);
    assert(astra_interface_segmented_field_init(segmented, 3u, &segment) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_interface_ui_init(&context, segmented, 4u, 160u, 100u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(astra_interface_ui_focus_control(&context, 101u) == ASTRA_OK);
    surface = render_view(160u, 100u);
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);

    tag.first_id = 110u;
    tag.tags = tags;
    tag.tag_count = 2u;
    tag.draft = &left;
    assert(astra_interface_tag_input_init(token_controls, 4u, &tag) ==
           ASTRA_OK);
    assert(token_controls[0]._private_style ==
           ASTRA_CONTAINER_STYLE_TAG_INPUT);
    assert(token_controls[1]._private_style == ASTRA_BUTTON_TAG);
    assert(token_controls[3]._private_control_kind == ASTRA_CONTROL_FIELD);
    context = (AstraUIContext)ASTRA_UI_CONTEXT_INIT;
    assert(astra_interface_ui_init(&context, token_controls, 4u,
        160u, 100u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(astra_interface_ui_focus_control(&context, 111u) == ASTRA_OK);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x28u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = key_event(0u, 0x28u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_ACTIVATE &&
           action.control_id == 111u);

    color_info.id = 120u;
    color_info.label = "#162D3A";
    color_info.label_length = 7u;
    color_info.color = (AstraColorRGBA8){22u, 45u, 58u, 255u};
    assert(astra_interface_colour_well_init(&well, &color_info) == ASTRA_OK);
    context = (AstraUIContext)ASTRA_UI_CONTEXT_INIT;
    surface = render_view(160u, 40u);
    assert(astra_interface_ui_init(&context, &well, 1u, 160u, 40u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(fill_calls == 3u &&
           fill_colors[2] == astra_surface_rgb565(22u, 45u, 58u));
    assert(astra_interface_colour_well_set(&context, &well,
        ASTRA_COLOUR_THEME_ACCENT, color_info.color, 0u) == ASTRA_OK);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(fill_colors[2] == astra_surface_rgb565(theme.accent.red,
        theme.accent.green, theme.accent.blue));
    assert(astra_interface_ui_focus_control(&context, 120u) == ASTRA_OK);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x28u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    event = key_event(0u, 0x28u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_ACTIVATE &&
           action.control_id == 120u);
}

static void test_text_field_rejections(void)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
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

    info.flags = ASTRA_FIELD_INLINE;
    field = (AstraControl)ASTRA_CONTROL_INIT;
    context = (AstraUIContext)ASTRA_UI_CONTEXT_INIT;
    assert(astra_interface_field_init(&field, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &field, 1u, 80u, 32u) ==
           ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(field._private_frame.width == 160u);
    AstraSurfaceView surface = render_view(80u, 32u);
    fill_calls = 0u;
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(fill_calls == 0u);
    assert(astra_interface_ui_focus_control(&context, 42u) == ASTRA_OK);
    surface = render_view(80u, 32u);
    fill_calls = 0u;
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(fill_calls == 1u && fill_width[0] == theme.mono_cell_width &&
           fill_height[0] == 2u &&
           fill_y[0] == field._private_frame.y + (int32_t)(
               (field._private_frame.height - theme.mono_font_height) / 2u +
               theme.mono_font_height - 2u));
}

static void test_text_view(void)
{
    static const char source[] = "A\t\xce\xa9\r\nSecond line\nThird\nLast";
    uint8_t content[256];
    _Alignas(4) uint8_t metadata[512];
    AstraTextModel model = ASTRA_TEXT_MODEL_INIT;
    AstraTextModelInfo model_info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraTextModelState model_state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraTextViewInfo text_info = ASTRA_TEXT_VIEW_INFO_INIT;
    AstraScrollModel scroll = ASTRA_SCROLL_MODEL_INIT;
    AstraScrollModelInfo scroll_info = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraScrollState scroll_state = ASTRA_SCROLL_STATE_INIT;
    AstraScrollViewInfo view_info = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraControl controls[2] = {ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraSurfaceView surface = render_view(64u, 56u);
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraControlSize measured;
    AstraWindowEvent event;
    uint32_t bytes;
    char copied[sizeof(source)];

    model_info.text = source; model_info.text_bytes = sizeof(source) - 1u;
    model_info.content_arena = content;
    model_info.content_arena_bytes = sizeof(content);
    model_info.metadata_arena = metadata;
    model_info.metadata_arena_bytes = sizeof(metadata);
    assert(astra_text_model_init(&model, &model_info) == ASTRA_OK);
    assert(astra_scroll_init(&scroll, &scroll_info) == ASTRA_OK);
    view_info.id = 1u; view_info.model = &scroll;
    view_info.preferred_width = 64u; view_info.preferred_height = 56u;
    assert(astra_interface_scroll_view_init(&controls[0], &view_info) == ASTRA_OK);
    text_info.id = 2u; text_info.model = &model;
    assert(astra_interface_text_view_init(&controls[1], &text_info) == ASTRA_OK);
    item.parent_id = 1u;
    assert(astra_interface_control_set_flex(&controls[1], &item) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 2u, 64u, 56u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(astra_interface_ui_measure(&context, &controls[1], &measured) == ASTRA_OK);
    assert(measured.width == 11u * theme.mono_cell_width &&
           measured.height == 4u * theme.control_height);
    assert(astra_scroll_get_state(&scroll, &scroll_state) == ASTRA_OK &&
           scroll_state.maximum_x > 0u && scroll_state.maximum_y > 0u);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(mono_text_calls == 2u);
    assert(astra_interface_text_view_set_inverted(&context, &controls[0], 1u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_interface_text_view_set_inverted(&context, &controls[1], 2u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_interface_text_view_set_inverted(&context, &controls[1], 1u) ==
           ASTRA_OK);
    surface = render_view(64u, 56u);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    int found_inverted_background = 0;
    for (uint32_t at = 0u; at < fill_calls && at < 32u; ++at)
        if (fill_colors[at] == theme_color(theme.client_text))
            found_inverted_background = 1;
    assert(found_inverted_background);
    assert(astra_interface_text_view_set_inverted(&context, &controls[1], 0u) ==
           ASTRA_OK);
    ((AstraDrawListHeader *)(void *)surface.pixels)->command_count =
        ASTRA_DRAW_LIST_COMMAND_MAX - 1u;
    uint32_t before_fills = fill_calls, before_text = mono_text_calls;
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_ERROR_NO_RESOURCES &&
           fill_calls == before_fills && mono_text_calls == before_text);
    surface = render_view(64u, 56u);

    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
        ASTRA_WINDOW_EVENT_DOWN, 8, 4);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK &&
           model_state.selection.focus == 1u);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u, 24, 32);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK &&
           model_state.selection.anchor == 1u && model_state.selection.focus == 9u);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON, 0u, 24, 32);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x04u, ASTRA_INPUT_MOD_META);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x06u, ASTRA_INPUT_MOD_META);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK &&
           action.type == ASTRA_UI_ACTION_COPY && action.control_id == 2u);
    assert(astra_text_model_copy(&model, 0u, sizeof(source) - 1u, copied,
        sizeof(copied), &bytes) == ASTRA_OK && bytes == sizeof(source) - 1u &&
        memcmp(copied, source, bytes) == 0);
    event = text_event('x');
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK &&
           action.type == ASTRA_UI_ACTION_NONE);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x2au, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK &&
           action.type == ASTRA_UI_ACTION_NONE);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK &&
           model_state.text_bytes == sizeof(source) - 1u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4au, ASTRA_INPUT_MOD_CTRL);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(astra_scroll_get_state(&scroll, &scroll_state) == ASTRA_OK &&
           scroll_state.offset_x == 0u && scroll_state.offset_y == 0u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x51u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK &&
           model_state.selection.focus == 6u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4eu, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK &&
           model_state.selection.focus == 24u);
    assert(astra_scroll_get_state(&scroll, &scroll_state) == ASTRA_OK &&
           scroll_state.offset_y > 0u);
    assert(astra_text_model_replace(&model, 0u, sizeof(source) - 1u, "short", 5u) == ASTRA_OK);
    assert(astra_interface_text_view_refresh(&context, &controls[1]) == ASTRA_OK);
    assert(astra_scroll_get_state(&scroll, &scroll_state) == ASTRA_OK &&
           scroll_state.maximum_x == 0u && scroll_state.maximum_y == 0u);
    text_info.reserved[0] = 1u;
    assert(astra_interface_text_view_init(&controls[1], &text_info) == ASTRA_ERROR_INVALID_ARGUMENT);
}

static AstraResult styled_text_at(void *context, uint32_t offset,
                                  AstraTextViewStyle *style)
{
    uint32_t *calls = context;
    ++calls[0];
    if (calls[1] != 0u && offset == 1u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (offset > 1u) return ASTRA_ERROR_INVALID_ARGUMENT;
    *style = (AstraTextViewStyle)ASTRA_TEXT_VIEW_STYLE_INIT;
    if (offset == 0u) {
        style->attributes = ASTRA_TEXT_STYLE_BOLD;
        style->foreground = 0xf800u;
    } else {
        style->attributes = ASTRA_TEXT_STYLE_ITALIC |
                            ASTRA_TEXT_STYLE_INVERSE;
        style->foreground = 0x001fu;
        style->background = 0xffe0u;
    }
    return ASTRA_OK;
}

static void test_text_view_styles(void)
{
    uint8_t content[32];
    _Alignas(4) uint8_t metadata[128];
    AstraTextModel model = ASTRA_TEXT_MODEL_INIT;
    AstraTextModelInfo model_info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraTextViewInfo info = ASTRA_TEXT_VIEW_INFO_INIT;
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraSurfaceView surface = render_view(64u, 40u);
    uint32_t calls[2] = {0u, 0u};

    model_info.text = "AB";
    model_info.text_bytes = 2u;
    model_info.content_arena = content;
    model_info.content_arena_bytes = sizeof(content);
    model_info.metadata_arena = metadata;
    model_info.metadata_arena_bytes = sizeof(metadata);
    assert(astra_text_model_init(&model, &model_info) == ASTRA_OK);
    info.id = 1u;
    info.model = &model;
    info.style_at = styled_text_at;
    info.style_context = calls;
    assert(astra_interface_text_view_init(&control, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &control, 1u, 64u, 40u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(calls[0] >= 2u && mono_text_calls == 2u);
    assert(mono_styles[0] == ASTRA_TEXT_STYLE_BOLD &&
           mono_colors[0] == 0xf800u);
    assert(mono_styles[1] == ASTRA_TEXT_STYLE_ITALIC &&
           mono_colors[1] == 0xffe0u);
    calls[1] = 1u;
    surface = render_view(64u, 40u);
    assert(astra_interface_ui_render(&context, &surface) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    astra_text_model_dispose(&model);
}

static void test_text_view_document_capacity(void)
{
    char source[30u * 81u];
    uint8_t content[4096];
    _Alignas(4) uint8_t metadata[4096];
    AstraTextModel model = ASTRA_TEXT_MODEL_INIT;
    AstraTextModelInfo model_info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraTextViewInfo text_info = ASTRA_TEXT_VIEW_INFO_INIT;
    AstraScrollModel scroll = ASTRA_SCROLL_MODEL_INIT;
    AstraScrollModelInfo scroll_info = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraScrollViewInfo view_info = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraControl controls[2] = {ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraSurfaceView surface = render_view(700u, 500u);
    for (uint32_t row = 0u; row < 30u; ++row) {
        memset(source + row * 81u, 'L', 80u); source[row * 81u + 80u] = '\n';
    }
    model_info.text = source; model_info.text_bytes = sizeof(source);
    model_info.content_arena = content; model_info.content_arena_bytes = sizeof(content);
    model_info.metadata_arena = metadata; model_info.metadata_arena_bytes = sizeof(metadata);
    assert(astra_text_model_init(&model, &model_info) == ASTRA_OK);
    assert(astra_scroll_init(&scroll, &scroll_info) == ASTRA_OK);
    view_info.id = 1u; view_info.model = &scroll;
    view_info.preferred_width = 700u; view_info.preferred_height = 500u;
    assert(astra_interface_scroll_view_init(&controls[0], &view_info) == ASTRA_OK);
    text_info.id = 2u; text_info.model = &model;
    assert(astra_interface_text_view_init(&controls[1], &text_info) == ASTRA_OK);
    item.parent_id = 1u;
    assert(astra_interface_control_set_flex(&controls[1], &item) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 2u, 700u, 500u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(mono_text_calls == 18u);
    AstraTextSelection selection = ASTRA_TEXT_SELECTION_INIT;
    selection.anchor = 10u; selection.focus = 17u * 81u + 40u;
    assert(astra_text_model_set_selection(&model, &selection) == ASTRA_OK);
    assert(astra_interface_text_view_refresh(&context, &controls[1]) == ASTRA_OK);
    surface = render_view(700u, 500u);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(mono_text_calls == 20u);
}

static void test_text_view_wrap(void)
{
    static const char source[] = "alpha beta gamma\nZ";
    uint8_t content[128];
    _Alignas(4) uint8_t metadata[256];
    AstraTextModel model = ASTRA_TEXT_MODEL_INIT;
    AstraTextModelInfo model_info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraTextModelState model_state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraTextViewInfo text_info = ASTRA_TEXT_VIEW_INFO_INIT;
    AstraScrollModel scroll = ASTRA_SCROLL_MODEL_INIT;
    AstraScrollModelInfo scroll_info = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraScrollState scroll_state = ASTRA_SCROLL_STATE_INIT;
    AstraScrollViewInfo view_info = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraControl controls[2] = {ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraControlSize measured;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    uint32_t width = 6u * theme.mono_cell_width;
    uint32_t height = 2u * theme.control_height;

    model_info.text = source;
    model_info.text_bytes = sizeof(source) - 1u;
    model_info.content_arena = content;
    model_info.content_arena_bytes = sizeof(content);
    model_info.metadata_arena = metadata;
    model_info.metadata_arena_bytes = sizeof(metadata);
    assert(astra_text_model_init(&model, &model_info) == ASTRA_OK);
    assert(astra_scroll_init(&scroll, &scroll_info) == ASTRA_OK);
    view_info.id = 1u; view_info.model = &scroll;
    view_info.preferred_width = width; view_info.preferred_height = height;
    assert(astra_interface_scroll_view_init(&controls[0], &view_info) == ASTRA_OK);
    text_info.id = 2u; text_info.model = &model;
    assert(astra_interface_text_view_init(&controls[1], &text_info) == ASTRA_OK);
    item.parent_id = 1u;
    assert(astra_interface_control_set_flex(&controls[1], &item) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 2u, width, height) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(astra_interface_text_view_set_wrap_width(&context, &controls[1], width) == ASTRA_OK);
    assert(astra_interface_ui_measure(&context, &controls[1], &measured) == ASTRA_OK);
    assert(measured.width == width && measured.height == 4u * theme.control_height);
    assert(astra_scroll_get_state(&scroll, &scroll_state) == ASTRA_OK &&
           scroll_state.maximum_x == 0u && scroll_state.maximum_y > 0u);
    AstraWindowEvent event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
        ASTRA_WINDOW_EVENT_DOWN, 1, (int32_t)theme.control_height + 2);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK &&
           model_state.selection.focus == 6u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x51u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK &&
           model_state.selection.focus == 11u);
    assert(astra_interface_text_view_set_wrap_width(&context, &controls[1],
        5u * theme.mono_cell_width) == ASTRA_OK);
    assert(astra_scroll_set_offset(&scroll, 0u, 0u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
        ASTRA_WINDOW_EVENT_DOWN, 1, (int32_t)theme.control_height + 2);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK &&
           model_state.selection.focus == 6u);
    assert(astra_interface_text_view_set_wrap_width(&context, &controls[1], width) == ASTRA_OK);
    assert(astra_scroll_set_offset(&scroll, 0u, 0u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
        ASTRA_WINDOW_EVENT_DOWN, (int32_t)width - 1, 2);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK &&
           model_state.selection.focus == 6u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4du, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK &&
           model_state.selection.focus == 6u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x51u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK &&
           model_state.selection.focus == 11u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x52u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK &&
           model_state.selection.focus == 5u);
    assert(astra_interface_text_view_set_wrap_width(&context, &controls[1],
        3u * theme.mono_cell_width) == ASTRA_OK);
    assert(astra_interface_ui_measure(&context, &controls[1], &measured) == ASTRA_OK &&
           measured.height == 7u * theme.control_height);
    assert(astra_interface_text_view_set_wrap_width(&context, &controls[1],
        12u * theme.mono_cell_width) == ASTRA_OK);
    assert(astra_interface_ui_measure(&context, &controls[1], &measured) == ASTRA_OK &&
           measured.height == 3u * theme.control_height);
    assert(astra_interface_text_view_set_wrap_width(&context, &controls[1], 0u) == ASTRA_OK);
    assert(astra_interface_ui_measure(&context, &controls[1], &measured) == ASTRA_OK &&
           measured.height == 2u * theme.control_height && measured.width > width);
    assert(astra_text_model_replace(&model, 0u, sizeof(source) - 1u,
        "a longword", 10u) == ASTRA_OK);
    assert(astra_interface_text_view_refresh(&context, &controls[1]) == ASTRA_OK);
    assert(astra_interface_text_view_set_wrap_width(&context, &controls[1],
        8u * theme.mono_cell_width) == ASTRA_OK);
    AstraTextSelection selection = ASTRA_TEXT_SELECTION_INIT;
    selection.anchor = selection.focus = 10u;
    assert(astra_text_model_set_selection(&model, &selection) == ASTRA_OK);
    assert(astra_interface_text_view_refresh(&context, &controls[1]) == ASTRA_OK);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x52u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK &&
           model_state.selection.focus == 2u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x51u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK &&
           model_state.selection.focus == 4u);
    assert(astra_text_model_replace(&model, 0u, 10u, "alpha ", 6u) == ASTRA_OK);
    assert(astra_interface_text_view_refresh(&context, &controls[1]) == ASTRA_OK);
    assert(astra_interface_text_view_set_wrap_width(&context, &controls[1],
        5u * theme.mono_cell_width) == ASTRA_OK);
    assert(astra_interface_ui_measure(&context, &controls[1], &measured) == ASTRA_OK &&
           measured.height == 2u * theme.control_height);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x4du, ASTRA_INPUT_MOD_CTRL);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK &&
           model_state.selection.focus == 6u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x52u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x51u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK &&
           model_state.selection.focus == 6u);
}

static void test_text_view_flow_edit_find(void)
{
    static const char source[] = "alpha beta\nGamma";
    uint8_t content[128];
    _Alignas(4) uint8_t metadata[256];
    AstraTextModel model = ASTRA_TEXT_MODEL_INIT;
    AstraTextModelInfo model_info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraTextModelState model_state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraTextViewInfo text_info = ASTRA_TEXT_VIEW_INFO_INIT;
    AstraScrollModel scroll = ASTRA_SCROLL_MODEL_INIT;
    AstraScrollModelInfo scroll_info = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraScrollState scroll_state = ASTRA_SCROLL_STATE_INIT;
    AstraScrollViewInfo view_info = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraControl controls[2] = {ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraControlSize measured;
    AstraTextSelection match = ASTRA_TEXT_SELECTION_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    char copied[32];
    uint32_t bytes;

    model_info.text = source;
    model_info.text_bytes = sizeof(source) - 1u;
    model_info.content_arena = content;
    model_info.content_arena_bytes = sizeof(content);
    model_info.metadata_arena = metadata;
    model_info.metadata_arena_bytes = sizeof(metadata);
    assert(astra_text_model_init(&model, &model_info) == ASTRA_OK);
    assert(astra_scroll_init(&scroll, &scroll_info) == ASTRA_OK);
    view_info.id = 1u; view_info.model = &scroll;
    view_info.preferred_width = 16u; view_info.preferred_height = 40u;
    assert(astra_interface_scroll_view_init(&controls[0], &view_info) == ASTRA_OK);
    text_info.id = 2u; text_info.model = &model;
    assert(astra_interface_text_view_init(&controls[1], &text_info) == ASTRA_OK);
    item.parent_id = 1u;
    assert(astra_interface_control_set_flex(&controls[1], &item) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 2u, 16u, 40u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(astra_interface_text_view_set_mode(&context, &controls[1],
        ASTRA_TEXT_SURFACE_GRID) == ASTRA_ERROR_INVALID_ARGUMENT);
    font_advance = 2u;
    font_line_height = 30u;
    assert(astra_interface_text_view_set_mode(&context, &controls[1],
        ASTRA_TEXT_SURFACE_FLOW) == ASTRA_OK);
    assert(astra_interface_text_view_set_wrap_width(&context, &controls[1],
        10u) == ASTRA_OK);
    assert(astra_interface_ui_measure(&context, &controls[1], &measured) == ASTRA_OK &&
        measured.width == 10u && measured.height > 40u &&
        measured.height % font_line_height == 0u);
    AstraSurfaceView surface = render_view(16u, 40u);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK &&
        text_calls != 0u && mono_text_calls == 0u);
    assert(astra_interface_text_view_find(&context, &controls[1],
        "beta", 4u, 0u, 0u, &match) == ASTRA_OK &&
        match.anchor == 6u && match.focus == 10u);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK &&
        model_state.selection.anchor == 6u && model_state.selection.focus == 10u);
    assert(astra_interface_text_view_find(&context, &controls[1],
        "missing", 7u, 0u, 0u, &match) == ASTRA_ERROR_NOT_PRESENT &&
        match.anchor == 6u && match.focus == 10u);
    assert(astra_interface_text_view_reveal(&context, &controls[1], 1u) == ASTRA_OK);
    assert(astra_interface_text_view_reveal(&context, &controls[1], 99u) ==
        ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_interface_text_view_replace_selection(&context, &controls[1],
        "B", 1u) == ASTRA_ERROR_PERMISSION);
    assert(astra_interface_text_view_set_editable(&context, &controls[1], 1u) == ASTRA_OK);
    assert(astra_interface_text_view_replace_selection(&context, &controls[1],
        "B", 1u) == ASTRA_OK);
    assert(astra_text_model_copy(&model, 0u, 13u, copied, sizeof(copied),
        &bytes) == ASTRA_OK && bytes == 13u &&
        memcmp(copied, "alpha B\nGamma", 13u) == 0);
    assert(astra_interface_text_view_select(&context, &controls[1], 7u, 7u) == ASTRA_OK);
    AstraWindowEvent event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
        ASTRA_WINDOW_EVENT_DOWN, 2, 2);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x28u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK &&
        action.type == ASTRA_UI_ACTION_TEXT_CHANGED);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK &&
        model_state.line_count == 3u);
    assert(astra_scroll_get_state(&scroll, &scroll_state) == ASTRA_OK &&
        scroll_state.maximum_y > 0u);
    assert(astra_interface_text_view_set_mode(&context, &controls[1],
        ASTRA_TEXT_SURFACE_CODE) == ASTRA_OK);
    assert(astra_interface_text_view_set_wrap_width(&context, &controls[1], 0u) ==
        ASTRA_OK);
    assert(astra_interface_ui_measure(&context, &controls[1], &measured) == ASTRA_OK);
    uint32_t code_width = measured.width;
    assert(astra_interface_text_view_set_line_numbers(&context, &controls[1], 1u) ==
        ASTRA_OK);
    assert(astra_interface_ui_measure(&context, &controls[1], &measured) == ASTRA_OK &&
        measured.width == code_width + 3u * theme.mono_cell_width);
    assert(astra_scroll_set_offset(&scroll, 0u, 0u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
        ASTRA_WINDOW_EVENT_DOWN, 2, 2);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &model_state) == ASTRA_OK &&
        model_state.selection.focus == 0u);
    font_advance = 1u;
    font_line_height = 0u;
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
        .version = ASTRA_DRAW_LIST_VERSION_1_8,
        .total_bytes = ASTRA_DRAW_LIST_AREA_BYTES,
        .width = 130u,
        .height = 60u,
        .command_capacity = ASTRA_DRAW_LIST_COMMAND_MAX};
    AstraSurfaceView draw_list = {
        (uint16_t *)(void *)&list, ASTRA_DRAW_LIST_AREA_BYTES, 0u,
        130u, 60u, ASTRA_SURFACE_VIEW_DRAW_LIST, 0u, 0u, 130u, 60u};
    AstraSurfaceView surface = render_view(130u, 60u);

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
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(fill_calls == 2u);
    assert(fill_x[0] == 120 && fill_y[0] == 0 &&
           fill_width[0] == 10u && fill_height[0] == 60u);
    assert(fill_x[1] == 122 && fill_y[1] == 0 &&
           fill_width[1] == 6u && fill_height[1] == 24u);
    assert(astra_scroll_set_marks(&model, marks, 2u) == ASTRA_OK);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
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
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
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
    assert((controls[0]._private_dynamic_state & ASTRA_CONTROL_FOCUSED) != 0u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x51u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_SCROLL_CHANGED &&
           action.control_id == 200u);
    assert(astra_scroll_get_state(&model, &state) == ASTRA_OK);
    assert(state.offset_y == 16u);
    assert(astra_scroll_set_offset(&model, 0u, 0u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &root) == ASTRA_OK);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x2bu, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
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
    pointer_shape_calls = 0u;
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
        AstraSurfaceView surface = render_view(200u, 80u);
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
    AstraSurfaceView surface = render_view(240u, 100u);
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

static void test_context_request(void)
{
    AstraControl controls[2] = {ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraToggleInfo checkbox = ASTRA_TOGGLE_INFO_INIT;
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraControlFrame frame, clip;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent event;

    checkbox.id = 21u;
    checkbox.text = "Check";
    checkbox.text_length = 5u;
    label.id = 22u;
    label.text = "Label";
    label.text_length = 5u;
    assert(astra_interface_checkbox_init(&controls[0], &checkbox) == ASTRA_OK);
    assert(astra_interface_label_init(&controls[1], &label) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, controls, 2u, 200u, 80u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(astra_interface_ui_geometry(&context, 0u, &frame, &clip) == ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON, ASTRA_WINDOW_EVENT_DOWN,
        frame.x + 1, frame.y + 1);
    event.data.pointer.button = 3u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_NONE);
    event.flags = 0u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_CONTEXT_MENU && action.control_id == 21u);
    assert((controls[0]._private_state & ASTRA_CONTROL_SELECTED) == 0u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x65u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_CONTEXT_MENU && action.control_id == 21u);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x43u, ASTRA_INPUT_MOD_SHIFT);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_CONTEXT_MENU && action.control_id == 21u);
    event.flags |= ASTRA_WINDOW_EVENT_REPEAT;
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_NONE);
    event = key_event(ASTRA_WINDOW_EVENT_DOWN, 0x43u, 0u);
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(action.type != ASTRA_UI_ACTION_CONTEXT_MENU);
    assert(astra_interface_ui_geometry(&context, 1u, &frame, &clip) == ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON, 0u,
        frame.x + 1, frame.y + 1);
    event.data.pointer.button = 3u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_CONTEXT_MENU && action.control_id == 22u);
    event.flags = ASTRA_WINDOW_EVENT_LOSS;
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_NONE);
    assert(astra_interface_ui_init(&context, NULL, 0u, 200u, 80u) == ASTRA_OK);
    event.flags = 0u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) == ASTRA_OK);
    assert(action.type == ASTRA_UI_ACTION_CONTEXT_MENU && action.control_id == 0u);
}

static void test_grid_view(void)
{
    AstraTextCell cells[160];
    AstraGridViewModel model = ASTRA_GRID_VIEW_MODEL_INIT;
    AstraGridViewInfo info = ASTRA_GRID_VIEW_INFO_INIT;
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraControlSize size = {0};
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    for (uint32_t at = 0u; at < 160u; ++at)
        cells[at] = (AstraTextCell){' ', ASTRA_TEXT_COLOR_DEFAULT,
            ASTRA_TEXT_COLOR_DEFAULT, 0u, 1u, 0u};
    cells[4] = (AstraTextCell){'X', 0xf800u, 0u,
        ASTRA_TEXT_STYLE_BOLD | ASTRA_TEXT_STYLE_INVERSE, 1u, 0u};
    model.cells = cells;
    model.columns = 80u;
    model.rows = 2u;
    model.capacity_cells = 160u;
    info.id = 1u;
    info.model = &model;
    assert(astra_interface_grid_view_init(&control, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &control, 1u, 800u, 80u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(astra_interface_ui_measure(&context, &control, &size) == ASTRA_OK);
    assert(size.width == 80u * theme.mono_cell_width);
    assert(size.height == 2u * theme.control_height);
    AstraSurfaceView surface = render_view(800u, 80u);
    reset_render_calls();
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(mono_text_calls != 0u);
    model.rows = 1u;
    assert(astra_interface_grid_view_refresh(&context, &control) == ASTRA_OK);
    assert(astra_interface_ui_measure(&context, &control, &size) == ASTRA_OK);
    assert(size.height == theme.control_height);
    model.rows = 0u;
    assert(astra_interface_grid_view_refresh(&context, &control) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(astra_interface_ui_measure(&context, &control, &size) == ASTRA_OK);
    assert(size.height == 1u);
    model.columns = 0u;
    assert(astra_interface_grid_view_refresh(&context, &control) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
}

static uint32_t custom_view_dry_calls, custom_view_draw_calls;

static AstraResult test_custom_view_draw(void *context,
    const AstraControlFrame *frame, AstraSurfaceView *surface,
    uint64_t *commands, uint64_t *payload)
{
    assert(context != NULL && frame != NULL);
    if (surface == NULL) {
        assert(commands != NULL && payload != NULL);
        ++*commands;
        ++custom_view_dry_calls;
    } else {
        assert(surface->kind == ASTRA_SURFACE_VIEW_DRAW_LIST);
        assert(surface->clip_right <= surface->width);
        ++custom_view_draw_calls;
        astra_surface_fill(surface, frame->x, frame->y,
                           frame->width, frame->height, 0xf800u);
        if (*(const int *)context == 2)
            ((AstraDrawListHeader *)surface->pixels)->command_count += 2u;
    }
    return ASTRA_OK;
}

static void test_custom_view(void)
{
    AstraViewModel model = ASTRA_VIEW_MODEL_INIT;
    AstraViewInfo info = ASTRA_VIEW_INFO_INIT;
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraControlSize size = {0};
    AstraControlFrame damage = {0};
    int token = 1;

    model.intrinsic_width = 160u;
    model.intrinsic_height = 90u;
    model.draw = test_custom_view_draw;
    model.context = &token;
    info.id = 7u;
    info.model = &model;
    assert(astra_interface_view_init(&control, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &control, 1u,
                                   320u, 180u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(astra_interface_ui_measure(&context, &control, &size) == ASTRA_OK);
    assert(size.width == 160u && size.height == 90u);
    AstraSurfaceView surface = render_view(320u, 180u);
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    assert(custom_view_dry_calls == 2u && custom_view_draw_calls == 1u);
    astra_interface_ui_damage_clear(&context);
    assert(astra_interface_view_invalidate(&context, &control) == ASTRA_OK);
    assert(astra_interface_ui_damage(&context, &damage) == ASTRA_OK);
    assert(damage.width != 0u && damage.height != 0u);
    token = 2;
    surface = render_view(320u, 180u);
    assert(astra_interface_ui_render(&context, &surface) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    token = 1;
    model.reserved[0] = 1u;
    assert(astra_interface_view_invalidate(&context, &control) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
}

static void test_popup_control(void)
{
    static const AstraChoiceItem items[] = {
        {"Name", 4u, {0u, 0u}},
        {"Date modified", 13u, {0u, 0u}}
    };
    AstraChoiceInfo info = ASTRA_CHOICE_INFO_INIT;
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraControlSize size = {0};
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent event;
    AstraSurfaceView surface = render_view(240u, 80u);
    int64_t value = -1;

    info.id = 91u;
    info.items = items;
    info.item_count = 2u;
    info.selected = 1u;
    assert(astra_interface_popup_init(&control, &info) == ASTRA_OK);
    assert(astra_interface_ui_init(&context, &control, 1u, 240u, 80u) ==
           ASTRA_OK);
    assert(astra_interface_ui_measure(&context, &control, &size) == ASTRA_OK);
    assert(size.width >= 104u && size.height > 0u);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    assert(astra_interface_ui_render(&context, &surface) == ASTRA_OK);
    event = pointer_event(ASTRA_WINDOW_EVENT_POINTER_BUTTON,
                          ASTRA_WINDOW_EVENT_DOWN,
                          control._private_frame.x + 8,
                          control._private_frame.y + 8);
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_NONE);
    event.flags = 0u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_ACTIVATE &&
           action.control_id == 91u && action.value == 1);
    assert(astra_interface_control_get_value(&control, &value, NULL) ==
           ASTRA_OK && value == 1);
    event = (AstraWindowEvent){0};
    event.size = sizeof(event);
    event.version = ASTRA_WINDOW_EVENT_VERSION;
    event.type = ASTRA_WINDOW_EVENT_KEY;
    event.flags = ASTRA_WINDOW_EVENT_DOWN;
    event.data.key.usage = 0x28u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_NONE);
    event.flags = 0u;
    assert(astra_interface_ui_handle_event(&context, &event, &action) ==
           ASTRA_OK && action.type == ASTRA_UI_ACTION_ACTIVATE &&
           action.control_id == 91u);
    assert(astra_interface_control_set_value(&context, &control, 0) ==
           ASTRA_OK);
    assert(astra_interface_control_get_value(&control, &value, NULL) ==
           ASTRA_OK && value == 0);
    assert(astra_interface_control_set_value(&context, &control, 2) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
}

static void test_split_button(void)
{
    AstraButtonInfo info = ASTRA_BUTTON_INFO_INIT;
    AstraControl controls[2] = { ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT };
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraControlSize size = {0};

    info.id = 92u;
    info.text = "Open";
    info.text_length = 4u;
    info.variant = ASTRA_BUTTON_TOOLBAR;
    assert(astra_interface_split_button_init(&controls[0], &controls[1],
                                             &info, 93u) == ASTRA_OK);
    assert(controls[0]._private_id == 92u &&
           controls[1]._private_id == 93u &&
           controls[1]._private_style == ASTRA_BUTTON_SPLIT_MENU);
    assert(astra_interface_ui_init(&context, controls, 2u, 240u, 80u) ==
           ASTRA_OK);
    assert(astra_interface_ui_measure(&context, &controls[1], &size) ==
           ASTRA_OK && size.width == 28u && size.height == 22u);
    assert(astra_interface_split_button_init(&controls[0], &controls[0],
                                             &info, 93u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
}

static void test_shared_file_panel(void)
{
    AstraDirectoryEntry entries[2] = {
        { .name = "Stories", .kind = ASTRA_VFS_KIND_DIRECTORY },
        { .name = "zork1.z3", .kind = ASTRA_VFS_KIND_FILE }
    };
    AstraDirectoryEntry next_entries[1] = {
        { .name = "planetfall.z3", .kind = ASTRA_VFS_KIND_FILE }
    };
    AstraCollectionItem items[2], next_items[2];
    AstraCollectionModel collection = ASTRA_COLLECTION_MODEL_INIT;
    AstraScrollModel scroll = ASTRA_SCROLL_MODEL_INIT;
    AstraScrollModelInfo scroll_info = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraTextModel name = ASTRA_TEXT_MODEL_INIT;
    AstraTextModelInfo text_info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraControl controls[ASTRA_FILE_PANEL_CONTROL_COUNT];
    AstraFilePanel panel = ASTRA_FILE_PANEL_INIT;
    AstraFilePanelInfo info = ASTRA_FILE_PANEL_INFO_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    uint8_t content[512] = {0};
    _Alignas(4) uint8_t metadata[1024] = {0};
    uint32_t outcome = 99u;

    scroll_info.line_width = 8u;
    scroll_info.line_height = 16u;
    assert(astra_scroll_init(&scroll, &scroll_info) == ASTRA_OK);
    info.first_id = 300u;
    info.mode = ASTRA_FILE_PANEL_OPEN;
    info.directory = "/dh0/Stories";
    info.entries = entries;
    info.entry_count = 2u;
    info.item_storage = items;
    info.item_capacity = 2u;
    info.collection = &collection;
    info.scroll = &scroll;
    assert(astra_interface_file_panel_init(&panel, controls, &info) == ASTRA_OK);
    assert(collection.item_count == 2u &&
           controls[ASTRA_FILE_PANEL_NAME]._private_state &
               ASTRA_CONTROL_COLLAPSED);
    assert(astra_interface_ui_init(&context, controls,
        ASTRA_FILE_PANEL_CONTROL_COUNT, 420u, 480u) == ASTRA_OK);
    layout.direction = ASTRA_FLEX_COLUMN;
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    action.type = ASTRA_UI_ACTION_ACTIVATE;
    action.control_id = 300u + ASTRA_FILE_PANEL_LIST;
    action.value = 0;
    assert(astra_interface_file_panel_action(&panel, &context, controls,
        &action, &outcome) == ASTRA_OK &&
        outcome == ASTRA_FILE_PANEL_NAVIGATE &&
        strcmp(panel.leaf, "Stories") == 0);
    action.value = 1;
    assert(astra_interface_file_panel_action(&panel, &context, controls,
        &action, &outcome) == ASTRA_OK &&
        outcome == ASTRA_FILE_PANEL_PICKED &&
        strcmp(panel.leaf, "zork1.z3") == 0);
    assert(astra_interface_file_panel_set_directory(&panel, &context,
        controls, "/dh0/Stories", next_entries, 1u, next_items, 2u) ==
        ASTRA_OK);
    assert(collection.item_count == 1u &&
           strcmp(panel.directory, "/dh0/Stories") == 0);
    action.control_id = 300u + ASTRA_FILE_PANEL_CANCEL;
    assert(astra_interface_file_panel_action(&panel, &context, controls,
        &action, &outcome) == ASTRA_OK &&
        outcome == ASTRA_FILE_PANEL_CANCELLED && panel.leaf[0] == '\0');

    text_info.content_arena = content;
    text_info.content_arena_bytes = sizeof(content);
    text_info.metadata_arena = metadata;
    text_info.metadata_arena_bytes = sizeof(metadata);
    text_info.text = "zork1.z3";
    text_info.text_bytes = 8u;
    assert(astra_text_model_init(&name, &text_info) == ASTRA_OK);
    context = (AstraUIContext)ASTRA_UI_CONTEXT_INIT;
    info.mode = ASTRA_FILE_PANEL_SAVE;
    info.name_model = &name;
    assert(astra_interface_file_panel_init(&panel, controls, &info) == ASTRA_OK);
    assert(controls[ASTRA_FILE_PANEL_NAME]._private_control_kind ==
           ASTRA_CONTROL_FIELD);
    assert(astra_interface_ui_init(&context, controls,
        ASTRA_FILE_PANEL_CONTROL_COUNT, 420u, 480u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    action.control_id = 300u + ASTRA_FILE_PANEL_PRIMARY;
    assert(astra_interface_file_panel_action(&panel, &context, controls,
        &action, &outcome) == ASTRA_OK &&
        outcome == ASTRA_FILE_PANEL_OVERWRITE &&
        strcmp(panel.leaf, "zork1.z3") == 0);
    assert(astra_text_model_replace(&name, 0u, 8u, "../oops", 7u) == ASTRA_OK);
    assert(astra_interface_file_panel_action(&panel, &context, controls,
        &action, &outcome) == ASTRA_ERROR_INVALID_ARGUMENT);
}

static void test_shared_colour_panel(void)
{
    AstraControl controls[ASTRA_COLOUR_PANEL_CONTROL_COUNT];
    AstraColourPanel panel = ASTRA_COLOUR_PANEL_INIT;
    AstraColorRGBA8 palette[2] = {
        { 4u, 8u, 12u, 255u }, { 220u, 30u, 60u, 255u }
    };
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    uint32_t outcome = 99u;

    assert(astra_interface_colour_panel_init(&panel, controls, 100u,
        ASTRA_COLOUR_PANEL_RGB565, 0xf800u, NULL, 0u) == ASTRA_OK);
    assert(controls[ASTRA_COLOUR_PANEL_RED]._private_maximum_value == 31 &&
           controls[ASTRA_COLOUR_PANEL_GREEN]._private_maximum_value == 63 &&
           controls[ASTRA_COLOUR_PANEL_BLUE]._private_maximum_value == 31);
    assert(astra_interface_ui_init(&context, controls,
        ASTRA_COLOUR_PANEL_CONTROL_COUNT, 320u, 480u) == ASTRA_OK);
    layout.direction = ASTRA_FLEX_COLUMN;
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    action.type = ASTRA_UI_ACTION_VALUE_CHANGED;
    action.control_id = 100u + ASTRA_COLOUR_PANEL_GREEN;
    action.value = 63;
    assert(astra_interface_control_set_value(&context,
        &controls[ASTRA_COLOUR_PANEL_GREEN], 63) == ASTRA_OK);
    assert(astra_interface_colour_panel_action(&panel, &context, controls,
        &action, &outcome) == ASTRA_OK);
    assert(outcome == ASTRA_COLOUR_PANEL_CHANGED && panel.packed == 0xffe0u);
    action.value = 64;
    assert(astra_interface_colour_panel_action(&panel, &context, controls,
        &action, &outcome) == ASTRA_ERROR_INVALID_ARGUMENT);
    assert(panel.packed == 0xffe0u);
    action.type = ASTRA_UI_ACTION_ACTIVATE;
    action.control_id = 100u + ASTRA_COLOUR_PANEL_USE;
    assert(astra_interface_colour_panel_action(&panel, &context, controls,
        &action, &outcome) == ASTRA_OK &&
        outcome == ASTRA_COLOUR_PANEL_ACCEPTED);

    context = (AstraUIContext)ASTRA_UI_CONTEXT_INIT;
    assert(astra_interface_colour_panel_init(&panel, controls, 100u,
        ASTRA_COLOUR_PANEL_XRGB8888, 0x00112233u, NULL, 0u) == ASTRA_OK);
    assert(controls[ASTRA_COLOUR_PANEL_RED]._private_value == 0x11 &&
           controls[ASTRA_COLOUR_PANEL_GREEN]._private_value == 0x22 &&
           controls[ASTRA_COLOUR_PANEL_BLUE]._private_value == 0x33);
    assert(astra_interface_ui_init(&context, controls,
        ASTRA_COLOUR_PANEL_CONTROL_COUNT, 320u, 480u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    action.type = ASTRA_UI_ACTION_VALUE_CHANGED;
    action.control_id = 100u + ASTRA_COLOUR_PANEL_BLUE;
    action.value = 255;
    assert(astra_interface_colour_panel_action(&panel, &context, controls,
        &action, &outcome) == ASTRA_OK);
    assert(panel.packed == 0x001122ffu);

    context = (AstraUIContext)ASTRA_UI_CONTEXT_INIT;
    assert(astra_interface_colour_panel_init(&panel, controls, 100u,
        ASTRA_COLOUR_PANEL_INDEX8, 1u, palette, 2u) == ASTRA_OK);
    assert(controls[ASTRA_COLOUR_PANEL_GREEN]._private_state &
           ASTRA_CONTROL_COLLAPSED);
    assert(astra_interface_ui_init(&context, controls,
        ASTRA_COLOUR_PANEL_CONTROL_COUNT, 320u, 480u) == ASTRA_OK);
    assert(astra_interface_ui_layout(&context, &layout) == ASTRA_OK);
    action.control_id = 100u + ASTRA_COLOUR_PANEL_RED;
    action.value = 0;
    assert(astra_interface_colour_panel_action(&panel, &context, controls,
        &action, &outcome) == ASTRA_OK && panel.packed == 0u);
    palette[1].alpha = 0u;
    assert(astra_interface_colour_panel_init(&panel, controls, 100u,
        ASTRA_COLOUR_PANEL_INDEX8, 1u, palette, 2u) ==
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
        alert_font_events = 1u;
        AstraResult result = astra_interface_show_alert(1u, &info);

        if (result != ASTRA_OK)
            fprintf(stderr, "alert result=%d\n", result);
        assert(result == ASTRA_OK);
    }
    assert(alert_create_calls == 1u && alert_name_calls == 1u &&
           alert_activate_calls == 1u && alert_close_calls == 1u);
    assert(alert_font_refreshes == 2u);
    alert_name_result = ASTRA_ERROR_IO;
    assert(astra_interface_show_alert(1u, &info) == ASTRA_ERROR_IO);
    assert(alert_create_calls == 2u && alert_name_calls == 2u &&
           alert_activate_calls == 1u && alert_close_calls == 2u);
    alert_name_result = ASTRA_OK;
    text_calls = 0u;
    {
        AstraSurfaceView surface = render_view(420u, 150u);
        AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;

        astra_interface_test_paint(&surface, &info);
        assert(text_calls == 2u);
        assert(first_text_color == astra_surface_rgb565(
            theme.client_text.red, theme.client_text.green,
            theme.client_text.blue));
        assert(first_text_color != astra_surface_rgb565(
            theme.client.red, theme.client.green, theme.client.blue));
    }
    test_context_request();
    test_controls();
    test_fault_label();
    test_label_installed_font();
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
    test_display_widgets();
    test_status_bar();
    test_segmented_control();
    test_list_control();
    test_popup_control();
    test_split_button();
    test_table_control();
    test_tree_control();
    test_pagination_control();
    test_icon_grid_control();
    test_scrolled_list_control();
    test_tab_control_and_collapsed_pages();
    test_stepper_control();
    test_animation_ownership();
    test_text_field();
    test_secure_and_search_fields();
    test_composed_fields_and_colour_well();
    test_shared_colour_panel();
    test_shared_file_panel();
    test_text_field_rejections();
    test_text_view();
    test_text_view_styles();
    test_text_view_document_capacity();
    test_text_view_wrap();
    test_text_view_flow_edit_find();
    test_grid_view();
    test_custom_view();
    test_scroll_view_layout_and_wheel();
    test_scroll_view_keyboard_listener();
    test_nested_scroll_chaining();
    test_scroll_view_rejects_non_single_child();
    test_splitter_composition();
    test_splitter_rejects_invalid_composition();
    test_horizontal_splitter();
    test_disclosure_control();
    test_toolbar_composition();
    test_toolbar_render();
    astra_interface_test_layout();
    return 0;
}
