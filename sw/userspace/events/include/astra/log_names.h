#ifndef ASTRA_LOG_NAMES_H
#define ASTRA_LOG_NAMES_H

/**
 * @file log_names.h
 * @brief Who each kernel-ring pid is, and where a log service resumes.
 *
 * `docs/SYSTEM_LOG.md` section 5. The kernel writes an
 * ::ASTRA_EVENT_MESSAGE_PROCESS_START chain into its ring before any other
 * record from a new image; ::astra_log_names_record folds those chains, in
 * ring order, into a pid -> name table, so a kernel-ring line is named from
 * the ring itself and never by asking a service.
 *
 * The table, the boot it belongs to and the last ring sequence whose text is
 * in the file are the service's resume point (::astra_log_state_encode). A
 * restarted service that finds the same boot continues after that sequence
 * with that table; anything else is a boot.
 */

#include <stdint.h>

#include <astra/event.h>
#include <astra/log_ring.h>

/**
 * Names remembered. A process that has not started or logged for this many
 * newer ones is forgotten, and its lines say `?`.
 *
 * ponytail: the ring says when a process starts, never when it ends, so the
 * table cannot drop the dead. Least recently used goes first; a kernel
 * process-exit record would let it hold exactly the live set.
 */
#define ASTRA_LOG_NAMES_MAX 128u

/** One pid's name: the basename of its argv[0], NUL-terminated. */
typedef struct AstraLogName {
    uint32_t pid;              /* 0: free */
    uint32_t used;             /* recency stamp, larger is newer */
    uint32_t pinned;           /* the boot set: evicted only when all are */
    char name[ASTRA_LOG_IDENT_MAX];
} AstraLogName;

/** The table, and the PROCESS_START chain being joined. */
typedef struct AstraLogNames {
    AstraLogName entry[ASTRA_LOG_NAMES_MAX];
    uint32_t clock;            /* last recency stamp handed out */
    uint32_t chain_pid;
    uint32_t chain_length;     /* bytes joined so far */
    uint32_t chain_open;       /* a head was seen and its tail is awaited */
    char chain[ASTRA_EVENT_ARGUMENT_MAX * 2u];
} AstraLogNames;

/** Forget everything, including an open chain. */
void astra_log_names_reset(AstraLogNames *names);

/**
 * Fold one ::ASTRA_EVENT_MESSAGE_PROCESS_START record into the table, in ring
 * order. A chain may span two calls (two drains). A record whose `activity`
 * (its byte offset in the name) does not continue the open chain for the
 * same pid is discarded: it is the tail of a chain whose head the ring
 * overwrote. The latest complete chain for a pid wins.
 *
 * @return Nonzero when the table changed.
 */
int astra_log_names_record(AstraLogNames *names,
                           const AstraEventDrained *record);

/** The name of `pid`, or NULL. Marks it recently used. */
const char *astra_log_names_find(AstraLogNames *names, uint32_t pid);

/**
 * Pin every name known now: the boot set, learned before the service went
 * live. The services that start at boot live for the whole boot and are the
 * lines most worth naming, so a stream of short-lived programs evicts every
 * unpinned name before any pinned one.
 */
void astra_log_names_pin(AstraLogNames *names);

/* ---------------------------------------------------------- ring records */

/**
 * One kernel-ring record's identity: its sequence, and what no other record
 * at that sequence would share -- the raw timestamp, process and message.
 * A record that is still in the ring with the same identity is the same
 * record, whatever the wall clock has done since. `sequence` 0: none.
 */
typedef struct AstraLogMark {
    uint32_t sequence;
    uint32_t process;
    uint32_t message;
    uint64_t stamp;            /* timestamp_high:timestamp_low */
} AstraLogMark;

/** `record`'s identity. */
AstraLogMark astra_log_mark(const AstraEventDrained *record);

/* ---------------------------------------------------------- resume state */

/**
 * Where a log service resumes. `sequence` is the last kernel-ring record
 * whose text is in the file (0: none). `mark` is a record of the boot that
 * wrote it: the record at `sequence`, or -- before anything is written --
 * this boot's first record. `boot` is the wall-clock identity, used only
 * when the ring no longer holds `mark`.
 */
typedef struct AstraLogResume {
    uint64_t boot;
    uint32_t sequence;
    AstraLogMark mark;
} AstraLogResume;

/** Largest encoding ::astra_log_state_encode produces. */
#define ASTRA_LOG_STATE_BYTES_MAX \
    (48u + ASTRA_LOG_NAMES_MAX * (12u + ASTRA_LOG_IDENT_MAX))

/**
 * The resume point and every name as bytes, with a magic, a version and a
 * CRC-32.
 *
 * @return Bytes written, or 0 when `capacity` is too small.
 */
uint32_t astra_log_state_encode(const AstraLogResume *resume,
                                const AstraLogNames *names, uint8_t *out,
                                uint32_t capacity);

/**
 * The inverse of ::astra_log_state_encode. `names` is reset first and holds
 * the decoded table on success. Anything malformed -- wrong length, magic,
 * version or CRC, an unterminated name -- is refused whole.
 *
 * @return Nonzero on success.
 */
int astra_log_state_decode(const uint8_t *bytes, uint32_t length,
                           AstraLogResume *resume, AstraLogNames *names);

/**
 * This boot's identity: the wall-clock second it began (wall - monotonic,
 * rounded), or 0 when the wall clock is not valid.
 */
uint64_t astra_log_boot_id(uint64_t wall_ns, uint64_t monotonic_ns,
                           int wall_valid);

/**
 * Where this boot begins in the kernel ring, from its records in ring order.
 *
 * The ring survives a warm reset (it is NOLOAD and the kernel keeps a valid
 * header) and its sequence carries on, but the timebase restarts: each
 * boot's records are stamped from its own reset. A record stamped earlier
 * than the one before it is therefore the first of a later boot.
 */
typedef struct AstraLogBootScan {
    uint64_t last;             /* the previous record's timestamp */
    uint32_t first;            /* first record of the newest boot; 0: the
                                  whole ring is one boot */
    uint32_t newest;           /* the newest record's sequence; 0: none */
    AstraLogMark anchor;       /* the newest boot's first record */
    uint32_t want;             /* in: a sequence to look for; 0: none */
    uint32_t found;            /* out: `want` was in the ring, as `at` */
    AstraLogMark at;
} AstraLogBootScan;

/** Feed one record, in ring order. Start from a zeroed scan, `want` set. */
void astra_log_boot_scan(AstraLogBootScan *scan,
                         const AstraEventDrained *record);

/**
 * Whether a saved resume point continues this boot (a service restart)
 * rather than an earlier one (a boot). Scan with `want` = the saved mark's
 * sequence.
 *
 * - The ring still holds the saved mark's sequence: a restart exactly when
 *   that record is the same one (::AstraLogMark) and is not before this
 *   boot's first. The wall clock plays no part, so a stepped clock cannot
 *   make a restart a boot, nor a cold boot without a clock a restart.
 * - It does not (the ring wrapped past it, or nothing was marked): the boot
 *   identities must agree to within a second -- the two clocks are sampled
 *   apart -- and the saved sequence must lie inside what the ring says is
 *   this boot: not after its newest record, and not before its first when
 *   that is known.
 */
int astra_log_state_restart(const AstraLogResume *saved, uint64_t boot,
                            const AstraLogBootScan *scan);

/** Whether ring sequence `sequence` is strictly after `other` (they wrap). */
int astra_log_sequence_after(uint32_t sequence, uint32_t other);

#endif
