#ifndef ASTRA_LOG_CLIENTS_H
#define ASTRA_LOG_CLIENTS_H

/**
 * @file log_clients.h
 * @brief One table of every process logging to the events service: setup
 * validation, draining every ring, and merging the result into one batch.
 *
 * `docs/SYSTEM_LOG.md` section 5. A client's ring is its own process's
 * ::AstraLogProducer on the other end (`sw/userspace/runtime/src/log_ring.c`);
 * this side only ever reads it with ::astra_log_consumer_next and moves
 * `position`, the process-local shadow of how far this table has read.
 */

#include <stdint.h>

#include <astra/log_format.h>
#include <astra/log_ring.h>

/** Complete number of processes the events service can log for at once. */
#define ASTRA_LOG_CLIENTS_MAX 96u

/**
 * Output budget of one drain pass: the `out` capacity the events service
 * passes to ::astra_log_clients_drain, and the size of the drain's own text
 * arena. 32 KiB holds about 160 one-line records (each reserves
 * ASTRA_LOG_LINE_BYTES of its text length, about 200 bytes for a short line)
 * and always at least one record of ASTRA_LOG_TEXT_MAX with its drop marker.
 */
#define ASTRA_LOG_DRAIN_OUT_BYTES 32768u

/** One process's log ring, as the events service tracks it. */
typedef struct AstraLogClient {
    uint32_t used;             /* nonzero once this slot holds a client */
    uint32_t pid;
    uint32_t consumer_handle;  /* kernel ring-consumer handle */
    uint32_t area_handle;      /* kernel area handle backing `ring` */
    uint32_t position;         /* next ring position to read */
    uint32_t last_dropped;     /* dropped count already announced */
    uint32_t dead;             /* nonzero once the ring read something corrupt */
    volatile const void *ring; /* mapped AstraBulkRingHeader + slots */
    char ident[ASTRA_LOG_IDENT_MAX];
} AstraLogClient;

/** The complete client table. */
typedef struct AstraLogClients {
    AstraLogClient client[ASTRA_LOG_CLIENTS_MAX];
    uint32_t cursor;           /* slot the next drain pass reads first */
} AstraLogClients;

/**
 * Validate a SETUP message and the ring header it describes, and add a new
 * client for it.
 *
 * Checks the message's ::AstraMessageHeader the way every other service
 * validates one (exact size, `header_size`, zero `flags`/`reserved`, the
 * protocol/version/operation this wire format assigns), then the
 * ::AstraLogSetup payload against this build's fixed ring layout, then the
 * mapped ::AstraBulkRingHeader the setup claims to describe. Nothing is
 * trusted from the process beyond what these checks confirm, including
 * `ident`'s NUL termination.
 *
 * @param clients Table to add to.
 * @param pid Logging process's id.
 * @param setup The SETUP message payload (including its header), as received.
 * @param setup_size Bytes actually received for `setup`.
 * @param ring The process's log ring, mapped read-write, at least
 *     ::ASTRA_LOG_RING_AREA_SIZE bytes.
 * @param mapped_size Bytes actually mapped at `ring`.
 * @param consumer_handle Kernel ring-consumer handle for `ring`.
 * @param area_handle Kernel area handle backing `ring`.
 * @return The new client, or NULL if `setup`, the ring header, or the mapping
 *     (any size but ::ASTRA_LOG_RING_AREA_SIZE) fails validation, if `pid`
 *     already holds ::ASTRA_LOG_SETUPS_MAX clients, or if the table is full.
 */
AstraLogClient *astra_log_clients_add(AstraLogClients *clients, uint32_t pid,
    const AstraLogSetup *setup, uint32_t setup_size,
    volatile const void *ring, uint32_t mapped_size,
    uint32_t consumer_handle, uint32_t area_handle);

/**
 * Called once per client whose `position` moved during a drain, before the
 * client's share of the batch is returned to the caller. The service uses
 * this to publish the ring's `consumer_position` (and ring any doorbell the
 * protocol needs) for exactly the clients that were actually read this pass.
 */
typedef void (*AstraLogConsumed)(void *context, AstraLogClient *client,
                                 uint32_t position);

/**
 * Drain every client's ring, merge the records -- and the caller's
 * kernel-ring lines -- by monotonic time, and format them into one batch.
 *
 * `extra` is the kernel ring's text, already joined and named by the caller,
 * in ring order. Its lines are drain input like any ring record: taken in
 * order, reserved against the same budget (the first always, the rest while
 * they stay within half of it, so ring clients keep the other half), and
 * sorted into the batch by `monotonic_ns`. `*extra_taken` says how many were
 * written; the caller drops those and passes the rest next time.
 *
 * Each call reads at most what its output budget -- the smaller of
 * `capacity` and ::ASTRA_LOG_DRAIN_OUT_BYTES -- can hold: before a record is
 * accepted, its worst-case line (::ASTRA_LOG_LINE_BYTES of its text length)
 * and, when it carries new drops, a marker line are reserved against that
 * budget. A record that does not fit is left in its ring, unconsumed, for a
 * later call, so a record is consumed only when its line is certain to be
 * written whole. Clients are read fairly: starting from a cursor that
 * rotates to whichever client was last refused for want of room, every live
 * client first gets an equal share of the budget (and at least one record,
 * room permitting), then the leftover is handed out in the same order. The
 * batch is then sorted by monotonic time (stable, so one process's own lines
 * keep their relative order) and formatted into `out`.
 *
 * Drops are counted in ring order: a record whose `dropped` count exceeds
 * the last one announced for its client gets one `[N lines dropped]` line
 * immediately before it; a count that goes backwards (racing producer
 * threads) announces nothing. A client whose ring reads back corrupt is
 * marked dead (`AstraLogClient::dead`), skipped by every later call, and
 * never reported as pending.
 *
 * `consumed` runs once per client whose `position` advanced, after this
 * call's own reads of that ring are complete but before the batch is
 * returned, so the service can publish `consumer_position` (and ring a
 * doorbell if the protocol needs one) before any formatted line reaches a
 * file a reader might already be tailing.
 *
 * Doorbell pairing (log_ring.h's "Doorbell pairing contract"): after running
 * every `consumed` call, this function issues the matching seq_cst fence and
 * re-reads each live client's `producer_position`, so a producer's doorbell
 * decision racing this call's `consumer_position` store is never silently
 * lost. `*pending` reports the result: nonzero if, after that reread, any
 * live client still has data published but not yet read by this table --
 * whether because it arrived in that race window or because this call's
 * output budget left it unread. The caller must treat `*pending` as part
 * of "is there more work right now": only call it safe to block on the LOG
 * port when a call returns both 0 bytes and `*pending == 0`.
 *
 * @param clients Table to drain.
 * @param extra Kernel-ring lines in ring order, or NULL.
 * @param extra_count Lines at `extra`.
 * @param extra_taken Set to how many of `extra` this call wrote; may be NULL
 *     when `extra` is.
 * @param out Output buffer for the formatted batch.
 * @param capacity Size of `out` in bytes; pass ::ASTRA_LOG_DRAIN_OUT_BYTES.
 *     A smaller `out` shrinks the pass. One smaller than the longest
 *     record's worst-case line plus its drop marker
 *     (`ASTRA_LOG_LINE_BYTES(ASTRA_LOG_TEXT_MAX)` +
 *     `ASTRA_LOG_LINE_BYTES(26)`) is refused: nothing is read or written,
 *     and `*pending` is 0, because such a caller could never drain a long
 *     record and would otherwise spin on it forever.
 * @param consumed Called once per client whose position moved; must not be
 *     NULL.
 * @param context Opaque context passed to every `consumed` call.
 * @param day Set to the YYYYMMDD (::astra_log_local_day) of the last record
 *     written that had a valid wall time; left unchanged if none did.
 *     Must not be NULL.
 * @param pending Set to nonzero if live, unread data remains after this
 *     call's own doorbell-pairing reread; otherwise set to 0. Must not be
 *     NULL.
 * @return Bytes written into `out`.
 */
uint32_t astra_log_clients_drain(AstraLogClients *clients,
    const AstraLogLine *extra, uint32_t extra_count, uint32_t *extra_taken,
    char *out, uint32_t capacity, AstraLogConsumed consumed, void *context,
    uint32_t *day, uint32_t *pending);

#endif
