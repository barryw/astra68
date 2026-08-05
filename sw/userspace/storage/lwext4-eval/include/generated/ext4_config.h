/*
 * Astra 68 lwext4 build profile.
 *
 * lwext4 never derives endianness itself: CONFIG_BIG_ENDIAN is a porter-supplied
 * define and no upstream build sets it. Derive it from the compiler so the same
 * profile is correct on an x86_64 host and on a big-endian MC68030.
 */
#ifndef ASTRA_EXT4_CONFIG_H
#define ASTRA_EXT4_CONFIG_H

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__) && \
    !defined(ASTRA_FORCE_LE)
#define CONFIG_BIG_ENDIAN 1
#endif

/* MC68030 faults on misaligned word/long access. */
#define CONFIG_UNALIGNED_ACCESS 0

#define CONFIG_EXT_FEATURE_SET_LVL 4 /* F_SET_EXT4 */

#define CONFIG_JOURNALING_ENABLE 1

/*
 * ext4_extent.c and ext4_xattr.c are the only GPLv2-or-later files in lwext4;
 * every other source file is BSD-2-clause. ASTRA_PROFILE_BSD builds without
 * them.
 */
#ifdef ASTRA_PROFILE_BSD
#define CONFIG_EXTENTS_ENABLE 0
#define CONFIG_XATTR_ENABLE 0
#else
#define CONFIG_EXTENTS_ENABLE 1
#define CONFIG_XATTR_ENABLE 1
#endif

#define CONFIG_BLOCK_DEV_CACHE_SIZE 16

#define CONFIG_HAVE_OWN_ERRNO 0
#define CONFIG_HAVE_OWN_OFLAGS 0
#define CONFIG_HAVE_OWN_ASSERT 0

#ifndef CONFIG_DEBUG_PRINTF
#define CONFIG_DEBUG_PRINTF 1
#endif
#ifndef CONFIG_DEBUG_ASSERT
#define CONFIG_DEBUG_ASSERT 1
#endif

#ifndef CONFIG_USE_USER_MALLOC
#define CONFIG_USE_USER_MALLOC 0
#endif

#if CONFIG_USE_USER_MALLOC
/*
 * lwext4 only #defines ext4_malloc to these names; it never declares them.
 * Declaring them here, in the force-included profile, keeps every translation
 * unit from falling back to an implicit int return.
 */
#include <stddef.h>
void *ext4_user_malloc(size_t size);
void *ext4_user_calloc(size_t count, size_t size);
void ext4_user_free(void *pointer);
#endif

#endif
