#include <astra/midi.h>

#include <astra/pcm_service.h>

#include <astra/host.h>
#include <astra/runtime.h>

#include <string.h>

#include "pcm_session.h"

/* Bytes one request carries through the session's transfer area. */
#define MIDI_CHUNK_BYTES ASTRA_PCM_TRANSFER_BYTES
/* How long astra_midi_presets() waits for a synth's fonts to load. */
#define PRESETS_WAIT_NS UINT64_C(30000000000)

/* The host's records travel big-endian, which is this machine's order:
 * they are read in place. The public types match them field for field. */
_Static_assert(sizeof(AstraMidiEvent) == ASTRA_HOST_MIDI_EVENT_BYTES,
               "MIDI event is the host's short message");
_Static_assert(sizeof(AstraMidiPreset) == sizeof(AstraHostMidiPreset) &&
                   ASTRA_MIDI_PRESET_NAME_MAX ==
                       ASTRA_HOST_MIDI_PRESET_NAME_MAX,
               "MIDI preset is the host's record");
_Static_assert(ASTRA_MIDI_FONT_NAME_MAX == ASTRA_HOST_FONT_NAME_MAX &&
                   ASTRA_MIDI_FONT_DEFAULT == ASTRA_HOST_AUDIO_FONT_DEFAULT,
               "MIDI font names and flags are the host's");
_Static_assert((uint32_t)ASTRA_MIDI_REVERB == ASTRA_HOST_MIDI_SET_REVERB &&
                   (uint32_t)ASTRA_MIDI_POSITION ==
                       ASTRA_HOST_MIDI_SET_POSITION &&
                   (uint32_t)ASTRA_MIDI_POLYPHONY ==
                       ASTRA_HOST_MIDI_SET_POLYPHONY,
               "MIDI settings are the host's");

AstraResult astra_midi_open(AstraHandle service, AstraMidiSynth *song)
{
    if (song == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    /* The session's format word only sizes its transfers: bytes. */
    return astra_pcm_session_open(service, ASTRA_PCM_MIDI_OPEN,
                                  ASTRA_PCM_FORMAT(ASTRA_PCM_ENCODING_U8,
                                                   1u, ASTRA_PCM_RATE),
                                  0u, ASTRA_PCM_TRANSFER_BYTES,
                                  &song->session);
}

/* Sends @p bytes of a song or SoundFont in area-sized pieces: each names
 * its offset and the whole size, so the service knows the last one. */
static AstraResult send_pieces(AstraMidiSynth *song, uint32_t operation,
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

AstraResult astra_midi_add_system_font(AstraMidiSynth *song, const char *name)
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

AstraResult astra_midi_add_font(AstraMidiSynth *song, const void *font,
                                uint32_t bytes)
{
    return send_pieces(song, ASTRA_PCM_MIDI_FONT, font, bytes);
}

AstraResult astra_midi_load(AstraMidiSynth *song, const void *file,
                            uint32_t bytes)
{
    return send_pieces(song, ASTRA_PCM_MIDI_LOAD, file, bytes);
}

AstraResult astra_midi_play(AstraMidiSynth *song, int32_t plays)
{
    AstraPcmReply reply = {0};

    if (song == NULL || plays == 0 || plays < ASTRA_MIDI_FOREVER)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return astra_pcm_session_exchange(
        &song->session, ASTRA_PCM_MIDI_PLAY, 0u,
        plays == ASTRA_MIDI_FOREVER ? ASTRA_PCM_MIDI_FOREVER :
                                      (uint32_t)plays, 0u, &reply);
}

AstraResult astra_midi_pause(AstraMidiSynth *song, int paused)
{
    AstraPcmReply reply = {0};

    if (song == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return astra_pcm_session_exchange(&song->session, ASTRA_PCM_PAUSE, 0u,
                                      paused != 0 ? 1u : 0u, 0u, &reply);
}

AstraResult astra_midi_stop(AstraMidiSynth *song)
{
    AstraPcmReply reply = {0};

    if (song == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return astra_pcm_session_exchange(&song->session, ASTRA_PCM_MIDI_STOP,
                                      0u, 0u, 0u, &reply);
}

AstraResult astra_midi_gain(AstraMidiSynth *song, uint32_t gain_q16)
{
    AstraPcmReply reply = {0};

    if (song == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return astra_pcm_session_exchange(&song->session, ASTRA_PCM_GAIN, 0u,
                                      gain_q16, 0u, &reply);
}

AstraResult astra_midi_active(AstraMidiSynth *song, int *active)
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

AstraResult astra_midi_close(AstraMidiSynth *song)
{
    if (song == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return astra_pcm_close(&song->session);
}

/* A request whose answer the service wrote into the area: frames_out
 * bytes of @p record_bytes records from @p first, and the total. */
static AstraResult list(AstraMidiSynth *song, uint32_t operation,
                        uint32_t first, uint32_t record_bytes,
                        AstraPcmReply *reply, uint32_t *total)
{
    AstraResult result;

    if (!astra_pcm_session_valid(&song->session))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_pcm_session_exchange(&song->session, operation, 0u, first,
                                        0u, reply);
    if (result != ASTRA_OK)
        return result;
    if (reply->frames_out > MIDI_CHUNK_BYTES ||
        reply->frames_out % record_bytes != 0u)
        return ASTRA_ERROR_IO;
    if (total != NULL)
        *total = reply->value;
    return ASTRA_OK;
}

AstraResult astra_midi_fonts(AstraMidiSynth *song, uint32_t first,
                             AstraMidiFont *fonts, uint32_t capacity,
                             uint32_t *count, uint32_t *total)
{
    AstraPcmReply reply = {0};
    AstraResult result;
    uint32_t copied = 0u;

    if (song == NULL || count == NULL || (capacity != 0u && fonts == NULL))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *count = 0u;
    result = list(song, ASTRA_PCM_MIDI_FONTS, first,
                  sizeof(AstraHostAudioFontRecord), &reply, total);
    if (result != ASTRA_OK)
        return result;
    for (uint32_t at = 0u; at < reply.frames_out /
                                    sizeof(AstraHostAudioFontRecord) &&
                           copied < capacity; ++at) {
        AstraHostAudioFontRecord record;

        (void)memcpy(&record, (const uint8_t *)song->session.mapped +
                                  at * sizeof(record), sizeof(record));
        record.name[sizeof(record.name) - 1u] = '\0';
        (void)memcpy(fonts[copied].name, record.name, sizeof(record.name));
        fonts[copied].bytes = (uint64_t)record.bytes_hi << 32 |
                              record.bytes_lo;
        fonts[copied].flags = record.flags;
        ++copied;
    }
    *count = copied;
    return ASTRA_OK;
}

AstraResult astra_midi_presets(AstraMidiSynth *song, uint32_t first,
                               AstraMidiPreset *presets, uint32_t capacity,
                               uint32_t *count, uint32_t *total)
{
    const uint64_t give_up = astra_clock_monotonic() + PRESETS_WAIT_NS;
    AstraPcmReply reply = {0};
    AstraResult result;
    uint32_t copied;

    if (song == NULL || count == NULL || (capacity != 0u && presets == NULL))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *count = 0u;
    /* A synth answers BUSY until it has loaded every font it was given. */
    while ((result = list(song, ASTRA_PCM_MIDI_PRESETS, first,
                          sizeof(AstraMidiPreset), &reply, total)) ==
               ASTRA_ERROR_BUSY &&
           astra_clock_monotonic() < give_up)
        (void)astra_rt_thread_sleep(UINT64_C(20000000),
                                    ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
    if (result != ASTRA_OK)
        return result;
    copied = reply.frames_out / sizeof(AstraMidiPreset);
    if (copied > capacity)
        copied = capacity;
    (void)memcpy(presets, song->session.mapped,
                 copied * sizeof(AstraMidiPreset));
    for (uint32_t at = 0u; at < copied; ++at)
        presets[at].name[ASTRA_MIDI_PRESET_NAME_MAX - 1u] = '\0';
    *count = copied;
    return ASTRA_OK;
}

AstraResult astra_midi_send(AstraMidiSynth *song, const AstraMidiEvent *events,
                            uint32_t count)
{
    const uint32_t per_request = MIDI_CHUNK_BYTES / sizeof(AstraMidiEvent);
    AstraPcmReply reply = {0};

    if (song == NULL || events == NULL || count == 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t sent = 0u; sent < count;) {
        uint32_t piece = count - sent < per_request ? count - sent :
                                                      per_request;
        AstraResult result;

        if (!astra_pcm_session_valid(&song->session))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        (void)memcpy(song->session.mapped, events + sent,
                     piece * sizeof(AstraMidiEvent));
        result = astra_pcm_session_exchange(
            &song->session, ASTRA_PCM_MIDI_EVENTS,
            piece * (uint32_t)sizeof(AstraMidiEvent), 0u, 0u, &reply);
        if (result != ASTRA_OK)
            return result;
        sent += piece;
    }
    return ASTRA_OK;
}

AstraResult astra_midi_set(AstraMidiSynth *song, AstraMidiSetting setting,
                           uint32_t value)
{
    AstraPcmReply reply = {0};

    if (song == NULL || (uint32_t)setting == 0u ||
        (uint32_t)setting > ASTRA_HOST_MIDI_SET_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return astra_pcm_session_exchange(&song->session, ASTRA_PCM_MIDI_SET, 0u,
                                      (uint32_t)setting, value, &reply);
}

AstraResult astra_midi_status(AstraMidiSynth *song, AstraMidiStatus *status)
{
    AstraPcmReply reply = {0};
    AstraHostMidiStatus record;
    AstraResult result;

    if (song == NULL || status == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = list(song, ASTRA_PCM_MIDI_STATUS, 0u, sizeof(record), &reply,
                  NULL);
    if (result != ASTRA_OK)
        return result;
    if (reply.frames_out != sizeof(record))
        return ASTRA_ERROR_IO;
    (void)memcpy(&record, song->session.mapped, sizeof(record));
    status->sounding = record.sounding != 0u;
    status->position_ticks = record.position_ticks;
    status->length_ticks = record.length_ticks;
    status->ticks_per_quarter = record.ticks_per_quarter;
    status->tempo_us_per_quarter = record.tempo_us_per_quarter;
    status->fonts_loading = record.fonts_loading;
    return ASTRA_OK;
}
