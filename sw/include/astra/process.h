#ifndef ASTRA_PROCESS_H
#define ASTRA_PROCESS_H

/**
 * @file process.h
 * @brief The process ABI: startup info, launch and exec requests, and the
 *        introspection records a process may query about itself.
 *
 * This is the boundary between Axiom (the kernel) and every process it
 * creates: the startup block the kernel publishes into a new process's one
 * reserved VM page, the launch and exec syscalls' packed argument records,
 * and the fixed-size info records ASTRA_SYSCALL_PROCESS_QUERY and
 * ASTRA_SYSCALL_THREAD_QUERY copy back out. Included from assembly in some
 * build contexts, so the constants below are available with no C types in
 * scope; the structs and inline helpers that need real types live behind
 * `#ifndef __ASSEMBLER__`.
 */

#ifndef __ASSEMBLER__
#include <astra/compiler.h>
#include <stddef.h>
#include <stdint.h>
#endif

/** Startup-block signature ("ASTR") identifying a valid ::AstraStartupInfo. */
#define ASTRA_STARTUP_MAGIC 0x41535452u
/** Current ::AstraStartupInfo ABI revision the kernel publishes and requires. */
#define ASTRA_STARTUP_ABI_VERSION 7u
/** Fixed byte size of ::AstraStartupInfo. */
#define ASTRA_STARTUP_INFO_SIZE 84u
/** Fixed byte size of one ::AstraStartupCapability entry. */
#define ASTRA_STARTUP_CAPABILITY_SIZE 92u
/** The kernel publishes startup state in the machine's one 4 KiB VM page. */
#define ASTRA_STARTUP_BLOCK_SIZE 4096u
/**
 * Maximum length of a capability's mount-relative root path, including the
 * terminating NUL.
 *
 * Where a granted name begins inside its mount, and the one number for it.
 * A grant that could not say this made every binding a child built land at
 * the mount's own root, so a child holding COMMANDS: was holding the whole
 * volume. Sixty-four bytes because that is what an assign's own root costs;
 * a shorter field here would be a second limit, a truncation rule, and an
 * explanation that never ends.
 */
#define ASTRA_CAPABILITY_ROOT_MAX 64u
/** Maximum length of a capability name, including the terminating NUL. */
#define ASTRA_CAPABILITY_NAME_MAX 16u
/** Maximum capability entries that fit in the startup block after ::AstraStartupInfo. */
#define ASTRA_STARTUP_CAPABILITY_MAX \
    ((ASTRA_STARTUP_BLOCK_SIZE - ASTRA_STARTUP_INFO_SIZE) / \
     ASTRA_STARTUP_CAPABILITY_SIZE)
/** Largest visible process identifier; PID zero is reserved. */
#define ASTRA_PROCESS_ID_MAX 65535u

/**
 * Startup flag bit reserved to mark the process as the supervisor.
 *
 * Part of ::ASTRA_STARTUP_FLAG_MASK; the kernel does not currently set this
 * bit when publishing startup state.
 */
#define ASTRA_STARTUP_FLAG_SUPERVISOR (1u << 0)
/** The initial PC belongs to the mapped interpreter, not the program. */
#define ASTRA_STARTUP_FLAG_INTERPRETED (1u << 1)
/** Every valid ::AstraStartupInfo flag bit. */
#define ASTRA_STARTUP_FLAG_MASK \
    (ASTRA_STARTUP_FLAG_SUPERVISOR | ASTRA_STARTUP_FLAG_INTERPRETED)

/** Fixed byte size of ::AstraProcessInfo. */
#define ASTRA_PROCESS_INFO_SIZE 80u
/** Fixed byte size of ::AstraThreadInfo. */
#define ASTRA_THREAD_INFO_SIZE 48u

/*
 * Scheduler priority is one number per thread; larger runs first, and equal
 * priorities share the CPU in ::ASTRA_PROCESS_QUANTUM_NS turns. A process
 * carries a default its new threads take. The bands below are names for
 * ranges of that one number, not a second setting:
 *
 *   1-7    background
 *   8-19   applications; 16 is every process's default
 *   20-23  system services
 *   24-27  media: audio and other work with a deadline
 *
 * 0 is the idle loop and 28-31 are unassigned. The kernel is not on the
 * scale: its traps and deferred work always run before any thread.
 *
 * A process may move itself and its threads anywhere from
 * ::ASTRA_PROCESS_PRIORITY_MIN to the higher of
 * ::ASTRA_PROCESS_PRIORITY_APPLICATION_MAX and the priority it was launched
 * at. Only a launcher can place a child above the application band
 * (::AstraLaunchArguments.priority). A process holding an open audio stream
 * (%audio_stream.h) may also place its threads up to
 * ::ASTRA_PROCESS_PRIORITY_MEDIA: the thread that feeds the stream has the
 * audio's deadline, as Haiku runs a BSoundPlayer thread at urgent priority.
 */
/** Lowest scheduler priority a thread may hold; larger values run first. */
#define ASTRA_PROCESS_PRIORITY_MIN 1u
/** Default scheduler priority of a process launched without one. */
#define ASTRA_PROCESS_PRIORITY_NORMAL 16u
/** Top of the application band: as high as any process may raise itself. */
#define ASTRA_PROCESS_PRIORITY_APPLICATION_MAX 19u
/** Bottom of the system-service band. */
#define ASTRA_PROCESS_PRIORITY_SYSTEM 20u
/** Bottom of the media band: audio runs here so a busy application cannot starve it. */
#define ASTRA_PROCESS_PRIORITY_MEDIA 24u
/** Highest scheduler priority a thread may hold; larger values run first. */
#define ASTRA_PROCESS_PRIORITY_MAX 27u
/** Equal-priority runnable threads rotate at this scheduler quantum, in nanoseconds. */
#define ASTRA_PROCESS_QUANTUM_NS UINT32_C(5000000)
/** Most negative (highest-priority) nice value, relative to ::ASTRA_PROCESS_PRIORITY_NORMAL. */
#define ASTRA_PROCESS_NICE_MIN \
    ((int)ASTRA_PROCESS_PRIORITY_NORMAL - (int)ASTRA_PROCESS_PRIORITY_MAX)
/** Largest (lowest-priority) nice value, relative to ::ASTRA_PROCESS_PRIORITY_NORMAL. */
#define ASTRA_PROCESS_NICE_MAX \
    ((int)ASTRA_PROCESS_PRIORITY_NORMAL - (int)ASTRA_PROCESS_PRIORITY_MIN)

/* Signal numbers are part of Astra's m68k process ABI. */
/** Hangup: sent to a session's children when its controlling terminal disconnects. */
#define ASTRA_SIGNAL_HANGUP    1u
/** Interrupt: the POSIX SIGINT equivalent. */
#define ASTRA_SIGNAL_INTERRUPT 2u
/** Quit: the POSIX SIGQUIT equivalent. */
#define ASTRA_SIGNAL_QUIT      3u
/** Kill: the POSIX SIGKILL equivalent; cannot be blocked. */
#define ASTRA_SIGNAL_KILL      9u
/** Alarm: the POSIX SIGALRM equivalent, delivered when a requested timer expires. */
#define ASTRA_SIGNAL_ALARM     14u
/** Terminate: the POSIX SIGTERM equivalent. */
#define ASTRA_SIGNAL_TERMINATE 15u
/** Stop: the POSIX SIGSTOP equivalent; cannot be blocked. */
#define ASTRA_SIGNAL_STOP      17u
/** TTY stop: the POSIX SIGTSTP equivalent. */
#define ASTRA_SIGNAL_TTY_STOP  18u
/** Continue: the POSIX SIGCONT equivalent, resuming a stopped process. */
#define ASTRA_SIGNAL_CONTINUE  19u
/** Child: the POSIX SIGCHLD equivalent, delivered on a child's state change. */
#define ASTRA_SIGNAL_CHILD     20u
/** Window: the POSIX SIGWINCH equivalent, delivered on a terminal resize. */
#define ASTRA_SIGNAL_WINDOW    28u

/**
 * Maximum ::AstraLaunchGrant entries one launch call may supply.
 *
 * Capability names a process always receives: itself and its first thread.
 * Names rather than four-character codes, because this table became the
 * machine's namespace -- COMMANDS, DRIVERS, WORK -- and a name a person reads
 * in a startup manifest cannot be limited to what fits in a uint32.
 *
 * What a launch hands a child is one name in the child's capability table,
 * standing for a handle the *caller already holds*, with rights that are a
 * subset of the caller's. A launch creates no authority: there is no path
 * through it that produces a capability which did not exist a moment
 * earlier, which is what makes it safe to expose as a syscall at all.
 *
 * A child always spends two startup slots naming itself and its first
 * thread. Every remaining slot may be granted by its launcher; the startup
 * page's actual packing check decides how many fit beside argv and the
 * environment. There is no second deployment-sized ceiling to raise when a
 * service gains a capability.
 */
#define ASTRA_LAUNCH_GRANT_MAX (ASTRA_STARTUP_CAPABILITY_MAX - 2u)
/**
 * Physical upper bound on launch argument count.
 *
 * Arguments share the startup page with its capability table and
 * environment. These are physical upper bounds only; the kernel accepts the
 * actual mixture when its pointer vector and strings fit the page. One
 * empty argument costs one byte and one four-byte pointer, so no valid page
 * can exceed this count.
 */
#define ASTRA_LAUNCH_ARGUMENT_BYTES ASTRA_STARTUP_BLOCK_SIZE
/** Physical upper bound on launch argument count; one empty argument costs 5 bytes. */
#define ASTRA_LAUNCH_ARGUMENT_MAX \
    ((ASTRA_STARTUP_BLOCK_SIZE - ASTRA_STARTUP_INFO_SIZE - \
      (2u * ASTRA_STARTUP_CAPABILITY_SIZE)) / 5u)
/**
 * Launch flag: the child is essential and must not be refused.
 *
 * Resource authority, not an application preference. Only the trusted
 * initial supervisor may launch an essential child; the kernel refuses this
 * bit from every other launcher.
 */
#define ASTRA_LAUNCH_FLAG_ESSENTIAL (1u << 0)
/** Every valid ::AstraLaunchArguments flag bit. */
#define ASTRA_LAUNCH_FLAG_MASK ASTRA_LAUNCH_FLAG_ESSENTIAL
/** Fixed ELF32 header; streaming loaders never need a larger initial window. */
#define ASTRA_EXECUTABLE_HEADER_SIZE 52u
/**
 * Byte size of the environment block's backing storage: the whole startup
 * page.
 *
 * Environment space is the startup page, not a smaller policy quota. The
 * kernel accepts the combination of capabilities, argv, pointers and strings
 * only when the actual packed block fits that page. Seven is the minimum
 * cost of one valid entry: "A=\0" plus its four-byte vector slot.
 */
#define ASTRA_LAUNCH_ENVIRONMENT_BYTES ASTRA_STARTUP_BLOCK_SIZE
/** Physical upper bound on environment entry count; one valid entry costs at least 7 bytes. */
#define ASTRA_LAUNCH_ENVIRONMENT_MAX (ASTRA_STARTUP_BLOCK_SIZE / 7u)

/** Where a launch request originated. */
typedef enum AstraLaunchSource {
    /** Launched by the system itself, with no interactive originator (e.g. boot, a service). */
    ASTRA_LAUNCH_SOURCE_SYSTEM = 0u,
    /** Launched from an interactive shell. */
    ASTRA_LAUNCH_SOURCE_SHELL = 1u,
    /** Launched from the desktop. */
    ASTRA_LAUNCH_SOURCE_DESKTOP = 2u
} AstraLaunchSource;

/**
 * What a grant is *for*, which is not the same question as what it confers.
 *
 * A capability table publishes every kind of authority a process holds, and
 * they are not all the same kind of thing. `WORK` is a name in a namespace and
 * `WORK:src/main.c` means something; `STDOUT` is a place to write and
 * `STDOUT:src/main.c` is nonsense. Without a flag the only way to tell them
 * apart is a list of names that are not mounts, which grows every time a new
 * kind of capability is invented and is kept in a file that has nothing to do
 * with any of them.
 *
 * So a grant says which it is, and a namespace is seeded from the ones that
 * said so. The rule is positive: a capability is not a name unless somebody
 * declared it one. The two the kernel installs for every process, PROCESS and
 * THREAD, carry no flags and are therefore excluded by construction rather
 * than by being remembered.
 *
 * Unknown bits are refused rather than passed through. A bit nobody interprets
 * today is a bit that means something else tomorrow, and accepting it now makes
 * the field unversionable.
 */
#define ASTRA_CAPABILITY_FLAG_NAMESPACE (1u << 0)
/**
 * And what the child may do with the name, which is a different vocabulary
 * from `rights` and cannot share the word with it.
 *
 * `rights` is what the *kernel* enforces on the handle: a port send endpoint
 * carries READ, SIGNAL, WAIT and TRANSFER, and a grant asking for more than
 * that is refused -- correctly, because there is no such authority to give.
 * "May write files through this mount" is not a property of a port; it is a
 * property of the mount, enforced above the kernel by the same Kit that
 * enforces it for the process doing the granting.
 *
 * Putting it here rather than in `rights` is what keeps the kernel out of
 * deciding what a name means. It carries these bits and never reads them.
 */
#define ASTRA_CAPABILITY_FLAG_READ      (1u << 1)
/** Grant flag: the child may write through the named mount. */
#define ASTRA_CAPABILITY_FLAG_WRITE     (1u << 2)
/** Every valid capability grant flag bit. */
#define ASTRA_CAPABILITY_FLAG_MASK                                            \
    (ASTRA_CAPABILITY_FLAG_NAMESPACE | ASTRA_CAPABILITY_FLAG_READ |           \
     ASTRA_CAPABILITY_FLAG_WRITE)

/**
 * One capability grant: one name in a launched child's capability table.
 *
 * See ::ASTRA_LAUNCH_GRANT_MAX for what a grant confers and why a launch can
 * never create authority that did not already exist.
 */
typedef struct AstraLaunchGrant {
    /** What the child calls it. */
    char     name[ASTRA_CAPABILITY_NAME_MAX];
    /** The caller's own handle being granted. */
    uint32_t handle;
    /** Rights granted on the handle; a subset of what the caller holds. */
    uint32_t rights;
    /** Combination of ::ASTRA_CAPABILITY_FLAG_NAMESPACE, ::ASTRA_CAPABILITY_FLAG_READ and ::ASTRA_CAPABILITY_FLAG_WRITE. */
    uint32_t flags;
    /**
     * Normalised, mount-relative, no leading separator: "commands", or "" for
     * the mount's own root. Carried by the kernel and never read by it, the
     * same contract `flags` has -- what a name means is the launcher's
     * statement to the child.
     */
    char     root[ASTRA_CAPABILITY_ROOT_MAX];
} AstraLaunchGrant;

/** Fixed byte size of ::AstraLaunchGrant. */
#define ASTRA_LAUNCH_GRANT_SIZE 92u
/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraLaunchGrant) == ASTRA_LAUNCH_GRANT_SIZE,
               "the launch grant is ABI: the kernel copies it in fixed steps");
/** @endcond */

/**
 * The whole of a launch's arguments in one block, so the syscall copies once
 * and validates once.
 *
 * `bytes` holds the argument words back to back, each NUL-terminated; a
 * count with no bytes behind it is refused rather than treated as an empty
 * vector, because a program's argv and its argc disagreeing is a defect the
 * child cannot detect.
 */
typedef struct AstraLaunchArguments {
    /** Number of argument words. */
    uint16_t count;
    /** Total byte length of the packed argument words, each NUL-terminated. */
    uint16_t length;
    /** One of ::AstraLaunchSource. */
    uint16_t source;
    /**
     * Combination of ::ASTRA_LAUNCH_FLAG_ESSENTIAL.
     *
     * Resource authority, not an application preference. Only the trusted
     * initial supervisor may launch an essential child; the kernel refuses
     * this bit from every other launcher.
    */
    uint16_t flags;
    /** User address of the packed argument words. */
    uint32_t argument_address;
    /** Number of environment entries. */
    uint16_t environment_count;
    /** Total byte length of the packed environment entries, each NUL-terminated. */
    uint16_t environment_length;
    /** User address of the packed environment entries. */
    uint32_t environment_address;
    /**
     * The child's scheduler priority, or zero for
     * ::ASTRA_PROCESS_PRIORITY_NORMAL. A launcher may not place a child above
     * its own ceiling, except the trusted initial supervisor, which places
     * services anywhere on the scale. Must be zero for exec, which keeps the
     * process's priority.
     */
    uint16_t priority;
    /** Must be zero. */
    uint16_t reserved;
} AstraLaunchArguments;

/** Fixed byte size of ::AstraLaunchArguments. */
#define ASTRA_LAUNCH_ARGUMENTS_SIZE 24u
/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraLaunchArguments) == ASTRA_LAUNCH_ARGUMENTS_SIZE,
               "launch-argument syscall ABI changed");
/** @endcond */

/**
 * Atomic image replacement.
 *
 * Arguments use the same packed representation as launch. `handoff` is
 * deliberately opaque to Axiom: a personality can carry state which
 * survives an exec without putting POSIX policy in the kernel.
 */
typedef struct AstraExecRequest {
    /** Size of this record; validated against ::ASTRA_EXEC_REQUEST_SIZE. */
    uint32_t size;
    /** The replacement image's packed launch arguments. */
    AstraLaunchArguments arguments;
    /** User address of the opaque handoff block carried across the exec, or zero for none. */
    uint32_t handoff_address;
    /** Byte size of the handoff block; zero exactly when `handoff_address` is zero. */
    uint32_t handoff_size;
} AstraExecRequest;

/** Fixed byte size of ::AstraExecRequest. */
#define ASTRA_EXEC_REQUEST_SIZE 36u
/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraExecRequest) == ASTRA_EXEC_REQUEST_SIZE,
               "exec-request syscall ABI changed");
/** @endcond */

/** Capability name every process receives for itself. */
#define ASTRA_CAPABILITY_PROCESS "PROCESS"
/** Capability name every process receives for its first thread. */
#define ASTRA_CAPABILITY_THREAD  "THREAD"
/* Read-only library namespace consumed and closed by loader.library. */

#ifndef __ASSEMBLER__

/**
 * The startup block the kernel publishes into a new process's reserved VM
 * page.
 *
 * Logical addresses are represented as integers at the process ABI
 * boundary.
 */
typedef struct AstraStartupInfo {
    /** ::ASTRA_STARTUP_MAGIC. */
    uint32_t magic;
    /** ::ASTRA_STARTUP_ABI_VERSION. */
    uint16_t abi_version;
    /** ::ASTRA_STARTUP_INFO_SIZE. */
    uint16_t header_size;
    /** Complete published byte count: this header, the capability table, argv and the environment. */
    uint32_t total_size;
    /** The kernel's `ASTRA_SYSCALL_ABI_VERSION`, checked against the caller's own build. */
    uint32_t syscall_abi_version;
    /** Combination of ::ASTRA_STARTUP_FLAG_MASK. */
    uint32_t flags;
    /** The process's own handle to itself. */
    uint32_t process_handle;
    /** The process's handle to its first thread. */
    uint32_t thread_handle;
    /** Number of argument words at `argv_address`. */
    uint32_t argc;
    /** User address of the packed argument words. */
    uint32_t argv_address;
    /** Number of environment entries at `environment_address`. */
    uint32_t environment_count;
    /** User address of the packed environment entries. */
    uint32_t environment_address;
    /** Number of ::AstraStartupCapability entries at `capabilities_address`; always at least two (PROCESS and THREAD). */
    uint32_t capability_count;
    /** User address of the packed ::AstraStartupCapability table. */
    uint32_t capabilities_address;
    /** One of ::AstraLaunchSource. */
    uint32_t launch_source;
    /** User address of the opaque handoff block carried across an exec, or zero for none. */
    uint32_t handoff_address;
    /** Byte size of the handoff block; zero exactly when `handoff_address` is zero. */
    uint32_t handoff_size;
    /** Original executable entry retained while the interpreter bootstraps. */
    uint32_t program_entry;
    /** Load bias added to every interpreter virtual address. */
    uint32_t interpreter_base;
    /** Exact mapped interpreter span, including holes between segments. */
    uint32_t interpreter_span;
    /** Interpreter entry after applying `interpreter_base`. */
    uint32_t interpreter_entry;
    /** Exact PT_LOAD memory bytes writable by the original executable. */
    uint32_t program_writable_bytes;
} AstraStartupInfo;

/**
 * What a process may learn about a process it holds a handle to.
 *
 * Enumeration of other processes is deliberately not a syscall: it belongs
 * to an introspection service holding observer authority, per
 * OBSERVABILITY.md. A process always holds its own handle, so
 * self-inspection needs nothing more.
 */
typedef struct AstraProcessInfo {
    /** Size of this record; always `sizeof(AstraProcessInfo)`. */
    uint32_t size;
    /** The process identifier. */
    uint32_t id;
    /** Generation counter distinguishing reused process slots. */
    uint32_t generation;
    /** Memory-owner identifier the process's pages are charged to. */
    uint32_t owner;
    /** Physical page frames currently resident and charged to the process. */
    uint32_t resident_frames;
    /** Number of times a thread of the process has been scheduled to run. */
    uint32_t run_count;
    /** Timer ticks the process has been charged. */
    uint32_t timer_ticks;
    /** Total syscalls issued by the process's threads. */
    uint32_t syscall_count;
    /** Exit status, meaningful once the process has exited. */
    uint32_t exit_status;
    /** Live handle references to this process held across the system. */
    uint16_t handle_references;
    /** Kernel process lifecycle state; an opaque numeric code owned by the kernel. */
    uint8_t process_state;
    /** Number of threads the process has ever created. */
    uint8_t thread_count;
    /** Number of threads currently live. */
    uint8_t live_threads;
    /** The process's default scheduler priority. */
    uint8_t default_priority;
    /** Highest scheduler priority the process may raise a thread to. */
    uint8_t priority_ceiling;
    /** Kernel process exit reason; an opaque numeric code owned by the kernel. */
    uint8_t exit_reason;
    /** Representative state of the process's threads; an opaque numeric code owned by the kernel. */
    uint8_t thread_state;
    /** Nonzero while the process is suspended. */
    uint8_t suspended;
    /** Reserved padding, always zero. */
    uint8_t reserved[2];
    /** Total CPU time charged to the process's threads, in nanoseconds. */
    uint64_t runtime_ns;
    /** Wall-clock time since the process started, in nanoseconds. */
    uint64_t elapsed_ns;
    /** Faulting program counter, meaningful after a fault exit. */
    uint32_t fault_pc;
    /** Faulting address, meaningful after a fault exit. */
    uint32_t fault_address;
    /** Faulting exception vector, meaningful after a fault exit. */
    uint16_t fault_vector;
    /** Faulting status word, meaningful after a fault exit. */
    uint16_t fault_status;
    /** Lifetime high-water mark of owner-charged resident memory, in frames. */
    uint32_t peak_resident_frames;
} AstraProcessInfo;

/**
 * What a process may learn about a thread it holds a QUERY capability to.
 *
 * Runtime uses the scheduler's own accounting interval, so a caller querying
 * itself receives all execution completed before the query entered Axiom.
 */
typedef struct AstraThreadInfo {
    /** Size of this record; always `sizeof(AstraThreadInfo)`. */
    uint32_t size;
    /** The thread identifier. */
    uint32_t id;
    /** Generation counter distinguishing reused thread slots. */
    uint32_t generation;
    /** Identifier of the process the thread belongs to. */
    uint32_t process_id;
    /** Number of times the thread has been scheduled to run. */
    uint32_t run_count;
    /** Timer ticks the thread has been charged. */
    uint32_t timer_ticks;
    /** Total syscalls issued by the thread. */
    uint32_t syscall_count;
    /** Kernel activity counter for the thread; an opaque numeric code owned by the kernel. */
    uint32_t activity;
    /** CPU time charged to the thread, in nanoseconds. */
    uint64_t runtime_ns;
    /** Live handle references to this thread held across the system. */
    uint16_t handle_references;
    /** Kernel thread lifecycle state; an opaque numeric code owned by the kernel. */
    uint8_t state;
    /** The thread's base scheduler priority. */
    uint8_t base_priority;
    /** The thread's current effective scheduler priority. */
    uint8_t effective_priority;
    /** Nonzero while the thread is suspended. */
    uint8_t suspended;
    /** Reserved padding, always zero. */
    uint8_t reserved[2];
} AstraThreadInfo;

/** One capability entry as published in the startup block. */
typedef struct AstraStartupCapability {
    /** What the child calls it. */
    char     name[ASTRA_CAPABILITY_NAME_MAX];
    /** The granted handle. */
    uint32_t handle;
    /** Rights granted on the handle. */
    uint32_t rights;
    /** Combination of ::ASTRA_CAPABILITY_FLAG_NAMESPACE, ::ASTRA_CAPABILITY_FLAG_READ and ::ASTRA_CAPABILITY_FLAG_WRITE. */
    uint32_t flags;
    /** Where the name begins inside its mount, "" for the mount's own root. */
    char     root[ASTRA_CAPABILITY_ROOT_MAX];
} AstraStartupCapability;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraStartupInfo) == ASTRA_STARTUP_INFO_SIZE,
               "startup-info ABI size changed");
_Static_assert(ASTRA_STARTUP_INFO_SIZE +
                       ASTRA_STARTUP_CAPABILITY_MAX *
                           ASTRA_STARTUP_CAPABILITY_SIZE <=
                   ASTRA_STARTUP_BLOCK_SIZE,
               "startup capabilities exceed their page");
_Static_assert(ASTRA_STARTUP_BLOCK_SIZE -
                       (ASTRA_STARTUP_INFO_SIZE +
                        ASTRA_STARTUP_CAPABILITY_MAX *
                            ASTRA_STARTUP_CAPABILITY_SIZE) <
                   ASTRA_STARTUP_CAPABILITY_SIZE,
               "startup page has room for another capability");
_Static_assert(sizeof(AstraProcessInfo) == ASTRA_PROCESS_INFO_SIZE,
               "process-info ABI size changed");
_Static_assert(sizeof(AstraThreadInfo) == ASTRA_THREAD_INFO_SIZE,
               "thread-info ABI size changed");
/** @endcond */

/**
 * Compares two capability names.
 *
 * Bounded on both sides: a field with no NUL in it is not a name, and
 * treating it as one would read past the record.
 *
 * @param left  First name field, exactly ::ASTRA_CAPABILITY_NAME_MAX bytes.
 * @param right Second name field, exactly ::ASTRA_CAPABILITY_NAME_MAX bytes.
 * @return Nonzero if both are NUL-terminated within the field and equal; zero otherwise, including when either pointer is NULL.
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

/**
 * Writes a name into a capability entry, NUL-filling the rest of the field.
 *
 * @param field Destination name field, exactly ::ASTRA_CAPABILITY_NAME_MAX bytes; does nothing if NULL.
 * @param name  NUL-terminated source name, truncated to fit the field; a NULL name yields an empty field.
 */
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

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraStartupCapability) ==
                   ASTRA_STARTUP_CAPABILITY_SIZE,
               "startup-capability ABI size changed");
/** @endcond */

/**
 * Copies and pads a root into a bounded ABI field.
 *
 * NULL is the empty root, which is what the firmware's own grants carry:
 * they name devices, not places in a filesystem, and a NULL here would
 * otherwise be a special case at every call site instead of one.
 *
 * @param field Destination root field, exactly ::ASTRA_CAPABILITY_ROOT_MAX bytes; does nothing if NULL.
 * @param root  NUL-terminated source root, truncated to fit the field; NULL yields an empty (mount-root) field.
 */
static inline void
astra_capability_root_set(char *field, const char *root)
{
    uint32_t index = 0u;

    if (field == NULL) {
        return;
    }
    while (root != NULL && index + 1u < ASTRA_CAPABILITY_ROOT_MAX &&
           root[index] != '\0') {
        field[index] = root[index];
        ++index;
    }
    while (index < ASTRA_CAPABILITY_ROOT_MAX) {
        field[index++] = '\0';
    }
}

#endif

#endif
