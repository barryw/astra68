#ifndef ASTRA_DISPLAY_MAILBOX_H
#define ASTRA_DISPLAY_MAILBOX_H

#include <astra/display.h>
#include <astra/render_batch.h>
#include <stdint.h>

#define ASTRA_DISPLAY_MAILBOX_HEADER_BYTES 4096u
#define ASTRA_DISPLAY_MAILBOX_FRAME_BYTES \
    (ASTRA_DISPLAY_WIDTH * ASTRA_DISPLAY_HEIGHT * 2u)
/* The payload carries either one RGB565 frame or one complete render batch. */
#define ASTRA_DISPLAY_MAILBOX_PAYLOAD_BYTES \
    (ASTRA_RENDER_BATCH_MAX_BYTES > ASTRA_DISPLAY_MAILBOX_FRAME_BYTES ? \
         ASTRA_RENDER_BATCH_MAX_BYTES : ASTRA_DISPLAY_MAILBOX_FRAME_BYTES)
#define ASTRA_DISPLAY_MAILBOX_MAGIC UINT32_C(0x41474658) /* AGFX */
/* 1.7: the mailbox is two shared mappings. The header file
   (ASTRA_DISPLAY_MAILBOX_PATH, HEADER_BYTES) holds this record and its futex
   words, which need an ordinary shared file. The payload
   (ASTRA_DISPLAY_PAYLOAD_PATH, PAYLOAD_BYTES) is separate so that on the DE25
   it can be the host arena (/dev/astra-host-arena): physically contiguous,
   non-cacheable HPS memory that the render engine reads directly through its
   host aperture, so the helper copies nothing. The helper futex-wakes
   completion_sequence after publishing it (since 1.6). */
/* 1.8: the cursor is a posted latest-value slot beside the request, written
   from Vesta's DISPLAY_CURSOR register: cursor holds the newest
   ASTRA_DISPLAY_HOST_CURSOR_PACK word and cursor_sequence counts the writes.
   It never occupies the request and is never completed. The helper's
   request loop futex-waits on wake_sequence, changed after every request;
   its cursor loop, a thread of its own, on cursor_sequence. */
/* 1.9: the panel's real scanout timing (AstraDisplayScanout), published by
   the helper beside the record, so the emulator's vblank is the panel's --
   as a KMS vblank event is the CRTC's, not a timer's. Before it, QEMU's
   vblank was a free-running 60 Hz timer that drifted through the panel's
   60.08 Hz about five frames a minute, and anything paced by it (the
   cursor, SDL's vsync) hitched on the panel as often. */
#define ASTRA_DISPLAY_MAILBOX_VERSION_1_9 UINT32_C(0x00010009)

/* Host-native shared record between QEMU and the Linux display helper. */
typedef struct AstraDisplayMailbox {
    uint32_t magic;
    uint32_t version;
    uint32_t request_sequence;
    uint32_t request_id;
    uint32_t operation;
    uint32_t color_rgb565;
    uint32_t completion_sequence;
    uint32_t completion_id;
    uint32_t completion_status;
    uint32_t completion_generation;
    uint32_t frame_pitch;
    uint32_t frame_bytes;
    uint32_t wake_sequence;
    uint32_t cursor_sequence;
    uint32_t cursor;
    uint32_t reserved;
} AstraDisplayMailbox;

_Static_assert(sizeof(AstraDisplayMailbox) == 64u,
               "display mailbox header must remain one cache line");

/*
 * At ASTRA_DISPLAY_SCANOUT_OFFSET in the header mapping, written only by
 * the helper. vblank_ns is a CLOCK_MONOTONIC instant at which a vertical
 * blank began and period_ns the measured frame period, so every later
 * vblank is vblank_ns + k * period_ns. sequence is odd while it is written;
 * zero means no panel has been measured.
 */
#define ASTRA_DISPLAY_SCANOUT_OFFSET 64u
typedef struct AstraDisplayScanout {
    uint32_t sequence;
    uint32_t reserved;
    uint64_t vblank_ns;
    uint64_t period_ns;
} AstraDisplayScanout;

_Static_assert(ASTRA_DISPLAY_SCANOUT_OFFSET >= sizeof(AstraDisplayMailbox) &&
                   ASTRA_DISPLAY_SCANOUT_OFFSET + sizeof(AstraDisplayScanout) <=
                       ASTRA_DISPLAY_MAILBOX_HEADER_BYTES,
               "display scanout record must sit after the mailbox");
_Static_assert(ASTRA_RENDER_BATCH_MAX_BYTES <=
                   ASTRA_DISPLAY_MAILBOX_PAYLOAD_BYTES &&
               ASTRA_DISPLAY_MAILBOX_FRAME_BYTES <=
                   ASTRA_DISPLAY_MAILBOX_PAYLOAD_BYTES,
               "display mailbox payload cannot hold every request");

#endif
