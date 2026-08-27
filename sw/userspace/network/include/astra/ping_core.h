#ifndef ASTRA_PING_CORE_H
#define ASTRA_PING_CORE_H

#include <stdint.h>

uint16_t astra_ping_checksum(const void *bytes, uint32_t length);

#endif
