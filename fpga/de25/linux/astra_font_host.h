#ifndef ASTRA_FONT_HOST_H
#define ASTRA_FONT_HOST_H

#include <stdint.h>

typedef struct AstraFontHostResult {
    uint8_t *afnt;
    uint32_t afnt_size;
    uint32_t faces;
    uint32_t status;
} AstraFontHostResult;

/* Run one disposable HPS worker. Zero means a complete versioned reply;
 * negative errno means launch, transport, timeout, or exit failure. Caller
 * frees afnt after a successful conversion. */
int astra_font_host_convert(const char *worker, const void *source,
    uint32_t source_size, uint32_t face_index, const char *name,
    uint32_t name_size, const char *license, uint32_t license_size,
    AstraFontHostResult *result);

#endif
