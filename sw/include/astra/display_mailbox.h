#ifndef ASTRA_DISPLAY_MAILBOX_H
#define ASTRA_DISPLAY_MAILBOX_H

#include <stdint.h>

#define ASTRA_DISPLAY_MAILBOX_HEADER_BYTES 4096u
#define ASTRA_DISPLAY_MAILBOX_FRAME_BYTES \
    (1280u * 720u * 2u)
#define ASTRA_DISPLAY_MAILBOX_BYTES \
    (ASTRA_DISPLAY_MAILBOX_HEADER_BYTES + ASTRA_DISPLAY_MAILBOX_FRAME_BYTES)
#define ASTRA_DISPLAY_MAILBOX_MAGIC UINT32_C(0x41474658) /* AGFX */
#define ASTRA_DISPLAY_MAILBOX_VERSION_1_1 UINT32_C(0x00010001)
#define ASTRA_DISPLAY_MAILBOX_VERSION_1_2 UINT32_C(0x00010002)
#define ASTRA_DISPLAY_MAILBOX_VERSION_1_3 UINT32_C(0x00010003)
#define ASTRA_DISPLAY_MAILBOX_VERSION_1_4 UINT32_C(0x00010004)

/* Host-native shared record between QEMU and the Arty Linux display helper. */
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
    uint32_t reserved[4];
} AstraDisplayMailbox;

_Static_assert(sizeof(AstraDisplayMailbox) == 64u,
               "display mailbox header must remain one cache line");

#endif
