#ifndef ASTRA_FONT_SERVICE_H
#define ASTRA_FONT_SERVICE_H

#include <stdint.h>
#include <astra/message_abi.h>
#include <astra/font_import.h>

#define ASTRA_FONT_SERVICE_PROTOCOL UINT32_C(0x464f4e54) /* FONT */
#define ASTRA_FONT_SERVICE_VERSION UINT16_C(8)
#define ASTRA_CAPABILITY_FONTS "FONTS"
#define ASTRA_CAPABILITY_FONTS_ADMIN "FONTS_ADMIN"
#define ASTRA_CAPABILITY_FONTS_RENDER "FONTS_RENDER"
/* Single-consumer auto-reset wake hint for Display; the catalog is authoritative. */
#define ASTRA_CAPABILITY_FONTS_CHANGED "FONTS_CHANGED"
#define ASTRA_FONT_FACE_ID_MAX 64u
#define ASTRA_FONT_SOURCE_NAME_MAX 128u
#define ASTRA_FONT_SOURCE_MAX (32u * 1024u * 1024u)

enum {
    ASTRA_FONT_SERVICE_CATALOG = 1u,
    ASTRA_FONT_SERVICE_SELECT = 2u,
    ASTRA_FONT_SERVICE_REPLY = 3u,
    ASTRA_FONT_SERVICE_IMPORT_BEGIN = 4u,
    ASTRA_FONT_SERVICE_OPEN_FACE = 5u,
    ASTRA_FONT_SERVICE_UNINSTALL = 6u,
    ASTRA_FONT_SERVICE_INFORMATION = 7u,
    ASTRA_FONT_SERVICE_IMPORT_STATE = 8u,
    ASTRA_FONT_SERVICE_IMPORT_CANCEL = 9u,
    ASTRA_FONT_SERVICE_IMPORT_RELEASE = 10u,
    ASTRA_FONT_SERVICE_IMPORT_CHANGED = 11u
};

typedef struct AstraFontServiceRequest {
    AstraMessageHeader header;
    char face_id[ASTRA_FONT_FACE_ID_MAX];
    uint32_t source_size;
    char source_name[ASTRA_FONT_SOURCE_NAME_MAX];
    uint32_t source_encoding;
    uint32_t reserved;
} AstraFontServiceRequest;

typedef struct AstraFontServiceReply {
    AstraMessageHeader header;
    uint32_t status;
    uint32_t generation;
    uint32_t area_size;
    char selected_id[ASTRA_FONT_FACE_ID_MAX];
    uint32_t reserved;
    char imported_id[ASTRA_FONT_FACE_ID_MAX];
    uint32_t import_state;
    uint32_t completed_faces;
    uint32_t total_faces;
    uint32_t import_status;
} AstraFontServiceReply;

_Static_assert(sizeof(AstraFontServiceRequest) == 228u,
               "font request ABI");
_Static_assert(sizeof(AstraFontServiceReply) == 184u,
               "font reply ABI");

#endif
