#include <astra/interface_library.h>
#include <astra/system_panels.h>
#include <astra/theme.h>
#include <string.h>
#include "panel_internal.h"

static const AstraChoiceItem colour_tabs[] = {
    { "Sliders", 7u, {0u, 0u} }, { "Field", 5u, {0u, 0u} },
    { "Theme", 5u, {0u, 0u} }, { "Recent", 6u, {0u, 0u} }
};
static const AstraCollectionItem theme_items[] = {
    {1u, "Accent", 6u, {0u, 0u}},
    {2u, "Warning", 7u, {0u, 0u}},
    {3u, "Fault", 5u, {0u, 0u}},
    {4u, "Canvas", 6u, {0u, 0u}}
};
static const char hex_digit[] = "0123456789ABCDEF";

static uint8_t expand5(uint32_t value)
{
    return (uint8_t)((value << 3u) | (value >> 2u));
}

static uint8_t expand6(uint32_t value)
{
    return (uint8_t)((value << 2u) | (value >> 4u));
}

static AstraColorRGBA8 panel_color(const AstraColourPanel *panel)
{
    AstraColorRGBA8 color = {0u, 0u, 0u, 255u};
    if (panel->format == ASTRA_COLOUR_PANEL_RGB565) {
        color.red = expand5((panel->packed >> 11u) & 31u);
        color.green = expand6((panel->packed >> 5u) & 63u);
        color.blue = expand5(panel->packed & 31u);
    } else if (panel->format == ASTRA_COLOUR_PANEL_XRGB8888) {
        color.red = (uint8_t)(panel->packed >> 16u);
        color.green = (uint8_t)(panel->packed >> 8u);
        color.blue = (uint8_t)panel->packed;
    } else {
        color = panel->palette[panel->packed];
    }
    return color;
}

AstraResult astra_interface_colour_panel_color(
    const AstraColourPanel *panel, AstraColorRGBA8 *color)
{
    if (panel == NULL || panel->size < sizeof(*panel) || color == NULL ||
        (panel->format != ASTRA_COLOUR_PANEL_RGB565 &&
         panel->format != ASTRA_COLOUR_PANEL_XRGB8888 &&
         panel->format != ASTRA_COLOUR_PANEL_INDEX8) ||
        (panel->format == ASTRA_COLOUR_PANEL_RGB565 && panel->packed > 65535u) ||
        (panel->format == ASTRA_COLOUR_PANEL_XRGB8888 &&
         panel->packed > 0x00ffffffu) ||
        (panel->format == ASTRA_COLOUR_PANEL_INDEX8 &&
         (panel->palette == NULL || panel->packed >= panel->palette_count)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *color = panel_color(panel);
    return ASTRA_OK;
}

static uint32_t panel_channel(const AstraColourPanel *panel, uint32_t index)
{
    if (panel->format == ASTRA_COLOUR_PANEL_INDEX8) return panel->packed;
    if (panel->format == ASTRA_COLOUR_PANEL_RGB565) {
        if (index == 0u) return (panel->packed >> 11u) & 31u;
        if (index == 1u) return (panel->packed >> 5u) & 63u;
        return panel->packed & 31u;
    }
    return (panel->packed >> (16u - 8u * index)) & 255u;
}

static uint32_t panel_max(const AstraColourPanel *panel, uint32_t index)
{
    if (panel->format == ASTRA_COLOUR_PANEL_INDEX8)
        return panel->palette_count - 1u;
    if (panel->format == ASTRA_COLOUR_PANEL_RGB565)
        return index == 1u ? 63u : 31u;
    return 255u;
}

static void format_rgb(char output[8], AstraColorRGBA8 color)
{
    const uint8_t channel[3] = {color.red, color.green, color.blue};
    output[0] = '#';
    for (uint32_t i = 0u; i < 3u; ++i) {
        output[1u + 2u * i] = hex_digit[channel[i] >> 4u];
        output[2u + 2u * i] = hex_digit[channel[i] & 15u];
    }
    output[7] = '\0';
}

static int parse_nibble(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int parse_rgb(const char *text, uint32_t length, AstraColorRGBA8 *color)
{
    uint32_t start = length == 7u && text[0] == '#' ? 1u : 0u;
    uint8_t *channel[3] = {&color->red, &color->green, &color->blue};
    if (length - start != 6u) return 0;
    for (uint32_t i = 0u; i < 3u; ++i) {
        int high = parse_nibble(text[start + 2u * i]);
        int low = parse_nibble(text[start + 2u * i + 1u]);
        if (high < 0 || low < 0) return 0;
        *channel[i] = (uint8_t)((high << 4u) | low);
    }
    color->alpha = 255u;
    return 1;
}

static uint32_t encode_color(const AstraColourPanel *panel,
    AstraColorRGBA8 color)
{
    if (panel->format == ASTRA_COLOUR_PANEL_RGB565)
        return ((uint32_t)(color.red >> 3u) << 11u) |
            ((uint32_t)(color.green >> 2u) << 5u) |
            (uint32_t)(color.blue >> 3u);
    if (panel->format == ASTRA_COLOUR_PANEL_XRGB8888)
        return ((uint32_t)color.red << 16u) |
            ((uint32_t)color.green << 8u) | color.blue;
    uint32_t chosen = 0u, shortest = UINT32_MAX;
    for (uint32_t i = 0u; i < panel->palette_count; ++i) {
        int32_t red = (int32_t)color.red - panel->palette[i].red;
        int32_t green = (int32_t)color.green - panel->palette[i].green;
        int32_t blue = (int32_t)color.blue - panel->palette[i].blue;
        uint32_t distance = (uint32_t)(red * red + green * green + blue * blue);
        if (distance < shortest) { shortest = distance; chosen = i; }
    }
    return chosen;
}

static AstraResult init_container(AstraControl *controls, uint32_t at,
    uint32_t first, uint32_t parent, uint32_t direction, uint32_t state)
{
    AstraContainerInfo info = ASTRA_CONTAINER_INFO_INIT;
    AstraResult result;
    info.id = first + at;
    info.layout.direction = direction;
    info.layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    info.layout.main_gap = 8u;
    info.state = state;
    result = astra_interface_container_init(&controls[at], &info);
    if (result != ASTRA_OK || at == ASTRA_COLOUR_PANEL_ROOT) return result;
    return astra_panel_child(&controls[at], first + parent, 0u);
}

static AstraResult init_label(AstraControl *controls, uint32_t at,
    uint32_t first, uint32_t parent, const char *text, uint32_t state)
{
    AstraLabelInfo info = ASTRA_LABEL_INFO_INIT;
    AstraResult result;
    info.id = first + at;
    info.text = text;
    info.text_length = (uint32_t)strlen(text);
    info.state = state;
    result = astra_interface_label_init(&controls[at], &info);
    if (result != ASTRA_OK) return result;
    return astra_panel_child(&controls[at], first + parent, 0u);
}

static AstraResult init_list(AstraControl *controls, uint32_t at,
    uint32_t first, uint32_t parent, AstraCollectionModel *model)
{
    AstraListInfo info = ASTRA_LIST_INFO_INIT;
    AstraResult result;
    info.id = first + at;
    info.model = model;
    result = astra_interface_list_init(&controls[at], &info);
    if (result != ASTRA_OK) return result;
    return astra_panel_child(&controls[at], first + parent, 0u);
}

static AstraResult panel_update(AstraColourPanel *panel,
    AstraUIContext *context, AstraControl *controls, uint32_t sync_field)
{
    AstraColorRGBA8 stored = panel_color(panel);
    uint32_t next = panel->active_text ^ 1u;
    uint32_t delta = (uint32_t)(panel->wanted.red > stored.red ?
        panel->wanted.red - stored.red : stored.red - panel->wanted.red);
    uint32_t diff;
    AstraResult result;
    diff = (uint32_t)(panel->wanted.green > stored.green ?
        panel->wanted.green - stored.green : stored.green - panel->wanted.green);
    if (diff > delta) delta = diff;
    diff = (uint32_t)(panel->wanted.blue > stored.blue ?
        panel->wanted.blue - stored.blue : stored.blue - panel->wanted.blue);
    if (diff > delta) delta = diff;
    memcpy(panel->difference_text[next], "Max channel error: ", 19u);
    panel->difference_text[next][19] = hex_digit[(delta >> 4u) & 15u];
    panel->difference_text[next][20] = hex_digit[delta & 15u];
    panel->difference_text[next][21] = '\0';
    memcpy(panel->stored_text[next], "Stored ", 7u);
    format_rgb(&panel->stored_text[next][7], stored);
    result = astra_interface_colour_well_set(context,
        &controls[ASTRA_COLOUR_PANEL_WANTED], ASTRA_COLOUR_LITERAL,
        panel->wanted, 0u);
    if (result != ASTRA_OK) return result;
    result = astra_interface_colour_well_set(context,
        &controls[ASTRA_COLOUR_PANEL_SWATCH],
        panel->format == ASTRA_COLOUR_PANEL_INDEX8 ?
            ASTRA_COLOUR_PALETTE_SLOT : ASTRA_COLOUR_LITERAL,
        stored, panel->format == ASTRA_COLOUR_PANEL_INDEX8 ? panel->packed : 0u);
    if (result != ASTRA_OK) return result;
    result = astra_interface_control_set_text(context,
        &controls[ASTRA_COLOUR_PANEL_DIFFERENCE],
        panel->difference_text[next], 21u);
    if (result != ASTRA_OK) return result;
    result = astra_interface_control_set_text(context,
        &controls[ASTRA_COLOUR_PANEL_STORED_VALUE],
        panel->stored_text[next], 14u);
    if (result != ASTRA_OK) return result;
    panel->active_text = next;
    for (uint32_t channel = 0u; channel < 3u; ++channel) {
        if (panel->format == ASTRA_COLOUR_PANEL_INDEX8 && channel != 0u)
            break;
        result = astra_interface_control_set_value(context,
            &controls[ASTRA_COLOUR_PANEL_RED + channel * 2u],
            panel_channel(panel, channel));
        if (result != ASTRA_OK) return result;
    }
    if (sync_field != 0u && panel->active_tab == 1u) {
        AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
        format_rgb(panel->hex_text, panel->wanted);
        result = astra_text_model_get_state(&panel->hex_model, &state);
        if (result != ASTRA_OK) return result;
        result = astra_text_model_replace(&panel->hex_model, 0u,
            state.text_bytes, panel->hex_text, 7u);
        if (result != ASTRA_OK) return result;
        result = astra_interface_field_refresh(context,
            &controls[ASTRA_COLOUR_PANEL_HEX_FIELD]);
        if (result != ASTRA_OK) return result;
        result = astra_interface_ui_set_state(context,
            &controls[ASTRA_COLOUR_PANEL_HEX_FIELD], 0u);
        if (result != ASTRA_OK) return result;
    }
    return ASTRA_OK;
}

AstraResult astra_interface_colour_panel_init(AstraColourPanel *panel,
    AstraControl *controls, uint32_t first_id, uint32_t format,
    uint32_t initial_packed, const AstraColorRGBA8 *palette,
    uint32_t palette_count)
{
    static const char *const channel_label[3] = {"Red", "Green", "Blue"};
    AstraChoiceInfo choice = ASTRA_CHOICE_INFO_INIT;
    AstraTextModelInfo model_info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraColourWellInfo well = ASTRA_COLOUR_WELL_INFO_INIT;
    AstraRangeInfo range = ASTRA_RANGE_INFO_INIT;
    AstraFieldInfo field = ASTRA_FIELD_INFO_INIT;
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraResult result;
    if (panel == NULL || controls == NULL || first_id == 0u ||
        first_id > UINT32_MAX - (ASTRA_COLOUR_PANEL_CONTROL_COUNT - 1u) ||
        (format != ASTRA_COLOUR_PANEL_RGB565 &&
         format != ASTRA_COLOUR_PANEL_XRGB8888 &&
         format != ASTRA_COLOUR_PANEL_INDEX8) ||
        (format == ASTRA_COLOUR_PANEL_RGB565 && initial_packed > 65535u) ||
        (format == ASTRA_COLOUR_PANEL_XRGB8888 && initial_packed > 0x00ffffffu) ||
        (format == ASTRA_COLOUR_PANEL_INDEX8 &&
         (palette == NULL || palette_count == 0u || palette_count > 256u ||
          initial_packed >= palette_count)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (format == ASTRA_COLOUR_PANEL_INDEX8) {
        for (uint32_t i = 0u; i < palette_count; ++i)
            if (palette[i].alpha != 255u) return ASTRA_ERROR_INVALID_ARGUMENT;
    } else if (palette != NULL || palette_count != 0u) {
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    *panel = (AstraColourPanel)ASTRA_COLOUR_PANEL_INIT;
    panel->first_id = first_id;
    panel->format = format;
    panel->packed = initial_packed;
    panel->palette = palette;
    panel->palette_count = palette_count;
    panel->wanted = panel_color(panel);
    panel->hex_valid = 1u;
    format_rgb(panel->hex_text, panel->wanted);
    model_info.text = panel->hex_text;
    model_info.text_bytes = 7u;
    model_info.content_arena = panel->hex_content;
    model_info.content_arena_bytes = sizeof(panel->hex_content);
    model_info.metadata_arena = panel->hex_metadata;
    model_info.metadata_arena_bytes = sizeof(panel->hex_metadata);
    panel->hex_model = (AstraTextModel)ASTRA_TEXT_MODEL_INIT;
    result = astra_text_model_init(&panel->hex_model, &model_info);
    if (result != ASTRA_OK) return result;
    result = astra_interface_collection_init(&panel->theme_model,
        theme_items, 4u, 0u);
    if (result != ASTRA_OK) return result;
    result = astra_interface_collection_init(&panel->recent_model,
        NULL, 0u, 0u);
    if (result != ASTRA_OK) return result;

    result = init_container(controls, ASTRA_COLOUR_PANEL_ROOT, first_id, 0u,
        ASTRA_FLEX_COLUMN, 0u);
    if (result != ASTRA_OK) return result;
    result = init_label(controls, ASTRA_COLOUR_PANEL_TITLE, first_id,
        ASTRA_COLOUR_PANEL_ROOT, "Colour", 0u);
    if (result != ASTRA_OK) return result;
    choice.id = first_id + ASTRA_COLOUR_PANEL_TABS;
    choice.items = colour_tabs;
    choice.item_count = 4u;
    result = astra_interface_segmented_init(&controls[ASTRA_COLOUR_PANEL_TABS],
        &choice);
    if (result != ASTRA_OK) return result;
    result = astra_panel_child(&controls[ASTRA_COLOUR_PANEL_TABS], first_id, 0u);
    if (result != ASTRA_OK) return result;
    result = init_container(controls, ASTRA_COLOUR_PANEL_SWATCHES, first_id,
        ASTRA_COLOUR_PANEL_ROOT, ASTRA_FLEX_ROW, 0u);
    if (result != ASTRA_OK) return result;
    for (uint32_t i = ASTRA_COLOUR_PANEL_WANTED;
         i <= ASTRA_COLOUR_PANEL_SWATCH; ++i) {
        well = (AstraColourWellInfo)ASTRA_COLOUR_WELL_INFO_INIT;
        well.id = first_id + i;
        well.label = i == ASTRA_COLOUR_PANEL_WANTED ? "Wanted" : "Stored";
        well.label_length = 6u;
        well.binding = i == ASTRA_COLOUR_PANEL_SWATCH &&
            format == ASTRA_COLOUR_PANEL_INDEX8 ?
            ASTRA_COLOUR_PALETTE_SLOT : ASTRA_COLOUR_LITERAL;
        well.color = panel->wanted;
        well.palette_slot = well.binding == ASTRA_COLOUR_PALETTE_SLOT ?
            initial_packed : 0u;
        result = astra_interface_colour_well_init(&controls[i], &well);
        if (result != ASTRA_OK) return result;
        result = astra_panel_child(&controls[i],
            first_id + ASTRA_COLOUR_PANEL_SWATCHES, 0u);
        if (result != ASTRA_OK) return result;
    }
    memcpy(panel->difference_text[0], "Max channel error: 00", 22u);
    result = init_label(controls, ASTRA_COLOUR_PANEL_DIFFERENCE, first_id,
        ASTRA_COLOUR_PANEL_ROOT, panel->difference_text[0], 0u);
    if (result != ASTRA_OK) return result;
    result = init_container(controls, ASTRA_COLOUR_PANEL_SLIDERS, first_id,
        ASTRA_COLOUR_PANEL_ROOT, ASTRA_FLEX_COLUMN, 0u);
    if (result != ASTRA_OK) return result;
    for (uint32_t i = 0u; i < 3u; ++i) {
        uint32_t label_at = ASTRA_COLOUR_PANEL_RED_LABEL + i * 2u;
        uint32_t slider_at = ASTRA_COLOUR_PANEL_RED + i * 2u;
        uint32_t state = format == ASTRA_COLOUR_PANEL_INDEX8 && i != 0u ?
            ASTRA_CONTROL_COLLAPSED : 0u;
        result = init_label(controls, label_at, first_id,
            ASTRA_COLOUR_PANEL_SLIDERS,
            format == ASTRA_COLOUR_PANEL_INDEX8 && i == 0u ?
                "Palette index" : channel_label[i], state);
        if (result != ASTRA_OK) return result;
        range = (AstraRangeInfo)ASTRA_RANGE_INFO_INIT;
        range.id = first_id + slider_at;
        range.value = panel_channel(panel, i);
        range.maximum = panel_max(panel, i);
        if (range.maximum == 0u) range.maximum = 1u;
        range.state = state;
        if (format == ASTRA_COLOUR_PANEL_INDEX8 && i == 0u &&
            palette_count == 1u) range.state = ASTRA_CONTROL_DISABLED;
        result = astra_interface_slider_init(&controls[slider_at], &range);
        if (result != ASTRA_OK) return result;
        result = astra_panel_child(&controls[slider_at],
            first_id + ASTRA_COLOUR_PANEL_SLIDERS, 0u);
        if (result != ASTRA_OK) return result;
    }
    result = init_container(controls, ASTRA_COLOUR_PANEL_FIELD_PAGE,
        first_id, ASTRA_COLOUR_PANEL_ROOT, ASTRA_FLEX_COLUMN,
        ASTRA_CONTROL_COLLAPSED);
    if (result != ASTRA_OK) return result;
    field.id = first_id + ASTRA_COLOUR_PANEL_HEX_FIELD;
    field.model = &panel->hex_model;
    field.preferred_columns = 8u;
    result = astra_interface_field_init(&controls[ASTRA_COLOUR_PANEL_HEX_FIELD],
        &field);
    if (result != ASTRA_OK) return result;
    result = astra_panel_child(&controls[ASTRA_COLOUR_PANEL_HEX_FIELD],
        first_id + ASTRA_COLOUR_PANEL_FIELD_PAGE, 0u);
    if (result != ASTRA_OK) return result;
    memcpy(panel->stored_text[0], "Stored ", 7u);
    format_rgb(&panel->stored_text[0][7], panel_color(panel));
    result = init_label(controls, ASTRA_COLOUR_PANEL_STORED_VALUE, first_id,
        ASTRA_COLOUR_PANEL_FIELD_PAGE, panel->stored_text[0], 0u);
    if (result != ASTRA_OK) return result;
    result = init_container(controls, ASTRA_COLOUR_PANEL_THEME_PAGE,
        first_id, ASTRA_COLOUR_PANEL_ROOT, ASTRA_FLEX_COLUMN,
        ASTRA_CONTROL_COLLAPSED);
    if (result != ASTRA_OK) return result;
    result = init_list(controls, ASTRA_COLOUR_PANEL_THEME_LIST, first_id,
        ASTRA_COLOUR_PANEL_THEME_PAGE, &panel->theme_model);
    if (result != ASTRA_OK) return result;
    result = init_container(controls, ASTRA_COLOUR_PANEL_RECENT_PAGE,
        first_id, ASTRA_COLOUR_PANEL_ROOT, ASTRA_FLEX_COLUMN,
        ASTRA_CONTROL_COLLAPSED);
    if (result != ASTRA_OK) return result;
    result = init_list(controls, ASTRA_COLOUR_PANEL_RECENT_LIST, first_id,
        ASTRA_COLOUR_PANEL_RECENT_PAGE, &panel->recent_model);
    if (result != ASTRA_OK) return result;
    result = init_container(controls, ASTRA_COLOUR_PANEL_ACTIONS, first_id,
        ASTRA_COLOUR_PANEL_ROOT, ASTRA_FLEX_ROW, 0u);
    if (result != ASTRA_OK) return result;
    for (uint32_t i = ASTRA_COLOUR_PANEL_CANCEL;
         i <= ASTRA_COLOUR_PANEL_USE; ++i) {
        button = (AstraButtonInfo)ASTRA_BUTTON_INFO_INIT;
        button.id = first_id + i;
        button.text = i == ASTRA_COLOUR_PANEL_USE ? "Use colour" : "Cancel";
        button.text_length = i == ASTRA_COLOUR_PANEL_USE ? 10u : 6u;
        button.variant = i == ASTRA_COLOUR_PANEL_USE ?
            ASTRA_BUTTON_PRIMARY : ASTRA_BUTTON_STANDARD;
        result = astra_interface_button_init(&controls[i], &button);
        if (result != ASTRA_OK) return result;
        result = astra_panel_child(&controls[i],
            first_id + ASTRA_COLOUR_PANEL_ACTIONS, 0u);
        if (result != ASTRA_OK) return result;
    }
    return ASTRA_OK;
}

AstraResult astra_interface_colour_panel_set_recent(AstraColourPanel *panel,
    AstraUIContext *context, AstraControl *controls,
    const AstraColorRGBA8 *colors, uint32_t count)
{
    uint32_t next;
    AstraResult result;
    if (panel == NULL || panel->size < sizeof(*panel) || context == NULL ||
        controls == NULL || count > 8u || (count != 0u && colors == NULL))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    next = panel->active_recent ^ 1u;
    for (uint32_t i = 0u; i < count; ++i) {
        if (colors[i].alpha != 255u) return ASTRA_ERROR_INVALID_ARGUMENT;
        panel->recent_colors[next][i] = colors[i];
        format_rgb(panel->recent_labels[next][i], colors[i]);
        panel->recent_items[next][i] = (AstraCollectionItem){
            (uint64_t)i + 1u, panel->recent_labels[next][i], 7u, {0u, 0u}};
    }
    result = astra_interface_list_set_items(context,
        &controls[ASTRA_COLOUR_PANEL_RECENT_LIST],
        count != 0u ? panel->recent_items[next] : NULL, count);
    if (result != ASTRA_OK) return result;
    panel->recent_count = count;
    panel->active_recent = next;
    return ASTRA_OK;
}

AstraResult astra_interface_colour_panel_action(AstraColourPanel *panel,
    AstraUIContext *context, AstraControl *controls,
    const AstraUIAction *action, uint32_t *outcome)
{
    uint32_t at, previous;
    AstraColorRGBA8 old_wanted;
    AstraResult result;
    if (panel == NULL || panel->size < sizeof(*panel) ||
        panel->first_id == 0u || context == NULL || controls == NULL ||
        action == NULL || action->size < sizeof(*action) || outcome == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *outcome = ASTRA_COLOUR_PANEL_NONE;
    if (action->control_id < panel->first_id ||
        action->control_id - panel->first_id >= ASTRA_COLOUR_PANEL_CONTROL_COUNT)
        return ASTRA_OK;
    at = action->control_id - panel->first_id;
    if (action->type == ASTRA_UI_ACTION_ACTIVATE) {
        if (at == ASTRA_COLOUR_PANEL_CANCEL) {
            *outcome = ASTRA_COLOUR_PANEL_CANCELLED;
            return ASTRA_OK;
        }
        if (at == ASTRA_COLOUR_PANEL_USE) {
            if (panel->hex_valid != 0u)
                *outcome = ASTRA_COLOUR_PANEL_ACCEPTED;
            return ASTRA_OK;
        }
    }
    if (at == ASTRA_COLOUR_PANEL_TABS &&
        action->type == ASTRA_UI_ACTION_VALUE_CHANGED) {
        static const uint32_t pages[4] = {
            ASTRA_COLOUR_PANEL_SLIDERS, ASTRA_COLOUR_PANEL_FIELD_PAGE,
            ASTRA_COLOUR_PANEL_THEME_PAGE, ASTRA_COLOUR_PANEL_RECENT_PAGE};
        if (action->value < 0 || action->value > 3) return ASTRA_ERROR_INVALID_ARGUMENT;
        for (uint32_t i = 0u; i < 4u; ++i) {
            result = astra_interface_ui_set_state(context, &controls[pages[i]],
                i == (uint32_t)action->value ? 0u : ASTRA_CONTROL_COLLAPSED);
            if (result != ASTRA_OK) return result;
        }
        panel->active_tab = (uint32_t)action->value;
        if (panel->active_tab == 1u)
            return panel_update(panel, context, controls, 1u);
        return ASTRA_OK;
    }
    previous = panel->packed;
    old_wanted = panel->wanted;
    if (at == ASTRA_COLOUR_PANEL_HEX_FIELD &&
        action->type == ASTRA_UI_ACTION_TEXT_CHANGED) {
        AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
        AstraColorRGBA8 parsed;
        char text[8];
        uint32_t bytes;
        result = astra_text_model_get_state(&panel->hex_model, &state);
        if (result != ASTRA_OK) return result;
        panel->hex_valid = 0u;
        if (state.text_bytes > 7u)
            return astra_interface_ui_set_state(context,
                &controls[ASTRA_COLOUR_PANEL_HEX_FIELD], ASTRA_CONTROL_ERROR);
        result = astra_text_model_copy(&panel->hex_model, 0u,
            state.text_bytes, text, sizeof(text), &bytes);
        if (result != ASTRA_OK) return result;
        if (!parse_rgb(text, bytes, &parsed))
            return astra_interface_ui_set_state(context,
                &controls[ASTRA_COLOUR_PANEL_HEX_FIELD], ASTRA_CONTROL_ERROR);
        result = astra_interface_ui_set_state(context,
            &controls[ASTRA_COLOUR_PANEL_HEX_FIELD], 0u);
        if (result != ASTRA_OK) return result;
        panel->wanted = parsed;
        panel->hex_valid = 1u;
        panel->packed = encode_color(panel, panel->wanted);
        result = panel_update(panel, context, controls, 0u);
    } else if (action->type == ASTRA_UI_ACTION_VALUE_CHANGED &&
        (at == ASTRA_COLOUR_PANEL_RED || at == ASTRA_COLOUR_PANEL_GREEN ||
         at == ASTRA_COLOUR_PANEL_BLUE)) {
        uint32_t channel = (at - ASTRA_COLOUR_PANEL_RED) / 2u;
        uint32_t shift, mask;
        if ((panel->format == ASTRA_COLOUR_PANEL_INDEX8 && channel != 0u) ||
            action->decimal_places != 0u || action->value < 0 ||
            (uint64_t)action->value > panel_max(panel, channel))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if (panel->format == ASTRA_COLOUR_PANEL_INDEX8) {
            panel->packed = (uint32_t)action->value;
        } else {
            shift = panel->format == ASTRA_COLOUR_PANEL_RGB565 ?
                (channel == 0u ? 11u : (channel == 1u ? 5u : 0u)) :
                16u - channel * 8u;
            mask = panel_max(panel, channel) << shift;
            panel->packed = (panel->packed & ~mask) |
                ((uint32_t)action->value << shift);
        }
        panel->wanted = panel_color(panel);
        panel->hex_valid = 1u;
        result = panel_update(panel, context, controls, 1u);
    } else if ((action->type == ASTRA_UI_ACTION_VALUE_CHANGED ||
                action->type == ASTRA_UI_ACTION_ACTIVATE) &&
        (at == ASTRA_COLOUR_PANEL_THEME_LIST ||
         at == ASTRA_COLOUR_PANEL_RECENT_LIST)) {
        AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
        const AstraColorRGBA8 theme_colors[] = {
            theme.accent, theme.warning, theme.fault, theme.canvas};
        uint32_t count = at == ASTRA_COLOUR_PANEL_THEME_LIST ?
            4u : panel->recent_count;
        if (action->value < 0 || (uint64_t)action->value >= count)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        panel->wanted = at == ASTRA_COLOUR_PANEL_THEME_LIST ?
            theme_colors[action->value] :
            panel->recent_colors[panel->active_recent][action->value];
        panel->packed = encode_color(panel, panel->wanted);
        panel->hex_valid = 1u;
        result = panel_update(panel, context, controls, 1u);
    } else {
        return ASTRA_OK;
    }
    if (result != ASTRA_OK) {
        panel->packed = previous;
        panel->wanted = old_wanted;
        return result;
    }
    *outcome = ASTRA_COLOUR_PANEL_CHANGED;
    return ASTRA_OK;
}
