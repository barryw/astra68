#define _GNU_SOURCE
#include "astra_font_host.h"
#include "astra_font_convert.h"

#include <astra/font_worker.h>

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FONT_WORKER_TIMEOUT_MS 60000u

static uint32_t decode32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
           (uint32_t)bytes[2] << 8 | bytes[3];
}

static void encode32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static int64_t now_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int exact(int socket, void *buffer, uint32_t size, int output,
                 int64_t deadline)
{
    uint8_t *bytes = buffer;
    while (size != 0u) {
        int64_t current = now_ms();
        int64_t remaining = deadline - current;
        struct pollfd wait = {socket, output ? POLLOUT : POLLIN, 0};
        ssize_t moved;
        int ready;
        if (current < 0) return -EIO;
        if (remaining <= 0) return -ETIMEDOUT;
        ready = poll(&wait, 1u, (int)remaining);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) return -errno;
        if (ready == 0) return -ETIMEDOUT;
        moved = output ? send(socket, bytes, size,
                              MSG_DONTWAIT | MSG_NOSIGNAL) :
                         recv(socket, bytes, size, MSG_DONTWAIT);
        if (moved < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (moved < 0) return -errno;
        if (moved == 0) return -EPIPE;
        bytes += moved;
        size -= (uint32_t)moved;
    }
    return 0;
}

static int reap(pid_t child, int64_t deadline, int terminate)
{
    int status;
    if (terminate) (void)kill(child, SIGKILL);
    for (;;) {
        pid_t finished = waitpid(child, &status, terminate ? 0 : WNOHANG);
        int64_t current;
        if (finished == child)
            return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -EIO;
        if (finished < 0 && errno == EINTR) continue;
        if (finished < 0) return -errno;
        current = now_ms();
        if (current < 0 || current >= deadline) {
            (void)kill(child, SIGKILL);
            terminate = 1;
            continue;
        }
        struct timespec pause = {0, 1000000};
        (void)nanosleep(&pause, NULL);
    }
}

int astra_font_host_convert(const char *worker, const void *source,
    uint32_t source_size, uint32_t face_index, const char *name,
    uint32_t name_size, const char *license, uint32_t license_size,
    AstraFontHostResult *result)
{
    uint8_t request[ASTRA_FONT_WORKER_REQUEST_SIZE] = {0};
    uint8_t reply[ASTRA_FONT_WORKER_REPLY_SIZE];
    posix_spawn_file_actions_t actions;
    char *const arguments[] = {(char *)worker, NULL};
    char *const environment[] = {"PATH=/usr/bin:/bin", "LANG=C", NULL};
    int sockets[2] = {-1, -1};
    int error = -EINVAL;
    int child_result;
    pid_t child;
    int64_t deadline;

    if (result == NULL || worker == NULL || worker[0] != '/' ||
        source == NULL || source_size == 0u ||
        source_size > ASTRA_FONT_WORKER_SOURCE_MAX ||
        face_index >= ASTRA_FONT_WORKER_FACES_MAX ||
        name == NULL || name_size == 0u ||
        name_size > ASTRA_FONT_WORKER_TEXT_MAX ||
        license == NULL || license_size > ASTRA_FONT_WORKER_TEXT_MAX ||
        memchr(name, '\0', name_size) != NULL ||
        memchr(license, '\0', license_size) != NULL)
        return error;
    *result = (AstraFontHostResult){0};
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0)
        return -errno;
    error = posix_spawn_file_actions_init(&actions);
    if (error != 0) {
        error = -error;
        goto close_sockets;
    }
    if ((error = posix_spawn_file_actions_adddup2(&actions, sockets[1],
                                                  STDIN_FILENO)) != 0 ||
        (error = posix_spawn_file_actions_adddup2(&actions, sockets[1],
                                                  STDOUT_FILENO)) != 0 ||
        (error = posix_spawn_file_actions_addclosefrom_np(&actions, 3)) != 0)
        goto close_actions;
    error = posix_spawn(&child, worker, &actions, NULL, arguments, environment);
close_actions:
    (void)posix_spawn_file_actions_destroy(&actions);
    if (error != 0) {
        error = -error;
        goto close_sockets;
    }
    close(sockets[1]);
    sockets[1] = -1;
    deadline = now_ms();
    if (deadline < 0) {
        error = -EIO;
        close(sockets[0]);
        sockets[0] = -1;
        (void)reap(child, 0, 1);
        goto close_sockets;
    }
    deadline += FONT_WORKER_TIMEOUT_MS;
    memcpy(request, ASTRA_FONT_WORKER_REQUEST_MAGIC, 4u);
    encode32(request + 4u, ASTRA_FONT_WORKER_VERSION);
    encode32(request + 8u, face_index);
    encode32(request + 12u, source_size);
    encode32(request + 16u, name_size);
    encode32(request + 20u, license_size);
    error = exact(sockets[0], request, sizeof(request), 1, deadline);
    if (error == 0) error = exact(sockets[0], (void *)name, name_size, 1, deadline);
    if (error == 0) error = exact(sockets[0], (void *)license, license_size, 1, deadline);
    if (error == 0) error = exact(sockets[0], (void *)source, source_size, 1, deadline);
    if (error == 0 && shutdown(sockets[0], SHUT_WR) != 0) error = -errno;
    if (error == 0) error = exact(sockets[0], reply, sizeof(reply), 0, deadline);
    if (error == 0 &&
        (memcmp(reply, ASTRA_FONT_WORKER_REPLY_MAGIC, 4u) != 0 ||
         decode32(reply + 4u) != ASTRA_FONT_WORKER_VERSION ||
         decode32(reply + 8u) > ASTRA_FONT_CONVERT_NOMEM ||
         decode32(reply + 12u) > ASTRA_FONT_WORKER_FACES_MAX ||
         (decode32(reply + 8u) == ASTRA_FONT_CONVERT_OK &&
          (decode32(reply + 12u) <= face_index ||
           decode32(reply + 16u) == 0u ||
           decode32(reply + 16u) > ASTRA_FONT_WORKER_OUTPUT_MAX)) ||
         (decode32(reply + 8u) != ASTRA_FONT_CONVERT_OK &&
          decode32(reply + 16u) != 0u)))
        error = -EPROTO;
    if (error == 0) {
        result->status = decode32(reply + 8u);
        result->faces = decode32(reply + 12u);
        result->afnt_size = decode32(reply + 16u);
        if (result->status == ASTRA_FONT_CONVERT_OK) {
            result->afnt = malloc(result->afnt_size);
            error = result->afnt == NULL ? -ENOMEM :
                exact(sockets[0], result->afnt, result->afnt_size, 0,
                      deadline);
        }
    }
    close(sockets[0]);
    sockets[0] = -1;
    child_result = reap(child, deadline, error != 0);
    if (error == 0 && child_result != 0) error = child_result;
    if (error != 0) {
        free(result->afnt);
        *result = (AstraFontHostResult){0};
    }
close_sockets:
    if (sockets[0] >= 0) close(sockets[0]);
    if (sockets[1] >= 0) close(sockets[1]);
    return error;
}
