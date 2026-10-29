#include <astra/interface_library.h>
#include <astra/bytes.h>
#include <astra/string.h>
#include <string.h>
#include "panel_internal.h"

static AstraResult font_panel_strikes(const AstraFontPanel *panel,
    uint32_t face_index, AstraFontPanelStrikeRow *rows,
    AstraCollectionItem *items, uint32_t *count)
{
    AstraInstalledFont face;
    AstraResult result = astra_fonts_catalog_face(panel->catalog,
                                                  face_index, &face);

    if (result != ASTRA_OK) return result;
    if (face.strike_count > panel->strike_capacity)
        return ASTRA_ERROR_BUFFER_TOO_SMALL;
    for (uint32_t at = 0u; at < face.strike_count; ++at) {
        AstraInstalledFontStrike strike;
        AstraString text;

        result = astra_fonts_catalog_strike(panel->catalog,
            face_index, at, &strike);
        if (result != ASTRA_OK) return result;
        astra_string_init(&text, rows[at].label,
                          sizeof(rows[at].label));
        if ((strike.pixel_width != 0u &&
             (!astra_string_append_u64(&text, strike.pixel_width) ||
              !astra_string_append_char(&text, 'x'))) ||
            !astra_string_append_u64(&text, strike.pixel_height) ||
            !astra_string_append(&text, " px"))
            return ASTRA_ERROR_BUFFER_TOO_SMALL;
        rows[at].pixel_height = strike.pixel_height;
        items[at] = (AstraCollectionItem){
            (uint64_t)at + 1u, text.text, text.length, { 0u, 0u }
        };
    }
    *count = face.strike_count;
    return ASTRA_OK;
}

static AstraResult font_panel_font(const AstraFontPanel *panel,
    AstraLabelFont *font, uint32_t *eligible)
{
    AstraInstalledFont face;
    AstraInstalledFontStrike strike;
    AstraResult result;

    if (panel->selected_face >= panel->catalog->face_count ||
        panel->selected_strike >= panel->strike_count)
        return ASTRA_ERROR_NOT_PRESENT;
    result = astra_fonts_catalog_face(panel->catalog,
        panel->selected_face, &face);
    if (result != ASTRA_OK) return result;
    result = astra_fonts_catalog_strike(panel->catalog,
        panel->selected_face, panel->selected_strike, &strike);
    if (result != ASTRA_OK) return result;
    *font = (AstraLabelFont)ASTRA_LABEL_FONT_INIT;
    font->gui = panel->gui;
    font->id = face.id;
    font->id_length = face.id_length;
    font->pixel_height = strike.pixel_height;
    *eligible = (face.flags & ASTRA_INSTALLED_FONT_SELECTABLE) != 0u;
    return ASTRA_OK;
}

static AstraResult font_panel_preview(AstraFontPanel *panel,
    AstraUIContext *context, AstraControl *controls)
{
    static const char sample[] =
        "The quick brown fox jumps over the lazy dog. 0123456789";
    AstraLabelFont font = ASTRA_LABEL_FONT_INIT;
    uint32_t eligible = 0u;
    const char *status;
    const char *preview = sample;
    AstraResult result = font_panel_font(panel, &font, &eligible);

    if (result == ASTRA_ERROR_NOT_PRESENT) {
        if (panel->selected_face < panel->catalog->face_count) {
            AstraInstalledFont face;

            result = astra_fonts_catalog_face(panel->catalog,
                panel->selected_face, &face);
            if (result != ASTRA_OK) return result;
            eligible = (face.flags & ASTRA_INSTALLED_FONT_SELECTABLE) != 0u;
        }
        result = ASTRA_OK;
    }
    if (result != ASTRA_OK) return result;
    result = astra_interface_label_set_font(context,
        &controls[ASTRA_FONT_PANEL_PREVIEW], &font);
    if (result != ASTRA_OK) {
        font = (AstraLabelFont)ASTRA_LABEL_FONT_INIT;
        result = astra_interface_label_set_font(context,
            &controls[ASTRA_FONT_PANEL_PREVIEW], &font);
        if (result != ASTRA_OK) return result;
        preview = "Preview unavailable for this font";
    }
    result = astra_interface_control_set_text(context,
        &controls[ASTRA_FONT_PANEL_PREVIEW], preview,
        (uint32_t)strlen(preview));
    if (result != ASTRA_OK) return result;
    status = panel->selected_face >= panel->catalog->face_count ?
        (panel->catalog->face_count == 0u ?
         "No fonts installed" : "Select a font") : eligible ?
        "Can be used as system font" : "Application font only";
    result = astra_interface_control_set_text(context,
        &controls[ASTRA_FONT_PANEL_ELIGIBILITY], status,
        (uint32_t)strlen(status));
    if (result != ASTRA_OK) return result;
    return astra_interface_ui_set_state(context,
        &controls[ASTRA_FONT_PANEL_USE],
        panel->selected_strike >= panel->strike_count ||
        (panel->system_only != 0u && eligible == 0u) ?
        ASTRA_CONTROL_DISABLED : 0u);
}

AstraResult astra_interface_font_panel_init(AstraFontPanel *panel,
    AstraControl *controls, const AstraFontPanelInfo *info)
{
    AstraContainerInfo container = ASTRA_CONTAINER_INFO_INIT;
    AstraScrollViewInfo scroll = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraListInfo list = ASTRA_LIST_INFO_INIT;
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraLabelFont preview_font = ASTRA_LABEL_FONT_INIT;
    uint32_t eligible = 0u;
    AstraResult result;

    if (panel == NULL || controls == NULL || info == NULL ||
        info->size < sizeof(*info) || info->first_id == 0u ||
        info->first_id > UINT32_MAX - (ASTRA_FONT_PANEL_CONTROL_COUNT - 1u) ||
        info->gui == 0 || info->catalog == NULL ||
        info->catalog->face_count > info->face_capacity ||
        info->face_rows == NULL || info->face_items == NULL ||
        info->strike_rows == NULL ||
        info->strike_items == NULL || info->strike_rows_spare == NULL ||
        info->strike_items_spare == NULL ||
        info->strike_rows == info->strike_rows_spare ||
        info->strike_items == info->strike_items_spare ||
        info->strike_capacity == 0u ||
        info->face_collection == NULL || info->strike_collection == NULL ||
        info->face_scroll == NULL || info->strike_scroll == NULL ||
        info->system_only > 1u || !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *panel = (AstraFontPanel)ASTRA_FONT_PANEL_INIT;
    panel->first_id = info->first_id;
    panel->gui = info->gui;
    panel->catalog = info->catalog;
    panel->face_rows = info->face_rows;
    panel->face_items = info->face_items;
    panel->strike_rows = info->strike_rows;
    panel->strike_items = info->strike_items;
    panel->strike_rows_spare = info->strike_rows_spare;
    panel->strike_items_spare = info->strike_items_spare;
    panel->strike_capacity = info->strike_capacity;
    panel->face_collection = info->face_collection;
    panel->strike_collection = info->strike_collection;
    panel->selected_face = info->catalog->face_count == 0u ?
        UINT32_MAX : 0u;
    panel->selected_strike = UINT32_MAX;
    panel->system_only = info->system_only;

    for (uint32_t at = 0u; at < info->catalog->face_count; ++at) {
        AstraInstalledFont face;
        AstraString text;

        result = astra_fonts_catalog_face(info->catalog, at, &face);
        if (result != ASTRA_OK) return result;
        astra_string_init(&text, info->face_rows[at].label,
            sizeof(info->face_rows[at].label));
        if (!astra_string_append_bytes(&text, face.family,
                face.family_length) ||
            !astra_string_append(&text, " · ") ||
            !astra_string_append_bytes(&text, face.style,
                face.style_length)) {
            astra_string_init(&text, info->face_rows[at].label,
                sizeof(info->face_rows[at].label));
            if (!astra_string_append_bytes(&text, face.id, face.id_length))
                return ASTRA_ERROR_BUFFER_TOO_SMALL;
        }
        info->face_items[at] = (AstraCollectionItem){
            (uint64_t)at + 1u, text.text, text.length, { 0u, 0u }
        };
        if (info->catalog->selected_id[0] != '\0' &&
            face.id_length == strlen(info->catalog->selected_id) &&
            memcmp(face.id, info->catalog->selected_id,
                   face.id_length) == 0)
            panel->selected_face = at;
    }
    result = astra_interface_collection_init(info->face_collection,
        info->face_items, info->catalog->face_count,
        panel->selected_face == UINT32_MAX ? 0u :
            (uint64_t)panel->selected_face + 1u);
    if (result != ASTRA_OK) return result;
    if (panel->selected_face != UINT32_MAX) {
        result = font_panel_strikes(panel, panel->selected_face,
            panel->strike_rows, panel->strike_items, &panel->strike_count);
        if (result != ASTRA_OK) return result;
        if (panel->strike_count != 0u) panel->selected_strike = 0u;
    }
    result = astra_interface_collection_init(info->strike_collection,
        info->strike_items, panel->strike_count,
        panel->selected_strike == UINT32_MAX ? 0u : 1u);
    if (result != ASTRA_OK) return result;
    result = font_panel_font(panel, &preview_font, &eligible);
    if (result != ASTRA_OK && result != ASTRA_ERROR_NOT_PRESENT) return result;

    container.id = info->first_id + ASTRA_FONT_PANEL_ROOT;
    container.layout.direction = ASTRA_FLEX_COLUMN;
    container.layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    container.layout.main_gap = 12u;
    result = astra_interface_container_init(&controls[ASTRA_FONT_PANEL_ROOT],
                                            &container);
    if (result != ASTRA_OK) return result;
    label.id = info->first_id + ASTRA_FONT_PANEL_TITLE;
    label.text = "Choose font";
    label.text_length = 11u;
    result = astra_interface_label_init(&controls[ASTRA_FONT_PANEL_TITLE],
                                        &label);
    if (result != ASTRA_OK) return result;
    result = astra_panel_child(&controls[ASTRA_FONT_PANEL_TITLE],
                              info->first_id, 0u);
    if (result != ASTRA_OK) return result;

    container = (AstraContainerInfo)ASTRA_CONTAINER_INFO_INIT;
    container.id = info->first_id + ASTRA_FONT_PANEL_BROWSERS;
    container.layout.direction = ASTRA_FLEX_ROW;
    container.layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    container.layout.main_gap = 12u;
    result = astra_interface_container_init(
        &controls[ASTRA_FONT_PANEL_BROWSERS], &container);
    if (result != ASTRA_OK) return result;
    result = astra_panel_child(&controls[ASTRA_FONT_PANEL_BROWSERS],
                              info->first_id, 1u);
    if (result != ASTRA_OK) return result;

    for (uint32_t at = 0u; at < 2u; ++at) {
        uint32_t scroll_at = at == 0u ? ASTRA_FONT_PANEL_FACE_SCROLL :
                                        ASTRA_FONT_PANEL_STRIKE_SCROLL;
        uint32_t list_at = scroll_at + 1u;

        scroll = (AstraScrollViewInfo)ASTRA_SCROLL_VIEW_INFO_INIT;
        scroll.id = info->first_id + scroll_at;
        scroll.model = at == 0u ? info->face_scroll : info->strike_scroll;
        scroll.preferred_width = at == 0u ? 260u : 160u;
        scroll.preferred_height = 220u;
        result = astra_interface_scroll_view_init(&controls[scroll_at],
                                                  &scroll);
        if (result != ASTRA_OK) return result;
        result = astra_panel_child(&controls[scroll_at],
            info->first_id + ASTRA_FONT_PANEL_BROWSERS, 1u);
        if (result != ASTRA_OK) return result;
        list = (AstraListInfo)ASTRA_LIST_INFO_INIT;
        list.id = info->first_id + list_at;
        list.model = at == 0u ? info->face_collection :
                                info->strike_collection;
        result = astra_interface_list_init(&controls[list_at], &list);
        if (result != ASTRA_OK) return result;
        result = astra_panel_child(&controls[list_at],
                                  info->first_id + scroll_at, 0u);
        if (result != ASTRA_OK) return result;
    }

    label = (AstraLabelInfo)ASTRA_LABEL_INFO_INIT;
    label.id = info->first_id + ASTRA_FONT_PANEL_PREVIEW;
    label.text = "The quick brown fox jumps over the lazy dog. 0123456789";
    label.text_length = sizeof("The quick brown fox jumps over the lazy dog. 0123456789") - 1u;
    label.font = preview_font;
    result = astra_interface_label_init(&controls[ASTRA_FONT_PANEL_PREVIEW],
                                        &label);
    if (result != ASTRA_OK) return result;
    result = astra_panel_child(&controls[ASTRA_FONT_PANEL_PREVIEW],
                              info->first_id, 0u);
    if (result != ASTRA_OK) return result;
    label = (AstraLabelInfo)ASTRA_LABEL_INFO_INIT;
    label.id = info->first_id + ASTRA_FONT_PANEL_ELIGIBILITY;
    label.text = panel->selected_face == UINT32_MAX ? "No fonts installed" :
        eligible ? "Can be used as system font" : "Application font only";
    label.text_length = (uint32_t)strlen(label.text);
    label.text_role = ASTRA_TEXT_CLIENT_SECONDARY;
    result = astra_interface_label_init(
        &controls[ASTRA_FONT_PANEL_ELIGIBILITY], &label);
    if (result != ASTRA_OK) return result;
    result = astra_panel_child(&controls[ASTRA_FONT_PANEL_ELIGIBILITY],
                              info->first_id, 0u);
    if (result != ASTRA_OK) return result;

    container = (AstraContainerInfo)ASTRA_CONTAINER_INFO_INIT;
    container.id = info->first_id + ASTRA_FONT_PANEL_ACTIONS;
    container.layout.direction = ASTRA_FLEX_ROW;
    container.layout.main_gap = 8u;
    result = astra_interface_container_init(
        &controls[ASTRA_FONT_PANEL_ACTIONS], &container);
    if (result != ASTRA_OK) return result;
    result = astra_panel_child(&controls[ASTRA_FONT_PANEL_ACTIONS],
                              info->first_id, 0u);
    if (result != ASTRA_OK) return result;
    for (uint32_t at = ASTRA_FONT_PANEL_CANCEL;
         at <= ASTRA_FONT_PANEL_USE; ++at) {
        button = (AstraButtonInfo)ASTRA_BUTTON_INFO_INIT;
        button.id = info->first_id + at;
        button.text = at == ASTRA_FONT_PANEL_CANCEL ? "Cancel" : "Use font";
        button.text_length = at == ASTRA_FONT_PANEL_CANCEL ? 6u : 8u;
        button.variant = at == ASTRA_FONT_PANEL_USE ?
            ASTRA_BUTTON_PRIMARY : ASTRA_BUTTON_STANDARD;
        if (at == ASTRA_FONT_PANEL_USE &&
            (panel->selected_strike == UINT32_MAX ||
             (info->system_only != 0u && eligible == 0u)))
            button.state = ASTRA_CONTROL_DISABLED;
        result = astra_interface_button_init(&controls[at], &button);
        if (result != ASTRA_OK) return result;
        result = astra_panel_child(&controls[at],
            info->first_id + ASTRA_FONT_PANEL_ACTIONS, 0u);
        if (result != ASTRA_OK) return result;
    }
    return ASTRA_OK;
}

AstraResult astra_interface_font_panel_choice(const AstraFontPanel *panel,
    AstraInstalledFont *face, AstraInstalledFontStrike *strike)
{
    AstraResult result;

    if (panel == NULL || panel->size < sizeof(*panel) ||
        panel->catalog == NULL || face == NULL || strike == NULL ||
        panel->selected_face >= panel->catalog->face_count ||
        panel->selected_strike >= panel->strike_count)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_fonts_catalog_face(panel->catalog,
        panel->selected_face, face);
    if (result != ASTRA_OK) return result;
    return astra_fonts_catalog_strike(panel->catalog,
        panel->selected_face, panel->selected_strike, strike);
}

AstraResult astra_interface_font_panel_action(AstraFontPanel *panel,
    AstraUIContext *context, AstraControl *controls,
    const AstraUIAction *action, uint32_t *outcome)
{
    uint32_t at;
    AstraResult result;

    if (panel == NULL || panel->size < sizeof(*panel) ||
        panel->first_id == 0u || context == NULL || controls == NULL ||
        action == NULL || action->size < sizeof(*action) || outcome == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *outcome = ASTRA_FONT_PANEL_NONE;
    if (action->control_id < panel->first_id ||
        action->control_id - panel->first_id >= ASTRA_FONT_PANEL_CONTROL_COUNT)
        return ASTRA_OK;
    at = action->control_id - panel->first_id;
    if (action->type == ASTRA_UI_ACTION_ACTIVATE) {
        if (at == ASTRA_FONT_PANEL_CANCEL)
            *outcome = ASTRA_FONT_PANEL_CANCELLED;
        else if (at == ASTRA_FONT_PANEL_USE) {
            AstraInstalledFont face;
            AstraInstalledFontStrike strike;

            result = astra_interface_font_panel_choice(panel, &face, &strike);
            if (result != ASTRA_OK) return result;
            if (panel->system_only != 0u &&
                (face.flags & ASTRA_INSTALLED_FONT_SELECTABLE) == 0u)
                return ASTRA_ERROR_INVALID_ARGUMENT;
            *outcome = ASTRA_FONT_PANEL_ACCEPTED;
        }
        return ASTRA_OK;
    }
    if (action->type != ASTRA_UI_ACTION_VALUE_CHANGED)
        return ASTRA_OK;
    if (at == ASTRA_FONT_PANEL_FACE_LIST) {
        uint32_t next_face;
        uint32_t next_count = 0u;
        uint32_t old_face = panel->selected_face;
        AstraFontPanelStrikeRow *old_rows = panel->strike_rows;
        AstraCollectionItem *old_items = panel->strike_items;

        if (action->value < -1 ||
            (action->value >= 0 &&
             (uint64_t)action->value >= panel->catalog->face_count))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        next_face = action->value < 0 ?
            UINT32_MAX : (uint32_t)action->value;
        if (next_face != UINT32_MAX) {
            result = font_panel_strikes(panel, next_face,
                panel->strike_rows_spare, panel->strike_items_spare,
                &next_count);
            if (result != ASTRA_OK) {
                (void)astra_interface_control_set_value(context,
                    &controls[ASTRA_FONT_PANEL_FACE_LIST],
                    old_face == UINT32_MAX ? INT64_C(-1) :
                        (int64_t)old_face);
                return result;
            }
        }
        result = astra_interface_list_set_items(context,
            &controls[ASTRA_FONT_PANEL_STRIKE_LIST], panel->strike_items_spare,
            next_count);
        if (result != ASTRA_OK) {
            (void)astra_interface_control_set_value(context,
                &controls[ASTRA_FONT_PANEL_FACE_LIST],
                old_face == UINT32_MAX ? INT64_C(-1) :
                    (int64_t)old_face);
            return result;
        }
        result = astra_interface_control_set_value(context,
            &controls[ASTRA_FONT_PANEL_STRIKE_LIST],
            next_count == 0u ? -1 : 0);
        if (result != ASTRA_OK) {
            (void)astra_interface_list_set_items(context,
                &controls[ASTRA_FONT_PANEL_STRIKE_LIST], old_items,
                panel->strike_count);
            (void)astra_interface_control_set_value(context,
                &controls[ASTRA_FONT_PANEL_FACE_LIST],
                old_face == UINT32_MAX ? INT64_C(-1) :
                    (int64_t)old_face);
            return result;
        }
        panel->selected_face = next_face;
        panel->selected_strike = next_count == 0u ? UINT32_MAX : 0u;
        panel->strike_count = next_count;
        panel->strike_rows = panel->strike_rows_spare;
        panel->strike_items = panel->strike_items_spare;
        panel->strike_rows_spare = old_rows;
        panel->strike_items_spare = old_items;
    } else if (at == ASTRA_FONT_PANEL_STRIKE_LIST) {
        if (action->value < -1 ||
            (action->value >= 0 &&
             (uint64_t)action->value >= panel->strike_count))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        panel->selected_strike = action->value < 0 ?
            UINT32_MAX : (uint32_t)action->value;
    } else {
        return ASTRA_OK;
    }
    result = font_panel_preview(panel, context, controls);
    if (result == ASTRA_OK) *outcome = ASTRA_FONT_PANEL_CHANGED;
    return result;
}
