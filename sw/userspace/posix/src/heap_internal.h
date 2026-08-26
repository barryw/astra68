#ifndef ASTRA_POSIX_HEAP_INTERNAL_H
#define ASTRA_POSIX_HEAP_INTERNAL_H

#include <stdint.h>

typedef struct AstraPosixHeapLayout {
    uint8_t *base;
    uint32_t page_count;
    uint32_t *page_run;
    uint32_t *extent_next;
    uint32_t *extent_pages;
} AstraPosixHeapLayout;

int astra_posix_heap_layout(AstraPosixHeapLayout *layout);

#endif
