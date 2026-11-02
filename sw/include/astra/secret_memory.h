#ifndef ASTRA_SECRET_MEMORY_H
#define ASTRA_SECRET_MEMORY_H

#include <stdint.h>

static inline void astra_secret_wipe(void *address, uint32_t length)
{
    volatile uint8_t *bytes = address;
    while (length-- != 0u) *bytes++ = 0u;
}

#endif
