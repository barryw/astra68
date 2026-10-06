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

/* ---------------------------------------------------------- resume state */

/** Largest encoding ::astra_log_state_encode produces. */
#define ASTRA_LOG_STATE_BYTES_MAX \
    (28u + ASTRA_LOG_NAMES_MAX * (8u + ASTRA_LOG_IDENT_MAX))

/**
 * The resume point as bytes: `boot`, `sequence` and every name, with a
 * magic, a version and a CRC-32.
 *
 * @return Bytes written, or 0 when `capacity` is too small.
 */
uint32_t astra_log_state_encode(uint64_t boot, uint32_t sequence,
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
                           uint64_t *boot, uint32_t *sequence,
                           AstraLogNames *names);

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
} AstraLogBootScan;

/** Feed one record, in ring order. Start from a zeroed scan. */
void astra_log_boot_scan(AstraLogBootScan *scan,
                         const AstraEventDrained *record);

/**
 * Whether a saved resume point continues this boot (a service restart)
 * rather than an earlier one (a boot). Both the boot identities must agree
 * to within a second -- the two clocks are sampled apart -- and the saved
 * sequence must lie inside what the ring says is this boot: not after its
 * newest record, and not before its first when that is known.
 */
int astra_log_state_restart(uint64_t saved_boot, uint32_t saved_sequence,
                            uint64_t boot, const AstraLogBootScan *scan);

/** Whether ring sequence `sequence` is strictly after `other` (they wrap). */
int astra_log_sequence_after(uint32_t sequence, uint32_t other);

#endif
