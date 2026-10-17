#include <astra/control.h>
#include <astra/fonts.h>
#include <astra/filesystem_library.h>
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
#include <astra/utf8.h>
#include <astra/vfs_process.h>
#include <astra/window.h>

#include <stdint.h>
#include <string.h>

#define FONTS_WIDTH 700u
#define FONTS_HEIGHT 520u
#define FONTS_FACE_MAX 64u
#define FONTS_STRIKE_MAX 32u
#define FONTS_DETAIL_MAX (FONTS_FACE_MAX * FONTS_STRIKE_MAX)
/* shortcut: fixed browser page; add paging before folders exceed 128 entries. */
#define FONTS_BROWSER_MAX 128u

enum {
    FONTS_TITLE = 1u,
    FONTS_HINT,
    FONTS_SCROLL,
    FONTS_LIST,
    FONTS_BACK,
    FONTS_USE,
    FONTS_IMPORT,
    FONTS_ENCODING,
    FONTS_UNINSTALL,
    FONTS_CONTROL_COUNT = FONTS_UNINSTALL
};

enum {
    FONTS_FAIL_CATALOG = ASTRA_STATUS_PROGRAM_FIRST,
    FONTS_FAIL_CONTROL,
    FONTS_FAIL_SURFACE,
    FONTS_FAIL_WINDOW,
    FONTS_FAIL_FILESYSTEM
};

ASTRA_PROGRAM("fonts", 0, 1, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static AstraFontsCatalog catalog = ASTRA_FONTS_CATALOG_INIT;
static const char system_font_requirements[] =
    "System fonts need all UI sizes and basic text characters";
static AstraChoiceItem families[FONTS_FACE_MAX];
static uint32_t family_count;
static AstraChoiceItem *details;
static char *detail_text;
static uint32_t detail_text_stride;
static uint32_t *detail_faces;
static uint32_t detail_capacity;
static uint32_t detail_count;
static uint32_t active_family = UINT32_MAX;
static uint32_t main_selection;
static uint32_t detail_selection;
static AstraHandle fonts_read;
static AstraHandle fonts_admin;
static const char *message;
static AstraProcessFilesystem files = ASTRA_PROCESS_FILESYSTEM_INIT;
static AstraDirectoryEntry browser_nodes[FONTS_BROWSER_MAX];
static AstraChoiceItem browser_items[FONTS_BROWSER_MAX];
static uint32_t browser_count;
static uint32_t browser_selection;
static uint32_t browsing;
static uint32_t amiga_encoding;
static uint32_t confirm_uninstall;
static char browser_path[ASTRA_VFS_PATH_MAX] = "/dh0";
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
    uint32_t longest_style = 0u;

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
        if (face.style_length > longest_style)
            longest_style = face.style_length;
        for (; match < family_count; ++match)
            if (same_family(&face, &families[match])) break;
        if (match != family_count) continue;
        families[family_count] = (AstraChoiceItem){
            face.family, face.family_length, {0u, 0u}
        };
        ++family_count;
    }
    uint64_t stride = (uint64_t)longest_style +
        sizeof("Cannot be system font  ·  65535x65535 px  ·  ");
    if (stride > UINT32_MAX || (uint64_t)strikes * stride > SIZE_MAX)
        return FONTS_FAIL_CATALOG;
    detail_text_stride = (uint32_t)stride;
    if (strikes != 0u) {
        details = astra_runtime_allocate(
            (size_t)strikes * sizeof(*details));
        detail_text = astra_runtime_allocate(
            (size_t)strikes * detail_text_stride);
        detail_faces = astra_runtime_allocate(
            (size_t)strikes * sizeof(*detail_faces));
        if (details == NULL || detail_text == NULL || detail_faces == NULL)
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
            astra_string_init(&text,
                              detail_text + (size_t)detail_count * detail_text_stride,
                              detail_text_stride);
            if (!astra_string_append(&text,
                    (face.flags & ASTRA_INSTALLED_FONT_SELECTABLE) != 0u ?
                    "Can be system font" : "Cannot be system font") ||
                !astra_string_append(&text, "  ·  ") ||
                (strike.pixel_width != 0u &&
                 (!astra_string_append_u64(&text, strike.pixel_width) ||
                  !astra_string_append_char(&text, 'x'))) ||
                !astra_string_append_u64(&text, strike.pixel_height) ||
                !astra_string_append(&text, " px  ·  ") ||
                !astra_string_append_bytes(&text, face.style,
                                           face.style_length))
                return FONTS_FAIL_CATALOG;
            details[detail_count] = (AstraChoiceItem){
                text.text, text.length, {0u, 0u}
            };
            detail_faces[detail_count] = at;
            ++detail_count;
        }
    }
    return ASTRA_STATUS_OK;
}

static uint32_t browser_load(void)
{
    AstraDirectory directory = ASTRA_DIRECTORY_INIT;
    uint32_t status = astra_filesystem_directory_open(
        &files.filesystem, browser_path, &directory);

    browser_count = 0u;
    browser_selection = 0u;
    if (status != ASTRA_VFS_OK) return status;
    while (status == ASTRA_VFS_OK) {
        AstraDirectoryEntry node;
        uint32_t count = 0u;

        status = astra_filesystem_directory_read(&directory, &node, 1u,
                                                 &count);
        if (status != ASTRA_VFS_OK || count == 0u) break;
        const char *end = memchr(node.name, '\0', sizeof(node.name));
        if (end == NULL || end == node.name ||
            !astra_utf8_validate(node.name,
                (uint32_t)(end - node.name), 0u) ||
            memchr(node.name, '/', (size_t)(end - node.name)) != NULL ||
            strcmp(node.name, ".") == 0 || strcmp(node.name, "..") == 0 ||
            (node.kind != ASTRA_VFS_KIND_FILE &&
             node.kind != ASTRA_VFS_KIND_DIRECTORY))
            continue;
        if (browser_count == FONTS_BROWSER_MAX) {
            status = ASTRA_VFS_ERR_LIMIT;
            break;
        }
        browser_nodes[browser_count] = node;
        browser_items[browser_count] = (AstraChoiceItem){
            browser_nodes[browser_count].name,
            (uint32_t)strlen(node.name), {0u, 0u}
        };
        ++browser_count;
    }
    astra_filesystem_directory_close(&directory);
    if (status != ASTRA_VFS_OK) browser_count = 0u;
    return status;
}

static uint32_t browser_enter(void)
{
    const AstraDirectoryEntry *node;
    size_t length = strlen(browser_path);
    size_t name_length;

    if (browser_selection >= browser_count) return ASTRA_VFS_ERR_INVALID;
    node = &browser_nodes[browser_selection];
    if (node->kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_INVALID;
    name_length = strlen(node->name);
    if (length + name_length + 2u > sizeof(browser_path))
        return ASTRA_VFS_ERR_LIMIT;
    browser_path[length] = '/';
    memcpy(browser_path + length + 1u, node->name, name_length + 1u);
    uint32_t status = browser_load();
    if (status != ASTRA_VFS_OK) {
        browser_path[length] = '\0';
        (void)browser_load();
    }
    return status;
}

static AstraResult browser_import(void)
{
    AstraFile file = ASTRA_FILE_INIT;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    const AstraDirectoryEntry *node;
    char path[ASTRA_VFS_PATH_MAX];
    char id[ASTRA_FONT_ID_MAX];
    void *bytes;
    uint32_t length = 0u;
    uint32_t status;
    uint32_t opened;
    size_t prefix_length;
    AstraResult result;

    if (browser_selection >= browser_count) return ASTRA_ERROR_INVALID_ARGUMENT;
    node = &browser_nodes[browser_selection];
    if (node->kind != ASTRA_VFS_KIND_FILE) return ASTRA_ERROR_INVALID_ARGUMENT;
    length = (uint32_t)strlen(node->name);
    if (length == 0u || length >= ASTRA_FONT_IMPORT_NAME_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    prefix_length = strlen(browser_path);
    if (prefix_length + length + 2u > sizeof(path))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    memcpy(path, browser_path, prefix_length);
    path[prefix_length] = '/';
    memcpy(path + prefix_length + 1u, node->name, length + 1u);
    status = astra_filesystem_open(&files.filesystem, path,
                                   ASTRA_VFS_OPEN_READ, &file);
    opened = status == ASTRA_VFS_OK;
    if (status == ASTRA_VFS_OK)
        status = astra_filesystem_file_info(&file, &info);
    if (opened) {
        uint32_t closed = astra_filesystem_close(&file);
        if (status == ASTRA_VFS_OK) status = closed;
    }
    if (status != ASTRA_VFS_OK) return ASTRA_ERROR_IO;
    if (info.kind != ASTRA_VFS_KIND_FILE || info.byte_size == 0u ||
        info.byte_size > ASTRA_FONT_IMPORT_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    bytes = astra_runtime_allocate((size_t)info.byte_size);
    if (bytes == NULL) return ASTRA_ERROR_OUT_OF_MEMORY;
    status = astra_process_read_file(&files, path, bytes,
                                     (uint32_t)info.byte_size, &length);
    if (status == ASTRA_VFS_OK && length == info.byte_size && length >= 4u &&
        memcmp(bytes, "\0\0\3\363", 4u) == 0 &&
        amiga_encoding == ASTRA_FONT_ENCODING_AUTO) {
        message = "Choose Amiga encoding before import";
        result = ASTRA_ERROR_INVALID_ARGUMENT;
    } else if (status == ASTRA_VFS_OK && length == info.byte_size) {
        uint32_t encoding = length >= 4u &&
            memcmp(bytes, "\0\0\3\363", 4u) == 0 ? amiga_encoding :
            ASTRA_FONT_ENCODING_AUTO;
        result = astra_fonts_import(fonts_admin, bytes, length, node->name,
                                    (uint32_t)strlen(node->name), encoding, id);
    } else result = ASTRA_ERROR_IO;
    astra_runtime_deallocate(bytes);
    return result;
}

static uint32_t build_controls(void)
{
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraScrollViewInfo view = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraChoiceInfo list = ASTRA_CHOICE_INFO_INIT;
    AstraButtonInfo back = ASTRA_BUTTON_INFO_INIT;
    AstraButtonInfo use = ASTRA_BUTTON_INFO_INIT;
    AstraButtonInfo import = ASTRA_BUTTON_INFO_INIT;
    AstraButtonInfo encoding = ASTRA_BUTTON_INFO_INIT;
    AstraButtonInfo uninstall = ASTRA_BUTTON_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    const AstraChoiceItem *title = active_family == UINT32_MAX ? NULL :
                                   &families[active_family];
    AstraInstalledFont selected_face;
    uint32_t system_ready = 0u;

    if (!browsing && active_family != UINT32_MAX &&
        detail_selection < detail_count) {
        if (astra_fonts_catalog_face(&catalog,
            detail_faces[detail_selection], &selected_face) != ASTRA_OK)
            return FONTS_FAIL_CATALOG;
        system_ready = (selected_face.flags &
                        ASTRA_INSTALLED_FONT_SELECTABLE) != 0u;
    }

    label.id = FONTS_TITLE;
    label.text = browsing ? browser_path : title == NULL ? "Fonts" :
                 title->text;
    label.text_length = browsing ? (uint32_t)strlen(browser_path) :
                        title == NULL ? 5u : title->text_length;
    label.text_role = ASTRA_TEXT_CLIENT_PRIMARY;
    if (astra_interface_label_init(CONTROL(FONTS_TITLE), &label) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    label.id = FONTS_HINT;
    label.text = message != NULL ? message : confirm_uninstall ?
                 "Remove this imported style and all its sizes? Source file remains." : browsing ?
                 "Open a folder or select a font file to import" :
                 active_family == UINT32_MAX ?
                 (family_count == 0u ? "No fonts installed" :
                  "Double-click a family to see installed styles and sizes") :
                 system_ready ?
                 "Select a style, then use it for system text" :
                 system_font_requirements;
    label.text_length = (uint32_t)strlen(label.text);
    label.text_role = ASTRA_TEXT_CLIENT_SECONDARY;
    if (astra_interface_label_init(CONTROL(FONTS_HINT), &label) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    view.id = FONTS_SCROLL;
    view.model = &scroll;
    view.preferred_width = FONTS_WIDTH - 48u;
    view.preferred_height = FONTS_HEIGHT - 160u;
    view.state = confirm_uninstall ? ASTRA_CONTROL_COLLAPSED : 0u;
    if (astra_interface_scroll_view_init(CONTROL(FONTS_SCROLL), &view) !=
        ASTRA_OK) return FONTS_FAIL_CONTROL;
    list.id = FONTS_LIST;
    list.items = browsing ? browser_items :
                 active_family == UINT32_MAX ? families : details;
    list.item_count = browsing ? browser_count :
                      active_family == UINT32_MAX ? family_count : detail_count;
    list.selected = list.item_count == 0u ? UINT32_MAX :
                    browsing ? browser_selection :
                    active_family == UINT32_MAX ? main_selection : detail_selection;
    list.state = confirm_uninstall ? ASTRA_CONTROL_COLLAPSED : 0u;
    if (astra_interface_list_init(CONTROL(FONTS_LIST), &list) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    back.id = FONTS_BACK;
    back.text = confirm_uninstall ? "Cancel" :
                browsing ? "Up" : "Back to families";
    back.text_length = (uint32_t)strlen(back.text);
    back.state = !browsing && active_family == UINT32_MAX ?
                 ASTRA_CONTROL_COLLAPSED : 0u;
    if (astra_interface_button_init(CONTROL(FONTS_BACK), &back) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    use.id = FONTS_USE;
    use.text = browsing ? "Import selected font" : "Use as system font";
    use.text_length = (uint32_t)strlen(use.text);
    use.state = confirm_uninstall ||
                (!browsing && active_family == UINT32_MAX) ?
                ASTRA_CONTROL_COLLAPSED :
                !browsing && !system_ready ? ASTRA_CONTROL_DISABLED : 0u;
    if (astra_interface_button_init(CONTROL(FONTS_USE), &use) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    import.id = FONTS_IMPORT;
    import.text = "Import font…";
    import.text_length = (uint32_t)strlen(import.text);
    import.state = browsing || active_family != UINT32_MAX ?
                   ASTRA_CONTROL_COLLAPSED : 0u;
    if (astra_interface_button_init(CONTROL(FONTS_IMPORT), &import) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    encoding.id = FONTS_ENCODING;
    encoding.text = amiga_encoding == ASTRA_FONT_ENCODING_ISO_8859_1 ?
        "Amiga encoding: ISO-8859-1" :
        amiga_encoding == ASTRA_FONT_ENCODING_ISO_8859_2 ?
        "Amiga encoding: ISO-8859-2" : "Choose Amiga encoding";
    encoding.text_length = (uint32_t)strlen(encoding.text);
    encoding.state = browsing ? 0u : ASTRA_CONTROL_COLLAPSED;
    if (astra_interface_button_init(CONTROL(FONTS_ENCODING), &encoding) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    uninstall.id = FONTS_UNINSTALL;
    uninstall.text = confirm_uninstall ? "Remove font" : "Uninstall…";
    uninstall.text_length = (uint32_t)strlen(uninstall.text);
    uninstall.state = browsing || active_family == UINT32_MAX ?
                      ASTRA_CONTROL_COLLAPSED : 0u;
    if (astra_interface_button_init(CONTROL(FONTS_UNINSTALL), &uninstall) !=
        ASTRA_OK) return FONTS_FAIL_CONTROL;

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
    if (astra_interface_control_set_flex(CONTROL(FONTS_USE), &item) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    if (astra_interface_control_set_flex(CONTROL(FONTS_IMPORT), &item) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    if (astra_interface_control_set_flex(CONTROL(FONTS_ENCODING), &item) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    if (astra_interface_control_set_flex(CONTROL(FONTS_UNINSTALL), &item) !=
        ASTRA_OK) return FONTS_FAIL_CONTROL;
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

static uint32_t reload_catalog(void)
{
    AstraFontsCatalog fresh = ASTRA_FONTS_CATALOG_INIT;

    if (astra_fonts_catalog_open(fonts_read, &fresh) != ASTRA_OK)
        return FONTS_FAIL_CATALOG;
    if (astra_fonts_catalog_close(&catalog) != ASTRA_OK) {
        AstraResult ignored = astra_fonts_catalog_close(&fresh);
        (void)ignored;
        return FONTS_FAIL_CATALOG;
    }
    astra_runtime_deallocate(details);
    astra_runtime_deallocate(detail_text);
    astra_runtime_deallocate(detail_faces);
    details = NULL;
    detail_text = NULL;
    detail_faces = NULL;
    detail_capacity = 0u;
    detail_count = 0u;
    family_count = 0u;
    main_selection = 0u;
    active_family = UINT32_MAX;
    browsing = 0u;
    confirm_uninstall = 0u;
    catalog = fresh;
    return collect_families();
}

static uint32_t import_selected(void)
{
    message = NULL;
    AstraResult result = browser_import();

    if (result == ASTRA_OK) {
        if (reload_catalog() != ASTRA_STATUS_OK) return FONTS_FAIL_CATALOG;
        message = "Font imported";
    } else if (message == NULL) {
        message = result == ASTRA_ERROR_UNSUPPORTED ?
                  "This font format is not supported yet" :
                  result == ASTRA_ERROR_INVALID_ARGUMENT ?
                  "Select a valid font file smaller than 32 MiB" :
                  "Could not import font file";
    }
    if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
    uint32_t status = present();
    if (status == ASTRA_STATUS_OK && result == ASTRA_OK)
        (void)astra_log("fonts UI import complete");
    return status;
}

static uint32_t action(const AstraUIAction *event)
{
    if (browsing) {
        if (event->type == ASTRA_UI_ACTION_ACTIVATE &&
            event->control_id == FONTS_ENCODING) {
            amiga_encoding = amiga_encoding == ASTRA_FONT_ENCODING_ISO_8859_1 ?
                ASTRA_FONT_ENCODING_ISO_8859_2 :
                ASTRA_FONT_ENCODING_ISO_8859_1;
            message = NULL;
            if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
            return present();
        }
        if (event->control_id == FONTS_LIST &&
            (event->type == ASTRA_UI_ACTION_VALUE_CHANGED ||
             event->type == ASTRA_UI_ACTION_ACTIVATE) &&
            event->value >= 0 && (uint64_t)event->value < browser_count)
            browser_selection = (uint32_t)event->value;
        if (event->type == ASTRA_UI_ACTION_ACTIVATE &&
            event->control_id == FONTS_LIST &&
            browser_selection < browser_count) {
            if (event->value < 0 || (uint64_t)event->value >= browser_count)
                return FONTS_FAIL_CONTROL;
            if (browser_nodes[browser_selection].kind == ASTRA_VFS_KIND_FILE)
                return import_selected();
            message = browser_enter() == ASTRA_VFS_OK ? NULL :
                      "Could not open folder";
            if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
            return present();
        }
        if (event->type == ASTRA_UI_ACTION_ACTIVATE &&
            event->control_id == FONTS_USE)
            return import_selected();
        if (event->type == ASTRA_UI_ACTION_ACTIVATE &&
            event->control_id == FONTS_BACK) {
            if (strcmp(browser_path, "/dh0") == 0) {
                browsing = 0u;
                message = NULL;
            } else {
                size_t at = strlen(browser_path);
                while (at > 4u && browser_path[at] != '/') --at;
                if (browser_path[at] != '/')
                    return FONTS_FAIL_CONTROL;
                browser_path[at] = '\0';
                message = browser_load() == ASTRA_VFS_OK ? NULL :
                          "Could not open parent folder";
            }
            if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
            return present();
        }
        return ASTRA_STATUS_OK;
    }
    if (event->type == ASTRA_UI_ACTION_ACTIVATE &&
        event->control_id == FONTS_IMPORT) {
        browsing = 1u;
        message = browser_load() == ASTRA_VFS_OK ? NULL :
                  "Could not open DH0";
        if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
        return present();
    }
    if (confirm_uninstall && event->type == ASTRA_UI_ACTION_ACTIVATE &&
        event->control_id == FONTS_BACK) {
        confirm_uninstall = 0u;
        message = NULL;
        if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
        return present();
    }
    if (event->type == ASTRA_UI_ACTION_ACTIVATE &&
        event->control_id == FONTS_UNINSTALL) {
        AstraInstalledFont face;

        if (detail_selection >= detail_count ||
            astra_fonts_catalog_face(&catalog,
                detail_faces[detail_selection], &face) != ASTRA_OK)
            return FONTS_FAIL_CATALOG;
        if ((face.flags & ASTRA_INSTALLED_FONT_SYSTEM) != 0u) {
            message = "Built-in fonts cannot be removed";
            if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
            return present();
        }
        if (!confirm_uninstall) {
            confirm_uninstall = 1u;
            message = NULL;
        } else {
            AstraResult result = astra_fonts_uninstall(
                fonts_admin, face.id, face.id_length);
            confirm_uninstall = 0u;
            if (result == ASTRA_OK) {
                if (reload_catalog() != ASTRA_STATUS_OK)
                    return FONTS_FAIL_CATALOG;
                message = "Font removed";
            } else
                message = result == ASTRA_ERROR_BUSY ?
                    "Choose another system font before removing this one" :
                    "Could not remove font";
        }
        if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
        return present();
    }
    if (event->control_id == FONTS_LIST &&
        event->type == ASTRA_UI_ACTION_VALUE_CHANGED &&
        active_family != UINT32_MAX && event->value >= 0 &&
        (uint64_t)event->value < detail_count) {
        AstraInstalledFont face;
        const char *hint;

        detail_selection = (uint32_t)event->value;
        if (astra_fonts_catalog_face(&catalog,
            detail_faces[detail_selection], &face) != ASTRA_OK)
            return FONTS_FAIL_CATALOG;
        uint32_t ready = (face.flags & ASTRA_INSTALLED_FONT_SELECTABLE) != 0u;
        hint = ready ? "Select a style, then use it for system text" :
                       system_font_requirements;
        message = NULL;
        if (astra_interface_ui_set_state(&ui, CONTROL(FONTS_USE),
                ready ? 0u : ASTRA_CONTROL_DISABLED) != ASTRA_OK ||
            astra_interface_control_set_text(&ui, CONTROL(FONTS_HINT), hint,
                (uint32_t)strlen(hint)) != ASTRA_OK)
            return FONTS_FAIL_CONTROL;
        return present();
    }
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
        detail_selection = 0u;
        confirm_uninstall = 0u;
        message = NULL;
        if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
        return present();
    }
    if (event->type == ASTRA_UI_ACTION_ACTIVATE &&
        event->control_id == FONTS_BACK) {
        active_family = UINT32_MAX;
        confirm_uninstall = 0u;
        message = NULL;
        if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
        return present();
    }
    if (event->type == ASTRA_UI_ACTION_ACTIVATE &&
        event->control_id == FONTS_USE) {
        AstraInstalledFont face;
        AstraResult result;

        if (detail_selection >= detail_count ||
            astra_fonts_catalog_face(&catalog,
                detail_faces[detail_selection], &face) != ASTRA_OK)
            return FONTS_FAIL_CATALOG;
        if ((face.flags & ASTRA_INSTALLED_FONT_SELECTABLE) == 0u)
            return ASTRA_STATUS_OK;
        result = astra_fonts_select(fonts_admin, face.id, face.id_length);
        message = result == ASTRA_OK ? "System font choice saved" :
                  result == ASTRA_ERROR_UNSUPPORTED ?
                  "Font lacks required system sizes or text characters" :
                  "Could not save system font choice";
        if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
        return present();
    }
    return ASTRA_STATUS_OK;
}

static uint32_t run(AstraHandle gui)
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
            if (event.type == ASTRA_WINDOW_EVENT_FONT_CHANGED) {
                AstraResult refreshed = astra_fonts_use_system(gui, &event);
                if (refreshed != ASTRA_OK)
                    (void)astra_log_failure("fonts UI font refresh", (uint32_t)refreshed);
            }
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
    const AstraStartupCapability *admin;
    const AstraStartupCapability *bootstrap;
    AstraScrollModelInfo scroll_info = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraWindowCreateInfo window_info = ASTRA_WINDOW_CREATE_INFO_INIT;
    uint32_t status = ASTRA_STATUS_OK;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    gui = astra_startup_capability(startup, ASTRA_CAPABILITY_GUI);
    fonts = astra_startup_capability(startup, ASTRA_CAPABILITY_FONTS);
    admin = astra_startup_capability(startup, ASTRA_CAPABILITY_FONTS_ADMIN);
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    if (gui == NULL || fonts == NULL || admin == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    fonts_read = fonts->handle;
    fonts_admin = admin->handle;
    if (astra_fonts_use_system(gui->handle, NULL) != ASTRA_OK)
        return FONTS_FAIL_CATALOG;
    if (astra_process_filesystem_open(&files, startup) != ASTRA_VFS_OK)
        status = FONTS_FAIL_FILESYSTEM;
    if (status == ASTRA_STATUS_OK &&
        (astra_fonts_catalog_open(fonts_read, &catalog) != ASTRA_OK ||
         collect_families() != ASTRA_STATUS_OK))
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
    if (status == ASTRA_STATUS_OK) status = run(gui->handle);
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
    astra_runtime_deallocate(detail_faces);
    astra_runtime_deallocate(details);
    astra_process_filesystem_close(&files);
    return (int)status;
}
