#ifndef ASTRA_SHARED_LIBRARY_H
#define ASTRA_SHARED_LIBRARY_H

/** @file shared_library.h @brief Versioned process shared libraries. */

#include <stdint.h>

#include <astra/attributes.h>

typedef struct AstraLibraryHandle {
    const void *exports;
    uint16_t version;
    uint16_t revision;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t _private_slot;
} AstraLibraryHandle;

/** Open the newest compatible installed library at or above `version`. */
ASTRA_NODISCARD AstraLibraryHandle *OpenLibrary(const char *name,
                                                uint16_t version);

/** Release one OpenLibrary reference. The mapping remains cacheable. */
void CloseLibrary(AstraLibraryHandle *library);

/** Cleanup callback used by ::ASTRA_AUTO_LIBRARY. */
void astra_library_cleanup(AstraLibraryHandle **library);

/** Close an opened library automatically on every normal scope exit. */
#define ASTRA_AUTO_LIBRARY ASTRA_CLEANUP(astra_library_cleanup)

#endif
