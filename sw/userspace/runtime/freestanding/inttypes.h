/*
 * Freestanding <inttypes.h> for Astra userspace.
 *
 * Only the format-string macros are provided, and only because vendored code
 * includes this header unconditionally while using them exclusively inside
 * debug-print paths that Astra compiles out. There is no imaxdiv, no strtoimax
 * and no wide-character support: nothing in Astra needs them, and declaring
 * functions the runtime does not implement would turn a compile error into a
 * link error.
 *
 * The widths match the MC68030 LP32 model, where long is 32 bits and long long
 * is 64.
 */
#ifndef ASTRA_FREESTANDING_INTTYPES_H
#define ASTRA_FREESTANDING_INTTYPES_H

#include <stdint.h>

#define PRId32 "ld"
#define PRIi32 "li"
#define PRIu32 "lu"
#define PRIo32 "lo"
#define PRIx32 "lx"
#define PRIX32 "lX"

#define PRId64 "lld"
#define PRIi64 "lli"
#define PRIu64 "llu"
#define PRIo64 "llo"
#define PRIx64 "llx"
#define PRIX64 "llX"

#endif
