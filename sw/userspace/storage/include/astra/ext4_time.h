#ifndef ASTRA_EXT4_TIME_H
#define ASTRA_EXT4_TIME_H

#include <stdint.h>

/*
 * Where lwext4's timestamps come from.
 *
 * lwext4 calls ext4_user_now() with no context argument, the same shape its
 * allocator hooks have, so the clock is bound once by whoever stands the
 * filesystem up: the storage service binds the machine's wall clock, and the
 * host mount test binds the host's.
 *
 * Nothing bound means zero, which is exactly what upstream wrote into every
 * timestamp before this existed. A machine that does not know the date says so
 * by leaving the field empty rather than by claiming 1970.
 */
typedef uint32_t (*AstraExt4Clock)(void);

void astra_ext4_clock_bind(AstraExt4Clock clock);

/*
 * The machine's own wall clock, ready to bind. Target only -- it calls the
 * runtime, so a host test binds the host's clock instead.
 */
uint32_t astra_ext4_clock_machine(void);

/* Seconds since the Unix epoch, or zero. Called by lwext4, not by callers. */
uint32_t ext4_user_now(void);

#endif
