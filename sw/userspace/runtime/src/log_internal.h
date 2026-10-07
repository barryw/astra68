#ifndef ASTRA_RUNTIME_LOG_INTERNAL_H
#define ASTRA_RUNTIME_LOG_INTERNAL_H

#include <stdint.h>

uint32_t astra_log_assertion(const char *file, uint32_t line,
                             const char *expression);
/* log.c: the log rings across fork (in the child) and exec (just before). */
void astra_log_after_fork_child(void);
void astra_log_before_exec(void);

#endif
