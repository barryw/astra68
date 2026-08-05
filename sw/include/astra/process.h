#ifndef ASTRA_PROCESS_H
#define ASTRA_PROCESS_H

#define ASTRA_STARTUP_MAGIC 0x41535452u
#define ASTRA_STARTUP_ABI_VERSION 1u
#define ASTRA_STARTUP_INFO_SIZE 64u
#define ASTRA_STARTUP_CAPABILITY_SIZE 16u
#define ASTRA_STARTUP_CAPABILITY_MAX 32u

#define ASTRA_STARTUP_FLAG_SUPERVISOR (1u << 0)

#define ASTRA_PROCESS_INFO_SIZE 48u

#ifndef __ASSEMBLER__

#include <stdint.h>

/* Logical addresses are represented as integers at the process ABI boundary. */
typedef struct AstraStartupInfo {
    uint32_t magic;
    uint16_t abi_version;
    uint16_t header_size;
    uint32_t total_size;
    uint32_t syscall_abi_version;
    uint32_t flags;
    uint32_t process_handle;
    uint32_t thread_handle;
    uint32_t argc;
    uint32_t argv_address;
    uint32_t environment_count;
    uint32_t environment_address;
    uint32_t capability_count;
    uint32_t capabilities_address;
    uint32_t reserved[3];
} AstraStartupInfo;

/*
 * What a process may learn about a process it holds a handle to. Enumeration
 * of other processes is deliberately not a syscall: it belongs to an
 * introspection service holding observer authority, per OBSERVABILITY.md. A
 * process always holds its own handle, so self-inspection needs nothing more.
 */
typedef struct AstraProcessInfo {
    uint32_t size;
    uint32_t id;
    uint32_t generation;
    uint32_t owner;
    uint32_t resident_frames;
    uint32_t run_count;
    uint32_t timer_ticks;
    uint32_t syscall_count;
    uint32_t exit_status;
    uint16_t handle_references;
    uint8_t process_state;
    uint8_t thread_count;
    uint8_t live_threads;
    uint8_t default_priority;
    uint8_t priority_ceiling;
    uint8_t exit_reason;
    uint32_t reserved;
} AstraProcessInfo;

typedef struct AstraStartupCapability {
    uint32_t name;
    uint32_t handle;
    uint32_t rights;
    uint32_t flags;
} AstraStartupCapability;

_Static_assert(sizeof(AstraStartupInfo) == ASTRA_STARTUP_INFO_SIZE,
               "startup-info ABI size changed");
_Static_assert(sizeof(AstraProcessInfo) == ASTRA_PROCESS_INFO_SIZE,
               "process-info ABI size changed");
_Static_assert(sizeof(AstraStartupCapability) ==
                   ASTRA_STARTUP_CAPABILITY_SIZE,
               "startup-capability ABI size changed");

#endif

#endif
