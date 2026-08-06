#ifndef ASTRA_PROCESS_H
#define ASTRA_PROCESS_H

#define ASTRA_STARTUP_MAGIC 0x41535452u
#define ASTRA_STARTUP_ABI_VERSION 1u
#define ASTRA_STARTUP_INFO_SIZE 64u
#define ASTRA_STARTUP_CAPABILITY_SIZE 28u
#define ASTRA_CAPABILITY_NAME_MAX 16u
#define ASTRA_STARTUP_CAPABILITY_MAX 32u

#define ASTRA_STARTUP_FLAG_SUPERVISOR (1u << 0)

#define ASTRA_PROCESS_INFO_SIZE 48u

/*
 * Capability names a process always receives: itself and its first thread.
 *
 * Names rather than four-character codes, because this table became the
 * machine's namespace -- COMMANDS, DRIVERS, WORK -- and a name a person reads
 * in a startup manifest cannot be limited to what fits in a uint32.
 */
/*
 * What a launch hands a child.
 *
 * One grant is one name in the child's capability table, standing for a handle
 * the *caller already holds*, with rights that are a subset of the caller's. A
 * launch creates no authority: there is no path through it that produces a
 * capability which did not exist a moment earlier, which is what makes it safe
 * to expose as a syscall at all.
 */
#define ASTRA_LAUNCH_GRANT_MAX 6u
#define ASTRA_LAUNCH_ARGUMENT_MAX 8u
#define ASTRA_LAUNCH_ARGUMENT_BYTES 192u

typedef struct AstraLaunchGrant {
    char     name[ASTRA_CAPABILITY_NAME_MAX];  /* what the child calls it */
    uint32_t handle;                           /* the caller's own handle */
    uint32_t rights;                           /* a subset of what it holds */
    uint32_t flags;
} AstraLaunchGrant;

#define ASTRA_LAUNCH_GRANT_SIZE 28u
_Static_assert(sizeof(AstraLaunchGrant) == ASTRA_LAUNCH_GRANT_SIZE,
               "the launch grant is ABI: the kernel copies it in fixed steps");

/*
 * The whole of a launch's arguments in one block, so the syscall copies once
 * and validates once. `bytes` holds the argument words back to back, each
 * NUL-terminated; a count with no bytes behind it is refused rather than
 * treated as an empty vector, because a program's argv and its argc disagreeing
 * is a defect the child cannot detect.
 */
typedef struct AstraLaunchArguments {
    uint16_t count;
    uint16_t length;
    char     bytes[ASTRA_LAUNCH_ARGUMENT_BYTES];
} AstraLaunchArguments;

#define ASTRA_CAPABILITY_PROCESS "PROCESS"
#define ASTRA_CAPABILITY_THREAD  "THREAD"

#ifndef __ASSEMBLER__

#include <stddef.h>
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
    char     name[ASTRA_CAPABILITY_NAME_MAX];
    uint32_t handle;
    uint32_t rights;
    uint32_t flags;
} AstraStartupCapability;

_Static_assert(sizeof(AstraStartupInfo) == ASTRA_STARTUP_INFO_SIZE,
               "startup-info ABI size changed");
_Static_assert(sizeof(AstraProcessInfo) == ASTRA_PROCESS_INFO_SIZE,
               "process-info ABI size changed");

/*
 * Compares two capability names. Bounded on both sides: a field with no NUL in
 * it is not a name, and treating it as one would read past the record.
 */
static inline int
astra_capability_name_equal(const char *left, const char *right)
{
    uint32_t index = 0u;

    if (left == NULL || right == NULL) {
        return 0;
    }
    while (index < ASTRA_CAPABILITY_NAME_MAX) {
        if (left[index] != right[index]) {
            return 0;
        }
        if (left[index] == '\0') {
            return 1;
        }
        ++index;
    }
    return 0;   /* unterminated: not a name */
}

/* Writes a name into a capability entry, NUL-filling the rest of the field. */
static inline void
astra_capability_name_set(char *field, const char *name)
{
    uint32_t index = 0u;

    if (field == NULL) {
        return;
    }
    while (name != NULL && index + 1u < ASTRA_CAPABILITY_NAME_MAX &&
           name[index] != '\0') {
        field[index] = name[index];
        ++index;
    }
    while (index < ASTRA_CAPABILITY_NAME_MAX) {
        field[index++] = '\0';
    }
}

_Static_assert(sizeof(AstraStartupCapability) ==
                   ASTRA_STARTUP_CAPABILITY_SIZE,
               "startup-capability ABI size changed");

#endif

#endif
