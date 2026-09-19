/** @file streams_library.c
 *  @brief Identity record for Astra's capability-based streams library.
 *
 * The complete callable API and its ownership, back-pressure, and lifecycle
 * contracts are documented in `<astra/stream.h>`.  This record is the stable
 * identity used by the loader and package dependency metadata.
 */

#include <astra/library.h>

ASTRA_DYNAMIC_LIBRARY(
    "streams.library.1", 1, 0, 0, 1, 0,
    "Astra68 contributors", "Copyright 2026 Astra68 contributors");
