#ifndef ASTRA_SUPERVISOR_MESSAGING_H
#define ASTRA_SUPERVISOR_MESSAGING_H

#include <stdint.h>
#include <loader.h>

uint32_t supervisor_messaging_start(uint32_t *receive, uint32_t *send);
void supervisor_messaging_pump(uint32_t receive, const SupervisorProcessTable *processes);
void supervisor_messaging_remove(uint32_t process_id);
int supervisor_messaging_needs_retry(void);
void supervisor_messaging_retry(void);

#endif
