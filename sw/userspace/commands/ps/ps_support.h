#ifndef ASTRA_COMMAND_PS_SUPPORT_H
#define ASTRA_COMMAND_PS_SUPPORT_H

#include <stddef.h>
#include <stdint.h>

#include <astra/proc.h>

uint32_t astra_ps_format_row(char *out, uint32_t capacity,
                             const AstraProcSnapshot *record);

typedef void *(*AstraPsReallocate)(void *pointer, size_t size);

uint32_t astra_ps_snapshot_allocate(uint64_t bytes,
                                    AstraPsReallocate reallocate,
                                    AstraProcSnapshot **records);

#endif
