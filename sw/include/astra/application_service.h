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

typedef struct AstraApplicationLaunchRequest {
    AstraMessageHeader header;
    AstraLaunchArguments arguments;
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
