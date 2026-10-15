#ifndef ASTRA_DESKTOP_USAGE_H
#define ASTRA_DESKTOP_USAGE_H

#include <astra/proc.h>
#include <astra/crc32.h>
#include <astra/utf8.h>
#include "desktop_usage_layout.h"

#define DESKTOP_USAGE_WIDTH 740u
#define DESKTOP_USAGE_HEIGHT 440u
#define DESKTOP_USAGE_VIEW_HEIGHT 320u
#define DESKTOP_USAGE_LINE_HEIGHT 20u
#define DESKTOP_USAGE_CLOSE_ID 5u

/* Application text borrows no provider memory. The common ScrollView and
 * scrollbar own all scrolling/input policy; this canvas only paints rows. */
typedef struct DesktopUsageRow {
    char *text;
    uint32_t length, y, height;
} DesktopUsageRow;
typedef struct DesktopUsage {
    AstraWindow window;
    AstraSharedSurface surface;
    AstraControl controls[5];
    AstraUIContext ui;
    AstraScrollModel scroll;
    DesktopUsageRow *rows;
    uint32_t count, crc, pointer_shape;
} DesktopUsage;

static void usage_close(DesktopUsage *usage)
{
    if (astra_window_event_wait_handle(&usage->window) != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_window_close(&usage->window); (void)ignored;
    }
    if (usage->surface.area != 0u) (void)astra_shared_surface_close(&usage->surface);
    for (uint32_t at = 0u; at < usage->count; ++at) astra_runtime_deallocate(usage->rows[at].text);
    astra_runtime_deallocate(usage->rows);
    memset(usage, 0, sizeof(*usage));
}

static void usage_escape(AstraString *out, const char *bytes, uint32_t length)
{
    for (uint32_t at = 0u; at < length; ++at) {
        unsigned char byte = (unsigned char)bytes[at];
        if (byte < 32u || byte == 127u) {
            (void)astra_string_append(out, "\\x");
            (void)astra_string_append_hex(out, byte, 2u);
        } else if (byte == '\\') (void)astra_string_append(out, "\\\\");
        else (void)astra_string_append_char(out, (char)byte);
    }
}

/* Return one complete UTF-8 line; measuring and painting use the same font
 * primitive. Newlines separate metadata from the complete escaped pathname. */
static AstraResult usage_rows(DesktopUsage *usage, const AstraVolumeUsageSnapshot *snapshot,
    const AstraProcSnapshot *processes, uint32_t process_count)
{
    static const char *const kinds[] = {"?", "file", "directory", "directory root", "active operation", "pending share", "image source"};
    if (snapshot->count > UINT32_MAX / sizeof(*usage->rows)) return ASTRA_ERROR_OUT_OF_MEMORY;
    usage->rows = astra_runtime_callocate(snapshot->count == 0u ? 1u : snapshot->count, sizeof(*usage->rows));
    if (usage->rows == NULL) return ASTRA_ERROR_OUT_OF_MEMORY;
    uint64_t y = 0u;
    uint32_t crc = UINT32_MAX;
    for (uint32_t at = 0u; at < snapshot->count; ++at) {
        const AstraVolumeUsageRecord *row = &snapshot->records[at];
        DesktopUsageRow *text = &usage->rows[at];
        uint32_t capacity = 512u + row->path_length * 4u;
        text->text = astra_runtime_allocate(capacity);
        if (text->text == NULL) return ASTRA_ERROR_OUT_OF_MEMORY;
        usage->count = at + 1u;
        AstraString out;
        astra_string_init(&out, text->text, capacity);
        (void)astra_string_append(&out, "PID "); (void)astra_string_append_u64(&out, row->process);
        const char *name = row->process == 0u ? "Storage provider" : "Process";
        for (uint32_t process = 0u; process < process_count; ++process)
            if (processes[process].process.size == sizeof(AstraProcessInfo) && processes[process].process.id == row->process &&
                memchr(processes[process].name, '\0', sizeof(processes[process].name)) != NULL &&
                astra_utf8_validate(processes[process].name, (uint32_t)strlen(processes[process].name), 0u)) name = processes[process].name;
        (void)astra_string_append(&out, " ("); usage_escape(&out, name, (uint32_t)strlen(name));
        (void)astra_string_append(&out, ") — "); (void)astra_string_append(&out, kinds[row->kind]);
        (void)astra_string_append(&out, " #"); (void)astra_string_append_u64(&out, row->handle);
        (void)astra_string_append(&out, " — ");
        (void)astra_string_append(&out, row->rights == 0u ? "access not applicable" :
            row->rights == ASTRA_RIGHT_WRITE ? "write only" :
            (row->rights & ASTRA_RIGHT_WRITE) != 0u ? "read/write" : "read only");
        (void)astra_string_append(&out, " — inode "); (void)astra_string_append_u64(&out, row->inode);
        if (row->flags & ASTRA_VOLUME_USAGE_LOST) (void)astra_string_append(&out, " [lost]");
        if (row->flags & ASTRA_VOLUME_USAGE_CLEANUP_PENDING) (void)astra_string_append(&out, " [cleanup pending]");
        if (row->flags & ASTRA_VOLUME_USAGE_ACTIVE) (void)astra_string_append(&out, " [active]");
        (void)astra_string_append_char(&out, '\n');
        if (row->flags & ASTRA_VOLUME_USAGE_UNNAMED) (void)astra_string_append(&out,
            (row->flags & ASTRA_VOLUME_USAGE_INVALID_NAME) != 0u ? "[Retained object: name cannot be represented]" : "[Unnamed retained object]");
        else usage_escape(&out, row->path, row->path_length);
        if (out.truncated) return ASTRA_ERROR_IO;
        text->length = out.length; text->y = (uint32_t)y;
        crc = astra_crc32_update(crc, text->text, text->length + 1u);
        uint32_t offset = 0u, lines = 0u;
        while (offset < text->length) {
            uint32_t advance;
            (void)usage_line(text->text + offset, text->length - offset, &advance);
            if (advance == 0u) return ASTRA_ERROR_IO;
            offset += advance; ++lines;
        }
        text->height = lines * DESKTOP_USAGE_LINE_HEIGHT + 12u;
        y += text->height;
        if (y > INT32_MAX) return ASTRA_ERROR_NO_RESOURCES;
    }
    usage->crc = ~crc;
    AstraScrollModelInfo model = ASTRA_SCROLL_MODEL_INFO_INIT;
    model.line_height = DESKTOP_USAGE_LINE_HEIGHT;
    AstraResult result = astra_scroll_init(&usage->scroll, &model);
    AstraContainerInfo row = ASTRA_CONTAINER_INFO_INIT, canvas = ASTRA_CONTAINER_INFO_INIT;
    AstraScrollViewInfo view = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraScrollbarInfo bar = ASTRA_SCROLLBAR_INFO_INIT;
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    row.id = 1u; row.layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    if (result == ASTRA_OK) result = astra_interface_container_init(&usage->controls[0], &row);
    item.minimum_height = item.maximum_height = DESKTOP_USAGE_VIEW_HEIGHT;
    if (result == ASTRA_OK) result = astra_interface_control_set_flex(&usage->controls[0], &item);
    view.id = 2u; view.model = &usage->scroll;
    view.preferred_width = DESKTOP_USAGE_VIEW_WIDTH; view.preferred_height = DESKTOP_USAGE_VIEW_HEIGHT;
    if (result == ASTRA_OK) result = astra_interface_scroll_view_init(&usage->controls[1], &view);
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT; item.parent_id = 1u; item.grow = 1u;
    if (result == ASTRA_OK) result = astra_interface_control_set_flex(&usage->controls[1], &item);
    canvas.id = 3u;
    if (result == ASTRA_OK) result = astra_interface_container_init(&usage->controls[2], &canvas);
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT; item.parent_id = 2u;
    item.minimum_width = item.maximum_width = DESKTOP_USAGE_VIEW_WIDTH;
    item.minimum_height = item.maximum_height = y == 0u ? DESKTOP_USAGE_LINE_HEIGHT : (uint32_t)y;
    if (result == ASTRA_OK) result = astra_interface_control_set_flex(&usage->controls[2], &item);
    bar.id = 4u; bar.model = &usage->scroll;
    if (result == ASTRA_OK) result = astra_interface_scrollbar_init(&usage->controls[3], &bar);
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT; item.parent_id = 1u;
    item.minimum_width = item.maximum_width = 12u;
    if (result == ASTRA_OK) result = astra_interface_control_set_flex(&usage->controls[3], &item);
    button.id = DESKTOP_USAGE_CLOSE_ID; button.text = "Close"; button.text_length = 5u; button.variant = ASTRA_BUTTON_PRIMARY;
    if (result == ASTRA_OK) result = astra_interface_button_init(&usage->controls[4], &button);
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT; item.minimum_width = 96u; item.minimum_height = 32u;
    item.align_self = ASTRA_FLEX_ALIGN_END;
    if (result == ASTRA_OK) result = astra_interface_control_set_flex(&usage->controls[4], &item);
    if (result == ASTRA_OK) result = astra_interface_ui_init(&usage->ui, usage->controls, 5u, DESKTOP_USAGE_WIDTH, DESKTOP_USAGE_HEIGHT);
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    layout.direction = ASTRA_FLEX_COLUMN; layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    layout.padding_left = layout.padding_right = 20u; layout.padding_top = 60u; layout.padding_bottom = 16u;
    layout.main_gap = 12u;
    if (result == ASTRA_OK) result = astra_interface_ui_layout(&usage->ui, &layout);
    return result;
}

static AstraResult usage_paint(DesktopUsage *usage)
{
    AstraSurfaceView *surface = &usage->surface.view;
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    if (!astra_draw_list_view_init(surface, surface->pixels, surface->byte_size, DESKTOP_USAGE_WIDTH, DESKTOP_USAGE_HEIGHT))
        return ASTRA_ERROR_IO;
    astra_surface_clear(surface, astra_surface_rgb565(theme.client.red, theme.client.green, theme.client.blue));
    uint16_t foreground = astra_surface_rgb565(theme.client_text.red, theme.client_text.green, theme.client_text.blue);
    static const char intro[] = "Close these resources before unmounting the volume:";
    astra_surface_ui_text(surface, 20, 28, intro, sizeof(intro) - 1u,
        ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT, foreground);
    AstraResult result = astra_interface_ui_render(&usage->ui, surface);
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
    if (result == ASTRA_OK) result = astra_scroll_get_state(&usage->scroll, &state);
    if (result != ASTRA_OK) return result;
    (void)astra_surface_clip(surface, 20, 60, DESKTOP_USAGE_VIEW_WIDTH, DESKTOP_USAGE_VIEW_HEIGHT);
    uint32_t first = UINT32_MAX, last = UINT32_MAX;
    if (usage->count == 0u) {
        static const char changed[] = "The holders changed during inspection. Close this dialog and retry.";
        astra_surface_ui_text(surface, 26, 80, changed, sizeof(changed) - 1u,
            ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT, foreground);
    }
    for (uint32_t at = 0u; at < usage->count; ++at) {
        DesktopUsageRow *row = &usage->rows[at];
        int32_t y = 60 + (int32_t)row->y - state.offset_y;
        if (y + (int32_t)row->height <= 60 || y >= 60 + (int32_t)DESKTOP_USAGE_VIEW_HEIGHT) continue;
        if (first == UINT32_MAX) first = at;
        last = at;
        uint32_t offset = 0u;
        while (offset < row->length) {
            uint32_t advance, fit = usage_line(row->text + offset, row->length - offset, &advance);
            astra_surface_ui_text(surface, 26, y + 16, row->text + offset, fit,
                ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT, foreground);
            offset += advance; y += DESKTOP_USAGE_LINE_HEIGHT;
        }
    }
    (void)astra_surface_clip(surface, 0, 0, DESKTOP_USAGE_WIDTH, DESKTOP_USAGE_HEIGHT);
    /* Bounded diagnostic identifies the actual presentation without publishing
     * privileged process names or paths into the general application log. */
    char line[128]; AstraString log;
    astra_string_init(&log, line, sizeof(line));
    (void)astra_string_append(&log, "desktop usage rendered ");
    const uint32_t values[] = {usage->count, usage->crc, state.offset_y, state.maximum_y, first, last};
    for (uint32_t at = 0u; at < sizeof(values) / sizeof(values[0]); ++at) {
        if (at != 0u) (void)astra_string_append_char(&log, ' ');
        (void)astra_string_append_u64(&log, values[at]);
    }
    (void)astra_log(line);
    astra_interface_ui_damage_clear(&usage->ui);
    return ASTRA_OK;
}

static AstraResult usage_open(DesktopUsage *usage, AstraHandle gui, AstraHandle inspector,
    AstraProcessFilesystem *filesystem, const AstraVolumeReference *reference)
{
    usage_close(usage);
    AstraVolumeUsageSnapshot snapshot = ASTRA_VOLUME_USAGE_SNAPSHOT_INIT;
    AstraResult result = ASTRA_ERROR_BUSY;
    for (uint32_t retry = 0u; retry < 8u && result == ASTRA_ERROR_BUSY; ++retry) {
        result = astra_volume_usage_snapshot_open(inspector, reference, &snapshot);
        if (result == ASTRA_ERROR_BUSY) (void)astra_rt_thread_sleep(UINT64_C(50000000), ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
    }
    AstraProcSnapshot *processes = NULL;
    uint32_t bytes = 0u;
    if (result == ASTRA_OK && (astra_process_read_file_alloc(filesystem, "/proc/snapshot", (void **)&processes, &bytes) != ASTRA_VFS_OK ||
        bytes % sizeof(*processes) != 0u)) { astra_runtime_deallocate(processes); processes = NULL; bytes = 0u; }
    if (result == ASTRA_OK) result = usage_rows(usage, &snapshot, processes, bytes / sizeof(*processes));
    astra_runtime_deallocate(processes);
    astra_volume_usage_snapshot_close(&snapshot);
    if (result == ASTRA_OK) result = astra_result_from_syscall(astra_shared_draw_list_create(&usage->surface, DESKTOP_USAGE_WIDTH, DESKTOP_USAGE_HEIGHT));
    if (result == ASTRA_OK) result = usage_paint(usage);
    AstraWindowCreateInfo create = ASTRA_WINDOW_CREATE_INFO_INIT;
    create.x = (ASTRA_DISPLAY_WIDTH - DESKTOP_USAGE_WIDTH) / 2u; create.y = (ASTRA_DISPLAY_HEIGHT - DESKTOP_USAGE_HEIGHT) / 2u;
    create.width = DESKTOP_USAGE_WIDTH; create.height = DESKTOP_USAGE_HEIGHT;
    create.flags = ASTRA_WINDOW_MODAL; create.type = ASTRA_WINDOW_DIALOG;
    create.title = "Volume in use"; create.title_length = 13u; create.content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST;
    create.event_mask = ASTRA_WINDOW_SUBSCRIBE_CLOSE_REQUEST | ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION |
        ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON | ASTRA_WINDOW_SUBSCRIBE_POINTER_WHEEL | ASTRA_WINDOW_SUBSCRIBE_KEY | ASTRA_WINDOW_SUBSCRIBE_STATE;
    if (result == ASTRA_OK) result = astra_window_create(gui, usage->surface.area, &create, &usage->window);
    if (result == ASTRA_OK) result = astra_window_activate(&usage->window);
    usage->pointer_shape = UINT32_MAX;
    if (result != ASTRA_OK) usage_close(usage);
    return result;
}

static AstraResult usage_event(DesktopUsage *usage)
{
    AstraWindowEvent event = {0};
    AstraResult result = astra_window_event_try(&usage->window, &event);
    if (result == ASTRA_ERROR_WOULD_BLOCK) return ASTRA_OK;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    int close = event.type == ASTRA_WINDOW_EVENT_CLOSE_REQUEST || (event.type == ASTRA_WINDOW_EVENT_KEY &&
        (event.flags & ASTRA_WINDOW_EVENT_DOWN) != 0u && event.data.key.usage == 0x29u);
    if (result == ASTRA_OK && !close)
        result = astra_interface_ui_handle_event(&usage->ui, &event, &action);
    if (result == ASTRA_OK && !close) result = astra_interface_ui_update_pointer(&usage->ui, &usage->window,
        ASTRA_POINTER_SHAPE_AUTOMATIC, &usage->pointer_shape);
    if (result != ASTRA_OK || close ||
        (action.type == ASTRA_UI_ACTION_ACTIVATE && action.control_id == DESKTOP_USAGE_CLOSE_ID)) {
        usage_close(usage); return result;
    }
    AstraControlFrame damage;
    if (astra_interface_ui_damage(&usage->ui, &damage) == ASTRA_OK) {
        result = usage_paint(usage);
        if (result == ASTRA_OK) result = astra_window_present(&usage->window);
    }
    return result;
}
#endif
