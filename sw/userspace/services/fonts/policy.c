#include "policy.h"

#include <astra/theme.h>

#include <string.h>

int fonts_face_selectable(const AstraFontCatalogFace *face)
{
    const uint16_t heights[] = {
        ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT,
        ASTRA_THEME_SYSTEM_TITLE_FONT_HEIGHT,
        ASTRA_THEME_SYSTEM_MENU_FONT_HEIGHT
    };
    if (face == NULL || face->strikes == NULL) return 0;
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
                        uint32_t size, uint32_t admin)
{
    return request != NULL && size == sizeof(*request) &&
           request->header.total_size == sizeof(*request) &&
           request->header.header_size == ASTRA_MESSAGE_HEADER_SIZE &&
           request->header.flags == 0u &&
           request->header.protocol == ASTRA_FONT_SERVICE_PROTOCOL &&
           request->header.protocol_version == ASTRA_FONT_SERVICE_VERSION &&
           request->header.reserved == 0u &&
           request->header.transaction_id != 0u &&
           request->reserved[0] == 0u && request->reserved[1] == 0u &&
           (request->header.operation == ASTRA_FONT_SERVICE_CATALOG ||
            (admin && request->header.operation == ASTRA_FONT_SERVICE_SELECT)) &&
           memchr(request->face_id, '\0', sizeof(request->face_id)) != NULL;
}
