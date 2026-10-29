#include <astra/interface_library.h>
#include <astra/system_panels.h>
#include <stddef.h>
#include "panel_internal.h"

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
    AstraColorRGBA8 color = { 0u, 0u, 0u, 255u };
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
        (panel->format == ASTRA_COLOUR_PANEL_RGB565 &&
         panel->packed > 65535u) ||
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

AstraResult astra_interface_colour_panel_init(AstraColourPanel *panel,
    AstraControl *controls, uint32_t first_id, uint32_t format,
    uint32_t initial_packed, const AstraColorRGBA8 *palette,
    uint32_t palette_count)
{
    static const char *const channel_label[3] = { "Red", "Green", "Blue" };
    AstraContainerInfo container = ASTRA_CONTAINER_INFO_INIT;
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraColourWellInfo well = ASTRA_COLOUR_WELL_INFO_INIT;
    AstraRangeInfo range = ASTRA_RANGE_INFO_INIT;
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraResult result;

    if (panel == NULL || controls == NULL || first_id == 0u ||
        first_id > UINT32_MAX - (ASTRA_COLOUR_PANEL_CONTROL_COUNT - 1u) ||
        (format != ASTRA_COLOUR_PANEL_RGB565 &&
         format != ASTRA_COLOUR_PANEL_XRGB8888 &&
         format != ASTRA_COLOUR_PANEL_INDEX8) ||
        (format == ASTRA_COLOUR_PANEL_RGB565 && initial_packed > 65535u) ||
        (format == ASTRA_COLOUR_PANEL_XRGB8888 &&
         initial_packed > 0x00ffffffu) ||
        (format == ASTRA_COLOUR_PANEL_INDEX8 &&
         (palette == NULL || palette_count == 0u || palette_count > 256u ||
          initial_packed >= palette_count)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (format == ASTRA_COLOUR_PANEL_INDEX8) {
        for (uint32_t i = 0u; i < palette_count; ++i)
            if (palette[i].alpha != 255u)
                return ASTRA_ERROR_INVALID_ARGUMENT;
    } else if (palette != NULL || palette_count != 0u) {
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }

    *panel = (AstraColourPanel)ASTRA_COLOUR_PANEL_INIT;
    panel->first_id = first_id;
    panel->format = format;
    panel->packed = initial_packed;
    panel->palette = palette;
    panel->palette_count = palette_count;

    container.id = first_id + ASTRA_COLOUR_PANEL_ROOT;
    container.layout.direction = ASTRA_FLEX_COLUMN;
    container.layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    container.layout.main_gap = 12u;
    result = astra_interface_container_init(&controls[ASTRA_COLOUR_PANEL_ROOT],
                                            &container);
    if (result != ASTRA_OK) return result;

    label.id = first_id + ASTRA_COLOUR_PANEL_TITLE;
    label.text = "Colour";
    label.text_length = 6u;
    result = astra_interface_label_init(&controls[ASTRA_COLOUR_PANEL_TITLE],
                                        &label);
    if (result != ASTRA_OK) return result;
    result = astra_panel_child(&controls[ASTRA_COLOUR_PANEL_TITLE], first_id, 0u);
    if (result != ASTRA_OK) return result;

    well.id = first_id + ASTRA_COLOUR_PANEL_SWATCH;
    well.label = "Preview";
    well.label_length = 7u;
    well.binding = format == ASTRA_COLOUR_PANEL_INDEX8 ?
        ASTRA_COLOUR_PALETTE_SLOT : ASTRA_COLOUR_LITERAL;
    well.color = panel_color(panel);
    well.palette_slot = format == ASTRA_COLOUR_PANEL_INDEX8 ?
        initial_packed : 0u;
    result = astra_interface_colour_well_init(
        &controls[ASTRA_COLOUR_PANEL_SWATCH], &well);
    if (result != ASTRA_OK) return result;
    result = astra_panel_child(&controls[ASTRA_COLOUR_PANEL_SWATCH], first_id, 0u);
    if (result != ASTRA_OK) return result;

    for (uint32_t i = 0u; i < 3u; ++i) {
        uint32_t label_at = ASTRA_COLOUR_PANEL_RED_LABEL + i * 2u;
        uint32_t slider_at = ASTRA_COLOUR_PANEL_RED + i * 2u;
        label = (AstraLabelInfo)ASTRA_LABEL_INFO_INIT;
        label.id = first_id + label_at;
        label.text = format == ASTRA_COLOUR_PANEL_INDEX8 && i == 0u ?
            "Palette index" : channel_label[i];
        label.text_length = format == ASTRA_COLOUR_PANEL_INDEX8 && i == 0u ?
            13u : (i == 1u ? 5u : (i == 2u ? 4u : 3u));
        label.text_role = ASTRA_TEXT_CLIENT_SECONDARY;
        if (format == ASTRA_COLOUR_PANEL_INDEX8 && i != 0u)
            label.state = ASTRA_CONTROL_COLLAPSED;
        result = astra_interface_label_init(&controls[label_at], &label);
        if (result != ASTRA_OK) return result;
        result = astra_panel_child(&controls[label_at], first_id, 0u);
        if (result != ASTRA_OK) return result;

        range = (AstraRangeInfo)ASTRA_RANGE_INFO_INIT;
        range.id = first_id + slider_at;
        range.value = panel_channel(panel, i);
        range.maximum = panel_max(panel, i);
        if (range.maximum == 0) range.maximum = 1;
        if (format == ASTRA_COLOUR_PANEL_INDEX8 && i == 0u &&
            palette_count == 1u)
            range.state = ASTRA_CONTROL_DISABLED;
        if (format == ASTRA_COLOUR_PANEL_INDEX8 && i != 0u)
            range.state = ASTRA_CONTROL_COLLAPSED;
        result = astra_interface_slider_init(&controls[slider_at], &range);
        if (result != ASTRA_OK) return result;
        result = astra_panel_child(&controls[slider_at], first_id, 0u);
        if (result != ASTRA_OK) return result;
    }

    container = (AstraContainerInfo)ASTRA_CONTAINER_INFO_INIT;
    container.id = first_id + ASTRA_COLOUR_PANEL_ACTIONS;
    container.layout.direction = ASTRA_FLEX_ROW;
    container.layout.main_gap = 8u;
    result = astra_interface_container_init(
        &controls[ASTRA_COLOUR_PANEL_ACTIONS], &container);
    if (result != ASTRA_OK) return result;
    result = astra_panel_child(&controls[ASTRA_COLOUR_PANEL_ACTIONS], first_id, 0u);
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

AstraResult astra_interface_colour_panel_action(AstraColourPanel *panel,
    AstraUIContext *context, AstraControl *controls,
    const AstraUIAction *action, uint32_t *outcome)
{
    uint32_t at;
    uint32_t channel;
    uint32_t mask;
    uint32_t shift;
    uint32_t previous;
    AstraResult result;

    if (panel == NULL || panel->size < sizeof(*panel) ||
        panel->first_id == 0u || context == NULL || controls == NULL ||
        action == NULL || action->size < sizeof(*action) || outcome == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *outcome = ASTRA_COLOUR_PANEL_NONE;
    if (action->control_id < panel->first_id ||
        action->control_id - panel->first_id >=
            ASTRA_COLOUR_PANEL_CONTROL_COUNT)
        return ASTRA_OK;
    at = action->control_id - panel->first_id;
    if (action->type == ASTRA_UI_ACTION_ACTIVATE) {
        if (at == ASTRA_COLOUR_PANEL_CANCEL)
            *outcome = ASTRA_COLOUR_PANEL_CANCELLED;
        else if (at == ASTRA_COLOUR_PANEL_USE)
            *outcome = ASTRA_COLOUR_PANEL_ACCEPTED;
        return ASTRA_OK;
    }
    if (action->type != ASTRA_UI_ACTION_VALUE_CHANGED ||
        (at != ASTRA_COLOUR_PANEL_RED && at != ASTRA_COLOUR_PANEL_GREEN &&
         at != ASTRA_COLOUR_PANEL_BLUE))
        return ASTRA_OK;
    channel = (at - ASTRA_COLOUR_PANEL_RED) / 2u;
    if (panel->format == ASTRA_COLOUR_PANEL_INDEX8 && channel != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (action->decimal_places != 0u || action->value < 0 ||
        (uint64_t)action->value > panel_max(panel, channel))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    previous = panel->packed;
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
    result = astra_interface_colour_well_set(context,
        &controls[ASTRA_COLOUR_PANEL_SWATCH],
        panel->format == ASTRA_COLOUR_PANEL_INDEX8 ?
            ASTRA_COLOUR_PALETTE_SLOT : ASTRA_COLOUR_LITERAL,
        panel_color(panel), panel->format == ASTRA_COLOUR_PANEL_INDEX8 ?
            panel->packed : 0u);
    if (result != ASTRA_OK) panel->packed = previous;
    if (result == ASTRA_OK) *outcome = ASTRA_COLOUR_PANEL_CHANGED;
    return result;
}
