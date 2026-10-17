#ifndef ASTRA_FONTS_POLICY_H
#define ASTRA_FONTS_POLICY_H

#include <astra/font_catalog.h>
#include <astra/font_service.h>
#include <astra/font_store.h>

int fonts_face_selectable(const AstraFontCatalogFace *face);
enum {
    FONTS_PORT_CATALOG = 0u,
    FONTS_PORT_ADMIN = 1u,
    FONTS_PORT_RENDER = 2u
};
int fonts_request_valid(const AstraFontServiceRequest *request,
                        uint32_t size, uint32_t port_role);
int fonts_inventory_parse(const char *text, uint32_t length,
                          char tokens[][ASTRA_FONT_OBJECT_TOKEN_SIZE],
                          uint32_t capacity, uint32_t *count);

#endif
