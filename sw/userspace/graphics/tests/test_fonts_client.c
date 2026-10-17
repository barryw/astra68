#include <astra/fonts.h>
#include <astra/font_catalog.h>
#include <astra/font_service.h>
#include <astra/port.h>
#include <astra/resource.h>
#include <astra/status.h>

#include <assert.h>
#include <string.h>

static uint8_t document[512];
static uint32_t document_size;
static uint32_t document_generation = 7u;
static uint32_t bad_reply;
static uint32_t closed_handles;

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
    assert(service == 1u);
    assert(call->request_size == sizeof(*request));
    assert(call->reply_capacity == sizeof(*reply));
    astra_message_header_set(&reply->header, sizeof(*reply),
                             ASTRA_FONT_SERVICE_PROTOCOL,
                             ASTRA_FONT_SERVICE_VERSION,
                             ASTRA_FONT_SERVICE_REPLY,
                             request->header.transaction_id);
    reply->status = ASTRA_STATUS_OK;
    reply->generation = document_generation;
    reply->catalog_size = request->header.operation ==
        ASTRA_FONT_SERVICE_CATALOG ? document_size : 0u;
    memcpy(reply->selected_id, "system:test", sizeof("system:test"));
    call->reply_size = bad_reply == 1u ? sizeof(*reply) - 1u : sizeof(*reply);
    call->reply_handle_count = request->header.operation ==
        ASTRA_FONT_SERVICE_CATALOG ? 1u : 0u;
    if (call->reply_handle_count != 0u) *call->reply_handles = 2u;
    if (bad_reply == 2u) reply->header.protocol_version = 99u;
    return ASTRA_OK;
}

AstraResult astra_area_map(AstraArea *area, uint32_t flags)
{
    assert(area->handle == 2u);
    assert(flags == ASTRA_AREA_MAP_READ);
    area->address = document;
    area->size = sizeof(document);
    area->map_flags = flags;
    return ASTRA_OK;
}

AstraResult astra_area_close(AstraArea *area)
{
    assert(area->handle == 2u);
    area->handle = ASTRA_INVALID_HANDLE;
    area->address = NULL;
    area->size = 0u;
    area->map_flags = 0u;
    ++closed_handles;
    return ASTRA_OK;
}

AstraResult astra_handle_close(AstraHandle *handle)
{
    assert(*handle == 2u);
    *handle = ASTRA_INVALID_HANDLE;
    ++closed_handles;
    return ASTRA_OK;
}

int main(void)
{
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
    return 0;
}
