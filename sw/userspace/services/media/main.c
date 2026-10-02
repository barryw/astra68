#include <astra/audio_host.h>
#include <astra/host.h>
#include <astra/pcm_service.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/area.h>

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
    /* Usable bytes of the client's area: what it mapped, up to the host
     * window. Every request is bounded by it. */
    uint32_t area_bytes;
    /* A converter's target frame width; zero for a voice. */
    uint32_t target_frame_bytes;
    /* A MIDI voice, and the SoundFont it is being sent, if any. */
    int midi;
    uint32_t upload;
} PcmSession;

/* A sent SoundFont's name on the host: its SHA-256. */
#define DIGEST_BYTES ASTRA_HOST_AUDIO_DIGEST_BYTES

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
        (request->header.operation == ASTRA_PCM_MIDI_STATUS ||
         request->header.operation == ASTRA_PCM_MIDI_FONTS ||
         request->header.operation == ASTRA_PCM_MIDI_PRESETS)) {
        reply.frames_out = frames_out;
        reply.value = host_result->result_value;
    }
    return astra_port_send(reply_handle, &reply, sizeof(reply), handles,
                           transfer == 0u ? 0u : 1u);
}

/* A MIDI voice with the default SoundFont set: the host reads the shared
 * fonts from the SOUND volume itself. */
static uint32_t open_midi(uint32_t *voice)
{
    AstraHostCommand *result = NULL;
    uint32_t status = host_command(ASTRA_HOST_AUDIO_MIDI_OPEN, 0u, 0u, 0u,
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
        session->area_bytes = mapped_size < ASTRA_PCM_TRANSFER_BYTES ?
                              mapped_size : ASTRA_PCM_TRANSFER_BYTES;
        if (status != ASTRA_SYSCALL_OK ||
            session->area_bytes < ASTRA_PCM_MAX_FRAME_BYTES)
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
                         operation <= ASTRA_PCM_OPERATION_MAX;

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

/* A host reply's data, copied into the session's area: frames_out bytes. */
static uint32_t answer_into_area(PcmSession *session, uint32_t operation,
                                 uint32_t value, AstraHostCommand **result,
                                 uint32_t *frames_out)
{
    const uint32_t area_bytes = session->area_bytes;
    uint32_t status = host_exchange(
        operation, operation == ASTRA_HOST_AUDIO_FONT_LIST ? 0u :
                   session->host_voice,
        value, 0u, 0u, area_bytes, result);

    if (status != ASTRA_STATUS_OK)
        return status;
    if ((*result)->result_length > area_bytes)
        return ASTRA_STATUS_PROTOCOL;
    (void)memcpy(session->samples, (const void *)(uintptr_t)host.data,
                 (*result)->result_length);
    *frames_out = (*result)->result_length;
    return ASTRA_STATUS_OK;
}

/* A shared font, by its file name in SOUND:soundfonts. */
static uint32_t system_font(PcmSession *session, const AstraPcmRequest *request)
{
    if (request->frames == 0u || request->frames > ASTRA_PCM_FONT_NAME_MAX)
        return ASTRA_STATUS_INVALID;
    (void)memcpy((void *)(uintptr_t)host.data, session->samples,
                 request->frames);
    return host_exchange(ASTRA_HOST_AUDIO_MIDI_SYSTEM_FONT,
                         session->host_voice, 0u, 0u, request->frames,
                         request->frames, NULL);
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
                request.frames > session->area_bytes ||
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
            status = request.frames == 0u && request.value == 0u ?
                     host_command(ASTRA_HOST_AUDIO_MIDI_STOP,
                                  session->host_voice, 0u, 0u, &result) :
                     ASTRA_STATUS_INVALID;
            break;
        case ASTRA_PCM_MIDI_STATUS:
        case ASTRA_PCM_MIDI_FONTS:
        case ASTRA_PCM_MIDI_PRESETS:
            status = request.frames != 0u ||
                     (operation == ASTRA_PCM_MIDI_STATUS &&
                      request.value != 0u) ? ASTRA_STATUS_INVALID :
                     answer_into_area(session,
                                      operation == ASTRA_PCM_MIDI_STATUS ?
                                          ASTRA_HOST_AUDIO_MIDI_STATUS :
                                      operation == ASTRA_PCM_MIDI_FONTS ?
                                          ASTRA_HOST_AUDIO_FONT_LIST :
                                          ASTRA_HOST_AUDIO_MIDI_PRESETS,
                                      request.value, &result, &frames_out);
            break;
        case ASTRA_PCM_MIDI_EVENTS:
            if (request.value != 0u || request.frames == 0u ||
                request.frames > session->area_bytes ||
                request.frames % ASTRA_HOST_MIDI_EVENT_BYTES != 0u) {
                status = ASTRA_STATUS_INVALID;
                break;
            }
            (void)memcpy((void *)(uintptr_t)host.data, session->samples,
                         request.frames);
            status = host_exchange(ASTRA_HOST_AUDIO_MIDI_EVENTS,
                                   session->host_voice, 0u, 0u,
                                   request.frames, request.frames, NULL);
            break;
        case ASTRA_PCM_MIDI_SET:
            status = request.frames == 0u ?
                     host_exchange(ASTRA_HOST_AUDIO_MIDI_SET,
                                   session->host_voice, request.value,
                                   request.target, 0u, 0u, NULL) :
                     ASTRA_STATUS_INVALID;
            break;
        case ASTRA_PCM_CONVERT: {
            const uint32_t area_bytes = session->area_bytes;

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
                request.frames > ASTRA_PCM_TRANSFER_FRAMES ||
                request.frames * session->frame_bytes > session->area_bytes) {
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
    status = astra_host_client_open(device->handle, ASTRA_HOST_CAP_AUDIO,
                                    ASTRA_PCM_TRANSFER_BYTES, &host);
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
