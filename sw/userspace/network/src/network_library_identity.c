/** @file network_library_identity.c
 *  @brief Dynamic-image identity for network.library.
 */

#include <astra/library.h>
#include <astra/network_library.h>

ASTRA_DYNAMIC_LIBRARY("network.library.1", 1, 0, 0,
                      ASTRA_NETWORK_LIBRARY_ABI_MAJOR,
                      ASTRA_NETWORK_LIBRARY_ABI_MINOR,
                      "Barry Walker", "Copyright 2026 Barry Walker");
