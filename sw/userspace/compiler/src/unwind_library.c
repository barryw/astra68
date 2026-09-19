/** @file unwind_library.c
 *  @brief Identity record for Astra's shared GCC exception unwinder.
 *
 * The unwinder is shared independently from the low-level compiler builtins
 * because frame registration needs libc allocation, string, and thread APIs.
 * This preserves an acyclic dependency graph while keeping one process-wide
 * exception state for exceptions crossing shared-library boundaries.
 */

#include <astra/library.h>

ASTRA_DYNAMIC_LIBRARY(
    "unwind.library.1", 1, 0, 0, 1, 0,
    "Astra68/GCC", "Copyright 2026 Astra68/GCC");
