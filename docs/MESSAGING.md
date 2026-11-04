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
topic. `PUBLISH` stamps the event with the authenticated sender's identity and
fans it out without waiting on subscribers. Each subscription has its own
monotonic sequence. A full queue drops that delivery and leaves a sequence
gap at the next delivered event. A publish reply gives delivered and dropped
counts. A publisher may attach a reply send endpoint; every delivered recipient
gets a copy and may answer directly. Events are transient, not state snapshots.
Subscribers obtain an initial state from the owning service after subscribing.

Limits: 8 broker requests queued; 128 direct registrations; 128 subscriptions;
384 payload bytes per event. Larger data travels through shared Areas or rings.
Process exit retires supervisor-owned registrations and subscriptions. Broker
never trusts caller-supplied service or application names. A command outside
supervisor's launch table is identified by its authenticated PID and checked
against the kernel process snapshot before registration or discovery.

Haiku reference: `src/servers/registrar/TRoster.cpp` routes roster watchers;
`src/kits/storage/VolumeRoster.cpp` subscribes to mount changes; and
`src/kits/app/Messenger.cpp` does direct target/reply messaging. Astra keeps
those roles separate while using native ports and capability grants, and
keeps diagnostic `EVENTS:` records separate from user event delivery.
