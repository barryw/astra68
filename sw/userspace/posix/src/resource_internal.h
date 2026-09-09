#ifndef ASTRA_POSIX_RESOURCE_INTERNAL_H
#define ASTRA_POSIX_RESOURCE_INTERNAL_H

#include <stdint.h>
#include <sys/resource.h>

#define ASTRA_POSIX_RESOURCE_COUNT 7u

typedef struct AstraPosixResourceState {
    uint64_t current[ASTRA_POSIX_RESOURCE_COUNT];
    uint64_t maximum[ASTRA_POSIX_RESOURCE_COUNT];
} AstraPosixResourceState;

void astra_posix_resource_reset(void);
void astra_posix_resource_export(AstraPosixResourceState *state);
int astra_posix_resource_validate(const AstraPosixResourceState *state);
void astra_posix_resource_import(const AstraPosixResourceState *state);

uint32_t astra_posix_resource_nofile(void);
int astra_posix_resource_heap_allows(uint64_t static_bytes,
                                     uint32_t used, uint32_t growth);
rlim_t astra_posix_resource_file_size(void);

#endif
