# Messaging directory and broker

`MESSAGING` is a public startup capability. Supervisor owns its receive port.
`system.library` exposes `astra_messaging_*` in `<astra/messaging.h>`. Services,
applications, and commands use the same protocol; terminal delegates the
capability to commands it starts.

`LIST` walks live supervisor-launched services and applications by process ID,
plus registered terminal-launched commands named by their kernel process
snapshot. `IDENTIFY` returns the caller's
broker identity. `REGISTER` publishes one direct send endpoint per process;
`LOOKUP` returns a reduced send/wait capability. A caller then uses the peer's
normal port protocol and `astra_port_call` for one-to-one requests and replies.
The broker routes only discovery, not direct request payloads.

`SUBSCRIBE` selects a publisher kind/name, optional exact process ID, and local
topic. Best-effort `PUBLISH` stamps the event with the authenticated sender's
identity and fans it out without waiting on subscribers. Each subscription has
its own monotonic sequence. A full port drops a best-effort delivery and leaves
a sequence gap at the next delivered event. A publish reply gives delivered and
dropped counts. A publisher may attach a reply send endpoint; every delivered
recipient gets a copy and may answer directly.

Reliable publication is opt-in. Broker admission reserves one event for every
current matching subscriber atomically, up to eight unacknowledged events per
subscriber; a full backlog returns `BUSY` without partial delivery. Wire sizes
stay at version 1; existing publishers send zero QoS and retain best-effort
behavior. Reliable events and ACKs are additive operations. Broker sends each
subscriber's head event in order, retries a full port every 100 ms,
redelivers an unacknowledged event after one second, and keeps it until that
subscriber acknowledges its sequence. Duplicate events carry the same sequence;
consumers must make processing idempotent. Duplicate ACKs
succeed. Reliable events use `ASTRA_MESSAGING_RELIABLE_EVENT`; best-effort
events use `ASTRA_MESSAGING_EVENT`. Reply `delivered` counts backlog admissions
for reliable publication. Publisher uses a finite call deadline and retries `BUSY`; a timed-out
call may have been accepted, so consumers must tolerate duplicate transactions.
Admission confirms broker custody, not application-level completion; use a
direct port request/reply when publisher needs the subscriber's result.
With no matching subscribers, admission succeeds with zero recipients and
stores nothing.
Reliable delivery is at least once within a live supervisor/subscription, not
durable across reboot or subscriber death. A subscriber that never acknowledges
backpressures its publisher. Best-effort `POST` remains one-way and cannot
request reliable admission. `ACK` is authenticated to the subscribing process.

Events are transient, not state snapshots. Subscribers obtain initial state
from the owning service after subscribing. For state-change topics, publishers
can coalesce pending changes and retry a reliable publication outside the
mutation path, so a slow observer cannot stall filesystem writes.

Limits: 8 broker requests queued; 128 direct registrations; 128 subscriptions;
384 payload bytes per event; 8 unacknowledged reliable events per subscriber.
Larger data travels through shared Areas or rings.
Process exit retires supervisor-owned registrations and subscriptions. Broker
never trusts caller-supplied service or application names. A command outside
supervisor's launch table is identified by its authenticated PID and checked
against the kernel process snapshot before registration or discovery.

Haiku reference: `src/servers/registrar/TRoster.cpp` routes roster watchers;
`src/kits/storage/VolumeRoster.cpp` subscribes to mount changes; and
`src/kits/app/Messenger.cpp` does direct target/reply messaging. Astra keeps
those roles separate while using native ports and capability grants, and
keeps diagnostic `EVENTS:` records separate from user event delivery.
