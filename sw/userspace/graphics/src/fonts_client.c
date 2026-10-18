#include <astra/fonts.h>
#include <astra/afnt.h>
#include <astra/json.h>

#include <astra/font_catalog.h>
#include <astra/font_service.h>
#include <astra/gui.h>

#include <astra/port.h>
#include <astra/resource.h>
#include <astra/result.h>
#include <astra/utf8.h>

#include <string.h>

_Static_assert(ASTRA_FONT_ID_MAX == ASTRA_FONT_FACE_ID_MAX,
               "font id contract drift");
_Static_assert(ASTRA_INSTALLED_FONT_SYSTEM == ASTRA_FONT_CATALOG_SYSTEM,
               "font system flag drift");
_Static_assert(ASTRA_INSTALLED_FONT_SELECTABLE == ASTRA_FONT_CATALOG_SELECTABLE,
               "font selectability flag drift");
_Static_assert(ASTRA_INSTALLED_FONT_BASIC_TEXT == ASTRA_FONT_CATALOG_BASIC_TEXT,
               "font text coverage flag drift");
_Static_assert(ASTRA_FONT_IMPORT_MAX == ASTRA_FONT_SOURCE_MAX,
               "font import size drift");
_Static_assert(ASTRA_FONT_IMPORT_NAME_MAX == ASTRA_FONT_SOURCE_NAME_MAX,
               "font source name drift");

AstraResult astra_fonts_text_measure(
    AstraHandle gui, const char *id, uint32_t id_length,
    uint16_t pixel_height, const char *text, uint32_t length,
    uint32_t maximum_width, AstraFontTextMetrics *metrics)
{
    uint32_t at = 0u;
    uint64_t advance = 0u;
    uint64_t maximum = (uint64_t)maximum_width * 64u;
    uint32_t line_height = 0u;

    if (metrics == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *metrics = (AstraFontTextMetrics)ASTRA_FONT_TEXT_METRICS_INIT;
    if (gui == ASTRA_INVALID_HANDLE || id == NULL || id_length == 0u ||
        id_length >= ASTRA_FONT_ID_MAX || pixel_height == 0u ||
        memchr(id, '\0', id_length) != NULL ||
        (length != 0u && !astra_utf8_validate(text, length, 0u)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    do {
        AstraGuiFontMeasureRequest request = {0};
        AstraGuiFontMeasureReply reply = {0};
        AstraCall call = {0};
        uint32_t take = length - at;
        AstraResult result;

        if (take > sizeof(request.text)) take = sizeof(request.text);
        while (take < length - at &&
               ((uint8_t)text[at + take] & 0xc0u) == 0x80u) --take;
        astra_message_header_set(&request.header, sizeof(request),
            ASTRA_GUI_PROTOCOL, ASTRA_GUI_VERSION, ASTRA_GUI_FONT_MEASURE, 1u);
        memcpy(request.face_id, id, id_length);
        request.pixel_height = pixel_height;
        request.text_length = take;
        request.maximum_advance = maximum - advance;
        if (take != 0u) memcpy(request.text, text + at, take);
        call.request = &request;
        call.request_size = sizeof(request);
        call.reply = &reply;
        call.reply_capacity = sizeof(reply);
        result = astra_port_call(gui, &call, ASTRA_DEADLINE_INFINITE);
        if (result != ASTRA_OK) return result;
        if (call.reply_size != sizeof(reply) || call.reply_handle_count != 0u ||
            reply.header.total_size != sizeof(reply) ||
            reply.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
            reply.header.protocol != ASTRA_GUI_PROTOCOL ||
            reply.header.protocol_version != ASTRA_GUI_VERSION ||
            reply.header.operation != ASTRA_GUI_FONT_MEASURE_REPLY ||
            reply.header.transaction_id != request.header.transaction_id ||
            reply.header.flags != 0u || reply.header.reserved != 0u ||
            reply.reserved != 0u ||
            reply.fitted_bytes > take ||
            (reply.fitted_bytes < take &&
             ((uint8_t)request.text[reply.fitted_bytes] & 0xc0u) == 0x80u) ||
            reply.advance > request.maximum_advance ||
            (reply.fitted_bytes == 0u && reply.advance != 0u) ||
            (reply.status == ASTRA_STATUS_OK &&
             (reply.line_height == 0u ||
              (line_height != 0u && line_height != reply.line_height))) ||
            (reply.status != ASTRA_STATUS_OK &&
             (reply.fitted_bytes != 0u || reply.advance != 0u || reply.line_height != 0u)))
            return ASTRA_ERROR_IO;
        result = astra_result_from_service(reply.status);
        if (result != ASTRA_OK) return result;
        advance += reply.advance;
        line_height = reply.line_height;
        at += reply.fitted_bytes;
        if (reply.fitted_bytes != take) break;
    } while (at < length);
    metrics->width = (uint32_t)((advance + 63u) / 64u);
    metrics->fitted_bytes = at;
    metrics->height = line_height;
    return ASTRA_OK;
}

AstraResult astra_fonts_use_system(AstraHandle gui, const AstraWindowEvent *change)
{
    AstraGuiFontContextRequest request = {0};
    AstraGuiFontContext reply = {0};
    AstraArea area = ASTRA_AREA_INIT;
    AstraCall call = {0};
    AstraResult result;

    if (gui == ASTRA_INVALID_HANDLE) return ASTRA_ERROR_INVALID_ARGUMENT;
    if (change != NULL) {
        if (change->size < sizeof(*change) || change->version != ASTRA_WINDOW_EVENT_VERSION ||
            change->type != ASTRA_WINDOW_EVENT_FONT_CHANGED ||
            change->data.font_changed.generation == 0u || change->data.font_changed.window == 0u)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        request.window = change->data.font_changed.window;
    }
    result = astra_fonts_context_id(request.adopted_id);
    if (result != ASTRA_OK) return result;
    result = astra_message_header_init(&request.header, sizeof(request),
        ASTRA_GUI_PROTOCOL, ASTRA_GUI_VERSION, ASTRA_GUI_FONT_CONTEXT, 1u);
    if (result != ASTRA_OK) return result;
    call.request = &request;
    call.request_size = sizeof(request);
    call.reply = &reply;
    call.reply_capacity = sizeof(reply);
    call.reply_handles = &area.handle;
    call.reply_handle_capacity = 1u;
    result = astra_port_call(gui, &call, ASTRA_DEADLINE_INFINITE);
    if (result != ASTRA_OK) goto done;
    if (call.reply_size != sizeof(reply) ||
        reply.header.total_size != sizeof(reply) ||
        reply.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        reply.header.protocol != ASTRA_GUI_PROTOCOL ||
        reply.header.protocol_version != ASTRA_GUI_VERSION ||
        reply.header.operation != ASTRA_GUI_FONT_CONTEXT_REPLY ||
        reply.header.transaction_id != request.header.transaction_id ||
        reply.header.flags != 0u || reply.header.reserved != 0u ||
        memchr(reply.face_id, '\0', sizeof(reply.face_id)) == NULL ||
        call.reply_handle_count > 1u ||
        reply.area_size > ASTRA_FONT_IMPORT_MAX ||
        (reply.area_size != 0u) != (call.reply_handle_count == 1u) ||
        (reply.area_size != 0u) != (reply.face_id[0] != '\0') ||
        (reply.status != ASTRA_STATUS_OK && reply.area_size != 0u)) {
        result = ASTRA_ERROR_IO;
        goto done;
    }
    result = astra_result_from_service(reply.status);
    if (result != ASTRA_OK) goto done;
    /* Immutable IDs need no remap; aliases share the active process mapping. */
    if (strcmp(reply.face_id, request.adopted_id) == 0) goto done;
    if (reply.area_size != 0u) {
        result = astra_area_map(&area, ASTRA_AREA_MAP_READ);
        if (result != ASTRA_OK) goto done;
        if (area.size < reply.area_size) {
            result = ASTRA_ERROR_IO;
            goto done;
        }
    }
    result = astra_fonts_use_snapshot(&area, reply.area_size, reply.face_id);
done:
    if (area.handle != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_area_close(&area);
        if (result == ASTRA_OK) result = closed;
    }
    return result;
}

static int import_reply_valid(const AstraFontServiceReply *reply)
{
    if (reply->import_state < ASTRA_FONT_IMPORT_RUNNING ||
        reply->import_state > ASTRA_FONT_IMPORT_CANCELLED ||
        reply->total_faces > 64u || reply->completed_faces > reply->total_faces)
        return 0;
    if (reply->import_state == ASTRA_FONT_IMPORT_SUCCEEDED)
        return reply->import_status == ASTRA_STATUS_OK &&
            reply->imported_id[0] != '\0' && reply->total_faces != 0u &&
            reply->completed_faces == reply->total_faces;
    return reply->imported_id[0] == '\0' &&
        (reply->import_state == ASTRA_FONT_IMPORT_FAILED ?
         reply->import_status != ASTRA_STATUS_OK : reply->import_status == ASTRA_STATUS_OK);
}

static AstraResult exchange(AstraHandle service, uint32_t operation,
                            const char *id, uint32_t id_length,
                            AstraHandle input_area, uint32_t source_size,
                            const char *source_name, uint32_t name_length,
                            uint32_t encoding,
                            AstraFontServiceReply *reply,
                            AstraHandle *area, AstraHandle notification)
{
    AstraFontServiceRequest request = {0};
    AstraCall call = {0};
    AstraHandle sent[2] = {input_area, notification};
    AstraHandle handles[2] = {ASTRA_INVALID_HANDLE, ASTRA_INVALID_HANDLE};
    uint32_t begin = operation == ASTRA_FONT_SERVICE_IMPORT_BEGIN;
    uint32_t job = begin || operation == ASTRA_FONT_SERVICE_IMPORT_STATE ||
                           operation == ASTRA_FONT_SERVICE_IMPORT_CANCEL ||
                           operation == ASTRA_FONT_SERVICE_IMPORT_RELEASE;
    uint32_t snapshot = operation == ASTRA_FONT_SERVICE_CATALOG ||
        operation == ASTRA_FONT_SERVICE_OPEN_FACE ||
        operation == ASTRA_FONT_SERVICE_INFORMATION;
    AstraResult result;

    *area = ASTRA_INVALID_HANDLE;
    result = astra_message_header_init(
        &request.header, sizeof(request), ASTRA_FONT_SERVICE_PROTOCOL,
        ASTRA_FONT_SERVICE_VERSION, operation, 1u);
    if (result != ASTRA_OK) goto done;
    if (id_length != 0u) memcpy(request.face_id, id, id_length);
    request.source_size = source_size;
    request.source_encoding = encoding;
    if (name_length != 0u)
        memcpy(request.source_name, source_name, name_length);
    call.request = &request;
    call.request_size = sizeof(request);
    call.handles = input_area == ASTRA_INVALID_HANDLE ? NULL : sent;
    call.handle_count = input_area == ASTRA_INVALID_HANDLE ? 0u :
        notification == ASTRA_INVALID_HANDLE ? 1u : 2u;
    call.reply_index = call.handle_count;
    call.reply = reply;
    call.reply_capacity = sizeof(*reply);
    call.reply_handles = handles;
    call.reply_handle_capacity = 2u;
    result = astra_port_call(service, &call, ASTRA_DEADLINE_INFINITE);
    if (result != ASTRA_OK) goto done;
    if (call.reply_size != sizeof(*reply) ||
        reply->header.total_size != sizeof(*reply) ||
        reply->header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        reply->header.flags != 0u ||
        reply->header.protocol != ASTRA_FONT_SERVICE_PROTOCOL ||
        reply->header.protocol_version != ASTRA_FONT_SERVICE_VERSION ||
        reply->header.operation != ASTRA_FONT_SERVICE_REPLY ||
        reply->header.transaction_id != request.header.transaction_id ||
        reply->header.reserved != 0u || reply->reserved != 0u ||
        reply->generation == 0u ||
        memchr(reply->selected_id, '\0', sizeof(reply->selected_id)) == NULL ||
        memchr(reply->imported_id, '\0', sizeof(reply->imported_id)) == NULL ||
        (reply->status == ASTRA_STATUS_OK &&
         call.reply_handle_count != (begin || snapshot ? 1u : 0u)) ||
        (reply->status != ASTRA_STATUS_OK &&
         call.reply_handle_count != 0u) ||
        (!snapshot && reply->area_size != 0u) ||
        (reply->status != ASTRA_STATUS_OK && reply->area_size != 0u) ||
        (reply->status == ASTRA_STATUS_OK && job ?
            !import_reply_valid(reply) :
            reply->import_state != 0u || reply->completed_faces != 0u ||
            reply->total_faces != 0u || reply->import_status != 0u ||
            reply->imported_id[0] != '\0') ||
        (call.reply_handle_count != 0u && handles[0] == ASTRA_INVALID_HANDLE)) {
        result = ASTRA_ERROR_IO;
        goto done;
    }
    result = reply->status == ASTRA_STATUS_NOT_FOUND ?
             ASTRA_ERROR_NOT_PRESENT : astra_result_from_service(reply->status);
    if (result == ASTRA_OK) {
        *area = handles[0];
        handles[0] = ASTRA_INVALID_HANDLE;
    }
done:
    for (uint32_t at = 0u; at < 2u; ++at)
        if (sent[at] != ASTRA_INVALID_HANDLE) {
            AstraResult ignored = astra_handle_close(&sent[at]);
            (void)ignored;
        }
    for (uint32_t at = 0u; at < 2u; ++at)
        if (handles[at] != ASTRA_INVALID_HANDLE) {
            AstraResult ignored = astra_handle_close(&handles[at]);
            (void)ignored;
        }
    return result;
}

AstraResult astra_fonts_catalog_open(AstraHandle service,
                                     AstraFontsCatalog *catalog)
{
    AstraFontServiceReply reply = {0};
    AstraFontCatalogHeader header;
    AstraHandle handle = ASTRA_INVALID_HANDLE;
    AstraResult result;

    if (service == ASTRA_INVALID_HANDLE || catalog == NULL ||
        catalog->_private_area.handle != ASTRA_INVALID_HANDLE ||
        catalog->_private_area.address != NULL ||
        catalog->_private_area.size != 0u ||
        catalog->_private_area.map_flags != 0u ||
        catalog->_catalog_bytes != 0u || catalog->generation != 0u ||
        catalog->face_count != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = exchange(service, ASTRA_FONT_SERVICE_CATALOG, NULL, 0u,
                      ASTRA_INVALID_HANDLE, 0u, NULL, 0u, 0u,
                      &reply, &handle, ASTRA_INVALID_HANDLE);
    if (result != ASTRA_OK) return result;
    if (reply.area_size < sizeof(header) ||
        reply.area_size > ASTRA_AREA_SIZE_MAX) {
        result = ASTRA_ERROR_IO;
        goto fail;
    }
    catalog->_private_area.handle = handle;
    handle = ASTRA_INVALID_HANDLE;
    result = astra_area_map(&catalog->_private_area, ASTRA_AREA_MAP_READ);
    if (result != ASTRA_OK) goto fail;
    if (catalog->_private_area.size < reply.area_size ||
        !astra_font_catalog_validate(catalog->_private_area.address,
                                     reply.area_size)) {
        result = ASTRA_ERROR_IO;
        goto fail;
    }
    memcpy(&header, catalog->_private_area.address, sizeof(header));
    if (header.generation != reply.generation) {
        result = ASTRA_ERROR_IO;
        goto fail;
    }
    catalog->_catalog_bytes = reply.area_size;
    catalog->generation = reply.generation;
    catalog->face_count = header.face_count;
    memcpy(catalog->selected_id, reply.selected_id,
           sizeof(catalog->selected_id));
    return ASTRA_OK;
fail:
    if (handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_handle_close(&handle);
        (void)ignored;
    }
    if (catalog->_private_area.handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_area_close(&catalog->_private_area);
        (void)ignored;
    }
    return result;
}

AstraResult astra_fonts_catalog_face(const AstraFontsCatalog *catalog,
                                     uint32_t index, AstraInstalledFont *face)
{
    AstraFontCatalogFace source;

    if (catalog == NULL || face == NULL || catalog->generation == 0u ||
        catalog->_private_area.address == NULL || index >= catalog->face_count)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!astra_font_catalog_face(catalog->_private_area.address,
                                 catalog->_catalog_bytes, index, &source))
        return ASTRA_ERROR_IO;
    *face = (AstraInstalledFont){
        source.id, source.id_length, source.family, source.family_length,
        source.style, source.style_length, source.strike_count, source.flags
    };
    return ASTRA_OK;
}

AstraResult astra_fonts_catalog_strike(const AstraFontsCatalog *catalog,
                                       uint32_t face_index,
                                       uint32_t strike_index,
                                       AstraInstalledFontStrike *strike)
{
    AstraFontCatalogFace source;

    if (catalog == NULL || strike == NULL || catalog->generation == 0u ||
        catalog->_private_area.address == NULL || face_index >= catalog->face_count)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!astra_font_catalog_face(catalog->_private_area.address,
                                 catalog->_catalog_bytes, face_index, &source))
        return ASTRA_ERROR_IO;
    if (strike_index >= source.strike_count) return ASTRA_ERROR_INVALID_ARGUMENT;
    *strike = (AstraInstalledFontStrike){
        source.strikes[strike_index].pixel_width,
        source.strikes[strike_index].pixel_height,
        source.strikes[strike_index].bitmap_format
    };
    return ASTRA_OK;
}

AstraResult astra_fonts_catalog_close(AstraFontsCatalog *catalog)
{
    AstraResult result;

    if (catalog == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_area_close(&catalog->_private_area);
    if (result == ASTRA_OK) {
        catalog->_catalog_bytes = 0u;
        catalog->generation = 0u;
        catalog->face_count = 0u;
        memset(catalog->selected_id, 0, sizeof(catalog->selected_id));
    }
    return result;
}

AstraResult astra_fonts_select(AstraHandle admin, const char *id,
                               uint32_t id_length)
{
    AstraFontServiceReply reply = {0};
    AstraHandle ignored = ASTRA_INVALID_HANDLE;

    if (admin == ASTRA_INVALID_HANDLE || id == NULL || id_length == 0u ||
        id_length >= ASTRA_FONT_ID_MAX ||
        memchr(id, '\0', id_length) != NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return exchange(admin, ASTRA_FONT_SERVICE_SELECT, id, id_length,
                    ASTRA_INVALID_HANDLE, 0u, NULL, 0u, 0u,
                    &reply, &ignored, ASTRA_INVALID_HANDLE);
}

AstraResult astra_fonts_uninstall(AstraHandle admin, const char *id,
                                  uint32_t id_length)
{
    AstraFontServiceReply reply = {0};
    AstraHandle ignored = ASTRA_INVALID_HANDLE;

    if (admin == ASTRA_INVALID_HANDLE || id == NULL || id_length == 0u ||
        id_length >= ASTRA_FONT_ID_MAX ||
        memchr(id, '\0', id_length) != NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return exchange(admin, ASTRA_FONT_SERVICE_UNINSTALL, id, id_length,
                    ASTRA_INVALID_HANDLE, 0u, NULL, 0u, 0u,
                    &reply, &ignored, ASTRA_INVALID_HANDLE);
}

static AstraResult snapshot_open(AstraHandle service, uint32_t operation,
                                  const char *id, uint32_t id_length,
                                  AstraArea *face, uint32_t minimum,
                                  uint32_t maximum, uint32_t *length)
{
    AstraFontServiceReply reply = {0};
    AstraHandle handle = ASTRA_INVALID_HANDLE;
    AstraResult result;

    if (service == ASTRA_INVALID_HANDLE || id == NULL || id_length == 0u ||
        id_length >= ASTRA_FONT_ID_MAX ||
        memchr(id, '\0', id_length) != NULL || face == NULL ||
        face->handle != ASTRA_INVALID_HANDLE || face->address != NULL ||
        face->size != 0u || face->map_flags != 0u || length == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *length = 0u;
    result = exchange(service, operation,
                      id, id_length, ASTRA_INVALID_HANDLE, 0u, NULL, 0u, 0u,
                      &reply, &handle, ASTRA_INVALID_HANDLE);
    if (result != ASTRA_OK) return result;
    if (reply.area_size < minimum || reply.area_size > maximum) {
        result = ASTRA_ERROR_IO;
        goto failed;
    }
    face->handle = handle;
    handle = ASTRA_INVALID_HANDLE;
    result = astra_area_map(face, ASTRA_AREA_MAP_READ);
    if (result != ASTRA_OK || face->size < reply.area_size) {
        if (result == ASTRA_OK) result = ASTRA_ERROR_IO;
        goto failed;
    }
    *length = reply.area_size;
    return ASTRA_OK;
failed:
    if (handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_handle_close(&handle);
        (void)ignored;
    }
    if (face->handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_area_close(face);
        (void)ignored;
    }
    return result;
}

AstraResult astra_fonts_face_open(AstraHandle render, const char *id,
                                  uint32_t id_length, AstraArea *face,
                                  uint32_t *length)
{
    return snapshot_open(render, ASTRA_FONT_SERVICE_OPEN_FACE, id, id_length,
                         face, 128u, ASTRA_FONT_IMPORT_MAX, length);
}

static int information_count(AstraJsonValue root, const char *key, uint32_t *count)
{
    AstraJsonValue array;
    int found = astra_json_member(root, key, &array);
    *count = 0u;
    return found == ASTRA_JSON_MISSING ||
        (found == ASTRA_JSON_FOUND && astra_json_array_count(array, count));
}

AstraResult astra_fonts_information_open(AstraHandle service, const char *id,
                                         uint32_t id_length,
                                         AstraFontInformation *information)
{
    if (information == NULL || information->_private_bytes != 0u ||
        information->source_count != 0u || information->notice_count != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    uint32_t length = 0u, sources = 0u, notices = 0u;
    AstraResult result = snapshot_open(service, ASTRA_FONT_SERVICE_INFORMATION,
        id, id_length, &information->_private_area, 2u, ASTRA_AFNT_NAME_MAX, &length);
    if (result != ASTRA_OK) return result;
    AstraJsonValue root = {information->_private_area.address, length};
    if (!information_count(root, "source_files", &sources) ||
        !information_count(root, "notices", &notices)) {
        AstraResult ignored = astra_area_close(&information->_private_area);
        (void)ignored;
        return ASTRA_ERROR_IO;
    }
    information->_private_bytes = length;
    information->source_count = sources;
    information->notice_count = notices;
    return ASTRA_OK;
}

static AstraResult information_object(const AstraFontInformation *information,
                                      const char *array_name, uint32_t index,
                                      AstraJsonValue *object)
{
    if (information == NULL || information->_private_bytes == 0u ||
        information->_private_area.address == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *object = (AstraJsonValue){information->_private_area.address,
                                information->_private_bytes};
    if (array_name == NULL)
        return index == 0u ? ASTRA_OK : ASTRA_ERROR_INVALID_ARGUMENT;
    uint32_t count = strcmp(array_name, "source_files") == 0 ?
        information->source_count : information->notice_count;
    if (index >= count) return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraJsonValue array;
    if (astra_json_member(*object, array_name, &array) != ASTRA_JSON_FOUND ||
        astra_json_element(array, index, object) != ASTRA_JSON_FOUND)
        return ASTRA_ERROR_IO;
    return ASTRA_OK;
}

AstraResult astra_fonts_information_string(const AstraFontInformation *information,
    uint32_t field, uint32_t index, char *text, uint32_t capacity, uint32_t *length)
{
    static const char *const keys[] = {"family", "style", "author", "license",
        "converter", "source_encoding", "name", "sha256", "text", "raw_hex",
        "language_tag", "language_tag_raw_hex"};
    if (length == NULL || (text == NULL && capacity != 0u) ||
        field < ASTRA_FONT_INFORMATION_FAMILY ||
        field > ASTRA_FONT_INFORMATION_NOTICE_LANGUAGE_RAW_HEX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *length = 0u;
    AstraJsonValue object, value;
    const char *array = field >= ASTRA_FONT_INFORMATION_NOTICE_TEXT ? "notices" :
        field >= ASTRA_FONT_INFORMATION_SOURCE_NAME ? "source_files" : NULL;
    AstraResult result = information_object(information, array, index, &object);
    if (result != ASTRA_OK) return result;
    int found = astra_json_member(object, keys[field - 1u], &value);
    if (found == ASTRA_JSON_MISSING) return ASTRA_ERROR_NOT_PRESENT;
    if (found != ASTRA_JSON_FOUND || !astra_json_string(value, NULL, 0u, length))
        return ASTRA_ERROR_IO;
    if (text != NULL && !astra_json_string(value, text, capacity, length))
        return ASTRA_ERROR_BUFFER_TOO_SMALL;
    return ASTRA_OK;
}

AstraResult astra_fonts_information_notice(const AstraFontInformation *information,
                                          uint32_t index, AstraFontNotice *notice)
{
    AstraJsonValue object, value;
    AstraFontNotice parsed = {0};
    if (notice == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    *notice = parsed;
    AstraResult result = information_object(information, "notices", index, &object);
    if (result != ASTRA_OK) return result;
    const char *keys[] = {"name_id", "platform_id", "encoding_id", "language_id"};
    uint32_t *fields[] = {&parsed.name_id, &parsed.platform_id,
                          &parsed.encoding_id, &parsed.language_id};
    for (uint32_t at = 0u; at < 4u; ++at)
        if (astra_json_member(object, keys[at], &value) != ASTRA_JSON_FOUND ||
            !astra_json_u32(value, fields[at]) || *fields[at] > UINT16_MAX)
            return ASTRA_ERROR_IO;
    *notice = parsed;
    return ASTRA_OK;
}

AstraResult astra_fonts_information_close(AstraFontInformation *information)
{
    if (information == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraResult result = astra_area_close(&information->_private_area);
    if (result == ASTRA_OK) {
        information->_private_bytes = 0u;
        information->source_count = 0u;
        information->notice_count = 0u;
    }
    return result;
}

AstraResult astra_fonts_import_begin(AstraHandle admin, const void *bytes,
                               uint32_t length, const char *source_name,
                               uint32_t name_length, uint32_t encoding,
                               AstraFontImport *import)
{
    AstraArea payload = ASTRA_AREA_INIT;
    AstraPort notifications = ASTRA_PORT_INIT;
    AstraHandle immutable = ASTRA_INVALID_HANDLE;
    AstraFontImport session = ASTRA_FONT_IMPORT_INIT;
    AstraFontServiceReply reply = {0};
    AstraResult result;

    if (admin == ASTRA_INVALID_HANDLE || bytes == NULL || length == 0u ||
        length > ASTRA_FONT_SOURCE_MAX || source_name == NULL ||
        name_length == 0u || name_length >= ASTRA_FONT_SOURCE_NAME_MAX ||
        memchr(source_name, '\0', name_length) != NULL ||
        !astra_utf8_validate(source_name, name_length, 0u) ||
        import == NULL || import->_private_control != ASTRA_INVALID_HANDLE ||
        import->_private_changed != ASTRA_INVALID_HANDLE ||
        encoding > ASTRA_FONT_ENCODING_ISO_8859_2)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_port_create(1u, sizeof(AstraMessageHeader), &notifications);
    if (result != ASTRA_OK) return result;
    session._private_changed = notifications.receive;
    notifications.receive = ASTRA_INVALID_HANDLE;
    result = astra_area_create(
        length, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
                ASTRA_RIGHT_TRANSFER, &payload);
    if (result != ASTRA_OK) goto done;
    result = astra_area_map(&payload,
                            ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE);
    if (result == ASTRA_OK) {
        if (payload.size < length) result = ASTRA_ERROR_IO;
        else memcpy(payload.address, bytes, length);
    }
    if (payload.address != NULL) {
        AstraResult unmapped = astra_area_unmap(&payload);
        if (result == ASTRA_OK && unmapped != ASTRA_OK) result = unmapped;
    }
    if (result == ASTRA_OK)
        result = astra_handle_duplicate(
            payload.handle,
            ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER,
            &immutable);
    if (result == ASTRA_OK) {
        result = exchange(admin, ASTRA_FONT_SERVICE_IMPORT_BEGIN, NULL, 0u,
                          immutable, length, source_name, name_length,
                          encoding,
                          &reply, &session._private_control, notifications.send);
        immutable = notifications.send = ASTRA_INVALID_HANDLE;
    }
done:
    {
        AstraResult closed = astra_area_close(&payload);
        if (result == ASTRA_OK && closed != ASTRA_OK) result = closed;
    }
    if (notifications.send != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_handle_close(&notifications.send);
        if (result == ASTRA_OK) result = closed;
    }
    if (result == ASTRA_OK) *import = session;
    else {
        AstraResult ignored = astra_fonts_import_close(&session);
        (void)ignored;
    }
    return result;
}

static AstraResult import_query(const AstraFontImport *import, uint32_t operation,
                                AstraFontImportProgress *progress)
{
    AstraFontServiceReply reply = {0};
    AstraHandle ignored = ASTRA_INVALID_HANDLE;
    if (import == NULL || import->_private_control == ASTRA_INVALID_HANDLE ||
        import->_private_changed == ASTRA_INVALID_HANDLE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (operation == ASTRA_FONT_SERVICE_IMPORT_STATE) {
        for (;;) {
            AstraMessageHeader hint = {0};
            AstraHandle handles[ASTRA_MESSAGE_HANDLES_MAX] = {0};
            uint32_t bytes = 0u, count = 0u;
            AstraResult received = astra_port_receive_try(import->_private_changed,
                &hint, sizeof(hint), handles, ASTRA_MESSAGE_HANDLES_MAX, &bytes, &count);
            if (received == ASTRA_ERROR_WOULD_BLOCK) break;
            if (received != ASTRA_OK) return received;
            for (uint32_t at = 0u; at < count; ++at) {
                AstraResult closed = astra_handle_close(&handles[at]);
                (void)closed;
            }
            if (bytes != sizeof(hint) || count != 0u || hint.total_size != sizeof(hint) ||
                hint.header_size != ASTRA_MESSAGE_HEADER_SIZE || hint.flags != 0u ||
                hint.protocol != ASTRA_FONT_SERVICE_PROTOCOL ||
                hint.protocol_version != ASTRA_FONT_SERVICE_VERSION ||
                hint.operation != ASTRA_FONT_SERVICE_IMPORT_CHANGED ||
                hint.transaction_id != 0u || hint.reserved != 0u)
                return ASTRA_ERROR_IO;
        }
    }
    AstraResult result = exchange(import->_private_control, operation, NULL, 0u,
        ASTRA_INVALID_HANDLE, 0u, NULL, 0u, 0u, &reply, &ignored, ASTRA_INVALID_HANDLE);
    if (result == ASTRA_OK && progress != NULL) {
        *progress = (AstraFontImportProgress)ASTRA_FONT_IMPORT_PROGRESS_INIT;
        progress->state = reply.import_state;
        progress->completed_faces = reply.completed_faces;
        progress->total_faces = reply.total_faces;
        progress->result = reply.import_state == ASTRA_FONT_IMPORT_CANCELLED ?
            ASTRA_ERROR_CANCELLED : astra_result_from_service(reply.import_status);
        memcpy(progress->installed_id, reply.imported_id, sizeof(progress->installed_id));
    }
    return result;
}

AstraResult astra_fonts_import_state(const AstraFontImport *import,
                                    AstraFontImportProgress *progress)
{
    if (progress == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    return import_query(import, ASTRA_FONT_SERVICE_IMPORT_STATE, progress);
}

AstraResult astra_fonts_import_cancel(const AstraFontImport *import)
{
    return import_query(import, ASTRA_FONT_SERVICE_IMPORT_CANCEL, NULL);
}

AstraHandle astra_fonts_import_wait_handle(const AstraFontImport *import)
{
    return import == NULL || import->_private_control == ASTRA_INVALID_HANDLE ?
        ASTRA_INVALID_HANDLE : import->_private_changed;
}

AstraResult astra_fonts_import_close(AstraFontImport *import)
{
    if (import == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraResult result = ASTRA_OK;
    if (import->_private_control != ASTRA_INVALID_HANDLE &&
        import->_private_changed != ASTRA_INVALID_HANDLE)
        result = import_query(import, ASTRA_FONT_SERVICE_IMPORT_RELEASE, NULL);
    if (import->_private_control != ASTRA_INVALID_HANDLE)
    {
        AstraResult closed = astra_handle_close(&import->_private_control);
        if (result == ASTRA_OK) result = closed;
    }
    if (import->_private_changed != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_handle_close(&import->_private_changed);
        if (result == ASTRA_OK) result = closed;
    }
    *import = (AstraFontImport)ASTRA_FONT_IMPORT_INIT;
    return result;
}
