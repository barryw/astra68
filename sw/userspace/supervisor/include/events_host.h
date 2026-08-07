#ifndef ASTRA_SUPERVISOR_EVENTS_HOST_H
#define ASTRA_SUPERVISOR_EVENTS_HOST_H

#include <stdint.h>

/*
 * Stands the events service up: loads the catalog off SYS:, drains the
 * kernel's ring into a bounded store, and publishes that store as EVENTS:.
 *
 * Returns 0 when it cannot -- and that is not a boot failure. A machine
 * without EVENTS: is a machine that still records everything, in the ring, and
 * simply cannot be asked about it from the terminal. The layout spec's rule
 * that a binding which cannot be made is omitted rather than fatal is the same
 * rule.
 */
int supervisor_events_start(uint32_t process_handle);

/*
 * Moves whatever the ring holds into the store, bounded per call so a burst
 * costs several passes rather than a stall. Called from wherever the
 * supervisor already has somewhere to do periodic work.
 */
/* The send handle a launch grants for EVENTS:. */
uint32_t supervisor_events_port(void);

/* Requests this service answered, and requests it could not. */
uint32_t supervisor_events_served(void);
uint32_t supervisor_events_refused(void);
uint32_t supervisor_events_stalled(void);

void supervisor_events_pump(void);

/*
 * How many messages the catalog on SYS: described. Zero means every line the
 * events tree renders will be a message id -- honest, and worth saying out
 * loud at boot rather than leaving a person to wonder why the text is missing.
 */
uint32_t supervisor_events_catalog_count(void);

/* What refused the catalog, when nothing loaded it. Zero when it did. */
uint32_t supervisor_events_catalog_status(void);

#endif
