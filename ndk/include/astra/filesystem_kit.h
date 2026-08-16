#ifndef ASTRA_FILESYSTEM_KIT_H
#define ASTRA_FILESYSTEM_KIT_H

/**
 * @file filesystem_kit.h
 * @brief Public Filesystem Kit shared-library identity.
 */

#include <astra/shared_library.h>

/** Logical name resolved beneath `LIBS:` by OpenLibrary(). */
#define ASTRA_FILESYSTEM_LIBRARY_NAME "filesystem.library"
/** Minimum compatible Filesystem Kit ABI major requested by applications. */
#define ASTRA_FILESYSTEM_LIBRARY_VERSION 1u

#endif
