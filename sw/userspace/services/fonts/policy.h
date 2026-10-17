#ifndef ASTRA_FONTS_POLICY_H
#define ASTRA_FONTS_POLICY_H

#include <astra/font_catalog.h>
#include <astra/font_service.h>

int fonts_face_selectable(const AstraFontCatalogFace *face);
int fonts_request_valid(const AstraFontServiceRequest *request,
                        uint32_t size, uint32_t admin);

#endif
