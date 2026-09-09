#define _POSIX_C_SOURCE 200809L
#include <limits.h>

#if !defined(SSIZE_MAX) || SSIZE_MAX < 0x10000L
#error "SSIZE_MAX must be a usable preprocessor limit"
#endif

_Static_assert(SSIZE_MAX == 0x7fffffff,
               "Astra m68k ssize_t must retain its 32-bit maximum");
_Static_assert(sizeof(long long) == 8,
               "zsh arithmetic requires a 64-bit long long");
_Static_assert(sizeof(unsigned long long) == 8,
               "zsh arithmetic requires a 64-bit unsigned long long");
_Static_assert(MB_LEN_MAX >= 6,
               "POSIX terminal programs require UTF-8 multibyte support");
