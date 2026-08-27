#define _POSIX_C_SOURCE 200809L
#define _POSIX_MONOTONIC_CLOCK 200809L

#include <astra/ntp_core.h>

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static uint64_t nanoseconds(const struct timespec *value)
{
    return (uint64_t)value->tv_sec * UINT64_C(1000000000) +
           (uint64_t)value->tv_nsec;
}

static AstraNtpStatus query_address(const struct addrinfo *address,
                                    AstraNtpSample *sample)
{
    uint8_t request[ASTRA_NTP_PACKET_SIZE];
    uint8_t response[ASTRA_NTP_PACKET_SIZE];
    struct timespec monotonic_send, monotonic_receive;
    struct timespec realtime_send, realtime_receive;
    uint64_t realtime_send_ns = 0u, realtime_receive_ns = 0u;
    uint64_t token;
    int descriptor;

    descriptor = socket(address->ai_family, SOCK_DGRAM, IPPROTO_UDP);
    if (descriptor < 0)
        return ASTRA_NTP_IO;
    if (connect(descriptor, address->ai_addr, address->ai_addrlen) != 0) {
        close(descriptor);
        return ASTRA_NTP_IO;
    }
    for (uint32_t attempt = 0u; attempt < 3u; ++attempt) {
        struct pollfd pollfd = {descriptor, POLLIN, 0};
        ssize_t received;

        if (clock_gettime(CLOCK_MONOTONIC, &monotonic_send) != 0)
            break;
        if (clock_gettime(CLOCK_REALTIME, &realtime_send) == 0)
            realtime_send_ns = nanoseconds(&realtime_send);
        else
            realtime_send_ns = 0u;
        token = realtime_send_ns != 0u ?
            astra_ntp_unix_ns_to_timestamp(realtime_send_ns) :
            (nanoseconds(&monotonic_send) ^ UINT64_C(0xa57a68c10c4e5450));
        astra_ntp_request(request, token);
        if (send(descriptor, request, sizeof(request), 0) !=
                (ssize_t)sizeof(request))
            continue;
        if (poll(&pollfd, 1u, 3000) <= 0)
            continue;
        received = recv(descriptor, response, sizeof(response), 0);
        if (received < 0 ||
            clock_gettime(CLOCK_MONOTONIC, &monotonic_receive) != 0)
            continue;
        if (realtime_send_ns != 0u &&
            clock_gettime(CLOCK_REALTIME, &realtime_receive) == 0)
            realtime_receive_ns = nanoseconds(&realtime_receive);
        else
            realtime_receive_ns = 0u;
        if (astra_ntp_response(
                response, (uint32_t)received, token,
                nanoseconds(&monotonic_send), nanoseconds(&monotonic_receive),
                realtime_send_ns, realtime_receive_ns,
                sample) == ASTRA_NTP_OK) {
            close(descriptor);
            return ASTRA_NTP_OK;
        }
    }
    close(descriptor);
    return errno == ETIMEDOUT ? ASTRA_NTP_TIMED_OUT : ASTRA_NTP_IO;
}

AstraNtpStatus astra_ntp_query(const char *server, AstraNtpSample *sample)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    AstraNtpStatus status = ASTRA_NTP_IO;

    if (server == NULL || server[0] == '\0' || sample == NULL)
        return ASTRA_NTP_INVALID;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    if (getaddrinfo(server, "123", &hints, &addresses) != 0)
        return ASTRA_NTP_RESOLVE;
    for (const struct addrinfo *address = addresses; address != NULL;
         address = address->ai_next) {
        status = query_address(address, sample);
        if (status == ASTRA_NTP_OK)
            break;
    }
    freeaddrinfo(addresses);
    return status;
}
