#define _POSIX_C_SOURCE 200809L
#include "astra_codec_wire.h"

#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>

int64_t astra_codec_wire_now_ms(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int astra_codec_wire_transfer(int socket, void *buffer, size_t bytes,
                              int output, int64_t deadline_ms)
{
    uint8_t *cursor = buffer;

    while (bytes != 0) {
        struct pollfd wait = {socket, output ? POLLOUT : POLLIN, 0};
        int timeout = -1;
        int ready;
        ssize_t moved;

        if (deadline_ms >= 0) {
            int64_t now = astra_codec_wire_now_ms();
            int64_t left = deadline_ms - now;

            if (now < 0)
                return -EIO;
            if (left <= 0)
                return -ETIMEDOUT;
            timeout = left > INT32_MAX ? INT32_MAX : (int)left;
        }
        ready = poll(&wait, 1, timeout);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready < 0)
            return -errno;
        if (ready == 0)
            return -ETIMEDOUT;
        moved = output ? send(socket, cursor, bytes,
                              MSG_DONTWAIT | MSG_NOSIGNAL) :
                         recv(socket, cursor, bytes, MSG_DONTWAIT);
        if (moved < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (moved < 0)
            return -errno;
        if (moved == 0)
            return -EPIPE;
        cursor += moved;
        bytes -= (size_t)moved;
    }
    return 0;
}

int astra_codec_wire_message(int socket, AstraCodecWorkerMessage *message,
                             int output, int64_t deadline_ms)
{
    int error;

    if (output) {
        message->magic = ASTRA_CODEC_WORKER_MAGIC;
        message->version = ASTRA_CODEC_WORKER_VERSION;
    }
    error = astra_codec_wire_transfer(socket, message, sizeof(*message),
                                      output, deadline_ms);
    if (error == 0 && !output &&
        (message->magic != ASTRA_CODEC_WORKER_MAGIC ||
         message->version != ASTRA_CODEC_WORKER_VERSION))
        return -EPROTO;
    return error;
}
