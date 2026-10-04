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
   It never occupies the request and is never completed. wake_sequence
   changes after every request and every cursor write, and is the one word
   the helper futex-waits on, so neither kind of change can be missed. */
#define ASTRA_DISPLAY_MAILBOX_VERSION_1_8 UINT32_C(0x00010008)

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
_Static_assert(ASTRA_RENDER_BATCH_MAX_BYTES <=
                   ASTRA_DISPLAY_MAILBOX_PAYLOAD_BYTES &&
               ASTRA_DISPLAY_MAILBOX_FRAME_BYTES <=
                   ASTRA_DISPLAY_MAILBOX_PAYLOAD_BYTES,
               "display mailbox payload cannot hold every request");

#endif
