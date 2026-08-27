#ifndef _ARPA_INET_H
#define _ARPA_INET_H

#include <stdint.h>
#include <netinet/in.h>

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#ifndef htonl
#define htonl(value) __builtin_bswap32((uint32_t)(value))
#endif
#ifndef htons
#define htons(value) __builtin_bswap16((uint16_t)(value))
#endif
#ifndef ntohl
#define ntohl(value) __builtin_bswap32((uint32_t)(value))
#endif
#ifndef ntohs
#define ntohs(value) __builtin_bswap16((uint16_t)(value))
#endif
#else
#ifndef htonl
#define htonl(value) ((uint32_t)(value))
#endif
#ifndef htons
#define htons(value) ((uint16_t)(value))
#endif
#ifndef ntohl
#define ntohl(value) ((uint32_t)(value))
#endif
#ifndef ntohs
#define ntohs(value) ((uint16_t)(value))
#endif
#endif

int inet_pton(int family, const char *text, void *address);
const char *inet_ntop(int family, const void *address, char *text,
                      socklen_t size);
int inet_aton(const char *text, struct in_addr *address);
in_addr_t inet_addr(const char *text);

#endif
