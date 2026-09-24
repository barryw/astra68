/** @file runtime_library.c
 *  @brief Identity record for Astra's foundational runtime shared library.
 *
 * The callable API is documented at its public declarations in the NDK.  This
 * record supplies the versioned install and dependency identity consumed by
 * the supervisor, loader, diagnostic tools, and future package manager.
 */

#include <astra/library.h>

ASTRA_DYNAMIC_LIBRARY(
    "runtime.library.1", 1, 8, 0, 1, 8,
    "Astra68 contributors", "Copyright 2026 Astra68 contributors");
