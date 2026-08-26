#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define NTP_PACKET_BYTES 48u
#define NTP_UNIX_DELTA UINT32_C(2208988800)

static uint32_t get32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24u) | ((uint32_t)bytes[1] << 16u) |
           ((uint32_t)bytes[2] << 8u) | bytes[3];
}

static void put32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24u);
    bytes[1] = (uint8_t)(value >> 16u);
    bytes[2] = (uint8_t)(value >> 8u);
    bytes[3] = (uint8_t)value;
}

static int synchronize(const char *server)
{
    static const char service[] = "123";
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    struct timespec started;
    struct timespec finished;
    uint8_t request[NTP_PACKET_BYTES] = {0};
    uint8_t response[NTP_PACKET_BYTES];
    int status;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    status = getaddrinfo(server, service, &hints, &addresses);
    if (status != 0) {
        fprintf(stderr, "NTP lookup failed for %s: %s\n", server,
                gai_strerror(status));
        return 0;
    }

    request[0] = 0x23u; /* Leap 0, NTP v4, client. */
    if (clock_gettime(CLOCK_MONOTONIC, &started) != 0) {
        perror("clock_gettime");
        freeaddrinfo(addresses);
        return 0;
    }
    put32(&request[40], (uint32_t)started.tv_sec);
    put32(&request[44], (uint32_t)started.tv_nsec ^ (uint32_t)getpid());

    for (address = addresses; address != NULL; address = address->ai_next) {
        struct pollfd ready;
        ssize_t bytes;
        int descriptor = socket(address->ai_family, address->ai_socktype,
                                address->ai_protocol);

        if (descriptor < 0)
            continue;
        if (connect(descriptor, address->ai_addr, address->ai_addrlen) != 0 ||
            send(descriptor, request, sizeof(request), 0) !=
            (ssize_t)sizeof(request)) {
            close(descriptor);
            continue;
        }
        ready.fd = descriptor;
        ready.events = POLLIN;
        ready.revents = 0;
        status = poll(&ready, 1u, 3000);
        if (status <= 0 || (ready.revents & POLLIN) == 0) {
            close(descriptor);
            continue;
        }
        bytes = recv(descriptor, response, sizeof(response), 0);
        close(descriptor);
        if (bytes != (ssize_t)sizeof(response) || (response[0] >> 6u) == 3u ||
            ((response[0] >> 3u) & 7u) < 3u ||
            ((response[0] >> 3u) & 7u) > 4u ||
            (response[0] & 7u) != 4u ||
            response[1] == 0u || response[1] >= 16u ||
            memcmp(&response[24], &request[40], 8u) != 0)
            continue;
        if (clock_gettime(CLOCK_MONOTONIC, &finished) != 0)
            continue;
        {
            uint32_t ntp_seconds = get32(&response[40]);
            uint32_t ntp_fraction = get32(&response[44]);
            uint64_t full_ntp_seconds = ntp_seconds;
            int64_t round_trip_ns =
                (int64_t)(finished.tv_sec - started.tv_sec) *
                    INT64_C(1000000000) +
                (int64_t)finished.tv_nsec - (int64_t)started.tv_nsec;
            struct timespec wall;

            if (round_trip_ns < 0)
                continue;
            if (ntp_seconds < NTP_UNIX_DELTA)
                full_ntp_seconds += UINT64_C(1) << 32u;
            wall.tv_sec = (time_t)(full_ntp_seconds - NTP_UNIX_DELTA);
            wall.tv_nsec = (long)(((uint64_t)ntp_fraction *
                                   UINT64_C(1000000000)) >> 32u);
            wall.tv_nsec += (long)(round_trip_ns / 2);
            if (wall.tv_nsec >= 1000000000L) {
                ++wall.tv_sec;
                wall.tv_nsec -= 1000000000L;
            }
            if (clock_settime(CLOCK_REALTIME, &wall) != 0) {
                perror("clock_settime");
                freeaddrinfo(addresses);
                return 0;
            }
            printf("NTP synchronized from %s: %lld.%09ld UTC\n", server,
                   (long long)wall.tv_sec, wall.tv_nsec);
            freeaddrinfo(addresses);
            return 1;
        }
    }
    freeaddrinfo(addresses);
    fprintf(stderr, "NTP server unavailable: %s\n", server);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 2) {
        fprintf(stderr, "usage: %s [server]\n", argv[0]);
        return 2;
    }
    return synchronize(argc == 2 ? argv[1] : "pool.ntp.org") ? 0 : 1;
}
