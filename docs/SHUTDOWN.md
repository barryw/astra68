# Astra shutdown contract

Normal **Shut Down** powers off the entire DE25; **Restart** reboots it. Both
use the same vetoable guest teardown. Force shutdown is a separate, explicit
action. A timeout must not silently convert either normal action into force.

## Process shutdown contract

Processes opt in to managed shutdown in their startup-ready message. An
unhandled process is terminated by the kernel on the supervisor's request;
this is intentional for programs and services with no unsaved state. A managed
participant instead receives a lifecycle request with a unique transaction
ID. A missing or broken reply from a managed process is never reclassified as
opt-out. This is not `SIGTERM`: a POSIX signal handler cannot silently turn a
saveable document into a killed process.

The `astra/shutdown.h` wire contract uses one request message and one attached
reply-port send capability. The participant sends exactly one `READY` or
`CANCEL` reply with the same nonzero transaction ID and no attached handles.
The requester accepts a reply only from the request's private reply port;
closing that port invalidates late replies. A managed process creates its own
private receive endpoint and publishes the cloneable sender with its ready
message. A managed process that forks owns its descendants
and must not report READY until they safely exit. Terminal supervises its zsh
session instead of patching upstream zsh or Vim.

Participants that call `astra_service_ready_cancellable` also advertise
`ASTRA_SERVICE_READY_SHUTDOWN_CANCELLABLE`. Only these participants receive
`ABORT`, carrying the matching transaction and no handles, in the same protocol
1 format. Managed synchronous services keep the original request/decision
contract and finish that decision before system cancellation can proceed.
The new readiness entry point is versioned in Runtime 3.1, so an interactive
client cannot load against a runtime without cancellation support. Updating
Runtime alone does not change existing managed participants' declaration.
Cancellation keeps the original reply port open. A participant that has not
committed to exit answers the original request with `CANCEL` and remains usable;
one already committed answers `READY` and completes its clean exit. The
supervisor waits for that decision and, for READY, for exit before restoring
services. Closing a reply port is not cancellation: it could discard READY
after the participant committed. A full lifecycle queue waits for send credit
while status, cancellation and the generic message broker remain responsive.
Terminal receives ABORT even while an edited shell line prevents exit. Stale
ABORTs are ignored; synchronous storage retirement may finish before cancellation
and is then restored through the existing recovery path. There is no forced
termination deadline.

System Kit exposes the authoritative `AstraSystemPowerStatus` through
`astra_system_power_status`, including transaction, phase, exact PID/name,
failed step/status/reason and recovery-failure count. It survives Desktop
replacement. `astra_system_power_cancel` accepts only the current active
transaction and cannot interrupt service recovery. Subscribe first to the
generic broker's `system.power` topic from service `supervisor`, exact PID 1,
then read the snapshot. Events are best-effort invalidations: a full receiver
already contains a wakeup, so reading current state covers coalesced changes.
The native teardown wait serves these requests and broker messages while
refusing new launches and service edits. One shared maintenance schedule keeps
the existing one-second heartbeat and reliable-message retries running during
ordinary operation, participant waits and service recovery. Observation does
not authorize or change teardown.

Interface Kit's caller-driven alerts share the same implementation as its
blocking alerts and confirmations. This adapts Haiku
`src/kits/interface/Alert.cpp:271-324`, whose synchronous and asynchronous Go
methods share preparation. Haiku's shutdown status window in
`src/servers/registrar/ShutdownProcess.cpp:235-315` similarly separates its UI
from lifecycle work. Its `_QuitApps` implementation at lines 1486-1548 drains
abort events before entering the system-application phase and disables that
phase's cancel button. Astra retains its explicit consent protocol and can
restore completed synchronous services after cancellation. Desktop uses the
shared `AstraPowerPanel` for initial confirmation, modeless progress, exact
transaction cancellation and retained failure details. It adds the panel's
handles to its existing wait set; no Desktop shutdown worker or renderer exists.
Recovery disables cancellation while ordinary applications remain available to
save before consenting. Hardware qualification of these additions remains pending.

Haiku `src/kits/interface/Alert.cpp:497-517` composes a read-only, wrapping
TextView; Astra uses its existing TextModel, TextView, ScrollView and Scrollbar
for complete alert bodies, retaining the existing dialog geometry. Names use
Runtime's bounded `astra_string_append_display`, shared with volume-use
diagnostics, rather than a second escape loop. Haiku
`src/kits/support/String.cpp:2575-2625` copies unescaped runs between special
characters; Astra adapts that pattern to its caller-owned builder and UTF-8
scalar-boundary truncation contract. Truncated diagnostics are rejected.

For each request, the participant must do exactly one of these:

- **Ready:** stop accepting new work, finish or safely discard in-flight work,
  close its children and resources, and send `READY` with the matching ID. It
  must then exit with status zero. The supervisor requires **both** the reply
  and the clean process exit before moving to the next dependency. A reply
  alone is not proof that later writes cannot occur.
- **Cancel:** send `CANCEL` with the matching ID and a reason when it cannot
  guarantee data safety, including when the user declines to close unsaved
  work or a flush fails. It remains alive and usable. Normal shutdown stops;
  it does not kill the participant or continue to storage teardown.

A process that launched descendants is responsible for getting them safely out
before replying `READY`. In particular, Terminal cannot approve shutdown
while its shell or a CLI editor is still running. The supervisor tracks the
direct children it launched; this ownership rule covers forked commands too.
For a managed participant, a malformed/stale reply, crash, nonzero exit, or
missing reply is **not** consent. Waiting may leave shutdown pending so a
user can finish saving; a deadline may cancel the attempt, never turn it into
force shutdown. A second request after cancellation gets a new transaction ID.

The same request/reply contract applies to services after their clients have
stopped. Storage answers `READY` only after VFS sessions and workers are
drained and every mounted backend has completed its flush/no-op and unmount
hooks. A failed hook answers `CANCEL`. The supervisor stops services in
reverse dependency order and must keep the desktop and input alive while an
application is deciding.

1. Stop accepting launches. Request shutdown from applications and their
   commands. An app may save, wait for its user, or cancel; while it does so,
   keep Astra usable.
2. After apps have exited, stop services in dependency order. A service fault
   does not become a kernel panic. Never terminate a writable volume before
   its clients and request workers have stopped.
3. HostFS drains queued work, then checks cached-area owners before retiring
   its private provider sessions and claim. Stalled ports, busy resources
   or failed session/claim release prevent a READY response. Public endpoints
   and native session IDs survive cancellation; provider bindings recover
   through the existing rebind path. Failed closes retain their owned IDs for
   retry. Local pager and transport cleanup follows a successfully sent READY,
   and process exit closes the remaining local capabilities. The Linux provider
   owns filesystem synchronization, unmount, loop/source release and child
   retirement; QEMU does not implement a second filesystem flush engine.
   The runtime waits for actual provider exit and never converts a cleanup
   delay into a forced kill. An outside guardian retains exact domain and
   child lifetimes for fatal provider cleanup. See
   [the filesystem production PRD](FILESYSTEM_PRODUCTION_PRD.md) (FS33).
4. PID 1 invokes `ASTRA_SYSCALL_SYSTEM_SHUTDOWN` or
   `ASTRA_SYSCALL_SYSTEM_RESTART` only after every phase succeeds. Axiom
   rejects any other caller and refuses while any other guest process or PID 1
   worker thread is still live. It completes PID 1 resource teardown and
   writes a distinct kernel status marker. QEMU exits 88 for clean shutdown
   or 89 for clean restart; only those codes make the Linux launcher request
   `systemctl poweroff --no-block` or `systemctl reboot --no-block`,
   respectively. A crash, panic, unrelated QEMU exit, or `systemctl stop astra`
   never triggers either host power action.

Private embedded-guest trials set `ASTRA_POWER_POLICY=guest` in their trusted
service environment. The same launcher still drains the provider and helpers,
then returns the exact clean guest status (88 or 89) without requesting a host
power action. The default `host` policy above remains unchanged. Invalid
policies are rejected before child launch; this is host lifecycle policy, not
a guest setting or an alternate shutdown path.

Haiku `src/system/kernel/shutdown.cpp:13-33` separates service teardown and sync
from architecture power control. Astra keeps guest teardown and provider
retirement unchanged and makes propagation to the containing host explicit.

Supervisor logs every failed shutdown stage with service name, PID, native
status and opaque reason. Request/reply transport errors remain distinct from
a participant's explicit cancellation or unsuccessful exit. Reporting uses
the existing launch-report formatter with a bounded stack buffer so allocation
failure does not suppress its own diagnostic. A dependency cycle reports the
blocked service and cancels rather than stopping a publisher still in use.
The service-policy gate uses the existing test-only fault-probe to close a
managed shutdown endpoint, checks exact `PEER_DEAD` context, opens a fresh
Terminal, removes the faulty service, then requires clean shutdown. This is
diagnostic/recovery coverage, not proof of recovery after partial service
teardown; that remaining gap is recorded below.

Cancellation-recovery checkpoint46 (DE25 qualified, unreleased) captures
definitions of live services before teardown, while configuration remains available. Preparation
failure refuses shutdown before closing any participant. After a later
refusal, the normal dependency-checked launcher restores only those services
that were running and have since stopped, including manual/never-restart
services. Applications are not relaunched and never-started services remain
stopped. Restore failures name the service and native status; they never
authorize force shutdown. Haiku
`src/servers/registrar/ShutdownProcess.cpp:650-730` restores roster admission
on cancellation; Astra additionally has already stopped services to recover.
The native regression fails on the previous Supervisor with the stopped
service still at PID zero. Host coverage includes preparation allocation and
configuration-read failure, owned snapshot cleanup and unchanged process
records. Native QEMU passes both broken-endpoint and explicit-CANCEL cases,
restored service/new PID, preserved manual/never-restart settings, an idle
service remaining stopped, clean shutdown and offline fsck. The complete
native filesystem/provider/HostFS recovery gate passes in 83.5 seconds.
DE25 passes the same two cancellation cases and restoration assertions,
provider and HostFS recovery, fresh I/O and clean shutdown 88. All four gate
outcomes are zero; private runtime takes 527 seconds within its unchanged
600-second guard. The sealed archive verifies 57 hashes, paired private
settings/key, read-only fsck and unchanged Linux boot identity. Private board
files are removed after verified transfer; selected services return to stage 8
with zero restarts, loop0 only and 8.4 GiB free. See Current State for exact
candidate and evidence identities.

Current implementation: the native mount lifecycle and HostFS/provider stop
handlers, process lifecycle transport, managed startup declaration, Terminal's
clean-prompt/zsh-exit gate, Supervisor reverse-order stop sequence, CLI and
desktop Shut Down/Restart actions, explicit kernel/QEMU power markers, and
Linux poweroff/reboot gates build with host tests. Beast-hosted QEMU boots ran
`shutdown` and `restart` in Terminal and exited with
codes 88 and 89; an edited/busy Terminal delayed Restart until it returned to
a clean prompt. QEMU pointer tests also clicked both ASTRA menu actions and
observed their respective clean-exit codes. The window-event NDK accepts all
three defined system actions and rejects unknown values. The complete native
filesystem lifecycle gate also verifies acknowledged data after clean shutdown
and offline fsck. Normal/fatal provider tests reread durable file and directory
acknowledgements after retirement. These tests do not simulate physical power
loss or loss of Linux's page cache.
Malformed wire replies have host tests. Desktop consent/status UI passes
the hardware qualification below. Checkpoint82's native replay passes initial
confirmation Cancel, live cancellation with the same edited Terminal line,
a 66-second pending wait, menu restart 89 and shutdown 88. Its recovery gate
also covers a service that consents then exits unsuccessfully: retire the
confirmed-dead record before restoring its saved definition, while preserving
the original failure and shared process-crash diagnostics. The old Supervisor
leaves it failed at PID zero; the corrected replay restores a new running PID.
All three recovery cases, native filesystem checks, clean exit and offline
fsck pass together. DE25 also passes the 66-second edited-line cancellation,
exact exit-77 recovery with fresh PID, manual/never policy and survivor/idle
checks, clean menu shutdown 88 and restart 89. Its preserved custom shell has
no fixture prompt markers: tests retain its configuration, prove live
cancellation, and explicitly observe its normal exit before final power action.
Actual stopped SD, exFAT and FAT32 pass read-only fsck after both power paths.
Linux stays up; the temporary guest-only power policy is removed and normal
stage 8/audio/RFB return. See `shutdown-ui-20261115/{native82-v2,menu82-v4,
power82-board,recovery82-board,restore82-restart-board}.log` on Beast.
The board evidence archive is verified before exact owned test-state cleanup;
normal services remain active with zero restarts and 8.5 GiB free. Physical
optical regression83 subsequently passes current native reads, exact busy eject,
Desktop Eject, media reinsertion and fresh Atlas access. Current State records
the remaining Linux-level controlled power-loss evidence gap separately.
Checkpoint47 qualifies HostFS failure retention on DE25:
busy checks follow request draining, failed retirement preserves public
endpoints, repeated close errors retain ownership, and failed READY delivery
reclaims provider authority instead of exiting. Host and ASan/UBSan tests pass.
Haiku
`src/system/kernel/fs/vfs.cpp:7888-8013` performs sync and busy checks before
destructive unmount work; Astra adapts that ordering to native service consent.

Deep cancellation testing also exposes missing ordinary service dependencies
in Supervisor's recovery snapshot. The existing per-process dependency list
now records received publications, including private GUI endpoints, alongside
narrowed filesystem grants. Shutdown uses all observed dependencies; namespace
recovery still waits only for filesystem bindings. Saved definitions inherit
the observed dependencies through the existing service-definition API; optional
capabilities that were not granted and implicit best-effort LOG access create
no teardown dependency. A requested restart retains its pending action when
termination reports that the target is already dead; otherwise a dependency
loss racing the stop syscall is misclassified as an unexpected critical exit.
Other termination errors remain failures. Haiku
`src/servers/launch/Job.cpp:235-287` orders job requirements and rejects cycles.
The complete native gate passes two injected close failures, preserved session
IDs, restored services, fresh I/O, provider reset, two HostFS replacements,
clean shutdown 88 and offline fsck in 61.4 seconds. DE25 passes the same
injected failures, preserved sessions, restored services and fresh I/O,
provider reset, HostFS replacement and clean shutdown 88 in 365/600 seconds.
All four gate outcomes are zero. The sealed archive verifies 66 hashes,
paired private settings/key, read-only fsck and unchanged Linux boot identity.
Disposable board files are removed after verified transfer; the unchanged
selected release reaches stage 8 with zero service restarts, loop0 only and
8.6 GiB free. This supersedes checkpoint46 for the scoped participant recovery;
the original trial31 refusal remains unexplained. The NDK
request acknowledges only that the supervisor accepted the request, not that
the power action completed. [Current State](CURRENT_STATE.md) records the exact
qualified build and production deployment status.
