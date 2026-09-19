#ifndef ASTRA_RUNTIME_LOG_INTERNAL_H
#define ASTRA_RUNTIME_LOG_INTERNAL_H

#include <stdint.h>

uint32_t astra_log_assertion(const char *file, uint32_t line,
                             const char *expression);

#endif
