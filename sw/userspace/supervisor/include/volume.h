#ifndef ASTRA_SUPERVISOR_VOLUME_H
#define ASTRA_SUPERVISOR_VOLUME_H

#include <stdint.h>
#include <astra/process.h>

/* Bootstrap borrows a read-only system session from the canonical provider.
 * It must release that session and filesystem claim before HostFS starts. */
uint32_t supervisor_volume_open(const AstraStartupInfo *startup);
uint32_t supervisor_volume_read_alloc(const char *path, void **buffer, uint32_t *length);
uint32_t supervisor_volume_source_open(const char *path, uint32_t *length);
uint32_t supervisor_volume_source_read_at(void *context, uint32_t offset,
    uint32_t length, const uint8_t **bytes, uint32_t *moved);
uint32_t supervisor_volume_source_close(void *context);
uint32_t supervisor_volume_unmount(void);

#endif
