#ifndef ASTRA_DATE_COMMAND_SUPPORT_H
#define ASTRA_DATE_COMMAND_SUPPORT_H

#include <astra/civil.h>

#include <stddef.h>
#include <stdint.h>
#include <time.h>

uint32_t date_format_alloc(const AstraCivilTime *civil,
                           const struct tm *rendered, const char *format,
                           char **output, size_t *length);

#endif
