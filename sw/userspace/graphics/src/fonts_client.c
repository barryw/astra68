#include <astra/fonts.h>

#include <astra/font_catalog.h>
#include <astra/font_service.h>
#include <astra/port.h>
#include <astra/resource.h>
#include <astra/result.h>

#include <string.h>

_Static_assert(ASTRA_FONT_ID_MAX == ASTRA_FONT_FACE_ID_MAX,
               "font id contract drift");
_Static_assert(ASTRA_INSTALLED_FONT_SYSTEM == ASTRA_FONT_CATALOG_SYSTEM,
               "font system flag drift");
_Static_assert(ASTRA_INSTALLED_FONT_SELECTABLE == ASTRA_FONT_CATALOG_SELECTABLE,
               "font selectability flag drift");
_Static_assert(ASTRA_FONT_IMPORT_MAX == ASTRA_FONT_SOURCE_MAX,
               "font import size drift");
_Static_assert(ASTRA_FONT_IMPORT_NAME_MAX == ASTRA_FONT_SOURCE_NAME_MAX,
               "font source name drift");

static AstraResult exchange(AstraHandle service, uint32_t operation,
                            const char *id, uint32_t id_length,
                            AstraHandle input_area, uint32_t source_size,
                            const char *source_name, uint32_t name_length,
                            uint32_t encoding,
                            AstraFontServiceReply *reply,
                            AstraHandle *area)
{
    AstraFontServiceRequest request = {0};
    AstraCall call = {0};
    AstraHandle sent = input_area;
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
    call.handles = input_area == ASTRA_INVALID_HANDLE ? NULL : &sent;
    call.handle_count = input_area == ASTRA_INVALID_HANDLE ? 0u : 1u;
    call.reply_index = call.handle_count;
    call.reply = reply;
    call.reply_capacity = sizeof(*reply);
    call.reply_handles = area;
    call.reply_handle_capacity = 1u;
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
         ((operation == ASTRA_FONT_SERVICE_CATALOG ||
           operation == ASTRA_FONT_SERVICE_OPEN_FACE) !=
          (call.reply_handle_count == 1u))) ||
        (reply->status != ASTRA_STATUS_OK &&
         call.reply_handle_count != 0u) ||
        (operation != ASTRA_FONT_SERVICE_CATALOG &&
         operation != ASTRA_FONT_SERVICE_OPEN_FACE &&
         reply->area_size != 0u) ||
        (reply->status != ASTRA_STATUS_OK && reply->area_size != 0u) ||
        (reply->status == ASTRA_STATUS_OK &&
         (operation == ASTRA_FONT_SERVICE_IMPORT ?
          reply->imported_id[0] == '\0' : reply->imported_id[0] != '\0')) ||
        (reply->status != ASTRA_STATUS_OK && reply->imported_id[0] != '\0')) {
        result = ASTRA_ERROR_IO;
        goto done;
    }
    result = reply->status == ASTRA_STATUS_NOT_FOUND ?
             ASTRA_ERROR_NOT_PRESENT : astra_result_from_service(reply->status);
done:
    if (sent != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_handle_close(&sent);
        (void)ignored;
    }
    if (result != ASTRA_OK && *area != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_handle_close(area);
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
                      &reply, &handle);
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
                    &reply, &ignored);
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
                    &reply, &ignored);
}

AstraResult astra_fonts_face_open(AstraHandle render, const char *id,
                                  uint32_t id_length, AstraArea *face,
                                  uint32_t *length)
{
    AstraFontServiceReply reply = {0};
    AstraHandle handle = ASTRA_INVALID_HANDLE;
    AstraResult result;

    if (render == ASTRA_INVALID_HANDLE || id == NULL || id_length == 0u ||
        id_length >= ASTRA_FONT_ID_MAX ||
        memchr(id, '\0', id_length) != NULL || face == NULL ||
        face->handle != ASTRA_INVALID_HANDLE || face->address != NULL ||
        face->size != 0u || face->map_flags != 0u || length == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *length = 0u;
    result = exchange(render, ASTRA_FONT_SERVICE_OPEN_FACE,
                      id, id_length, ASTRA_INVALID_HANDLE, 0u, NULL, 0u, 0u,
                      &reply, &handle);
    if (result != ASTRA_OK) return result;
    if (reply.area_size < 128u || reply.area_size > ASTRA_FONT_IMPORT_MAX) {
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

AstraResult astra_fonts_import(AstraHandle admin, const void *bytes,
                               uint32_t length, const char *source_name,
                               uint32_t name_length, uint32_t encoding,
                               char installed_id[ASTRA_FONT_ID_MAX])
{
    AstraArea payload = ASTRA_AREA_INIT;
    AstraHandle immutable = ASTRA_INVALID_HANDLE;
    AstraHandle ignored = ASTRA_INVALID_HANDLE;
    AstraFontServiceReply reply = {0};
    AstraResult result;

    if (admin == ASTRA_INVALID_HANDLE || bytes == NULL || length == 0u ||
        length > ASTRA_FONT_SOURCE_MAX || source_name == NULL ||
        name_length == 0u || name_length >= ASTRA_FONT_SOURCE_NAME_MAX ||
        memchr(source_name, '\0', name_length) != NULL ||
        installed_id == NULL || encoding > ASTRA_FONT_ENCODING_ISO_8859_2)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    installed_id[0] = '\0';
    result = astra_area_create(
        length, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
                ASTRA_RIGHT_TRANSFER, &payload);
    if (result != ASTRA_OK) return result;
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
    if (result == ASTRA_OK)
        result = exchange(admin, ASTRA_FONT_SERVICE_IMPORT, NULL, 0u,
                          immutable, length, source_name, name_length,
                          encoding,
                          &reply, &ignored);
    immutable = ASTRA_INVALID_HANDLE;
    {
        AstraResult closed = astra_area_close(&payload);
        if (result == ASTRA_OK && closed != ASTRA_OK) result = closed;
    }
    if (result == ASTRA_OK)
        memcpy(installed_id, reply.imported_id, sizeof(reply.imported_id));
    return result;
}
