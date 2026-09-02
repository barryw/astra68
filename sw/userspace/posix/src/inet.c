#include <arpa/inet.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

static int parse_ipv4(const char *text, uint8_t bytes[4])
{
    for (uint32_t part = 0u; part < 4u; ++part) {
        uint32_t value = 0u, digits = 0u;

        while (*text >= '0' && *text <= '9') {
            value = value * 10u + (uint32_t)(*text++ - '0');
            if (++digits > 3u || value > 255u)
                return 0;
        }
        if (digits == 0u || (part != 3u && *text++ != '.'))
            return 0;
        bytes[part] = (uint8_t)value;
    }
    return *text == '\0';
}

int inet_pton(int family, const char *text, void *address)
{
    uint8_t bytes[16] = {0};
    uint8_t *at = bytes, *compressed = NULL;
    const char *cursor;

    if (text == NULL || address == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (family == AF_INET) {
        if (!parse_ipv4(text, bytes))
            return 0;
        (void)memcpy(address, bytes, 4u);
        return 1;
    }
    if (family != AF_INET6) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    cursor = text;
    if (*cursor == ':' && *++cursor != ':')
        return 0;
    while (*cursor != '\0') {
        const char *start = cursor;
        uint32_t value = 0u, digits = 0u;

        if (*cursor == ':') {
            if (compressed != NULL)
                return 0;
            compressed = at;
            ++cursor;
            if (*cursor == '\0')
                break;
            continue;
        }
        while ((*cursor >= '0' && *cursor <= '9') ||
               (*cursor >= 'a' && *cursor <= 'f') ||
               (*cursor >= 'A' && *cursor <= 'F')) {
            uint32_t digit = *cursor <= '9' ? (uint32_t)(*cursor - '0') :
                *cursor <= 'F' ? (uint32_t)(*cursor - 'A' + 10) :
                                 (uint32_t)(*cursor - 'a' + 10);
            value = (value << 4) | digit;
            ++cursor;
            if (++digits > 4u)
                return 0;
        }
        if (*cursor == '.' && at <= bytes + 12u) {
            uint8_t ipv4[4];

            if (!parse_ipv4(start, ipv4))
                return 0;
            (void)memcpy(at, ipv4, 4u);
            at += 4u;
            cursor += strlen(cursor);
            break;
        }
        if (digits == 0u || at > bytes + 14u)
            return 0;
        *at++ = (uint8_t)(value >> 8);
        *at++ = (uint8_t)value;
        if (*cursor == '\0')
            break;
        if (*cursor++ != ':')
            return 0;
    }
    if (compressed != NULL) {
        uint32_t tail = (uint32_t)(at - compressed);
        uint32_t gap = (uint32_t)((bytes + 16u) - at);

        if (gap == 0u)
            return 0;
        (void)memmove(compressed + gap, compressed, tail);
        (void)memset(compressed, 0, gap);
        at = bytes + 16u;
    }
    if (at != bytes + 16u)
        return 0;
    (void)memcpy(address, bytes, 16u);
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
