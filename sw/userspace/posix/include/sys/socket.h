#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef uint16_t sa_family_t;
typedef uint32_t socklen_t;

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

struct sockaddr_storage {
    sa_family_t ss_family;
    uint8_t __storage[126];
};

struct iovec {
    void *iov_base;
    size_t iov_len;
};

struct msghdr {
    void *msg_name;
    socklen_t msg_namelen;
    struct iovec *msg_iov;
    size_t msg_iovlen;
    void *msg_control;
    size_t msg_controllen;
    int msg_flags;
};

#define AF_UNSPEC 0
#define AF_INET 2
#define AF_INET6 10
#define PF_UNSPEC AF_UNSPEC
#define PF_INET AF_INET
#define PF_INET6 AF_INET6

#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_NONBLOCK 0x00000800
#define SOCK_CLOEXEC 0x00080000

#define SOL_SOCKET 0xffff
#define SO_REUSEADDR 0x0004
#define SO_ERROR 0x1007
#define SO_TYPE 0x1008
#define SO_SNDBUF 0x1001
#define SO_RCVBUF 0x1002
#define SO_KEEPALIVE 0x0008

#define MSG_PEEK 0x0002
#define MSG_TRUNC 0x0020
#define MSG_DONTWAIT 0x0040
#define MSG_WAITALL 0x0100
#define MSG_NOSIGNAL 0x4000

#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2

int socket(int domain, int type, int protocol);
int bind(int socket, const struct sockaddr *address, socklen_t address_len);
int connect(int socket, const struct sockaddr *address, socklen_t address_len);
int listen(int socket, int backlog);
int accept(int socket, struct sockaddr *address, socklen_t *address_len);
int shutdown(int socket, int how);
int getsockname(int socket, struct sockaddr *address, socklen_t *address_len);
int getpeername(int socket, struct sockaddr *address, socklen_t *address_len);
int getsockopt(int socket, int level, int option, void *value,
               socklen_t *value_len);
int setsockopt(int socket, int level, int option, const void *value,
               socklen_t value_len);
ssize_t send(int socket, const void *buffer, size_t length, int flags);
ssize_t sendto(int socket, const void *buffer, size_t length, int flags,
               const struct sockaddr *address, socklen_t address_len);
ssize_t recv(int socket, void *buffer, size_t length, int flags);
ssize_t recvfrom(int socket, void *buffer, size_t length, int flags,
                 struct sockaddr *address, socklen_t *address_len);
ssize_t sendmsg(int socket, const struct msghdr *message, int flags);
ssize_t recvmsg(int socket, struct msghdr *message, int flags);

#endif
