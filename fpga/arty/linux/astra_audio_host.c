// SPDX-License-Identifier: MIT

#define _GNU_SOURCE

#include "astra_graphics_hw.h"

#include <astra/audio_host.h>
#include <astra/pcm_format.h>
#include <astra/status.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

enum {
    AUDIO_BASE = ASTRA_CONTROL_BASE + 0x6000u,
    AUDIO_BYTES = 0x1000u,
    AUDIO_ID = 0x41554430u,
    AUDIO_VERSION = 0x00010000u,
    AUDIO_RATE = 48000u,
    AUDIO_FRAMES = 512u,
    REG_ID = 0x00u,
    REG_VERSION = 0x04u,
    REG_CONTROL = 0x0cu,
    REG_STATUS = 0x10u,
    REG_LEFT = 0x14u,
    REG_RIGHT = 0x18u,
    REG_UNDERRUNS = 0x1cu,
    REG_OVERFLOWS = 0x20u,
    REG_RATE = 0x24u,
    REG_FRAMES = 0x28u,
    CONTROL_ENABLE = 1u,
    CONTROL_DRAIN = 2u,
    STATUS_LEVEL_MASK = 0x3ffu,
    PREFILL_FRAMES = 384u,
};

typedef struct Voice {
    struct Voice *next;
    uint8_t *frames;
    uint32_t handle;
    uint32_t gain_q16;
    uint32_t format;
    uint32_t frame_bytes;
    uint32_t read_at;
    uint32_t queued;
    int paused;
    int finished;
    int gap_reported;
    int ever_written;
} Voice;

typedef struct Client {
    struct Client *next;
    Voice *voices;
    int fd;
    int monitor;
} Client;

typedef struct AudioHost {
    volatile uint32_t *registers;
    Client *clients;
    int listener;
    int epoll_fd;
    int lock_fd;
    uint32_t next_handle;
    uint32_t underrun_start;
    uint32_t overflow_start;
    uint32_t software_gaps;
    uint64_t mixed_frames;
    uint32_t maximum_active_voices;
    uint32_t minimum_level;
    uint32_t tail_written;
    int playing;
    int tailing;
    int draining;
    AstraAudioHostMonitorPacket monitor_packet;
} AudioHost;

static volatile sig_atomic_t running = 1;

static void stop_running(int signal_number)
{
    (void)signal_number;
    running = 0;
}

static uint32_t read_reg(const AudioHost *host, unsigned offset)
{
    return host->registers[offset / 4u];
}

static void write_reg(AudioHost *host, unsigned offset, uint32_t value)
{
    host->registers[offset / 4u] = value;
}

static int32_t s24le(const uint8_t *data)
{
    uint32_t bits = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                    ((uint32_t)data[2] << 16);

    return (int32_t)(bits ^ UINT32_C(0x800000)) - INT32_C(0x800000);
}

static int32_t s16be_as_s24(const uint8_t *data)
{
    uint16_t bits = ((uint16_t)data[0] << 8) | data[1];

    return (int32_t)((int16_t)bits) * 256;
}

static int32_t saturate24(int64_t sample)
{
    if (sample > INT32_C(0x7fffff))
        return INT32_C(0x7fffff);
    if (sample < -INT32_C(0x800000))
        return -INT32_C(0x800000);
    return (int32_t)sample;
}

static Voice *find_voice(Client *client, uint32_t handle)
{
    for (Voice *voice = client->voices; voice != NULL; voice = voice->next)
        if (voice->handle == handle)
            return voice;
    return NULL;
}

static uint32_t queued_any(const AudioHost *host)
{
    for (const Client *client = host->clients; client != NULL;
         client = client->next)
        for (const Voice *voice = client->voices; voice != NULL;
             voice = voice->next)
            if (voice->queued != 0u && !voice->paused)
                return 1u;
    return 0u;
}

static void free_client(AudioHost *host, Client *client);

static void mix_frame(AudioHost *host, int32_t *left, int32_t *right)
{
    int64_t sum_left = 0;
    int64_t sum_right = 0;
    uint32_t active_voices = 0u;

    for (Client *client = host->clients; client != NULL;
         client = client->next)
        for (Voice *voice = client->voices; voice != NULL;
             voice = voice->next) {
            const uint8_t *frame;

            if (voice->paused)
                continue;
            if (voice->queued == 0u) {
                if (voice->ever_written && !voice->finished &&
                    !voice->gap_reported) {
                    ++host->software_gaps;
                    voice->gap_reported = 1;
                }
                continue;
            }
            frame = voice->frames +
                    voice->read_at * voice->frame_bytes;
            ++active_voices;
            if (voice->format == ASTRA_PCM_FORMAT_S24LE_STEREO) {
                sum_left += ((int64_t)s24le(frame) * voice->gain_q16) >> 16;
                sum_right += ((int64_t)s24le(frame + 3u) * voice->gain_q16) >> 16;
            } else {
                sum_left += ((int64_t)s16be_as_s24(frame) *
                             voice->gain_q16) >> 16;
                sum_right += ((int64_t)s16be_as_s24(frame + 2u) *
                              voice->gain_q16) >> 16;
            }
            voice->read_at = (voice->read_at + 1u) %
                             ASTRA_AUDIO_HOST_QUEUE_FRAMES;
            --voice->queued;
        }
    if (active_voices > host->maximum_active_voices)
        host->maximum_active_voices = active_voices;
    *left = saturate24(sum_left);
    *right = saturate24(sum_right);
}

static void monitor_flush(AudioHost *host)
{
    Client *client = host->clients;
    size_t length;

    if (host->monitor_packet.frames == 0u)
        return;
    length = 12u + host->monitor_packet.frames * 4u;
    while (client != NULL) {
        Client *next = client->next;

        if (client->monitor) {
            ssize_t written = send(client->fd, &host->monitor_packet, length,
                                   MSG_NOSIGNAL | MSG_DONTWAIT);

            if (written != (ssize_t)length &&
                !(written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)))
                free_client(host, client);
        }
        client = next;
    }
    host->monitor_packet.frames = 0u;
}

static void monitor_frame(AudioHost *host, int32_t left, int32_t right)
{
    uint8_t *pcm;
    int16_t samples[2] = {(int16_t)(left / 256), (int16_t)(right / 256)};

    if (host->monitor_packet.frames == 0u) {
        bool listening = false;

        for (Client *client = host->clients; client != NULL;
             client = client->next)
            if (client->monitor)
                listening = true;
        if (!listening)
            return;
        host->monitor_packet.magic = ASTRA_AUDIO_HOST_MAGIC;
        host->monitor_packet.version = ASTRA_AUDIO_HOST_VERSION;
    }
    pcm = host->monitor_packet.pcm + host->monitor_packet.frames * 4u;
    for (unsigned channel = 0u; channel < 2u; ++channel) {
        pcm[channel * 2u] = (uint8_t)samples[channel];
        pcm[channel * 2u + 1u] = (uint8_t)((uint16_t)samples[channel] >> 8);
    }
    if (++host->monitor_packet.frames == ASTRA_AUDIO_HOST_MONITOR_FRAMES)
        monitor_flush(host);
}

static void feed(AudioHost *host)
{
    uint32_t level = read_reg(host, REG_STATUS) & STATUS_LEVEL_MASK;
    uint32_t room = AUDIO_FRAMES - level;

    if (host->draining) {
        if (level != 0u)
            return;
        write_reg(host, REG_CONTROL, 0u);
        host->draining = 0;
    }
    if (host->playing && !host->tailing && queued_any(host) &&
        level < host->minimum_level)
        host->minimum_level = level;
    if (queued_any(host)) {
        host->tailing = 0;
        host->tail_written = 0u;
    }
    while (room != 0u && queued_any(host)) {
        int32_t left;
        int32_t right;

        mix_frame(host, &left, &right);
        write_reg(host, REG_LEFT, (uint32_t)left & UINT32_C(0xffffff));
        write_reg(host, REG_RIGHT, (uint32_t)right & UINT32_C(0xffffff));
        monitor_frame(host, left, right);
        --room;
        ++level;
        ++host->mixed_frames;
        if (!host->playing && level >= PREFILL_FRAMES) {
            write_reg(host, REG_CONTROL, CONTROL_ENABLE);
            host->playing = 1;
        }
    }
    if (queued_any(host))
        return;
    if (!host->playing && level == 0u)
        return;
    while (room != 0u && host->tail_written < AUDIO_FRAMES) {
        if (!host->tailing)
            for (Client *client = host->clients; client != NULL;
                 client = client->next)
                for (Voice *voice = client->voices; voice != NULL;
                     voice = voice->next)
                    if (!voice->paused && voice->ever_written &&
                        !voice->finished &&
                        !voice->gap_reported) {
                        ++host->software_gaps;
                        voice->gap_reported = 1;
                    }
        host->tailing = 1;
        write_reg(host, REG_LEFT, 0u);
        write_reg(host, REG_RIGHT, 0u);
        monitor_frame(host, 0, 0);
        --room;
        ++level;
        ++host->tail_written;
        if (!host->playing && level >= PREFILL_FRAMES) {
            write_reg(host, REG_CONTROL, CONTROL_ENABLE);
            host->playing = 1;
        }
    }
    if (host->tail_written == AUDIO_FRAMES) {
        write_reg(host, REG_CONTROL, CONTROL_DRAIN);
        host->playing = 0;
        host->tailing = 0;
        host->tail_written = 0u;
        host->draining = 1;
    }
}

static void free_client(AudioHost *host, Client *client)
{
    Client **at = &host->clients;

    while (*at != NULL && *at != client)
        at = &(*at)->next;
    if (*at == client)
        *at = client->next;
    while (client->voices != NULL) {
        Voice *voice = client->voices;

        client->voices = voice->next;
        free(voice->frames);
        free(voice);
    }
    (void)epoll_ctl(host->epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
    (void)close(client->fd);
    free(client);
}

static uint32_t validate_request(const AstraAudioHostRequest *request,
                                 size_t packet_length)
{
    if (packet_length < sizeof(*request) ||
        request->magic != ASTRA_AUDIO_HOST_MAGIC ||
        request->version != ASTRA_AUDIO_HOST_VERSION ||
        request->data_length != packet_length - sizeof(*request))
        return ASTRA_STATUS_PROTOCOL;
    if (request->operation == ASTRA_HOST_AUDIO_WRITE)
        return request->handle != 0u && request->value == 0u &&
                       request->data_length != 0u &&
                       request->data_length % 2u == 0u &&
                       request->data_length <=
                           ASTRA_AUDIO_HOST_PACKET_FRAMES *
                               ASTRA_AUDIO_HOST_FRAME_BYTES ?
                   ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
    if (request->data_length != 0u)
        return ASTRA_STATUS_INVALID;
    if (request->operation == ASTRA_AUDIO_HOST_MONITOR)
        return request->handle == 0u && request->value == 0u ?
               ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
    if (request->operation == ASTRA_HOST_AUDIO_OPEN)
        return request->handle == 0u &&
               astra_pcm_format_frame_bytes(request->value) != 0u ?
               ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
    if (request->operation == ASTRA_HOST_AUDIO_GAIN)
        return request->handle != 0u ? ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
    if (request->operation == ASTRA_HOST_AUDIO_STATUS)
        return request->value == 0u ? ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
    if (request->operation == ASTRA_HOST_AUDIO_CLOSE)
        return request->handle != 0u && request->value == 0u ?
               ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
    if (request->operation == ASTRA_HOST_AUDIO_FINISH)
        return request->handle != 0u && request->value == 0u ?
               ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
    if (request->operation == ASTRA_HOST_AUDIO_PAUSE)
        return request->handle != 0u && request->value <= 1u ?
               ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
    if (request->operation == ASTRA_HOST_AUDIO_CLEAR)
        return request->handle != 0u && request->value == 0u ?
               ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
    return ASTRA_STATUS_UNSUPPORTED;
}

static void execute(AudioHost *host, Client *client,
                    const AstraAudioHostRequest *request,
                    const uint8_t *data, AstraAudioHostReply *reply)
{
    Voice *voice;

    switch (request->operation) {
    case ASTRA_AUDIO_HOST_MONITOR:
        if (client->voices != NULL)
            reply->status = ASTRA_STATUS_INVALID;
        else
            client->monitor = 1;
        break;
    case ASTRA_HOST_AUDIO_OPEN:
        voice = calloc(1u, sizeof(*voice));
        if (voice != NULL)
            voice->frames = malloc(ASTRA_AUDIO_HOST_QUEUE_FRAMES *
                                   ASTRA_AUDIO_HOST_FRAME_BYTES);
        if (voice == NULL || voice->frames == NULL) {
            free(voice);
            reply->status = ASTRA_STATUS_NO_SPACE;
            break;
        }
        if (++host->next_handle == 0u)
            ++host->next_handle;
        voice->handle = host->next_handle;
        voice->gain_q16 = UINT32_C(65536);
        voice->format = request->value;
        voice->frame_bytes = astra_pcm_format_frame_bytes(request->value);
        voice->next = client->voices;
        client->voices = voice;
        reply->handle = voice->handle;
        break;
    case ASTRA_HOST_AUDIO_WRITE:
        voice = find_voice(client, request->handle);
        if (voice == NULL) {
            reply->status = ASTRA_STATUS_BAD_HANDLE;
            break;
        }
        if (voice->finished) {
            reply->status = ASTRA_STATUS_INVALID;
            break;
        }
        if (request->data_length % voice->frame_bytes != 0u) {
            reply->status = ASTRA_STATUS_INVALID;
            break;
        }
        if (request->data_length / voice->frame_bytes >
            ASTRA_AUDIO_HOST_QUEUE_FRAMES - voice->queued) {
            reply->status = ASTRA_STATUS_BUSY;
            break;
        }
        for (uint32_t at = 0u; at < request->data_length;
             at += voice->frame_bytes) {
            uint32_t write_at = (voice->read_at + voice->queued) %
                                ASTRA_AUDIO_HOST_QUEUE_FRAMES;

            memcpy(voice->frames + write_at * voice->frame_bytes,
                   data + at, voice->frame_bytes);
            ++voice->queued;
        }
        voice->ever_written = 1;
        voice->gap_reported = 0;
        reply->queued_frames = voice->queued;
        break;
    case ASTRA_HOST_AUDIO_GAIN:
        voice = find_voice(client, request->handle);
        if (voice == NULL)
            reply->status = ASTRA_STATUS_BAD_HANDLE;
        else
            voice->gain_q16 = request->value;
        break;
    case ASTRA_HOST_AUDIO_STATUS:
        if (request->handle != 0u) {
            voice = find_voice(client, request->handle);
            if (voice == NULL)
                reply->status = ASTRA_STATUS_BAD_HANDLE;
            else
                reply->queued_frames = voice->queued;
        }
        break;
    case ASTRA_HOST_AUDIO_CLOSE: {
        Voice **at = &client->voices;

        while (*at != NULL && (*at)->handle != request->handle)
            at = &(*at)->next;
        if (*at == NULL) {
            reply->status = ASTRA_STATUS_BAD_HANDLE;
            break;
        }
        voice = *at;
        *at = voice->next;
        free(voice->frames);
        free(voice);
        break;
    }
    case ASTRA_HOST_AUDIO_FINISH:
        voice = find_voice(client, request->handle);
        if (voice == NULL)
            reply->status = ASTRA_STATUS_BAD_HANDLE;
        else
            voice->finished = 1;
        break;
    case ASTRA_HOST_AUDIO_PAUSE:
        voice = find_voice(client, request->handle);
        if (voice == NULL)
            reply->status = ASTRA_STATUS_BAD_HANDLE;
        else
            voice->paused = request->value != 0u;
        break;
    case ASTRA_HOST_AUDIO_CLEAR:
        voice = find_voice(client, request->handle);
        if (voice == NULL)
            reply->status = ASTRA_STATUS_BAD_HANDLE;
        else {
            voice->queued = 0u;
            voice->read_at = 0u;
            voice->ever_written = 0;
            voice->gap_reported = 0;
        }
        break;
    default:
        reply->status = ASTRA_STATUS_UNSUPPORTED;
        break;
    }
}

static void receive_client(AudioHost *host, Client *client)
{
    uint8_t packet[sizeof(AstraAudioHostRequest) +
                   ASTRA_AUDIO_HOST_PACKET_FRAMES *
                       ASTRA_AUDIO_HOST_FRAME_BYTES];
    AstraAudioHostRequest request;
    AstraAudioHostReply reply = {.magic = ASTRA_AUDIO_HOST_MAGIC,
                                 .status = ASTRA_STATUS_OK};
    ssize_t received = recv(client->fd, packet, sizeof(packet),
                            MSG_DONTWAIT | MSG_TRUNC);

    if (received <= 0) {
        if (received == 0 || (errno != EAGAIN && errno != EINTR))
            free_client(host, client);
        return;
    }
    if ((size_t)received > sizeof(packet)) {
        reply.status = ASTRA_STATUS_INVALID;
    } else {
        memset(&request, 0, sizeof(request));
        memcpy(&request, packet, received < (ssize_t)sizeof(request) ?
               (size_t)received : sizeof(request));
        reply.status = validate_request(&request, (size_t)received);
        if (reply.status == ASTRA_STATUS_OK && client->monitor &&
            request.operation != ASTRA_AUDIO_HOST_MONITOR)
            reply.status = ASTRA_STATUS_INVALID;
        if (reply.status == ASTRA_STATUS_OK)
            execute(host, client, &request, packet + sizeof(request), &reply);
    }
    reply.hardware_frames = read_reg(host, REG_STATUS) & STATUS_LEVEL_MASK;
    reply.underruns = read_reg(host, REG_UNDERRUNS) - host->underrun_start;
    reply.overflows = read_reg(host, REG_OVERFLOWS) - host->overflow_start;
    reply.software_gaps = host->software_gaps;
    if (send(client->fd, &reply, sizeof(reply), MSG_NOSIGNAL | MSG_DONTWAIT) !=
        sizeof(reply))
        free_client(host, client);
}

static int setup_hardware(AudioHost *host)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);

    if (fd < 0)
        return -1;
    host->registers = mmap(NULL, AUDIO_BYTES, PROT_READ | PROT_WRITE,
                           MAP_SHARED, fd, AUDIO_BASE);
    (void)close(fd);
    if (host->registers == MAP_FAILED) {
        host->registers = NULL;
        return -1;
    }
    if (read_reg(host, REG_ID) != AUDIO_ID ||
        read_reg(host, REG_VERSION) != AUDIO_VERSION ||
        read_reg(host, REG_RATE) != AUDIO_RATE ||
        read_reg(host, REG_FRAMES) != AUDIO_FRAMES) {
        errno = ENODEV;
        return -1;
    }
    write_reg(host, REG_CONTROL, CONTROL_DRAIN);
    for (unsigned attempt = 0u; attempt < 100u; ++attempt) {
        struct timespec delay = {.tv_sec = 0, .tv_nsec = 1000000};

        if ((read_reg(host, REG_STATUS) & STATUS_LEVEL_MASK) == 0u)
            break;
        (void)nanosleep(&delay, NULL);
    }
    if ((read_reg(host, REG_STATUS) & STATUS_LEVEL_MASK) != 0u) {
        errno = EBUSY;
        return -1;
    }
    write_reg(host, REG_CONTROL, 0u);
    host->underrun_start = read_reg(host, REG_UNDERRUNS);
    host->overflow_start = read_reg(host, REG_OVERFLOWS);
    host->minimum_level = AUDIO_FRAMES;
    return 0;
}

static int claim_hardware(AudioHost *host)
{
    host->lock_fd = open(ASTRA_AUDIO_HOST_LOCK,
                         O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (host->lock_fd < 0)
        return -1;
    if (flock(host->lock_fd, LOCK_EX | LOCK_NB) == 0)
        return 0;
    errno = EBUSY;
    return -1;
}

static int setup_socket(AudioHost *host, const char *path)
{
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    struct stat st;
    struct epoll_event event = {.events = EPOLLIN, .data.ptr = host};

    if (strlen(path) >= sizeof(address.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (lstat(path, &st) == 0) {
        if (!S_ISSOCK(st.st_mode) || unlink(path) != 0)
            return -1;
    } else if (errno != ENOENT) {
        return -1;
    }
    host->listener = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK |
                           SOCK_CLOEXEC, 0);
    if (host->listener < 0)
        return -1;
    memcpy(address.sun_path, path, strlen(path) + 1u);
    if (bind(host->listener, (struct sockaddr *)&address,
             sizeof(address)) != 0 ||
        chmod(path, 0600) != 0 || listen(host->listener, SOMAXCONN) != 0)
        return -1;
    host->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (host->epoll_fd < 0 ||
        epoll_ctl(host->epoll_fd, EPOLL_CTL_ADD, host->listener, &event) != 0)
        return -1;
    return 0;
}

static void accept_client(AudioHost *host)
{
    struct epoll_event event = {.events = EPOLLIN | EPOLLRDHUP};
    Client *client = calloc(1u, sizeof(*client));

    if (client == NULL)
        return;
    client->fd = accept4(host->listener, NULL, NULL,
                         SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client->fd < 0) {
        free(client);
        return;
    }
    event.data.ptr = client;
    if (epoll_ctl(host->epoll_fd, EPOLL_CTL_ADD, client->fd, &event) != 0) {
        (void)close(client->fd);
        free(client);
        return;
    }
    client->next = host->clients;
    host->clients = client;
}

static int mix_self_test(void)
{
    AudioHost host = {0};
    Client client = {0};
    uint32_t handles[16];
    const uint8_t frame[4] = {0u, 1u, 0u, 1u};
    AstraAudioHostRequest request = {
        .magic = ASTRA_AUDIO_HOST_MAGIC,
        .version = ASTRA_AUDIO_HOST_VERSION,
        .operation = ASTRA_HOST_AUDIO_OPEN,
        .value = ASTRA_PCM_FORMAT_S16BE_STEREO,
    };
    AstraAudioHostReply reply;
    int32_t left = 0, right = 0;
    int passed = 0;
    unsigned stage = 0u;

    host.clients = &client;
    for (unsigned i = 0u; i < 16u; ++i) {
        stage = 100u + i;
        reply = (AstraAudioHostReply){.status = ASTRA_STATUS_OK};
        if (validate_request(&request, sizeof(request)) != ASTRA_STATUS_OK)
            goto done;
        execute(&host, &client, &request, NULL, &reply);
        if (reply.status != ASTRA_STATUS_OK || reply.handle == 0u)
            goto done;
        handles[i] = reply.handle;
        request.operation = ASTRA_HOST_AUDIO_WRITE;
        request.handle = reply.handle;
        request.value = 0u;
        request.data_length = sizeof(frame);
        reply = (AstraAudioHostReply){.status = ASTRA_STATUS_OK};
        execute(&host, &client, &request, frame, &reply);
        if (reply.status != ASTRA_STATUS_OK || reply.queued_frames != 1u)
            goto done;
        request.operation = ASTRA_HOST_AUDIO_OPEN;
        request.handle = 0u;
        request.value = ASTRA_PCM_FORMAT_S16BE_STEREO;
        request.data_length = 0u;
    }
    mix_frame(&host, &left, &right);
    stage = 200u;
    if (left != 4096 || right != 4096 ||
        host.maximum_active_voices != 16u)
        goto done;
    for (unsigned i = 0u; i < 16u; ++i) {
        stage = 300u + i;
        request.operation = ASTRA_HOST_AUDIO_WRITE;
        request.handle = handles[i];
        request.value = 0u;
        request.data_length = sizeof(frame);
        reply = (AstraAudioHostReply){.status = ASTRA_STATUS_OK};
        execute(&host, &client, &request, frame, &reply);
        if (reply.status != ASTRA_STATUS_OK)
            goto done;
        if (i < 8u) {
            request.operation = ASTRA_HOST_AUDIO_PAUSE;
            request.value = 1u;
            request.data_length = 0u;
            reply = (AstraAudioHostReply){.status = ASTRA_STATUS_OK};
            execute(&host, &client, &request, NULL, &reply);
            if (reply.status != ASTRA_STATUS_OK)
                goto done;
        }
    }
    mix_frame(&host, &left, &right);
    stage = 400u;
    if (left != 2048 || right != 2048)
        goto done;
    request.operation = ASTRA_HOST_AUDIO_PAUSE;
    stage = 500u;
    request.handle = handles[0];
    request.value = 2u;
    request.data_length = 0u;
    if (validate_request(&request, sizeof(request)) != ASTRA_STATUS_INVALID)
        goto done;
    request.operation = ASTRA_HOST_AUDIO_WRITE;
    stage = 600u;
    request.value = 0u;
    request.data_length = 6u;
    if (validate_request(&request, sizeof(request) + 6u) != ASTRA_STATUS_OK)
        goto done;
    reply = (AstraAudioHostReply){.status = ASTRA_STATUS_OK};
    execute(&host, &client, &request, frame, &reply);
    if (reply.status != ASTRA_STATUS_INVALID)
        goto done;
    request.operation = ASTRA_HOST_AUDIO_CLEAR;
    stage = 700u;
    request.handle = UINT32_MAX;
    request.data_length = 0u;
    reply = (AstraAudioHostReply){.status = ASTRA_STATUS_OK};
    execute(&host, &client, &request, NULL, &reply);
    if (reply.status != ASTRA_STATUS_BAD_HANDLE)
        goto done;
    for (unsigned i = 0u; i < 8u; ++i) {
        stage = 800u + i;
        request.handle = handles[i];
        reply = (AstraAudioHostReply){.status = ASTRA_STATUS_OK};
        execute(&host, &client, &request, NULL, &reply);
        if (reply.status != ASTRA_STATUS_OK)
            goto done;
    }
    request.operation = ASTRA_HOST_AUDIO_PAUSE;
    request.value = 0u;
    for (unsigned i = 0u; i < 8u; ++i) {
        stage = 900u + i;
        request.handle = handles[i];
        reply = (AstraAudioHostReply){.status = ASTRA_STATUS_OK};
        execute(&host, &client, &request, NULL, &reply);
        if (reply.status != ASTRA_STATUS_OK)
            goto done;
    }
    mix_frame(&host, &left, &right);
    stage = 1000u;
    if (left != 0 || right != 0)
        goto done;
    passed = 1;
done:
    if (!passed)
        fprintf(stderr, "audio mix self-test failed at %u (%d,%d)\n",
                stage, left, right);
    while (client.voices != NULL) {
        Voice *voice = client.voices;

        client.voices = voice->next;
        free(voice->frames);
        free(voice);
    }
    return passed;
}

static int self_test(void)
{
    uint8_t sample[] = {0xffu, 0xffu, 0x7fu,
                        0x00u, 0x00u, 0x80u};
    AstraAudioHostRequest request = {
        .magic = ASTRA_AUDIO_HOST_MAGIC,
        .version = ASTRA_AUDIO_HOST_VERSION,
        .operation = ASTRA_HOST_AUDIO_WRITE,
        .handle = 1u,
        .data_length = sizeof(sample),
    };
    AudioHost monitor_host = {0};
    Client monitor_client = {.fd = -1, .monitor = 1};
    AstraAudioHostMonitorPacket packet;
    int sockets[2];
    ssize_t received;

    if (!mix_self_test() ||
        s24le(sample) != INT32_C(0x7fffff) ||
        s16be_as_s24((const uint8_t[]){0x80u, 0u}) !=
            -INT32_C(0x800000) ||
        s24le(sample + 3u) != -INT32_C(0x800000) ||
        saturate24(INT64_C(9000000)) != INT32_C(0x7fffff) ||
        saturate24(-INT64_C(9000000)) != -INT32_C(0x800000) ||
        validate_request(&request, sizeof(request) + sizeof(sample)) !=
            ASTRA_STATUS_OK)
        return EXIT_FAILURE;
    request.data_length = 5u;
    if (validate_request(&request, sizeof(request) + 5u) !=
        ASTRA_STATUS_INVALID)
        return EXIT_FAILURE;
    request.data_length = 0u;
    if (validate_request(&request, sizeof(request)) != ASTRA_STATUS_INVALID)
        return EXIT_FAILURE;
    request.operation = ASTRA_HOST_AUDIO_OPEN;
    request.handle = 0u;
    request.value = ASTRA_PCM_FORMAT_S24LE_STEREO;
    if (validate_request(&request, sizeof(request)) != ASTRA_STATUS_OK)
        return EXIT_FAILURE;
    request.operation = ASTRA_AUDIO_HOST_MONITOR;
    request.value = 0u;
    if (validate_request(&request, sizeof(request)) != ASTRA_STATUS_OK)
        return EXIT_FAILURE;
    request.handle = 1u;
    if (validate_request(&request, sizeof(request)) != ASTRA_STATUS_INVALID)
        return EXIT_FAILURE;
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) != 0)
        return EXIT_FAILURE;
    monitor_frame(&monitor_host, INT32_C(0x400000), -INT32_C(0x400000));
    if (monitor_host.monitor_packet.frames != 0u)
        goto monitor_failed;
    monitor_client.fd = sockets[0];
    monitor_host.clients = &monitor_client;
    for (unsigned frame = 0u; frame < ASTRA_AUDIO_HOST_MONITOR_FRAMES;
         ++frame)
        monitor_frame(&monitor_host, INT32_C(0x400000), -INT32_C(0x400000));
    received = recv(sockets[1], &packet, sizeof(packet), MSG_DONTWAIT);
    if (received != (ssize_t)sizeof(packet) ||
        packet.magic != ASTRA_AUDIO_HOST_MAGIC ||
        packet.version != ASTRA_AUDIO_HOST_VERSION ||
        packet.frames != ASTRA_AUDIO_HOST_MONITOR_FRAMES ||
        memcmp(packet.pcm, (const uint8_t[]){0x00, 0x40, 0x00, 0xc0},
               4u) != 0 ||
        monitor_host.monitor_packet.frames != 0u)
        goto monitor_failed;
    (void)close(sockets[0]);
    (void)close(sockets[1]);
    request.magic = 0u;
    return validate_request(&request, sizeof(request)) ==
           ASTRA_STATUS_PROTOCOL ? EXIT_SUCCESS : EXIT_FAILURE;

monitor_failed:
    (void)close(sockets[0]);
    (void)close(sockets[1]);
    return EXIT_FAILURE;
}

int main(int argc, char **argv)
{
    const char *path = ASTRA_AUDIO_HOST_SOCKET;
    AudioHost host = {.listener = -1, .epoll_fd = -1, .lock_fd = -1};
    struct epoll_event events[32];
    int result = EXIT_FAILURE;

    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
        return self_test();
    if (argc == 3 && strcmp(argv[1], "--socket") == 0)
        path = argv[2];
    else if (argc != 1) {
        fprintf(stderr, "usage: %s [--self-test | --socket PATH]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (claim_hardware(&host) != 0 || setup_hardware(&host) != 0 ||
        setup_socket(&host, path) != 0) {
        perror("Astra audio host setup");
        goto done;
    }
    fprintf(stderr, "ASTRA AUDIO HOST ready socket=%s rate=%u channels=2\n",
            path, AUDIO_RATE);
    (void)signal(SIGTERM, stop_running);
    (void)signal(SIGINT, stop_running);
    while (running) {
        int count = epoll_wait(host.epoll_fd, events,
                               sizeof(events) / sizeof(events[0]),
                               host.playing || queued_any(&host) ? 1 : 1000);

        if (count < 0 && errno != EINTR) {
            perror("Astra audio host poll");
            goto done;
        }
        for (int index = 0; index < count; ++index) {
            if (events[index].data.ptr == &host)
                accept_client(&host);
            else
                receive_client(&host, events[index].data.ptr);
        }
        feed(&host);
    }
    result = EXIT_SUCCESS;

done:
    if (host.registers != NULL) {
        write_reg(&host, REG_CONTROL, 0u);
        fprintf(stderr, "ASTRA AUDIO HOST frames=%llu voices_peak=%u "
                "min_fifo=%u underruns=%u overflows=%u gaps=%u\n",
                (unsigned long long)host.mixed_frames,
                host.maximum_active_voices,
                host.minimum_level,
                read_reg(&host, REG_UNDERRUNS) - host.underrun_start,
                read_reg(&host, REG_OVERFLOWS) - host.overflow_start,
                host.software_gaps);
    }
    while (host.clients != NULL)
        free_client(&host, host.clients);
    if (host.epoll_fd >= 0)
        (void)close(host.epoll_fd);
    if (host.listener >= 0)
        (void)close(host.listener);
    if (host.lock_fd >= 0)
        (void)close(host.lock_fd);
    if (host.registers != NULL)
        (void)munmap((void *)host.registers, AUDIO_BYTES);
    if (result == EXIT_SUCCESS)
        (void)unlink(path);
    return result;
}
