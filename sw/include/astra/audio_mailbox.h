#ifndef ASTRA_AUDIO_MAILBOX_H
#define ASTRA_AUDIO_MAILBOX_H

#include <astra/audio_stream.h>
#include <astra/pcm_format.h>
#include <stdint.h>

/** @file audio_mailbox.h @brief QEMU's audio streams, shared with the host
 * audio daemon.
 *
 * Host-native, like the display mailbox: one shared file
 * (ASTRA_AUDIO_MAILBOX_PATH) that QEMU and astra-audio-host both map. It is
 * Haiku's mixer input (MixerInput.cpp:263-298) split across two processes:
 * when an application hands a stream buffer to the host, QEMU copies it out
 * of guest memory into that stream's ring here and gives the buffer back at
 * once; the daemon's feed thread, clocked by the output FIFO, decodes from
 * the ring just as much as the next mix needs.
 *
 * A ring holds at most ASTRA_AUDIO_MAILBOX_DEPTH buffers, so a stream is at
 * most that many buffers ahead of the mix here, plus the application's own.
 * When the daemon's reading makes room for another buffer it increments
 * `room_sequence` and futex-wakes it; QEMU then copies the next queued
 * buffers. One wake per buffer, in each direction, and no reply.
 *
 * Counters are free-running 32-bit byte counts. `written` is QEMU's alone,
 * `read` the daemon's alone. A slot is open while `generation` is nonzero;
 * QEMU clears it first on close and sets it last on open, with `start` and
 * `written` equal to the `read` it found (an empty ring). The daemon reads
 * a new generation from `start`: a late store of `read` it made for the
 * generation before cannot move the new one, and QEMU, which may briefly
 * see that stale `read`, treats a ring it cannot make sense of as full.
 */

/** Where astra-audio-host and QEMU meet unless ASTRA_AUDIO_MAILBOX_PATH
 * names another file. */
#define ASTRA_AUDIO_MAILBOX_DEFAULT_PATH "/run/astra/audio.mailbox"

#define ASTRA_AUDIO_MAILBOX_MAGIC UINT32_C(0x414d4258) /* AMBX */
#define ASTRA_AUDIO_MAILBOX_VERSION UINT32_C(0x00010000)
/** Slots: one per kernel stream slot. */
#define ASTRA_AUDIO_MAILBOX_STREAMS 32u
/** Buffers a ring holds at most. */
#define ASTRA_AUDIO_MAILBOX_DEPTH 2u
/** Bytes of one ring: two of the largest buffer. */
#define ASTRA_AUDIO_MAILBOX_RING_BYTES \
    (ASTRA_AUDIO_MAILBOX_DEPTH * ASTRA_AUDIO_STREAM_PERIOD_MAX * \
     ASTRA_PCM_MAX_FRAME_BYTES)
/** Byte offset of slot 0's ring; slot n's is n rings further. */
#define ASTRA_AUDIO_MAILBOX_RING_OFFSET 4096u
/** Bytes of the whole file. */
#define ASTRA_AUDIO_MAILBOX_BYTES \
    (ASTRA_AUDIO_MAILBOX_RING_OFFSET + \
     ASTRA_AUDIO_MAILBOX_STREAMS * ASTRA_AUDIO_MAILBOX_RING_BYTES)

typedef struct AstraAudioMailboxStream {
    uint32_t generation;   /* QEMU: nonzero while open */
    uint32_t format;       /* QEMU: ASTRA_PCM_FORMAT() word */
    uint32_t period_frames; /* QEMU */
    uint32_t buffer_bytes; /* QEMU: bytes of audio in each buffer */
    uint32_t written;      /* QEMU: bytes ever copied in */
    uint32_t read;         /* daemon: bytes ever taken out */
    uint32_t start;        /* QEMU: `written` when this generation opened */
    /* Daemon: nonzero ASTRA_SYSCALL_* when it cannot play this generation
     * (a format its resampler cannot take, no memory). QEMU puts it in the
     * stream header, so the application's wait fails instead of hanging. */
    uint32_t status;
    uint32_t reserved[8];
} AstraAudioMailboxStream;

typedef struct AstraAudioMailbox {
    uint32_t magic;
    uint32_t version;
    uint32_t stream_count;
    uint32_t ring_bytes;
    uint32_t ring_offset;
    uint32_t room_sequence; /* daemon: bumped and futex-woken on room */
    uint32_t reserved[10];
    AstraAudioMailboxStream streams[ASTRA_AUDIO_MAILBOX_STREAMS];
} AstraAudioMailbox;

_Static_assert(sizeof(AstraAudioMailboxStream) == 64u,
               "audio mailbox stream slot changed");
_Static_assert(sizeof(AstraAudioMailbox) <= ASTRA_AUDIO_MAILBOX_RING_OFFSET,
               "audio mailbox header overlaps the rings");

#endif
