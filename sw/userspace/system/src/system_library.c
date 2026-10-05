/** @file system_library.c
 *  @brief Identity record for system.library, the foundational client library.
 *
 * The library owns the single public implementation of NDK clients and
 * bundle/manifest parsers used above the runtime syscall boundary. Higher
 * level Kits depend on this boundary instead of embedding private copies of
 * resource, IPC, window, pointer, clipboard, and metadata machinery.
 */

#include <astra/library.h>

ASTRA_DYNAMIC_LIBRARY(
    "system.library.3", 3, 0, 0, 3, 0,
    "Astra68 contributors", "Copyright 2026 Astra68 contributors");
