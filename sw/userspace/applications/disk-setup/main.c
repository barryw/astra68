#include <astra/control.h>
#include <astra/dos_library.h>
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
#include <astra/text_model.h>
#include <astra/theme.h>
#include <astra/volume.h>
#include <astra/window.h>

#include <stdint.h>
#include <string.h>

#define DISK_WIDTH 760u
#define DISK_HEIGHT 700u
#define DISK_ROW_BYTES 176u
#define DISK_MIB UINT64_C(1048576)

enum {
    DISK_TITLE = 1u,
    DISK_HINT,
    DISK_BODY,
    DISK_ACTIONS,
    DISK_SCROLL,
    DISK_LIST,
    DISK_DETAILS,
    DISK_REFRESH,
    DISK_MODE,
    DISK_LABEL_CAPTION,
    DISK_LABEL_FIELD,
    DISK_FORMAT_FAT32,
    DISK_FORMAT_EXFAT,
    DISK_CONFIRM_CAPTION,
    DISK_CONFIRM_FIELD,
    DISK_ERASE,
    DISK_CREATE_EXFAT,
    DISK_DELETE_PARTITION,
    DISK_CONTROL_COUNT = DISK_DELETE_PARTITION
};

_Static_assert(DISK_HINT == DISK_TITLE + ASTRA_SETTINGS_PANE_DESCRIPTION &&
    DISK_BODY == DISK_TITLE + ASTRA_SETTINGS_PANE_BODY &&
    DISK_ACTIONS == DISK_TITLE + ASTRA_SETTINGS_PANE_ACTIONS,
    "Disk Setup uses the shared Settings pane composition");

enum {
    DISK_FAIL_CATALOG = ASTRA_STATUS_PROGRAM_FIRST,
    DISK_FAIL_CONTROL,
    DISK_FAIL_SURFACE,
    DISK_FAIL_WINDOW,
    DISK_FAIL_FORMAT
};

ASTRA_PROGRAM("disk-setup", 0, 2, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static AstraHandle volumes;
static AstraHandle usage_service;
static AstraHandle disk_admin;
static AstraVolumeWatch watch = ASTRA_VOLUME_WATCH_INIT;
static AstraVolumeSnapshot snapshot = ASTRA_VOLUME_SNAPSHOT_INIT;
static AstraVolumeUsageSnapshot usage = ASTRA_VOLUME_USAGE_SNAPSHOT_INIT;
static AstraDosDiskJob active_job = {0};
static AstraVolumeReference selected = {0};
static AstraChoiceItem *items;
static uint32_t *indices;
static char *row_text;
static uint32_t row_count, selected_row = UINT32_MAX;
static char details[256];
static char notice[128];
static AstraVolumeReference format_source = {0};
static AstraVolumeReference edit_device = {0};
static uint32_t pending_format;
static uint32_t formatting;
static uint32_t partitioning;
static uint32_t partition_mode;
static uint64_t partition_mib = 4096u;
static AstraTextModel label_model = ASTRA_TEXT_MODEL_INIT;
static AstraTextModel confirm_model = ASTRA_TEXT_MODEL_INIT;
static uint8_t label_content[4096], confirm_content[4096];
static _Alignas(4) uint8_t label_metadata[8192], confirm_metadata[8192];
static AstraScrollModel scroll = ASTRA_SCROLL_MODEL_INIT;
static AstraControl controls[DISK_CONTROL_COUNT];
static AstraUIContext ui = ASTRA_UI_CONTEXT_INIT;
static AstraSharedSurface surface;
static AstraWindow window = ASTRA_WINDOW_INIT;
static const AstraChoiceItem modes[] = {
    {"Format", sizeof("Format") - 1u, {0}},
    {"Partitions", sizeof("Partitions") - 1u, {0}}
};

#define CONTROL(id) (&controls[(id) - 1u])

static const char *class_name(uint32_t value)
{
    switch (value) {
    case ASTRA_VOLUME_FIXED: return "Fixed disk";
    case ASTRA_VOLUME_USB: return "USB disk";
    case ASTRA_VOLUME_OPTICAL: return "Optical drive";
    default: return "Device";
    }
}

static AstraResult field_start(AstraTextModel *model, const char *value,
    uint8_t *content, uint32_t content_bytes, uint8_t *metadata,
    uint32_t metadata_bytes)
{
    AstraTextModelInfo info = ASTRA_TEXT_MODEL_INFO_INIT;
    info.text = value;
    info.text_bytes = (uint32_t)strlen(value);
    info.content_arena = content;
    info.content_arena_bytes = content_bytes;
    info.metadata_arena = metadata;
    info.metadata_arena_bytes = metadata_bytes;
    info.selection.anchor = info.text_bytes;
    info.selection.focus = info.text_bytes;
    return astra_text_model_init(model, &info);
}

static AstraResult field_text(AstraTextModel *model, char *text, uint32_t capacity)
{
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    uint32_t copied = 0u;
    if (astra_text_model_get_state(model, &state) != ASTRA_OK ||
        state.text_bytes >= capacity ||
        astra_text_model_copy(model, 0u, state.text_bytes, text, capacity - 1u,
            &copied) != ASTRA_OK || copied != state.text_bytes)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    text[copied] = '\0';
    return ASTRA_OK;
}

static const AstraVolumeRecord *editable_device(const AstraVolumeRecord *record)
{
    if (record == NULL) return NULL;
    if (record->kind == ASTRA_VOLUME_DEVICE) return record;
    if (record->kind != ASTRA_VOLUME_SOURCE ||
        record->origin != ASTRA_VOLUME_ORIGIN_DEVICE) return NULL;
    const AstraVolumeReference reference = {snapshot.epoch, record->id,
        record->generation};
    return astra_volume_snapshot_ancestor(&snapshot, &reference,
        ASTRA_VOLUME_DEVICE);
}

static int allowed_size(const AstraVolumeRecord *record, uint32_t operation,
    uint64_t size_bytes)
{
    if (record == NULL) return 0;
    const AstraDosDiskSnapshot view = {snapshot, usage};
    const AstraDosDiskRequest request = {
        {snapshot.epoch, record->id, record->generation}, operation,
        size_bytes, "ASTRA"};
    return astra_dos_disk_check(&view, &request) == ASTRA_OK;
}

static int allowed(const AstraVolumeRecord *record, uint32_t operation)
{
    return allowed_size(record, operation, partition_mib * DISK_MIB);
}

static void dispose_rows(void)
{
    astra_runtime_deallocate(items);
    astra_runtime_deallocate(indices);
    astra_runtime_deallocate(row_text);
    items = NULL;
    indices = NULL;
    row_text = NULL;
    row_count = 0u;
    selected_row = UINT32_MAX;
}

static void append_row(uint32_t at, AstraVolumeRecord *record)
{
    char *text = row_text + (size_t)row_count * DISK_ROW_BYTES;
    AstraString label;
    astra_string_init(&label, text, DISK_ROW_BYTES);
    if (record->kind == ASTRA_VOLUME_DEVICE) {
        (void)astra_string_append(&label, class_name(record->class_id));
        (void)astra_string_append(&label, "  |  ");
        (void)astra_string_append_u64(&label, record->capacity_bytes);
        (void)astra_string_append(&label, " bytes  |  ");
        (void)astra_string_append(&label,
            astra_volume_partition_map_name(record->partition_map));
    } else {
        if (record->partition_number == 0u)
            (void)astra_string_append(&label, "    Whole disk");
        else {
            (void)astra_string_append(&label, "    Partition ");
            (void)astra_string_append_u64(&label, record->partition_number);
        }
        (void)astra_string_append(&label, "  |  ");
        (void)astra_string_append(&label,
            astra_volume_source_issue(record) == ASTRA_VOLUME_SOURCE_NDOS ?
                "NDOS" : record->format == ASTRA_VOLUME_UNSUPPORTED ?
                record->detected_type : astra_volume_format_name(record->format));
        (void)astra_string_append(&label, "  |  ");
        (void)astra_string_append_u64(&label, record->capacity_bytes);
        (void)astra_string_append(&label, " bytes");
        if (record->label[0] != '\0') {
            (void)astra_string_append(&label, "  |  ");
            (void)astra_string_append(&label, record->label);
        }
    }
    items[row_count] = (AstraChoiceItem){text, label.length, {0u, 0u}};
    indices[row_count] = at;
    const AstraVolumeReference reference = {snapshot.epoch, record->id,
        record->generation};
    if (selected.epoch == reference.epoch && selected.id == reference.id &&
        selected.generation == reference.generation)
        selected_row = row_count;
    ++row_count;
}

static uint32_t collect_rows(void)
{
    uint32_t capacity = snapshot.count;
    if (capacity > SIZE_MAX / DISK_ROW_BYTES)
        return DISK_FAIL_CATALOG;
    if (capacity != 0u) {
        items = astra_runtime_callocate(capacity, sizeof(*items));
        indices = astra_runtime_callocate(capacity, sizeof(*indices));
        row_text = astra_runtime_callocate(capacity, DISK_ROW_BYTES);
        if (items == NULL || indices == NULL || row_text == NULL) {
            dispose_rows();
            return DISK_FAIL_CATALOG;
        }
    }
    if (selected.id != 0u && selected.epoch == snapshot.epoch) {
        for (uint32_t at = 0u; at < snapshot.count; ++at)
            if (snapshot.records[at].id == selected.id &&
                snapshot.records[at].state != ASTRA_VOLUME_LOST) {
                selected.generation = snapshot.records[at].generation;
                break;
            }
        const AstraVolumeRecord *target = astra_volume_snapshot_find(&snapshot, &selected);
        if (target != NULL && target->kind == ASTRA_VOLUME_MOUNT) {
            const AstraVolumeRecord *source = astra_volume_snapshot_ancestor(
                &snapshot, &selected, ASTRA_VOLUME_SOURCE);
            if (source != NULL && source->origin == ASTRA_VOLUME_ORIGIN_DEVICE)
                selected = (AstraVolumeReference){snapshot.epoch, source->id,
                    source->generation};
        }
    }
    for (uint32_t device = 0u; device < snapshot.count; ++device) {
        AstraVolumeRecord *record = &snapshot.records[device];
        if (record->kind != ASTRA_VOLUME_DEVICE) continue;
        append_row(device, record);
        for (uint32_t source = 0u; source < snapshot.count; ++source) {
            AstraVolumeRecord *partition = &snapshot.records[source];
            if (partition->kind != ASTRA_VOLUME_SOURCE ||
                partition->origin != ASTRA_VOLUME_ORIGIN_DEVICE)
                continue;
            const AstraVolumeReference reference = {snapshot.epoch,
                partition->id, partition->generation};
            const AstraVolumeRecord *parent = astra_volume_snapshot_ancestor(
                &snapshot, &reference, ASTRA_VOLUME_DEVICE);
            if (parent == record) append_row(source, partition);
        }
    }
    if (selected_row == UINT32_MAX && selected.id == 0u && row_count != 0u)
        selected_row = 0u;
    if (selected_row != UINT32_MAX) {
        const AstraVolumeRecord *record = &snapshot.records[indices[selected_row]];
        selected = (AstraVolumeReference){snapshot.epoch, record->id,
            record->generation};
    }
    return ASTRA_STATUS_OK;
}

static void describe_selection(void)
{
    AstraString text;
    astra_string_init(&text, details, sizeof(details));
    if (selected_row == UINT32_MAX) {
        (void)astra_string_append(&text, selected.id == 0u ?
            "No physical disks found" : "Selected disk changed or disappeared");
        if (notice[0] != '\0') {
            (void)astra_string_append(&text, ". ");
            (void)astra_string_append(&text, notice);
        }
        return;
    }
    const AstraVolumeRecord *record = &snapshot.records[indices[selected_row]];
    const AstraVolumeRecord *scope = record;
    if (record->kind == ASTRA_VOLUME_SOURCE)
        scope = astra_volume_snapshot_ancestor(&snapshot, &selected,
            ASTRA_VOLUME_DEVICE);
    uint32_t mounted_count = 0u, holder_count = 0u;
    const AstraVolumeUsageRecord *first_holder = NULL;
    for (uint32_t at = 0u; at < snapshot.count; ++at) {
        const AstraVolumeRecord *mount = &snapshot.records[at];
        if (mount->kind != ASTRA_VOLUME_MOUNT ||
            (mount->state != ASTRA_VOLUME_MOUNTED &&
             mount->state != ASTRA_VOLUME_QUIESCING))
            continue;
        const AstraVolumeReference reference = {snapshot.epoch, mount->id,
            mount->generation};
        const AstraVolumeRecord *ancestor = scope == NULL ? NULL :
            astra_volume_snapshot_ancestor(&snapshot, &reference, scope->kind);
        if (ancestor != scope) continue;
        ++mounted_count;
        for (uint32_t held = 0u; held < usage.count; ++held)
            if (usage.records[held].volume.epoch == reference.epoch &&
                usage.records[held].volume.id == reference.id &&
                usage.records[held].volume.generation == reference.generation) {
                if (first_holder == NULL) first_holder = &usage.records[held];
                ++holder_count;
            }
    }
    if (record->kind == ASTRA_VOLUME_DEVICE) {
        (void)astra_string_append(&text, "Disk capacity: ");
        (void)astra_string_append_u64(&text, record->capacity_bytes);
        (void)astra_string_append(&text, " bytes. Partition map: ");
        (void)astra_string_append(&text,
            astra_volume_partition_map_name(record->partition_map));
        (void)astra_string_append(&text,
            (record->flags & ASTRA_VOLUME_PHYSICAL_READ_ONLY) != 0u ?
            ". Write protected" : ". Writable media");
        (void)astra_string_append(&text, ". Mounted: ");
        (void)astra_string_append_u64(&text, mounted_count);
        if (record->state == ASTRA_VOLUME_PARTITIONING)
            (void)astra_string_append(&text, ". Partitioning");
    } else {
        (void)astra_string_append(&text, "Start: ");
        (void)astra_string_append_u64(&text, record->offset_bytes);
        (void)astra_string_append(&text, " bytes. Size: ");
        (void)astra_string_append_u64(&text, record->capacity_bytes);
        (void)astra_string_append(&text, " bytes. ");
        int mounted = 0;
        for (uint32_t at = 0u; at < snapshot.count; ++at)
            if (snapshot.records[at].kind == ASTRA_VOLUME_MOUNT &&
                snapshot.records[at].parent == record->id &&
                snapshot.records[at].parent_generation == record->generation &&
                snapshot.records[at].state == ASTRA_VOLUME_MOUNTED) {
                mounted = 1;
                (void)astra_string_append(&text, snapshot.records[at].name);
                (void)astra_string_append(&text,
                    (snapshot.records[at].flags & ASTRA_VOLUME_READ_ONLY) != 0u ?
                    " (read only)" : " (writable)");
                break;
            }
        if (!mounted) (void)astra_string_append(&text, "Not mounted");
        (void)astra_string_append(&text, ". Device mounts: ");
        (void)astra_string_append_u64(&text, mounted_count);
        if ((record->flags & ASTRA_VOLUME_PHYSICAL_READ_ONLY) != 0u)
            (void)astra_string_append(&text, ". Write protected");
        else if ((record->flags & ASTRA_VOLUME_READ_ONLY) != 0u)
            (void)astra_string_append(&text, ". Filesystem read only");
        if (record->state == ASTRA_VOLUME_FORMATTING)
            (void)astra_string_append(&text, ". Formatting");
    }
    (void)astra_string_append(&text, ". Retained resources: ");
    (void)astra_string_append_u64(&text, holder_count);
    if (first_holder != NULL) {
        (void)astra_string_append(&text, ". PID ");
        (void)astra_string_append_u64(&text, first_holder->process);
        (void)astra_string_append(&text, " uses ");
        (void)astra_string_append(&text, first_holder->path);
    }
    if (notice[0] != '\0') {
        (void)astra_string_append(&text, ". ");
        (void)astra_string_append(&text, notice);
    }
}

static uint32_t build_controls(void)
{
    AstraSettingsPaneInfo pane = ASTRA_SETTINGS_PANE_INFO_INIT;
    AstraScrollViewInfo view = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraChoiceInfo list = ASTRA_CHOICE_INFO_INIT;
    AstraChoiceInfo tabs = ASTRA_CHOICE_INFO_INIT;
    AstraLabelInfo detail = ASTRA_LABEL_INFO_INIT;
    AstraLabelInfo caption = ASTRA_LABEL_INFO_INIT;
    AstraButtonInfo refresh = ASTRA_BUTTON_INFO_INIT;
    AstraButtonInfo action = ASTRA_BUTTON_INFO_INIT;
    AstraFieldInfo field = ASTRA_FIELD_INFO_INIT;
    AstraStepperInfo stepper = ASTRA_STEPPER_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    pane.first_id = DISK_TITLE;
    pane.title = "Disk Setup";
    pane.title_length = sizeof("Disk Setup") - 1u;
    pane.description = "Inspect and manage physical disks and partitions";
    pane.description_length = (uint32_t)strlen(pane.description);
    if (astra_interface_settings_pane_init(controls, &pane) != ASTRA_OK)
        return DISK_FAIL_CONTROL;
    view.id = DISK_SCROLL;
    view.model = &scroll;
    view.preferred_width = DISK_WIDTH - 48u;
    view.preferred_height = partition_mode ? 110u : 160u;
    if (astra_interface_scroll_view_init(CONTROL(DISK_SCROLL), &view) != ASTRA_OK)
        return DISK_FAIL_CONTROL;
    list.id = DISK_LIST;
    list.items = items;
    list.item_count = row_count;
    list.selected = selected_row;
    if (astra_interface_list_init(CONTROL(DISK_LIST), &list) != ASTRA_OK)
        return DISK_FAIL_CONTROL;
    describe_selection();
    detail.id = DISK_DETAILS;
    detail.text = details;
    detail.text_length = (uint32_t)strlen(details);
    if (astra_interface_label_init(CONTROL(DISK_DETAILS), &detail) != ASTRA_OK)
        return DISK_FAIL_CONTROL;
    refresh.id = DISK_REFRESH;
    refresh.text = "Refresh";
    refresh.text_length = sizeof("Refresh") - 1u;
    if (astra_interface_button_init(CONTROL(DISK_REFRESH), &refresh) != ASTRA_OK)
        return DISK_FAIL_CONTROL;
    tabs.id = DISK_MODE;
    tabs.items = modes;
    tabs.item_count = 2u;
    tabs.selected = partition_mode;
    if (astra_interface_tab_init(CONTROL(DISK_MODE), &tabs) != ASTRA_OK)
        return DISK_FAIL_CONTROL;
    caption.id = DISK_LABEL_CAPTION;
    caption.text = partition_mode ? "Type ERASE before changing selected disk" :
        "Volume name (1-11 letters, digits, spaces or underscores)";
    caption.text_length = (uint32_t)strlen(caption.text);
    if (astra_interface_label_init(CONTROL(DISK_LABEL_CAPTION), &caption) != ASTRA_OK)
        return DISK_FAIL_CONTROL;
    field.id = DISK_LABEL_FIELD;
    field.model = partition_mode ? &confirm_model : &label_model;
    field.preferred_columns = 20u;
    field.state = formatting || partitioning ? ASTRA_CONTROL_DISABLED : 0u;
    if (astra_interface_field_init(CONTROL(DISK_LABEL_FIELD), &field) != ASTRA_OK)
        return DISK_FAIL_CONTROL;
    const AstraVolumeRecord *chosen = selected_row == UINT32_MAX ? NULL :
        &snapshot.records[indices[selected_row]];
    const AstraVolumeRecord *disk = editable_device(chosen);
    uint64_t maximum = disk != NULL && disk->capacity_bytes > 2u * DISK_MIB ?
        disk->capacity_bytes / DISK_MIB - 2u : partition_mib;
    if (maximum < 2u) maximum = 2u;
    if (maximum > INT64_MAX) maximum = INT64_MAX;
    if (partition_mode && disk != NULL && partition_mib > maximum)
        partition_mib = maximum;
    int idle = !formatting && !partitioning;
    int can_format = idle && allowed(chosen, ASTRA_DOS_DISK_FORMAT_FAT32);
    int can_init = idle && allowed(chosen, ASTRA_DOS_DISK_INITIALIZE_MBR);
    int can_create = idle && allowed_size(chosen,
        ASTRA_DOS_DISK_CREATE_FAT32, DISK_MIB);
    int can_create_selected = idle && allowed(chosen,
        ASTRA_DOS_DISK_CREATE_FAT32);
    int can_delete = idle && allowed(chosen, ASTRA_DOS_DISK_DELETE_PARTITION);
    action.id = DISK_FORMAT_FAT32;
    action.text = partition_mode ? "Initialize MBR map" : "Prepare FAT32 format";
    action.text_length = (uint32_t)strlen(action.text);
    action.state = (partition_mode ? can_init : can_format) ? 0u : ASTRA_CONTROL_DISABLED;
    if (astra_interface_button_init(CONTROL(DISK_FORMAT_FAT32), &action) != ASTRA_OK)
        return DISK_FAIL_CONTROL;
    action.id = DISK_FORMAT_EXFAT;
    action.text = partition_mode ? "Initialize GPT map" : "Prepare exFAT format";
    action.text_length = (uint32_t)strlen(action.text);
    if (astra_interface_button_init(CONTROL(DISK_FORMAT_EXFAT), &action) != ASTRA_OK)
        return DISK_FAIL_CONTROL;
    caption.id = DISK_CONFIRM_CAPTION;
    caption.text = partition_mode ? "New partition size (MiB); first free extent" :
        "For selected source, type ERASE to confirm";
    caption.text_length = (uint32_t)strlen(caption.text);
    if (astra_interface_label_init(CONTROL(DISK_CONFIRM_CAPTION), &caption) != ASTRA_OK)
        return DISK_FAIL_CONTROL;
    if (partition_mode) {
        stepper.id = DISK_CONFIRM_FIELD;
        stepper.minimum = 1;
        stepper.maximum = (int64_t)maximum;
        stepper.value = (int64_t)partition_mib;
        stepper.step = 1;
        stepper.state = can_create ? 0u : ASTRA_CONTROL_DISABLED;
        if (astra_interface_stepper_init(CONTROL(DISK_CONFIRM_FIELD),
            &stepper) != ASTRA_OK) return DISK_FAIL_CONTROL;
    } else {
        field.id = DISK_CONFIRM_FIELD;
        field.model = &confirm_model;
        field.preferred_columns = 20u;
        field.state = pending_format != 0u && idle ? 0u : ASTRA_CONTROL_DISABLED;
        if (astra_interface_field_init(CONTROL(DISK_CONFIRM_FIELD), &field) != ASTRA_OK)
            return DISK_FAIL_CONTROL;
    }
    action.id = DISK_ERASE;
    action.text = partition_mode ? "Create FAT32 partition" :
        "Erase and format selected source";
    action.text_length = (uint32_t)strlen(action.text);
    action.state = (partition_mode ? can_create_selected :
        pending_format != 0u && idle) ? 0u : ASTRA_CONTROL_DISABLED;
    if (astra_interface_button_init(CONTROL(DISK_ERASE), &action) != ASTRA_OK)
        return DISK_FAIL_CONTROL;
    if (partition_mode) {
        action.id = DISK_CREATE_EXFAT;
        action.text = "Create exFAT partition";
        action.text_length = (uint32_t)strlen(action.text);
        action.state = can_create_selected ? 0u : ASTRA_CONTROL_DISABLED;
        if (astra_interface_button_init(CONTROL(DISK_CREATE_EXFAT),
            &action) != ASTRA_OK) return DISK_FAIL_CONTROL;
        action.id = DISK_DELETE_PARTITION;
        action.text = "Delete selected partition";
        action.text_length = (uint32_t)strlen(action.text);
        action.state = can_delete ? 0u : ASTRA_CONTROL_DISABLED;
        if (astra_interface_button_init(CONTROL(DISK_DELETE_PARTITION),
            &action) != ASTRA_OK) return DISK_FAIL_CONTROL;
    }
    item.parent_id = DISK_BODY;
    item.grow = 1u;
    item.minimum_height = view.preferred_height;
    if (astra_interface_control_set_flex(CONTROL(DISK_SCROLL), &item) != ASTRA_OK)
        return DISK_FAIL_CONTROL;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = DISK_SCROLL;
    item.minimum_width = view.preferred_width;
    if (astra_interface_control_set_flex(CONTROL(DISK_LIST), &item) != ASTRA_OK)
        return DISK_FAIL_CONTROL;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = DISK_BODY;
    if (astra_interface_control_set_flex(CONTROL(DISK_DETAILS), &item) != ASTRA_OK)
        return DISK_FAIL_CONTROL;
    item.parent_id = DISK_ACTIONS;
    uint32_t control_count = partition_mode ? DISK_CONTROL_COUNT : DISK_ERASE;
    for (uint32_t id = DISK_REFRESH; id <= control_count; ++id)
        if (astra_interface_control_set_flex(CONTROL(id), &item) != ASTRA_OK)
            return DISK_FAIL_CONTROL;
    if (astra_interface_ui_init(&ui, controls, control_count,
        DISK_WIDTH, DISK_HEIGHT) != ASTRA_OK)
        return DISK_FAIL_CONTROL;
    AstraFlexLayout layout = ASTRA_SETTINGS_PANE_LAYOUT_INIT;
    return astra_interface_ui_layout(&ui, &layout) == ASTRA_OK ?
        ASTRA_STATUS_OK : DISK_FAIL_CONTROL;
}

static uint32_t present(void)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    if (!astra_draw_list_view_init(&surface.view, surface.view.pixels,
        surface.view.byte_size, DISK_WIDTH, DISK_HEIGHT))
        return DISK_FAIL_SURFACE;
    astra_surface_clear(&surface.view, astra_surface_rgb565(
        theme.client.red, theme.client.green, theme.client.blue));
    if (astra_interface_ui_render(&ui, &surface.view) != ASTRA_OK)
        return DISK_FAIL_CONTROL;
    const AstraWindowFrame frame = {0u, 0u, DISK_WIDTH, DISK_HEIGHT};
    if (astra_window_present_region(&window, &frame) != ASTRA_OK)
        return DISK_FAIL_WINDOW;
    astra_interface_ui_damage_clear(&ui);
    return ASTRA_STATUS_OK;
}

static uint32_t reload(void)
{
    AstraDosDiskSnapshot fresh = ASTRA_DOS_DISK_SNAPSHOT_INIT;
    AstraResult result = astra_dos_disk_snapshot_open(volumes, usage_service, &fresh);
    if (result == ASTRA_ERROR_BUSY) return ASTRA_STATUS_BUSY;
    if (result != ASTRA_OK) return DISK_FAIL_CATALOG;
    dispose_rows();
    AstraDosDiskSnapshot old = {snapshot, usage};
    astra_dos_disk_snapshot_close(&old);
    snapshot = fresh.volumes;
    usage = fresh.usage;
    uint32_t status = collect_rows();
    if (status == ASTRA_STATUS_OK) status = build_controls();
    if (status == ASTRA_STATUS_OK) status = present();
    return status;
}

static uint32_t clear_confirmation(void)
{
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    return astra_text_model_get_state(&confirm_model, &state) == ASTRA_OK &&
        astra_text_model_replace(&confirm_model, 0u, state.text_bytes, NULL, 0u) == ASTRA_OK ?
        ASTRA_STATUS_OK : DISK_FAIL_CONTROL;
}

static void format_notice(AstraResult result)
{
    const char *message = result == ASTRA_OK ? "Format complete; scanning volume" :
        result == ASTRA_ERROR_BUSY ? "Disk in use; inspect mounted volumes and retained resources" :
        result == ASTRA_ERROR_PERMISSION ? "Protected or write-protected disk; format refused" :
        result == ASTRA_ERROR_INVALID_HANDLE ? "Selected source changed or disappeared" :
        "Format failed; media may be incomplete";
    (void)astra_string_copy(notice, sizeof(notice), message);
}

static uint32_t prepare_format(uint32_t format)
{
    if (selected_row == UINT32_MAX ||
        !allowed(&snapshot.records[indices[selected_row]], format)) {
        (void)astra_string_copy(notice, sizeof(notice),
            "Source unavailable or in use; inspect mounted volumes");
        return reload();
    }
    if (clear_confirmation() != ASTRA_STATUS_OK) return DISK_FAIL_CONTROL;
    pending_format = format;
    format_source = selected;
    (void)astra_string_copy(notice, sizeof(notice),
        "Type ERASE and press erase button; all files on selected source will be lost");
    return build_controls() == ASTRA_STATUS_OK ? present() : DISK_FAIL_CONTROL;
}

static uint32_t start_format(void)
{
    char word[6], label[12];
    if (pending_format == 0u || formatting ||
        field_text(&confirm_model, word, sizeof(word)) != ASTRA_OK ||
        strcmp(word, "ERASE") != 0) {
        (void)astra_string_copy(notice, sizeof(notice), "Type ERASE before formatting");
        return build_controls() == ASTRA_STATUS_OK ? present() : DISK_FAIL_CONTROL;
    }
    if (field_text(&label_model, label, sizeof(label)) != ASTRA_OK) {
        (void)astra_string_copy(notice, sizeof(notice), "Volume name must fit 11 characters");
        return build_controls() == ASTRA_STATUS_OK ? present() : DISK_FAIL_CONTROL;
    }
    uint32_t status = reload();
    if (status != ASTRA_STATUS_OK) return status;
    if (selected_row == UINT32_MAX || selected.epoch != format_source.epoch ||
        selected.id != format_source.id || selected.generation != format_source.generation ||
        !allowed(&snapshot.records[indices[selected_row]], pending_format)) {
        pending_format = 0u;
        (void)astra_string_copy(notice, sizeof(notice),
            "Selection changed or source is in use; format cancelled");
        return build_controls() == ASTRA_STATUS_OK ? present() : DISK_FAIL_CONTROL;
    }
    const AstraDosDiskSnapshot view = {snapshot, usage};
    const AstraDosDiskRequest request = {format_source, pending_format, 0u, label};
    AstraResult result = astra_dos_disk_submit(disk_admin, &view, &request,
        &active_job);
    if (result != ASTRA_OK) {
        format_notice(result);
        pending_format = 0u;
    } else {
        formatting = 1u;
        (void)astra_string_copy(notice, sizeof(notice),
            "Formatting selected source; do not remove media");
    }
    return build_controls() == ASTRA_STATUS_OK ? present() : DISK_FAIL_CONTROL;
}

static void partition_notice(AstraResult result)
{
    const char *message = result == ASTRA_OK ?
        "Disk map updated; scanning partitions. Format new partitions before use" :
        result == ASTRA_ERROR_BUSY ?
        "Disk in use; unmount every partition and inspect retained resources" :
        result == ASTRA_ERROR_PERMISSION ?
        "Protected or write-protected disk; edit refused" :
        result == ASTRA_ERROR_INVALID_HANDLE ?
        "Selected disk changed or disappeared" :
        "Disk edit failed; map may be incomplete. Inspect before retrying";
    (void)astra_string_copy(notice, sizeof(notice), message);
}

static uint32_t start_partition(uint32_t action)
{
    char word[6];
    if (!partition_mode || formatting || partitioning ||
        field_text(&confirm_model, word, sizeof(word)) != ASTRA_OK ||
        strcmp(word, "ERASE") != 0) {
        (void)astra_string_copy(notice, sizeof(notice),
            "Type ERASE before changing the disk map");
        return build_controls() == ASTRA_STATUS_OK ? present() : DISK_FAIL_CONTROL;
    }
    AstraVolumeReference expected = selected;
    uint32_t status = reload();
    if (status != ASTRA_STATUS_OK) return status;
    if (selected_row == UINT32_MAX || selected.epoch != expected.epoch ||
        selected.id != expected.id || selected.generation != expected.generation) {
        (void)astra_string_copy(notice, sizeof(notice),
            "Selection changed; disk edit cancelled");
        return build_controls() == ASTRA_STATUS_OK ? present() : DISK_FAIL_CONTROL;
    }
    uint32_t operation;
    if (action == DISK_FORMAT_FAT32 || action == DISK_FORMAT_EXFAT) {
        operation = action == DISK_FORMAT_FAT32 ?
            ASTRA_DOS_DISK_INITIALIZE_MBR : ASTRA_DOS_DISK_INITIALIZE_GPT;
    } else if (action == DISK_ERASE || action == DISK_CREATE_EXFAT) {
        operation = action == DISK_ERASE ?
            ASTRA_DOS_DISK_CREATE_FAT32 : ASTRA_DOS_DISK_CREATE_EXFAT;
    } else if (action == DISK_DELETE_PARTITION) {
        operation = ASTRA_DOS_DISK_DELETE_PARTITION;
    } else return DISK_FAIL_CONTROL;
    const AstraDosDiskSnapshot view = {snapshot, usage};
    const AstraDosDiskRequest request = {selected, operation,
        partition_mib * DISK_MIB, NULL};
    AstraResult result = astra_dos_disk_submit(disk_admin, &view, &request,
        &active_job);
    if (result != ASTRA_OK) partition_notice(result);
    else {
        edit_device = active_job.reference;
        partitioning = 1u;
        (void)clear_confirmation();
        (void)astra_string_copy(notice, sizeof(notice),
            "Updating disk map; do not remove media");
    }
    return build_controls() == ASTRA_STATUS_OK ? present() : DISK_FAIL_CONTROL;
}

static uint32_t run(AstraHandle gui)
{
    uint32_t volume_refresh = 1u;
    uint64_t volume_retry = 0u;
    uint64_t work_poll_at = 0u;
    for (;;) {
        uint32_t handles[2] = {astra_window_event_wait_handle(&window),
            watch.receive};
        uint32_t selected_handle = 0u;
        uint64_t deadline = volume_refresh ? volume_retry : ASTRA_DEADLINE_FOREVER;
        if ((formatting || partitioning) && work_poll_at < deadline)
            deadline = work_poll_at;
        uint32_t waited = astra_wait_multiple(handles, 2u, deadline,
            &selected_handle, NULL);
        if (waited == ASTRA_SYSCALL_PEER_DEAD && selected_handle == 1u)
            return DISK_FAIL_CATALOG;
        if (waited != ASTRA_SYSCALL_OK && waited != ASTRA_SYSCALL_TIMED_OUT)
            return DISK_FAIL_WINDOW;
        if (selected_handle == 1u) {
            AstraVolumeChange change;
            AstraResult result = astra_volume_watch_receive(&watch, &change);
            if (result != ASTRA_OK && result != ASTRA_ERROR_WOULD_BLOCK)
                return DISK_FAIL_CATALOG;
            volume_refresh = 1u;
            volume_retry = 0u;
        }
        if (volume_refresh && astra_clock_monotonic() >= volume_retry) {
            uint32_t status = reload();
            if (status == ASTRA_STATUS_BUSY)
                volume_retry = astra_clock_monotonic() + UINT64_C(50000000);
            else if (status != ASTRA_STATUS_OK) return status;
            else volume_refresh = 0u;
        }
        if ((formatting || partitioning) &&
            astra_clock_monotonic() >= work_poll_at) {
            AstraResult outcome = ASTRA_ERROR_BUSY;
            AstraResult result = astra_dos_disk_status(disk_admin,
                &active_job, &outcome);
            if (result != ASTRA_OK || outcome != ASTRA_ERROR_BUSY) {
                if (partitioning) {
                    partition_notice(result == ASTRA_OK ? outcome : result);
                    if (result == ASTRA_OK && outcome == ASTRA_OK)
                        selected = (AstraVolumeReference){edit_device.epoch,
                            edit_device.id, edit_device.generation + 1u};
                }
                else format_notice(result == ASTRA_OK ? outcome : result);
                partitioning = 0u;
                formatting = 0u;
                pending_format = 0u;
                volume_refresh = 1u;
                volume_retry = 0u;
            } else work_poll_at = astra_clock_monotonic() + UINT64_C(100000000);
        }
        if (waited == ASTRA_SYSCALL_TIMED_OUT || selected_handle == 1u)
            continue;
        AstraWindowEvent event = {0};
        AstraResult result = astra_window_event_try(&window, &event);
        if (result == ASTRA_ERROR_WOULD_BLOCK) continue;
        if (result != ASTRA_OK) return DISK_FAIL_WINDOW;
        if (event.type == ASTRA_WINDOW_EVENT_CLOSE_REQUEST)
            return ASTRA_STATUS_OK;
        if (event.type == ASTRA_WINDOW_EVENT_FONT_CHANGED &&
            astra_fonts_use_system(gui, &event) != ASTRA_OK)
            return DISK_FAIL_CONTROL;
        AstraUIAction action = ASTRA_UI_ACTION_INIT;
        if (astra_interface_ui_handle_event(&ui, &event, &action) != ASTRA_OK)
            return DISK_FAIL_CONTROL;
        if (action.control_id == DISK_LIST &&
            action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
            action.value >= 0 && (uint64_t)action.value < row_count) {
            selected_row = (uint32_t)action.value;
            const AstraVolumeRecord *record = &snapshot.records[indices[selected_row]];
            selected = (AstraVolumeReference){snapshot.epoch, record->id,
                record->generation};
            pending_format = 0u;
            if (clear_confirmation() != ASTRA_STATUS_OK) return DISK_FAIL_CONTROL;
            if (!formatting && !partitioning) notice[0] = '\0';
            if (build_controls() != ASTRA_STATUS_OK || present() != ASTRA_STATUS_OK)
                return DISK_FAIL_CONTROL;
            continue;
        }
        if (action.control_id == DISK_MODE &&
            action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
            (action.value == 0 || action.value == 1)) {
            partition_mode = (uint32_t)action.value;
            pending_format = 0u;
            if (clear_confirmation() != ASTRA_STATUS_OK) return DISK_FAIL_CONTROL;
            notice[0] = '\0';
            if (build_controls() != ASTRA_STATUS_OK || present() != ASTRA_STATUS_OK)
                return DISK_FAIL_CONTROL;
            continue;
        }
        if (partition_mode && action.control_id == DISK_CONFIRM_FIELD &&
            action.type == ASTRA_UI_ACTION_VALUE_CHANGED && action.value > 0) {
            partition_mib = (uint64_t)action.value;
            if (present() != ASTRA_STATUS_OK)
                return DISK_FAIL_CONTROL;
            continue;
        }
        if (action.type == ASTRA_UI_ACTION_ACTIVATE &&
            (action.control_id == DISK_FORMAT_FAT32 || action.control_id == DISK_FORMAT_EXFAT)) {
            uint32_t status = partition_mode ? start_partition(action.control_id) :
                prepare_format(action.control_id == DISK_FORMAT_FAT32 ?
                    ASTRA_DOS_DISK_FORMAT_FAT32 : ASTRA_DOS_DISK_FORMAT_EXFAT);
            if (status != ASTRA_STATUS_OK && status != ASTRA_STATUS_BUSY) return status;
            if (partition_mode) work_poll_at = 0u;
            continue;
        }
        if (action.type == ASTRA_UI_ACTION_ACTIVATE &&
            (action.control_id == DISK_ERASE ||
             (partition_mode && (action.control_id == DISK_CREATE_EXFAT ||
                action.control_id == DISK_DELETE_PARTITION)))) {
            uint32_t status = partition_mode ? start_partition(action.control_id) :
                start_format();
            if (status != ASTRA_STATUS_OK && status != ASTRA_STATUS_BUSY) return status;
            work_poll_at = 0u;
            continue;
        }
        if (action.control_id == DISK_REFRESH &&
            action.type == ASTRA_UI_ACTION_ACTIVATE) {
            volume_refresh = 1u;
            volume_retry = 0u;
            continue;
        }
        if (astra_interface_ui_damage(&ui, &(AstraControlFrame){0}) == ASTRA_OK &&
            present() != ASTRA_STATUS_OK)
            return DISK_FAIL_CONTROL;
    }
}

int astra_main(const AstraStartupInfo *startup)
{
    if (!astra_startup_validate(startup)) return ASTRA_STATUS_INVALID;
    if (startup->argc == 5u) {
        const char *option = astra_startup_argument(startup, 1u);
        if (option == NULL || strcmp(option, "--target") != 0 ||
            !astra_volume_reference_parse(
                astra_startup_argument(startup, 2u),
                astra_startup_argument(startup, 3u),
                astra_startup_argument(startup, 4u), &selected))
            return ASTRA_STATUS_INVALID;
    } else if (startup->argc != 1u) return ASTRA_STATUS_INVALID;
    const AstraStartupCapability *gui = astra_startup_capability(startup,
        ASTRA_CAPABILITY_GUI);
    const AstraStartupCapability *metadata = astra_startup_capability(startup,
        ASTRA_CAPABILITY_VOLUMES);
    const AstraStartupCapability *holders = astra_startup_capability(startup,
        ASTRA_CAPABILITY_VOLUME_USAGE);
    const AstraStartupCapability *administrator = astra_startup_capability(startup,
        ASTRA_CAPABILITY_DISK_ADMIN);
    const AstraStartupCapability *ready = astra_startup_capability(startup,
        ASTRA_CAPABILITY_SERVICE_READY);
    if (gui == NULL || metadata == NULL || holders == NULL || administrator == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    volumes = metadata->handle;
    usage_service = holders->handle;
    disk_admin = administrator->handle;
    uint32_t status = astra_fonts_use_system(gui->handle, NULL) == ASTRA_OK ?
        ASTRA_STATUS_OK : DISK_FAIL_CONTROL;
    AstraScrollModelInfo scroll_info = ASTRA_SCROLL_MODEL_INFO_INIT;
    scroll_info.line_height = 32u;
    if (status == ASTRA_STATUS_OK &&
        astra_scroll_init(&scroll, &scroll_info) != ASTRA_OK)
        status = DISK_FAIL_CONTROL;
    if (status == ASTRA_STATUS_OK &&
        astra_volume_watch_open(volumes, &watch) != ASTRA_OK)
        status = DISK_FAIL_CATALOG;
    if (status == ASTRA_STATUS_OK &&
        (field_start(&label_model, "ASTRA", label_content, sizeof(label_content),
            label_metadata, sizeof(label_metadata)) != ASTRA_OK ||
         field_start(&confirm_model, "", confirm_content, sizeof(confirm_content),
            confirm_metadata, sizeof(confirm_metadata)) != ASTRA_OK))
        status = DISK_FAIL_CONTROL;
    if (status == ASTRA_STATUS_OK) status = build_controls();
    if (status == ASTRA_STATUS_OK &&
        astra_shared_draw_list_create(&surface, DISK_WIDTH, DISK_HEIGHT) !=
            ASTRA_SYSCALL_OK)
        status = DISK_FAIL_SURFACE;
    if (status == ASTRA_STATUS_OK) {
        AstraWindowCreateInfo info = ASTRA_WINDOW_CREATE_INFO_INIT;
        info.x = 420u; info.y = 220u;
        info.width = DISK_WIDTH; info.height = DISK_HEIGHT;
        info.content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST;
        info.type = ASTRA_WINDOW_STANDARD;
        info.title = "Disk Setup";
        info.title_length = sizeof("Disk Setup") - 1u;
        info.event_mask = ASTRA_WINDOW_SUBSCRIBE_DEFAULT |
            ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION |
            ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON |
            ASTRA_WINDOW_SUBSCRIBE_POINTER_WHEEL |
            ASTRA_WINDOW_SUBSCRIBE_KEY |
            ASTRA_WINDOW_SUBSCRIBE_TEXT;
        if (astra_window_create(gui->handle, surface.area, &info, &window) !=
                ASTRA_OK ||
            astra_window_set_application_name(&window, "Disk Setup",
                sizeof("Disk Setup") - 1u) != ASTRA_OK ||
            astra_window_activate(&window) != ASTRA_OK)
            status = DISK_FAIL_WINDOW;
    }
    if (ready != NULL) {
        (void)astra_service_ready(ready->handle, status, NULL, 0u);
        (void)astra_close(ready->handle);
    }
    if (status == ASTRA_STATUS_OK) status = present();
    if (status == ASTRA_STATUS_OK) status = run(gui->handle);
    if (window._private_control != ASTRA_INVALID_HANDLE &&
        astra_window_close(&window) != ASTRA_OK && status == ASTRA_STATUS_OK)
        status = DISK_FAIL_WINDOW;
    if (surface.area != ASTRA_INVALID_HANDLE &&
        astra_shared_surface_close(&surface) != ASTRA_SYSCALL_OK &&
        status == ASTRA_STATUS_OK)
        status = DISK_FAIL_SURFACE;
    if (watch.receive != ASTRA_INVALID_HANDLE &&
        astra_volume_watch_close(volumes, &watch) != ASTRA_OK &&
        status == ASTRA_STATUS_OK)
        status = DISK_FAIL_CATALOG;
    dispose_rows();
    astra_volume_snapshot_close(&snapshot);
    astra_volume_usage_snapshot_close(&usage);
    astra_text_model_dispose(&label_model);
    astra_text_model_dispose(&confirm_model);
    if (status != ASTRA_STATUS_OK)
        (void)astra_log_failure("Disk Setup", status);
    return (int)status;
}
