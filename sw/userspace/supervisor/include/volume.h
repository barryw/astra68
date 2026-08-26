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

/* Reads one bootstrap file while the supervisor temporarily owns the mount. */
uint32_t supervisor_volume_read(const char *path, void *buffer,
                                uint32_t capacity, uint32_t *length);

/* One bootstrap file streamed through the runtime executable loader. */
uint32_t supervisor_volume_source_open(const char *path, uint32_t *length);
uint32_t supervisor_volume_source_read_at(
    void *context, uint32_t offset, uint32_t length,
    const uint8_t **bytes, uint32_t *moved);
uint32_t supervisor_volume_source_close(void *context);

/* Releases the temporary bootstrap mount before the storage service starts. */
uint32_t supervisor_volume_unmount(void);

/* Releases bootstrap DMA after the temporary mount is gone. */
void supervisor_bootstrap_block_release(void);

/* Drops the supervisor's block authority after storage has published. */
void supervisor_bootstrap_block_close(void);

/*
 * The last status the device gave this volume's block port, as an
 * AstraBlockStatus.
 *
 * lwext4 has one errno for a timeout, a cancellation, a short transfer and a
 * corrupt reply, so everything below it arrives at a caller as `I/O error` --
 * which names the layer that gave up rather than the thing that went wrong.
 * The port already keeps the distinction; this is how anything above it can
 * say which, and a refusal a person is shown should carry it.
 */
uint32_t supervisor_volume_device_status(void);

/*
 * Where the last refused transfer gave up, as (site * 256) + syscall status,
 * or zero if none has been. See AstraLeaseBlockSite. The device status above
 * says a transfer failed; this says which call in its life did.
 */
uint32_t supervisor_volume_device_failure(void);

#endif
