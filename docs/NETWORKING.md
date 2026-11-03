# Astra 68 networking

Networking is a brokered system service, not a kernel TCP/IP stack. The host
already owns the network stack; Axiom owns capability enforcement, memory and
device admission, interrupt delivery, and process isolation.

```
application / POSIX program
        │  network.library or POSIX sockets
        ▼
Network.kit ── shared transfer slots + request/reply ports + readiness events
        ▼
network service ── NETWORK_DEVICE + NETWORK_IRQ
        ▼
Axiom network lease ── bounded DMA area and IRQ
        ▼
QEMU host sockets and resolver
```

Host descriptors, host `sockaddr` layouts, `errno`, and backend pointers never
cross the guest ABI. `sw/include/astra/network.h` defines the append-only wire
contract: fixed-width IPv4/IPv6 addresses, endpoint generations, status values,
readiness bits, transport requests, and completions. One 64 KiB transfer slot
covers the complete 16-bit IP packet length; slot count is derived from the
shared area granted to a session rather than a compiled application limit.

The service holds every host endpoint and validates the calling session,
endpoint generation, shared-area range, and operation before touching the
lease. Endpoint control capabilities survive `fork`/`exec`; a child opens its
own session, and an endpoint remains alive until its last capability closes.
Listening requires the separate `NETWORK_LISTEN` capability. Ordinary clients
receive `NETWORK` only. Apps never receive the device lease or IRQ.

`network.library` ABI 1.1 exposes sessions, TCP/UDP endpoints, bind, connect,
listen/accept, send/receive, shutdown, socket options, local/peer addresses,
readiness events, asynchronous DNS, cancellation, and capability-only state
export/import for atomic exec. `Network.kit` is the NDK-distributed bundle.
ABI 1.4 adds native IPv4/IPv6 parsing, interface enumeration, and a bounded
Ethernet IPv4 profile. The POSIX `inet_pton` adapter calls the native parser,
so Settings and Unix ports share one address grammar. The profile names an
interface and selects DHCP or manual address/prefix/gateway with up to three
explicit DNS servers. Ethernet admin calls require a separate `NETWORK_ADMIN`
capability; socket clients cannot apply host settings.

The POSIX library maps the same owner implementation to:

- IPv4 and IPv6 `socket`, `bind`, `connect`, `listen`, `accept`, and `shutdown`;
- `send`, `sendto`, `sendmsg`, `recv`, `recvfrom`, and `recvmsg`;
- `getsockname`, `getpeername`, `getsockopt`, and `setsockopt`;
- `getaddrinfo`/`freeaddrinfo` and `inet_pton`/`inet_ntop` helpers;
- descriptor inheritance across `fork` and atomic `exec`.

Requests do not use guessed elapsed-time failures. Blocking operations wait for
completion, cancellation, peer closure, or a caller-supplied deadline. The
backend reports immediate readiness and arms only the unsatisfied readiness
groups, preventing a permanently writable socket from hiding a later readable
transition.

## Configuration and clock synchronization

Settings has a Network > Ethernet page built from shared Sidebar, List, Label,
Segmented, Form Row, Field, and Button controls. Network Kit enumerates
non-loopback devices including those without an address, then reports assigned
IPv4/IPv6 addresses. Users select DHCP or manual IPv4, enter address/prefix,
optional gateway, and up to three DNS servers, then Apply. Settings shows a
120-second Keep/Revert countdown; closing the session reverts an unconfirmed
change. The network service stores confirmed profiles in its scoped system
Config Kit document, and native Network Kit hides that storage format from
clients. Link enable/disable and IPv6 editing are separate future work.

Network service owns the guest admin session; QEMU forwards typed commands on
a versioned private socket to a root NetworkManager helper. The helper accepts
only the expected QEMU process credentials, checks the device type, creates a
timed NetworkManager checkpoint, stages an in-memory connection, waits for
activation, and persists it only after Keep. Revert, session death, or timeout
uses the checkpoint to restore the previous connection. Unconfirmed changes
do not alter Config Kit. Ordinary `NETWORK` clients retain status and socket
access only. Haiku's read-only `src/preferences/network/InterfaceView.cpp`
and `InterfaceAddressView.cpp` informed the separate live-status and editable
configuration surfaces. NetworkManager's documented
[checkpoint API](https://networkmanager.pages.freedesktop.org/NetworkManager/NetworkManager/gdbus-org.freedesktop.NetworkManager.html)
and [in-memory profile API](https://www.networkmanager.dev/docs/api/latest/gdbus-org.freedesktop.NetworkManager.Settings.html)
provide host rollback and persistence.

DE25 Ethernet configuration gate, 2026-11-03: release
`6f39fa46ee6ccd379f19ddd7db9159e790e2ba4abb46550b19022c70679aace4`
from main `7a5a05a359024eaa0d7aed1b8b1aff55a01d1587` passed a pristine
private boot and production activation. Settings listed `eth0` and isolated
`astra-test0`, loaded a saved Manual `192.0.2.10/24` profile, and completed
Apply/Keep. Earlier private trials also passed Revert and automatic checkpoint
timeout. The production desktop reopened Settings and Atlas after deployment;
all three Astra services remained active with no panic. Test veths and host
profile were removed, with 8.0 GiB board space free. Evidence is archived at
`/mnt/Documents/astra68/ethernet-config-20261103/` on Beast. Physical `eth0`
was not reconfigured because it carries remote board access.
Keep first writes a pending decision in the service's scoped Config Kit
document. After host checkpoint confirmation, one atomic Config Kit batch
promotes that profile and removes the pending record. A service restart or
failed host confirmation replays the pending decision until the host and saved
profile agree. The host refuses nested checkpoint creation, so an orphaned
checkpoint must roll back before recovery stages another connection. A DE25
private trial seeded the interrupted state and recovered `astra-test0` to
`198.18.0.2/30`, with no pending record or checkpoint afterward; a separate
Settings Apply/Keep trial exercised the normal path. Neither test altered
physical `eth0`, which carries remote access.
The exact release `a2fe757c722a337e417d054327b0d8814228c98e78af4fa70361f46df020c1ae`
passed a 120-second DE25 private boot and standard production activation on
2026-11-03. Installed identity and runtime, audio, and remote desktop services
verified after cutover. Physical `eth0` remained `192.168.1.52/24`.

`ntpd` opens `config.library` and asks for its `pool` and `server` settings; it
does not know a configuration path, file format, or storage API. The supervisor
scopes its `CONFIG:r` capability to the service's private configuration root,
and the image currently supplies one repeated-key list entry for
`pool.ntp.org`. On startup ntpd keeps retrying until a valid NTP reply updates
the Astra clock, then releases the required-service boot dependency. Periodic
and manual synchronization reload the settings through the same library.

Persistent system, service, and application settings use owner-scoped Config
Kit documents; command-line tools use ordinary files under
`HOME:/.config/<command>/`. The on-disk text is deliberately forgiving
for administrators: whitespace, comments, optional `=`, quoted escapes, and
repeated keys are accepted. Scalars are one value; repeated keys are ordered
lists. Library getters expose strings, signed and unsigned 64-bit integers, and
booleans, while writes sync a temporary file before atomic rename so a failed
write cannot replace the last valid configuration.

## Validation

Network core/library, service, POSIX, runtime, supervisor, Terminal, kernel,
sanitizer, analyzer, and clean MC68040 builds pass on Beast. The source-identified
QEMU gate covers DNS plus IPv4 UDP and TCP through a fork+exec child. Four final
focused runs completed in 2.98, 2.98, 2.99, and 2.99 seconds; the complete
two-boot 73-command gate, Vim, Lua, pipes, redirects, filesystem, and desktop
tests also pass. Generated MC68040 for the exchange, transfer, readiness, IRQ,
and platform paths was inspected; measurement did not justify an assembly fork
of the shared C implementation.

The current candidate identities are:

| Artifact | SHA-256 |
|---|---|
| QEMU source identity | `9676eee2d916ed8d405daecd6f1ee458c924992bf15b983d328b5f3f754a75e5` |
| Beast x86 QEMU | `f2b0b2addb3f2240fc13d941cccfd0e7c1ae3305382c9451c1361524ba4d4a27` |
| Arty ARM QEMU | `4ea90cfcdf6d112761ba2e8be16ee1fdd077070c8379f4e69799cf3667b0d118` |
| 268,396-byte ROM | `1a8f8895868b323d0dcae79e0a62620140acd69c47ff044bacba746f86ee19de` |
| 64 MiB pre-boot storage image | `8af292be708ac4fa3633df1fda6590696703b47eb5d3406bbb983103b4fd072d` |

Physical Arty qualification passes. Restoring the Ethernet cable raised carrier,
DHCP reacquired `192.168.1.188`, and the already-installed firstboot source
(`96a555ad5287b3c9c52eecd88ad3dd629216cfd3bbf9d146e850889771754c3b`)
synchronized NTP and released Astra without intervention. The exact candidate
then passed the complete POSIX command including guest DNS, IPv4 UDP, TCP
listen/connect, descriptor inheritance, and TCP fork+exec twice on the
Cortex-A9-hosted MC68040, in 33.61 and 32.48 seconds. The command includes the
full POSIX/filesystem matrix before its network phase; those times are not
claimed as isolated socket latency.

The inherited host image still logs that its DHCP hook cannot replace the
read-only `/etc/resolv.conf`. Its existing resolver configuration successfully
resolved `pool.ntp.org`, so this did not weaken the gate, but making the DHCP
resolver target writable remains host-image cleanup.

The verified QEMU, ROM, and pre-boot image were promoted atomically. Active
boot reached stage 8 with the correct host wall clock. The prior artifacts are
retained at `/data/astra/deploy/network-1a8f8895868b/rollback` with hashes
`62ff1ab2194c52478dc60ba088624fbb661d0c47941b0d59633ce3b187024d52`,
`3a3a7305459122d39cc94217f62bafc2cde16d3afb3c58d08d1b1c685b733162`,
and `4b2d978ca9ac70a6a4bd2c9172b5179db5e56dd83e1afbf9fb24ed732a80cc28`
for QEMU, ROM, and storage respectively.
