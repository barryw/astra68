/** @file libc_library.c
 *  @brief Identity record for Astra's unified C and POSIX shared library.
 *
 * The public C surface is supplied by picolibc; Astra supplies the operating
 * system integration, allocator, threads, sockets, descriptors, and process
 * semantics. Both are linked into this one library so native and ported code
 * cannot accidentally select different implementations.
 */

#include <astra/library.h>

ASTRA_DYNAMIC_LIBRARY(
    "libc.library.1", 1, 0, 0, 1, 0,
    "Astra68/picolibc", "Copyright 2026 Astra68/picolibc");
