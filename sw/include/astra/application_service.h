#ifndef ASTRA_APPLICATION_SERVICE_H
#define ASTRA_APPLICATION_SERVICE_H

#include <stdint.h>

#include <astra/process.h>
#include <astra/syscall.h>

#define ASTRA_CAPABILITY_APPLICATION_LAUNCH "APP_LAUNCH"

#define ASTRA_APPLICATION_PROTOCOL UINT32_C(0x4150504c) /* APPL */
#define ASTRA_APPLICATION_VERSION 2u

#define ASTRA_APPLICATION_LAUNCH 1u
#define ASTRA_APPLICATION_LAUNCHED 2u
#define ASTRA_APPLICATION_PATH_MAX 128u

/* Inline application-launch transport.  The syscall itself accepts the full
 * startup page; this service record is bounded by its existing port budget. */
#define ASTRA_APPLICATION_ARGUMENT_BYTES 192u
#define ASTRA_APPLICATION_ARGUMENT_MAX \
    (ASTRA_APPLICATION_ARGUMENT_BYTES / 2u)

typedef struct AstraApplicationLaunchArguments {
    uint16_t count;
    uint16_t length;
    uint16_t source;
    uint16_t flags;
    char bytes[ASTRA_APPLICATION_ARGUMENT_BYTES];
    uint16_t environment_count;
    uint16_t environment_length;
    uint32_t environment_address;
} AstraApplicationLaunchArguments;

typedef struct AstraApplicationLaunchRequest {
    AstraMessageHeader header;
    AstraApplicationLaunchArguments arguments;
} AstraApplicationLaunchRequest;

typedef struct AstraApplicationLaunchReply {
    AstraMessageHeader header;
    uint32_t status;
    uint32_t process_id;
} AstraApplicationLaunchReply;

#define ASTRA_APPLICATION_LAUNCH_REQUEST_SIZE 232u
#define ASTRA_APPLICATION_LAUNCH_REPLY_SIZE 32u

_Static_assert(sizeof(AstraApplicationLaunchRequest) ==
                   ASTRA_APPLICATION_LAUNCH_REQUEST_SIZE,
               "application launch request ABI changed");
_Static_assert(sizeof(AstraApplicationLaunchReply) ==
                   ASTRA_APPLICATION_LAUNCH_REPLY_SIZE,
               "application launch reply ABI changed");

#endif
