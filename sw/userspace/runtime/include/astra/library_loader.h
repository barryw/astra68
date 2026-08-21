#ifndef ASTRA_USERSPACE_LIBRARY_LOADER_H
#define ASTRA_USERSPACE_LIBRARY_LOADER_H

#include <stdint.h>

#include <astra/library.h>

typedef struct AstraLoadedLibrary {
    const AstraLibrary *identity;
    const void *exports;
    uint32_t base;
    uint32_t span;
} AstraLoadedLibrary;

/* A hit returns OK; a miss returns WOULD_BLOCK without touching `library`. */
uint32_t astra_library_find(const char *name, uint16_t abi_major,
                            uint16_t minimum_abi_minor,
                            const AstraLoadedLibrary **library);

/* A miss returns WOULD_BLOCK; no filesystem bytes are needed on a hit. */
uint32_t astra_library_attach(const AstraLibraryReference *reference,
                              const AstraLoadedLibrary **library);
uint32_t astra_library_attach_cached(const char *name, uint16_t abi_major,
                                     uint16_t minimum_abi_minor,
                                     const AstraLoadedLibrary **library);

/* Maps, eagerly relocates, and caches one self-contained Astra library. */
uint32_t astra_library_load(const void *image, uint32_t length,
                            const char *expected_name, uint16_t abi_major,
                            uint16_t minimum_abi_minor,
                            const AstraLoadedLibrary **library);

#endif
