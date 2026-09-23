#ifndef ASTRA_USERSPACE_STREAM_SOURCE_H
#define ASTRA_USERSPACE_STREAM_SOURCE_H

#include <stdint.h>

#include <astra/runtime.h>

uint32_t astra_stream_read_exact(AstraReadAt read_at, void *context,
                                 uint32_t image_size, uint32_t offset,
                                 uint32_t length, const uint8_t **bytes,
                                 AstraProcessLoadProfile *profile);
uint32_t astra_stream_read_up_to(AstraReadAt read_at, void *context,
                                 uint32_t image_size, uint32_t offset,
                                 uint32_t length, const uint8_t **bytes,
                                 uint32_t *moved,
                                 AstraProcessLoadProfile *profile);
uint32_t astra_stream_feed(uint32_t load_handle, uint32_t image_size,
                           AstraReadAt read_at, void *context,
                           uint32_t write_syscall, uint32_t offset,
                           uint32_t length,
                           AstraProcessLoadProfile *profile);

#endif
