#ifndef ASTRA_AUDIO_HOST_H
#define ASTRA_AUDIO_HOST_H

/* Internal QEMU/audio-provider packet; not an application API. */

#include <stdint.h>

#include <astra/host.h>
#include <astra/pcm_format.h>

#define ASTRA_AUDIO_HOST_SOCKET "/run/astra/audio.sock"
#define ASTRA_AUDIO_HOST_LOCK "/run/astra/audio.lock"
#define ASTRA_AUDIO_HOST_MAGIC UINT32_C(0x41554431) /* AUD1 */
#define ASTRA_AUDIO_HOST_VERSION 3u
#define ASTRA_AUDIO_HOST_FRAME_BYTES ASTRA_PCM_MAX_FRAME_BYTES
#define ASTRA_AUDIO_HOST_PACKET_FRAMES 1024u
#define ASTRA_AUDIO_HOST_QUEUE_FRAMES ASTRA_PCM_QUEUE_FRAMES
#define ASTRA_AUDIO_HOST_MONITOR UINT32_C(0x80000001)
#define ASTRA_AUDIO_HOST_MONITOR_FRAMES 480u

typedef struct AstraAudioHostRequest {
    uint32_t magic;
    uint32_t version;
    uint32_t operation;
    uint32_t handle;
    uint32_t value;
    /* CONVERT_OPEN: the target format; CONVERT: the reply's capacity. */
    uint32_t value_hi;
    uint32_t data_length;
} AstraAudioHostRequest;

/* A reply carries data_length bytes after it in the same packet: a
 * CONVERT's converted frames, and nothing for every other operation. */

typedef struct AstraAudioHostReply {
    uint32_t magic;
    uint32_t status;
    uint32_t handle;
    uint32_t queued_frames;
    uint32_t hardware_frames;
    uint32_t underruns;
    uint32_t overflows;
    uint32_t software_gaps;
    uint32_t data_length;
} AstraAudioHostReply;

/* Host-local read-only tap of the final HDMI mix, signed 16-bit LE stereo. */
typedef struct AstraAudioHostMonitorPacket {
    uint32_t magic;
    uint32_t version;
    uint32_t frames;
    uint8_t pcm[ASTRA_AUDIO_HOST_MONITOR_FRAMES * 4u];
} AstraAudioHostMonitorPacket;

_Static_assert(sizeof(AstraAudioHostRequest) == 28u,
               "audio host request layout changed");
_Static_assert(sizeof(AstraAudioHostReply) == 36u,
               "audio host reply layout changed");
_Static_assert(sizeof(AstraAudioHostMonitorPacket) == 1932u,
               "audio monitor packet layout changed");

#endif
