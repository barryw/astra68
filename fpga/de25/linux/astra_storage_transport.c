#define _GNU_SOURCE
#include "astra_storage_transport.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

static void close_fds(int *fds, unsigned count)
{
    for (unsigned at = 0; at < count; ++at)
        if (fds[at] >= 0) {
            close(fds[at]);
            fds[at] = -1;
        }
}

int astra_storage_send_packet(int socket, const void *packet, size_t size,
                               const int *fds, unsigned count)
{
    struct iovec vector = {.iov_base = (void *)packet, .iov_len = size};
    char control[CMSG_SPACE(sizeof(int) * ASTRA_STORAGE_PROVIDER_FDS)] = {0};
    struct msghdr message = {.msg_iov = &vector, .msg_iovlen = 1};
    ssize_t sent;

    if (count > ASTRA_STORAGE_PROVIDER_FDS || (count != 0u && fds == NULL))
        return EINVAL;

    if (count != 0u) {
        struct cmsghdr *header;
        message.msg_control = control;
        message.msg_controllen = CMSG_SPACE(count * sizeof(int));
        header = CMSG_FIRSTHDR(&message);
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        header->cmsg_len = CMSG_LEN(count * sizeof(int));
        memcpy(CMSG_DATA(header), fds, count * sizeof(int));
    }
    do {
        sent = sendmsg(socket, &message, MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);
    return sent < 0 ? errno : (size_t)sent == size ? 0 : EPROTO;
}

int astra_storage_receive_packet(int socket, void *packet, size_t size,
                                  int fds[ASTRA_STORAGE_PROVIDER_FDS], unsigned *count,
                                  struct ucred *credentials)
{
    struct iovec vector = {.iov_base = packet, .iov_len = size};
    char control[CMSG_SPACE(sizeof(int) * ASTRA_STORAGE_PROVIDER_FDS) +
                 CMSG_SPACE(sizeof(struct ucred))] = {0};
    struct msghdr message = {.msg_iov = &vector, .msg_iovlen = 1,
        .msg_control = control, .msg_controllen = sizeof(control)};
    bool have_credentials = false;
    int error = 0;
    ssize_t got;

    *count = 0;
    for (unsigned at = 0; at < ASTRA_STORAGE_PROVIDER_FDS; ++at)
        fds[at] = -1;
    do {
        got = recvmsg(socket, &message, MSG_CMSG_CLOEXEC);
    } while (got < 0 && errno == EINTR);
    if (got <= 0)
        return got < 0 ? errno : EPIPE;
    for (struct cmsghdr *header = CMSG_FIRSTHDR(&message); header != NULL;
         header = CMSG_NXTHDR(&message, header)) {
        if (header->cmsg_level != SOL_SOCKET || header->cmsg_len < CMSG_LEN(0)) {
            error = EPROTO;
            continue;
        }
        if (header->cmsg_type == SCM_RIGHTS) {
            size_t bytes = header->cmsg_len - CMSG_LEN(0);
            const int *received = (const int *)(const void *)CMSG_DATA(header);
            if (bytes % sizeof(int) != 0u)
                error = EPROTO;
            for (size_t at = 0; at < bytes / sizeof(int); ++at) {
                if (*count < ASTRA_STORAGE_PROVIDER_FDS)
                    fds[(*count)++] = received[at];
                else {
                    close(received[at]);
                    error = EPROTO;
                }
            }
        } else if (header->cmsg_type == SCM_CREDENTIALS && credentials != NULL &&
                   header->cmsg_len == CMSG_LEN(sizeof(*credentials)) &&
                   !have_credentials) {
            memcpy(credentials, CMSG_DATA(header), sizeof(*credentials));
            have_credentials = true;
        } else {
            error = EPROTO;
        }
    }
    if ((size_t)got != size || message.msg_flags & (MSG_TRUNC | MSG_CTRUNC) ||
        (credentials != NULL && !have_credentials))
        error = EPROTO;
    if (error != 0)
        close_fds(fds, *count);
    return error;
}

int astra_storage_path_call(int socket, const AstraStoragePathRequest *request,
                             const int fds[ASTRA_STORAGE_PROVIDER_FDS],
                             AstraStoragePathReply *reply, int *result)
{
    int packed[ASTRA_STORAGE_PROVIDER_FDS];
    unsigned count = 0;
    unsigned received = 0;
    int error;

    *result = -1;
    if (request->fd_mask & ~7u)
        return EINVAL;
    for (unsigned at = 0; at < ASTRA_STORAGE_PROVIDER_FDS; ++at)
        if (request->fd_mask & (1u << at))
            packed[count++] = fds[at];
    error = astra_storage_send_packet(socket, request, sizeof(*request), packed, count);
    if (error != 0)
        return error;
    error = astra_storage_receive_packet(socket, reply, sizeof(*reply), packed, &received, NULL);
    if (error != 0)
        return error;
    if (reply->version != ASTRA_STORAGE_PROVIDER_VERSION ||
        reply->serial != request->serial || reply->error < 0 ||
        reply->epoch != request->epoch ||
        reply->error > 4095 ||
        reply->length > sizeof(reply->data) || received > 1u ||
        (reply->error != 0 && received != 0u) ||
        (reply->error == 0 && received !=
            (unsigned)(request->operation == ASTRA_STORAGE_PATH_OPEN ||
                       request->operation == ASTRA_STORAGE_PATH_RESOLVE)) ||
        reply->leaf[sizeof(reply->leaf) - 1u] != '\0' ||
        reply->continuation[sizeof(reply->continuation) - 1u] != '\0') {
        close_fds(packed, received);
        return EPROTO;
    }
    if (received != 0u)
        *result = packed[0];
    return 0;
}

int astra_storage_control_call(int socket, const AstraStorageControlRequest *request,
                                int source, AstraStorageControlReply *reply, int *root)
{
    int fds[ASTRA_STORAGE_PROVIDER_FDS];
    unsigned count = 0u;
    int error;
    *root = -1;
    error = astra_storage_send_packet(socket, request, sizeof(*request), &source,
                                       source < 0 ? 0u : 1u);
    if (error != 0)
        return error;
    error = astra_storage_receive_packet(socket, reply, sizeof(*reply), fds, &count, NULL);
    if (error != 0)
        return error;
    if (reply->version != ASTRA_STORAGE_PROVIDER_VERSION || reply->error < 0 ||
        reply->error > 4095 || reply->serial != request->serial || reply->reserved != 0u ||
        reply->epoch == 0u || (request->epoch != 0u && reply->epoch != request->epoch &&
                               reply->error != ESTALE) ||
        reply->count > ASTRA_STORAGE_PROVIDER_PAGE ||
        count != (unsigned)((request->operation == ASTRA_STORAGE_CONTROL_ROOT ||
            request->operation == ASTRA_STORAGE_CONTROL_BACKING) && reply->error == 0)) {
        for (unsigned at = 0u; at < count; ++at)
            close(fds[at]);
        return EPROTO;
    }
    if (reply->error == 0 && (request->operation == ASTRA_STORAGE_CONTROL_MOUNT ||
        request->operation == ASTRA_STORAGE_CONTROL_IMAGE) && (reply->count != 1u ||
            reply->object == 0u || reply->records[0].id != reply->object ||
            reply->records[0].generation == 0u)) {
        for (unsigned at = 0u; at < count; ++at) close(fds[at]);
        return EPROTO;
    }
    if (count != 0u)
        *root = fds[0];
    return 0;
}
