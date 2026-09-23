#if defined(__GNUC__)
#pragma GCC system_header
#endif

/*
 * GCC's limits.h enters syslimits.h, which can find this wrapper again.
 * Let that second entry pass through to the real C library limits.h.
 */
#ifdef ASTRA_POSIX_LIMITS_H
#include_next <limits.h>
#else
#define ASTRA_POSIX_LIMITS_H
#include_next <limits.h>
#include <stdint.h>

#if defined(__m68k__)
#include <astra/vfs_service.h>

/* The current VFS pathname ABI includes its terminating NUL in this bound. */
#ifdef PATH_MAX
#undef PATH_MAX
#endif
#define PATH_MAX ASTRA_VFS_PATH_MAX

#ifndef SSIZE_MAX
#define SSIZE_MAX (SIZE_MAX >> 1)
#endif
#endif

#endif
