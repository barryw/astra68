#ifndef ASTRA_SHELL_SERVICE_H
#define ASTRA_SHELL_SERVICE_H

#include <stdint.h>

#include <astra/syscall.h>

#define ASTRA_CAPABILITY_SHELL "SHELL"

#define ASTRA_SHELL_SERVICE_PROTOCOL UINT32_C(0x53484c4c) /* SHLL */
#define ASTRA_SHELL_SERVICE_VERSION UINT16_C(1)

#define ASTRA_SHELL_EXECUTE UINT32_C(1)
#define ASTRA_SHELL_EXECUTED UINT32_C(2)
#define ASTRA_SHELL_HANDLE_NONE UINT8_C(0xff)

typedef struct AstraShellExecuteRequest {
    AstraMessageHeader header;
    uint32_t command_length;
    uint32_t environment_length;
    uint16_t environment_count;
    uint8_t stdin_index;
    uint8_t stdout_index;
    uint8_t stderr_index;
    uint8_t handle_count;
    uint16_t reserved;
} AstraShellExecuteRequest;

typedef struct AstraShellExecuteReply {
    AstraMessageHeader header;
    uint32_t status;
    uint32_t command_status;
} AstraShellExecuteReply;

#define ASTRA_SHELL_EXECUTE_REQUEST_SIZE UINT32_C(40)
#define ASTRA_SHELL_EXECUTE_REPLY_SIZE UINT32_C(32)

_Static_assert(sizeof(AstraShellExecuteRequest) ==
                   ASTRA_SHELL_EXECUTE_REQUEST_SIZE,
               "shell execute request ABI changed");
_Static_assert(sizeof(AstraShellExecuteReply) ==
                   ASTRA_SHELL_EXECUTE_REPLY_SIZE,
               "shell execute reply ABI changed");

#endif
