#ifndef ASTRA_SHARED_LIBRARY_H
#define ASTRA_SHARED_LIBRARY_H

/** @file shared_library.h @brief Versioned process shared libraries. */

#include <stdint.h>

#include <astra/attributes.h>

/** Open shared-library mapping and its immutable exported table. */
typedef struct AstraLibraryHandle {
    const void *exports; /**< Library-specific exported function table. */
    uint16_t version; /**< Semantic major version. */
    uint16_t revision; /**< Semantic minor version. */
    uint16_t abi_major; /**< Export-table ABI major version. */
    uint16_t abi_minor; /**< Export-table ABI minor version. */
    /** @cond ASTRA_INTERNAL */
    uint32_t _private_slot;
    /** @endcond */
} AstraLibraryHandle;

/** Open the newest compatible installed library at or above `version`.
 * @param name Logical library filename beneath `LIBS:`.
 * @param version Minimum compatible major version.
 * @return Open handle, or NULL when no compatible library is available.
 */
ASTRA_NODISCARD AstraLibraryHandle *OpenLibrary(const char *name,
                                                uint16_t version);

/** Release one OpenLibrary reference. The mapping remains cacheable.
 * @param library Open handle, or NULL.
 */
void CloseLibrary(AstraLibraryHandle *library);

/** Cleanup callback used by ::ASTRA_AUTO_LIBRARY.
 * @param library Address of an open handle variable.
 */
void astra_library_cleanup(AstraLibraryHandle **library);

/** Close an opened library automatically on every normal scope exit. */
#define ASTRA_AUTO_LIBRARY ASTRA_CLEANUP(astra_library_cleanup)

#endif
