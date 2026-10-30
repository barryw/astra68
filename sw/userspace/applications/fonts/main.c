#include <astra/control.h>
#include <astra/clipboard.h>
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
#include <astra/system_panels.h>
#include <astra/theme.h>
#include <astra/utf8.h>
#include <astra/vfs_process.h>
#include <astra/volume.h>
#include <astra/window.h>

#include "../../common/directory_snapshot.h"

#include <stdint.h>
#include <string.h>

#define FONTS_WIDTH 700u
#define FONTS_HEIGHT 660u
#define FONTS_FACE_MAX 64u
#define FONTS_STRIKE_MAX 32u
#define FONTS_DETAIL_MAX (FONTS_FACE_MAX * FONTS_STRIKE_MAX)

enum {
    FONTS_TITLE = 1u,
    FONTS_HINT,
    FONTS_BODY,
    FONTS_ACTIONS,
    FONTS_PREVIEW,
    FONTS_SCROLL,
    FONTS_LIST,
    FONTS_PROGRESS,
    FONTS_BACK,
    FONTS_USE,
    FONTS_IMPORT,
    FONTS_ENCODING,
    FONTS_INFORMATION,
    FONTS_UNINSTALL,
    FONTS_FILE_PANEL_FIRST,
    FONTS_CONTROL_COUNT = FONTS_FILE_PANEL_FIRST +
        ASTRA_FILE_PANEL_CONTROL_COUNT - 1u
};

_Static_assert(FONTS_HINT == FONTS_TITLE + ASTRA_SETTINGS_PANE_DESCRIPTION &&
    FONTS_BODY == FONTS_TITLE + ASTRA_SETTINGS_PANE_BODY &&
    FONTS_ACTIONS == FONTS_TITLE + ASTRA_SETTINGS_PANE_ACTIONS,
    "Fonts shell IDs must match the shared settings composition");

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
static AstraCollectionItem families[FONTS_FACE_MAX];
static uint32_t family_count;
static AstraCollectionItem *details;
static char *detail_text;
static uint32_t detail_text_stride;
typedef struct FontDetail { uint32_t face; uint32_t strike; } FontDetail;
static FontDetail *detail_sources;
static uint32_t detail_capacity;
static uint32_t detail_count;
static uint32_t active_family = UINT32_MAX;
static uint32_t main_selection;
static uint32_t detail_selection;
static AstraHandle fonts_read;
static AstraHandle fonts_admin;
static AstraHandle fonts_gui;
static const char *message;
static AstraProcessFilesystem files = ASTRA_PROCESS_FILESYSTEM_INIT;
static AstraCollectionModel list_model = ASTRA_COLLECTION_MODEL_INIT;
typedef struct BrowserSnapshot {
    AstraDirectorySnapshot directory;
    AstraCollectionItem *items;
    uint32_t capacity;
} BrowserSnapshot;
typedef struct BrowserPlaces {
    AstraVolumeSnapshot volume;
    AstraFilePanelPlace *places;
    AstraCollectionItem *items;
    uint32_t count;
} BrowserPlaces;
static BrowserSnapshot browser_active = {
    .directory = ASTRA_DIRECTORY_SNAPSHOT_INIT};
static BrowserSnapshot browser_spare = {
    .directory = ASTRA_DIRECTORY_SNAPSHOT_INIT};
static BrowserPlaces browser_places = {
    .volume = ASTRA_VOLUME_SNAPSHOT_INIT};
static AstraFilePanel file_panel = ASTRA_FILE_PANEL_INIT;
static AstraCollectionModel browser_model = ASTRA_COLLECTION_MODEL_INIT;
static AstraScrollModel browser_scroll = ASTRA_SCROLL_MODEL_INIT;
static const AstraSurface **browser_grid_icons;
static uint32_t browser_grid_capacity;
static AstraHandle volumes_metadata;
static AstraVolumeWatch volumes_watch = ASTRA_VOLUME_WATCH_INIT;
static uint32_t browsing;
static uint32_t amiga_encoding;
static uint32_t confirm_uninstall;
static uint32_t showing_information;
static uint32_t showing_import;
static AstraFontImport import_job = ASTRA_FONT_IMPORT_INIT;
static AstraFontImportProgress import_progress = ASTRA_FONT_IMPORT_PROGRESS_INIT;
static char import_message[160];
static AstraHandle fonts_clipboard;
typedef struct FontDocument {
    AstraTextModel model;
    void *content;
    void *metadata;
} FontDocument;
static FontDocument information_document = { .model = ASTRA_TEXT_MODEL_INIT };
static char browser_path[ASTRA_VFS_PATH_MAX] = "/dh0";
static AstraControl controls[FONTS_CONTROL_COUNT];
static AstraUIContext ui = ASTRA_UI_CONTEXT_INIT;
static AstraScrollModel scroll = ASTRA_SCROLL_MODEL_INIT;
static AstraSharedSurface surface;
static AstraWindow window = ASTRA_WINDOW_INIT;
static uint16_t client_width = FONTS_WIDTH;
static uint16_t client_height = FONTS_HEIGHT;

#define CONTROL(id) (&controls[(id) - 1u])

static void document_dispose(FontDocument *document)
{
    if (document->metadata != NULL) astra_text_model_dispose(&document->model);
    astra_runtime_deallocate(document->content);
    astra_runtime_deallocate(document->metadata);
    *document = (FontDocument){ .model = ASTRA_TEXT_MODEL_INIT };
}

static AstraResult document_append(FontDocument *document, const char *text,
                                    uint32_t length)
{
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraTextModelRequirements need = ASTRA_TEXT_MODEL_REQUIREMENTS_INIT;
    if (document->metadata == NULL) {
        AstraTextModelInfo info = ASTRA_TEXT_MODEL_INFO_INIT;
        document->metadata = astra_runtime_allocate(sizeof(uint32_t));
        if (document->metadata == NULL) return ASTRA_ERROR_OUT_OF_MEMORY;
        info.metadata_arena = document->metadata;
        info.metadata_arena_bytes = sizeof(uint32_t);
        AstraResult result = astra_text_model_init(&document->model, &info);
        if (result != ASTRA_OK) return result;
    }
    AstraResult result = astra_text_model_get_state(&document->model, &state);
    if (result != ASTRA_OK) return result;
    result = astra_text_model_replace_requirements(&document->model,
        state.text_bytes, state.text_bytes, text, length, &need);
    if (result != ASTRA_OK) return result;
    if (need.content_arena_bytes > state.content_arena_bytes ||
        need.metadata_arena_bytes > state.metadata_arena_bytes) {
        void *content = need.content_arena_bytes == 0u ? NULL :
            astra_runtime_allocate(need.content_arena_bytes);
        void *metadata = astra_runtime_allocate(need.metadata_arena_bytes);
        if ((need.content_arena_bytes != 0u && content == NULL) || metadata == NULL)
            result = ASTRA_ERROR_OUT_OF_MEMORY;
        else result = astra_text_model_move_arenas(&document->model, content,
            need.content_arena_bytes, metadata, need.metadata_arena_bytes);
        if (result != ASTRA_OK) {
            astra_runtime_deallocate(content);
            astra_runtime_deallocate(metadata);
            return result;
        }
        astra_runtime_deallocate(document->content);
        astra_runtime_deallocate(document->metadata);
        document->content = content;
        document->metadata = metadata;
    }
    return astra_text_model_replace(&document->model, state.text_bytes,
                                     state.text_bytes, text, length);
}

static AstraResult document_field(FontDocument *document,
    const AstraFontInformation *information, uint32_t field, uint32_t index,
    const char *heading)
{
    uint32_t length = 0u;
    AstraResult result = astra_fonts_information_string(information, field,
        index, NULL, 0u, &length);
    if (result == ASTRA_ERROR_NOT_PRESENT) return ASTRA_OK;
    if (result != ASTRA_OK) return result;
    if (length == UINT32_MAX) return ASTRA_ERROR_NO_RESOURCES;
    char *text = astra_runtime_allocate((size_t)length + 1u);
    if (text == NULL) return ASTRA_ERROR_OUT_OF_MEMORY;
    result = astra_fonts_information_string(information, field, index,
        text, length + 1u, &length);
    if (result == ASTRA_OK)
        result = document_append(document, heading, (uint32_t)strlen(heading));
    if (result == ASTRA_OK) result = document_append(document, text, length);
    if (result == ASTRA_OK) result = document_append(document, "\n\n", 2u);
    astra_runtime_deallocate(text);
    return result;
}

static AstraResult collect_information(const AstraFontInformation *information,
                                        FontDocument *document)
{
    static const char *const headings[] = {
        "Family: ", "Style: ", "Author: ", "License summary:\n",
        "Converter: ", "Source encoding: "
    };
    AstraResult result = ASTRA_OK;
    for (uint32_t at = 0u; at < sizeof(headings) / sizeof(headings[0]) &&
         result == ASTRA_OK; ++at)
        result = document_field(document, information,
            ASTRA_FONT_INFORMATION_FAMILY + at, 0u, headings[at]);
    for (uint32_t at = 0u; at < information->source_count && result == ASTRA_OK; ++at) {
        result = document_field(document, information,
            ASTRA_FONT_INFORMATION_SOURCE_NAME, at, "Original source: ");
        if (result == ASTRA_OK) result = document_field(document, information,
            ASTRA_FONT_INFORMATION_SOURCE_SHA256, at, "SHA-256: ");
    }
    for (uint32_t at = 0u; at < information->notice_count && result == ASTRA_OK; ++at) {
        AstraFontNotice notice;
        char label[128];
        AstraString text;
        result = astra_fonts_information_notice(information, at, &notice);
        if (result != ASTRA_OK) break;
        astra_string_init(&text, label, sizeof(label));
        if (!astra_string_append(&text, notice.name_id == 0u ? "Copyright" :
                notice.name_id == 13u ? "License" : notice.name_id == 14u ?
                "License URL" : "Notice") ||
            !astra_string_append(&text, " (name ") ||
            !astra_string_append_u64(&text, notice.name_id) ||
            !astra_string_append(&text, ", platform ") ||
            !astra_string_append_u64(&text, notice.platform_id) ||
            !astra_string_append(&text, ", encoding ") ||
            !astra_string_append_u64(&text, notice.encoding_id) ||
            !astra_string_append(&text, ", language ") ||
            !astra_string_append_u64(&text, notice.language_id) ||
            !astra_string_append(&text, ")\n")) return ASTRA_ERROR_NO_RESOURCES;
        result = document_append(document, text.text, text.length);
        uint32_t length = 0u;
        if (result == ASTRA_OK) {
            result = astra_fonts_information_string(information,
                ASTRA_FONT_INFORMATION_NOTICE_TEXT, at, NULL, 0u, &length);
            if (result == ASTRA_ERROR_NOT_PRESENT)
                result = document_append(document,
                    "Text encoding unavailable; original bytes below.\n\n",
                    sizeof("Text encoding unavailable; original bytes below.\n\n") - 1u);
            else if (result == ASTRA_OK)
                result = document_field(document, information,
                    ASTRA_FONT_INFORMATION_NOTICE_TEXT, at, "");
        }
        if (result == ASTRA_OK) result = document_field(document, information,
            ASTRA_FONT_INFORMATION_NOTICE_LANGUAGE, at, "Language tag: ");
        if (result == ASTRA_OK) result = document_field(document, information,
            ASTRA_FONT_INFORMATION_NOTICE_RAW_HEX, at, "Original bytes (hex): ");
        if (result == ASTRA_OK) result = document_field(document, information,
            ASTRA_FONT_INFORMATION_NOTICE_LANGUAGE_RAW_HEX, at, "Language tag bytes (hex): ");
    }
    if (result == ASTRA_OK && information->notice_count == 0u)
        result = document_append(document, "No embedded notices supplied.\n",
            sizeof("No embedded notices supplied.\n") - 1u);
    if (result == ASTRA_OK) {
        AstraTextSelection selection = ASTRA_TEXT_SELECTION_INIT;
        result = astra_text_model_set_selection(&document->model, &selection);
    }
    return result;
}

static uint32_t layout_controls(void)
{
    AstraFlexLayout layout = ASTRA_SETTINGS_PANE_LAYOUT_INIT;
    return astra_interface_ui_layout(&ui, &layout) == ASTRA_OK ?
           ASTRA_STATUS_OK : FONTS_FAIL_CONTROL;
}

static int same_family(const AstraInstalledFont *face,
                       const AstraCollectionItem *family)
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
        families[family_count] = (AstraCollectionItem){
            (uint64_t)at + 1u, face.family, face.family_length, {0u, 0u}
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
        detail_sources = astra_runtime_allocate(
            (size_t)strikes * sizeof(*detail_sources));
        if (details == NULL || detail_text == NULL || detail_sources == NULL)
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
            details[detail_count] = (AstraCollectionItem){
                (uint64_t)detail_count + 1u,
                text.text, text.length, {0u, 0u}
            };
            detail_sources[detail_count] = (FontDetail){at, strike_at};
            ++detail_count;
        }
    }
    return ASTRA_STATUS_OK;
}

static void browser_snapshot_dispose(BrowserSnapshot *snapshot)
{
    astra_directory_snapshot_dispose(&snapshot->directory);
    astra_runtime_deallocate(snapshot->items);
    *snapshot = (BrowserSnapshot){
        .directory = ASTRA_DIRECTORY_SNAPSHOT_INIT};
}

static uint32_t browser_snapshot_load(BrowserSnapshot *snapshot,
                                      const char *path)
{
    uint32_t status = astra_directory_snapshot_load(&snapshot->directory,
        &files.filesystem, path, NULL);
    uint32_t capacity;
    if (status != ASTRA_VFS_OK) return status;
    capacity = snapshot->directory.count == 0u ? 1u :
        snapshot->directory.count;
    if (capacity > UINT32_MAX / sizeof(*snapshot->items))
        status = ASTRA_VFS_ERR_LIMIT;
    else {
        snapshot->items = astra_runtime_allocate(
            (size_t)capacity * sizeof(*snapshot->items));
        if (snapshot->items == NULL) status = ASTRA_VFS_ERR_LIMIT;
    }
    if (status == ASTRA_VFS_OK) snapshot->capacity = capacity;
    else browser_snapshot_dispose(snapshot);
    return status;
}

static void browser_places_dispose(BrowserPlaces *places)
{
    astra_volume_snapshot_close(&places->volume);
    astra_runtime_deallocate(places->places);
    astra_runtime_deallocate(places->items);
    *places = (BrowserPlaces){.volume = ASTRA_VOLUME_SNAPSHOT_INIT};
}

static AstraResult browser_places_load(BrowserPlaces *places)
{
    AstraResult result = astra_volume_snapshot_open(volumes_metadata,
                                                    &places->volume);
    if (result != ASTRA_OK) return result;
    uint32_t capacity = places->volume.count == 0u ? 1u :
                        places->volume.count;
    if (capacity > UINT32_MAX / sizeof(*places->places) ||
        capacity > UINT32_MAX / sizeof(*places->items))
        result = ASTRA_ERROR_NO_RESOURCES;
    else {
        places->places = astra_runtime_allocate(
            (size_t)capacity * sizeof(*places->places));
        places->items = astra_runtime_allocate(
            (size_t)capacity * sizeof(*places->items));
        if (places->places == NULL || places->items == NULL)
            result = ASTRA_ERROR_OUT_OF_MEMORY;
    }
    if (result == ASTRA_OK) {
        for (uint32_t at = 0u; at < places->volume.count; ++at) {
            const AstraVolumeRecord *record = &places->volume.records[at];
            if (record->kind != ASTRA_VOLUME_MOUNT ||
                record->state != ASTRA_VOLUME_MOUNTED ||
                record->name[0] != '/') continue;
            const char *label = record->label[0] != '\0' ?
                record->label : record->name + 1u;
            if (label[0] == '\0') continue;
            places->places[places->count++] = (AstraFilePanelPlace){
                label, (uint32_t)strlen(label), record->name};
        }
    }
    if (result != ASTRA_OK) browser_places_dispose(places);
    return result;
}

static AstraResult browser_grid_reserve(uint32_t capacity)
{
    const AstraSurface **icons;
    if (capacity <= browser_grid_capacity) return ASTRA_OK;
    if (capacity > UINT32_MAX / sizeof(*icons)) return ASTRA_ERROR_NO_RESOURCES;
    icons = astra_runtime_allocate((size_t)capacity * sizeof(*icons));
    if (icons == NULL) return ASTRA_ERROR_OUT_OF_MEMORY;
    astra_runtime_deallocate(browser_grid_icons);
    browser_grid_icons = icons;
    browser_grid_capacity = capacity;
    return ASTRA_OK;
}

static AstraResult browser_prepare_spare(uint32_t capacity)
{
    AstraCollectionItem *items;
    if (browser_spare.capacity >= capacity) return ASTRA_OK;
    items = astra_runtime_allocate((size_t)capacity * sizeof(*items));
    if (items == NULL) return ASTRA_ERROR_OUT_OF_MEMORY;
    astra_runtime_deallocate(browser_spare.items);
    browser_spare.items = items;
    browser_spare.capacity = capacity;
    return ASTRA_OK;
}

static uint32_t browser_open_directory(const char *path)
{
    BrowserSnapshot next = {.directory = ASTRA_DIRECTORY_SNAPSHOT_INIT};
    const AstraSurface **next_grid = NULL;
    AstraCollectionItem *next_spare = NULL;
    uint32_t status = browser_snapshot_load(&next, path);
    if (status != ASTRA_VFS_OK) return status;
    if (strlen(path) >= sizeof(browser_path)) {
        browser_snapshot_dispose(&next);
        return ASTRA_VFS_ERR_LIMIT;
    }
    if (next.capacity > browser_grid_capacity) {
        next_grid = astra_runtime_allocate((size_t)next.capacity *
            sizeof(*next_grid));
        if (next_grid == NULL) status = ASTRA_VFS_ERR_LIMIT;
    }
    if (status == ASTRA_VFS_OK && next.capacity > browser_spare.capacity) {
        next_spare = astra_runtime_allocate((size_t)next.capacity *
            sizeof(*next_spare));
        if (next_spare == NULL) status = ASTRA_VFS_ERR_LIMIT;
    }
    if (status != ASTRA_VFS_OK) {
        astra_runtime_deallocate(next_grid);
        astra_runtime_deallocate(next_spare);
        browser_snapshot_dispose(&next);
        return status;
    }
    if (next_grid != NULL) {
        astra_runtime_deallocate(browser_grid_icons);
        browser_grid_icons = next_grid;
        browser_grid_capacity = next.capacity;
    }
    if (next_spare != NULL) {
        astra_runtime_deallocate(browser_spare.items);
        browser_spare.items = next_spare;
        browser_spare.capacity = next.capacity;
    }
    browser_snapshot_dispose(&browser_active);
    browser_active = next;
    memcpy(browser_path, path, strlen(path) + 1u);
    return ASTRA_VFS_OK;
}

static AstraResult browser_import(const char *path, const char *name)
{
    AstraFile file = ASTRA_FILE_INIT;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    void *bytes;
    uint32_t length = 0u;
    uint32_t status;
    uint32_t opened;
    AstraResult result;

    if (path == NULL || name == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    length = (uint32_t)strlen(name);
    if (length == 0u || length >= ASTRA_FONT_IMPORT_NAME_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
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
        result = astra_fonts_import_begin(fonts_admin, bytes, length, name,
            (uint32_t)strlen(name), encoding, &import_job);
    } else result = ASTRA_ERROR_IO;
    astra_runtime_deallocate(bytes);
    return result;
}

static uint32_t selected_preview_font(AstraLabelFont *font)
{
    AstraInstalledFont face;
    AstraInstalledFontStrike strike;
    *font = (AstraLabelFont)ASTRA_LABEL_FONT_INIT;
    if (active_family == UINT32_MAX) return ASTRA_STATUS_OK;
    if (detail_selection >= detail_count ||
        astra_fonts_catalog_face(&catalog,
            detail_sources[detail_selection].face, &face) != ASTRA_OK ||
        astra_fonts_catalog_strike(&catalog,
            detail_sources[detail_selection].face,
            detail_sources[detail_selection].strike, &strike) != ASTRA_OK)
        return FONTS_FAIL_CATALOG;
    font->gui = fonts_gui;
    font->id = face.id;
    font->id_length = face.id_length;
    font->pixel_height = strike.pixel_height;
    return ASTRA_STATUS_OK;
}

static uint32_t update_preview(void)
{
    AstraLabelFont font = ASTRA_LABEL_FONT_INIT;
    const char *text = "The quick brown fox jumps over the lazy dog. 0123456789";

    if (browsing || showing_import || showing_information || active_family == UINT32_MAX)
        return ASTRA_STATUS_OK;
    if (selected_preview_font(&font) != ASTRA_STATUS_OK)
        return FONTS_FAIL_CATALOG;
    if (astra_interface_label_set_font(&ui, CONTROL(FONTS_PREVIEW), &font) != ASTRA_OK) {
        font = (AstraLabelFont)ASTRA_LABEL_FONT_INIT;
        if (astra_interface_label_set_font(&ui, CONTROL(FONTS_PREVIEW), &font) != ASTRA_OK)
            return FONTS_FAIL_CONTROL;
        text = "Preview unavailable for this font";
    }
    return astra_interface_control_set_text(&ui, CONTROL(FONTS_PREVIEW),
        text, (uint32_t)strlen(text)) == ASTRA_OK ?
        ASTRA_STATUS_OK : FONTS_FAIL_CONTROL;
}

static uint32_t build_controls(void)
{
    AstraSettingsPaneInfo pane = ASTRA_SETTINGS_PANE_INFO_INIT;
    AstraFilePanelInfo panel_info = ASTRA_FILE_PANEL_INFO_INIT;
    AstraScrollViewInfo view = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraListInfo list = ASTRA_LIST_INFO_INIT;
    AstraButtonInfo back = ASTRA_BUTTON_INFO_INIT;
    AstraButtonInfo use = ASTRA_BUTTON_INFO_INIT;
    AstraButtonInfo import = ASTRA_BUTTON_INFO_INIT;
    AstraButtonInfo encoding = ASTRA_BUTTON_INFO_INIT;
    AstraButtonInfo uninstall = ASTRA_BUTTON_INFO_INIT;
    AstraButtonInfo information = ASTRA_BUTTON_INFO_INIT;
    AstraProgressInfo progress = ASTRA_PROGRESS_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    const AstraCollectionItem *title = active_family == UINT32_MAX ? NULL :
                                   &families[active_family];
    AstraInstalledFont selected_face = {0};
    uint32_t system_ready = 0u;

    if (!browsing && active_family != UINT32_MAX &&
        detail_selection < detail_count) {
        if (astra_fonts_catalog_face(&catalog,
            detail_sources[detail_selection].face, &selected_face) != ASTRA_OK)
            return FONTS_FAIL_CATALOG;
        system_ready = (selected_face.flags &
                        ASTRA_INSTALLED_FONT_SELECTABLE) != 0u;
    }

    pane.first_id = FONTS_TITLE;
    pane.title = showing_import ? "Import font" : browsing ? browser_path : title == NULL ? "Fonts" :
                 title->text;
    pane.title_length = showing_import ? sizeof("Import font") - 1u :
        browsing ? (uint32_t)strlen(browser_path) : title == NULL ? 5u : title->text_length;
    pane.description = message != NULL ? message : showing_information ?
                 "Source and license information · Select text and copy with Meta+C" : confirm_uninstall ?
                 "Remove this imported style and all its sizes? Source file remains." : browsing ?
                 "Open a folder or select a font file to import" :
                 active_family == UINT32_MAX ?
                 (family_count == 0u ? "No fonts installed" :
                  "Double-click a family to see installed styles and sizes") :
                 system_ready ?
                 "Select a style, then use it for system text" :
                 system_font_requirements;
    pane.description_length = (uint32_t)strlen(pane.description);
    if (astra_interface_settings_pane_init(controls, &pane) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    {
        AstraLabelInfo preview = ASTRA_LABEL_INFO_INIT;
        preview.id = FONTS_PREVIEW;
        preview.text = "Preview unavailable for this font";
        preview.text_length = (uint32_t)strlen(preview.text);
        preview.state = showing_import || browsing || showing_information || active_family == UINT32_MAX ?
                        ASTRA_CONTROL_COLLAPSED : 0u;
        if (astra_interface_label_init(CONTROL(FONTS_PREVIEW), &preview) != ASTRA_OK)
            return FONTS_FAIL_CONTROL;
    }
    view.id = FONTS_SCROLL;
    view.model = &scroll;
    view.preferred_width = client_width > 48u ? client_width - 48u : 1u;
    view.preferred_height = 120u;
    view.state = showing_import || confirm_uninstall || browsing ?
        ASTRA_CONTROL_COLLAPSED : 0u;
    if (astra_interface_scroll_view_init(CONTROL(FONTS_SCROLL), &view) !=
        ASTRA_OK) return FONTS_FAIL_CONTROL;
    list.id = FONTS_LIST;
    const AstraCollectionItem *list_items = active_family == UINT32_MAX ?
        families : details;
    uint32_t list_count = active_family == UINT32_MAX ?
        family_count : detail_count;
    uint32_t list_selected = active_family == UINT32_MAX ?
        main_selection : detail_selection;
    if (astra_interface_collection_init(&list_model, list_items, list_count,
        list_selected < list_count ? list_items[list_selected].id : 0u) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    list.model = &list_model;
    list.state = showing_import || confirm_uninstall || browsing ?
        ASTRA_CONTROL_COLLAPSED : 0u;
    if (showing_information) {
        AstraTextViewInfo text = ASTRA_TEXT_VIEW_INFO_INIT;
        text.id = FONTS_LIST;
        text.model = &information_document.model;
        if (astra_interface_text_view_init(CONTROL(FONTS_LIST), &text) != ASTRA_OK)
            return FONTS_FAIL_CONTROL;
    } else if (astra_interface_list_init(CONTROL(FONTS_LIST), &list) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    progress.id = FONTS_PROGRESS;
    progress.value = import_progress.completed_faces;
    progress.maximum = import_progress.total_faces;
    progress.state = showing_import ? 0u : ASTRA_CONTROL_COLLAPSED;
    if (astra_interface_progress_init(CONTROL(FONTS_PROGRESS), &progress) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    back.id = FONTS_BACK;
    back.text = showing_import ? "Cancel import" : showing_information ? "Back to styles and sizes" : confirm_uninstall ? "Cancel" :
                "Back to families";
    back.text_length = (uint32_t)strlen(back.text);
    back.state = showing_import ?
        (import_progress.state == ASTRA_FONT_IMPORT_CANCELLING ? ASTRA_CONTROL_DISABLED : 0u) :
        browsing || active_family == UINT32_MAX ?
                 ASTRA_CONTROL_COLLAPSED : 0u;
    if (astra_interface_button_init(CONTROL(FONTS_BACK), &back) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    use.id = FONTS_USE;
    use.text = "Use as system font";
    use.text_length = (uint32_t)strlen(use.text);
    use.state = showing_import || showing_information || confirm_uninstall ||
                browsing || active_family == UINT32_MAX ?
                ASTRA_CONTROL_COLLAPSED :
                !system_ready ? ASTRA_CONTROL_DISABLED : 0u;
    if (astra_interface_button_init(CONTROL(FONTS_USE), &use) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    import.id = FONTS_IMPORT;
    import.text = "Import font…";
    import.text_length = (uint32_t)strlen(import.text);
    import.state = showing_import || browsing || active_family != UINT32_MAX ?
                   ASTRA_CONTROL_COLLAPSED : 0u;
    if (astra_interface_button_init(CONTROL(FONTS_IMPORT), &import) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    encoding.id = FONTS_ENCODING;
    encoding.text = amiga_encoding == ASTRA_FONT_ENCODING_ISO_8859_1 ?
        "Amiga encoding: ISO-8859-1" :
        amiga_encoding == ASTRA_FONT_ENCODING_ISO_8859_2 ?
        "Amiga encoding: ISO-8859-2" : "Choose Amiga encoding";
    encoding.text_length = (uint32_t)strlen(encoding.text);
    encoding.state = browsing && !showing_import ? 0u : ASTRA_CONTROL_COLLAPSED;
    if (astra_interface_button_init(CONTROL(FONTS_ENCODING), &encoding) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    uninstall.id = FONTS_UNINSTALL;
    uninstall.text = (active_family != UINT32_MAX &&
                      detail_selection < detail_count &&
                      (selected_face.flags & ASTRA_INSTALLED_FONT_SYSTEM) != 0u) ?
                     "Protected" : confirm_uninstall ? "Remove font" : "Uninstall…";
    uninstall.text_length = (uint32_t)strlen(uninstall.text);
    uninstall.state = showing_import || showing_information || browsing ||
                      active_family == UINT32_MAX || detail_selection >= detail_count ?
                      ASTRA_CONTROL_COLLAPSED :
                      (selected_face.flags & ASTRA_INSTALLED_FONT_SYSTEM) != 0u ?
                      ASTRA_CONTROL_DISABLED : 0u;
    if (astra_interface_button_init(CONTROL(FONTS_UNINSTALL), &uninstall) !=
        ASTRA_OK) return FONTS_FAIL_CONTROL;
    information.id = FONTS_INFORMATION;
    information.text = "Source and license…";
    information.text_length = sizeof("Source and license…") - 1u;
    information.state = showing_import || showing_information || browsing || confirm_uninstall ||
        active_family == UINT32_MAX || detail_count == 0u ? ASTRA_CONTROL_COLLAPSED : 0u;
    if (astra_interface_button_init(CONTROL(FONTS_INFORMATION), &information) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;

    panel_info.first_id = FONTS_FILE_PANEL_FIRST;
    panel_info.mode = ASTRA_FILE_PANEL_OPEN;
    panel_info.directory = browser_path;
    panel_info.entries = browser_active.directory.entries;
    panel_info.entry_count = browser_active.directory.count;
    panel_info.item_storage = browser_active.items;
    panel_info.item_capacity = browser_active.capacity;
    panel_info.spare_item_storage = browser_spare.items;
    panel_info.spare_item_capacity = browser_spare.capacity;
    panel_info.grid_icons = browser_grid_icons;
    panel_info.grid_icon_capacity = browser_grid_capacity;
    panel_info.places = browser_places.places;
    panel_info.place_count = browser_places.count;
    panel_info.place_items = browser_places.items;
    panel_info.place_capacity = browser_places.volume.count;
    panel_info.collection = &browser_model;
    panel_info.scroll = &browser_scroll;
    if (astra_interface_file_panel_init(&file_panel,
        CONTROL(FONTS_FILE_PANEL_FIRST), &panel_info) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;

    item.parent_id = FONTS_BODY;
    if (astra_interface_control_set_flex(CONTROL(FONTS_PREVIEW), &item) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    item.grow = 1u;
    item.minimum_height = view.preferred_height;
    if (astra_interface_control_set_flex(CONTROL(FONTS_SCROLL), &item) !=
        ASTRA_OK) return FONTS_FAIL_CONTROL;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = FONTS_SCROLL;
    item.minimum_width = view.preferred_width;
    if (astra_interface_control_set_flex(CONTROL(FONTS_LIST), &item) !=
        ASTRA_OK) return FONTS_FAIL_CONTROL;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = FONTS_BODY;
    item.minimum_width = view.preferred_width;
    if (astra_interface_control_set_flex(CONTROL(FONTS_PROGRESS), &item) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    item.grow = 1u;
    item.minimum_height = view.preferred_height;
    if (astra_interface_control_set_flex(CONTROL(FONTS_FILE_PANEL_FIRST),
        &item) != ASTRA_OK) return FONTS_FAIL_CONTROL;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = FONTS_ACTIONS;
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
    if (astra_interface_control_set_flex(CONTROL(FONTS_INFORMATION), &item) !=
        ASTRA_OK) return FONTS_FAIL_CONTROL;
    if (astra_interface_ui_init(&ui, controls, FONTS_CONTROL_COUNT,
                                client_width, client_height) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    if (astra_interface_ui_set_state(&ui,
        CONTROL(FONTS_FILE_PANEL_FIRST),
        browsing && !showing_import ? 0u : ASTRA_CONTROL_COLLAPSED) != ASTRA_OK)
        return FONTS_FAIL_CONTROL;
    if (update_preview() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
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

static uint32_t present(const char *completion)
{
    AstraWindowFrame frame = {0u, 0u, client_width, client_height};

    if (paint() != ASTRA_STATUS_OK ||
        astra_window_present_region(&window, &frame) != ASTRA_OK)
        return FONTS_FAIL_WINDOW;
    astra_interface_ui_damage_clear(&ui);
    if (completion != NULL) (void)astra_log(completion);
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
    astra_runtime_deallocate(detail_sources);
    details = NULL;
    detail_text = NULL;
    detail_sources = NULL;
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

static const char *import_error(AstraResult result)
{
    return result == ASTRA_ERROR_UNSUPPORTED ? "This font format is not supported yet" :
        result == ASTRA_ERROR_INVALID_ARGUMENT ? "Choose a valid font file, up to 32 MiB" :
        result == ASTRA_ERROR_BUSY ? "Another import is active; finish it before trying again" :
        result == ASTRA_ERROR_NO_RESOURCES || result == ASTRA_ERROR_OUT_OF_MEMORY ?
            "Not enough memory or installed-font capacity; remove unused fonts and retry" :
        result == ASTRA_ERROR_CANCELLED ? "Import cancelled; installed fonts unchanged" :
        result == ASTRA_ERROR_PEER_DEAD ? "Fonts service stopped; reopen Fonts after it restarts" :
        "Could not read or save font; check source file and system storage";
}

static uint32_t update_import(void)
{
    if (!showing_import) return ASTRA_STATUS_OK;
    AstraFontImportProgress progress = ASTRA_FONT_IMPORT_PROGRESS_INIT;
    AstraResult result = astra_fonts_import_state(&import_job, &progress);
    if (result == ASTRA_OK && (progress.state == ASTRA_FONT_IMPORT_RUNNING ||
                              progress.state == ASTRA_FONT_IMPORT_CANCELLING)) {
        if (memcmp(&progress, &import_progress, sizeof(progress)) == 0)
            return ASTRA_STATUS_OK;
        import_progress = progress;
        AstraString text;
        astra_string_init(&text, import_message, sizeof(import_message));
        if (!astra_string_append(&text, progress.state == ASTRA_FONT_IMPORT_CANCELLING ?
                "Cancelling; waiting for converter and cleanup" :
                "Converting font; installed fonts remain unchanged"))
            return FONTS_FAIL_CONTROL;
        if (progress.total_faces != 0u &&
            (!astra_string_append(&text, " · ") ||
             !astra_string_append_u64(&text, progress.completed_faces) ||
             !astra_string_append(&text, "/") ||
             !astra_string_append_u64(&text, progress.total_faces) ||
             !astra_string_append(&text, " faces staged"))) return FONTS_FAIL_CONTROL;
        message = import_message;
        if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
        return present(progress.state == ASTRA_FONT_IMPORT_CANCELLING ?
            "fonts UI import cancelling" : "fonts UI import progress");
    }
    if (result == ASTRA_OK) result = progress.result;
    AstraResult closed = astra_fonts_import_close(&import_job);
    showing_import = 0u;
    import_progress = (AstraFontImportProgress)ASTRA_FONT_IMPORT_PROGRESS_INIT;
    if (result == ASTRA_OK) {
        if (reload_catalog() != ASTRA_STATUS_OK) return FONTS_FAIL_CATALOG;
        message = closed == ASTRA_OK ? "Font imported" :
            "Font imported; could not confirm session release";
    } else message = import_error(result);
    if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
    return present(result == ASTRA_OK ? "fonts UI import complete" :
        result == ASTRA_ERROR_CANCELLED ? "fonts UI import cancelled" : "fonts UI import failed");
}

static uint32_t import_selected(void)
{
    message = NULL;
    AstraResult result = browser_import(file_panel.target, file_panel.leaf);

    if (result == ASTRA_OK) {
        showing_import = 1u;
        import_progress = (AstraFontImportProgress)ASTRA_FONT_IMPORT_PROGRESS_INIT;
        message = "Converting font; installed fonts remain unchanged";
    } else if (message == NULL) {
        message = import_error(result);
    }
    if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
    return present(result == ASTRA_OK ? "fonts UI import started" : NULL);
}

static uint32_t browser_refresh_places(void)
{
    BrowserPlaces next = {.volume = ASTRA_VOLUME_SNAPSHOT_INIT};
    if (browser_places_load(&next) != ASTRA_OK) return FONTS_FAIL_FILESYSTEM;
    if (browsing && !showing_import &&
        astra_interface_file_panel_set_places(&file_panel, &ui,
            CONTROL(FONTS_FILE_PANEL_FIRST), next.places, next.count,
            next.items, next.volume.count) != ASTRA_OK) {
        browser_places_dispose(&next);
        return FONTS_FAIL_CONTROL;
    }
    browser_places_dispose(&browser_places);
    browser_places = next;
    return ASTRA_STATUS_OK;
}

static uint32_t action(const AstraUIAction *event)
{
    if (showing_import) {
        if (event->type == ASTRA_UI_ACTION_ACTIVATE && event->control_id == FONTS_BACK) {
            AstraResult result = astra_fonts_import_cancel(&import_job);
            if (result != ASTRA_OK) {
                message = import_error(result);
                if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
                return present(NULL);
            }
            return update_import();
        }
        return ASTRA_STATUS_OK;
    }
    if (showing_information) {
        if (event->type == ASTRA_UI_ACTION_ACTIVATE && event->control_id == FONTS_BACK) {
            showing_information = 0u;
            message = NULL;
            if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
            document_dispose(&information_document);
            return present("fonts UI information close complete");
        }
        if (event->type == ASTRA_UI_ACTION_COPY && event->control_id == FONTS_LIST) {
            AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
            AstraClipboardRepresentation representation = ASTRA_CLIPBOARD_REPRESENTATION_INIT;
            if (astra_text_model_get_state(&information_document.model, &state) != ASTRA_OK)
                return FONTS_FAIL_CONTROL;
            uint32_t start = state.selection.anchor < state.selection.focus ?
                state.selection.anchor : state.selection.focus;
            uint32_t end = state.selection.anchor > state.selection.focus ?
                state.selection.anchor : state.selection.focus;
            if (start == end) return ASTRA_STATUS_OK;
            char *text = astra_runtime_allocate(end - start);
            uint32_t copied = 0u;
            AstraResult result = text == NULL ? ASTRA_ERROR_OUT_OF_MEMORY :
                astra_text_model_copy(&information_document.model, start, end,
                                      text, end - start, &copied);
            if (result == ASTRA_OK) {
                representation.type = ASTRA_CLIPBOARD_TYPE_UTF8;
                representation.type_length = sizeof(ASTRA_CLIPBOARD_TYPE_UTF8) - 1u;
                representation.data = text;
                representation.data_length = copied;
                result = astra_clipboard_write(fonts_clipboard, &representation, 1u, NULL);
            }
            astra_runtime_deallocate(text);
            message = result == ASTRA_OK ? "Information copied" :
                "Could not copy information; select less text and try again";
            if (astra_interface_control_set_text(&ui, CONTROL(FONTS_HINT), message,
                (uint32_t)strlen(message)) != ASTRA_OK) return FONTS_FAIL_CONTROL;
            return present(result == ASTRA_OK ? "fonts UI information copy complete" : NULL);
        }
        return ASTRA_STATUS_OK;
    }
    if (event->type == ASTRA_UI_ACTION_ACTIVATE && event->control_id == FONTS_INFORMATION) {
        AstraFontInformation information = ASTRA_FONT_INFORMATION_INIT;
        FontDocument document = { .model = ASTRA_TEXT_MODEL_INIT };
        AstraInstalledFont face;
        if (browsing || confirm_uninstall || active_family == UINT32_MAX ||
            detail_selection >= detail_count) return ASTRA_STATUS_OK;
        if (astra_fonts_catalog_face(&catalog,
            detail_sources[detail_selection].face, &face) != ASTRA_OK)
            return FONTS_FAIL_CATALOG;
        AstraResult result = astra_fonts_information_open(fonts_read, face.id,
            face.id_length, &information);
        if (result == ASTRA_OK) {
            result = collect_information(&information, &document);
            AstraResult closed = astra_fonts_information_close(&information);
            if (result == ASTRA_OK) result = closed;
        }
        if (result == ASTRA_OK) {
            document_dispose(&information_document);
            information_document = document;
            showing_information = 1u;
            message = NULL;
        } else {
            document_dispose(&document);
            (void)astra_log_failure("fonts UI information", (uint32_t)result);
            message = result == ASTRA_ERROR_OUT_OF_MEMORY ?
                "Not enough memory to show font information" :
                "Could not read font information; font management remains available";
        }
        if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
        return present(result == ASTRA_OK ? "fonts UI information complete" : NULL);
    }
    if (browsing) {
        if (event->type == ASTRA_UI_ACTION_ACTIVATE &&
            event->control_id == FONTS_ENCODING) {
            amiga_encoding = amiga_encoding == ASTRA_FONT_ENCODING_ISO_8859_1 ?
                ASTRA_FONT_ENCODING_ISO_8859_2 :
                ASTRA_FONT_ENCODING_ISO_8859_1;
            const char *label = amiga_encoding == ASTRA_FONT_ENCODING_ISO_8859_1 ?
                "Amiga encoding: ISO-8859-1" : "Amiga encoding: ISO-8859-2";
            if (astra_interface_control_set_text(&ui,
                CONTROL(FONTS_ENCODING), label,
                (uint32_t)strlen(label)) != ASTRA_OK)
                return FONTS_FAIL_CONTROL;
            return present(NULL);
        }
        if (event->control_id >= FONTS_FILE_PANEL_FIRST &&
            event->control_id <= FONTS_CONTROL_COUNT) {
            uint32_t outcome = ASTRA_FILE_PANEL_NONE;
            AstraResult result = astra_interface_file_panel_action(&file_panel,
                &ui, CONTROL(FONTS_FILE_PANEL_FIRST), event, &outcome);
            if (result != ASTRA_OK) {
                message = "Could not select that file";
                if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
                return present(NULL);
            }
            if (outcome == ASTRA_FILE_PANEL_PICKED) return import_selected();
            if (outcome == ASTRA_FILE_PANEL_NAVIGATE) {
                char target[ASTRA_VFS_PATH_MAX];
                if (strlen(file_panel.target) >= sizeof(target)) return FONTS_FAIL_CONTROL;
                memcpy(target, file_panel.target, strlen(file_panel.target) + 1u);
                message = browser_open_directory(target) == ASTRA_VFS_OK ? NULL :
                    "Could not open folder or volume";
                if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
                return present(NULL);
            }
            if (outcome == ASTRA_FILE_PANEL_CANCELLED) {
                browsing = 0u;
                message = NULL;
                if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
                return present(NULL);
            }
        }
        return ASTRA_STATUS_OK;
    }
    if (event->type == ASTRA_UI_ACTION_ACTIVATE &&
        event->control_id == FONTS_IMPORT) {
        uint32_t status = browser_open_directory(browser_path);
        if (status == ASTRA_VFS_OK) {
            browsing = 1u;
            message = NULL;
        } else message = "Could not open font source volume";
        if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
        return present(NULL);
    }
    if (confirm_uninstall && event->type == ASTRA_UI_ACTION_ACTIVATE &&
        event->control_id == FONTS_BACK) {
        confirm_uninstall = 0u;
        message = NULL;
        if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
        return present(NULL);
    }
    if (event->type == ASTRA_UI_ACTION_ACTIVATE &&
        event->control_id == FONTS_UNINSTALL) {
        AstraInstalledFont face;
        const char *completion = NULL;

        if (detail_selection >= detail_count ||
            astra_fonts_catalog_face(&catalog,
                detail_sources[detail_selection].face, &face) != ASTRA_OK)
            return FONTS_FAIL_CATALOG;
        if ((face.flags & ASTRA_INSTALLED_FONT_SYSTEM) != 0u) {
            message = "Built-in fonts cannot be removed";
            if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
            return present(NULL);
        }
        if (!confirm_uninstall) {
            confirm_uninstall = 1u;
            message = NULL;
            completion = "fonts UI confirmation complete";
        } else {
            AstraResult result = astra_fonts_uninstall(
                fonts_admin, face.id, face.id_length);
            confirm_uninstall = 0u;
            if (result == ASTRA_OK) {
                if (reload_catalog() != ASTRA_STATUS_OK)
                    return FONTS_FAIL_CATALOG;
                message = "Font removed";
                completion = "fonts UI removal complete";
            } else
                message = result == ASTRA_ERROR_BUSY ?
                    "Choose another system font before removing this one" :
                    "Could not remove font";
        }
        if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
        return present(completion);
    }
    if (event->control_id == FONTS_LIST &&
        event->type == ASTRA_UI_ACTION_VALUE_CHANGED &&
        active_family != UINT32_MAX && event->value >= 0 &&
        (uint64_t)event->value < detail_count) {
        AstraInstalledFont face;
        const char *hint;

        detail_selection = (uint32_t)event->value;
        if (astra_fonts_catalog_face(&catalog,
            detail_sources[detail_selection].face, &face) != ASTRA_OK)
            return FONTS_FAIL_CATALOG;
        uint32_t ready = (face.flags & ASTRA_INSTALLED_FONT_SELECTABLE) != 0u;
        hint = ready ? "Select a style, then use it for system text" :
                       system_font_requirements;
        message = NULL;
        if (astra_interface_ui_set_state(&ui, CONTROL(FONTS_USE),
                ready ? 0u : ASTRA_CONTROL_DISABLED) != ASTRA_OK ||
            update_preview() != ASTRA_STATUS_OK ||
            astra_interface_control_set_text(&ui, CONTROL(FONTS_HINT), hint,
                (uint32_t)strlen(hint)) != ASTRA_OK)
            return FONTS_FAIL_CONTROL;
        return present("fonts UI preview complete");
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
        return present("fonts UI details complete");
    }
    if (event->type == ASTRA_UI_ACTION_ACTIVATE &&
        event->control_id == FONTS_BACK) {
        active_family = UINT32_MAX;
        confirm_uninstall = 0u;
        message = NULL;
        if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
        return present(NULL);
    }
    if (event->type == ASTRA_UI_ACTION_ACTIVATE &&
        event->control_id == FONTS_USE) {
        AstraInstalledFont face;
        AstraResult result;

        if (detail_selection >= detail_count ||
            astra_fonts_catalog_face(&catalog,
                detail_sources[detail_selection].face, &face) != ASTRA_OK)
            return FONTS_FAIL_CATALOG;
        if ((face.flags & ASTRA_INSTALLED_FONT_SELECTABLE) == 0u)
            return ASTRA_STATUS_OK;
        result = astra_fonts_select(fonts_admin, face.id, face.id_length);
        message = result == ASTRA_OK ? "System font choice saved" :
                  result == ASTRA_ERROR_UNSUPPORTED ?
                  "Font lacks required system sizes or text characters" :
                  "Could not save system font choice";
        if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
        return present(NULL);
    }
    return ASTRA_STATUS_OK;
}

static uint32_t run(AstraHandle gui)
{
    for (;;) {
        AstraWindowEvent event = {0};
        if (update_import() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
        uint32_t waits[3] = {astra_window_event_wait_handle(&window)};
        uint32_t count = 1u, volume_slot = UINT32_MAX, import_slot = UINT32_MAX;
        uint32_t selected = 0u;
        if (volumes_watch.receive != ASTRA_INVALID_HANDLE) {
            volume_slot = count;
            waits[count++] = volumes_watch.receive;
        }
        if (showing_import) {
            import_slot = count;
            waits[count++] = astra_fonts_import_wait_handle(&import_job);
        }
        uint32_t waited = astra_wait_multiple(waits, count,
            ASTRA_DEADLINE_FOREVER, &selected, NULL);
        if (selected == import_slot &&
            (waited == ASTRA_SYSCALL_OK || waited == ASTRA_SYSCALL_PEER_DEAD))
            continue;
        if (selected == volume_slot &&
            (waited == ASTRA_SYSCALL_OK || waited == ASTRA_SYSCALL_PEER_DEAD)) {
            AstraVolumeChange change;
            AstraResult received = astra_volume_watch_receive(&volumes_watch,
                &change);
            if (received == ASTRA_OK && change.status == ASTRA_STATUS_OK) {
                if (browser_refresh_places() != ASTRA_STATUS_OK)
                    message = "Could not refresh mounted volumes";
                if (browsing) {
                    if (build_controls() != ASTRA_STATUS_OK) return FONTS_FAIL_CONTROL;
                    if (present(NULL) != ASTRA_STATUS_OK) return FONTS_FAIL_WINDOW;
                }
            } else {
                AstraResult closed = astra_volume_watch_close(volumes_metadata,
                    &volumes_watch);
                (void)closed;
                message = "Volume service unavailable; reopen Fonts after restart";
            }
            continue;
        }
        if (waited != ASTRA_SYSCALL_OK) return FONTS_FAIL_WINDOW;
        AstraResult result = astra_window_event_try(&window, &event);

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
            present(NULL) != ASTRA_STATUS_OK) return FONTS_FAIL_WINDOW;
    }
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *gui;
    const AstraStartupCapability *fonts;
    const AstraStartupCapability *admin;
    const AstraStartupCapability *bootstrap;
    const AstraStartupCapability *clipboard;
    const AstraStartupCapability *volumes;
    AstraScrollModelInfo scroll_info = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraWindowCreateInfo window_info = ASTRA_WINDOW_CREATE_INFO_INIT;
    uint32_t status = ASTRA_STATUS_OK;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    gui = astra_startup_capability(startup, ASTRA_CAPABILITY_GUI);
    fonts = astra_startup_capability(startup, ASTRA_CAPABILITY_FONT_CATALOG);
    admin = astra_startup_capability(startup, ASTRA_CAPABILITY_FONTS_ADMIN);
    clipboard = astra_startup_capability(startup, ASTRA_CAPABILITY_CLIPBOARD);
    volumes = astra_startup_capability(startup, ASTRA_CAPABILITY_VOLUMES);
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    if (gui == NULL || fonts == NULL || admin == NULL || clipboard == NULL ||
        volumes == NULL || astra_startup_capability(startup,
            ASTRA_CAPABILITY_VOLUME_READ) == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    fonts_read = fonts->handle;
    fonts_admin = admin->handle;
    fonts_gui = gui->handle;
    fonts_clipboard = clipboard->handle;
    volumes_metadata = volumes->handle;
    if (astra_fonts_use_system(gui->handle, NULL) != ASTRA_OK)
        return FONTS_FAIL_CATALOG;
    if (astra_process_filesystem_open(&files, startup) != ASTRA_VFS_OK)
        status = FONTS_FAIL_FILESYSTEM;
    if (status == ASTRA_STATUS_OK &&
        astra_volume_watch_open(volumes_metadata, &volumes_watch) != ASTRA_OK)
        status = FONTS_FAIL_FILESYSTEM;
    if (status == ASTRA_STATUS_OK &&
        (astra_fonts_catalog_open(fonts_read, &catalog) != ASTRA_OK ||
         collect_families() != ASTRA_STATUS_OK))
        status = FONTS_FAIL_CATALOG;
    scroll_info.line_height = 32u;
    if (status == ASTRA_STATUS_OK &&
        astra_scroll_init(&scroll, &scroll_info) != ASTRA_OK)
        status = FONTS_FAIL_CONTROL;
    if (status == ASTRA_STATUS_OK &&
        astra_scroll_init(&browser_scroll, &scroll_info) != ASTRA_OK)
        status = FONTS_FAIL_CONTROL;
    if (status == ASTRA_STATUS_OK &&
        (browser_prepare_spare(1u) != ASTRA_OK ||
         browser_grid_reserve(1u) != ASTRA_OK))
        status = FONTS_FAIL_CONTROL;
    if (status == ASTRA_STATUS_OK &&
        browser_places_load(&browser_places) != ASTRA_OK)
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
        window_info.y = 64u;
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
        AstraResult window_result = astra_window_create(gui->handle,
            surface.area, &window_info, &window);
        if (window_result != ASTRA_OK)
            (void)astra_log_failure("fonts window create", (uint32_t)-window_result);
        if (window_result == ASTRA_OK) {
            window_result = astra_window_set_application_name(&window,
                "Fonts", 5u);
            if (window_result != ASTRA_OK)
                (void)astra_log_failure("fonts window name", (uint32_t)-window_result);
        }
        if (window_result == ASTRA_OK) {
            window_result = astra_window_activate(&window);
            if (window_result != ASTRA_OK)
                (void)astra_log_failure("fonts window activate", (uint32_t)-window_result);
        }
        if (window_result != ASTRA_OK) {
            status = FONTS_FAIL_WINDOW;
        }
    }
    if (bootstrap != NULL) {
        (void)astra_service_ready(bootstrap->handle, status, NULL, 0u);
        (void)astra_close(bootstrap->handle);
    }
    if (status == ASTRA_STATUS_OK) status = run(gui->handle);
    if (showing_import) {
        AstraResult closed = astra_fonts_import_close(&import_job);
        if (status == ASTRA_STATUS_OK && closed != ASTRA_OK) status = FONTS_FAIL_CATALOG;
    }
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
    astra_runtime_deallocate(detail_sources);
    astra_runtime_deallocate(details);
    document_dispose(&information_document);
    if (volumes_watch.receive != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_volume_watch_close(volumes_metadata,
            &volumes_watch);
        (void)closed;
    }
    browser_snapshot_dispose(&browser_active);
    browser_snapshot_dispose(&browser_spare);
    astra_interface_file_panel_dispose(&file_panel);
    browser_places_dispose(&browser_places);
    astra_runtime_deallocate(browser_grid_icons);
    astra_process_filesystem_close(&files);
    if (status != ASTRA_STATUS_OK) (void)astra_log_failure("fonts UI exit", status);
    return (int)status;
}
