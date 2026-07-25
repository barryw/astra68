# Shared Memory And Bulk IPC

Message ports carry bounded control messages and capability handles. Shared
areas carry larger payloads without copying those bytes through Axiom. A bulk
ring combines both mechanisms: a setup message transfers a reduced-right area
handle and exactly one move-only endpoint, then the peers exchange batches in
the mapped area and use the endpoint as a waitable doorbell.

## Areas

`astra_area_create()` commits and zeroes the complete area before returning.
There is no overcommit. Mapping is explicit, requires `ASTRA_RIGHT_MAP` plus
the requested read or write rights, and uses a kernel-selected fixed address.
The same area appears at the same logical address in every process.

An area handle, its local mapping, and child rings have separate lifetimes.
`astra_area_close()` first removes the local mapping and then closes the
handle. Creator death revokes every mapping and causes foreign handles and
child rings to report `ASTRA_ERROR_PEER_DEAD`.

Use `astra_handle_duplicate()` to create a reduced-right area capability for a
peer. Numeric handle assignment is not duplication. Ring endpoints cannot be
duplicated; transfer them through a message port.

## Ring ownership

A ring is single-producer/single-consumer and unidirectional. Full-duplex
protocols create two nonoverlapping rings in one area. The producer and
consumer must each map the area read/write because each owns one shared
position field.

An `AstraBulkRing` view consumes its endpoint handle only after a successful
`astra_bulk_ring_attach()`. One thread may operate on a view at a time. If an
application shares a view between threads, it must serialize the complete
reserve, access, and commit sequence itself.

## Batching

Producer order is:

1. Reserve an element with `astra_bulk_ring_write_reserve()`.
2. Fill the complete fixed-size element.
3. Publish it with `astra_bulk_ring_write_commit()`.
4. Repeat for the bounded batch.
5. Call `astra_bulk_ring_notify()` once.

Consumer order is symmetrical: wait, reserve, copy or process the element,
commit, and notify once after releasing a batch. The kernel sees only notified
positions. This is deliberate: an unnotified producer batch remains invisible
to a sleeping consumer, and unnotified consumer progress does not release
kernel-accounted producer capacity.

`astra_bulk_ring_wait_until()` accepts an absolute monotonic deadline.
`ASTRA_DEADLINE_POLL` probes once, while `ASTRA_DEADLINE_INFINITE` has no time
limit. A closed peer wakes waiters with `ASTRA_ERROR_PEER_DEAD`; malformed
shared metadata closes only the affected ring with `ASTRA_ERROR_IO`.

Ring capacity is fixed at creation. Full and empty operations return
`ASTRA_ERROR_WOULD_BLOCK`, and neither path allocates memory nor changes the
shared position. Applications must treat the returned element pointer as
borrowed until the matching commit and must never retain it after unmapping or
closing the area.

The checked implementation example is `examples/bulk_ring.c`.
