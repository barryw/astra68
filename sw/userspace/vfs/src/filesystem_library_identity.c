/** @file filesystem_library_identity.c
 *  @brief Dynamic-image identity for filesystem.library.
 */

#include <astra/filesystem_library.h>
#include <astra/library.h>

ASTRA_DYNAMIC_LIBRARY("filesystem.library.4", 4, 0, 0,
                      ASTRA_FILESYSTEM_LIBRARY_ABI_MAJOR,
                      ASTRA_FILESYSTEM_LIBRARY_ABI_MINOR,
                      "Barry Walker", "Copyright 2026 Barry Walker");
