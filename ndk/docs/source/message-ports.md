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
protocol. A client that needs a reply makes a call (below) rather than a reply
port. There is no implicit global reply port and no kernel interpretation of
service protocols.

Closing the receive endpoint discards queued messages and makes every sender
observe {c:enumerator}`ASTRA_ERROR_PEER_DEAD`. Closing the final send endpoint
allows already queued messages to drain; the receiver then observes peer
death. Process termination performs the same cleanup even when application
code never runs its normal close path.

## Bounded datagrams

Every message begins with the 24-byte {c:struct}`AstraMessageHeader` and
carries at most {c:macro}`ASTRA_MESSAGE_INLINE_MAX` (1,024) bytes after it.
Handles may accompany the datagram, up to the process's whole handle
namespace ({c:macro}`ASTRA_MESSAGE_HANDLES_MAX`). A port is created with a
limit on both its queued message count and its queued bytes; queue storage
is allocated as messages arrive, within those limits.

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

## Calls

{c:func}`astra_port_call` sends a request and waits for its reply in one
kernel call. The kernel makes a one-shot reply capability for the calling
thread and puts it in the request's handle list at
{c:member}`AstraCall.reply_index`, where the protocol expects its reply
handle. The service answers with an ordinary send on that capability and
closes it, exactly as it would a reply port's send endpoint, so services do not
know which kind of client they are serving. The reply is written straight into
{c:member}`AstraCall.reply`; no port is created and nothing is queued.

- A capability answers once. A second send on it reports
  {c:enumerator}`ASTRA_ERROR_CLOSED`; it cannot be duplicated or waited on.
- A service that closes the capability, or dies holding it, releases the
  caller with {c:enumerator}`ASTRA_ERROR_PEER_DEAD`.
- A caller that stops waiting -- deadline, cancellation, death -- abandons the
  call. Its request was delivered, so a cancelled call is not retried; the
  service's late reply fails with {c:enumerator}`ASTRA_ERROR_PEER_DEAD` and
  the service keeps the handles it tried to send.
- A reply that does not fit reports
  {c:enumerator}`ASTRA_ERROR_BUFFER_TOO_SMALL` with the sizes it needed; the
  service's send still succeeds.
- The request's handles move once it is sent, whatever the outcome. A call
  refused before sending -- a full port, a dead service, a bad argument --
  leaves them with the caller.

## Blocking and deadlines

The `_try` functions perform one bounded syscall. The `_until` functions retry
that operation around the kernel wait primitive with one unchanged
{c:type}`AstraMonotonicDeadline`. Use {c:macro}`ASTRA_DEADLINE_POLL` for a
single attempt and {c:macro}`ASTRA_DEADLINE_INFINITE` when no finite deadline
is appropriate.

No message pointer or handle-vector pointer remains in the kernel while a
thread sleeps, with one exception: a call keeps the address of its reply
buffer, which the kernel commits when the call is made, until the call ends. Queue readiness uses a failed-probe sequence token, so a state
change between the try and wait cannot be lost and a message larger than the
minimum header does not spin merely because 24 bytes remain writable.

## Checked example

This compact protocol request is cross-compiled with every NDK example build:

```{literalinclude} ../../examples/port_message.c
:language: c
:linenos:
```
