#include <netdb.h>
#include <netinet/in.h>
#include "socket_internal.h"
#include <astra/runtime.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int h_errno;

/* POSIX legacy hostent storage is invalidated by the next call. */
static struct hostent host_result;
static char *host_name;
static char **host_addresses;
static unsigned char *host_address_bytes;
static char *host_aliases[1];

static void clear_host_result(void)
{
    free(host_name);
    free(host_addresses);
    free(host_address_bytes);
    host_name = NULL;
    host_addresses = NULL;
    host_address_bytes = NULL;
    (void)memset(&host_result, 0, sizeof(host_result));
}

static int reserve_host_result(const char *name, size_t count,
                               size_t address_size, int family)
{
    size_t length = strlen(name) + 1u;

    if (count > (SIZE_MAX / sizeof(*host_addresses)) - 1u ||
        count > SIZE_MAX / address_size) {
        h_errno = NO_RECOVERY;
        errno = ENOMEM;
        return 0;
    }
    host_name = malloc(length);
    host_addresses = calloc(count + 1u, sizeof(*host_addresses));
    host_address_bytes = calloc(count, address_size);
    if (host_name == NULL || host_addresses == NULL ||
        host_address_bytes == NULL) {
        clear_host_result();
        h_errno = NO_RECOVERY;
        errno = ENOMEM;
        return 0;
    }
    (void)memcpy(host_name, name, length);
    host_result.h_name = host_name;
    host_result.h_aliases = host_aliases;
    host_result.h_addrtype = family;
    host_result.h_length = (int)address_size;
    host_result.h_addr_list = host_addresses;
    return 1;
}

struct hostent *gethostbyname(const char *name)
{
    struct addrinfo hints = {0}, *addresses = NULL, *entry;
    size_t count = 0u, index = 0u;
    const char *canonical;
    int status;

    clear_host_result();
    if (name == NULL || *name == '\0') {
        h_errno = HOST_NOT_FOUND;
        return NULL;
    }
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;
    status = getaddrinfo(name, NULL, &hints, &addresses);
    if (status != 0) {
        if (status == EAI_MEMORY)
            errno = ENOMEM;
        h_errno = status == EAI_AGAIN ? TRY_AGAIN :
                  status == EAI_NONAME ? HOST_NOT_FOUND : NO_RECOVERY;
        return NULL;
    }
    for (entry = addresses; entry != NULL; entry = entry->ai_next) {
        if (entry->ai_family == AF_INET && entry->ai_addr != NULL &&
            entry->ai_addrlen >= sizeof(struct sockaddr_in)) {
            if (count == SIZE_MAX / sizeof(*host_addresses)) {
                h_errno = NO_RECOVERY;
                freeaddrinfo(addresses);
                return NULL;
            }
            ++count;
        }
    }
    if (count == 0u) {
        h_errno = NO_DATA;
        freeaddrinfo(addresses);
        return NULL;
    }
    canonical = addresses->ai_canonname != NULL ?
        addresses->ai_canonname : name;
    if (!reserve_host_result(canonical, count, sizeof(struct in_addr),
                             AF_INET)) {
        freeaddrinfo(addresses);
        return NULL;
    }
    for (entry = addresses; entry != NULL; entry = entry->ai_next) {
        if (entry->ai_family == AF_INET && entry->ai_addr != NULL &&
            entry->ai_addrlen >= sizeof(struct sockaddr_in)) {
            host_addresses[index] =
                (char *)(host_address_bytes + index * sizeof(struct in_addr));
            (void)memcpy(host_addresses[index],
                         &((const struct sockaddr_in *)entry->ai_addr)->sin_addr,
                         sizeof(struct in_addr));
            ++index;
        }
    }
    freeaddrinfo(addresses);
    h_errno = 0;
    return &host_result;
}

struct hostent *gethostbyaddr(const void *address, socklen_t length,
                              int family)
{
    AstraNetworkAddress native = {0};
    AstraNetworkRequest pending = ASTRA_NETWORK_REQUEST_INIT;
    AstraNetworkSession *session;
    AstraNetworkStatus status;
    char name[ASTRA_NETWORK_NAME_MAX + 1u];
    uint32_t required = 0u;
    size_t bytes = family == AF_INET ? sizeof(struct in_addr) :
                   family == AF_INET6 ? sizeof(struct in6_addr) : 0u;

    clear_host_result();
    if (address == NULL || bytes == 0u || length != bytes) {
        errno = EINVAL;
        h_errno = NO_RECOVERY;
        return NULL;
    }
    session = astra_posix_network_session();
    if (session == NULL) {
        h_errno = NO_RECOVERY;
        return NULL;
    }
    native.size = sizeof(native);
    native.family = family == AF_INET ? ASTRA_NETWORK_FAMILY_IPV4 :
                                          ASTRA_NETWORK_FAMILY_IPV6;
    (void)memcpy(native.address, address, bytes);
    status = astra_network_reverse_start(session, &native, &pending);
    if (status == ASTRA_NETWORK_IN_PROGRESS)
        status = astra_network_reverse_wait(
            &pending, name, sizeof(name), &required, ASTRA_DEADLINE_FOREVER);
    if (status != ASTRA_NETWORK_OK) {
        if (pending._private_state != 0u)
            (void)astra_network_request_cancel(&pending);
        h_errno = status == ASTRA_NETWORK_NAME_NOT_FOUND ? HOST_NOT_FOUND :
                  status == ASTRA_NETWORK_NAME_TEMPORARY ||
                  status == ASTRA_NETWORK_TIMED_OUT ? TRY_AGAIN : NO_RECOVERY;
        if (status == ASTRA_NETWORK_OUT_OF_MEMORY ||
            status == ASTRA_NETWORK_RESOURCE_LIMIT)
            errno = ENOMEM;
        return NULL;
    }
    if (!reserve_host_result(name, 1u, bytes, family))
        return NULL;
    host_addresses[0] = (char *)host_address_bytes;
    (void)memcpy(host_address_bytes, address, bytes);
    h_errno = 0;
    return &host_result;
}
