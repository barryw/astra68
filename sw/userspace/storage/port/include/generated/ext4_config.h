/*
 * Astra 68 lwext4 build profile.
 *
 * lwext4 includes this file as <generated/ext4_config.h> whenever
 * CONFIG_USE_DEFAULT_CFG is 0. It is the single place the vendored library is
 * configured, and the same profile is used for the host tests and for the
 * freestanding MC68030 build. Those two must not diverge: a host test that
 * exercises a different configuration than the one that ships is not a test of
 * what ships.
 */
#ifndef ASTRA_EXT4_CONFIG_H
#define ASTRA_EXT4_CONFIG_H

/*
 * lwext4 never derives endianness itself: CONFIG_BIG_ENDIAN is a
 * porter-supplied define and no upstream build sets it, which is why upstream
 * big-endian was never compiled. Derive it from the compiler so the same
 * profile is correct on an x86_64 host and on a big-endian MC68030.
 * ASTRA_FORCE_LE is the control build that separates a big-endian defect from
 * a 32-bit or m68k ABI defect.
 */
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__) && \
    !defined(ASTRA_FORCE_LE)
#define CONFIG_BIG_ENDIAN 1
#endif

/* MC68030 faults on misaligned word/long access. */
#define CONFIG_UNALIGNED_ACCESS 0

#define CONFIG_EXT_FEATURE_SET_LVL 4 /* F_SET_EXT4 */

#define CONFIG_JOURNALING_ENABLE 1

/*
 * ext4_extent.c and ext4_xattr.c are the only GPLv2-or-later files in lwext4
 * and third_party/lwext4 does not vendor them. These are therefore not a
 * tunable: switching either on will not link. See
 * third_party/lwext4/ASTRA_VENDOR.md for what the exclusion costs.
 */
#define CONFIG_EXTENTS_ENABLE 0
#define CONFIG_XATTR_ENABLE 0

#define CONFIG_BLOCK_DEV_CACHE_SIZE 1024

/*
 * Astra links -nostdlib and the MC68030 toolchain ships no C library at all,
 * so every libc-derived declaration must come from the port. Owning errno and
 * the open flags removes <errno.h>, <fcntl.h> and <unistd.h> from the
 * dependency set outright; what remains is covered by port/freestanding.
 */
#define CONFIG_HAVE_OWN_ERRNO 1
#define CONFIG_HAVE_OWN_OFLAGS 1

/*
 * CONFIG_HAVE_OWN_ASSERT selects lwext4's *own* assert, which prints and then
 * spins forever. A service that spins is a service that has stopped answering
 * with no record of why, so Astra takes the <assert.h> path instead and
 * supplies the header: on the host that is the real libc abort, on the target
 * it is a traced, tagged process exit. Assertions stay compiled in because
 * they fire on metadata read from a volume Astra did not create.
 */
#define CONFIG_HAVE_OWN_ASSERT 0
#define CONFIG_DEBUG_ASSERT 1

/* No stdout exists in a service; diagnosis goes through metrics and trace. */
#ifndef CONFIG_DEBUG_PRINTF
#define CONFIG_DEBUG_PRINTF 0
#endif

/*
 * Services must not depend on an unbounded heap. Allocation is bound to
 * sw/userspace/alloc by src/ext4_alloc.c.
 */
#define CONFIG_USE_USER_MALLOC 1

/*
 * lwext4 only #defines ext4_malloc to these names; it never declares them.
 * Declaring them here, in the profile every translation unit includes, keeps
 * callers from falling back to an implicit int return. ext4_user_realloc is
 * deliberately absent: no file in the vendored set calls ext4_realloc, and the
 * bounded allocator has no resize operation to bind it to.
 */
#include <stddef.h>
void *ext4_user_malloc(size_t size);
void *ext4_user_calloc(size_t count, size_t size);
void ext4_user_free(void *pointer);

/*
 * The machine knows what day it is, so the filesystem stamps what it writes.
 * ext4_user_now is bound by whoever stands the filesystem up; see
 * astra/ext4_time.h. Declared here for the same reason the allocator is:
 * lwext4 defines the macro and never the function.
 */
#define CONFIG_USE_USER_TIME 1
#include <stdint.h>
uint32_t ext4_user_now(void);

/*
 * lwext4's own errno set has no EBUSY, and the port needs one to report a
 * re-entrant block-device lock. The value matches every platform Astra builds
 * on, so the host and target builds agree.
 */
#ifndef EBUSY
#define EBUSY 16
#endif

#endif
