# Shared Memory And Bulk IPC

Message ports carry bounded control messages and capability handles. Shared
areas carry larger payloads without copying those bytes through Axiom. A bulk
ring combines both mechanisms: a setup message transfers a reduced-right area
handle and exactly one move-only endpoint, then the peers exchange batches in
the mapped area and use the endpoint as a waitable doorbell.

## Areas

`astra_area_create()` commits and zeroes the complete area before returning.
There is no overcommit. Shared mapping requires `ASTRA_RIGHT_MAP` plus the
requested read or write rights. Mapping chooses a free address unless an
explicit aligned address is requested with `astra_rt_area_map_range_at()`.

An area handle, its local mapping, and child rings have separate lifetimes.
`astra_area_close()` first removes the local mapping and then closes the
handle. By default, closing the final handle and child reference revokes any
remaining mappings. Creator death removes its local mappings and releases its
handles; independently retained references still own the backing.

Use `astra_rt_area_create_flagged()` with
`ASTRA_AREA_CREATE_MAPPED_LIFETIME` when mappings should also own backing.
The temporary creation handle can then close after mapping. Backing survives
fork and the creator's exit while another view or child reference remains;
final unmap/exit releases it when no handle or child owns it. This is useful
for anonymous shared or private memory without an extra userspace handle
ledger. Add `ASTRA_AREA_CREATE_RESERVED` for demand commitment. Private-copy
semantics remain selected per view, independently of lifetime.

Pager-backed areas also depend on the process owning their pager endpoint.
If it exits, all clients' views are revoked with `PEER_DEAD`, outstanding dirty
snapshots are cancelled, and backing is released. Retained handles or pending
requests keep only the terminal area record until their references close.
Creator exit alone does not revoke an area served by another live pager.

Use `astra_handle_duplicate()` to create a reduced-right area capability for a
peer. Numeric handle assignment is not duplication. Ring endpoints cannot be
duplicated; transfer them through a message port.

Native `astra_rt_area_map*()` calls also accept `ASTRA_AREA_MAP_PRIVATE`.
Private views require READ and MAP authority even when writable: reads alias
the source page read-only, and the first private write copies it into ordinary
process-owned memory. Source bytes and dirty state remain unchanged. Fork uses
the same copy-on-write mechanism as other process pages. Clean source-page
reclaim withdraws unchanged aliases while preserving private copies; unmap and
process teardown release both kinds of page through the existing VM owner.
Private views retain the area's normal lifetime and revocation rules.

`astra_rt_area_unmap_range()` removes a page-aligned range from the calling
process. It trims or splits existing views, ignores holes, and preserves other
processes' mappings and surviving backing. A split allocation failure leaves
all views unchanged. Shared-table edge fragments use the same shared-frame
fault mapping as other native views; reclaim, writeback and EOF still belong
to the canonical area. `astra_rt_area_unmap()` removes one complete local view.

Explicit unmap checks shared pager owners before removing any view. Saved
backing errors return `ASTRA_SYSCALL_IO_ERROR` and keep the requested range
intact for retry. Changes to native views during a blocked check restart
validation under the same 30-second deadline. Private/anonymous views, holes
and a provider's own mappings need no provider call. Pending dirty generations
still own backing work after successful unmap; use synchronous range sync when
backing durability is required.

`astra_rt_area_sync_range(address, page_bytes, flags, &backing_status)` resolves the
calling process's existing views and synchronizes their shared pager backing.
It needs no file descriptor or area handle. Private and anonymous views require
no backing write; a hole returns `ASTRA_SYSCALL_BAD_ADDRESS`. The native owner
may synchronize a larger cache extent. Dirty data survives failed writeback.
The return value reports kernel/IPC success or failure; `backing_status` also
preserves an exact `ASTRA_STATUS_*` backing failure when the return is
`ASTRA_SYSCALL_IO_ERROR`. Page-aligned, nonempty ranges are required.
Requests retain the area across bounded native IPC, including control-worker
handoff, cancellation and timeout. A successful reply covers native writeback
and backing synchronization, not merely delivery to the pager.
With `ASTRA_AREA_SYNC_ASYNC`, success instead acknowledges the native owner's
existing bounded writeback work; dirty generations retain pending data after
unmap. Zero flags await backing synchronization. Both report saved backing
errors without replacing the native worker with an application-specific queue.

### Pager-owned pages

A pager owner can populate an absent reserved page with
`astra_rt_area_publish_clean_page()`. It requires READ and ADMINISTER authority,
copies the supplied valid bytes, and zeroes the remaining page before publication.
It neither calls the pager nor creates dirty/writeback state. This lets a file
cache prepare a cold page while holding its native node I/O owner.

`astra_rt_area_publish_dirty_page()` instead requires WRITE and ADMINISTER and
atomically publishes a complete modified page. Both refuse to replace a resident
page. Failed source copying leaves the page absent. Existing resident writes and
writeback snapshots keep their normal dirty-generation lifecycle.

`astra_rt_area_resize_backing()` publishes a pager area's current byte length
through ADMINISTER authority. Capacity and mapped address reservations stay
fixed. Pages beyond EOF cannot fault in; shrinking revokes private copies too
and clears the unused tail of the last page. Affected in-flight writeback
refuses the change with WOULD_BLOCK before any bytes or mappings change.
The owner may stage new pages before publishing growth. Length changes advance
the existing pager epoch, so stale replies cannot restore truncated data.

An empty or not-yet-grown backing does not prevent creation of a mapped view.
Access to an unavailable backing page terminates with the native
`ASTRA_STATUS_BACKING_FAULTED` verdict. Permission-denied accesses and addresses
outside an existing view retain the ordinary fault verdict. POSIX death waits translate
these to `SIGBUS` and `SIGSEGV`, respectively; the kernel contains no POSIX
signal policy. The verdict is retained in process information and death waits.

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
