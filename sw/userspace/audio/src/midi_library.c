#include <astra/midi.h>

#include <astra/pcm_service.h>

#include <string.h>

#include "pcm_session.h"

/* Bytes one request carries through the session's transfer area. */
#define MIDI_CHUNK_BYTES (ASTRA_PCM_TRANSFER_FRAMES * ASTRA_PCM_MAX_FRAME_BYTES)

AstraResult astra_midi_open(AstraHandle service, AstraMidiSong *song)
{
    if (song == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    /* The session's format word only sizes its transfers: bytes. */
    return astra_pcm_session_open(service, ASTRA_PCM_MIDI_OPEN,
                                  ASTRA_PCM_FORMAT(ASTRA_PCM_ENCODING_U8,
                                                   1u, ASTRA_PCM_RATE),
                                  0u, &song->session);
}

/* Sends @p bytes of a song or SoundFont in area-sized pieces: each names
 * its offset and the whole size, so the service knows the last one. */
static AstraResult send_pieces(AstraMidiSong *song, uint32_t operation,
                               const void *data, uint32_t bytes)
{
    const uint8_t *from = data;
    AstraPcmReply reply = {0};

    if (song == NULL || data == NULL || bytes == 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t sent = 0u; sent < bytes;) {
        uint32_t piece = bytes - sent < MIDI_CHUNK_BYTES ?
                         bytes - sent : MIDI_CHUNK_BYTES;
        AstraResult result;

        if (!astra_pcm_session_valid(&song->session))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        (void)memcpy(song->session.mapped, from + sent, piece);
        result = astra_pcm_session_exchange(&song->session, operation, piece,
                                            sent, bytes, &reply);
        if (result != ASTRA_OK)
            return result;
        sent += piece;
    }
    return ASTRA_OK;
}

AstraResult astra_midi_add_system_font(AstraMidiSong *song, const char *name)
{
    AstraPcmReply reply = {0};
    size_t length;

    if (song == NULL || name == NULL ||
        !astra_pcm_session_valid(&song->session))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    length = strlen(name);
    if (length == 0u || length >= ASTRA_PCM_FONT_NAME_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    (void)memcpy(song->session.mapped, name, length);
    return astra_pcm_session_exchange(&song->session,
                                      ASTRA_PCM_MIDI_SYSTEM_FONT,
                                      (uint32_t)length, 0u, 0u, &reply);
}

AstraResult astra_midi_add_font(AstraMidiSong *song, const void *font,
                                uint32_t bytes)
{
    return send_pieces(song, ASTRA_PCM_MIDI_FONT, font, bytes);
}

AstraResult astra_midi_load(AstraMidiSong *song, const void *file,
                            uint32_t bytes)
{
    return send_pieces(song, ASTRA_PCM_MIDI_LOAD, file, bytes);
}

AstraResult astra_midi_play(AstraMidiSong *song, int32_t plays)
{
    AstraPcmReply reply = {0};

    if (song == NULL || plays == 0 || plays < ASTRA_MIDI_FOREVER)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return astra_pcm_session_exchange(
        &song->session, ASTRA_PCM_MIDI_PLAY, 0u,
        plays == ASTRA_MIDI_FOREVER ? ASTRA_PCM_MIDI_FOREVER :
                                      (uint32_t)plays, 0u, &reply);
}

AstraResult astra_midi_pause(AstraMidiSong *song, int paused)
{
    AstraPcmReply reply = {0};

    if (song == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return astra_pcm_session_exchange(&song->session, ASTRA_PCM_PAUSE, 0u,
                                      paused != 0 ? 1u : 0u, 0u, &reply);
}

AstraResult astra_midi_stop(AstraMidiSong *song)
{
    AstraPcmReply reply = {0};

    if (song == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return astra_pcm_session_exchange(&song->session, ASTRA_PCM_MIDI_STOP,
                                      0u, 0u, 0u, &reply);
}

AstraResult astra_midi_gain(AstraMidiSong *song, uint32_t gain_q16)
{
    AstraPcmReply reply = {0};

    if (song == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return astra_pcm_session_exchange(&song->session, ASTRA_PCM_GAIN, 0u,
                                      gain_q16, 0u, &reply);
}

AstraResult astra_midi_active(AstraMidiSong *song, int *active)
{
    AstraPcmReply reply = {0};
    AstraResult result;

    if (song == NULL || active == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_pcm_session_exchange(&song->session, ASTRA_PCM_MIDI_STATUS,
                                        0u, 0u, 0u, &reply);
    *active = result == ASTRA_OK && reply.value != 0u;
    return result;
}

AstraResult astra_midi_close(AstraMidiSong *song)
{
    if (song == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return astra_pcm_close(&song->session);
}
