/** @file compiler_library.c
 *  @brief Identity record for Astra's shared MC68040 compiler runtime.
 *
 * GCC-generated arithmetic, conversion, exception, and unwinding helpers live
 * here once per process. Applications and higher-level libraries consume the
 * routines indirectly through compiler-generated calls; there is deliberately
 * no second hand-maintained function declaration surface.
 */

#include <astra/library.h>

ASTRA_DYNAMIC_LIBRARY(
    "compiler.library.1", 1, 0, 0, 1, 0,
    "Astra68/GCC", "Copyright 2026 Astra68/GCC");
