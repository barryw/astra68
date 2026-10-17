#include "policy.h"

#include <astra/theme.h>
#include <astra/utf8.h>
#include <astra/font_encoding.h>

#include <string.h>

int fonts_face_selectable(const AstraFontCatalogFace *face)
{
    const uint16_t heights[] = {
        ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT,
        ASTRA_THEME_SYSTEM_TITLE_FONT_HEIGHT,
        ASTRA_THEME_SYSTEM_MENU_FONT_HEIGHT
    };
    if (face == NULL || face->strikes == NULL ||
        (face->flags & ASTRA_FONT_CATALOG_BASIC_TEXT) == 0u) return 0;
    for (uint32_t role = 0u; role < 3u; ++role) {
        uint32_t found = 0u;
        for (uint32_t at = 0u; at < face->strike_count; ++at)
            if (face->strikes[at].pixel_height == heights[role] &&
                (face->strikes[at].bitmap_format == 1u ||
                 face->strikes[at].bitmap_format == 5u)) {
                found = 1u;
                break;
            }
        if (found == 0u) return 0;
    }
    return 1;
}

int fonts_request_valid(const AstraFontServiceRequest *request,
                        uint32_t size, uint32_t port_role)
{
    uint32_t import;
    const char *name_end;

    if (request == NULL || size != sizeof(*request)) return 0;
    import = request->header.operation == ASTRA_FONT_SERVICE_IMPORT;
    name_end = memchr(request->source_name, '\0',
                      sizeof(request->source_name));
    return
           request->header.total_size == sizeof(*request) &&
           request->header.header_size == ASTRA_MESSAGE_HEADER_SIZE &&
           request->header.flags == 0u &&
           request->header.protocol == ASTRA_FONT_SERVICE_PROTOCOL &&
           request->header.protocol_version == ASTRA_FONT_SERVICE_VERSION &&
           request->header.reserved == 0u &&
           request->header.transaction_id != 0u &&
           request->reserved == 0u &&
           request->source_encoding <= ASTRA_FONT_ENCODING_ISO_8859_2 &&
           (import || request->source_encoding == ASTRA_FONT_ENCODING_AUTO) &&
           ((port_role == FONTS_PORT_CATALOG &&
             request->header.operation == ASTRA_FONT_SERVICE_CATALOG) ||
            (port_role == FONTS_PORT_ADMIN &&
             (request->header.operation == ASTRA_FONT_SERVICE_SELECT ||
              request->header.operation == ASTRA_FONT_SERVICE_UNINSTALL ||
              import)) ||
            (port_role == FONTS_PORT_RENDER &&
             request->header.operation == ASTRA_FONT_SERVICE_OPEN_FACE)) &&
           memchr(request->face_id, '\0', sizeof(request->face_id)) != NULL &&
           (request->header.operation != ASTRA_FONT_SERVICE_OPEN_FACE ||
            request->face_id[0] != '\0') &&
           (import ?
                request->face_id[0] == '\0' &&
                request->source_size > 0u &&
                request->source_size <= ASTRA_FONT_SOURCE_MAX &&
                name_end != NULL && name_end != request->source_name &&
                astra_utf8_validate(request->source_name,
                    (uint32_t)(name_end - request->source_name), 0u) :
                request->source_size == 0u &&
                request->source_name[0] == '\0');
}

int fonts_inventory_parse(const char *text, uint32_t length,
                          char tokens[][ASTRA_FONT_OBJECT_TOKEN_SIZE],
                          uint32_t capacity, uint32_t *count)
{
    uint32_t used = 0u;

    if (count == NULL || (length != 0u && text == NULL) ||
        (capacity != 0u && tokens == NULL))
        return 0;
    *count = 0u;
    if (length == 0u) return 1;
    for (uint32_t at = 0u; at < length;) {
        if (used == capacity || length - at < ASTRA_FONT_OBJECT_TOKEN_LENGTH ||
            !astra_font_object_token_valid(text + at,
                                           ASTRA_FONT_OBJECT_TOKEN_LENGTH))
            return 0;
        for (uint32_t previous = 0u; previous < used; ++previous)
            if (memcmp(tokens[previous], text + at,
                       ASTRA_FONT_OBJECT_TOKEN_LENGTH) == 0)
                return 0;
        memcpy(tokens[used], text + at, ASTRA_FONT_OBJECT_TOKEN_LENGTH);
        tokens[used][ASTRA_FONT_OBJECT_TOKEN_LENGTH] = '\0';
        ++used;
        at += ASTRA_FONT_OBJECT_TOKEN_LENGTH;
        if (at == length) break;
        if (text[at++] != ' ' || at == length) return 0;
    }
    *count = used;
    return 1;
}
