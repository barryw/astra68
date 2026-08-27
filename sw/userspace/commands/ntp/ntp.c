#include <astra/ntp.h>
#include <astra/ntp_core.h>
#include <astra/posix_descriptor.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/status.h>

#include <inttypes.h>
#include <stdio.h>

ASTRA_PROGRAM("ntp", 0, 1, 0, "Astra68 contributors",
              "Toybox SNTP (0BSD)");

int main(int argc, char **argv)
{
    const AstraStartupCapability *service;
    AstraNtpControlReply reply;
    uint64_t round_trip;
    int64_t offset;
    uint32_t status;

    if (argc > 2) {
        fprintf(stderr, "usage: ntp [server]\n");
        return ASTRA_STATUS_INVALID;
    }
    service = astra_startup_capability(astra_posix_startup(),
                                       ASTRA_CAPABILITY_NTP);
    if (service == NULL) {
        fprintf(stderr, "ntp: ntpd is not available\n");
        return ASTRA_STATUS_ACCESS;
    }
    status = astra_ntp_sync(service->handle, argc == 2 ? argv[1] : NULL,
                            &reply);
    if (status != ASTRA_NTP_OK) {
        fprintf(stderr, "ntp: %s\n",
                astra_ntp_status_text((AstraNtpStatus)status));
        return ASTRA_STATUS_IO;
    }
    round_trip = ((uint64_t)reply.round_trip_hi << 32) |
                 reply.round_trip_lo;
    offset = (int64_t)(((uint64_t)reply.offset_hi << 32) | reply.offset_lo);
    printf("stratum %" PRIu32 ", round trip %" PRIu64
           " us, offset %" PRId64 " us\n",
           reply.stratum, round_trip / 1000u, offset / 1000);
    return 0;
}
