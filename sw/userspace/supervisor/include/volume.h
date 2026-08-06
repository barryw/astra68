#ifndef ASTRA_SUPERVISOR_VOLUME_H
#define ASTRA_SUPERVISOR_VOLUME_H

#include <stdint.h>

#include <astra/block_device.h>

/*
 * Mounts Astra's volume from the leased device, writes and re-reads a file, and
 * then unmounts unless `keep_mounted` asks it not to. Returns 0 when the volume
 * verified or when the card carries no Astra partition at all, and
 * ASTRA_SUPERVISOR_FAIL_VOLUME when a volume was present and did not behave.
 * How far it reached is reported through the progress counter, because the
 * status halfword has only one bit left.
 *
 * The device must already be initialised and its geometry queried, and its
 * lease must still be held: this runs inside the same attachment as the sector
 * round-trip rather than reattaching, because transfer memory is hard-capped
 * and claiming it twice is exactly what that cap exists to prevent.
 *
 * With `keep_mounted` the device and its lease must outlive this call, and
 * supervisor_volume_is_mounted() is how the caller knows when they may go.
 */
uint32_t supervisor_verify_volume(AstraBlockDevice *block,
                                  int keep_mounted);

/*
 * Whether a volume is mounted right now.
 *
 * The caller owns the device and the lease behind it, and a mounted volume
 * holds a pointer to that device for as long as it stays mounted. This is the
 * one place that answers whether releasing either is still legal, so the
 * lifetime rule has a single source of truth rather than the caller
 * re-deriving `keep_mounted && it worked` and getting it wrong.
 */
int supervisor_volume_is_mounted(void);

#endif
