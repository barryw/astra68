/*
 * Astra port of Toybox ping.c (0BSD), with Toybox's Linux command framework
 * replaced by POSIX sockets. See third_party/toybox/ASTRA_VENDOR.md.
 */

#define _POSIX_C_SOURCE 200809L
#define _POSIX_MONOTONIC_CLOCK 200809L

#include <astra/ping_core.h>
#include <astra/program.h>
#include <astra/status.h>

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/ip_icmp.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

ASTRA_PROGRAM("ping", 0, 1, 0, "Toybox / Astra68 contributors",
              "Toybox ping (0BSD)");

#define PING_DEFAULT_COUNT 3u
#define PING_DEFAULT_PAYLOAD 56u
#define PING_DEFAULT_INTERVAL_MS 1000u
#define PING_DEFAULT_WAIT_MS 3000u
#define PING_PAYLOAD_MAX 65487u /* 65535-byte IPv6 packet - headers */

static uint64_t milliseconds(void)
{
    struct timespec value;

    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        return 0u;
    return (uint64_t)value.tv_sec * 1000u + (uint64_t)value.tv_nsec / 1000000u;
}

static int number(const char *text, uint32_t maximum, uint32_t *value)
{
    char *end;
    unsigned long parsed;

    if (text == NULL || text[0] == '\0' || text[0] == '-')
        return 0;
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > maximum)
        return 0;
    *value = (uint32_t)parsed;
    return 1;
}

static int duration(const char *text, uint32_t *milliseconds_out)
{
    uint64_t whole = 0u, fraction = 0u, scale = 100u;
    int after = 0;

    if (text == NULL || text[0] == '\0')
        return 0;
    for (; *text != '\0'; ++text) {
        if (*text == '.' && !after) {
            after = 1;
            continue;
        }
        if (*text < '0' || *text > '9')
            return 0;
        if (!after) {
            whole = whole * 10u + (uint32_t)(*text - '0');
            if (whole > UINT32_MAX / 1000u)
                return 0;
        } else if (scale != 0u) {
            fraction += (uint32_t)(*text - '0') * scale;
            scale /= 10u;
        }
    }
    *milliseconds_out = (uint32_t)(whole * 1000u + fraction);
    return 1;
}

static void usage(void)
{
    fprintf(stderr, "usage: ping [-46q] [-c count] [-i seconds] "
                    "[-s bytes] [-W seconds] host\n");
}

int main(int argc, char **argv)
{
    struct addrinfo hints, *addresses = NULL, *address;
    struct icmp_echo_header *header;
    uint8_t *packet;
    const char *host = NULL;
    uint32_t count = PING_DEFAULT_COUNT, payload = PING_DEFAULT_PAYLOAD;
    uint32_t interval = PING_DEFAULT_INTERVAL_MS;
    uint32_t wait = PING_DEFAULT_WAIT_MS;
    uint32_t sent = 0u, received = 0u;
    uint64_t total = 0u, minimum = UINT64_MAX, maximum = 0u;
    int family = AF_UNSPEC, quiet = 0, descriptor = -1, result = 1;
    char printable[INET6_ADDRSTRLEN];

    for (int at = 1; at < argc; ++at) {
        const char *option = argv[at];
        uint32_t *target = NULL;

        if (strcmp(option, "-4") == 0) family = AF_INET;
        else if (strcmp(option, "-6") == 0) family = AF_INET6;
        else if (strcmp(option, "-q") == 0) quiet = 1;
        else if (strcmp(option, "-c") == 0) target = &count;
        else if (strcmp(option, "-s") == 0) target = &payload;
        else if (strcmp(option, "-i") == 0 || strcmp(option, "-W") == 0) {
            if (++at == argc ||
                !duration(argv[at], strcmp(option, "-i") == 0 ?
                                      &interval : &wait)) {
                usage();
                return ASTRA_STATUS_INVALID;
            }
            continue;
        } else if (option[0] == '-') {
            usage();
            return ASTRA_STATUS_INVALID;
        } else if (host == NULL) {
            host = option;
            continue;
        } else {
            usage();
            return ASTRA_STATUS_INVALID;
        }
        if (target != NULL &&
            (++at == argc || !number(argv[at], target == &payload ?
                                              PING_PAYLOAD_MAX : UINT32_MAX,
                                     target))) {
            usage();
            return ASTRA_STATUS_INVALID;
        }
    }
    if (host == NULL) {
        usage();
        return ASTRA_STATUS_INVALID;
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    if (getaddrinfo(host, NULL, &hints, &addresses) != 0) {
        fprintf(stderr, "ping: could not resolve %s\n", host);
        return ASTRA_STATUS_NOT_FOUND;
    }
    for (address = addresses; address != NULL; address = address->ai_next) {
        int protocol;

        if (address->ai_family != AF_INET && address->ai_family != AF_INET6)
            continue;
        protocol = address->ai_family == AF_INET ? IPPROTO_ICMP :
                                                   IPPROTO_ICMPV6;
        descriptor = socket(address->ai_family, SOCK_DGRAM, protocol);
        if (descriptor >= 0 &&
            connect(descriptor, address->ai_addr, address->ai_addrlen) == 0)
            break;
        if (descriptor >= 0)
            (void)close(descriptor);
        descriptor = -1;
    }
    if (address == NULL || descriptor < 0) {
        fprintf(stderr, "ping: ICMP socket unavailable\n");
        freeaddrinfo(addresses);
        return ASTRA_STATUS_ACCESS;
    }
    if (inet_ntop(address->ai_family,
                  address->ai_family == AF_INET ?
                    (const void *)&((const struct sockaddr_in *)
                        address->ai_addr)->sin_addr :
                    (const void *)&((const struct sockaddr_in6 *)
                        address->ai_addr)->sin6_addr,
                  printable, sizeof(printable)) == NULL)
        strcpy(printable, host);
    packet = calloc(1u, sizeof(*header) + payload);
    if (packet == NULL) {
        (void)close(descriptor);
        freeaddrinfo(addresses);
        return ASTRA_STATUS_LIMIT;
    }
    header = (struct icmp_echo_header *)(void *)packet;
    if (!quiet)
        printf("PING %s (%s): %" PRIu32 " data bytes\n",
               host, printable, payload);
    for (uint32_t sequence = 1u; count == 0u || sequence <= count;
         ++sequence) {
        struct pollfd descriptor_poll = {descriptor, POLLIN, 0};
        uint64_t started, elapsed;
        int ready;

        memset(packet, 0, sizeof(*header) + payload);
        header->type = address->ai_family == AF_INET ? ICMP_ECHO :
                                                       ICMP6_ECHO_REQUEST;
        header->identifier = htons((uint16_t)getpid());
        header->sequence = htons((uint16_t)sequence);
        header->checksum = htons(astra_ping_checksum(
            packet, (uint32_t)(sizeof(*header) + payload)));
        started = milliseconds();
        if (send(descriptor, packet, sizeof(*header) + payload, 0) ==
                (ssize_t)(sizeof(*header) + payload))
            ++sent;
        else
            fprintf(stderr, "ping: send failed\n");
        ready = poll(&descriptor_poll, 1u, (int)wait);
        elapsed = milliseconds() - started;
        if (ready > 0) {
            ssize_t length = recv(descriptor, packet,
                                  sizeof(*header) + payload, 0);

            if (length >= (ssize_t)sizeof(*header) &&
                ntohs(header->sequence) == (uint16_t)sequence &&
                (header->type == ICMP_ECHOREPLY ||
                 header->type == ICMP6_ECHO_REPLY)) {
                ++received;
                total += elapsed;
                if (elapsed < minimum) minimum = elapsed;
                if (elapsed > maximum) maximum = elapsed;
                result = 0;
                if (!quiet)
                    printf("%zd bytes from %s: icmp_seq=%" PRIu32
                           " time=%" PRIu64 " ms\n",
                           length, printable, sequence, elapsed);
            }
        }
        if ((count == 0u || sequence < count) && elapsed < interval)
            (void)poll(NULL, 0u, (int)(interval - elapsed));
    }
    if (!quiet) {
        printf("\n--- %s ping statistics ---\n", host);
        printf("%" PRIu32 " packets transmitted, %" PRIu32
               " received, %" PRIu32 "%% packet loss\n",
               sent, received, sent == 0u ? 100u :
                    (sent - received) * 100u / sent);
        if (received != 0u)
            printf("round-trip min/avg/max = %" PRIu64 "/%" PRIu64
                   "/%" PRIu64 " ms\n", minimum, total / received, maximum);
    }
    free(packet);
    (void)close(descriptor);
    freeaddrinfo(addresses);
    return result;
}
