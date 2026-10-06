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

#include <astra/log_ring.h>

/** Complete number of processes the events service can log for at once. */
#define ASTRA_LOG_CLIENTS_MAX 96u

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
 *     fails validation, or the table is full.
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
 * Drain every client's ring, merge the records by monotonic time, and format
 * them into one batch.
 *
 * Reads at most `DRAIN_BATCH` (log_clients.c) records in total across every
 * client per call, in client-table order, then sorts that batch by monotonic
 * time (stable, so one process's own lines keep their relative order) and
 * formats it into `out`. A client whose dropped count increased since the
 * last drain gets one `[N lines dropped]` line inserted immediately before
 * its next record. A client whose ring reads back corrupt is marked dead
 * (`AstraLogClient::dead`) and skipped by every later call.
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
 * `DRAIN_BATCH` cap left it unread. The caller must treat `*pending` as part
 * of "is there more work right now": only call it safe to block on the LOG
 * port when a call returns both 0 bytes and `*pending == 0`.
 *
 * @param clients Table to drain.
 * @param out Output buffer for the formatted batch.
 * @param capacity Size of `out` in bytes.
 * @param consumed Called once per client whose position moved; must not be
 *     NULL.
 * @param context Opaque context passed to every `consumed` call.
 * @param day Set to the YYYYMMDD (::astra_log_local_day) of the last record
 *     in the batch that had a valid wall time; left unchanged if none did.
 *     Must not be NULL.
 * @param pending Set to nonzero if live, unread data remains after this
 *     call's own doorbell-pairing reread; otherwise set to 0. Must not be
 *     NULL.
 * @return Bytes written into `out`.
 */
uint32_t astra_log_clients_drain(AstraLogClients *clients, char *out,
    uint32_t capacity, AstraLogConsumed consumed, void *context,
    uint32_t *day, uint32_t *pending);

#endif
