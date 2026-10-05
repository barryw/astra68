#ifndef ASTRA_PCM_SERVICE_H
#define ASTRA_PCM_SERVICE_H

/* Private media-service wire format. Applications use <astra/pcm.h>. */

#include <astra/message_abi.h>
#include <astra/pcm_format.h>
#include <stdint.h>

#define ASTRA_PCM_PROTOCOL UINT32_C(0x50434d31) /* PCM1 */
#define ASTRA_PCM_PROTOCOL_VERSION UINT16_C(6)

enum {
    ASTRA_PCM_OPEN = 1u,
    ASTRA_PCM_WRITE,
    ASTRA_PCM_GAIN,
    ASTRA_PCM_STATUS,
    ASTRA_PCM_FINISH,
    ASTRA_PCM_CLOSE,
    ASTRA_PCM_REPLY,
    ASTRA_PCM_PAUSE,
    ASTRA_PCM_CLEAR,
    /* Factory: value is the source format, target the target format. The
     * transfer area is written both ways, so it is sent writable. */
    ASTRA_PCM_CONVERT_OPEN,
    /* frames source frames from the area (value ASTRA_PCM_CONVERT_END: no
     * more follow); the reply's frames_out target frames replace them in
     * the area, and queued_frames more are ready. */
    ASTRA_PCM_CONVERT,
    /* Factory: a MIDI voice with the default SoundFont set; the area is
     * sent writable, as for CONVERT_OPEN. */
    ASTRA_PCM_MIDI_OPEN,
    /* frames bytes of a shared font's file name in SOUND:soundfonts, in
     * the area. */
    ASTRA_PCM_MIDI_SYSTEM_FONT,
    /* frames bytes of a SoundFont or a Standard MIDI File at offset value
     * of target bytes; the piece that reaches target ends it. */
    ASTRA_PCM_MIDI_FONT,
    ASTRA_PCM_MIDI_LOAD,
    /* value plays, or ASTRA_PCM_MIDI_FOREVER. */
    ASTRA_PCM_MIDI_PLAY,
    ASTRA_PCM_MIDI_STOP,
    /* An AstraHostMidiStatus in the area (frames_out bytes); the reply's
     * value is nonzero while the voice sounds. */
    ASTRA_PCM_MIDI_STATUS,
    /* AstraHostAudioFontRecords from index value into the area
     * (frames_out bytes); the reply's value is the total. */
    ASTRA_PCM_MIDI_FONTS,
    /* AstraHostMidiPresets likewise; BUSY while a font is loading. */
    ASTRA_PCM_MIDI_PRESETS,
    /* frames bytes of short MIDI messages in the area. */
    ASTRA_PCM_MIDI_EVENTS,
    /* Setting value (ASTRA_HOST_MIDI_SET_*) becomes target. */
    ASTRA_PCM_MIDI_SET,
    /* Factory, with one handle: the reply port. The reply carries a host
     * device handle with ASTRA_RIGHT_AUDIO_STREAM alone, with which the
     * client opens audio streams on its own memory (audio_stream.h). The
     * service is not involved again. UNSUPPORTED when the host has none. */
    ASTRA_PCM_STREAM_GRANT
};
#define ASTRA_PCM_OPERATION_MAX ASTRA_PCM_STREAM_GRANT
#define ASTRA_PCM_CONVERT_END 1u
#define ASTRA_PCM_MIDI_FOREVER UINT32_C(0xffffffff)
#define ASTRA_PCM_FONT_NAME_MAX 128u

typedef struct AstraPcmRequest {
    AstraMessageHeader header;
    uint32_t frames;
    uint32_t value;
    uint32_t target;
} AstraPcmRequest;

typedef struct AstraPcmReply {
    AstraMessageHeader header;
    uint32_t status;
    uint32_t queued_frames;
    uint32_t hardware_frames;
    uint32_t underruns;
    uint32_t overflows;
    uint32_t software_gaps;
    uint32_t frames_out;
    uint32_t value;
} AstraPcmReply;

_Static_assert(sizeof(AstraPcmRequest) == 36u, "PCM request ABI changed");
_Static_assert(sizeof(AstraPcmReply) == 56u, "PCM reply ABI changed");

#endif
