#ifndef ASTRA_PCM_SESSION_H
#define ASTRA_PCM_SESSION_H

/* pcm.library's private transport to the media service, shared by its PCM
 * streams, converters and MIDI songs. Not exported. */

#include <astra/pcm.h>
#include <astra/pcm_service.h>

int astra_pcm_session_valid(const AstraPcmStream *stream);
/* A voice's area holds one ASTRA_PCM_TRANSFER_FRAMES batch. */
#define ASTRA_PCM_VOICE_AREA_BYTES \
    (ASTRA_PCM_TRANSFER_FRAMES * ASTRA_PCM_MAX_FRAME_BYTES)

/* Opens a session whose transfer area is @p area_bytes, at most
 * ASTRA_PCM_TRANSFER_BYTES: the most one exchange moves either way. */
AstraResult astra_pcm_session_open(AstraHandle service, uint32_t operation,
                                   uint32_t format, uint32_t target,
                                   uint32_t area_bytes,
                                   AstraPcmStream *stream);
AstraResult astra_pcm_session_exchange(AstraPcmStream *stream,
                                       uint32_t operation, uint32_t frames,
                                       uint32_t value, uint32_t target,
                                       AstraPcmReply *reply);

#endif
