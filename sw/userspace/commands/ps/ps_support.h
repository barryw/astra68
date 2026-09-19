#ifndef ASTRA_COMMAND_PS_SUPPORT_H
#define ASTRA_COMMAND_PS_SUPPORT_H

#include <stdint.h>

#include <astra/proc.h>

uint32_t astra_ps_format_row(char *out, uint32_t capacity,
                             const AstraProcSnapshot *record);

#endif
