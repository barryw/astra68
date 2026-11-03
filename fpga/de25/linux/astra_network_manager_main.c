#define _GNU_SOURCE
#include <astra/network_control_host.h>
#include "astra_network_nm.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    AstraNetworkNm network = {0};
    AstraNetworkControlMessage message;
    struct ucred credential;
    socklen_t length = sizeof(credential);
    char *end;
    long parsed, allowed_uid, allowed_pid;
    int fd, type;

    if (argc != 4 || geteuid() != 0 || argv[1][0] == '\0' ||
        argv[2][0] == '\0' || argv[3][0] == '\0')
        return 2;
    errno = 0;
    parsed = strtol(argv[1], &end, 10);
    if (errno != 0 || *end != '\0' || parsed < 3 || parsed > INT_MAX)
        return 2;
    fd = (int)parsed;
    errno = 0;
    allowed_uid = strtol(argv[2], &end, 10);
    if (errno != 0 || *end != '\0' || allowed_uid <= 0 ||
        allowed_uid > UINT32_MAX)
        return 2;
    errno = 0;
    allowed_pid = strtol(argv[3], &end, 10);
    if (errno != 0 || *end != '\0' || allowed_pid <= 0 ||
        allowed_pid > INT_MAX)
        return 2;
    length = sizeof(type);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &length) != 0 ||
        type != SOCK_SEQPACKET || astra_network_nm_open(&network) != 0)
        return 1;
    for (;;) {
        char control[CMSG_SPACE(sizeof(credential))];
        struct iovec iov = {&message, sizeof(message)};
        struct msghdr header = {.msg_iov = &iov, .msg_iovlen = 1u,
            .msg_control = control, .msg_controllen = sizeof(control)};
        ssize_t moved = recvmsg(fd, &header, 0);
        if (moved == 0)
            break;
        if (moved < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (moved != sizeof(message) || (header.msg_flags & MSG_TRUNC) != 0)
            break;
        struct cmsghdr *item = CMSG_FIRSTHDR(&header);
        if (item == NULL || item->cmsg_level != SOL_SOCKET ||
            item->cmsg_type != SCM_CREDENTIALS ||
            item->cmsg_len != CMSG_LEN(sizeof(credential)))
            break;
        __builtin_memcpy(&credential, CMSG_DATA(item), sizeof(credential));
        if (credential.uid != (uid_t)allowed_uid ||
            credential.pid != (pid_t)allowed_pid)
            break;
        int result = EINVAL;
        if (message.version == ASTRA_NETWORK_CONTROL_VERSION &&
            message.error == 0u) {
            switch (message.operation) {
            case ASTRA_NETWORK_CONTROL_PROBE:
                result = astra_network_nm_probe(&network, message.profile.name);
                break;
            case ASTRA_NETWORK_CONTROL_STAGE:
                result = astra_network_nm_stage(&network, &message.profile,
                                                 message.rollback_seconds);
                break;
            case ASTRA_NETWORK_CONTROL_CONFIRM:
                result = astra_network_nm_confirm(&network);
                break;
            case ASTRA_NETWORK_CONTROL_REVERT:
                result = astra_network_nm_revert(&network);
                break;
            }
        }
        message.error = (uint32_t)result;
        if (send(fd, &message, sizeof(message), MSG_NOSIGNAL) != sizeof(message))
            break;
    }
    astra_network_nm_close(&network);
    close(fd);
    return 0;
}
