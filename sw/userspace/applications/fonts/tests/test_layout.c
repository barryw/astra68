#include "../main.c"

#include <assert.h>
#include <stdio.h>

static uint32_t selectable = ASTRA_INSTALLED_FONT_SELECTABLE;
static uint32_t preview_unavailable;

AstraResult astra_fonts_catalog_face(const AstraFontsCatalog *snapshot,
    uint32_t index, AstraInstalledFont *face)
{
    (void)snapshot;
    assert(index == 0u);
    *face = (AstraInstalledFont){
        .id = "user:test", .id_length = 9u,
        .family = "Test", .family_length = 4u,
        .style = "Regular", .style_length = 7u,
        .strike_count = 2u, .flags = selectable
    };
    return ASTRA_OK;
}

AstraResult astra_fonts_catalog_strike(const AstraFontsCatalog *snapshot,
    uint32_t face, uint32_t index, AstraInstalledFontStrike *strike)
{
    (void)snapshot;
    assert(face == 0u && index < 2u);
    *strike = (AstraInstalledFontStrike){
        .pixel_height = index == 0u ? 11u : 20u
    };
    return ASTRA_OK;
}

AstraResult astra_fonts_text_measure(AstraHandle gui,
    const char *id, uint32_t id_length, uint16_t pixel_height,
    const char *text, uint32_t length, uint32_t maximum,
    AstraFontTextMetrics *metrics)
{
    assert(gui == 1u && id_length == 9u && memcmp(id, "user:test", 9u) == 0);
    assert(text != NULL);
    *metrics = (AstraFontTextMetrics)ASTRA_FONT_TEXT_METRICS_INIT;
    if (preview_unavailable) return ASTRA_ERROR_UNSUPPORTED;
    metrics->fitted_bytes = maximum / 8u < length ? maximum / 8u : length;
    metrics->width = metrics->fitted_bytes * 8u;
    metrics->height = pixel_height + 3u;
    return ASTRA_OK;
}

static void check_layout(void)
{
    assert(build_controls() == ASTRA_STATUS_OK);
    for (uint32_t at = 0u; at < FONTS_CONTROL_COUNT; ++at) {
        const AstraControl *control = &controls[at];
        const AstraControlFrame *frame = &control->_private_frame;
        if (at == FONTS_LIST - 1u ||
            (control->_private_state & ASTRA_CONTROL_COLLAPSED) != 0u)
            continue;
        assert(frame->x >= 24 && frame->y >= 24);
        assert((uint64_t)(uint32_t)frame->x + frame->width <= client_width - 24u);
        assert((uint64_t)(uint32_t)frame->y + frame->height <= client_height - 24u);
    }
}

int main(void)
{
    AstraScrollModelInfo info = ASTRA_SCROLL_MODEL_INFO_INIT;
    static AstraChoiceItem rows[] = {
        {"Regular 11 px", 13u, {0u, 0u}},
        {"Regular 20 px", 13u, {0u, 0u}}
    };
    static FontDetail sources[] = {{0u, 0u}, {0u, 1u}};
    info.line_height = 32u;
    assert(astra_scroll_init(&scroll, &info) == ASTRA_OK);
    families[0] = (AstraChoiceItem){"Test", 4u, {0u, 0u}};
    family_count = 1u;
    details = rows;
    detail_sources = sources;
    detail_count = 2u;
    fonts_gui = 1u;
    check_layout();
    active_family = 0u;
    check_layout();
    detail_selection = 1u;
    check_layout();
    selectable = 0u;
    check_layout();
    preview_unavailable = 1u;
    check_layout();
    assert(CONTROL(FONTS_PREVIEW)->_private_label_font.gui == ASTRA_INVALID_HANDLE);
    assert(strcmp(CONTROL(FONTS_PREVIEW)->_private_text,
                  "Preview unavailable for this font") == 0);
    confirm_uninstall = 1u;
    check_layout();
    confirm_uninstall = 0u;
    browsing = 1u;
    check_layout();
    client_width = 650u;
    client_height = 640u;
    check_layout();
    browsing = 0u;
    check_layout();
    puts("Fonts common-control layout tests passed");
    return 0;
}
