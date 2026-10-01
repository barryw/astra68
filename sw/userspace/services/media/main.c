#include <astra/audio_host.h>
#include <astra/host.h>
#include <astra/pcm_service.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/area.h>
#include <astra/vfs_process.h>

#include <stdint.h>
#include <string.h>

#include "pcm_protocol.h"

ASTRA_PROGRAM("media", 0, 1, 0, "Astra68 contributors", "Astra native");

#define HEALTH_INTERVAL_NS UINT64_C(1000000000)

typedef struct PcmSession {
    struct PcmSession *next;
    uint32_t receive;
    uint32_t reply;
    uint32_t area;
    void *samples;
    uint32_t host_voice;
    uint32_t frame_bytes;
    /* A converter's target frame width; zero for a voice. */
    uint32_t target_frame_bytes;
    /* A MIDI voice, and the SoundFont it is being sent, if any. */
    int midi;
    uint32_t upload;
} PcmSession;

/* Astra's SoundFonts, as fetch_soundfonts.py indexed them. */
#define FONT_DIRECTORY "/system/media/soundfonts"
#define FONTS_MAX ASTRA_HOST_MIDI_FONTS_MAX
#define DIGEST_BYTES ASTRA_HOST_AUDIO_DIGEST_BYTES

typedef struct SystemFont {
    uint8_t digest[DIGEST_BYTES];
    uint32_t size;
    char file[ASTRA_PCM_FONT_NAME_MAX];
    char name[ASTRA_PCM_FONT_NAME_MAX];
    int default_font;
    int held;
} SystemFont;

static AstraProcessFilesystem filesystem = ASTRA_PROCESS_FILESYSTEM_INIT;
static int filesystem_open;
static SystemFont fonts[FONTS_MAX];
static uint32_t font_count;
static int fonts_indexed;

static AstraHostChannelClient host;
static PcmSession *sessions;
static uint32_t session_count;
static int host_failed;

static uint32_t host_exchange(uint16_t operation, uint32_t voice,
                              uint32_t value, uint32_t value_hi,
                              uint32_t length, uint32_t capacity,
                              AstraHostCommand **result)
{
    AstraHostCommand *command = astra_host_client_prepare(
        &host, ASTRA_HOST_SERVICE_AUDIO, operation);

    if (command == NULL) {
        host_failed = 1;
        return ASTRA_STATUS_PEER_DEAD;
    }
    command->handle = voice;
    command->value_lo = value;
    command->value_hi = value_hi;
    command->data_length = length;
    command->data_capacity = capacity;
    if (astra_host_client_submit(&host) != ASTRA_SYSCALL_OK) {
        host_failed = 1;
        return ASTRA_STATUS_PEER_DEAD;
    }
    if (command->status == ASTRA_STATUS_PEER_DEAD ||
        (operation != ASTRA_HOST_AUDIO_OPEN &&
         operation != ASTRA_HOST_AUDIO_CONVERT_OPEN &&
         command->status == ASTRA_STATUS_BAD_HANDLE)) {
        host_failed = 1;
        return ASTRA_STATUS_PEER_DEAD;
    }
    if (result != NULL)
        *result = command;
    return command->status;
}

static uint32_t host_command(uint16_t operation, uint32_t voice,
                             uint32_t value, uint32_t length,
                             AstraHostCommand **result)
{
    return host_exchange(operation, voice, value, 0u, length,
                         operation == ASTRA_HOST_AUDIO_STATUS ?
                         sizeof(AstraHostAudioStatus) : length, result);
}

static void session_release(PcmSession *session)
{
    PcmSession **link = &sessions;

    while (*link != NULL && *link != session)
        link = &(*link)->next;
    if (*link == session) {
        *link = session->next;
        --session_count;
    }
    if (session->upload != 0u)
        (void)host_command(ASTRA_HOST_AUDIO_CLOSE, session->upload, 0u, 0u,
                           NULL);
    if (session->host_voice != 0u)
        (void)host_command(ASTRA_HOST_AUDIO_CLOSE, session->host_voice,
                           0u, 0u, NULL);
    if (session->samples != NULL)
        (void)astra_rt_area_unmap(session->samples);
    if (session->area != 0u)
        (void)astra_close(session->area);
    if (session->reply != 0u)
        (void)astra_close(session->reply);
    if (session->receive != 0u)
        (void)astra_close(session->receive);
    astra_runtime_deallocate(session);
}

static uint32_t send_reply(uint32_t reply_handle,
                           const AstraPcmRequest *request,
                           uint32_t status, AstraHostCommand *host_result,
                           uint32_t frames_out, uint32_t transfer)
{
    AstraPcmReply reply = {0};
    const uint32_t *handles = transfer == 0u ? NULL : &transfer;

    astra_message_header_set(&reply.header, sizeof(reply),
                             ASTRA_PCM_PROTOCOL, ASTRA_PCM_PROTOCOL_VERSION,
                             ASTRA_PCM_REPLY, request->header.transaction_id);
    reply.status = status;
    if (status == ASTRA_STATUS_OK && host_result != NULL &&
        request->header.operation == ASTRA_PCM_STATUS &&
        host_result->result_length == sizeof(AstraHostAudioStatus)) {
        const volatile AstraHostAudioStatus *state =
            (const volatile AstraHostAudioStatus *)(const void *)host.data;

        reply.queued_frames = state->queued_frames;
        reply.hardware_frames = state->hardware_frames;
        reply.underruns = state->underruns;
        reply.overflows = state->overflows;
        reply.software_gaps = state->software_gaps;
    }
    if (status == ASTRA_STATUS_OK && host_result != NULL &&
        request->header.operation == ASTRA_PCM_CONVERT) {
        reply.frames_out = frames_out;
        reply.queued_frames = host_result->result_value;
    }
    if (status == ASTRA_STATUS_OK && host_result != NULL &&
        request->header.operation == ASTRA_PCM_MIDI_STATUS)
        reply.value = host_result->result_value;
    return astra_port_send(reply_handle, &reply, sizeof(reply), handles,
                           transfer == 0u ? 0u : 1u);
}

static int hex_value(char digit)
{
    if (digit >= '0' && digit <= '9')
        return digit - '0';
    if (digit >= 'a' && digit <= 'f')
        return digit - 'a' + 10;
    return -1;
}

/* Copies one space-separated word of at most capacity - 1 bytes. */
static const char *word(const char *at, const char *end, char *out,
                        uint32_t capacity)
{
    uint32_t length = 0u;

    while (at < end && *at == ' ')
        ++at;
    while (at < end && *at != ' ' && *at != '\n') {
        if (length + 1u >= capacity)
            return NULL;
        out[length++] = *at++;
    }
    out[length] = '\0';
    return length != 0u ? at : NULL;
}

/* "DIGEST SIZE FILE NAME ROLE" per line; any malformed line ends it. */
static uint32_t index_fonts(void)
{
    char *text = NULL;
    uint32_t length = 0u;
    const char *at, *end;

    if (fonts_indexed)
        return font_count != 0u ? ASTRA_STATUS_OK : ASTRA_STATUS_NOT_FOUND;
    fonts_indexed = 1;
    if (!filesystem_open ||
        astra_process_read_file_alloc(&filesystem, FONT_DIRECTORY "/index",
                                      (void **)&text, &length) !=
            ASTRA_VFS_OK)
        return ASTRA_STATUS_NOT_FOUND;
    at = text;
    end = text + length;
    while (at < end && font_count < FONTS_MAX) {
        SystemFont *font = &fonts[font_count];
        char field[2u * DIGEST_BYTES + 1u];
        uint32_t size = 0u;

        at = word(at, end, field, sizeof(field));
        if (at == NULL || field[2u * DIGEST_BYTES - 1u] == '\0')
            break;
        for (uint32_t i = 0u; i < DIGEST_BYTES; ++i) {
            int high = hex_value(field[2u * i]);
            int low = hex_value(field[2u * i + 1u]);

            if (high < 0 || low < 0)
                goto done;
            font->digest[i] = (uint8_t)(high << 4 | low);
        }
        at = word(at, end, field, sizeof(field));
        if (at == NULL)
            break;
        for (const char *digit = field; *digit != '\0'; ++digit) {
            if (*digit < '0' || *digit > '9' || size > UINT32_MAX / 10u)
                goto done;
            size = size * 10u + (uint32_t)(*digit - '0');
        }
        font->size = size;
        if ((at = word(at, end, font->file, sizeof(font->file))) == NULL ||
            (at = word(at, end, font->name, sizeof(font->name))) == NULL ||
            (at = word(at, end, field, sizeof(field))) == NULL)
            break;
        font->default_font = strcmp(field, "default") == 0;
        ++font_count;
        while (at < end && *at++ != '\n')
            ;
    }
done:
    astra_runtime_deallocate(text);
    return font_count != 0u ? ASTRA_STATUS_OK : ASTRA_STATUS_NOT_FOUND;
}

/* Makes sure the host holds @p font, sending it from the system volume the
 * first time the host has never seen it. */
static uint32_t hold_font(SystemFont *font)
{
    const uint32_t chunk = ASTRA_PCM_TRANSFER_FRAMES *
                           ASTRA_PCM_MAX_FRAME_BYTES;
    AstraHostCommand *result = NULL;
    AstraFile file = ASTRA_FILE_INIT;
    char path[sizeof(FONT_DIRECTORY) + ASTRA_PCM_FONT_NAME_MAX + 1u];
    uint32_t status, upload;

    if (font->held)
        return ASTRA_STATUS_OK;
    (void)memcpy((void *)(uintptr_t)host.data, font->digest, DIGEST_BYTES);
    status = host_exchange(ASTRA_HOST_AUDIO_FONT_QUERY, 0u, 0u, 0u,
                           DIGEST_BYTES, DIGEST_BYTES, NULL);
    if (status != ASTRA_STATUS_NOT_FOUND) {
        font->held = status == ASTRA_STATUS_OK;
        return status;
    }
    (void)memcpy(path, FONT_DIRECTORY "/", sizeof(FONT_DIRECTORY));
    (void)memcpy(path + sizeof(FONT_DIRECTORY), font->file,
                 strlen(font->file) + 1u);
    if (astra_filesystem_open(&filesystem.filesystem, path,
                              ASTRA_VFS_OPEN_READ, &file) != ASTRA_VFS_OK)
        return ASTRA_STATUS_NOT_FOUND;
    (void)memcpy((void *)(uintptr_t)host.data, font->digest, DIGEST_BYTES);
    status = host_exchange(ASTRA_HOST_AUDIO_FONT_BEGIN, 0u, font->size, 0u,
                           DIGEST_BYTES, DIGEST_BYTES, &result);
    upload = status == ASTRA_STATUS_OK ? result->result_value : 0u;
    for (uint32_t sent = 0u; status == ASTRA_STATUS_OK && sent < font->size;) {
        uint32_t moved = 0u;
        uint32_t want = font->size - sent < chunk ? font->size - sent : chunk;

        if (astra_filesystem_read(&file, (void *)(uintptr_t)host.data, want,
                                  &moved) != ASTRA_VFS_OK || moved == 0u) {
            status = ASTRA_STATUS_IO;
            break;
        }
        status = host_exchange(ASTRA_HOST_AUDIO_FONT_DATA, upload, sent, 0u,
                               moved, moved, NULL);
        sent += moved;
    }
    if (status == ASTRA_STATUS_OK)
        status = host_exchange(ASTRA_HOST_AUDIO_FONT_END, upload, 0u, 0u, 0u,
                               DIGEST_BYTES, NULL);
    else if (upload != 0u)
        (void)host_command(ASTRA_HOST_AUDIO_CLOSE, upload, 0u, 0u, NULL);
    (void)astra_filesystem_close(&file);
    font->held = status == ASTRA_STATUS_OK;
    return status;
}

/* A MIDI voice with every default font, lowest first. */
static uint32_t open_midi(uint32_t *voice)
{
    AstraHostCommand *result = NULL;
    uint8_t digests[FONTS_MAX * DIGEST_BYTES];
    uint32_t count = 0u;
    uint32_t status = index_fonts();

    for (uint32_t i = 0u; status == ASTRA_STATUS_OK && i < font_count; ++i) {
        if (!fonts[i].default_font)
            continue;
        status = hold_font(&fonts[i]);
        (void)memcpy(digests + count++ * DIGEST_BYTES, fonts[i].digest,
                     DIGEST_BYTES);
    }
    if (status == ASTRA_STATUS_OK && count == 0u)
        status = ASTRA_STATUS_NOT_FOUND;
    if (status != ASTRA_STATUS_OK)
        return status;
    (void)memcpy((void *)(uintptr_t)host.data, digests, count * DIGEST_BYTES);
    status = host_exchange(ASTRA_HOST_AUDIO_MIDI_OPEN, 0u, 0u, 0u,
                           count * DIGEST_BYTES, count * DIGEST_BYTES,
                           &result);
    if (status == ASTRA_STATUS_OK)
        *voice = result->result_value;
    return status;
}

static void serve_open(uint32_t factory)
{
    AstraPcmRequest request = {0};
    uint32_t handles[2] = {0};
    uint32_t size = 0u, count = 0u;
    uint32_t status = astra_port_receive(factory, &request, sizeof(request),
                                          handles, 2u, &size, &count);
    uint32_t mapped_size = 0u;
    uint32_t send = 0u;
    AstraHostCommand *result = NULL;
    PcmSession *session = NULL;

    if (status != ASTRA_SYSCALL_OK)
        return;
    if (count != 2u)
        goto done;
    status = astra_pcm_request_valid(&request, size, 1) &&
             request.frames == 0u &&
             astra_pcm_format_frame_bytes(request.value) != 0u &&
             (request.header.operation != ASTRA_PCM_CONVERT_OPEN ||
              astra_pcm_format_frame_bytes(request.target) != 0u) ?
             ASTRA_STATUS_OK : ASTRA_STATUS_PROTOCOL;
    /* ponytail: one wait set holds the factory and active voices; shard
     * sessions across workers if the handle namespace grows beyond it. */
    if (status == ASTRA_STATUS_OK &&
        session_count == ASTRA_WAIT_MULTIPLE_MAX - 1u)
        status = ASTRA_STATUS_LIMIT;
    if (status == ASTRA_STATUS_OK) {
        session = astra_runtime_callocate(1u, sizeof(*session));
        if (session == NULL)
            status = ASTRA_STATUS_NO_SPACE;
    }
    if (status == ASTRA_STATUS_OK) {
        session->frame_bytes = astra_pcm_format_frame_bytes(request.value);
        if (request.header.operation == ASTRA_PCM_CONVERT_OPEN)
            session->target_frame_bytes =
                astra_pcm_format_frame_bytes(request.target);
        session->midi = request.header.operation == ASTRA_PCM_MIDI_OPEN;
        session->area = handles[0];
        handles[0] = 0u;
        session->reply = handles[1];
        handles[1] = 0u;
        status = astra_rt_area_map(session->area,
                                   ASTRA_AREA_MAP_READ |
                                   (session->target_frame_bytes != 0u ||
                                    session->midi ?
                                    ASTRA_AREA_MAP_WRITE : 0u),
                                   &session->samples, &mapped_size);
        if (status != ASTRA_SYSCALL_OK ||
            mapped_size < ASTRA_PCM_TRANSFER_FRAMES * ASTRA_PCM_MAX_FRAME_BYTES)
            status = ASTRA_STATUS_BAD_HANDLE;
        else
            status = ASTRA_STATUS_OK;
    }
    if (status == ASTRA_STATUS_OK) {
        status = session->midi ? open_midi(&session->host_voice) :
                 session->target_frame_bytes != 0u ?
                 host_exchange(ASTRA_HOST_AUDIO_CONVERT_OPEN, 0u,
                               request.value, request.target, 0u, 0u,
                               &result) :
                 host_command(ASTRA_HOST_AUDIO_OPEN, 0u, request.value,
                              0u, &result);
        if (status == ASTRA_STATUS_OK && !session->midi)
            session->host_voice = result->result_value;
        if (status == ASTRA_STATUS_OK && session->host_voice == 0u)
            status = ASTRA_STATUS_IO;
    }
    if (status == ASTRA_STATUS_OK) {
        status = astra_rt_port_create(1u, sizeof(AstraPcmRequest),
                                      &session->receive, &send);
        if (status != ASTRA_SYSCALL_OK)
            status = ASTRA_STATUS_NO_SPACE;
        else
            status = ASTRA_STATUS_OK;
    }
    if (session != NULL && session->reply != 0u) {
        if (send_reply(session->reply, &request, status, NULL, 0u,
                       status == ASTRA_STATUS_OK ? send : 0u) !=
            ASTRA_SYSCALL_OK)
            status = ASTRA_STATUS_PEER_DEAD;
    } else if (handles[1] != 0u) {
        (void)send_reply(handles[1], &request, status, NULL, 0u, 0u);
    }
    if (status == ASTRA_STATUS_OK) {
        session->next = sessions;
        sessions = session;
        ++session_count;
        session = NULL;
        send = 0u;
    }
done:
    if (send != 0u)
        (void)astra_close(send);
    if (session != NULL)
        session_release(session);
    for (uint32_t index = 0u; index < count; ++index)
        if (handles[index] != 0u)
            (void)astra_close(handles[index]);
}

/* A converter takes CONVERT, a MIDI voice its MIDI operations, PAUSE and
 * GAIN, and a PCM voice the rest; every session takes CLOSE. */
static int session_takes(const PcmSession *session, uint32_t operation)
{
    int midi_operation = operation >= ASTRA_PCM_MIDI_SYSTEM_FONT &&
                         operation <= ASTRA_PCM_MIDI_STATUS;

    if (operation == ASTRA_PCM_CLOSE)
        return 1;
    if (session->target_frame_bytes != 0u)
        return operation == ASTRA_PCM_CONVERT;
    if (session->midi)
        return midi_operation || operation == ASTRA_PCM_PAUSE ||
               operation == ASTRA_PCM_GAIN;
    return !midi_operation && operation != ASTRA_PCM_CONVERT;
}

/* One piece of a SoundFont the client sends: the first opens an upload,
 * the last keeps the font and stacks it on the voice. */
static uint32_t font_piece(PcmSession *session, const AstraPcmRequest *request)
{
    AstraHostCommand *result = NULL;
    uint8_t digest[DIGEST_BYTES];
    uint32_t status;

    if (request->value == 0u) {
        if (session->upload != 0u)
            (void)host_command(ASTRA_HOST_AUDIO_CLOSE, session->upload, 0u,
                               0u, NULL);
        session->upload = 0u;
        status = host_exchange(ASTRA_HOST_AUDIO_FONT_BEGIN, 0u,
                               request->target, 0u, 0u, 0u, &result);
        if (status != ASTRA_STATUS_OK)
            return status;
        session->upload = result->result_value;
    }
    if (session->upload == 0u)
        return ASTRA_STATUS_INVALID;
    (void)memcpy((void *)(uintptr_t)host.data, session->samples,
                 request->frames);
    status = host_exchange(ASTRA_HOST_AUDIO_FONT_DATA, session->upload,
                           request->value, 0u, request->frames,
                           request->frames, NULL);
    if (status != ASTRA_STATUS_OK ||
        request->value + request->frames != request->target)
        return status;
    status = host_exchange(ASTRA_HOST_AUDIO_FONT_END, session->upload, 0u,
                           0u, 0u, DIGEST_BYTES, &result);
    session->upload = 0u;
    if (status != ASTRA_STATUS_OK)
        return status;
    if (result->result_length != DIGEST_BYTES)
        return ASTRA_STATUS_PROTOCOL;
    (void)memcpy(digest, (const void *)(uintptr_t)host.data, DIGEST_BYTES);
    (void)memcpy((void *)(uintptr_t)host.data, digest, DIGEST_BYTES);
    return host_exchange(ASTRA_HOST_AUDIO_MIDI_FONT, session->host_voice, 0u,
                         0u, DIGEST_BYTES, DIGEST_BYTES, NULL);
}

static uint32_t system_font(PcmSession *session, const AstraPcmRequest *request)
{
    char name[ASTRA_PCM_FONT_NAME_MAX];
    uint32_t status;

    if (request->frames == 0u || request->frames >= sizeof(name))
        return ASTRA_STATUS_INVALID;
    (void)memcpy(name, session->samples, request->frames);
    name[request->frames] = '\0';
    status = index_fonts();
    for (uint32_t i = 0u; status == ASTRA_STATUS_OK && i < font_count; ++i) {
        if (strcmp(fonts[i].name, name) != 0)
            continue;
        status = hold_font(&fonts[i]);
        if (status != ASTRA_STATUS_OK)
            return status;
        (void)memcpy((void *)(uintptr_t)host.data, fonts[i].digest,
                     DIGEST_BYTES);
        return host_exchange(ASTRA_HOST_AUDIO_MIDI_FONT, session->host_voice,
                             0u, 0u, DIGEST_BYTES, DIGEST_BYTES, NULL);
    }
    return ASTRA_STATUS_NOT_FOUND;
}

static void serve_session(PcmSession *session)
{
    AstraPcmRequest request = {0};
    AstraHostCommand *result = NULL;
    uint32_t size = 0u, count = 0u, transfer = 0u, frames_out = 0u;
    uint32_t operation;
    uint32_t status = astra_port_receive(session->receive, &request,
                                          sizeof(request), &transfer, 1u,
                                          &size, &count);

    if (status != ASTRA_SYSCALL_OK)
        return;
    status = astra_pcm_request_valid(&request, size, 0) && count == 0u ?
             ASTRA_STATUS_OK : ASTRA_STATUS_PROTOCOL;
    operation = request.header.operation;
    if (status == ASTRA_STATUS_OK && !session_takes(session, operation))
        status = ASTRA_STATUS_INVALID;
    if (status == ASTRA_STATUS_OK) {
        switch (operation) {
        case ASTRA_PCM_MIDI_SYSTEM_FONT:
            status = system_font(session, &request);
            break;
        case ASTRA_PCM_MIDI_FONT:
        case ASTRA_PCM_MIDI_LOAD:
            if (request.frames == 0u ||
                request.frames > ASTRA_PCM_TRANSFER_FRAMES *
                                     ASTRA_PCM_MAX_FRAME_BYTES ||
                request.value > request.target ||
                request.frames > request.target - request.value) {
                status = ASTRA_STATUS_INVALID;
            } else if (operation == ASTRA_PCM_MIDI_FONT) {
                status = font_piece(session, &request);
            } else {
                (void)memcpy((void *)(uintptr_t)host.data, session->samples,
                             request.frames);
                status = host_exchange(ASTRA_HOST_AUDIO_MIDI_LOAD,
                                       session->host_voice, request.value,
                                       request.target, request.frames,
                                       request.frames, NULL);
            }
            break;
        case ASTRA_PCM_MIDI_PLAY:
            status = request.frames == 0u ?
                     host_command(ASTRA_HOST_AUDIO_MIDI_PLAY,
                                  session->host_voice, request.value, 0u,
                                  NULL) : ASTRA_STATUS_INVALID;
            break;
        case ASTRA_PCM_MIDI_STOP:
        case ASTRA_PCM_MIDI_STATUS:
            status = request.frames == 0u && request.value == 0u ?
                     host_command(operation == ASTRA_PCM_MIDI_STOP ?
                                  ASTRA_HOST_AUDIO_MIDI_STOP :
                                  ASTRA_HOST_AUDIO_MIDI_STATUS,
                                  session->host_voice, 0u, 0u, &result) :
                     ASTRA_STATUS_INVALID;
            break;
        case ASTRA_PCM_CONVERT: {
            const uint32_t area_bytes = ASTRA_PCM_TRANSFER_FRAMES *
                                        ASTRA_PCM_MAX_FRAME_BYTES;

            if (request.value > ASTRA_PCM_CONVERT_END ||
                request.frames > area_bytes / session->frame_bytes) {
                status = ASTRA_STATUS_INVALID;
                break;
            }
            (void)memcpy((void *)(uintptr_t)host.data, session->samples,
                         request.frames * session->frame_bytes);
            status = host_exchange(
                ASTRA_HOST_AUDIO_CONVERT, session->host_voice,
                request.value == ASTRA_PCM_CONVERT_END ?
                    ASTRA_HOST_AUDIO_CONVERT_END : 0u,
                0u, request.frames * session->frame_bytes,
                area_bytes - area_bytes % session->target_frame_bytes,
                &result);
            if (status != ASTRA_STATUS_OK)
                break;
            if (result->result_length > area_bytes ||
                result->result_length % session->target_frame_bytes != 0u) {
                status = ASTRA_STATUS_PROTOCOL;
                break;
            }
            (void)memcpy(session->samples, (const void *)(uintptr_t)host.data,
                         result->result_length);
            frames_out = result->result_length /
                         session->target_frame_bytes;
            break;
        }
        case ASTRA_PCM_WRITE:
            if (request.value != 0u || request.frames == 0u ||
                request.frames > ASTRA_PCM_TRANSFER_FRAMES) {
                status = ASTRA_STATUS_INVALID;
                break;
            }
            (void)memcpy((void *)(uintptr_t)host.data, session->samples,
                         request.frames * session->frame_bytes);
            status = host_command(ASTRA_HOST_AUDIO_WRITE, session->host_voice,
                                  0u, request.frames * session->frame_bytes,
                                  NULL);
            break;
        case ASTRA_PCM_GAIN:
            status = request.frames == 0u ?
                     host_command(ASTRA_HOST_AUDIO_GAIN, session->host_voice,
                                  request.value, 0u, NULL) :
                     ASTRA_STATUS_INVALID;
            break;
        case ASTRA_PCM_PAUSE:
            status = request.frames == 0u && request.value <= 1u ?
                     host_command(ASTRA_HOST_AUDIO_PAUSE,
                                  session->host_voice, request.value, 0u,
                                  NULL) : ASTRA_STATUS_INVALID;
            break;
        case ASTRA_PCM_CLEAR:
            status = request.frames == 0u && request.value == 0u ?
                     host_command(ASTRA_HOST_AUDIO_CLEAR,
                                  session->host_voice, 0u, 0u, NULL) :
                     ASTRA_STATUS_INVALID;
            break;
        case ASTRA_PCM_STATUS:
            status = request.frames == 0u && request.value == 0u ?
                     host_command(ASTRA_HOST_AUDIO_STATUS,
                                  session->host_voice, 0u, 0u, &result) :
                     ASTRA_STATUS_INVALID;
            if (status == ASTRA_STATUS_OK &&
                result->result_length != sizeof(AstraHostAudioStatus))
                status = ASTRA_STATUS_PROTOCOL;
            break;
        case ASTRA_PCM_FINISH:
            status = request.frames == 0u && request.value == 0u ?
                     host_command(ASTRA_HOST_AUDIO_FINISH,
                                  session->host_voice, 0u, 0u, NULL) :
                     ASTRA_STATUS_INVALID;
            break;
        case ASTRA_PCM_CLOSE:
            if (request.frames != 0u || request.value != 0u) {
                status = ASTRA_STATUS_INVALID;
                break;
            }
            status = host_command(ASTRA_HOST_AUDIO_CLOSE,
                                  session->host_voice, 0u, 0u, NULL);
            if (status == ASTRA_STATUS_OK)
                session->host_voice = 0u;
            break;
        default:
            status = ASTRA_STATUS_PROTOCOL;
            break;
        }
    }
    (void)send_reply(session->reply, &request, status, result, frames_out,
                     0u);
    if (transfer != 0u)
        (void)astra_close(transfer);
    if (request.header.operation == ASTRA_PCM_CLOSE &&
        status == ASTRA_STATUS_OK)
        session_release(session);
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *bootstrap;
    const AstraStartupCapability *device;
    uint32_t factory = 0u, publication = 0u;
    uint32_t status;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    device = astra_startup_capability(startup, ASTRA_CAPABILITY_HOST_DEVICE);
    if (bootstrap == NULL || device == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    /* The system volume holds the SoundFonts; without it MIDI voices are
     * refused and PCM works as before. */
    filesystem_open = astra_process_filesystem_open(&filesystem, startup) ==
                      ASTRA_VFS_OK;
    status = astra_host_client_open(device->handle, ASTRA_HOST_CAP_AUDIO,
                                    ASTRA_PCM_TRANSFER_FRAMES *
                                    ASTRA_PCM_MAX_FRAME_BYTES, &host);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_rt_port_create(1u, sizeof(AstraPcmRequest),
                                      &factory, &publication);
    if (status == ASTRA_SYSCALL_OK)
        status = host_command(ASTRA_HOST_AUDIO_STATUS, 0u, 0u, 0u, NULL) ==
                 ASTRA_STATUS_OK ? ASTRA_SYSCALL_OK :
                 ASTRA_SYSCALL_PEER_DEAD;
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_log_failure("media host", ASTRA_STATUS_PEER_DEAD);
        (void)astra_service_ready(bootstrap->handle, ASTRA_STATUS_PEER_DEAD,
                                  NULL, 0u);
        return ASTRA_STATUS_PEER_DEAD;
    }
    status = astra_service_ready(bootstrap->handle, ASTRA_STATUS_OK,
                                 &publication, 1u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_PEER_DEAD;
    for (;;) {
        uint32_t waits[ASTRA_WAIT_MULTIPLE_MAX];
        PcmSession *selected[ASTRA_WAIT_MULTIPLE_MAX];
        uint32_t count = 1u, index = ASTRA_WAIT_INDEX_NONE;

        waits[0] = factory;
        selected[0] = NULL;
        for (PcmSession *session = sessions; session != NULL;
             session = session->next) {
            if (count == ASTRA_WAIT_MULTIPLE_MAX)
                return ASTRA_STATUS_LIMIT;
            waits[count] = session->receive;
            selected[count++] = session;
        }
        status = astra_wait_multiple(waits, count,
                                     astra_clock_monotonic() +
                                     HEALTH_INTERVAL_NS,
                                     &index, NULL);
        if (status == ASTRA_SYSCALL_TIMED_OUT) {
            if (host_command(ASTRA_HOST_AUDIO_STATUS, 0u, 0u, 0u,
                             NULL) != ASTRA_STATUS_OK) {
                (void)astra_log_failure("media host",
                                        ASTRA_STATUS_PEER_DEAD);
                return ASTRA_STATUS_PEER_DEAD;
            }
            continue;
        }
        if (index >= count)
            return ASTRA_STATUS_PROTOCOL;
        if (status == ASTRA_SYSCALL_PEER_DEAD ||
            status == ASTRA_SYSCALL_CLOSED) {
            if (index != 0u)
                session_release(selected[index]);
            continue;
        }
        if (status != ASTRA_SYSCALL_OK)
            return ASTRA_STATUS_IO;
        if (index == 0u)
            serve_open(factory);
        else
            serve_session(selected[index]);
        if (host_failed) {
            (void)astra_log_failure("media host", ASTRA_STATUS_PEER_DEAD);
            return ASTRA_STATUS_PEER_DEAD;
        }
    }
}
