#ifndef ASTRA_FONTS_HOST_H
#define ASTRA_FONTS_HOST_H

#include <astra/host.h>
#include <stdint.h>

typedef struct FontsHost {
    AstraHostChannelClient channel;
    uint32_t active;
} FontsHost;

uint32_t fonts_host_begin(FontsHost *host, uint32_t device,
    const void *source, uint32_t source_size, const char *name,
    uint32_t name_size);
uint32_t fonts_host_convert(FontsHost *host, uint32_t face,
    void **afnt, uint32_t *afnt_size, uint32_t *faces);
void fonts_host_close(FontsHost *host);

#endif
