#include "../main.c"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <astra/render_builder.h>

static uint32_t selectable = ASTRA_INSTALLED_FONT_SELECTABLE;
static uint32_t preview_unavailable;
static uint32_t allocations, allocation_calls, fail_allocation;
static char long_notice[32769];
static char raw_notice[131073];

void *astra_runtime_allocate(size_t size)
{
    if (++allocation_calls == fail_allocation) return NULL;
    void *memory = malloc(size);
    if (memory != NULL) ++allocations;
    return memory;
}

void astra_runtime_deallocate(void *memory)
{
    if (memory != NULL) { assert(allocations != 0u); --allocations; }
    free(memory);
}

AstraResult astra_fonts_information_string(const AstraFontInformation *information,
    uint32_t field, uint32_t index, char *output, uint32_t capacity, uint32_t *length)
{
    (void)information;
    const char *text = field == ASTRA_FONT_INFORMATION_FAMILY ? "Test" :
        field == ASTRA_FONT_INFORMATION_LICENSE ? long_notice :
        field == ASTRA_FONT_INFORMATION_SOURCE_NAME ? "original.ttf" :
        field == ASTRA_FONT_INFORMATION_SOURCE_SHA256 ? "original-hash" :
        field == ASTRA_FONT_INFORMATION_NOTICE_TEXT && index == 0u ? long_notice :
        field == ASTRA_FONT_INFORMATION_NOTICE_RAW_HEX ?
            (index == 0u ? raw_notice : "ff00") : NULL;
    *length = text == NULL ? 0u : (uint32_t)strlen(text);
    if (text == NULL) return ASTRA_ERROR_NOT_PRESENT;
    if (output != NULL) {
        if (capacity <= *length) return ASTRA_ERROR_BUFFER_TOO_SMALL;
        memcpy(output, text, *length + 1u);
    }
    return ASTRA_OK;
}

AstraResult astra_fonts_information_notice(const AstraFontInformation *information,
    uint32_t index, AstraFontNotice *notice)
{
    (void)information;
    assert(index < 2u);
    *notice = (AstraFontNotice){13u, index == 0u ? 3u : 42u, 99u, 0u};
    return ASTRA_OK;
}

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

static void check_render(void)
{
    uint32_t list[ASTRA_DRAW_LIST_AREA_BYTES / sizeof(uint32_t)];
    AstraSurfaceView view;
    assert(astra_draw_list_view_init(&view, list, sizeof(list), client_width, client_height));
    assert(astra_interface_ui_render(&ui, &view) == ASTRA_OK);
    AstraRenderBuilder builder;
    void *batch = malloc(ASTRA_RENDER_BUILDER_BYTES);
    assert(batch != NULL && astra_render_builder_init(&builder, batch,
        ASTRA_RENDER_BUILDER_BYTES, 1u));
    uint32_t destination = astra_render_builder_surface_at(&builder,
        ASTRA_RENDER_BATCH_WORKSPACE_LIMIT, (uint32_t)client_width * client_height * 2u,
        client_width, client_height);
    assert(destination != 0u);
    int result = astra_render_builder_replay(&builder, destination, (void *)list);
    if (!result) fprintf(stderr, "information replay failed: builder=%u commands=%u\n",
        builder.failed, builder.command_count);
    assert(result);
    free(batch);
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
    showing_import = 1u;
    import_progress.state = ASTRA_FONT_IMPORT_RUNNING;
    message = "Converting font; installed fonts remain unchanged";
    check_layout(); check_render();
    assert(CONTROL(FONTS_PROGRESS)->_private_control_kind == ASTRA_CONTROL_PROGRESS &&
        (CONTROL(FONTS_PROGRESS)->_private_state & ASTRA_CONTROL_COLLAPSED) == 0u);
    assert((CONTROL(FONTS_LIST)->_private_state & ASTRA_CONTROL_COLLAPSED) != 0u);
    import_progress.state = ASTRA_FONT_IMPORT_CANCELLING;
    import_progress.completed_faces = 1u; import_progress.total_faces = 2u;
    message = "Cancelling; waiting for converter and cleanup";
    check_layout(); check_render();
    assert((CONTROL(FONTS_BACK)->_private_state & ASTRA_CONTROL_DISABLED) != 0u);
    showing_import = 0u; message = NULL;
    client_width = 650u;
    client_height = 640u;
    check_layout();
    browsing = 0u;
    check_layout();
    const char sample[] = "License \xce\xa9 \xe4\xb8\x96\xe7\x95\x8c\tcomplete original line\r\n";
    for (uint32_t at = 0u; at + sizeof(sample) <= sizeof(long_notice); at += sizeof(sample) - 1u)
        memcpy(long_notice + at, sample, sizeof(sample));
    memset(raw_notice, '0', sizeof(raw_notice) - 1u);
    AstraFontInformation information = ASTRA_FONT_INFORMATION_INIT;
    information.source_count = 1u;
    information.notice_count = 2u;
    uint32_t before = allocation_calls;
    assert(collect_information(&information, &information_document) == ASTRA_OK);
    uint32_t calls = allocation_calls - before;
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    assert(astra_text_model_get_state(&information_document.model, &state) == ASTRA_OK);
    assert(state.text_bytes > 32768u && state.line_count > 10u &&
        state.selection.anchor == 0u && state.selection.focus == 0u);
    char *text = malloc((size_t)state.text_bytes + 1u);
    uint32_t copied = 0u;
    assert(text != NULL && astra_text_model_copy(&information_document.model, 0u,
        state.text_bytes, text, state.text_bytes, &copied) == ASTRA_OK);
    text[copied] = '\0';
    assert(strstr(text, "Original source: original.ttf") != NULL &&
        strstr(text, "SHA-256: original-hash") != NULL &&
        strstr(text, "Text encoding unavailable; original bytes below.") != NULL &&
        strstr(text, "Original bytes (hex): ff00") != NULL &&
        strstr(text, long_notice) != NULL);
    free(text);
    showing_information = 1u;
    client_width = FONTS_WIDTH; client_height = FONTS_HEIGHT;
    check_layout();
    assert(CONTROL(FONTS_LIST)->_private_control_kind == ASTRA_CONTROL_TEXT_VIEW);
    assert((CONTROL(FONTS_USE)->_private_state & ASTRA_CONTROL_COLLAPSED) != 0u);
    check_render();
    showing_information = 0u;
    check_layout();
    document_dispose(&information_document);
    assert(allocations == 0u);
    for (uint32_t at = 1u; at <= calls; ++at) {
        fail_allocation = allocation_calls + at;
        FontDocument document = { .model = ASTRA_TEXT_MODEL_INIT };
        assert(collect_information(&information, &document) == ASTRA_ERROR_OUT_OF_MEMORY);
        document_dispose(&document);
        assert(allocations == 0u);
    }
    fail_allocation = 0u;
    puts("Fonts common-control layout tests passed");
    return 0;
}
