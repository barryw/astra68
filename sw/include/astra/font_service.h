#ifndef ASTRA_FONT_SERVICE_H
#define ASTRA_FONT_SERVICE_H

#include <stdint.h>
#include <astra/message_abi.h>

#define ASTRA_FONT_SERVICE_PROTOCOL UINT32_C(0x464f4e54) /* FONT */
#define ASTRA_FONT_SERVICE_VERSION UINT16_C(1)
#define ASTRA_CAPABILITY_FONTS "FONTS"
#define ASTRA_CAPABILITY_FONTS_ADMIN "FONTS_ADMIN"
#define ASTRA_FONT_FACE_ID_MAX 64u

enum {
    ASTRA_FONT_SERVICE_CATALOG = 1u,
    ASTRA_FONT_SERVICE_SELECT = 2u,
    ASTRA_FONT_SERVICE_REPLY = 3u
};

typedef struct AstraFontServiceRequest {
    AstraMessageHeader header;
    char face_id[ASTRA_FONT_FACE_ID_MAX];
    uint32_t reserved[2];
} AstraFontServiceRequest;

typedef struct AstraFontServiceReply {
    AstraMessageHeader header;
    uint32_t status;
    uint32_t generation;
    uint32_t catalog_size;
    char selected_id[ASTRA_FONT_FACE_ID_MAX];
    uint32_t reserved;
} AstraFontServiceReply;

_Static_assert(sizeof(AstraFontServiceRequest) == 96u,
               "font request ABI");
_Static_assert(sizeof(AstraFontServiceReply) == 104u,
               "font reply ABI");

#endif
