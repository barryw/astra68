#ifndef ASTRA_POSIX_PROCESS_H
#define ASTRA_POSIX_PROCESS_H

#include <stdint.h>

#include <astra/message_abi.h>
#include <astra/process.h>

#define ASTRA_CAPABILITY_POSIX_PROCESS "POSIX_PROCESS"
#define ASTRA_POSIX_PROCESS_PROTOCOL UINT32_C(0x50505243) /* PPRC */
#define ASTRA_POSIX_PROCESS_VERSION UINT16_C(3)

#define ASTRA_POSIX_PROCESS_REGISTER 1u
#define ASTRA_POSIX_PROCESS_SIGNAL   2u
#define ASTRA_POSIX_PROCESS_SETPGID  3u
#define ASTRA_POSIX_PROCESS_SETSID   4u
#define ASTRA_POSIX_PROCESS_QUERY    5u
#define ASTRA_POSIX_PROCESS_TTY_ATTACH 6u
#define ASTRA_POSIX_PROCESS_TTY_FOREGROUND 7u
#define ASTRA_POSIX_PROCESS_TTY_SET_FOREGROUND 8u
#define ASTRA_POSIX_PROCESS_TTY_SIGNAL 9u
#define ASTRA_POSIX_PROCESS_SIGNAL_GROUP 10u

#define ASTRA_POSIX_SIGNAL_INTERRUPT ASTRA_SIGNAL_INTERRUPT
#define ASTRA_POSIX_SIGNAL_QUIT      ASTRA_SIGNAL_QUIT
#define ASTRA_POSIX_SIGNAL_KILL      ASTRA_SIGNAL_KILL
#define ASTRA_POSIX_SIGNAL_CHILD     ASTRA_SIGNAL_CHILD
#define ASTRA_POSIX_SIGNAL_CONTINUE  ASTRA_SIGNAL_CONTINUE
#define ASTRA_POSIX_SIGNAL_STOP      ASTRA_SIGNAL_STOP
#define ASTRA_POSIX_SIGNAL_TTY_STOP  ASTRA_SIGNAL_TTY_STOP

#define ASTRA_POSIX_PROCESS_NEW_SESSION (1u << 0)
#define ASTRA_POSIX_PROCESS_FLAG_MASK ASTRA_POSIX_PROCESS_NEW_SESSION

typedef struct AstraPosixProcessRequest {
    AstraMessageHeader header;
    int32_t process;
    int32_t value;
    uint32_t flags;
    uint32_t reserved;
} AstraPosixProcessRequest;

typedef struct AstraPosixProcessReply {
    AstraMessageHeader header;
    uint32_t status;
    int32_t process;
    int32_t parent;
    int32_t group;
    int32_t session;
} AstraPosixProcessReply;

#define ASTRA_POSIX_PROCESS_REQUEST_SIZE 40u
#define ASTRA_POSIX_PROCESS_REPLY_SIZE 44u

_Static_assert(sizeof(AstraPosixProcessRequest) ==
                   ASTRA_POSIX_PROCESS_REQUEST_SIZE,
               "POSIX process request ABI changed");
_Static_assert(sizeof(AstraPosixProcessReply) ==
                   ASTRA_POSIX_PROCESS_REPLY_SIZE,
               "POSIX process reply ABI changed");

#endif
