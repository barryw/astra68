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
3. For each mounted filesystem, VFS releases sessions, invokes the backend's
   `flush` hook, then its `unmount` hook. Read-only filesystems such as CDFS
   use a no-op flush. Ext4 commits its journal, disables write-back, stops the
   journal, unmounts, unregisters the device, and flushes the block device.
   RAMFS releases its memory; HostFS closes guest sessions and lets Linux's
   normal poweroff flush its host filesystem. AstraFS and other backends
   provide their own hooks; the supervisor never calls filesystem-specific
   functions. Any failure prevents clean shutdown.
4. PID 1 invokes `ASTRA_SYSCALL_SYSTEM_SHUTDOWN` or
   `ASTRA_SYSCALL_SYSTEM_RESTART` only after every phase succeeds. Axiom
   rejects any other caller and refuses while any other guest process or PID 1
   worker thread is still live. It completes PID 1 resource teardown and
   writes a distinct kernel status marker. QEMU exits 88 for clean shutdown
   or 89 for clean restart; only those codes make the Linux launcher request
   `systemctl poweroff --no-block` or `systemctl reboot --no-block`,
   respectively. A crash, panic, unrelated QEMU exit, or `systemctl stop astra`
   never triggers either host power action.

Current implementation: the VFS mount lifecycle, ext4, RAMFS, and HostFS stop
handlers, process lifecycle transport, managed startup declaration, Terminal's
clean-prompt/zsh-exit gate, Supervisor reverse-order stop sequence, CLI and
desktop Shut Down/Restart actions, explicit kernel/QEMU power markers, and
Linux poweroff/reboot gates build with host tests. Beast-hosted QEMU boots from
the current source ran `shutdown` and `restart` in Terminal and exited with
codes 88 and 89; an edited/busy Terminal delayed Restart until it returned to
a clean prompt. QEMU pointer tests also clicked both ASTRA menu actions and
observed their respective clean-exit codes. The window-event NDK accepts all
three defined system actions and rejects unknown values.
Malformed wire replies have host tests. Desktop consent/status UI and recovery
from a cancellation after partial service teardown remain to be built. The NDK
request acknowledges only that the supervisor accepted the request, not that
the power action completed. No DE25 deployment has been made for this work.
