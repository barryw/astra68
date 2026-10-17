#include <astra/control.h>
#include <astra/fonts.h>
#include <astra/graphics_kit.h>
#include <astra/interface_library.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/scroll.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/string.h>
#include <astra/surface.h>
#include <astra/theme.h>
#include <astra/window.h>

#include <stdint.h>
#include <string.h>

#define FONTS_WIDTH 700u
#define FONTS_HEIGHT 520u
#define FONTS_FACE_MAX 64u
#define FONTS_STRIKE_MAX 32u
#define FONTS_DETAIL_MAX (FONTS_FACE_MAX * FONTS_STRIKE_MAX)

enum {
    FONTS_TITLE = 1u,
    FONTS_HINT,
    FONTS_SCROLL,
    FONTS_LIST,
    FONTS_BACK,
    FONTS_CONTROL_COUNT = FONTS_BACK
};

enum {
    FONTS_FAIL_CATALOG = ASTRA_STATUS_PROGRAM_FIRST,
    FONTS_FAIL_CONTROL,
    FONTS_FAIL_SURFACE,
    FONTS_FAIL_WINDOW
};

ASTRA_PROGRAM("fonts", 0, 1, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static AstraFontsCatalog catalog = ASTRA_FONTS_CATALOG_INIT;
static AstraChoiceItem families[FONTS_FACE_MAX];
static uint32_t family_count;
static AstraChoiceItem *details;
static char (*detail_text)[160];
static uint32_t detail_capacity;
static uint32_t detail_count;
static uint32_t active_family = UINT32_MAX;
static uint32_t main_selection;
static AstraControl controls[FONTS_CONTROL_COUNT];
static AstraUIContext ui = ASTRA_UI_CONTEXT_INIT;
static AstraScrollModel scroll = ASTRA_SCROLL_MODEL_INIT;
static AstraSharedSurface surface;
static AstraWindow window = ASTRA_WINDOW_INIT;
static uint16_t client_width = FONTS_WIDTH;
static uint16_t client_height = FONTS_HEIGHT;

#define CONTROL(id) (&controls[(id) - 1u])

static uint32_t layout_controls(void)
{
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;

    layout.direction = ASTRA_FLEX_COLUMN;
    layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    layout.padding_left = 24u;
    layout.padding_top = 24u;
    layout.padding_right = 24u;
    layout.padding_bottom = 24u;
    layout.main_gap = 12u;
    return astra_interface_ui_layout(&ui, &layout) == ASTRA_OK ?
           ASTRA_STATUS_OK : FONTS_FAIL_CONTROL;
}

static int same_family(const AstraInstalledFont *face,
                       const AstraChoiceItem *family)
{
    return face->family_length == family->text_length &&
           memcmp(face->family, family->text, face->family_length) == 0;
}

static uint32_t collect_families(void)
{
    uint32_t strikes = 0u;

    if (catalog.face_count > FONTS_FACE_MAX) return FONTS_FAIL_CATALOG;
    for (uint32_t at = 0u; at < catalog.face_count; ++at) {
        AstraInstalledFont face;
        uint32_t match = 0u;

        if (astra_fonts_catalog_face(&catalog, at, &face) != ASTRA_OK)
            return FONTS_FAIL_CATALOG;
        if (face.strike_count > FONTS_STRIKE_MAX ||
            face.strike_count > FONTS_DETAIL_MAX - strikes)
            return FONTS_FAIL_CATALOG;
        strikes += face.strike_count;
        for (; match < family_count; ++match)
            if (same_family(&face, &families[match])) break;
        if (match != family_count) continue;
        families[family_count] = (AstraChoiceItem){
            face.family, face.family_length, {0u, 0u}
        };
        ++family_count;
    }
    if (strikes != 0u) {
        details = astra_runtime_allocate(
            (size_t)strikes * sizeof(*details));
        detail_text = astra_runtime_allocate(
            (size_t)strikes * sizeof(*detail_text));
        if (details == NULL || detail_text == NULL)
            return FONTS_FAIL_CATALOG;
    }
    detail_capacity = strikes;
    return ASTRA_STATUS_OK;
}

static uint32_t collect_details(uint32_t family_index)
{
    detail_count = 0u;
    if (family_index >= family_count) return FONTS_FAIL_CATALOG;
    for (uint32_t at = 0u; at < catalog.face_count; ++at) {
        AstraInstalledFont face;

        if (astra_fonts_catalog_face(&catalog, at, &face) != ASTRA_OK)
            return FONTS_FAIL_CATALOG;
        if (!same_family(&face, &families[family_index])) continue;
        if (face.strike_count > detail_capacity - detail_count)
            return FONTS_FAIL_CATALOG;
        for (uint32_t strike_at = 0u; strike_at < face.strike_count;
             ++strike_at) {
            AstraInstalledFontStrike strike;
            AstraString text;

            if (astra_fonts_catalog_strike(&catalog, at, strike_at,
                                           &strike) != ASTRA_OK)
                return FONTS_FAIL_CATALOG;
            astra_string_init(&text, detail_text[detail_count],
                              sizeof(detail_text[detail_count]));
            if (!astra_string_append_bytes(&text, face.style,
                                           face.style_length) ||
                !astra_string_append(&text, "  ·  ") ||
                (strike.pixel_width != 0u &&
                 (!astra_string_append_u64(&text, strike.pixel_width) ||
                  !astra_string_append_char(&text, 'x'))) ||
                !astra_string_append_u64(&text, strike.pixel_height) ||
                !astra_string_append(&text, " px"))
                return FONTS_FAIL_CATALOG;
            details[detail_count] = (AstraChoiceItem){
                text.text, text.length, {0u, 0u}
            };
            ++detail_count;
        }
    }
    return ASTRA_STATUS_OK;
}

static uint32_t build_controls(void)
{
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraScrollViewInfo view = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraChoiceInfo list = ASTRA_CHOICE_INFO_INIT;
    AstraButtonInfo back = ASTRA_BUTTON_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    const AstraChoiceItem *title = active_family == UINT32_MAX ? NULL :
                                   &families[active_family];

    label.id = FONTS_TITLE;
    label.text = title == NULL ? "Fonts" : title->text;
    label.text_length = title == NULL ? 5u : title->text_length;
    label.text_role = ASTRA_TEXT_CLIENT_PRIMARY;
    if (astra_interface_label_init(CONTROL(FONTS_TITLE), &label) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    label.id = FONTS_HINT;
    label.text = active_family == UINT32_MAX ?
                 (family_count == 0u ? "No fonts installed" :
                  "Double-click a family to see installed styles and sizes") :
                 "Installed styles and bitmap sizes";
    label.text_length = (uint32_t)strlen(label.text);
    label.text_role = ASTRA_TEXT_CLIENT_SECONDARY;
    if (astra_interface_label_init(CONTROL(FONTS_HINT), &label) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    view.id = FONTS_SCROLL;
    view.model = &scroll;
    view.preferred_width = FONTS_WIDTH - 48u;
    view.preferred_height = FONTS_HEIGHT - 160u;
    if (astra_interface_scroll_view_init(CONTROL(FONTS_SCROLL), &view) !=
        ASTRA_OK) return FONTS_FAIL_CONTROL;
    list.id = FONTS_LIST;
    list.items = active_family == UINT32_MAX ? families : details;
    list.item_count = active_family == UINT32_MAX ? family_count : detail_count;
    list.selected = list.item_count == 0u ? UINT32_MAX :
                    active_family == UINT32_MAX ? main_selection : 0u;
    if (astra_interface_list_init(CONTROL(FONTS_LIST), &list) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    back.id = FONTS_BACK;
    back.text = "Back to families";
    back.text_length = sizeof("Back to families") - 1u;
    back.state = active_family == UINT32_MAX ? ASTRA_CONTROL_COLLAPSED : 0u;
    if (astra_interface_button_init(CONTROL(FONTS_BACK), &back) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;

    item.flags = ASTRA_FLEX_BREAK_AFTER;
    if (astra_interface_control_set_flex(CONTROL(FONTS_TITLE), &item) != ASTRA_OK ||
        astra_interface_control_set_flex(CONTROL(FONTS_HINT), &item) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    item.grow = 1u;
    item.minimum_height = 120u;
    if (astra_interface_control_set_flex(CONTROL(FONTS_SCROLL), &item) !=
        ASTRA_OK) return FONTS_FAIL_CONTROL;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = FONTS_SCROLL;
    item.minimum_width = FONTS_WIDTH - 48u;
    if (astra_interface_control_set_flex(CONTROL(FONTS_LIST), &item) !=
        ASTRA_OK) return FONTS_FAIL_CONTROL;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    if (astra_interface_control_set_flex(CONTROL(FONTS_BACK), &item) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    if (astra_interface_ui_init(&ui, controls, FONTS_CONTROL_COUNT,
                                client_width, client_height) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    if (astra_scroll_set_offset(&scroll, 0u, 0u) != ASTRA_OK ||
        layout_controls() != ASTRA_STATUS_OK)
        return FONTS_FAIL_CONTROL;
    return ASTRA_STATUS_OK;
}

static uint32_t paint(void)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;

    if (!astra_draw_list_view_init(&surface.view, surface.view.pixels,
                                   surface.view.byte_size,
                                   client_width, client_height))
        return FONTS_FAIL_SURFACE;
    astra_surface_clear(&surface.view, astra_surface_rgb565(
        theme.client.red, theme.client.green, theme.client.blue));
    return astra_interface_ui_render(&ui, &surface.view) == ASTRA_OK ?
           ASTRA_STATUS_OK : FONTS_FAIL_CONTROL;
}

static uint32_t present(void)
{
    AstraWindowFrame frame = {0u, 0u, client_width, client_height};

    if (paint() != ASTRA_STATUS_OK ||
        astra_window_present_region(&window, &frame) != ASTRA_OK)
        return FONTS_FAIL_WINDOW;
    astra_interface_ui_damage_clear(&ui);
    return ASTRA_STATUS_OK;
}

static uint32_t action(const AstraUIAction *event)
{
    if (event->control_id == FONTS_LIST &&
        event->type == ASTRA_UI_ACTION_VALUE_CHANGED &&
        active_family == UINT32_MAX && event->value >= 0 &&
        (uint64_t)event->value < family_count)
        main_selection = (uint32_t)event->value;
    if (event->type == ASTRA_UI_ACTION_ACTIVATE &&
        event->control_id == FONTS_LIST && active_family == UINT32_MAX) {
        if (event->value < 0 || (uint64_t)event->value >= family_count)
            return FONTS_FAIL_CONTROL;
        main_selection = (uint32_t)event->value;
        if (collect_details(main_selection) != ASTRA_STATUS_OK)
            return FONTS_FAIL_CATALOG;
        active_family = main_selection;
        if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
        return present();
    }
    if (event->type == ASTRA_UI_ACTION_ACTIVATE &&
        event->control_id == FONTS_BACK) {
        active_family = UINT32_MAX;
        if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
        return present();
    }
    return ASTRA_STATUS_OK;
}

static uint32_t run(void)
{
    for (;;) {
        AstraWindowEvent event = {0};
        AstraResult result = astra_window_event_wait(
            &window, &event, ASTRA_DEADLINE_INFINITE);

        if (result != ASTRA_OK) return FONTS_FAIL_WINDOW;
        do {
            AstraUIAction reply = ASTRA_UI_ACTION_INIT;

            if (event.type == ASTRA_WINDOW_EVENT_CLOSE_REQUEST)
                return ASTRA_STATUS_OK;
            if (astra_interface_ui_handle_event(&ui, &event, &reply) != ASTRA_OK)
                return FONTS_FAIL_CONTROL;
            if (event.type == ASTRA_WINDOW_EVENT_RESIZE &&
                event.data.resize.width != 0u &&
                event.data.resize.height != 0u) {
                client_width = event.data.resize.width;
                client_height = event.data.resize.height;
                if (layout_controls() != ASTRA_STATUS_OK)
                    return FONTS_FAIL_CONTROL;
            }
            if (action(&reply) != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
            result = astra_window_event_try(&window, &event);
        } while (result == ASTRA_OK);
        if (result != ASTRA_ERROR_WOULD_BLOCK) return FONTS_FAIL_WINDOW;
        if (astra_interface_ui_damage(&ui, &(AstraControlFrame){0}) == ASTRA_OK &&
            present() != ASTRA_STATUS_OK) return FONTS_FAIL_WINDOW;
    }
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *gui;
    const AstraStartupCapability *fonts;
    const AstraStartupCapability *bootstrap;
    AstraScrollModelInfo scroll_info = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraWindowCreateInfo window_info = ASTRA_WINDOW_CREATE_INFO_INIT;
    uint32_t status = ASTRA_STATUS_OK;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    gui = astra_startup_capability(startup, ASTRA_CAPABILITY_GUI);
    fonts = astra_startup_capability(startup, ASTRA_CAPABILITY_FONTS);
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    if (gui == NULL || fonts == NULL) return ASTRA_STATUS_BAD_HANDLE;
    if (astra_fonts_catalog_open(fonts->handle, &catalog) != ASTRA_OK ||
        collect_families() != ASTRA_STATUS_OK)
        status = FONTS_FAIL_CATALOG;
    scroll_info.line_height = 32u;
    if (status == ASTRA_STATUS_OK &&
        astra_scroll_init(&scroll, &scroll_info) != ASTRA_OK)
        status = FONTS_FAIL_CONTROL;
    if (status == ASTRA_STATUS_OK) status = build_controls();
    if (status == ASTRA_STATUS_OK &&
        astra_shared_draw_list_create(&surface, client_width,
                                      client_height) != ASTRA_SYSCALL_OK)
        status = FONTS_FAIL_SURFACE;
    if (status == ASTRA_STATUS_OK) status = paint();
    if (status == ASTRA_STATUS_OK) {
        window_info.flags = ASTRA_WINDOW_RESIZABLE;
        window_info.x = 460u;
        window_info.y = 210u;
        window_info.width = client_width;
        window_info.height = client_height;
        window_info.content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST;
        window_info.type = ASTRA_WINDOW_STANDARD;
        window_info.title = "Fonts";
        window_info.title_length = 5u;
        window_info.event_mask = ASTRA_WINDOW_SUBSCRIBE_DEFAULT |
                                 ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION |
                                 ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON |
                                 ASTRA_WINDOW_SUBSCRIBE_POINTER_WHEEL |
                                 ASTRA_WINDOW_SUBSCRIBE_KEY;
        if (astra_window_create(gui->handle, surface.area,
                                &window_info, &window) != ASTRA_OK ||
            astra_window_set_application_name(&window, "Fonts", 5u) !=
                ASTRA_OK || astra_window_activate(&window) != ASTRA_OK)
            status = FONTS_FAIL_WINDOW;
    }
    if (bootstrap != NULL) {
        (void)astra_service_ready(bootstrap->handle, status, NULL, 0u);
        (void)astra_close(bootstrap->handle);
    }
    if (status == ASTRA_STATUS_OK) status = run();
    if (window._private_control != ASTRA_INVALID_HANDLE &&
        astra_window_close(&window) != ASTRA_OK &&
        status == ASTRA_STATUS_OK)
        status = FONTS_FAIL_WINDOW;
    if (surface.area != ASTRA_INVALID_HANDLE &&
        astra_shared_surface_close(&surface) != ASTRA_SYSCALL_OK &&
        status == ASTRA_STATUS_OK)
        status = FONTS_FAIL_SURFACE;
    if (catalog.generation != 0u &&
        astra_fonts_catalog_close(&catalog) != ASTRA_OK &&
        status == ASTRA_STATUS_OK)
        status = FONTS_FAIL_CATALOG;
    astra_runtime_deallocate(detail_text);
    astra_runtime_deallocate(details);
    return (int)status;
}
