#include <astra/network_kit.h>
#include <astra/posix_descriptor.h>
#include <astra/runtime.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct PosixSocket {
    uint32_t active;
    uint16_t family;
    uint8_t type;
    uint8_t protocol;
    AstraNetworkEndpoint endpoint;
} PosixSocket;

#define POSIX_SOCKET_EXEC_MAGIC UINT32_C(0x50534f43)
#define POSIX_SOCKET_EXEC_VERSION 1u

typedef struct PosixSocketExecState {
    uint32_t magic;
    uint32_t size;
    uint32_t version;
    uint32_t active;
    AstraNetworkSessionState session;
} PosixSocketExecState;

static PosixSocket *sockets;
static uint32_t socket_capacity;
static AstraLibraryHandle *network_handle;
static const AstraNetworkLibraryV1 *network_library;
static AstraNetworkSession network_session = ASTRA_NETWORK_SESSION_INIT;

const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
const struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;

static int network_errno(AstraNetworkStatus status)
{
    switch (status) {
    case ASTRA_NETWORK_OK: return 0;
    case ASTRA_NETWORK_WOULD_BLOCK: return EAGAIN;
    case ASTRA_NETWORK_IN_PROGRESS: return EINPROGRESS;
    case ASTRA_NETWORK_CANCELLED: return EINTR;
    case ASTRA_NETWORK_TIMED_OUT: return ETIMEDOUT;
    case ASTRA_NETWORK_REFUSED: return ECONNREFUSED;
    case ASTRA_NETWORK_RESET: return ECONNRESET;
    case ASTRA_NETWORK_UNREACHABLE: return EHOSTUNREACH;
    case ASTRA_NETWORK_ADDRESS_IN_USE: return EADDRINUSE;
    case ASTRA_NETWORK_ADDRESS_NOT_AVAILABLE: return EADDRNOTAVAIL;
    case ASTRA_NETWORK_PEER_CLOSED: return 0;
    case ASTRA_NETWORK_INVALID: return EINVAL;
    case ASTRA_NETWORK_ACCESS: return EACCES;
    case ASTRA_NETWORK_RESOURCE_LIMIT: return ENOBUFS;
    case ASTRA_NETWORK_OUT_OF_MEMORY: return ENOMEM;
    case ASTRA_NETWORK_UNSUPPORTED: return EOPNOTSUPP;
    case ASTRA_NETWORK_PEER_DEAD: return EPIPE;
    case ASTRA_NETWORK_BUFFER_TOO_SMALL: return ENOBUFS;
    case ASTRA_NETWORK_FORKED: return ENOTSUP;
    default: return EIO;
    }
}

static int network_gai_error(const char *operation, AstraNetworkStatus status)
{
    switch (status) {
    case ASTRA_NETWORK_NAME_NOT_FOUND: return EAI_NONAME;
    case ASTRA_NETWORK_NAME_TEMPORARY:
    case ASTRA_NETWORK_TIMED_OUT: return EAI_AGAIN;
    case ASTRA_NETWORK_OUT_OF_MEMORY:
    case ASTRA_NETWORK_RESOURCE_LIMIT: return EAI_MEMORY;
    default:
        errno = network_errno(status);
        (void)astra_log_failure(operation, status);
        return EAI_SYSTEM;
    }
}

static int network_library_open(void)
{
    if (network_library != NULL)
        return 1;
    network_handle = OpenLibrary(ASTRA_NETWORK_LIBRARY_NAME,
                                 ASTRA_NETWORK_LIBRARY_VERSION);
    if (network_handle == NULL || network_handle->exports == NULL) {
        errno = ENOSYS;
        return 0;
    }
    network_library = network_handle->exports;
    if (network_library->abi_major != ASTRA_NETWORK_LIBRARY_ABI_MAJOR ||
        network_library->structure_size < sizeof(*network_library)) {
        CloseLibrary(network_handle);
        network_handle = NULL;
        network_library = NULL;
        errno = ENOSYS;
        return 0;
    }
    return 1;
}

static int network_open(void)
{
    const AstraStartupInfo *startup;
    const AstraStartupCapability *capability;
    AstraNetworkStatus status;

    if (network_session._private_id != 0u)
        return 1;
    if (!network_library_open())
        return 0;
    startup = astra_posix_startup();
    capability = astra_startup_capability(
        startup, ASTRA_CAPABILITY_NETWORK_LISTEN);
    if (capability == NULL)
        capability = astra_startup_capability(startup,
                                               ASTRA_CAPABILITY_NETWORK);
    if (capability == NULL) {
        errno = EACCES;
        return 0;
    }
    status = network_library->session_open(capability->handle,
                                            &network_session);
    if (status != ASTRA_NETWORK_OK) {
        errno = network_errno(status);
        CloseLibrary(network_handle);
        network_handle = NULL;
        network_library = NULL;
        return 0;
    }
    return 1;
}

static int grow_sockets(uint32_t wanted)
{
    uint32_t capacity = socket_capacity == 0u ? 4u : socket_capacity;
    PosixSocket *grown;

    while (capacity < wanted) {
        if (capacity > UINT32_MAX / 2u) {
            errno = EMFILE;
            return 0;
        }
        capacity *= 2u;
    }
    grown = realloc(sockets, (size_t)capacity * sizeof(*grown));
    if (grown == NULL) {
        errno = ENOMEM;
        return 0;
    }
    (void)memset(grown + socket_capacity, 0,
                 (size_t)(capacity - socket_capacity) * sizeof(*grown));
    sockets = grown;
    socket_capacity = capacity;
    return 1;
}

static int claim_socket(void)
{
    for (uint32_t index = 0u; index < socket_capacity; ++index)
        if (sockets[index].active == 0u)
            return (int)index;
    if (!grow_sockets(socket_capacity + 1u))
        return -1;
    return (int)(socket_capacity == 4u ? 0u : socket_capacity / 2u);
}

static PosixSocket *socket_for_fd(int fd)
{
    int slot = astra_posix_descriptor_socket_slot(fd);

    if (slot < 0 || (uint32_t)slot >= socket_capacity ||
        sockets[slot].active == 0u) {
        errno = ENOTSOCK;
        return NULL;
    }
    return &sockets[slot];
}

static uint32_t network_message_flags(int flags)
{
    uint32_t result = 0u;

    if ((flags & MSG_PEEK) != 0) result |= ASTRA_NETWORK_MESSAGE_PEEK;
    if ((flags & MSG_WAITALL) != 0) result |= ASTRA_NETWORK_MESSAGE_WAIT_ALL;
    if ((flags & MSG_TRUNC) != 0) result |= ASTRA_NETWORK_MESSAGE_TRUNCATE;
    return result;
}

static int flags_valid(int flags)
{
    return (flags & ~(MSG_PEEK | MSG_WAITALL | MSG_TRUNC | MSG_DONTWAIT |
                      MSG_NOSIGNAL)) == 0;
}

static int wait_ready(PosixSocket *socket, uint32_t wanted)
{
    for (;;) {
        uint32_t ready = network_library->readiness(&socket->endpoint);

        if ((ready & wanted) != 0u)
            return 0;
        if ((ready & ASTRA_NETWORK_READY_PEER_CLOSED) != 0u)
            return 1;
        if ((ready & ASTRA_NETWORK_READY_ERROR) != 0u) {
            uint32_t error = ASTRA_NETWORK_IO;

            (void)network_library->get_option(
                &socket->endpoint, ASTRA_NETWORK_OPTION_ERROR, &error);
            errno = network_errno((AstraNetworkStatus)error);
            return -1;
        }
        {
            uint32_t status = astra_wait_one(
                network_library->readiness_handle(&socket->endpoint),
                ASTRA_DEADLINE_FOREVER, NULL);

            if (status != ASTRA_SYSCALL_OK) {
                errno = status == ASTRA_SYSCALL_CANCELLED ? EINTR : EPIPE;
                return -1;
            }
        }
    }
}

static int to_network_address(const struct sockaddr *source, socklen_t length,
                              AstraNetworkAddress *target)
{
    (void)memset(target, 0, sizeof(*target));
    target->size = sizeof(*target);
    if (source == NULL) {
        errno = EFAULT;
        return 0;
    }
    if (source->sa_family == AF_INET && length >= sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *address = (const struct sockaddr_in *)source;

        target->family = ASTRA_NETWORK_FAMILY_IPV4;
        target->port = ntohs(address->sin_port);
        (void)memcpy(target->address, &address->sin_addr,
                     sizeof(address->sin_addr));
        return 1;
    }
    if (source->sa_family == AF_INET6 &&
        length >= sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *address =
            (const struct sockaddr_in6 *)source;

        target->family = ASTRA_NETWORK_FAMILY_IPV6;
        target->port = ntohs(address->sin6_port);
        target->scope_id = address->sin6_scope_id;
        (void)memcpy(target->address, &address->sin6_addr,
                     sizeof(address->sin6_addr));
        return 1;
    }
    errno = EAFNOSUPPORT;
    return 0;
}

#if defined(__GNUC__) && !defined(__clang__)
/* GCC's analyzer loses the full-byte initialization when the aligned union is
 * subsequently written through a sockaddr member. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-use-of-uninitialized-value"
#endif
static int from_network_address(const AstraNetworkAddress *source,
                                struct sockaddr *target, socklen_t *length)
{
    union {
        struct sockaddr base;
        struct sockaddr_in ipv4;
        struct sockaddr_in6 ipv6;
    } storage;
    uint32_t needed;

    if (length == NULL) {
        errno = EFAULT;
        return 0;
    }
    (void)__builtin_memset(&storage, 0, sizeof(storage));
    if (source->family == ASTRA_NETWORK_FAMILY_IPV4) {
        struct sockaddr_in *address = &storage.ipv4;

        needed = sizeof(*address);
        address->sin_family = AF_INET;
        address->sin_port = htons(source->port);
        (void)memcpy(&address->sin_addr, source->address,
                     sizeof(address->sin_addr));
    } else if (source->family == ASTRA_NETWORK_FAMILY_IPV6) {
        struct sockaddr_in6 *address = &storage.ipv6;

        needed = sizeof(*address);
        address->sin6_family = AF_INET6;
        address->sin6_port = htons(source->port);
        address->sin6_scope_id = source->scope_id;
        (void)memcpy(&address->sin6_addr, source->address,
                     sizeof(address->sin6_addr));
    } else {
        errno = EAFNOSUPPORT;
        return 0;
    }
    if (target != NULL && *length != 0u)
        (void)memcpy(target, &storage,
                     *length < needed ? *length : needed);
    *length = needed;
    return 1;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

static ssize_t socket_send(PosixSocket *socket, const void *buffer,
                           size_t length, int flags,
                           const AstraNetworkAddress *address,
                           int status_flags)
{
    size_t moved = 0u;
    AstraNetworkStatus status;

    if (!flags_valid(flags)) {
        errno = EOPNOTSUPP;
        return -1;
    }
    for (;;) {
        status = address == NULL ?
            network_library->send(&socket->endpoint, buffer, length,
                                  network_message_flags(flags), &moved) :
            network_library->send_to(&socket->endpoint, buffer, length,
                                     network_message_flags(flags), address,
                                     &moved);
        if (status == ASTRA_NETWORK_OK)
            return (ssize_t)moved;
        if (status != ASTRA_NETWORK_WOULD_BLOCK) {
            (void)astra_log_failure("POSIX socket send", status);
            errno = network_errno(status);
            return -1;
        }
        if ((status_flags & O_NONBLOCK) != 0 ||
            (flags & MSG_DONTWAIT) != 0) {
            errno = EAGAIN;
            return -1;
        }
        if (wait_ready(socket, ASTRA_NETWORK_READY_WRITABLE |
                               ASTRA_NETWORK_READY_CONNECTED) < 0)
            return -1;
    }
}

static ssize_t socket_receive(PosixSocket *socket, void *buffer,
                              size_t length, int flags,
                              AstraNetworkAddress *address,
                              int status_flags)
{
    size_t moved = 0u;
    AstraNetworkStatus status;

    if (!flags_valid(flags)) {
        errno = EOPNOTSUPP;
        return -1;
    }
    for (;;) {
        status = address == NULL ?
            network_library->receive(&socket->endpoint, buffer, length,
                                     network_message_flags(flags), &moved) :
            network_library->receive_from(
                &socket->endpoint, buffer, length, network_message_flags(flags),
                address, &moved);
        if (status == ASTRA_NETWORK_OK)
            return (ssize_t)moved;
        if (status == ASTRA_NETWORK_PEER_CLOSED)
            return 0;
        if (status != ASTRA_NETWORK_WOULD_BLOCK) {
            errno = network_errno(status);
            return -1;
        }
        if ((status_flags & O_NONBLOCK) != 0 ||
            (flags & MSG_DONTWAIT) != 0) {
            errno = EAGAIN;
            return -1;
        }
        {
            int ready = wait_ready(socket, ASTRA_NETWORK_READY_READABLE);

            if (ready < 0)
                return -1;
            if (ready > 0)
                return 0;
        }
    }
}

static ssize_t descriptor_socket_read(uint32_t slot, void *bytes,
                                      size_t length, int flags)
{
    if (slot >= socket_capacity || sockets[slot].active == 0u) {
        errno = EBADF;
        return -1;
    }
    return socket_receive(&sockets[slot], bytes, length, 0, NULL, flags);
}

static ssize_t descriptor_socket_write(uint32_t slot, const void *bytes,
                                       size_t length, int flags)
{
    if (slot >= socket_capacity || sockets[slot].active == 0u) {
        errno = EBADF;
        return -1;
    }
    return socket_send(&sockets[slot], bytes, length, MSG_NOSIGNAL, NULL,
                       flags);
}

static int descriptor_socket_close(uint32_t slot)
{
    AstraNetworkStatus status;

    if (slot >= socket_capacity || sockets[slot].active == 0u) {
        errno = EBADF;
        return -1;
    }
    status = network_library->endpoint_close(&sockets[slot].endpoint);
    (void)memset(&sockets[slot], 0, sizeof(sockets[slot]));
    if (status != ASTRA_NETWORK_OK) {
        errno = network_errno(status);
        return -1;
    }
    return 0;
}

static int descriptor_socket_poll(uint32_t slot, short events,
                                  short *revents, uint32_t handles[2],
                                  uint32_t *count)
{
    uint32_t ready;

    if (slot >= socket_capacity || sockets[slot].active == 0u) {
        *revents = POLLNVAL;
        return 0;
    }
    ready = network_library->readiness(&sockets[slot].endpoint);
    if ((events & POLLIN) != 0 &&
        (ready & (ASTRA_NETWORK_READY_READABLE |
                  ASTRA_NETWORK_READY_ACCEPTABLE)) != 0u)
        *revents |= POLLIN;
    if ((events & POLLOUT) != 0 &&
        (ready & (ASTRA_NETWORK_READY_WRITABLE |
                  ASTRA_NETWORK_READY_CONNECTED)) != 0u)
        *revents |= POLLOUT;
    if ((ready & ASTRA_NETWORK_READY_PEER_CLOSED) != 0u)
        *revents |= POLLHUP;
    if ((ready & ASTRA_NETWORK_READY_ERROR) != 0u)
        *revents |= POLLERR;
    if (*revents == 0) {
        handles[0] = network_library->readiness_handle(&sockets[slot].endpoint);
        *count = 1u;
    }
    return 0;
}

static uint32_t descriptor_socket_exec_size(void)
{
    return sizeof(PosixSocketExecState);
}

static uint32_t descriptor_socket_state_size(void)
{
    return sizeof(AstraNetworkEndpointState);
}

static int descriptor_socket_exec_export(void *state, uint32_t capacity,
                                         uint32_t *used)
{
    PosixSocketExecState *wire = state;

    if (state == NULL || used == NULL || capacity != sizeof(*wire)) {
        errno = EINVAL;
        return -1;
    }
    (void)memset(wire, 0, sizeof(*wire));
    wire->magic = POSIX_SOCKET_EXEC_MAGIC;
    wire->size = sizeof(*wire);
    wire->version = POSIX_SOCKET_EXEC_VERSION;
    wire->active = network_session._private_id != 0u;
    *used = sizeof(*wire);
    if (wire->active != 0u &&
        network_library->session_export(&network_session, &wire->session) !=
            ASTRA_NETWORK_OK) {
        errno = EIO;
        return -1;
    }
    return 0;
}

static int descriptor_socket_exec_import(const void *state, uint32_t size)
{
    const PosixSocketExecState *wire = state;
    AstraNetworkStatus status;

    if (state == NULL || size != sizeof(*wire) ||
        wire->magic != POSIX_SOCKET_EXEC_MAGIC || wire->size != sizeof(*wire) ||
        wire->version != POSIX_SOCKET_EXEC_VERSION || wire->active > 1u) {
        errno = EINVAL;
        return -1;
    }
    if (wire->active == 0u)
        return 0;
    if (!network_library_open())
        return -1;
    status = network_library->session_import(&wire->session,
                                              &network_session);
    if (status != ASTRA_NETWORK_OK) {
        (void)astra_log_failure("POSIX socket exec session import", status);
        errno = network_errno(status);
        return -1;
    }
    return 0;
}

static int descriptor_socket_state_export(uint32_t slot, void *state,
                                          uint32_t size)
{
    AstraNetworkStatus status;

    if (slot >= socket_capacity || sockets[slot].active == 0u ||
        state == NULL || size != sizeof(AstraNetworkEndpointState)) {
        errno = EBADF;
        return -1;
    }
    status = network_library->endpoint_export(&sockets[slot].endpoint, state);
    if (status != ASTRA_NETWORK_OK) {
        errno = network_errno(status);
        return -1;
    }
    return 0;
}

static int descriptor_socket_state_import(const void *state, uint32_t size,
                                          uint32_t *slot_out)
{
    const AstraNetworkEndpointState *wire = state;
    AstraNetworkStatus status;
    int slot;

    if (state == NULL || size != sizeof(*wire) || slot_out == NULL ||
        network_session._private_id == 0u) {
        errno = EINVAL;
        return -1;
    }
    slot = claim_socket();
    if (slot < 0)
        return -1;
    status = network_library->endpoint_import(
        &network_session, wire, &sockets[slot].endpoint);
    if (status != ASTRA_NETWORK_OK) {
        (void)astra_log_failure("POSIX socket exec endpoint import", status);
        errno = network_errno(status);
        return -1;
    }
    sockets[slot].active = 1u;
    sockets[slot].family = wire->family;
    sockets[slot].type = wire->type;
    sockets[slot].protocol = wire->protocol;
    *slot_out = (uint32_t)slot;
    return 0;
}

static const AstraPosixSocketOps descriptor_socket_ops = {
    descriptor_socket_read, descriptor_socket_write,
    descriptor_socket_close, descriptor_socket_poll,
    descriptor_socket_exec_size, descriptor_socket_state_size,
    descriptor_socket_exec_export, descriptor_socket_exec_import,
    descriptor_socket_state_export, descriptor_socket_state_import
};

void astra_posix_socket_prepare(void)
{
    astra_posix_socket_bind(&descriptor_socket_ops);
}

void astra_posix_socket_after_fork_child(void)
{
    if (network_session._private_id == 0u)
        return;
    (void)network_library->session_close(&network_session);
    (void)network_open();
}

int socket(int domain, int type, int protocol)
{
    AstraNetworkStatus status;
    uint16_t family;
    uint8_t network_type;
    uint8_t network_protocol;
    int flags = O_RDWR;
    int slot;
    int fd;

    if (domain == AF_INET)
        family = ASTRA_NETWORK_FAMILY_IPV4;
    else if (domain == AF_INET6)
        family = ASTRA_NETWORK_FAMILY_IPV6;
    else {
        errno = EAFNOSUPPORT;
        return -1;
    }
    if ((type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC)) == SOCK_STREAM)
        network_type = ASTRA_NETWORK_TYPE_STREAM;
    else if ((type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC)) == SOCK_DGRAM)
        network_type = ASTRA_NETWORK_TYPE_DATAGRAM;
    else {
        errno = ESOCKTNOSUPPORT;
        return -1;
    }
    network_protocol = protocol == 0 ? ASTRA_NETWORK_PROTOCOL_DEFAULT :
        protocol == IPPROTO_ICMP ? ASTRA_NETWORK_PROTOCOL_ICMP :
        protocol == IPPROTO_TCP ? ASTRA_NETWORK_PROTOCOL_TCP :
        protocol == IPPROTO_UDP ? ASTRA_NETWORK_PROTOCOL_UDP :
        protocol == IPPROTO_ICMPV6 ? ASTRA_NETWORK_PROTOCOL_ICMPV6 : 0xffu;
    if (network_protocol == 0xffu) {
        errno = EPROTONOSUPPORT;
        return -1;
    }
    if (!network_open())
        return -1;
    slot = claim_socket();
    if (slot < 0)
        return -1;
    status = network_library->endpoint_open(
        &network_session, family, network_type, network_protocol,
        &sockets[slot].endpoint);
    if (status != ASTRA_NETWORK_OK) {
        (void)astra_log_failure("socket endpoint open", status);
        errno = network_errno(status);
        return -1;
    }
    sockets[slot].active = 1u;
    sockets[slot].family = family;
    sockets[slot].type = network_type;
    sockets[slot].protocol = network_protocol;
    if ((type & SOCK_NONBLOCK) != 0)
        flags |= O_NONBLOCK;
    astra_posix_socket_bind(&descriptor_socket_ops);
    fd = astra_posix_descriptor_socket((uint32_t)slot, flags);
    if (fd < 0) {
        (void)descriptor_socket_close((uint32_t)slot);
        return -1;
    }
    if ((type & SOCK_CLOEXEC) != 0 && fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
        (void)close(fd);
        return -1;
    }
    return fd;
}

int bind(int fd, const struct sockaddr *address, socklen_t length)
{
    PosixSocket *socket = socket_for_fd(fd);
    AstraNetworkAddress native;
    AstraNetworkStatus status;

    if (socket == NULL || !to_network_address(address, length, &native))
        return -1;
    status = network_library->bind(&socket->endpoint, &native);
    if (status != ASTRA_NETWORK_OK) {
        errno = network_errno(status);
        return -1;
    }
    return 0;
}

int connect(int fd, const struct sockaddr *address, socklen_t length)
{
    PosixSocket *socket = socket_for_fd(fd);
    AstraNetworkAddress native;
    AstraNetworkStatus status;
    int flags;

    if (socket == NULL || !to_network_address(address, length, &native))
        return -1;
    status = network_library->connect(&socket->endpoint, &native);
    if (status == ASTRA_NETWORK_OK)
        return 0;
    if (status != ASTRA_NETWORK_IN_PROGRESS) {
        errno = network_errno(status);
        return -1;
    }
    flags = astra_posix_descriptor_flags(fd);
    if ((flags & O_NONBLOCK) != 0) {
        errno = EINPROGRESS;
        return -1;
    }
    if (wait_ready(socket, ASTRA_NETWORK_READY_CONNECTED) < 0)
        return -1;
    {
        uint32_t error = ASTRA_NETWORK_OK;

        status = network_library->get_option(
            &socket->endpoint, ASTRA_NETWORK_OPTION_ERROR, &error);
        if (status != ASTRA_NETWORK_OK || error != ASTRA_NETWORK_OK) {
            errno = status != ASTRA_NETWORK_OK ? network_errno(status) :
                    network_errno((AstraNetworkStatus)error);
            return -1;
        }
    }
    return 0;
}

int listen(int fd, int backlog)
{
    PosixSocket *socket = socket_for_fd(fd);
    AstraNetworkStatus status;

    if (socket == NULL)
        return -1;
    if (backlog < 0)
        backlog = 0;
    status = network_library->listen(&socket->endpoint, (uint32_t)backlog);
    if (status != ASTRA_NETWORK_OK) {
        errno = network_errno(status);
        return -1;
    }
    return 0;
}

int accept(int fd, struct sockaddr *address, socklen_t *length)
{
    PosixSocket *listener = socket_for_fd(fd);
    AstraNetworkEndpoint accepted = ASTRA_NETWORK_ENDPOINT_INIT;
    AstraNetworkAddress native;
    AstraNetworkStatus status;
    int descriptor_flags;
    int slot;
    int result;

    if (listener == NULL || (address != NULL && length == NULL)) {
        if (listener != NULL) errno = EFAULT;
        return -1;
    }
    descriptor_flags = astra_posix_descriptor_flags(fd);
    for (;;) {
        status = network_library->accept(&listener->endpoint, &accepted,
                                         &native);
        if (status == ASTRA_NETWORK_OK)
            break;
        if (status != ASTRA_NETWORK_WOULD_BLOCK) {
            errno = network_errno(status);
            return -1;
        }
        if ((descriptor_flags & O_NONBLOCK) != 0) {
            errno = EAGAIN;
            return -1;
        }
        if (wait_ready(listener, ASTRA_NETWORK_READY_ACCEPTABLE) < 0)
            return -1;
    }
    slot = claim_socket();
    if (slot < 0) {
        (void)network_library->endpoint_close(&accepted);
        return -1;
    }
    sockets[slot].active = 1u;
    sockets[slot].family = listener->family;
    sockets[slot].type = listener->type;
    sockets[slot].protocol = listener->protocol;
    sockets[slot].endpoint = accepted;
    result = astra_posix_descriptor_socket((uint32_t)slot,
                                           descriptor_flags & O_NONBLOCK);
    if (result < 0) {
        (void)descriptor_socket_close((uint32_t)slot);
        return -1;
    }
    if (address != NULL && !from_network_address(&native, address, length)) {
        (void)close(result);
        return -1;
    }
    return result;
}

ssize_t send(int fd, const void *buffer, size_t length, int flags)
{
    PosixSocket *socket = socket_for_fd(fd);
    int status_flags;

    if (socket == NULL) {
        (void)astra_log_failure("POSIX socket lookup", (uint32_t)errno);
        return -1;
    }
    status_flags = astra_posix_descriptor_flags(fd);
    if (status_flags < 0) {
        (void)astra_log_failure("POSIX socket descriptor", (uint32_t)fd);
        return -1;
    }
    return socket_send(socket, buffer, length, flags, NULL, status_flags);
}

ssize_t sendto(int fd, const void *buffer, size_t length, int flags,
               const struct sockaddr *address, socklen_t address_length)
{
    PosixSocket *socket = socket_for_fd(fd);
    AstraNetworkAddress native;
    int status_flags;

    if (socket == NULL ||
        !to_network_address(address, address_length, &native))
        return -1;
    status_flags = astra_posix_descriptor_flags(fd);
    return socket_send(socket, buffer, length, flags, &native, status_flags);
}

ssize_t recv(int fd, void *buffer, size_t length, int flags)
{
    PosixSocket *socket = socket_for_fd(fd);
    int status_flags;

    if (socket == NULL)
        return -1;
    status_flags = astra_posix_descriptor_flags(fd);
    return socket_receive(socket, buffer, length, flags, NULL, status_flags);
}

ssize_t recvfrom(int fd, void *buffer, size_t length, int flags,
                 struct sockaddr *address, socklen_t *address_length)
{
    PosixSocket *socket = socket_for_fd(fd);
    AstraNetworkAddress native;
    ssize_t result;
    int status_flags;

    if (socket == NULL || (address != NULL && address_length == NULL)) {
        if (socket != NULL) errno = EFAULT;
        return -1;
    }
    status_flags = astra_posix_descriptor_flags(fd);
    result = socket_receive(socket, buffer, length, flags,
                            address == NULL ? NULL : &native, status_flags);
    if (result >= 0 && address != NULL &&
        !from_network_address(&native, address, address_length))
        return -1;
    return result;
}

static int iovec_size(const struct iovec *iov, size_t count, size_t *total)
{
    size_t result = 0u;

    if (count != 0u && iov == NULL)
        return 0;
    for (size_t index = 0u; index < count; ++index) {
        if (iov[index].iov_len > SIZE_MAX - result)
            return 0;
        result += iov[index].iov_len;
    }
    *total = result;
    return 1;
}

ssize_t sendmsg(int fd, const struct msghdr *message, int flags)
{
    PosixSocket *socket = socket_for_fd(fd);
    AstraNetworkAddress address;
    const AstraNetworkAddress *target = NULL;
    uint8_t *buffer;
    size_t total;
    size_t send_size;
    size_t at = 0u;
    ssize_t result;
    int status_flags;

    if (socket == NULL)
        return -1;
    if (message == NULL || message->msg_control != NULL ||
        message->msg_controllen != 0u ||
        !iovec_size(message->msg_iov, message->msg_iovlen, &total)) {
        errno = EINVAL;
        return -1;
    }
    if (message->msg_name != NULL) {
        if (!to_network_address(message->msg_name, message->msg_namelen,
                                &address))
            return -1;
        target = &address;
    }
    if (socket->type == ASTRA_NETWORK_TYPE_DATAGRAM &&
        total > ASTRA_NETWORK_SLOT_BYTES) {
        errno = EMSGSIZE;
        return -1;
    }
    send_size = total > ASTRA_NETWORK_SLOT_BYTES ?
        ASTRA_NETWORK_SLOT_BYTES : total;
    buffer = send_size == 0u ? NULL : malloc(send_size);
    if (send_size != 0u && buffer == NULL) {
        errno = ENOMEM;
        return -1;
    }
    for (size_t index = 0u;
         index < message->msg_iovlen && at < send_size; ++index) {
        size_t moved = message->msg_iov[index].iov_len;

        if (moved > send_size - at)
            moved = send_size - at;
        if (moved != 0u)
            (void)memcpy(buffer + at, message->msg_iov[index].iov_base,
                         moved);
        at += moved;
    }
    status_flags = astra_posix_descriptor_flags(fd);
    result = socket_send(socket, buffer, send_size, flags, target,
                         status_flags);
    free(buffer);
    return result;
}

ssize_t recvmsg(int fd, struct msghdr *message, int flags)
{
    PosixSocket *socket = socket_for_fd(fd);
    AstraNetworkAddress address;
    uint8_t *buffer;
    size_t total;
    size_t at = 0u;
    ssize_t result;
    int status_flags;

    if (socket == NULL)
        return -1;
    if (message == NULL || message->msg_control != NULL ||
        message->msg_controllen != 0u ||
        !iovec_size(message->msg_iov, message->msg_iovlen, &total)) {
        errno = EINVAL;
        return -1;
    }
    if (total > ASTRA_NETWORK_SLOT_BYTES)
        total = ASTRA_NETWORK_SLOT_BYTES;
    buffer = total == 0u ? NULL : malloc(total);
    if (total != 0u && buffer == NULL) {
        errno = ENOMEM;
        return -1;
    }
    status_flags = astra_posix_descriptor_flags(fd);
    result = socket_receive(socket, buffer, total,
                            socket->type == ASTRA_NETWORK_TYPE_DATAGRAM ?
                                flags | MSG_TRUNC : flags,
                            message->msg_name == NULL ? NULL : &address,
                            status_flags);
    if (result >= 0) {
        size_t copied = (size_t)result > total ? total : (size_t)result;
        size_t remaining = copied;

        for (size_t index = 0u;
             index < message->msg_iovlen && remaining != 0u; ++index) {
            size_t moved = message->msg_iov[index].iov_len < remaining ?
                message->msg_iov[index].iov_len : remaining;

            if (moved != 0u)
                (void)memcpy(message->msg_iov[index].iov_base, buffer + at,
                             moved);
            at += moved;
            remaining -= moved;
        }
        message->msg_flags = (size_t)result > total ? MSG_TRUNC : 0;
        if ((flags & MSG_TRUNC) == 0 && (size_t)result > total)
            result = (ssize_t)total;
        if (message->msg_name != NULL &&
            !from_network_address(&address, message->msg_name,
                                  &message->msg_namelen))
            result = -1;
    }
    free(buffer);
    return result;
}

int shutdown(int fd, int how)
{
    PosixSocket *socket = socket_for_fd(fd);
    uint32_t flags;
    AstraNetworkStatus status;

    if (socket == NULL)
        return -1;
    flags = how == SHUT_RD ? ASTRA_NETWORK_SHUTDOWN_READ :
            how == SHUT_WR ? ASTRA_NETWORK_SHUTDOWN_WRITE :
            how == SHUT_RDWR ? ASTRA_NETWORK_SHUTDOWN_READ |
                                ASTRA_NETWORK_SHUTDOWN_WRITE : 0u;
    if (flags == 0u) {
        errno = EINVAL;
        return -1;
    }
    status = network_library->shutdown(&socket->endpoint, flags);
    if (status != ASTRA_NETWORK_OK) {
        errno = network_errno(status);
        return -1;
    }
    return 0;
}

static int socket_name(int fd, struct sockaddr *address, socklen_t *length,
                       int peer)
{
    PosixSocket *socket = socket_for_fd(fd);
    AstraNetworkAddress native;
    AstraNetworkStatus status;

    if (socket == NULL || address == NULL || length == NULL) {
        if (socket != NULL) errno = EFAULT;
        return -1;
    }
    status = peer ? network_library->peer_address(&socket->endpoint, &native) :
                    network_library->local_address(&socket->endpoint, &native);
    if (status != ASTRA_NETWORK_OK) {
        errno = network_errno(status);
        return -1;
    }
    return from_network_address(&native, address, length) ? 0 : -1;
}

int getsockname(int fd, struct sockaddr *address, socklen_t *length)
{
    return socket_name(fd, address, length, 0);
}

int getpeername(int fd, struct sockaddr *address, socklen_t *length)
{
    return socket_name(fd, address, length, 1);
}

static uint32_t socket_option(int level, int option)
{
    if (level == SOL_SOCKET) {
        if (option == SO_ERROR) return ASTRA_NETWORK_OPTION_ERROR;
        if (option == SO_TYPE) return ASTRA_NETWORK_OPTION_TYPE;
        if (option == SO_REUSEADDR) return ASTRA_NETWORK_OPTION_REUSE_ADDRESS;
        if (option == SO_KEEPALIVE) return ASTRA_NETWORK_OPTION_KEEPALIVE;
        if (option == SO_SNDBUF) return ASTRA_NETWORK_OPTION_SEND_BUFFER;
        if (option == SO_RCVBUF) return ASTRA_NETWORK_OPTION_RECEIVE_BUFFER;
    }
    if (level == IPPROTO_TCP && option == TCP_NODELAY)
        return ASTRA_NETWORK_OPTION_TCP_NO_DELAY;
    if (level == IPPROTO_IPV6 && option == IPV6_V6ONLY)
        return ASTRA_NETWORK_OPTION_IPV6_ONLY;
    return 0u;
}

int getsockopt(int fd, int level, int option, void *value,
               socklen_t *value_length)
{
    PosixSocket *socket = socket_for_fd(fd);
    uint32_t native_option = socket_option(level, option);
    uint32_t result = 0u;
    AstraNetworkStatus status;
    int output;

    if (socket == NULL)
        return -1;
    if (native_option == 0u) {
        errno = ENOPROTOOPT;
        return -1;
    }
    if (value == NULL || value_length == NULL || *value_length < sizeof(int)) {
        errno = EINVAL;
        return -1;
    }
    status = network_library->get_option(&socket->endpoint, native_option,
                                         &result);
    if (status != ASTRA_NETWORK_OK) {
        errno = network_errno(status);
        return -1;
    }
    output = option == SO_ERROR ? network_errno((AstraNetworkStatus)result) :
             option == SO_TYPE ?
                (result == ASTRA_NETWORK_TYPE_STREAM ? SOCK_STREAM :
                 result == ASTRA_NETWORK_TYPE_DATAGRAM ? SOCK_DGRAM : 0) :
             (int)result;
    (void)memcpy(value, &output, sizeof(output));
    *value_length = sizeof(output);
    return 0;
}

int setsockopt(int fd, int level, int option, const void *value,
               socklen_t value_length)
{
    PosixSocket *socket = socket_for_fd(fd);
    uint32_t native_option = socket_option(level, option);
    int input;
    AstraNetworkStatus status;

    if (socket == NULL)
        return -1;
    if (native_option == 0u || option == SO_ERROR || option == SO_TYPE) {
        errno = ENOPROTOOPT;
        return -1;
    }
    if (value == NULL || value_length < sizeof(input)) {
        errno = EINVAL;
        return -1;
    }
    (void)memcpy(&input, value, sizeof(input));
    status = network_library->set_option(&socket->endpoint, native_option,
                                         (uint32_t)input);
    if (status != ASTRA_NETWORK_OK) {
        errno = network_errno(status);
        return -1;
    }
    return 0;
}

static int service_port(const char *service, uint16_t *port)
{
    uint32_t value = 0u;

    if (service == NULL) {
        *port = 0u;
        return 1;
    }
    if (*service == '\0')
        return 0;
    while (*service != '\0') {
        uint32_t digit;

        if (*service < '0' || *service > '9')
            return 0;
        digit = (uint32_t)(*service++ - '0');
        if (value > (UINT16_MAX - digit) / 10u)
            return 0;
        value = value * 10u + digit;
    }
    *port = (uint16_t)value;
    return 1;
}

static struct addrinfo *address_info(const AstraNetworkAddress *address,
                                     int type, int protocol, int flags,
                                     const char *canonname)
{
    struct addrinfo *info;
    socklen_t length;

    info = calloc(1u, sizeof(*info) + sizeof(struct sockaddr_in6));
    if (info == NULL)
        return NULL;
    info->ai_flags = flags;
    info->ai_family = address->family == ASTRA_NETWORK_FAMILY_IPV4 ?
        AF_INET : AF_INET6;
    info->ai_socktype = type;
    info->ai_protocol = protocol;
    info->ai_addr = (struct sockaddr *)(void *)(info + 1);
    length = sizeof(struct sockaddr_in6);
    if (!from_network_address(address, info->ai_addr, &length)) {
        free(info);
        return NULL;
    }
    info->ai_addrlen = length;
    if (canonname != NULL) {
        size_t size = strlen(canonname) + 1u;

        info->ai_canonname = malloc(size);
        if (info->ai_canonname == NULL) {
            free(info);
            return NULL;
        }
        (void)memcpy(info->ai_canonname, canonname, size);
    }
    return info;
}

void freeaddrinfo(struct addrinfo *info)
{
    while (info != NULL) {
        struct addrinfo *next = info->ai_next;

        free(info->ai_canonname);
        free(info);
        info = next;
    }
}

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **result)
{
    AstraNetworkAddress stack_addresses[16];
    AstraNetworkAddress *addresses = stack_addresses;
    AstraNetworkRequest request = ASTRA_NETWORK_REQUEST_INIT;
    struct addrinfo *head = NULL, **tail = &head;
    uint32_t capacity = 16u, count = 0u;
    uint16_t port;
    int family = hints == NULL ? AF_UNSPEC : hints->ai_family;
    int type = hints == NULL ? 0 : hints->ai_socktype;
    int protocol = hints == NULL ? 0 : hints->ai_protocol;
    int flags = hints == NULL ? 0 : hints->ai_flags;
    AstraNetworkStatus status;
    int numeric = 0;

    if (result == NULL)
        return EAI_FAIL;
    *result = NULL;
    if ((flags & ~(AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST |
                   AI_NUMERICSERV)) != 0)
        return EAI_BADFLAGS;
    if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6)
        return EAI_FAMILY;
    if (type != 0 && type != SOCK_STREAM && type != SOCK_DGRAM)
        return EAI_SOCKTYPE;
    if ((protocol != 0 && protocol != IPPROTO_TCP && protocol != IPPROTO_UDP &&
         protocol != IPPROTO_ICMP && protocol != IPPROTO_ICMPV6) ||
        (type == SOCK_STREAM && protocol == IPPROTO_UDP) ||
        (type == SOCK_DGRAM && protocol == IPPROTO_TCP))
        return EAI_SERVICE;
    if (!service_port(service, &port))
        return EAI_SERVICE;
    if (node == NULL) {
        uint16_t first = family == AF_INET6 ? ASTRA_NETWORK_FAMILY_IPV6 :
                         ASTRA_NETWORK_FAMILY_IPV4;
        uint16_t last = family == AF_INET ? ASTRA_NETWORK_FAMILY_IPV4 :
                        ASTRA_NETWORK_FAMILY_IPV6;

        for (uint16_t current = first; current <= last; ++current) {
            AstraNetworkAddress *address = &stack_addresses[count++];

            (void)memset(address, 0, sizeof(*address));
            address->size = sizeof(*address);
            address->family = current;
            address->port = port;
            if ((flags & AI_PASSIVE) == 0) {
                if (current == ASTRA_NETWORK_FAMILY_IPV4) {
                    address->address[0] = 127u;
                    address->address[3] = 1u;
                } else {
                    address->address[15] = 1u;
                }
            }
        }
        numeric = 1;
    } else {
        AstraNetworkAddress address = {0};

        address.size = sizeof(address);
        if ((family == AF_UNSPEC || family == AF_INET) &&
            inet_pton(AF_INET, node, address.address) == 1) {
            address.family = ASTRA_NETWORK_FAMILY_IPV4;
            numeric = 1;
        } else if ((family == AF_UNSPEC || family == AF_INET6) &&
                   inet_pton(AF_INET6, node, address.address) == 1) {
            address.family = ASTRA_NETWORK_FAMILY_IPV6;
            numeric = 1;
        }
        if (numeric) {
            address.port = port;
            stack_addresses[0] = address;
            count = 1u;
        } else if ((flags & AI_NUMERICHOST) != 0) {
            return EAI_NONAME;
        }
    }
    if (!numeric) {
        uint16_t native_family = family == AF_INET ?
            ASTRA_NETWORK_FAMILY_IPV4 : family == AF_INET6 ?
            ASTRA_NETWORK_FAMILY_IPV6 : ASTRA_NETWORK_FAMILY_UNSPEC;
        uint8_t native_type = type == SOCK_STREAM ?
            ASTRA_NETWORK_TYPE_STREAM : type == SOCK_DGRAM ?
            ASTRA_NETWORK_TYPE_DATAGRAM : 0u;
        uint8_t native_protocol = protocol == IPPROTO_TCP ?
            ASTRA_NETWORK_PROTOCOL_TCP : protocol == IPPROTO_UDP ?
            ASTRA_NETWORK_PROTOCOL_UDP : protocol == IPPROTO_ICMP ?
            ASTRA_NETWORK_PROTOCOL_ICMP : protocol == IPPROTO_ICMPV6 ?
            ASTRA_NETWORK_PROTOCOL_ICMPV6 : ASTRA_NETWORK_PROTOCOL_DEFAULT;

        if (!network_open())
            return EAI_SYSTEM;
        status = network_library->resolve_start(
            &network_session, node, port, native_family, native_type,
            native_protocol, &request);
        if (status != ASTRA_NETWORK_IN_PROGRESS)
            return network_gai_error("name resolution start", status);
        status = network_library->request_wait(
            &request, addresses, capacity, &count, ASTRA_DEADLINE_FOREVER);
        if (status == ASTRA_NETWORK_BUFFER_TOO_SMALL && count > capacity) {
            addresses = calloc(count, sizeof(*addresses));
            if (addresses == NULL) {
                (void)network_library->request_cancel(&request);
                return EAI_MEMORY;
            }
            capacity = count;
            status = network_library->request_wait(
                &request, addresses, capacity, &count,
                ASTRA_DEADLINE_FOREVER);
        }
        if (status != ASTRA_NETWORK_OK) {
            if (addresses != stack_addresses) free(addresses);
            return network_gai_error("name resolution wait", status);
        }
    }
    for (uint32_t index = 0u; index < count; ++index) {
        int first_type = type == 0 ? SOCK_STREAM : type;
        int last_type = type == 0 ? SOCK_DGRAM : type;

        addresses[index].port = port;
        for (int current = first_type; current <= last_type; ++current) {
            int current_protocol = protocol != 0 ? protocol :
                current == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP;
            const char *canon = (flags & AI_CANONNAME) != 0 && head == NULL ?
                node : NULL;
            struct addrinfo *info = address_info(
                &addresses[index], current, current_protocol, flags, canon);

            if (info == NULL) {
                freeaddrinfo(head);
                if (addresses != stack_addresses) free(addresses);
                return EAI_MEMORY;
            }
            *tail = info;
            tail = &info->ai_next;
        }
    }
    if (addresses != stack_addresses)
        free(addresses);
    *result = head;
    return head == NULL ? EAI_NONAME : 0;
}

const char *gai_strerror(int error)
{
    switch (error) {
    case 0: return "success";
    case EAI_BADFLAGS: return "invalid address-information flags";
    case EAI_NONAME: return "name or service not known";
    case EAI_AGAIN: return "temporary name-resolution failure";
    case EAI_FAIL: return "name-resolution failure";
    case EAI_FAMILY: return "address family not supported";
    case EAI_SOCKTYPE: return "socket type not supported";
    case EAI_SERVICE: return "service not supported";
    case EAI_MEMORY: return "out of memory";
    case EAI_SYSTEM: return "system error";
    case EAI_OVERFLOW: return "result too large";
    default: return "unknown address-information error";
    }
}
