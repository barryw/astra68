#define _POSIX_C_SOURCE 200809L

#include <astra/clock.h>
#include <astra/config_library.h>
#include <astra/ntp.h>
#include <astra/ntp_core.h>
#include <astra/posix.h>
#include <astra/posix_descriptor.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/vfs_process.h>

#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

ASTRA_PROGRAM("ntpd", 0, 1, 0, "Astra68 contributors",
              "Toybox SNTP (0BSD)");

#define NTP_POLL_NS (UINT64_C(1000) * UINT64_C(1000000000))

static uint32_t control_receive;
static uint32_t control_send;
static AstraLibraryHandle *config_handle;
static const AstraConfigLibraryV1 *config_library;
static AstraConfig config = ASTRA_CONFIG_INIT;

static AstraNtpStatus set_from(const char *server, AstraNtpSample *sample)
{
    struct timespec value;
    AstraNtpStatus status = astra_ntp_query(server, sample);

    if (status != ASTRA_NTP_OK)
        return status;
    value.tv_sec = (time_t)(sample->realtime_ns / UINT64_C(1000000000));
    value.tv_nsec = (long)(sample->realtime_ns % UINT64_C(1000000000));
    return clock_settime(CLOCK_REALTIME, &value) == 0 ?
        ASTRA_NTP_OK : ASTRA_NTP_CLOCK;
}

static AstraNtpStatus synchronize(const char *override, AstraNtpSample *sample)
{
    static const char *const keys[] = {"pool", "server"};
    AstraNtpStatus result = ASTRA_NTP_INVALID;

    if (override != NULL && override[0] != '\0')
        return set_from(override, sample);
    for (uint32_t key = 0u; key < 2u; ++key) {
        uint32_t count = 0u;

        if (config_library->count(&config, keys[key], &count) !=
            ASTRA_CONFIG_OK)
            return ASTRA_NTP_CONFIG;
        for (uint32_t index = 0u; index < count; ++index) {
            char source[ASTRA_NETWORK_NAME_MAX + 1u];
            uint32_t length = 0u;

            if (config_library->get_string(
                    &config, keys[key], index, source, sizeof(source),
                    &length) != ASTRA_CONFIG_OK || length == 0u)
                return ASTRA_NTP_CONFIG;
            if (result != ASTRA_NTP_OK)
                result = set_from(source, sample);
        }
    }
    return result;
}

static void control(void)
{
    AstraNtpControlRequest request;
    AstraNtpControlReply reply;
    AstraNtpSample sample;
    uint32_t carried = 0u, handle_count = 0u, size = 0u;
    uint32_t status = astra_port_receive(
        control_receive, &request, sizeof(request), &carried, 1u,
        &size, &handle_count);

    if (status != ASTRA_SYSCALL_OK)
        return;
    memset(&reply, 0, sizeof(reply));
    status = ASTRA_NTP_INVALID;
    if (size == sizeof(request) && handle_count == 1u &&
        request.header.total_size == sizeof(request) &&
        request.header.header_size == ASTRA_MESSAGE_HEADER_SIZE &&
        request.header.protocol == ASTRA_NTP_CONTROL_PROTOCOL &&
        request.header.protocol_version == ASTRA_NTP_CONTROL_VERSION &&
        request.header.operation == ASTRA_NTP_CONTROL_SYNC &&
        request.server[ASTRA_NETWORK_NAME_MAX] == '\0')
        status = request.server[0] != '\0' ?
            synchronize(request.server, &sample) :
            (config_library->reload(&config, NULL) == ASTRA_CONFIG_OK ?
                 synchronize(NULL, &sample) : ASTRA_NTP_CONFIG);
    astra_message_header_set(&reply.header, sizeof(reply),
                             ASTRA_NTP_CONTROL_PROTOCOL,
                             ASTRA_NTP_CONTROL_VERSION,
                             ASTRA_NTP_CONTROL_SYNC,
                             request.header.transaction_id);
    reply.status = status;
    if (status == ASTRA_NTP_OK) {
        reply.realtime_hi = (uint32_t)(sample.realtime_ns >> 32);
        reply.realtime_lo = (uint32_t)sample.realtime_ns;
        reply.round_trip_hi = (uint32_t)(sample.round_trip_ns >> 32);
        reply.round_trip_lo = (uint32_t)sample.round_trip_ns;
        reply.offset_hi = (uint32_t)((uint64_t)sample.offset_ns >> 32);
        reply.offset_lo = (uint32_t)sample.offset_ns;
        reply.stratum = sample.stratum;
    }
    if (handle_count == 1u) {
        (void)astra_port_send(carried, &reply, sizeof(reply), NULL, 0u);
        (void)astra_close(carried);
    }
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *bootstrap, *clock;
    AstraConfigError config_error;
    AstraNtpSample sample;
    AstraNtpStatus last_failure = ASTRA_NTP_OK;
    uint32_t published, status;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    clock = astra_startup_capability(startup, ASTRA_CAPABILITY_CLOCK);
    if (bootstrap == NULL || clock == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    astra_posix_file_prepare();
    astra_posix_socket_prepare();
    astra_posix_start(startup);
    if (astra_process_vfs_init(startup) != ASTRA_VFS_OK)
        return ASTRA_STATUS_NOT_FOUND;
    config_handle = OpenLibrary(ASTRA_CONFIG_LIBRARY_NAME,
                                ASTRA_CONFIG_LIBRARY_VERSION);
    if (config_handle == NULL)
        return ASTRA_STATUS_NOT_FOUND;
    config_library = config_handle->exports;
    if (config_library->abi_major != ASTRA_CONFIG_LIBRARY_ABI_MAJOR ||
        config_library->structure_size < sizeof(*config_library))
        return ASTRA_STATUS_PROTOCOL;
    if (config_library->open(startup, 1u, ASTRA_CONFIG_OPEN_READ,
                             &config, &config_error) != ASTRA_CONFIG_OK)
        return ASTRA_STATUS_INVALID;
    if (astra_rt_port_create(4u, 4u * sizeof(AstraNtpControlRequest),
                             &control_receive, &control_send) !=
        ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_LIMIT;
    for (;;) {
        AstraNtpStatus sync_status = synchronize(NULL, &sample);

        if (sync_status == ASTRA_NTP_OK)
            break;
        if (sync_status != last_failure) {
            (void)astra_log(astra_ntp_status_text(sync_status));
            last_failure = sync_status;
        }
        (void)poll(NULL, 0u, 1000);
    }
    /* A valid local clock enables the full four-timestamp refinement. */
    (void)synchronize(NULL, &sample);
    published = control_send;
    status = astra_service_ready(bootstrap->handle, ASTRA_STATUS_OK,
                                 &published, 1u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_PEER_DEAD;
    for (;;) {
        uint64_t deadline = astra_clock_monotonic() + NTP_POLL_NS;

        status = astra_wait_one(control_receive, deadline, NULL);
        if (status == ASTRA_SYSCALL_OK)
            control();
        else if (status == ASTRA_SYSCALL_TIMED_OUT) {
            if (config_library->reload(&config, NULL) == ASTRA_CONFIG_OK)
                (void)synchronize(NULL, &sample);
        }
        else
            return ASTRA_STATUS_PEER_DEAD;
    }
}
