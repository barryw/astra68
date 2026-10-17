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

static AstraResult exchange(AstraHandle service, uint32_t operation,
                            const char *id, uint32_t id_length,
                            AstraFontServiceReply *reply,
                            AstraHandle *area)
{
    AstraFontServiceRequest request = {0};
    AstraCall call = {0};
    AstraResult result;

    *area = ASTRA_INVALID_HANDLE;
    result = astra_message_header_init(
        &request.header, sizeof(request), ASTRA_FONT_SERVICE_PROTOCOL,
        ASTRA_FONT_SERVICE_VERSION, operation, 1u);
    if (result != ASTRA_OK) return result;
    if (id_length != 0u) memcpy(request.face_id, id, id_length);
    call.request = &request;
    call.request_size = sizeof(request);
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
        (reply->status == ASTRA_STATUS_OK &&
         ((operation == ASTRA_FONT_SERVICE_CATALOG) !=
          (call.reply_handle_count == 1u))) ||
        (reply->status != ASTRA_STATUS_OK &&
         call.reply_handle_count != 0u) ||
        (operation != ASTRA_FONT_SERVICE_CATALOG &&
         reply->catalog_size != 0u) ||
        (reply->status != ASTRA_STATUS_OK && reply->catalog_size != 0u)) {
        result = ASTRA_ERROR_IO;
        goto done;
    }
    result = reply->status == ASTRA_STATUS_NOT_FOUND ?
             ASTRA_ERROR_NOT_PRESENT : astra_result_from_service(reply->status);
done:
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
                      &reply, &handle);
    if (result != ASTRA_OK) return result;
    if (reply.catalog_size < sizeof(header) ||
        reply.catalog_size > ASTRA_AREA_SIZE_MAX) {
        result = ASTRA_ERROR_IO;
        goto fail;
    }
    catalog->_private_area.handle = handle;
    handle = ASTRA_INVALID_HANDLE;
    result = astra_area_map(&catalog->_private_area, ASTRA_AREA_MAP_READ);
    if (result != ASTRA_OK) goto fail;
    if (catalog->_private_area.size < reply.catalog_size ||
        !astra_font_catalog_validate(catalog->_private_area.address,
                                     reply.catalog_size)) {
        result = ASTRA_ERROR_IO;
        goto fail;
    }
    memcpy(&header, catalog->_private_area.address, sizeof(header));
    if (header.generation != reply.generation) {
        result = ASTRA_ERROR_IO;
        goto fail;
    }
    catalog->_catalog_bytes = reply.catalog_size;
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
                    &reply, &ignored);
}
