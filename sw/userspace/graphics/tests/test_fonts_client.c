#include <astra/fonts.h>
#include <astra/font_catalog.h>
#include <astra/font_service.h>
#include <astra/port.h>
#include <astra/resource.h>
#include <astra/status.h>
#include <astra/gui.h>
#include <astra/utf8.h>

#include <assert.h>
#include <string.h>

static uint8_t document[65536];
static uint32_t document_size;
static uint32_t document_generation = 7u;
static uint32_t bad_reply;
static uint32_t closed_handles;
static uint8_t imported_bytes[16];
static uint32_t imported_length;
static uint32_t installed_contexts;
static char adopted_id[ASTRA_FONT_ID_MAX];
static uint32_t fail_snapshot;
static uint32_t fail_context_map;
static uint32_t requested_window;
static uint32_t measure_calls;
static uint32_t job_state = ASTRA_FONT_IMPORT_SUCCEEDED;
static AstraResult hint_result = ASTRA_ERROR_WOULD_BLOCK;

AstraResult astra_port_create(uint32_t messages, uint32_t bytes, AstraPort *port)
{
    assert(messages == 1u && bytes == sizeof(AstraMessageHeader));
    port->receive = 6u; port->send = 7u;
    return ASTRA_OK;
}
AstraResult astra_port_close(AstraPort *port)
{
    assert(port->receive == ASTRA_INVALID_HANDLE && port->send == ASTRA_INVALID_HANDLE);
    return ASTRA_ERROR_INVALID_HANDLE;
}
AstraResult astra_port_receive_try(AstraHandle receive, void *data, uint32_t capacity,
    AstraHandle *handles, uint32_t maximum, uint32_t *bytes, uint32_t *count)
{
    assert(receive == 6u && capacity == sizeof(AstraMessageHeader) && maximum == ASTRA_MESSAGE_HANDLES_MAX);
    (void)data; (void)handles; (void)bytes; (void)count;
    return hint_result;
}

AstraResult astra_fonts_context_id(char id[ASTRA_FONT_ID_MAX])
{
    memcpy(id, adopted_id, sizeof(adopted_id));
    return ASTRA_OK;
}

AstraResult astra_fonts_use_snapshot(AstraArea *area, uint32_t length, const char *id)
{
    assert(length == 128u && strcmp(id, "system:test") == 0);
    assert(area->handle == 2u && area->address == document &&
        area->map_flags == ASTRA_AREA_MAP_READ);
    if (fail_snapshot) return ASTRA_ERROR_INVALID_ARGUMENT;
    memcpy(adopted_id, id, strlen(id) + 1u);
    *area = (AstraArea)ASTRA_AREA_INIT;
    ++installed_contexts;
    return ASTRA_OK;
}

AstraResult astra_message_header_init(AstraMessageHeader *header,
                                      uint32_t total_size, uint32_t protocol,
                                      uint16_t version, uint32_t operation,
                                      uint32_t transaction)
{
    astra_message_header_set(header, total_size, protocol, version,
                             operation, transaction);
    return ASTRA_OK;
}

AstraResult astra_port_call(AstraHandle service, AstraCall *call,
                            AstraMonotonicDeadline deadline)
{
    const AstraFontServiceRequest *request = call->request;
    AstraFontServiceReply *reply = call->reply;

    (void)deadline;
    assert(service == 1u || service == 5u);
    if (request->header.protocol == ASTRA_GUI_PROTOCOL &&
        request->header.operation == ASTRA_GUI_FONT_MEASURE) {
        const AstraGuiFontMeasureRequest *query = call->request;
        AstraGuiFontMeasureReply *metrics = call->reply;
        uint32_t at = 0u;
        ++measure_calls;
        assert(call->request_size == sizeof(*query) &&
               call->reply_capacity == sizeof(*metrics));
        assert(strcmp(query->face_id, "user:test") == 0 &&
               query->pixel_height == 13u && query->reserved16 == 0u);
        assert(query->text_length <= ASTRA_GUI_FONT_MEASURE_TEXT_MAX &&
               astra_utf8_validate(query->text, query->text_length, 0u));
        astra_message_header_set(&metrics->header, sizeof(*metrics),
            ASTRA_GUI_PROTOCOL, ASTRA_GUI_VERSION,
            ASTRA_GUI_FONT_MEASURE_REPLY, query->header.transaction_id);
        while (at < query->text_length &&
               metrics->advance + 65u <= query->maximum_advance) {
            assert(astra_utf8_scalar_advance(query->text, query->text_length, &at));
            metrics->advance += 65u;
        }
        metrics->fitted_bytes = at;
        metrics->line_height = 16u;
        call->reply_size = sizeof(*metrics);
        if (bad_reply == 1u) --call->reply_size;
        if (bad_reply == 2u) ++metrics->header.protocol_version;
        if (bad_reply == 3u) metrics->advance = query->maximum_advance + 1u;
        if (bad_reply == 4u) metrics->fitted_bytes = 1u;
        if (bad_reply == 5u) metrics->fitted_bytes = query->text_length + 1u;
        if (bad_reply == 6u) {
            metrics->status = ASTRA_STATUS_UNSUPPORTED;
            metrics->advance = metrics->fitted_bytes = 0u;
            metrics->line_height = 0u;
        }
        return ASTRA_OK;
    }
    if (request->header.protocol == ASTRA_GUI_PROTOCOL) {
        const AstraGuiFontContextRequest *query = call->request;
        AstraGuiFontContext *context = call->reply;
        assert(call->request_size == sizeof(*query) &&
            request->header.operation == ASTRA_GUI_FONT_CONTEXT);
        assert(strcmp(query->adopted_id, adopted_id) == 0);
        assert(query->window == requested_window);
        assert(call->reply_capacity == sizeof(*context));
        astra_message_header_set(&context->header, sizeof(*context),
            ASTRA_GUI_PROTOCOL, ASTRA_GUI_VERSION,
            ASTRA_GUI_FONT_CONTEXT_REPLY, request->header.transaction_id);
        context->area_size = 128u;
        memcpy(context->face_id, "system:test", sizeof("system:test"));
        call->reply_size = bad_reply == 1u ? sizeof(*context) - 1u : sizeof(*context);
        call->reply_handle_count = 1u;
        *call->reply_handles = 2u;
        if (bad_reply == 2u) context->header.protocol_version = 99u;
        return ASTRA_OK;
    }
    assert(call->request_size == sizeof(*request));
    assert(call->reply_capacity == sizeof(*reply));
    if (request->header.operation == ASTRA_FONT_SERVICE_IMPORT_BEGIN) {
        assert(call->handle_count == 2u && call->reply_index == 2u);
        assert(call->handles[0] == 4u && call->handles[1] == 7u);
        assert(request->source_size == imported_length);
        assert(strcmp(request->source_name, "test.afnt") == 0);
        assert(request->source_encoding == ASTRA_FONT_ENCODING_AUTO);
        call->handles[0] = ASTRA_INVALID_HANDLE;
        call->handles[1] = ASTRA_INVALID_HANDLE;
    } else
        assert(call->handle_count == 0u && call->reply_index == 0u);
    astra_message_header_set(&reply->header, sizeof(*reply),
                             ASTRA_FONT_SERVICE_PROTOCOL,
                             ASTRA_FONT_SERVICE_VERSION,
                             ASTRA_FONT_SERVICE_REPLY,
                             request->header.transaction_id);
    reply->status = ASTRA_STATUS_OK;
    reply->generation = document_generation;
    reply->area_size = request->header.operation ==
        ASTRA_FONT_SERVICE_CATALOG ? document_size :
        request->header.operation == ASTRA_FONT_SERVICE_INFORMATION ? document_size :
        request->header.operation == ASTRA_FONT_SERVICE_OPEN_FACE ? 128u : 0u;
    memcpy(reply->selected_id, "system:test", sizeof("system:test"));
    uint32_t job = request->header.operation == ASTRA_FONT_SERVICE_IMPORT_BEGIN ||
        request->header.operation == ASTRA_FONT_SERVICE_IMPORT_STATE ||
        request->header.operation == ASTRA_FONT_SERVICE_IMPORT_CANCEL ||
        request->header.operation == ASTRA_FONT_SERVICE_IMPORT_RELEASE;
    if (job) {
        reply->import_state = job_state;
        reply->total_faces = 1u;
        reply->completed_faces = job_state == ASTRA_FONT_IMPORT_SUCCEEDED ? 1u : 0u;
        reply->import_status = job_state == ASTRA_FONT_IMPORT_FAILED ? ASTRA_STATUS_UNSUPPORTED : 0u;
        if (job_state == ASTRA_FONT_IMPORT_SUCCEEDED && bad_reply != 3u)
            memcpy(reply->imported_id, "user:test", sizeof("user:test"));
        if (bad_reply == 4u) reply->completed_faces = 2u;
        if (bad_reply == 5u) reply->import_state = 99u;
    }
    call->reply_size = bad_reply == 1u ? sizeof(*reply) - 1u : sizeof(*reply);
    call->reply_handle_count =
        request->header.operation == ASTRA_FONT_SERVICE_CATALOG ||
        request->header.operation == ASTRA_FONT_SERVICE_OPEN_FACE ||
        request->header.operation == ASTRA_FONT_SERVICE_INFORMATION ? 1u :
        request->header.operation == ASTRA_FONT_SERVICE_IMPORT_BEGIN ? 1u : 0u;
    if (call->reply_handle_count == 1u) *call->reply_handles =
        request->header.operation == ASTRA_FONT_SERVICE_IMPORT_BEGIN ? 5u : 2u;
    if (bad_reply == 2u) reply->header.protocol_version = 99u;
    return ASTRA_OK;
}

AstraResult astra_area_map(AstraArea *area, uint32_t flags)
{
    assert(area->handle == 2u || area->handle == 3u);
    assert(flags == (area->handle == 2u ? ASTRA_AREA_MAP_READ :
                     ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE));
    if (fail_context_map) return ASTRA_ERROR_OUT_OF_MEMORY;
    area->address = area->handle == 2u ? document : imported_bytes;
    area->size = area->handle == 2u ? sizeof(document) :
                                        sizeof(imported_bytes);
    area->map_flags = flags;
    return ASTRA_OK;
}

AstraResult astra_area_create(uint32_t size, uint32_t rights, AstraArea *area)
{
    assert(size == imported_length && rights ==
        (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
         ASTRA_RIGHT_TRANSFER));
    area->handle = 3u;
    return ASTRA_OK;
}

AstraResult astra_area_unmap(AstraArea *area)
{
    assert(area->handle == 3u && area->address == imported_bytes);
    area->address = NULL;
    area->size = 0u;
    area->map_flags = 0u;
    return ASTRA_OK;
}

AstraResult astra_handle_duplicate(AstraHandle source, uint32_t rights,
                                    AstraHandle *duplicate)
{
    assert(source == 3u && rights ==
        (ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER));
    *duplicate = 4u;
    return ASTRA_OK;
}

AstraResult astra_area_close(AstraArea *area)
{
    assert(area->handle == 2u || area->handle == 3u);
    area->handle = ASTRA_INVALID_HANDLE;
    area->address = NULL;
    area->size = 0u;
    area->map_flags = 0u;
    ++closed_handles;
    return ASTRA_OK;
}

AstraResult astra_handle_close(AstraHandle *handle)
{
    assert(*handle == 2u || *handle == 4u || *handle == 5u || *handle == 6u || *handle == 7u);
    *handle = ASTRA_INVALID_HANDLE;
    ++closed_handles;
    return ASTRA_OK;
}

static void information_tests(void)
{
    const char prefix[] = "{\"family\":\"Test\",\"license\":\"";
    const char suffix[] = "\\r\\n\\t\\u03a9\",\"source_files\":[{\"name\":\"source.ttf\","
        "\"sha256\":\"abc\"}],\"notices\":[{\"name_id\":13,\"platform_id\":3,"
        "\"encoding_id\":1,\"language_id\":1033,\"raw_hex\":\"0041\",\"text\":\"A\"},"
        "{\"name_id\":14,\"platform_id\":42,\"encoding_id\":99,\"language_id\":0,"
        "\"raw_hex\":\"ff00\"}]}";
    memcpy(document, prefix, sizeof(prefix) - 1u);
    memset(document + sizeof(prefix) - 1u, 'L', 32768u);
    memcpy(document + sizeof(prefix) - 1u + 32768u, suffix, sizeof(suffix) - 1u);
    document_size = sizeof(prefix) - 1u + 32768u + sizeof(suffix) - 1u;
    AstraFontInformation info = ASTRA_FONT_INFORMATION_INIT;
    AstraFontNotice notice;
    uint32_t bytes = 0u;
    char output[32774];
    assert(astra_fonts_information_open(1u, "system:test", 11u, &info) == ASTRA_OK);
    assert(info.source_count == 1u && info.notice_count == 2u);
    assert(astra_fonts_information_open(1u, "system:test", 11u, &info) == ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_fonts_information_string(&info, ASTRA_FONT_INFORMATION_LICENSE, 0u,
        NULL, 0u, &bytes) == ASTRA_OK && bytes == 32773u);
    memset(output, 'Z', sizeof(output));
    assert(astra_fonts_information_string(&info, ASTRA_FONT_INFORMATION_LICENSE, 0u,
        output, bytes, &bytes) == ASTRA_ERROR_BUFFER_TOO_SMALL && output[0] == 'Z');
    assert(astra_fonts_information_string(&info, ASTRA_FONT_INFORMATION_LICENSE, 0u,
        output, sizeof(output), &bytes) == ASTRA_OK &&
        memcmp(output + 32768u, "\r\n\t\xce\xa9", 6u) == 0);
    assert(astra_fonts_information_string(&info, ASTRA_FONT_INFORMATION_SOURCE_NAME, 0u,
        output, sizeof(output), &bytes) == ASTRA_OK && strcmp(output, "source.ttf") == 0);
    assert(astra_fonts_information_string(&info, ASTRA_FONT_INFORMATION_SOURCE_SHA256, 0u,
        output, sizeof(output), &bytes) == ASTRA_OK && strcmp(output, "abc") == 0);
    assert(astra_fonts_information_notice(&info, 0u, &notice) == ASTRA_OK &&
        notice.name_id == 13u && notice.platform_id == 3u &&
        notice.encoding_id == 1u && notice.language_id == 1033u);
    assert(astra_fonts_information_notice(&info, 1u, &notice) == ASTRA_OK &&
        notice.platform_id == 42u);
    assert(astra_fonts_information_string(&info, ASTRA_FONT_INFORMATION_NOTICE_TEXT, 1u,
        output, sizeof(output), &bytes) == ASTRA_ERROR_NOT_PRESENT && bytes == 0u);
    assert(astra_fonts_information_string(&info, ASTRA_FONT_INFORMATION_NOTICE_RAW_HEX, 1u,
        output, sizeof(output), &bytes) == ASTRA_OK && strcmp(output, "ff00") == 0);
    assert(astra_fonts_information_string(&info, ASTRA_FONT_INFORMATION_FAMILY, 1u,
        NULL, 0u, &bytes) == ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_fonts_information_notice(&info, 2u, &notice) == ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_fonts_information_close(&info) == ASTRA_OK && info._private_bytes == 0u);
    uint32_t before = closed_handles;
    document_size = 1048577u;
    assert(astra_fonts_information_open(1u, "system:test", 11u, &info) == ASTRA_ERROR_IO);
    assert(closed_handles == before + 1u);
    const char *invalid[] = {"{\"x\":[,]}", "{\"notices\":false}",
        "{\"notices\":[],\"notices\":[]}", "[]"};
    for (uint32_t at = 0u; at < sizeof(invalid) / sizeof(invalid[0]); ++at) {
        document_size = (uint32_t)strlen(invalid[at]);
        memcpy(document, invalid[at], document_size);
        assert(astra_fonts_information_open(1u, "system:test", 11u, &info) == ASTRA_ERROR_IO &&
            info._private_area.handle == ASTRA_INVALID_HANDLE && info._private_bytes == 0u);
    }
    memcpy(document, "{}", 2u); document_size = 2u;
    fail_context_map = 1u;
    assert(astra_fonts_information_open(1u, "system:test", 11u, &info) == ASTRA_ERROR_OUT_OF_MEMORY);
    fail_context_map = 0u;
    assert(astra_fonts_information_open(1u, "system:test", 11u, &info) == ASTRA_OK);
    assert(info.source_count == 0u && info.notice_count == 0u);
    assert(astra_fonts_information_close(&info) == ASTRA_OK);
}

int main(void)
{
    AstraFontTextMetrics metrics = ASTRA_FONT_TEXT_METRICS_INIT;
    char text[520];
    memset(text, 'A', sizeof(text));
    assert(astra_fonts_text_measure(1u, "user:test", 9u, 13u,
        text, sizeof(text), UINT32_MAX, &metrics) == ASTRA_OK);
    assert(measure_calls == 3u && metrics.width == 529u && metrics.fitted_bytes == sizeof(text));
    assert(astra_fonts_text_measure(1u, "user:test", 9u, 13u,
        text, sizeof(text), 260u, &metrics) == ASTRA_OK);
    assert(metrics.width == 260u && metrics.fitted_bytes == 256u);
    text[255] = (char)0xc4;
    text[256] = (char)0x84;
    assert(astra_fonts_text_measure(1u, "user:test", 9u, 13u,
        text, sizeof(text), UINT32_MAX, &metrics) == ASTRA_OK);
    assert(metrics.width == 528u && metrics.fitted_bytes == sizeof(text));
    assert(astra_fonts_text_measure(1u, "user:test", 9u, 13u,
        "\xc4\x84", 2u, 1u, &metrics) == ASTRA_OK);
    assert(metrics.width == 0u && metrics.fitted_bytes == 0u);
    for (bad_reply = 1u; bad_reply <= 5u; ++bad_reply) {
        assert(astra_fonts_text_measure(1u, "user:test", 9u, 13u,
            "\xc4\x84", 2u, UINT32_MAX, &metrics) == ASTRA_ERROR_IO);
        assert(metrics.width == 0u && metrics.fitted_bytes == 0u);
    }
    assert(astra_fonts_text_measure(1u, "user:test", 9u, 13u,
        "A", 1u, UINT32_MAX, &metrics) == ASTRA_ERROR_UNSUPPORTED);
    bad_reply = 0u;
    assert(astra_fonts_text_measure(1u, "user:test", 9u, 13u,
        "\xff", 1u, UINT32_MAX, &metrics) == ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_fonts_text_measure(1u, "user:test", 9u, 13u,
        NULL, 0u, UINT32_MAX, &metrics) == ASTRA_OK);
    assert(metrics.width == 0u && metrics.fitted_bytes == 0u && metrics.height == 16u);
    const AstraFontCatalogStrike strike = {8u, 16u, 5u, 0u};
    const AstraFontCatalogFace source = {
        "system:test", 11u, "Test", 4u, "Regular", 7u,
        &strike, 1u, ASTRA_FONT_CATALOG_SYSTEM
    };
    AstraFontsCatalog catalog = ASTRA_FONTS_CATALOG_INIT;
    AstraInstalledFont face;
    AstraInstalledFontStrike size;

    assert(astra_font_catalog_pack(&source, 1u, 7u, document,
                                   sizeof(document), &document_size));
    assert(astra_fonts_catalog_open(1u, &catalog) == ASTRA_OK);
    assert(catalog.generation == 7u && catalog.face_count == 1u);
    assert(strcmp(catalog.selected_id, "system:test") == 0);
    assert(astra_fonts_catalog_face(&catalog, 0u, &face) == ASTRA_OK);
    assert(face.family_length == 4u && memcmp(face.family, "Test", 4u) == 0);
    assert(astra_fonts_catalog_strike(&catalog, 0u, 0u, &size) == ASTRA_OK);
    assert(size.pixel_height == 16u && size.bitmap_format == 5u);
    assert(astra_fonts_catalog_strike(&catalog, 0u, 1u, &size) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_fonts_select(1u, face.id, face.id_length) == ASTRA_OK);
    assert(astra_fonts_uninstall(1u, "user:test", 9u) == ASTRA_OK);
    assert(astra_fonts_uninstall(1u, "", 0u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_fonts_catalog_close(&catalog) == ASTRA_OK);
    assert(closed_handles == 1u);

    bad_reply = 1u;
    assert(astra_fonts_catalog_open(1u, &catalog) == ASTRA_ERROR_IO);
    assert(closed_handles == 2u);
    bad_reply = 2u;
    assert(astra_fonts_catalog_open(1u, &catalog) == ASTRA_ERROR_IO);
    assert(closed_handles == 3u);
    bad_reply = 0u;
    document_generation = 8u;
    assert(astra_fonts_catalog_open(1u, &catalog) == ASTRA_ERROR_IO);
    assert(closed_handles == 4u);
    document_generation = 7u;
    document[0] = 0u;
    assert(astra_fonts_catalog_open(1u, &catalog) == ASTRA_ERROR_IO);
    assert(closed_handles == 5u);
    assert(catalog.generation == 0u);
    AstraFontImport job = ASTRA_FONT_IMPORT_INIT;
    AstraFontImportProgress progress = ASTRA_FONT_IMPORT_PROGRESS_INIT;
    const char source_bytes[] = "AFNT";
    imported_length = sizeof(source_bytes) - 1u;
    bad_reply = 0u;
    assert(astra_fonts_import_begin(1u, source_bytes, imported_length,
                              "test.afnt", 9u, ASTRA_FONT_ENCODING_AUTO,
                              &job) == ASTRA_OK);
    assert(astra_fonts_import_wait_handle(&job) == 6u);
    assert(astra_fonts_import_state(&job, &progress) == ASTRA_OK &&
           progress.state == ASTRA_FONT_IMPORT_SUCCEEDED && progress.result == ASTRA_OK);
    assert(strcmp(progress.installed_id, "user:test") == 0);
    assert(memcmp(imported_bytes, source_bytes, imported_length) == 0);
    assert(closed_handles == 6u);
    assert(astra_fonts_import_close(&job) == ASTRA_OK && closed_handles == 8u);
    bad_reply = 3u;
    assert(astra_fonts_import_begin(1u, source_bytes, imported_length,
                              "test.afnt", 9u, ASTRA_FONT_ENCODING_AUTO,
                              &job) ==
           ASTRA_ERROR_IO);
    assert(job._private_control == ASTRA_INVALID_HANDLE && closed_handles == 11u);
    assert(astra_fonts_import_begin(1u, source_bytes, 0u, "test.afnt", 9u,
                              ASTRA_FONT_ENCODING_AUTO,
                              &job) == ASTRA_ERROR_INVALID_ARGUMENT);
    AstraArea rendered = ASTRA_AREA_INIT;
    uint32_t face_bytes = 0u;
    bad_reply = 0u;
    assert(astra_fonts_face_open(1u, "system:test", 11u, &rendered,
                                 &face_bytes) == ASTRA_OK);
    assert(face_bytes == 128u && rendered.address == document);
    assert(astra_area_close(&rendered) == ASTRA_OK && closed_handles == 12u);
    assert(astra_fonts_face_open(1u, "", 0u, &rendered,
                                 &face_bytes) == ASTRA_ERROR_INVALID_ARGUMENT);
    bad_reply = 2u;
    assert(astra_fonts_face_open(1u, "system:test", 11u, &rendered,
                                 &face_bytes) == ASTRA_ERROR_IO);
    assert(closed_handles == 13u);
    bad_reply = 0u;
    assert(astra_fonts_use_system(1u, NULL) == ASTRA_OK && installed_contexts == 1u);
    assert(astra_fonts_use_system(ASTRA_INVALID_HANDLE, NULL) == ASTRA_ERROR_INVALID_ARGUMENT);
    bad_reply = 1u;
    assert(astra_fonts_use_system(1u, NULL) == ASTRA_ERROR_IO && installed_contexts == 1u);
    bad_reply = 2u;
    assert(astra_fonts_use_system(1u, NULL) == ASTRA_ERROR_IO && installed_contexts == 1u);
    assert(closed_handles == 15u);
    bad_reply = 0u;
    strcpy(adopted_id, "system:prior");
    fail_context_map = 1u;
    assert(astra_fonts_use_system(1u, NULL) == ASTRA_ERROR_OUT_OF_MEMORY &&
           installed_contexts == 1u && strcmp(adopted_id, "system:prior") == 0);
    fail_context_map = 0u;
    fail_snapshot = 1u;
    assert(astra_fonts_use_system(1u, NULL) == ASTRA_ERROR_INVALID_ARGUMENT &&
           installed_contexts == 1u && strcmp(adopted_id, "system:prior") == 0);
    fail_snapshot = 0u;
    assert(astra_fonts_use_system(1u, NULL) == ASTRA_OK && installed_contexts == 2u);
    assert(closed_handles == 17u);
    AstraWindowEvent change = {.size = sizeof(change), .version = ASTRA_WINDOW_EVENT_VERSION,
        .type = ASTRA_WINDOW_EVENT_FONT_CHANGED};
    change.data.font_changed.generation = 7u;
    change.data.font_changed.window = requested_window = 42u;
    fail_context_map = 1u;
    assert(astra_fonts_use_system(1u, &change) == ASTRA_OK && installed_contexts == 2u &&
           closed_handles == 18u);
    fail_context_map = 0u;
    change.data.font_changed.window = 0u;
    assert(astra_fonts_use_system(1u, &change) == ASTRA_ERROR_INVALID_ARGUMENT);
    change.data.font_changed.window = 42u;
    change.data.font_changed.generation = 0u;
    assert(astra_fonts_use_system(1u, &change) == ASTRA_ERROR_INVALID_ARGUMENT);
    change.data.font_changed.generation = 7u;
    --change.version;
    assert(astra_fonts_use_system(1u, &change) == ASTRA_ERROR_INVALID_ARGUMENT);
    information_tests();
    bad_reply = 0u;
    job_state = ASTRA_FONT_IMPORT_RUNNING;
    assert(astra_fonts_import_begin(1u, source_bytes, imported_length, "test.afnt", 9u,
        ASTRA_FONT_ENCODING_AUTO, &job) == ASTRA_OK);
    assert(astra_fonts_import_state(&job, &progress) == ASTRA_OK &&
        progress.state == ASTRA_FONT_IMPORT_RUNNING && progress.result == ASTRA_OK &&
        progress.installed_id[0] == '\0');
    assert(astra_fonts_import_begin(1u, source_bytes, imported_length, "test.afnt", 9u,
        ASTRA_FONT_ENCODING_AUTO, &job) == ASTRA_ERROR_INVALID_ARGUMENT);
    job_state = ASTRA_FONT_IMPORT_CANCELLING;
    assert(astra_fonts_import_cancel(&job) == ASTRA_OK);
    job_state = ASTRA_FONT_IMPORT_CANCELLED;
    assert(astra_fonts_import_state(&job, &progress) == ASTRA_OK &&
        progress.result == ASTRA_ERROR_CANCELLED && !progress.installed_id[0]);
    job_state = ASTRA_FONT_IMPORT_FAILED;
    assert(astra_fonts_import_state(&job, &progress) == ASTRA_OK &&
        progress.result == ASTRA_ERROR_UNSUPPORTED);
    for (bad_reply = 4u; bad_reply <= 5u; ++bad_reply)
        assert(astra_fonts_import_state(&job, &progress) == ASTRA_ERROR_IO);
    bad_reply = 0u;
    hint_result = ASTRA_ERROR_PEER_DEAD;
    assert(astra_fonts_import_state(&job, &progress) == ASTRA_ERROR_PEER_DEAD);
    hint_result = ASTRA_ERROR_WOULD_BLOCK;
    assert(astra_fonts_import_close(&job) == ASTRA_OK);
    assert(astra_fonts_import_wait_handle(&job) == ASTRA_INVALID_HANDLE);
    assert(astra_fonts_import_close(&job) == ASTRA_OK);
    assert(astra_fonts_import_state(&job, &progress) == ASTRA_ERROR_INVALID_ARGUMENT);
    fail_context_map = 1u;
    uint32_t before_failed_begin = closed_handles;
    assert(astra_fonts_import_begin(1u, source_bytes, imported_length, "test.afnt", 9u,
        ASTRA_FONT_ENCODING_AUTO, &job) == ASTRA_ERROR_OUT_OF_MEMORY);
    assert(closed_handles == before_failed_begin + 3u &&
        job._private_control == ASTRA_INVALID_HANDLE && job._private_changed == ASTRA_INVALID_HANDLE);
    fail_context_map = 0u;
    return 0;
}
