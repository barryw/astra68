# Astra storage and VFS

The [filesystem production PRD](FILESYSTEM_PRODUCTION_PRD.md) owns the production
contract. [Current State](CURRENT_STATE.md) records accepted source identities,
test evidence and remaining release gates; an implemented contract is not a
claim that physical removable media have been qualified.

## One filesystem implementation

Linux owns filesystem mounts, format drivers, partition probing, caching and
journaling. The Linux storage provider owns device, medium, source and mounted
instance records. QEMU exposes that provider through versioned Astra protocols.
HostFS publishes the native grants, inventory, notifications and control
endpoints. Supervisor owns startup grants and service policy. Applications use
`filesystem.library.14`; POSIX file descriptors adapt those same native objects.

The production bootstrap opens the selected system volume read-only through
the same provider, reads startup policy and closes its claim before HostFS
starts. Recovery uses that bootstrap when the system publication is absent.
Missing policy alone permits defaults; malformed policy, I/O, disabled policy
and failed claim cleanup refuse launch. There is no independent guest format
mount or alternate-media fallback.

The old guest ext4 and RAM backends remain test/development implementations,
not production fallbacks. Production RAM storage is bounded Linux tmpfs;
optical media and retained ISO files use Linux ISO9660/UDF support.

## Authority and identity

Storage protocol 38 is the minimum accepted version. Its fixed control record
and bounded operand areas carry 255-byte UTF-8 names and 4095-byte paths.
Filesystem Kit 11 exports Filesystem library ABI 14. HostChannel 5 carries the
kernel-stamped native PID separately from the resource owner: usage attribution
and mount requests use native process identity, while quotas and resource
retirement use the exact owner. Neither field is inferred from the other.

Only HostFS holds `HOST_FILESYSTEM` with scope-creation authority. Ordinary
`HOST_DEVICE` clients cannot create filesystem scopes. Each grant has a retained
provider root, rights and exact volume incarnation. Kernel-bound channels and
QEMU enforce those sessions and rights; client-side path checks are not the
trust boundary. Ordered descriptor lookup and `openat2` confinement prevent host
namespace traversal. Escape continuations return to the caller's native assign
resolver, which retains the link budget and rechecks mount-crossing authority.

Inventory metadata grants neither file access nor mount authority. Separate
read/write brokers admit exact public roots; administrator and usage
capabilities authorize lifecycle control and diagnostics. Configured RAM
brokers retain source identity while unmounted, so a new `/ram0` incarnation
cannot retarget an old grant.

## Objects and failure

Provider descriptions own offsets, directory cursors and append state. Fork
uses retained, single-use share/adopt receipts; it never reopens inherited files
by pathname. Only a transport implementing retained descriptions advertises
that feature. Epoch-checked local objects and pins prevent token reuse and
concurrent close from targeting replacement descriptions.

Unlink and rename preserve open-object identity. Current directories retain
directory objects. Native and POSIX resolution share one namespace, including
root dirfds, cross-volume links, trailing separators and no-follow operations.
File and directory sync propagate errors. Durable atomic save requires file
sync, rename and parent-directory sync; a successful write alone is not durable
acknowledgement.

Unmount fences new admission and refuses retained files, directories, shares,
active operations and image dependencies. The complete usage query powers both
Desktop's detailed busy dialog and `lsof`. Failed cleanup stays visible and
charged until the actual mount, descriptions and source lease retire. Names
become reusable only after that retirement; identities never recycle within a
provider epoch.

Provider loss invalidates old descriptions and grants. Old handles and CWDs
return lost/stale errors; they do not silently reconnect to replacement media.
Fresh absolute opens may obtain a new exact root through surviving brokers.
HostFS recovery restarts managed clients after its publications return. An
outside provider guardian retains exact child/domain lifetimes and completes
normal or fatal cleanup without scanning unrelated mounts or directories.

## Qualification

`emu/qemu/fs-gates.sh` runs resolver and hostile-caller checks, raw HostFS,
native/POSIX semantics, resource recovery, stress, offline fsck and host-channel
authority tests. `test-linux-first.py --lifecycle` covers real ISO mounts,
readonly enforcement, busy reporting, Atlas, source retirement, RAM remount,
HostFS recovery and durable shutdown. QEMU runs as the unprivileged caller;
privileged observers mount only private disposable images.

Physical USB discovery, optical media change/eject, device I/O faults and power
loss require their own hardware evidence. Killing QEMU does not simulate loss
of Linux's page cache. Performance qualification follows correctness; historical
lane measurements remain in this document's Git history and the durable stores
linked from Current State.
