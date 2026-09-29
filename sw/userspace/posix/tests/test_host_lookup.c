#include <netdb.h>
#include <netinet/in.h>
#include <astra/runtime.h>
#include "../src/socket_internal.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

static int lookup_status;
static int return_addresses = 1;
static struct sockaddr_in socket_addresses[2];
static struct addrinfo entries[2];
static AstraNetworkSession native_session = ASTRA_NETWORK_SESSION_INIT;
static AstraNetworkStatus reverse_result = ASTRA_NETWORK_OK;
static int reverse_available = 1;
static unsigned int reverse_cancelled;

AstraNetworkSession *astra_posix_network_session(void)
{
    return reverse_available ? &native_session : NULL;
}

AstraNetworkStatus astra_network_reverse_start(
    AstraNetworkSession *session, const AstraNetworkAddress *address,
    AstraNetworkRequest *request)
{
    assert(session == &native_session);
    assert(address->size == sizeof(*address));
    assert(address->family == ASTRA_NETWORK_FAMILY_IPV4);
    assert(address->address[0] == 192u && address->address[3] == 7u);
    request->_private_state = 2u;
    return ASTRA_NETWORK_IN_PROGRESS;
}

AstraNetworkStatus astra_network_reverse_wait(
    AstraNetworkRequest *request, char *name, uint32_t capacity,
    uint32_t *required, uint64_t deadline)
{
    static const char hostname[] = "reverse.example";

    assert(request->_private_state == 2u);
    assert(deadline == ASTRA_DEADLINE_FOREVER);
    if (reverse_result != ASTRA_NETWORK_BUFFER_TOO_SMALL)
        request->_private_state = 0u;
    if (reverse_result != ASTRA_NETWORK_OK)
        return reverse_result;
    assert(capacity >= sizeof(hostname));
    (void)memcpy(name, hostname, sizeof(hostname));
    *required = sizeof(hostname);
    return ASTRA_NETWORK_OK;
}

AstraNetworkStatus astra_network_request_cancel(AstraNetworkRequest *request)
{
    ++reverse_cancelled;
    request->_private_state = 0u;
    return ASTRA_NETWORK_CANCELLED;
}

int getaddrinfo(const char *name, const char *service,
                const struct addrinfo *hints, struct addrinfo **result)
{
    assert(name != NULL && service == NULL);
    assert(hints->ai_family == AF_INET);
    assert(hints->ai_socktype == SOCK_STREAM);
    assert(hints->ai_flags == AI_CANONNAME);
    if (lookup_status != 0)
        return lookup_status;
    (void)memset(entries, 0, sizeof(entries));
    if (!return_addresses) {
        *result = NULL;
        return 0;
    }
    for (size_t i = 0u; i < 2u; ++i) {
        entries[i].ai_family = AF_INET;
        entries[i].ai_addrlen = sizeof(socket_addresses[i]);
        entries[i].ai_addr = (struct sockaddr *)&socket_addresses[i];
    }
    entries[0].ai_canonname = "canonical.example";
    entries[0].ai_next = &entries[1];
    *result = entries;
    return 0;
}

void freeaddrinfo(struct addrinfo *result)
{
    (void)result;
}

int main(void)
{
    struct hostent *host;
    unsigned char reverse_address[4] = {192u, 0u, 2u, 7u};
    uint32_t first = UINT32_C(0x01020304);
    uint32_t second = UINT32_C(0x05060708);

    (void)memcpy(&socket_addresses[0].sin_addr, &first, sizeof(first));
    (void)memcpy(&socket_addresses[1].sin_addr, &second, sizeof(second));
    host = gethostbyname("lookup.example");
    assert(host != NULL && h_errno == 0);
    assert(strcmp(host->h_name, "canonical.example") == 0);
    assert(host->h_addrtype == AF_INET);
    assert(host->h_length == (int)sizeof(struct in_addr));
    assert(host->h_aliases[0] == NULL && host->h_addr_list[2] == NULL);
    assert(memcmp(host->h_addr_list[0], &first, sizeof(first)) == 0);
    assert(memcmp(host->h_addr_list[1], &second, sizeof(second)) == 0);

    lookup_status = EAI_NONAME;
    assert(gethostbyname("missing.example") == NULL);
    assert(h_errno == HOST_NOT_FOUND);
    lookup_status = EAI_AGAIN;
    assert(gethostbyname("later.example") == NULL);
    assert(h_errno == TRY_AGAIN);
    lookup_status = EAI_MEMORY;
    assert(gethostbyname("oom.example") == NULL);
    assert(h_errno == NO_RECOVERY && errno == ENOMEM);
    lookup_status = 0;
    return_addresses = 0;
    assert(gethostbyname("empty.example") == NULL);
    assert(h_errno == NO_DATA);
    assert(gethostbyname(NULL) == NULL && h_errno == HOST_NOT_FOUND);

    host = gethostbyaddr(reverse_address, sizeof(reverse_address), AF_INET);
    assert(host != NULL && h_errno == 0);
    assert(strcmp(host->h_name, "reverse.example") == 0);
    assert(host->h_addrtype == AF_INET && host->h_length == 4);
    assert(memcmp(host->h_addr_list[0], reverse_address, 4u) == 0);
    assert(host->h_addr_list[1] == NULL);
    reverse_result = ASTRA_NETWORK_NAME_NOT_FOUND;
    assert(gethostbyaddr(reverse_address, 4u, AF_INET) == NULL);
    assert(h_errno == HOST_NOT_FOUND);
    reverse_result = ASTRA_NETWORK_NAME_TEMPORARY;
    assert(gethostbyaddr(reverse_address, 4u, AF_INET) == NULL);
    assert(h_errno == TRY_AGAIN);
    reverse_result = ASTRA_NETWORK_BUFFER_TOO_SMALL;
    assert(gethostbyaddr(reverse_address, 4u, AF_INET) == NULL);
    assert(h_errno == NO_RECOVERY && reverse_cancelled == 1u);
    reverse_result = ASTRA_NETWORK_OK;
    reverse_available = 0;
    assert(gethostbyaddr(reverse_address, 4u, AF_INET) == NULL);
    assert(h_errno == NO_RECOVERY);
    assert(gethostbyaddr(reverse_address, 3u, AF_INET) == NULL);
    assert(h_errno == NO_RECOVERY && errno == EINVAL);
    return 0;
}
