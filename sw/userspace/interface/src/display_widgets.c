#include <astra/interface_library.h>
#include <astra/bytes.h>
#include <astra/surface.h>
#include <astra/theme.h>
#include <astra/utf8.h>

#include "control_internal.h"
#include "display_widgets_internal.h"

#include <limits.h>

static uint16_t color(AstraColorRGBA8 rgba)
{
    return astra_surface_rgb565(rgba.red, rgba.green, rgba.blue);
}

static int model_valid(uint32_t kind, const void *model)
{
    if (kind == ASTRA_CONTROL_LEVEL_METER) {
        const AstraLevelMeterModel *meter = model;

        if (meter == NULL || meter->size < sizeof(*meter) ||
            meter->values == NULL || meter->peaks == NULL ||
            meter->channels == 0u || meter->channels > 16u ||
            meter->maximum == 0u || meter->maximum > INT32_MAX ||
            !astra_words_zero(meter->reserved, 2u)) return 0;
        for (uint32_t at = 0u; at < meter->channels; ++at)
            if (meter->values[at] > meter->peaks[at] ||
                meter->peaks[at] > meter->maximum) return 0;
        return 1;
    }
    if (kind == ASTRA_CONTROL_SPARKLINE) {
        const AstraSparklineModel *line = model;

        if (line == NULL || line->size < sizeof(*line) ||
            line->samples == NULL || line->sample_count == 0u ||
            line->sample_count > 1024u || line->maximum == 0u ||
            line->maximum > INT32_MAX ||
            line->warning_threshold > line->maximum ||
            !astra_words_zero(line->reserved, 2u)) return 0;
        for (uint32_t at = 0u; at < line->sample_count; ++at)
            if (line->samples[at] > line->maximum) return 0;
        return 1;
    }
    return 0;
}

int astra_interface_display_model_valid(const AstraControl *control)
{
    return control->_private_text == NULL &&
        control->_private_text_length == 0u &&
        control->_private_style == 0u &&
        (control->_private_state & ~(ASTRA_CONTROL_DISABLED |
            ASTRA_CONTROL_ERROR | ASTRA_CONTROL_COLLAPSED)) == 0u &&
        control->_private_preview_state == 0u &&
        control->_private_value == 0 &&
        control->_private_minimum_value == 0 &&
        control->_private_maximum_value == 0 &&
        control->_private_value_step == 0 &&
        model_valid(control->_private_control_kind,
                    control->_private_display_model);
}

static AstraResult display_init(AstraControl *control,
    const AstraDisplayInfo *info, uint32_t kind)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u ||
        (info->state & ~(ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_ERROR |
                          ASTRA_CONTROL_COLLAPSED)) != 0u ||
        !astra_words_zero(info->reserved, 4u) ||
        !model_valid(kind, info->model))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = kind;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_display_model = info->model;
    return ASTRA_OK;
}

AstraResult astra_interface_level_meter_init(AstraControl *control,
    const AstraDisplayInfo *info)
{
    return display_init(control, info, ASTRA_CONTROL_LEVEL_METER);
}

AstraResult astra_interface_sparkline_init(AstraControl *control,
    const AstraDisplayInfo *info)
{
    return display_init(control, info, ASTRA_CONTROL_SPARKLINE);
}

AstraResult astra_interface_display_measure(const AstraControl *control,
    AstraControlSize *size)
{
    if (size == NULL || !astra_interface_display_model_valid(control))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (control->_private_control_kind == ASTRA_CONTROL_LEVEL_METER) {
        const AstraLevelMeterModel *model = control->_private_display_model;
        size->width = model->channels * 12u + (model->channels - 1u) * 4u;
        size->height = 80u;
    } else {
        const AstraSparklineModel *model = control->_private_display_model;
        size->width = model->sample_count * 2u;
        size->height = 32u;
    }
    return ASTRA_OK;
}

/* One geometry path is used for draw-list budgeting and actual rendering. */
AstraResult astra_interface_display_render(const AstraControl *control,
    const AstraTheme *theme, AstraSurfaceView *surface, uint64_t *commands)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t state = control->_private_state;

    if (theme == NULL || !astra_interface_display_model_valid(control))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (frame->width == 0u || frame->height == 0u) return ASTRA_OK;
    if (control->_private_control_kind == ASTRA_CONTROL_LEVEL_METER) {
        const AstraLevelMeterModel *model = control->_private_display_model;
        uint32_t gap = 4u;
        uint32_t track_width;

        if (frame->width <= (model->channels - 1u) * gap)
            gap = 0u;
        track_width = (frame->width - (model->channels - 1u) * gap) /
                      model->channels;
        if (track_width == 0u) return ASTRA_OK;
        for (uint32_t at = 0u; at < model->channels; ++at) {
            int32_t x = frame->x + (int32_t)(at * (track_width + gap));
            uint32_t filled = (uint32_t)((uint64_t)model->values[at] *
                                         frame->height / model->maximum);
            uint32_t peak = (uint32_t)((uint64_t)model->peaks[at] *
                                       (frame->height - 1u) / model->maximum);
            AstraColorRGBA8 bar = (state & ASTRA_CONTROL_ERROR) != 0u ?
                theme->fault : (state & ASTRA_CONTROL_DISABLED) != 0u ?
                theme->text_tertiary : theme->accent;

            if (model->values[at] != 0u && filled == 0u) filled = 1u;
            if (commands != NULL) *commands += 3u;
            if (surface == NULL) continue;
            astra_surface_fill_round(surface, x, frame->y, track_width,
                frame->height, track_width / 4u,
                color(theme->client_border));
            if (filled != 0u)
                astra_surface_fill(surface, x,
                    frame->y + (int32_t)(frame->height - filled),
                    track_width, filled, color(bar));
            astra_surface_fill(surface, x,
                frame->y + (int32_t)(frame->height - 1u - peak),
                track_width, 2u, color(theme->frame));
        }
    } else {
        const AstraSparklineModel *model = control->_private_display_model;
        uint32_t visible = frame->width / 2u;
        uint32_t start;

        if (visible > model->sample_count) visible = model->sample_count;
        if (visible == 0u) return ASTRA_OK;
        start = model->sample_count - visible;
        if (commands != NULL) *commands += 1u + visible;
        if (surface != NULL)
            astra_surface_fill(surface, frame->x,
                frame->y + (int32_t)frame->height - 1,
                frame->width, 1u, color(theme->client_border));
        for (uint32_t at = 0u; at < visible; ++at) {
            uint32_t sample = model->samples[start + at];
            uint32_t height = (uint32_t)((uint64_t)sample * frame->height /
                                         model->maximum);
            AstraColorRGBA8 bar = (state & ASTRA_CONTROL_ERROR) != 0u ||
                (model->warning_threshold != 0u &&
                 sample >= model->warning_threshold) ? theme->fault :
                (state & ASTRA_CONTROL_DISABLED) != 0u ?
                    theme->text_tertiary :
                at + 1u == visible ? theme->accent : theme->text_muted;

            if (sample != 0u && height == 0u) height = 1u;
            if (surface != NULL && height != 0u)
                astra_surface_fill(surface, frame->x + (int32_t)(at * 2u),
                    frame->y + (int32_t)(frame->height - height),
                    1u, height, color(bar));
        }
    }
    return ASTRA_OK;
}

AstraResult astra_interface_status_bar_init(AstraControl *controls,
    const AstraStatusBarInfo *info)
{
    AstraContainerInfo row = ASTRA_CONTAINER_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraResult result;

    if (controls == NULL || info == NULL || info->size < sizeof(*info) ||
        info->first_id == 0u || info->first_id > UINT32_MAX - 2u ||
        !astra_utf8_validate(info->left, info->left_length, 0u) ||
        !astra_utf8_validate(info->right, info->right_length, 0u) ||
        info->left_length == 0u || info->right_length == 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    row.id = info->first_id;
    row.layout.justify = ASTRA_FLEX_JUSTIFY_SPACE_BETWEEN;
    row.layout.align_items = ASTRA_FLEX_ALIGN_CENTER;
    row.layout.padding_left = row.layout.padding_right = 8u;
    row.layout.padding_top = row.layout.padding_bottom = 5u;
    result = astra_interface_container_init(&controls[0], &row);
    if (result != ASTRA_OK) return result;
    controls[0]._private_style = ASTRA_CONTAINER_STYLE_STATUS_BAR;
    for (uint32_t at = 0u; at < 2u; ++at) {
        AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;

        label.id = info->first_id + at + 1u;
        label.text = at == 0u ? info->left : info->right;
        label.text_length = at == 0u ? info->left_length : info->right_length;
        label.text_role = ASTRA_TEXT_CLIENT_SECONDARY;
        result = astra_interface_label_init(&controls[at + 1u], &label);
        if (result != ASTRA_OK) return result;
        item.parent_id = info->first_id;
        item.shrink = 1u;
        result = astra_interface_control_set_flex(&controls[at + 1u], &item);
        if (result != ASTRA_OK) return result;
    }
    return ASTRA_OK;
}
