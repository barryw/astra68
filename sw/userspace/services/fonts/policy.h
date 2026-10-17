#ifndef ASTRA_FONTS_POLICY_H
#define ASTRA_FONTS_POLICY_H

#include <astra/font_catalog.h>
#include <astra/font_service.h>
#include <astra/font_store.h>

int fonts_face_selectable(const AstraFontCatalogFace *face);
int fonts_request_valid(const AstraFontServiceRequest *request,
                        uint32_t size, uint32_t admin);
int fonts_inventory_parse(const char *text, uint32_t length,
                          char tokens[][ASTRA_FONT_OBJECT_TOKEN_SIZE],
                          uint32_t capacity, uint32_t *count);

#endif
