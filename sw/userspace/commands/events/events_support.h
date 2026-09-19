#ifndef ASTRA_EVENTS_COMMAND_SUPPORT_H
#define ASTRA_EVENTS_COMMAND_SUPPORT_H

#include <astra/filesystem_library.h>

#include <stddef.h>
#include <stdint.h>

int events_build_path(char *path, size_t capacity, int by_activity,
                      const char *activity, const char *subsystem,
                      const char *level, int previous_boot);
uint32_t events_tail_from(AstraFile *file, uint32_t lines, uint64_t *offset);

#endif
