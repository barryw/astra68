#ifndef ASTRA_FONT_CONVERT_H
#define ASTRA_FONT_CONVERT_H

#include <stdint.h>

enum {
    ASTRA_FONT_CONVERT_OK = 0,
    ASTRA_FONT_CONVERT_INVALID,
    ASTRA_FONT_CONVERT_UNSUPPORTED,
    ASTRA_FONT_CONVERT_LIMIT,
    ASTRA_FONT_CONVERT_NOMEM
};

/* Internal HPS worker operation. Caller owns returned AFNT buffer. */
int astra_font_convert_outline(const void *source, uint32_t source_length,
    uint32_t face_index, const char *source_name, const char *license,
    uint8_t **output, uint32_t *output_length, uint32_t *face_count);

#endif
