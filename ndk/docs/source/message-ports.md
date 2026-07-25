# Message Ports

Astra message ports provide the native control plane between protected
processes and services. They preserve the useful receiver-owned, asynchronous
shape of Amiga ports while adding fixed queue limits, explicit rights,
generation-safe handles, process isolation, deadlines, and peer-death results.

## Endpoint ownership

{c:func}`astra_port_create` returns one {c:struct}`AstraPort` containing:

- a nontransferable receive endpoint owned by the process that creates the
  port;
- a transferable send endpoint that clients use to enqueue requests.

A service publishes its send endpoint through its bootstrap or discovery
protocol. A client that needs a reply creates another port and moves that
port's send endpoint with its request. There is no implicit global reply port
and no kernel interpretation of service protocols.

Closing the receive endpoint discards queued messages and makes every sender
observe {c:enumerator}`ASTRA_ERROR_PEER_DEAD`. Closing the final send endpoint
allows already queued messages to drain; the receiver then observes peer
death. Process termination performs the same cleanup even when application
code never runs its normal close path.

## Bounded datagrams

Every message begins with {c:struct}`AstraMessageHeader` and contains at most
256 additional bytes. Up to eight {c:type}`AstraHandle` values may accompany
the datagram. Queue storage is reserved when the port is created and is bounded
by both message count and total bytes.

A full queue returns {c:enumerator}`ASTRA_ERROR_WOULD_BLOCK`; it never grows or
allocates opportunistically. Use shared-memory areas and bounded rings for bulk
data. Ports carry compact commands, replies, notifications, shared-area
handles, and fence values.

## Atomic handle movement

The handle vector passed to {c:func}`astra_port_send_try` or
{c:func}`astra_port_send_until` is move-only:

- success consumes every source capability and replaces every array entry
  with {c:macro}`ASTRA_INVALID_HANDLE`;
- any error leaves all source capabilities and array entries unchanged;
- duplicate, stale, or insufficient-rights handles reject the entire send.

Receive is also atomic. If either output capacity is too small,
{c:enumerator}`ASTRA_ERROR_BUFFER_TOO_SMALL` reports both required sizes and
leaves the message queued. A copy fault publishes no destination handle and
also leaves the message queued for a valid retry.

## Blocking and deadlines

The `_try` functions perform one bounded syscall. The `_until` functions retry
that operation around the kernel wait primitive with one unchanged
{c:type}`AstraMonotonicDeadline`. Use {c:macro}`ASTRA_DEADLINE_POLL` for a
single attempt and {c:macro}`ASTRA_DEADLINE_INFINITE` when no finite deadline
is appropriate.

No message pointer or handle-vector pointer remains in the kernel while a
thread sleeps. Queue readiness uses a failed-probe sequence token, so a state
change between the try and wait cannot be lost and a message larger than the
minimum header does not spin merely because 24 bytes remain writable.

## Checked example

This compact protocol request is cross-compiled with every NDK example build:

```{literalinclude} ../../examples/port_message.c
:language: c
:linenos:
```
