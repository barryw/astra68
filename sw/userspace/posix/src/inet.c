#include <arpa/inet.h>
#include <astra/network_library.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

int inet_pton(int family, const char *text, void *address)
{
    AstraNetworkAddress parsed;
    uint16_t native_family;

    if (text == NULL || address == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (family != AF_INET && family != AF_INET6) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    native_family = family == AF_INET ? ASTRA_NETWORK_FAMILY_IPV4 :
                                         ASTRA_NETWORK_FAMILY_IPV6;
    if (astra_network_address_parse(native_family, text, &parsed) !=
        ASTRA_NETWORK_OK)
        return 0;
    (void)memcpy(address, parsed.address, family == AF_INET ? 4u : 16u);
    return 1;
}

static char *put_decimal(char *at, uint32_t value)
{
    char reversed[3];
    uint32_t count = 0u;

    do {
        reversed[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    while (count != 0u)
        *at++ = reversed[--count];
    return at;
}

#if defined(__GNUC__) && !defined(__clang__)
/* The analyzer cannot relate the symbolic memcpy length to the bytes emitted
 * by the IPv6 compression loop. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-use-of-uninitialized-value"
#endif
const char *inet_ntop(int family, const void *address, char *text,
                      socklen_t size)
{
    const uint8_t *bytes = address;
    char output[INET6_ADDRSTRLEN];
    char *at = output;

    if (address == NULL || text == NULL) {
        errno = EFAULT;
        return NULL;
    }
    if (family == AF_INET) {
        for (uint32_t index = 0u; index < 4u; ++index) {
            if (index != 0u) *at++ = '.';
            at = put_decimal(at, bytes[index]);
        }
    } else if (family == AF_INET6) {
        uint32_t best_start = 8u, best_length = 0u;

        for (uint32_t index = 0u; index < 8u;) {
            uint32_t start = index;

            while (index < 8u && bytes[index * 2u] == 0u &&
                   bytes[index * 2u + 1u] == 0u)
                ++index;
            if (index - start > best_length) {
                best_start = start;
                best_length = index - start;
            }
            if (index == start) ++index;
        }
        if (best_length < 2u) best_start = 8u;
        for (uint32_t index = 0u; index < 8u;) {
            uint32_t value;
            char reversed[4];
            uint32_t count = 0u;

            if (index == best_start) {
                *at++ = ':'; *at++ = ':';
                index += best_length;
                continue;
            }
            if (index != 0u && index != best_start + best_length)
                *at++ = ':';
            value = ((uint32_t)bytes[index * 2u] << 8) |
                    bytes[index * 2u + 1u];
            do {
                uint32_t digit = value & 15u;
                reversed[count++] = (char)(digit < 10u ? '0' + digit :
                                                       'a' + digit - 10u);
                value >>= 4;
            } while (value != 0u);
            while (count != 0u) *at++ = reversed[--count];
            ++index;
        }
    } else {
        errno = EAFNOSUPPORT;
        return NULL;
    }
    *at = '\0';
    if ((size_t)(at - output) + 1u > size) {
        errno = ENOSPC;
        return NULL;
    }
    (void)memcpy(text, output, (size_t)(at - output) + 1u);
    return text;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

int inet_aton(const char *text, struct in_addr *address)
{
    return inet_pton(AF_INET, text, address) == 1;
}

in_addr_t inet_addr(const char *text)
{
    struct in_addr address;

    return inet_aton(text, &address) ? address.s_addr : INADDR_NONE;
}

char *inet_ntoa(struct in_addr address)
{
    static _Thread_local char text[INET_ADDRSTRLEN];

    return (char *)inet_ntop(AF_INET, &address, text, sizeof(text));
}
