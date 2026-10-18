#include "../policy.h"
#include <astra/font_encoding.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    AstraFontCatalogStrike strikes[] = {
        {0u, 11u, 5u, 0u},
        {0u, 13u, 5u, 0u},
        {0u, 16u, 5u, 0u}
    };
    AstraFontCatalogFace face = {
        .strikes = strikes,
        .strike_count = 3u,
        .flags = ASTRA_FONT_CATALOG_BASIC_TEXT
    };
    assert(fonts_face_selectable(&face));
    face.flags = 0u;
    assert(!fonts_face_selectable(&face));
    face.flags = ASTRA_FONT_CATALOG_BASIC_TEXT;
    strikes[1].bitmap_format = 4u;
    assert(!fonts_face_selectable(&face));
    strikes[1].bitmap_format = 5u;
    face.strike_count = 2u;
    assert(!fonts_face_selectable(&face));
    face.strike_count = 3u;

    AstraFontServiceRequest request = {0};
    astra_message_header_set(&request.header, sizeof(request),
                             ASTRA_FONT_SERVICE_PROTOCOL,
                             ASTRA_FONT_SERVICE_VERSION,
                             ASTRA_FONT_SERVICE_CATALOG, 1u);
    assert(fonts_request_valid(&request, sizeof(request), FONTS_PORT_CATALOG));
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_ADMIN));
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_RENDER));
    request.header.operation = ASTRA_FONT_SERVICE_SELECT;
    strcpy(request.face_id, "system:astra-workbench");
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_CATALOG));
    assert(fonts_request_valid(&request, sizeof(request), FONTS_PORT_ADMIN));
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_RENDER));
    request.source_encoding = ASTRA_FONT_ENCODING_ISO_8859_2;
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_ADMIN));
    request.source_encoding = ASTRA_FONT_ENCODING_ISO_8859_2 + 1u;
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_ADMIN));
    request.source_encoding = ASTRA_FONT_ENCODING_AUTO;
    request.header.operation = ASTRA_FONT_SERVICE_UNINSTALL;
    assert(fonts_request_valid(&request, sizeof(request), FONTS_PORT_ADMIN));
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_CATALOG));
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_RENDER));
    request.header.operation = ASTRA_FONT_SERVICE_SELECT;
    memset(request.face_id, 'x', sizeof(request.face_id));
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_ADMIN));
    request.face_id[0] = '\0';
    request.header.flags = 1u;
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_ADMIN));
    request.header.flags = 0u;
    request.header.operation = ASTRA_FONT_SERVICE_IMPORT;
    request.source_size = 3u;
    memcpy(request.source_name, "abc.afnt", sizeof("abc.afnt"));
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_CATALOG));
    assert(fonts_request_valid(&request, sizeof(request), FONTS_PORT_ADMIN));
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_RENDER));
    request.source_encoding = ASTRA_FONT_ENCODING_ISO_8859_2;
    assert(fonts_request_valid(&request, sizeof(request), FONTS_PORT_ADMIN));
    request.source_encoding = ASTRA_FONT_ENCODING_ISO_8859_2 + 1u;
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_ADMIN));
    request.source_encoding = ASTRA_FONT_ENCODING_AUTO;
    request.source_name[0] = '\0';
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_ADMIN));
    request.source_name[0] = (char)0xff;
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_ADMIN));
    request.source_name[0] = 'a';
    request.source_size = ASTRA_FONT_SOURCE_MAX + 1u;
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_ADMIN));
    request.header.operation = ASTRA_FONT_SERVICE_OPEN_FACE;
    request.source_size = 0u;
    memset(request.source_name, 0, sizeof(request.source_name));
    strcpy(request.face_id, "system:astra-workbench");
    assert(fonts_request_valid(&request, sizeof(request), FONTS_PORT_RENDER));
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_CATALOG));
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_ADMIN));
    request.face_id[0] = '\0';
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_RENDER));
    request.header.operation = ASTRA_FONT_SERVICE_INFORMATION;
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_CATALOG));
    strcpy(request.face_id, "system:astra-workbench");
    assert(fonts_request_valid(&request, sizeof(request), FONTS_PORT_CATALOG));
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_ADMIN));
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_RENDER));
    request.header.protocol_version = ASTRA_FONT_SERVICE_VERSION - 1u;
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_CATALOG));
    request.header.protocol_version = ASTRA_FONT_SERVICE_VERSION;
    request.source_size = 1u;
    assert(!fonts_request_valid(&request, sizeof(request), FONTS_PORT_CATALOG));
    request.source_size = 0u;

    char token[ASTRA_FONT_OBJECT_TOKEN_SIZE];
    char inventory[2u * ASTRA_FONT_OBJECT_TOKEN_LENGTH + 1u];
    char parsed[2][ASTRA_FONT_OBJECT_TOKEN_SIZE];
    uint32_t count = UINT32_MAX;
    astra_font_object_token("abc", 3u, token);
    assert(fonts_inventory_parse("", 0u, parsed, 2u, &count) && count == 0u);
    assert(fonts_inventory_parse(token, ASTRA_FONT_OBJECT_TOKEN_LENGTH,
                                 parsed, 2u, &count) && count == 1u &&
           strcmp(parsed[0], token) == 0);
    memcpy(inventory, token, ASTRA_FONT_OBJECT_TOKEN_LENGTH);
    inventory[ASTRA_FONT_OBJECT_TOKEN_LENGTH] = ' ';
    memcpy(inventory + ASTRA_FONT_OBJECT_TOKEN_LENGTH + 1u,
           token, ASTRA_FONT_OBJECT_TOKEN_LENGTH);
    assert(!fonts_inventory_parse(inventory, sizeof(inventory), parsed,
                                  2u, &count));
    inventory[ASTRA_FONT_OBJECT_TOKEN_LENGTH] = '\n';
    assert(!fonts_inventory_parse(inventory, sizeof(inventory), parsed,
                                  2u, &count));
    assert(!fonts_inventory_parse(token, ASTRA_FONT_OBJECT_TOKEN_LENGTH,
                                  parsed, 0u, &count));
    token[ASTRA_FONT_OBJECT_TOKEN_LENGTH - 1u] = 'b';
    assert(!fonts_inventory_parse(token, ASTRA_FONT_OBJECT_TOKEN_LENGTH,
                                  parsed, 2u, &count));

    FILE *built_in = fopen("../../graphics/fonts/astra-workbench.afnt", "rb");
    assert(built_in != NULL && fseek(built_in, 0, SEEK_END) == 0);
    long file_size = ftell(built_in);
    assert(file_size > 0 && (uint64_t)file_size <= UINT32_MAX &&
           fseek(built_in, 0, SEEK_SET) == 0);
    uint8_t *afnt = malloc((size_t)file_size);
    assert(afnt != NULL && fread(afnt, 1u, (size_t)file_size, built_in) ==
           (size_t)file_size && fclose(built_in) == 0);
    char family[256], style[128];
    AstraFontCatalogStrike built_in_strikes[32];
    AstraFontCatalogFace built_in_face;
    assert(astra_font_catalog_face_from_afnt(
        afnt, (uint32_t)file_size, "system:astra-workbench", 22u,
        ASTRA_FONT_CATALOG_SYSTEM, family, sizeof(family), style,
        sizeof(style), built_in_strikes, 32u, &built_in_face));
    assert(fonts_face_selectable(&built_in_face));
    free(afnt);
    puts("fonts service policy passed");
    return 0;
}
