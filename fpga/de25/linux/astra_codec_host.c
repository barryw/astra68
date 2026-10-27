#define _GNU_SOURCE
#include "astra_codec_host.h"
#include "astra_codec_wire.h"

#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CODEC_EVENT_TIMEOUT_MS 10000

static int event_receive(AstraCodecHostSession *session,
                         AstraCodecWorkerMessage *event, void *output,
                         uint32_t output_capacity)
{
    int64_t deadline = astra_codec_wire_now_ms() + CODEC_EVENT_TIMEOUT_MS;
    int error = astra_codec_wire_message(session->socket, event, 0, deadline);

    if (error != 0)
        return error;
    if (event->status > 3u)
        return -EPROTO;
    if (event->kind == ASTRA_CODEC_WORKER_NEED_SOURCE) {
        if (event->status != 0 || event->capacity == 0 ||
            event->capacity > ASTRA_CODEC_WORKER_LIMIT ||
            event->length != 0 || session->source_pending)
            return -EPROTO;
        session->requested_offset = ((uint64_t)event->offset_hi << 32) |
                                    event->offset_lo;
        session->requested_capacity = event->capacity;
        session->source_pending = 1;
        return 0;
    }
    if ((session->pending == ASTRA_CODEC_WORKER_OPEN &&
         event->kind != ASTRA_CODEC_WORKER_FORMAT) ||
        (session->pending == ASTRA_CODEC_WORKER_READ &&
         event->kind != ASTRA_CODEC_WORKER_CHUNK) ||
        (session->pending == ASTRA_CODEC_WORKER_SEEK &&
         event->kind != ASTRA_CODEC_WORKER_SEEKED) ||
        (event->kind == ASTRA_CODEC_WORKER_CHUNK &&
         (event->length > output_capacity ||
          (event->length != 0 && output == NULL) ||
          event->frame_offset > event->frame_bytes)))
        return -EPROTO;
    if (event->length != 0) {
        error = astra_codec_wire_transfer(session->socket, output,
                                           event->length, 0, deadline);
        if (error != 0)
            return error;
    }
    session->pending = 0;
    return 0;
}

static int request_send(AstraCodecHostSession *session, uint32_t kind,
                        uint64_t value, uint32_t capacity,
                        AstraCodecWorkerMessage *event, void *output)
{
    AstraCodecWorkerMessage request = {0};
    int64_t deadline = astra_codec_wire_now_ms() + CODEC_EVENT_TIMEOUT_MS;
    int error;

    if (session == NULL || session->socket < 0 || session->pending != 0 ||
        session->source_pending)
        return -EINVAL;
    request.kind = kind;
    request.offset_hi = (uint32_t)(value >> 32);
    request.offset_lo = (uint32_t)value;
    request.capacity = capacity;
    session->pending = kind;
    error = astra_codec_wire_message(session->socket, &request, 1, deadline);
    if (error == 0)
        error = event_receive(session, event, output, capacity);
    if (error != 0)
        astra_codec_host_close(session);
    return error;
}

int astra_codec_host_open(const char *worker, uint64_t source_length,
                          AstraCodecHostSession *session,
                          AstraCodecWorkerMessage *event)
{
    posix_spawn_file_actions_t actions;
    char *const arguments[] = {(char *)worker, NULL};
    char *const environment[] = {"PATH=/usr/bin:/bin", "LANG=C", NULL};
    int sockets[2] = {-1, -1};
    int error;

    if (worker == NULL || worker[0] != '/' || session == NULL ||
        event == NULL ||
        (session->socket != 0 && session->socket != -1) ||
        session->child != 0)
        return -EINVAL;
    *session = (AstraCodecHostSession){.socket = -1};
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0)
        return -errno;
    error = posix_spawn_file_actions_init(&actions);
    if (error != 0)
        goto failed;
    if ((error = posix_spawn_file_actions_adddup2(&actions, sockets[1],
                                                  STDIN_FILENO)) != 0 ||
        (error = posix_spawn_file_actions_adddup2(&actions, sockets[1],
                                                  STDOUT_FILENO)) != 0 ||
        (error = posix_spawn_file_actions_addclosefrom_np(&actions, 3)) != 0) {
        (void)posix_spawn_file_actions_destroy(&actions);
        goto failed;
    }
    error = posix_spawn(&session->child, worker, &actions, NULL,
                        arguments, environment);
    (void)posix_spawn_file_actions_destroy(&actions);
    if (error != 0)
        goto failed;
    close(sockets[1]);
    session->socket = sockets[0];
    return request_send(session, ASTRA_CODEC_WORKER_OPEN, source_length,
                        0, event, NULL);
failed:
    if (sockets[0] >= 0)
        close(sockets[0]);
    if (sockets[1] >= 0)
        close(sockets[1]);
    *session = (AstraCodecHostSession){0};
    return -error;
}

int astra_codec_host_read(AstraCodecHostSession *session, uint32_t capacity,
                          AstraCodecWorkerMessage *event, void *output)
{
    if (event == NULL || output == NULL || capacity == 0 ||
        capacity > ASTRA_CODEC_WORKER_LIMIT)
        return -EINVAL;
    return request_send(session, ASTRA_CODEC_WORKER_READ, 0, capacity,
                        event, output);
}

int astra_codec_host_seek(AstraCodecHostSession *session,
                          uint64_t timestamp_ns,
                          AstraCodecWorkerMessage *event)
{
    if (event == NULL)
        return -EINVAL;
    return request_send(session, ASTRA_CODEC_WORKER_SEEK,
                        timestamp_ns, 0, event, NULL);
}

int astra_codec_host_supply(AstraCodecHostSession *session, uint64_t offset,
                            const void *data, uint32_t bytes,
                            AstraCodecWorkerMessage *event, void *output,
                            uint32_t output_capacity)
{
    AstraCodecWorkerMessage reply = {0};
    int64_t deadline = astra_codec_wire_now_ms() + CODEC_EVENT_TIMEOUT_MS;
    int error;

    if (session == NULL || session->socket < 0 || !session->source_pending ||
        event == NULL || offset != session->requested_offset ||
        bytes > session->requested_capacity ||
        (bytes != 0 && data == NULL))
        return -EINVAL;
    reply.kind = ASTRA_CODEC_WORKER_SOURCE;
    reply.offset_hi = (uint32_t)(offset >> 32);
    reply.offset_lo = (uint32_t)offset;
    reply.length = bytes;
    session->source_pending = 0;
    error = astra_codec_wire_message(session->socket, &reply, 1, deadline);
    if (error == 0 && bytes != 0)
        error = astra_codec_wire_transfer(session->socket, (void *)data,
                                           bytes, 1, deadline);
    if (error == 0)
        error = event_receive(session, event, output, output_capacity);
    if (error != 0)
        astra_codec_host_close(session);
    return error;
}

void astra_codec_host_close(AstraCodecHostSession *session)
{
    int status;

    if (session == NULL)
        return;
    if (session->child > 0 && session->socket >= 0)
        close(session->socket);
    if (session->child > 0) {
        int64_t deadline = astra_codec_wire_now_ms() + 1000;

        for (;;) {
            pid_t result = waitpid(session->child, &status, WNOHANG);
            if (result == session->child || (result < 0 && errno != EINTR))
                break;
            if (astra_codec_wire_now_ms() >= deadline) {
                (void)kill(session->child, SIGKILL);
                while (waitpid(session->child, &status, 0) < 0 &&
                       errno == EINTR) {}
                break;
            }
            struct timespec pause = {0, 1000000};
            (void)nanosleep(&pause, NULL);
        }
    }
    *session = (AstraCodecHostSession){.socket = -1};
}
