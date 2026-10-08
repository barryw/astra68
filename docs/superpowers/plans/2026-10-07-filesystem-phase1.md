# Filesystem Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Revision 2026-10-08.** Task 3 (the HostFS relay, `62b72fc2`, `6d4e6a2c`)
cost about 2.6x per operation on QEMU, which the user rejected. The binding
decisions of 2026-10-08 (`.superpowers/sdd/progress.md`, "Filesystem Phase
1") replace the relay with a *scoped direct lane* (Task 3b), move
enforcement into the kernel binding and the provider (Tasks 4 and 5), add
the inline fast path (Task 12b) and change Tasks 8, 10, 11 and 13 to match.
The evidence is `.superpowers/sdd/fs-perf-investigation.md`. Tasks 1 and 2
are done and unchanged. Task 3 is done; Task 3b supersedes its data path.

**Goal:** Turn the existing HostFS path into the first increment of the
trusted Linux-backed filesystem contract: a recorded DE25 baseline, a closed
FS44 bypass, provider-enforced rooted grants reached through kernel-bound
scopes at the direct-path cost, Linux-ordered path resolution, stable
identity, retained objects and cwd, shared open descriptions, a defined
provider-loss result, measured host budgets, a demonstrated bootstrap read
and warm commands served on the vCPU -- each proven by tests that run on
every verify.

**Architecture:**

- **Control plane and data plane.** HostFS is the authority. It is the only
  process that holds the host device with `ASTRA_RIGHT_HOST_SCOPE`, so the
  only one that can create *scopes*, and the only one that opens provider
  sessions. Clients talk to HostFS only to get and give up authority: HELLO,
  BYE, `GRANT_DERIVE`. Every file operation goes client -> its own host
  channel -> QEMU, with no server between.
- **Scopes are kernel objects.** A scope names one provider session, its
  rights (read-only or not) and its creator. HostFS mints one per grant. A
  client receives a duplicate without ADMINISTER at HELLO, and without
  TRANSFER unless its grant is delegable. The kernel binds a channel to the
  scopes a thread attaches, and writes the binding into the channel
  configuration the provider reads: the client never supplies it.
  Revocation goes through the kernel, which detaches the scope from every
  channel at once.
- **QEMU enforces.** On a bound channel QEMU accepts only filesystem
  commands, resolves every path beneath the session root (`openat2`
  `RESOLVE_BENEATH` from the session's root descriptor), refuses mutations
  in a read-only scope, checks the generation, and ignores the command's
  owner. The command's session field selects one of the scopes the kernel
  attached to that channel; any other value is refused. Handles are tagged
  with the session they were opened in.
- **Grants are ports.** HostFS answers each grant on its own port. The kernel
  stamps which port a HELLO arrived on and who sent it. A holder may only
  *derive* a narrower grant. Each grant maps to a QEMU *session* whose root is
  an `O_PATH` directory beneath the volume.
- **Linux resolves, Astra routes.** QEMU walks storage paths in order (`.`,
  `..`, symlinks, trailing slashes) beneath the session root. When a path
  leaves the root, QEMU says where, and the Filesystem Kit continues in the
  Astra namespace. Neither libc nor the Kit folds `..` for a provider
  binding.
- **Linux objects carry the semantics.** Open files and directories are
  retained Linux descriptors in QEMU, keyed by the owning process; their
  position is the Linux open file description's. Fork shares one by a
  provider-held share token; cwd is a retained directory handle.

Design inputs: `docs/FILESYSTEM_PRODUCTION_PRD.md` (Phase 1 row),
`.superpowers/sdd/fs-phase1-design-brief.md` (D1-D12),
`.superpowers/sdd/fs-phase1-map.md`, `.superpowers/sdd/fs-linux-audit.md`,
`.superpowers/sdd/fs-perf-investigation.md` (the lane, measured).

**Tech Stack:** C11 freestanding kernel and userspace (`m68k-astra-gcc` on
`beast`), picolibc POSIX layer, the Astra QEMU 9.2.4 fork (`emu/qemu`), Linux
`openat2`, `statx`, `preadv2`/`pwritev2` (`RWF_APPEND`, `RWF_NOWAIT`), host
tests with plain `assert`, qtest and full-boot Python gates.

## Global Constraints

- **Build host.** Everything that builds or runs for real runs on `beast`, one
  build at a time, as `~/astra-mg/locked <command>` (`CLAUDE.md`, "One build at
  a time on beast, enforced").
- **Sync line, exactly** (`AGENTS.md`):
  `rtk rsync -az --checksum --no-times --delete --exclude .git --exclude build --exclude '*/build' ./ beast:/home/barry/Git/astra68/`
- **Build order.** `make -C sw/userspace` before `make -C sw/boot`: the ROM
  embeds the supervisor, which links userspace static libraries it has no
  rule to rebuild (`CLAUDE.md`).
- **QEMU edits.** After any edit under `emu/qemu/qemu-9.2/` run
  `ASTRA_QEMU_WORK_ROOT=$HOME/.cache/astra68/qemu-9.2.4 ~/astra-mg/locked emu/qemu/build.sh host`.
  Without `ASTRA_QEMU_WORK_ROOT` the NAS clock skew stops meson (`CLAUDE.md`).
- **Kernel tree.** `sw/kernel/build/` is shared by host tests and the m68k
  build: `make -C sw/kernel clean` before `make -C sw/kernel test`, and again
  before the ROM build.
- **Full suite, every task.** `~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-taskN.log`
  on `beast` (it takes the build lock itself; do not wrap it in `locked`).
  Green means: `BUILD-OK`, `IMAGES-OK`, every gate line `EXIT=0` or `PASS`,
  `SUITE-DONE`, `T9-ALL-DONE`, and no `FAIL`. From Task 2 on the suite
  includes `emu/qemu/fs-gates.sh`.
- **Perturbation, every behavior change.** Break the new code deliberately,
  re-sync, rebuild, see the named test fail, restore, re-sync with the same
  `--no-times` rsync, rebuild, see it pass. A green gate that cannot be made
  red has measured nothing (`CLAUDE.md`, "Passing gates are not evidence").
- **Never commit failing tests.** Commit only after the task's beast run is
  green. Stage explicit paths only: another agent works in this tree.
- **Compatibility discipline** (`AGENTS.md`). An incompatible change bumps the
  protocol or library major, updates every in-tree producer, consumer, test,
  manifest and document together, and deletes the superseded path. No
  aliases, no fallbacks, no repurposed private fields. A version number used
  by any commit on this branch is never reused for a different contract,
  even though none has shipped: a stale object built at that commit must
  fail its version check, not misread the new layout.
- **Versions in this plan** (each row is the value a task leaves):

  | Item | Task 3 (done) | 3b | 4 | 5 | 6 | 7 | 8 | 9 | 11 |
  |---|---|---|---|---|---|---|---|---|---|
  | Storage protocol `ASTRA_VFS_VERSION` | 27 | 28 | | 29 | 30 | 31 | 32 | 33 | |
  | Host transport `ASTRA_HOST_VERSION` | 0x0001000c | 0x0001000d | 0x0001000e | | 0x0001000f | | 0x00010010 | 0x00010011 | |
  | New host capability bit | | `FS_SCOPE` (10) | `FS_SESSIONS` (11) | | `FS_RESOLVES` (12) | | `FS_DESCRIPTIONS` (13) | `FS_PATHS` (14) | |
  | Channel config `ASTRA_HOST_CHANNEL_CONFIG_VERSION` | 1 | 2 (scope words, ATTACH, DETACH) | | | | | | | 3 (`scope_files`) |
  | Host command record `ASTRA_HOST_COMMAND_VERSION` | 2 | 2 | 3 | | | | | | |
  | Kernel syscalls | | `HOST_SCOPE_CREATE` 109, `HOST_SCOPE_REVOKE` 110, `HOST_SCOPE_ATTACH` 111 | | | | | | | `HOST_SCOPE_LIMIT` 112 |
  | Kernel object | | `KERNEL_OBJECT_HOST_SCOPE` 17 | | | | | | | |
  | Handle right | | `ASTRA_RIGHT_HOST_SCOPE` (1u << 9) | | | | | | | |
  | `runtime.library` | 1.13 | 1.14 (`astra_host_scope_*`) | | | | | | | 1.15 (`astra_host_scope_limit`) |
  | `filesystem.library` (Kit) | 5.0 | 6.0 (lane fields) | | 7.0 (`features`, `received_handle`) | | 8.0 (identity) | 8.1 | 8.2 | |
  | `ASTRA_HOST_METRICS_VERSION` | 2 | 2 | 3 | | | | 4 | 5 | |

  Every Filesystem Kit major bump edits Task 3 Step 7's file list and
  `ndk/tests/headers.c`, which asserts `ASTRA_FILESYSTEM_LIBRARY_VERSION`
  (Task 3's report, correction 3).
  The host *transport* major stays 1 (`sw/kernel/platform.c:1336-1338`): each
  new provider facility is a minor plus a capability bit, and every consumer
  that needs it checks the bit. The channel configuration is written only by
  the kernel and read only by QEMU, which accepts exactly the current
  version: kernel and QEMU ship together in a release.
- **Host filesystem operation registration.** Adding or removing an
  `ASTRA_HOST_FS_*` operation updates, in the same commit:
  `fs_group_names` in `sw/userspace/services/hostfs/main.c` (its static
  assert enforces the count), the QOM property table in
  `emu/qemu/qemu-9.2/hw/m68k/astra68.c` (search `astra-host-fs-invalid`),
  and `HOST_OPERATION_PROPERTIES` in `emu/qemu/test-filesystem-stress.py`.
  The per-operation metrics size the snapshot
  (`ASTRA_HOST_METRIC_COUNT`, `sw/include/astra/host.h:306-327`), so the same
  commit bumps `ASTRA_HOST_METRICS_VERSION`. `ASTRA_HOST_METRICS_SNAPSHOT_SIZE`
  is computed from the count (Task 4), and the static assert holds.
- **The lane rule.** From Task 3b on, a host-volume client (any
  `AstraVfsClient` connected through `astra_vfs_host_port_connect`) runs
  the host backend in its own process. Its operations reach QEMU through
  the Kit's lane: the fast paths in `sw/userspace/vfs/src/vfs_client.c`
  (each `if (client->direct_backend_ops != NULL)` block) and, for everything
  else, the request translator `astra_vfs_host_direct_transport` in
  `sw/userspace/vfs/src/vfs_host_direct.c`. HostFS's service core never
  sees a host-volume data operation. So every later task that adds a
  storage-protocol operation, flag or reply field for host volumes adds it
  to the translator (and to any fast path of the same operation), as each
  task's "Lane" step shows. The service-core path remains for ramfs,
  storage, proc, metrics and events.
- **One host channel per thread.** The kernel maps one doorbell page per
  thread slot, so a thread has at most one host channel. Every host-volume
  client of a process shares the thread's lane, and the lane carries up to
  `ASTRA_HOST_CHANNEL_SCOPE_MAX` (8) attached scopes. A process that drives
  the host device on one thread (entropy, media, remote desktop) cannot use
  a host volume on that same thread; none does in the shipped manifest.
- **Linux-only host code.** `openat2`, `statx`, `O_PATH`, `RWF_APPEND`,
  `RWF_NOWAIT`, `RESOLVE_CACHED` and `/proc/self/fd` are used under
  `#ifdef CONFIG_LINUX` in QEMU. The macOS `desktop` profile must still
  compile; there the provider answers `ASTRA_STATUS_UNSUPPORTED` for the
  operations it cannot honor, and never runs a command inline.
- **Kernel rules.** No libgcc, no 64-bit `/`, no variable 64-bit shift, no
  `= {0}` on word arrays (`CLAUDE.md`). Kernel changes are confined to
  Task 3b (the scope object, three syscalls, the import-rights hook, the
  channel binding) and Task 11 (DMA buffer records, the scope file limit).
  Each new path gets a kernel host test, and Task 3b's channel open is the
  only path it changes that a data operation crosses; no per-operation
  kernel work is added (`AGENTS.md`, Kernel Development Discipline).
- **Haiku first.** Shared descriptions follow Haiku's `file_descriptor`
  (`~/Git/haiku/headers/private/kernel/fs/fd.h:56-67`: `ref_count`, `pos`)
  and fork inheritance (`src/system/kernel/fs/vfs.cpp:5026-5080`,
  `vfs_new_io_context` copies parent descriptors). The retained cwd follows
  `io_context->cwd` as a referenced vnode (`vfs.cpp:8343` `set_cwd`) and
  `getcwd` as `dir_vnode_to_path` (`vfs.cpp:8318`, `:2637`). Ordered resolution
  follows `vnode_path_to_vnode` (`vfs.cpp:2204-2378`): `..` at the IO
  context root stays put, a link restarts at the root when absolute, and
  `B_LINK_LIMIT` bounds following. Revocation follows `disconnect_fd`
  (`src/system/kernel/fs/fd.cpp:262-264`): a disconnected descriptor stays
  in its table and answers an error until it is closed. Haiku has no server
  on the file path; Astra's equivalent of its kernel VFS is the kernel's
  scope binding plus the provider (investigation, section 2, "Haiku").
- **Evidence.** Measurements and run logs go to
  `/mnt/Documents/astra68/builds/fs-phase1-<checkpoint>-<shortsha>/` on the NAS
  (mounted on `beast`); never into git (`docs/ARTIFACT_POLICY.md`).
- **The board.** `root@192.168.1.52` from `beast` (the name `astra-de25` does
  not resolve on `beast`). It is Ubuntu 22.04, systemd 249, util-linux 2.37.2;
  QEMU runs as root with `LimitNOFILE` 524288 (measured 2026-10-07). Releases
  are retained and selectable with
  `python3 /var/lib/astra/current/bin/astra-release.py select /var/lib/astra current IDENTITY`
  (`tools/astra_release.py`).
- **Standing user decisions** (2026-10-07, still binding): after a HostFS
  restart the supervisor restarts its managed services that hold HostFS
  grants, and applications get `ESTALE` (Task 10); the provider-object host
  budget is 64 MiB (Task 11); Tasks 1, 11 and 13 may publish releases to the
  board; gates use the drop-privilege root helper (Task 2).
- **Follow-up outside this plan.** Keeping QEMU's softmmu TLB and jump cache
  across address-space switches (ASID-style tagging) is a separate
  investigation after Phase 1 (decision 2026-10-08). The lane makes the
  filesystem path independent of it.

## Decisions this plan changes, with evidence

**D2, how a grant is registered.** The brief says the supervisor registers
(owner process, volume, root, rights) with HostFS when it launches a process.
The code shows processes the supervisor never launches:

- Terminal delegates `WORK`, `CWD` and other namespace grants to every
  command with client-chosen flags and roots
  (`sw/userspace/services/terminal/console_session.c:99-214`; it strips LIBS
  write in its own memory, `:159-160`).
- `fork()` children inherit handles without the supervisor
  (`sw/userspace/posix/src/process.c:100-177`).

A pid-keyed registry would miss both. The correction uses what the kernel
already authenticates: **each grant is its own HostFS port**. The receiving
port is kernel-determined, and so is the sender. Launch, exec and fork
transfer the send handle under the kernel's existing rules, and a holder can
only narrow a grant (`GRANT_DERIVE`), never widen it. The tree already does
this for PROC: `PROC:rw` and `PROC:r` are two different ports
(`sw/userspace/supervisor/src/loader.c:978-996`). HostFS still binds every
session to the kernel-stamped sender. A session opened on one grant port is
refused on any other.

**D5, exec.** The brief has exec transfer handles with the share operation.
Exec is in place: same process and same sessions, with state exported and
re-imported (`sw/userspace/vfs/src/vfs_process.c:118-305`). `FD_CLOEXEC` is
already honored (`sw/userspace/posix/src/console.c:582-594`, `:714`). Exec
needs no share operation. Only fork does.

**D7, long paths.** The brief moves paths out of the 192-byte host command
fields and supports 4096-byte paths. Task 4 does that on the host wire. The
storage protocol between clients and HostFS stays at 192 bytes in Phase 1.
`ASTRA_VFS_PATH_MAX` is inline in every request record
(`sw/include/astra/vfs_service.h:105`, `:458-463`). Port messages are capped
at 1,024 inline bytes (`sw/include/astra/message_abi.h:15`). Every Kit and
POSIX path buffer is `char[ASTRA_VFS_PATH_MAX]`. The guest ext4 backend that
Phase 2 deletes also consumes these records. Moving the storage-protocol
path into the bound transfer area belongs after that deletion, as a Phase 2
task. FS18 is not in the Phase 1 exit list.

**D1, concurrency.** Routing every WORK request through HostFS with its
current single-threaded pump (`sw/userspace/services/hostfs/main.c:385-422`)
would serialize all clients behind one slow Linux miss. That is the PRD's
"concurrent cached reads under a held miss" metric. Task 3 gave HostFS
configured worker threads ("D1 again" below removes them with the relay). The port layer already supports concurrent
workers (`sw/userspace/vfs/include/astra/vfs_port_transport.h:162-165`,
`tests/test_vfs_port_threads.c`).

**D10, which volume.** Phase 1 has no Linux-side system volume. Task 12 adds
an optional QEMU `system` volume (`$ASTRA_HOSTFS_ROOT/system`). The
supervisor reads `/startup/system` from it before any service starts and
compares the bytes with the lwext4 read. The provider client is kept for the
Phase 2 cutover; the comparison is replaced by it.

**D1 again, 2026-10-08: the scoped direct lane.** Task 3 routed every
host-volume request through HostFS and measured the cost: 2.55x stat-warm
and 2.62x rand-read at the median on QEMU. The investigation
(`.superpowers/sdd/fs-perf-investigation.md`) traced it to four
cross-address-space switches per operation, about 45k host cycles (9 µs)
each under TCG, plus the kernel IPC around them. Guest instructions per
stat rise from 10.2k to 27.1k. Any relay needs at least two such switches
per synchronous operation, a floor near 1.7x. So the user chose the lane:

- HostFS keeps the authority and leaves the data path.
- The kernel binds each client channel to scopes HostFS minted.
- QEMU enforces the binding on every command.

The prototype (worktree `astra68-fsproto`, uncommitted) measured
0.98x/1.00x (stat median/p95) and 1.00x/1.02x (4 KiB read) against the
direct baseline, interleaved. Cross-AS switches were 0.01 per operation and
HostFS used no CPU. Running warm commands inline on the vCPU took a stat to
12.4 µs (0.36x). That is Task 12b. D1's worker-thread HostFS goes: the
control plane does not need it (Task 3b, "What survives").

Two prototype gaps are closed here:

- **Only the filesystem service can mint a scope.** The kernel requires
  `ASTRA_RIGHT_HOST_SCOPE` on the device handle (Task 3b), which the
  supervisor grants only through the `HOST_FILESYSTEM` capability. QEMU
  also requires a scope's creator to be the claimed filesystem owner
  (Task 4).
- **Handles carry their session.** QEMU tags every handle with the scope
  word it was opened under (Task 3b: a volume; Task 4: a session) and
  refuses it under any other.

Two prototype choices change, for reasons the prototype did not meet:

- **A channel carries several scopes.** The prototype bound a channel to
  one scope and ignored the command's volume. A thread has one channel,
  but a process holds several grants (Terminal's commands get `WORK` and a
  derived `CWD`; a game gets `WORK` and `SOUND`). So the kernel attaches up
  to eight scopes to a thread's channel. The command's volume or session
  word selects one of them. QEMU ignores the command's owner and refuses
  every word the kernel did not attach.
- **One scope per grant.** The prototype minted a scope per HELLO and never
  revoked it. A fork child shares the parent's scope, so revoking it at the
  parent's BYE would cut off the child. HostFS revokes a scope only when
  the authority goes:
  - the grant is retired (Task 5);
  - the provider is lost (Task 10);
  - HostFS dies (the kernel, Task 3b).

## File Structure

| File | Responsibility |
|---|---|
| `sw/userspace/commands/fsbench/fsbench.c` | Guest filesystem benchmark: D8 workloads, JSON result (Task 1) |
| `sw/userspace/commands/fsprobe/fsprobe.c` (new, 3b) | Per-operation cost probe: latency, cross-AS switches, per-process CPU and syscalls |
| `emu/qemu/fs-baseline.py` | Board-side baseline driver (Task 1) |
| `emu/qemu/fs-probe.py` (new, 3b) | Host harness for fsprobe; writes a JSON record |
| `tools/fs-baseline-compare.py`, `tools/fs-probe-compare.py` (new, 3b) | Compare records against the PRD budget and the lane's structural bounds |
| `tools/astra-qemu-root` | Narrow root helper for gates (Task 2) |
| `emu/qemu/qemu_runtime.py`, `emu/qemu/test-terminal.py` | Gate helpers (Task 2) |
| `sw/userspace/commands/fssem/fssem.c`, `emu/qemu/test-fs-semantics.py`, `emu/qemu/fs-gates.sh` | Semantic suite and gates (Task 2 on) |
| `sw/include/astra/object_abi.h`, `sw/include/astra/syscall.h`, `sw/include/astra/host.h` | `ASTRA_RIGHT_HOST_SCOPE`, scope syscalls 109-111, transport minors, capability bits, channel config 2-3, command record 3 |
| `sw/kernel/process.c`, `sw/kernel/handle.c`, `handle.h`, `device.h`, `platform.c`, `platform.h` | Scope object, binding, revocation, import rights; DMA buffer records (Task 11) |
| `sw/kernel/tests/test_process.c`, `test_platform.c`, `test_handle.c` | Kernel host tests for the above |
| `sw/userspace/runtime/src/syscall.c`, `runtime.library.exports`, `ndk/include/astra/runtime.h` | `astra_host_scope_create/revoke/attach` (runtime 1.14) |
| `emu/qemu/qemu-9.2/hw/m68k/astra68.c` | Provider: channel scopes, claim, sessions, v3 commands, base handles, loss, limits, inline execution |
| `emu/qemu/qemu-9.2/hw/m68k/astra68-resolve.c`, `.h` (new, Task 6) | Ordered path resolution beneath a session root |
| `emu/qemu/tests/test-astra68-resolve.c`, `Makefile` (new, Task 6) | Host test and benchmark of the resolver |
| `sw/include/astra/vfs_service.h`, `sw/include/astra/status.h` | Storage protocol 28-33; LOST and ESCAPED statuses |
| `sw/userspace/vfs/src/vfs_host_direct.c`, `include/astra/vfs_host_direct.h` (restored, 3b) | The Kit's lane: one process-wide transport, per-client backend, request translator |
| `sw/userspace/vfs/src/vfs_host_transport.c`, `include/astra/vfs_host_transport.h` | Per-thread lanes, scope attachment |
| `sw/userspace/vfs/src/backends/host.c`, `include/astra/vfs_host_backend.h` | The host backend (runs in every client), v3 commands, escapes, shares |
| `sw/userspace/vfs/src/vfs_port_transport.c`, `include/astra/vfs_port_transport.h` | HELLO hands out the grant's scope; control-plane ports; grant pumps |
| `sw/userspace/vfs/src/vfs_service_core.c`, `include/astra/vfs_service_core.h` | Grant-bound sessions (control plane) |
| `sw/userspace/vfs/src/vfs_union.c`, `vfs_assign.c`, `vfs_path.c` | Escape continuation; no lexical `..` for provider bindings |
| `sw/userspace/vfs/src/filesystem_library.c`, `vfs_client.c`, `vfs_process.c` | Lane fast paths, features, identity, descriptions, cwd helpers, exec and fork |
| `sw/userspace/services/hostfs/main.c` | Scopes, claim, sessions, grants, recovery, settings |
| `sw/userspace/posix/src/file.c`, `path.c`, `process.c` | Raw paths, identity, fork sharing, retained cwd |
| `sw/userspace/supervisor/src/loader.c`, `bootstrap_host.c` (new, Task 12) | `HOST_FILESYSTEM` grant; derived grants; restart of grant holders; bootstrap probe |
| `emu/qemu/astra_image.py` | HostFS manifest and settings |
| Docs | `AGENTS.md`, `docs/FILESYSTEM_PRODUCTION_PRD.md`, `docs/CURRENT_STATE.md`, `docs/STORAGE_AND_VFS.md`, `docs/DRIVER_AND_SERVICE_ARCHITECTURE.md`, `docs/ABI.md`, `docs/FILESYSTEM_KIT.md`, `ndk/docs/source/filesystem.md` |

---

### Task 1: DE25 baseline on the current source (D8)

No data-path change lands before this record exists. The only additions are
a guest benchmark command and a board-side driver, so the release measured
here runs exactly the current filesystem code.

**Files:**
- Create: `sw/userspace/commands/fsbench/fsbench.c`
- Modify: `sw/userspace/commands/Makefile` (add `fsbench` to `COMMANDS`, `fsbench_LIBRARIES`)
- Create: `emu/qemu/fs-baseline.py`
- Modify: `emu/qemu/create-de25-release.sh` (ship `bin/fs-baseline.py`)
- Modify: `emu/qemu/test-create-de25-release.py` (expect the new file)
- Create: `tools/fs-baseline-compare.py`

**Interfaces:**
- Produces: `fsbench --label NAME [--dir DIR] [--cold-handshake] [--only W]...`
  writing `/work/.astra/fsbench-NAME.json` and printing `fsbench: done NAME`.
  Workload names: `stat-cold stat-warm open-close-cold open-close-warm readdir
  seq-write seq-read-cold seq-read-warm rand-read rand-write append
  durable-save held-miss spawn`.
- Produces: `fs-baseline.py LABEL OUT.json [--restart] [--no-cold]` (runs on the
  board as root) writing `{"identity":{...},"host":{...},"guest":<fsbench JSON>}`.
- Produces: `tools/fs-baseline-compare.py BASE.json NEW.json` exiting 0 when
  every workload is within budget (median and p95 at most 10% higher,
  throughput at least 90% of baseline), 1 otherwise, printing one line per
  workload.
- Consumed by Task 2 (QEMU baseline through the gate harness), Task 3 (hop
  cost) and Task 13 (closing comparison).

- [ ] **Step 1: Write `sw/userspace/commands/fsbench/fsbench.c`**

```c
/*
 * fsbench -- the filesystem baseline (docs/FILESYSTEM_PRODUCTION_PRD.md,
 * "Acceptance and performance gates").
 *
 *   fsbench --label NAME [--dir DIR] [--cold-handshake] [--only W]...
 *
 * Runs each workload over DIR (default /work/fsbench-NAME) and writes one
 * JSON object to /work/.astra/fsbench-NAME.json, then prints
 * "fsbench: done NAME". With --cold-handshake it stops before each cold
 * workload, creates /work/.astra/fsbench-NAME.cold, prints
 * "fsbench: cold NAME", and waits (60 s at most) until a host driver has
 * dropped the Linux page cache and removed the file.
 *
 * Times are CLOCK_MONOTONIC, which Astra derives from QEMU's guest timebase:
 * 12.5 MHz, so MC68040 cycles = ns / 80. "host_commands" and "max_inflight"
 * are the HostFS host.channel counters around the workload, when METRICS: is
 * granted; null otherwise.
 */

#define _POSIX_C_SOURCE 200809L

#include <astra/metrics.h>
#include <astra/program.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

ASTRA_PROGRAM("fsbench", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

#define FILES 200u
#define BIG_BYTES (4u * 1024u * 1024u)
#define CHUNK (64u * 1024u)
#define SMALL 4096u
#define RANDOM_OPS 1024u
#define APPENDS 1000u
#define APPEND_BYTES 100u
#define SAVES 20u
#define SAVE_BYTES (64u * 1024u)
#define SCANS 20u
#define SPAWNS 10u
#define HELD_READS 200u
#define SAMPLES_MAX 1024u
#define NS_PER_CYCLE 80u

typedef struct Result {
    const char *name;
    uint32_t count;
    uint64_t samples[SAMPLES_MAX];
    uint64_t total_ns;
    uint64_t bytes;
    uint64_t commands_before;
    uint64_t commands_after;
    uint64_t max_inflight;
    int metrics;
    int ran;
    int failed;
} Result;

enum {
    W_STAT_COLD, W_STAT_WARM, W_OPEN_COLD, W_OPEN_WARM, W_READDIR,
    W_SEQ_WRITE, W_SEQ_READ_COLD, W_SEQ_READ_WARM, W_RAND_READ,
    W_RAND_WRITE, W_APPEND, W_DURABLE, W_HELD_MISS, W_SPAWN, W_COUNT
};

static const char *const names[W_COUNT] = {
    "stat-cold", "stat-warm", "open-close-cold", "open-close-warm",
    "readdir", "seq-write", "seq-read-cold", "seq-read-warm", "rand-read",
    "rand-write", "append", "durable-save", "held-miss", "spawn"
};

static Result results[W_COUNT];
static uint8_t chunk[CHUNK];
static char directory[128];
static char label[48];
static int handshake;
static int only[W_COUNT];
static int any_only;

static uint64_t now_ns(void)
{
    struct timespec time;

    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * UINT64_C(1000000000) +
           (uint64_t)time.tv_nsec;
}

static int wanted(uint32_t workload)
{
    return !any_only || only[workload];
}

static void file_name(char *out, size_t capacity, const char *leaf,
                      uint32_t index)
{
    (void)snprintf(out, capacity, "%s/%s%03u", directory, leaf,
                   (unsigned)index);
}

/* The HostFS channel counters, or 0 when METRICS: is not granted. */
static int host_counter(const char *name, uint64_t *value)
{
    AstraMetricRecord record;
    int fd = open("/metrics/snapshot", O_RDONLY);
    int found = 0;

    if (fd < 0)
        return 0;
    while (read(fd, &record, sizeof(record)) == (ssize_t)sizeof(record)) {
        if (record.size == sizeof(record) &&
            strncmp(record.group, "host.channel", sizeof(record.group)) == 0 &&
            strncmp(record.name, name, sizeof(record.name)) == 0) {
            *value = astra_metric_record_value(&record);
            found = 1;
            break;
        }
    }
    (void)close(fd);
    return found;
}

static void begin(Result *result, uint32_t workload)
{
    result->name = names[workload];
    result->count = 0u;
    result->total_ns = 0u;
    result->bytes = 0u;
    result->ran = 1;
    result->metrics = host_counter("commands", &result->commands_before);
}

static void sample(Result *result, uint64_t elapsed)
{
    if (result->count < SAMPLES_MAX)
        result->samples[result->count++] = elapsed;
    result->total_ns += elapsed;
}

static void end(Result *result)
{
    if (result->metrics)
        result->metrics =
            host_counter("commands", &result->commands_after) &&
            host_counter("max_inflight", &result->max_inflight);
}

static int compare(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *)left;
    uint64_t b = *(const uint64_t *)right;

    return a < b ? -1 : a > b ? 1 : 0;
}

static uint64_t percentile(Result *result, uint32_t percent)
{
    uint32_t index;

    if (result->count == 0u)
        return 0u;
    qsort(result->samples, result->count, sizeof(result->samples[0]),
          compare);
    index = (result->count * percent + 99u) / 100u;
    return result->samples[index == 0u ? 0u : index - 1u];
}

static int cold_point(void)
{
    char path[128];
    int fd;

    if (!handshake)
        return 0;
    (void)snprintf(path, sizeof(path), "/work/.astra/fsbench-%s.cold", label);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;
    (void)close(fd);
    printf("fsbench: cold %s\n", label);
    fflush(stdout);
    for (uint32_t waited = 0u; waited < 600u; ++waited) {
        struct stat st;
        struct timespec pause = {0, 100000000L};

        if (stat(path, &st) != 0 && errno == ENOENT)
            return 0;
        (void)nanosleep(&pause, NULL);
    }
    return -1;
}

static int write_whole(const char *path, uint32_t bytes)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    uint32_t done = 0u;

    if (fd < 0)
        return -1;
    while (done < bytes) {
        uint32_t part = bytes - done < CHUNK ? bytes - done : CHUNK;
        ssize_t moved = write(fd, chunk, part);

        if (moved <= 0) {
            (void)close(fd);
            return -1;
        }
        done += (uint32_t)moved;
    }
    return close(fd);
}

static int prepare(void)
{
    char path[160];

    for (uint32_t index = 0u; index < CHUNK; ++index)
        chunk[index] = (uint8_t)(index * 131u + 7u);
    if (mkdir(directory, 0755) != 0) {
        fprintf(stderr, "fsbench: %s exists or cannot be made; "
                "use a new --label\n", directory);
        return -1;
    }
    for (uint32_t index = 0u; index < FILES; ++index) {
        file_name(path, sizeof(path), "f", index);
        if (write_whole(path, 64u) != 0)
            return -1;
    }
    (void)snprintf(path, sizeof(path), "%s/big", directory);
    if (write_whole(path, BIG_BYTES) != 0)
        return -1;
    (void)snprintf(path, sizeof(path), "%s/big2", directory);
    return write_whole(path, BIG_BYTES);
}

static void run_stat(Result *result)
{
    char path[160];

    for (uint32_t index = 0u; index < FILES; ++index) {
        struct stat st;
        uint64_t start;

        file_name(path, sizeof(path), "f", index);
        start = now_ns();
        if (stat(path, &st) != 0)
            result->failed = 1;
        sample(result, now_ns() - start);
    }
}

static void run_open_close(Result *result)
{
    char path[160];

    for (uint32_t index = 0u; index < FILES; ++index) {
        uint64_t start;
        int fd;

        file_name(path, sizeof(path), "f", index);
        start = now_ns();
        fd = open(path, O_RDONLY);
        if (fd < 0 || close(fd) != 0)
            result->failed = 1;
        sample(result, now_ns() - start);
    }
}

static void run_readdir(Result *result)
{
    for (uint32_t scan = 0u; scan < SCANS; ++scan) {
        uint64_t start = now_ns();
        DIR *listing = opendir(directory);
        uint32_t entries = 0u;

        if (listing == NULL) {
            result->failed = 1;
            return;
        }
        while (readdir(listing) != NULL)
            ++entries;
        (void)closedir(listing);
        sample(result, now_ns() - start);
        if (entries < FILES)
            result->failed = 1;
    }
}

static void run_sequential(Result *result, int writing)
{
    char path[160];
    int fd;

    (void)snprintf(path, sizeof(path), "%s/%s", directory,
                   writing ? "written" : "big");
    fd = writing ? open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644) :
                   open(path, O_RDONLY);
    if (fd < 0) {
        result->failed = 1;
        return;
    }
    for (uint32_t done = 0u; done < BIG_BYTES; done += CHUNK) {
        uint64_t start = now_ns();
        ssize_t moved = writing ? write(fd, chunk, CHUNK) :
                                  read(fd, chunk, CHUNK);

        sample(result, now_ns() - start);
        if (moved != (ssize_t)CHUNK) {
            result->failed = 1;
            break;
        }
        result->bytes += CHUNK;
    }
    (void)close(fd);
}

static void run_random(Result *result, int writing)
{
    char path[160];
    uint32_t state = 0x68040u;
    int fd;

    (void)snprintf(path, sizeof(path), "%s/big", directory);
    fd = open(path, writing ? O_RDWR : O_RDONLY);
    if (fd < 0) {
        result->failed = 1;
        return;
    }
    for (uint32_t op = 0u; op < RANDOM_OPS; ++op) {
        off_t offset;
        uint64_t start;
        ssize_t moved;

        state = state * 1664525u + 1013904223u;
        offset = (off_t)((state >> 8) % (BIG_BYTES / SMALL)) * SMALL;
        start = now_ns();
        moved = writing ? pwrite(fd, chunk, SMALL, offset) :
                          pread(fd, chunk, SMALL, offset);
        sample(result, now_ns() - start);
        if (moved != (ssize_t)SMALL) {
            result->failed = 1;
            break;
        }
        result->bytes += SMALL;
    }
    (void)close(fd);
}

static void run_append(Result *result)
{
    char path[160];
    int fd;

    (void)snprintf(path, sizeof(path), "%s/appended", directory);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
    if (fd < 0) {
        result->failed = 1;
        return;
    }
    for (uint32_t op = 0u; op < APPENDS; ++op) {
        uint64_t start = now_ns();
        ssize_t moved = write(fd, chunk, APPEND_BYTES);

        sample(result, now_ns() - start);
        if (moved != (ssize_t)APPEND_BYTES) {
            result->failed = 1;
            break;
        }
        result->bytes += APPEND_BYTES;
    }
    (void)close(fd);
}

/* write, fsync, rename over the target, fsync the directory. */
static void run_durable(Result *result)
{
    char temporary[160];
    char target[160];

    (void)snprintf(temporary, sizeof(temporary), "%s/save.tmp", directory);
    (void)snprintf(target, sizeof(target), "%s/save", directory);
    for (uint32_t op = 0u; op < SAVES; ++op) {
        uint64_t start = now_ns();
        int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        int ok = fd >= 0;
        int parent;

        for (uint32_t done = 0u; ok && done < SAVE_BYTES; done += CHUNK)
            ok = write(fd, chunk, CHUNK) == (ssize_t)CHUNK;
        ok = ok && fsync(fd) == 0;
        if (fd >= 0)
            ok = close(fd) == 0 && ok;
        ok = ok && rename(temporary, target) == 0;
        parent = open(directory, O_RDONLY | O_DIRECTORY);
        ok = ok && parent >= 0 && fsync(parent) == 0;
        if (parent >= 0)
            (void)close(parent);
        sample(result, now_ns() - start);
        if (!ok) {
            result->failed = 1;
            break;
        }
        result->bytes += SAVE_BYTES;
    }
}

typedef struct Miss {
    pthread_t thread;
    int fd;
    int failed;
} Miss;

static void *read_miss(void *argument)
{
    static uint8_t buffer[CHUNK];
    Miss *miss = argument;

    for (uint32_t done = 0u; done < BIG_BYTES; done += CHUNK)
        if (read(miss->fd, buffer, CHUNK) != (ssize_t)CHUNK) {
            miss->failed = 1;
            break;
        }
    return NULL;
}

/* Cached 4 KiB reads of `big` while another thread reads cold `big2`. */
static void run_held_miss(Result *result)
{
    char cold[160];
    char warm[160];
    Miss miss = {0};
    int fd;

    (void)snprintf(cold, sizeof(cold), "%s/big2", directory);
    (void)snprintf(warm, sizeof(warm), "%s/big", directory);
    miss.fd = open(cold, O_RDONLY);
    fd = open(warm, O_RDONLY);
    if (miss.fd < 0 || fd < 0 ||
        pthread_create(&miss.thread, NULL, read_miss, &miss) != 0) {
        result->failed = 1;
        if (miss.fd >= 0)
            (void)close(miss.fd);
        if (fd >= 0)
            (void)close(fd);
        return;
    }
    for (uint32_t op = 0u; op < HELD_READS; ++op) {
        uint64_t start = now_ns();
        ssize_t moved = pread(fd, chunk, SMALL, (off_t)(op % 64u) * SMALL);

        sample(result, now_ns() - start);
        if (moved != (ssize_t)SMALL)
            result->failed = 1;
        result->bytes += SMALL;
    }
    (void)pthread_join(miss.thread, NULL);
    result->failed |= miss.failed;
    (void)close(fd);
    (void)close(miss.fd);
}

/* Process start: fork, exec /commands/lua with an empty chunk, wait. */
static void run_spawn(Result *result)
{
    for (uint32_t op = 0u; op < SPAWNS; ++op) {
        uint64_t start = now_ns();
        int status = 0;
        pid_t child = fork();

        if (child == 0) {
            char *const argv[] = {"lua", "-e", "", NULL};

            execv("/commands/lua", argv);
            _exit(127);
        }
        if (child < 0 || waitpid(child, &status, 0) != child ||
            !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            result->failed = 1;
        sample(result, now_ns() - start);
    }
}

static int report(void)
{
    char temporary[128];
    char path[128];
    FILE *out;

    (void)snprintf(temporary, sizeof(temporary),
                   "/work/.astra/fsbench-%s.json.tmp", label);
    (void)snprintf(path, sizeof(path), "/work/.astra/fsbench-%s.json", label);
    out = fopen(temporary, "w");
    if (out == NULL)
        return -1;
    fprintf(out, "{\"label\":\"%s\",\"ns_per_cycle\":%u,\"workloads\":{",
            label, NS_PER_CYCLE);
    for (uint32_t index = 0u, first = 1u; index < W_COUNT; ++index) {
        Result *result = &results[index];
        uint64_t median;
        uint64_t p95;

        if (!result->ran)
            continue;
        median = percentile(result, 50u);
        p95 = percentile(result, 95u);
        fprintf(out, "%s\"%s\":{\"ops\":%u,\"failed\":%s,"
                "\"median_ns\":%llu,\"p95_ns\":%llu,\"total_ns\":%llu,"
                "\"cycles_per_op\":%llu,\"bytes\":%llu,",
                first ? "" : ",", result->name, (unsigned)result->count,
                result->failed ? "true" : "false",
                (unsigned long long)median, (unsigned long long)p95,
                (unsigned long long)result->total_ns,
                (unsigned long long)(median / NS_PER_CYCLE),
                (unsigned long long)result->bytes);
        if (result->bytes != 0u && result->total_ns != 0u)
            fprintf(out, "\"throughput_bps\":%llu,",
                    (unsigned long long)(result->bytes *
                                         UINT64_C(1000000000) /
                                         result->total_ns));
        else
            fprintf(out, "\"throughput_bps\":null,");
        if (result->metrics)
            fprintf(out, "\"host_commands\":%llu,\"max_inflight\":%llu}",
                    (unsigned long long)(result->commands_after -
                                         result->commands_before),
                    (unsigned long long)result->max_inflight);
        else
            fprintf(out, "\"host_commands\":null,\"max_inflight\":null}");
        first = 0u;
    }
    fprintf(out, "}}\n");
    if (fclose(out) != 0 || rename(temporary, path) != 0)
        return -1;
    printf("fsbench: done %s\n", label);
    return 0;
}

int main(int argc, char **argv)
{
    int failed = 0;

    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--label") == 0 && index + 1 < argc)
            (void)snprintf(label, sizeof(label), "%s", argv[++index]);
        else if (strcmp(argv[index], "--dir") == 0 && index + 1 < argc)
            (void)snprintf(directory, sizeof(directory), "%s",
                           argv[++index]);
        else if (strcmp(argv[index], "--cold-handshake") == 0)
            handshake = 1;
        else if (strcmp(argv[index], "--only") == 0 && index + 1 < argc) {
            const char *name = argv[++index];
            uint32_t at = 0u;

            while (at < W_COUNT && strcmp(names[at], name) != 0)
                ++at;
            if (at == W_COUNT) {
                fprintf(stderr, "fsbench: no workload %s\n", name);
                return 2;
            }
            only[at] = 1;
            any_only = 1;
        } else {
            fprintf(stderr, "usage: fsbench --label NAME [--dir DIR] "
                    "[--cold-handshake] [--only WORKLOAD]...\n");
            return 2;
        }
    }
    if (label[0] == '\0')
        return 2;
    if (directory[0] == '\0')
        (void)snprintf(directory, sizeof(directory), "/work/fsbench-%s",
                       label);
    (void)mkdir("/work/.astra", 0755);
    if (prepare() != 0) {
        fprintf(stderr, "fsbench: prepare failed: %s\n", strerror(errno));
        return 1;
    }
    for (uint32_t workload = 0u; workload < W_COUNT; ++workload) {
        Result *result = &results[workload];
        int cold = workload == W_STAT_COLD || workload == W_OPEN_COLD ||
                   workload == W_SEQ_READ_COLD || workload == W_HELD_MISS;

        if (!wanted(workload))
            continue;
        if (cold && cold_point() != 0) {
            fprintf(stderr, "fsbench: no cold handshake\n");
            return 1;
        }
        begin(result, workload);
        switch (workload) {
        case W_STAT_COLD: case W_STAT_WARM: run_stat(result); break;
        case W_OPEN_COLD: case W_OPEN_WARM: run_open_close(result); break;
        case W_READDIR: run_readdir(result); break;
        case W_SEQ_WRITE: run_sequential(result, 1); break;
        case W_SEQ_READ_COLD: case W_SEQ_READ_WARM:
            run_sequential(result, 0);
            break;
        case W_RAND_READ: run_random(result, 0); break;
        case W_RAND_WRITE: run_random(result, 1); break;
        case W_APPEND: run_append(result); break;
        case W_DURABLE: run_durable(result); break;
        case W_HELD_MISS: run_held_miss(result); break;
        default: run_spawn(result); break;
        }
        end(result);
        failed |= result->failed;
    }
    if (report() != 0) {
        fprintf(stderr, "fsbench: cannot write the result\n");
        return 1;
    }
    return failed ? 1 : 0;
}
```

`spawn` runs last on purpose. `fork()` refuses while a regular file is open
(`astra_posix_file_fork_ready`, `sw/userspace/posix/src/file.c:205-211`), and
every workload before it closes what it opened.

- [ ] **Step 2: Add it to the commands build**

In `sw/userspace/commands/Makefile`, add `fsbench` to `COMMANDS` after
`fsstress` (it ships: the board baseline runs it), and add the library
declaration beside `fsstress_LIBRARIES`:

```make
fsbench_LIBRARIES := $(LIBC_LIBRARY) $(RUNTIME_LIBRARY)
```

`astra_metric_record_value` is `static inline` in `sw/include/astra/metrics.h:45`,
so the command needs no filesystem library.

On beast: `~/astra-mg/locked make -C sw/userspace/commands build/m68k/fsbench`
Expected: the image links; `m68k-astra-readelf -dW sw/userspace/commands/build/m68k/fsbench | grep NEEDED`
lists exactly `libc.library` and `runtime.library` (the executable contract in
the Makefile checks the same list).

- [ ] **Step 3: Write the board driver `emu/qemu/fs-baseline.py`**

```python
#!/usr/bin/env python3
"""DE25 filesystem baseline (docs/FILESYSTEM_PRODUCTION_PRD.md).

usage: fs-baseline.py LABEL OUT.json [--restart] [--no-cold]

Runs on the board, as root, from the release's bin directory:
  - with --restart, restarts astra.service and waits for the remote desktop;
  - opens Terminal by double-clicking its icon (the desktop logs the icon's
    rectangle as "desktop icon /apps/Terminal.app L T W H");
  - types `fsbench --label LABEL --cold-handshake`;
  - at each cold marker drops the Linux page cache and removes the marker;
  - samples QEMU's RSS, descriptor count and CPU time before and after;
  - writes {"identity", "host", "guest"} to OUT.json.
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
STORE = os.environ.get("ASTRA_STORE", "/var/lib/astra")
WORK = os.path.join(STORE, "hostfs", "work")
ASTRA = os.path.join(WORK, ".astra")
PASSWORD = "/etc/astra/remote-desktop.password"
ICON = re.compile(r"desktop icon /apps/Terminal\.app (\d+) (\d+) (\d+) (\d+)$")
QEMU_NAME = "qemu-system-m68k-astra"


def rfb(*arguments):
    frame = os.path.join("/tmp", "fs-baseline-frame.raw")
    return subprocess.run(
        [sys.executable, os.path.join(HERE, "de25-rfb.py"), frame,
         "--password-file", PASSWORD] + list(arguments),
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


def syslog():
    text = ""
    for name in ("syslog.3", "syslog.2", "syslog.1", "syslog"):
        path = os.path.join(ASTRA, name)
        if os.path.exists(path):
            with open(path, encoding="utf-8", errors="replace") as source:
                text += source.read()
    return text


def terminal_icon(deadline):
    end = time.monotonic() + deadline
    while time.monotonic() < end:
        for line in reversed(syslog().splitlines()):
            match = ICON.search(line)
            if match:
                left, top, width, height = (int(v) for v in match.groups())
                return left + width // 2, top + height // 2
        time.sleep(1.0)
    raise SystemExit("fs-baseline: the desktop never logged Terminal's icon")


def qemu_pid():
    output = subprocess.run(["pidof", QEMU_NAME], capture_output=True,
                            text=True).stdout.split()
    if len(output) != 1:
        raise SystemExit("fs-baseline: expected one %s, found %r" %
                         (QEMU_NAME, output))
    return int(output[0])


def host_sample(pid):
    with open("/proc/%d/status" % pid) as source:
        status = dict(line.split(":", 1) for line in source if ":" in line)
    with open("/proc/%d/stat" % pid) as source:
        fields = source.read().rsplit(")", 1)[1].split()
    ticks = os.sysconf("SC_CLK_TCK")
    return {
        "rss_kib": int(status["VmRSS"].split()[0]),
        "fds": len(os.listdir("/proc/%d/fd" % pid)),
        "threads": int(status["Threads"].split()[0]),
        # utime and stime are fields 14 and 15 of /proc/PID/stat, which are
        # indices 11 and 12 once the "pid (comm)" prefix is removed.
        "cpu_s": (int(fields[11]) + int(fields[12])) / ticks,
        "mem_available_kib": int(open("/proc/meminfo").read().split(
            "MemAvailable:")[1].split()[0]),
    }


def identity():
    current = os.path.realpath(os.path.join(STORE, "current"))
    result = {"release": os.path.basename(current)}
    for name in ("artifacts.sha256", "source/SOURCE_SHA256SUMS"):
        path = os.path.join(current, name)
        with open(path, "rb") as source:
            result[name] = hashlib.sha256(source.read()).hexdigest()
    result["kernel"] = os.uname().release
    return result


def drop_caches():
    subprocess.run(["sync"], check=True)
    with open("/proc/sys/vm/drop_caches", "w") as control:
        control.write("3\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("label")
    parser.add_argument("output")
    parser.add_argument("--restart", action="store_true")
    parser.add_argument("--no-cold", action="store_true")
    arguments = parser.parse_args()
    if os.geteuid() != 0:
        raise SystemExit("fs-baseline: run as root on the board")
    if arguments.restart:
        subprocess.run(["systemctl", "restart", "astra.service"], check=True)
        for _ in range(90):
            if rfb():
                break
            time.sleep(2)
        else:
            raise SystemExit("fs-baseline: the remote desktop never answered")
        time.sleep(20)
    x, y = terminal_icon(60)
    if not rfb("--double-click", str(x), str(y)):
        raise SystemExit("fs-baseline: double click failed")
    time.sleep(15)
    pid = qemu_pid()
    before = host_sample(pid)
    result_path = os.path.join(ASTRA, "fsbench-%s.json" % arguments.label)
    cold_path = os.path.join(ASTRA, "fsbench-%s.cold" % arguments.label)
    command = "fsbench --label %s%s\n" % (
        arguments.label, "" if arguments.no_cold else " --cold-handshake")
    if not rfb("--keys", command):
        raise SystemExit("fs-baseline: typing failed")
    deadline = time.monotonic() + 1800
    peak = dict(before)
    while not os.path.exists(result_path):
        if time.monotonic() > deadline:
            raise SystemExit("fs-baseline: fsbench never finished")
        if os.path.exists(cold_path):
            drop_caches()
            os.unlink(cold_path)
        current = host_sample(pid)
        peak["rss_kib"] = max(peak["rss_kib"], current["rss_kib"])
        peak["fds"] = max(peak["fds"], current["fds"])
        time.sleep(0.2)
    after = host_sample(pid)
    with open(result_path) as source:
        guest = json.load(source)
    record = {"identity": identity(),
              "host": {"before": before, "after": after, "peak": peak,
                       "cpu_s": after["cpu_s"] - before["cpu_s"]},
              "guest": guest}
    with open(arguments.output, "w") as output:
        json.dump(record, output, indent=1, sort_keys=True)
    print("fs-baseline: wrote %s" % arguments.output)


if __name__ == "__main__":
    main()
```

The driver polls every 200 ms, which is how it finds the cold marker. When it
finds one, it runs `sync` and `echo 3 > /proc/sys/vm/drop_caches`, then
deletes the marker. That deletion is the go signal: the guest's `stat` of the
marker starts failing and fsbench continues.

- [ ] **Step 4: Ship it in the release**

In `emu/qemu/create-de25-release.sh`, add after the
`"bin/de25-rfb.py=$SCRIPT_DIR/de25-rfb.py" \` line:

```sh
    "bin/fs-baseline.py=$SCRIPT_DIR/fs-baseline.py" \
```

In `emu/qemu/test-create-de25-release.py`, add `"bin/fs-baseline.py"` to the
list of expected release files. Find the list with
`grep -n "de25-rfb.py" emu/qemu/test-create-de25-release.py`.

- [ ] **Step 5: Write `tools/fs-baseline-compare.py`**

```python
#!/usr/bin/env python3
"""Compare two fs-baseline records against the PRD's release budget.

usage: fs-baseline-compare.py BASE.json NEW.json [--budget 0.10]

A workload passes when its median and p95 are at most (1 + budget) times
the baseline's, and its throughput, when it has one, is at least
(1 - budget) times the baseline's. Exit 0 when every workload present in
both passes, 1 otherwise. docs/FILESYSTEM_PRODUCTION_PRD.md, "Acceptance
and performance gates".
"""

import argparse
import json
import sys


def workloads(path):
    with open(path) as source:
        record = json.load(source)
    return record.get("guest", record)["workloads"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("base")
    parser.add_argument("new")
    parser.add_argument("--budget", type=float, default=0.10)
    arguments = parser.parse_args()
    base = workloads(arguments.base)
    new = workloads(arguments.new)
    failed = False
    for name in sorted(set(base) & set(new)):
        old, now = base[name], new[name]
        verdicts = []
        for key in ("median_ns", "p95_ns"):
            ratio = now[key] / old[key] if old[key] else 1.0
            ok = ratio <= 1.0 + arguments.budget
            verdicts.append("%s %.2fx%s" % (key, ratio, "" if ok else "!"))
            failed |= not ok
        if old.get("throughput_bps") and now.get("throughput_bps"):
            ratio = now["throughput_bps"] / old["throughput_bps"]
            ok = ratio >= 1.0 - arguments.budget
            verdicts.append("throughput %.2fx%s" % (ratio, "" if ok else "!"))
            failed |= not ok
        if now.get("failed"):
            verdicts.append("FAILED-RUN")
            failed = True
        print("%-16s %s" % (name, "  ".join(verdicts)))
    print("BUDGET %s" % ("FAIL" if failed else "PASS"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
```

Self-check on the Mac before relying on it:

```sh
python3 - <<'EOF'
import json, subprocess, tempfile, os
d = tempfile.mkdtemp()
base = {"guest": {"workloads": {"w": {"median_ns": 100, "p95_ns": 200,
        "throughput_bps": 1000, "failed": False}}}}
slow = {"guest": {"workloads": {"w": {"median_ns": 111, "p95_ns": 200,
        "throughput_bps": 1000, "failed": False}}}}
for name, value in (("b", base), ("s", slow)):
    json.dump(value, open(os.path.join(d, name), "w"))
tool = "tools/fs-baseline-compare.py"
assert subprocess.run(["python3", tool, d + "/b", d + "/b"]).returncode == 0
assert subprocess.run(["python3", tool, d + "/b", d + "/s"]).returncode == 1
print("COMPARE SELF-CHECK PASS")
EOF
```

Expected: `COMPARE SELF-CHECK PASS`.

- [ ] **Step 6: Verify the tree on beast, then publish the baseline release**

On the Mac:
`rtk rsync -az --checksum --no-times --delete --exclude .git --exclude build --exclude '*/build' ./ beast:/home/barry/Git/astra68/`

On beast, in `~/Git/astra68`:

```sh
~/astra-mg/locked make -C sw/userspace -j32
~/astra-mg/locked make -C sw/userspace test
~/astra-mg/locked python3 emu/qemu/test-create-de25-release.py
~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-task1.log
```

Expected: the userspace tests PASS, the release test passes, and the t9 log is
green (Global Constraints). Then publish exactly this tree to the board:

```sh
~/astra-mg/locked emu/qemu/publish-de25-release.sh 2>&1 | tee /tmp/fs-phase1-publish-baseline.log
```

Expected: the publisher's source manifest check (`cmp "$SOURCE_BEFORE" "$SOURCE_AFTER"`)
passes and the deploy step reports the new release current.

- [ ] **Step 7: Record the baseline on the board**

From beast:

```sh
SHA=$(git -C ~/Git/astra68 rev-parse --short HEAD)
OUT=/mnt/Documents/astra68/builds/fs-phase1-baseline-$(date +%Y%m%d)-$SHA
mkdir -p "$OUT"
for run in 1 2 3; do
  ssh root@192.168.1.52 "python3 /var/lib/astra/current/bin/fs-baseline.py base$run /data/fs-base$run.json --restart" &&
  scp root@192.168.1.52:/data/fs-base$run.json "$OUT/"
done
ssh root@192.168.1.52 'cat /proc/$(pidof qemu-system-m68k-astra)/limits; free -k; uname -a' > "$OUT/board.txt"
git -C ~/Git/astra68 rev-parse HEAD > "$OUT/source-commit.txt"
cp /tmp/t9-task1.log /tmp/fs-phase1-publish-baseline.log "$OUT/"
python3 ~/Git/astra68/tools/fs-baseline-compare.py "$OUT/fs-base1.json" "$OUT/fs-base2.json" | tee "$OUT/run-to-run.txt"
```

Expected: three records, each with `"failed": false` in every workload. The
run-to-run comparison prints the noise floor; record it, whether it passes or
not. If two identical releases differ by more than 10%, the budget cannot be
judged from one run, and Task 13 must compare medians of three runs. Write
that down in `run-to-run.txt`.

The `spawn` and application-start numbers read `/commands` from the guest
ext4 system volume, which Phase 1 does not change. They are the Phase 2
baseline, recorded now so Phase 2 has one.

- [ ] **Step 8: Commit**

```bash
git add sw/userspace/commands/fsbench/fsbench.c sw/userspace/commands/Makefile \
  emu/qemu/fs-baseline.py emu/qemu/create-de25-release.sh \
  emu/qemu/test-create-de25-release.py tools/fs-baseline-compare.py
git commit -m "test(fs): fsbench and the DE25 filesystem baseline driver"
```

---

### Task 2: Gate infrastructure and the semantic suite skeleton (D11, D12)

The filesystem gates run QEMU the way the board does: as root, with the
provider root on a Linux-mounted ext4 volume. `beast` already grants `barry`
passwordless sudo. The wrapper is therefore a posture, not a security
boundary: one narrow command that the gates use and the sudoers file names.

**Files:**
- Create: `tools/astra-qemu-root`
- Modify: `emu/qemu/qemu_runtime.py` (`root_provider`)
- Modify: `emu/qemu/test-terminal.py` (`Machine(..., launcher=())`)
- Create: `sw/userspace/commands/fssem/fssem.c`
- Modify: `sw/userspace/commands/Makefile` (`fssem` in `TEST_COMMANDS`, `fssem_LIBRARIES`)
- Create: `emu/qemu/test-fs-semantics.py`
- Create: `emu/qemu/fs-gates.sh`
- Modify: `AGENTS.md` (the root gate posture)
- On beast (not in git): `/usr/local/sbin/astra-qemu-root`,
  `/etc/sudoers.d/astra-qemu-root`, one line appended to `~/astra-mg/suite.sh`

**Interfaces:**
- Produces: `sudo -n /usr/local/sbin/astra-qemu-root --image IMG --mount DIR
  [--env TZ=VALUE] -- QEMU ARGS...`
- Produces: `qemu_runtime.root_provider(work, size_mib=256) -> (launcher: list[str], mount: str, image: str)`
- Produces: `terminal.Machine(qemu, rom, image, run, launcher=..., hostfs_root=mount)`
- Produces: `fssem CASE...` printing `FSSEM PASS|FAIL <case>` and
  `FSSEM DONE passed P failed F`. Every later task adds cases to the
  `cases[]` table and to `CASES` in the gate.
- Produces: `test-fs-semantics.py QEMU ROM IMAGE [--case C]... [--check K]... [--bench LABEL OUT]`
  with a `CHECKS` table that later tasks extend.
- Produces: `emu/qemu/fs-gates.sh QEMU ROM IMAGE`, printing `== name` and the
  tail of each gate's log with `EXIT=n`.

- [ ] **Step 1: Write `tools/astra-qemu-root`**

```bash
#!/bin/bash
# astra-qemu-root -- run the built Astra QEMU as root against a
# Linux-mounted ext4 provider root: the posture astra.service has on the
# board (docs/FILESYSTEM_PRODUCTION_PRD.md FS11).
#
# usage: sudo -n astra-qemu-root --image IMAGE --mount DIR
#            [--env TZ=VALUE] -- QEMU ARGS...
#
# Refuses anything but:
#   IMAGE  a regular file in a caller-owned 0700 directory directly under /tmp;
#   DIR    an empty caller-owned directory in that same directory;
#   QEMU   ~caller/.cache/astra68/qemu-9.2.4/build-host-*/qemu-system-m68k;
#   ARGS   -M -m -bios -display -monitor -serial -no-reboot -qmp -drive
#          -nodefaults -S, every path in a caller-owned 0700 /tmp directory
#          or inside ~caller/Git/astra68.
# IMAGE is loop-mounted on DIR (nodev,nosuid) in the caller's mount
# namespace, so a gate can read the host copy of the syslog. QEMU then runs
# in a private mount namespace with umask 000, so the files it creates are
# readable by the gate and the QMP socket accepts it. DIR is unmounted when
# QEMU exits; a failed unmount is an error, never a lazy detach.
set -euo pipefail

die() { echo "astra-qemu-root: $*" >&2; exit 64; }

[ "$(id -u)" -eq 0 ] || die "run through sudo"
[ -n "${SUDO_UID:-}" ] && [ -n "${SUDO_GID:-}" ] && [ -n "${SUDO_USER:-}" ] ||
    die "run through sudo"
CALLER_HOME=$(getent passwd "$SUDO_USER" | cut -d: -f6)

private_tmp_dir() {
    local dir
    dir=$(realpath -e -- "$1") || return 1
    case "$dir" in
        /tmp/*/*) return 1 ;;
        /tmp/?*) ;;
        *) return 1 ;;
    esac
    [ -d "$dir" ] && [ "$(stat -c %u "$dir")" = "$SUDO_UID" ] &&
        [ "$(stat -c %a "$dir")" = 700 ]
}

allowed_path() {
    local path=$1 real
    if private_tmp_dir "$(dirname -- "$path")"; then
        return 0
    fi
    real=$(realpath -e -- "$path") || return 1
    case "$real" in
        "$CALLER_HOME/Git/astra68/"*) return 0 ;;
    esac
    return 1
}

IMAGE= MOUNT=
ENVS=()
while [ "$#" -gt 0 ]; do
    case "$1" in
        --image) IMAGE=$2; shift 2 ;;
        --mount) MOUNT=$2; shift 2 ;;
        --env)
            case "$2" in
                TZ=*) ENVS+=("$2") ;;
                *) die "environment $2 not allowed" ;;
            esac
            shift 2 ;;
        --) shift; break ;;
        *) die "unknown option $1" ;;
    esac
done
[ -n "$IMAGE" ] && [ -n "$MOUNT" ] && [ "$#" -ge 1 ] || die "usage"
[ -f "$IMAGE" ] && private_tmp_dir "$(dirname -- "$IMAGE")" ||
    die "image must be a file in a private /tmp directory"
[ "$(stat -c %u "$IMAGE")" = "$SUDO_UID" ] || die "image not the caller's"
[ "$(realpath -e -- "$(dirname -- "$MOUNT")")" = \
  "$(realpath -e -- "$(dirname -- "$IMAGE")")" ] ||
    die "mount point must sit beside the image"
[ -d "$MOUNT" ] && [ "$(stat -c %u "$MOUNT")" = "$SUDO_UID" ] &&
    [ -z "$(ls -A -- "$MOUNT")" ] || die "mount point must be empty"

QEMU=$(realpath -e -- "$1") || die "no QEMU"
shift
case "$QEMU" in
    "$CALLER_HOME"/.cache/astra68/qemu-9.2.4/build-host-*/qemu-system-m68k) ;;
    *) die "QEMU must be a beast host build" ;;
esac
[ "$(stat -c %u "$QEMU")" = "$SUDO_UID" ] || die "QEMU not the caller's"

ARGS=()
while [ "$#" -gt 0 ]; do
    option=$1
    case "$option" in
        -no-reboot|-nodefaults|-S)
            ARGS+=("$option"); shift; continue ;;
        -M|-m|-bios|-display|-monitor|-serial|-qmp|-drive)
            [ "$#" -ge 2 ] || die "$option needs a value"
            value=$2 ;;
        *) die "option $option not allowed" ;;
    esac
    case "$option" in
        -M) case "$value" in astra68|astra68,accel=qtest) ;; *) die "machine $value" ;; esac ;;
        -m) [[ "$value" =~ ^[0-9]+[MG]$ ]] || die "memory $value" ;;
        -bios) allowed_path "$value" || die "ROM path $value" ;;
        -display|-monitor) [ "$value" = none ] || die "$option $value" ;;
        -serial) [ "$value" = stdio ] || die "serial $value" ;;
        -qmp)
            [[ "$value" =~ ^unix:([^,]+),server=on,wait=off$ ]] || die "qmp $value"
            allowed_path "${BASH_REMATCH[1]}" || die "qmp path ${BASH_REMATCH[1]}" ;;
        -drive)
            [[ "$value" =~ ^if=none,format=raw,file=([^,]+)$ ]] || die "drive $value"
            allowed_path "${BASH_REMATCH[1]}" || die "drive path ${BASH_REMATCH[1]}" ;;
    esac
    ARGS+=("$option" "$value")
    shift 2
done

mount -o loop,nodev,nosuid -- "$IMAGE" "$MOUNT"
cleanup() {
    if ! umount -- "$MOUNT"; then
        echo "astra-qemu-root: cannot unmount $MOUNT" >&2
        exit 70
    fi
}
trap cleanup EXIT
# QEMU creates the volume directories 0700 as root (astra68.c,
# astra68_init); creating them first keeps them the caller's.
install -d -o "$SUDO_UID" -g "$SUDO_GID" -m 0755 "$MOUNT/work" "$MOUNT/sound"

umask 000
env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin ASTRA_HOSTFS_ROOT="$MOUNT" \
    "${ENVS[@]}" unshare --mount --propagation private -- "$QEMU" "${ARGS[@]}" &
child=$!
trap 'kill -TERM "$child" 2>/dev/null || true' TERM INT HUP
set +e
wait "$child"
status=$?
while kill -0 "$child" 2>/dev/null; do
    wait "$child"
    status=$?
done
exit "$status"
```

- [ ] **Step 2: Install it on beast**

```sh
sudo install -o root -g root -m 0755 ~/Git/astra68/tools/astra-qemu-root /usr/local/sbin/astra-qemu-root
echo 'barry ALL=(root) NOPASSWD: /usr/local/sbin/astra-qemu-root' | sudo tee /etc/sudoers.d/astra-qemu-root
sudo chmod 0440 /etc/sudoers.d/astra-qemu-root
sudo visudo -cf /etc/sudoers.d/astra-qemu-root
```

Expected: `parsed OK`. The installed copy is reinstalled by this same
command whenever `tools/astra-qemu-root` changes; the gate compares the two
(Step 5) and fails on a stale copy.

Prove the refusals before anything uses it:

```sh
sudo -n /usr/local/sbin/astra-qemu-root --image /etc/passwd --mount /tmp -- /bin/true; echo "EXIT=$?"
```

Expected: `astra-qemu-root: image must be a file in a private /tmp directory`
and `EXIT=64`.

- [ ] **Step 3: The gate helper and the launcher prefix**

Append to `emu/qemu/qemu_runtime.py`:

```python
import subprocess

ROOT_WRAPPER = "/usr/local/sbin/astra-qemu-root"
ROOT_WRAPPER_SOURCE = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools",
    "astra-qemu-root")


def root_provider(work, size_mib=256):
    """A fresh ext4 provider image and its empty mount point in `work`.

    `work` must be a caller-owned 0700 directory directly under /tmp (what
    tempfile.mkdtemp(dir="/tmp") makes). Returns (launcher prefix, mount
    point, image): the prefix runs QEMU as root through astra-qemu-root,
    the board's posture (FS11). Explicit features keep the image mountable
    by the board's e2fsprogs 1.46 (.superpowers/sdd/fs-linux-audit.md G15).
    """
    with open(ROOT_WRAPPER, "rb") as installed, \
            open(ROOT_WRAPPER_SOURCE, "rb") as source:
        if installed.read() != source.read():
            raise RuntimeError("%s is stale; reinstall tools/astra-qemu-root"
                               % ROOT_WRAPPER)
    image = os.path.join(work, "provider.img")
    mount = os.path.join(work, "provider")
    os.mkdir(mount)
    with open(image, "wb") as handle:
        handle.truncate(size_mib * 1024 * 1024)
    subprocess.run(["/usr/sbin/mkfs.ext4", "-q", "-F", "-O", "^orphan_file",
                    "-E", "root_owner=%d:%d" % (os.getuid(), os.getgid()),
                    image], check=True)
    return (["sudo", "-n", ROOT_WRAPPER, "--image", image, "--mount", mount,
             "--"], mount, image)
```

In `emu/qemu/test-terminal.py`, change `Machine.__init__`
(`test-terminal.py:722-748`):

- add the parameter `launcher=()` after `debug_guest=False`;
- build the command as `command = list(launcher) + ([qemu, "-M", ...` and
  leave the rest of the list unchanged;
- set the environment as
  `environment = qemu_environment(qemu, hostfs_root=None if launcher else hostfs_root)`:
  the wrapper owns `ASTRA_HOSTFS_ROOT`; the reader still reads `hostfs_root`.

- [ ] **Step 4: Write the suite skeleton `sw/userspace/commands/fssem/fssem.c`**

```c
/*
 * fssem -- the filesystem semantic suite (docs/FILESYSTEM_PRODUCTION_PRD.md,
 * Phase 1 exit evidence).
 *
 *   fssem CASE...
 *
 * Each case runs in a fresh directory /work/fssem-<pid>-<case> and prints
 * "FSSEM PASS <case>" or "FSSEM FAIL <case>: <detail>". The last line is
 * "FSSEM DONE passed P failed F"; the exit status is 0 only when F is 0.
 * Cases use the POSIX layer unless their name says otherwise; the raw-*
 * cases speak the storage protocol on a port directly.
 */

#define _POSIX_C_SOURCE 200809L

#include <astra/program.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

ASTRA_PROGRAM("fssem", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

typedef int (*FssemCase)(const char *directory);

static char failure[192];

static int fail(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    (void)vsnprintf(failure, sizeof(failure), format, arguments);
    va_end(arguments);
    return -1;
}

#define CHECK(condition, ...)                                              \
    do {                                                                   \
        if (!(condition))                                                  \
            return fail(__VA_ARGS__);                                      \
    } while (0)

static void join(char *out, size_t capacity, const char *directory,
                 const char *leaf)
{
    (void)snprintf(out, capacity, "%s/%s", directory, leaf);
}

static int write_text(const char *path, const char *text, int flags)
{
    size_t length = strlen(text);
    int fd = open(path, flags, 0644);
    ssize_t moved;

    if (fd < 0)
        return -1;
    moved = write(fd, text, length);
    if (close(fd) != 0 || moved != (ssize_t)length)
        return -1;
    return 0;
}

static ssize_t read_text(const char *path, char *out, size_t capacity)
{
    int fd = open(path, O_RDONLY);
    ssize_t moved;

    if (fd < 0)
        return -1;
    moved = read(fd, out, capacity - 1u);
    (void)close(fd);
    if (moved >= 0)
        out[moved] = '\0';
    return moved;
}

/* Create, read back, rename, unlink: the floor every later case stands on. */
static int case_basic(const char *directory)
{
    char a[160];
    char b[160];
    char text[32];
    struct stat st;

    join(a, sizeof(a), directory, "a");
    join(b, sizeof(b), directory, "b");
    CHECK(write_text(a, "basic", O_WRONLY | O_CREAT | O_EXCL) == 0,
          "create %s: %s", a, strerror(errno));
    CHECK(read_text(a, text, sizeof(text)) == 5 &&
          strcmp(text, "basic") == 0, "read back %s", a);
    CHECK(rename(a, b) == 0, "rename: %s", strerror(errno));
    CHECK(stat(a, &st) != 0 && errno == ENOENT, "old name survived rename");
    CHECK(stat(b, &st) == 0 && st.st_size == 5, "new name: %s",
          strerror(errno));
    CHECK(unlink(b) == 0, "unlink: %s", strerror(errno));
    return 0;
}

static const struct {
    const char *name;
    FssemCase run;
} cases[] = {
    {"basic", case_basic},
};

int main(int argc, char **argv)
{
    uint32_t passed = 0u;
    uint32_t failed = 0u;

    if (argc < 2) {
        fprintf(stderr, "usage: fssem CASE...\n");
        return 2;
    }
    for (int index = 1; index < argc; ++index) {
        char directory[128];
        size_t at = 0u;

        while (at < sizeof(cases) / sizeof(cases[0]) &&
               strcmp(cases[at].name, argv[index]) != 0)
            ++at;
        if (at == sizeof(cases) / sizeof(cases[0])) {
            printf("FSSEM FAIL %s: no such case\n", argv[index]);
            ++failed;
            continue;
        }
        (void)snprintf(directory, sizeof(directory), "/work/fssem-%ld-%s",
                       (long)getpid(), cases[at].name);
        failure[0] = '\0';
        if (mkdir(directory, 0755) != 0) {
            printf("FSSEM FAIL %s: mkdir %s: %s\n", cases[at].name,
                   directory, strerror(errno));
            ++failed;
            continue;
        }
        if (cases[at].run(directory) == 0) {
            printf("FSSEM PASS %s\n", cases[at].name);
            ++passed;
        } else {
            printf("FSSEM FAIL %s: %s\n", cases[at].name, failure);
            ++failed;
        }
        fflush(stdout);
    }
    printf("FSSEM DONE passed %u failed %u\n", (unsigned)passed,
           (unsigned)failed);
    return failed == 0u ? 0 : 1;
}
```

In `sw/userspace/commands/Makefile` append `fssem` to `TEST_COMMANDS`
and declare:

```make
fssem_LIBRARIES := $(LIBC_LIBRARY) $(RUNTIME_LIBRARY) $(FILESYSTEM_LIBRARY)
```

(The filesystem library is used from Task 5 on, by the native and raw cases.
It is declared now so the executable contract does not change under a later
task.)

- [ ] **Step 5: Write the gate `emu/qemu/test-fs-semantics.py`**

```python
#!/usr/bin/env python3
"""Filesystem semantic gate (docs/FILESYSTEM_PRODUCTION_PRD.md, Phase 1).

usage: test-fs-semantics.py QEMU ROM IMAGE [--case NAME]... [--check NAME]...
                            [--bench LABEL OUT.json]

Boots a copy of IMAGE with `fssem` installed. The provider root is a
Linux-mounted ext4 image, and QEMU runs as root through astra-qemu-root,
the board's posture (FS11). It opens a terminal the way a person does and
runs `fssem` over the selected cases (all of CASES by default). Then it runs
the selected orchestrated CHECKS (all by default). After the machine stops,
it runs `e2fsck -fn` on the provider image.

--bench runs `fsbench --label LABEL` instead and copies its result to
OUT.json: the QEMU-on-beast record that per-task comparisons use.
"""

import argparse
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import astra_image  # noqa: E402
import qemu_runtime  # noqa: E402


def load(name, file):
    spec = importlib.util.spec_from_file_location(
        name, os.path.join(HERE, file))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


terminal = load("astra_terminal_gate", "test-terminal.py")

# Every fssem case the suite runs. Later tasks append here and in fssem.c.
CASES = ["basic"]
# Orchestrated checks: name -> function(machine, provider_mount). Later tasks
# add entries; each prints "PASS <name>" or calls fail().
CHECKS = {}


def fail(text):
    print("FAIL %s" % text)
    sys.exit(1)


def command(machine, text, timeout, number):
    """Types `text` and returns (lines printed, exit status)."""
    marker = "FSSEM-GATE-%u-" % number
    machine.settle()
    before = machine.sequence()
    machine.qmp.type_line(text + "; print -r -- " + marker + "$?")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        lines, _ = machine.said(before)
        for line in lines:
            if line.startswith(marker) and line[len(marker):].isdigit():
                return lines, int(line[len(marker):])
        time.sleep(0.1)
    fail("%r never finished: %r" % (text, machine.said(before)[0][-20:]))


def run_cases(machine, cases):
    lines, status = command(machine, "fssem " + " ".join(cases), 600, 1)
    done = [line for line in lines if line.startswith("FSSEM DONE")]
    for line in lines:
        if line.startswith("FSSEM "):
            print(line)
    if status != 0 or done != ["FSSEM DONE passed %d failed 0" % len(cases)]:
        fail("fssem: status %d, %r" % (status, done))
    print("PASS fssem %s" % " ".join(cases))


def run_bench(machine, mount, label, output):
    lines, status = command(machine, "fsbench --label %s" % label, 3600, 2)
    if status != 0 or "fsbench: done %s" % label not in lines:
        fail("fsbench: status %d, %r" % (status, lines[-10:]))
    with open(os.path.join(mount, "work", ".astra",
                           "fsbench-%s.json" % label)) as source:
        guest = json.load(source)
    commit = subprocess.run(["git", "-C", ROOT, "rev-parse", "HEAD"],
                            capture_output=True, text=True).stdout.strip()
    with open(output, "w") as out:
        json.dump({"identity": {"host": "beast-qemu", "commit": commit},
                   "guest": guest}, out, indent=1, sort_keys=True)
    print("PASS bench %s -> %s" % (label, output))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("--case", action="append", choices=CASES)
    parser.add_argument("--check", action="append", choices=sorted(CHECKS))
    parser.add_argument("--bench", nargs=2, metavar=("LABEL", "OUT"))
    arguments = parser.parse_args()
    work = tempfile.mkdtemp(prefix="astra-fs-sem-", dir="/tmp")
    try:
        image = os.path.join(work, "card.img")
        shutil.copyfile(arguments.image, image)
        astra_image.install_test_command(image, "fssem")
        launcher, mount, provider = qemu_runtime.root_provider(work)
        run = tempfile.mkdtemp(prefix="astra-fs-run-", dir="/tmp")
        machine = terminal.Machine(arguments.qemu, arguments.rom, image, run,
                                   launcher=launcher, hostfs_root=mount)
        try:
            if not terminal.open_terminal(machine, 120, 60):
                fail("no terminal")
            if arguments.bench:
                run_bench(machine, mount, *arguments.bench)
            else:
                run_cases(machine, arguments.case or CASES)
                for name in arguments.check or sorted(CHECKS):
                    CHECKS[name](machine, mount)
        finally:
            machine.close()
            shutil.rmtree(run, ignore_errors=True)
        checked = subprocess.run(["/usr/sbin/e2fsck", "-fn", provider],
                                 capture_output=True, text=True)
        if checked.returncode != 0:
            fail("provider e2fsck: %s" % checked.stdout[-400:])
        print("PASS provider e2fsck")
        print("ASTRA FS SEMANTICS PASS")
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
```

- [ ] **Step 6: Write `emu/qemu/fs-gates.sh`**

```sh
#!/bin/sh
# The filesystem gates (plan docs/superpowers/plans/2026-10-07-filesystem-phase1.md).
# usage: fs-gates.sh QEMU ROM IMAGE   -- run from the repository root on beast
set -u
Q=$1 R=$2 I=$3
python3 emu/qemu/test-hostfs.py "$Q" > /tmp/mg-fs-hostfs.log 2>&1
echo "EXIT=$?" >> /tmp/mg-fs-hostfs.log
python3 emu/qemu/test-fs-semantics.py "$Q" "$R" "$I" > /tmp/mg-fs-semantics.log 2>&1
echo "EXIT=$?" >> /tmp/mg-fs-semantics.log
rm -rf /tmp/astra-fs-stress /tmp/astra-fs-stress-host
python3 emu/qemu/test-filesystem-stress.py "$Q" "$R" "$I" \
    --work /tmp/astra-fs-stress --host-work /tmp/astra-fs-stress-host \
    --operations 400 > /tmp/mg-fs-stress.log 2>&1
echo "EXIT=$?" >> /tmp/mg-fs-stress.log
for f in fs-hostfs fs-semantics fs-stress; do
    echo "== $f"
    tail -4 /tmp/mg-$f.log
done
```

`test-filesystem-stress.py` copies its own image into `--work`
(`test-filesystem-stress.py:344-383`); it keeps its unprivileged host
directory until Phase 2 moves the system volume.

On beast, wire it into the suite once (the scripts in `~/astra-mg` are not
in git):

```sh
grep -q fs-gates.sh ~/astra-mg/suite.sh || sed -i '/^echo SUITE-DONE/i bash emu/qemu/fs-gates.sh "$Q" "$R" "$I/base.img" 2>\&1 | grep -E "^==|PASS|FAIL|EXIT"' ~/astra-mg/suite.sh
grep -n fs-gates ~/astra-mg/suite.sh
```

Expected: one line before `echo SUITE-DONE`.

- [ ] **Step 7: Document the posture in `AGENTS.md`**

Add under "Execution Topology":

```markdown
- Filesystem gates run QEMU as root, as `astra.service` does on the board:
  `tools/astra-qemu-root` (installed as `/usr/local/sbin/astra-qemu-root`,
  allowed by `/etc/sudoers.d/astra-qemu-root`) loop-mounts a fresh ext4
  provider image and runs only a `beast` host build of QEMU, with an
  allowlisted argument shape, in a private mount namespace. Reinstall it
  with `sudo install -o root -g root -m 0755 tools/astra-qemu-root
  /usr/local/sbin/` after any change; gates refuse a stale copy.
```

- [ ] **Step 8: Run it, then perturb it**

Sync, then on beast:

```sh
~/astra-mg/locked make -C sw/userspace -j32
~/astra-mg/locked make -C sw/userspace test
~/astra-mg/locked make -C sw/boot -j32
Q=$(tail -n1 /tmp/qemu-host-build.log); R=sw/boot/build/astra_boot.bin
~/astra-mg/locked python3 emu/qemu/test-fs-semantics.py $Q $R ~/astra-mg/base.img
```

Expected: `FSSEM PASS basic`, `PASS fssem basic`, `PASS provider e2fsck`,
`ASTRA FS SEMANTICS PASS`. While it runs, a second shell on beast shows the
loop mount (`findmnt -t ext4 | grep astra-fs-sem`) and `ps -o user,cmd -C qemu-system-m68k`
shows `root`.

Perturb: in `case_basic`, replace `strcmp(text, "basic") == 0` with
`strcmp(text, "basix") == 0`. Re-sync, rebuild userspace, rerun. Expected:
`FSSEM FAIL basic: read back /work/fssem-...-basic/a`, then
`FAIL fssem: status 1, ...` and a nonzero exit. Restore the line, re-sync
with the same `--no-times` rsync, rebuild, rerun: PASS again.

Perturb the wrapper: run
`sudo -n /usr/local/sbin/astra-qemu-root --image /tmp/x --mount /tmp/y -- $Q -plugin x`.
Expected: refusal, `EXIT=64`.

- [ ] **Step 9: The QEMU baseline on beast, then the full suite**

```sh
SHA=$(git rev-parse --short HEAD)
OUT=/mnt/Documents/astra68/builds/fs-phase1-baseline-$(date +%Y%m%d)-$SHA
mkdir -p "$OUT"
for run in 1 2 3; do
  ~/astra-mg/locked python3 emu/qemu/test-fs-semantics.py $Q $R ~/astra-mg/base.img --bench qbase$run "$OUT/qemu-base$run.json"
done
python3 tools/fs-baseline-compare.py "$OUT/qemu-base1.json" "$OUT/qemu-base2.json" | tee "$OUT/qemu-run-to-run.txt"
~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-task2.log
```

Expected: three QEMU records, and a green t9 log that now includes
`== fs-hostfs`, `== fs-semantics` and `== fs-stress`, each with `EXIT=0`.
This tree changes no data path, so the QEMU record has the same source as
the DE25 record of Task 1. Every later task compares against it.

- [ ] **Step 10: Commit**

```bash
git add tools/astra-qemu-root emu/qemu/qemu_runtime.py emu/qemu/test-terminal.py \
  sw/userspace/commands/fssem/fssem.c sw/userspace/commands/Makefile \
  emu/qemu/test-fs-semantics.py emu/qemu/fs-gates.sh AGENTS.md
git commit -m "test(fs): root-posture filesystem gates and the fssem suite"
```

---

### Task 3: Close the FS44 bypass (D1)

**Status: done.** The work is `62b72fc2`: the relay, storage protocol 27,
Kit 5 and HostFS workers. `6d4e6a2c` added the docs and made the
host-channel gate read the system log. The review was deferred, and Task
3b's review replaces it, because Task 3b deletes the relay data path this
task built. The steps below record what was done. Do not run them again.
The measured hop (2.55x stat-warm, 2.62x rand-read, median) is in
`/mnt/Documents/astra68/builds/fs-phase1-task3-60f2d30a/`, and Task 3b
compares against it.

Today HELLO duplicates the host-device capability to every client
(`sw/userspace/vfs/src/vfs_port_transport.c:2332-2342`). The client then
drives QEMU itself with `WORK` hard-coded
(`sw/userspace/vfs/src/vfs_host_direct.c:46-88`), and QEMU trusts the
command's `volume` (`astra68.c:4072-4093`). This task deletes that path.
From here on, a filesystem request for a host volume reaches QEMU only
through HostFS. HostFS gains worker threads, so a slow Linux miss holds one
worker, not the whole service. The model is the storage service
(`sw/userspace/services/storage/main.c:277-315`, `:444-459`).

**Files:**
- Delete: `sw/userspace/vfs/src/vfs_host_direct.c`, `sw/userspace/vfs/include/astra/vfs_host_direct.h`
- Modify: `sw/userspace/vfs/Makefile` (drop `vfs_host_direct` from `HEADERS`, the archive and library object lists, and its rule: `grep -n vfs_host_direct sw/userspace/vfs/Makefile` lists lines 47, 63, 73, 76, 91, 167)
- Modify: `ndk/include/astra/vfs_client.h`, `sw/userspace/vfs/src/vfs_client.c`
- Modify: `sw/userspace/vfs/src/vfs_port_transport.c`, `sw/userspace/vfs/include/astra/vfs_port_transport.h`
- Modify: `sw/userspace/vfs/src/vfs_process.c`
- Modify: `sw/include/astra/vfs_service.h` (version 27)
- Modify: `sw/userspace/services/hostfs/main.c` (no accelerator; worker threads; settings)
- Modify: `sw/userspace/supervisor/src/loader.c`, `sw/userspace/supervisor/src/vfs_host.c`
- Modify: `sw/userspace/services/hostbench/main.c`
- Modify: Filesystem Kit major 4 -> 5: `ndk/include/astra/filesystem_library.h`, `sw/userspace/vfs/src/filesystem_library_identity.c`, and every `filesystem.library.4` reference (list in Step 7)
- Modify: `emu/qemu/astra_image.py` (hostfs grants `CONFIG:r`; `services/hostfs/settings.conf`)
- Test: `sw/userspace/vfs/tests/test_vfs_port.c`, `sw/userspace/vfs/tests/test_vfs_process.c`

**Interfaces:**
- Removes: `AstraVfsPortAcceleratorOps`, `astra_vfs_port_connect_with_accelerator`,
  `astra_vfs_port_connect_lazy_with_accelerator`, `astra_vfs_port_service_set_accelerator`,
  `astra_vfs_host_port_connect`, `astra_vfs_host_port_connect_lazy`, every
  `astra_vfs_host_direct_*`, and the `AstraVfsClient` fields `port_direct_*`,
  `direct_backend_*` and `port_accelerator_ops`. The `AstraVfsPortLane` fields
  `direct_area`, `direct_address` and `direct_size` go too.
- Keeps: `astra_vfs_port_connect(client, service)` (eager) and
  `astra_vfs_client_connect_service(client, service)` (lazy, HELLO fused with
  the first operation).
- Produces: `ASTRA_VFS_VERSION` 27 ("HELLO never carries a capability");
  `services/hostfs/settings.conf` with `workers N` (1 to
  `ASTRA_PROCESS_THREAD_SLOT_COUNT - 1`). Task 11 adds the admission limits
  to this file.

- [ ] **Step 1: Write the failing test**

In `sw/userspace/vfs/tests/test_vfs_port.c`, add after
`test_a_message_that_is_not_the_protocol_is_refused`:

```c
/*
 * Version 27: a HELLO answers with a session and nothing else. The host
 * device capability that version 18 attached let any client drive QEMU
 * itself (docs/FILESYSTEM_PRODUCTION_PRD.md FS44).
 */
static void
test_hello_attaches_no_capability(void)
{
    AstraVfsPortService host;
    AstraVfsRequestMessage message;
    AstraVfsReplyMessage reply;
    uint32_t service_handle;
    uint32_t reply_handle;
    uint32_t handles[1] = {0u};
    uint32_t size = 0u;
    uint32_t count = 0u;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    reply_handle = mock_open(1u);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    memset(&message, 0, sizeof(message));
    astra_message_header_set(&message.header, (uint32_t)sizeof(message),
                             ASTRA_VFS_PROTOCOL, ASTRA_VFS_VERSION,
                             ASTRA_VFS_OP_HELLO, 1u);
    message.request.size = ASTRA_VFS_REQUEST_SIZE;
    message.request.version = ASTRA_VFS_VERSION;
    handles[0] = reply_handle;
    assert(astra_port_send(service_handle, &message, sizeof(message),
                           handles, 1u) == ASTRA_SYSCALL_OK);
    assert(astra_vfs_port_service_pump(&host, 1u) == 1u);
    assert(astra_port_receive(reply_handle, &reply, sizeof(reply), handles,
                              1u, &size, &count) == ASTRA_SYSCALL_OK);
    assert(size == sizeof(reply));
    assert(reply.reply.status == ASTRA_VFS_OK);
    assert(reply.reply.version == UINT16_C(27));
    assert(reply.reply.session != ASTRA_VFS_SESSION_INVALID);
    assert(count == 0u);
}
```

Call it from `main` after `test_a_message_that_is_not_the_protocol_is_refused();`.

Delete the accelerator tests and fixtures they own:
`test_host_accelerator_moves_data_plane_out_of_the_service`,
`test_shared_accelerated_client_keeps_thread_requests_in_flight`,
`test_direct_borrowed_read_reuses_only_an_undisturbed_area`,
`test_accelerated_bulk_append_returns_the_atomic_backend_position`, their
calls in `main`, the `direct_*` counters (`test_vfs_port.c:75-90`), the
`mock_accelerator_ops` table, the `astra_vfs_host_direct_*` stubs
(`:216-260`) and `direct_read_thread`. `grep -n "accelerat\|direct_\|host_direct" sw/userspace/vfs/tests/test_vfs_port.c`
must print nothing afterwards.

In `sw/userspace/vfs/tests/test_vfs_process.c`, delete the stubs
`astra_vfs_host_port_connect_lazy`, `astra_vfs_host_direct_resume` and
`astra_vfs_host_direct_after_fork` (`:225-260`) and replace them with a stub
of `astra_vfs_client_connect_service` that behaves the way the deleted
`astra_vfs_host_port_connect_lazy` stub did. Delete every assertion on
`port_direct_*`.

- [ ] **Step 2: Run it to see it fail**

On beast: `~/astra-mg/locked make -C sw/userspace/vfs build/host/test_vfs_port && sw/userspace/vfs/build/host/test_vfs_port`
Expected: assertion failure on `reply.reply.version == UINT16_C(27)` (the
service still answers 26).

- [ ] **Step 3: Delete the direct path**

1. Delete `sw/userspace/vfs/src/vfs_host_direct.c` and
   `sw/userspace/vfs/include/astra/vfs_host_direct.h`, and their lines in
   `sw/userspace/vfs/Makefile`.

2. `ndk/include/astra/vfs_client.h`: delete `AstraVfsPortAcceleratorOps`
   (`:110-126`); the `AstraVfsPortLane` fields `direct_area`,
   `direct_address`, `direct_size` (`:105-107`); and from `AstraVfsClient` the
   fields `port_direct_detached`, `port_direct_address`, `port_direct_area`,
   `port_direct_device`, `port_direct_session`, `port_direct_lock`,
   `direct_backend_ops`, `direct_backend_context`, `direct_backend_enter`,
   `direct_backend_leave`, `port_accelerator_ops` (`:145-163`).

3. `sw/userspace/vfs/src/vfs_client.c`: delete `backend_enter` and
   `backend_leave` (`:15-40`), the four assignments in `astra_vfs_connect`
   (`:172-175`), and every `if (client->direct_backend_ops != NULL) { ... }`
   block. Each block is a complete local fast path that returns before the
   wire path below it, so deleting it leaves the wire path as the only path.
   List them with `grep -n "direct_backend_ops != NULL" sw/userspace/vfs/src/vfs_client.c`:
   lines 255, 352, 383, 432, 485, 510, 541, 597, 649, 698, 905, 934, 968
   today. For example, in `astra_vfs_open_at_mode` the block starting
   `if (client->direct_backend_ops != NULL) {` at line 255 and ending at its
   matching `}` before `call = begin(client);` goes, with the locals it alone
   used (`ops`, `context`, `node`, `info`).

4. `sw/userspace/vfs/src/vfs_port_transport.c`, client side:
   - Replace `astra_vfs_port_connect` and `astra_vfs_port_connect_with_accelerator`
     (`:648-698`) with one function:

     ```c
     uint32_t
     astra_vfs_port_connect(AstraVfsClient *client, uint32_t service)
     {
         uint32_t status;

         if (client == NULL || service == 0u)
             return ASTRA_VFS_ERR_INVALID;
         client->port_service = service;
         memset(client->port_lanes, 0, sizeof(client->port_lanes));
         client->port_lane_lock = 0u;
         client->port_connect_lock = 0u;
         client->port_connecting = 0u;
         client->port_inflight = 0u;
         client->port_inflight_waiters = 0u;
         client->port_lifecycle = PORT_CLIENT_OPEN;
         if (astra_rt_semaphore_create(
                 1u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
                 &client->port_connect_lock) != ASTRA_SYSCALL_OK)
             return ASTRA_VFS_ERR_LIMIT;
         status = astra_vfs_connect(client, astra_vfs_port_transport, client);
         if (status != ASTRA_VFS_OK) {
             port_client_close(client, 0);
         } else {
             client->port_area_capable = 1u;
             client->area_payload = astra_vfs_port_call_area;
             client->call_acquire = astra_vfs_port_call_acquire;
         }
         return status;
     }
     ```

   - Replace `astra_vfs_client_connect_service` and
     `astra_vfs_port_connect_lazy_with_accelerator` (`:700-750`) with:

     ```c
     uint32_t
     astra_vfs_client_connect_service(AstraVfsClient *client, uint32_t service)
     {
         if (client == NULL || service == 0u)
             return ASTRA_VFS_ERR_INVALID;
         client->transport = astra_vfs_port_transport;
         client->context = client;
         atomic_store_explicit(&client->session, ASTRA_VFS_SESSION_INVALID,
                               memory_order_relaxed);
         client->version = ASTRA_VFS_VERSION;
         client->port_area_capable = 1u;
         client->port_service = service;
         memset(client->port_lanes, 0, sizeof(client->port_lanes));
         client->port_lane_lock = 0u;
         client->port_connect_lock = 0u;
         client->port_connecting = 0u;
         client->port_inflight = 0u;
         client->port_inflight_waiters = 0u;
         client->port_lifecycle = PORT_CLIENT_OPEN;
         if (astra_rt_semaphore_create(
                 1u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
                 &client->port_connect_lock) != ASTRA_SYSCALL_OK)
             return ASTRA_VFS_ERR_LIMIT;
         client->area_payload = astra_vfs_port_call_area;
         client->call_acquire = astra_vfs_port_call_acquire;
         return ASTRA_VFS_OK;
     }
     ```

   - Rename `ensure_accelerated_connection` to `ensure_connection`; its body
     is unchanged, because it never touched the accelerator.
   - In `astra_vfs_port_transport`'s reply check (`:1241-1253`), a reply
     that carries a handle is now always a protocol error. Replace the term
     `(reply_handle_count != 0u && (wire_operation != ASTRA_VFS_OP_HELLO || incoming->reply.version < UINT16_C(18)))`
     with `reply_handle_count != 0u`. Delete the block after
     `*reply = incoming->reply;` that hands `reply_handles[0]` to
     `port_accelerator_ops->connect` (`:1268-1274`), and the accelerated
     retry inside the fused-HELLO branch, which is the
     `if (reply_handle_count == 1u) { ... }` at `:1279-1288`.
   - Delete `ensure_direct_area` (`:827-864`) and `direct_accelerated`
     (`:1442-1446`), and the `port_direct_detached` reset in
     `port_client_handles_reset` (`:431`). In `port_client_finish` delete the
     `port_accelerator_ops` block (`:472-477`). In `lane_resources_close`
     delete the `direct_address`/`direct_area` releases (`:177-180`). In
     `astra_vfs_port_call_area` delete the `port_direct_address` branch
     (`:876-880`).
   - Every remaining conditional on `direct_accelerated(client)`,
     `client->port_direct_address`, `client->port_accelerator_ops` or
     `client->direct_backend_ops` keeps only its port branch. For example,
     `vfs_port_transport.c:1595-1611`:

     ```c
     status = direct_accelerated(client) ?
         ensure_direct_area(client, length) :
         ensure_area_size(client, length);
     ```

     becomes `status = ensure_area_size(client, length);`, and
     `length > (direct_accelerated(client) ? lane->direct_size : lane->area_size)`
     becomes `length > lane->area_size`. The `if (client->direct_backend_ops != NULL) { ... }`
     blocks in the bulk read and write paths (`:1579-1594`, `:1697-1703`,
     `:1790-1796`) are deleted whole.

   Service side:
   - Delete `astra_vfs_port_service_set_accelerator` (`:2170-2178`), the
     `host->accelerator = 0u;` initialisation (`:2138`), and the HELLO block
     that duplicates `host->accelerator` (`:2332-2342`) with its local
     `accelerated`.
   - Replace `} else if (fused_hello && accelerated) {` with nothing: the
     fused operation now always runs in the service.
   - Keep `outgoing_handles` and `outgoing_handle_count` as the reply's
     handle transfer, still closed on a failed reply (`:2597-2607`). Task 5's
     `GRANT_DERIVE` uses them.

   `sw/userspace/vfs/include/astra/vfs_port_transport.h`: delete the
   declarations of `astra_vfs_port_connect_with_accelerator`,
   `astra_vfs_port_connect_lazy_with_accelerator` and
   `astra_vfs_port_service_set_accelerator`, and the `accelerator` member of
   `AstraVfsPortService`.

   Completion check: `grep -rn "accelerat\|port_direct\|direct_backend\|host_direct\|host_port_connect" sw/userspace ndk/include --include=*.c --include=*.h | grep -v /build/`
   prints nothing.

5. `sw/userspace/vfs/src/vfs_process.c`:
   - `PROCESS_VFS_EXEC_VERSION` 4 -> 5. Delete the `port_direct_area`,
     `port_direct_device` and `port_direct_session` members of
     `ProcessVfsExecClient`, their export lines (`:148-150`), the three
     import validity terms on them (`:254-258`) and the direct resume
     (`:288-296`).
   - `client_ready` calls `astra_vfs_client_connect_service(&clients[slot].client, clients[slot].handle)`.
   - In `seed_process_vfs` for a fork child, every connected client is
     treated alike:

     ```c
     for (uint32_t index = 0u; index < client_count; ++index) {
         if (clients[index].connected == 0u)
             continue;
         astra_vfs_port_after_fork_child(&clients[index].client);
         astra_vfs_port_abandon(&clients[index].client);
         clients[index].connected = 0u;
     }
     ```

   - Drop `#include <astra/vfs_host_direct.h>`.

6. `sw/include/astra/vfs_service.h`: `ASTRA_VFS_VERSION` 26 -> 27. Append to
   the version history comment: "Version 27 withdraws the version 18
   attachment: HELLO answers with a session and never a capability. Every
   request for a host volume reaches the provider through the service that
   enforces its grant." Delete the sentence on version 18's attachment.

- [ ] **Step 4: Callers**

- `sw/userspace/supervisor/src/loader.c:1229`: `astra_vfs_host_port_connect(client, ...)`
  -> `astra_vfs_port_connect(client, ...)`. Remove `#include <astra/vfs_host_direct.h>` (`:28`).
- `sw/userspace/supervisor/src/vfs_host.c:178`: the same replacement; remove
  its `#include <astra/vfs_host_direct.h>`.
- `sw/userspace/services/hostbench/main.c`: delete `run_layers` and every
  helper only it calls (`transport_loop` through `paired_write_loops`,
  `main.c:294-852`), the statics `shared_transport`, `shared_backend`,
  `shared_direct_client`, `shared_assigns`, `shared_filesystem`,
  `filesystem_client_calls`, `report_layer`, the includes of
  `vfs_host_direct.h`, `vfs_host_backend.h`, `vfs_host_transport.h`,
  `filesystem_library.h` and `vfs_path.h`, and in `astra_main` the
  `run_layers` call with its `fail(bootstrap, 7u)`. hostbench measured
  filesystem layers through the direct path that no longer exists. fsbench
  (Task 1) measures the real path. hostbench keeps its raw channel, adaptive
  polling and memory measurements.

- [ ] **Step 5: HostFS worker threads**

Replace the single-threaded loop in `sw/userspace/services/hostfs/main.c`
(`:385-422`) with workers modelled on `storage_worker`
(`sw/userspace/services/storage/main.c:277-315`). Add, beside the existing
statics:

```c
#include <astra/config_library.h>
#include <astra/vfs_process.h>

/* Every worker owns its request scratch; the service, the port tables and
 * the host transport (one lane per thread) are shared. */
static AstraVfsPortWorker *workers;
static uint32_t *worker_handles;
static AstraThreadStart *worker_starts;
static uint32_t worker_count = 1u;
static uint32_t shutdown_event;
static uint32_t shutdown_receive;
static uint32_t shutdown_reply;
static uint32_t shutdown_transaction;
static volatile uint32_t worker_failed;

/* services/hostfs/settings.conf: `workers N`, 1 to one less than the
 * process thread slots (the initial thread is worker 0). */
static uint32_t configured_workers(const AstraStartupInfo *startup,
                                   uint32_t *count)
{
    AstraConfig config = ASTRA_CONFIG_INIT;
    AstraConfigError error = {0};
    uint64_t value = 0u;
    uint32_t found = 0u;
    uint32_t status;

    if (astra_process_vfs_init(startup) != ASTRA_VFS_OK)
        return ASTRA_STATUS_NOT_FOUND;
    if (astra_config_open(startup, 1u, ASTRA_CONFIG_OPEN_READ, &config,
                          &error) != ASTRA_CONFIG_OK)
        return ASTRA_STATUS_INVALID;
    status = astra_config_count(&config, "workers", &found);
    if (status == ASTRA_CONFIG_OK && found == 1u)
        status = astra_config_get_u64(&config, "workers", 0u, &value);
    astra_config_close(&config);
    if (status != ASTRA_CONFIG_OK || found != 1u || value == 0u ||
        value >= ASTRA_PROCESS_THREAD_SLOT_COUNT)
        return ASTRA_STATUS_INVALID;
    *count = (uint32_t)value;
    return ASTRA_STATUS_OK;
}

/*
 * Worker 0 alone serves METRICS and the shutdown port: the metrics backend
 * refreshes one static snapshot (`host_metrics`) and has no state lock, so
 * it stays single-threaded as it was. WORK and SOUND are served by all.
 */
static void hostfs_worker(uint32_t index)
{
    for (;;) {
        uint32_t handles[5] = {shutdown_event, port.receive,
                               sound_port.receive, metrics_port.receive,
                               shutdown_receive};
        uint32_t ready = ASTRA_WAIT_INDEX_NONE;
        uint32_t status = astra_wait_multiple(
            handles, index == 0u ? 5u : 3u, ASTRA_DEADLINE_FOREVER, &ready,
            NULL);

        if (status == ASTRA_SYSCALL_OK && ready == 0u)
            break;
        if (status == ASTRA_SYSCALL_OK && ready == 4u) {
            AstraShutdownRequest request = {0};

            if (astra_shutdown_receive(shutdown_receive, &request,
                                       &shutdown_reply) !=
                ASTRA_SYSCALL_OK) {
                __atomic_store_n(&worker_failed, 1u, __ATOMIC_RELEASE);
                break;
            }
            shutdown_transaction = request.header.transaction_id;
            shutdown_requested = 1u;
            break;
        }
        if (index == 0u && shutdown_requested != 0u)
            break;
        if (status == ASTRA_SYSCALL_CANCELLED)
            continue;
        if (status != ASTRA_SYSCALL_OK) {
            (void)astra_log_failure("hostfs receive wait", status);
            __atomic_store_n(&worker_failed, 1u, __ATOMIC_RELEASE);
            break;
        }
        if (ready == 1u)
            (void)astra_vfs_port_service_worker_pump(&port, &workers[index],
                                                     1u);
        else if (ready == 2u)
            (void)astra_vfs_port_service_worker_pump(&sound_port,
                                                     &workers[index], 1u);
        else if (ready == 3u)
            (void)astra_vfs_port_service_worker_pump(&metrics_port,
                                                     &workers[index], 1u);
    }
    if (index != 0u)
        astra_thread_exit(ASTRA_STATUS_OK);
}
```

`finish_hostfs` starts by stopping the other workers:

```c
    if (astra_rt_signal(shutdown_event, 1u, NULL) != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_IO;
    for (uint32_t index = 1u; index < worker_count; ++index) {
        if (astra_wait_one(worker_handles[index - 1u], ASTRA_DEADLINE_FOREVER,
                           NULL) != ASTRA_SYSCALL_OK)
            return ASTRA_STATUS_IO;
        (void)astra_close(worker_handles[index - 1u]);
    }
    if (__atomic_load_n(&worker_failed, __ATOMIC_ACQUIRE) != 0u)
        return ASTRA_STATUS_IO;
```

and then drains the three ports with `workers[0]` instead of the deleted
`worker`, `metrics_worker` and `sound_worker` statics. In `astra_main`:

- delete `!astra_vfs_port_service_set_accelerator(&port, device_handle)`;
- before creating the ports, read `configured_workers(startup, &worker_count)`
  (failure -> `ASTRA_STATUS_INVALID` reported through `astra_service_ready_managed`),
  create `shutdown_event` with
  `astra_rt_event_create(ASTRA_EVENT_MANUAL_RESET, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL, &shutdown_event)`,
  and allocate `workers`, `worker_handles` and `worker_starts` with
  `astra_runtime_allocate` (zero them; `worker_count` entries,
  `worker_count - 1` for the thread arrays);
- after the ports and before `astra_service_ready_managed`, start
  workers 1 to `worker_count - 1` exactly as storage does
  (`storage/main.c:437-459`: `astra_process_info` for `default_priority`,
  then `astra_rt_thread_create(&worker_starts[i - 1], info.default_priority,
  ASTRA_RIGHT_READ | ASTRA_RIGHT_WAIT, &worker_handles[i - 1], NULL)`);
- replace the `for (;;)` loop with `hostfs_worker(0u);`, then
  `status = finish_hostfs();`, then the shutdown reply
  (`astra_shutdown_respond(shutdown_reply, shutdown_transaction, ...)` when
  `shutdown_reply != 0u`), then `return (int)status;`.

`sw/userspace/services/hostfs/Makefile`: add the config library to the link
the way `sw/userspace/services/ramfs/Makefile` does
(`grep -n config sw/userspace/services/ramfs/Makefile`).

In `emu/qemu/astra_image.py`:
- the hostfs manifest line becomes
  `"service /services/hostfs grants HOST_DEVICE CONFIG:r "`
  `"serves WORK:rw METRICS:r SOUND:rw required priority=20\n"`;
- add to `CONFIGURATION`:

  ```python
      "services/hostfs/settings.conf": (
          "astra-config 1\n"
          "schema 1\n"
          "workers 4\n"),
  ```

- update `emu/qemu/test-astra-image-providers.py` wherever it asserts the
  manifest text (`grep -n "services/hostfs" emu/qemu/test-astra-image-providers.py`).

- [ ] **Step 6: Run the host tests**

On beast: `~/astra-mg/locked make -C sw/userspace/vfs test`
Expected: `ASTRA VFS PORT PASS` and every other vfs test PASS.

- [ ] **Step 7: Filesystem Kit major 5**

The public `AstraVfsClient` and `AstraVfsPortLane` layouts changed, so the
Kit's ABI major changes:

- `ndk/include/astra/filesystem_library.h`: `ASTRA_FILESYSTEM_LIBRARY_VERSION 5u`,
  `ASTRA_FILESYSTEM_LIBRARY_ABI_MINOR 0u`.
- `sw/userspace/vfs/src/filesystem_library_identity.c`:
  `ASTRA_DYNAMIC_LIBRARY("filesystem.library.5", 5, 0, 0, ...)`.
- `sw/userspace/vfs/Makefile:247`: the identity check reads
  `'^filesystem.library.5 5.0.0 ABI 5.0$$'`.
- Replace `filesystem.library.4` with `filesystem.library.5` in:
  `sw/userspace/posix/Makefile`, `sw/userspace/libraries.mk`,
  `sw/userspace/vfs/Makefile`, `sw/userspace/kits/Configuration.kit/manifest`,
  `sw/userspace/config/Makefile`, `sw/userspace/kits/Posix.kit/manifest`,
  `sw/userspace/kits/Filesystem.kit/manifest`, `sw/userspace/tests/test_boundaries.py`,
  `sw/userspace/lua/Makefile`, `sw/userspace/apps/Terminal.app/manifest`,
  `sw/userspace/apps/InterfaceGallery.app/manifest`, `ndk/docs/source/filesystem.md`,
  `tools/tests/test_check_bundle_dependencies.py`, `docs/FILESYSTEM_KIT.md`,
  `docs/ABI.md`. Leave the dated handover documents alone.

Check: `grep -rln "filesystem\.library\.4" --exclude-dir=build --exclude-dir=.git . | grep -v HANDOVER`
prints nothing.

- [ ] **Step 8: Measure the hop on QEMU**

Sync, then on beast:

```sh
~/astra-mg/locked make -C sw/userspace -j32
~/astra-mg/locked make -C sw/userspace test
~/astra-mg/locked make -C sw/boot -j32
~/astra-mg/locked bash ~/astra-mg/images.sh
Q=$(tail -n1 /tmp/qemu-host-build.log); R=sw/boot/build/astra_boot.bin
OUT=/mnt/Documents/astra68/builds/fs-phase1-task3-$(git rev-parse --short HEAD)
mkdir -p "$OUT"
for run in 1 2 3; do
  ~/astra-mg/locked python3 emu/qemu/test-fs-semantics.py $Q $R ~/astra-mg/base.img --bench t3-$run "$OUT/qemu-t3-$run.json"
done
BASE=$(ls -d /mnt/Documents/astra68/builds/fs-phase1-baseline-* | tail -1)
python3 tools/fs-baseline-compare.py "$BASE/qemu-base1.json" "$OUT/qemu-t3-1.json" | tee "$OUT/hop-vs-baseline.txt"
```

Expected: three records without failed workloads, and a comparison table.
This step does not gate on the table. It is the measured cost of the extra
hop that D1 asks for. Record it in the commit message: the median and p95
ratio of `stat-warm`, `open-close-warm`, `rand-read` and `held-miss`. If
`held-miss` p95 rises above twice baseline with `workers 4`, rerun with
`workers 1` (edit the image's settings with
`astra_image._replace_volume_file`, the way `test-system-log.py:612-618`
does) and with `workers 8`, and record all three. The worker count is the
lever, and the numbers are what decide it.

- [ ] **Step 9: Perturb**

The bypass must be gone at the place it lived. Temporarily restore, in
`vfs_port_transport.c`'s HELLO handling, a duplicate of any service-held
handle attached to the reply:

```c
        if (wire_operation == ASTRA_VFS_OP_HELLO &&
            astra_rt_handle_duplicate(host->receive, ASTRA_RIGHT_SIGNAL,
                                      &outgoing_handles[0]) ==
                ASTRA_SYSCALL_OK)
            outgoing_handle_count = 1u;
```

Re-sync, run `test_vfs_port`. Expected: `test_hello_attaches_no_capability`
fails on `count == 0u`. Delete the lines, re-sync with the `--no-times`
rsync, rerun: `ASTRA VFS PORT PASS`.

- [ ] **Step 10: Full suite**

`~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-task3.log`. Expected: green, and
the terminal and system-log gates, which write `/work`, pass through the
port path.

- [ ] **Step 11: Commit**

```bash
git rm sw/userspace/vfs/src/vfs_host_direct.c sw/userspace/vfs/include/astra/vfs_host_direct.h
git add sw/userspace/vfs sw/include/astra/vfs_service.h ndk/include/astra/vfs_client.h \
  ndk/include/astra/filesystem_library.h sw/userspace/services/hostfs \
  sw/userspace/services/hostbench sw/userspace/supervisor/src/loader.c \
  sw/userspace/supervisor/src/vfs_host.c emu/qemu/astra_image.py \
  emu/qemu/test-astra-image-providers.py sw/userspace/posix/Makefile \
  sw/userspace/libraries.mk sw/userspace/kits sw/userspace/config/Makefile \
  sw/userspace/tests/test_boundaries.py sw/userspace/lua/Makefile \
  sw/userspace/apps/Terminal.app/manifest sw/userspace/apps/InterfaceGallery.app/manifest \
  ndk/docs/source/filesystem.md tools/tests/test_check_bundle_dependencies.py \
  docs/FILESYSTEM_KIT.md docs/ABI.md
git commit -m "fix(vfs): close the HostFS client bypass (FS44); storage protocol 27, Kit 5"
```

---

### Task 3b: The scoped direct lane (D1 again; FS15, FS44)

Task 3 closed the bypass by putting HostFS on every request, and that costs
2.6x. This task closes it the other way. A client drives its own host
channel again, as before protocol 27. But the channel is now bound by the
kernel to *scopes* only HostFS can mint, and QEMU enforces the binding on
every command. The design is the investigation's option (A) and the
prototype in `/Users/barry/Git/astra68-fsproto`. Here it gets its two
missing checks:

- only the filesystem service can mint a scope;
- every handle is tagged with the scope it was opened under.

Two choices change, for reasons given in "Decisions this plan changes"
(D1 again):

- a channel carries several scopes;
- there is one scope per grant.

The host command record stays at version 2 here, so a scope's provider word
is a *volume*. Task 4 makes it a rooted *session* and adds QEMU's own
creator check.

**What survives from `62b72fc2`, and why:**

| Piece of 62b72fc2 | Fate | Reason |
|---|---|---|
| HELLO never duplicates the host device | **Stays.** HELLO now attaches the grant's *scope*, a capability that can only reach that scope. The test becomes `test_hello_attaches_the_scope_and_never_the_device`. | The device in a client's hands was FS44 itself. |
| The relay data path (HostFS serving every WORK/SOUND operation through its core and the host backend) | **Deleted.** A port that hands out scopes answers every data operation `ASTRA_VFS_ERR_UNSUPPORTED`; there is no fallback. | It is what cost 2.6x; the lane replaces it. |
| HostFS worker threads, `services/hostfs/settings.conf` `workers N`, the `CONFIG:r` grant and the config-library link | **Deleted** (back to the single pump). | The control plane is HELLO, BYE and (Task 5) `GRANT_DERIVE`, a few per process launch, none doing Linux I/O except a derive's session open. A held Linux miss now blocks only the client thread that issued it. Task 5 adds `settings.conf` back for `grants N`, with its own reader. |
| The workers <= `ASTRA_DMA_SLOT_COUNT` (4) bound | **Deleted with the workers.** The underlying limit moves to the client: every thread of a process that uses a host volume needs a lane, and a lane is one DMA buffer. | Task 11 lifts the per-process DMA buffer limit. |
| hostbench's deleted layer benchmark and `test-host-channel-userspace.py` without `LAYER` lines (and `6d4e6a2c`'s syslog fix) | **Stay.** hostbench gains the scope-mint refusal check. | The layer benchmark drove the device as a WORK client, which is now impossible. `fsprobe` (this task) measures the lane. |
| Storage protocol 27 | **Stays in history; 28 is new.** | Version 28 means "HELLO from a scoped port attaches the grant's scope". A 62b72fc2 object meeting 3b code must fail the version check, not misread an attached handle (Global Constraints). |
| Filesystem Kit 5 | **Stays in history; 6 is new.** | The lane fields come back into `AstraVfsClient` with a different shape (a scope, no per-client transport). Kit 5 binaries must not load it. |

**Files:**
- Modify: `sw/include/astra/object_abi.h` (`ASTRA_RIGHT_HOST_SCOPE`), `sw/include/astra/syscall.h` (109-111), `sw/include/astra/host.h` (version, capability, scope flags, config 2, lease scope words, `ASTRA_CAPABILITY_HOST_FILESYSTEM`)
- Modify: `sw/kernel/handle.h`, `sw/kernel/handle.c` (object type 17, import-rights hook), `sw/kernel/device.h` (`KERNEL_DEVICE_RIGHT_HOST_SCOPE`), `sw/kernel/process.c` (scope object, syscalls, binding, revocation), `sw/kernel/platform.c`, `sw/kernel/platform.h` (config words, ATTACH, DETACH)
- Test: `sw/kernel/tests/test_process.c` (`test_host_scope`), `sw/kernel/tests/test_platform.c`, `sw/kernel/tests/test_handle.c`
- Modify: `sw/userspace/runtime/src/syscall.c`, `sw/userspace/runtime/runtime.library.exports`, `sw/userspace/runtime/src/runtime_library.c`, `sw/userspace/runtime/Makefile`, `ndk/include/astra/runtime.h`, `sw/userspace/kits/Runtime.kit/manifest`, `sw/userspace/kits/Filesystem.kit/manifest` (runtime 1.14)
- Modify: `emu/qemu/qemu-9.2/hw/m68k/astra68.c`; Test: `emu/qemu/test-hostfs.py` (`scopes`, `scope_revocation`)
- Restore from `60f2d30a`, then modify: `sw/userspace/vfs/src/vfs_host_direct.c`, `sw/userspace/vfs/include/astra/vfs_host_direct.h`, `sw/userspace/vfs/Makefile`, `sw/userspace/vfs/src/vfs_client.c`, `ndk/include/astra/vfs_client.h`, `ndk/tests/headers.c`, `sw/userspace/vfs/src/vfs_port_transport.c`, `sw/userspace/vfs/include/astra/vfs_port_transport.h`, `sw/userspace/vfs/src/vfs_process.c`, `sw/userspace/vfs/tests/test_vfs_port.c`, `sw/userspace/vfs/tests/test_vfs_process.c`, `sw/userspace/supervisor/src/loader.c`, `sw/userspace/supervisor/src/vfs_host.c`, `sw/userspace/services/hostfs/main.c`, `sw/userspace/services/hostfs/Makefile`, `emu/qemu/astra_image.py`, `emu/qemu/test-astra-image-providers.py`
- Modify: `sw/userspace/vfs/src/vfs_host_transport.c`, `sw/userspace/vfs/include/astra/vfs_host_transport.h`, `sw/userspace/vfs/include/astra/vfs_host_backend.h`, `sw/userspace/vfs/src/backends/host.c`; Test: `sw/userspace/vfs/tests/test_vfs_host_transport.c`, `sw/userspace/vfs/tests/test_vfs_host_backend.c`
- Modify: `sw/include/astra/vfs_service.h` (28), Kit 6 (Task 3 Step 7's list with `6`)
- Modify: `sw/userspace/services/hostbench/main.c` (mint refusal), `emu/qemu/test-host-channel-userspace.py`, `emu/qemu/fs-gates.sh`
- Create: `sw/userspace/commands/fsprobe/fsprobe.c`, `emu/qemu/fs-probe.py`, `tools/fs-probe-compare.py`; Modify: `sw/userspace/commands/Makefile`
- Modify: `sw/userspace/commands/fssem/fssem.c`, `emu/qemu/test-fs-semantics.py` (`raw-lane`, `lane-fork-exec`)
- Modify: `docs/STORAGE_AND_VFS.md`, `docs/FILESYSTEM_PRODUCTION_PRD.md` (gap table, FS44), `docs/DRIVER_AND_SERVICE_ARCHITECTURE.md` §8, `docs/ABI.md`, `docs/FILESYSTEM_KIT.md`, `ndk/docs/source/filesystem.md`

**Interfaces:**
- Produces (kernel ABI):
  - `ASTRA_SYSCALL_HOST_SCOPE_CREATE` (109): `data[1]` a host device handle
    with `ASTRA_RIGHT_HOST_SCOPE`, `data[2]` the provider word (a volume until
    Task 4, then a session), `data[3]` flags `ASTRA_HOST_SCOPE_READ_ONLY |
    ASTRA_HOST_SCOPE_DELEGATE`. It answers in `data[1]` a handle with READ,
    WRITE, TRANSFER and ADMINISTER.
  - `ASTRA_SYSCALL_HOST_SCOPE_REVOKE` (110): `data[1]` a scope with
    ADMINISTER.
  - `ASTRA_SYSCALL_HOST_SCOPE_ATTACH` (111): `data[1]` a scope with WRITE;
    it binds the scope to the calling thread's bound channel.
  - `HOST_QUERY`, `HOST_CHANNEL_OPEN` and `HOST_CHANNEL_CLOSE` accept a
    scope where they took a device. `HOST_EXECUTE` does not.
  - `AstraHostLeaseInfo.scope` and `.scope_flags` answer a scope's word and
    flags.
- Produces (runtime 1.14): `uint32_t astra_host_scope_create(uint32_t device, uint32_t word, uint32_t flags, uint32_t *scope)`,
  `uint32_t astra_host_scope_revoke(uint32_t scope)`,
  `uint32_t astra_host_scope_attach(uint32_t scope)`.
- Produces (provider): channel configuration version 2 with `scope`,
  `scope_flags`, `scope_owner` and operations `ATTACH` (3) and `DETACH`
  (4). On a bound channel:
  - only filesystem commands run; anything else answers `ASTRA_STATUS_ACCESS`;
  - the command's volume selects an attached scope, and any other volume is
    `ACCESS`;
  - a read-only scope answers `ASTRA_STATUS_READ_ONLY` to a mutation;
  - a handle answers only under the volume it was opened in.

  QOM `astra-host-scoped-commands` (read) counts commands run through a
  scope.
- Produces (Kit 6): `AstraVfsClient.port_direct_scope` (was
  `port_direct_device` before 27); `astra_vfs_host_port_connect`,
  `astra_vfs_host_port_connect_lazy`, `astra_vfs_host_direct_*` as before
  27, the connect taking a scope; `astra_vfs_host_direct_fork_child(void)`.
- Produces (port): `astra_vfs_port_service_set_scope(host, scope) -> int`.
  Such a host is control-only.
- Produces (transport): `astra_vfs_host_transport_begin(context, scope, data_capacity, request)`.
  `scope` 0 means the transport's own handle. `AstraVfsHostBackend.scope`.
- Produces (tools): `fsprobe LABEL N [PAUSE_MS]`;
  `fs-probe.py QEMU ROM IMAGE OUT.json [--n N] [--label L]`;
  `fs-probe-compare.py BASE[,BASE...] NEW[,NEW...] [--budget 0.10] [--cross 0.05] [--hostfs-ns 500]`.
- Consumed by: Task 4 (the word becomes a session; creator check against
  the claim), Task 5 (one scope per grant, delegation), Task 8 (fork on the
  inherited scope), Task 10 (revocation in recovery), Task 11 (DMA records,
  per-scope file limit), Task 12b (inline execution keys on bound
  channels), Task 13 (fsprobe gates).

- [ ] **Step 1: Write the failing kernel test**

In `sw/kernel/tests/test_process.c`, extend the platform fakes. Add beside
the other host statics:

```c
static uint32_t host_channel_scope;
static uint32_t host_channel_scope_flags;
static uint32_t host_channel_scope_owner;
static uint32_t host_channel_attach_calls;
static uint32_t host_channel_attach_scope;
static uint32_t host_channel_attach_flags;
static uint32_t host_channel_detach_calls;
static uint32_t host_channel_detach_scope;
```

Reset them where `initialize_test` resets `host_channel_open_calls`
(`grep -n "host_channel_open_calls = 0" sw/kernel/tests/test_process.c`).
The open fake takes and records the scope words:

```c
uint32_t kernel_platform_host_channel_open(
    uint32_t owner, uint32_t host_generation, uint32_t channel_generation,
    uint32_t slot, uint32_t physical_buffer, uint32_t byte_size,
    uint32_t command_capacity, uint32_t scope, uint32_t scope_flags,
    uint32_t scope_owner)
{
    assert(owner != 0u && host_generation == host_state.host_generation);
    assert(channel_generation != 0u &&
           slot < KERNEL_VM_HOST_CHANNEL_PAGE_COUNT);
    assert(byte_size >= ASTRA_HOST_CHANNEL_HEADER_SIZE +
                        ASTRA_HOST_COMMAND_SIZE &&
           byte_size <= KERNEL_PAGE_SIZE && command_capacity == 1u);
    host_channel_scope = scope;
    host_channel_scope_flags = scope_flags;
    host_channel_scope_owner = scope_owner;
    host_channel_physical = physical_buffer;
    host_channel_byte_size = byte_size;
    host_channel_slot = slot;
    host_channel_states[slot].state_flags = ASTRA_HOST_STATE_READY;
    host_channel_states[slot].generation = channel_generation;
    host_channel_states[slot].status = ASTRA_SYSCALL_OK;
    ++host_channel_open_calls;
    return ASTRA_SYSCALL_OK;
}

uint32_t kernel_platform_host_channel_attach(
    uint32_t owner, uint32_t host_generation, uint32_t channel_generation,
    uint32_t slot, uint32_t scope, uint32_t scope_flags, uint32_t scope_owner)
{
    assert(owner != 0u && host_generation == host_state.host_generation);
    assert(channel_generation != 0u && slot == host_channel_slot);
    assert(scope_owner != 0u);
    host_channel_attach_scope = scope;
    host_channel_attach_flags = scope_flags;
    ++host_channel_attach_calls;
    return ASTRA_SYSCALL_OK;
}

uint32_t kernel_platform_host_channel_detach(
    uint32_t owner, uint32_t host_generation, uint32_t channel_generation,
    uint32_t slot, uint32_t scope)
{
    assert(owner != 0u && host_generation == host_state.host_generation);
    assert(channel_generation != 0u &&
           slot < KERNEL_VM_HOST_CHANNEL_PAGE_COUNT);
    host_channel_detach_scope = scope;
    ++host_channel_detach_calls;
    return ASTRA_SYSCALL_OK;
}
```

Then the test, after `test_audio_stream`. It uses `test_trap`
(`test_process.c:5411`):

```c
static uint32_t scope_call(uint32_t number, uint32_t d1, uint32_t d2,
                           uint32_t d3, uint32_t d4, uint32_t d5,
                           KernelCpuContext **next)
{
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];

    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR, LOADER_TEXT_VADDR, 0u);
    assert(test_trap(KERNEL_PROCESS_STACK_TOP - 1536u, frame, next, number,
                     d1, d2, d3, d4, d5) == KERNEL_PROCESS_OK);
    return (*next)->data[0];
}

/* Sends `handle` to this process through a fresh port and answers what
 * arrives: the transfer path a HELLO reply takes. */
static uint32_t scope_round_trip(uint32_t handle, KernelCpuContext **next)
{
    const uint32_t user_message = KERNEL_PROCESS_STACK_TOP - 1024u;
    const uint32_t user_handles = KERNEL_PROCESS_STACK_TOP - 512u;
    AstraMessageHeader message;
    uint32_t receive;
    uint32_t send;
    uint32_t moved = 0u;

    assert(scope_call(ASTRA_SYSCALL_PORT_CREATE, 1u, sizeof(message), 0u, 0u,
                      0u, next) == ASTRA_SYSCALL_OK);
    receive = (*next)->data[1];
    send = (*next)->data[2];
    memset(&message, 0, sizeof(message));
    message.total_size = sizeof(message);
    message.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    message.protocol = 0x53434f50u;
    message.protocol_version = 1u;
    message.operation = 1u;
    assert(kernel_user_copy_to_asm(user_message, &message,
                                   sizeof(message)) == KERNEL_USER_COPY_OK);
    assert(kernel_user_copy_to_asm(user_handles, &handle, sizeof(handle)) ==
           KERNEL_USER_COPY_OK);
    assert(scope_call(ASTRA_SYSCALL_PORT_SEND_TRY, send, user_message,
                      sizeof(message), user_handles, 1u, next) ==
           ASTRA_SYSCALL_OK);
    assert(scope_call(ASTRA_SYSCALL_PORT_RECEIVE_TRY, receive, user_message,
                      sizeof(message), user_handles, 1u, next) ==
           ASTRA_SYSCALL_OK);
    assert(kernel_user_copy_from_asm(&moved, user_handles, sizeof(moved)) ==
           KERNEL_USER_COPY_OK);
    assert(scope_call(ASTRA_SYSCALL_CLOSE, receive, 0u, 0u, 0u, 0u, next) ==
           ASTRA_SYSCALL_OK);
    assert(scope_call(ASTRA_SYSCALL_CLOSE, send, 0u, 0u, 0u, 0u, next) ==
           ASTRA_SYSCALL_OK);
    return moved;
}

/*
 * A filesystem scope (FS15, FS44; decisions of 2026-10-08): minted only with
 * ASTRA_RIGHT_HOST_SCOPE, stamped by the kernel into the channels bound to
 * it, attached several to one thread's channel, revoked everywhere at once,
 * delegated only when DELEGATE says so, and revoked with its creator.
 */
static void test_host_scope(void)
{
    const uint32_t user_info = KERNEL_PROCESS_STACK_TOP - 64u;
    const uint32_t user_open = KERNEL_PROCESS_STACK_TOP - 128u;
    const uint32_t user_lease = KERNEL_PROCESS_STACK_TOP - 256u;
    KernelProcessBootstrapCapability capability = {0};
    KernelCpuContext *next = NULL;
    KernelProcessSnapshot process;
    AstraStartupCapability table[3];
    AstraDmaBufferInfo buffer;
    AstraHostChannelOpen channel;
    AstraHostLeaseInfo lease;
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint32_t process_id = 0u;
    uint32_t device = 0u;
    uint32_t no_scope_right;
    uint32_t work;
    uint32_t work_client;
    uint32_t readonly;
    uint32_t delegable;
    uint32_t moved;

    loader_build_image();
    initialize_test();
    host_state.capabilities |= ASTRA_HOST_CAP_FS_SCOPE;
    capability.name = ASTRA_CAPABILITY_HOST_DEVICE;
    capability.kind = KERNEL_PROCESS_BOOTSTRAP_DEVICE;
    capability.device_id = ASTRA_DEVICE_ID_HOST0;
    capability.rights = KERNEL_DEVICE_RIGHTS;
    assert(kernel_process_create_executable(loader_image, loader_image_size,
                                            &capability, 1u, &process_id) ==
           KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    assert(kernel_user_copy_from_asm(
               table, KERNEL_VM_USER_MIN + ASTRA_STARTUP_INFO_SIZE,
               sizeof(table)) == KERNEL_USER_COPY_OK);
    for (uint32_t index = 0u; index < 3u; ++index)
        if (astra_capability_name_equal(table[index].name,
                                        ASTRA_CAPABILITY_HOST_DEVICE))
            device = table[index].handle;
    assert(device != 0u);
    assert(kernel_process_snapshot(0u, &process));

    /* Entropy, media and remote desktop hold the device without
     * HOST_SCOPE: they cannot mint a scope. */
    assert(scope_call(ASTRA_SYSCALL_HANDLE_DUPLICATE, device,
                      KERNEL_DEVICE_RIGHTS & ~ASTRA_RIGHT_HOST_SCOPE, 0u, 0u,
                      0u, &next) == ASTRA_SYSCALL_OK);
    no_scope_right = next->data[1];
    assert(scope_call(ASTRA_SYSCALL_HOST_SCOPE_CREATE, no_scope_right,
                      ASTRA_HOST_FS_VOLUME_WORK, 0u, 0u, 0u, &next) ==
           ASTRA_SYSCALL_ACCESS_DENIED);
    assert(scope_call(ASTRA_SYSCALL_HOST_SCOPE_CREATE, device,
                      ASTRA_HOST_FS_VOLUME_WORK, 1u << 7, 0u, 0u, &next) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);
    host_state.capabilities &= ~ASTRA_HOST_CAP_FS_SCOPE;
    assert(scope_call(ASTRA_SYSCALL_HOST_SCOPE_CREATE, device,
                      ASTRA_HOST_FS_VOLUME_WORK, 0u, 0u, 0u, &next) ==
           ASTRA_SYSCALL_UNSUPPORTED);
    host_state.capabilities |= ASTRA_HOST_CAP_FS_SCOPE;

    assert(scope_call(ASTRA_SYSCALL_HOST_SCOPE_CREATE, device,
                      ASTRA_HOST_FS_VOLUME_WORK, 0u, 0u, 0u, &next) ==
           ASTRA_SYSCALL_OK);
    work = next->data[1];
    assert(scope_call(ASTRA_SYSCALL_HOST_SCOPE_CREATE, device,
                      ASTRA_HOST_FS_VOLUME_SOUND, ASTRA_HOST_SCOPE_READ_ONLY,
                      0u, 0u, &next) == ASTRA_SYSCALL_OK);
    readonly = next->data[1];

    /* HOST_QUERY on a scope answers the word the kernel will stamp. */
    assert(scope_call(ASTRA_SYSCALL_HOST_QUERY, readonly, user_lease, 0u, 0u,
                      0u, &next) == ASTRA_SYSCALL_OK);
    assert(kernel_user_copy_from_asm(&lease, user_lease, sizeof(lease)) ==
           KERNEL_USER_COPY_OK);
    assert(lease.scope == ASTRA_HOST_FS_VOLUME_SOUND &&
           lease.scope_flags == ASTRA_HOST_SCOPE_READ_ONLY);

    /* What a client receives: no ADMINISTER, and never the device path. */
    assert(scope_call(ASTRA_SYSCALL_HANDLE_DUPLICATE, work,
                      ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                          ASTRA_RIGHT_TRANSFER, 0u, 0u, 0u, &next) ==
           ASTRA_SYSCALL_OK);
    work_client = next->data[1];
    assert(scope_call(ASTRA_SYSCALL_HOST_SCOPE_REVOKE, work_client, 0u, 0u,
                      0u, 0u, &next) == ASTRA_SYSCALL_ACCESS_DENIED);
    assert(scope_call(ASTRA_SYSCALL_HOST_EXECUTE, work_client, user_info, 0u,
                      0u, 0u, &next) == ASTRA_SYSCALL_INVALID_HANDLE);
    /* Nothing to attach to yet. */
    assert(scope_call(ASTRA_SYSCALL_HOST_SCOPE_ATTACH, readonly, 0u, 0u, 0u,
                      0u, &next) == ASTRA_SYSCALL_INVALID_ARGUMENT);

    /* A channel opened on the scope is bound to it by the kernel. */
    assert(scope_call(ASTRA_SYSCALL_DMA_CREATE, KERNEL_PAGE_SIZE, user_info,
                      0u, 0u, 0u, &next) == ASTRA_SYSCALL_OK);
    assert(kernel_user_copy_from_asm(&buffer, user_info, sizeof(buffer)) ==
           KERNEL_USER_COPY_OK);
    memset(&channel, 0, sizeof(channel));
    channel.size = sizeof(channel);
    channel.buffer = buffer.handle;
    channel.byte_size = ASTRA_HOST_CHANNEL_HEADER_SIZE +
                        ASTRA_HOST_COMMAND_SIZE;
    channel.command_capacity = 1u;
    assert(kernel_user_copy_to_asm(user_open, &channel, sizeof(channel)) ==
           KERNEL_USER_COPY_OK);
    assert(scope_call(ASTRA_SYSCALL_HOST_CHANNEL_OPEN, work_client,
                      user_open, 0u, 0u, 0u, &next) == ASTRA_SYSCALL_OK);
    assert(host_channel_open_calls == 1u);
    assert(host_channel_scope == ASTRA_HOST_FS_VOLUME_WORK &&
           host_channel_scope_flags == ASTRA_HOST_SCOPE_BOUND &&
           host_channel_scope_owner == process.owner);

    /* A second scope rides the same channel, once. */
    assert(scope_call(ASTRA_SYSCALL_HOST_SCOPE_ATTACH, readonly, 0u, 0u, 0u,
                      0u, &next) == ASTRA_SYSCALL_OK);
    assert(host_channel_attach_calls == 1u &&
           host_channel_attach_scope == ASTRA_HOST_FS_VOLUME_SOUND &&
           host_channel_attach_flags ==
               (ASTRA_HOST_SCOPE_BOUND | ASTRA_HOST_SCOPE_READ_ONLY));
    assert(scope_call(ASTRA_SYSCALL_HOST_SCOPE_ATTACH, readonly, 0u, 0u, 0u,
                      0u, &next) == ASTRA_SYSCALL_OK);
    assert(host_channel_attach_calls == 1u);
    assert(scope_call(ASTRA_SYSCALL_HOST_SCOPE_ATTACH, device, 0u, 0u, 0u,
                      0u, &next) == ASTRA_SYSCALL_INVALID_HANDLE);

    /* Revocation unbinds it from every channel and refuses it after. */
    assert(scope_call(ASTRA_SYSCALL_HOST_SCOPE_REVOKE, readonly, 0u, 0u, 0u,
                      0u, &next) == ASTRA_SYSCALL_OK);
    assert(host_channel_detach_calls == 1u &&
           host_channel_detach_scope == ASTRA_HOST_FS_VOLUME_SOUND);
    assert(scope_call(ASTRA_SYSCALL_HOST_SCOPE_ATTACH, readonly, 0u, 0u, 0u,
                      0u, &next) == ASTRA_SYSCALL_PEER_DEAD);
    assert(scope_call(ASTRA_SYSCALL_HOST_QUERY, readonly, user_lease, 0u,
                      0u, 0u, &next) == ASTRA_SYSCALL_PEER_DEAD);
    assert(host_channel_close_calls == 0u);

    /* Delegation: a scope without DELEGATE arrives without TRANSFER, so its
     * holder cannot duplicate or send it on; one with DELEGATE keeps it.
     * Neither gains ADMINISTER on the way. */
    moved = scope_round_trip(work_client, &next);
    assert(scope_call(ASTRA_SYSCALL_HANDLE_DUPLICATE, moved, ASTRA_RIGHT_READ,
                      0u, 0u, 0u, &next) == ASTRA_SYSCALL_ACCESS_DENIED);
    assert(scope_call(ASTRA_SYSCALL_HOST_SCOPE_REVOKE, moved, 0u, 0u, 0u, 0u,
                      &next) == ASTRA_SYSCALL_ACCESS_DENIED);
    assert(scope_call(ASTRA_SYSCALL_HOST_SCOPE_CREATE, device,
                      ASTRA_HOST_FS_VOLUME_SOUND, ASTRA_HOST_SCOPE_DELEGATE,
                      0u, 0u, &next) == ASTRA_SYSCALL_OK);
    delegable = next->data[1];
    assert(scope_call(ASTRA_SYSCALL_HANDLE_DUPLICATE, delegable,
                      ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                          ASTRA_RIGHT_TRANSFER, 0u, 0u, 0u, &next) ==
           ASTRA_SYSCALL_OK);
    moved = scope_round_trip(next->data[1], &next);
    assert(scope_call(ASTRA_SYSCALL_HANDLE_DUPLICATE, moved, ASTRA_RIGHT_READ,
                      0u, 0u, 0u, &next) == ASTRA_SYSCALL_OK);
    assert(scope_call(ASTRA_SYSCALL_HANDLE_DUPLICATE, moved,
                      ASTRA_RIGHT_READ | ASTRA_RIGHT_ADMINISTER, 0u, 0u, 0u,
                      &next) == ASTRA_SYSCALL_ACCESS_DENIED);
    /* DELEGATE is the kernel's: the provider never sees it. */
    assert(scope_call(ASTRA_SYSCALL_HOST_QUERY, moved, user_lease, 0u, 0u,
                      0u, &next) == ASTRA_SYSCALL_OK);
    assert(kernel_user_copy_from_asm(&lease, user_lease, sizeof(lease)) ==
           KERNEL_USER_COPY_OK);
    assert(lease.scope_flags == 0u);

    /* The creator's death revokes what it created, channel binding first. */
    registers[0] = ASTRA_SYSCALL_EXIT;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR, LOADER_TEXT_VADDR, 0u);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(host_channel_detach_calls == 2u &&
           host_channel_detach_scope == ASTRA_HOST_FS_VOLUME_WORK);
    assert(host_channel_close_calls == 1u);
}
```

Call `test_host_scope();` after `test_audio_stream();` in `main`
(`test_process.c:14910`).

In `sw/kernel/tests/test_handle.c`, the hook is generic and tested there.
Use the file's own table fixture and object. Find them with
`grep -n "kernel_handle_install(\|kernel_handle_import_commit" sw/kernel/tests/test_handle.c`;
there is an existing import test to extend.

```c
static uint32_t strip_transfer(KernelObjectType type, void *object,
                               uint32_t rights)
{
    (void)object;
    return type == KERNEL_OBJECT_EVENT ? rights & ~ASTRA_RIGHT_TRANSFER :
                                         rights;
}
```

Register the hook with `kernel_handle_set_import_rights(strip_transfer)`
before the existing transfer test's import. Assert that the imported event
handle's entry has rights without `ASTRA_RIGHT_TRANSFER`, using the test's
existing way of reading an entry's rights (`kernel_handle_lookup` with
`ASTRA_RIGHT_TRANSFER` answers `KERNEL_HANDLE_ACCESS_DENIED`). Then reset
with `kernel_handle_set_import_rights(NULL)`, and show that a second import
keeps TRANSFER. Use whichever object type the existing test transfers if it
is not an event, and match `strip_transfer` to it.

- [ ] **Step 2: Run it to see it fail**

On beast: `~/astra-mg/locked make -C sw/kernel clean && ~/astra-mg/locked make -C sw/kernel test`.
Expected: compile errors. `ASTRA_SYSCALL_HOST_SCOPE_CREATE`,
`ASTRA_RIGHT_HOST_SCOPE`, `AstraHostLeaseInfo.scope` and the extra fake
parameters do not exist yet.

- [ ] **Step 3: The ABI**

`sw/include/astra/object_abi.h`, after `ASTRA_RIGHT_AUDIO_STREAM`:

```c
/** On a host device handle: create filesystem scopes
 * (::ASTRA_SYSCALL_HOST_SCOPE_CREATE), and nothing else the device does not
 * already allow. The supervisor grants it only as HOST_FILESYSTEM, to the
 * filesystem service. */
#define ASTRA_RIGHT_HOST_SCOPE (1u << 9)
```

`sw/include/astra/syscall.h`, after `ASTRA_SYSCALL_AUDIO_STREAM_WAIT`:

```c
/**
 * Create a filesystem scope: the authority to drive host channels bound to
 * one provider session (docs/FILESYSTEM_PRODUCTION_PRD.md FS15). data[1] is
 * a host device handle carrying ::ASTRA_RIGHT_HOST_SCOPE; data[2] the
 * provider's word for the session (a volume under host command record 2, a
 * session id from record 3); data[3] ::ASTRA_HOST_SCOPE_READ_ONLY and/or
 * ::ASTRA_HOST_SCOPE_DELEGATE. Returns in data[1] a handle with READ (query),
 * WRITE (open and attach channels), TRANSFER and ADMINISTER (revoke).
 * UNSUPPORTED when the provider lacks ::ASTRA_HOST_CAP_FS_SCOPE.
 */
#define ASTRA_SYSCALL_HOST_SCOPE_CREATE 109
/**
 * Revoke a scope: it is detached from every channel that carries it, which
 * the provider honors before this returns, and every later open, attach
 * or query on it answers PEER_DEAD. data[1] is the scope with
 * ::ASTRA_RIGHT_ADMINISTER.
 */
#define ASTRA_SYSCALL_HOST_SCOPE_REVOKE 110
/**
 * Bind one more scope to the calling thread's host channel, which must have
 * been opened on a scope; at most ::ASTRA_HOST_CHANNEL_SCOPE_MAX. A scope
 * already bound there is not bound twice. data[1] is the scope with
 * ::ASTRA_RIGHT_WRITE.
 */
#define ASTRA_SYSCALL_HOST_SCOPE_ATTACH 111
```

`sw/include/astra/host.h`:
- `#define ASTRA_HOST_VERSION UINT32_C(0x0001000d)`.
- Beside `ASTRA_CAPABILITY_HOST_DEVICE`:

  ```c
  /** The host device with ::ASTRA_RIGHT_HOST_SCOPE: the filesystem
   * service's grant. Every other holder is granted HOST_DEVICE without it. */
  #define ASTRA_CAPABILITY_HOST_FILESYSTEM "HOST_FILESYSTEM"
  ```

- After `ASTRA_HOST_CAP_AUDIO_STREAM`:

  ```c
  /** Host channels may be bound to filesystem scopes
   * (::ASTRA_SYSCALL_HOST_SCOPE_CREATE). On a bound channel the provider
   * runs only filesystem commands, each through one scope the kernel
   * attached: beneath its root, with its rights. */
  #define ASTRA_HOST_CAP_FS_SCOPE (1u << 10)
  /** Channel configuration: the scope words name a scope. Kernel-written. */
  #define ASTRA_HOST_SCOPE_BOUND     (1u << 0)
  /** The scope refuses every mutation (ASTRA_STATUS_READ_ONLY). */
  #define ASTRA_HOST_SCOPE_READ_ONLY (1u << 1)
  /** Kept by the kernel, never sent to the provider: the scope may be handed
   * on, so it arrives with ::ASTRA_RIGHT_TRANSFER. Without it a scope
   * arrives without TRANSFER: its holder may use it and fork with it. */
  #define ASTRA_HOST_SCOPE_DELEGATE  (1u << 2)
  /** Scopes one host channel carries at most. */
  #define ASTRA_HOST_CHANNEL_SCOPE_MAX 8u
  ```

- `AstraHostLeaseInfo`: replace `uint32_t reserved[2];` with

  ```c
      uint32_t scope;          /**< HOST_QUERY on a scope: the provider word it binds; 0 on a device. */
      uint32_t scope_flags;    /**< HOST_QUERY on a scope: ::ASTRA_HOST_SCOPE_READ_ONLY or 0. */
  ```

- The channel configuration: `ASTRA_HOST_CHANNEL_CONFIG_VERSION 2u`, and
  after `ASTRA_HOST_CHANNEL_CONFIG_CLOSE`:

  ```c
  /** Bind one more scope to an open bound channel. */
  #define ASTRA_HOST_CHANNEL_CONFIG_ATTACH 3u
  /** Unbind a scope (revocation): a command naming it that has not started
   * is refused from the moment this configuration write returns. */
  #define ASTRA_HOST_CHANNEL_CONFIG_DETACH 4u
  ```

  In `AstraHostChannelConfig`, replace `uint32_t reserved[7];` with

  ```c
      uint32_t scope;        /**< OPEN (bound), ATTACH, DETACH: the scope's provider word. */
      uint32_t scope_flags;  /**< ASTRA_HOST_SCOPE_BOUND | READ_ONLY; 0 for an unbound OPEN and for CLOSE and DETACH. */
      uint32_t scope_owner;  /**< OPEN (bound), ATTACH: owner of the process that created the scope. */
      uint32_t reserved[4];  /**< Zero. */
  ```

`sw/kernel/handle.h`: `KERNEL_OBJECT_HOST_SCOPE = 17` after
`KERNEL_OBJECT_AUDIO_STREAM = 16`, and:

```c
/* Rights an imported handle keeps, given the object's type and the rights it
 * was sent with. NULL keeps them all. One hook, set once at boot: the handle
 * layer stays type-blind. */
typedef uint32_t (*KernelHandleImportRights)(KernelObjectType type,
                                             void *object, uint32_t rights);
void kernel_handle_set_import_rights(KernelHandleImportRights filter);
```

`sw/kernel/device.h`: `#define KERNEL_DEVICE_RIGHT_HOST_SCOPE (1u << 9)`,
and add it to `KERNEL_DEVICE_RIGHTS`. Add a static assert next to the
others: `_Static_assert(KERNEL_DEVICE_RIGHT_HOST_SCOPE == ASTRA_RIGHT_HOST_SCOPE, "device scope right");`.

- [ ] **Step 4: The kernel**

`sw/kernel/handle.c`:

```c
static KernelHandleImportRights import_rights;

void kernel_handle_set_import_rights(KernelHandleImportRights filter)
{
    import_rights = filter;
}
```

In `kernel_handle_import_commit`, the one place every received handle is
installed (`handle.c:1394`), replace
`destination->rights = source->rights;` with:

```c
        destination->rights = import_rights != NULL ?
            import_rights(source->type, source->object, source->rights) :
            source->rights;
```

`sw/kernel/process.c`. This is the prototype's code with four changes:
- creation requires `KERNEL_DEVICE_RIGHT_HOST_SCOPE`;
- a channel carries up to eight scopes (`ATTACH`), and revocation
  *detaches* the scope rather than closing whole channels;
- the creator's death revokes;
- delegation goes through the import hook.

Beside `KernelProcessDmaBuffer`:

```c
/*
 * A filesystem scope (docs/FILESYSTEM_PRODUCTION_PRD.md FS15, FS44): the
 * authority to drive host channels bound to one provider session. Only a
 * device handle carrying ASTRA_RIGHT_HOST_SCOPE creates one -- HostFS's, one
 * per grant -- and clients receive duplicates without ADMINISTER. The kernel
 * writes the scope's word, rights and creator into every channel bound to
 * it; the provider enforces them. A bound channel holds a reference, so a
 * scope outlives its handles while a channel carries it.
 */
#define KERNEL_HOST_SCOPE_MAX 64u
enum {
    KERNEL_HOST_SCOPE_FREE = 0,
    KERNEL_HOST_SCOPE_ACTIVE,
    KERNEL_HOST_SCOPE_REVOKED
};
typedef struct KernelHostScope {
    KernelDeviceLease *lease;
    uint32_t word;           /* the provider's name for the session */
    uint32_t flags;          /* ASTRA_HOST_SCOPE_READ_ONLY | _DELEGATE */
    uint32_t creator_owner;
    uint16_t references;
    uint8_t state;
    uint8_t reserved;
} KernelHostScope;
_Static_assert(KERNEL_HOST_SCOPE_MAX < 256u,
               "a channel names its scopes in one byte");
```

`KernelHostChannel` gains, after `command_capacity`:

```c
    uint32_t owner;          /* the process owner stamped at open */
    /* host_scopes index + 1 of each scope bound here; 0 is empty. A channel
     * opened on a scope is `bound`, and only a bound channel takes more. */
    uint8_t scopes[ASTRA_HOST_CHANNEL_SCOPE_MAX];
```

and `uint8_t reserved[2];` becomes `uint8_t bound; uint8_t reserved;`.
Beside `host_channels`:
`static KernelHostScope host_scopes[KERNEL_HOST_SCOPE_MAX] KERNEL_TABLES;`.
`kernel_process_init` clears it beside `host_channels`
(`kernel_bytes_clear(host_scopes, sizeof(host_scopes));`), and registers
the hook: `kernel_handle_set_import_rights(host_scope_import_rights);`.

The scope functions, before `host_channel_close_slot`:

```c
static uint8_t host_scope_index(const KernelHostScope *scope)
{
    return (uint8_t)((uint32_t)(scope - host_scopes) + 1u);
}

static uint32_t host_scope_wire_flags(const KernelHostScope *scope)
{
    return ASTRA_HOST_SCOPE_BOUND |
           (scope->flags & ASTRA_HOST_SCOPE_READ_ONLY);
}

static void host_scope_drop(KernelHostScope *scope)
{
    if (scope == NULL || scope->references == 0u)
        return;
    if (--scope->references != 0u)
        return;
    kernel_device_handle_release(scope->lease, NULL);
    kernel_bytes_clear(scope, sizeof(*scope));
}

static bool host_scope_retain(void *object, void *context)
{
    KernelHostScope *scope = object;

    (void)context;
    if (scope == NULL || scope->state == KERNEL_HOST_SCOPE_FREE ||
        scope->references == UINT16_MAX)
        return false;
    ++scope->references;
    return true;
}

static void host_scope_release(void *object, void *context)
{
    (void)context;
    host_scope_drop(object);
}

/* A scope that may not be delegated arrives without TRANSFER: its holder can
 * use it and fork with it, not hand it on. The send from its creator is the
 * one transfer it makes (decision 2026-10-08, delegation). */
static uint32_t host_scope_import_rights(KernelObjectType type, void *object,
                                         uint32_t rights)
{
    const KernelHostScope *scope = object;

    if (type == KERNEL_OBJECT_HOST_SCOPE && scope != NULL &&
        (scope->flags & ASTRA_HOST_SCOPE_DELEGATE) == 0u)
        return rights & ~ASTRA_RIGHT_TRANSFER;
    return rights;
}

/* Binds `scope` to the open bound channel in `slot`; the binding holds a
 * reference. Already bound there: nothing to do. */
static uint32_t host_channel_attach(uint32_t slot, KernelHostScope *scope)
{
    KernelHostChannel *channel = &host_channels[slot];
    uint32_t free_at = ASTRA_HOST_CHANNEL_SCOPE_MAX;

    for (uint32_t at = 0u; at < ASTRA_HOST_CHANNEL_SCOPE_MAX; ++at) {
        if (channel->scopes[at] == host_scope_index(scope))
            return ASTRA_SYSCALL_OK;
        if (channel->scopes[at] == 0u && free_at == ASTRA_HOST_CHANNEL_SCOPE_MAX)
            free_at = at;
    }
    if (free_at == ASTRA_HOST_CHANNEL_SCOPE_MAX ||
        scope->references == UINT16_MAX)
        return ASTRA_SYSCALL_RESOURCE_LIMIT;
    /* The provider refuses a word already bound to this channel, which only
     * a second scope over the same session could be. */
    if (kernel_platform_host_channel_attach(
            channel->owner, channel->host_generation,
            channel->channel_generation, slot, scope->word,
            host_scope_wire_flags(scope), scope->creator_owner) !=
        ASTRA_SYSCALL_OK)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    channel->scopes[free_at] = host_scope_index(scope);
    ++scope->references;
    return ASTRA_SYSCALL_OK;
}

/*
 * The scope is unbound from every channel at once; the provider refuses
 * anything naming it that has not started by the time each detach returns.
 * The channels themselves stay open: their other scopes are unaffected.
 */
static uint32_t host_scope_revoke(KernelHostScope *scope)
{
    uint32_t result = ASTRA_SYSCALL_OK;
    uint8_t index = host_scope_index(scope);
    uint32_t word = scope->word;

    if (scope->state != KERNEL_HOST_SCOPE_ACTIVE)
        return ASTRA_SYSCALL_OK;
    scope->state = KERNEL_HOST_SCOPE_REVOKED;
    ++scope->references;    /* held while the bindings go */
    for (uint32_t slot = 0u; slot < KERNEL_VM_HOST_CHANNEL_PAGE_COUNT;
         ++slot) {
        KernelHostChannel *channel = &host_channels[slot];

        if (channel->active == 0u)
            continue;
        for (uint32_t at = 0u; at < ASTRA_HOST_CHANNEL_SCOPE_MAX; ++at) {
            if (channel->scopes[at] != index)
                continue;
            if (kernel_platform_host_channel_detach(
                    channel->owner, channel->host_generation,
                    channel->channel_generation, slot, word) !=
                ASTRA_SYSCALL_OK)
                result = ASTRA_SYSCALL_PEER_DEAD;
            channel->scopes[at] = 0u;
            host_scope_drop(scope);
        }
    }
    host_scope_drop(scope);
    return result;
}

/* Its creator is gone: so is what it granted. A restarted HostFS mints
 * again; nothing its predecessor issued survives it. */
static void host_scopes_revoke_creator(uint32_t owner)
{
    for (uint32_t index = 0u; index < KERNEL_HOST_SCOPE_MAX; ++index)
        if (host_scopes[index].state == KERNEL_HOST_SCOPE_ACTIVE &&
            host_scopes[index].creator_owner == owner)
            (void)host_scope_revoke(&host_scopes[index]);
}
```

`host_channel_close_slot` drops the channel's bindings after clearing it:

```c
    KernelHostScope *bound[ASTRA_HOST_CHANNEL_SCOPE_MAX];
    ...
    for (uint32_t at = 0u; at < ASTRA_HOST_CHANNEL_SCOPE_MAX; ++at)
        bound[at] = channel->scopes[at] != 0u ?
            &host_scopes[channel->scopes[at] - 1u] : NULL;
    kernel_bytes_clear(channel, sizeof(*channel));
    kernel_thread_wait_queue_init(&channel->waiters);
    for (uint32_t at = 0u; at < ASTRA_HOST_CHANNEL_SCOPE_MAX; ++at)
        host_scope_drop(bound[at]);
    return result;
```

`finish_reap` (`process.c:1653`), first statement of the
`handles_closed == 0u` block: `host_scopes_revoke_creator(process->owner);`.

`host_channel_open` gains `KernelHostScope *scope` (NULL: opened with the
device). Before `kernel_platform_host_channel_open`:

```c
    if (scope != NULL && (scope->state != KERNEL_HOST_SCOPE_ACTIVE ||
                          scope->references == UINT16_MAX)) {
        (void)kernel_dma_abort(&token);
        return ASTRA_SYSCALL_PEER_DEAD;
    }
```

The open passes `scope != NULL ? scope->word : 0u`,
`scope != NULL ? host_scope_wire_flags(scope) : 0u` and
`scope != NULL ? scope->creator_owner : 0u`. After the channel's other
fields are set:

```c
    host_channels[slot].owner = process->owner;
    host_channels[slot].bound = scope != NULL ? 1u : 0u;
    if (scope != NULL) {
        host_channels[slot].scopes[0] = host_scope_index(scope);
        ++scope->references;
    }
```

`host_syscall` gains `KernelHostScope *scope` and passes it to
`host_channel_open`. Its QUERY branch, after `maximum_commands`:

```c
        if (scope != NULL) {
            info.scope = scope->word;
            info.scope_flags = scope->flags & ASTRA_HOST_SCOPE_READ_ONLY;
        }
```

The dispatch of `HOST_QUERY`, `HOST_EXECUTE`, `HOST_CHANNEL_OPEN` and
`HOST_CHANNEL_CLOSE` (`process.c:12771`) takes the prototype's fallback:

```c
        KernelHostScope *scope = NULL;
        ...
        handle_status = kernel_handle_lookup(
            current->handles, thread->context.data[1], KERNEL_OBJECT_DEVICE,
            required_rights, (void **)&lease);
        /* A scope opens bound channels and answers queries; it never runs
         * the synchronous HOST_EXECUTE path. */
        if (handle_status == KERNEL_HANDLE_TYPE_MISMATCH &&
            syscall != ASTRA_SYSCALL_HOST_EXECUTE) {
            handle_status = kernel_handle_lookup(
                current->handles, thread->context.data[1],
                KERNEL_OBJECT_HOST_SCOPE, required_rights, (void **)&scope);
            if (handle_status == KERNEL_HANDLE_OK && scope != NULL) {
                if (scope->state != KERNEL_HOST_SCOPE_ACTIVE &&
                    syscall != ASTRA_SYSCALL_HOST_CHANNEL_CLOSE) {
                    result = ASTRA_SYSCALL_PEER_DEAD;
                    break;
                }
                lease = scope->lease;
            }
        }
```

The existing error mapping follows unchanged. Then
`result = host_syscall(current, thread, syscall, snapshot.generation, scope);`.

The three new cases, after that one:

```c
    case ASTRA_SYSCALL_HOST_SCOPE_CREATE: {
        KernelDeviceLease *lease = NULL;
        KernelDeviceSnapshot snapshot;
        KernelPlatformHostState state;
        KernelHandleStatus handle_status;
        KernelHostScope *scope = NULL;
        KernelHandle handle;
        uint32_t flags = thread->context.data[3];

        handle_status = kernel_handle_lookup(
            current->handles, thread->context.data[1], KERNEL_OBJECT_DEVICE,
            KERNEL_DEVICE_RIGHT_HOST_SCOPE, (void **)&lease);
        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
            handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || lease == NULL)
            return KERNEL_PROCESS_CORRUPT;
        if ((flags & ~(ASTRA_HOST_SCOPE_READ_ONLY |
                       ASTRA_HOST_SCOPE_DELEGATE)) != 0u) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        if (kernel_device_query(lease, &snapshot) != KERNEL_DEVICE_OK ||
            snapshot.device_id != ASTRA_DEVICE_ID_HOST0 ||
            snapshot.lease_state != ASTRA_DEVICE_LEASE_ACTIVE ||
            !kernel_platform_host_state(&state)) {
            result = ASTRA_SYSCALL_PEER_DEAD;
            break;
        }
        if ((state.capabilities & ASTRA_HOST_CAP_FS_SCOPE) == 0u) {
            result = ASTRA_SYSCALL_UNSUPPORTED;
            break;
        }
        for (uint32_t index = 0u; index < KERNEL_HOST_SCOPE_MAX; ++index)
            if (host_scopes[index].state == KERNEL_HOST_SCOPE_FREE &&
                host_scopes[index].references == 0u) {
                scope = &host_scopes[index];
                break;
            }
        if (scope == NULL || !kernel_device_handle_retain(lease, NULL)) {
            result = ASTRA_SYSCALL_RESOURCE_LIMIT;
            break;
        }
        scope->lease = lease;
        scope->word = thread->context.data[2];
        scope->flags = flags;
        scope->creator_owner = current->owner;
        scope->references = 1u;
        scope->state = KERNEL_HOST_SCOPE_ACTIVE;
        if (kernel_handle_install_cloneable(
                current->handles, KERNEL_OBJECT_HOST_SCOPE,
                ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_TRANSFER |
                    ASTRA_RIGHT_ADMINISTER,
                scope, host_scope_retain, host_scope_release, NULL,
                &handle) != KERNEL_HANDLE_OK) {
            host_scope_drop(scope);
            result = ASTRA_SYSCALL_RESOURCE_LIMIT;
            break;
        }
        thread->context.data[1] = handle;
        result = ASTRA_SYSCALL_OK;
        break;
    }
    case ASTRA_SYSCALL_HOST_SCOPE_REVOKE:
    case ASTRA_SYSCALL_HOST_SCOPE_ATTACH: {
        KernelHostScope *scope = NULL;
        KernelHandleStatus handle_status = kernel_handle_lookup(
            current->handles, thread->context.data[1],
            KERNEL_OBJECT_HOST_SCOPE,
            syscall == ASTRA_SYSCALL_HOST_SCOPE_REVOKE ?
                ASTRA_RIGHT_ADMINISTER : ASTRA_RIGHT_WRITE,
            (void **)&scope);
        uint32_t slot = KERNEL_VM_HOST_CHANNEL_PAGE_COUNT;

        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
            handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || scope == NULL)
            return KERNEL_PROCESS_CORRUPT;
        if (syscall == ASTRA_SYSCALL_HOST_SCOPE_REVOKE) {
            result = host_scope_revoke(scope);
            break;
        }
        if (scope->state != KERNEL_HOST_SCOPE_ACTIVE) {
            result = ASTRA_SYSCALL_PEER_DEAD;
            break;
        }
        for (uint32_t index = 0u; index < KERNEL_VM_HOST_CHANNEL_PAGE_COUNT;
             ++index)
            if (host_channels[index].active != 0u &&
                host_channels[index].process_id == current->id &&
                host_channels[index].thread_id == thread->id) {
                slot = index;
                break;
            }
        if (slot == KERNEL_VM_HOST_CHANNEL_PAGE_COUNT) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        /* A device channel answers to its device, not to scopes. */
        result = host_channels[slot].bound != 0u ?
            host_channel_attach(slot, scope) : ASTRA_SYSCALL_ACCESS_DENIED;
        break;
    }
```

`sw/kernel/platform.c` and `platform.h`. Take the prototype's three extra
parameters on `kernel_platform_host_channel_configure` and
`kernel_platform_host_channel_open`. `config.version` stays
`ASTRA_HOST_CHANNEL_CONFIG_VERSION`, which is now 2. Add:

```c
uint32_t kernel_platform_host_channel_attach(
    uint32_t owner, uint32_t host_generation, uint32_t channel_generation,
    uint32_t slot, uint32_t scope, uint32_t scope_flags, uint32_t scope_owner)
{
    return kernel_platform_host_channel_configure(
        ASTRA_HOST_CHANNEL_CONFIG_ATTACH, owner, host_generation,
        channel_generation, slot, 0u, 0u, 0u, scope, scope_flags,
        scope_owner);
}

uint32_t kernel_platform_host_channel_detach(
    uint32_t owner, uint32_t host_generation, uint32_t channel_generation,
    uint32_t slot, uint32_t scope)
{
    return kernel_platform_host_channel_configure(
        ASTRA_HOST_CHANNEL_CONFIG_DETACH, owner, host_generation,
        channel_generation, slot, 0u, 0u, 0u, scope, 0u, 0u);
}
```

Declare both in `platform.h`. Update the open call in
`sw/kernel/tests/test_platform.c:306` with `0u, 0u, 0u`, the prototype's
edit.

- [ ] **Step 5: Kernel tests pass**

On beast: `~/astra-mg/locked make -C sw/kernel clean && ~/astra-mg/locked make -C sw/kernel test`.
Expected: every kernel suite passes, `test_host_scope` among them. Then run
`~/astra-mg/locked make -C sw/kernel clean` again before any ROM build.

- [ ] **Step 6: The runtime wrappers**

`sw/userspace/runtime/src/syscall.c`, after `astra_audio_stream_wait`:

```c
uint32_t
astra_host_scope_create(uint32_t device, uint32_t word, uint32_t flags,
                        uint32_t *scope)
{
    AstraSyscallResult result;

    if (scope == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    *scope = 0u;
    astra_syscall5(ASTRA_SYSCALL_HOST_SCOPE_CREATE, device, word, flags, 0u,
                   0u, &result);
    if (result.status == ASTRA_SYSCALL_OK)
        *scope = result.value0;
    return result.status;
}

uint32_t
astra_host_scope_revoke(uint32_t scope)
{
    AstraSyscallResult result;

    astra_syscall5(ASTRA_SYSCALL_HOST_SCOPE_REVOKE, scope, 0u, 0u, 0u, 0u,
                   &result);
    return result.status;
}

uint32_t
astra_host_scope_attach(uint32_t scope)
{
    AstraSyscallResult result;

    astra_syscall5(ASTRA_SYSCALL_HOST_SCOPE_ATTACH, scope, 0u, 0u, 0u, 0u,
                   &result);
    return result.status;
}
```

`result.value0` is `data[1]`. The prototype's HELLO used it the same way
(`scope.value0`). Declare the three in `ndk/include/astra/runtime.h` after
`astra_host_channel_close`, with doc comments that repeat the syscall
contracts above.

- In `sw/userspace/runtime/runtime.library.exports`, append
  `ASTRA_RUNTIME_1.14 { global: astra_host_scope_attach; astra_host_scope_create; astra_host_scope_revoke; } ASTRA_RUNTIME_1.13;`
  in the file's layout.
- `runtime_library.c`: `"runtime.library.1", 1, 14, 0, 1, 14`.
- `sw/userspace/runtime/Makefile:172`:
  `'^runtime.library.1 1.14.0 ABI 1.14$$'`. Add
  `@$(READELF) -VW $< | grep 'ASTRA_RUNTIME_1.14' >/dev/null` after the
  1.13 line, if the 1.13 line is there. Otherwise add it after the 1.12
  line, following the precedent of `1707179a`.
- `sw/userspace/kits/Runtime.kit/manifest`: `version 1.14.0` and
  `provides runtime.library 1 1.14.0`.
- `sw/userspace/kits/Filesystem.kit/manifest`:
  `requires runtime.library 1 1.14.0`. The lane calls
  `astra_host_scope_attach`.

- [ ] **Step 7: The provider: failing qtest, then the binding**

`emu/qemu/test-hostfs.py`. Constants beside the others:

```python
CONFIG_VERSION = 2
CONFIG_OPEN = 1
CONFIG_CLOSE = 2
CONFIG_ATTACH = 3
CONFIG_DETACH = 4
SCOPE_BOUND = 1
SCOPE_READ_ONLY = 2
STATUS_READ_ONLY = 19
CAP_FS_SCOPE = 1 << 10
```

In `configure_channel`, write `CONFIG_VERSION` where it packs `1` after
`64`. In `run`, expect `0x0001000D` from `HACC_VERSION` and
`511 | CAP_FS_SCOPE` from `HACC_CAPS`. Then add:

```python
def configure_scope(qtest, generation, operation, slot, channel_generation,
                    scope=0, scope_flags=0, scope_owner=0, owner=0x1001,
                    buffer=BUFFER, byte_size=8192, capacity=2):
    """The kernel's channel configuration, version 2, with scope words."""
    config = bytearray(64)
    opening = operation == CONFIG_OPEN
    struct.pack_into(">IHHIIIIIIIIII", config, 0, 64, CONFIG_VERSION,
                     operation, slot, owner, generation, channel_generation,
                     buffer if opening else 0, byte_size if opening else 0,
                     capacity if opening else 0, scope, scope_flags,
                     scope_owner)
    qtest.write(CHANNEL_CONFIG, config)
    qtest.write32(HACC_CHANNEL_CONFIG, CHANNEL_CONFIG)
    return qtest.read32(HACC_CHANNEL_RESULT)


def scoped_post(qtest, slot, producer, command, buffer=BUFFER, capacity=2):
    """Puts `command` at producer position `producer` and kicks."""
    at = buffer + CHANNEL_HEADER_SIZE + \
        ((producer - 1) % capacity) * COMMAND_SIZE
    qtest.write(at, bytes(command))
    qtest.write32(buffer + 36, producer)
    qtest.write32(CHANNEL_APERTURE + slot * 4096 + 0x20, producer)
    return at


def scoped_wait(qtest, producer, at, buffer=BUFFER):
    qtest.wait32(buffer + 48, producer)
    return qtest.read(at, COMMAND_SIZE)


def scoped(qtest, slot, producer, command):
    return scoped_wait(qtest, producer, scoped_post(qtest, slot, producer,
                                                    command))


def scopes(qtest, root):
    """A bound channel acts only through the scopes the kernel attached: the
    command's volume picks one and an unattached volume is refused, other
    services are refused, a read-only scope mutates nothing, a handle answers
    only under the volume it was opened in, and malformed scope words are
    refused at configuration."""
    generation = qtest.read32(HACC_GENERATION)
    # Unbound OPEN carries no scope words; a bound one names a real volume
    # and a creator.
    assert configure_scope(qtest, generation, CONFIG_OPEN, 7, 21,
                           scope=VOLUME_SOUND) != 0
    assert configure_scope(qtest, generation, CONFIG_OPEN, 7, 21,
                           VOLUME_COUNT, SCOPE_BOUND, 0x2002) != 0
    assert configure_scope(qtest, generation, CONFIG_OPEN, 7, 21,
                           VOLUME_WORK, SCOPE_BOUND, 0) != 0
    assert configure_scope(qtest, generation, CONFIG_OPEN, 7, 21,
                           VOLUME_WORK, SCOPE_BOUND | 4, 0x2002) != 0
    assert configure_scope(qtest, generation, CONFIG_OPEN, 7, 21,
                           VOLUME_WORK, SCOPE_BOUND, 0x2002) == 0
    made = scoped(qtest, 7, 1, make_command(
        generation, FS_MKDIR, "/scoped-dir", value=0o755,
        volume=VOLUME_WORK))
    assert status(made) == STATUS_OK
    assert os.path.isdir(os.path.join(root, "work", "scoped-dir"))
    # A forged volume: SOUND is not attached to this channel.
    forged = scoped(qtest, 7, 2, make_command(
        generation, FS_MKDIR, "/forged-dir", value=0o755,
        volume=VOLUME_SOUND))
    assert status(forged) == STATUS_ACCESS
    assert not os.path.exists(os.path.join(root, "sound", "forged-dir"))
    entropy = scoped(qtest, 7, 3, make_command(
        generation, ENTROPY_FILL, service=SERVICE_ENTROPY,
        data_offset=CHANNEL_HEADER_SIZE + 2 * COMMAND_SIZE,
        data_capacity=32))
    assert status(entropy) == STATUS_ACCESS
    # Attach a read-only SOUND scope: the same channel now reaches it, and
    # only to read.
    assert configure_scope(qtest, generation, CONFIG_ATTACH, 7, 21,
                           VOLUME_SOUND, SCOPE_BOUND | SCOPE_READ_ONLY,
                           0x2002) == 0
    assert configure_scope(qtest, generation, CONFIG_ATTACH, 7, 21,
                           VOLUME_SOUND, SCOPE_BOUND, 0x2002) != 0
    refused = scoped(qtest, 7, 4, make_command(
        generation, FS_MKDIR, "/ro-dir", value=0o755, volume=VOLUME_SOUND))
    assert status(refused) == STATUS_READ_ONLY
    assert not os.path.exists(os.path.join(root, "sound", "ro-dir"))
    allowed = scoped(qtest, 7, 5, make_command(
        generation, FS_STAT, "/", volume=VOLUME_SOUND))
    assert status(allowed) == STATUS_OK
    # A handle belongs to the volume it was opened under.
    with open(os.path.join(root, "work", "scoped-file"), "wb") as out:
        out.write(b"scoped")
    opened = scoped(qtest, 7, 6, make_command(
        generation, FS_OPEN, "/scoped-file", flags=OPEN_READ,
        volume=VOLUME_WORK))
    assert status(opened) == STATUS_OK
    crossed = scoped(qtest, 7, 7, make_command(
        generation, FS_CLOSE, handle=get32(opened, 16),
        volume=VOLUME_SOUND))
    assert status(crossed) == STATUS_BAD_HANDLE
    closed = scoped(qtest, 7, 8, make_command(
        generation, FS_CLOSE, handle=get32(opened, 16), volume=VOLUME_WORK))
    assert status(closed) == STATUS_OK
    assert configure_scope(qtest, generation, CONFIG_DETACH, 7, 21,
                           VOLUME_SOUND) == 0
    detached = scoped(qtest, 7, 9, make_command(
        generation, FS_STAT, "/", volume=VOLUME_SOUND))
    assert status(detached) == STATUS_ACCESS
    assert configure_scope(qtest, generation, CONFIG_CLOSE, 7, 21) == 0
    # A device channel takes no scopes.
    assert configure_channel(qtest, generation, CONFIG_OPEN, slot=8,
                             channel_generation=22) == 0
    assert configure_scope(qtest, generation, CONFIG_ATTACH, 8, 22,
                           VOLUME_WORK, SCOPE_BOUND, 0x2002) != 0
    assert configure_channel(qtest, generation, CONFIG_CLOSE, slot=8,
                             channel_generation=22) == 0
    print("PASS scopes")


def scope_revocation(qtest, root):
    """What has started when the scope is detached finishes; what has not
    is refused. The machine runs one thread-pool worker
    (-object main-loop,thread-pool-max=1), so the second command waits
    behind the first, which a FIFO holds open."""
    qtest.detect_endian()
    generation = qtest.read32(HACC_GENERATION)
    fifo = os.path.join(root, "work", "held")
    os.mkfifo(fifo)
    assert configure_scope(qtest, generation, CONFIG_OPEN, 9, 31,
                           VOLUME_WORK, SCOPE_BOUND, 0x2002) == 0
    first = scoped_post(qtest, 9, 1, make_command(
        generation, FS_OPEN, "/held", flags=OPEN_READ, volume=VOLUME_WORK))
    time.sleep(0.5)
    assert qtest.read32(BUFFER + 48) == 0, \
        "the FIFO open did not hold the only worker"
    second = scoped_post(qtest, 9, 2, make_command(
        generation, FS_MKDIR, "/after-revoke", value=0o755,
        volume=VOLUME_WORK))
    assert configure_scope(qtest, generation, CONFIG_DETACH, 9, 31,
                           VOLUME_WORK) == 0
    writer = os.open(fifo, os.O_WRONLY)
    try:
        assert status(scoped_wait(qtest, 1, first)) == STATUS_OK
        assert status(scoped_wait(qtest, 2, second)) == STATUS_ACCESS
    finally:
        os.close(writer)
    assert not os.path.exists(os.path.join(root, "work", "after-revoke"))
    later = scoped(qtest, 9, 3, make_command(generation, FS_STAT, "/",
                                             volume=VOLUME_WORK))
    assert status(later) == STATUS_ACCESS
    assert configure_scope(qtest, generation, CONFIG_CLOSE, 9, 31) == 0
    print("PASS scope-revocation")
```

`main` boots twice, so move the `Popen` block into a helper (Task 11
reuses it):

```python
def booted(command, environment, qtest_path, body):
    if os.path.exists(qtest_path):
        os.unlink(qtest_path)
    with tempfile.TemporaryFile(mode="w+") as errors:
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                                   stderr=errors, text=True,
                                   env=environment)
        qtest = None
        try:
            qtest = QTest(qtest_path)
            body(qtest)
        finally:
            if qtest is not None:
                qtest.close()
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            if process.returncode not in (-15, 0):
                errors.seek(0)
                raise SystemExit(errors.read())
```

The first boot runs `run`, `volumes` and `scopes(qtest, root)`. The second
uses a fresh root (`root2`, with `work`/`sound`) and adds
`["-object", "main-loop,id=main-loop,thread-pool-max=1"]` to `command`. It
runs `scope_revocation(qtest, root2)`. Then `print("ASTRA QEMU HOSTFS PASS")`.
Before relying on the option, confirm that
`$Q -object main-loop,help` lists `thread-pool-max`. If it does not, stop
and report: the revocation check needs another way to hold the worker.

Run it now against the current QEMU:

```sh
ASTRA_QEMU_WORK_ROOT=$HOME/.cache/astra68/qemu-9.2.4 ~/astra-mg/locked emu/qemu/build.sh host
Q=$(tail -n1 /tmp/qemu-host-build.log)
~/astra-mg/locked python3 emu/qemu/test-hostfs.py $Q
```

Expected: `run` fails at `HACC_VERSION == 0x0001000D`. With that line
skipped by hand, `scopes` fails because the version-2 configuration is
refused. That is Red 1 of the investigation.

`emu/qemu/qemu-9.2/hw/m68k/astra68.c`:

State. Beside `AstraHostChannel`:

```c
/* A scope the kernel bound to a channel: the provider word (a volume until
 * command record 3), its rights, and its creator (FS15, FS44). */
typedef struct AstraHostChannelScope {
    uint32_t scope;
    uint32_t flags;          /* ASTRA_HOST_SCOPE_READ_ONLY */
    uint32_t owner;
} AstraHostChannelScope;
```

`AstraHostChannel` gains:

```c
    /* Opened on a scope: filesystem commands only, each through one of
     * `scopes` (vCPU writes under files_lock; workers read under it). */
    bool bound;
    uint32_t scope_count;
    AstraHostChannelScope scopes[ASTRA_HOST_CHANNEL_SCOPE_MAX];
```

`AstraHostState` gains `uint64_t scoped_commands;`, and `AstraHostFile`
gains `uint32_t volume;  /* the volume it was opened under */`.

Helpers, before `astra_host_channel_configure`:

```c
/* The configuration's scope words, valid for `operation`: none for CLOSE,
 * DETACH names only the word, an OPEN is unbound (no words) or bound
 * (BOUND, a volume this machine has, a creator), and ATTACH is bound. */
static bool astra_host_config_scope(Astra68State *s, const uint8_t *config,
                                    uint16_t operation,
                                    AstraHostChannelScope *scope)
{
    uint32_t flags = ldl_be_p(config + HOST_CHANNEL_CONFIG_FIELD(scope_flags));

    scope->scope = ldl_be_p(config + HOST_CHANNEL_CONFIG_FIELD(scope));
    scope->owner = ldl_be_p(config + HOST_CHANNEL_CONFIG_FIELD(scope_owner));
    scope->flags = flags & ASTRA_HOST_SCOPE_READ_ONLY;
    switch (operation) {
    case ASTRA_HOST_CHANNEL_CONFIG_CLOSE:
        return scope->scope == 0 && flags == 0 && scope->owner == 0;
    case ASTRA_HOST_CHANNEL_CONFIG_DETACH:
        return flags == 0 && scope->owner == 0;
    case ASTRA_HOST_CHANNEL_CONFIG_OPEN:
        if (flags == 0)
            return scope->scope == 0 && scope->owner == 0;
        /* fall through */
    case ASTRA_HOST_CHANNEL_CONFIG_ATTACH:
        return (flags & ASTRA_HOST_SCOPE_BOUND) != 0 &&
               (flags & ~(ASTRA_HOST_SCOPE_BOUND |
                          ASTRA_HOST_SCOPE_READ_ONLY)) == 0 &&
               scope->owner != 0 &&
               scope->scope < ASTRA_HOST_FS_VOLUME_COUNT &&
               s->host.volume_fds[scope->scope] >= 0;
    default:
        return false;
    }
}

/* ATTACH or DETACH one scope on an open bound channel. One scope per word:
 * a second ATTACH of a word already bound is refused. */
static uint32_t astra_host_channel_rebind(Astra68State *s,
                                          AstraHostChannel *channel,
                                          uint16_t operation,
                                          const AstraHostChannelScope *scope)
{
    uint32_t result = ASTRA_SYSCALL_INVALID_ARGUMENT;

    qemu_mutex_lock(&s->host.files_lock);
    for (uint32_t at = 0; at < channel->scope_count; ++at) {
        if (channel->scopes[at].scope != scope->scope)
            continue;
        if (operation == ASTRA_HOST_CHANNEL_CONFIG_DETACH) {
            channel->scopes[at] = channel->scopes[--channel->scope_count];
            result = ASTRA_SYSCALL_OK;
        }
        goto out;
    }
    if (operation == ASTRA_HOST_CHANNEL_CONFIG_ATTACH &&
        channel->scope_count < ASTRA_HOST_CHANNEL_SCOPE_MAX) {
        channel->scopes[channel->scope_count++] = *scope;
        result = ASTRA_SYSCALL_OK;
    }
out:
    qemu_mutex_unlock(&s->host.files_lock);
    return result;
}

/* The scope `word` names on `channel`, copied out under files_lock. */
static bool astra_host_channel_scope(Astra68State *s,
                                     AstraHostChannel *channel,
                                     uint32_t word,
                                     AstraHostChannelScope *out)
{
    bool found = false;

    qemu_mutex_lock(&s->host.files_lock);
    for (uint32_t at = 0; at < channel->scope_count; ++at)
        if (channel->scopes[at].scope == word) {
            *out = channel->scopes[at];
            found = true;
            break;
        }
    qemu_mutex_unlock(&s->host.files_lock);
    return found;
}
```

`astra_host_channel_configure`:
- After the fields are read, declare `AstraHostChannelScope scope;`. Then
  `if (!astra_host_config_scope(s, config, operation, &scope)) return;`.
  The reserved-bytes loop already covers only the four words left, because
  it iterates over `sizeof(reserved)`.
- After the CLOSE branch's `astra_host_channel_drain(channel);`, clear the
  binding:

  ```c
          qemu_mutex_lock(&s->host.files_lock);
          channel->bound = false;
          channel->scope_count = 0;
          qemu_mutex_unlock(&s->host.files_lock);
  ```

- Before the OPEN validation:

  ```c
      if (operation == ASTRA_HOST_CHANNEL_CONFIG_ATTACH ||
          operation == ASTRA_HOST_CHANNEL_CONFIG_DETACH) {
          if (physical_buffer != 0 || byte_size != 0 ||
              command_capacity != 0 || !channel->active || !channel->bound ||
              channel->owner != owner ||
              channel->host_generation != host_generation ||
              channel->channel_generation != channel_generation) {
              s->host.channel_result = channel->active ?
                  ASTRA_SYSCALL_ACCESS_DENIED : ASTRA_SYSCALL_PEER_DEAD;
              return;
          }
          s->host.channel_result =
              astra_host_channel_rebind(s, channel, operation, &scope);
          return;
      }
  ```

- Where OPEN sets `channel->owner = owner;`:

  ```c
      qemu_mutex_lock(&s->host.files_lock);
      channel->bound = scope.owner != 0;
      channel->scope_count = channel->bound ? 1 : 0;
      channel->scopes[0] = scope;
      qemu_mutex_unlock(&s->host.files_lock);
  ```

`astra_host_execute_command`, first statement, from the prototype:

```c
    if (channel != NULL && channel->bound &&
        lduw_be_p(command + HOST_FIELD(service)) !=
            ASTRA_HOST_SERVICE_FILESYSTEM) {
        stl_be_p(command + HOST_FIELD(status), ASTRA_STATUS_ACCESS);
        return;
    }
```

and its last branch passes `channel` to `astra_host_execute_fs`, which
gains the parameter (`AstraHostChannel *channel` after `owner`; NULL from
`HOST_EXECUTE` submissions). In `astra_host_execute_fs`, after the
validation that ends with `volume >= ASTRA_HOST_FS_VOLUME_COUNT` and before
`root = s->host.volume_fds[volume];`:

```c
    /* A bound channel acts only through a scope the kernel attached: the
     * command's volume picks one, its owner is the channel's, and a
     * read-only scope mutates nothing (FS15, FS44). */
    if (channel != NULL && channel->bound) {
        AstraHostChannelScope scope;

        if (!astra_host_channel_scope(s, channel, volume, &scope)) {
            status = ASTRA_STATUS_ACCESS;
            qatomic_inc(&s->host.operation_counts[0]);
            goto done;
        }
        if ((scope.flags & ASTRA_HOST_SCOPE_READ_ONLY) != 0 &&
            astra_host_fs_mutates(operation, flags)) {
            status = ASTRA_STATUS_READ_ONLY;
            qatomic_inc(&s->host.operation_counts[0]);
            goto done;
        }
        qatomic_inc(&s->host.scoped_commands);
    }
```

`astra_host_fs_mutates` is the prototype's function, verbatim, before
`astra_host_execute_fs`. Task 4 rewrites it for record 3's operation set.

Handles carry their volume:
- `astra_host_insert_file(s, owner, volume, fd, directory, flags)` stores
  it.
- `astra_host_publish_open(s, owner, volume, command, fd, flags)` passes it
  through.
- `astra_host_file_lock(s, owner, volume, handle)` answers NULL, with the
  file released, when `file->volume != volume`.
- `astra_host_close_file(s, owner, volume, handle)` refuses a mismatch the
  same way.

Update the twelve `astra_host_file_lock` calls and the two
`astra_host_publish_open` calls in `astra_host_execute_fs`. All have
`volume` in scope (`grep -n "astra_host_file_lock(\|astra_host_publish_open(\|astra_host_close_file(" emu/qemu/qemu-9.2/hw/m68k/astra68.c`).
A handle used under another volume answers `ASTRA_STATUS_BAD_HANDLE`, as an
unknown one does. Task 4 renames the tag to the session.

The worker drops a job whose channel has closed or been regenerated before
the job starts. That is the prototype's check at the top of
`astra_host_channel_worker`, verbatim.

The capability register (`0x888`) adds `ASTRA_HOST_CAP_FS_SCOPE`. Beside
the other host properties in `astra68_init`:

```c
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-host-scoped-commands",
                                   &s->host.scoped_commands,
                                   OBJ_PROP_FLAG_READ);
```

Do not take the prototype's `ASTRA_HOST_INLINE_SCOPED` experiment. Task
12b builds the real version.

Rebuild QEMU and rerun `test-hostfs.py`. Expected: `PASS scopes`,
`PASS scope-revocation`, `ASTRA QEMU HOSTFS PASS`.

- [ ] **Step 8: Restore the lane, scoped**

Take the pre-27 files. They are exactly what `62b72fc2` changed, and
`6d4e6a2c` did not touch them:

```sh
git checkout 60f2d30a -- \
  sw/userspace/vfs/src/vfs_host_direct.c sw/userspace/vfs/include/astra/vfs_host_direct.h \
  sw/userspace/vfs/Makefile sw/userspace/vfs/src/vfs_client.c ndk/include/astra/vfs_client.h \
  ndk/tests/headers.c sw/userspace/vfs/src/vfs_port_transport.c \
  sw/userspace/vfs/include/astra/vfs_port_transport.h sw/userspace/vfs/src/vfs_process.c \
  sw/userspace/vfs/tests/test_vfs_port.c sw/userspace/vfs/tests/test_vfs_process.c \
  sw/userspace/supervisor/src/loader.c sw/userspace/supervisor/src/vfs_host.c \
  sw/userspace/services/hostfs/main.c sw/userspace/services/hostfs/Makefile \
  emu/qemu/astra_image.py emu/qemu/test-astra-image-providers.py
```

Keep the `HEAD` versions of everything else that `62b72fc2` and `6d4e6a2c`
touched:
- the vfs_service.h version line (edited below);
- the Kit-version files (Step 11);
- hostbench;
- `benchmark-hostbench.py`;
- `test-host-channel-userspace.py`;
- the docs (Step 16).

`git diff 60f2d30a -- <those files>` shows that `loader.c` and `vfs_host.c`
differ only by the connect call, and `astra_image.py` only by the HostFS
line and the settings file.

Then the modifications.

`sw/userspace/vfs/include/astra/vfs_host_backend.h`: `AstraVfsHostBackend`
gains, after `volume`:

```c
    /* The scope handle commands go through (HOST_SCOPE_ATTACH on first use
     * by a lane); 0: the transport's own device handle. */
    uint32_t scope;
```

`astra_vfs_host_init` zeroes it. `host.c` `command_begin` passes it:
`astra_vfs_host_transport_begin(backend->transport, backend->scope, data_capacity, request)`.

`sw/userspace/vfs/include/astra/vfs_host_transport.h`:
- `AstraVfsHostLane`'s `uint8_t reserved[3];` becomes
  `uint8_t scope_count; uint8_t reserved[2];`, and the lane gains
  `uint32_t scopes[ASTRA_HOST_CHANNEL_SCOPE_MAX];`, the scope handles this
  lane's channel was opened on or has attached.
- `astra_vfs_host_transport_begin` gains `uint32_t scope` after `context`.

`sw/userspace/vfs/src/vfs_host_transport.c`:

```c
static int lane_carries(const AstraVfsHostLane *lane, uint32_t scope)
{
    if (scope == 0u)
        return 1;
    for (uint32_t at = 0u; at < lane->scope_count; ++at)
        if (lane->scopes[at] == scope)
            return 1;
    return 0;
}

/* A thread has one channel (one doorbell page per thread slot), so every
 * host-volume client of the process shares it: each scope is attached the
 * first time the lane carries it. A full cache only costs the syscall,
 * which the kernel answers OK for a scope already bound. */
static uint32_t lane_attach(AstraVfsHostLane *lane, uint32_t scope)
{
    uint32_t status;

    if (lane_carries(lane, scope))
        return ASTRA_VFS_OK;
    status = astra_host_scope_attach(scope);
    if (status != ASTRA_SYSCALL_OK)
        return astra_vfs_host_status_from_syscall(status);
    if (lane->scope_count < ASTRA_HOST_CHANNEL_SCOPE_MAX)
        lane->scopes[lane->scope_count++] = scope;
    return ASTRA_VFS_OK;
}
```

- `lane_open(transport, lane, thread, scope)` opens with
  `scope != 0u ? scope : transport->device`. On success it sets
  `lane->scope_count = 0u;` and, when `scope != 0u`, records
  `lane->scopes[lane->scope_count++] = scope;`.
- `lane_allocate`, `lane_resize`, `ensure_lane` and `lane_get_slow` gain
  `uint32_t scope` and pass it down. `lane_resize` closes with
  `astra_host_channel_close(scope != 0u ? scope : transport->device)`. A
  reopened channel carries only the scope it was opened on, and
  `lane_open` resets the list.
- `astra_vfs_host_transport_begin(context, scope, data_capacity, request)`:
  the cached fast path also requires `lane_carries(lane_cache.lane, scope)`.
  After either path has a lane,
  `status = lane_attach(lane, scope); if (status != ASTRA_VFS_OK) return status;`.
  The internal call in `astra_vfs_host_transport_execute`
  (`vfs_host_transport.c:517`) passes `0u`.
- `astra_vfs_host_transport_after_fork` also forgets the lanes:
  `memset(transport->lanes, 0, sizeof(transport->lanes));`. A fork child
  holds neither the parent's channels nor its DMA handles.
- `astra_vfs_host_transport_init` requires `ASTRA_HOST_CAP_FS_SCOPE` beside
  `ASTRA_HOST_CAP_FILESYSTEM | ASTRA_HOST_CAP_OWNER_SCOPED`.

`sw/userspace/vfs/tests/test_vfs_host_transport.c`:
- Every `astra_vfs_host_transport_begin(&transport, X, &request)` gains
  `0u` as its second argument.
- The capability lines need `| ASTRA_HOST_CAP_FS_SCOPE`. The line that
  expects init to fail keeps `FILESYSTEM | OWNER_SCOPED` without it.
- The channel-open mock accepts device `41u` or `77u`, and records it in
  `static uint32_t last_open_device;`. The close mock does the same.
- Add a mock and a test, run last in `main`:

```c
static uint32_t scope_attaches;
static uint32_t last_attached;

uint32_t astra_host_scope_attach(uint32_t scope)
{
    ++scope_attaches;
    last_attached = scope;
    return ASTRA_SYSCALL_OK;
}

/* One lane per thread carries every scope its process uses: the first
 * opens the channel, the next attaches once, and device commands neither. */
static void test_lane_carries_scopes(void)
{
    AstraVfsHostTransport transport;
    AstraVfsHostRequest request;

    capabilities = ASTRA_HOST_CAP_FILESYSTEM | ASTRA_HOST_CAP_OWNER_SCOPED |
                   ASTRA_HOST_CAP_FS_SCOPE;
    assert(astra_vfs_host_transport_init(&transport, 41u, acquire, release,
                                         &lock_depth));
    assert(astra_vfs_host_transport_begin(&transport, 77u, 0u, &request) ==
           ASTRA_VFS_OK);
    assert(last_open_device == 77u && scope_attaches == 0u);
    request.command->generation = transport.generation;
    assert(astra_vfs_host_transport_submit(&transport, &request, NULL, 0u,
                                           NULL, 0u) == ASTRA_VFS_OK);
    assert(astra_vfs_host_transport_begin(&transport, 78u, 0u, &request) ==
           ASTRA_VFS_OK);
    assert(scope_attaches == 1u && last_attached == 78u);
    request.command->generation = transport.generation;
    assert(astra_vfs_host_transport_submit(&transport, &request, NULL, 0u,
                                           NULL, 0u) == ASTRA_VFS_OK);
    assert(astra_vfs_host_transport_begin(&transport, 78u, 0u, &request) ==
           ASTRA_VFS_OK);
    assert(scope_attaches == 1u);
    request.command->generation = transport.generation;
    assert(astra_vfs_host_transport_submit(&transport, &request, NULL, 0u,
                                           NULL, 0u) == ASTRA_VFS_OK);
    astra_vfs_host_transport_destroy(&transport);
}
```

If the mock's thread or DMA bookkeeping needs a fresh state between tests,
call the same reset the file's earlier blocks use before a second `init`.
Find it with `grep -n "dma_slot_count = \|channel_opened" sw/userspace/vfs/tests/test_vfs_host_transport.c`.
`test_vfs_host_backend.c`'s stub of `astra_vfs_host_transport_begin` gains
the parameter and asserts `scope == 0u`. Its backend is initialised without
one.

`sw/userspace/vfs/src/vfs_host_direct.c`. The request translator
(`astra_vfs_host_direct_transport`, the bulk path and their helpers) stays
as restored. The connection functions change around one process-wide
transport. Replace the struct, the statics and the five lifecycle functions
(`connect`, `resume`, `after_fork`, `disconnect`, `abandon`):

```c
typedef struct AstraVfsHostDirect {
    uint32_t magic;
    uint32_t version;
    uint32_t byte_size;
    uint32_t generation;     /* the provider generation the lane began in */
    AstraVfsHostBackend backend;
    char staged_path[ASTRA_VFS_PATH_MAX];
    uint8_t staged_operation;
} AstraVfsHostDirect;

#define ASTRA_VFS_HOST_DIRECT_MAGIC UINT32_C(0x56484452)
#define ASTRA_VFS_HOST_DIRECT_VERSION 7u

/*
 * One host transport per process. A thread has one host channel, so every
 * host-volume client of the process shares the thread's lane; each client
 * names its own scope per command (AstraVfsHostBackend.scope), and the lane
 * attaches it on first use. The first scope connected answers HOST_QUERY for
 * the transport.
 */
static AstraVfsHostTransport lane_transport;
static uint32_t lane_users;
static uint32_t lane_lock __attribute__((aligned(4)));
static uint32_t lane_setup __attribute__((aligned(4)));

static uint32_t lane_acquire(uint32_t scope, uint32_t *generation)
{
    uint32_t status = ASTRA_VFS_OK;

    (void)astra_vfs_state_lock_acquire(&lane_setup);
    if (lane_users == 0u &&
        !astra_vfs_host_transport_init(&lane_transport, scope,
                                       astra_vfs_state_lock_acquire,
                                       astra_vfs_state_lock_release,
                                       &lane_lock))
        status = ASTRA_VFS_ERR_UNSUPPORTED;
    if (status == ASTRA_VFS_OK) {
        ++lane_users;
        *generation = lane_transport.generation;
    }
    astra_vfs_state_lock_release(&lane_setup);
    return status;
}

static void lane_release(void)
{
    (void)astra_vfs_state_lock_acquire(&lane_setup);
    if (lane_users != 0u && --lane_users == 0u)
        astra_vfs_host_transport_destroy(&lane_transport);
    astra_vfs_state_lock_release(&lane_setup);
}

/* A fork child: the parent's channels and DMA handles did not come across.
 * Its clients keep their scopes and open new lanes on first use. Called
 * once, before any client's astra_vfs_host_direct_after_fork. */
void astra_vfs_host_direct_fork_child(void)
{
    astra_vfs_host_transport_after_fork(&lane_transport);
    lane_lock = 0u;
    lane_setup = 0u;
}

static void client_clear(AstraVfsClient *client)
{
    client->port_direct_address = NULL;
    client->port_direct_area = 0u;
    client->port_direct_scope = 0u;
    client->port_direct_session = ASTRA_VFS_SESSION_INVALID;
    client->port_direct_detached = 0u;
    client->direct_backend_ops = NULL;
    client->direct_backend_context = NULL;
    client->direct_backend_enter = NULL;
    client->direct_backend_leave = NULL;
}

static void client_bind(AstraVfsClient *client, AstraVfsHostDirect *direct,
                        uint32_t area, uint32_t scope, uint32_t session)
{
    client->port_direct_address = direct;
    client->port_direct_area = area;
    client->port_direct_scope = scope;
    client->port_direct_session = session;
    client->port_direct_detached = 0u;
    client->direct_backend_ops = astra_vfs_host_ops();
    client->direct_backend_context = &direct->backend;
    client->direct_backend_enter = astra_vfs_port_client_enter;
    client->direct_backend_leave = astra_vfs_port_client_leave;
}

uint32_t astra_vfs_host_direct_connect(AstraVfsClient *client,
                                       uint32_t scope)
{
    AstraHostLeaseInfo lease = {0};
    AstraVfsHostDirect *direct;
    void *address = NULL;
    uint32_t generation = 0u;
    uint32_t area = 0u;
    uint32_t span = 0u;
    uint32_t status;

    if (client == NULL || scope == 0u || client->port_direct_address != NULL)
        return ASTRA_VFS_ERR_INVALID;
    /* The kernel says which session this scope binds; the client only
     * names it back, and the provider refuses any other. */
    if (astra_host_lease_query(scope, &lease) != ASTRA_SYSCALL_OK ||
        (lease.capabilities & ASTRA_HOST_CAP_FS_SCOPE) == 0u)
        return ASTRA_VFS_ERR_UNSUPPORTED;
    status = lane_acquire(scope, &generation);
    if (status != ASTRA_VFS_OK)
        return status;
    status = ASTRA_VFS_ERR_LIMIT;
    if (astra_rt_area_create_flagged(
            (uint32_t)sizeof(AstraVfsHostDirect),
            ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP,
            ASTRA_AREA_CREATE_RESERVED, &area) != ASTRA_SYSCALL_OK ||
        astra_rt_area_map(area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                          &address, &span) != ASTRA_SYSCALL_OK ||
        address == NULL || span < sizeof(AstraVfsHostDirect))
        goto fail;
    direct = address;
    memset(direct, 0, sizeof(*direct));
    if (!astra_vfs_host_init(&direct->backend, &lane_transport, generation,
                             lease.scope)) {
        status = ASTRA_VFS_ERR_INVALID;
        goto fail;
    }
    direct->backend.scope = scope;
    direct->magic = ASTRA_VFS_HOST_DIRECT_MAGIC;
    direct->version = ASTRA_VFS_HOST_DIRECT_VERSION;
    direct->byte_size = span;
    direct->generation = generation;
    client_bind(client, direct, area, scope, 1u);
    return ASTRA_VFS_OK;

fail:
    if (address != NULL)
        (void)astra_rt_area_unmap(address);
    if (area != 0u)
        (void)astra_close(area);
    lane_release();
    return status;
}

static int direct_state_valid(const AstraVfsHostDirect *direct, uint32_t span,
                              uint32_t session)
{
    return direct->magic == ASTRA_VFS_HOST_DIRECT_MAGIC &&
           direct->version == ASTRA_VFS_HOST_DIRECT_VERSION &&
           direct->byte_size == span && session == 1u &&
           direct->staged_operation <= ASTRA_VFS_STAGE_SYMLINK;
}

/* After an in-place exec: the area and the scope handle survived, the lanes
 * did not (exec closed the channels). The provider generation must not have
 * moved, or the files the old image opened are gone. */
uint32_t astra_vfs_host_direct_resume(AstraVfsClient *client, uint32_t area,
                                      uint32_t scope, uint32_t session)
{
    AstraVfsHostDirect *direct;
    void *address = NULL;
    uint32_t span = 0u;
    uint32_t generation = 0u;

    if (client == NULL || area == 0u || scope == 0u ||
        session == ASTRA_VFS_SESSION_INVALID ||
        client->port_direct_address != NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (astra_rt_area_map(area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                          &address, &span) != ASTRA_SYSCALL_OK ||
        address == NULL || span < sizeof(AstraVfsHostDirect)) {
        if (address != NULL)
            (void)astra_rt_area_unmap(address);
        (void)astra_close(area);
        (void)astra_close(scope);
        return ASTRA_VFS_ERR_BAD_HANDLE;
    }
    direct = address;
    if (!direct_state_valid(direct, span, session) ||
        lane_acquire(scope, &generation) != ASTRA_VFS_OK)
        goto invalid;
    if (generation != direct->generation) {
        lane_release();
        goto invalid;
    }
    direct->backend.transport = &lane_transport;
    direct->backend.scope = scope;
    client_bind(client, direct, area, scope, session);
    client->port_accelerator_ops = &host_accelerator_ops;
    return ASTRA_VFS_OK;

invalid:
    (void)astra_rt_area_unmap(address);
    (void)astra_close(area);
    (void)astra_close(scope);
    return ASTRA_VFS_ERR_INVALID;
}

/* A fork child's client: drop the parent's lane state and port session,
 * keep the inherited scope (the same grant's), HELLO again lazily, and
 * drive a lane of the child's own on that scope. */
uint32_t astra_vfs_host_direct_after_fork(AstraVfsClient *client)
{
    AstraVfsHostDirect *direct;
    uint32_t activity;
    uint32_t area;
    uint32_t scope;
    uint32_t service;
    uint32_t status;

    if (client == NULL || client->port_direct_address == NULL ||
        client->port_direct_area == 0u || client->port_direct_scope == 0u)
        return ASTRA_VFS_ERR_INVALID;
    activity = client->activity;
    direct = client->port_direct_address;
    area = client->port_direct_area;
    scope = client->port_direct_scope;
    service = client->port_service;
    client_clear(client);
    (void)astra_rt_area_unmap(direct);
    (void)astra_close(area);
    lane_release();
    client->port_accelerator_ops = NULL;
    astra_vfs_port_abandon(client);
    status = astra_vfs_host_port_connect_lazy(client, service);
    if (status != ASTRA_VFS_OK) {
        (void)astra_close(scope);
        return status;
    }
    client->activity = activity;
    status = astra_vfs_host_direct_connect(client, scope);
    if (status != ASTRA_VFS_OK) {
        (void)astra_close(scope);
        return status;
    }
    client->session = client->port_direct_session;
    client->port_direct_detached = 1u;
    return ASTRA_VFS_OK;
}

void astra_vfs_host_direct_disconnect(AstraVfsClient *client)
{
    void *address;
    uint32_t area;
    uint32_t scope;

    if (client == NULL || client->port_direct_address == NULL)
        return;
    address = client->port_direct_address;
    area = client->port_direct_area;
    scope = client->port_direct_scope;
    client_clear(client);
    (void)astra_rt_area_unmap(address);
    (void)astra_close(area);
    (void)astra_close(scope);
    lane_release();
}

void astra_vfs_host_direct_abandon(AstraVfsClient *client)
{
    astra_vfs_host_direct_disconnect(client);
}
```

`abandon` and `disconnect` were two functions only because the old one
destroyed a per-client transport with I/O. Nothing here does I/O, so one
calls the other. A process that stops using every host volume destroys the
transport, which closes the lanes' DMA handles, and the kernel closes their
channels with them (`host_channels_close_buffer`). Add `#include <astra/host.h>`
for `AstraHostLeaseInfo`.

`sw/userspace/vfs/include/astra/vfs_host_direct.h`: rename the `device`
parameters to `scope`, and declare
`void astra_vfs_host_direct_fork_child(void);`.

`ndk/include/astra/vfs_client.h`:
- Rename `port_direct_device` to `port_direct_scope`, with the comment
  `/* the grant's scope handle (Kit 6); the lane names it per command */`.
- Delete `port_direct_lock`: the lock is the process-wide transport's now.
- `AstraVfsPortAcceleratorOps.connect`'s second parameter becomes
  `uint32_t scope`, with the doc line
  "Establish the lane on a scope a HELLO attached."

`ndk/tests/headers.c`: drop the `port_direct_lock` assertion, as
`62b72fc2` did, and expect `ASTRA_FILESYSTEM_LIBRARY_VERSION == 6u`.

`sw/userspace/vfs/src/vfs_process.c`:
- `PROCESS_VFS_EXEC_VERSION` 4 -> 6. Do not reuse 5: `62b72fc2` gave it a
  different layout.
- Rename `port_direct_device` to `port_direct_scope` in
  `ProcessVfsExecClient`, the export, the validity terms and the
  `astra_vfs_host_direct_resume` call.
- In the fork-child branch of `seed_process_vfs`, call
  `astra_vfs_host_direct_fork_child();` once, before the loop that calls
  `astra_vfs_host_direct_after_fork` per client.

- [ ] **Step 9: The port layer hands out scopes; the relay goes**

`sw/userspace/vfs/include/astra/vfs_port_transport.h` (restored):
- `AstraVfsPortService.accelerator` becomes
  `uint32_t scope;  /* the grant's scope; nonzero: a control-only port */`.
- `astra_vfs_port_service_set_accelerator` becomes
  `int astra_vfs_port_service_set_scope(AstraVfsPortService *host, uint32_t scope);`.
- `astra_vfs_port_connect_with_accelerator` and `..._lazy_with_accelerator`
  stay, with an `AstraVfsPortAcceleratorOps` whose `connect` takes the
  scope.

`sw/userspace/vfs/src/vfs_port_transport.c`, service side:
- `astra_vfs_port_service_set_scope` stores the scope where
  `set_accelerator` stored the device (`host->scope = scope; return scope != 0u;`).
  `astra_vfs_port_service_init` clears it.
- The HELLO attachment (pre-27 `vfs_port_transport.c:2332-2342`) becomes:

  ```c
          /* A scoped port answers HELLO with the grant's scope: a
           * capability that reaches that scope and nothing else, never the
           * device (FS44). It travels without ADMINISTER, and the kernel
           * strips TRANSFER on arrival unless the scope is delegable. */
          if (wire_operation == ASTRA_VFS_OP_HELLO &&
              incoming->request.version >= UINT16_C(28) &&
              host->scope != 0u &&
              astra_rt_handle_duplicate(
                  host->scope,
                  ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                      ASTRA_RIGHT_TRANSFER,
                  &outgoing_handles[0]) == ASTRA_SYSCALL_OK) {
              outgoing_handle_count = 1u;
              scoped = 1;
          }
  ```

  Rename the local `accelerated` to `scoped`, including in
  `} else if (fused_hello && scoped) {`: a fused operation on a scoped
  port is answered with the HELLO alone, and the client runs it on its
  lane.
- There is no relay. Just after the activity adoption, before the
  operation branches:

  ```c
          /* The data plane is the client's own lane; a scoped port is
           * control only (HELLO, BYE; GRANT_DERIVE from version 29). */
          if (host->scope != 0u && wire_operation != ASTRA_VFS_OP_HELLO &&
              wire_operation != ASTRA_VFS_OP_BYE) {
              outgoing->reply.status = ASTRA_VFS_ERR_UNSUPPORTED;
              goto answer_ready;
          }
  ```

  If the pump's answer label has another name, use the label the existing
  refusals jump to (`grep -n "goto answer" sw/userspace/vfs/src/vfs_port_transport.c`).

Client side: in the reply check, the HELLO exception's `UINT16_C(18)`
becomes `UINT16_C(28)`. A reply handle on anything but a version-28 HELLO is
a protocol error, as it is today.

`sw/userspace/vfs/tests/test_vfs_port.c` (restored):
- Rename `astra_vfs_port_service_set_accelerator` to
  `astra_vfs_port_service_set_scope` and the local `accelerator` to
  `scope`.
- Count mkdir calls in the fake backend. Add
  `static uint32_t backend_mkdir_calls;` and `++backend_mkdir_calls;` at the
  top of `backend_mkdir`, and reset it in `mock_reset`. Task 5 uses it too.
- Replace any test that asserts a HELLO carries no handle with:

```c
/*
 * Version 28: a scoped port answers HELLO with a session and one duplicate
 * of its scope, never the device; a port with no scope attaches nothing.
 */
static void
test_hello_attaches_the_scope_and_never_the_device(void)
{
    AstraVfsPortService host;
    AstraVfsRequestMessage message;
    AstraVfsReplyMessage reply;
    uint32_t handles[1] = {0u};
    uint32_t size = 0u;
    uint32_t count = 0u;

    for (int scoped = 0; scoped < 2; ++scoped) {
        uint32_t service_handle;
        uint32_t reply_handle;
        uint32_t scope;

        mock_reset();
        service_start();
        service_handle = mock_open(MOCK_QUEUE_MAX);
        reply_handle = mock_open(1u);
        scope = mock_open(1u);
        assert(astra_vfs_port_service_init(&host, service_handle, &service));
        if (scoped)
            assert(astra_vfs_port_service_set_scope(&host, scope));
        memset(&message, 0, sizeof(message));
        astra_message_header_set(&message.header, (uint32_t)sizeof(message),
                                 ASTRA_VFS_PROTOCOL, ASTRA_VFS_VERSION,
                                 ASTRA_VFS_OP_HELLO, 1u);
        message.request.size = ASTRA_VFS_REQUEST_SIZE;
        message.request.version = ASTRA_VFS_VERSION;
        handles[0] = reply_handle;
        assert(astra_port_send(service_handle, &message, sizeof(message),
                               handles, 1u) == ASTRA_SYSCALL_OK);
        assert(astra_vfs_port_service_pump(&host, 1u) == 1u);
        handles[0] = 0u;
        assert(astra_port_receive(reply_handle, &reply, sizeof(reply),
                                  handles, 1u, &size, &count) ==
               ASTRA_SYSCALL_OK);
        assert(reply.reply.status == ASTRA_VFS_OK);
        assert(reply.reply.version == UINT16_C(28));
        assert(reply.reply.session != ASTRA_VFS_SESSION_INVALID);
        assert(count == (scoped ? 1u : 0u));
        /* The mock's duplicate keeps its source's target. */
        if (scoped)
            assert(handles[0] != scope &&
                   ports[handles[0]].target == ports[scope].target);
    }
}

/* A scoped port runs no data operation: that is the client's lane. */
static void
test_a_scoped_port_serves_no_data(void)
{
    AstraVfsPortService host;
    AstraVfsClient remote;
    uint32_t service_handle;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    assert(astra_vfs_port_service_set_scope(&host, mock_open(1u)));
    served = &host;
    assert(astra_vfs_port_connect(&remote, service_handle) == ASTRA_VFS_OK);
    assert(astra_vfs_mkdir(&remote, "/x") == ASTRA_VFS_ERR_UNSUPPORTED);
    assert(backend_mkdir_calls == 0u);
    assert(astra_vfs_disconnect(&remote) == ASTRA_VFS_OK);
    served = NULL;
}
```

Register both in `main`. The restored accelerator tests stay. They drive
the lane's client contract through `mock_accelerator_ops`, which needs only
the rename. Use the name the file gives `astra_vfs_mkdir`'s mode argument,
if it has one (`grep -n "astra_vfs_mkdir(" sw/userspace/vfs/tests/test_vfs_port.c`).
`astra_vfs_port_connect` without accelerator ops receives the HELLO's scope
and closes it: no accelerator takes it.

`sw/userspace/vfs/tests/test_vfs_process.c` (restored): rename
`port_direct_device` to `port_direct_scope` in the stubs and assertions.
Add a stub `void astra_vfs_host_direct_fork_child(void) { ++fork_child_calls; }`.
Assert `fork_child_calls == 1u` in the existing fork-child test, whatever
its count of connected clients.

- [ ] **Step 10: HostFS mints; the supervisor narrows the device**

`sw/userspace/services/hostfs/main.c` (restored, single pump):
- The device is the HOST_FILESYSTEM grant:
  `astra_startup_capability(startup, ASTRA_CAPABILITY_HOST_FILESYSTEM)`
  where it read `ASTRA_CAPABILITY_HOST_DEVICE` (`main.c:386`).
- After the transport is initialised, one scope per volume. These are the
  grants of protocol 28; Task 5 makes them one per grant:

  ```c
  static uint32_t work_scope;
  static uint32_t sound_scope;
  ...
      if (status == ASTRA_STATUS_OK &&
          (astra_host_scope_create(device_handle, ASTRA_HOST_FS_VOLUME_WORK,
                                   0u, &work_scope) != ASTRA_SYSCALL_OK ||
           astra_host_scope_create(device_handle, ASTRA_HOST_FS_VOLUME_SOUND,
                                   0u, &sound_scope) != ASTRA_SYSCALL_OK))
          status = HOSTFS_FAIL_DEVICE;
  ```

- `!astra_vfs_port_service_set_accelerator(&port, device_handle)` becomes
  `!astra_vfs_port_service_set_scope(&port, work_scope) || !astra_vfs_port_service_set_scope(&sound_port, sound_scope)`.
  METRICS stays a plain port, served by HostFS's own channel.
- Nothing revokes these two in 3b. They live as long as HostFS, and the
  kernel revokes them when it dies (Step 4, `host_scopes_revoke_creator`).

`sw/userspace/supervisor/src/loader.c`, in `build_grants`, after the
`ASTRA_CAPABILITY_EVENT_CONTROL` block and before
`const AstraStartupCapability *held = ...`:

```c
            /* The host device reaches every host service; minting
             * filesystem scopes is the filesystem service's alone, granted
             * by name (decision 2026-10-08). */
            if (strcmp(wanted->name, ASTRA_CAPABILITY_HOST_FILESYSTEM) == 0 ||
                strcmp(wanted->name, ASTRA_CAPABILITY_HOST_DEVICE) == 0) {
                const AstraStartupCapability *device =
                    astra_startup_capability(startup,
                                             ASTRA_CAPABILITY_HOST_DEVICE);

                if (device == NULL)
                    continue;
                status = add_grant(
                    out, count, wanted->name, device->handle,
                    strcmp(wanted->name,
                           ASTRA_CAPABILITY_HOST_FILESYSTEM) == 0 ?
                        device->rights :
                        device->rights & ~ASTRA_RIGHT_HOST_SCOPE,
                    0u, NULL);
                if (status != ASTRA_STATUS_OK)
                    return status;
                continue;
            }
```

`sw/userspace/supervisor/tests/test_loader_manifest.c`: add
`"HOST_FILESYSTEM"` to `default_application_ceiling`'s `refused` list.
`emu/qemu/astra_image.py`: the hostfs line becomes
`"service /services/hostfs grants HOST_FILESYSTEM "` and
`"serves WORK:rw METRICS:r SOUND:rw required priority=20\n"`. Update
`test-astra-image-providers.py` to match. Search
`grep -rn "services/hostfs grants" emu sw tools docs` for any other copy of
the line, and change it the same way.

`sw/userspace/services/hostbench/main.c`: after it finds its
`HOST_DEVICE`, prove the narrowing:

```c
    /* HOST_DEVICE arrives without ASTRA_RIGHT_HOST_SCOPE: only the
     * filesystem service mints scopes (decision 2026-10-08). */
    {
        uint32_t scope = 0u;
        uint32_t minted = astra_host_scope_create(
            device->handle, ASTRA_HOST_FS_VOLUME_WORK, 0u, &scope);

        if (minted != ASTRA_SYSCALL_ACCESS_DENIED) {
            if (scope != 0u)
                (void)astra_close(scope);
            (void)astra_log("ASTRA HOSTBENCH SCOPE MINT NOT REFUSED");
            return fail(bootstrap, 8u);
        }
        (void)astra_log("ASTRA HOSTBENCH SCOPE MINT REFUSED");
    }
```

Use hostbench's own failure exit with its next unused code. `fail(bootstrap, N)`
is how `astra_main` reports its others; check with
`grep -n "fail(bootstrap" sw/userspace/services/hostbench/main.c`.
`emu/qemu/test-host-channel-userspace.py`: once `ASTRA RAW USER PASS` is
seen, require `ASTRA HOSTBENCH SCOPE MINT REFUSED` in the same
`said_with_syslog(0)` output, or raise
`RuntimeError("hostbench minted a scope")`.

`emu/qemu/fs-gates.sh` runs that gate on every verify. Before the
`for f in` summary:

```sh
cp "$I" /tmp/mg-fs-hostbench.img
python3 emu/qemu/astra_image.py --hostbench /tmp/mg-fs-hostbench.img \
    > /tmp/mg-fs-hostbench.log 2>&1 &&
python3 emu/qemu/test-host-channel-userspace.py "$Q" "$R" /tmp/mg-fs-hostbench.img \
    --work /tmp/mg-fs-hcu-work --host-work /tmp/mg-fs-hcu-host \
    >> /tmp/mg-fs-hostbench.log 2>&1
echo "EXIT=$?" >> /tmp/mg-fs-hostbench.log
rm -f /tmp/mg-fs-hostbench.img
```

Then add `fs-hostbench` to the summary loop. `astra_image.py --hostbench`
is the command Task 3's follow-up used (`fs1-task-3-report.md`).

- [ ] **Step 11: Protocol 28, Kit 6**

`sw/include/astra/vfs_service.h`: `ASTRA_VFS_VERSION` 27 -> 28. Append to
the history:

> Version 28 replaces 27's relay with the scoped direct lane. A HELLO on a
> port that serves a host volume attaches one handle: the grant's
> filesystem scope (`ASTRA_SYSCALL_HOST_SCOPE_CREATE`), never the host
> device. The client drives its own host channel through it. Such a port
> answers every other operation except BYE with `ASTRA_VFS_ERR_UNSUPPORTED`.

Kit 6: Task 3 Step 7's edit with `5` -> `6`:
- `ASTRA_FILESYSTEM_LIBRARY_VERSION 6u`, ABI minor 0;
- `ASTRA_DYNAMIC_LIBRARY("filesystem.library.6", 6, 0, 0, ...)`;
- `'^filesystem.library.6 6.0.0 ABI 6.0$$'` in the vfs Makefile;
- `filesystem.library.5` -> `filesystem.library.6` everywhere in Task 3
  Step 7's list. Start from the `HEAD` copies, which say 5. The exceptions
  are `sw/userspace/vfs/Makefile` and `ndk/tests/headers.c`, restored from
  `60f2d30a` in Step 8, which say 4. Set those to 6 as well.

Check: `grep -rln "filesystem\.library\.5" --exclude-dir=build --exclude-dir=.git . | grep -v "HANDOVER\|superpowers/plans"`
prints nothing. `docs/ABI.md` gains a "Filesystem Kit ABI 6.0" section
after 5.0. It says:
- `AstraVfsClient` regains the lane: `port_direct_address`, `_area`,
  `_scope`, `_session`, `_detached`, `direct_backend_*`,
  `port_accelerator_ops`; `port_direct_lock` is gone;
- the lane fields of `AstraVfsPortLane` are back;
- `AstraVfsPortAcceleratorOps.connect` takes a scope.

- [ ] **Step 12: The probe and its comparison**

`sw/userspace/commands/fsprobe/fsprobe.c` is
`.superpowers/sdd/fsperf/fsprobe.c` verbatim. The exception is the header
comment's first two lines:

```c
/*
 * fsprobe -- per-operation cost of one warm stat and one 4 KiB pread, with
 * the kernel's own accounting of where it went (switches, per-process CPU
 * and syscalls). A test command: the lane's structural gate (Tasks 3b, 13).
```

`sw/userspace/commands/Makefile`: add `fsprobe` to `TEST_COMMANDS` and
`fsprobe_LIBRARIES := $(LIBC_LIBRARY) $(RUNTIME_LIBRARY)`, the prototype's
two lines.

`emu/qemu/fs-probe.py` is `.superpowers/sdd/fsperf/fsperf.py` without the
plugin and perf options. It takes `TREE QEMU OUT.json [--n N] [--label L]`
and writes a record instead of a text file. After the run, replace the
`fsprobe.txt` write with:

```python
def parse(lines, operations):
    """fsprobe's lines -> per-phase medians and the per-operation counters
    the lane must keep near zero."""
    phases = {}
    for line in lines:
        words = line.split()
        if len(words) < 3 or words[0] != "fsprobe:":
            continue
        kind, phase = words[1], words[2]
        fields = dict(word.split("=", 1) for word in words[3:]
                      if "=" in word)
        record = phases.setdefault(phase, {"hostfs_ns": 0, "client_sc": 0})
        if kind == "lat":
            record["median_ns"] = int(fields["med"])
        elif kind == "tail":
            record["p95_ns"] = int(fields["p95"])
        elif kind == "sw":
            record["cross"] = int(fields["cross"])
        elif kind == "proc":
            name = words[3]
            if "hostfs" in name:
                record["hostfs_ns"] += int(fields["rt"])
            if "fsprobe" in name:
                record["client_sc"] += int(fields["sc"])
    result = {}
    for phase in ("stat", "read"):
        record = phases.get(phase)
        if record is None or "median_ns" not in record:
            raise SystemExit("fs-probe: no %s phase in %r" % (phase,
                                                             lines[-10:]))
        result[phase] = {
            "median_ns": record["median_ns"],
            "p95_ns": record["p95_ns"],
            "cross_per_op": record["cross"] / operations,
            "hostfs_ns_per_op": record["hostfs_ns"] / operations,
            "client_syscalls_per_op": record["client_sc"] / operations,
        }
    return result
```

and:

```python
        record = {"identity": {"tree": tree, "qemu": os.path.abspath(args.qemu),
                               "n": args.n},
                  "phases": parse(lines_out, args.n)}
        with open(args.out, "w") as out:
            json.dump(record, out, indent=1, sort_keys=True)
        print("fs-probe: wrote %s" % args.out)
```

The guest pause stays the default 600 ms, so no host profiler is needed.
Every guest line is still printed as it arrives.

`tools/fs-probe-compare.py`:

```python
#!/usr/bin/env python3
"""Compare fs-probe records: the lane against the direct baseline.

usage: fs-probe-compare.py BASE[,BASE...] NEW[,NEW...]
                           [--budget 0.10] [--cross 0.05] [--hostfs-ns 500]

A side's value per phase is the median over its runs. NEW passes when its
stat and read medians and p95s are within the budget of BASE, and when its
cross-address-space switches per operation and HostFS CPU per operation
stay at or under the bounds: the lane runs no server on the data path.
"""

import argparse
import json
import statistics
import sys

FIELDS = ("median_ns", "p95_ns", "cross_per_op", "hostfs_ns_per_op",
          "client_syscalls_per_op")


def side(spec):
    runs = []
    for path in spec.split(","):
        with open(path) as source:
            runs.append(json.load(source)["phases"])
    return {phase: {field: statistics.median(run[phase][field]
                                             for run in runs)
                    for field in FIELDS}
            for phase in ("stat", "read")}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("base")
    parser.add_argument("new")
    parser.add_argument("--budget", type=float, default=0.10)
    parser.add_argument("--cross", type=float, default=0.05)
    parser.add_argument("--hostfs-ns", type=float, default=500.0)
    arguments = parser.parse_args()
    base = side(arguments.base)
    new = side(arguments.new)
    failed = []
    for phase in ("stat", "read"):
        for field in ("median_ns", "p95_ns"):
            ratio = new[phase][field] / base[phase][field]
            print("%-5s %-9s base %10.0f new %10.0f ratio %.3f" %
                  (phase, field, base[phase][field], new[phase][field],
                   ratio))
            if ratio > 1.0 + arguments.budget:
                failed.append("%s %s %.3f" % (phase, field, ratio))
        print("%-5s cross/op %.3f  hostfs ns/op %.0f  syscalls/op %.2f" %
              (phase, new[phase]["cross_per_op"],
               new[phase]["hostfs_ns_per_op"],
               new[phase]["client_syscalls_per_op"]))
        if new[phase]["cross_per_op"] > arguments.cross:
            failed.append("%s cross %.3f" % (phase,
                                             new[phase]["cross_per_op"]))
        if new[phase]["hostfs_ns_per_op"] > arguments.hostfs_ns:
            failed.append("%s hostfs %.0f" % (
                phase, new[phase]["hostfs_ns_per_op"]))
    print("PROBE FAIL %s" % "; ".join(failed) if failed else "PROBE PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
```

Self-check, on the Mac or beast:

```sh
python3 - <<'EOF'
import json, os, subprocess, tempfile
d = tempfile.mkdtemp()
def record(median, cross, hostfs):
    phase = {"median_ns": median, "p95_ns": median * 1.2, "cross_per_op": cross,
             "hostfs_ns_per_op": hostfs, "client_syscalls_per_op": 3.0}
    return {"phases": {"stat": phase, "read": phase}}
for name, value in (("b", record(35000, 0.01, 0)), ("lane", record(34000, 0.01, 0)),
                    ("relay", record(92000, 3.98, 37000)),
                    ("hidden", record(35000, 0.01, 37000))):
    json.dump(value, open(os.path.join(d, name), "w"))
tool = "tools/fs-probe-compare.py"
run = lambda a, b: subprocess.run(["python3", tool, a, b], capture_output=True).returncode
assert run(d + "/b", d + "/lane") == 0
assert run(d + "/b", d + "/relay") == 1
assert run(d + "/b", d + "/hidden") == 1   # fast, but a server is on the path
assert run(d + "/b", ",".join([d + "/lane"] * 3 + [d + "/relay"] * 2)) == 0
assert run(d + "/b", ",".join([d + "/relay"] * 3 + [d + "/lane"] * 2)) == 1
print("PROBE COMPARE SELF-CHECK PASS")
EOF
```

Expected: `PROBE COMPARE SELF-CHECK PASS`.

- [ ] **Step 13: The guest cases**

`sw/userspace/commands/fssem/fssem.c`:

```c
#include <astra/host.h>
#include <astra/runtime.h>
#include <pthread.h>
#include <sys/wait.h>

typedef struct RawLane {
    uint32_t scope;
    uint32_t error;
    uint32_t statuses[3];
} RawLane;

/* On a thread of its own: a thread has one host channel, and the main
 * thread's carries the Kit's lane. Three raw commands on a channel opened
 * with fssem's own WORK scope. */
static void *raw_lane_thread(void *argument)
{
    static const struct {
        uint16_t service;
        uint16_t operation;
        uint32_t volume;
        const char *path;
    } steps[] = {
        {ASTRA_HOST_SERVICE_FILESYSTEM, ASTRA_HOST_FS_STAT,
         ASTRA_HOST_FS_VOLUME_WORK, "/"},
        {ASTRA_HOST_SERVICE_FILESYSTEM, ASTRA_HOST_FS_MKDIR,
         ASTRA_HOST_FS_VOLUME_SOUND, "/raw-lane-forged"},
        {ASTRA_HOST_SERVICE_ENTROPY, ASTRA_HOST_ENTROPY_FILL, 0u, ""},
    };
    RawLane *raw = argument;
    AstraHostChannelClient channel;

    memset(&channel, 0, sizeof(channel));
    raw->error = astra_host_client_open(raw->scope, ASTRA_HOST_CAP_FS_SCOPE,
                                        64u, &channel);
    if (raw->error != ASTRA_SYSCALL_OK)
        return NULL;
    for (uint32_t index = 0u; index < 3u; ++index) {
        AstraHostCommand *command = astra_host_client_prepare(
            &channel, steps[index].service, steps[index].operation);

        command->volume = steps[index].volume;
        command->value_lo = 0755u;
        (void)strcpy(command->path, steps[index].path);
        if (steps[index].service == ASTRA_HOST_SERVICE_ENTROPY)
            command->data_capacity = 32u;
        else
            command->data_offset = 0u;
        raw->error = astra_host_client_submit(&channel);
        if (raw->error != ASTRA_SYSCALL_OK)
            break;
        raw->statuses[index] = command->status;
    }
    (void)astra_host_client_close(&channel);
    return NULL;
}

/*
 * A client's raw commands on its own lane (FS15, FS44): its own scope
 * answers; a forged volume and every other service are refused by the
 * provider. The gate's `lane` check confirms nothing landed in SOUND.
 */
static int case_raw_lane(const char *directory)
{
    const AstraAssign *work =
        astra_assign_lookup(astra_process_vfs_assigns(), "WORK");
    AstraVfsClient *client = astra_process_vfs_client_for(work);
    pthread_t thread;
    struct stat st;
    RawLane raw;

    (void)directory;
    CHECK(stat("/work", &st) == 0 && client != NULL, "no WORK client");
    memset(&raw, 0, sizeof(raw));
    raw.scope = client->port_direct_scope;
    CHECK(raw.scope != 0u, "WORK is not on a lane");
    CHECK(pthread_create(&thread, NULL, raw_lane_thread, &raw) == 0 &&
          pthread_join(thread, NULL) == 0, "raw lane thread");
    CHECK(raw.error == ASTRA_SYSCALL_OK, "raw lane: %u", (unsigned)raw.error);
    CHECK(raw.statuses[0] == ASTRA_STATUS_OK, "own scope STAT: %u",
          (unsigned)raw.statuses[0]);
    CHECK(raw.statuses[1] == ASTRA_STATUS_ACCESS, "forged volume: %u",
          (unsigned)raw.statuses[1]);
    CHECK(raw.statuses[2] == ASTRA_STATUS_ACCESS, "entropy on a lane: %u",
          (unsigned)raw.statuses[2]);
    return 0;
}

/* fork and exec keep host volumes working: the child opens its own lane on
 * the inherited scope, and an exec'd image resumes it. No file is open at
 * the fork: until Task 8, fork refuses while one is. */
static int case_lane_fork_exec(const char *directory)
{
    char path[160];
    struct stat st;
    pid_t child;
    int status;

    join(path, sizeof(path), directory, "lane-file");
    CHECK(write_text(path, "lane", O_WRONLY | O_CREAT | O_EXCL) == 0,
          "seed");
    child = fork();
    CHECK(child >= 0, "fork: %s", strerror(errno));
    if (child == 0)
        _exit(stat(path, &st) == 0 && st.st_size == 4 ? 0 : 1);
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
          WEXITSTATUS(status) == 0, "fork child could not stat on its lane");
    child = fork();
    CHECK(child >= 0, "fork: %s", strerror(errno));
    if (child == 0) {
        char *const argv[] = {"fssem", "--lane-helper", path, NULL};

        execv("/commands/fssem", argv);
        _exit(127);
    }
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
          WEXITSTATUS(status) == 0, "exec'd image could not stat: %d",
          WEXITSTATUS(status));
    return 0;
}
```

`main` gains, before the case loop:

```c
    if (argc == 3 && strcmp(argv[1], "--lane-helper") == 0) {
        struct stat st;

        return stat(argv[2], &st) == 0 && st.st_size == 4 ? 0 : 2;
    }
```

Register `{"raw-lane", case_raw_lane}` and
`{"lane-fork-exec", case_lane_fork_exec}`.
- `astra_assign_lookup`, `astra_process_vfs_assigns` and
  `astra_process_vfs_client_for` are the process-VFS calls Task 11's
  `hold_sessions` uses. Confirm their headers with
  `grep -rn "astra_process_vfs_client_for" sw/userspace ndk/include | grep "\.h:"`.
- If fssem's Makefile entry does not link the Kit, add
  `$(FILESYSTEM_LIBRARY)` to it.
- `install_test_command` installs fssem at `/commands/fssem`; check
  `astra_image.py:704-730`.

`emu/qemu/test-fs-semantics.py`: `CASES` gains `"raw-lane"` and
`"lane-fork-exec"`. Add:

```python
def scoped_commands(machine):
    return machine.qmp.execute("qom-get", {
        "path": "/machine", "property": "astra-host-scoped-commands"})


def check_lane(machine, mount):
    """The lane is the path taken (FS44): fssem's own work moves the
    provider's scoped-command count, and raw-lane's forged MKDIR left
    nothing in SOUND."""
    before = scoped_commands(machine)
    lines, status = command(machine, "fssem basic", 120, 61)
    if status != 0:
        fail("fssem basic: %r" % lines[-5:])
    moved = scoped_commands(machine) - before
    if moved < 10:
        fail("only %d scoped commands for fssem basic" % moved)
    if os.path.exists(os.path.join(mount, "sound", "raw-lane-forged")):
        fail("a forged volume reached SOUND")
    print("PASS lane %d" % moved)


CHECKS["lane"] = check_lane
```

Use `machine.qmp.execute` as Task 10 and Task 11 do. If the QMP helper
returns the response object rather than its `return` value, index it the
way the existing `qom-get` callers in `test-terminal.py` do
(`grep -n "qom-get" emu/qemu/test-terminal.py`).

- [ ] **Step 14: Build and run everything**

Sync, then on beast:

```sh
~/astra-mg/locked make -C sw/kernel clean
~/astra-mg/locked make -C sw/kernel test
~/astra-mg/locked make -C sw/kernel clean
~/astra-mg/locked make -C sw/userspace -j32
~/astra-mg/locked make -C sw/userspace test
~/astra-mg/locked make -C sw/boot -j32
ASTRA_QEMU_WORK_ROOT=$HOME/.cache/astra68/qemu-9.2.4 ~/astra-mg/locked emu/qemu/build.sh host
Q=$(tail -n1 /tmp/qemu-host-build.log); R=sw/boot/build/astra_boot.bin
~/astra-mg/locked bash ~/astra-mg/images.sh
~/astra-mg/locked python3 emu/qemu/test-hostfs.py $Q
~/astra-mg/locked python3 emu/qemu/test-fs-semantics.py $Q $R ~/astra-mg/base.img
cp ~/astra-mg/base.img /tmp/t3b-hostbench.img
python3 emu/qemu/astra_image.py --hostbench /tmp/t3b-hostbench.img
~/astra-mg/locked python3 emu/qemu/test-host-channel-userspace.py $Q $R /tmp/t3b-hostbench.img \
  --work /tmp/t3b-hcu-work --host-work /tmp/t3b-hcu-host
```

Expected:
- the kernel suites PASS, including `test_host_scope` and the handle
  import test;
- the userspace tests PASS: `ASTRA VFS PORT PASS`, the host transport test
  with `test_lane_carries_scopes`, and the process and host-backend tests;
- `PASS scopes`, `PASS scope-revocation`, `ASTRA QEMU HOSTFS PASS`;
- `FSSEM PASS raw-lane`, `FSSEM PASS lane-fork-exec`, `PASS lane <n>` with
  n >= 10, `ASTRA FS SEMANTICS PASS`;
- `ASTRA RAW USERSPACE HOST CHANNEL PASS`, with
  `ASTRA HOSTBENCH SCOPE MINT REFUSED` in the log.

- [ ] **Step 15: Perturb**

Each perturbation must turn the named check red. Restore and re-sync with
the `--no-times` rsync after each, rebuild what it touched (the kernel and
ROM, the userspace and ROM, or QEMU), and see it pass again.

1. **A non-filesystem holder mints.** In `HOST_SCOPE_CREATE`, look the
   device up with `KERNEL_DEVICE_RIGHT_EXECUTE` in place of
   `KERNEL_DEVICE_RIGHT_HOST_SCOPE`. Expected: `test_host_scope` fails at
   the `no_scope_right` ACCESS_DENIED assertion, and the hostbench gate
   fails "hostbench minted a scope".
2. **The supervisor does not narrow.** In `build_grants`, grant
   `HOST_DEVICE` with `device->rights` unmasked. Expected: the hostbench
   gate fails "hostbench minted a scope". The kernel is unchanged, so this
   proves the supervisor half.
3. **A forged volume is honored.** In `astra_host_execute_fs`, ignore the
   lookup's answer: take `channel->scopes[0]` under the lock whatever
   `volume` says. Expected: `scopes` fails at `status(forged) == STATUS_ACCESS`,
   and `raw-lane` fails "forged volume".
4. **Read-only is not enforced.** Delete the `ASTRA_HOST_SCOPE_READ_ONLY`
   branch. Expected: `scopes` fails at `status(refused) == STATUS_READ_ONLY`.
5. **Revocation does not reach in-flight work.** Make `DETACH` answer OK
   without removing the entry. Expected: `scope_revocation` fails at the
   second command's `STATUS_ACCESS`.
6. **Handles are not tagged.** Drop the `file->volume != volume` test in
   `astra_host_file_lock` and `astra_host_close_file`. Expected: `scopes`
   fails at `status(crossed) == STATUS_BAD_HANDLE`.
7. **Delegation widens.** Remove
   `kernel_handle_set_import_rights(host_scope_import_rights);`. Expected:
   `test_host_scope` fails at the moved handle's duplicate being
   ACCESS_DENIED.
8. **A dead creator's scopes live on.** Remove the
   `host_scopes_revoke_creator` call. Expected: `test_host_scope` fails at
   `host_channel_detach_calls == 2u`.
9. **The relay comes back.** Delete the control-only refusal in the pump.
   Expected: `test_a_scoped_port_serves_no_data` fails on
   `backend_mkdir_calls == 0u`.
10. **A fork child uses its parent's lanes.** Skip
    `astra_vfs_host_direct_fork_child()` in `seed_process_vfs`. Expected:
    `lane-fork-exec` fails "fork child could not stat on its lane", and
    `test_vfs_process` fails on `fork_child_calls == 1u`.

- [ ] **Step 16: Measure: the lane against the direct path, interleaved**

The direct baseline is the pre-27 tree `60f2d30a`. Set it up on beast once.
On the Mac:

```sh
git archive 60f2d30a | rtk proxy ssh beast 'rm -rf ~/fsperf/direct && mkdir -p ~/fsperf/direct && tar -x -C ~/fsperf/direct'
rtk rsync -az sw/userspace/commands/fsprobe/ beast:fsperf/direct/sw/userspace/commands/fsprobe/
```

On beast:

```sh
cd ~/fsperf
for repo in ~/Git/SDL2* ~/Git/chocolate-doom; do ln -sfn "$repo" ~/fsperf/; done
sed -i 's/^\(TEST_COMMANDS := .*fssem\)$/\1 fsprobe/' direct/sw/userspace/commands/Makefile
grep -q '^fsprobe_LIBRARIES' direct/sw/userspace/commands/Makefile ||
  sed -i '/^fssem_LIBRARIES/a fsprobe_LIBRARIES := $(LIBC_LIBRARY) $(RUNTIME_LIBRARY)' direct/sw/userspace/commands/Makefile
build() {  # TREE -- the investigation's build.sh
  ~/astra-mg/locked sh -c "cd $1 && make -C sw/kernel clean && make -C sw/userspace -j32 all &&
    make -C sw/boot -j32 && make -C sw/userspace/commands build/m68k/fsprobe &&
    python3 emu/qemu/astra_image.py --create base.img 256 && python3 emu/qemu/astra_image.py base.img"
}
build ~/fsperf/direct
(cd ~/fsperf/direct && ASTRA_QEMU_WORK_ROOT=$HOME/.cache/astra68/qemu-9.2.4 ~/astra-mg/locked emu/qemu/build.sh host)
QD=$(tail -n1 /tmp/qemu-host-build.log)
build ~/Git/astra68
(cd ~/Git/astra68 && ASTRA_QEMU_WORK_ROOT=$HOME/.cache/astra68/qemu-9.2.4 ~/astra-mg/locked emu/qemu/build.sh host)
QL=$(tail -n1 /tmp/qemu-host-build.log)
OUT=/mnt/Documents/astra68/builds/fs-phase1-task3b-$(date +%Y%m%d)
mkdir -p "$OUT"
for run in 1 2 3 4 5; do
  ~/astra-mg/locked python3 ~/Git/astra68/emu/qemu/fs-probe.py ~/fsperf/direct $QD "$OUT/direct-$run.json" --n 5000 --label d$run
  ~/astra-mg/locked python3 ~/Git/astra68/emu/qemu/fs-probe.py ~/Git/astra68 $QL "$OUT/lane-$run.json" --n 5000 --label l$run
done
python3 ~/Git/astra68/tools/fs-probe-compare.py \
  $(ls $OUT/direct-*.json | paste -sd,) $(ls $OUT/lane-*.json | paste -sd,) \
  | tee "$OUT/lane-vs-direct.txt"
```

The two `build.sh host` runs leave each tree's QEMU in its own build
directory. `tail -n1 /tmp/qemu-host-build.log` right after each one names
it. Run `fs-probe.py` from this tree in both cases: it loads the tree it is
given for the ROM, image and fsprobe.

Expected: `lane-vs-direct.txt` ends `PROBE PASS`. The prototype measured
0.98x/1.00x stat and 1.00x/1.02x read, with 0.01 cross-AS switches and no
HostFS CPU per operation.

Also run fsbench as Task 3 did, three times, and compare against the
QEMU baseline and the relay:

```sh
for run in 1 2 3; do
  ~/astra-mg/locked python3 emu/qemu/test-fs-semantics.py $Q $R ~/astra-mg/base.img --bench t3b-$run "$OUT/qemu-t3b-$run.json"
done
BASE=/mnt/Documents/astra68/builds/fs-phase1-baseline-20261007-133dc6bc
RELAY=/mnt/Documents/astra68/builds/fs-phase1-task3-60f2d30a
python3 tools/fs-baseline-compare.py "$BASE/qemu-base1.json" "$OUT/qemu-t3b-1.json" | tee "$OUT/lane-vs-baseline.txt"
python3 tools/fs-baseline-compare.py "$RELAY/qemu-t3-1.json" "$OUT/qemu-t3b-1.json" | tee "$OUT/lane-vs-relay.txt"
```

This plan does not gate on these two tables; Task 13 compares medians.
Record the `stat-warm`, `open-close-warm`, `rand-read` and `held-miss`
ratios in the commit message. `lane-vs-relay.txt` should show the 2.5x
gone.

If `PROBE FAIL` names `cross` or `hostfs`, a server is still on the path.
Stop and find which operation goes through HostFS: its pump counts
`requests`. Do not raise the bounds. If only the latency ratio fails, run
five more pairs. The investigation measured +-10% between single runs of
one build. If the medians of ten still fail, stop and report the numbers.

- [ ] **Step 17: The documents**

- `docs/STORAGE_AND_VFS.md`, "Current implementation": replace the bullets
  `6d4e6a2c` wrote. Say that:
  - HostFS is the only holder of `HOST_FILESYSTEM` and mints one scope per
    volume;
  - HELLO attaches it (protocol 28);
  - clients drive their own channels through it, with the kernel binding
    and QEMU enforcing;
  - grant *roots* are still client-only until Task 5.

  Replace "Cost of the HostFS hop" with "Cost of the lane". It gives this
  task's `lane-vs-direct.txt` medians and the relay's 2.6x it replaced.
- `docs/FILESYSTEM_PRODUCTION_PRD.md`, the gap row "Linux-backed files": the
  current state is the scoped lane. FS44 reads: "The broad host-device
  handoff was removed at storage protocol 27. Protocol 28 hands a client a
  kernel-bound filesystem scope instead, which reaches only that scope's
  session; the device itself must not return as a grant bypass." FS15
  changes in Task 5.
- `docs/DRIVER_AND_SERVICE_ARCHITECTURE.md` §8: replace the 62b72fc2
  paragraph. HostFS holds `HOST_FILESYSTEM` and mints scopes. Clients
  drive scoped channels. Entropy, media and remote desktop hold
  `HOST_DEVICE`, which cannot mint.
- `docs/ABI.md`:
  - storage protocol 28;
  - host transport `0x0001000d` with `ASTRA_HOST_CAP_FS_SCOPE`;
  - channel configuration 2;
  - syscalls 109-111, `ASTRA_RIGHT_HOST_SCOPE`, `KERNEL_OBJECT_HOST_SCOPE`;
  - runtime 1.14;
  - Kit 6 (Step 11).
- `docs/FILESYSTEM_KIT.md` and `ndk/docs/source/filesystem.md`:
  `filesystem.library` 6.0, linked as `filesystem.library.6`.

- [ ] **Step 18: Full suite, commit**

`~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-task3b.log`. Expected: green.
`fs-gates.sh` now also runs `fs-hostbench`. Copy the log into `$OUT`.

```bash
git add sw/include/astra sw/kernel ndk sw/userspace/runtime sw/userspace/kits \
  sw/userspace/vfs sw/userspace/services/hostfs sw/userspace/services/hostbench \
  sw/userspace/supervisor sw/userspace/commands/Makefile sw/userspace/commands/fsprobe \
  sw/userspace/commands/fssem/fssem.c sw/userspace/posix/Makefile sw/userspace/libraries.mk \
  sw/userspace/config/Makefile sw/userspace/lua/Makefile sw/userspace/tests/test_boundaries.py \
  sw/userspace/apps/Terminal.app/manifest sw/userspace/apps/InterfaceGallery.app/manifest \
  emu/qemu/qemu-9.2/hw/m68k/astra68.c emu/qemu/test-hostfs.py emu/qemu/astra_image.py \
  emu/qemu/test-astra-image-providers.py emu/qemu/test-host-channel-userspace.py \
  emu/qemu/fs-gates.sh emu/qemu/fs-probe.py emu/qemu/test-fs-semantics.py \
  tools/fs-probe-compare.py tools/tests/test_check_bundle_dependencies.py \
  docs/STORAGE_AND_VFS.md docs/FILESYSTEM_PRODUCTION_PRD.md \
  docs/DRIVER_AND_SERVICE_ARCHITECTURE.md docs/ABI.md docs/FILESYSTEM_KIT.md \
  ndk/docs/source/filesystem.md
git commit -m "feat(fs): the scoped direct lane -- kernel scopes bind client channels, QEMU enforces (FS15, FS44); storage protocol 28, Kit 6"
```

The message body records:
- the probe medians and the cross-AS/HostFS per-operation figures;
- the fsbench ratios against the baseline and the relay;
- the evidence directory.

---

### Task 4: Host command record 3 -- claimed provider, rooted sessions, path operands, node record (FS43, D2 provider half, D4 wire, D7 host wire)

One flag day for the shared host command record. Every service validates
`ASTRA_HOST_COMMAND_VERSION` for equality, so every consumer is rebuilt and
re-verified together: entropy, metrics, remote desktop, audio, hostbench and
the raw channel benchmark (FS43). The filesystem changes are these:

- QEMU accepts filesystem commands on an unbound channel from exactly one
  *claimed* owner, so the entropy, media and remote-desktop services, which
  also hold `HOST_DEVICE`, cannot drive files. On a bound channel (Task 3b)
  it accepts them through a kernel-attached scope whose session the claimed
  owner opened and whose creator is the claimed owner (Step 4b).
- Every command names a *session*. A session is a root directory opened
  beneath a volume, or beneath another session. A read-only session refuses
  every mutation in QEMU itself. No command names a volume.
- Paths are length-counted operands in the data span, up to 4,095 bytes.
- Metadata is a `statx` node record: inode, device, blocks, nanosecond times
  and birth time, with a validity mask.
- Every path operation takes a base directory handle (0: the session root).
  The four `*_AT` operations and `STAT_FILE`/`CHMOD_FILE` therefore fold into
  their plain forms. Retired operation numbers are not kept: the record
  version changes, so the table is renumbered densely.

Path *resolution* semantics do not change here: `.`, `..` and symlinks are
still refused (Task 6 changes that). HostFS behaves as today, with one root
session per volume.

**Files:**
- Modify: `sw/include/astra/host.h`
- Modify: `emu/qemu/qemu-9.2/hw/m68k/astra68.c`
- Modify: `sw/userspace/vfs/include/astra/vfs_host_backend.h`, `sw/userspace/vfs/src/backends/host.c`, `sw/userspace/vfs/src/vfs_host_transport.c`
- Modify: `sw/userspace/services/hostfs/main.c`
- Modify: `emu/qemu/test-hostfs.py`, `emu/qemu/test-filesystem-stress.py`
- Test: `sw/userspace/vfs/tests/test_vfs_host_backend.c`, `sw/userspace/vfs/tests/test_vfs_host_transport.c`

**Interfaces:**
- Produces (host.h): `ASTRA_HOST_COMMAND_VERSION 3`, `ASTRA_HOST_VERSION 0x0001000e`,
  `ASTRA_HOST_CAP_FS_SESSIONS`, `ASTRA_HOST_FS_PATH_MAX 4096u`,
  `AstraHostNode`, `AstraHostTime`, `ASTRA_HOST_NODE_*` validity bits,
  operations `ASTRA_HOST_FS_OPEN`..`ASTRA_HOST_FS_CLAIM` (1..19), flags
  `ASTRA_HOST_FS_UNLINK_DIRECTORY`, `ASTRA_HOST_FS_UNLINK_FILE`,
  `ASTRA_HOST_FS_SESSION_READ_ONLY`, `ASTRA_HOST_FS_CLAIM_RELEASE`.
- Produces (host backend): `astra_vfs_host_init(backend, transport, generation, session)`
  (a session id where the volume was), `AstraVfsHostBackend.session`,
  `AstraVfsHostRequest.path/path_length/path2/path2_length`,
  `astra_vfs_host_claim(transport, release) -> status`,
  `astra_vfs_host_session_open(transport, parent, volume, root, flags, &session) -> status`,
  `astra_vfs_host_session_close(transport, session) -> status`.
- Produces (provider): scopes name sessions; a bound channel's scope must
  name a live session of the claimed owner, created by it (Step 4b); handles
  are tagged with their session, and a session's handles close with it.
- Consumed by: Task 5 (sessions and scopes per grant), Task 6 (resolver),
  Task 7 (node record), Task 12 (bootstrap client).

- [ ] **Step 1: Write the failing backend test**

Rewrite the fake in `sw/userspace/vfs/tests/test_vfs_host_backend.c` for
record 3:

- `astra_vfs_host_transport_submit` asserts
  `command->version == ASTRA_HOST_COMMAND_VERSION` (3), `command->session == 7u`
  (every command names its backend's session) and, for path operations,
  that `request->path`/`request->path_length` carry the path. It no longer
  reads `command->path`.
- Replace each `strcmp(command->path, X) == 0` with
  `operand(request->path, request->path_length, X)`, where:

  ```c
  static int operand(const char *bytes, uint32_t length, const char *expected)
  {
      return length == strlen(expected) && memcmp(bytes, expected, length) == 0;
  }
  ```

  Do the same for `path2`.
- The folded operations change. `ASTRA_HOST_FS_OPEN_AT` is
  `ASTRA_HOST_FS_OPEN` with `command->handle == 0x1234u` and the relative
  operand `child`. `ASTRA_HOST_FS_STAT_AT` is `ASTRA_HOST_FS_STAT` with
  handle `0x1234u` and operand `child`. `ASTRA_HOST_FS_STAT_FILE` is
  `ASTRA_HOST_FS_STAT` with handle `0x1234u` and `path_length == 0`.
  `ASTRA_HOST_FS_CHMOD_FILE` is `ASTRA_HOST_FS_CHMOD` with an empty operand.
  `ASTRA_HOST_FS_CHMOD_AT` is `ASTRA_HOST_FS_CHMOD` with handle and operand,
  and `flags == 0`, since Task 4's provider never follows. `ASTRA_HOST_FS_UNLINK_AT`
  with `ASTRA_VFS_AT_REMOVE_DIRECTORY` is `ASTRA_HOST_FS_UNLINK` with handle
  and `flags == ASTRA_HOST_FS_UNLINK_DIRECTORY`. The path `unlink` sends
  `flags == 0` (either kind).
- Metadata answers are written into `command->node`. For STAT_AT's answer:

  ```c
  command->node.valid = ASTRA_HOST_NODE_TYPE | ASTRA_HOST_NODE_MODE |
                        ASTRA_HOST_NODE_NLINK | ASTRA_HOST_NODE_UID |
                        ASTRA_HOST_NODE_GID | ASTRA_HOST_NODE_SIZE |
                        ASTRA_HOST_NODE_MTIME;
  command->node.size_lo = 23u;
  command->node.mtime.seconds_lo = 99u;
  command->node.uid = 17u;
  command->node.gid = 23u;
  command->node.kind = ASTRA_VFS_KIND_FILE;
  command->node.mode = 0640u;
  command->node.nlink = 2u;
  ```

- `main` initialises with `astra_vfs_host_init(&backend, transport, 9u, 7u)`.
  The volume assertions (`ASTRA_HOST_FS_VOLUME_COUNT` refused) become: a
  session of 0 is refused. The final count is `host.calls == 22u`: the
  same 22 backend calls, now carried by 16 operations.

Run on beast: `~/astra-mg/locked make -C sw/userspace/vfs build/host/test_vfs_host_backend`
Expected: compile errors (`session`, `node`, `path_length` do not exist).

- [ ] **Step 2: The record in `sw/include/astra/host.h`**

- `ASTRA_HOST_VERSION` -> `UINT32_C(0x0001000e)`. Delete the unused revision
  constants `ASTRA_HOST_VERSION_1_1` through `ASTRA_HOST_VERSION_1_8`
  (`grep -rn ASTRA_HOST_VERSION_1_ sw emu fpga tools ndk` finds no user).
- After `ASTRA_HOST_CAP_AUDIO_STREAM`:

  ```c
  /** Filesystem commands use record version 3: one claimed owner, rooted
   * sessions, path operands in the data span and the AstraHostNode record. */
  #define ASTRA_HOST_CAP_FS_SESSIONS (1u << 11)
  ```

- Replace `ASTRA_HOST_FS_PATH_MAX 192u` and the operation enum
  (`host.h:76-107`) with:

  ```c
  /** Bytes of the largest path buffer, terminator included: a path operand
   * on the wire is at most ASTRA_HOST_FS_PATH_MAX - 1 bytes, unterminated. */
  #define ASTRA_HOST_FS_PATH_MAX 4096u

  /** Filesystem operations, AstraHostCommand::operation when service is
   * ::ASTRA_HOST_SERVICE_FILESYSTEM. Every path operation resolves its
   * `path` beneath `handle`, an open directory, or beneath the session root
   * when `handle` is 0; with `handle` 0 the path is absolute ("/" names the
   * root), otherwise relative. */
  enum {
      ASTRA_HOST_FS_OPEN = 1u,        /**< Open a path; `handle` answers the new handle. */
      ASTRA_HOST_FS_CLOSE,            /**< Close `handle`. */
      ASTRA_HOST_FS_READ,             /**< Read `handle` at offset_hi/offset_lo. */
      ASTRA_HOST_FS_WRITE,            /**< Write `handle` at offset_hi/offset_lo, or append. */
      ASTRA_HOST_FS_SYNC,             /**< fsync(2) `handle`. */
      ASTRA_HOST_FS_TRUNCATE,         /**< Set `handle`'s length to value_hi/value_lo. */
      ASTRA_HOST_FS_STAT,             /**< No-follow metadata of a path, or of `handle` itself when the path is empty. */
      ASTRA_HOST_FS_READDIR,          /**< Next entry of open directory `handle` (empty path), or of the path. */
      ASTRA_HOST_FS_MKDIR,            /**< Create a directory; value_lo is its mode. */
      ASTRA_HOST_FS_UNLINK,           /**< Remove a path; ASTRA_HOST_FS_UNLINK_* in flags. */
      ASTRA_HOST_FS_RENAME,           /**< Rename `path` (beneath `handle`) to `path2` (beneath directory handle value_lo). */
      ASTRA_HOST_FS_CHMOD,            /**< Change a path's mode, or `handle`'s when the path is empty. */
      ASTRA_HOST_FS_READLINK,         /**< Read a symbolic link's stored target into the data span. */
      ASTRA_HOST_FS_SYMLINK,          /**< Create `path2` (beneath `handle`) holding the literal target `path`. */
      ASTRA_HOST_FS_LINK,             /**< Hard-link `path` (beneath `handle`) as `path2` (beneath value_lo). */
      ASTRA_HOST_FS_FILESYSTEM_INFO,  /**< ::AstraHostFilesystemInfo for a path or `handle`. */
      ASTRA_HOST_FS_SESSION_OPEN,     /**< Open a rooted session; result_value answers its id. */
      ASTRA_HOST_FS_SESSION_CLOSE,    /**< Close `session`. */
      ASTRA_HOST_FS_CLAIM             /**< Bind the filesystem service to the submitting owner, or release it. */
  };
  /** Highest valid ASTRA_HOST_FS_* operation code. */
  #define ASTRA_HOST_FS_MAX ASTRA_HOST_FS_CLAIM

  /** Volumes a root session may open (SESSION_OPEN value_lo with session 0):
   * one host directory each beneath the provider root. */
  enum {
      ASTRA_HOST_FS_VOLUME_WORK = 0u, /**< The general-purpose working volume. */
      ASTRA_HOST_FS_VOLUME_SOUND,     /**< The volume holding shared SoundFonts. */
      ASTRA_HOST_FS_VOLUME_COUNT      /**< Count of defined volumes. */
  };

  /** WRITE flag: append at end of file, atomically, for this write. */
  #define ASTRA_HOST_FS_WRITE_APPEND      (1u << 15)
  /** UNLINK flag: the path must be a directory. */
  #define ASTRA_HOST_FS_UNLINK_DIRECTORY  (1u << 0)
  /** UNLINK flag: the path must not be a directory. With neither flag the
   * provider removes whichever the path names. */
  #define ASTRA_HOST_FS_UNLINK_FILE       (1u << 1)
  /** SESSION_OPEN flag: every mutation in the session is refused
   * (ASTRA_STATUS_READ_ONLY); a session derived from a read-only one must
   * carry it too. */
  #define ASTRA_HOST_FS_SESSION_READ_ONLY (1u << 0)
  /** CLAIM flag: release the claim and close every session and handle of
   * the owner. */
  #define ASTRA_HOST_FS_CLAIM_RELEASE     (1u << 0)
  ```

  Keep the WORK/SOUND enum where it was and delete the old flags block
  (`host.h:118-121`).

- Make the snapshot size follow the count, and bump its version. Replace
  `#define ASTRA_HOST_METRICS_VERSION UINT16_C(2)` with `UINT16_C(3)` and
  `#define ASTRA_HOST_METRICS_SNAPSHOT_SIZE 480u` with:

  ```c
  /** Fixed byte size of ::AstraHostMetricsSnapshot for this version. */
  #define ASTRA_HOST_METRICS_SNAPSHOT_SIZE \
      (16u + 8u * (uint32_t)ASTRA_HOST_METRIC_COUNT)
  ```

  and move it below the metric enum.

- Before `ASTRA_HOST_COMMAND_VERSION`, add the node record:

  ```c
  /** Seconds since the epoch, split, and nanoseconds within the second. */
  typedef struct AstraHostTime {
      int32_t seconds_hi;     /**< Signed seconds, upper 32 bits. */
      uint32_t seconds_lo;    /**< Seconds, lower 32 bits. */
      uint32_t nanoseconds;   /**< 0..999999999. */
  } AstraHostTime;

  /** AstraHostNode::valid bits: which fields the provider filled. A field
   * whose bit is clear is unknown, never zero-by-default. */
  enum {
      ASTRA_HOST_NODE_TYPE = 1u << 0,
      ASTRA_HOST_NODE_MODE = 1u << 1,
      ASTRA_HOST_NODE_NLINK = 1u << 2,
      ASTRA_HOST_NODE_UID = 1u << 3,
      ASTRA_HOST_NODE_GID = 1u << 4,
      ASTRA_HOST_NODE_SIZE = 1u << 5,
      ASTRA_HOST_NODE_BLOCKS = 1u << 6,
      ASTRA_HOST_NODE_ATIME = 1u << 7,
      ASTRA_HOST_NODE_MTIME = 1u << 8,
      ASTRA_HOST_NODE_CTIME = 1u << 9,
      ASTRA_HOST_NODE_BTIME = 1u << 10,
      ASTRA_HOST_NODE_INODE = 1u << 11,
      ASTRA_HOST_NODE_DEVICE = 1u << 12,
      ASTRA_HOST_NODE_MOUNT = 1u << 13,
      ASTRA_HOST_NODE_BLOCK_SIZE = 1u << 14
  };

  /** One node's metadata, as statx(2) reports it (docs/FILESYSTEM_PRODUCTION_PRD.md FS22). */
  typedef struct AstraHostNode {
      uint32_t valid;         /**< ASTRA_HOST_NODE_* bits. */
      uint16_t kind;          /**< ASTRA_VFS_KIND_*. */
      uint16_t mode;          /**< st_mode: type and permission bits. */
      uint32_t nlink;         /**< Hard links. */
      uint32_t uid;           /**< Owning user. */
      uint32_t gid;           /**< Owning group. */
      uint32_t block_size;    /**< Preferred I/O size. */
      uint32_t device_hi;     /**< Device major. */
      uint32_t device_lo;     /**< Device minor. */
      uint32_t inode_hi;      /**< Inode number, upper 32 bits. */
      uint32_t inode_lo;      /**< Inode number, lower 32 bits. */
      uint32_t size_hi;       /**< Bytes, upper 32 bits. */
      uint32_t size_lo;       /**< Bytes, lower 32 bits. */
      uint32_t blocks_hi;     /**< 512-byte blocks allocated, upper 32 bits. */
      uint32_t blocks_lo;     /**< 512-byte blocks allocated, lower 32 bits. */
      uint32_t mount_hi;      /**< Linux mount id, upper 32 bits. */
      uint32_t mount_lo;      /**< Linux mount id, lower 32 bits. */
      AstraHostTime atime;    /**< Last access. */
      AstraHostTime mtime;    /**< Last modification. */
      AstraHostTime ctime;    /**< Last status change. */
      AstraHostTime btime;    /**< Creation: tells a reused inode number apart. */
  } AstraHostNode;

  /** @cond ASTRA_INTERNAL */
  _Static_assert(sizeof(AstraHostTime) == 12u, "host time ABI changed");
  _Static_assert(sizeof(AstraHostNode) == 112u, "host node ABI changed");
  /** @endcond */
  ```

- `ASTRA_HOST_COMMAND_VERSION` -> `3u`. Replace `AstraHostCommand`'s fields
  from `volume` to the end (`host.h:443-458`) with:

  ```c
      /** Filesystem: the session the operation acts in (0 only for CLAIM
       * and a root SESSION_OPEN). Every other service: zero. */
      uint32_t session;
      /** Filesystem: DMA-buffer offset of the path operands -- path_length
       * bytes of `path` then path2_length bytes of `path2`, unterminated;
       * zero when both are empty. Every other service: zero. */
      uint32_t path_offset;
      uint16_t path_length;   /**< Bytes of the first path operand. */
      uint16_t path2_length;  /**< Bytes of the second path operand. */
      /** Filesystem: metadata answered by OPEN, STAT and READDIR. */
      AstraHostNode node;
      uint32_t reserved[82];  /**< Reserved; zero. */
  } AstraHostCommand;
  ```

  Rewrite the comment above the struct: paths are operands in the data span
  rather than fixed fields, so the record no longer caps a path.

- [ ] **Step 3: The guest host transport and backend**

`sw/userspace/vfs/include/astra/vfs_host_backend.h`:

```c
typedef struct AstraVfsHostRequest {
    AstraHostCommand *command;
    /* Path operands, copied into the data span by submit. */
    const char *path;
    uint32_t path_length;
    const char *path2;
    uint32_t path2_length;
    uintptr_t private_lane;
    uint32_t private_capacity;
    uint32_t private_producer;
} AstraVfsHostRequest;

typedef struct AstraVfsHostBackend {
    AstraVfsHostTransport *transport;
    uint32_t generation;
    /* The provider session every command names (ASTRA_HOST_FS_SESSION_OPEN). */
    uint32_t session;
} AstraVfsHostBackend;

int astra_vfs_host_init(AstraVfsHostBackend *backend,
                        AstraVfsHostTransport *transport,
                        uint32_t generation, uint32_t session);
```

`sw/userspace/vfs/src/vfs_host_transport.c`, `astra_vfs_host_transport_begin`:
also clear `request->path`, `request->path_length`, `request->path2`,
`request->path2_length`. In `astra_vfs_host_transport_submit`, lay the
operands out ahead of the data:

```c
    uint32_t operands = request->path_length + request->path2_length;
    ...
    if (operands > (ASTRA_HOST_FS_PATH_MAX - 1u) * 2u ||
        request->path_length >= ASTRA_HOST_FS_PATH_MAX ||
        request->path2_length >= ASTRA_HOST_FS_PATH_MAX ||
        (request->path_length != 0u && request->path == NULL) ||
        (request->path2_length != 0u && request->path2 == NULL))
        return ASTRA_VFS_ERR_INVALID;
    data_offset = ASTRA_HOST_CHANNEL_HEADER_SIZE +
                  lane->command_capacity * ASTRA_HOST_COMMAND_SIZE;
    if (data_offset > lane->byte_size ||
        operands > lane->byte_size - data_offset ||
        data_capacity > lane->byte_size - data_offset - operands)
        return ASTRA_VFS_ERR_LIMIT;
    command = request->command;
    command->path_offset = operands != 0u ? data_offset : 0u;
    command->path_length = (uint16_t)request->path_length;
    command->path2_length = (uint16_t)request->path2_length;
    if (request->path_length != 0u)
        memcpy(lane->bytes + data_offset, request->path,
               request->path_length);
    if (request->path2_length != 0u)
        memcpy(lane->bytes + data_offset + request->path_length,
               request->path2, request->path2_length);
    data_offset += operands;
    command->data_offset = data_capacity != 0u ? data_offset : 0u;
```

and use this `data_offset` for the input copy and the output copy below.
The lane must hold operands plus data: callers pass
`data_capacity + operands` to `begin`. `command_begin` in `host.c` (below)
does so.

Keep `astra_vfs_host_transport_execute` and `..._execute_batch` for their
tests and for callers that send operand-free commands. In `publish_command`,
replace the `switch` with
`return source->path_length == 0u && source->path2_length == 0u ? ASTRA_VFS_OK : ASTRA_VFS_ERR_INVALID;`,
and copy the fixed fields up to and including `path2_length`. In
`collect_command`, copy `node` instead of the old stat fields. `publish_path`
goes.

`sw/userspace/vfs/src/backends/host.c`:
- Delete the `_Static_assert` on equal path limits and add
  `_Static_assert(ASTRA_VFS_PATH_MAX <= ASTRA_HOST_FS_PATH_MAX, "a storage path must fit a host path operand");`.
- Replace `copy_path` with `path_operand`, which sets the request's operand:

  ```c
  static uint32_t path_operand(AstraVfsHostRequest *request, int second,
                               const char *path)
  {
      size_t length;

      if (path == NULL)
          return ASTRA_VFS_ERR_INVALID;
      length = strlen(path);
      if (length >= ASTRA_HOST_FS_PATH_MAX)
          return ASTRA_VFS_ERR_INVALID;
      if (second) {
          request->path2 = path;
          request->path2_length = (uint32_t)length;
      } else {
          request->path = path;
          request->path_length = (uint32_t)length;
      }
      return ASTRA_VFS_OK;
  }
  ```

- `command_begin(backend, request, operation, data_capacity, path, path2)`
  begins with capacity `data_capacity + strlen(path) + strlen(path2)` (NULL
  counts 0), stamps `command->session = backend->session`, and sets the
  operands with `path_operand`. Replace the `memset(command, 0, offsetof(AstraHostCommand, path))`
  with `memset(command, 0, sizeof(*command))`.
- `publish_info` reads `command->node`:

  ```c
  static void publish_info(const AstraHostCommand *command,
                           AstraVfsNodeInfo *info)
  {
      const AstraHostNode *node = &command->node;

      memset(info, 0, sizeof(*info));
      info->size = ((uint64_t)node->size_hi << 32) | node->size_lo;
      info->mtime = (int64_t)(((uint64_t)(uint32_t)node->mtime.seconds_hi << 32) |
                              node->mtime.seconds_lo);
      info->uid = node->uid;
      info->gid = node->gid;
      info->kind = node->kind;
      info->mode = node->mode;
      info->nlink = node->nlink > UINT16_MAX ? UINT16_MAX : (uint16_t)node->nlink;
  }
  ```

- Fold the operations:
  - `host_open_at` and `host_open` share `host_open_common`, which now
    always sends `ASTRA_HOST_FS_OPEN` with `handle = directory`;
  - `host_stat_at` sends `ASTRA_HOST_FS_STAT` with `handle = directory`;
  - `host_stat_node` sends `ASTRA_HOST_FS_STAT` with `handle = node` and no
    path;
  - `host_chmod_node` sends `ASTRA_HOST_FS_CHMOD` with `handle = node` and
    no path;
  - `host_chmod_at` sends `ASTRA_HOST_FS_CHMOD` with `handle = directory`
    and `flags = 0`;
  - `host_unlink` sends `ASTRA_HOST_FS_UNLINK` with `flags = 0`;
  - `host_unlink_at` sends `ASTRA_HOST_FS_UNLINK` with `handle = directory`
    and `flags = (flags & ASTRA_VFS_AT_REMOVE_DIRECTORY) != 0u ? ASTRA_HOST_FS_UNLINK_DIRECTORY : ASTRA_HOST_FS_UNLINK_FILE`;
  - `host_two_paths` sets both operands, `handle = 0` and `value_lo = 0`
    (both bases are the session root).
- `astra_vfs_host_init` refuses `session == 0u` and stores it.
- Add the session and claim operations, used by HostFS and Task 12's
  bootstrap client:

  ```c
  static uint32_t control(AstraVfsHostTransport *transport,
                          uint32_t generation, uint16_t operation,
                          uint16_t flags, uint32_t session, uint32_t volume,
                          const char *path, uint32_t *result)
  {
      AstraVfsHostRequest request;
      AstraHostCommand *command;
      size_t length = path != NULL ? strlen(path) : 0u;
      uint32_t status;

      if (length >= ASTRA_HOST_FS_PATH_MAX)
          return ASTRA_VFS_ERR_INVALID;
      status = astra_vfs_host_transport_begin(transport, 0u, (uint32_t)length,
                                              &request);
      if (status != ASTRA_VFS_OK)
          return status;
      command = request.command;
      memset(command, 0, sizeof(*command));
      command->size = sizeof(*command);
      command->version = ASTRA_HOST_COMMAND_VERSION;
      command->service = ASTRA_HOST_SERVICE_FILESYSTEM;
      command->operation = operation;
      command->flags = flags;
      command->generation = generation;
      command->session = session;
      command->value_lo = volume;
      request.path = path;
      request.path_length = (uint32_t)length;
      status = astra_vfs_host_transport_submit(transport, &request, NULL, 0u,
                                               NULL, 0u);
      if (status == ASTRA_VFS_OK && result != NULL)
          *result = command->result_value;
      return status;
  }

  uint32_t astra_vfs_host_claim(AstraVfsHostTransport *transport,
                                uint32_t generation, int release)
  {
      return control(transport, generation, ASTRA_HOST_FS_CLAIM,
                     release ? ASTRA_HOST_FS_CLAIM_RELEASE : 0u, 0u, 0u,
                     NULL, NULL);
  }

  uint32_t astra_vfs_host_session_open(AstraVfsHostTransport *transport,
                                       uint32_t generation, uint32_t parent,
                                       uint32_t volume, const char *root,
                                       uint16_t flags, uint32_t *session)
  {
      uint32_t status;

      if (session == NULL || root == NULL)
          return ASTRA_VFS_ERR_INVALID;
      *session = 0u;
      status = control(transport, generation, ASTRA_HOST_FS_SESSION_OPEN,
                       flags, parent, parent == 0u ? volume : 0u, root,
                       session);
      return status == ASTRA_VFS_OK && *session == 0u ?
          ASTRA_VFS_ERR_PROTOCOL : status;
  }

  uint32_t astra_vfs_host_session_close(AstraVfsHostTransport *transport,
                                        uint32_t generation, uint32_t session)
  {
      return control(transport, generation, ASTRA_HOST_FS_SESSION_CLOSE, 0u,
                     session, 0u, NULL, NULL);
  }
  ```

  Declare the three in `vfs_host_backend.h` (with
  `#include <astra/vfs_host_transport.h>` moved below the typedefs, or a
  forward declaration of `AstraVfsHostTransport`, which the header already
  has).

- [ ] **Step 4: The provider (`emu/qemu/qemu-9.2/hw/m68k/astra68.c`)**

State (`astra68.c:524-598`). Add to `AstraHostFile`:
`uint32_t session;   /* the session it was opened in */`. Add:

```c
typedef struct AstraHostSession {
    int root;               /* directory fd: the confinement root */
    uint32_t owner;         /* the claimed owner that opened it */
    uint32_t volume;
    uint32_t references;
    uint16_t flags;         /* ASTRA_HOST_FS_SESSION_READ_ONLY */
    bool closing;
} AstraHostSession;
```

and to `AstraHostState`:

```c
    GHashTable *sessions;   /* id -> AstraHostSession, under files_lock */
    uint32_t next_session;
    uint32_t fs_owner;      /* the claimed filesystem owner, 0 when free */
```

In `astra68_init` create the table beside `s->host.files`:
`s->host.sessions = g_hash_table_new(g_direct_hash, g_direct_equal);`.

Session lifetime, beside `astra_host_file_acquire` (`astra68.c:2889`):

```c
static AstraHostSession *astra_host_session_acquire(Astra68State *s,
                                                    uint32_t owner,
                                                    uint32_t id)
{
    AstraHostSession *session;

    if (id == 0u)
        return NULL;
    qemu_mutex_lock(&s->host.files_lock);
    session = g_hash_table_lookup(s->host.sessions, GUINT_TO_POINTER(id));
    if (session == NULL || session->owner != owner || session->closing)
        session = NULL;
    else
        ++session->references;
    qemu_mutex_unlock(&s->host.files_lock);
    return session;
}

static void astra_host_session_release(Astra68State *s,
                                       AstraHostSession *session)
{
    bool last;

    qemu_mutex_lock(&s->host.files_lock);
    last = --session->references == 0u;
    qemu_mutex_unlock(&s->host.files_lock);
    if (last) {
        close(session->root);
        g_free(session);
    }
}

/* Closes every session of `owner`, or every session when owner is 0. */
static void astra_host_release_sessions(Astra68State *s, uint32_t owner)
{
    GHashTableIter iterator;
    gpointer key;
    gpointer value;
    GPtrArray *released = g_ptr_array_new();

    qemu_mutex_lock(&s->host.files_lock);
    g_hash_table_iter_init(&iterator, s->host.sessions);
    while (g_hash_table_iter_next(&iterator, &key, &value)) {
        AstraHostSession *session = value;

        if (owner != 0u && session->owner != owner)
            continue;
        session->closing = true;
        g_hash_table_iter_steal(&iterator);
        g_ptr_array_add(released, session);
    }
    qemu_mutex_unlock(&s->host.files_lock);
    for (guint index = 0u; index < released->len; ++index)
        astra_host_session_release(s, g_ptr_array_index(released, index));
    g_ptr_array_free(released, true);
}
```

`astra_host_release_owner` (`:2953`) calls
`astra_host_release_sessions(s, owner);` after `astra_host_release_files`,
and clears the claim:
`if (qatomic_read(&s->host.fs_owner) == owner) qatomic_set(&s->host.fs_owner, 0u);`.
`astra_host_reset` (`:4650`) calls `astra_host_release_sessions(s, 0u);` and
`qatomic_set(&s->host.fs_owner, 0u);`.

Operands, beside `astra_host_command_data` (`:3294`):

```c
/*
 * The path operands, copied out of the DMA span into NUL-terminated
 * buffers. A path is bytes, not a C string: an embedded NUL, a length of
 * ASTRA_HOST_FS_PATH_MAX or more, or a range outside the buffer is INVALID.
 */
static uint32_t astra_host_fs_operands(Astra68State *s, uint32_t physical,
                                       uint32_t bytes, uint32_t command_bytes,
                                       const uint8_t *command, char **path,
                                       char **path2)
{
    uint32_t offset = ldl_be_p(command + HOST_FIELD(path_offset));
    uint32_t first = lduw_be_p(command + HOST_FIELD(path_length));
    uint32_t second = lduw_be_p(command + HOST_FIELD(path2_length));
    uint8_t *span = NULL;

    *path = NULL;
    *path2 = NULL;
    if (first >= ASTRA_HOST_FS_PATH_MAX || second >= ASTRA_HOST_FS_PATH_MAX)
        return ASTRA_STATUS_INVALID;
    if (first + second == 0u) {
        if (offset != 0u)
            return ASTRA_STATUS_INVALID;
    } else {
        if (offset < command_bytes)
            return ASTRA_STATUS_INVALID;
        span = astra_dma_data(s, physical, bytes, offset, first + second);
        if (span == NULL || memchr(span, 0, first + second) != NULL)
            return ASTRA_STATUS_INVALID;
    }
    *path = g_malloc(first + 1u);
    *path2 = g_malloc(second + 1u);
    if (first != 0u)
        memcpy(*path, span, first);
    (*path)[first] = '\0';
    if (second != 0u)
        memcpy(*path2, span + first, second);
    (*path2)[second] = '\0';
    return ASTRA_STATUS_OK;
}
```

`astra_host_path_terminated` goes. `astra_host_path_valid` and
`astra_host_relative_path_valid` check `strlen(path) < ASTRA_HOST_FS_PATH_MAX`
in its place: their input is now a terminated copy. The copies are exactly
as long as their path, so every `memcpy(copy, path, sizeof(copy))`
(`astra_host_open_parent_walk` `:3094`, `astra_host_open_parent` `:3165`)
must become `memcpy(copy, path, strlen(path) + 1u)`. The old form read 192
bytes of a fixed field; on a heap copy it would read past the end. Every
`char leaf[ASTRA_HOST_FS_PATH_MAX]` and `char copy[ASTRA_HOST_FS_PATH_MAX]`
is now 4 KiB on the stack of a thread-pool worker or the vCPU thread. Both
have megabyte stacks.

The node record replaces `astra_host_publish_stat` (`:3233-3251`):

```c
#define NODE_FIELD(field) (HOST_FIELD(node) + offsetof(AstraHostNode, field))

static uint16_t astra_host_kind(uint32_t mode)
{
    return S_ISREG(mode) ? ASTRA_VFS_KIND_FILE :
           S_ISDIR(mode) ? ASTRA_VFS_KIND_DIRECTORY :
           S_ISLNK(mode) ? ASTRA_VFS_KIND_SYMLINK : ASTRA_VFS_KIND_UNKNOWN;
}

static void astra_host_put_time(uint8_t *command, size_t field,
                                int64_t seconds, uint32_t nanoseconds)
{
    stl_be_p(command + field, (uint32_t)((uint64_t)seconds >> 32));
    stl_be_p(command + field + 4u, (uint32_t)seconds);
    stl_be_p(command + field + 8u, nanoseconds);
}

/*
 * No-follow metadata of `path` beneath `directory` -- of `directory` itself
 * when `path` is "" -- into the command's node record. statx(2) on Linux:
 * nanosecond times, the birth time that tells a reused inode number apart,
 * and the mount id where the C library knows it. Elsewhere: stat(2)'s
 * fields with whole seconds, and no birth time.
 */
static int astra_host_publish_node(uint8_t *command, int directory,
                                   const char *path)
{
    uint32_t valid = 0u;
#ifdef CONFIG_LINUX
    struct statx sx;
    unsigned mask = STATX_BASIC_STATS | STATX_BTIME;
    static const struct { unsigned statx; uint32_t node; } bits[] = {
        {STATX_TYPE, ASTRA_HOST_NODE_TYPE}, {STATX_MODE, ASTRA_HOST_NODE_MODE},
        {STATX_NLINK, ASTRA_HOST_NODE_NLINK}, {STATX_UID, ASTRA_HOST_NODE_UID},
        {STATX_GID, ASTRA_HOST_NODE_GID}, {STATX_SIZE, ASTRA_HOST_NODE_SIZE},
        {STATX_BLOCKS, ASTRA_HOST_NODE_BLOCKS},
        {STATX_ATIME, ASTRA_HOST_NODE_ATIME},
        {STATX_MTIME, ASTRA_HOST_NODE_MTIME},
        {STATX_CTIME, ASTRA_HOST_NODE_CTIME},
        {STATX_BTIME, ASTRA_HOST_NODE_BTIME}, {STATX_INO, ASTRA_HOST_NODE_INODE},
    };

# ifdef STATX_MNT_ID
    mask |= STATX_MNT_ID;
# endif
    if (statx(directory, path,
              AT_SYMLINK_NOFOLLOW | (path[0] == '\0' ? AT_EMPTY_PATH : 0),
              mask, &sx) != 0)
        return -1;
    for (size_t index = 0u; index < ARRAY_SIZE(bits); ++index)
        if ((sx.stx_mask & bits[index].statx) != 0u)
            valid |= bits[index].node;
    valid |= ASTRA_HOST_NODE_DEVICE | ASTRA_HOST_NODE_BLOCK_SIZE;
    stw_be_p(command + NODE_FIELD(kind), astra_host_kind(sx.stx_mode));
    stw_be_p(command + NODE_FIELD(mode), sx.stx_mode);
    stl_be_p(command + NODE_FIELD(nlink), sx.stx_nlink);
    stl_be_p(command + NODE_FIELD(uid), sx.stx_uid);
    stl_be_p(command + NODE_FIELD(gid), sx.stx_gid);
    stl_be_p(command + NODE_FIELD(block_size), sx.stx_blksize);
    stl_be_p(command + NODE_FIELD(device_hi), sx.stx_dev_major);
    stl_be_p(command + NODE_FIELD(device_lo), sx.stx_dev_minor);
    astra_host_put64(command, NODE_FIELD(inode_hi), sx.stx_ino);
    astra_host_put64(command, NODE_FIELD(size_hi), sx.stx_size);
    astra_host_put64(command, NODE_FIELD(blocks_hi), sx.stx_blocks);
# ifdef STATX_MNT_ID
    if ((sx.stx_mask & STATX_MNT_ID) != 0u) {
        valid |= ASTRA_HOST_NODE_MOUNT;
        astra_host_put64(command, NODE_FIELD(mount_hi), sx.stx_mnt_id);
    }
# endif
    astra_host_put_time(command, NODE_FIELD(atime), sx.stx_atime.tv_sec,
                        sx.stx_atime.tv_nsec);
    astra_host_put_time(command, NODE_FIELD(mtime), sx.stx_mtime.tv_sec,
                        sx.stx_mtime.tv_nsec);
    astra_host_put_time(command, NODE_FIELD(ctime), sx.stx_ctime.tv_sec,
                        sx.stx_ctime.tv_nsec);
    astra_host_put_time(command, NODE_FIELD(btime), sx.stx_btime.tv_sec,
                        sx.stx_btime.tv_nsec);
#else
    struct stat st;

    if ((path[0] == '\0' ? fstat(directory, &st) :
         fstatat(directory, path, &st, AT_SYMLINK_NOFOLLOW)) != 0)
        return -1;
    valid = ASTRA_HOST_NODE_TYPE | ASTRA_HOST_NODE_MODE |
            ASTRA_HOST_NODE_NLINK | ASTRA_HOST_NODE_UID |
            ASTRA_HOST_NODE_GID | ASTRA_HOST_NODE_SIZE |
            ASTRA_HOST_NODE_BLOCKS | ASTRA_HOST_NODE_ATIME |
            ASTRA_HOST_NODE_MTIME | ASTRA_HOST_NODE_CTIME |
            ASTRA_HOST_NODE_INODE | ASTRA_HOST_NODE_DEVICE |
            ASTRA_HOST_NODE_BLOCK_SIZE;
    stw_be_p(command + NODE_FIELD(kind), astra_host_kind(st.st_mode));
    stw_be_p(command + NODE_FIELD(mode), st.st_mode);
    stl_be_p(command + NODE_FIELD(nlink), st.st_nlink);
    stl_be_p(command + NODE_FIELD(uid), st.st_uid);
    stl_be_p(command + NODE_FIELD(gid), st.st_gid);
    stl_be_p(command + NODE_FIELD(block_size), st.st_blksize);
    astra_host_put64(command, NODE_FIELD(device_hi), (uint64_t)st.st_dev);
    astra_host_put64(command, NODE_FIELD(inode_hi), st.st_ino);
    astra_host_put64(command, NODE_FIELD(size_hi), st.st_size);
    astra_host_put64(command, NODE_FIELD(blocks_hi), st.st_blocks);
    astra_host_put_time(command, NODE_FIELD(atime), st.st_atime, 0u);
    astra_host_put_time(command, NODE_FIELD(mtime), st.st_mtime, 0u);
    astra_host_put_time(command, NODE_FIELD(ctime), st.st_ctime, 0u);
#endif
    stl_be_p(command + NODE_FIELD(valid), valid);
    return 0;
}
```

Every `astra_host_publish_stat(command, &st)` becomes a call of
`astra_host_publish_node` with the same directory and name the `stat` used:
`astra_host_publish_open` passes `(fd, "")` after its `fstat`; the STAT
paths pass `(parent, leaf)` or `(file->fd, "")`; READDIR passes
`(dirfd(directory), entry->d_name)`. A failed publish answers
`astra_host_status_from_errno(errno)`.

Rewrite `astra_host_execute_fs` (`:4059-4648`) around a session. Its
prologue:

```c
static void astra_host_execute_fs(Astra68State *s, uint32_t owner,
                                  uint8_t *command, uint32_t physical,
                                  uint32_t bytes, uint32_t command_bytes,
                                  uint32_t expected_generation)
{
    uint16_t operation = lduw_be_p(command + HOST_FIELD(operation));
    uint16_t flags = lduw_be_p(command + HOST_FIELD(flags));
    uint32_t handle = ldl_be_p(command + HOST_FIELD(handle));
    uint32_t session_id = ldl_be_p(command + HOST_FIELD(session));
    uint32_t operation_index = 0u;
    uint64_t started_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    AstraHostSession *session = NULL;
    AstraHostFile *file = NULL;
    char *path = NULL;
    char *path2 = NULL;
    uint32_t status = ASTRA_STATUS_INVALID;
    int root;
    int base;

    stl_be_p(command + HOST_FIELD(status), 0u);
    stl_be_p(command + HOST_FIELD(result_length), 0u);
    stl_be_p(command + HOST_FIELD(result_value), 0u);
    memset(command + HOST_FIELD(node), 0, sizeof(AstraHostNode));
    if (ldl_be_p(command + HOST_FIELD(size)) != ASTRA_HOST_COMMAND_SIZE ||
        lduw_be_p(command + HOST_FIELD(version)) !=
            ASTRA_HOST_COMMAND_VERSION ||
        lduw_be_p(command + HOST_FIELD(service)) !=
            ASTRA_HOST_SERVICE_FILESYSTEM ||
        ldl_be_p(command + HOST_FIELD(generation)) != expected_generation) {
        qatomic_inc(&s->host.operation_counts[0]);
        goto done;
    }
    /* Unknown before claimed: an operation code is not authority, and the
     * raw channel benchmarks time exactly this refusal. */
    if (operation == 0u || operation > ASTRA_HOST_FS_MAX) {
        qatomic_inc(&s->host.operation_counts[0]);
        status = ASTRA_STATUS_UNSUPPORTED;
        goto done;
    }
    operation_index = operation;
    qatomic_inc(&s->host.operation_counts[operation_index]);
    for (size_t index = HOST_FIELD(reserved);
         index < ASTRA_HOST_COMMAND_SIZE; ++index)
        if (command[index] != 0u)
            goto done;
    if (operation == ASTRA_HOST_FS_CLAIM) {
        status = astra_host_fs_claim(s, owner, flags);
        goto done;
    }
    /* One owner speaks for the filesystem: the service that enforces the
     * grants. Every other HOST_DEVICE holder is refused here. */
    if (owner == 0u || owner != qatomic_read(&s->host.fs_owner)) {
        status = ASTRA_STATUS_ACCESS;
        goto done;
    }
    status = astra_host_fs_operands(s, physical, bytes, command_bytes,
                                    command, &path, &path2);
    if (status != ASTRA_STATUS_OK)
        goto done;
    if (operation == ASTRA_HOST_FS_SESSION_OPEN) {
        status = astra_host_fs_session_open(s, owner, command, flags, path);
        goto done;
    }
    session = astra_host_session_acquire(s, owner, session_id);
    if (session == NULL) {
        status = ASTRA_STATUS_BAD_HANDLE;
        goto done;
    }
    if (operation == ASTRA_HOST_FS_SESSION_CLOSE) {
        status = astra_host_fs_session_close(s, owner, session_id);
        goto done;
    }
    if ((session->flags & ASTRA_HOST_FS_SESSION_READ_ONLY) != 0u &&
        astra_host_fs_mutates(operation, flags)) {
        status = ASTRA_STATUS_READ_ONLY;
        goto done;
    }
    root = session->root;
    ...
```

The epilogue releases what the prologue acquired:

```c
done:
    if (file != NULL)
        astra_host_file_unlock(s, file);
    if (session != NULL)
        astra_host_session_release(s, session);
    g_free(path);
    g_free(path2);
    stl_be_p(command + HOST_FIELD(status), status);
    qatomic_add(&s->host.operation_execution_ns[operation_index],
                qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - started_ns);
}
```

In every operation case, `file` is acquired with `astra_host_file_lock`
and released only by the epilogue. Delete each case's own
`if (file != NULL) astra_host_file_unlock(s, file);`. A handle opened in one
session is not usable in another, even by the same owner:

```c
/* `handle`, locked, when it belongs to `session_id`; NULL otherwise. */
static AstraHostFile *astra_host_session_file(Astra68State *s,
                                              uint32_t owner,
                                              uint32_t session_id,
                                              uint32_t handle)
{
    AstraHostFile *file = astra_host_file_lock(s, owner, handle);

    if (file != NULL && file->session != session_id) {
        astra_host_file_unlock(s, file);
        file = NULL;
    }
    return file;
}
```

`astra_host_insert_file(s, owner, session_id, fd, directory, flags)` takes
the session id and stores it in the file, and
`astra_host_publish_open(s, owner, session_id, command, fd, flags)` passes
it through.

The new helpers:

```c
static bool astra_host_fs_mutates(uint16_t operation, uint16_t flags)
{
    switch (operation) {
    case ASTRA_HOST_FS_OPEN:
        return (flags & (ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                         ASTRA_VFS_OPEN_TRUNCATE | ASTRA_VFS_OPEN_APPEND)) != 0u;
    case ASTRA_HOST_FS_WRITE: case ASTRA_HOST_FS_TRUNCATE:
    case ASTRA_HOST_FS_MKDIR: case ASTRA_HOST_FS_UNLINK:
    case ASTRA_HOST_FS_RENAME: case ASTRA_HOST_FS_CHMOD:
    case ASTRA_HOST_FS_SYMLINK: case ASTRA_HOST_FS_LINK:
        return true;
    default:
        return false;
    }
}

static uint32_t astra_host_fs_claim(Astra68State *s, uint32_t owner,
                                    uint16_t flags)
{
    if (owner == 0u || (flags & ~ASTRA_HOST_FS_CLAIM_RELEASE) != 0u)
        return ASTRA_STATUS_INVALID;
    if ((flags & ASTRA_HOST_FS_CLAIM_RELEASE) != 0u) {
        if (qatomic_cmpxchg(&s->host.fs_owner, owner, 0u) != owner)
            return ASTRA_STATUS_ACCESS;
        astra_host_release_files(s, owner);
        astra_host_release_sessions(s, owner);
        return ASTRA_STATUS_OK;
    }
    if (qatomic_cmpxchg(&s->host.fs_owner, 0u, owner) == 0u ||
        qatomic_read(&s->host.fs_owner) == owner)
        return ASTRA_STATUS_OK;
    return ASTRA_STATUS_ACCESS;
}

/*
 * A root session opens a volume's directory; a derived session opens a
 * directory beneath its parent's root, and inherits read-only. The root is
 * an O_PATH descriptor on Linux: it names a directory and grants nothing
 * by itself.
 */
static uint32_t astra_host_fs_session_open(Astra68State *s, uint32_t owner,
                                           uint8_t *command, uint16_t flags,
                                           const char *path)
{
    uint32_t parent_id = ldl_be_p(command + HOST_FIELD(session));
    uint32_t volume = ldl_be_p(command + HOST_FIELD(value_lo));
    AstraHostSession *parent = NULL;
    AstraHostSession *session;
    uint32_t id;
    int base;
    int root;
#ifdef CONFIG_LINUX
    const int directory_flags = O_PATH | O_DIRECTORY;
#else
    const int directory_flags = O_RDONLY | O_DIRECTORY;
#endif

    if ((flags & ~ASTRA_HOST_FS_SESSION_READ_ONLY) != 0u ||
        ldl_be_p(command + HOST_FIELD(handle)) != 0u)
        return ASTRA_STATUS_INVALID;
    if (parent_id == 0u) {
        if (volume >= ASTRA_HOST_FS_VOLUME_COUNT ||
            s->host.volume_fds[volume] < 0)
            return ASTRA_STATUS_INVALID;
        base = s->host.volume_fds[volume];
    } else {
        if (volume != 0u)
            return ASTRA_STATUS_INVALID;
        parent = astra_host_session_acquire(s, owner, parent_id);
        if (parent == NULL)
            return ASTRA_STATUS_BAD_HANDLE;
        flags |= parent->flags & ASTRA_HOST_FS_SESSION_READ_ONLY;
        volume = parent->volume;
        base = parent->root;
    }
    root = astra_host_open_path(base, path[0] == '\0' ? "/" : path,
                                directory_flags, 0);
    if (parent != NULL)
        astra_host_session_release(s, parent);
    if (root < 0)
        return astra_host_status_from_errno(errno);
    session = g_new0(AstraHostSession, 1);
    session->root = root;
    session->owner = owner;
    session->volume = volume;
    session->flags = flags;
    session->references = 1u;
    qemu_mutex_lock(&s->host.files_lock);
    do {
        astra_network_next(&s->host.next_session);
        id = s->host.next_session;
    } while (g_hash_table_contains(s->host.sessions, GUINT_TO_POINTER(id)));
    g_hash_table_insert(s->host.sessions, GUINT_TO_POINTER(id), session);
    qemu_mutex_unlock(&s->host.files_lock);
    stl_be_p(command + HOST_FIELD(result_value), id);
    return ASTRA_STATUS_OK;
}

static uint32_t astra_host_fs_session_close(Astra68State *s, uint32_t owner,
                                            uint32_t id)
{
    AstraHostSession *session;

    qemu_mutex_lock(&s->host.files_lock);
    session = g_hash_table_lookup(s->host.sessions, GUINT_TO_POINTER(id));
    if (session == NULL || session->owner != owner || session->closing) {
        qemu_mutex_unlock(&s->host.files_lock);
        return ASTRA_STATUS_BAD_HANDLE;
    }
    session->closing = true;
    g_hash_table_steal(s->host.sessions, GUINT_TO_POINTER(id));
    qemu_mutex_unlock(&s->host.files_lock);
    astra_host_session_release(s, session);
    return ASTRA_STATUS_OK;
}
```

`astra_host_open_path` (`:3184`) opens with `flags | O_NOFOLLOW`, and
`O_PATH | O_DIRECTORY | O_NOFOLLOW` is valid. Its "/" case returns `dup(root)`;
for `O_PATH` that keeps the root's own mode, which is what a session wants.

The cases, with `base` the directory the path is relative to:

```c
    if (handle != 0u &&
        operation != ASTRA_HOST_FS_CLOSE && operation != ASTRA_HOST_FS_READ &&
        operation != ASTRA_HOST_FS_WRITE && operation != ASTRA_HOST_FS_SYNC &&
        operation != ASTRA_HOST_FS_TRUNCATE &&
        operation != ASTRA_HOST_FS_READDIR) {
        file = astra_host_session_file(s, owner, session_id, handle);
        if (file == NULL) {
            status = ASTRA_STATUS_BAD_HANDLE;
            goto done;
        }
        if (path[0] != '\0' && file->directory == NULL) {
            status = ASTRA_STATUS_NOT_DIR;
            goto done;
        }
    }
    base = file != NULL ? file->fd : -1;
```

Then, per operation (old case -> new case):

| v3 operation | `handle == 0` (absolute path beneath the session root) | `handle != 0` |
|---|---|---|
| OPEN | old OPEN with `root` | old OPEN_AT with `base` (path relative, non-empty) |
| CLOSE, READ, WRITE, SYNC, TRUNCATE | unchanged, with `astra_host_session_file` in place of `astra_host_file_lock` | -- |
| STAT | old STAT with `root` | empty path: old STAT_FILE (`astra_host_publish_node(command, base, "")`); otherwise old STAT_AT |
| READDIR | old path READDIR with `root` | old handle READDIR (path must be empty); the handle must be in this session |
| MKDIR, READLINK | old code with `astra_host_open_parent(root, path, ...)` | `astra_host_open_relative_parent(base, path, ...)` |
| UNLINK | old UNLINK with `root`; then the flags below | old UNLINK_AT with `base`; then the flags below |
| RENAME, LINK | old code with `root` for both when `handle == 0` and `value_lo == 0` | each side resolves against its own base (`handle` for `path`, directory handle `value_lo` for `path2`, both in this session), absolute when its base is 0 and relative otherwise |
| CHMOD | old CHMOD with `root` | empty path: `fchmod(base, ...)` (old CHMOD_FILE); otherwise old CHMOD_AT. `flags` must be 0 |
| SYMLINK | old SYMLINK with the link path `path2` beneath `root` | link path relative beneath `base` |
| FILESYSTEM_INFO | old code with `root` | `fstatvfs(base)` when the path is empty, otherwise the path relative beneath `base` |

UNLINK's flags. `ASTRA_HOST_FS_UNLINK_DIRECTORY` uses `AT_REMOVEDIR`.
`ASTRA_HOST_FS_UNLINK_FILE` uses `0`. Neither flag uses `AT_REMOVEDIR` when
`fstatat(..., AT_SYMLINK_NOFOLLOW)` says directory. Both flags, or any other
bit, is INVALID.

Write the two-base resolution once and use it in RENAME and LINK:

```c
/* The parent directory and leaf of `path` beneath `base_handle` (an open
 * directory of this session) or, when it is 0, of the absolute `path`
 * beneath the session root. On success *parent is an fd to close and
 * *held, when not NULL, a locked file to unlock. */
static int astra_host_fs_parent(Astra68State *s, uint32_t owner,
                                uint32_t session_id, int root,
                                uint32_t base_handle, const char *path,
                                int *parent, char leaf[ASTRA_HOST_FS_PATH_MAX],
                                AstraHostFile **held)
{
    *held = NULL;
    if (base_handle == 0u)
        return astra_host_open_parent(root, path, parent, leaf);
    *held = astra_host_session_file(s, owner, session_id, base_handle);
    if (*held == NULL || (*held)->directory == NULL) {
        if (*held != NULL)
            astra_host_file_unlock(s, *held);
        *held = NULL;
        errno = EBADF;
        return -1;
    }
    return astra_host_open_relative_parent((*held)->fd, path, parent, leaf);
}
```

RENAME and LINK reject the same handle locked twice: when both bases name
the same nonzero handle, resolve the second path with the first `held`.
Deadlock is impossible, because a single file mutex is never taken twice.
`astra_host_status_from_errno(EBADF)` must answer `ASTRA_STATUS_BAD_HANDLE`:
add `case EBADF: return ASTRA_STATUS_BAD_HANDLE;` to `:2977-3006`.

The other services. In `astra_host_execute_metrics` (`:3330-3353`),
`astra_host_execute_remote_desktop` (`:3504-3537`),
`astra_host_execute_entropy` (`:3569-3600`) and `astra_host_execute_audio`
(`:3672-3700`), replace every per-field zero test from `volume` through
`reserved1`, and the loop from `HOST_FIELD(path)`, with one loop from
`HOST_FIELD(session)` to `ASTRA_HOST_COMMAND_SIZE`. Their `memset` of the
result area becomes `stl_be_p(command + HOST_FIELD(result_length), 0u); stl_be_p(command + HOST_FIELD(result_value), 0u);`
as entropy and remote desktop already do.

The channel's data-safety check (`astra_host_channel_data_safe`,
`:5072-5151`) must also see path operands, as read-only ranges. Before
`if (service == ASTRA_HOST_SERVICE_FILESYSTEM && operation == ASTRA_HOST_FS_WRITE)`
add:

```c
        if (service == ASTRA_HOST_SERVICE_FILESYSTEM) {
            uint32_t operands = lduw_be_p(command + HOST_FIELD(path_length)) +
                                lduw_be_p(command + HOST_FIELD(path2_length));
            uint32_t at = ldl_be_p(command + HOST_FIELD(path_offset));

            if (operands != 0u && at >= command_bytes &&
                at <= channel->byte_size &&
                operands <= channel->byte_size - at)
                ranges[range_count++] = (AstraHostDataRange){
                    at, at + operands, false
                };
        }
```

and allocate `ranges` for `2 * outstanding` entries.

Capabilities. In the 0x888 register read (`:5946-5955`) add
`ASTRA_HOST_CAP_FS_SESSIONS` to the set. In `astra_vfs_host_transport_init`
(`vfs_host_transport.c:160-168`) require it beside
`ASTRA_HOST_CAP_FILESYSTEM | ASTRA_HOST_CAP_OWNER_SCOPED`.

Registration (Global Constraints). The QOM property table (search
`astra-host-fs-invalid`) becomes, in operation order: `invalid, open, close,
read, write, sync, truncate, stat, readdir, mkdir, unlink, rename, chmod,
readlink, symlink, link, filesystem-info, session-open, session-close,
claim`, each with its `-execution-ns` twin.

- [ ] **Step 4b: Scopes name sessions; the creator is the claimed owner**

From record 3 a scope's provider word is a *session*, not a volume. QEMU
binds a scope only when the claimed filesystem owner opened that session,
and only when the scope's creator is that owner. This is the provider's
half of the creator rule; the kernel's `ASTRA_RIGHT_HOST_SCOPE` (Task 3b)
is the other.

`astra_host_config_scope` (Task 3b). For a bound OPEN and an ATTACH,
replace the volume test (`scope->scope < ASTRA_HOST_FS_VOLUME_COUNT && s->host.volume_fds[scope->scope] >= 0`)
with:

```c
               scope->owner == qatomic_read(&s->host.fs_owner) &&
               astra_host_session_live(s, scope->owner, scope->scope);
```

and add, beside the session helpers:

```c
static bool astra_host_session_live(Astra68State *s, uint32_t owner,
                                    uint32_t id)
{
    AstraHostSession *session = astra_host_session_acquire(s, owner, id);

    if (session == NULL)
        return false;
    astra_host_session_release(s, session);
    return true;
}
```

The prologue of `astra_host_execute_fs`, from the reserved-bytes loop to the
session acquisition, becomes:

```c
    uint32_t session_owner;
    AstraHostChannelScope scope;
    bool bound = channel != NULL && channel->bound;
    ...
    if (bound) {
        /* A bound channel names one of its kernel-attached scopes by the
         * command's session, never the claim or another session's; its
         * owner field does not exist, and the submitting owner keys only
         * the handles it opens (FS15, FS44). */
        if (operation == ASTRA_HOST_FS_CLAIM ||
            operation == ASTRA_HOST_FS_SESSION_OPEN ||
            operation == ASTRA_HOST_FS_SESSION_CLOSE ||
            !astra_host_channel_scope(s, channel, session_id, &scope) ||
            scope.owner != qatomic_read(&s->host.fs_owner)) {
            status = ASTRA_STATUS_ACCESS;
            goto done;
        }
        if ((scope.flags & ASTRA_HOST_SCOPE_READ_ONLY) != 0u &&
            astra_host_fs_mutates(operation, flags)) {
            status = ASTRA_STATUS_READ_ONLY;
            goto done;
        }
        qatomic_inc(&s->host.scoped_commands);
        session_owner = scope.owner;
    } else {
        if (operation == ASTRA_HOST_FS_CLAIM) {
            status = astra_host_fs_claim(s, owner, flags);
            goto done;
        }
        /* One owner speaks for the filesystem: the service that enforces
         * the grants. Every other HOST_DEVICE holder is refused here. */
        if (owner == 0u || owner != qatomic_read(&s->host.fs_owner)) {
            status = ASTRA_STATUS_ACCESS;
            goto done;
        }
        session_owner = owner;
    }
    status = astra_host_fs_operands(s, physical, bytes, command_bytes,
                                    command, &path, &path2);
    if (status != ASTRA_STATUS_OK)
        goto done;
    if (operation == ASTRA_HOST_FS_SESSION_OPEN) {
        status = astra_host_fs_session_open(s, owner, command, flags, path);
        goto done;
    }
    session = astra_host_session_acquire(s, session_owner, session_id);
```

The remainder is Step 4's prologue: the SESSION_CLOSE branch, the session's
own read-only test, `root = session->root;`. Task 3b's volume-based bound
check in this function goes; this block replaces it. Every handle the
command opens is still inserted for `owner`, the submitting process, so its
death releases them (`astra_host_release_owner`). Each is tagged with
`session_id`.

Handles are tagged with their session. Rename Task 3b's tag throughout:
`AstraHostFile.volume` becomes `session`, and
`astra_host_file_lock(s, owner, volume, handle)` becomes
`astra_host_session_file(s, owner, session_id, handle)` with the same body.
The Step 4 text above calls it by that name. Do the same for
`astra_host_close_file`, `astra_host_insert_file` and
`astra_host_publish_open`, whose tag argument is now `session_id`. There is
no separate wrapper.

A session's handles go with it, whoever opened them. Revoking a grant
(Task 5) or losing HostFS must leave nothing usable behind:

```c
/* Closes every handle tagged with `id`, of every owner. */
static void astra_host_release_session_files(Astra68State *s, uint32_t id)
{
    GHashTableIter iterator;
    gpointer key;
    gpointer value;
    GPtrArray *released = g_ptr_array_new();

    qemu_mutex_lock(&s->host.files_lock);
    g_hash_table_iter_init(&iterator, s->host.files);
    while (g_hash_table_iter_next(&iterator, &key, &value)) {
        AstraHostFile *file = value;

        if (file->session != id || file->closing)
            continue;
        file->closing = true;
        g_hash_table_iter_steal(&iterator);
        g_ptr_array_add(released, file);
    }
    qemu_mutex_unlock(&s->host.files_lock);
    for (guint index = 0u; index < released->len; ++index)
        astra_host_file_release(s, g_ptr_array_index(released, index));
    g_ptr_array_free(released, true);
}
```

`astra_host_fs_session_close` calls `astra_host_release_session_files(s, id);`
after it steals the session. `astra_host_release_sessions` records each
released session's id and does the same for each. The table key is the
id, so collect `GPOINTER_TO_UINT(key)` beside the session pointer.
`astra_host_reset` already releases every file.

`astra_host_fs_mutates` (Task 3b) is rewritten for record 3's operation
set, as Step 4's helper list shows. Delete the old one.

- [ ] **Step 5: Consumers**

- `sw/userspace/services/hostfs/main.c`:
  - `fs_group_names` becomes `"host.fs.invalid", "host.fs.open",
    "host.fs.close", "host.fs.read", "host.fs.write", "host.fs.sync",
    "host.fs.truncate", "host.fs.stat", "host.fs.readdir", "host.fs.mkdir",
    "host.fs.unlink", "host.fs.rename", "host.fs.chmod", "host.fs.readlink",
    "host.fs.symlink", "host.fs.link", "host.fs.filesystem_info",
    "host.fs.session_open", "host.fs.session_close", "host.fs.claim"`.
  - After `astra_vfs_host_transport_init`, claim the service, then open one
    root session per volume:

    ```c
        uint32_t work_session = 0u;
        uint32_t sound_session = 0u;
        ...
        if (status == ASTRA_STATUS_OK &&
            (astra_vfs_host_claim(&transport, transport.generation, 0) !=
                 ASTRA_VFS_OK ||
             astra_vfs_host_session_open(&transport, transport.generation, 0u,
                                         ASTRA_HOST_FS_VOLUME_WORK, "/", 0u,
                                         &work_session) != ASTRA_VFS_OK ||
             astra_vfs_host_session_open(&transport, transport.generation, 0u,
                                         ASTRA_HOST_FS_VOLUME_SOUND, "/", 0u,
                                         &sound_session) != ASTRA_VFS_OK))
            status = HOSTFS_FAIL_DEVICE;
    ```

  - `astra_vfs_host_init(&backend, &transport, transport.generation, work_session)`
    and likewise `sound_session` for SOUND.
  - Task 3b's scopes name volumes. They now name the sessions, and are
    created after them:
    `astra_host_scope_create(device_handle, work_session, 0u, &work_scope)`
    and the same for `sound_session`. The session must exist before its
    scope can be bound (Step 4b).
- `sw/userspace/vfs/src/vfs_host_direct.c`: the lane's backend is
  initialised with `lease.scope`, now the session. `astra_vfs_host_init`
  refuses 0, and a scope never names session 0. Every command the lane
  sends names its scope's session, which is how QEMU selects among the
  scopes bound to the thread's channel.
- `sw/userspace/commands/fssem/fssem.c`: `raw-lane` moves to record 3.
  Replace Task 3b's `RawLane` and `raw_lane_thread` with a general helper.
  Task 5 reuses it.

  ```c
  typedef struct LaneCall {
      uint32_t scope;
      uint32_t session;
      uint16_t service;
      uint16_t operation;
      uint16_t flags;
      const char *path;
      uint32_t error;
      uint32_t status;
  } LaneCall;

  static void *lane_call_thread(void *argument)
  {
      LaneCall *call = argument;
      AstraHostChannelClient channel;
      AstraHostCommand *command;
      size_t length = strlen(call->path);

      memset(&channel, 0, sizeof(channel));
      call->error = astra_host_client_open(call->scope,
                                           ASTRA_HOST_CAP_FS_SCOPE, 256u,
                                           &channel);
      if (call->error != ASTRA_SYSCALL_OK)
          return NULL;
      command = astra_host_client_prepare(&channel, call->service,
                                          call->operation);
      command->flags = call->flags;
      command->session = call->service == ASTRA_HOST_SERVICE_FILESYSTEM ?
                             call->session : 0u;
      command->value_lo = call->operation == ASTRA_HOST_FS_OPEN ? 0600u :
                                                                  0755u;
      if (call->service == ASTRA_HOST_SERVICE_ENTROPY) {
          command->data_capacity = 32u;
      } else {
          command->data_offset = 0u;
          if (length != 0u) {
              memcpy((void *)(uintptr_t)channel.data, call->path, length);
              command->path_offset = ASTRA_HOST_CHANNEL_HEADER_SIZE +
                                     ASTRA_HOST_COMMAND_SIZE;
              command->path_length = (uint16_t)length;
          }
      }
      call->error = astra_host_client_submit(&channel);
      if (call->error == ASTRA_SYSCALL_OK)
          call->status = command->status;
      (void)astra_host_client_close(&channel);
      return NULL;
  }

  /* One raw command on a lane of its own -- a thread has one host channel --
   * through `scope`: what a hostile program can send with a scope it holds.
   * Answers the provider's status, or ASTRA_STATUS_IO when the transport
   * itself failed. */
  static uint32_t lane_call(uint32_t scope, uint32_t session,
                            uint16_t service, uint16_t operation,
                            uint16_t flags, const char *path)
  {
      LaneCall call = {scope, session, service, operation, flags, path,
                       0u, 0u};
      pthread_t thread;

      if (pthread_create(&thread, NULL, lane_call_thread, &call) != 0 ||
          pthread_join(thread, NULL) != 0)
          return ASTRA_STATUS_IO;
      return call.error == ASTRA_SYSCALL_OK ? call.status : ASTRA_STATUS_IO;
  }

  /* The session the kernel says `scope` binds. */
  static uint32_t scope_session(uint32_t scope)
  {
      AstraHostLeaseInfo lease = {0};

      return astra_host_lease_query(scope, &lease) == ASTRA_SYSCALL_OK ?
          lease.scope : 0u;
  }

  static int case_raw_lane(const char *directory)
  {
      const AstraAssign *work =
          astra_assign_lookup(astra_process_vfs_assigns(), "WORK");
      AstraVfsClient *client = astra_process_vfs_client_for(work);
      struct stat st;
      uint32_t scope;
      uint32_t own;

      (void)directory;
      CHECK(stat("/work", &st) == 0 && client != NULL, "no WORK client");
      scope = client->port_direct_scope;
      own = scope_session(scope);
      CHECK(scope != 0u && own != 0u, "WORK is not on a lane");
      CHECK(lane_call(scope, own, ASTRA_HOST_SERVICE_FILESYSTEM,
                      ASTRA_HOST_FS_STAT, 0u, "/") == ASTRA_STATUS_OK,
            "own scope STAT");
      /* SOUND's session is opened right after WORK's: the likeliest
       * forgery, and not bound to this lane. */
      CHECK(lane_call(scope, own + 1u, ASTRA_HOST_SERVICE_FILESYSTEM,
                      ASTRA_HOST_FS_MKDIR, 0u, "/raw-lane-forged") ==
                ASTRA_STATUS_ACCESS, "forged session");
      CHECK(lane_call(scope, own, ASTRA_HOST_SERVICE_ENTROPY,
                      ASTRA_HOST_ENTROPY_FILL, 0u, "") == ASTRA_STATUS_ACCESS,
            "entropy on a lane");
      return 0;
  }
  ```
- `emu/qemu/test-filesystem-stress.py`: `HOST_OPERATION_PROPERTIES` names
  `"invalid", "open", "close", "read", "write", "sync", "truncate", "stat",
  "readdir", "mkdir", "unlink", "rename", "chmod", "readlink", "symlink",
  "link", "filesystem-info", "session-open", "session-close", "claim"`.
- `sw/userspace/services/hostbench/main.c` and
  `emu/qemu/benchmark-host-channel-raw.c` send filesystem operation 0. It is
  still answered `ASTRA_STATUS_UNSUPPORTED`, before the claim check (Step 4
  prologue), so both stay as they are. They pick up record 3 by rebuilding
  against the new header. Confirm both zero-fill their commands:
  `hostbench` with `clear_words`, the raw benchmark with
  `grep -n "memset\|clear" emu/qemu/benchmark-host-channel-raw.c`.
- `fpga/arty/linux/astra_audio_synth.c` and
  `sw/userspace/audio/src/midi_library.c` include `host.h` for the audio
  records only. Rebuild them in the full suite; no edit.
- `sw/kernel/tests/test_platform.c:297,329` program `ASTRA_HOST_VERSION`
  into the fake register. The kernel accepts major 1, which is unchanged.
  Run the kernel suite to prove it.

- [ ] **Step 6: `emu/qemu/test-hostfs.py` for record 3**

Replace the command helpers (`test-hostfs.py:43-93`, `:168-211`) with:

```python
COMMAND_VERSION = 3
SESSION_FIELD = 60
PATH_OFFSET_FIELD = 64
NODE_FIELD = 72
NODE_KIND = NODE_FIELD + 4
NODE_MODE = NODE_FIELD + 6
NODE_DEVICE = NODE_FIELD + 24
NODE_INODE = NODE_FIELD + 32
NODE_SIZE = NODE_FIELD + 40
RESERVED_FIELD = 184
PATH_MAX = 4096

FS_OPEN = 1
FS_CLOSE = 2
FS_READ = 3
FS_WRITE = 4
FS_SYNC = 5
FS_TRUNCATE = 6
FS_STAT = 7
FS_READDIR = 8
FS_MKDIR = 9
FS_UNLINK = 10
FS_RENAME = 11
FS_CHMOD = 12
FS_READLINK = 13
FS_SYMLINK = 14
FS_LINK = 15
FS_FILESYSTEM_INFO = 16
FS_SESSION_OPEN = 17
FS_SESSION_CLOSE = 18
FS_CLAIM = 19
UNLINK_DIRECTORY = 1
SESSION_READ_ONLY = 1
CLAIM_RELEASE = 1
STATUS_READ_ONLY = 19
CAP_FS_SCOPE = 1 << 10
CAP_FS_SESSIONS = 1 << 11
NODE_INODE_BIT = 1 << 11
NODE_DEVICE_BIT = 1 << 12


class Command:
    """One record-3 command and its operands; place() lays them out."""

    def __init__(self, generation, operation, path=b"", path2=b"", flags=0,
                 handle=0, value=0, offset=0, data=b"", capacity=0,
                 service=1, session=0):
        self.raw = bytearray(COMMAND_SIZE)
        struct.pack_into(">IHHHH", self.raw, 0, COMMAND_SIZE, COMMAND_VERSION,
                         service, operation, flags)
        put32(self.raw, 16, handle)
        put32(self.raw, 20, generation)
        put32(self.raw, 24, offset >> 32)
        put32(self.raw, 28, offset)
        put32(self.raw, 32, value >> 32)
        put32(self.raw, 36, value)
        put32(self.raw, SESSION_FIELD, session)
        self.path = path.encode("utf-8") if isinstance(path, str) else path
        self.path2 = path2.encode("utf-8") if isinstance(path2, str) else path2
        self.data = data
        self.capacity = capacity

    def place(self, at):
        """Fills the operand and data fields for bytes laid out at buffer
        offset `at`; returns (bytes, data offset)."""
        operands = self.path + self.path2
        struct.pack_into(">IHH", self.raw, PATH_OFFSET_FIELD,
                         at if operands else 0, len(self.path),
                         len(self.path2))
        span = max(len(self.data), self.capacity)
        data_at = at + len(operands)
        if span:
            put32(self.raw, 40, data_at)
            put32(self.raw, 44, len(self.data))
            put32(self.raw, 48, self.capacity)
        tail = self.data + b"\xA5" * (span - len(self.data))
        return bytes(operands) + tail, data_at


class Answer:
    def __init__(self, raw, data_at):
        self.raw = raw
        self.data_at = data_at


def lay_out(commands, start):
    arena = bytearray()
    offsets = []
    for command in commands:
        chunk, data_at = command.place(start + len(arena))
        arena += chunk
        arena += b"\0" * (-len(arena) % 8)
        offsets.append(data_at)
    return bytes(arena), offsets


def execute(qtest, commands, owner=0x1001):
    head = len(commands) * COMMAND_SIZE
    arena, offsets = lay_out(commands, head)
    payload = b"".join(bytes(command.raw) for command in commands) + arena
    submission = bytearray(SUBMISSION_SIZE)
    struct.pack_into(">IHHIIIII", submission, 0, SUBMISSION_SIZE, 1, 0,
                     owner, get32(commands[0].raw, 20), BUFFER, len(payload),
                     len(commands))
    qtest.write(BUFFER, payload)
    qtest.write(SUBMISSION, submission)
    qtest.write32(HACC_SUBMIT, SUBMISSION)
    result_word = qtest.read32(HACC_SUBMIT_RESULT)
    assert result_word & 0xFFFF == 0
    assert result_word >> 16 == len(commands)
    result = qtest.read(BUFFER, head)
    return [Answer(result[index * COMMAND_SIZE:(index + 1) * COMMAND_SIZE],
                   offsets[index]) for index in range(len(commands))]


def channel_write(qtest, buffer, capacity, commands, first=0):
    """Writes commands at producer positions first.. of a channel at
    `buffer` with `capacity` slots, operands in its data span."""
    data_start = CHANNEL_HEADER_SIZE + capacity * COMMAND_SIZE
    arena, offsets = lay_out(commands, data_start)
    for index, command in enumerate(commands):
        slot = (first + index) % capacity
        qtest.write(buffer + CHANNEL_HEADER_SIZE + slot * COMMAND_SIZE,
                    bytes(command.raw))
    if arena:
        qtest.write(buffer + data_start, arena)
    return offsets


def status(answer):
    return get32(answer.raw if isinstance(answer, Answer) else answer, 12)


def output(qtest, answer, length):
    return qtest.read(BUFFER + answer.data_at, length)


def claim(qtest, generation, owner=0x1001, release=False):
    return status(execute(qtest, [Command(
        generation, FS_CLAIM, flags=CLAIM_RELEASE if release else 0)],
        owner=owner)[0])


def open_session(qtest, generation, volume=0, root="/", read_only=False,
                 parent=0, owner=0x1001):
    answer = execute(qtest, [Command(
        generation, FS_SESSION_OPEN, path=root,
        value=volume if parent == 0 else 0,
        flags=SESSION_READ_ONLY if read_only else 0, session=parent)],
        owner=owner)[0]
    assert status(answer) == STATUS_OK, status(answer)
    return get32(answer.raw, 56)
```

Convert the existing body of `run` and `volumes` mechanically:

- `make_command(generation, OP, path, path2, ..., volume=V)` becomes
  `Command(generation, OP, path, path2, ..., session=S)`. `S` is the WORK
  session `work = open_session(qtest, generation)` opened right after
  `assert claim(qtest, generation) == STATUS_OK`, at the top of `run`, or
  the SOUND session in `volumes`.
- Single-command data now lives at `answer.data_at`.
  `qtest.read(BUFFER + COMMAND_SIZE, n)` after an `execute` becomes
  `output(qtest, answer, n)`. Inputs pass `data=...`, outputs
  `capacity=...`, and the explicit `data_offset=`/`data_length=`/`data_capacity=`
  arguments go.
- Every `get32(x, ...)`, `get64(x, ...)` or `struct.unpack_from(..., x, ...)`
  on an execute result reads `x.raw`. The old `kind` at offset 88 is
  `NODE_KIND`; the old `mode` at 90 is `NODE_MODE`.
- Channel tests write through `channel_write(qtest, buffer, capacity, [...], first=N)`.
  Each configured channel's `byte_size` grows by 4096 so the operands fit
  (for example `byte_size=CHANNEL_HEADER_SIZE + capacity * COMMAND_SIZE + 4096`).
  The single-slot `direct` channel at `BUFFER` becomes `byte_size=4096 + 4096`.
  The overlap test keeps its two `FS_READ` commands with one writable data
  range, now through `channel_write` with `capacity=64` on both. Its
  expectation stays `SYSCALL_INVALID_ARGUMENT`.
- The folded operations: `FS_OPEN_AT` is `FS_OPEN` with `handle=directory`,
  `FS_STAT_FILE` is `FS_STAT` with `handle=` and no path, `FS_CHMOD_FILE` is
  `FS_CHMOD` with `handle=` and no path, `FS_CHMOD_AT` is `FS_CHMOD` with
  `handle=directory`, `FS_UNLINK_AT` is `FS_UNLINK` with `handle=directory`.
- `volumes()` asserted that `volume=VOLUME_COUNT` is refused. It now opens a
  root session on `VOLUME_COUNT` and expects `STATUS_INVALID`. `/../work`
  through the SOUND session is still refused.
- The version check: `run` asserts `qtest.read32(HACC_VERSION) == 0x0001000E`
  and `HACC_CAPS == 511 | CAP_FS_SCOPE | CAP_FS_SESSIONS`. The metrics snapshot asserts
  `version == 3` and `count == (metrics_size - 16) // 8`. `METRICS_CAPACITY`
  becomes `16 + 8 * (12 + 2 * 20)` (the new `ASTRA_HOST_METRIC_COUNT`).
- After `qtest.write32(HACC_RELEASE_OWNER, 0x1001)`, the old handle answers
  `STATUS_ACCESS`: the owner no longer holds the claim. The reset test then
  claims again, opens a session in the new generation and expects
  `STATUS_BAD_HANDLE` for the stale handle.

Add these checks, called from `main` after `run`:

```python
def claims_and_sessions(qtest, root):
    """Record 3: one claimed owner, rooted sessions, read-only sessions,
    handles bound to their session, path operands."""
    generation = qtest.read32(HACC_GENERATION)
    stray = execute(qtest, [Command(generation, FS_STAT, "/", session=1)],
                    owner=0x2002)[0]
    assert status(stray) == STATUS_ACCESS
    assert claim(qtest, generation) == STATUS_OK
    assert claim(qtest, generation) == STATUS_OK
    assert claim(qtest, generation, owner=0x2002) == STATUS_ACCESS
    work = open_session(qtest, generation)
    assert status(execute(qtest, [Command(
        generation, FS_STAT, "/", session=0)])[0]) == STATUS_BAD_HANDLE
    assert status(execute(qtest, [Command(
        generation, FS_STAT, "/", session=0xDEAD)])[0]) == STATUS_BAD_HANDLE

    # A read-only session refuses every mutation in the provider itself.
    readonly = open_session(qtest, generation, read_only=True)
    for command in (
            Command(generation, FS_MKDIR, "/ro-made", value=0o755,
                    session=readonly),
            Command(generation, FS_OPEN, "/ro-file",
                    flags=OPEN_READ | OPEN_WRITE | OPEN_CREATE, value=0o600,
                    session=readonly)):
        assert status(execute(qtest, [command])[0]) == STATUS_READ_ONLY
    assert not os.path.exists(os.path.join(root, "ro-made"))

    # A session derived beneath "/sub" sees only "/sub", and cannot shed
    # its parent's read-only flag.
    assert status(execute(qtest, [Command(
        generation, FS_MKDIR, "/sub", value=0o755, session=work)])[0]) == \
        STATUS_OK
    with open(os.path.join(root, "sub", "inside"), "wb") as handle:
        handle.write(b"in")
    sub = open_session(qtest, generation, root="/sub", parent=work)
    inside = execute(qtest, [Command(generation, FS_STAT, "/inside",
                                     session=sub)])[0]
    assert status(inside) == STATUS_OK
    assert get64(inside.raw, NODE_INODE) == \
        os.stat(os.path.join(root, "sub", "inside")).st_ino
    st_dev = os.stat(os.path.join(root, "sub", "inside")).st_dev
    assert get64(inside.raw, NODE_DEVICE) == \
        (os.major(st_dev) << 32) | os.minor(st_dev)
    assert get32(inside.raw, NODE_FIELD) & (NODE_INODE_BIT | NODE_DEVICE_BIT) \
        == NODE_INODE_BIT | NODE_DEVICE_BIT
    sub_readonly = open_session(qtest, generation, root="/sub",
                                parent=readonly)
    assert status(execute(qtest, [Command(
        generation, FS_MKDIR, "/x", value=0o755,
        session=sub_readonly)])[0]) == STATUS_READ_ONLY

    # A handle belongs to its session, even for the same owner.
    opened = execute(qtest, [Command(generation, FS_OPEN, "/inside",
                                     flags=OPEN_READ, session=sub)])[0]
    assert status(opened) == STATUS_OK
    crossed = execute(qtest, [Command(
        generation, FS_READ, handle=get32(opened.raw, 16), capacity=2,
        session=work)])[0]
    assert status(crossed) == STATUS_BAD_HANDLE

    # Operands: a 4095-byte path to a real file works, one byte more is
    # refused before Linux sees it, and so is an embedded NUL. Eighteen
    # 200-byte directories and one of 220 make 3839 bytes; a 255-byte leaf
    # (Linux NAME_MAX) brings the path to exactly 4095.
    path = ""
    for length in [200] * 18 + [220]:
        path += "/" + "d" * length
        assert status(execute(qtest, [Command(
            generation, FS_MKDIR, path, value=0o755, session=work)])[0]) == \
            STATUS_OK
    leaf = path + "/" + "f" * (PATH_MAX - 2 - len(path))
    assert len(leaf) == PATH_MAX - 1
    deep = execute(qtest, [Command(
        generation, FS_OPEN, leaf,
        flags=OPEN_READ | OPEN_WRITE | OPEN_CREATE, value=0o600,
        session=work)])[0]
    assert status(deep) == STATUS_OK, status(deep)
    assert status(execute(qtest, [Command(
        generation, FS_STAT, leaf + "x", session=work)])[0]) == \
        STATUS_INVALID
    assert status(execute(qtest, [Command(
        generation, FS_STAT, b"/in\0side", session=sub)])[0]) == \
        STATUS_INVALID

    # Every service rejects a nonzero byte past its fields.
    entropy = Command(generation, 1, capacity=32, service=SERVICE_ENTROPY)
    entropy.raw[RESERVED_FIELD] = 1
    assert status(execute(qtest, [entropy])[0]) == STATUS_INVALID
    stat = Command(generation, FS_STAT, "/", session=work)
    stat.raw[RESERVED_FIELD + 40] = 1
    assert status(execute(qtest, [stat])[0]) == STATUS_INVALID
    assert status(execute(qtest, [Command(
        generation, FS_SESSION_CLOSE, session=sub)])[0]) == STATUS_OK
    assert status(execute(qtest, [Command(
        generation, FS_STAT, "/inside", session=sub)])[0]) == STATUS_BAD_HANDLE
    print("PASS claims-and-sessions")
```

The 4,095-byte open proves the operand limit is not lower than the PRD's
4,096-byte buffer (FS18) and that Linux resolves the whole path. The
4,096-byte stat could be refused by either check; Perturbation 4 in Step 8
shows the operand check is the one that bites.

Order in `main`: `run(...)` does its own claim first. Then
`qtest.write32(HACC_RESET, 1)` drops every claim and session, and then
`claims_and_sessions(qtest, os.path.join(root, "work"))` and `volumes(...)`
run, each claiming again.

Task 3b's `scopes` and `scope_revocation` move to sessions:
- Each starts with `assert claim(qtest, generation) == STATUS_OK` and opens
  `work = open_session(qtest, generation)` and
  `sound = open_session(qtest, generation, volume=1)`.
- Every scope word `VOLUME_WORK`/`VOLUME_SOUND` becomes `work`/`sound`, and
  every `scope_owner` becomes `0x1001`, the claimed owner.
- Every `make_command(..., volume=V)` on a bound channel becomes
  `Command(..., session=S)`, laid out with `channel_write(qtest, BUFFER, 2, [command], first=producer - 1)`
  before the kick. `scoped_post` takes the arena layout: write the command
  through `channel_write` and keep its kick.
- The malformed-word assertion `VOLUME_COUNT` becomes a session nobody
  opened (`0xDEAD`). The configured channel's `byte_size` grows by 4096 for
  the operands, as Step 6 says for every channel.

Then add the creator check and the session-close release to `scopes`:

```python
    # The creator check: a scope whose creator is not the claimed owner is
    # refused at the binding, even over a live session.
    assert configure_scope(qtest, generation, CONFIG_OPEN, 7, 21, work,
                           SCOPE_BOUND, 0x2002) != 0
```

Put this before the first successful bound OPEN. Then, after the handle
tests:

```python
    # A session's handles die with it, whoever opened them.
    with open(os.path.join(root, "work", "held-open"), "wb") as out:
        out.write(b"held")
    sub = open_session(qtest, generation, root="/", parent=work)
    assert configure_scope(qtest, generation, CONFIG_ATTACH, 7, 21, sub,
                           SCOPE_BOUND, 0x1001) == 0
    held = scoped(qtest, 7, 10, Command(generation, FS_OPEN, "/held-open",
                                        flags=OPEN_READ, session=sub))
    assert status(held) == STATUS_OK
    target = os.path.join(root, "work", "held-open")
    assert provider_holds(target)
    assert status(execute(qtest, [Command(
        generation, FS_SESSION_CLOSE, session=sub)])[0]) == STATUS_OK
    assert not provider_holds(target)
```

with:

```python
def provider_holds(path):
    """True while a process of ours -- the QEMU under test -- holds `path`."""
    target = os.path.realpath(path)
    for fd in glob.glob("/proc/[0-9]*/fd/*"):
        try:
            if os.readlink(fd) == target:
                return True
        except OSError:
            pass
    return False
```

Add `import glob`. The gates run on Linux, where `/proc` is real.
`scoped(...)` here takes a `Command`. Its producer numbers continue
after the ones already used on slot 7.

- [ ] **Step 7: Run everything**

Sync, then on beast:

```sh
~/astra-mg/locked make -C sw/kernel clean test
~/astra-mg/locked make -C sw/kernel clean
~/astra-mg/locked make -C sw/userspace -j32
~/astra-mg/locked make -C sw/userspace test
~/astra-mg/locked make -C sw/boot -j32
ASTRA_QEMU_WORK_ROOT=$HOME/.cache/astra68/qemu-9.2.4 ~/astra-mg/locked emu/qemu/build.sh host
Q=$(tail -n1 /tmp/qemu-host-build.log)
~/astra-mg/locked python3 emu/qemu/test-hostfs.py $Q
```

Expected: kernel suites PASS, userspace tests PASS (including
`test_vfs_host_backend`), `PASS claims-and-sessions`, `PASS scopes`,
`PASS scope-revocation`, `ASTRA QEMU HOSTFS PASS`. Then rebuild images and
run `test-fs-semantics.py`: `FSSEM PASS raw-lane` on record 3, `PASS lane`.

- [ ] **Step 8: Perturb**

1. Claim. In `astra_host_execute_fs`, change
   `owner != qatomic_read(&s->host.fs_owner)` to `false &&`. Rebuild QEMU
   (`emu/qemu/build.sh host`), rerun `test-hostfs.py`. Expected:
   `assert status(stray) == STATUS_ACCESS` fails. Restore, re-sync, rebuild,
   pass.
2. Read-only. Make `astra_host_fs_mutates` return `false` for
   `ASTRA_HOST_FS_MKDIR`. Expected: the read-only loop fails with
   `STATUS_OK != STATUS_READ_ONLY`. Restore, re-sync, rebuild, pass.
3. Session binding. In `astra_host_session_file`, drop the
   `file->session != session_id` test. Expected: `crossed` answers OK and
   the assertion fails. Restore, re-sync, rebuild, pass.
4. Operand limit. In `astra_host_fs_operands`, change both
   `>= ASTRA_HOST_FS_PATH_MAX` to `>= ASTRA_HOST_FS_PATH_MAX - 1`. Expected:
   the 4,095-byte `deep` open answers `STATUS_INVALID` and its assertion
   fails. Restore, re-sync, rebuild, pass.
5. Creator check. In `astra_host_config_scope`, drop
   `scope->owner == qatomic_read(&s->host.fs_owner) &&`. Expected: `scopes`
   fails at the creator assertion (`0x2002` bound). Restore, re-sync,
   rebuild, pass.
6. Session release. Skip `astra_host_release_session_files` in
   `astra_host_fs_session_close`. Expected: `scopes` fails at
   `not provider_holds(target)`. Restore, re-sync, rebuild, pass.
7. Forged session on a lane. In Step 4b's bound branch, look the scope up
   by `channel->scopes[0].scope` instead of `session_id`. Expected:
   `raw-lane` fails "forged session" on the booted machine, and the gate's
   `lane` check fails if `sound/raw-lane-forged` appeared.

- [ ] **Step 9: Full suite, hop check, commit**

`~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-task4.log`. Expected: green.
The audio, entropy, remote-desktop, Doom, terminal and system-log gates
exercise every non-filesystem consumer of record 3.

Run one `--bench t4` as in Task 3 Step 8 and compare it with Task 3b's
`qemu-t3b-1.json`. The record change must not move `stat-warm` or
`rand-read` by more than run-to-run noise. Record the comparison in the
commit message.

```bash
git add sw/include/astra/host.h emu/qemu/qemu-9.2/hw/m68k/astra68.c \
  sw/userspace/vfs sw/userspace/services/hostfs/main.c \
  sw/userspace/commands/fssem/fssem.c \
  emu/qemu/test-hostfs.py emu/qemu/test-filesystem-stress.py
git commit -m "feat(host): command record 3 -- claimed provider, rooted sessions, path operands, statx node"
```

---

### Task 5: Grants are the control plane; scopes carry them (D2, corrected; FS15)

A grant becomes a HostFS port with a root, rights and a scope. HostFS keeps
one `AstraVfsService` per volume for session bookkeeping. It answers each
grant on that grant's own receive port. A session remembers the grant it was
opened on and is refused on any other.

Each grant has its own provider session (Task 4) and its own scope (Task
3b), minted over that session with the grant's rights:
- read-only when the grant lacks write;
- delegable only when the grant is.

HELLO on a grant answers the session, the `GRANTS` feature and a duplicate
of that scope. The client's lane carries every data operation, and QEMU
confines it to the session's root with the scope's rights. HostFS never sees
the data.

A holder narrows a grant with `GRANT_DERIVE`: a sub-root, read-only,
non-delegable, or any mix. Never wider. The supervisor uses it for every
manifest grant narrower than the one it holds, and for every child that does
not `delegate`. When a grant's last holder closes its port, HostFS revokes
the grant's scope through the kernel, which stops every bound channel at
once, and closes the session. The raw confinement cases drive *lanes*
directly with the scopes they hold, and drive the control ports with raw
messages, without the Kit.

**Files:**
- Modify: `sw/include/astra/vfs_service.h` (version 29, `features`, `GRANT_DERIVE`, grant rights)
- Modify: `sw/userspace/vfs/include/astra/vfs_service_core.h`, `sw/userspace/vfs/src/vfs_service_core.c`
- Modify: `sw/userspace/vfs/include/astra/vfs_port_transport.h`, `sw/userspace/vfs/src/vfs_port_transport.c`
- Modify: `ndk/include/astra/vfs_client.h`, `sw/userspace/vfs/src/vfs_client.c`, `sw/userspace/vfs/filesystem.library.exports`
- Modify: `sw/userspace/services/hostfs/main.c`, `sw/userspace/services/hostfs/Makefile`, `emu/qemu/astra_image.py`, `emu/qemu/test-astra-image-providers.py`
- Modify: `sw/userspace/supervisor/src/loader.c`
- Modify: Kit major 7 (the file list of Task 3 Step 7)
- Modify: `sw/userspace/commands/fssem/fssem.c`, `emu/qemu/test-fs-semantics.py`
- Modify: `docs/FILESYSTEM_PRODUCTION_PRD.md` (FS15), `docs/STORAGE_AND_VFS.md`
- Test: `sw/userspace/vfs/tests/test_vfs_service_core.c`, `sw/userspace/vfs/tests/test_vfs_port.c`

**Interfaces:**
- Consumes: the scope syscalls and `astra_host_scope_create/revoke` (Task 3b).
  Also `astra_vfs_host_session_open/close` and record-3 sessions with their
  creator check (Task 4).
- Produces (wire, version 29):
  - `AstraVfsReply.features` replaces `reserved2`. HELLO answers
    `ASTRA_VFS_FEATURE_*`; every other operation answers 0.
    `ASTRA_VFS_FEATURE_GRANTS (1u << 0)` says the session is bound to a
    provider-enforced grant.
  - `ASTRA_VFS_OP_GRANT_DERIVE` (36). `flags` is a subset of
    `ASTRA_VFS_GRANT_READ (1u << 0) | ASTRA_VFS_GRANT_WRITE (1u << 1) | ASTRA_VFS_GRANT_DELEGATE (1u << 2)`,
    and `body.path` is the new root, relative to the session's grant root
    (`""` keeps it). The reply carries one send handle: the derived grant's
    port. HELLO on that port hands out its scope.
- Produces (core):
  - `astra_vfs_service_dispatch_grant(service, owner, grant, features, operation, request, reply)`
  - `astra_vfs_service_session_bound(service, session, owner, grant) -> int`
  - `astra_vfs_service_release_grant(service, grant)`
- Produces (port):
  - `AstraVfsPortGrant {receive, id, rights, scope}`
  - `AstraVfsPortDerive`
  - `astra_vfs_port_service_set_derive(host, derive, context) -> int`
  - `astra_vfs_port_service_grant_pump(host, worker, grant, budget) -> uint32_t`
  - Task 3b's `astra_vfs_port_service_set_scope` and `AstraVfsPortService.scope`
    are deleted: every scoped port is now a grant.
- Produces (Kit 7):
  - `AstraVfsClient.features`, `AstraVfsPortThreadState.received_handle`
  - `astra_vfs_grant_derive(client, root, rights, &handle) -> status`
- Consumed by: Task 6 (features), Task 8 (a fork child shares its parent's
  grant scope), Task 10 (grant recovery after provider loss), Task 11 (the
  per-grant file limit rides the scope).

- [ ] **Step 1: Write the failing core and port tests**

In `sw/userspace/vfs/tests/test_vfs_service_core.c`:

```c
/*
 * A session is bound to the grant it said HELLO on: the same session id on
 * another grant is refused (docs/FILESYSTEM_PRODUCTION_PRD.md FS15).
 */
static void
test_sessions_are_bound_to_their_grant(void)
{
    AstraVfsService service;
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t session;

    service_start(&service);
    memset(&request, 0, sizeof(request));
    request.size = ASTRA_VFS_REQUEST_SIZE;
    request.version = ASTRA_VFS_VERSION;
    astra_vfs_service_dispatch_grant(&service, 7u, 1u,
                                     ASTRA_VFS_FEATURE_GRANTS,
                                     ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(reply.features == ASTRA_VFS_FEATURE_GRANTS);
    session = reply.session;
    assert(astra_vfs_service_session_bound(&service, session, 7u, 1u));
    assert(!astra_vfs_service_session_bound(&service, session, 7u, 2u));
    assert(!astra_vfs_service_session_bound(&service, session, 8u, 1u));

    request.session = session;
    astra_vfs_service_dispatch_grant(&service, 7u, 2u, 0u, ASTRA_VFS_OP_BYE,
                                     &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_ACCESS);
    assert(astra_vfs_service_session_bound(&service, session, 7u, 1u));

    astra_vfs_service_release_grant(&service, 1u);
    assert(!astra_vfs_service_session_bound(&service, session, 7u, 1u));
}
```

Use the file's existing service fixture. If its name differs from
`service_start(&service)`, find it with
`grep -n "astra_vfs_service_init(" sw/userspace/vfs/tests/test_vfs_service_core.c`
and call it the same way. Register the test in `main`.

In `sw/userspace/vfs/tests/test_vfs_port.c` add the port-level cases. Task
3b's `backend_mkdir_calls` counts the fake backend's mkdirs.

```c
static uint32_t derive_calls;
static uint32_t derive_rights;
static char derive_root[ASTRA_VFS_PATH_MAX];

static uint32_t
mock_derive(void *context, const AstraVfsPortGrant *parent, const char *root,
            uint32_t rights, uint32_t *send)
{
    (void)context;
    (void)parent;
    ++derive_calls;
    derive_rights = rights;
    strcpy(derive_root, root);
    *send = mock_open(1u);
    return ASTRA_VFS_OK;
}

/* Sends one standard request for `session` on `grant`'s port and pumps it. */
static uint32_t
grant_round_trip(AstraVfsPortService *host, const AstraVfsPortGrant *grant,
                 uint32_t operation, uint32_t session, uint32_t flags,
                 const char *path, uint32_t attach, uint32_t reply_handle,
                 AstraVfsReplyMessage *reply, uint32_t *received)
{
    AstraVfsRequestMessage message;
    uint32_t handles[1] = {attach};
    uint32_t size = 0u;
    uint32_t count = 0u;
    AstraVfsPortWorker worker;

    memset(&message, 0, sizeof(message));
    astra_message_header_set(&message.header, (uint32_t)sizeof(message),
                             ASTRA_VFS_PROTOCOL, ASTRA_VFS_VERSION, operation,
                             1u);
    message.request.size = ASTRA_VFS_REQUEST_SIZE;
    message.request.version = ASTRA_VFS_VERSION;
    message.request.session = session;
    message.request.flags = flags;
    strcpy((char *)message.request.body.path, path);
    assert(astra_port_send(grant->receive, &message, sizeof(message),
                           attach != 0u ? handles : NULL,
                           attach != 0u ? 1u : 0u) == ASTRA_SYSCALL_OK);
    if (astra_vfs_port_service_grant_pump(host, &worker, grant, 1u) == 0u)
        return ASTRA_SYSCALL_WOULD_BLOCK;
    handles[0] = 0u;
    assert(astra_port_receive(reply_handle, reply, sizeof(*reply), handles,
                              1u, &size, &count) == ASTRA_SYSCALL_OK);
    *received = count != 0u ? handles[0] : 0u;
    return ASTRA_SYSCALL_OK;
}

static void
test_grants_hand_out_their_scope_and_only_narrow(void)
{
    AstraVfsPortService host;
    AstraVfsPortGrant writable = {0};
    AstraVfsPortGrant readable = {0};
    AstraVfsReplyMessage reply;
    uint32_t reply_handle;
    uint32_t reply_two;
    uint32_t received = 0u;
    uint32_t session;

    mock_reset();
    service_start();
    writable.receive = mock_open(MOCK_QUEUE_MAX);
    writable.id = 1u;
    writable.rights = ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE |
                      ASTRA_VFS_GRANT_DELEGATE;
    writable.scope = mock_open(1u);
    readable.receive = mock_open(MOCK_QUEUE_MAX);
    readable.id = 2u;
    readable.rights = ASTRA_VFS_GRANT_READ;
    readable.scope = mock_open(1u);
    assert(astra_vfs_port_service_init(&host, writable.receive, &service));
    assert(astra_vfs_port_service_set_derive(&host, mock_derive, NULL));
    reply_handle = mock_open(4u);
    reply_two = mock_open(4u);

    /* HELLO on a grant: a session, the GRANTS feature, the grant's scope. */
    assert(grant_round_trip(&host, &writable, ASTRA_VFS_OP_HELLO, 0u, 0u,
                            "", reply_handle, reply_handle, &reply,
                            &received) == ASTRA_SYSCALL_OK);
    assert(reply.reply.status == ASTRA_VFS_OK);
    assert(reply.reply.features == ASTRA_VFS_FEATURE_GRANTS);
    assert(received != 0u &&
           ports[received].target == ports[writable.scope].target);
    session = reply.reply.session;

    /* The same session on the read-only grant's port: refused unanswered. */
    assert(grant_round_trip(&host, &readable, ASTRA_VFS_OP_BYE, session, 0u,
                            "", 0u, reply_handle, &reply, &received) ==
           ASTRA_SYSCALL_WOULD_BLOCK);
    assert(host.refused == 1u);

    /* A grant port is control only: the data is the lane's. */
    assert(grant_round_trip(&host, &writable, ASTRA_VFS_OP_MKDIR, session,
                            0u, "/x", 0u, reply_handle, &reply,
                            &received) == ASTRA_SYSCALL_OK);
    assert(reply.reply.status == ASTRA_VFS_ERR_UNSUPPORTED);
    assert(backend_mkdir_calls == 0u);

    /* Derive: never wider, and DELEGATE only from a delegable grant. */
    assert(grant_round_trip(&host, &readable, ASTRA_VFS_OP_HELLO, 0u, 0u,
                            "", reply_two, reply_two, &reply,
                            &received) == ASTRA_SYSCALL_OK);
    {
        uint32_t readonly_session = reply.reply.session;

        assert(grant_round_trip(&host, &readable, ASTRA_VFS_OP_GRANT_DERIVE,
                                readonly_session,
                                ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE,
                                "", 0u, reply_two, &reply,
                                &received) == ASTRA_SYSCALL_OK);
        assert(reply.reply.status == ASTRA_VFS_ERR_ACCESS && received == 0u);
        assert(grant_round_trip(&host, &readable, ASTRA_VFS_OP_GRANT_DERIVE,
                                readonly_session,
                                ASTRA_VFS_GRANT_READ |
                                    ASTRA_VFS_GRANT_DELEGATE,
                                "", 0u, reply_two, &reply,
                                &received) == ASTRA_SYSCALL_OK);
        assert(reply.reply.status == ASTRA_VFS_ERR_ACCESS && received == 0u);
        assert(derive_calls == 0u);
    }
    assert(grant_round_trip(&host, &writable, ASTRA_VFS_OP_GRANT_DERIVE,
                            session, ASTRA_VFS_GRANT_READ, "sub", 0u,
                            reply_handle, &reply,
                            &received) == ASTRA_SYSCALL_OK);
    assert(reply.reply.status == ASTRA_VFS_OK);
    assert(received != 0u && derive_calls == 1u);
    assert(derive_rights == ASTRA_VFS_GRANT_READ);
    assert(strcmp(derive_root, "sub") == 0);
}
```

Replace Task 3b's `test_hello_attaches_the_scope_and_never_the_device` and
`test_a_scoped_port_serves_no_data`: the test above covers both through
grants. Register it in `main`.

Run on beast: `~/astra-mg/locked make -C sw/userspace/vfs test`. Expected:
compile errors (the new functions and fields do not exist).

- [ ] **Step 2: The wire (`sw/include/astra/vfs_service.h`)**

- `ASTRA_VFS_VERSION` -> 29. Append to the history: "Version 29 binds a
  session to the grant port it said HELLO on, and hands out that grant's
  scope. HELLO answers `features`. `GRANT_DERIVE` returns a narrower grant:
  a sub-root, read-only, non-delegable, or a mix; never wider."
- After `ASTRA_VFS_OP_STAT_FILE`:

  ```c
  /** Derive a narrower grant: `flags` a subset of the session's grant rights,
   * `body.path` the new root relative to its root ("" keeps it). The reply
   * carries the new grant's send handle; HELLO on it hands out its scope. */
  #define ASTRA_VFS_OP_GRANT_DERIVE UINT32_C(36)
  /** Highest operation code this build defines. */
  #define ASTRA_VFS_OP_MAX          ASTRA_VFS_OP_GRANT_DERIVE

  /* Rights a grant carries, in GRANT_DERIVE's flags. */
  /** Read files, list directories, stat. Every grant has it. */
  #define ASTRA_VFS_GRANT_READ     (UINT32_C(1) << 0)
  /** Create, write, truncate, rename, unlink, chmod, link. */
  #define ASTRA_VFS_GRANT_WRITE    (UINT32_C(1) << 1)
  /** Its holders may hand its scope on (the scope keeps TRANSFER) and
   * derive grants that keep this right. */
  #define ASTRA_VFS_GRANT_DELEGATE (UINT32_C(1) << 2)

  /* AstraVfsReply::features, answered by HELLO from version 29. */
  /** The session is bound to a grant the provider enforces. */
  #define ASTRA_VFS_FEATURE_GRANTS (UINT32_C(1) << 0)
  ```

- In `AstraVfsReply`, replace `/** Must be zero. */ uint32_t reserved2;` with:

  ```c
      /** HELLO, version 29 on: ASTRA_VFS_FEATURE_* the service offers this
       * session. Every other operation: zero. */
      uint32_t features;
  ```

- [ ] **Step 3: The core (`vfs_service_core.h`, `vfs_service_core.c`)**

`AstraVfsSessionSlot` gains, after `owner`:

```c
    uint32_t grant;         /* the grant it said HELLO on, 0 for none */
```

In `vfs_service_core.c`:
- `handle_hello` gains `uint32_t grant, uint32_t features`. It stores
  `grant` in the slot. When the agreed version is at least 29 it sets
  `reply->features = features`. The `reap_session` reset clears `grant`.
- Rename `dispatch_from_unlocked` to `dispatch_unlocked`, with the extra
  arguments `uint32_t grant, uint32_t features`. HELLO passes them to
  `handle_hello`. For every other operation, right after the existing owner
  check:

  ```c
      if (slot->grant != grant) {
          ++service->stats.cross_session_denied;
          reply->status = ASTRA_VFS_ERR_ACCESS;
          goto done;
      }
  ```

- Public entry points:

  ```c
  void
  astra_vfs_service_dispatch_grant(AstraVfsService *service, uint32_t owner,
                                   uint32_t grant, uint32_t features,
                                   uint32_t operation,
                                   const AstraVfsRequest *request,
                                   AstraVfsReply *reply)
  {
      if (service == NULL || reply == NULL)
          return;
      if (!state_acquire(service)) {
          clear_reply(reply, ASTRA_VFS_VERSION);
          reply->status = ASTRA_VFS_ERR_IO;
          return;
      }
      dispatch_unlocked(service, owner, grant, features, operation, request,
                        reply);
      state_release(service);
  }

  void
  astra_vfs_service_dispatch_from(AstraVfsService *service, uint32_t owner,
                                  uint32_t operation,
                                  const AstraVfsRequest *request,
                                  AstraVfsReply *reply)
  {
      astra_vfs_service_dispatch_grant(service, owner, 0u, 0u, operation,
                                       request, reply);
  }

  int
  astra_vfs_service_session_bound(const AstraVfsService *service,
                                  uint32_t session, uint32_t owner,
                                  uint32_t grant)
  {
      AstraVfsService *mutable = (AstraVfsService *)(uintptr_t)service;
      int bound = 0;

      if (service == NULL || session == ASTRA_VFS_SESSION_INVALID ||
          !state_acquire(mutable))
          return 0;
      for (uint32_t index = 0u; index < service->session_capacity; ++index)
          if (service->sessions[index].id == session) {
              bound = service->sessions[index].owner == owner &&
                      service->sessions[index].grant == grant;
              break;
          }
      state_release(mutable);
      return bound;
  }

  void
  astra_vfs_service_release_grant(AstraVfsService *service, uint32_t grant)
  {
      if (service == NULL || grant == 0u)
          return;
      for (uint32_t index = 0u; index < service->session_capacity; ++index)
          if (service->sessions[index].id != 0u &&
              service->sessions[index].grant == grant)
              astra_vfs_service_release_session(service,
                                                service->sessions[index].id);
  }
  ```

  `astra_vfs_service_session_owned` becomes
  `return astra_vfs_service_session_bound(service, session, owner, 0u);`.
  Its one caller, the port pump, moves to `session_bound` in Step 4.
- `operation_takes_path` includes `ASTRA_VFS_OP_GRANT_DERIVE`. The core
  answers GRANT_DERIVE with `ASTRA_VFS_ERR_UNSUPPORTED`, because only a port
  can carry the new handle; the existing `default:` case already does that.

Declare the new functions in `vfs_service_core.h` beside the old ones. The
grant has no backend context here: HostFS runs no data operation for it.

- [ ] **Step 4: The port (`vfs_port_transport.h`, `vfs_port_transport.c`)**

Header:

```c
/* One grant a service answers on its own port (docs/FILESYSTEM_PRODUCTION_PRD.md
 * FS15). The kernel stamps which port a message arrived on, and so which
 * grant it is under; the service owns these records. */
typedef struct AstraVfsPortGrant {
    uint32_t receive;          /* the grant's receive port, 0 when free */
    uint32_t id;               /* nonzero, never reused by its service */
    uint32_t rights;           /* ASTRA_VFS_GRANT_* */
    uint32_t scope;            /* the grant's scope, ADMINISTER; HELLO hands
                                * out a duplicate without it */
} AstraVfsPortGrant;

/* Make a grant at most `rights` (already a checked subset of `parent`'s)
 * rooted at `root` beneath `parent`'s root; answer its send handle in
 * *send. */
typedef uint32_t (*AstraVfsPortDerive)(void *context,
                                       const AstraVfsPortGrant *parent,
                                       const char *root, uint32_t rights,
                                       uint32_t *send);

int astra_vfs_port_service_set_derive(AstraVfsPortService *host,
                                      AstraVfsPortDerive derive,
                                      void *context);
uint32_t astra_vfs_port_service_grant_pump(AstraVfsPortService *host,
                                           AstraVfsPortWorker *worker,
                                           const AstraVfsPortGrant *grant,
                                           uint32_t budget);
```

`AstraVfsPortService` gains `AstraVfsPortDerive derive; void *derive_context;`
and loses Task 3b's `scope`. `astra_vfs_port_service_init` clears the two
new fields. Delete `astra_vfs_port_service_set_scope`.

Source:
- Turn the body of `astra_vfs_port_service_worker_pump` into
  `static uint32_t pump(AstraVfsPortService *host, AstraVfsPortWorker *worker, uint32_t receive, const AstraVfsPortGrant *grant, uint32_t budget)`.
  Both public pumps call it:
  `astra_vfs_port_service_worker_pump(host, worker, budget)` passes
  `host->receive, NULL`, and `astra_vfs_port_service_grant_pump` passes
  `grant->receive, grant` (refusing `grant == NULL || grant->receive == 0u || grant->id == 0u`).
- Inside `pump`, with `uint32_t grant_id = grant != NULL ? grant->id : 0u;`
  and `uint32_t features = grant != NULL ? ASTRA_VFS_FEATURE_GRANTS : 0u;`:
  - receive from `receive` in both `astra_port_receive_from` calls;
  - `session_owned` uses
    `astra_vfs_service_session_bound(host->service, incoming->request.session, sender, grant_id)`;
  - the fused-HELLO dispatch and the final `else` dispatch use
    `astra_vfs_service_dispatch_grant(host->service, sender, grant_id, features, ...)`;
  - Task 3b's HELLO attachment duplicates `grant->scope` in place of
    `host->scope`. Its condition reads
    `incoming->request.version >= UINT16_C(28) && grant != NULL && grant->scope != 0u`;
  - Task 3b's control-only refusal reads `grant != NULL` in place of
    `host->scope != 0u`, and also lets `ASTRA_VFS_OP_GRANT_DERIVE` through;
  - add, before the final `else`:

    ```c
        } else if (operation == ASTRA_VFS_OP_GRANT_DERIVE) {
            uint32_t derived = 0u;

            if (incoming->request.version < UINT16_C(29) || grant == NULL ||
                host->derive == NULL)
                outgoing->reply.status = ASTRA_VFS_ERR_UNSUPPORTED;
            else if ((incoming->request.flags & ~grant->rights) != 0u ||
                     (incoming->request.flags & ASTRA_VFS_GRANT_READ) == 0u)
                outgoing->reply.status = ASTRA_VFS_ERR_ACCESS;
            else if (memchr(incoming->request.body.path, 0,
                            ASTRA_VFS_PATH_MAX) == NULL)
                outgoing->reply.status = ASTRA_VFS_ERR_INVALID;
            else {
                outgoing->reply.status = host->derive(
                    host->derive_context, grant,
                    (const char *)incoming->request.body.path,
                    incoming->request.flags, &derived);
                if (outgoing->reply.status == ASTRA_VFS_OK) {
                    outgoing_handles[0] = derived;
                    outgoing_handle_count = 1u;
                }
            }
    ```

  The existing close-on-failure (`:2597-2607`) disposes of `derived` when
  the reply cannot be sent.

Client side, in `astra_vfs_port_transport`'s reply check, a reply may carry
a handle only on a version-28 HELLO (Task 3b) or on GRANT_DERIVE:
`(reply_handle_count != 0u && !((wire_operation == ASTRA_VFS_OP_HELLO && incoming->reply.version >= UINT16_C(28)) || wire_operation == ASTRA_VFS_OP_GRANT_DERIVE))`.
After `*reply = incoming->reply;`, a derived grant goes to the calling
thread and a HELLO's scope to the lane, as in Task 3b:

```c
    if (reply_handle_count == 1u &&
        wire_operation == ASTRA_VFS_OP_GRANT_DERIVE)
        port_state(client)->received_handle = reply_handles[0];
    else if (reply_handle_count == 1u) {
        /* Task 3b's accelerator connect, unchanged. */
    }
```

In `ndk/include/astra/vfs_client.h`, `AstraVfsPortThreadState` gains
`uint32_t received_handle;` after `borrow_base`, and `AstraVfsClient` gains
`uint32_t features;` after `version`.

- [ ] **Step 5: The Kit (`vfs_client.c`)**

`astra_vfs_connect`: after a successful HELLO,
`client->features = call->reply.features;`. The lazy path publishes the
session in `port_client_publish_session`. Record `features` there too, by
passing `reply->features` (add the parameter). Then:

```c
/*
 * A narrower grant of the one this client holds: `root` beneath its root
 * ("" keeps it), at most `rights` (ASTRA_VFS_GRANT_*). Answers the new
 * grant's send handle, which the caller owns -- to keep, transfer to a
 * child, or close.
 */
uint32_t
astra_vfs_grant_derive(AstraVfsClient *client, const char *root,
                       uint32_t rights, uint32_t *handle)
{
    AstraVfsCallState *call;
    uint32_t status;

    if (client == NULL || root == NULL || handle == NULL ||
        (rights & ~(ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE |
                    ASTRA_VFS_GRANT_DELEGATE)) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    *handle = 0u;
    if ((client->features & ASTRA_VFS_FEATURE_GRANTS) == 0u)
        return ASTRA_VFS_ERR_UNSUPPORTED;
    call = begin(client);
    if (!set_path(&call->request, root))
        return ASTRA_VFS_ERR_INVALID;
    call->request.flags = rights;
    astra_vfs_port_received_reset(client);
    status = exchange(client, call, ASTRA_VFS_OP_GRANT_DERIVE);
    if (status != ASTRA_VFS_OK)
        return status;
    *handle = astra_vfs_port_received_take(client);
    return *handle != 0u ? ASTRA_VFS_OK : ASTRA_VFS_ERR_PROTOCOL;
}
```

`begin`, `set_path` and `exchange` are the file's own request helpers
(`vfs_client.c`, as `astra_vfs_stat_meta` uses them at `:530-575`).
GRANT_DERIVE has no lane fast path. It goes through `exchange` to the port,
like HELLO: the lane carries data, the port carries authority. The
translator (`astra_vfs_host_direct_transport`) must not claim it. If its
`default:` case would answer it locally, add GRANT_DERIVE to the
operations that `astra_vfs_port_transport` routes to the port for a lane
client, next to HELLO and BYE.
`astra_vfs_port_received_reset` and `astra_vfs_port_received_take` are two
small functions in `vfs_port_transport.c`, declared in
`vfs_port_transport.h`:

```c
void astra_vfs_port_received_reset(AstraVfsClient *client)
{
    port_state(client)->received_handle = 0u;
}

uint32_t astra_vfs_port_received_take(AstraVfsClient *client)
{
    uint32_t handle = port_state(client)->received_handle;

    port_state(client)->received_handle = 0u;
    return handle;
}
```

`vfs_client.c` calls them only when
`client->transport == astra_vfs_port_transport`. A local client has no
grants, and the `features` test above has already returned UNSUPPORTED for
it. Declare `astra_vfs_grant_derive` in `ndk/include/astra/vfs_client.h`
with a doc comment, and add it to `sw/userspace/vfs/filesystem.library.exports`
in sorted position.

Kit major 7: repeat Task 3 Step 7 with `6` -> `7`:
- `filesystem.library.7`;
- `ASTRA_DYNAMIC_LIBRARY("filesystem.library.7", 7, 0, 0, ...)`;
- `'^filesystem.library.7 7.0.0 ABI 7.0$$'`.

`ndk/tests/headers.c` expects 7.

- [ ] **Step 6: HostFS serves grants (`services/hostfs/main.c`)**

Replace Task 3b's two scopes and two scoped ports with a grant table. HostFS
stays single-threaded: it serves only the control plane. `METRICS` keeps
its plain port. Make the device handle file-scope
(`static uint32_t device_handle;`, as before 27).

```c
#include <astra/config_library.h>
#include <astra/vfs_host_backend.h>
#include <astra/vfs_process.h>

/* A grant HostFS answers on its own port: its provider session, the scope
 * that binds clients' channels to that session with the grant's rights,
 * and the service whose sessions it binds (FS15). */
typedef struct HostfsGrant {
    AstraVfsPortGrant port;        /* receive, id, rights, scope */
    AstraVfsService *service;      /* WORK's or SOUND's */
    uint32_t session;              /* its provider session */
} HostfsGrant;

/* The kernel's scope pool is 64 (KERNEL_HOST_SCOPE_MAX in
 * sw/kernel/process.c); one scope per grant, with room for recovery. */
#define HOSTFS_GRANTS_MAX 48u

static HostfsGrant *grants;
static uint32_t grant_capacity;    /* settings.conf `grants N` */
static uint32_t next_grant_id = 1u;
static uint32_t *wait_handles;     /* grant_capacity + 2 */
static uint32_t *wait_owners;      /* what each handle is */

enum { WAIT_METRICS = UINT32_MAX - 1u, WAIT_SHUTDOWN };

/* services/hostfs/settings.conf `grants N`: the two base grants and every
 * derived one, at most one wait set and the kernel's scope pool. */
static uint32_t configured_grants(const AstraStartupInfo *startup,
                                  uint32_t *count)
{
    AstraConfig config = ASTRA_CONFIG_INIT;
    AstraConfigError error = {0};
    uint64_t value = 0u;
    uint32_t found = 0u;
    uint32_t status;

    if (astra_process_vfs_init(startup) != ASTRA_VFS_OK)
        return ASTRA_STATUS_NOT_FOUND;
    if (astra_config_open(startup, 1u, ASTRA_CONFIG_OPEN_READ, &config,
                          &error) != ASTRA_CONFIG_OK)
        return ASTRA_STATUS_INVALID;
    status = astra_config_count(&config, "grants", &found);
    if (status == ASTRA_CONFIG_OK && found == 1u)
        status = astra_config_get_u64(&config, "grants", 0u, &value);
    astra_config_close(&config);
    if (status != ASTRA_CONFIG_OK || found != 1u || value < 2u ||
        value > HOSTFS_GRANTS_MAX || value > ASTRA_WAIT_MULTIPLE_MAX - 2u)
        return ASTRA_STATUS_INVALID;
    *count = (uint32_t)value;
    return ASTRA_STATUS_OK;
}

static AstraVfsPortService *grant_host(const HostfsGrant *grant)
{
    return grant->service == &service ? &port : &sound_port;
}

/* Adds a grant over provider `session` with `rights`; answers its send
 * handle. Its scope carries exactly those rights: read-only without WRITE,
 * delegable only with DELEGATE. */
static uint32_t grant_add(AstraVfsService *owner, uint32_t session,
                          uint32_t rights, uint32_t *send)
{
    HostfsGrant *grant = NULL;
    uint32_t receive = 0u;
    uint32_t scope = 0u;
    uint32_t flags =
        ((rights & ASTRA_VFS_GRANT_WRITE) == 0u ?
             ASTRA_HOST_SCOPE_READ_ONLY : 0u) |
        ((rights & ASTRA_VFS_GRANT_DELEGATE) != 0u ?
             ASTRA_HOST_SCOPE_DELEGATE : 0u);

    *send = 0u;
    for (uint32_t index = 0u; index < grant_capacity; ++index)
        if (grants[index].port.receive == 0u) {
            grant = &grants[index];
            break;
        }
    if (grant == NULL ||
        astra_host_scope_create(device_handle, session, flags, &scope) !=
            ASTRA_SYSCALL_OK)
        return ASTRA_VFS_ERR_LIMIT;
    if (astra_rt_port_create(
            ASTRA_PORT_MESSAGES_MAX,
            ASTRA_PORT_MESSAGES_MAX *
                (uint32_t)sizeof(AstraVfsRenameRequestMessage),
            &receive, send) != ASTRA_SYSCALL_OK) {
        (void)astra_host_scope_revoke(scope);
        (void)astra_close(scope);
        return ASTRA_VFS_ERR_LIMIT;
    }
    memset(grant, 0, sizeof(*grant));
    grant->service = owner;
    grant->session = session;
    grant->port.receive = receive;
    grant->port.id = next_grant_id++;
    grant->port.rights = rights;
    grant->port.scope = scope;
    return ASTRA_VFS_OK;
}

static uint32_t hostfs_derive(void *context, const AstraVfsPortGrant *parent,
                              const char *root, uint32_t rights,
                              uint32_t *send)
{
    const HostfsGrant *from = (const HostfsGrant *)(const void *)parent;
    char provider_root[ASTRA_VFS_PATH_MAX + 1u];
    uint32_t session = 0u;
    uint32_t status;

    (void)context;
    *send = 0u;
    /* A root is a relative path beneath the parent's root; the provider
     * resolves it beneath that session and refuses anything outside. */
    if (root[0] == '/')
        return ASTRA_VFS_ERR_INVALID;
    provider_root[0] = '/';
    (void)strcpy(provider_root + 1, root);
    status = astra_vfs_host_session_open(
        &transport, transport.generation, from->session, 0u, provider_root,
        (rights & ASTRA_VFS_GRANT_WRITE) == 0u ?
            ASTRA_HOST_FS_SESSION_READ_ONLY : 0u, &session);
    if (status != ASTRA_VFS_OK)
        return status;
    status = grant_add(from->service, session, rights, send);
    if (status != ASTRA_VFS_OK)
        (void)astra_vfs_host_session_close(&transport, transport.generation,
                                           session);
    return status;
}

/* The grant's last holder closed its handle. Revoke its scope -- every
 * channel bound to it stops at once -- then end its sessions and its
 * provider session, whose handles go with it (Task 4). */
static void grant_retire(HostfsGrant *grant)
{
    (void)astra_host_scope_revoke(grant->port.scope);
    (void)astra_close(grant->port.scope);
    astra_vfs_service_release_grant(grant->service, grant->port.id);
    (void)astra_vfs_host_session_close(&transport, transport.generation,
                                       grant->session);
    (void)astra_close(grant->port.receive);
    memset(grant, 0, sizeof(*grant));
}
```

The pump is the pre-27 loop (`main.c` after `astra_service_ready_managed`)
with the wait set built from the table each time round:

```c
    for (;;) {
        uint32_t count = 0u;
        uint32_t selected = ASTRA_WAIT_INDEX_NONE;

        wait_handles[count] = shutdown_receive;
        wait_owners[count++] = WAIT_SHUTDOWN;
        wait_handles[count] = metrics_port.receive;
        wait_owners[count++] = WAIT_METRICS;
        for (uint32_t at = 0u; at < grant_capacity; ++at)
            if (grants[at].port.receive != 0u) {
                wait_handles[count] = grants[at].port.receive;
                wait_owners[count++] = at;
            }
        status = astra_wait_multiple(wait_handles, count,
                                     ASTRA_DEADLINE_FOREVER, &selected,
                                     NULL);
        if (status == ASTRA_SYSCALL_OK && selected == 0u) {
            /* the pre-27 shutdown branch, unchanged */
        }
        if (shutdown_requested != 0u)
            return (int)finish_hostfs();
        if (status == ASTRA_SYSCALL_CANCELLED)
            continue;
        if (selected >= count)
            return (int)status;
        if (wait_owners[selected] == WAIT_METRICS) {
            (void)astra_vfs_port_service_worker_pump(&metrics_port,
                                                     &metrics_worker, 1u);
            continue;
        }
        if (status == ASTRA_SYSCALL_PEER_DEAD) {
            grant_retire(&grants[wait_owners[selected]]);
            continue;
        }
        if (status != ASTRA_SYSCALL_OK)
            return (int)status;
        (void)astra_vfs_port_service_grant_pump(
            grant_host(&grants[wait_owners[selected]]), &worker,
            &grants[wait_owners[selected]].port, 1u);
    }
```

A receive port whose last sender has closed, and whose queue is empty,
answers a wait with `PEER_DEAD` (`sw/kernel/port.c:1266-1270`). Confirm
that `astra_wait_multiple` reports the index of the handle that answered on
a `PEER_DEAD` result: read the syscall's wait loop
(`grep -n "WAIT_MULTIPLE" sw/kernel/process.c`). If it does not, the pump
cannot tell which grant died. Stop and report it: that needs a kernel
change outside this plan.

Startup in `astra_main`, after the claim and the two root sessions
(Task 4). Task 4 also created a scope over each session; those calls go,
because grants own their scopes now.
- Read `configured_grants(startup, &grant_capacity)`. On failure, report
  `ASTRA_STATUS_INVALID` through `astra_service_ready_managed`. Allocate
  `grants`, `wait_handles` and `wait_owners` (the last two with
  `grant_capacity + 2` entries) with `astra_runtime_allocate`, zeroed.
- Add the base grants. They are delegable: the supervisor narrows per
  child (Step 7).

  ```c
      uint32_t work_send = 0u;
      uint32_t sound_send = 0u;
      const uint32_t base = ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE |
                            ASTRA_VFS_GRANT_DELEGATE;

      if (status == ASTRA_STATUS_OK &&
          (grant_add(&service, work_session, base, &work_send) !=
               ASTRA_VFS_OK ||
           grant_add(&sound_service, sound_session, base, &sound_send) !=
               ASTRA_VFS_OK))
          status = ASTRA_STATUS_LIMIT;
  ```

- Initialise `port` and `sound_port` over the base grants' receives
  (`grants[0].port.receive`, `grants[1].port.receive`). Give both
  `astra_vfs_port_service_set_derive(&port, hostfs_derive, NULL)` and the
  same for `sound_port`. A port host's reply lanes are keyed by session, and
  sessions are unique per service, so SOUND's grants use `sound_port`.
- Publish `{work_send, metrics_send, sound_send}` in the ready message,
  where `{send, metrics_send, sound_send}` were. The old `receive`/`send`
  and `sound_receive`/`sound_send` pairs go: the grants' ports replace them.

`finish_hostfs` drains each live grant with
`astra_vfs_port_service_grant_pump(grant_host(&grants[i]), &worker, &grants[i].port, 1u)`
until it answers 0. Then it revokes and closes every grant's scope, and
shuts the two services down as before.

`sw/userspace/services/hostfs/Makefile` links the config library again,
exactly as `62b72fc2` did (`git show 62b72fc2 -- sw/userspace/services/hostfs/Makefile`).
In `emu/qemu/astra_image.py`:
- the hostfs line becomes
  `"service /services/hostfs grants HOST_FILESYSTEM CONFIG:r "`
  `"serves WORK:rw METRICS:r SOUND:rw required priority=20\n"`;
- `CONFIGURATION` gains:

  ```python
      "services/hostfs/settings.conf": (
          "astra-config 1\n"
          "schema 1\n"
          "grants 32\n"),
  ```

- update `test-astra-image-providers.py` wherever it asserts the
  manifest text.

- [ ] **Step 7: The supervisor narrows what it grants**

In `sw/userspace/supervisor/src/loader.c`, the namespace member loop of
`build_grants` (`:997-1016`) grants `held->handle` with the wanted rights in
client flags only. When the provider enforces grants, it grants a derived
one instead. That happens for a narrower right, and for a child that does
not `delegate`.

```c
/* A narrower grant of a provider-enforced binding, made once per source
 * handle and rights and kept while the source is published. */
typedef struct SupervisorNarrowed {
    uint32_t source;
    uint32_t rights;
    uint32_t handle;
} SupervisorNarrowed;

static SupervisorNarrowed *narrowed;
static uint32_t narrowed_count;
static uint32_t narrowed_capacity;

static uint32_t narrowed_grant(const AstraAssign *held, uint32_t rights,
                               int delegates, uint32_t *handle)
{
    AstraVfsClient *client = supervisor_vfs_client_for(held);
    uint32_t wanted = ASTRA_VFS_GRANT_READ |
        ((rights & ASTRA_RIGHT_WRITE) != 0u ? ASTRA_VFS_GRANT_WRITE : 0u) |
        (delegates ? ASTRA_VFS_GRANT_DELEGATE : 0u);
    uint32_t status;

    *handle = held->handle;
    if (client == NULL ||
        (client->features & ASTRA_VFS_FEATURE_GRANTS) == 0u)
        return ASTRA_STATUS_OK;
    /* The published grant reads, writes and delegates: a child that may do
     * all three gets it as it is. */
    if ((held->rights & ~rights & ASTRA_RIGHT_WRITE) == 0u && delegates)
        return ASTRA_STATUS_OK;
    for (uint32_t index = 0u; index < narrowed_count; ++index)
        if (narrowed[index].source == held->handle &&
            narrowed[index].rights == wanted) {
            *handle = narrowed[index].handle;
            return ASTRA_STATUS_OK;
        }
    if (narrowed_count == narrowed_capacity) {
        uint32_t capacity = narrowed_capacity == 0u ? 4u :
                            narrowed_capacity * 2u;
        SupervisorNarrowed *grown = astra_runtime_reallocate(
            narrowed, (size_t)capacity * sizeof(*grown));

        if (grown == NULL)
            return ASTRA_STATUS_LIMIT;
        narrowed = grown;
        narrowed_capacity = capacity;
    }
    status = astra_vfs_grant_derive(client, held->root, wanted, handle);
    if (status != ASTRA_VFS_OK)
        return status == ASTRA_VFS_ERR_LIMIT ? ASTRA_STATUS_LIMIT :
                                               ASTRA_STATUS_ACCESS;
    narrowed[narrowed_count++] = (SupervisorNarrowed){
        held->handle, wanted, *handle
    };
    return ASTRA_STATUS_OK;
}

/* A source was unpublished: its derived grants go with it. */
static void narrowed_drop(uint32_t source)
{
    uint32_t index = 0u;

    while (index < narrowed_count) {
        if (narrowed[index].source != source) {
            ++index;
            continue;
        }
        (void)astra_close(narrowed[index].handle);
        narrowed[index] = narrowed[--narrowed_count];
    }
}
```

In the member loop, after the rights check:

```c
status = narrowed_grant(held, wanted->rights, entry->delegates != 0u, &handle);
if (status != ASTRA_STATUS_OK)
    return status;
```

Then pass `handle` to `add_grant` in place of `held->handle`. In
`unpublish_owner`, call `narrowed_drop(service_handles[index]);` before
`(void)astra_close(service_handles[index]);`.

`held->root` is `""` for every host binding today, because HostFS publishes
volume roots. Passing it keeps the call right for the carved-out bindings
that Phase 2 moves to the provider.

- [ ] **Step 8: FS15 says where enforcement is**

`docs/FILESYSTEM_PRODUCTION_PRD.md`, FS15's second paragraph
("Enforcement runs in the guest HostFS service: ..."), becomes:

```markdown
Enforcement is split between an authority and a boundary, and neither is a
server on the data path. HostFS is the authority: it alone holds the host
device's scope right, opens a rooted provider session per grant and mints
that grant's *scope*, a kernel object naming the session and its rights.
A client receives the scope when it says HELLO on the grant's port; the
kernel stamps the scope's session, rights and creator into every channel
the client binds to it, and never a client-supplied owner. QEMU is the
boundary: on a bound channel it runs only filesystem commands, resolves
every path beneath the session's root with `openat2` `RESOLVE_BENEATH`,
refuses mutations through a read-only scope and handles from another
session, and accepts a scope only if the claimed filesystem owner created
it. A scope travels further only when its grant is delegable, and a holder
can narrow a grant only through HostFS (`GRANT_DERIVE`). Revocation goes
through the kernel, which detaches the scope from every channel at once.
```

`docs/STORAGE_AND_VFS.md` "Current implementation": the grant bullet now
names grant ports, per-grant sessions and scopes.

- [ ] **Step 9: The raw confinement cases (`fssem.c`, the gate)**

Task 4 turned `raw-lane` into a `lane_call` helper (Task 4 Step 5).
Add the port-level helpers, and have `raw_open` keep the scope its HELLO
attaches:

```c
#include <astra/metrics.h>
#include <astra/posix.h>
#include <astra/vfs_service.h>

/* A storage-protocol client with no Kit: what a hostile program can send. */
typedef struct RawClient {
    uint32_t service;
    uint32_t reply_receive;
    uint32_t session;
    uint32_t scope;            /* what its HELLO attached */
} RawClient;

#define RAW_LANE 0x5EA1u

static uint32_t raw_open(RawClient *raw, uint32_t service)
{
    uint32_t reply_send = 0u;
    AstraVfsRequestMessage message;
    AstraVfsReplyMessage reply;
    uint32_t handles[1];
    uint32_t size = 0u;
    uint32_t count = 0u;
    uint32_t status;

    memset(raw, 0, sizeof(*raw));
    raw->service = service;
    status = astra_rt_port_create(4u, 4u * (uint32_t)sizeof(reply),
                                  &raw->reply_receive, &reply_send);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    memset(&message, 0, sizeof(message));
    astra_message_header_set(&message.header, (uint32_t)sizeof(message),
                             ASTRA_VFS_PROTOCOL, ASTRA_VFS_VERSION,
                             ASTRA_VFS_OP_HELLO, RAW_LANE);
    message.request.size = ASTRA_VFS_REQUEST_SIZE;
    message.request.version = ASTRA_VFS_VERSION;
    handles[0] = reply_send;
    status = astra_port_send(service, &message, sizeof(message), handles, 1u);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    status = astra_wait_one(raw->reply_receive, ASTRA_DEADLINE_FOREVER, NULL);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_port_receive(raw->reply_receive, &reply, sizeof(reply),
                                    handles, 1u, &size, &count);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    if (reply.reply.status != ASTRA_VFS_OK)
        return reply.reply.status;
    raw->session = reply.reply.session;
    raw->scope = count != 0u ? handles[0] : 0u;
    return ASTRA_SYSCALL_OK;
}

/* One request for `session` on `service`, answered within two seconds or
 * refused: ASTRA_SYSCALL_TIMED_OUT is the service declining to answer. */
static uint32_t raw_call(RawClient *raw, uint32_t service, uint32_t session,
                         uint32_t operation, uint32_t flags, const char *path,
                         AstraVfsReplyMessage *reply, uint32_t *received)
{
    AstraVfsRequestMessage message;
    uint32_t handles[1] = {0u};
    uint32_t size = 0u;
    uint32_t count = 0u;
    uint32_t status;

    memset(&message, 0, sizeof(message));
    astra_message_header_set(&message.header, (uint32_t)sizeof(message),
                             ASTRA_VFS_PROTOCOL, ASTRA_VFS_VERSION,
                             operation, RAW_LANE);
    message.request.size = ASTRA_VFS_REQUEST_SIZE;
    message.request.version = ASTRA_VFS_VERSION;
    message.request.session = session;
    message.request.flags = flags;
    message.request.offset = ASTRA_VFS_MODE_DEFAULT;
    (void)snprintf((char *)message.request.body.path, ASTRA_VFS_PATH_MAX,
                   "%s", path);
    status = astra_port_send(service, &message, sizeof(message), NULL, 0u);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    status = astra_wait_one(raw->reply_receive,
                            astra_clock_monotonic() + UINT64_C(2000000000),
                            NULL);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    status = astra_port_receive(raw->reply_receive, reply, sizeof(*reply),
                                handles, 1u, &size, &count);
    if (received != NULL)
        *received = count != 0u ? handles[0] : 0u;
    else if (count != 0u)
        (void)astra_close(handles[0]);
    return status;
}

/* The grant-relative path of `leaf` in a /work case directory. */
static void wire_path(char *out, size_t capacity, const char *directory,
                      const char *leaf)
{
    (void)snprintf(out, capacity, "%s%s%s", directory + strlen("/work"),
                   leaf[0] != '\0' ? "/" : "", leaf);
}

static uint32_t work_grant(void)
{
    const AstraStartupCapability *work =
        astra_startup_capability(astra_posix_startup(), "WORK");

    return work != NULL ? work->handle : 0u;
}

/* A read-only grant, derived and used raw on its own lane: the provider
 * refuses every mutation, and the grant cannot be widened again. */
static int case_raw_readonly(const char *directory)
{
    RawClient rw;
    RawClient ro;
    AstraVfsReplyMessage reply;
    uint32_t derived = 0u;
    uint32_t session;
    char wire[160];
    char posix[160];
    struct stat st;

    CHECK(work_grant() != 0u, "no WORK grant");
    CHECK(raw_open(&rw, work_grant()) == ASTRA_SYSCALL_OK, "HELLO on WORK");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ, "", &reply, &derived) ==
              ASTRA_SYSCALL_OK && reply.reply.status == ASTRA_VFS_OK &&
          derived != 0u, "derive read-only: status %u",
          (unsigned)reply.reply.status);
    CHECK(raw_open(&ro, derived) == ASTRA_SYSCALL_OK && ro.scope != 0u,
          "HELLO on the derived grant attached no scope");
    CHECK(raw_call(&ro, ro.service, ro.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE, "", &reply,
                   NULL) == ASTRA_SYSCALL_OK &&
          reply.reply.status == ASTRA_VFS_ERR_ACCESS,
          "widened a read-only grant: status %u", (unsigned)reply.reply.status);
    session = scope_session(ro.scope);
    wire_path(wire, sizeof(wire), directory, "ro-made");
    CHECK(lane_call(ro.scope, session, ASTRA_HOST_SERVICE_FILESYSTEM,
                    ASTRA_HOST_FS_MKDIR, 0u, wire) == ASTRA_STATUS_READ_ONLY,
          "read-only MKDIR on the lane was not refused");
    CHECK(lane_call(ro.scope, session, ASTRA_HOST_SERVICE_FILESYSTEM,
                    ASTRA_HOST_FS_OPEN,
                    ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE, wire) ==
              ASTRA_STATUS_READ_ONLY,
          "read-only OPEN for writing on the lane was not refused");
    join(posix, sizeof(posix), directory, "ro-made");
    CHECK(stat(posix, &st) != 0 && errno == ENOENT, "read-only MKDIR made it");
    join(posix, sizeof(posix), directory, "readable");
    CHECK(write_text(posix, "ok", O_WRONLY | O_CREAT | O_EXCL) == 0,
          "seed readable");
    wire_path(wire, sizeof(wire), directory, "readable");
    CHECK(lane_call(ro.scope, session, ASTRA_HOST_SERVICE_FILESYSTEM,
                    ASTRA_HOST_FS_STAT, 0u, wire) == ASTRA_STATUS_OK,
          "read-only STAT on the lane");
    (void)astra_close(ro.scope);
    (void)astra_close(derived);
    return 0;
}

/* A session is refused on any port but its own grant's, and a lane cannot
 * name a session its scope does not bind. */
static int case_raw_cross_grant(const char *directory)
{
    RawClient rw;
    RawClient ro;
    AstraVfsReplyMessage reply;
    uint32_t derived = 0u;
    char wire[160];
    char posix[160];
    struct stat st;

    CHECK(raw_open(&rw, work_grant()) == ASTRA_SYSCALL_OK, "HELLO on WORK");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ, "", &reply, &derived) ==
              ASTRA_SYSCALL_OK && derived != 0u, "derive read-only");
    CHECK(raw_open(&ro, derived) == ASTRA_SYSCALL_OK, "HELLO on derived");
    /* The writable session's id, on the read-only grant's port. */
    CHECK(raw_call(&ro, ro.service, rw.session, ASTRA_VFS_OP_BYE, 0u, "",
                   &reply, NULL) == ASTRA_SYSCALL_TIMED_OUT,
          "a foreign session was answered");
    /* The writable grant's provider session, on the read-only scope's lane. */
    wire_path(wire, sizeof(wire), directory, "borrowed");
    CHECK(lane_call(ro.scope, scope_session(rw.scope),
                    ASTRA_HOST_SERVICE_FILESYSTEM, ASTRA_HOST_FS_MKDIR, 0u,
                    wire) == ASTRA_STATUS_ACCESS,
          "a lane reached a session its scope does not bind");
    join(posix, sizeof(posix), directory, "borrowed");
    CHECK(stat(posix, &st) != 0 && errno == ENOENT,
          "a refused request made the directory");
    (void)astra_close(ro.scope);
    (void)astra_close(rw.scope);
    (void)astra_close(derived);
    return 0;
}

/* A grant derived beneath a sub-directory sees that directory as its root
 * and cannot name its parent's tree or another volume. */
static int case_raw_subroot(const char *directory)
{
    RawClient rw;
    RawClient sub;
    AstraVfsReplyMessage reply;
    uint32_t derived = 0u;
    uint32_t session;
    char wire[160];
    char posix[160];

    join(posix, sizeof(posix), directory, "sub");
    CHECK(mkdir(posix, 0755) == 0, "mkdir sub");
    join(posix, sizeof(posix), directory, "sub/inside");
    CHECK(write_text(posix, "in", O_WRONLY | O_CREAT | O_EXCL) == 0, "seed");
    join(posix, sizeof(posix), directory, "outside");
    CHECK(write_text(posix, "out", O_WRONLY | O_CREAT | O_EXCL) == 0, "seed");
    CHECK(raw_open(&rw, work_grant()) == ASTRA_SYSCALL_OK, "HELLO on WORK");
    wire_path(wire, sizeof(wire), directory, "sub");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ, wire + 1, &reply, &derived) ==
              ASTRA_SYSCALL_OK && reply.reply.status == ASTRA_VFS_OK &&
          derived != 0u, "derive sub: status %u",
          (unsigned)reply.reply.status);
    CHECK(raw_open(&sub, derived) == ASTRA_SYSCALL_OK, "HELLO on sub");
    session = scope_session(sub.scope);
    CHECK(lane_call(sub.scope, session, ASTRA_HOST_SERVICE_FILESYSTEM,
                    ASTRA_HOST_FS_STAT, 0u, "/inside") == ASTRA_STATUS_OK,
          "sub sees inside");
    CHECK(lane_call(sub.scope, session, ASTRA_HOST_SERVICE_FILESYSTEM,
                    ASTRA_HOST_FS_STAT, 0u, "/../outside") != ASTRA_STATUS_OK,
          "sub reached its parent through ..");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ, "../sound", &reply, NULL) ==
              ASTRA_SYSCALL_OK && reply.reply.status != ASTRA_VFS_OK,
          "derived a grant outside the volume");
    (void)astra_close(sub.scope);
    (void)astra_close(rw.scope);
    (void)astra_close(derived);
    return 0;
}

/* Delegation never widens: a scope from a grant without DELEGATE arrives
 * without TRANSFER, and DELEGATE cannot be derived from a grant without it.
 * WORK as fssem holds it may or may not delegate (that is its launcher's
 * choice); the case checks both kinds explicitly. */
static int case_raw_delegate(const char *directory)
{
    RawClient rw;
    RawClient plain;
    RawClient onward;
    AstraVfsReplyMessage reply;
    uint32_t derived = 0u;
    uint32_t again = 0u;
    uint32_t copy = 0u;
    int work_delegates;

    (void)directory;
    CHECK(raw_open(&rw, work_grant()) == ASTRA_SYSCALL_OK, "HELLO on WORK");
    work_delegates = astra_rt_handle_duplicate(rw.scope, ASTRA_RIGHT_READ,
                                               &copy) == ASTRA_SYSCALL_OK;
    if (copy != 0u)
        (void)astra_close(copy);
    copy = 0u;
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE, "", &reply,
                   &derived) == ASTRA_SYSCALL_OK &&
          reply.reply.status == ASTRA_VFS_OK && derived != 0u,
          "derive without DELEGATE");
    CHECK(raw_open(&plain, derived) == ASTRA_SYSCALL_OK && plain.scope != 0u,
          "HELLO on the non-delegable grant");
    CHECK(astra_rt_handle_duplicate(plain.scope, ASTRA_RIGHT_READ, &copy) ==
              ASTRA_SYSCALL_ACCESS_DENIED,
          "a non-delegable scope arrived with TRANSFER");
    CHECK(raw_call(&plain, plain.service, plain.session,
                   ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_DELEGATE, "",
                   &reply, NULL) == ASTRA_SYSCALL_OK &&
          reply.reply.status == ASTRA_VFS_ERR_ACCESS,
          "DELEGATE derived from a grant without it");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_DELEGATE, "",
                   &reply, &again) == ASTRA_SYSCALL_OK &&
          (reply.reply.status == ASTRA_VFS_OK) == work_delegates,
          "derive DELEGATE from WORK (delegable %d): status %u",
          work_delegates, (unsigned)reply.reply.status);
    if (work_delegates) {
        CHECK(raw_open(&onward, again) == ASTRA_SYSCALL_OK &&
              astra_rt_handle_duplicate(onward.scope, ASTRA_RIGHT_READ,
                                        &copy) == ASTRA_SYSCALL_OK,
              "a delegable scope lost TRANSFER");
        (void)astra_close(copy);
        (void)astra_close(onward.scope);
        (void)astra_close(again);
    }
    (void)astra_close(plain.scope);
    (void)astra_close(rw.scope);
    (void)astra_close(derived);
    return 0;
}

/* Retiring a grant -- its last port handle closed -- revokes its scope: the
 * lane then answers ACCESS, not the session's BAD_HANDLE (Task 10 makes it
 * LOST). */
static int case_raw_retire(const char *directory)
{
    RawClient rw;
    RawClient ro;
    AstraVfsReplyMessage reply;
    uint32_t derived = 0u;
    uint32_t session;
    uint32_t answer = ASTRA_STATUS_OK;

    (void)directory;
    CHECK(raw_open(&rw, work_grant()) == ASTRA_SYSCALL_OK, "HELLO on WORK");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ, "", &reply, &derived) ==
              ASTRA_SYSCALL_OK && derived != 0u, "derive");
    CHECK(raw_open(&ro, derived) == ASTRA_SYSCALL_OK, "HELLO on derived");
    session = scope_session(ro.scope);
    CHECK(lane_call(ro.scope, session, ASTRA_HOST_SERVICE_FILESYSTEM,
                    ASTRA_HOST_FS_STAT, 0u, "/") == ASTRA_STATUS_OK,
          "the derived scope works before retirement");
    (void)astra_close(derived);
    for (int tries = 0; tries < 40 && answer == ASTRA_STATUS_OK; ++tries) {
        usleep(50000);
        answer = lane_call(ro.scope, session, ASTRA_HOST_SERVICE_FILESYSTEM,
                           ASTRA_HOST_FS_STAT, 0u, "/");
    }
    CHECK(answer == ASTRA_STATUS_ACCESS,
          "a retired grant's scope answered %u", (unsigned)answer);
    (void)astra_close(ro.scope);
    (void)astra_close(rw.scope);
    return 0;
}
```

`wire + 1` drops the leading slash, because a derived root is relative to
the parent's root.
- Register `{"raw-readonly", case_raw_readonly}`,
  `{"raw-cross-grant", case_raw_cross_grant}`,
  `{"raw-subroot", case_raw_subroot}`,
  `{"raw-delegate", case_raw_delegate}` and
  `{"raw-retire", case_raw_retire}`.
- In `test-fs-semantics.py`, extend `CASES` with the same five names.
- `lane_call` and `scope_session` are Task 4's helpers (Task 4 Step 5).
- These cases call `astra_posix_startup` and `astra_startup_capability`.
  If `<astra/posix.h>` does not declare the first, use the declaration
  `file.c` uses (`grep -n "astra_posix_startup" sw/userspace/posix/include/astra/*.h`).
- `astra_wait_one`'s deadline is absolute monotonic nanoseconds; confirm
  with `grep -n -B6 "uint32_t astra_wait_one" ndk/include/astra/runtime.h`.

- [ ] **Step 10: Run, then perturb**

Sync, then on beast:

```sh
~/astra-mg/locked make -C sw/userspace -j32
~/astra-mg/locked make -C sw/userspace test
~/astra-mg/locked make -C sw/boot -j32
~/astra-mg/locked bash ~/astra-mg/images.sh
Q=$(tail -n1 /tmp/qemu-host-build.log); R=sw/boot/build/astra_boot.bin
~/astra-mg/locked python3 emu/qemu/test-fs-semantics.py $Q $R ~/astra-mg/base.img
```

Expected:
- the core and port tests PASS;
- `FSSEM PASS raw-readonly`, `raw-cross-grant`, `raw-subroot`,
  `raw-delegate` and `raw-retire`;
- `PASS lane <n>` and `ASTRA FS SEMANTICS PASS`.

Perturbations. Restore and re-sync with the `--no-times` rsync after each.

1. **Read-only, one layer.** In `grant_add`, never set
   `ASTRA_HOST_SCOPE_READ_ONLY`. Expected: `raw-readonly` still *passes*:
   the session is read-only in QEMU too (Task 4). Then also drop
   `ASTRA_HOST_FS_SESSION_READ_ONLY` in `hostfs_derive`. Expected:
   `raw-readonly` fails "read-only MKDIR on the lane was not refused". Both
   layers refuse, and either alone is enough.
2. **Session binding.** In `astra_vfs_service_session_bound`, compare only
   the owner. Expected: `raw-cross-grant` fails "a foreign session was
   answered", and the core test fails on
   `!astra_vfs_service_session_bound(..., 2u)`.
3. **Delegation.** In `grant_add`, always set `ASTRA_HOST_SCOPE_DELEGATE`.
   Expected: `raw-delegate` fails "a non-delegable scope arrived with
   TRANSFER".
4. **Widening.** In `pump`'s GRANT_DERIVE branch, drop
   `(incoming->request.flags & ~grant->rights) != 0u ||`. Expected: the port
   test fails at `derive_calls == 0u`.
5. **Retirement.** In `grant_retire`, skip `astra_host_scope_revoke`.
   Expected: `raw-retire` fails "a retired grant's scope answered 9"
   (`BAD_HANDLE`: the session is gone, but the scope is still bound).

- [ ] **Step 11: Full suite, measurement, commit**

`~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-task5.log`. Expected: green.
Run one `--bench t5` and compare it with Task 3b's `qemu-t3b-1.json`.
Record the `stat-warm` and `open-close-warm` ratios in the commit message.
Grants are control-plane only, so both should be within run-to-run noise.

```bash
git add sw/include/astra/vfs_service.h sw/userspace/vfs ndk/include/astra ndk/tests/headers.c \
  sw/userspace/services/hostfs sw/userspace/supervisor/src/loader.c \
  sw/userspace/commands/fssem/fssem.c emu/qemu/test-fs-semantics.py \
  emu/qemu/astra_image.py emu/qemu/test-astra-image-providers.py \
  sw/userspace/posix/Makefile sw/userspace/libraries.mk \
  sw/userspace/kits sw/userspace/config/Makefile sw/userspace/tests/test_boundaries.py \
  sw/userspace/lua/Makefile sw/userspace/apps/Terminal.app/manifest \
  sw/userspace/apps/InterfaceGallery.app/manifest ndk/docs/source/filesystem.md \
  tools/tests/test_check_bundle_dependencies.py docs/FILESYSTEM_KIT.md docs/ABI.md \
  docs/FILESYSTEM_PRODUCTION_PRD.md docs/STORAGE_AND_VFS.md
git commit -m "feat(hostfs): grants are ports with scopes; derive narrows, delegation never widens (FS15); storage protocol 29, Kit 7"
```

---

### Task 6: Ordered path resolution (D3)

QEMU stops refusing `.`, `..` and symbolic links (`astra68.c:3025-3150`,
`:3873-3973`) and resolves them in order beneath the session root, the way
Haiku's `vnode_path_to_vnode` does (`vfs.cpp:2204-2378`). A path that leaves
the root does not fail. The provider says where it left
(`ASTRA_STATUS_ESCAPED`: through `..`, or through a link with its target),
and the Filesystem Kit continues in the Astra namespace. The Kit and libc
stop folding `..` for a binding whose provider resolves.

D3 leaves one choice to measurement: a per-component walk alone, or
`openat2(RESOLVE_BENEATH)` first with the walk as fallback. The walk is
needed either way, because only it can say *where* a path left the root,
and an `EXDEV` from `openat2` cannot. This task builds both, measures them
on beast and the DE25, decides by the rule in Step 4, and deletes the
loser.

**Files:**
- Create: `emu/qemu/qemu-9.2/hw/m68k/astra68-resolve.c`, `emu/qemu/qemu-9.2/hw/m68k/astra68-resolve.h`
- Create: `emu/qemu/tests/test-astra68-resolve.c`, `emu/qemu/tests/Makefile`
- Modify: `emu/qemu/qemu-9.2/meson.build.patch` (build `astra68-resolve.c`)
- Modify: `emu/qemu/qemu-9.2/hw/m68k/astra68.c`
- Modify: `sw/include/astra/status.h`, `sw/include/astra/host.h`, `sw/include/astra/vfs_service.h`
- Modify: `sw/userspace/vfs/src/vfs_status.c`, `sw/userspace/vfs/include/astra/vfs_backend.h`, `sw/userspace/vfs/src/vfs_service_core.c`, `sw/userspace/vfs/src/vfs_port_transport.c`, `sw/userspace/vfs/src/backends/host.c`
- Modify: `sw/userspace/vfs/src/vfs_assign.c`, `sw/userspace/vfs/src/vfs_path.c`, `sw/userspace/vfs/src/vfs_union.c`, `sw/userspace/vfs/src/vfs_client.c`, `sw/userspace/vfs/src/filesystem_library.c`, `sw/userspace/vfs/src/process_filesystem.c`, `ndk/include/astra/vfs_client.h`, `ndk/include/astra/vfs_assign.h`, `ndk/include/astra/vfs_union.h`, `sw/userspace/vfs/filesystem.library.exports`
- Modify: `sw/userspace/posix/src/path.c`, `sw/userspace/posix/src/file.c` (open flags, errno)
- Modify: `sw/userspace/services/hostfs/main.c` (features)
- Modify: `emu/qemu/test-hostfs.py`, `emu/qemu/fs-gates.sh`, `sw/userspace/commands/fssem/fssem.c`, `emu/qemu/test-fs-semantics.py`
- Test: `sw/userspace/vfs/tests/test_vfs_union.c`, `sw/userspace/vfs/tests/test_vfs_path.c`, `sw/userspace/vfs/tests/test_vfs_service_core.c`

**Interfaces:**
- Produces (resolver): `astra_resolve(root, base, path, flags, AstraResolution *) -> 0 | -errno`,
  `ASTRA_RESOLVE_FOLLOW_FINAL`, `ASTRA_RESOLVE_ESCAPE_PARENT` (1),
  `ASTRA_RESOLVE_ESCAPE_SYMLINK` (2), `ASTRA_RESOLVE_LINK_MAX` (40).
- Produces (status): `ASTRA_STATUS_ESCAPED` (20) / `ASTRA_VFS_ERR_ESCAPED`.
- Produces (host): `ASTRA_HOST_VERSION 0x0001000f`,
  `ASTRA_HOST_CAP_FS_RESOLVES (1u << 12)`, `ASTRA_HOST_FS_STAT_FOLLOW`
  (STAT flag), `ASTRA_HOST_FS_ESCAPE_PARENT`/`_SYMLINK`. On ESCAPED,
  `result_value` answers the bytes consumed and `value_lo` the kind.
- Produces (storage protocol 30): `ASTRA_VFS_FEATURE_RESOLVES_PATHS (1u << 1)`,
  `ASTRA_VFS_OPEN_NOFOLLOW (1u << 7)`, `ASTRA_VFS_STAT_FOLLOW (1u << 0)`
  (a STAT flag; Task 9 extends it to STAT_AT), and `AstraVfsEscape` (ESCAPED's payload).
- Produces (backend): optional `stat_follow` and `escape` operations in
  `AstraVfsBackendOps` (NULL: unsupported).
- Produces (Kit): `astra_vfs_lookup(client, path, flags, entry, escape)`,
  `astra_vfs_features(client)`, `astra_assign_resolve_raw(...)`,
  `astra_vfs_assign_wire(...)`, `astra_path_normalise_escape(...)`.
- Consumed by: Task 7 (identity of resolved nodes), Task 9 (cwd-relative
  resolution), Task 12 (bootstrap reads through the same resolver).

- [ ] **Step 1: The resolver and its failing host test**

`emu/qemu/qemu-9.2/hw/m68k/astra68-resolve.h`:

```c
#ifndef ASTRA68_RESOLVE_H
#define ASTRA68_RESOLVE_H

/*
 * Storage pathname resolution beneath a session root
 * (docs/FILESYSTEM_PRODUCTION_PRD.md FS14). Plain C and POSIX: it builds
 * into QEMU and into the host test in emu/qemu/tests.
 */

#include <stdint.h>

/** Follow a symbolic link in the final component. Without it the final
 * component is the answer, link or not (lstat). A trailing separator
 * always follows. */
#define ASTRA_RESOLVE_FOLLOW_FINAL 1u
/** Links followed in one resolution before ELOOP, as Linux does. */
#define ASTRA_RESOLVE_LINK_MAX 40u
/** The path left the root through "..". */
#define ASTRA_RESOLVE_ESCAPE_PARENT 1u
/** The path left the root through a symbolic link: an absolute target, or
 * a relative one that climbs above the root. */
#define ASTRA_RESOLVE_ESCAPE_SYMLINK 2u

typedef struct AstraResolution {
    int parent;          /* the directory holding `leaf`; the caller closes it */
    char leaf[256];      /* final component, or "" when the path names `parent` */
    uint32_t trailing;   /* the path ended in a separator */
    uint32_t escape;     /* ASTRA_RESOLVE_ESCAPE_* when the answer is -EXDEV */
    uint32_t consumed;   /* on escape: bytes of the path through the component
                            that left, or through the outermost link whose
                            expansion left */
} AstraResolution;

/*
 * Resolves `path` beneath `root`, starting at `base` (the root itself, or an
 * open directory beneath it); an absolute path starts at the root. Every
 * component but the last is followed. Answers 0 with `parent` and `leaf`;
 * -EXDEV with `escape` and `consumed` when the path leaves `root`; -ESTALE
 * when `base` is no longer beneath `root`; or another -errno. It never opens
 * anything outside `root`.
 */
int astra_resolve(int root, int base, const char *path, unsigned flags,
                  AstraResolution *out);
/* The candidates the Task 6 measurement compares. */
int astra_resolve_walk(int root, int base, const char *path, unsigned flags,
                       AstraResolution *out);
#ifdef __linux__
/* openat2(RESOLVE_BENEATH) for the parent: 0 or -errno when it decided,
 * 1 when only the walk can (an escape, a final link or directory). */
int astra_resolve_beneath(int base, const char *path, unsigned flags,
                          AstraResolution *out);
#endif

#endif
```

`emu/qemu/qemu-9.2/hw/m68k/astra68-resolve.c`:

```c
/*
 * Storage pathname resolution beneath a session root.
 *
 * Components are taken in order: ".", "..", symbolic links and trailing
 * separators mean what POSIX says, and nothing is folded before lookup.
 * Haiku resolves the same way (src/system/kernel/fs/vfs.cpp:2204-2378,
 * vnode_path_to_vnode): ".." at the IO context's root stays there, and a
 * link restarts from the root when absolute. Here the root is a session
 * root, and the namespace above it belongs to the Filesystem Kit, so
 * leaving the root is not an error but an answer: -EXDEV and where.
 *
 * Directories are held open from the starting base downward, so ".." pops
 * and never asks the filesystem. Only a ".." above the base looks up its
 * ancestors, once, and that is also where a base moved out from under its
 * root is found (-ESTALE).
 */

#include "astra68-resolve.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/openat2.h>
#include <sys/syscall.h>
#endif

#ifdef __linux__
# define DIRECTORY_FLAGS (O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
#else
# define DIRECTORY_FLAGS (O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
#endif
/* Spliced link targets plus the rest of the path. */
#define RESOLVE_TEXT_MAX 16384u
#define ANCESTORS_MAX 4096u

typedef struct Walk {
    dev_t root_device;
    ino_t root_inode;
    int *stack;          /* open directories; the innermost is last */
    uint32_t depth;
    uint32_t capacity;
    bool anchored;       /* stack[0] is the root */
} Walk;

static bool is_root(const Walk *walk, int fd)
{
    struct stat st;

    return fstat(fd, &st) == 0 && st.st_dev == walk->root_device &&
           st.st_ino == walk->root_inode;
}

static int reserve(Walk *walk, uint32_t needed)
{
    int *grown;
    uint32_t capacity = walk->capacity == 0u ? 16u : walk->capacity;

    while (capacity < needed)
        capacity *= 2u;
    if (capacity == walk->capacity)
        return 0;
    grown = realloc(walk->stack, capacity * sizeof(*grown));
    if (grown == NULL)
        return -ENOMEM;
    walk->stack = grown;
    walk->capacity = capacity;
    return 0;
}

static int push(Walk *walk, int fd)
{
    int status = reserve(walk, walk->depth + 1u);

    if (status != 0) {
        close(fd);
        return status;
    }
    walk->stack[walk->depth++] = fd;
    return 0;
}

static void empty(Walk *walk)
{
    while (walk->depth != 0u)
        close(walk->stack[--walk->depth]);
}

static int restart_at_root(Walk *walk, int root)
{
    int fd;

    empty(walk);
    fd = dup(root);
    if (fd < 0)
        return -errno;
    walk->anchored = true;
    return push(walk, fd);
}

/* The stack holds only the base, which is not the root: put its ancestors,
 * up to the root, beneath it so ".." can pop them. */
static int anchor(Walk *walk)
{
    int chain[ANCESTORS_MAX];
    uint32_t count = 0u;
    int child = walk->stack[0];
    int status = -ESTALE;

    while (count < ANCESTORS_MAX) {
        struct stat below;
        struct stat above;
        int parent = openat(child, "..", DIRECTORY_FLAGS);

        if (parent < 0) {
            status = -errno;
            break;
        }
        if (fstat(child, &below) != 0 || fstat(parent, &above) != 0) {
            status = -errno;
            close(parent);
            break;
        }
        if (above.st_dev == below.st_dev && above.st_ino == below.st_ino) {
            close(parent);           /* the filesystem's own root */
            break;
        }
        chain[count++] = parent;
        if (above.st_dev == walk->root_device &&
            above.st_ino == walk->root_inode) {
            status = reserve(walk, count + 1u);
            if (status != 0)
                break;
            walk->stack[count] = walk->stack[0];
            for (uint32_t at = 0u; at < count; ++at)
                walk->stack[at] = chain[count - 1u - at];
            walk->depth = count + 1u;
            walk->anchored = true;
            return 0;
        }
        child = parent;
    }
    for (uint32_t at = 0u; at < count; ++at)
        close(chain[at]);
    return status;
}

int astra_resolve_walk(int root, int base, const char *path, unsigned flags,
                       AstraResolution *out)
{
    Walk walk = {0};
    struct stat root_st;
    char *text;
    size_t at = 0u;
    uint32_t links = 0u;
    uint32_t splice_end = 0u;
    int status;
    int fd;

    out->parent = -1;
    out->leaf[0] = '\0';
    out->trailing = 0u;
    out->escape = 0u;
    out->consumed = 0u;
    if (fstat(root, &root_st) != 0)
        return -errno;
    walk.root_device = root_st.st_dev;
    walk.root_inode = root_st.st_ino;
    text = strdup(path);
    if (text == NULL)
        return -ENOMEM;
    fd = dup(base);
    status = fd < 0 ? -errno : push(&walk, fd);
    if (status != 0)
        goto done;
    walk.anchored = is_root(&walk, base);
    if (text[0] == '/') {
        status = restart_at_root(&walk, root);
        if (status != 0)
            goto done;
    }
    for (;;) {
        int top = walk.stack[walk.depth - 1u];
        size_t start;
        size_t end;
        size_t next;
        bool last;
        bool trailing;
        char name[256];

        while (text[at] == '/')
            ++at;
        if (text[at] == '\0')
            break;                       /* the directory on top itself */
        start = at;
        while (text[at] != '\0' && text[at] != '/')
            ++at;
        end = at;
        next = at;
        while (text[next] == '/')
            ++next;
        last = text[next] == '\0';
        trailing = last && next != end;
        if (end - start >= sizeof(name)) {
            status = -ENAMETOOLONG;
            goto done;
        }
        memcpy(name, text + start, end - start);
        name[end - start] = '\0';
        if (strcmp(name, ".") == 0) {
            at = next;
            continue;
        }
        if (strcmp(name, "..") == 0) {
            if (walk.depth == 1u && !walk.anchored) {
                status = anchor(&walk);
                if (status != 0)
                    goto done;
            }
            if (walk.depth == 1u) {
                out->escape = splice_end != 0u ?
                    ASTRA_RESOLVE_ESCAPE_SYMLINK : ASTRA_RESOLVE_ESCAPE_PARENT;
                out->consumed = splice_end != 0u ? splice_end : (uint32_t)end;
                status = -EXDEV;
                goto done;
            }
            close(walk.stack[--walk.depth]);
            at = next;
            continue;
        }
        if (last && !trailing && (flags & ASTRA_RESOLVE_FOLLOW_FINAL) == 0u) {
            memcpy(out->leaf, name, end - start + 1u);
            break;
        }
        fd = openat(top, name, DIRECTORY_FLAGS);
        if (fd >= 0) {
            status = push(&walk, fd);
            if (status != 0)
                goto done;
            at = next;
            continue;
        }
        if (errno == ENOENT && last) {
            memcpy(out->leaf, name, end - start + 1u);
            out->trailing = trailing;
            break;
        }
        if (errno != ENOTDIR && errno != ELOOP) {
            status = -errno;
            goto done;
        }
        {
            struct stat st;
            char target[4096];
            ssize_t length;
            size_t rest_length;
            char *spliced;

            if (fstatat(top, name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
                status = -errno;
                goto done;
            }
            if (!S_ISLNK(st.st_mode)) {
                if (last && !trailing) {
                    memcpy(out->leaf, name, end - start + 1u);
                    break;
                }
                status = -ENOTDIR;
                goto done;
            }
            if (++links > ASTRA_RESOLVE_LINK_MAX) {
                status = -ELOOP;
                goto done;
            }
            length = readlinkat(top, name, target, sizeof(target) - 1u);
            if (length <= 0) {
                status = length == 0 ? -ENOENT : -errno;
                goto done;
            }
            target[length] = '\0';
            if (splice_end == 0u)
                splice_end = (uint32_t)end;
            if (target[0] == '/') {
                out->escape = ASTRA_RESOLVE_ESCAPE_SYMLINK;
                out->consumed = splice_end;
                status = -EXDEV;
                goto done;
            }
            rest_length = strlen(text + end);
            if ((size_t)length + rest_length + 1u > RESOLVE_TEXT_MAX) {
                status = -ENAMETOOLONG;
                goto done;
            }
            spliced = malloc((size_t)length + rest_length + 1u);
            if (spliced == NULL) {
                status = -ENOMEM;
                goto done;
            }
            memcpy(spliced, target, (size_t)length);
            memcpy(spliced + length, text + end, rest_length + 1u);
            free(text);
            text = spliced;
            at = 0u;
        }
    }
    out->parent = dup(walk.stack[walk.depth - 1u]);
    status = out->parent < 0 ? -errno : 0;
done:
    empty(&walk);
    free(walk.stack);
    free(text);
    return status;
}

#ifdef __linux__
int astra_resolve_beneath(int base, const char *path, unsigned flags,
                          AstraResolution *out)
{
    struct open_how how;
    const char *slash = strrchr(path, '/');
    const char *leaf = slash != NULL ? slash + 1 : path;
    size_t length = strlen(path);
    size_t leaf_length = strlen(leaf);
    char *directory;
    int fd;

    out->parent = -1;
    out->leaf[0] = '\0';
    out->trailing = 0u;
    out->escape = 0u;
    out->consumed = 0u;
    if (length == 0u || path[0] == '/' || path[length - 1u] == '/' ||
        leaf_length >= sizeof(out->leaf) || strcmp(leaf, ".") == 0 ||
        strcmp(leaf, "..") == 0)
        return 1;
    directory = slash != NULL ? strndup(path, (size_t)(slash - path)) :
                                strdup(".");
    if (directory == NULL)
        return -ENOMEM;
    memset(&how, 0, sizeof(how));
    how.flags = O_PATH | O_DIRECTORY | O_CLOEXEC;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS;
    fd = (int)syscall(SYS_openat2, base, directory, &how, sizeof(how));
    free(directory);
    if (fd < 0)
        return errno == EXDEV || errno == ELOOP || errno == ENOSYS ||
               errno == EAGAIN ? 1 : -errno;
    if ((flags & ASTRA_RESOLVE_FOLLOW_FINAL) != 0u) {
        struct stat st;

        if (fstatat(fd, leaf, &st, AT_SYMLINK_NOFOLLOW) == 0 &&
            (S_ISLNK(st.st_mode) || S_ISDIR(st.st_mode))) {
            close(fd);
            return 1;
        }
    }
    memcpy(out->leaf, leaf, leaf_length + 1u);
    out->parent = fd;
    return 0;
}
#endif

/* Decided by measurement, Task 6 Step 4. */
#ifndef ASTRA_RESOLVE_BENEATH_FIRST
#define ASTRA_RESOLVE_BENEATH_FIRST 0
#endif

int astra_resolve(int root, int base, const char *path, unsigned flags,
                  AstraResolution *out)
{
#if defined(__linux__) && ASTRA_RESOLVE_BENEATH_FIRST
    int decided = astra_resolve_beneath(base, path, flags, out);

    if (decided <= 0)
        return decided;
#endif
    return astra_resolve_walk(root, base, path, flags, out);
}
```

`RESOLVE_BENEATH` is relative to `base`, which is stricter than the root
when `base` is beneath it: anything `openat2` resolves is inside the root.
Whatever it refuses (`EXDEV`), the walk decides.

The host test, `emu/qemu/tests/test-astra68-resolve.c`, builds a tree and
asserts a table of resolutions for both strategies, with each answer
checked by the identity of the node it names:

```c
/*
 * The resolver against a real tree (plan Task 6). With --bench N it times
 * both candidate strategies instead.
 */
#define _GNU_SOURCE
#include "astra68-resolve.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char top[256];

static void mk(const char *relative)
{
    char path[512];

    snprintf(path, sizeof(path), "%s/%s", top, relative);
    assert(mkdir(path, 0755) == 0);
}

static void file(const char *relative)
{
    char path[512];
    int fd;

    snprintf(path, sizeof(path), "%s/%s", top, relative);
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    assert(fd >= 0 && close(fd) == 0);
}

static void link_to(const char *target, const char *relative)
{
    char path[512];

    snprintf(path, sizeof(path), "%s/%s", top, relative);
    assert(symlink(target, path) == 0);
}

static ino_t inode_of(const char *relative)
{
    char path[512];
    struct stat st;

    snprintf(path, sizeof(path), "%s/%s", top, relative);
    assert(lstat(path, &st) == 0);
    return st.st_ino;
}

/* The inode a resolution names: its leaf in its parent, or the parent. */
static ino_t named(const AstraResolution *out)
{
    struct stat st;

    if (out->leaf[0] == '\0')
        assert(fstat(out->parent, &st) == 0);
    else if (fstatat(out->parent, out->leaf, &st, AT_SYMLINK_NOFOLLOW) != 0)
        return 0;
    return st.st_ino;
}

typedef struct Case {
    const char *base;      /* relative to root/, "" for root */
    const char *path;
    unsigned flags;
    int status;            /* 0, or -errno */
    uint32_t escape;
    uint32_t consumed;
    const char *names;     /* expected node, relative to top/, or NULL */
    const char *leaf;      /* expected leaf when the node does not exist */
} Case;

static const Case cases[] = {
    {"", "a/f", 0, 0, 0, 0, "root/a/f", NULL},
    {"", "/a/f", 0, 0, 0, 0, "root/a/f", NULL},
    {"", "a/./f", 0, 0, 0, 0, "root/a/f", NULL},
    {"", "a/b/../f", 0, 0, 0, 0, "root/a/f", NULL},
    {"", "a/..", 0, 0, 0, 0, "root", NULL},
    {"", "..", 0, -EXDEV, ASTRA_RESOLVE_ESCAPE_PARENT, 2, NULL, NULL},
    {"", "a/../../x", 0, -EXDEV, ASTRA_RESOLVE_ESCAPE_PARENT, 7, NULL, NULL},
    {"", "/..", 0, -EXDEV, ASTRA_RESOLVE_ESCAPE_PARENT, 3, NULL, NULL},
    {"", "l/f", 0, 0, 0, 0, "root/a/f", NULL},
    {"", "l", 0, 0, 0, 0, "root/l", NULL},
    {"", "l", ASTRA_RESOLVE_FOLLOW_FINAL, 0, 0, 0, "root/a", NULL},
    {"", "l/", 0, 0, 0, 0, "root/a", NULL},
    {"", "abs/x", 0, -EXDEV, ASTRA_RESOLVE_ESCAPE_SYMLINK, 3, NULL, NULL},
    {"", "esc/sentinel", 0, -EXDEV, ASTRA_RESOLVE_ESCAPE_SYMLINK, 3, NULL,
     NULL},
    {"", "up/a/f", 0, -EXDEV, ASTRA_RESOLVE_ESCAPE_SYMLINK, 2, NULL, NULL},
    {"", "within/f", 0, 0, 0, 0, "root/a/f", NULL},
    {"", "l1", ASTRA_RESOLVE_FOLLOW_FINAL, -ELOOP, 0, 0, NULL, NULL},
    {"", "a/f/", 0, -ENOTDIR, 0, 0, NULL, NULL},
    {"", "a/f/x", 0, -ENOTDIR, 0, 0, NULL, NULL},
    {"", "a/missing", 0, 0, 0, 0, NULL, "missing"},
    {"", "missing/x", 0, -ENOENT, 0, 0, NULL, NULL},
    {"", "a/new/", 0, 0, 0, 0, NULL, "new"},
    {"a/b", "../f", 0, 0, 0, 0, "root/a/f", NULL},
    {"a/b", "../../..", 0, -EXDEV, ASTRA_RESOLVE_ESCAPE_PARENT, 8, NULL,
     NULL},
    {"a/b", "/a/f", 0, 0, 0, 0, "root/a/f", NULL},
};

static void check(int (*resolve)(int, int, const char *, unsigned,
                                 AstraResolution *), int root)
{
    for (size_t index = 0u; index < sizeof(cases) / sizeof(cases[0]);
         ++index) {
        const Case *test = &cases[index];
        AstraResolution out;
        int base = root;
        int status;

        if (test->base[0] != '\0') {
            base = openat(root, test->base, O_RDONLY | O_DIRECTORY);
            assert(base >= 0);
        }
        status = resolve(root, base, test->path, test->flags, &out);
        if (status == 1) {               /* beneath: the walk decides */
            if (base != root)
                close(base);
            continue;
        }
        if (status != test->status) {
            fprintf(stderr, "case %zu %s: status %d, want %d\n", index,
                    test->path, status, test->status);
            abort();
        }
        if (status == -EXDEV) {
            assert(out.escape == test->escape);
            assert(out.consumed == test->consumed);
        }
        if (status == 0 && test->names != NULL)
            assert(named(&out) == inode_of(test->names));
        if (status == 0 && test->leaf != NULL)
            assert(strcmp(out.leaf, test->leaf) == 0 && named(&out) == 0);
        if (status == 0)
            close(out.parent);
        if (base != root)
            close(base);
    }
}

/* A base moved out from under its root answers -ESTALE on "..". */
static void check_moved_base(int root)
{
    char from[512];
    char to[512];
    AstraResolution out;
    int base;

    base = openat(root, "a/b", O_RDONLY | O_DIRECTORY);
    assert(base >= 0);
    snprintf(from, sizeof(from), "%s/root/a/b", top);
    snprintf(to, sizeof(to), "%s/outside/b", top);
    assert(rename(from, to) == 0);
    assert(astra_resolve_walk(root, base, "../x", 0, &out) == -ESTALE);
    assert(rename(to, from) == 0);
    close(base);
}

static long long now_ns(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000000000LL + t.tv_nsec;
}

static void bench(int root, unsigned iterations)
{
    static const char *const paths[] = {"d1/f", "d1/d2/d3/d4/f",
                                        "d1/d2/d3/d4/d5/d6/d7/d8/f"};
    static const unsigned depths[] = {1u, 4u, 8u};

    for (size_t index = 0u; index < 3u; ++index) {
        AstraResolution out;
        long long start = now_ns();

        for (unsigned round = 0u; round < iterations; ++round) {
            assert(astra_resolve_walk(root, root, paths[index], 0, &out) == 0);
            close(out.parent);
        }
        printf("walk depth=%u ns=%lld\n", depths[index],
               (now_ns() - start) / (long long)iterations);
#ifdef __linux__
        start = now_ns();
        for (unsigned round = 0u; round < iterations; ++round) {
            assert(astra_resolve_beneath(root, paths[index], 0, &out) == 0);
            close(out.parent);
        }
        printf("beneath depth=%u ns=%lld\n", depths[index],
               (now_ns() - start) / (long long)iterations);
#endif
    }
}

int main(int argc, char **argv)
{
    int root;

    snprintf(top, sizeof(top), "/tmp/astra68-resolve-XXXXXX");
    assert(mkdtemp(top) != NULL);
    mk("root");
    mk("outside");
    file("outside/sentinel");
    mk("root/a");
    mk("root/a/b");
    file("root/a/f");
    link_to("a", "root/l");
    link_to("/etc", "root/abs");
    link_to("../outside", "root/esc");
    link_to("..", "root/up");
    link_to("a/b/..", "root/within");
    link_to("l2", "root/l1");
    link_to("l1", "root/l2");
    mk("root/d1");
    mk("root/d1/d2");
    mk("root/d1/d2/d3");
    mk("root/d1/d2/d3/d4");
    file("root/d1/f");
    file("root/d1/d2/d3/d4/f");
    mk("root/d1/d2/d3/d4/d5");
    mk("root/d1/d2/d3/d4/d5/d6");
    mk("root/d1/d2/d3/d4/d5/d6/d7");
    mk("root/d1/d2/d3/d4/d5/d6/d7/d8");
    file("root/d1/d2/d3/d4/d5/d6/d7/d8/f");
    {
        char path[512];

        snprintf(path, sizeof(path), "%s/root", top);
        root = open(path, O_RDONLY | O_DIRECTORY);
        assert(root >= 0);
    }
    if (argc == 3 && strcmp(argv[1], "--bench") == 0) {
        bench(root, (unsigned)strtoul(argv[2], NULL, 10));
    } else {
        check(astra_resolve_walk, root);
#ifdef __linux__
        check(astra_resolve_beneath, root);
#endif
        check(astra_resolve, root);
        check_moved_base(root);
        puts("ASTRA68 RESOLVE PASS");
    }
    return 0;
}
```

The `up/a/f` case shows link accounting. `up` is `..` from the root, so its
expansion leaves the root; the escape is charged to the link `up`
(`consumed` 2, through "up"), and the Kit continues from that link's target.
Clean the temporary tree with `rm -rf /tmp/astra68-resolve-*` after a run;
the test leaves it for inspection when it fails.

`emu/qemu/tests/Makefile`:

```make
# Host tests of QEMU-side Astra code that builds without QEMU.
CC ?= cc
CROSS_AARCH64 ?= aarch64-linux-gnu-gcc
CFLAGS ?= -O2 -std=gnu11 -Wall -Wextra -Werror
M68K := ../qemu-9.2/hw/m68k
RESOLVE := $(M68K)/astra68-resolve.c $(M68K)/astra68-resolve.h

build/test-astra68-resolve: test-astra68-resolve.c $(RESOLVE)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -I$(M68K) $< $(M68K)/astra68-resolve.c -o $@

build/aarch64/test-astra68-resolve: test-astra68-resolve.c $(RESOLVE)
	@mkdir -p $(@D)
	$(CROSS_AARCH64) $(CFLAGS) -static -I$(M68K) $< \
		$(M68K)/astra68-resolve.c -o $@

test: build/test-astra68-resolve
	./build/test-astra68-resolve

.PHONY: test
```

Run on beast: `~/astra-mg/locked make -C emu/qemu/tests test`. Expected: it
builds and fails on the first case whose expectation the code does not yet
meet; while writing it, iterate until `ASTRA68 RESOLVE PASS`. Then perturb
it in Step 3.

- [ ] **Step 2: Wire the resolver into QEMU**

`emu/qemu/qemu-9.2/meson.build.patch`: the hunk line
`+m68k_ss.add(files('astra68.c'))` becomes
`+m68k_ss.add(files('astra68.c', 'astra68-resolve.c'))`.

`sw/include/astra/status.h`: replace `/* 20..31 are unassigned, and are the system's to spend. */` with:

```c
/** A path left the root it was resolved in; the answer says where, so the
 * namespace above can continue (docs/FILESYSTEM_PRODUCTION_PRD.md FS14). */
#define ASTRA_STATUS_ESCAPED         20
/* 21..31 are unassigned, and are the system's to spend. */
```

and update the `ASTRA_STATUS_SYSTEM_MAX` comment to "21..31". Add
`"left its root"` as entry 20 of `vfs_status.c`'s table, and
`#define ASTRA_VFS_ERR_ESCAPED ((uint32_t)ASTRA_STATUS_ESCAPED)` beside
`ASTRA_VFS_ERR_READ_ONLY` in `vfs_service.h`.

`sw/include/astra/host.h`: `ASTRA_HOST_VERSION` -> `0x0001000f`, and add:

```c
/** The provider resolves ".", ".." and symbolic links in order beneath a
 * session root, and answers ASTRA_STATUS_ESCAPED where a path leaves it. */
#define ASTRA_HOST_CAP_FS_RESOLVES (1u << 12)
/** STAT flag: follow a symbolic link in the final component. */
#define ASTRA_HOST_FS_STAT_FOLLOW       (1u << 0)
/** ESCAPED answers, in value_lo, with result_value the bytes consumed. */
#define ASTRA_HOST_FS_ESCAPE_PARENT     1u
#define ASTRA_HOST_FS_ESCAPE_SYMLINK    2u
```

`sw/include/astra/vfs_service.h`: add the open flag beside
`ASTRA_VFS_OPEN_APPEND`:

```c
/** Refuse (ASTRA_VFS_ERR_LOOP) when the final component is a symbolic link,
 * like POSIX O_NOFOLLOW. Without it a provider that resolves paths follows
 * a final link; a backend that never follows answers LOOP either way. */
#define ASTRA_VFS_OPEN_NOFOLLOW  (UINT32_C(1) << 7)
```

In `astra68.c`:
- `#include "astra68-resolve.h"`.
- `astra_host_native_open_flags` accepts `ASTRA_VFS_OPEN_NOFOLLOW` (it maps
  to nothing: following is the resolver's choice).
- Delete `astra_host_path_valid`, `astra_host_open_beneath`,
  `astra_host_open_parent_walk`, `astra_host_open_parent`,
  `astra_host_open_path`, `astra_host_parent_pair`, `astra_host_stat_path`,
  `astra_host_relative_path_valid`, `astra_host_open_relative_parent` and
  `astra_host_open_relative` (`:3020-3231`, `:3857-3973`). In their place:

```c
/*
 * A command's path, resolved beneath its session root from `base` (the
 * root, or an open directory of the session). An absolute path is only
 * meaningful from the root. An escape answers ESCAPED: result_value the
 * bytes of the path consumed, value_lo how it left.
 */
static uint32_t astra_host_resolve(uint8_t *command, int root, int base,
                                   const char *path, unsigned flags,
                                   AstraResolution *out)
{
    int status;

    out->parent = -1;
    if (base != root && path[0] == '/')
        return ASTRA_STATUS_INVALID;
    status = astra_resolve(root, base, path, flags, out);
    if (status == -EXDEV && out->escape != 0u) {
        stl_be_p(command + HOST_FIELD(result_value), out->consumed);
        stl_be_p(command + HOST_FIELD(value_lo), out->escape);
        return ASTRA_STATUS_ESCAPED;
    }
    if (status == -ESTALE)
        return ASTRA_STATUS_NOT_FOUND;
    return status == 0 ? ASTRA_STATUS_OK :
                         astra_host_status_from_errno(-status);
}
```

- Every path operation calls `astra_host_resolve` with `base = file != NULL ? file->fd : root`
  and acts on `(out.parent, out.leaf)`, closing `out.parent` on every
  exit. Per operation:

| Operation | Resolve with | Then |
|---|---|---|
| OPEN | FOLLOW_FINAL unless `flags & ASTRA_VFS_OPEN_NOFOLLOW` | leaf `""`: `O_CREAT|O_EXCL` answers EXISTS; otherwise `openat(parent, ".", native & ~(O_CREAT | O_TRUNC))`. Leaf missing with `out.trailing` and `O_CREAT`: IS_DIR. Otherwise `openat(parent, leaf, native | O_NOFOLLOW, mode)`: the final link, when allowed, was already followed, so `O_NOFOLLOW` turns a link left by NOFOLLOW into LOOP |
| STAT | FOLLOW_FINAL when `flags & ASTRA_HOST_FS_STAT_FOLLOW` (any other bit: INVALID) | `astra_host_publish_node(command, parent, leaf)`; leaf `""` publishes `parent` |
| MKDIR | no follow | leaf `""`: EXISTS; otherwise `mkdirat(parent, leaf, mode)` |
| UNLINK | no follow | leaf `""` (the path ends in `.` or `..`): INVALID; otherwise as Task 4 |
| RENAME, LINK | no follow, each operand from its own base | an escape in either operand: CROSS_DEVICE, because the operands are not both in this session. Leaf `""` in either: INVALID. Otherwise `renameat`/`linkat` |
| SYMLINK | the link path (`path2`) no follow; the target (`path`) is stored, never resolved | leaf `""`: EXISTS |
| READLINK | no follow | leaf `""`: INVALID |
| CHMOD | FOLLOW_FINAL (POSIX `chmod` follows) | `fchmodat(parent, leaf[0] ? leaf : ".", mode, 0)` |
| READDIR (path) | FOLLOW_FINAL | open the result as a directory |
| FILESYSTEM_INFO | FOLLOW_FINAL | `fstatvfs` of the result |
| SESSION_OPEN | FOLLOW_FINAL beneath the parent's root (or the volume) | an escape answers ACCESS: a derived root never leaves its parent. The result must be a directory |

- The capability register adds `ASTRA_HOST_CAP_FS_RESOLVES`.

- [ ] **Step 3: Run the resolver test and the provider test, and perturb**

`emu/qemu/test-hostfs.py`: the old refusals become resolutions. Replace the
checks at `test-hostfs.py:593-629`:
- `/data-link` opened by OPEN now follows to `/data` (OK); with
  `flags=OPEN_READ | OPEN_NOFOLLOW` (`OPEN_NOFOLLOW = 1 << 7`) it answers
  `STATUS_LOOP`;
- `/escape/sentinel` (the link to the absolute host path `outside`) answers
  `STATUS_ESCAPED` (`20`) with `get32(answer.raw, 56) == len("/escape")`
  and `get32(answer.raw, 36) == 2`;
- `/../sentinel` answers `STATUS_ESCAPED`, consumed 3, kind 1;
- `//data`, `/./data` and `/batch-0/../data` now resolve: STAT answers OK
  and names the same inode as `/data` (`NODE_INODE`);
- RENAME `/data` -> `/escape/stolen` answers `STATUS_CROSS_DEVICE` (`17`),
  and `outside/stolen` must not exist;
- add: two links `l1 -> l2`, `l2 -> l1`; STAT `/l1` with
  `flags=STAT_FOLLOW` (`1`) answers `STATUS_LOOP`; STAT `/data/` answers
  `STATUS_NOT_DIR` (`4`).

Sync, then on beast:

```sh
~/astra-mg/locked make -C emu/qemu/tests test
ASTRA_QEMU_WORK_ROOT=$HOME/.cache/astra68/qemu-9.2.4 ~/astra-mg/locked emu/qemu/build.sh host
Q=$(tail -n1 /tmp/qemu-host-build.log)
~/astra-mg/locked python3 emu/qemu/test-hostfs.py $Q
```

Expected: `ASTRA68 RESOLVE PASS`, `ASTRA QEMU HOSTFS PASS`.

Perturb, restoring and re-syncing with the `--no-times` rsync after each:
1. In `astra_resolve_walk`'s `..` handling, delete the
   `if (walk.depth == 1u) { ... -EXDEV ... }` block, so `..` at the root
   pops nothing. Expected: the case `".."` fails with status 0, want -18.
2. Replace `if (target[0] == '/')` with `if (0)`. Expected: the case
   `abs/x` fails (the walk now opens `/etc` relative to the root and
   answers ENOENT, or worse).
3. Delete `walk.anchored = is_root(&walk, base);`. Expected: the case
   `{"a/b", "../f"}` fails or `check_moved_base` fails.

- [ ] **Step 4: Measure the two strategies and decide**

On beast:

```sh
~/astra-mg/locked make -C emu/qemu/tests build/test-astra68-resolve build/aarch64/test-astra68-resolve
OUT=/mnt/Documents/astra68/builds/fs-phase1-task6-resolver-$(git rev-parse --short HEAD)
mkdir -p "$OUT"
for run in 1 2 3; do emu/qemu/tests/build/test-astra68-resolve --bench 100000; done | tee "$OUT/beast.txt"
scp emu/qemu/tests/build/aarch64/test-astra68-resolve root@192.168.1.52:/data/
ssh root@192.168.1.52 'for run in 1 2 3; do /data/test-astra68-resolve --bench 20000; done; rm -f /data/test-astra68-resolve' | tee "$OUT/de25.txt"
```

The board runs it while Astra runs, the load the provider sees.

**Decision rule.** Take the median of the three DE25 runs at depth 4. Keep
`openat2` first (`ASTRA_RESOLVE_BENEATH_FIRST 1`) only if both hold:
- `beneath` is at least 25% faster than `walk` at depth 4; and
- the difference, `walk - beneath` in nanoseconds, is at least 2% of the
  DE25 `stat-warm` median from the Task 1 record (`guest.workloads["stat-warm"].median_ns`
  in `fs-base1.json`).

Otherwise keep the walk alone. The second condition asks whether the saving
is visible to a program at all. A stat is a cross-process round trip plus a
provider command, and a saving under 2% of that is noise against the PRD's
10% budget, so it does not justify a second code path.

Then implement the choice:
- **Walk alone:** delete `astra_resolve_beneath`, its prototype, the
  `ASTRA_RESOLVE_BENEATH_FIRST` block and the bench and check calls to it.
  `astra_resolve` becomes
  `return astra_resolve_walk(root, base, path, flags, out);`.
- **openat2 first:** set `#define ASTRA_RESOLVE_BENEATH_FIRST 1` and remove
  the `#ifndef` around it.

Either way, write the measured numbers and the verdict into the comment at
the head of `astra68-resolve.c`, and into
`/mnt/Documents/astra68/builds/.../decision.txt`. Rerun the host test and
`test-hostfs.py`. Whichever was chosen, the table passes unchanged.

- [ ] **Step 5: The backend records escapes; the wire carries them**

`sw/userspace/vfs/include/astra/vfs_backend.h`: `AstraVfsBackendOps` gains,
after `stat_node`:

```c
    /** Path metadata following a final symbolic link; NULL: unsupported. */
    uint32_t (*stat_follow)(void *context, const char *path,
                            AstraVfsNodeInfo *info);
    /** After an operation answered ASTRA_VFS_ERR_ESCAPED on this thread,
     * write its AstraVfsEscape into `payload`; NULL: never escapes. */
    uint32_t (*escape)(void *context, void *payload, uint32_t capacity);
```

Both are optional. `astra_vfs_service_init` does not require them, and the
other backends (ext4, ram, metrics, events, proc) leave them NULL. Add
`NULL` initialisers where those tables use positional initialisation:
`sw/userspace/vfs/src/backends/ram.c`, `ext4.c`, `metrics/src/metrics_vfs.c`,
`events/src/event_backend.c`, `supervisor/src/proc_tree.c` and the test
fakes. Find them with `grep -rn "AstraVfsBackendOps .* = {" sw/userspace`.

`sw/include/astra/vfs_service.h`:
- `ASTRA_VFS_VERSION` -> 30. History: "Version 30 lets a provider resolve
  storage paths in order: HELLO may answer `ASTRA_VFS_FEATURE_RESOLVES_PATHS`.
  STAT and STAT_AT take `ASTRA_VFS_STAT_FOLLOW`, OPEN takes
  `ASTRA_VFS_OPEN_NOFOLLOW`, and a path that leaves its grant's root answers
  `ASTRA_VFS_ERR_ESCAPED` with an `AstraVfsEscape`."
- Add:

  ```c
  /** The service resolves ".", ".." and symbolic links itself; send paths
   * unfolded and continue an ESCAPED answer in the namespace. */
  #define ASTRA_VFS_FEATURE_RESOLVES_PATHS (UINT32_C(1) << 1)
  /** STAT flag: follow a symbolic link in the final component. */
  #define ASTRA_VFS_STAT_FOLLOW (UINT32_C(1) << 0)
  /** AstraVfsEscape::kind */
  #define ASTRA_VFS_ESCAPE_PARENT  UINT16_C(1)
  #define ASTRA_VFS_ESCAPE_SYMLINK UINT16_C(2)

  /** ASTRA_VFS_ERR_ESCAPED's payload: where the path left its grant's root. */
  typedef struct AstraVfsEscape {
      /** ASTRA_VFS_ESCAPE_PARENT: ".." above the root. ASTRA_VFS_ESCAPE_SYMLINK:
       * a link whose target is absolute or climbs above the root. */
      uint16_t kind;
      /** Bytes of `target`; zero for PARENT. */
      uint16_t target_length;
      /** Bytes of the request path through the component that left. */
      uint32_t consumed;
      /** SYMLINK: the link's stored target, unterminated. */
      uint8_t target[ASTRA_VFS_IO_MAX - 8u];
  } AstraVfsEscape;

  /** @cond ASTRA_INTERNAL */
  _Static_assert(sizeof(AstraVfsEscape) == ASTRA_VFS_IO_MAX,
                 "VFS escape record must fill the reply payload");
  /** @endcond */
  ```

`sw/userspace/vfs/src/backends/host.c` records the last escape per thread.
The backend runs in every client, on each lane's thread (Global
Constraints, the lane rule), and the runtime supports `_Thread_local`:

```c
static _Thread_local AstraVfsEscape last_escape;

/* An ESCAPED answer: remember where; for a link, read its target, the
 * namespace needs it. `request_path` is the operand the command carried. */
static uint32_t note_escape(AstraVfsHostBackend *backend,
                            const AstraHostCommand *command,
                            uint32_t directory, const char *request_path)
{
    uint32_t consumed = command->result_value;
    uint32_t kind = command->value_lo;

    memset(&last_escape, 0, sizeof(last_escape));
    if (consumed == 0u || consumed > strlen(request_path) ||
        (kind != ASTRA_HOST_FS_ESCAPE_PARENT &&
         kind != ASTRA_HOST_FS_ESCAPE_SYMLINK))
        return ASTRA_VFS_ERR_PROTOCOL;
    last_escape.kind = (uint16_t)kind;
    last_escape.consumed = consumed;
    if (kind == ASTRA_HOST_FS_ESCAPE_SYMLINK) {
        char prefix[ASTRA_HOST_FS_PATH_MAX];
        uint32_t length = 0u;
        uint32_t status;

        memcpy(prefix, request_path, consumed);
        prefix[consumed] = '\0';
        status = host_readlink_at(backend, directory, prefix,
                                  last_escape.target,
                                  sizeof(last_escape.target), &length);
        if (status != ASTRA_VFS_OK)
            return status == ASTRA_VFS_ERR_BUFFER_TOO_SMALL ?
                ASTRA_VFS_ERR_LIMIT : status;
        last_escape.target_length = (uint16_t)length;
    }
    return ASTRA_VFS_ERR_ESCAPED;
}

static uint32_t host_escape(void *context, void *payload, uint32_t capacity)
{
    (void)context;
    if (payload == NULL || capacity < sizeof(last_escape) ||
        last_escape.kind == 0u)
        return ASTRA_VFS_ERR_INVALID;
    memcpy(payload, &last_escape, sizeof(last_escape));
    return ASTRA_VFS_OK;
}
```

`host_readlink_at(backend, directory, path, buffer, capacity, &length)` is
`host_readlink` with a base handle; write it by giving `host_readlink` a
`directory` argument (0 for the path form). In every path operation of
`host.c`, when `submit` answers `ASTRA_VFS_ERR_ESCAPED`, return
`note_escape(backend, request.command, directory, path)` with the
operation's base and first path. `host_stat_follow` is `host_stat` sending
`ASTRA_HOST_FS_STAT_FOLLOW`. Add both to `host_ops` (`stat_follow`, `escape`).

`vfs_service_core.c`: `handle_open` accepts `ASTRA_VFS_OPEN_NOFOLLOW` in the
allowed flag set, so `O_NOFOLLOW` keeps working on the other services. The
core changes nothing else. Host volumes reach QEMU through the lane, and
Step 5b gives the translator the follow flag and the escape payload.

`services/hostfs/main.c`: grants answer
`ASTRA_VFS_FEATURE_GRANTS | ASTRA_VFS_FEATURE_RESOLVES_PATHS`. In
`vfs_port_transport.c`'s `pump`, `features` becomes
`grant != NULL ? ASTRA_VFS_FEATURE_GRANTS | grant_features : 0u`. Give
`AstraVfsPortGrant` a `uint32_t features;` field set by HostFS to
`ASTRA_VFS_FEATURE_RESOLVES_PATHS`, so the port layer knows nothing about
paths. `astra_vfs_host_transport_init` requires `ASTRA_HOST_CAP_FS_RESOLVES`.

- [ ] **Step 5b: The lane carries resolution and escapes**

Host volumes reach QEMU through each client's own lane (Global Constraints,
the lane rule). So following a final link and answering an escape belong in
the lane's request translator, where every host-volume request without a
fast path is answered.

`host.c`'s `last_escape` is `_Thread_local`, and every lane thread keeps
its own.

`sw/userspace/vfs/src/vfs_host_direct.c`, `astra_vfs_host_direct_transport`:
- STAT takes the follow flag:

  ```c
      case ASTRA_VFS_OP_STAT: {
          AstraVfsNodeInfo info = {0};

          if (request->flags != 0u &&
              request->flags != ASTRA_VFS_STAT_FOLLOW)
              reply->status = ASTRA_VFS_ERR_INVALID;
          else if (request->flags != 0u &&
                   (client->version < UINT16_C(30) ||
                    ops->stat_follow == NULL))
              reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
          else if (request->flags != 0u)
              reply->status = ops->stat_follow(
                  backend.context, (const char *)request->body.path, &info);
          else
              reply->status = ops->stat(
                  backend.context, (const char *)request->body.path, &info);
          if (reply->status == ASTRA_VFS_OK)
              publish_info(reply, &info);
          break;
      }
  ```

- OPEN and OPEN_AT add `ASTRA_VFS_OPEN_NOFOLLOW` to their `allowed`
  masks. So does the `vfs_client.c` open fast path (`open_mode_at`'s
  `allowed`).
- After the `switch`, before the function returns, and the same in
  `astra_vfs_host_direct_bulk` after its own:

  ```c
      /* Where the path left the grant's root: the same AstraVfsEscape a
       * service's reply would carry (Step 5). A client that cannot follow
       * it is told the name is not there. */
      if (reply->status == ASTRA_VFS_ERR_ESCAPED &&
          (client->version < UINT16_C(30) || ops->escape == NULL ||
           ops->escape(backend.context, reply->payload,
                       sizeof(reply->payload)) != ASTRA_VFS_OK))
          reply->status = ASTRA_VFS_ERR_NOT_FOUND;
  ```

A `vfs_client.c` fast path that receives ESCAPED from the backend returns
it as its status. Only `astra_vfs_lookup` (Step 6) needs the payload, and
it has no fast path: it reaches the translator through `exchange`. The
union's continuation therefore sees exactly what it would see from a
service.

- [ ] **Step 6: The Kit continues escapes; libc stops folding**

`sw/userspace/vfs/src/vfs_path.c`: an escaping variant of the normaliser,
used for bindings whose provider does not resolve (guest ext4 and the
synthetic trees in Phase 1):

```c
/*
 * astra_path_normalise, except that ".." above the start is not an error
 * but an answer: ASTRA_VFS_ERR_ESCAPED, with *consumed the bytes of `rest`
 * through that "..". The namespace above continues from there.
 */
uint32_t
astra_path_normalise_escape(const char *rest, char *out, uint32_t capacity,
                            uint32_t *consumed)
```

Its body is `astra_path_normalise`'s with one change. Where `..` finds
`length == 0u`, it sets `*consumed = end;` and returns
`ASTRA_VFS_ERR_ESCAPED` instead of `ASTRA_VFS_ERR_NOT_FOUND`. Implement
`astra_path_normalise` as a call to it that maps ESCAPED back to NOT_FOUND,
so there is one normaliser. Declare it in `ndk/include/astra/vfs_path.h`
(`grep -rn "astra_path_normalise" ndk/include`) and export it.
`test_vfs_path.c` gains:
`assert(astra_path_normalise_escape("a/../../b", out, sizeof(out), &consumed) == ASTRA_VFS_ERR_ESCAPED && consumed == 7u);`.

`sw/userspace/vfs/src/vfs_assign.c`: `astra_assign_resolve_raw` is
`astra_assign_resolve` with the normaliser replaced by a bounded verbatim
copy of `rest` (UTF-8 checked, too long: INVALID). The root prefix logic is
unchanged. Declare it in `ndk/include/astra/vfs_assign.h`.

`sw/userspace/vfs/src/vfs_client.c`:

```c
/* The features the service offered this session, connecting first when the
 * client is still lazy. */
uint32_t astra_vfs_features(AstraVfsClient *client)
{
    if (client == NULL)
        return 0u;
    if (client->session == ASTRA_VFS_SESSION_INVALID &&
        client->transport == astra_vfs_port_transport &&
        astra_vfs_port_connect_now(client) != ASTRA_VFS_OK)
        return 0u;
    return client->features;
}

/* STAT with flags (ASTRA_VFS_STAT_FOLLOW); on ESCAPED, *escape says where. */
uint32_t astra_vfs_lookup(AstraVfsClient *client, const char *path,
                          uint32_t flags, AstraVfsDirEntry *meta,
                          AstraVfsEscape *escape)
{
    AstraVfsCallState *call;
    uint32_t status;

    if (client == NULL || meta == NULL || escape == NULL ||
        (flags & ~ASTRA_VFS_STAT_FOLLOW) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    if (flags != 0u && client->version < UINT16_C(30))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    call = begin(client);
    if (!set_path(&call->request, path))
        return ASTRA_VFS_ERR_INVALID;
    call->request.flags = flags;
    status = exchange(client, call, ASTRA_VFS_OP_STAT);
    if (status == ASTRA_VFS_ERR_ESCAPED) {
        memcpy(escape, call->reply.payload, sizeof(*escape));
        return status;
    }
    if (status != ASTRA_VFS_OK)
        return status;
    meta->name[0] = '\0';
    meta->size = call->reply.node_size;
    meta->mtime = call->reply.mtime;
    meta->uid = call->reply.uid;
    meta->gid = call->reply.gid;
    meta->kind = call->reply.kind;
    meta->mode = call->reply.mode;
    meta->nlink = call->reply.nlink;
    meta->reserved = 0u;
    return ASTRA_VFS_OK;
}
```

`astra_vfs_port_connect_now` is the old `ensure_connection` of
`vfs_port_transport.c`, exported under that name. Confirm that `exchange`
returns the reply status (`astra_vfs_stat_meta` relies on it at `:563-565`).
Declare both in `ndk/include/astra/vfs_client.h` and export them.

`sw/userspace/vfs/src/vfs_union.c`:
- Add the provider decision and the raw lookup over a union's members:

  ```c
  static int binding_resolves(const AstraAssignTable *table, const char *path,
                              AstraVfsAssignClientFn client_for, void *context)
  {
      char wire[ASTRA_VFS_PATH_MAX];
      const AstraAssign *assign = NULL;
      AstraVfsClient *serving;

      if (astra_assign_resolve_raw(table, path, ASTRA_RIGHT_READ, 0u, wire,
                                   sizeof(wire), &assign) != ASTRA_VFS_OK)
          return 0;
      serving = client_for(assign, context);
      return serving != NULL &&
             (astra_vfs_features(serving) &
              ASTRA_VFS_FEATURE_RESOLVES_PATHS) != 0u;
  }

  /* The wire path for `path`: unfolded for a provider that resolves, folded
   * for one that does not. */
  uint32_t
  astra_vfs_assign_wire(const AstraAssignTable *table, const char *path,
                        uint32_t rights, uint32_t member,
                        AstraVfsAssignClientFn client_for, void *context,
                        char *wire, uint32_t capacity,
                        const AstraAssign **assign)
  {
      return binding_resolves(table, path, client_for, context) ?
          astra_assign_resolve_raw(table, path, rights, member, wire,
                                   capacity, assign) :
          astra_assign_resolve(table, path, rights, member, wire, capacity,
                               assign);
  }
  ```

  `assign_raw_lookup` is `assign_raw_lstat` (`:87-134`) with
  `astra_assign_resolve_raw` in place of `astra_assign_resolve` and
  `astra_vfs_lookup(serving, wire, follow ? ASTRA_VFS_STAT_FOLLOW : 0u, &found, escape)`
  in place of `astra_vfs_stat_meta`. On `ASTRA_VFS_ERR_ESCAPED` it returns
  at once with `*found_assign = assign`: the first member that answers
  decides where the path went.
- The continuation:

  ```c
  /*
   * Where a path that left its binding's root continues in the namespace.
   * `path` is "/name/rest" as looked up; `wire` what the binding was sent
   * ("/" root "/" rest); `escape` the provider's account.
   */
  static uint32_t continuation(const char *path, const char *wire,
                               const AstraAssign *assign,
                               const AstraVfsEscape *escape, char *out,
                               uint32_t capacity)
  {
      size_t root_length = strlen(assign->root);
      size_t prefix = 1u + root_length + (root_length != 0u ? 1u : 0u);
      size_t wire_length = strlen(wire);
      size_t name_length = 1u;
      const char *remainder;
      AstraString text;

      if (escape->consumed > wire_length || escape->consumed + 1u < prefix)
          return ASTRA_VFS_ERR_PROTOCOL;
      while (path[name_length] != '\0' && path[name_length] != '/')
          ++name_length;
      remainder = wire + escape->consumed;
      while (*remainder == '/')
          ++remainder;
      astra_string_init(&text, out, capacity);
      if (escape->kind == ASTRA_VFS_ESCAPE_PARENT) {
          (void)astra_string_append_char(&text, '/');
      } else if (escape->kind == ASTRA_VFS_ESCAPE_SYMLINK &&
                 escape->target_length != 0u &&
                 escape->target_length <= sizeof(escape->target)) {
          if (escape->target[0] != '/') {
              /* The link's directory, in namespace terms. */
              size_t link_dir = escape->consumed;

              while (link_dir > prefix && wire[link_dir - 1u] != '/')
                  --link_dir;
              (void)astra_string_append_bytes(&text, path, name_length);
              if (link_dir > prefix) {
                  (void)astra_string_append_char(&text, '/');
                  (void)astra_string_append_bytes(&text, wire + prefix,
                                                  link_dir - prefix);
              } else {
                  (void)astra_string_append_char(&text, '/');
              }
          }
          (void)astra_string_append_bytes(&text,
                                          (const char *)escape->target,
                                          escape->target_length);
          if (*remainder != '\0')
              (void)astra_string_append_char(&text, '/');
      } else {
          return ASTRA_VFS_ERR_PROTOCOL;
      }
      (void)astra_string_append(&text, remainder);
      if (text.truncated)
          return ASTRA_VFS_ERR_INVALID;
      return namespace_root_parents(out);
  }
  ```

  `namespace_root_parents` folds only `.` and `..` components that stand
  directly on the namespace root ("/../x" is "/x": the root is its own
  parent, as `/..` is in POSIX). It leaves every component after the first
  name alone, because those belong to a binding. Write it as a loop that
  strips a leading `"/."` or `"/.."` component (followed by `/` or the end)
  until none is left, keeping at least `"/"`.

- `follow_path_inner` (`:190-350`):
  - Start with the namespace form of `path`. For a provider binding that is
    `path` unchanged; for any other, `canonical_path`, which now uses
    `astra_path_normalise_escape`. If it answers ESCAPED, `current` becomes
    `"/"` plus the rest after the consumed bytes, run through
    `namespace_root_parents`.
  - At the top of the loop: if `current` is exactly `"/"`, return
    `ASTRA_VFS_ERR_ESCAPED` with `logical` set to `"/"`. The caller decides
    what the namespace root means for its operation (below).
  - Then, when `binding_resolves(table, current, client_for, context)`, call
    `assign_raw_lookup(table, current, rights, follow_final, ...)` and:
    - on OK, or on NOT_FOUND with `allow_missing_final`: return exactly as
      the existing direct lookup does (`:237-256`);
    - on ESCAPED: build `next` with `continuation`, `++hops`, and refuse
      with `ASTRA_VFS_ERR_LOOP` past `40`. `path_visit(history, next)` must
      succeed (a revisit is a loop). Copy `next` into `current` and
      `continue`;
    - on anything else: return it.

    The provider branch never reaches the per-component walk below it: the
    provider has already followed every link inside its root.
  - In the existing lexical branch, a `canonical_path` call that answers
    ESCAPED becomes the same `"/"`-plus-remainder continuation.
- `astra_vfs_assign_primary` (`:52-85`) resolves with `astra_vfs_assign_wire`.

`sw/userspace/vfs/src/filesystem_library.c`:
- `filesystem_resolve_single` (`:113-130`) resolves with
  `astra_vfs_assign_wire`.
- In `astra_filesystem_open_mode`, an `ASTRA_VFS_ERR_ESCAPED` from the
  first open falls through to the locating path, the way LOOP and NOT_DIR
  do (`:228-231`).
- The namespace root reached by an escape answers what `"/"` answers for
  each operation:
  - `filesystem_stat_common`: the root's metadata (the `strcmp(path, "/") == 0` branch);
  - `astra_filesystem_directory_open`: retry with `"/"`;
  - `astra_filesystem_open_mode`: `ASTRA_VFS_ERR_IS_DIR`;
  - `astra_filesystem_mkdir_mode`: `ASTRA_VFS_ERR_EXISTS`;
  - unlink, rename, link, symlink and chmod: `ASTRA_VFS_ERR_READ_ONLY`.

  Apply this where each calls into `vfs_union.c` and gets
  `ASTRA_VFS_ERR_ESCAPED` back.

`sw/userspace/vfs/src/process_filesystem.c:66-69`: resolve the fast path
with `astra_vfs_assign_wire(assigns, path, ASTRA_RIGHT_READ, 0u, astra_process_vfs_assign_client, NULL, wire, sizeof(wire), &assign)`.

`sw/userspace/posix/src/path.c`: the Kit resolves in order now, so libc
passes the joined path through unfolded. It keeps the folded form only for
the cwd string, until Task 9 makes cwd an object:

```c
/* Collapses runs of separators; folds nothing else. */
static int collapse(const char *input, char *out, uint32_t capacity)
{
    uint32_t length = 0u;

    for (uint32_t at = 0u; input[at] != '\0'; ++at) {
        if (input[at] == '/' && length != 0u && out[length - 1u] == '/')
            continue;
        if (length + 1u >= capacity)
            return 0;
        out[length++] = input[at];
    }
    out[length] = '\0';
    return 1;
}

int astra_posix_path_resolve(const char *cwd, const char *path,
                             char *normal, uint32_t normal_capacity,
                             char *native, uint32_t native_capacity)
{
    char input[ASTRA_VFS_PATH_MAX];

    if (cwd == NULL || path == NULL || path[0] == '\0' || normal == NULL ||
        native == NULL || cwd[0] != '/')
        return -1;
    if (!input_path(cwd, path, input, sizeof(input)) ||
        !normalise(input, normal, normal_capacity) ||
        !collapse(input, native, native_capacity))
        return -1;
    return native[1] == '\0' ? 0 : 1;
}
```

`sw/userspace/posix/src/file.c`:
- `translate_open_flags` maps `O_NOFOLLOW` to `ASTRA_VFS_OPEN_NOFOLLOW`.
- `posix_errno` maps `ASTRA_VFS_ERR_ESCAPED` to `EIO`. The Kit consumes
  every escape, so one reaching libc is a bug and must not read as
  something benign.
- `stat_path` (`:1007-1012`) treats a final `.` or `..` as a directory
  stat. That is right; leave it.

- [ ] **Step 7: Kit tests**

In `sw/userspace/vfs/tests/test_vfs_union.c` add a provider stand-in. A
second client stands for the provider, and the stub `astra_vfs_features`
reports `ASTRA_VFS_FEATURE_RESOLVES_PATHS` for it alone:

```c
static AstraVfsClient provider;
static const char *escape_path;
static AstraVfsEscape escape_answer;
static char lookups[8][ASTRA_VFS_PATH_MAX];
static uint32_t lookup_count;

uint32_t astra_vfs_features(AstraVfsClient *client)
{
    return client == &provider ?
        ASTRA_VFS_FEATURE_GRANTS | ASTRA_VFS_FEATURE_RESOLVES_PATHS : 0u;
}

uint32_t astra_vfs_lookup(AstraVfsClient *client, const char *path,
                          uint32_t flags, AstraVfsDirEntry *meta,
                          AstraVfsEscape *escape)
{
    (void)flags;
    if (lookup_count < 8u)
        (void)snprintf(lookups[lookup_count++], ASTRA_VFS_PATH_MAX, "%s",
                       path);
    if (client == &provider && escape_path != NULL &&
        strcmp(path, escape_path) == 0) {
        *escape = escape_answer;
        return ASTRA_VFS_ERR_ESCAPED;
    }
    return astra_vfs_stat_meta(client, path, meta);
}

static AstraVfsClient *client_for_provider(const AstraAssign *assign,
                                           void *context)
{
    (void)context;
    return assign->handle == 77u ? &provider : &standin;
}

/*
 * A provider binding gets its rest unfolded, and an escape continues in the
 * namespace: ".." at WORK's root leads to DH0, and an absolute link leads
 * wherever it names (docs/FILESYSTEM_PRODUCTION_PRD.md FS14).
 */
static void test_provider_escapes_continue_in_the_namespace(void)
{
    AstraAssignTable table;
    AstraVfsDirEntry entry;
    char wire[ASTRA_VFS_PATH_MAX];

    astra_assign_table_init(&table);
    assert(astra_assign_bind(&table, "WORK", 77u,
                             ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE, "") ==
           ASTRA_VFS_OK);
    assert(astra_assign_bind(&table, "DH0", 9u, ASTRA_RIGHT_READ, "") ==
           ASTRA_VFS_OK);
    reset_asked();
    lookup_count = 0u;
    answers = "/x";
    answers_also = NULL;

    /* Unfolded: the provider sees "a/../b" as written. */
    escape_path = NULL;
    (void)astra_vfs_assign_stat(&table, "/work/a/../b", ASTRA_RIGHT_READ,
                                client_for_provider, NULL, wire,
                                sizeof(wire), &entry, NULL, NULL, NULL);
    assert(strcmp(lookups[0], "/a/../b") == 0);

    /* ".." at WORK's root: continue at the namespace root, then DH0. */
    lookup_count = 0u;
    escape_path = "/../dh0/x";
    escape_answer = (AstraVfsEscape){.kind = ASTRA_VFS_ESCAPE_PARENT,
                                     .consumed = 3u};
    assert(astra_vfs_assign_stat(&table, "/work/../dh0/x", ASTRA_RIGHT_READ,
                                 client_for_provider, NULL, wire,
                                 sizeof(wire), &entry, NULL, NULL, NULL) ==
           ASTRA_VFS_OK);
    assert(strcmp(wire, "/x") == 0);

    /* An absolute link: its target, then the rest of the path. */
    lookup_count = 0u;
    escape_path = "/l/x";
    escape_answer = (AstraVfsEscape){.kind = ASTRA_VFS_ESCAPE_SYMLINK,
                                     .target_length = 4u, .consumed = 2u};
    memcpy(escape_answer.target, "/dh0", 4u);
    assert(astra_vfs_assign_stat(&table, "/work/l/x", ASTRA_RIGHT_READ,
                                 client_for_provider, NULL, wire,
                                 sizeof(wire), &entry, NULL, NULL, NULL) ==
           ASTRA_VFS_OK);
    assert(strcmp(wire, "/x") == 0);

    /* Ending on the namespace root itself is the caller's to interpret. */
    escape_path = "/..";
    escape_answer = (AstraVfsEscape){.kind = ASTRA_VFS_ESCAPE_PARENT,
                                     .consumed = 3u};
    assert(astra_vfs_assign_stat(&table, "/work/..", ASTRA_RIGHT_READ,
                                 client_for_provider, NULL, wire,
                                 sizeof(wire), &entry, NULL, NULL, NULL) ==
           ASTRA_VFS_ERR_ESCAPED);

    /* A link that names itself through the namespace is a loop, bounded. */
    escape_path = "/l/x";
    escape_answer = (AstraVfsEscape){.kind = ASTRA_VFS_ESCAPE_SYMLINK,
                                     .target_length = 9u, .consumed = 2u};
    memcpy(escape_answer.target, "/work/l/x", 9u);
    assert(astra_vfs_assign_stat(&table, "/work/l/x", ASTRA_RIGHT_READ,
                                 client_for_provider, NULL, wire,
                                 sizeof(wire), &entry, NULL, NULL, NULL) ==
           ASTRA_VFS_ERR_LOOP);
    escape_path = NULL;
    astra_assign_table_destroy(&table);
}
```

`answers = "/x"` makes the stand-in answer for DH0's wire path `/x`. Note
the first case's expectation: the provider binding's lookup goes through
`astra_vfs_lookup`, and the stand-in delegates to `astra_vfs_stat_meta`,
which records it too. Read `lookups[]`, not `stat_asked[]`. Register the
test. The existing `test_links_follow_without_crossing_authority`
`"../secret"` case still answers NOT_FOUND: its escape continues at
`/secret`, which no binding serves.

The escape payload is the lane translator's (Step 5b). Its tests are the
guest cases below, which reach it through `astra_vfs_lookup`, and
Perturbation 4.

- [ ] **Step 8: The guest semantic cases**

Add to `fssem.c` (register each, and add their names to `CASES`):

```c
/* A link inside the volume is followed by the provider; lstat sees it. */
static int case_symlink_in_volume(const char *directory)
{
    char d[160];
    char link[160];
    char through[160];
    char text[16];
    struct stat st;
    ssize_t length;

    join(d, sizeof(d), directory, "d");
    join(link, sizeof(link), directory, "l");
    join(through, sizeof(through), directory, "l/f");
    CHECK(mkdir(d, 0755) == 0, "mkdir: %s", strerror(errno));
    CHECK(symlink("d", link) == 0, "symlink: %s", strerror(errno));
    CHECK(write_text(through, "via", O_WRONLY | O_CREAT | O_EXCL) == 0,
          "create through the link: %s", strerror(errno));
    join(through, sizeof(through), directory, "d/f");
    CHECK(read_text(through, text, sizeof(text)) == 3 &&
          strcmp(text, "via") == 0, "the file is not in d");
    CHECK(lstat(link, &st) == 0 && S_ISLNK(st.st_mode), "lstat: not a link");
    CHECK(stat(link, &st) == 0 && S_ISDIR(st.st_mode), "stat: not followed");
    length = readlink(link, text, sizeof(text));
    CHECK(length == 1 && text[0] == 'd', "readlink");
    return 0;
}

/* A link to another volume's path is followed through the namespace. */
static int case_symlink_cross_volume(const char *directory)
{
    char link[160];
    char missing[160];
    struct stat st;
    struct stat system;

    join(link, sizeof(link), directory, "to-system");
    join(missing, sizeof(missing), directory, "to-nowhere");
    CHECK(stat("/system/startup", &system) == 0, "no /system/startup");
    CHECK(symlink("/system/startup", link) == 0, "symlink: %s",
          strerror(errno));
    CHECK(stat(link, &st) == 0 && S_ISDIR(st.st_mode),
          "cross-volume link not followed: %s", strerror(errno));
    CHECK(st.st_mtime == system.st_mtime, "followed somewhere else");
    CHECK(symlink("/no-such-binding/x", missing) == 0, "symlink");
    CHECK(stat(missing, &st) != 0 && errno == ENOENT,
          "a link to an unbound name resolved");
    return 0;
}

/* ".." at a binding's root is the namespace root, in order, not folded. */
static int case_dotdot_at_root(const char *directory)
{
    char path[200];
    struct stat st;
    struct stat via;

    CHECK(stat("/work/..", &st) == 0 && S_ISDIR(st.st_mode),
          "/work/..: %s", strerror(errno));
    CHECK(stat("/system", &st) == 0, "/system");
    CHECK(stat("/work/../system", &via) == 0 && S_ISDIR(via.st_mode),
          "/work/../system: %s", strerror(errno));
    CHECK(via.st_mtime == st.st_mtime, "/work/../system is not /system");
    (void)snprintf(path, sizeof(path), "%s/../%s", directory,
                   strrchr(directory, '/') + 1);
    CHECK(stat(path, &st) == 0 && S_ISDIR(st.st_mode), "%s: %s", path,
          strerror(errno));
    return 0;
}

/* A relative link that climbs out of the volume continues in the namespace. */
static int case_escape_relative(const char *directory)
{
    char link[160];
    char through[200];
    struct stat st;

    join(link, sizeof(link), directory, "up");
    CHECK(symlink("../../..", link) == 0, "symlink: %s", strerror(errno));
    (void)snprintf(through, sizeof(through), "%s/up/system/startup",
                   directory);
    CHECK(stat(through, &st) == 0 && S_ISDIR(st.st_mode),
          "%s: %s", through, strerror(errno));
    return 0;
}

/* Loops, trailing slashes and O_NOFOLLOW mean what POSIX says. */
static int case_link_edges(const char *directory)
{
    char a[160];
    char b[160];
    char file[160];
    char slashed[170];
    char link[160];
    struct stat st;
    int fd;

    join(a, sizeof(a), directory, "a");
    join(b, sizeof(b), directory, "b");
    CHECK(symlink("b", a) == 0 && symlink("a", b) == 0, "symlinks");
    CHECK(stat(a, &st) != 0 && errno == ELOOP, "loop: %s", strerror(errno));
    join(file, sizeof(file), directory, "file");
    CHECK(write_text(file, "x", O_WRONLY | O_CREAT | O_EXCL) == 0, "file");
    (void)snprintf(slashed, sizeof(slashed), "%s/", file);
    CHECK(stat(slashed, &st) != 0 && errno == ENOTDIR,
          "file/: %s", strerror(errno));
    join(link, sizeof(link), directory, "to-file");
    CHECK(symlink("file", link) == 0, "symlink");
    fd = open(link, O_RDONLY | O_NOFOLLOW);
    CHECK(fd < 0 && errno == ELOOP, "O_NOFOLLOW opened a link");
    fd = open(link, O_RDONLY);
    CHECK(fd >= 0, "open through a link: %s", strerror(errno));
    (void)close(fd);
    return 0;
}
```

Register `symlink-in-volume`, `symlink-cross-volume`, `dotdot-at-root`,
`escape-relative` and `link-edges`. In `case_raw_subroot`, the
`/../outside` stat now answers `ASTRA_VFS_ERR_ESCAPED` rather than an
error. Assert exactly that, and that the escape record's `kind` is
`ASTRA_VFS_ESCAPE_PARENT`. The `../sound` derive still fails, because a
derived root never leaves its parent.

`emu/qemu/fs-gates.sh`: add the resolver host test before the QEMU gates:

```sh
make -C emu/qemu/tests test > /tmp/mg-fs-resolve.log 2>&1
echo "EXIT=$?" >> /tmp/mg-fs-resolve.log
```

and `fs-resolve` to its summary loop.

- [ ] **Step 9: Run, perturb, full suite, commit**

Sync, then on beast build userspace, test it, build `sw/boot`, build QEMU,
rebuild images, and run `test-hostfs.py` and `test-fs-semantics.py`.
Expected: every new FSSEM case PASS and the gate PASS.

Perturb (restore and re-sync after each):
1. In `binding_resolves`, return 0. Expected: the union test fails on
   `lookups[0]` (the rest arrives folded as `/b`), and `dotdot-at-root`
   still passes while `escape-relative` fails. The Kit's old lexical path
   cannot follow a link out of a volume.
2. In `continuation`, drop the remainder (`astra_string_append(&text, remainder)`).
   Expected: the union test fails on `wire == "/x"`, and
   `symlink-cross-volume` fails.
3. In `path.c`, pass `normal` instead of `input` to `collapse`. Expected:
   `escape-relative` fails, because libc has folded `up/..` away before the
   provider saw the link.
4. In the lane translator (Step 5b), delete the ESCAPED payload block.
   Expected: `symlink-cross-volume` and `escape-relative` fail: the Kit's
   lookup gets NOT_FOUND where it needed a continuation.

`~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-task6.log`. Expected: green,
including the terminal gate's zsh paths, which use `..` in `cd`. Run one
`--bench t6` and compare it with Task 3b's `qemu-t3b-1.json`.

```bash
git add emu/qemu/qemu-9.2 emu/qemu/tests sw/include/astra sw/userspace/vfs \
  ndk/include/astra sw/userspace/posix/src/path.c sw/userspace/posix/src/file.c \
  sw/userspace/services/hostfs/main.c sw/userspace/metrics sw/userspace/events \
  sw/userspace/supervisor/src/proc_tree.c sw/userspace/commands/fssem/fssem.c \
  emu/qemu/test-hostfs.py emu/qemu/test-fs-semantics.py emu/qemu/fs-gates.sh
git commit -m "feat(fs): ordered resolution beneath a grant root, escapes continue in the namespace (FS14); storage protocol 30"
```

---

### Task 7: Stable identity and honest metadata (D4, first half)

The wire has carried no `st_dev` or `st_ino` (`host.h:446-455` before
Task 4; `AstraVfsReply` today), so POSIX invents them. Task 4 already gets a
`statx` node from the provider. This task carries it to clients.

- **Volume.** A volume identity is assigned by HostFS, one per Linux
  filesystem the provider reports (by device number), stable for the
  provider's generation. Bind mounts of one filesystem share a device
  number, so every binding and alias of one filesystem shares the identity
  (FS05).
- **Node.** The inode number, plus a generation, which is the birth time
  where Linux knows it, so a reused inode number is told apart.
- **Validity.** Every metadata field gets a validity bit. A real mode 0 is
  then reported as 0 (FS22).
- **POSIX.** `st_dev` and `st_ino` come from these fields.

**Files:**
- Modify: `sw/include/astra/vfs_service.h` (version 31, `AstraVfsNodeMeta`, `ASTRA_VFS_FEATURE_NODE_META`)
- Modify: `sw/userspace/vfs/include/astra/vfs_backend.h` (`AstraVfsNodeInfo` grows)
- Modify: `sw/userspace/vfs/src/vfs_service_core.c`, `sw/userspace/vfs/src/backends/host.c`, `sw/userspace/vfs/include/astra/vfs_host_transport.h`, `sw/userspace/vfs/src/vfs_host_transport.c`
- Modify: `ndk/include/astra/vfs_client.h`, `ndk/include/astra/vfs_union.h`, `ndk/include/astra/filesystem_library.h`, `sw/userspace/vfs/src/vfs_client.c`, `sw/userspace/vfs/src/vfs_union.c`, `sw/userspace/vfs/src/filesystem_library.c`, `sw/userspace/supervisor/src/loader.c`, `sw/userspace/vfs/filesystem.library.exports`
- Modify: `sw/userspace/posix/src/file.c` (`fill`)
- Modify: Kit major 8 (Task 3 Step 7's list)
- Modify: `sw/userspace/commands/fssem/fssem.c`, `emu/qemu/test-fs-semantics.py`
- Test: `sw/userspace/vfs/tests/test_vfs_host_backend.c`, `sw/userspace/vfs/tests/test_vfs_service_core.c`, `sw/userspace/vfs/tests/test_filesystem_library.c`

**Interfaces:**
- Produces (wire 31): `AstraVfsNodeMeta` (72 bytes) in the payload of OPEN,
  OPEN_AT, STAT, STAT_AT and STAT_FILE replies when `valid != 0`; the
  `ASTRA_VFS_META_*` bits; `ASTRA_VFS_FEATURE_NODE_META (1u << 2)`.
- Produces (Kit 8): `astra_vfs_stat_meta`, `astra_vfs_stat_at_meta`,
  `astra_vfs_stat_file_meta`, `astra_vfs_lookup`, `astra_vfs_assign_stat`
  and `astra_vfs_assign_lstat` take `AstraVfsNodeMeta *meta` (NULL:
  unwanted) after the entry. `AstraFileInfo` gains `meta_valid`, `volume`,
  `inode`, `generation`, `blocks`, `block_size`, `nlink_full`, `atime`,
  `ctime`, `atime_nsec`, `mtime_nsec`, `ctime_nsec`, `reserved`, filled when
  `info->size` covers them.
- Consumed by: Task 8 (shared descriptions keep identity), Task 9 (cwd
  identity), Task 13 (`ls -i` evidence).

- [ ] **Step 1: Failing tests**

`test_vfs_host_backend.c`: the STAT_AT answer from Task 4 also sets
`command->node.valid |= ASTRA_HOST_NODE_INODE | ASTRA_HOST_NODE_DEVICE | ASTRA_HOST_NODE_BTIME`,
`command->node.inode_lo = 4242u`, `device_hi = 8u`, `device_lo = 1u`,
`btime.seconds_lo = 7u`, `btime.nanoseconds = 5u`. Assert:

```c
    assert(info.valid & ASTRA_VFS_META_INODE);
    assert(info.inode == 4242u);
    assert(info.volume == 0x00800001u);     /* major 8, minor 1 */
    assert(info.generation == UINT64_C(7000000005));
```

A STAT answer with another device (`device_lo = 2u`) gives
`volume == 0x00800002u`. The first device again gives `0x00800001u`
(Step 3: the identity is computed from the device, the same in every
process).

`test_vfs_service_core.c`: the fake's `stat` fills
`info->valid = ASTRA_VFS_META_INODE | ASTRA_VFS_META_VOLUME | ASTRA_VFS_META_MODE`,
`inode = 99`, `volume = 3`, `mode = 0`. A version-31 STAT reply carries an
`AstraVfsNodeMeta` with those values and `ASTRA_VFS_META_MODE` set (a mode
of 0 that is real). A version-30 reply's payload is all zero.

Run: `~/astra-mg/locked make -C sw/userspace/vfs test`. Expected: compile
errors.

- [ ] **Step 2: The wire and the backend record**

`vfs_service.h`: version 31 ("Version 31 carries node identity, an
`AstraVfsNodeMeta`, in the payload of OPEN, OPEN_AT, STAT, STAT_AT and
STAT_FILE replies"), and:

```c
/** HELLO feature: metadata replies carry an AstraVfsNodeMeta. */
#define ASTRA_VFS_FEATURE_NODE_META (UINT32_C(1) << 2)

/* AstraVfsNodeMeta::valid -- a field whose bit is clear is unknown. */
#define ASTRA_VFS_META_VOLUME     (UINT32_C(1) << 0)
#define ASTRA_VFS_META_INODE      (UINT32_C(1) << 1)
#define ASTRA_VFS_META_GENERATION (UINT32_C(1) << 2)
#define ASTRA_VFS_META_BLOCKS     (UINT32_C(1) << 3)
#define ASTRA_VFS_META_ATIME      (UINT32_C(1) << 4)
#define ASTRA_VFS_META_MTIME      (UINT32_C(1) << 5)
#define ASTRA_VFS_META_CTIME      (UINT32_C(1) << 6)
#define ASTRA_VFS_META_MODE       (UINT32_C(1) << 7)
#define ASTRA_VFS_META_NLINK      (UINT32_C(1) << 8)
#define ASTRA_VFS_META_OWNER      (UINT32_C(1) << 9)

/** Node identity and the metadata the fixed reply fields cannot hold
 * (docs/FILESYSTEM_PRODUCTION_PRD.md FS05, FS22). */
typedef struct AstraVfsNodeMeta {
    uint32_t valid;         /**< ASTRA_VFS_META_* bits. */
    /** One per mounted filesystem, shared by every binding of it. */
    uint32_t volume;
    uint64_t inode;         /**< The filesystem's node number. */
    /** Tells a reused node number apart: its birth time, in ns. */
    uint64_t generation;
    uint64_t blocks;        /**< 512-byte blocks allocated. */
    uint32_t block_size;    /**< Preferred I/O size. */
    uint32_t nlink;         /**< Hard links, full width. */
    int64_t atime;          /**< Last access, seconds. */
    uint32_t atime_nsec;
    uint32_t mtime_nsec;    /**< Nanoseconds of the reply's mtime. */
    int64_t ctime;          /**< Last status change, seconds. */
    uint32_t ctime_nsec;
    uint32_t reserved;      /**< Zero. */
} AstraVfsNodeMeta;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraVfsNodeMeta) == 72u, "VFS node meta ABI changed");
_Static_assert(sizeof(AstraVfsNodeMeta) <= ASTRA_VFS_IO_MAX,
               "VFS node meta must fit a reply payload");
/** @endcond */
```

`vfs_backend.h`: append to `AstraVfsNodeInfo`, after `reserved`:

```c
    /* Identity and the rest, with ASTRA_VFS_META_* validity; a backend that
     * knows none of it leaves `valid` zero. */
    uint32_t valid;
    uint32_t volume;
    uint64_t inode;
    uint64_t generation;
    uint64_t blocks;
    uint32_t block_size;
    uint32_t nlink_full;
    int64_t atime;
    uint32_t atime_nsec;
    uint32_t mtime_nsec;
    int64_t ctime;
    uint32_t ctime_nsec;
```

`node_info_clear` (`vfs_service_core.c:197-208`) becomes
`memset(info, 0, sizeof(*info));`. `node_info_publish` gains the slot's
version and the reply:

```c
static void
node_info_publish(AstraVfsReply *reply, const AstraVfsNodeInfo *info,
                  uint16_t version)
{
    reply->node_size = info->size;
    reply->mtime = info->mtime;
    reply->uid = info->uid;
    reply->gid = info->gid;
    reply->kind = info->kind;
    reply->mode = info->mode;
    reply->nlink = info->nlink;
    if (version >= UINT16_C(31) && info->valid != 0u) {
        AstraVfsNodeMeta meta;

        memset(&meta, 0, sizeof(meta));
        meta.valid = info->valid;
        meta.volume = info->volume;
        meta.inode = info->inode;
        meta.generation = info->generation;
        meta.blocks = info->blocks;
        meta.block_size = info->block_size;
        meta.nlink = info->nlink_full;
        meta.atime = info->atime;
        meta.atime_nsec = info->atime_nsec;
        meta.mtime_nsec = info->mtime_nsec;
        meta.ctime = info->ctime;
        meta.ctime_nsec = info->ctime_nsec;
        memcpy(reply->payload, &meta, sizeof(meta));
    }
}
```

Pass the slot's version at each call site (OPEN, OPEN_AT, STAT, STAT_AT,
STAT_FILE). HostFS grants add `ASTRA_VFS_FEATURE_NODE_META` to their
`features`.

- [ ] **Step 3: HostFS fills identity**

Every client runs this backend itself (Global Constraints, the lane rule).
A volume map kept per connection would number the filesystems differently
in each process, and one file would show two `st_dev` values, which FS05
forbids. So the identity is computed from the device number, with no table:

```c
/*
 * The Astra volume identity of the Linux filesystem on major:minor: the
 * same in every process and across provider restarts, with no table to
 * share. Zero (unknown) outside the classic 12-bit major, 20-bit minor
 * that every local Linux filesystem fits; ext4 and the synthetic trees
 * report no identity at all (Step 4), so nothing else uses these numbers.
 */
static uint32_t volume_identity(uint32_t major, uint32_t minor)
{
    if (major >= (UINT32_C(1) << 12) || minor >= (UINT32_C(1) << 20))
        return 0u;
    return (major << 20) | minor;
}
```

`publish_info` takes the backend and fills the rest from the node:

```c
    const AstraHostNode *node = &command->node;
    uint32_t valid = node->valid;
    ...
    if ((valid & ASTRA_HOST_NODE_DEVICE) != 0u) {
        info->volume = volume_identity(node->device_hi, node->device_lo);
        if (info->volume != 0u)
            info->valid |= ASTRA_VFS_META_VOLUME;
    }
    if ((valid & ASTRA_HOST_NODE_INODE) != 0u) {
        info->inode = ((uint64_t)node->inode_hi << 32) | node->inode_lo;
        info->valid |= ASTRA_VFS_META_INODE;
    }
    if ((valid & ASTRA_HOST_NODE_BTIME) != 0u) {
        uint64_t seconds = ((uint64_t)(uint32_t)node->btime.seconds_hi << 32) |
                           node->btime.seconds_lo;

        info->generation = seconds * UINT64_C(1000000000) +
                           node->btime.nanoseconds;
        info->valid |= ASTRA_VFS_META_GENERATION;
    }
    if ((valid & ASTRA_HOST_NODE_BLOCKS) != 0u) {
        info->blocks = ((uint64_t)node->blocks_hi << 32) | node->blocks_lo;
        info->block_size = node->block_size;
        info->valid |= ASTRA_VFS_META_BLOCKS;
    }
    if ((valid & ASTRA_HOST_NODE_ATIME) != 0u) {
        info->atime = (int64_t)(((uint64_t)(uint32_t)node->atime.seconds_hi << 32) |
                                node->atime.seconds_lo);
        info->atime_nsec = node->atime.nanoseconds;
        info->valid |= ASTRA_VFS_META_ATIME;
    }
    if ((valid & ASTRA_HOST_NODE_MTIME) != 0u) {
        info->mtime_nsec = node->mtime.nanoseconds;
        info->valid |= ASTRA_VFS_META_MTIME;
    }
    if ((valid & ASTRA_HOST_NODE_CTIME) != 0u) {
        info->ctime = (int64_t)(((uint64_t)(uint32_t)node->ctime.seconds_hi << 32) |
                                node->ctime.seconds_lo);
        info->ctime_nsec = node->ctime.nanoseconds;
        info->valid |= ASTRA_VFS_META_CTIME;
    }
    if ((valid & ASTRA_HOST_NODE_MODE) != 0u)
        info->valid |= ASTRA_VFS_META_MODE;
    if ((valid & ASTRA_HOST_NODE_NLINK) != 0u) {
        info->nlink_full = node->nlink;
        info->valid |= ASTRA_VFS_META_NLINK;
    }
    if ((valid & (ASTRA_HOST_NODE_UID | ASTRA_HOST_NODE_GID)) ==
        (ASTRA_HOST_NODE_UID | ASTRA_HOST_NODE_GID))
        info->valid |= ASTRA_VFS_META_OWNER;
```

READDIR's `publish_info` call stays; its entries do not carry identity on
the wire. Directory entries with inode numbers are Phase 2's (the PRD's
"Metadata and tools" family: `ls -i`, recursive cycle detection).

- [ ] **Step 3b: The lane carries identity**

Every client runs the host backend itself (Global Constraints, the lane
rule), so the reply that carries an `AstraVfsNodeMeta` is built by the
lane's translator as well as by the service core. Write the record once, in
`vfs_backend.h` after `AstraVfsNodeInfo` (add `#include <string.h>` if the
header lacks it):

```c
/* The reply payload's identity record for `info` (storage protocol 31),
 * built the same way by the service core and the lane's translator. */
static inline void
astra_vfs_node_meta_from_info(const AstraVfsNodeInfo *info,
                              AstraVfsNodeMeta *meta)
{
    memset(meta, 0, sizeof(*meta));
    meta->valid = info->valid;
    meta->volume = info->volume;
    meta->inode = info->inode;
    meta->generation = info->generation;
    meta->blocks = info->blocks;
    meta->block_size = info->block_size;
    meta->nlink = info->nlink_full;
    meta->atime = info->atime;
    meta->atime_nsec = info->atime_nsec;
    meta->mtime_nsec = info->mtime_nsec;
    meta->ctime = info->ctime;
    meta->ctime_nsec = info->ctime_nsec;
}
```

Step 2's `node_info_publish` uses it in place of its inline copy.
`vfs_host_direct.c`'s `publish_info(reply, info)` gains
`uint16_t version`, and every call passes `client->version`. After its
fixed fields:

```c
    if (version >= UINT16_C(31) && info->valid != 0u) {
        AstraVfsNodeMeta meta;

        astra_vfs_node_meta_from_info(info, &meta);
        memcpy(reply->payload, &meta, sizeof(meta));
    }
```

In `vfs_client.c` (Step 4), a function that takes `AstraVfsNodeMeta *meta`
and has a lane fast path fills it from the backend's `info`. That covers
`astra_vfs_stat_meta`, `astra_vfs_stat_at_meta` and
`astra_vfs_stat_file_meta`. After the backend call:

```c
        if (meta != NULL) {
            if (status == ASTRA_VFS_OK && client->version >= UINT16_C(31))
                astra_vfs_node_meta_from_info(&info, meta);
            else
                memset(meta, 0, sizeof(*meta));
        }
```

Step 4's payload copy covers the exchange path.

- [ ] **Step 4: The Kit and libc**

- `vfs_client.c`: `astra_vfs_stat_meta`, `astra_vfs_stat_at_meta`,
  `astra_vfs_stat_file_meta` and `astra_vfs_lookup` take
  `AstraVfsNodeMeta *meta` after the entry. When `meta != NULL`, zero it,
  and when `client->version >= 31` copy `call->reply.payload` into it.
  Update the six declarations, and every caller (`grep -rn "astra_vfs_stat_meta\|astra_vfs_stat_at_meta\|astra_vfs_stat_file_meta\|astra_vfs_lookup" sw ndk | grep -v /build/`):
  callers that do not need identity pass `NULL`.
- `vfs_union.c`: `astra_vfs_assign_stat` and `astra_vfs_assign_lstat` take
  `AstraVfsNodeMeta *meta` after `entry`, and pass it down to the lookup that
  finds the node (`assign_raw_lookup`, or `assign_raw_lstat`'s
  `astra_vfs_stat_meta`). `filesystem_locate` and `filesystem_locate_literal`
  pass a meta through.
- `filesystem_library.h`: append to `AstraFileInfo`:

  ```c
      /* Filesystem Kit 8: identity and the rest of the metadata, filled
       * when `size` covers them. `meta_valid` holds ASTRA_VFS_META_* bits:
       * a field whose bit is clear is unknown, never zero-by-default. */
      uint32_t meta_valid;
      uint32_t volume;     /**< Shared by every binding of one filesystem. */
      uint64_t inode;
      uint64_t generation;
      uint64_t blocks;
      uint32_t block_size;
      uint32_t nlink_full;
      int64_t atime;
      int64_t ctime;
      uint32_t atime_nsec;
      uint32_t mtime_nsec;
      uint32_t ctime_nsec;
      uint32_t reserved;
  ```

  `filesystem_stat_common`, `astra_filesystem_stat_at` and
  `astra_filesystem_file_info` copy them from the meta when
  `info->size >= sizeof(AstraFileInfo)`.
- `sw/userspace/posix/src/file.c` `fill` (`:960-979`):

  ```c
  static void
  fill(struct stat *out, const AstraFileInfo *info)
  {
      int directory = info->kind == ASTRA_VFS_KIND_DIRECTORY;
      int symlink = info->kind == ASTRA_VFS_KIND_SYMLINK;
      int mode_known = (info->meta_valid & ASTRA_VFS_META_MODE) != 0u;

      (void)memset(out, 0, sizeof(*out));
      out->st_mode = (mode_t)((directory ? S_IFDIR : symlink ? S_IFLNK :
                               S_IFREG) |
                              (mode_known ? (info->mode & 07777u) :
                               info->mode != 0u ? info->mode :
                               (directory ? 0755u : symlink ? 0777u : 0644u)));
      out->st_size = (off_t)info->byte_size;
      out->st_nlink = (nlink_t)((info->meta_valid & ASTRA_VFS_META_NLINK) != 0u ?
                                info->nlink_full :
                                info->nlink != 0u ? info->nlink : 1u);
      out->st_uid = (uid_t)info->uid;
      out->st_gid = (gid_t)info->gid;
      out->st_dev = (dev_t)info->volume;
      out->st_ino = (ino_t)info->inode;
      out->st_mtim.tv_sec = (time_t)info->mtime;
      out->st_mtim.tv_nsec = (long)info->mtime_nsec;
      if ((info->meta_valid & ASTRA_VFS_META_ATIME) != 0u) {
          out->st_atim.tv_sec = (time_t)info->atime;
          out->st_atim.tv_nsec = (long)info->atime_nsec;
      } else {
          out->st_atim = out->st_mtim;
      }
      if ((info->meta_valid & ASTRA_VFS_META_CTIME) != 0u) {
          out->st_ctim.tv_sec = (time_t)info->ctime;
          out->st_ctim.tv_nsec = (long)info->ctime_nsec;
      } else {
          out->st_ctim = out->st_mtim;
      }
      if ((info->meta_valid & ASTRA_VFS_META_BLOCKS) != 0u) {
          out->st_blksize = (blksize_t)info->block_size;
          out->st_blocks = (blkcnt_t)info->blocks;
      } else {
          out->st_blksize = 512;
          out->st_blocks = (blkcnt_t)((info->byte_size + 511u) / 512u);
      }
  }
  ```

  A filesystem that reports no mode keeps the old defaults. That is the
  guest ext4 and synthetic trees until Phase 2, and libc does not invent
  `st_dev` or `st_ino` for them: they stay 0, honestly unknown.
- Kit major 8: Task 3 Step 7's edit with `8`.

- [ ] **Step 5: The guest case**

```c
/* One node through two bindings is one identity; a link shares it; mode 0
 * is real (docs/FILESYSTEM_PRODUCTION_PRD.md FS05, FS17, FS22). */
static int case_alias_identity(const char *directory)
{
    char work[160];
    char cwd[200];
    char hard[160];
    char other[160];
    char moved[160];
    struct stat a;
    struct stat b;

    join(work, sizeof(work), directory, "f");
    (void)snprintf(cwd, sizeof(cwd), "/cwd%s/f",
                   directory + strlen("/work"));
    join(hard, sizeof(hard), directory, "h");
    join(other, sizeof(other), directory, "g");
    join(moved, sizeof(moved), directory, "moved");
    CHECK(write_text(work, "id", O_WRONLY | O_CREAT | O_EXCL) == 0, "f");
    CHECK(write_text(other, "id", O_WRONLY | O_CREAT | O_EXCL) == 0, "g");
    CHECK(stat(work, &a) == 0 && stat(cwd, &b) == 0, "stat: %s",
          strerror(errno));
    CHECK(a.st_ino != 0u && a.st_dev != 0u, "identity missing");
    CHECK(a.st_dev == b.st_dev && a.st_ino == b.st_ino,
          "/work and /cwd disagree about one file");
    CHECK(stat(other, &b) == 0 && b.st_ino != a.st_ino, "two files, one inode");
    CHECK(link(work, hard) == 0, "link: %s", strerror(errno));
    CHECK(stat(hard, &b) == 0 && b.st_ino == a.st_ino && b.st_nlink == 2,
          "the link is another node");
    (void)snprintf(cwd, sizeof(cwd), "/cwd%s/h",
                   directory + strlen("/work"));
    CHECK(rename(cwd, moved) == 0, "rename across bindings: %s",
          strerror(errno));
    CHECK(stat(moved, &b) == 0 && b.st_ino == a.st_ino,
          "rename changed identity");
    CHECK(chmod(work, 0) == 0 && stat(work, &b) == 0 &&
          (b.st_mode & 07777) == 0, "mode 0 read back as %o",
          (unsigned)(b.st_mode & 07777));
    CHECK(chmod(work, 0644) == 0, "chmod back");
    return 0;
}
```

Register it as `alias-identity`, and add it to `CASES`. `/cwd` is the CWD
grant Terminal passes to every command, the WORK grant itself
(`console_session.c:179-197`). The rename crosses two names of one grant
and must work. Two *different* grants of one volume are left for Phase 2
(see Self-Review, gaps).

In `case_raw_subroot`, compare identity across grants. The STAT of
`/inside` through the derived grant and the STAT of the same file through
WORK both carry an `AstraVfsNodeMeta` in `reply.reply.payload`. Assert
that their `volume` and `inode` are equal and nonzero.

- [ ] **Step 6: Run, perturb, full suite, commit**

Sync, build and test userspace, build `sw/boot`, rebuild images, run
`test-fs-semantics.py`. Expected: `FSSEM PASS alias-identity` and every
earlier case still passes.

Perturb (restore and re-sync each time):
1. In `publish_info`, stop setting `ASTRA_VFS_META_INODE`. Expected:
   `alias-identity` fails "identity missing".
2. In `volume_identity`, return `transport->volume_count + 1u` without
   storing, a new identity each call. Expected: the backend test fails on
   the repeated device, and `raw-subroot` fails on volume equality.
3. In `fill`, drop `mode_known` (always the defaults). Expected:
   `alias-identity` fails "mode 0 read back as 644".

`~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-task7.log`. Expected: green.
The terminal gate's `ls -l` output must not change for ext4 files.

```bash
git add sw/include/astra/vfs_service.h sw/userspace/vfs ndk sw/userspace/posix/src/file.c \
  sw/userspace/services/hostfs/main.c sw/userspace/supervisor/src/loader.c \
  sw/userspace/commands/fssem/fssem.c emu/qemu/test-fs-semantics.py \
  sw/userspace/posix/Makefile sw/userspace/libraries.mk sw/userspace/kits \
  sw/userspace/config/Makefile sw/userspace/tests/test_boundaries.py \
  sw/userspace/lua/Makefile sw/userspace/apps/Terminal.app/manifest \
  sw/userspace/apps/InterfaceGallery.app/manifest \
  tools/tests/test_check_bundle_dependencies.py docs/FILESYSTEM_KIT.md docs/ABI.md
git commit -m "feat(fs): node identity and validity on the wire; st_dev/st_ino from the provider (FS05, FS22); storage protocol 31, Kit 8"
```

---

### Task 8: Shared open descriptions (D5)

The file position moves out of the client (`AstraFile._private_offset`,
`filesystem_library.c:474-546`) into the Linux open file description the
provider holds. Sequential I/O on a description advances one position
atomically, and positioned I/O leaves it alone. An append is atomic per
write with `RWF_APPEND`, and positioned writes on an append description
honor their offset, as POSIX requires and Linux's `O_APPEND` does not.
QEMU therefore opens without `O_APPEND` (this fixes the per-write append
flag QEMU ignored, `astra68.c:4167-4170`).

Fork shares a description the way Haiku's `vfs_new_io_context`
(`vfs.cpp:5026-5080`) inherits `file_descriptor`s. With the lane the
description lives in QEMU, keyed by the process that opened it, so the
share does too.

Before the clone, the parent asks the provider, on its own lane, to *share*
each open file. QEMU duplicates the Linux descriptor (`fcntl(F_DUPFD_CLOEXEC)`:
the same open file description, so the same position). It holds the
duplicate under a random 64-bit token, bound to the handle's session and
charged to the parent.

The child inherits the parent's grant scope (Task 3b) and opens its own lane
on it. It *adopts* the token there and gets a handle of its own. QEMU allows
an adoption only through a channel that carries the share's session, so a
process holding another grant cannot take it, even with the token.

The token lives in memory both processes have, so no pathname is ever
reopened. The parent withdraws unclaimed tokens when it reaps the child, and
a share dies with its sharer. Exec needs nothing new (Decisions, D5): it
happens in place and already honors `FD_CLOEXEC`.

**Files:**
- Modify: `sw/include/astra/host.h` (`SEEK`, `SHARE`, `ADOPT`, `ASTRA_HOST_FS_IO_SEQUENTIAL`, capability, version)
- Modify: `emu/qemu/qemu-9.2/hw/m68k/astra68.c` (positions, the share table)
- Modify: `sw/include/astra/vfs_service.h` (version 32, `SEEK`, `SHARE`, `ADOPT`, `ASTRA_VFS_IO_SEQUENTIAL`, feature)
- Modify: `sw/userspace/vfs/include/astra/vfs_backend.h`, `sw/userspace/vfs/src/backends/host.c`, `sw/userspace/vfs/src/vfs_host_direct.c` (the translator), `sw/userspace/vfs/src/vfs_port_transport.c`
- Modify: `ndk/include/astra/vfs_client.h`, `ndk/include/astra/filesystem_library.h`, `sw/userspace/vfs/src/vfs_client.c`, `sw/userspace/vfs/src/filesystem_library.c`, `sw/userspace/vfs/filesystem.library.exports` (Kit 8.1)
- Modify: `sw/userspace/services/hostfs/main.c` (the feature)
- Modify: `sw/userspace/posix/src/file.c`, `sw/userspace/posix/src/process.c`, `sw/userspace/posix/include/astra/posix_descriptor.h`
- Modify: `sw/userspace/commands/fssem/fssem.c`, `emu/qemu/test-fs-semantics.py`, `emu/qemu/test-hostfs.py`, `emu/qemu/test-filesystem-stress.py`
- Test: `sw/userspace/vfs/tests/test_vfs_host_backend.c`, `sw/userspace/vfs/tests/test_filesystem_library.c`

**Interfaces:**
- Produces (host):
  - ops `ASTRA_HOST_FS_SEEK` (20), `ASTRA_HOST_FS_SHARE` (21) and
    `ASTRA_HOST_FS_ADOPT` (22), with `ASTRA_HOST_FS_MAX = ASTRA_HOST_FS_ADOPT`;
  - READ/WRITE flag `ASTRA_HOST_FS_IO_SEQUENTIAL (1u << 14)`;
  - ADOPT flag `ASTRA_HOST_FS_ADOPT_DISCARD (1u << 0)`;
  - `ASTRA_HOST_CAP_FS_DESCRIPTIONS (1u << 13)`;
  - `ASTRA_HOST_VERSION 0x00010010`;
  - `ASTRA_HOST_METRICS_VERSION 4`.
- Produces (wire 32):
  - `ASTRA_VFS_FEATURE_DESCRIPTIONS (1u << 3)`;
  - `ASTRA_VFS_IO_SEQUENTIAL (1u << 1)` on READ, READ_AREA, WRITE and
    WRITE_AREA: the offset is ignored, and `node_size` answers the position
    after;
  - `ASTRA_VFS_OP_SEEK` (37): `file`, `offset` as int64, `flags` 0 set /
    1 current / 2 end; `node_size` answers the position;
  - `ASTRA_VFS_OP_SHARE` (38): `file`; `cursor` answers the token;
  - `ASTRA_VFS_OP_ADOPT` (39): `offset` is the token; `file`, `kind` and
    `node_size` answer the adopted handle. The flag `ASTRA_VFS_ADOPT_DISCARD`
    withdraws an unclaimed share instead.
- Produces (backend, optional): `read_sequential`, `seek`, `share`, `adopt`.
- Produces (Kit 8.1): `astra_vfs_seek`, `astra_vfs_share`, `astra_vfs_adopt`,
  `astra_vfs_unshare`, `astra_vfs_port_read_bulk_position`,
  `astra_filesystem_share(AstraFile *, uint64_t *token)`,
  `astra_filesystem_adopt(AstraFile *, uint64_t token)`,
  `astra_filesystem_unshare(const AstraFile *, uint64_t token)`.
- Produces (libc): `astra_posix_file_share_all(uint64_t *tokens, uint32_t capacity, uint32_t *count) -> int`,
  `astra_posix_file_unshare(const uint64_t *tokens, uint32_t count)`.
- Consumed by: Task 9 (cwd crosses fork by the same share), Task 11 (a
  share counts against its sharer).

- [ ] **Step 1: Failing backend test**

`test_vfs_host_backend.c`. The fake transport answers SHARE with
`value_hi`/`value_lo` = `0x0123456789abcdefull`. It answers ADOPT with
`handle = 0x4321u`, `node.kind = ASTRA_VFS_KIND_FILE` and a size of 7, and
it records the flags of each command.

```c
static void
test_shares_go_to_the_provider(void)
{
    AstraVfsNodeInfo info;
    uintptr_t node = 0u;
    uint64_t token = 0u;

    assert(host_ops->share(&backend, 0x1234u, &token) == ASTRA_VFS_OK);
    assert(token == UINT64_C(0x0123456789abcdef));
    assert(last_operation == ASTRA_HOST_FS_SHARE && last_handle == 0x1234u);
    assert(host_ops->adopt(&backend, token, 0u, &node, &info) ==
           ASTRA_VFS_OK);
    assert(node == 0x4321u && info.kind == ASTRA_VFS_KIND_FILE &&
           info.size == 7u);
    assert(last_operation == ASTRA_HOST_FS_ADOPT && last_flags == 0u);
    assert(host_ops->adopt(&backend, token, ASTRA_VFS_ADOPT_DISCARD, NULL,
                           NULL) == ASTRA_VFS_OK);
    assert(last_flags == ASTRA_HOST_FS_ADOPT_DISCARD);
}
```

`last_operation`, `last_handle` and `last_flags` are what the fake's
`submit` stub sees. Add them if it has none. Register the test. Expected:
compile errors.

- [ ] **Step 2: The provider**

`host.h`:

```c
/** The provider keeps each handle's position (its Linux open file
 * description): SEEK, SHARE, ADOPT and IO_SEQUENTIAL. */
#define ASTRA_HOST_CAP_FS_DESCRIPTIONS (1u << 13)
/** READ/WRITE flag: at the handle's position, which advances; offset ignored.
 * value_hi/value_lo answer the position after. */
#define ASTRA_HOST_FS_IO_SEQUENTIAL     (1u << 14)
/** ADOPT flag: withdraw the unclaimed share instead (its sharer only). */
#define ASTRA_HOST_FS_ADOPT_DISCARD     (1u << 0)
```

Append three operations to the enum after `CLAIM`:
- `ASTRA_HOST_FS_SEEK`: takes `handle`; `offset_hi`/`offset_lo` is a signed
  offset; `flags` is 0 set, 1 current or 2 end; `value_hi`/`value_lo`
  answer the position.
- `ASTRA_HOST_FS_SHARE`: takes `handle`; `value_hi`/`value_lo` answer the
  token.
- `ASTRA_HOST_FS_ADOPT`: `value_hi`/`value_lo` is the token; `handle`
  answers the adopted handle.

Then:
- Set `ASTRA_HOST_FS_MAX` to `ASTRA_HOST_FS_ADOPT`.
- Set `ASTRA_HOST_VERSION` to `0x00010010` and `ASTRA_HOST_METRICS_VERSION`
  to 4.
- Register the three operations (Global Constraints): `seek`, `share` and
  `adopt` in the three lists.

`astra68.c`:
- `astra_host_native_open_flags` no longer maps `ASTRA_VFS_OPEN_APPEND` to
  `O_APPEND`. The handle keeps `ASTRA_VFS_OPEN_APPEND` in `file->flags`,
  and every write through it appends (below).
- READ:

  ```c
              if ((flags & ~ASTRA_HOST_FS_IO_SEQUENTIAL) != 0u) {
                  status = ASTRA_STATUS_INVALID;
              } else if ((flags & ASTRA_HOST_FS_IO_SEQUENTIAL) != 0u) {
                  moved = read(file->fd, data, capacity);
                  if (moved >= 0) {
                      off_t position = lseek(file->fd, 0, SEEK_CUR);

                      if (position >= 0)
                          astra_host_put64(command, HOST_FIELD(value_hi),
                                           position);
                  }
              } else {
                  moved = pread(file->fd, data, capacity,
                                astra_host_get64(command, HOST_FIELD(offset_hi)));
              }
  ```

- WRITE:

  ```c
  /* One write: appended atomically when asked (RWF_APPEND, so the position
   * becomes the end), at the description's position when sequential, at
   * the given offset otherwise. A positioned write honors its offset even
   * on an append description, as POSIX pwrite requires. */
  static ssize_t astra_host_write_one(AstraHostFile *file, const void *data,
                                      size_t length, uint64_t offset,
                                      bool append, bool sequential)
  {
  #ifdef CONFIG_LINUX
      struct iovec vector = {(void *)data, length};

      if (append)
          return pwritev2(file->fd, &vector, 1, -1, RWF_APPEND);
  #else
      if (append && lseek(file->fd, 0, SEEK_END) < 0)
          return -1;
  #endif
      return append || sequential ? write(file->fd, data, length) :
                                    pwrite(file->fd, data, length,
                                           (off_t)offset);
  }
  ```

  The WRITE case calls it with
  `append = (flags & ASTRA_HOST_FS_WRITE_APPEND) != 0u || (file->flags & ASTRA_VFS_OPEN_APPEND) != 0u`
  and `sequential = (flags & ASTRA_HOST_FS_IO_SEQUENTIAL) != 0u`. Any other
  flag bit is INVALID. It answers the position:
  `lseek(file->fd, 0, SEEK_CUR)` for append and sequential writes, and
  `offset + moved` for positioned ones. On macOS the append path is a seek
  and a write under the handle's lock, so it is atomic only per handle.
  That is a development convenience there, and the gates run on Linux.
- SEEK:

  ```c
      case ASTRA_HOST_FS_SEEK: {
          static const int whences[] = {SEEK_SET, SEEK_CUR, SEEK_END};
          off_t position;

          file = astra_host_session_file(s, owner, session_id, handle);
          if (file == NULL)
              status = ASTRA_STATUS_BAD_HANDLE;
          else if (flags > 2u || file->directory != NULL)
              status = file->directory != NULL ? ASTRA_STATUS_IS_DIR :
                                                 ASTRA_STATUS_INVALID;
          else if ((position = lseek(file->fd,
                                     (off_t)(int64_t)astra_host_get64(
                                         command, HOST_FIELD(offset_hi)),
                                     whences[flags])) < 0)
              status = astra_host_status_from_errno(errno);
          else {
              astra_host_put64(command, HOST_FIELD(value_hi),
                               (uint64_t)position);
              status = ASTRA_STATUS_OK;
          }
          break;
      }
  ```

- The share table. `AstraHostState` gains
  `GHashTable *shares;  /* token -> AstraHostShare, under files_lock */`.
  `astra68_init` creates it with `g_int64_hash`/`g_int64_equal`, freeing
  the key with `g_free`.

  ```c
  /* A description held for another process of the same session (SHARE),
   * until it is adopted, withdrawn, or its sharer or session goes. */
  typedef struct AstraHostShare {
      int fd;                  /* F_DUPFD_CLOEXEC of the shared handle */
      uint32_t owner;          /* the sharer: only it may withdraw */
      uint32_t session;        /* adoptable only through this session */
      uint16_t flags;          /* the shared handle's open flags */
      bool directory;
  } AstraHostShare;

  static void astra_host_share_free(AstraHostShare *share)
  {
      close(share->fd);
      g_free(share);
  }

  /* Drops the shares of `owner` (0: any) in `session` (0: any). */
  static void astra_host_release_shares(Astra68State *s, uint32_t owner,
                                        uint32_t session)
  {
      GHashTableIter iterator;
      gpointer key;
      gpointer value;
      GPtrArray *released = g_ptr_array_new();

      qemu_mutex_lock(&s->host.files_lock);
      g_hash_table_iter_init(&iterator, s->host.shares);
      while (g_hash_table_iter_next(&iterator, &key, &value)) {
          AstraHostShare *share = value;

          if ((owner != 0u && share->owner != owner) ||
              (session != 0u && share->session != session))
              continue;
          g_hash_table_iter_steal(&iterator);
          g_free(key);
          g_ptr_array_add(released, share);
      }
      qemu_mutex_unlock(&s->host.files_lock);
      for (guint index = 0u; index < released->len; ++index)
          astra_host_share_free(g_ptr_array_index(released, index));
      g_ptr_array_free(released, true);
  }
  ```

  The sharer's death drops its unclaimed shares. Call
  `astra_host_release_shares(s, owner, 0u)` from
  `astra_host_release_owner`, `astra_host_release_shares(s, 0u, id)` from
  every session release (Task 4's `astra_host_release_session_files`
  sites), and `astra_host_release_shares(s, 0u, 0u)` from
  `astra_host_reset` and Task 10's `astra_host_fs_reset`.
- SHARE and ADOPT:

  ```c
      case ASTRA_HOST_FS_SHARE: {
          AstraHostShare *share;
          guint64 *token;
          int copy;

          file = astra_host_session_file(s, owner, session_id, handle);
          if (file == NULL) {
              status = ASTRA_STATUS_BAD_HANDLE;
              break;
          }
          copy = fcntl(file->fd, F_DUPFD_CLOEXEC, 0);
          if (copy < 0) {
              status = astra_host_status_from_errno(errno);
              break;
          }
          share = g_new0(AstraHostShare, 1);
          share->fd = copy;
          share->owner = owner;
          share->session = session_id;
          share->flags = file->flags;
          share->directory = file->directory != NULL;
          token = g_new(guint64, 1);
          qemu_mutex_lock(&s->host.files_lock);
          do {
              qemu_guest_getrandom_nofail(token, sizeof(*token));
          } while (*token == 0u ||
                   g_hash_table_contains(s->host.shares, token));
          g_hash_table_insert(s->host.shares, token, share);
          qemu_mutex_unlock(&s->host.files_lock);
          astra_host_put64(command, HOST_FIELD(value_hi), *token);
          status = ASTRA_STATUS_OK;
          break;
      }
      case ASTRA_HOST_FS_ADOPT: {
          guint64 token = astra_host_get64(command, HOST_FIELD(value_hi));
          AstraHostShare *share = NULL;
          gpointer key = NULL;

          if ((flags & ~ASTRA_HOST_FS_ADOPT_DISCARD) != 0u) {
              status = ASTRA_STATUS_INVALID;
              break;
          }
          /* Through this session only: a token is not authority by
           * itself. A withdrawal is its sharer's alone. */
          qemu_mutex_lock(&s->host.files_lock);
          if (g_hash_table_lookup_extended(s->host.shares, &token, &key,
                                           (gpointer *)&share) &&
              share->session == session_id &&
              ((flags & ASTRA_HOST_FS_ADOPT_DISCARD) == 0u ||
               share->owner == owner))
              g_hash_table_steal(s->host.shares, &token);
          else
              share = NULL;
          qemu_mutex_unlock(&s->host.files_lock);
          if (share == NULL) {
              status = ASTRA_STATUS_BAD_HANDLE;
              break;
          }
          g_free(key);
          if ((flags & ASTRA_HOST_FS_ADOPT_DISCARD) != 0u) {
              astra_host_share_free(share);
              status = ASTRA_STATUS_OK;
              break;
          }
          /* Same description, same flags; a directory gets its own stream
           * over the shared position. */
          status = astra_host_publish_open(s, owner, session_id, command,
                                           share->fd, share->flags);
          g_free(share);
          break;
      }
  ```

  `astra_host_publish_open` takes ownership of the descriptor, and makes a
  stream for a directory, as it does for an OPEN. `qemu_guest_getrandom_nofail`
  is QEMU's entropy (`include/qemu/guest-random.h`); add the include if
  `astra68.c` lacks it.
- The capability register adds `ASTRA_HOST_CAP_FS_DESCRIPTIONS`.

`test-hostfs.py` gains `descriptions`, after `scopes` in the first boot:

```python
FS_SEEK = 20
FS_SHARE = 21
FS_ADOPT = 22
IO_SEQUENTIAL = 1 << 14
ADOPT_DISCARD = 1
OPEN_APPEND = 1 << 6


def descriptions(qtest, root):
    """Sequential I/O and SEEK move the description's position; a share is
    adopted once, by another owner on a channel bound to the same session,
    and both handles move one position; a positioned write on an append
    description writes where it says."""
    generation = qtest.read32(HACC_GENERATION)
    assert claim(qtest, generation) == STATUS_OK
    work = open_session(qtest, generation)
    sound = open_session(qtest, generation, volume=1)
    opened = execute(qtest, [Command(
        generation, FS_OPEN, "/shared",
        flags=OPEN_READ | OPEN_WRITE | OPEN_CREATE, value=0o600,
        session=work)])[0]
    assert status(opened) == STATUS_OK
    first = get32(opened.raw, 16)
    wrote = execute(qtest, [Command(generation, FS_WRITE, handle=first,
                                    flags=IO_SEQUENTIAL, data=b"ab",
                                    session=work)])[0]
    assert status(wrote) == STATUS_OK and get64(wrote.raw, 32) == 2
    shared = execute(qtest, [Command(generation, FS_SHARE, handle=first,
                                     session=work)])[0]
    assert status(shared) == STATUS_OK
    token = get64(shared.raw, 32)
    assert token != 0
    # A channel of another owner, bound to SOUND: the token is no authority.
    assert configure_scope(qtest, generation, CONFIG_OPEN, 14, 71, sound,
                           SCOPE_BOUND, 0x1001, owner=0x4004,
                           byte_size=8192 + 4096) == 0
    stranger = scoped(qtest, 14, 1, Command(generation, FS_ADOPT,
                                            value=token, session=sound))
    assert status(stranger) == STATUS_BAD_HANDLE
    assert configure_scope(qtest, generation, CONFIG_CLOSE, 14, 71,
                           owner=0x4004) == 0
    # The same owner bound to WORK adopts it once.
    assert configure_scope(qtest, generation, CONFIG_OPEN, 14, 72, work,
                           SCOPE_BOUND, 0x1001, owner=0x4004,
                           byte_size=8192 + 4096) == 0
    adopted = scoped(qtest, 14, 1, Command(generation, FS_ADOPT, value=token,
                                           session=work))
    assert status(adopted) == STATUS_OK
    second = get32(adopted, 16)
    assert status(scoped(qtest, 14, 2, Command(
        generation, FS_ADOPT, value=token,
        session=work))) == STATUS_BAD_HANDLE
    moved = scoped(qtest, 14, 3, Command(generation, FS_SEEK, handle=second,
                                         flags=1, session=work))
    assert status(moved) == STATUS_OK and get64(moved, 32) == 2
    assert status(scoped(qtest, 14, 4, Command(
        generation, FS_SEEK, handle=second, offset=4,
        session=work))) == STATUS_OK
    where = execute(qtest, [Command(generation, FS_SEEK, handle=first,
                                    flags=1, session=work)])[0]
    assert get64(where.raw, 32) == 4, "the two handles do not share a position"
    # Withdrawal is the sharer's alone.
    again = execute(qtest, [Command(generation, FS_SHARE, handle=first,
                                    session=work)])[0]
    token = get64(again.raw, 32)
    assert status(scoped(qtest, 14, 5, Command(
        generation, FS_ADOPT, value=token, flags=ADOPT_DISCARD,
        session=work))) == STATUS_BAD_HANDLE
    assert status(execute(qtest, [Command(
        generation, FS_ADOPT, value=token, flags=ADOPT_DISCARD,
        session=work)])[0]) == STATUS_OK
    assert configure_scope(qtest, generation, CONFIG_CLOSE, 14, 72,
                           owner=0x4004) == 0
    # A positioned write on an append description honors its offset.
    appending = execute(qtest, [Command(
        generation, FS_OPEN, "/shared", flags=OPEN_WRITE | OPEN_APPEND,
        session=work)])[0]
    assert status(execute(qtest, [Command(
        generation, FS_WRITE, handle=get32(appending.raw, 16), offset=0,
        data=b"X", session=work)])[0]) == STATUS_OK
    with open(os.path.join(root, "work", "shared"), "rb") as source:
        content = source.read()
    assert content[:1] == b"X" and len(content) == 2, content
    print("PASS descriptions")
```

The open flag value `OPEN_APPEND` is `ASTRA_VFS_OPEN_APPEND`. Confirm the
bit with `grep -n "define ASTRA_VFS_OPEN_APPEND" sw/include/astra/vfs_service.h`.
The offsets 32/36 are `value_hi`/`value_lo` in record 3 (`test-hostfs.py`'s
`Command`).

- [ ] **Step 3: The wire, the backend and the lane**

`vfs_service.h`: version 32 ("Version 32 lets the provider hold a handle's
position: IO_SEQUENTIAL, SEEK, SHARE and ADOPT"), and:

```c
/** HELLO feature: the provider holds each open description's position. */
#define ASTRA_VFS_FEATURE_DESCRIPTIONS (UINT32_C(1) << 3)
/** READ, READ_AREA, WRITE, WRITE_AREA flag: at the description's position,
 * which advances atomically; `offset` is ignored and `node_size` answers
 * the position after. */
#define ASTRA_VFS_IO_SEQUENTIAL (UINT32_C(1) << 1)
/** Move a description's position: `offset` (two's complement) from
 * `flags` 0 the start, 1 the position, 2 the end; `node_size` answers it. */
#define ASTRA_VFS_OP_SEEK   UINT32_C(37)
/** Hold `file`'s description for another process of this grant; `cursor`
 * answers a 64-bit token. */
#define ASTRA_VFS_OP_SHARE  UINT32_C(38)
/** Take the description shared under token `offset` as a new handle of
 * this session; with ASTRA_VFS_ADOPT_DISCARD, withdraw it unclaimed. */
#define ASTRA_VFS_OP_ADOPT  UINT32_C(39)
#define ASTRA_VFS_ADOPT_DISCARD (UINT32_C(1) << 0)
/** Highest operation code this build defines. */
#define ASTRA_VFS_OP_MAX    ASTRA_VFS_OP_ADOPT
```

`vfs_backend.h`, four optional operations after `escape`:

```c
    /** Read at the node's own position, advancing it; *position answers it
     * after. NULL: the backend holds no positions. */
    uint32_t (*read_sequential)(void *context, uintptr_t node, void *buffer,
                                uint32_t length, uint32_t *moved,
                                uint64_t *position);
    /** Move the node's position (whence 0 start, 1 current, 2 end). */
    uint32_t (*seek)(void *context, uintptr_t node, int64_t offset,
                     uint32_t whence, uint64_t *position);
    /** Hold `node`'s description for adoption; answers its token. */
    uint32_t (*share)(void *context, uintptr_t node, uint64_t *token);
    /** Adopt (flags 0) or withdraw (ASTRA_VFS_ADOPT_DISCARD) a share. */
    uint32_t (*adopt)(void *context, uint64_t token, uint32_t flags,
                      uintptr_t *node, AstraVfsNodeInfo *info);
```

`write` already takes `flags` and answers `position`. The host backend maps
`ASTRA_VFS_IO_SEQUENTIAL` to `ASTRA_HOST_FS_IO_SEQUENTIAL`. `host.c`
implements:
- `host_read_sequential`: READ with the flag; `*position` comes from
  `value_hi`/`value_lo`;
- `host_seek`: SEEK;
- `host_share`: SHARE; `*token` comes from `value_hi`/`value_lo`;
- `host_adopt`: ADOPT with the token in `value_hi`/`value_lo`, and
  `ASTRA_HOST_FS_ADOPT_DISCARD` when asked. It answers `handle` and
  `publish_info`.

Add the four to `host_ops`.

The service core gets none of this. Only host volumes hold descriptions,
and they reach QEMU through the lane (Global Constraints). The Kit's
functions (Step 4) reach the lane's translator through `exchange`.
`sw/userspace/vfs/src/vfs_host_direct.c`, in `astra_vfs_host_direct_transport`:

```c
    case ASTRA_VFS_OP_SEEK: {
        uint64_t position = 0u;

        if (client->version < UINT16_C(32) || ops->seek == NULL)
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
        else if (request->flags > 2u)
            reply->status = ASTRA_VFS_ERR_INVALID;
        else
            reply->status = ops->seek(backend.context, request->file,
                                      (int64_t)request->offset,
                                      request->flags, &position);
        if (reply->status == ASTRA_VFS_OK)
            reply->node_size = position;
        break;
    }
    case ASTRA_VFS_OP_SHARE:
        reply->status = client->version < UINT16_C(32) || ops->share == NULL ?
            ASTRA_VFS_ERR_UNSUPPORTED :
            ops->share(backend.context, request->file, &reply->cursor);
        break;
    case ASTRA_VFS_OP_ADOPT: {
        AstraVfsNodeInfo info = {0};
        uintptr_t node = 0u;

        if (client->version < UINT16_C(32) || ops->adopt == NULL)
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
        else if ((request->flags & ~ASTRA_VFS_ADOPT_DISCARD) != 0u ||
                 request->offset == 0u)
            reply->status = ASTRA_VFS_ERR_INVALID;
        else
            reply->status = ops->adopt(backend.context, request->offset,
                                       request->flags, &node, &info);
        if (reply->status == ASTRA_VFS_OK &&
            (request->flags & ASTRA_VFS_ADOPT_DISCARD) == 0u) {
            reply->file = (AstraVfsFile)node;
            publish_info(reply, &info);
        }
        break;
    }
```

The READ and WRITE cases pass `request->flags`. A READ with
`ASTRA_VFS_IO_SEQUENTIAL` calls `ops->read_sequential` and answers
`node_size` with the position. A WRITE allows the flag beside
`ASTRA_VFS_WRITE_APPEND` and answers `node_size` for both kinds.
`astra_vfs_host_direct_bulk`'s READ_AREA and WRITE_AREA do the same. The
Kit's read and write fast paths in `vfs_client.c` pass offsets only, so a
sequential call takes the bulk path (Step 4), never a fast path.

HostFS grants add `ASTRA_VFS_FEATURE_DESCRIPTIONS` to the features they
answer. `astra_vfs_host_transport_init` requires
`ASTRA_HOST_CAP_FS_DESCRIPTIONS`.

- [ ] **Step 4: The Kit**

`vfs_client.c`, after `astra_vfs_truncate`:

```c
uint32_t astra_vfs_seek(AstraVfsClient *client, AstraVfsFile file,
                        int64_t offset, uint32_t whence, uint64_t *position)
{
    AstraVfsCallState *call;
    uint32_t status;

    if (client == NULL || position == NULL || whence > 2u)
        return ASTRA_VFS_ERR_INVALID;
    if (client->version < UINT16_C(32))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    call = begin(client);
    call->request.file = file;
    call->request.offset = (uint64_t)offset;
    call->request.flags = whence;
    status = exchange(client, call, ASTRA_VFS_OP_SEEK);
    if (status == ASTRA_VFS_OK)
        *position = call->reply.node_size;
    return status;
}

uint32_t astra_vfs_share(AstraVfsClient *client, AstraVfsFile file,
                         uint64_t *token)
{
    AstraVfsCallState *call;
    uint32_t status;

    if (client == NULL || token == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (client->version < UINT16_C(32))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    call = begin(client);
    call->request.file = file;
    status = exchange(client, call, ASTRA_VFS_OP_SHARE);
    *token = status == ASTRA_VFS_OK ? call->reply.cursor : 0u;
    return status;
}

uint32_t astra_vfs_adopt(AstraVfsClient *client, uint64_t token,
                         AstraVfsFile *file, uint16_t *kind, uint64_t *size)
{
    AstraVfsCallState *call;
    uint32_t status;

    if (client == NULL || file == NULL || token == 0u)
        return ASTRA_VFS_ERR_INVALID;
    call = begin(client);
    call->request.offset = token;
    status = exchange(client, call, ASTRA_VFS_OP_ADOPT);
    if (status != ASTRA_VFS_OK)
        return status;
    *file = call->reply.file;
    if (kind != NULL)
        *kind = call->reply.kind;
    if (size != NULL)
        *size = call->reply.node_size;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_unshare(AstraVfsClient *client, uint64_t token)
{
    AstraVfsCallState *call;

    if (client == NULL || token == 0u)
        return ASTRA_VFS_ERR_INVALID;
    call = begin(client);
    call->request.offset = token;
    call->request.flags = ASTRA_VFS_ADOPT_DISCARD;
    return exchange(client, call, ASTRA_VFS_OP_ADOPT);
}
```

`astra_vfs_adopt` runs on a lazy client whose first request may be fused
with HELLO. ADOPT is not in `first_operation`, so the transport says HELLO
first; that is right, because adoption needs the session.

`vfs_port_transport.c`: `astra_vfs_port_read_bulk_position(client, file, offset, flags, buffer, length, moved, position)`
is `astra_vfs_port_read_bulk` with `flags` in the READ_AREA request and the
reply's `node_size` copied to `*position`. Implement `read_bulk` as a call
of it with flags 0.

`filesystem_library.c`:

```c
static int file_described(const AstraFile *file)
{
    return (astra_vfs_features(file->_private_client) &
            ASTRA_VFS_FEATURE_DESCRIPTIONS) != 0u;
}
```

- `astra_filesystem_read`: for a described file, read in chunks of at most
  `ASTRA_VFS_BULK_MAX` with
  `astra_vfs_port_read_bulk_position(client, file, 0u, ASTRA_VFS_IO_SEQUENTIAL, ...)`,
  stopping on a short part, and set `_private_offset` to the last answered
  position. Otherwise, as today.
- `astra_filesystem_write`: for a described file, `filesystem_write_from`
  with flags `ASTRA_VFS_IO_SEQUENTIAL`, or-ed with `ASTRA_VFS_WRITE_APPEND`
  when the file was opened to append. `_private_offset` takes the
  position.
- `astra_filesystem_seek`: for a described file,
  `astra_vfs_seek(client, file, delta, origin, &result)`, with
  `ASTRA_FILE_SEEK_BEGIN/CURRENT/END` being 0/1/2.
- `astra_filesystem_file_info`: for a described file, report `offset` from
  `astra_vfs_seek(client, file, 0, 1, &position)`. The cached value can be
  stale when a child moved the shared position.
- The share trio:

  ```c
  uint32_t astra_filesystem_share(AstraFile *file, uint64_t *token)
  {
      if (!file_valid(file) || token == NULL)
          return ASTRA_VFS_ERR_INVALID;
      if (!file_described(file))
          return ASTRA_VFS_ERR_UNSUPPORTED;
      return astra_vfs_share(file->_private_client, file->_private_file,
                             token);
  }

  /* In a fork child: replace the parent's handle in `file` with this
   * process's own handle on the same description. */
  uint32_t astra_filesystem_adopt(AstraFile *file, uint64_t token)
  {
      AstraVfsFile adopted = ASTRA_VFS_FILE_INVALID;
      uint32_t status;

      if (file == NULL || file->_private_client == NULL || token == 0u)
          return ASTRA_VFS_ERR_INVALID;
      status = astra_vfs_adopt(file->_private_client, token, &adopted, NULL,
                               NULL);
      if (status == ASTRA_VFS_OK)
          file->_private_file = adopted;
      return status;
  }

  uint32_t astra_filesystem_unshare(const AstraFile *file, uint64_t token)
  {
      return file == NULL || file->_private_client == NULL ?
          ASTRA_VFS_ERR_INVALID :
          astra_vfs_unshare(file->_private_client, token);
  }
  ```

The child's `AstraFile` still points at the client the fork copied. Task 3b
keeps that client a lane client in the child (`astra_vfs_host_direct_after_fork`).
The child drops the parent's lanes, keeps the inherited grant scope, and
says HELLO again lazily. So the adopt's exchange runs on the child's own
lane, bound to the same scope and session as the parent's handle, which is
what QEMU's ADOPT requires. A sequential read or write (`IO_SEQUENTIAL`)
goes through `astra_vfs_port_read_bulk_position` and the write bulk path,
which reach the translator's READ_AREA and WRITE_AREA (Step 3). No fast
path takes a flag.

Declare the new functions with doc comments in `vfs_client.h`,
`vfs_port_transport.h` and `filesystem_library.h`, add them to
`filesystem.library.exports` in sorted order, and set
`ASTRA_FILESYSTEM_LIBRARY_ABI_MINOR` to `1u` (identity `8, 1, 0`; the
Makefile check `'^filesystem.library.8 8.1.0 ABI 8.1$$'`).

- [ ] **Step 5: libc fork shares, the child adopts**

`sw/userspace/posix/src/file.c`. `PosixFileSlot` gains `uint64_t share_token;`.
Then:

```c
/* fork: every open file a share token, in the parent, before the clone
 * copies the parent's memory -- so the child finds them in its own copy.
 * Refused (ENOTSUP, nothing shared) when a file's service cannot share. */
int
astra_posix_file_share_all(uint64_t *tokens, uint32_t capacity,
                           uint32_t *count)
{
    uint32_t used = 0u;

    *count = 0u;
    for (uint32_t slot = 0u; slot < file_capacity; ++slot) {
        uint32_t status;

        if (file_slots[slot].used == 0u)
            continue;
        if (used == capacity) {
            errno = EMFILE;
            goto undo;
        }
        status = astra_filesystem_share(&file_slots[slot].file,
                                        &file_slots[slot].share_token);
        if (status != ASTRA_VFS_OK) {
            errno = status == ASTRA_VFS_ERR_UNSUPPORTED ? ENOTSUP :
                    posix_errno(status);
            goto undo;
        }
        tokens[used++] = file_slots[slot].share_token;
    }
    *count = used;
    return 0;
undo:
    astra_posix_file_unshare(tokens, used);
    return -1;
}

/* The parent, after the child has been reaped or failed to start: withdraw
 * whatever the child did not adopt. */
void
astra_posix_file_unshare(const uint64_t *tokens, uint32_t count)
{
    for (uint32_t slot = 0u; slot < file_capacity; ++slot) {
        if (file_slots[slot].used == 0u ||
            file_slots[slot].share_token == 0u)
            continue;
        for (uint32_t index = 0u; index < count; ++index)
            if (tokens[index] == file_slots[slot].share_token) {
                (void)astra_filesystem_unshare(&file_slots[slot].file,
                                               tokens[index]);
                file_slots[slot].share_token = 0u;
                break;
            }
    }
}
```

`astra_posix_file_fork_ready` is deleted: fork no longer refuses because a
file is open, only because a file cannot be shared, and that is decided by
the share itself. `astra_posix_file_after_fork_child` adopts after
re-seeding the namespace:

```c
int
astra_posix_file_after_fork_child(void)
{
    uint32_t status = astra_process_vfs_after_fork_child(
        astra_posix_startup());

    if (status != ASTRA_VFS_OK) {
        errno = posix_errno(status);
        return -1;
    }
    for (uint32_t slot = 0u; slot < file_capacity; ++slot) {
        if (file_slots[slot].used == 0u)
            continue;
        status = astra_filesystem_adopt(&file_slots[slot].file,
                                        file_slots[slot].share_token);
        file_slots[slot].share_token = 0u;
        if (status != ASTRA_VFS_OK) {
            errno = posix_errno(status);
            return -1;
        }
    }
    return 0;
}
```

A child that cannot adopt exits 127 through the existing path
(`process.c:130-141`). It must not run on with a parent's handle that is
not its own.

`sw/userspace/posix/src/process.c`:
- `AstraPosixChild` gains `uint64_t *shares; uint32_t share_count;`.
- In `fork()`, replace the `astra_posix_file_fork_ready` refusal with
  sharing before the clone:

  ```c
      uint64_t *shares = NULL;
      uint32_t share_count = 0u;
      uint32_t open_files = astra_posix_file_open_count();

      if (open_files != 0u) {
          shares = malloc(open_files * sizeof(*shares));
          if (shares == NULL) {
              errno = ENOMEM;
              return (pid_t)-1;
          }
          if (astra_posix_file_share_all(shares, open_files,
                                         &share_count) != 0) {
              free(shares);
              return (pid_t)-1;
          }
      }
  ```

  On every parent failure path after this point, call
  `astra_posix_file_unshare(shares, share_count); free(shares);`. On
  success store `shares` and `share_count` in the child record. In the
  child branch (`process_id == 0u`), `free(shares)`.
- Where `waitpid` frees a reaped child (`:384-388`), and in
  `discard_inherited_children`, first call
  `astra_posix_file_unshare(child->shares, child->share_count); free(child->shares);`.
  In `discard_inherited_children`, which runs in a fork child for its
  parent's children, only free the array: those tokens belong to the
  grandparent.
- `astra_posix_file_open_count()` counts used slots; declare it, and the
  share and unshare functions, in `posix_descriptor.h` in place of
  `astra_posix_file_fork_ready`.

- [ ] **Step 6: The guest cases**

```c
static int read_all(const char *path, char *out, size_t capacity)
{
    return (int)read_text(path, out, capacity);
}

/* dup, fork and exec share one position; pread/pwrite do not move it;
 * pwrite on an append description writes where it says
 * (docs/FILESYSTEM_PRODUCTION_PRD.md FS20). */
static int case_shared_description(const char *directory)
{
    char path[160];
    char text[32];
    int fd;
    int copy;
    pid_t child;
    int status;

    join(path, sizeof(path), directory, "shared");
    fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0644);
    CHECK(fd >= 0, "open: %s", strerror(errno));
    CHECK(write(fd, "P1", 2) == 2, "write P1");
    copy = dup(fd);
    CHECK(copy >= 0 && write(copy, "D1", 2) == 2, "write through dup");
    CHECK(lseek(fd, 0, SEEK_CUR) == 4, "dup did not share the position");
    child = fork();
    CHECK(child >= 0, "fork with an open file: %s", strerror(errno));
    if (child == 0)
        _exit(write(fd, "C1", 2) == 2 ? 0 : 1);
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
          WEXITSTATUS(status) == 0, "child write failed");
    CHECK(lseek(fd, 0, SEEK_CUR) == 6, "the child's write did not move "
          "the parent's position");
    CHECK(pwrite(fd, "x", 1, 0) == 1 && lseek(fd, 0, SEEK_CUR) == 6,
          "pwrite moved the position");
    CHECK(pwrite(fd, "P", 1, 0) == 1, "pwrite back");
    CHECK(write(fd, "P2", 2) == 2, "write P2");
    (void)close(copy);
    (void)close(fd);
    CHECK(read_all(path, text, sizeof(text)) == 8 &&
          strcmp(text, "P1D1C1P2") == 0, "content %s", text);

    fd = open(path, O_WRONLY | O_APPEND);
    CHECK(fd >= 0 && pwrite(fd, "Q", 1, 0) == 1, "pwrite on append");
    (void)close(fd);
    CHECK(read_all(path, text, sizeof(text)) == 8 &&
          strcmp(text, "Q1D1C1P2") == 0,
          "pwrite on an append description appended: %s", text);
    return 0;
}

/* Two processes appending to one file never interleave inside a line. */
static int case_append_race(const char *directory)
{
    char path[160];
    char line[33];
    struct stat st;
    pid_t child;
    int status;
    int fd;

    join(path, sizeof(path), directory, "appended");
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_APPEND, 0644);
    CHECK(fd >= 0, "open: %s", strerror(errno));
    child = fork();
    CHECK(child >= 0, "fork: %s", strerror(errno));
    memset(line, child == 0 ? 'c' : 'p', 31);
    line[31] = '\n';
    for (int index = 0; index < 100; ++index)
        if (write(fd, line, 32) != 32) {
            if (child == 0)
                _exit(1);
            return fail("parent append %d", index);
        }
    if (child == 0)
        _exit(0);
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
          WEXITSTATUS(status) == 0, "child appends failed");
    (void)close(fd);
    CHECK(stat(path, &st) == 0 && st.st_size == 200 * 32,
          "size %lld", (long long)st.st_size);
    fd = open(path, O_RDONLY);
    for (int index = 0; index < 200; ++index) {
        CHECK(read(fd, line, 32) == 32, "read line %d", index);
        for (int at = 1; at < 31; ++at)
            CHECK(line[at] == line[0], "line %d interleaved", index);
    }
    (void)close(fd);
    return 0;
}

/* exec keeps a description open across the image and closes CLOEXEC. */
static int case_exec_inherit(const char *directory)
{
    char path[160];
    char number[16];
    char closed[16];
    char text[16];
    pid_t child;
    int status;
    int fd;
    int gone;

    join(path, sizeof(path), directory, "execd");
    fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0644);
    gone = open(path, O_RDONLY | O_CLOEXEC);
    CHECK(fd >= 0 && gone >= 0, "open: %s", strerror(errno));
    CHECK(write(fd, "A", 1) == 1, "write A");
    (void)snprintf(number, sizeof(number), "%d", fd);
    (void)snprintf(closed, sizeof(closed), "%d", gone);
    child = fork();
    CHECK(child >= 0, "fork: %s", strerror(errno));
    if (child == 0) {
        char *const argv[] = {"fssem", "--exec-helper", number, closed, NULL};

        execv("/commands/fssem", argv);
        _exit(127);
    }
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
          WEXITSTATUS(status) == 0, "exec helper status %d",
          WEXITSTATUS(status));
    CHECK(write(fd, "P", 1) == 1, "write P");
    (void)close(fd);
    (void)close(gone);
    CHECK(read_all(path, text, sizeof(text)) == 3 && strcmp(text, "AEP") == 0,
          "content %s", text);
    return 0;
}
```

`main` gains, before the case loop:

```c
    if (argc == 4 && strcmp(argv[1], "--exec-helper") == 0) {
        int fd = atoi(argv[2]);
        int gone = atoi(argv[3]);

        if (fcntl(gone, F_GETFD) != -1 || errno != EBADF)
            return 3;
        return write(fd, "E", 1) == 1 ? 0 : 2;
    }
```

`fssem` lives where `install_test_command` puts it. Confirm that is
`/commands/fssem` (`astra_image.py:704-730`); if it is another directory,
use that path in `execv`. Register `shared-description`, `append-race` and
`exec-inherit`, and add them to `CASES`. Add `#include <sys/wait.h>`.

- [ ] **Step 7: Run, perturb, full suite, commit**

Sync, then build and test userspace, build `sw/boot`, run
`emu/qemu/build.sh host`, rebuild images, and run `test-hostfs.py` and
`test-fs-semantics.py`. Expected: all PASS, including the three new cases.

Perturb (restore and re-sync each time):
1. QEMU SHARE: replace `fcntl(file->fd, F_DUPFD_CLOEXEC, 0)` with a fresh
   open of the same file. Use `/proc/self/fd/N` on Linux:
   `open(g_strdup_printf("/proc/self/fd/%d", file->fd), O_RDWR)`.
   Expected: `shared-description` fails "the child's write did not move the
   parent's position", and `test-hostfs.py`'s `descriptions` fails "the two
   handles do not share a position".
2. QEMU append: call `write` instead of `pwritev2(..., RWF_APPEND)` for
   append writes. Expected: `append-race` fails with a short size or an
   interleaved line. Run it three times: a race can pass once.
3. Open with `O_APPEND` again in `astra_host_native_open_flags`. Expected:
   `shared-description` fails "pwrite on an append description appended".
4. libc: skip `astra_filesystem_adopt` in the fork child. Expected: the
   child's write uses the parent's handle, which belongs to another owner,
   and is refused (BAD_HANDLE). `shared-description` fails "child write
   failed".
5. QEMU ADOPT: drop `share->session == session_id`. Expected:
   `descriptions` fails at the stranger's `STATUS_BAD_HANDLE`: a token
   alone became authority.

`~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-task8.log`. Expected: green.
The terminal gate's zsh pipelines and `fsstress` (which forks) must still
pass. Run one `--bench t8` and compare it with Task 3b's `qemu-t3b-1.json`;
`seq-read-warm`, `seq-write` and `append` are the workloads this task
touches.

```bash
git add sw/include/astra emu/qemu/qemu-9.2/hw/m68k/astra68.c sw/userspace/vfs ndk \
  sw/userspace/services/hostfs/main.c sw/userspace/posix \
  sw/userspace/commands/fssem/fssem.c emu/qemu/test-fs-semantics.py \
  emu/qemu/test-hostfs.py emu/qemu/test-filesystem-stress.py
git commit -m "feat(fs): the provider holds positions; fork shares descriptions by token (FS20); storage protocol 32, Kit 8.1"
```

---

### Task 9: Retained cwd (D4, second half)

Today the current directory is a string (`file.c` `cwd`, set by `chdir`
at `:1522-1549`). Every relative path is joined to it and resolved again,
so renaming the directory a process stands in breaks every relative name
it uses. Haiku keeps `io_context->cwd` as a referenced vnode
(`vfs.cpp:8343`, `set_cwd`) and builds `getcwd` from it
(`dir_vnode_to_path`, `vfs.cpp:8318` and `:2637`). This task does the same.

- cwd is a retained directory handle (`AstraFile`) whenever the binding's
  service can say where a handle is (`ASTRA_VFS_FEATURE_DIRECTORY_PATH`,
  HostFS only). Other services keep the string cwd. They are the guest
  ext4, ram, proc and events trees, and Phase 2 deletes the first.
- Every relative path operation is sent relative to that handle. The
  storage protocol already has OPEN_AT, UNLINK_AT, CHMOD_AT and STAT_AT
  (`vfs_service.h:224-234`). Version 33 lets MKDIR, RENAME, LINK, SYMLINK and
  READLINK name a base handle too, and STAT_AT follow a final link. The host
  wire has taken a base handle on every path operation since Task 4.
- `getcwd` asks the provider where the handle is now (host `PATH`, storage
  `DIRECTORY_PATH`). Linux's `/proc/self/fd` link is the hint, and the
  answer is re-resolved beneath the session root and must name the same
  inode. A cwd that has been removed, or moved where its binding cannot
  name it, answers `ENOENT`. That is the defined failure, the same one
  Linux `getcwd` gives for an unlinked cwd.
- fork shares the cwd handle by Task 8's token, and exec exports it like an
  open file.
- An escape from a cwd-relative operation (`..` above the grant root, or a
  link out of the volume) re-resolves the path as a string from the
  current `getcwd`, which Task 6's continuation then follows.

**Files:**
- Modify: `sw/include/astra/host.h` (`PATH`, capability, version, metrics version)
- Modify: `emu/qemu/qemu-9.2/hw/m68k/astra68.c`, `emu/qemu/test-hostfs.py`, `emu/qemu/test-filesystem-stress.py`, `sw/userspace/services/hostfs/main.c` (`fs_group_names`)
- Modify: `sw/include/astra/vfs_service.h` (version 33, `DIRECTORY_PATH`, feature, `STAT_AT` follow)
- Modify: `sw/userspace/vfs/include/astra/vfs_backend.h`, `sw/userspace/vfs/src/vfs_service_core.c`, `sw/userspace/vfs/src/backends/host.c`
- Modify: `ndk/include/astra/vfs_client.h`, `ndk/include/astra/filesystem_library.h`, `sw/userspace/vfs/src/vfs_client.c`, `sw/userspace/vfs/src/filesystem_library.c`, `sw/userspace/vfs/filesystem.library.exports` (Kit 8.2)
- Modify: `sw/userspace/posix/src/file.c`
- Modify: `sw/userspace/commands/fssem/fssem.c`, `emu/qemu/test-fs-semantics.py`
- Test: `sw/userspace/vfs/tests/test_vfs_service_core.c`, `sw/userspace/vfs/tests/test_vfs_host_backend.c`

**Interfaces:**
- Consumes: Task 4's base handles (`handle`, and `value_lo` for the second
  operand); Task 6's `note_escape`, `host_readlink_at` and
  `ASTRA_VFS_STAT_FOLLOW`; Task 8's `astra_filesystem_share/adopt/unshare`
  and `astra_posix_file_share_all/unshare`.
- Produces (host): `ASTRA_HOST_FS_PATH` (23) with `ASTRA_HOST_FS_MAX` set to it;
  `ASTRA_HOST_CAP_FS_PATHS (1u << 14)`; `ASTRA_HOST_VERSION 0x00010011`;
  `ASTRA_HOST_METRICS_VERSION 5`.
- Produces (wire 33): `ASTRA_VFS_FEATURE_DIRECTORY_PATH (1u << 4)`;
  `ASTRA_VFS_OP_DIRECTORY_PATH` (40: `file`; `length` the capacity;
  `payload`/`count` answer the path beneath the grant root). MKDIR, READLINK
  and SYMLINK (for its link path) take a base directory in `file`. RENAME
  and LINK take the source's base in `file` and the destination's in
  `offset`. `ASTRA_VFS_FILE_INVALID` (0) is the grant root with an absolute
  path, as before. A real base requires a relative, non-empty path. STAT_AT
  accepts `ASTRA_VFS_STAT_FOLLOW`.
- Produces (backend, optional): `mkdir_at`, `rename_at`, `link_at`,
  `symlink_at`, `readlink_at`, `stat_at_follow`, `directory_path`.
- Produces (Kit 8.2): `astra_vfs_mkdir_at`, `astra_vfs_rename_at`,
  `astra_vfs_link_at`, `astra_vfs_symlink_at`, `astra_vfs_readlink_at`,
  `astra_vfs_stat_at_follow_meta`, `astra_vfs_directory_path`;
  `astra_filesystem_mkdir_at`, `astra_filesystem_rename_at`,
  `astra_filesystem_link_at`, `astra_filesystem_symlink_at`,
  `astra_filesystem_readlink_at`, `astra_filesystem_stat_at_follow`,
  `astra_filesystem_directory_path`.

- [ ] **Step 1: Failing backend test**

Directory-relative operations run in the host backend of each client
(Step 3b explains why the service core gets none of them). Test them in
`test_vfs_host_backend.c`. The fake transport's `submit` stub records
`last_operation`, `last_handle`, `last_value_lo`, the first and second path
operands (`last_path`/`last_path_length`, `last_path2`/`last_path2_length`,
copied from the request). For `ASTRA_HOST_FS_PATH` it answers by copying
`fake_path_answer` into the output buffer and setting `result_length`. Add
what the fake lacks. Then:

```c
/* Version 33: a path operation relative to a base handle sends the handle
 * and the relative operand; PATH asks where a directory is now. */
static void
test_relative_operations_name_their_base(void)
{
    char where[16];
    uint32_t length = 0u;

    assert(host_ops->mkdir_at(&backend, 0x77u, "sub/new", 0750u) ==
           ASTRA_VFS_OK);
    assert(last_operation == ASTRA_HOST_FS_MKDIR && last_handle == 0x77u &&
           last_value_lo == 0750u &&
           operand(last_path, last_path_length, "sub/new"));
    assert(host_ops->rename_at(&backend, 0x77u, "a", 0x78u, "b") ==
           ASTRA_VFS_OK);
    assert(last_operation == ASTRA_HOST_FS_RENAME && last_handle == 0x77u &&
           last_value_lo == 0x78u &&
           operand(last_path, last_path_length, "a") &&
           operand(last_path2, last_path2_length, "b"));
    assert(host_ops->symlink_at(&backend, "target", 0x77u, "link") ==
           ASTRA_VFS_OK);
    assert(last_operation == ASTRA_HOST_FS_SYMLINK && last_handle == 0x77u &&
           operand(last_path, last_path_length, "target") &&
           operand(last_path2, last_path2_length, "link"));
    fake_path_answer = "/d";
    assert(host_ops->directory_path(&backend, 0x77u, where, sizeof(where),
                                    &length) == ASTRA_VFS_OK);
    assert(last_operation == ASTRA_HOST_FS_PATH && last_handle == 0x77u &&
           length == 2u && memcmp(where, "/d", 2u) == 0);
}
```

`operand` is Task 4's helper in this file. Register the test. Expected:
compile errors (`mkdir_at`, `ASTRA_HOST_FS_PATH`, the backend fields).

- [ ] **Step 2: The provider**

`host.h`:

```c
/** The provider reports where a handle is: ASTRA_HOST_FS_PATH. */
#define ASTRA_HOST_CAP_FS_PATHS (1u << 14)
```

Append `ASTRA_HOST_FS_PATH` after `ASTRA_HOST_FS_ADOPT` with the doc comment
"Where `handle` is now, beneath the session root, into the data span
(`result_length` bytes); NOT_FOUND when it has no such path". Set
`ASTRA_HOST_FS_MAX` to `ASTRA_HOST_FS_PATH`, `ASTRA_HOST_VERSION` to
`0x00010011` and `ASTRA_HOST_METRICS_VERSION` to 5. Register `path` in the
three lists (Global Constraints).

`astra68.c`, beside the resolver includes:

```c
#ifdef CONFIG_LINUX
/*
 * Where `fd` is now, as a path beneath `root`. The kernel's /proc/self/fd
 * link is only a hint: it says "(deleted)" for an unlinked directory and
 * may name a place outside `root`. So the answer is resolved again beneath
 * `root`, with no links, and must reach the same inode
 * (Haiku: dir_vnode_to_path, vfs.cpp:2637, walks the vnode chain instead).
 */
static uint32_t astra_host_fs_path(int root, int fd, char *out,
                                   uint32_t capacity, uint32_t *length)
{
    struct open_how how = {
        .flags = O_PATH | O_CLOEXEC,
        .resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS |
                   RESOLVE_NO_MAGICLINKS,
    };
    char link[48];
    char *root_path;
    char *path;
    const char *rest;
    size_t prefix;
    struct stat want;
    struct stat got;
    uint32_t status = ASTRA_STATUS_NOT_FOUND;
    int check;

    snprintf(link, sizeof(link), "/proc/self/fd/%d", root);
    root_path = g_file_read_link(link, NULL);
    snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
    path = g_file_read_link(link, NULL);
    if (root_path == NULL || path == NULL || fstat(fd, &want) != 0)
        goto done;
    prefix = strcmp(root_path, "/") == 0 ? 0u : strlen(root_path);
    if (strncmp(path, root_path, prefix) != 0 ||
        (path[prefix] != '\0' && path[prefix] != '/'))
        goto done;
    rest = path[prefix] == '\0' ? "/" : path + prefix;
    check = syscall(SYS_openat2, root, rest[1] == '\0' ? "." : rest + 1,
                    &how, sizeof(how));
    if (check < 0)
        goto done;
    if (fstat(check, &got) == 0 && got.st_dev == want.st_dev &&
        got.st_ino == want.st_ino) {
        *length = (uint32_t)strlen(rest);
        if (*length > capacity) {
            status = ASTRA_STATUS_BUFFER_TOO_SMALL;
        } else {
            memcpy(out, rest, *length);
            status = ASTRA_STATUS_OK;
        }
    }
    close(check);
done:
    g_free(root_path);
    g_free(path);
    return status;
}
#endif
```

The case, in `astra_host_execute_fs` beside READLINK:

```c
    case ASTRA_HOST_FS_PATH: {
        uint32_t capacity = ldl_be_p(command + HOST_FIELD(data_capacity));
        uint8_t *data = astra_host_command_data(
            s, physical, bytes, command_bytes, command, capacity);
        uint32_t length = 0u;

        file = astra_host_session_file(s, owner, session_id, handle);
        if (file == NULL) {
            status = ASTRA_STATUS_BAD_HANDLE;
        } else if (data == NULL || capacity == 0u) {
            status = ASTRA_STATUS_INVALID;
        } else {
#ifdef CONFIG_LINUX
            status = astra_host_fs_path(root, file->fd, (char *)data,
                                        capacity, &length);
            if (status == ASTRA_STATUS_OK)
                stl_be_p(command + HOST_FIELD(result_length), length);
#else
            status = ASTRA_STATUS_UNSUPPORTED;
#endif
        }
        break;
    }
```

The capability register adds `ASTRA_HOST_CAP_FS_PATHS` under
`CONFIG_LINUX` only. On macOS the provider does not offer it, so HostFS
there does not advertise `ASTRA_VFS_FEATURE_DIRECTORY_PATH` (Step 3).

`test-hostfs.py`, a check `paths` (Task 4's helpers):
- open session S on `work`; MKDIR `a`, MKDIR `a/b`; OPEN `a/b` with
  `OPEN_DIRECTORY` as handle H; PATH H answers `/a/b`;
- RENAME `a` to `c`; PATH H answers `/c/b`;
- a derived session T rooted at `/c`: OPEN `b` in T as H2; PATH H2 answers
  `/b`. The answer never shows what lies above a session's root;
- UNLINK `c/b` with `UNLINK_DIRECTORY`; PATH H answers `STATUS_NOT_FOUND`
  (2), not `/c/b (deleted)`.

- [ ] **Step 3: The wire, the backend and the core**

`vfs_service.h`: version 33 ("Version 33 lets a path operation name the
directory it is relative to, and asks where a directory is:
DIRECTORY_PATH"). Add:

```c
/** HELLO feature: DIRECTORY_PATH answers, so a client may hold its current
 * directory as a handle. */
#define ASTRA_VFS_FEATURE_DIRECTORY_PATH (UINT32_C(1) << 4)
/** Where directory `file` is now, beneath the grant root: `payload`,
 * `count` bytes, at most `length`. NOT_FOUND when it has no path. */
#define ASTRA_VFS_OP_DIRECTORY_PATH UINT32_C(40)
/** Highest operation code this build defines. */
#define ASTRA_VFS_OP_MAX ASTRA_VFS_OP_DIRECTORY_PATH
```

Extend the comments on MKDIR, READLINK, SYMLINK, RENAME and LINK with the
base-handle rule from Interfaces, and the STAT_AT comment with
"`ASTRA_VFS_STAT_FOLLOW` (version 33) follows a final link".

`vfs_backend.h`, after `adopt`:

```c
    /* Version 33: path operations relative to an open directory node; a
     * node of 0 is the session root with an absolute path. NULL: the
     * backend has no directory-relative form of the operation. */
    uint32_t (*mkdir_at)(void *context, uintptr_t directory,
                         const char *path, uint16_t create_mode);
    uint32_t (*rename_at)(void *context, uintptr_t from_directory,
                          const char *from, uintptr_t to_directory,
                          const char *to);
    uint32_t (*link_at)(void *context, uintptr_t from_directory,
                        const char *from, uintptr_t to_directory,
                        const char *to);
    uint32_t (*symlink_at)(void *context, const char *target,
                           uintptr_t directory, const char *path);
    uint32_t (*readlink_at)(void *context, uintptr_t directory,
                            const char *path, void *buffer,
                            uint32_t capacity, uint32_t *length);
    uint32_t (*stat_at_follow)(void *context, uintptr_t directory,
                               const char *path, AstraVfsNodeInfo *info);
    /** Where `node` is now, beneath the root; ASTRA_VFS_ERR_NOT_FOUND
     * when it has no path. */
    uint32_t (*directory_path)(void *context, uintptr_t node, char *buffer,
                               uint32_t capacity, uint32_t *length);
```

The other backends initialise their tables positionally and stop before
these fields, so C zero-fills them. No other table changes.

`host.c`:

```c
static uint32_t host_mkdir_at(void *context, uintptr_t directory,
                              const char *path, uint16_t create_mode)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    uint32_t status = command_begin(backend, &request, ASTRA_HOST_FS_MKDIR,
                                    0u, path, NULL);

    if (status != ASTRA_VFS_OK)
        return status;
    request.command->handle = (uint32_t)directory;
    request.command->value_lo = create_mode == ASTRA_VFS_MODE_DEFAULT ?
                                0777u : create_mode;
    status = submit(backend, &request, NULL, 0u, NULL, 0u);
    return status == ASTRA_VFS_ERR_ESCAPED ?
        note_escape(backend, request.command, (uint32_t)directory, path) :
        status;
}

/* RENAME and LINK: each operand beneath its own base. The provider
 * answers CROSS_DEVICE for an operand that leaves the session (Task 6). */
static uint32_t host_two_paths_at(void *context, uint16_t operation,
                                  uintptr_t left_directory, const char *left,
                                  uintptr_t right_directory,
                                  const char *right)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    uint32_t status = command_begin(backend, &request, operation, 0u, left,
                                    right);

    if (status != ASTRA_VFS_OK)
        return status;
    request.command->handle = (uint32_t)left_directory;
    request.command->value_lo = (uint32_t)right_directory;
    return submit(backend, &request, NULL, 0u, NULL, 0u);
}

static uint32_t host_rename_at(void *context, uintptr_t from_directory,
                               const char *from, uintptr_t to_directory,
                               const char *to)
{
    return host_two_paths_at(context, ASTRA_HOST_FS_RENAME, from_directory,
                             from, to_directory, to);
}

static uint32_t host_link_at(void *context, uintptr_t from_directory,
                             const char *from, uintptr_t to_directory,
                             const char *to)
{
    return host_two_paths_at(context, ASTRA_HOST_FS_LINK, from_directory,
                             from, to_directory, to);
}

static uint32_t host_symlink_at(void *context, const char *target,
                                uintptr_t directory, const char *path)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    uint32_t status = command_begin(backend, &request, ASTRA_HOST_FS_SYMLINK,
                                    0u, target, path);

    if (status != ASTRA_VFS_OK)
        return status;
    request.command->handle = (uint32_t)directory;
    status = submit(backend, &request, NULL, 0u, NULL, 0u);
    return status == ASTRA_VFS_ERR_ESCAPED ?
        note_escape(backend, request.command, (uint32_t)directory, path) :
        status;
}

static uint32_t host_readlink_at_node(void *context, uintptr_t directory,
                                      const char *path, void *buffer,
                                      uint32_t capacity, uint32_t *length)
{
    return host_readlink_at(context, (uint32_t)directory, path, buffer,
                            capacity, length);
}

static uint32_t host_directory_path(void *context, uintptr_t node,
                                    char *buffer, uint32_t capacity,
                                    uint32_t *length)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    uint32_t status = command_begin(backend, &request, ASTRA_HOST_FS_PATH,
                                    capacity, NULL, NULL);

    *length = 0u;
    if (status != ASTRA_VFS_OK)
        return status;
    request.command->handle = (uint32_t)node;
    status = submit(backend, &request, NULL, 0u, buffer, capacity);
    if (status != ASTRA_VFS_OK)
        return status;
    if (request.command->result_length > capacity)
        return ASTRA_VFS_ERR_PROTOCOL;
    *length = request.command->result_length;
    return ASTRA_VFS_OK;
}
```

`host_stat_at_follow` is `host_stat_at` sending `ASTRA_HOST_FS_STAT_FOLLOW`
in `flags`. Write both as one `host_stat_at_flags(context, directory, path, flags, info)`
and two one-line wrappers. Add the seven functions to `host_ops` in field
order. Check `host_readlink_at`'s first parameter type, which Task 6 wrote
as the backend pointer, and cast as needed.

The service core does not change: no core backend has these operations,
and host volumes reach QEMU through the lane. Step 3b gives the lane's
translator the base handles and DIRECTORY_PATH.

HostFS grants add `ASTRA_VFS_FEATURE_DIRECTORY_PATH` when the lease has
`ASTRA_HOST_CAP_FS_PATHS`. `astra_vfs_host_transport_init` does not
require it, because the macOS provider lacks it.

- [ ] **Step 3b: The lane carries base handles and DIRECTORY_PATH**

No service-core backend has the `_at` operations or `directory_path`. Only
host volumes do, and they reach QEMU through the lane (Global Constraints).
So Step 3's `vfs_service_core.c` changes are **not** made: `base_node`, the
dispatch changes and the DIRECTORY_PATH case. Their equivalents live in the
lane's translator. There a base is simpler. A lane's file handles *are*
the provider's handles of this process, so a base needs no file table:
QEMU checks that it is a directory of the scope's session (Task 4) and
answers `NOT_DIR` or `BAD_HANDLE` itself.

`sw/userspace/vfs/src/vfs_host_direct.c`:

```c
/* Version 33's base handle: ASTRA_VFS_FILE_INVALID is the grant root with
 * the path as it always was; a real base is one of this client's own
 * directory handles and needs a relative, non-empty path. */
static uint32_t direct_base(const AstraVfsClient *client, uint32_t handle,
                            const uint8_t *path, uintptr_t *node)
{
    *node = 0u;
    if (handle == ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_OK;
    if (client->version < UINT16_C(33))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    if (path[0] == '/' || path[0] == '\0')
        return ASTRA_VFS_ERR_INVALID;
    *node = handle;
    return ASTRA_VFS_OK;
}
```

In `astra_vfs_host_direct_transport`:
- **MKDIR.** After its mode check:

  ```c
          uintptr_t base = 0u;

          reply->status = direct_base(client, request->file,
                                      request->body.path, &base);
          if (reply->status == ASTRA_VFS_OK)
              reply->status = base == 0u ?
                  ops->mkdir(backend.context,
                             (const char *)request->body.path, mode) :
                  ops->mkdir_at(backend.context, base,
                                (const char *)request->body.path, mode);
  ```

  `mode` is the value the case computes today.
- **READLINK.** It is the same: a base selects
  `ops->readlink_at(backend.context, base, path, reply->payload, capacity, &reply->count)`.
- **SYMLINK** (the one-request form). The base is checked against the link
  path `symlink->to`, and a base selects
  `ops->symlink_at(backend.context, target, base, to)`.
- **RENAME and LINK** (one-request forms). There are two bases:
  `request->file` against `body.path`, and `(uint32_t)request->offset`
  against `to`. An `offset` above `UINT32_MAX` is INVALID. When either
  base is real, call `rename_at`/`link_at`, with 0 for the root side.
- **STAT_AT.** Accept `request->flags == ASTRA_VFS_STAT_FOLLOW` when
  `client->version >= 33`, and route it to `ops->stat_at_follow`.
- **DIRECTORY_PATH:**

  ```c
      case ASTRA_VFS_OP_DIRECTORY_PATH: {
          uint32_t length = 0u;
          uint32_t capacity = request->length < ASTRA_VFS_PATH_MAX - 1u ?
              request->length : ASTRA_VFS_PATH_MAX - 1u;

          if (client->version < UINT16_C(33) ||
              ops->directory_path == NULL) {
              reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
              break;
          }
          reply->status = ops->directory_path(
              backend.context, request->file, (char *)reply->payload,
              capacity, &length);
          if (reply->status == ASTRA_VFS_OK) {
              if (length == 0u || length > capacity ||
                  reply->payload[0] != '/' ||
                  memchr(reply->payload, 0, length) != NULL ||
                  !astra_utf8_validate(reply->payload, length, 0u))
                  reply->status = ASTRA_VFS_ERR_PROTOCOL;
              else
                  reply->count = length;
          }
          break;
      }
  ```

The capacity is bounded by `ASTRA_VFS_PATH_MAX - 1`: a cwd whose path is
longer than the storage protocol carries (Decisions, D7) answers
`BUFFER_TOO_SMALL`, which `getcwd` reports as `ENAMETOOLONG`. Step 4's Kit
functions reach these cases through `exchange`, because none of them has a
fast path. Step 1's failing test moves with the code. The core test of
directory-relative operations becomes a `test_vfs_host_backend.c` test of
the seven backend functions, which drive the provider with `handle` and
`value_lo` set as Step 3's `host.c` shows. It asserts each command's
`handle`, `value_lo` and operands.

- [ ] **Step 4: The Kit**

`vfs_client.c`. Split the body of `astra_vfs_stat_at_meta` into
`static uint32_t stat_at_common(client, directory, path, flags, entry, meta)`,
which sets `call->request.flags = flags` and refuses a non-zero `flags`
below version 33. Then:

```c
static int relative_path_valid(const char *path)
{
    return path_text_valid(path, NULL) && path[0] != '\0' && path[0] != '/';
}

uint32_t astra_vfs_stat_at_follow_meta(AstraVfsClient *client,
                                       AstraVfsFile directory,
                                       const char *path,
                                       AstraVfsDirEntry *entry,
                                       AstraVfsNodeMeta *meta)
{
    return stat_at_common(client, directory, path, ASTRA_VFS_STAT_FOLLOW,
                          entry, meta);
}

uint32_t astra_vfs_mkdir_at(AstraVfsClient *client, AstraVfsFile directory,
                            const char *path, uint16_t create_mode)
{
    AstraVfsCallState *call;

    if (client == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (directory == ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (client->version < UINT16_C(33))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    if (!relative_path_valid(path))
        return ASTRA_VFS_ERR_INVALID;
    call = begin(client);
    (void)set_path(&call->request, path);
    call->request.file = directory;
    call->request.offset = create_mode;
    return exchange(client, call, ASTRA_VFS_OP_MKDIR);
}

/* RENAME, LINK and SYMLINK carry two paths. A base of
 * ASTRA_VFS_FILE_INVALID means that operand is absolute from the grant
 * root. */
static uint32_t two_paths_at(AstraVfsClient *client, uint32_t operation,
                             AstraVfsFile first_directory, const char *first,
                             AstraVfsFile second_directory,
                             const char *second)
{
    AstraVfsCallState *call;
    AstraVfsRenameRequest *request;

    if (client == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (client->version < UINT16_C(33))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    if ((first_directory != ASTRA_VFS_FILE_INVALID &&
         !relative_path_valid(first)) ||
        (second_directory != ASTRA_VFS_FILE_INVALID &&
         !relative_path_valid(second)))
        return ASTRA_VFS_ERR_INVALID;
    call = call_state(client);
    request = &call->rename_request;
    begin_request(client, &request->request,
                  (uint16_t)ASTRA_VFS_RENAME_REQUEST_SIZE);
    if (!set_path(&request->request, first) ||
        !set_path_bytes(request->to, second))
        return ASTRA_VFS_ERR_INVALID;
    request->request.file = first_directory;
    request->request.offset = second_directory;
    return exchange_request(client, operation, &request->request, call);
}

uint32_t astra_vfs_rename_at(AstraVfsClient *client,
                             AstraVfsFile from_directory, const char *from,
                             AstraVfsFile to_directory, const char *to)
{
    return two_paths_at(client, ASTRA_VFS_OP_RENAME, from_directory, from,
                        to_directory, to);
}

uint32_t astra_vfs_link_at(AstraVfsClient *client,
                           AstraVfsFile from_directory, const char *from,
                           AstraVfsFile to_directory, const char *to)
{
    return two_paths_at(client, ASTRA_VFS_OP_LINK, from_directory, from,
                        to_directory, to);
}

/* The target is stored, never resolved: it is not relative to anything. */
uint32_t astra_vfs_symlink_at(AstraVfsClient *client, const char *target,
                              AstraVfsFile directory, const char *path)
{
    AstraVfsCallState *call;
    AstraVfsRenameRequest *request;

    if (client == NULL || target == NULL || target[0] == '\0')
        return ASTRA_VFS_ERR_INVALID;
    if (directory == ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (client->version < UINT16_C(33))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    if (!relative_path_valid(path))
        return ASTRA_VFS_ERR_INVALID;
    call = call_state(client);
    request = &call->rename_request;
    begin_request(client, &request->request,
                  (uint16_t)ASTRA_VFS_RENAME_REQUEST_SIZE);
    if (!set_path(&request->request, target) ||
        !set_path_bytes(request->to, path))
        return ASTRA_VFS_ERR_INVALID;
    request->request.file = directory;
    return exchange_request(client, ASTRA_VFS_OP_SYMLINK, &request->request,
                            call);
}

uint32_t astra_vfs_directory_path(AstraVfsClient *client,
                                  AstraVfsFile directory, char *path,
                                  uint32_t capacity, uint32_t *length)
{
    AstraVfsCallState *call;
    uint32_t status;

    if (client == NULL || path == NULL || capacity < 2u || length == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *length = 0u;
    if (client->version < UINT16_C(33))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    call = begin(client);
    call->request.file = directory;
    call->request.length = capacity - 1u;
    status = exchange(client, call, ASTRA_VFS_OP_DIRECTORY_PATH);
    if (status != ASTRA_VFS_OK)
        return status;
    if (call->reply.count == 0u || call->reply.count > capacity - 1u)
        return ASTRA_VFS_ERR_PROTOCOL;
    memcpy(path, call->reply.payload, call->reply.count);
    path[call->reply.count] = '\0';
    *length = call->reply.count;
    return ASTRA_VFS_OK;
}
```

`astra_vfs_readlink_at(client, directory, path, buffer, capacity, length)`
is `astra_vfs_readlink` with `call->request.file = directory`, the
`relative_path_valid` check and the version-33 gate. Write it by moving
`astra_vfs_readlink`'s body into `static uint32_t readlink_common(client, directory, path, buffer, capacity, length)`,
used by both.

`filesystem_library.c`:

```c
static uint32_t directory_for(const AstraFile *directory, uint32_t rights)
{
    if (!file_valid(directory))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (directory->_private_kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_NOT_DIR;
    if ((file_rights(directory) & rights) != rights)
        return ASTRA_VFS_ERR_ACCESS;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_mkdir_at(const AstraFile *directory,
                                   const char *path, uint16_t create_mode)
{
    uint32_t status = directory_for(directory, ASTRA_RIGHT_WRITE);

    return status != ASTRA_VFS_OK ? status :
        astra_vfs_mkdir_at(directory->_private_client,
                           directory->_private_file, path, create_mode);
}

uint32_t astra_filesystem_symlink_at(const char *target,
                                     const AstraFile *directory,
                                     const char *path)
{
    uint32_t status = directory_for(directory, ASTRA_RIGHT_WRITE);

    return status != ASTRA_VFS_OK ? status :
        astra_vfs_symlink_at(directory->_private_client, target,
                             directory->_private_file, path);
}

uint32_t astra_filesystem_readlink_at(const AstraFile *directory,
                                      const char *path, void *buffer,
                                      uint32_t capacity, uint32_t *length)
{
    uint32_t status = directory_for(directory, ASTRA_RIGHT_READ);

    return status != ASTRA_VFS_OK ? status :
        astra_vfs_readlink_at(directory->_private_client,
                              directory->_private_file, path, buffer,
                              capacity, length);
}

typedef uint32_t (*FilesystemTwoPathsAt)(AstraVfsClient *, AstraVfsFile,
                                         const char *, AstraVfsFile,
                                         const char *);

/*
 * One operand may be relative to a directory and the other absolute in
 * the namespace. Both must reach the same client: a rename cannot cross a
 * grant, which is CROSS_DEVICE as it is for two paths.
 */
static uint32_t filesystem_two_paths_at(AstraFilesystem *filesystem,
                                        const AstraFile *from_directory,
                                        const char *from,
                                        const AstraFile *to_directory,
                                        const char *to,
                                        FilesystemTwoPathsAt operation)
{
    AstraVfsClient *from_client = NULL;
    AstraVfsClient *to_client = NULL;
    AstraVfsFile from_base = ASTRA_VFS_FILE_INVALID;
    AstraVfsFile to_base = ASTRA_VFS_FILE_INVALID;
    char from_wire[ASTRA_VFS_PATH_MAX];
    char to_logical[ASTRA_VFS_PATH_MAX];
    char to_wire[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    if (from == NULL || to == NULL ||
        (from_directory == NULL && to_directory == NULL))
        return ASTRA_VFS_ERR_INVALID;
    if (from_directory != NULL) {
        status = directory_for(from_directory, ASTRA_RIGHT_WRITE);
        if (status != ASTRA_VFS_OK)
            return status;
        from_client = from_directory->_private_client;
        from_base = from_directory->_private_file;
        if (strlen(from) >= sizeof(from_wire))
            return ASTRA_VFS_ERR_INVALID;
        (void)strcpy(from_wire, from);
    } else {
        status = filesystem_locate_literal(
            filesystem, from, ASTRA_RIGHT_WRITE, &from_client, from_wire,
            NULL, NULL, NULL, NULL, NULL);
        if (status != ASTRA_VFS_OK)
            return status;
    }
    if (to_directory != NULL) {
        status = directory_for(to_directory, ASTRA_RIGHT_WRITE);
        if (status != ASTRA_VFS_OK)
            return status;
        to_client = to_directory->_private_client;
        to_base = to_directory->_private_file;
        if (strlen(to) >= sizeof(to_wire))
            return ASTRA_VFS_ERR_INVALID;
        (void)strcpy(to_wire, to);
    } else {
        if (!filesystem_valid(filesystem))
            return ASTRA_VFS_ERR_INVALID;
        status = astra_vfs_assign_destination(
            filesystem->_private_assigns, to, ASTRA_RIGHT_WRITE, 0,
            filesystem_client_for, filesystem, to_logical,
            sizeof(to_logical), to_wire, sizeof(to_wire), &to_client, NULL);
        if (status != ASTRA_VFS_OK)
            return status;
    }
    if (from_client != to_client)
        return ASTRA_VFS_ERR_CROSS_DEVICE;
    return operation(from_client, from_base, from_wire, to_base, to_wire);
}

uint32_t astra_filesystem_rename_at(AstraFilesystem *filesystem,
                                    const AstraFile *from_directory,
                                    const char *from,
                                    const AstraFile *to_directory,
                                    const char *to)
{
    return filesystem_two_paths_at(filesystem, from_directory, from,
                                   to_directory, to, astra_vfs_rename_at);
}

uint32_t astra_filesystem_link_at(AstraFilesystem *filesystem,
                                  const AstraFile *from_directory,
                                  const char *from,
                                  const AstraFile *to_directory,
                                  const char *to)
{
    return filesystem_two_paths_at(filesystem, from_directory, from,
                                   to_directory, to, astra_vfs_link_at);
}

/*
 * The namespace path of directory handle `directory`, opened through
 * binding `assign` (spelled as the caller spells it: the result's first
 * component). The provider answers where the handle is beneath the grant
 * root; this strips the binding's own root from that and prefixes the
 * name. NOT_FOUND: the directory was removed, or now lies outside what the
 * binding can name.
 */
uint32_t astra_filesystem_directory_path(AstraFilesystem *filesystem,
                                         const char *assign,
                                         const AstraFile *directory,
                                         char *path, size_t capacity)
{
    const AstraAssign *binding;
    char wire[ASTRA_VFS_PATH_MAX];
    const char *rest;
    uint32_t length = 0u;
    size_t root;
    uint32_t status;

    if (!filesystem_valid(filesystem) || assign == NULL || path == NULL ||
        capacity == 0u)
        return ASTRA_VFS_ERR_INVALID;
    status = directory_for(directory, 0u);
    if (status != ASTRA_VFS_OK)
        return status;
    if ((astra_vfs_features(directory->_private_client) &
         ASTRA_VFS_FEATURE_DIRECTORY_PATH) == 0u)
        return ASTRA_VFS_ERR_UNSUPPORTED;
    binding = astra_assign_member(filesystem->_private_assigns, assign,
                                  file_member(directory));
    if (binding == NULL)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_vfs_directory_path(directory->_private_client,
                                      directory->_private_file, wire,
                                      sizeof(wire), &length);
    if (status != ASTRA_VFS_OK)
        return status;
    root = strlen(binding->root);
    if (root == 0u) {
        rest = wire;
    } else if (strncmp(wire + 1, binding->root, root) == 0 &&
               (wire[1 + root] == '\0' || wire[1 + root] == '/')) {
        rest = wire + 1 + root;
    } else {
        return ASTRA_VFS_ERR_NOT_FOUND;
    }
    if (strcmp(rest, "/") == 0)
        rest = "";
    if (1u + strlen(assign) + strlen(rest) + 1u > capacity)
        return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    path[0] = '/';
    (void)strcpy(path + 1, assign);
    (void)strcat(path, rest);
    return ASTRA_VFS_OK;
}
```

`astra_filesystem_stat_at_follow(directory, path, info)` is
`astra_filesystem_stat_at` with `astra_vfs_stat_at_follow_meta`. Factor
the shared body into one static function taking the lookup function.

Declare every new function with a doc comment in `vfs_client.h` and
`filesystem_library.h`, add them to `filesystem.library.exports` in sorted
order, and set `ASTRA_FILESYSTEM_LIBRARY_ABI_MINOR` to `2u` (identity
`8, 2, 0`; the Makefile check `'^filesystem.library.8 8.2.0 ABI 8.2$$'`).

Add `test_vfs_host_backend.c` cases for `host_mkdir_at` (the command
carries `handle` and the path operand), `host_rename_at` (`handle` and
`value_lo`) and `host_directory_path` (a fake answer of `/x/y` with
`result_length` 4 arrives as `/x/y`; one longer than the capacity is
`ASTRA_VFS_ERR_PROTOCOL`). Follow that file's existing fake-transport
pattern.

- [ ] **Step 5: libc holds cwd as a handle**

`sw/userspace/posix/src/file.c`. State, beside `cwd`:

```c
/*
 * The current directory, as Haiku's io_context->cwd is a vnode
 * (vfs.cpp:8343): a held directory, so renaming it changes nothing for
 * relative names. `cwd` stays the last known path, which getcwd refreshes.
 * Only bindings whose service can say where a handle is are held; the rest
 * keep the string alone.
 */
static AstraFile cwd_file = ASTRA_FILE_INIT;
static int cwd_held;
static char cwd_assign[ASTRA_CAPABILITY_NAME_MAX];
static uint64_t cwd_share_token;
```

`PosixPath` gains `const AstraFile *base;` (NULL: `native` is a namespace
path). Then:

```c
static void
cwd_release(void)
{
    if (cwd_held)
        (void)astra_filesystem_close(&cwd_file);
    cwd_file = (AstraFile)ASTRA_FILE_INIT;
    cwd_held = 0;
    cwd_assign[0] = '\0';
}

/* Where the held cwd is now. 0 with errno set when it cannot be named. */
static int
cwd_refresh(void)
{
    char fresh[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    if (!cwd_held)
        return 1;
    status = astra_filesystem_directory_path(&filesystem.filesystem,
                                             cwd_assign, &cwd_file, fresh,
                                             sizeof(fresh));
    if (status != ASTRA_VFS_OK) {
        errno = status == ASTRA_VFS_ERR_BUFFER_TOO_SMALL ? ENAMETOOLONG :
                status == ASTRA_VFS_ERR_NOT_FOUND ? ENOENT :
                posix_errno(status);
        return 0;
    }
    if (strcmp(fresh, cwd) != 0) {
        (void)strcpy(cwd, fresh);
        (void)set_process_cwd(cwd);
    }
    return 1;
}
```

`resolve_path` refreshes first when the path is relative and cwd is held,
so callers that still need a namespace string (`execve`, `statvfs`,
`opendir`'s root check) get the directory's current name:

```c
    if (cwd_held && !astra_posix_path_is_absolute(path) && !cwd_refresh())
        return 0;
```

goes before the `astra_posix_path_resolve` call. Add:

```c
/*
 * A path for an operation that has a directory-relative form: a relative
 * path against the held cwd goes as it is, unresolved, and the provider
 * walks it from the handle. Anything else resolves as before.
 */
static int
resolve_beneath(const char *path, PosixPath *out, char *normal)
{
    out->base = NULL;
    if (cwd_held && path != NULL && path[0] != '\0' &&
        !astra_posix_path_is_absolute(path)) {
        if (!start())
            return 0;
        if (strlen(path) >= sizeof(out->native)) {
            errno = ENAMETOOLONG;
            return 0;
        }
        (void)strcpy(out->native, path);
        out->root = 0;
        out->base = &cwd_file;
        if (normal != NULL)
            normal[0] = '\0';
        return 1;
    }
    return resolve_path(path, out, normal);
}

/*
 * A cwd-relative operation left its grant (`..` above the root, or a link
 * out of the volume). Redo it as a namespace path from where cwd is now;
 * Task 6's continuation follows it from there.
 */
static int
rebase(uint32_t *status, const char *path, PosixPath *out, char *normal)
{
    if (*status != ASTRA_VFS_ERR_ESCAPED || out->base == NULL)
        return 0;
    out->base = NULL;
    if (!resolve_path(path, out, normal)) {
        *status = ASTRA_VFS_ERR_NOT_FOUND;
        return 0;
    }
    return 1;
}
```

`resolve_path` must set `out->base = NULL` too.

Then each relative-capable operation uses `resolve_beneath` and a retry.
`open_path`:

```c
    if (!resolve_beneath(path, &resolved, NULL))
        return -1;
    ...
again:
    status = resolved.base != NULL ?
        astra_filesystem_open_at_mode(resolved.base, resolved.native, wanted,
                                      (uint16_t)create_mode, &opened) :
        astra_filesystem_open_mode(&filesystem.filesystem, resolved.native,
                                   wanted, (uint16_t)create_mode, &opened);
    if (status == ASTRA_VFS_ERR_IS_DIR && may_open_directory(wanted))
        status = resolved.base != NULL ?
            astra_filesystem_open_at_mode(resolved.base, resolved.native,
                                          wanted | ASTRA_VFS_OPEN_DIRECTORY,
                                          (uint16_t)create_mode, &opened) :
            astra_filesystem_open_mode(&filesystem.filesystem,
                                       resolved.native,
                                       wanted | ASTRA_VFS_OPEN_DIRECTORY,
                                       (uint16_t)create_mode, &opened);
    if (rebase(&status, path, &resolved, NULL))
        goto again;
```

The same shape, with these pairs (`base` form / path form):
- `stat_path`: `astra_filesystem_stat_at_follow` or
  `astra_filesystem_stat_at` (literal) / `astra_filesystem_stat` or
  `astra_filesystem_lstat`. Keep the "final slash, `.` or `..` names a
  directory" rule ahead of both.
- `mkdir`: `astra_filesystem_mkdir_at` / `astra_filesystem_mkdir_mode`.
- `remove_path`: with a base, `astra_filesystem_unlink_at(resolved.base, resolved.native, flags)`.
  The provider walks a multi-component relative path, so the parent-split
  code stays for the path form only.
- `chmod`: `astra_filesystem_chmod_at(base, native, mode, 0u)` / `astra_filesystem_chmod`.
- `readlink`: `astra_filesystem_readlink_at` / `astra_filesystem_readlink`.
- `symlink`: `astra_filesystem_symlink_at(native_target, base, native)` / `astra_filesystem_symlink`.
- `rename` and `link`: resolve both operands with `resolve_beneath`, then
  `astra_filesystem_rename_at(&filesystem.filesystem, from.base, from.native, to.base, to.native)`
  when either has a base, or the path form when neither does. On ESCAPED,
  rebase both.

`fstatat`, `unlinkat`, `fchmodat` and `openat` with `AT_FDCWD` reach these
functions already. `fstatat` with a real `dirfd` is wrong today in both
directions (`file.c:1053-1068`): with `AT_SYMLINK_NOFOLLOW` it answers
`ENOTSUP`, and without it, it calls `astra_filesystem_stat_at`, which is
the no-follow lookup (STAT_AT, `vfs_service.h:233`). Make the flag choose:
`AT_SYMLINK_NOFOLLOW` calls `astra_filesystem_stat_at`, and no flag calls
`astra_filesystem_stat_at_follow`.

`chdir`:

```c
int
chdir(const char *path)
{
    AstraFile opened = ASTRA_FILE_INIT;
    PosixPath resolved;
    char normal[ASTRA_VFS_PATH_MAX];
    char assign[ASTRA_CAPABILITY_NAME_MAX];
    char fresh[ASTRA_VFS_PATH_MAX];
    const uint32_t flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY;
    uint32_t status;

    if (!resolve_beneath(path, &resolved, normal))
        return -1;
again:
    if (resolved.root) {
        if (set_process_cwd("/") != 0)
            return -1;
        cwd_release();
        (void)strcpy(cwd, "/");
        return 0;
    }
    status = resolved.base != NULL ?
        astra_filesystem_open_at(resolved.base, resolved.native, flags,
                                 &opened) :
        astra_filesystem_open(&filesystem.filesystem, resolved.native, flags,
                              &opened);
    if (rebase(&status, path, &resolved, normal))
        goto again;
    if (status != ASTRA_VFS_OK)
        return fail(status);
    /* The binding a held cwd names: the first component of the namespace
     * path, or the old cwd's when the path was relative to it. */
    if (resolved.base != NULL) {
        (void)strcpy(assign, cwd_assign);
    } else {
        const char *start = normal + 1;
        size_t length = strcspn(start, "/");

        if (length >= sizeof(assign)) {
            (void)astra_filesystem_close(&opened);
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(assign, start, length);
        assign[length] = '\0';
    }
    status = astra_filesystem_directory_path(&filesystem.filesystem, assign,
                                             &opened, fresh, sizeof(fresh));
    if (status == ASTRA_VFS_ERR_UNSUPPORTED) {
        /* A service that cannot say where a handle is: the string cwd. */
        (void)astra_filesystem_close(&opened);
        if (resolved.base != NULL) {
            errno = EIO;
            return -1;
        }
        if (set_process_cwd(normal) != 0)
            return -1;
        cwd_release();
        (void)strcpy(cwd, normal);
        return 0;
    }
    if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_NOT_FOUND &&
        status != ASTRA_VFS_ERR_BUFFER_TOO_SMALL) {
        (void)astra_filesystem_close(&opened);
        return fail(status);
    }
    cwd_release();
    cwd_file = opened;
    cwd_held = 1;
    (void)strcpy(cwd_assign, assign);
    if (status == ASTRA_VFS_OK) {
        (void)strcpy(cwd, fresh);
    } else if (resolved.base == NULL) {
        (void)strcpy(cwd, normal);
    } else {
        cwd[0] = '\0';          /* only getcwd's failure can describe it */
    }
    if (cwd[0] != '\0')
        (void)set_process_cwd(cwd);
    return 0;
}
```

The `resolved.base != NULL` arm of the UNSUPPORTED branch cannot happen,
because a held cwd is held only when its service supports the feature. It
is answered rather than asserted.

`getcwd`:

```c
    if (!start())
        return NULL;
    if (cwd_held && !cwd_refresh())
        return NULL;            /* ENOENT: removed, or out of its binding */
    if (cwd[0] == '\0') {
        errno = ENOENT;
        return NULL;
    }
```

before the length check.

Exec. `PosixFileExecHeader` gains, after `cwd`:

```c
    uint32_t cwd_held;
    char cwd_assign[ASTRA_CAPABILITY_NAME_MAX];
    AstraPosixFileExecState cwd_handle;
```

Set `POSIX_FILE_EXEC_VERSION` to 2. `file_exec_export` fills them with
`astra_process_file_export(&cwd_file, &header->cwd_handle)` when held.
`file_exec_import` validates `cwd_assign` is terminated. After
`astra_process_filesystem_open`, it imports the handle with
`astra_process_file_import(&header->cwd_handle, &cwd_file)` when
`cwd_held`. It calls `set_process_cwd` only when `header->cwd[0] != '\0'`.

Fork. Task 8's `astra_posix_file_share_all` also shares the held cwd:
`astra_filesystem_share(&cwd_file, &cwd_share_token)` into the next token
slot. `astra_posix_file_open_count()` counts it, so the token array has
room. `astra_posix_file_unshare` withdraws `cwd_share_token` like a file's.
`astra_posix_file_after_fork_child` adopts it after the files with
`astra_filesystem_adopt(&cwd_file, cwd_share_token)`. A failure is the same
exit-127 path as a file's.

`posix_errno` maps nothing new. `ASTRA_VFS_ERR_ESCAPED` from a dirfd-relative
call (not cwd) stays `EIO` as Task 6 defined. A dirfd has no binding name
to rebase from, and Phase 2's live namespace gives one.

- [ ] **Step 6: The guest case**

```c
/* A process standing in a directory that is renamed keeps working, and
 * getcwd follows it; one standing in a removed directory gets ENOENT
 * (docs/FILESYSTEM_PRODUCTION_PRD.md FS19). */
static int case_cwd_rename(const char *directory)
{
    char old[256];
    char before[160];
    char after[160];
    char here[256];
    char text[16];
    char gone[160];
    struct stat st;
    pid_t child;
    int status;
    int fd;

    CHECK(getcwd(old, sizeof(old)) != NULL, "getcwd: %s", strerror(errno));
    join(before, sizeof(before), directory, "cwd-a");
    join(after, sizeof(after), directory, "cwd-b");
    CHECK(mkdir(before, 0755) == 0, "mkdir: %s", strerror(errno));
    CHECK(chdir(before) == 0, "chdir: %s", strerror(errno));
    CHECK(write_text("inside", "1", O_WRONLY | O_CREAT | O_EXCL) == 0,
          "create relative");
    CHECK(rename(before, after) == 0, "rename cwd: %s", strerror(errno));
    CHECK(stat("inside", &st) == 0, "relative stat after rename: %s",
          strerror(errno));
    CHECK(write_text("second", "2", O_WRONLY | O_CREAT | O_EXCL) == 0,
          "relative create after rename");
    CHECK(mkdir("sub", 0755) == 0 && rename("sub", "sub2") == 0 &&
          rmdir("sub2") == 0, "relative mkdir/rename/rmdir: %s",
          strerror(errno));
    CHECK(symlink("inside", "alias") == 0 &&
          readlink("alias", text, sizeof(text)) == 6 &&
          memcmp(text, "inside", 6) == 0, "relative symlink/readlink");
    fd = open(".", O_RDONLY | O_DIRECTORY);
    CHECK(fd >= 0 && fstatat(fd, "alias", &st, 0) == 0 &&
          S_ISREG(st.st_mode) &&
          fstatat(fd, "alias", &st, AT_SYMLINK_NOFOLLOW) == 0 &&
          S_ISLNK(st.st_mode), "fstatat follow/nofollow");
    (void)close(fd);
    CHECK(link("inside", "hard") == 0 && stat("hard", &st) == 0 &&
          st.st_nlink == 2, "relative link");
    CHECK(getcwd(here, sizeof(here)) != NULL && strcmp(here, after) == 0,
          "getcwd %s, wanted %s", here, after);
    child = fork();
    CHECK(child >= 0, "fork: %s", strerror(errno));
    if (child == 0)
        _exit(stat("second", &st) == 0 ? 0 : 1);
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
          WEXITSTATUS(status) == 0, "the fork child lost the cwd");
    CHECK(chdir("..") == 0 && getcwd(here, sizeof(here)) != NULL &&
          strcmp(here, directory) == 0, "cd .. reached %s", here);

    join(gone, sizeof(gone), directory, "cwd-gone");
    CHECK(mkdir(gone, 0755) == 0 && chdir(gone) == 0, "chdir gone");
    CHECK(rmdir(gone) == 0, "rmdir the cwd: %s", strerror(errno));
    CHECK(getcwd(here, sizeof(here)) == NULL && errno == ENOENT,
          "getcwd of a removed cwd: %s", strerror(errno));
    fd = open("new", O_WRONLY | O_CREAT, 0644);
    CHECK(fd < 0 && errno == ENOENT, "created in a removed directory");
    CHECK(chdir(old) == 0, "chdir back: %s", strerror(errno));
    return 0;
}
```

Register `cwd-rename` and add it to `CASES`. The case assumes `directory`
is the normalized namespace path the harness passes to every case. If
`getcwd` reports a different spelling of the same place (case of the
assign), compare with `strcasecmp` on the first component and say so in
the case comment.

- [ ] **Step 7: Run, perturb, full suite, commit**

Sync, then build and test userspace, build `sw/boot`, run
`emu/qemu/build.sh host`, rebuild images, and run `test-hostfs.py` and
`test-fs-semantics.py`. Expected: PASS, including `paths` and `cwd-rename`.

Perturb (restore and re-sync each time):
1. `resolve_beneath`: never set `base` (fall through to `resolve_path`).
   Expected: `cwd-rename` fails "relative stat after rename". This is the
   bug the task removes.
2. QEMU `astra_host_fs_path`: skip the inode comparison (treat any
   successful `openat2` as a match, and use the link text without the
   prefix check). Expected: `test-hostfs.py` `paths` fails on the removed
   directory, and `cwd-rename` fails "getcwd of a removed cwd".
3. libc fork: do not share the cwd. Expected: the fork child's relative
   `stat` uses the parent's handle, which another owner holds (BAD_HANDLE, so
   `EBADF`), and `cwd-rename` fails "the fork child lost the cwd".

`~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-task9.log`. Expected: green.
The terminal gate's zsh `cd` paths are the main consumer. Run one
`--bench t9` and compare it with Task 3b's `qemu-t3b-1.json`. Relative names now cost
the same as absolute ones, so `stat-warm` should not regress.

```bash
git add sw/include/astra emu/qemu/qemu-9.2/hw/m68k/astra68.c sw/userspace/vfs ndk \
  sw/userspace/services/hostfs/main.c sw/userspace/posix/src/file.c \
  sw/userspace/commands/fssem/fssem.c emu/qemu/test-fs-semantics.py \
  emu/qemu/test-hostfs.py emu/qemu/test-filesystem-stress.py
git commit -m "feat(fs): cwd is a held directory; getcwd asks the provider (FS19); storage protocol 33, Kit 8.2"
```

---

### Task 10: Provider loss (D6)

Two losses, one answer. A client whose files or service are gone gets
`ASTRA_VFS_ERR_LOST`, and POSIX reports it as `ESTALE`, the errno NFS uses
for the same situation. It never gets a wrong file, a hang, or `ENOENT`.
Each loss leaves a defined way back (FS35). The user's decision of
2026-10-07 holds: the supervisor restarts its managed services that hold
HostFS grants, and applications get `ESTALE`.

- **The provider loses its state.** QEMU's provider restarts; in Phase 1
  the seam is a QOM write, `astra-host-fs-reset`. Every session goes, and
  so does the claim. Every handle is *disconnected*, the way Haiku's
  `disconnect_fd` marks a descriptor `O_DISCONNECTED`
  (`src/system/kernel/fs/fd.cpp:262-264`): its Linux descriptor is closed,
  and it answers `LOST` until its owner closes it. A bound channel naming
  a lost session answers `LOST` too.

  The client's lane marks itself lost. The Kit fails that client, and its
  next use says HELLO on the same grant port again.

  HostFS recovers on that HELLO, or on any other control message. It first
  sees the provider's *epoch* change, which its claim answers. Then it
  claims again, and reopens each grant's provider session from the grant's
  recorded volume and root. It revokes each grant's old scope through the
  kernel, which detaches it from every channel, and mints a new one over
  the new session. The HELLO hands out the new scope.

  So the files opened before the loss answer `LOST`, and new opens through
  the same grant work. The grant ports never closed, so no process loses
  its namespace. Reopening by recorded path is what a restarted Linux
  provider would have to do too. A grant whose root directory was renamed
  after it was made fails to reopen, and HostFS logs it (below).
- **HostFS dies** (killed, or faulted). The kernel revokes every scope it
  created (Task 3b, `host_scopes_revoke_creator`). QEMU drops its claim,
  sessions and handles (Task 4). Every lane then answers `LOST`, and the
  Kit's re-HELLO finds the grant port dead, so it answers `LOST` from then
  on.

  The supervisor restarts HostFS, and the new instance publishes new grant
  ports. The supervisor then restarts every managed service whose manifest
  grants a name the new HostFS serves (decision 2026-10-07). Those services
  get the new grants. Applications keep `ESTALE` for their old grants until
  they are restarted. Re-binding a running application's namespace needs
  Phase 2's live namespace publication (FS16).

**Files:**
- Modify: `sw/include/astra/status.h`, `sw/include/astra/vfs_service.h` (`LOST`)
- Modify: `emu/qemu/qemu-9.2/hw/m68k/astra68.c` (disconnected handles, lost bound sessions, scope tombstones, fs reset, epoch, QOM seam)
- Modify: `sw/userspace/vfs/include/astra/vfs_host_backend.h`, `sw/userspace/vfs/src/backends/host.c` (`lost`, claim epoch)
- Modify: `sw/userspace/vfs/src/vfs_host_direct.c`, `sw/userspace/vfs/src/vfs_port_transport.c`, `sw/userspace/vfs/include/astra/vfs_port_transport.h`, `sw/userspace/vfs/src/vfs_process.c`, `sw/userspace/vfs/src/vfs_client.c` (the lane fails and reconnects; PEER becomes LOST)
- Modify: `sw/userspace/services/hostfs/main.c` (recovery)
- Modify: `sw/userspace/supervisor/src/loader.c`, `sw/userspace/supervisor/include/loader.h` (restart the holders of a republished name)
- Modify: `sw/userspace/posix/src/file.c` (`ESTALE`)
- Modify: `sw/userspace/commands/fssem/fssem.c`, `emu/qemu/test-fs-semantics.py`, `emu/qemu/test-hostfs.py`
- Test: `sw/userspace/vfs/tests/test_vfs_host_backend.c`, `sw/userspace/vfs/tests/test_vfs_port.c`

**Interfaces:**
- Produces (status): `ASTRA_STATUS_LOST` (21) and
  `ASTRA_VFS_ERR_LOST ((uint32_t)ASTRA_STATUS_LOST)`.
- Produces (QEMU): QOM `astra-host-fs-reset` (write-only bool; `true` resets
  the filesystem provider) and `astra-host-fs-resets` (read, count).
  CLAIM answers the provider epoch in `result_value`. A disconnected handle,
  or a bound channel's lost or revoked session, answers `ASTRA_STATUS_LOST`.
- Produces (backend): `AstraVfsHostBackend.lost`;
  `astra_vfs_host_claim(transport, generation, release, &epoch)`.
- Produces (port): `void astra_vfs_port_fail(AstraVfsClient *)`,
  `int astra_vfs_port_client_closed(const AstraVfsClient *)`.
- Produces (Kit): a storage call whose service or scope is gone answers
  `ASTRA_VFS_ERR_LOST` rather than `ASTRA_VFS_ERR_PEER`. The transport
  function `astra_vfs_port_transport` still answers PEER, as its contract
  says (`vfs_port_transport.h:44-53`); the mapping is one level up.
- Produces (gate): fssem `--loss-probe PATH SECONDS reopen|dead`; checks
  `provider-reset` and `hostfs-kill`.

- [ ] **Step 1: Failing tests**

`test_vfs_host_backend.c`. The fake transport answers `ASTRA_VFS_ERR_LOST`
on demand (`fake_lost = 1`). A path operation that answers LOST marks the
backend lost. A handle operation does not: only that file is gone.

```c
static void
test_a_lost_session_marks_the_backend(void)
{
    AstraVfsNodeInfo info;
    uint8_t bytes[4];
    uint32_t moved = 0u;

    assert(astra_vfs_host_init(&backend, transport, 9u, 7u));
    fake_lost = 1;
    assert(host_ops->read(&backend, 0x1234u, 0u, bytes, sizeof(bytes),
                          &moved) == ASTRA_VFS_ERR_LOST);
    assert(backend.lost == 0u);
    assert(host_ops->stat(&backend, "/a", &info) == ASTRA_VFS_ERR_LOST);
    assert(backend.lost == 1u);
    fake_lost = 0;
}
```

Use the file's own names for the backend, the transport fake and the ops
table (`grep -n "astra_vfs_host_ops()\|static AstraVfsHostBackend" sw/userspace/vfs/tests/test_vfs_host_backend.c`).
The fake's `submit` stub sets `request->command->status = ASTRA_STATUS_LOST`
and answers `ASTRA_VFS_ERR_LOST` while `fake_lost` is set.

`test_vfs_port.c`. Two assertions say that a vanished service answers
`ASTRA_VFS_ERR_PEER`: `:1068` through `astra_vfs_open`, and `:1551` through
the bulk read. They now expect `ASTRA_VFS_ERR_LOST`. Update their comments:
the transport says PEER, and the caller is told LOST. Add:

```c
/* A failed client is closed, and says so: vfs_process reconnects it. */
static void
test_a_failed_client_reports_closed(void)
{
    AstraVfsPortService host;
    AstraVfsClient remote;
    uint32_t service_handle;

    mock_reset();
    service_start();
    service_handle = mock_open(MOCK_QUEUE_MAX);
    assert(astra_vfs_port_service_init(&host, service_handle, &service));
    served = &host;
    assert(astra_vfs_port_connect(&remote, service_handle) == ASTRA_VFS_OK);
    assert(!astra_vfs_port_client_closed(&remote));
    astra_vfs_port_fail(&remote);
    assert(astra_vfs_port_client_closed(&remote));
    served = NULL;
}
```

Expected: compile errors, then the two port assertions failing.

- [ ] **Step 2: Status and wire**

`status.h`, after `ASTRA_STATUS_ESCAPED`:

```c
/* The object, session or service a request named was lost with its
 * provider (restart or reset); reopen through a fresh grant. */
#define ASTRA_STATUS_LOST            21
```

`vfs_service.h`: `#define ASTRA_VFS_ERR_LOST ((uint32_t)ASTRA_STATUS_LOST)`
with the comment "The file, scope or service was lost with its provider;
old handles stay lost, and new opens may succeed". This adds a status value
and changes no layout or operation, so there is no version bump. A
version-32 client that sees 21 maps it to `EIO` through its default case.

- [ ] **Step 3: The provider**

`astra68.c`. Add to `AstraHostState`:

```c
    uint32_t fs_lost_owner;  /* the claimant at the last fs reset, until it
                                claims again */
    uint32_t fs_epoch;       /* advances at every fs reset; CLAIM answers it */
    uint64_t fs_resets;
    GHashTable *lost_handles;/* handle -> owner: disconnected, not yet closed */
```

`astra68_init` creates `lost_handles` beside `files`
(`g_hash_table_new(g_direct_hash, g_direct_equal)`) and sets
`s->host.fs_epoch = 1u;`. `AstraHostChannelScope` gains `bool live;`.
Task 3b's `astra_host_channel_rebind` keeps a detached scope as a
tombstone (`live = false`) rather than removing it. An ATTACH reuses a
tombstone's slot when no free slot is left. `astra_host_channel_scope`
copies tombstones out too, so the caller can tell *revoked* (LOST) from
*never bound* (ACCESS).

```c
/*
 * The filesystem provider loses its state, as a restarted Linux provider
 * would. Every session goes, and the claim with them. Every handle is
 * disconnected (Haiku's O_DISCONNECTED): its descriptor closes now, and it
 * answers LOST until its owner closes it. The owner that held the claim is
 * told LOST, not ACCESS, until it claims again.
 */
static void astra_host_fs_reset(Astra68State *s)
{
    uint32_t owner = qatomic_xchg(&s->host.fs_owner, 0u);
    GHashTableIter iterator;
    gpointer key;
    gpointer value;

    if (owner != 0u)
        qatomic_set(&s->host.fs_lost_owner, owner);
    qemu_mutex_lock(&s->host.files_lock);
    g_hash_table_iter_init(&iterator, s->host.files);
    while (g_hash_table_iter_next(&iterator, &key, &value)) {
        AstraHostFile *file = value;

        g_hash_table_insert(s->host.lost_handles, key,
                            GUINT_TO_POINTER(file->owner));
    }
    qemu_mutex_unlock(&s->host.files_lock);
    astra_host_release_files(s, 0);
    astra_host_release_sessions(s, 0u);
    qatomic_inc(&s->host.fs_epoch);
    qatomic_inc(&s->host.fs_resets);
}

/* A handle `owner` held when the provider was reset. */
static bool astra_host_handle_lost(Astra68State *s, uint32_t owner,
                                   uint32_t handle)
{
    bool lost;

    qemu_mutex_lock(&s->host.files_lock);
    lost = GPOINTER_TO_UINT(g_hash_table_lookup(
               s->host.lost_handles, GUINT_TO_POINTER(handle))) == owner;
    qemu_mutex_unlock(&s->host.files_lock);
    return lost;
}

/* CLOSE of a disconnected handle: forget it. */
static bool astra_host_forget_lost(Astra68State *s, uint32_t owner,
                                   uint32_t handle)
{
    bool forgotten = false;

    qemu_mutex_lock(&s->host.files_lock);
    if (GPOINTER_TO_UINT(g_hash_table_lookup(
            s->host.lost_handles, GUINT_TO_POINTER(handle))) == owner)
        forgotten = g_hash_table_remove(s->host.lost_handles,
                                        GUINT_TO_POINTER(handle));
    qemu_mutex_unlock(&s->host.files_lock);
    return forgotten;
}

static void astra_host_fs_reset_set(Object *object, Visitor *visitor,
                                    const char *name, void *opaque,
                                    Error **errp)
{
    bool value;

    if (!visit_type_bool(visitor, name, &value, errp) || !value)
        return;
    astra_host_fs_reset(opaque);
}
```

Handle numbers come from `next_handle`, which a reset does not rewind
(`astra68.c:3268`). So a lost number is not reused until the counter wraps.
`astra_host_release_owner` removes the owner's entries from `lost_handles`,
and `astra_host_reset` clears the table.

Registered beside the other host properties in `astra68_init`:

```c
    object_property_add(OBJECT(machine), "astra-host-fs-reset", "bool",
                        NULL, astra_host_fs_reset_set, NULL, s);
    object_property_add_uint64_ptr(OBJECT(machine), "astra-host-fs-resets",
                                   &s->host.fs_resets, OBJ_PROP_FLAG_READ);
```

In `astra_host_execute_fs`:
- The unbound claim check (Task 4 Step 4b) becomes:

  ```c
          if (owner == 0u || owner != qatomic_read(&s->host.fs_owner)) {
              status = owner != 0u &&
                       owner == qatomic_read(&s->host.fs_lost_owner) ?
                  ASTRA_STATUS_LOST : ASTRA_STATUS_ACCESS;
              goto done;
          }
  ```

- On a bound channel, a tombstoned scope answers LOST, while a word never
  bound answers ACCESS. When the session cannot be acquired, the answer
  is LOST as well. A bound session vanishes only through a provider reset,
  a retired grant or HostFS's death, and to the client each is a loss:

  ```c
          if (!astra_host_channel_scope(s, channel, session_id, &scope)) {
              status = ASTRA_STATUS_ACCESS;
              goto done;
          }
          if (!scope.live || scope.owner != qatomic_read(&s->host.fs_owner)) {
              status = ASTRA_STATUS_LOST;
              goto done;
          }
          ...
      session = astra_host_session_acquire(s, session_owner, session_id);
      if (session == NULL) {
          status = bound ? ASTRA_STATUS_LOST : ASTRA_STATUS_BAD_HANDLE;
          goto done;
      }
  ```

- CLOSE: when `astra_host_close_file` fails and
  `astra_host_forget_lost(s, owner, handle)` succeeds, answer OK.
- At `done:`, before the status is stored:

  ```c
      if (status == ASTRA_STATUS_BAD_HANDLE && handle != 0u &&
          astra_host_handle_lost(s, owner, handle))
          status = ASTRA_STATUS_LOST;
  ```

`astra_host_fs_claim`'s success path runs
`qatomic_cmpxchg(&s->host.fs_lost_owner, owner, 0u);` and answers the epoch:
`stl_be_p(command + HOST_FIELD(result_value), qatomic_read(&s->host.fs_epoch));`.
It gains `command` as a parameter. `astra_host_release_owner` clears
`fs_lost_owner` the same way, and so does `astra_host_reset`
(`qatomic_set(&s->host.fs_lost_owner, 0u)`), because a reset machine has no
survivors to tell.

The property is a test seam, like the existing read-write
`astra-host-execution-max-ns`. It is reachable only through QMP on the host
side, which the board's launcher binds to a root-owned socket
(`run-arty.sh:50`). The guest cannot reach it.

A CLOSE of a disconnected handle answers OK *before* the session is
looked up, because its session is gone too. In the prologue, ahead of the
session acquisition, add:

```c
    if (operation == ASTRA_HOST_FS_CLOSE &&
        astra_host_forget_lost(s, owner, handle)) {
        status = ASTRA_STATUS_OK;
        goto done;
    }
```

`test-hostfs.py` gains a `loss` check. The first boot also gets a QMP
socket (`-qmp unix:<temp>/qmp.sock,server=on,wait=off`), because qtest
cannot set a QOM property. `loss(qtest, root, qmp_path)` runs after
`scopes`:

```python
STATUS_LOST = 21


def qmp_set(path, prop, value):
    """One qom-set on /machine over the QMP socket."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.connect(path)
        stream = sock.makefile("rw")
        json.loads(stream.readline())
        for command in ({"execute": "qmp_capabilities"},
                        {"execute": "qom-set",
                         "arguments": {"path": "/machine", "property": prop,
                                       "value": value}}):
            stream.write(json.dumps(command) + "\n")
            stream.flush()
            reply = json.loads(stream.readline())
            while "return" not in reply and "error" not in reply:
                reply = json.loads(stream.readline())
            assert "return" in reply, reply


def loss(qtest, root, qmp_path):
    """After a provider reset: the claimant hears LOST until it claims again,
    and its new claim answers a new epoch; a held handle answers LOST until
    closed; a bound channel's session answers LOST; a stranger still hears
    ACCESS."""
    generation = qtest.read32(HACC_GENERATION)
    claimed = execute(qtest, [Command(generation, FS_CLAIM)])[0]
    assert status(claimed) == STATUS_OK
    epoch = get32(claimed.raw, 56)
    work = open_session(qtest, generation)
    with open(os.path.join(root, "work", "lost-file"), "wb") as out:
        out.write(b"lost")
    opened = execute(qtest, [Command(generation, FS_OPEN, "/lost-file",
                                     flags=OPEN_READ, session=work)])[0]
    assert status(opened) == STATUS_OK
    handle = get32(opened.raw, 16)
    assert configure_scope(qtest, generation, CONFIG_OPEN, 12, 51, work,
                           SCOPE_BOUND, 0x1001,
                           byte_size=8192 + 4096) == 0
    qmp_set(qmp_path, "astra-host-fs-reset", True)
    assert status(execute(qtest, [Command(
        generation, FS_STAT, "/", session=work)])[0]) == STATUS_LOST
    assert status(execute(qtest, [Command(
        generation, FS_STAT, "/", session=work)], owner=0x2002)[0]) == \
        STATUS_ACCESS
    bound = scoped(qtest, 12, 1, Command(generation, FS_STAT, "/",
                                         session=work))
    assert status(bound) == STATUS_LOST
    again = execute(qtest, [Command(generation, FS_CLAIM)])[0]
    assert status(again) == STATUS_OK and get32(again.raw, 56) == epoch + 1
    assert status(execute(qtest, [Command(
        generation, FS_READ, handle=handle, capacity=4,
        session=work)])[0]) == STATUS_LOST
    assert status(execute(qtest, [Command(
        generation, FS_CLOSE, handle=handle, session=work)])[0]) == STATUS_OK
    assert status(execute(qtest, [Command(
        generation, FS_CLOSE, handle=handle, session=work)])[0]) == \
        STATUS_BAD_HANDLE
    assert configure_scope(qtest, generation, CONFIG_CLOSE, 12, 51) == 0
    print("PASS loss")
```

Add `import json`.

- [ ] **Step 4: The backend marks itself lost**

`vfs_host_backend.h`: `AstraVfsHostBackend` gains

```c
    /* A path operation answered LOST: the scope's session is gone. The lane
     * fails its client on next use, and the client reconnects. */
    volatile uint32_t lost;
```

`astra_vfs_host_init` zeroes it. `host.c` `submit`, where it returns the
command's status:

```c
    if (status == ASTRA_VFS_ERR_LOST && request->command->handle == 0u)
        backend->lost = 1u;
```

A handle operation that answers LOST names a disconnected file, and the
session may still be fine.

`astra_vfs_host_claim` gains `uint32_t *epoch` (NULL: unwanted). It is
`control(..., ASTRA_HOST_FS_CLAIM, ..., epoch)`, because `control` already
answers `result_value`.

- [ ] **Step 5: The lane fails and reconnects**

`sw/userspace/vfs/src/vfs_port_transport.c`. Export the two lifecycle
facts, and declare them in `vfs_port_transport.h`:

```c
/* Fail the client: in-flight calls finish with PEER, the lane is
 * abandoned, and the client becomes closed (see port_client_fail). */
void astra_vfs_port_fail(AstraVfsClient *client)
{
    if (client != NULL)
        port_client_fail(client);
}

int astra_vfs_port_client_closed(const AstraVfsClient *client)
{
    return client != NULL &&
           __atomic_load_n(&client->port_lifecycle, __ATOMIC_ACQUIRE) ==
               PORT_CLIENT_CLOSED;
}
```

`sw/userspace/vfs/src/vfs_host_direct.c`. The lane's enter hook notices a
lost scope before it runs anything:

```c
/* Entry to every lane operation. A scope the provider lost cannot be used
 * again: fail the client, answer LOST, and let the next use reconnect --
 * which says HELLO, which is where HostFS recovers (Task 10). */
static uint32_t lane_enter(AstraVfsClient *client)
{
    AstraVfsHostDirect *direct = client->port_direct_address;
    uint32_t status = astra_vfs_port_client_enter(client);

    if (status != ASTRA_VFS_OK || direct == NULL ||
        direct->backend.lost == 0u)
        return status;
    astra_vfs_port_client_leave(client);
    astra_vfs_port_fail(client);
    return ASTRA_VFS_ERR_LOST;
}
```

`client_bind` (Task 3b) sets `client->direct_backend_enter = lane_enter;`.
`astra_vfs_host_direct_transport` and `astra_vfs_host_direct_bulk` start
with:

```c
    if (direct->backend.lost != 0u) {
        astra_vfs_port_fail(client);
        return ASTRA_VFS_ERR_LOST;
    }
```

`sw/userspace/vfs/src/vfs_process.c`, `client_ready`: before the existing
connect, a closed client is reconnected on the same grant handle:

```c
    if (clients[slot].connected != 0u &&
        astra_vfs_port_client_closed(&clients[slot].client)) {
        astra_vfs_port_abandon(&clients[slot].client);
        clients[slot].connected = 0u;
    }
```

`vfs_client.c` `exchange_request`, where the transport's status is
returned:

```c
    status = client->transport(client->context, operation, request, reply);
    /* The transport's PEER is the caller's LOST: the service holding this
     * session is gone, and nothing reached through it comes back (FS35). */
    if (status == ASTRA_VFS_ERR_PEER)
        return ASTRA_VFS_ERR_LOST;
    if (status != ASTRA_VFS_OK)
        return status;
```

`vfs_port_transport.c`: the public data-plane functions that answer the
caller directly return through `caller_status`:
- `astra_vfs_port_read_bulk_position`;
- `astra_vfs_port_read_bulk`;
- `astra_vfs_port_write_bulk`;
- any other `astra_vfs_port_*` entry point listed in
  `vfs_port_transport.h` that returns a filesystem status to a Kit caller.

```c
static uint32_t caller_status(uint32_t status)
{
    return status == ASTRA_VFS_ERR_PEER ? ASTRA_VFS_ERR_LOST : status;
}
```

Find them with `grep -n "^astra_vfs_port_" sw/userspace/vfs/src/vfs_port_transport.c`.
Leave `astra_vfs_port_transport` and the service-side pump alone. The
lane's fast paths in `vfs_client.c` return the backend's status unchanged,
and the backend already says LOST.

`posix/src/file.c` `posix_errno`: `case ASTRA_VFS_ERR_LOST: return ESTALE;`.

- [ ] **Step 6: HostFS recovers**

`HostfsGrant` gains:

```c
    uint32_t volume;                          /* ASTRA_HOST_FS_VOLUME_* */
    uint16_t session_flags;                   /* READ_ONLY or 0 */
    char root[ASTRA_VFS_PATH_MAX + 1u];       /* absolute in the volume */
```

`grant_add` gains `uint32_t volume, const char *root, uint16_t session_flags`
and records them. The base grants pass `"/"`. `hostfs_derive` passes:
- the parent's `volume`;
- the joined root: `from->root`, then `/`, then `root`, with no doubled
  slash when the parent is `/`; longer than `ASTRA_VFS_PATH_MAX` is
  `ASTRA_VFS_ERR_INVALID`;
- the flags it opened the session with.

Then:

```c
static uint32_t provider_epoch;   /* the claim's answer for the live sessions */

/* Every grant's scope flags, from its rights (Task 5's grant_add). */
static uint32_t grant_scope_flags(const HostfsGrant *grant)
{
    return ((grant->port.rights & ASTRA_VFS_GRANT_WRITE) == 0u ?
                ASTRA_HOST_SCOPE_READ_ONLY : 0u) |
           ((grant->port.rights & ASTRA_VFS_GRANT_DELEGATE) != 0u ?
                ASTRA_HOST_SCOPE_DELEGATE : 0u);
}

/*
 * Before any control message: has the provider lost everything (FS35)?
 * The claim answers its epoch. If it moved, reopen every grant's session
 * where the grant says it is rooted, revoke the grant's old scope -- the
 * kernel detaches it from every channel, which then answers LOST -- and
 * mint a new one over the new session for the next HELLO to hand out.
 */
static void hostfs_check_provider(void)
{
    uint32_t epoch = 0u;

    if (astra_vfs_host_claim(&transport, transport.generation, 0, &epoch) !=
            ASTRA_VFS_OK ||
        epoch == provider_epoch)
        return;
    for (uint32_t index = 0u; index < grant_capacity; ++index) {
        HostfsGrant *grant = &grants[index];
        uint32_t session = 0u;
        uint32_t scope = 0u;

        if (grant->port.receive == 0u)
            continue;
        (void)astra_host_scope_revoke(grant->port.scope);
        (void)astra_close(grant->port.scope);
        if (astra_vfs_host_session_open(
                &transport, transport.generation, 0u, grant->volume,
                grant->root, grant->session_flags, &session) !=
                ASTRA_VFS_OK ||
            astra_host_scope_create(device_handle, session,
                                    grant_scope_flags(grant), &scope) !=
                ASTRA_SYSCALL_OK) {
            ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_FILESYSTEM,
                         ASTRA_EVENT_LEVEL_WARNING,
                         "grant %u root gone after provider loss",
                         grant->port.id);
            scope = 0u;
        }
        /* 0: HELLO attaches nothing, and the Kit answers LOST. */
        grant->session = session;
        grant->port.scope = scope;
    }
    provider_epoch = epoch;
    ASTRA_EVENT0(ASTRA_EVENT_SUBSYSTEM_FILESYSTEM, ASTRA_EVENT_LEVEL_NOTICE,
                 "hostfs recovered from provider loss");
}
```

- Startup records `provider_epoch` from its first claim (Task 4's
  `astra_vfs_host_claim` call gains `&provider_epoch`).
- The pump calls `hostfs_check_provider();` before each grant pump. Every
  control message (HELLO, BYE, GRANT_DERIVE) costs one CLAIM command;
  no data operation pays it.
- Task 5's HELLO attachment already skips a grant whose `scope` is 0.
- On the Kit side, a HELLO on a grant (`ASTRA_VFS_FEATURE_GRANTS`) that
  attaches no scope leaves the client without a lane. The client answers
  `ASTRA_VFS_ERR_LOST` for every data operation: in
  `astra_vfs_port_transport`, a lane client (`port_accelerator_ops != NULL`)
  whose `port_direct_address` is NULL after its HELLO fails with LOST.
- Check the event subsystem constant's name in `sw/include/astra/event.h`
  (`grep -n "ASTRA_EVENT_SUBSYSTEM_" sw/include/astra/event.h`), and use
  the filesystem one or the closest one listed there.

A detached scope is now a tombstone that answers LOST, so three earlier
checks change their expectation from `ACCESS` to `LOST`:
- Task 3b's `scopes` (`status(detached)`);
- `scope_revocation` (the second command and `later`);
- Task 5's `raw-retire`. Update its comment too.

A word that was never bound still answers `ACCESS` (`scopes`' forged
volume or session).

- [ ] **Step 7: The supervisor restarts the holders of a republished name**

The decision of 2026-10-07: after a HostFS restart, the supervisor restarts
its managed services that hold HostFS grants. Their old scopes died with
HostFS, so only a restart gives them the new grants.

`sw/userspace/supervisor/src/loader.c`:
- The process record gains
  `const SupervisorManifestEntry *entry;  /* the manifest line it came from; NULL for an application */`.
  It is set where the record is filled at launch (`loader.c:1536-1556`,
  `process_table.records[process_count].critical = entry->critical;`).
  Applications launched from bundles leave it NULL.
- A list of names whose provider died, beside `paused_services`:

```c
/* Namespace names whose provider died while published: when a provider
 * publishes one of them again, the managed services granted it restart, so
 * they hold the new provider's grant (decision 2026-10-07). */
static char orphaned_names[SUPERVISOR_MANIFEST_PUBLICATION_MAX * 4u]
                          [ASTRA_CAPABILITY_NAME_MAX];
static uint32_t orphaned_count;

static void orphan_names_of(const char *owner)
{
    for (uint32_t index = 0u; index < service_count; ++index)
        if (service_is_vfs[index] != 0u &&
            strcmp(service_owner_names[index], owner) == 0 &&
            orphaned_count < sizeof(orphaned_names) /
                                 sizeof(orphaned_names[0]))
            astra_capability_name_set(orphaned_names[orphaned_count++],
                                      service_names[index]);
}

static void restart_grantees(const char *name)
{
    for (uint32_t at = 0u; at < orphaned_count; ++at) {
        if (!astra_capability_name_equal(orphaned_names[at], name))
            continue;
        (void)memcpy(orphaned_names[at], orphaned_names[--orphaned_count],
                     sizeof(orphaned_names[at]));
        for (uint32_t slot = 0u; slot < process_count; ++slot) {
            const SupervisorManifestEntry *entry =
                process_table.records[slot].entry;

            if (entry != NULL &&
                process_table.records[slot].service_name[0] != '\0' &&
                process_table.records[slot].action ==
                    SUPERVISOR_PROCESS_ACTION_NONE &&
                entry_grants(entry, name)) {
                restart_log("service restart for republished ", name);
                (void)stop_process_slot(slot,
                                        SUPERVISOR_PROCESS_ACTION_RESTART);
            }
        }
        return;
    }
}
```

- In the exit handler (`loader.c:2950`), call
  `orphan_names_of(service_name);` before `unpublish_owner(service_name);`,
  but only when the process died unasked
  (`action == SUPERVISOR_PROCESS_ACTION_NONE`). A service the user stopped
  or restarted is not a provider loss.
- At the end of a successful VFS `publish` (after `++service_count`), call
  `restart_grantees(publication->name);`.

`stop_process_slot` with `SUPERVISOR_PROCESS_ACTION_RESTART` is the path
`service restart` takes (`loader.c:1921`). The exit handler relaunches the
service when it is gone. A critical holder restarted this way is not a
system failure, because its action is not NONE.

- [ ] **Step 8: The probe and the gate checks**

`fssem.c`, before the case loop in `main`:

```c
    if (argc == 5 && strcmp(argv[1], "--loss-probe") == 0)
        return loss_probe(argv[2], atoi(argv[3]),
                          strcmp(argv[4], "reopen") == 0);
```

and:

```c
static int loss_fail(const char *what, int error)
{
    printf("FSSEM LOSS FAIL %s errno=%d\n", what, error);
    return 1;
}

/*
 * Writes to PATH until the provider is lost under it, then checks the
 * answer is the defined one: ESTALE, for the write and for the descriptor
 * afterwards. With `reopen`, a fresh open through the same namespace must
 * work (HostFS recovered); without, it must also be ESTALE (HostFS died,
 * and this process's grant died with it). Always ends with one verdict
 * line: PASS, FAIL or none.
 */
static int loss_probe(const char *path, int seconds, int reopen)
{
    char again[200];
    struct stat st;
    time_t end;
    long writes = 0;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd < 0)
        return loss_fail("open", errno);
    printf("FSSEM LOSS armed\n");
    fflush(stdout);
    end = time(NULL) + seconds;
    while (time(NULL) < end) {
        if (write(fd, "0123456789abcdef", 16) == 16) {
            ++writes;
            continue;
        }
        if (errno != ESTALE)
            return loss_fail("write", errno);
        if (fstat(fd, &st) == 0 || errno != ESTALE)
            return loss_fail("descriptor did not stay lost", errno);
        (void)close(fd);
        (void)snprintf(again, sizeof(again), "%s.after", path);
        fd = open(again, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (reopen ? fd < 0 : !(fd < 0 && errno == ESTALE))
            return loss_fail("reopen", fd < 0 ? errno : 0);
        if (fd >= 0)
            (void)close(fd);
        printf("FSSEM LOSS PASS after=%ld\n", writes);
        return 0;
    }
    printf("FSSEM LOSS none after=%ld\n", writes);
    return 2;
}
```

Add `#include <time.h>`. The write that first sees the loss answers LOST
from the provider (a disconnected handle). The `fstat` answers LOST from
the lane, which fails its client then. The `open` reconnects the client:
its HELLO is where HostFS recovers, and the new scope serves the open.

`test-fs-semantics.py`. The gate's machine runs HostFS as a restartable
user service, so the kill check can end it. A required service refuses
`kill` (`test-terminal.py:376-377`), and `test-events.py:69-71` does the
same for the events service:

```python
# HostFS as a user service, so `kill` may end it (hostfs-kill).
MANIFEST = astra_image.STARTUP_MANIFEST.replace(
    "serves WORK:rw METRICS:r SOUND:rw required priority=20",
    "serves WORK:rw METRICS:r SOUND:rw priority=20")
# Checks that leave the machine changed run after all the others.
FINAL_CHECKS = ("hostfs-kill",)
```

In `main`, after `install_test_command`:

```python
        if MANIFEST == astra_image.STARTUP_MANIFEST:
            fail("the hostfs manifest line changed: update MANIFEST")
        manifest = os.path.join(work, "startup")
        with open(manifest, "w", encoding="ascii", newline="\n") as out:
            out.write(MANIFEST)
        astra_image._replace_volume_file(image, manifest, "/startup/system")
```

and the check loop becomes
`for name in sorted(arguments.check or CHECKS, key=lambda name: (name in FINAL_CHECKS, name)):`.

```python
VERDICTS = ("FSSEM LOSS PASS", "FSSEM LOSS FAIL", "FSSEM LOSS none")


def wait_line(machine, before, needles, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        lines, _ = machine.said(before)
        if any(needle in line for line in lines for needle in needles):
            return lines
        time.sleep(0.2)
    fail("never saw %r: %r" % (needles, machine.said(before)[0][-20:]))


def loss(machine, mode, act):
    """The probe writes in the background; `act` loses the provider under
    it; the probe's verdict decides."""
    machine.settle()
    before = machine.sequence()
    machine.qmp.type_line("fssem --loss-probe /work/loss-%s 120 %s &"
                          % (mode, mode))
    wait_line(machine, before, ("FSSEM LOSS armed",) + VERDICTS, 60)
    machine.settle()
    act()
    lines = wait_line(machine, before, VERDICTS, 180)
    if not any(line.startswith("FSSEM LOSS PASS") for line in lines):
        fail("loss probe (%s): %r" % (mode, lines[-10:]))


def check_provider_reset(machine, mount):
    def reset():
        machine.qmp.execute("qom-set", {"path": "/machine",
                                        "property": "astra-host-fs-reset",
                                        "value": True})
    loss(machine, "reopen", reset)
    # The same terminal's namespace still works: the grant survived.
    lines, status = command(machine, "fssem basic", 120, 31)
    if status != 0:
        fail("fssem after provider reset: %r" % lines[-10:])
    print("PASS provider-reset")


# Ends HostFS the way a fault would (test-events.py:58-63).
KILL_HOSTFS = ("ps | while read -r id rest; do "
               "[[ $rest == *' /services/hostfs' ]] && "
               "print kill > /proc/$id/ctl; done")


def sample_mtime(mount):
    try:
        return os.stat(os.path.join(mount, "work", ".astra", "sample")).st_mtime
    except OSError:
        return 0.0


def check_hostfs_kill(machine, mount):
    """HostFS dies under a writer. The writer gets ESTALE; the supervisor
    restarts HostFS and the managed services that hold its grants (the
    sampler writes WORK again); a terminal opened afterwards sees /work."""
    killed_at = time.time()
    loss(machine, "dead", lambda: machine.qmp.type_line(KILL_HOSTFS))
    deadline = time.monotonic() + 60
    while sample_mtime(mount) <= killed_at + 1.0:
        if time.monotonic() > deadline:
            fail("the sampler never wrote WORK after the HostFS restart")
        time.sleep(0.5)
    before = machine.sequence()
    machine.qmp.double_click(*machine.desktop_icon())
    if machine.wait_for_text(terminal.BANNER, 60, before)[0] is None or \
            not machine.wait_for_ready(60, before):
        fail("no second terminal after the HostFS restart")
    lines, status = command(
        machine, "print -r -- alive > /work/after-kill && "
                 "cat /work/after-kill", 60, 41)
    if status != 0 or "alive" not in lines:
        fail("/work after the HostFS restart: %r" % lines[-10:])
    print("PASS hostfs-kill")


CHECKS["provider-reset"] = check_provider_reset
CHECKS["hostfs-kill"] = check_hostfs_kill
```

The probe runs in the background, so the kill line lands at a prompt.
Confirm that `terminal.BANNER` and `machine.wait_for_ready` are the names
`open_terminal` uses (`test-terminal.py:942-980`). Typed text goes to the
focused window, which is the new terminal after the double click. The
sampler copies the machine's accounting to `WORK:.astra/sample` once a
second (`astra_image.py`, the manifest comment), so its file's mtime is
the evidence that it was restarted with a live grant.

- [ ] **Step 9: Run, perturb, full suite, commit**

Sync, then build and test userspace, build `sw/boot`, run
`emu/qemu/build.sh host`, rebuild images, and run `test-hostfs.py` and
`test-fs-semantics.py`. Expected: `PASS loss`,
`PASS provider-reset` and `PASS hostfs-kill`.

Perturb. Restore and re-sync after each.

1. **QEMU forgets the claimant.** Answer `ACCESS` for the lost owner, as
   before this task. Expected: HostFS's epoch check still sees the new
   epoch, but `test-hostfs.py`'s `loss` fails at the `0x1001`
   `STATUS_LOST` assertion.
2. **Handles are freed rather than disconnected.** Skip filling
   `lost_handles` in `astra_host_fs_reset`. Expected: the probe's write
   answers `EBADF`, the probe prints `FSSEM LOSS FAIL write errno=9`, and
   `provider-reset` fails.
3. **The lane never fails.** Make `lane_enter` ignore `backend.lost`.
   Expected: the reopen keeps the dead scope, the probe prints
   `FSSEM LOSS FAIL reopen errno=116` (`ESTALE`), and `provider-reset`
   fails.
4. **No re-mint.** In `hostfs_check_provider`, keep the old scope. Expected:
   `provider-reset` fails the same way: the HELLO hands out a revoked
   scope.
5. **Kit says PEER.** Remove the PEER-to-LOST mapping in
   `exchange_request`. Expected: the probe prints
   `FSSEM LOSS FAIL reopen errno=5` (EIO) after the kill, `hostfs-kill`
   fails, and the port test's two assertions fail too.
6. **No holder restart.** Skip `restart_grantees` in `publish`. Expected:
   `hostfs-kill` fails "the sampler never wrote WORK after the HostFS
   restart".

`~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-task10.log`. Expected: green.

```bash
git add sw/include/astra emu/qemu/qemu-9.2/hw/m68k/astra68.c sw/userspace/vfs \
  sw/userspace/services/hostfs/main.c sw/userspace/supervisor \
  sw/userspace/posix/src/file.c sw/userspace/commands/fssem/fssem.c \
  emu/qemu/test-fs-semantics.py emu/qemu/test-hostfs.py
git commit -m "feat(fs): provider loss is one answer, LOST/ESTALE; lanes reconnect, HostFS re-mints, holders restart (FS35)"
```

---

### Task 11: Host budgets and admission (D9)

Measure what each provider object costs the host. Then bound by
configuration every pool that can grow when a guest asks (FS37, FS38). With
the lane, HostFS is off the data path, so the per-process limits move to
the code that holds the objects: QEMU, keyed by the scope the kernel bound.

| Resource | Owner | Bound | Configured by |
|---|---|---|---|
| Linux descriptors held for guests (sessions, handles, shares) | QEMU | `ASTRA_HOSTFS_MAX_DESCRIPTORS` | the launcher's environment; default: QEMU's own `RLIMIT_NOFILE` less 256 for QEMU itself |
| Directory streams (each also a `DIR` buffer) | QEMU | `ASTRA_HOSTFS_MAX_DIRECTORIES` | the launcher; default: the descriptor bound |
| Open handles and shares per process, per scope | QEMU, from the scope the kernel bound | `owner_files` | `services/hostfs/settings.conf`; stamped into each grant's scope (`HOST_SCOPE_LIMIT`) |
| HostFS sessions per process | HostFS (control plane) | `owner_sessions` | same file |
| Queued control requests per grant | HostFS (its grant port) | `queue_messages` | same file, at most the kernel's `ASTRA_PORT_MESSAGES_MAX` |
| Grants | HostFS | `grants` (Task 5) | same file |
| Lanes per process (one DMA buffer each) | the kernel | `KERNEL_PROCESS_DMA_BUFFER_MAX` (16) within the 32 MiB DMA window | compiled: a kernel table, sized to the thread slots |

No value is compiled into an image except the kernel's table size. The
defaults come from the host's own limit, and the board's launcher sets the
measured ones (Step 8). `owner_files 1024` is Linux's default soft
`RLIMIT_NOFILE`, the per-process expectation POSIX programs are written
against. `owner_sessions 64` allows one session per thread slot with room
for library clients. Check `ASTRA_PROCESS_THREAD_SLOT_COUNT`, and raise it
if the slot count is larger.

**The lane cap.** Before this task a process could hold four DMA buffers:
`KernelProcess.dma_buffers[KERNEL_VM_DMA_SLOT_COUNT]`, one 8 MiB window slot
each. Every thread that touches a host volume needs a lane, which is one
DMA buffer, so the fifth such thread failed with `VFS host DMA create:5`.
That is what capped HostFS at four workers in `62b72fc2`. The window stays
32 MiB and a buffer stays at most one slot (8 MiB, the static assert on a
native frame). The kernel now places buffers anywhere in the window by
page, and keeps up to `KERNEL_PROCESS_DMA_BUFFER_MAX` (16) of them: one per
thread slot.

**Files:**
- Modify: `sw/kernel/process.c` (DMA records, `HOST_SCOPE_LIMIT`, scope `files`), `sw/kernel/platform.c`, `sw/kernel/platform.h` (config word), `sw/include/astra/syscall.h` (112), `sw/include/astra/host.h` (config version 3, `scope_files`), `sw/include/astra/address_space.h` (`ASTRA_DMA_BUFFER_MAX`)
- Test: `sw/kernel/tests/test_process.c` (`test_dma_buffers_fill_the_window`, `test_host_scope` extended)
- Modify: `sw/userspace/runtime/src/syscall.c`, `sw/userspace/runtime/runtime.library.exports`, `ndk/include/astra/runtime.h`, runtime 1.15 (the files of Task 3b Step 6)
- Modify: `emu/qemu/qemu-9.2/hw/m68k/astra68.c` (admission, per-scope file limit, gauges)
- Modify: `sw/userspace/vfs/include/astra/vfs_service_core.h`, `sw/userspace/vfs/src/vfs_service_core.c` (HELLO sessions per owner)
- Modify: `sw/userspace/services/hostfs/main.c` (settings), `emu/qemu/astra_image.py` (settings values)
- Modify: `sw/userspace/commands/fsbench/fsbench.c` (`--hold`), `sw/userspace/commands/fssem/fssem.c` (`--hold-sessions`, `--flood`, `many-lanes`)
- Modify: `emu/qemu/fs-baseline.py` (`--budget`), `emu/qemu/test-fs-semantics.py` (`budgets`, `flood`), `emu/qemu/test-hostfs.py` (`limits`)
- Modify: `emu/qemu/run-arty.sh` (measured limits); `emu/qemu/test-run-arty.py` if it asserts the environment
- Test: `sw/userspace/vfs/tests/test_vfs_service_core.c`

**Interfaces:**
- Produces (kernel): `ASTRA_SYSCALL_HOST_SCOPE_LIMIT` (112). `data[1]` is
  a scope with ADMINISTER and `data[2]` the per-process handle limit (0:
  none). It is refused `ASTRA_SYSCALL_BUSY` once any channel carries the
  scope. `ASTRA_DMA_BUFFER_MAX 16u` DMA buffers per process.
- Produces (runtime 1.15): `uint32_t astra_host_scope_limit(uint32_t scope, uint32_t files)`.
- Produces (provider): channel configuration version 3 with
  `scope_files`. Environment `ASTRA_HOSTFS_MAX_DESCRIPTORS` and
  `ASTRA_HOSTFS_MAX_DIRECTORIES` take a positive decimal; anything else
  refuses to start. QOM gauges: `astra-host-fs-descriptors`,
  `astra-host-fs-directories`, `astra-host-rss-kib`, `astra-host-fds`.
  Refusal is `ASTRA_STATUS_LIMIT`.
- Produces (core): `int astra_vfs_service_set_owner_sessions(AstraVfsService *, uint32_t sessions)`
  (0: no limit). Refusals are counted in the existing, so far unused,
  `owner_session_quota_denied` (`vfs_service_core.h:83-84`).
- Produces (tools): `fsbench --hold file|dir COUNT`;
  `fssem --hold-sessions COUNT`; `fssem --flood DIR LIMIT`;
  `fs-baseline.py LABEL OUT.json --budget`.

- [ ] **Step 1: Failing kernel tests**

`sw/kernel/tests/test_process.c`:

```c
/*
 * A process holds up to ASTRA_DMA_BUFFER_MAX DMA buffers, placed by page in
 * its DMA window: one lane per thread slot (Task 11, the lane cap). The
 * window, not a slot count, bounds the bytes.
 */
static void test_dma_buffers_fill_the_window(void)
{
    const uint32_t user_info = KERNEL_PROCESS_STACK_TOP - 64u;
    KernelProcessBootstrapCapability capability = {0};
    KernelCpuContext *next = NULL;
    AstraDmaBufferInfo buffer;
    uint32_t bases[ASTRA_DMA_BUFFER_MAX];
    uint32_t handles[ASTRA_DMA_BUFFER_MAX];
    uint32_t process_id = 0u;

    loader_build_image();
    initialize_test();
    capability.name = ASTRA_CAPABILITY_HOST_DEVICE;
    capability.kind = KERNEL_PROCESS_BOOTSTRAP_DEVICE;
    capability.device_id = ASTRA_DEVICE_ID_HOST0;
    capability.rights = KERNEL_DEVICE_RIGHTS;
    assert(kernel_process_create_executable(loader_image, loader_image_size,
                                            &capability, 1u, &process_id) ==
           KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    for (uint32_t index = 0u; index < ASTRA_DMA_BUFFER_MAX; ++index) {
        assert(scope_call(ASTRA_SYSCALL_DMA_CREATE, 2u * KERNEL_PAGE_SIZE,
                          user_info, 0u, 0u, 0u, &next) == ASTRA_SYSCALL_OK);
        assert(kernel_user_copy_from_asm(&buffer, user_info,
                                         sizeof(buffer)) ==
               KERNEL_USER_COPY_OK);
        bases[index] = buffer.virtual_base;
        handles[index] = buffer.handle;
        assert(bases[index] >= ASTRA_DMA_ADDRESS_START &&
               bases[index] + 2u * KERNEL_PAGE_SIZE <= ASTRA_DMA_ADDRESS_END);
        for (uint32_t prior = 0u; prior < index; ++prior)
            assert(bases[index] + 2u * KERNEL_PAGE_SIZE <= bases[prior] ||
                   bases[prior] + 2u * KERNEL_PAGE_SIZE <= bases[index]);
    }
    assert(scope_call(ASTRA_SYSCALL_DMA_CREATE, KERNEL_PAGE_SIZE, user_info,
                      0u, 0u, 0u, &next) == ASTRA_SYSCALL_RESOURCE_LIMIT);
    /* A freed buffer's pages are placed again; a whole slot still fits. */
    for (uint32_t index = 0u; index < ASTRA_DMA_BUFFER_MAX; ++index)
        assert(scope_call(ASTRA_SYSCALL_CLOSE, handles[index], 0u, 0u, 0u,
                          0u, &next) == ASTRA_SYSCALL_OK);
    assert(scope_call(ASTRA_SYSCALL_DMA_CREATE, ASTRA_DMA_SLOT_SIZE,
                      user_info, 0u, 0u, 0u, &next) == ASTRA_SYSCALL_OK);
}
```

If the kernel memory fixture cannot back a whole 8 MiB slot, the last
create answers `ASTRA_SYSCALL_OUT_OF_MEMORY`. In that case assert a 1 MiB
buffer instead, and note it in the test's comment.

`test_host_scope` (Task 3b) gains, before the creator-death block:

```c
    /* The per-process handle limit rides the scope into every channel; it
     * is fixed before the first binding. */
    assert(scope_call(ASTRA_SYSCALL_HOST_SCOPE_CREATE, device,
                      ASTRA_HOST_FS_VOLUME_SOUND, 0u, 0u, 0u, &next) ==
           ASTRA_SYSCALL_OK);
    limited = next->data[1];
    assert(scope_call(ASTRA_SYSCALL_HOST_SCOPE_LIMIT, limited, 1024u, 0u, 0u,
                      0u, &next) == ASTRA_SYSCALL_OK);
    assert(scope_call(ASTRA_SYSCALL_HOST_SCOPE_ATTACH, limited, 0u, 0u, 0u,
                      0u, &next) == ASTRA_SYSCALL_OK);
    assert(host_channel_attach_files == 1024u);
    assert(scope_call(ASTRA_SYSCALL_HOST_SCOPE_LIMIT, limited, 8u, 0u, 0u,
                      0u, &next) == ASTRA_SYSCALL_BUSY);
    assert(scope_call(ASTRA_SYSCALL_HOST_SCOPE_LIMIT, work_client, 8u, 0u,
                      0u, 0u, &next) == ASTRA_SYSCALL_ACCESS_DENIED);
```

with `uint32_t limited;` declared and the attach fake recording
`host_channel_attach_files` (its new parameter).
Register `test_dma_buffers_fill_the_window` after `test_host_scope`.

Run: `~/astra-mg/locked make -C sw/kernel clean && ~/astra-mg/locked make -C sw/kernel test`.
Expected: compile errors (`ASTRA_DMA_BUFFER_MAX`, `ASTRA_SYSCALL_HOST_SCOPE_LIMIT`).

- [ ] **Step 2: The kernel**

`sw/include/astra/address_space.h`, after `ASTRA_DMA_SLOT_COUNT`:

```c
/** DMA buffers one process may hold at once, each at most
 * ::ASTRA_DMA_SLOT_SIZE, placed by page within the DMA region: one host
 * lane per thread slot. */
#define ASTRA_DMA_BUFFER_MAX 16u
```

`sw/kernel/process.c`:
- `KernelProcess.dma_buffers[KERNEL_VM_DMA_SLOT_COUNT]` becomes
  `dma_buffers[ASTRA_DMA_BUFFER_MAX]`. Build the kernel host test:
  `_Static_assert(sizeof(KernelProcess) <= 4608u, ...)` is the check. If it
  fires, stop and report the measured size. Do not raise the bound without
  the user: the process table's memory budget is `docs/MEMORY_BUDGET.md`'s.
- `create_dma_buffer` finds a free record among the 16, and places the
  buffer with:

  ```c
  /* The lowest page-aligned base in the process's DMA window where
   * `page_count` pages overlap no live buffer; false when none. First fit,
   * at most sixteen buffers: the scan is the table. */
  static bool dma_window_place(const KernelProcess *process,
                               uint32_t page_count, uint32_t *base)
  {
      const uint32_t end = KERNEL_VM_DMA_BASE +
          KERNEL_VM_DMA_SLOT_COUNT * KERNEL_VM_DMA_SLOT_SIZE;
      const uint32_t bytes = page_count * KERNEL_PAGE_SIZE;
      uint32_t candidate = KERNEL_VM_DMA_BASE;
      bool moved = true;

      while (moved) {
          moved = false;
          if (candidate > end || bytes > end - candidate)
              return false;
          for (uint32_t index = 0u; index < ASTRA_DMA_BUFFER_MAX; ++index) {
              const KernelProcessDmaBuffer *buffer =
                  &process->dma_buffers[index];
              uint32_t buffer_end;

              if (buffer->active == 0u)
                  continue;
              buffer_end = buffer->virtual_base +
                           (uint32_t)buffer->page_count * KERNEL_PAGE_SIZE;
              if (candidate < buffer_end &&
                  buffer->virtual_base < candidate + bytes) {
                  candidate = buffer_end;
                  moved = true;
              }
          }
      }
      *base = candidate;
      return true;
  }
  ```

  Call it after the record search. No base means
  `KERNEL_PROCESS_RESOURCE_LIMIT`, before the engine allocates anything.
  Use its answer for `buffer->virtual_base` in place of
  `KERNEL_VM_DMA_BASE + (slot * KERNEL_VM_DMA_SLOT_SIZE)`. `buffer->slot`
  records the record index, as it did. Nothing else reads the slot as an
  address (`grep -n "dma_buffers\|KERNEL_VM_DMA_BASE" sw/kernel/*.c`).
- `KernelHostScope` gains `uint32_t files;  /* per-process handle limit; 0: none */`.
  `host_channel_attach` and the bound open pass `scope->files` to the
  platform as a new last argument. `kernel_platform_host_channel_open` and
  `_attach` gain `uint32_t scope_files`, written into the configuration's
  `scope_files`. The configuration version is 3.
- The new case, beside `HOST_SCOPE_REVOKE`:

  ```c
      case ASTRA_SYSCALL_HOST_SCOPE_LIMIT: {
          KernelHostScope *scope = NULL;
          KernelHandleStatus handle_status = kernel_handle_lookup(
              current->handles, thread->context.data[1],
              KERNEL_OBJECT_HOST_SCOPE, ASTRA_RIGHT_ADMINISTER,
              (void **)&scope);
          bool bound = false;

          if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
              handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
              result = ASTRA_SYSCALL_INVALID_HANDLE;
              break;
          }
          if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
              result = ASTRA_SYSCALL_ACCESS_DENIED;
              break;
          }
          if (handle_status != KERNEL_HANDLE_OK || scope == NULL)
              return KERNEL_PROCESS_CORRUPT;
          /* Fixed before the first binding, so every channel carrying the
           * scope carries the same limit. */
          for (uint32_t slot = 0u; slot < KERNEL_VM_HOST_CHANNEL_PAGE_COUNT &&
               !bound; ++slot)
              for (uint32_t at = 0u; at < ASTRA_HOST_CHANNEL_SCOPE_MAX; ++at)
                  if (host_channels[slot].active != 0u &&
                      host_channels[slot].scopes[at] ==
                          host_scope_index(scope)) {
                      bound = true;
                      break;
                  }
          if (scope->state != KERNEL_HOST_SCOPE_ACTIVE)
              result = ASTRA_SYSCALL_PEER_DEAD;
          else if (bound)
              result = ASTRA_SYSCALL_BUSY;
          else {
              scope->files = thread->context.data[2];
              result = ASTRA_SYSCALL_OK;
          }
          break;
      }
  ```

`sw/include/astra/syscall.h`: `ASTRA_SYSCALL_HOST_SCOPE_LIMIT 112` with a
doc comment from Interfaces. `host.h`: `ASTRA_HOST_CHANNEL_CONFIG_VERSION 3u`,
and `AstraHostChannelConfig`'s `reserved[4]` becomes
`uint32_t scope_files; uint32_t reserved[3];`. Runtime 1.15: add
`astra_host_scope_limit` beside Task 3b's three, the same way, with
`ASTRA_RUNTIME_1.15`. That covers the identity `1, 15`, the Makefile
check, the Runtime.kit manifest and Filesystem.kit's
`requires runtime.library 1 1.15.0`. Use `ASTRA_SYSCALL_BUSY` if the
status header names it so; otherwise use the closest "in use" status
there, and use it in both the kernel and the test.

Run the kernel tests again. Expected: PASS.

- [ ] **Step 3: HELLO sessions per owner, in the core**

`test_vfs_service_core.c`:

```c
static void
test_owner_sessions_are_limited(void)
{
    AstraVfsService service;
    AstraVfsRequest request;
    AstraVfsReply reply;

    service_start(&service);
    assert(astra_vfs_service_set_owner_sessions(&service, 2u));
    memset(&request, 0, sizeof(request));
    request.size = ASTRA_VFS_REQUEST_SIZE;
    request.version = ASTRA_VFS_VERSION;
    for (int index = 0; index < 2; ++index) {
        astra_vfs_service_dispatch_grant(&service, 7u, 1u, 0u,
                                         ASTRA_VFS_OP_HELLO, &request,
                                         &reply);
        assert(reply.status == ASTRA_VFS_OK);
    }
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, 0u,
                                     ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_LIMIT);
    /* Another owner is not charged for owner 7. */
    astra_vfs_service_dispatch_grant(&service, 8u, 1u, 0u,
                                     ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(service.stats.owner_session_quota_denied == 1u);
}
```

`vfs_service_core.h`: `AstraVfsService` gains `uint32_t owner_session_limit;`
and the declaration from Interfaces. `vfs_service_core.c`:

```c
int
astra_vfs_service_set_owner_sessions(AstraVfsService *service,
                                     uint32_t sessions)
{
    if (service == NULL)
        return 0;
    service->owner_session_limit = sessions;
    return 1;
}

/* Sessions one owner holds. Called with the state lock held.
 * ponytail: a scan of the session table per HELLO; keep a per-owner count
 * if a profile ever shows it. */
static uint32_t
owner_sessions(const AstraVfsService *service, uint32_t owner)
{
    uint32_t count = 0u;

    for (uint32_t index = 0u; index < service->session_capacity; ++index)
        if (service->sessions[index].id != ASTRA_VFS_SESSION_INVALID &&
            service->sessions[index].owner == owner)
            ++count;
    return count;
}
```

In the HELLO path, before a slot is claimed: when `owner_session_limit != 0`
and `owner != 0` and the owner already holds that many, answer
`ASTRA_VFS_ERR_LIMIT` and `++service->stats.owner_session_quota_denied`.
Use the session-in-use test and the capacity field HELLO itself uses, if
they are not `id == ASTRA_VFS_SESSION_INVALID` and `session_capacity`.

- [ ] **Step 4: HostFS settings**

Replace Task 5's `configured_grants` with one reader:

```c
/* services/hostfs/settings.conf: one positive integer `name N`, required,
 * within [minimum, maximum]. */
static uint32_t setting(AstraConfig *config, const char *name,
                        uint64_t minimum, uint64_t maximum, uint32_t *out)
{
    uint64_t value = 0u;
    uint32_t found = 0u;
    uint32_t status = astra_config_count(config, name, &found);

    if (status == ASTRA_CONFIG_OK && found == 1u)
        status = astra_config_get_u64(config, name, 0u, &value);
    if (status != ASTRA_CONFIG_OK || found != 1u || value < minimum ||
        value > maximum)
        return ASTRA_STATUS_INVALID;
    *out = (uint32_t)value;
    return ASTRA_STATUS_OK;
}

typedef struct HostfsSettings {
    uint32_t grants;
    uint32_t owner_files;
    uint32_t owner_sessions;
    uint32_t queue_messages;
} HostfsSettings;

static uint32_t read_settings(const AstraStartupInfo *startup,
                              HostfsSettings *out)
{
    AstraConfig config = ASTRA_CONFIG_INIT;
    AstraConfigError error = {0};
    uint32_t status;

    if (astra_process_vfs_init(startup) != ASTRA_VFS_OK)
        return ASTRA_STATUS_NOT_FOUND;
    if (astra_config_open(startup, 1u, ASTRA_CONFIG_OPEN_READ, &config,
                          &error) != ASTRA_CONFIG_OK)
        return ASTRA_STATUS_INVALID;
    status = setting(&config, "grants", 2u,
                     HOSTFS_GRANTS_MAX < ASTRA_WAIT_MULTIPLE_MAX - 2u ?
                         HOSTFS_GRANTS_MAX : ASTRA_WAIT_MULTIPLE_MAX - 2u,
                     &out->grants);
    if (status == ASTRA_STATUS_OK)
        status = setting(&config, "owner_files", 1u, UINT32_MAX,
                         &out->owner_files);
    if (status == ASTRA_STATUS_OK)
        status = setting(&config, "owner_sessions", 1u, UINT32_MAX,
                         &out->owner_sessions);
    if (status == ASTRA_STATUS_OK)
        status = setting(&config, "queue_messages", 1u,
                         ASTRA_PORT_MESSAGES_MAX, &out->queue_messages);
    astra_config_close(&config);
    return status;
}
```

- Startup calls `read_settings` once into a file-scope `settings`, and uses
  `settings.grants` where Task 5 used `grant_capacity`'s reader.
- Call `astra_vfs_service_set_owner_sessions(&service, settings.owner_sessions)`
  for both services.
- `grant_add` creates the grant port with
  `astra_rt_port_create(settings.queue_messages, settings.queue_messages * (uint32_t)sizeof(AstraVfsRenameRequestMessage), ...)`.
- Right after `astra_host_scope_create`, `grant_add` stamps the limit:
  `astra_host_scope_limit(scope, settings.owner_files)`. Failure revokes
  and closes the scope and answers `ASTRA_VFS_ERR_LIMIT`. Task 10's
  recovery re-mints through the same two calls.

`astra_image.py`, `services/hostfs/settings.conf`, after `grants 32`:

```python
          "owner_files 1024\n"
          "owner_sessions 64\n"
          "queue_messages 16\n"
```

If the kernel's `ASTRA_PORT_MESSAGES_MAX` is below 16, use it for
`queue_messages` (`grep -rn "define ASTRA_PORT_MESSAGES_MAX" sw/include`).

- [ ] **Step 5: QEMU admission, the per-scope limit and the gauges**

`AstraHostState` gains:

```c
    /* Provider admission (FS37, FS38): Linux descriptors held for guests
     * (sessions, handles, shares) and directory streams, under files_lock.
     * Bounds come from the launcher, or from QEMU's own descriptor limit;
     * never compiled in. */
    uint32_t fs_descriptors;
    uint32_t fs_directories;
    uint32_t fs_max_descriptors;
    uint32_t fs_max_directories;
    /* Handles and shares per (owner, session), under files_lock: what the
     * scope's `files` limit counts. Key: g_new(guint64) (owner << 32 |
     * session); value: GUINT_TO_POINTER(count). */
    GHashTable *owner_files;
```

`astra68_init` creates it:
`g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, NULL)`.

`AstraHostChannelScope` gains `uint32_t files;`, read from the
configuration's `scope_files` by `astra_host_config_scope`. A bound
channel's OPEN and ADOPT (Task 8) refuse before acting:

```c
        if (bound && scope.files != 0u &&
            astra_host_owner_files(s, owner, session_id) >= scope.files) {
            status = ASTRA_STATUS_LIMIT;
            goto done;
        }
```

with:

```c
static guint64 astra_host_owner_key(uint32_t owner, uint32_t session)
{
    return ((guint64)owner << 32) | session;
}

static uint32_t astra_host_owner_files(Astra68State *s, uint32_t owner,
                                       uint32_t session)
{
    guint64 key = astra_host_owner_key(owner, session);
    uint32_t count;

    qemu_mutex_lock(&s->host.files_lock);
    count = GPOINTER_TO_UINT(g_hash_table_lookup(s->host.owner_files, &key));
    qemu_mutex_unlock(&s->host.files_lock);
    return count;
}

/* Called with files_lock held. */
static void astra_host_owner_files_add(Astra68State *s, uint32_t owner,
                                       uint32_t session, int delta)
{
    guint64 key = astra_host_owner_key(owner, session);
    uint32_t count = GPOINTER_TO_UINT(
        g_hash_table_lookup(s->host.owner_files, &key)) + (uint32_t)delta;
    guint64 *stored;

    if (count == 0u) {
        g_hash_table_remove(s->host.owner_files, &key);
        return;
    }
    stored = g_new(guint64, 1);
    *stored = key;
    g_hash_table_replace(s->host.owner_files, stored,
                         GUINT_TO_POINTER(count));
}
```

`astra_host_insert_file` adds 1 under `files_lock` for `(owner, session)`.
The final release of a file subtracts 1, under the same lock. A pending
share (Task 8) counts for its sharer, from SHARE until it is adopted or
withdrawn. The unbound HostFS channel opens no handles, so its zero limit
costs nothing.

In `astra68_init`, before the volumes are opened:

```c
static uint32_t astra_host_fs_bound(const char *name, uint32_t fallback)
{
    const char *text = g_getenv(name);
    uint64_t value = 0;

    if (text == NULL || text[0] == '\0')
        return fallback;
    if (qemu_strtou64(text, NULL, 10, &value) != 0 || value == 0 ||
        value > UINT32_MAX) {
        error_report("%s must be a positive count, not '%s'", name, text);
        exit(EXIT_FAILURE);
    }
    return (uint32_t)value;
}
```

```c
    {
        struct rlimit files;
        uint32_t own;

        if (getrlimit(RLIMIT_NOFILE, &files) != 0 ||
            (files.rlim_cur != RLIM_INFINITY && files.rlim_cur <= 256u)) {
            error_report("Astra host filesystem: descriptor limit too low");
            exit(EXIT_FAILURE);
        }
        own = files.rlim_cur == RLIM_INFINITY ||
              files.rlim_cur - 256u > UINT32_MAX ?
            UINT32_MAX : (uint32_t)(files.rlim_cur - 256u);
        s->host.fs_max_descriptors =
            astra_host_fs_bound("ASTRA_HOSTFS_MAX_DESCRIPTORS", own);
        s->host.fs_max_directories =
            astra_host_fs_bound("ASTRA_HOSTFS_MAX_DIRECTORIES",
                                s->host.fs_max_descriptors);
    }
```

Admission. `astra_host_insert_file` checks and charges under `files_lock`,
and answers handle 0 when full:

```c
    qemu_mutex_lock(&s->host.files_lock);
    if (s->host.fs_descriptors >= s->host.fs_max_descriptors ||
        (directory != NULL &&
         s->host.fs_directories >= s->host.fs_max_directories)) {
        qemu_mutex_unlock(&s->host.files_lock);
        qemu_mutex_destroy(&file->lock);
        g_free(file);
        return 0u;
    }
    ++s->host.fs_descriptors;
    if (directory != NULL)
        ++s->host.fs_directories;
```

Each caller of `astra_host_insert_file` (through `astra_host_publish_open`)
closes the descriptor on 0 (`closedir` for a stream) and answers
`ASTRA_STATUS_LIMIT`. The final release of a file decrements both counts
under `files_lock`. A session open charges one descriptor the same way and
answers `ASTRA_STATUS_LIMIT` when full, and a share (Task 8) does the same
for its duplicate. A session's release, and a share's adoption or
withdrawal, give the descriptor back.

Gauges, beside the other host properties:

```c
static void astra_host_rss_get(Object *object, Visitor *visitor,
                               const char *name, void *opaque, Error **errp)
{
    uint64_t value = 0;
#ifdef CONFIG_LINUX
    unsigned long size;
    unsigned long resident;
    FILE *statm = fopen("/proc/self/statm", "r");

    if (statm != NULL) {
        if (fscanf(statm, "%lu %lu", &size, &resident) == 2)
            value = (uint64_t)resident * qemu_real_host_page_size() / 1024u;
        fclose(statm);
    }
#endif
    visit_type_uint64(visitor, name, &value, errp);
}

static void astra_host_fds_get(Object *object, Visitor *visitor,
                               const char *name, void *opaque, Error **errp)
{
    uint64_t value = 0;
#ifdef CONFIG_LINUX
    DIR *list = opendir("/proc/self/fd");
    struct dirent *entry;

    if (list != NULL) {
        while ((entry = readdir(list)) != NULL)
            if (entry->d_name[0] != '.')
                ++value;
        closedir(list);
        if (value != 0)
            --value;                    /* the listing's own descriptor */
    }
#endif
    visit_type_uint64(visitor, name, &value, errp);
}
```

```c
    object_property_add_uint32_ptr(OBJECT(machine),
                                   "astra-host-fs-descriptors",
                                   &s->host.fs_descriptors,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(OBJECT(machine),
                                   "astra-host-fs-directories",
                                   &s->host.fs_directories,
                                   OBJ_PROP_FLAG_READ);
    object_property_add(OBJECT(machine), "astra-host-rss-kib", "uint64",
                        astra_host_rss_get, NULL, NULL, s);
    object_property_add(OBJECT(machine), "astra-host-fds", "uint64",
                        astra_host_fds_get, NULL, NULL, s);
```

Include `<sys/resource.h>` and `<dirent.h>` if `astra68.c` does not
already.

`test-hostfs.py`. Task 3b's `booted` helper boots a machine with small
bounds:

```python
def limits(qtest):
    """MAX_DESCRIPTORS=4 (the session root and three handles),
    MAX_DIRECTORIES=1, and a scope limiting its owner to two handles."""
    qtest.detect_endian()
    generation = qtest.read32(HACC_GENERATION)
    assert claim(qtest, generation) == STATUS_OK
    work = open_session(qtest, generation)
    handles = []
    for index in range(2):
        answer = execute(qtest, [Command(
            generation, FS_OPEN, "/f%d" % index,
            flags=OPEN_READ | OPEN_WRITE | OPEN_CREATE, session=work)])[0]
        assert status(answer) == STATUS_OK, status(answer)
        handles.append(get32(answer.raw, 16))
    directory = execute(qtest, [Command(
        generation, FS_OPEN, "/", flags=OPEN_READ | OPEN_DIRECTORY,
        session=work)])[0]
    assert status(directory) == STATUS_OK
    full = execute(qtest, [Command(
        generation, FS_OPEN, "/f9", flags=OPEN_READ | OPEN_WRITE | OPEN_CREATE,
        session=work)])[0]
    assert status(full) == STATUS_LIMIT, status(full)
    closed = execute(qtest, [Command(
        generation, FS_CLOSE, handle=handles[0], session=work)])[0]
    assert status(closed) == STATUS_OK
    second_directory = execute(qtest, [Command(
        generation, FS_OPEN, "/", flags=OPEN_READ | OPEN_DIRECTORY,
        session=work)])[0]
    assert status(second_directory) == STATUS_LIMIT
    print("PASS limits")


def scope_files(qtest):
    """A scope's `files` limit counts its owner's handles in its session."""
    qtest.detect_endian()
    generation = qtest.read32(HACC_GENERATION)
    assert claim(qtest, generation) == STATUS_OK
    work = open_session(qtest, generation)
    assert configure_scope(qtest, generation, CONFIG_OPEN, 13, 61, work,
                           SCOPE_BOUND, 0x1001, owner=0x3003,
                           byte_size=8192 + 4096, files=2) == 0
    for producer in (1, 2):
        opened = scoped(qtest, 13, producer, Command(
            generation, FS_OPEN, "/s%d" % producer,
            flags=OPEN_READ | OPEN_WRITE | OPEN_CREATE, session=work))
        assert status(opened) == STATUS_OK
    third = scoped(qtest, 13, 3, Command(
        generation, FS_OPEN, "/s3", flags=OPEN_READ | OPEN_WRITE | OPEN_CREATE,
        session=work))
    assert status(third) == STATUS_LIMIT
    assert configure_scope(qtest, generation, CONFIG_CLOSE, 13, 61,
                           owner=0x3003) == 0
    print("PASS scope-files")
```

- `configure_scope` gains `files=0`, packed as the fourteenth word, and
  writes `CONFIG_VERSION = 3`.
- Add `STATUS_LIMIT = 10`.
- In `main`, refuse `ASTRA_HOSTFS_MAX_DESCRIPTORS=0` the way the missing
  root is refused: exit non-zero, with a message that names the variable.
- Boot a third time with a fresh `qtest-limits.sock`,
  `ASTRA_HOSTFS_MAX_DESCRIPTORS=4` and `ASTRA_HOSTFS_MAX_DIRECTORIES=1`,
  and run `limits`.
- `scope_files` runs in the first boot, after `scopes`.

- [ ] **Step 6: The measuring tools and the lane cap case**

`fsbench.c`, before the option loop in `main`:

```c
    if (argc == 4 && strcmp(argv[1], "--hold") == 0)
        return hold(argv[2], strtol(argv[3], NULL, 10));
```

and:

```c
/*
 * --hold file|dir COUNT: keep COUNT open files or directory streams while
 * a host driver measures what they cost it (FS37). Creates
 * /work/.astra/fsbench-hold-KIND.ready, prints "fsbench: holding", waits
 * until the driver removes the file, then releases everything.
 */
static int hold(const char *kind, long count)
{
    const char *target = "/work/.astra/fsbench-hold.target";
    char marker[96];
    int directory = strcmp(kind, "dir") == 0;
    int *fds = NULL;
    DIR **streams = NULL;
    struct stat st;
    long held = 0;
    int status = 0;
    int fd;

    if ((!directory && strcmp(kind, "file") != 0) || count <= 0) {
        fprintf(stderr, "usage: fsbench --hold file|dir COUNT\n");
        return 2;
    }
    {
        /* libc's own descriptor limit is not what is being measured. */
        struct rlimit unlimited = {RLIM_INFINITY, RLIM_INFINITY};

        (void)setrlimit(RLIMIT_NOFILE, &unlimited);
    }
    fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return 1;
    (void)close(fd);
    fds = directory ? NULL : calloc((size_t)count, sizeof(*fds));
    streams = directory ? calloc((size_t)count, sizeof(*streams)) : NULL;
    if (fds == NULL && streams == NULL)
        return 1;
    for (; held < count; ++held) {
        if (directory)
            streams[held] = opendir("/work/.astra");
        else
            fds[held] = open(target, O_RDONLY);
        if (directory ? streams[held] == NULL : fds[held] < 0) {
            printf("fsbench: hold %s failed at %ld: %s\n", kind, held,
                   strerror(errno));
            status = 1;
            break;
        }
    }
    (void)snprintf(marker, sizeof(marker),
                   "/work/.astra/fsbench-hold-%s.ready", kind);
    if (status == 0) {
        fd = open(marker, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0)
            (void)close(fd);
        printf("fsbench: holding %ld %s\n", held, kind);
        fflush(stdout);
        while (stat(marker, &st) == 0)
            usleep(100000);
    }
    while (held > 0) {
        --held;
        if (directory)
            (void)closedir(streams[held]);
        else
            (void)close(fds[held]);
    }
    free(fds);
    free(streams);
    printf("fsbench: hold done %s\n", kind);
    return status;
}
```

Add `#include <unistd.h>` and `#include <sys/resource.h>` if absent. The
file hold stays below `owner_files` (1000 < 1024) and so measures QEMU,
not the limit.

`fssem.c`, two more modes before the case loop:

```c
    if (argc == 3 && strcmp(argv[1], "--hold-sessions") == 0)
        return hold_sessions(atoi(argv[2]));
    if (argc == 4 && strcmp(argv[1], "--flood") == 0)
        return flood(argv[2], atol(argv[3]));
```

```c
/*
 * --hold-sessions COUNT: COUNT provider sessions (each a derived grant, so
 * one O_PATH directory in QEMU) held while the gate measures them.
 */
static int hold_sessions(int count)
{
    const char *marker = "/work/.astra/fssem-sessions.ready";
    const AstraAssign *work =
        astra_assign_lookup(astra_process_vfs_assigns(), "WORK");
    AstraVfsClient *client = astra_process_vfs_client_for(work);
    uint32_t handles[64];
    struct stat st;
    int held = 0;
    int fd;

    if (client == NULL || count <= 0 || count > 64)
        return 2;
    for (; held < count; ++held)
        if (astra_vfs_grant_derive(client, "", ASTRA_VFS_GRANT_READ,
                                   &handles[held]) != ASTRA_VFS_OK)
            break;
    fd = open(marker, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
        (void)close(fd);
    printf("FSSEM SESSIONS holding %d\n", held);
    fflush(stdout);
    while (held == count && stat(marker, &st) == 0)
        usleep(100000);
    while (held > 0)
        (void)astra_close(handles[--held]);
    printf("FSSEM SESSIONS done\n");
    return 0;
}

/*
 * --flood DIR LIMIT: open one file until refused, hold everything, and
 * pass when the refusal is EMFILE at the scope's per-process limit (FS38),
 * which QEMU enforces -- not libc's, and not QEMU's global bound. A few of
 * the process's handles may already be open (its cwd, the namespace), so
 * the count is LIMIT less at most 8.
 */
static int flood(const char *directory, long limit)
{
    const char *marker = "/work/.astra/fssem-flood.ready";
    struct rlimit unlimited = {RLIM_INFINITY, RLIM_INFINITY};
    char path[160];
    struct stat st;
    int *fds = NULL;
    long count = 0;
    long capacity = 0;
    long opened;
    int error = 0;
    int fd;

    (void)setrlimit(RLIMIT_NOFILE, &unlimited);
    (void)mkdir(directory, 0755);
    join(path, sizeof(path), directory, "target");
    if (write_text(path, "x", O_WRONLY | O_CREAT | O_TRUNC) != 0)
        return fail("flood target: %s", strerror(errno));
    for (;;) {
        if (count == capacity) {
            int *grown = realloc(fds, (size_t)(capacity + 256) *
                                          sizeof(*fds));

            if (grown == NULL) {
                error = ENOMEM;
                break;
            }
            fds = grown;
            capacity += 256;
        }
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            error = errno;
            break;
        }
        fds[count++] = fd;
    }
    opened = count;
    if (count > 0)
        (void)close(fds[--count]);     /* one back, for the marker */
    fd = open(marker, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
        (void)close(fd);
    printf("FSSEM FLOOD n=%ld errno=%d\n", opened, error);
    fflush(stdout);
    while (stat(marker, &st) == 0)
        usleep(100000);
    while (count > 0)
        (void)close(fds[--count]);
    free(fds);
    if (error == EMFILE && opened <= limit && opened >= limit - 8) {
        printf("FSSEM FLOOD PASS\n");
        return 0;
    }
    printf("FSSEM FLOOD FAIL n=%ld errno=%d\n", opened, error);
    return 1;
}

typedef struct LaneThread {
    char path[160];
    int failures;
} LaneThread;

static void *lane_thread(void *argument)
{
    LaneThread *lane = argument;
    struct stat st;

    for (int index = 0; index < 50; ++index)
        if (stat(lane->path, &st) != 0)
            ++lane->failures;
    return NULL;
}

/* Eight threads use WORK at once: eight lanes, one DMA buffer each, past
 * the old four-buffer cap (Task 11). */
static int case_many_lanes(const char *directory)
{
    LaneThread lanes[8];
    pthread_t threads[8];

    for (int index = 0; index < 8; ++index) {
        join(lanes[index].path, sizeof(lanes[index].path), directory,
             "lanes");
        lanes[index].failures = 0;
    }
    CHECK(write_text(lanes[0].path, "x", O_WRONLY | O_CREAT | O_EXCL) == 0,
          "seed");
    for (int index = 0; index < 8; ++index)
        CHECK(pthread_create(&threads[index], NULL, lane_thread,
                             &lanes[index]) == 0, "thread %d", index);
    for (int index = 0; index < 8; ++index) {
        CHECK(pthread_join(threads[index], NULL) == 0, "join %d", index);
        CHECK(lanes[index].failures == 0, "lane %d: %d failed stats",
              index, lanes[index].failures);
    }
    return 0;
}
```

Add `#include <sys/resource.h>`. Register
`{"many-lanes", case_many_lanes}`, and add `"many-lanes"` to `CASES`.

- [ ] **Step 7: The gate and the board driver**

`test-fs-semantics.py`:

```python
GAUGES = ("astra-host-rss-kib", "astra-host-fds",
          "astra-host-fs-descriptors", "astra-host-fs-directories")
HOLDS = (("fsbench --hold file 1000", "fsbench: holding",
          "fsbench-hold-file.ready", "fsbench: hold done", 1000, "file"),
         ("fsbench --hold dir 200", "fsbench: holding",
          "fsbench-hold-dir.ready", "fsbench: hold done", 200, "dir"),
         ("fssem --hold-sessions 16", "FSSEM SESSIONS holding",
          "fssem-sessions.ready", "FSSEM SESSIONS done", 16, "session"))
OWNER_FILES = 1024          # services/hostfs/settings.conf


def gauges(machine):
    return {name: machine.qmp.execute("qom-get", {"path": "/machine",
                                                  "property": name})
            for name in GAUGES}


def check_budgets(machine, mount):
    """What one file, directory stream and session cost QEMU (FS37)."""
    record = {}
    for command_text, ready, marker, done, count, kind in HOLDS:
        before = gauges(machine)
        machine.settle()
        start_at = machine.sequence()
        machine.qmp.type_line(command_text + " &")
        wait_line(machine, start_at, (ready,), 300)
        time.sleep(1.0)
        during = gauges(machine)
        machine.qmp.type_line("rm /work/.astra/" + marker)
        wait_line(machine, start_at, (done,), 120)
        record[kind] = {name: (during[name] - before[name]) / count
                        for name in GAUGES}
        if record[kind]["astra-host-fds"] < 0.9:
            fail("%s: %r descriptors per object" % (kind, record[kind]))
    with open("/tmp/fs-budget-beast.json", "w") as out:
        json.dump(record, out, indent=1, sort_keys=True)
    print("PASS budgets %s" % json.dumps(record, sort_keys=True))


def check_flood(machine, mount):
    """A handle flood is refused at the scope's per-process limit, and the
    terminal and the desktop still answer (FS38)."""
    machine.settle()
    start_at = machine.sequence()
    machine.qmp.type_line("fssem --flood /work/flood %d &" % OWNER_FILES)
    lines = wait_line(machine, start_at, ("FSSEM FLOOD n=",), 600)
    print([line for line in lines if line.startswith("FSSEM FLOOD")][-1])
    lines, status = command(machine, "ls /work > /dev/null", 10, 51)
    if status != 0:
        fail("the terminal under a flood: %r" % lines[-5:])
    before = machine.sequence()
    machine.qmp.double_click(*machine.desktop_icon())
    if machine.wait_for_text(terminal.BANNER, 60, before)[0] is None or \
            not machine.wait_for_ready(60, before):
        fail("the desktop could not open a terminal under a flood")
    machine.qmp.type_line("rm /work/.astra/fssem-flood.ready")
    lines = wait_line(machine, start_at,
                      ("FSSEM FLOOD PASS", "FSSEM FLOOD FAIL"), 120)
    if "FSSEM FLOOD PASS" not in lines:
        fail("flood: %r" % [line for line in lines
                            if line.startswith("FSSEM FLOOD")])
    print("PASS flood")


CHECKS["budgets"] = check_budgets
CHECKS["flood"] = check_flood
```

The `rm` is typed into the second terminal, which has focus. The flood
reads the marker through its own lane, so it does not matter which terminal
removes the file. Its verdict is a line (`FSSEM FLOOD PASS` or `FAIL`),
because a background job's exit status is not reported where the gate reads
it. The flood's process is limited per scope, so the second terminal, a
different process, opens freely: that is FS38's "the desktop still
answers".

`fs-baseline.py` gets a `--budget` flag. When it is given, it runs this
after opening Terminal:

```python
BOARD_HOLDS = (("file", 1000), ("dir", 200))


def budget(pid, output):
    record = {"identity": identity(), "holds": {}}
    for kind, count in BOARD_HOLDS:
        ready = os.path.join(ASTRA, "fsbench-hold-%s.ready" % kind)
        before = host_sample(pid)
        if not rfb("--keys", "fsbench --hold %s %d\n" % (kind, count)):
            raise SystemExit("fs-baseline: typing failed")
        deadline = time.monotonic() + 300
        while not os.path.exists(ready):
            if time.monotonic() > deadline:
                raise SystemExit("fs-baseline: hold %s never ready" % kind)
            time.sleep(0.2)
        time.sleep(1.0)
        during = host_sample(pid)
        os.unlink(ready)
        time.sleep(5.0)
        record["holds"][kind] = {
            "count": count,
            "rss_kib_per_object":
                (during["rss_kib"] - before["rss_kib"]) / count,
            "fds_per_object": (during["fds"] - before["fds"]) / count,
            "mem_available_kib": during["mem_available_kib"],
        }
    with open(output, "w") as out:
        json.dump(record, out, indent=1, sort_keys=True)
    print("fs-baseline: wrote %s" % output)
```

In `main`: `parser.add_argument("--budget", action="store_true")`, and
after `pid = qemu_pid()`, `if arguments.budget: budget(pid, arguments.output); return`.

- [ ] **Step 8: Measure and set the board's bounds**

On beast: sync, run the kernel tests, build userspace and `sw/boot`, build
QEMU, rebuild images, and run `test-hostfs.py` and `test-fs-semantics.py`.
Expected:
- the kernel suites PASS;
- `PASS limits`, `PASS scope-files`;
- `FSSEM PASS many-lanes`;
- `PASS budgets {...}`, `PASS flood`.

On the board, after publishing this build as Task 1 Step 7 does:

```bash
ssh root@192.168.1.52 "python3 /var/lib/astra/current/bin/fs-baseline.py budget /data/fs-budget.json --budget --restart"
scp root@192.168.1.52:/data/fs-budget.json /mnt/Documents/astra68/builds/fs-phase1-budget-$(date +%Y%m%d)-$(git rev-parse --short HEAD)/de25.json
cp /tmp/fs-budget-beast.json /mnt/Documents/astra68/builds/fs-phase1-budget-$(date +%Y%m%d)-$(git rev-parse --short HEAD)/beast.json
```

Run the `scp` and `cp` from `beast`, which reaches both the board and the
NAS. The bounds follow from the measurement and from the host budget for
provider objects, 64 MiB (decision 2026-10-07):

```bash
python3 - <<'EOF'
import json, glob
record = json.load(open(sorted(glob.glob(
    "/mnt/Documents/astra68/builds/fs-phase1-budget-*/de25.json"))[-1]))
budget_kib = 64 * 1024
for kind in ("file", "dir"):
    per = max(record["holds"][kind]["rss_kib_per_object"], 0.25)
    print(kind, int(budget_kib / per))
EOF
```

Write the two numbers into `emu/qemu/run-arty.sh`, beside `HOSTFS_ROOT`:

```sh
# Provider admission (FS37): measured per-object host cost on the DE25
# (fs-phase1-budget-<date>-<sha>/de25.json) against a 64 MiB budget for
# provider objects. Override in the environment.
HOSTFS_MAX_DESCRIPTORS=${ASTRA_HOSTFS_MAX_DESCRIPTORS:-<file count>}
HOSTFS_MAX_DIRECTORIES=${ASTRA_HOSTFS_MAX_DIRECTORIES:-<dir count>}
```

Pass them in the `env` list that starts QEMU
(`ASTRA_HOSTFS_MAX_DESCRIPTORS="$HOSTFS_MAX_DESCRIPTORS" \` and the
directory one, after `ASTRA_HOSTFS_ROOT=`).
- The two counts are the printed numbers, not values chosen here.
- The comment names the evidence directory, so the numbers can be traced.
- Update `emu/qemu/test-run-arty.py` if it asserts the exact QEMU
  environment.

- [ ] **Step 9: Perturb, full suite, commit**

Perturb. Restore and re-sync each time.

1. **QEMU per-scope limit.** Skip the `scope.files` check. Expected: the
   flood runs to the global bound, `flood` fails with
   `FSSEM FLOOD FAIL n=<more than 1024>`, and `test-hostfs.py` fails in
   `scope_files`.
2. **QEMU global bound.** Skip the descriptor check in
   `astra_host_insert_file`. Expected: `test-hostfs.py` fails in `limits` at
   the `STATUS_LIMIT` assertion.
3. **The lane cap.** Set `ASTRA_DMA_BUFFER_MAX` back to 4. Expected:
   `test_dma_buffers_fill_the_window` fails. Rebuilt end to end,
   `many-lanes` fails "lane 4: 50 failed stats" (or a higher lane number).
4. **The limit is fixed before binding.** Let `HOST_SCOPE_LIMIT` succeed on
   a bound scope. Expected: `test_host_scope` fails at the `BUSY`
   assertion.

`~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-task11.log`. Expected: green.

```bash
git add sw/include/astra sw/kernel ndk/include/astra/runtime.h sw/userspace/runtime \
  sw/userspace/kits emu/qemu/qemu-9.2/hw/m68k/astra68.c sw/userspace/vfs \
  sw/userspace/services/hostfs/main.c emu/qemu/astra_image.py \
  sw/userspace/commands/fsbench/fsbench.c sw/userspace/commands/fssem/fssem.c \
  sw/userspace/commands/Makefile emu/qemu/fs-baseline.py \
  emu/qemu/test-fs-semantics.py emu/qemu/test-hostfs.py emu/qemu/run-arty.sh \
  emu/qemu/test-run-arty.py
git commit -m "feat(fs): measured host budgets; per-scope and provider admission; 16 DMA buffers per process (FS37, FS38)"
```

---

### Task 12: Bootstrap availability (D10, FS09)

The point is to show that the supervisor can reach the Linux provider
before any guest service exists, through the same transport, claim,
session and host backend HostFS uses, and read its own startup manifest
that way. Phase 2 makes that read the only one. Phase 1 makes it, compares
it with the lwext4 read the supervisor still trusts, and logs one of
three answers:

- `bootstrap-provider match bytes=N`;
- `bootstrap-provider mismatch provider=N volume=M`;
- `bootstrap-provider absent status=S`, when the host has no system volume,
  as the board does until Phase 2.

The read uses the claim and gives it up again. QEMU accepts filesystem
commands from one owner (Task 4), and HostFS claims after the supervisor
has finished. The transport and backend objects stay in this file, which
is the bootstrap client Phase 2's hand-over keeps (FS09: "Reuse the
bootstrap client/transport").

**With the lane (Task 3b).** This probe is the one unbound filesystem
client left besides HostFS. Its transport opens its channel with the
device, claims, reads, gives the claim back, and is destroyed, all before
HostFS starts. A thread has one host channel, and the supervisor's own
`WORK` client later opens a lane on that same thread. So
`astra_vfs_host_transport_destroy` must close the probe's channel (Step 3's
confirmation below). The supervisor's startup `HOST_DEVICE` carries
`ASTRA_RIGHT_HOST_SCOPE`, because the kernel gives the initial image every
device right. The supervisor never mints scopes, though: it grants that
right only as `HOST_FILESYSTEM` (Task 3b). The probe's backend has no
scope, so its commands pass `0u` to `astra_vfs_host_transport_begin`.

**Files:**
- Modify: `sw/include/astra/host.h` (`ASTRA_HOST_FS_VOLUME_SYSTEM`)
- Modify: `emu/qemu/qemu-9.2/hw/m68k/astra68.c` (optional `system` volume)
- Create: `sw/userspace/supervisor/src/bootstrap_host.c`, `sw/userspace/supervisor/include/bootstrap_host.h`
- Modify: `sw/userspace/supervisor/src/loader.c`, `sw/userspace/supervisor/Makefile`
- Modify: `emu/qemu/test-hostfs.py` (`volumes`), `emu/qemu/test-fs-semantics.py` (`bootstrap`), `emu/qemu/qemu_runtime.py` (seed the system volume)

**Interfaces:**
- Produces (host): `ASTRA_HOST_FS_VOLUME_SYSTEM` (2), with
  `ASTRA_HOST_FS_VOLUME_COUNT` 3. SESSION_OPEN on a volume the host does not
  provide answers `ASTRA_STATUS_NOT_FOUND`. The enum grows at its end and
  the absence is a runtime answer, so neither `ASTRA_HOST_VERSION` nor the
  command record changes.
- Produces (supervisor): `uint32_t supervisor_bootstrap_probe(const AstraStartupInfo *startup, const void *expected, uint32_t length)`,
  which logs the verdict and returns the status it logged (OK for a
  match).

- [ ] **Step 1: The provider's optional volume**

`host.h`:

```c
    ASTRA_HOST_FS_VOLUME_SOUND,     /**< The volume holding shared SoundFonts. */
    /** The selected system volume, when the host provides one (FS09).
     * Optional: SESSION_OPEN answers NOT_FOUND without it. */
    ASTRA_HOST_FS_VOLUME_SYSTEM,
    ASTRA_HOST_FS_VOLUME_COUNT      /**< Count of defined HostFS volumes. */
```

`astra68.c` `astra68_init`, the volume loop (`:6874-6895`). Add
`[ASTRA_HOST_FS_VOLUME_SYSTEM] = "system"` to `names`. Only the work and
sound volumes are created. The system volume is opened if it exists:

```c
            if (volume != ASTRA_HOST_FS_VOLUME_SYSTEM &&
                mkdirat(s->host.root_fd, names[volume], 0700) < 0 &&
                errno != EEXIST) {
                ...                     /* unchanged */
            }
            s->host.volume_fds[volume] = openat(
                s->host.root_fd, names[volume],
                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (s->host.volume_fds[volume] < 0 &&
                !(volume == ASTRA_HOST_FS_VOLUME_SYSTEM && errno == ENOENT)) {
                ...                     /* unchanged error_report and exit */
            }
```

`astra_host_fs_session_open` (Task 4) answers `ASTRA_STATUS_NOT_FOUND` when
`s->host.volume_fds[volume] < 0`, before it opens anything.

`test-hostfs.py` `volumes`: the "unknown volume" command uses volume 3, the
new count. Add two checks. A session on volume 2 answers `STATUS_NOT_FOUND`
(2) while `root/system` does not exist. A second boot is not needed for
the present case, because Step 4's gate covers it.

- [ ] **Step 2: The supervisor's bootstrap client**

`sw/userspace/supervisor/include/bootstrap_host.h`:

```c
#ifndef SUPERVISOR_BOOTSTRAP_HOST_H
#define SUPERVISOR_BOOTSTRAP_HOST_H

#include <stdint.h>

#include <astra/startup.h>

/*
 * Reads /startup/system from the Linux provider's system volume before any
 * service exists and compares it with `expected` (the lwext4 copy). Logs
 * "bootstrap-provider match|mismatch|absent" and returns ASTRA_STATUS_OK
 * only for a match. Never fatal in Phase 1.
 */
uint32_t supervisor_bootstrap_probe(const AstraStartupInfo *startup,
                                    const void *expected, uint32_t length);

#endif
```

Use the header that declares `AstraStartupInfo` in the other supervisor
headers. Check `include/loader.h` and use the same include.

`sw/userspace/supervisor/src/bootstrap_host.c`:

```c
/*
 * The supervisor's provider client (docs/FILESYSTEM_PRODUCTION_PRD.md FS09).
 *
 * Before any guest service runs, the supervisor claims the host filesystem,
 * opens a read-only session on the system volume, and reads its startup
 * manifest through the same host backend HostFS serves from. Phase 1
 * compares the bytes with the lwext4 read and gives the claim back for
 * HostFS; Phase 2 makes this the read, and keeps this client.
 */

#include <bootstrap_host.h>

#include <astra/event_emit.h>
#include <astra/host.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <astra/vfs_backend.h>
#include <astra/vfs_host_backend.h>
#include <astra/vfs_host_transport.h>

#include <string.h>

#define MANIFEST "/startup/system"
#define CHUNK 4096u

static AstraVfsHostTransport transport;
static AstraVfsHostBackend backend;

static uint32_t verdict(uint32_t status, uint64_t provider, uint32_t volume)
{
    if (status == ASTRA_STATUS_OK)
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR,
                     ASTRA_EVENT_LEVEL_NOTICE,
                     "bootstrap-provider match bytes=%u", volume);
    else if (status == ASTRA_STATUS_INVALID)
        ASTRA_EVENT2(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR,
                     ASTRA_EVENT_LEVEL_WARNING,
                     "bootstrap-provider mismatch provider=%u volume=%u",
                     (uint32_t)provider, volume);
    else
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR,
                     ASTRA_EVENT_LEVEL_NOTICE,
                     "bootstrap-provider absent status=%u", status);
    return status;
}

/* The manifest through the provider, compared chunk by chunk. */
static uint32_t compare(uintptr_t node, uint64_t size, const uint8_t *expected,
                        uint32_t length)
{
    const AstraVfsBackendOps *ops = astra_vfs_host_ops();
    uint8_t *chunk;
    uint64_t offset = 0u;
    uint32_t status = ASTRA_STATUS_OK;

    if (size != length)
        return ASTRA_STATUS_INVALID;
    chunk = astra_runtime_allocate(CHUNK);
    if (chunk == NULL)
        return ASTRA_STATUS_LIMIT;
    while (offset < size && status == ASTRA_STATUS_OK) {
        uint32_t want = size - offset < CHUNK ? (uint32_t)(size - offset) :
                                                CHUNK;
        uint32_t moved = 0u;

        status = ops->read(&backend, node, offset, chunk, want, &moved);
        if (status == ASTRA_STATUS_OK &&
            (moved != want || memcmp(chunk, expected + offset, want) != 0))
            status = ASTRA_STATUS_INVALID;
        offset += moved;
    }
    astra_runtime_deallocate(chunk);
    return status;
}

uint32_t supervisor_bootstrap_probe(const AstraStartupInfo *startup,
                                    const void *expected, uint32_t length)
{
    const AstraStartupCapability *device =
        astra_startup_capability(startup, ASTRA_CAPABILITY_HOST_DEVICE);
    const AstraVfsBackendOps *ops = astra_vfs_host_ops();
    AstraVfsNodeInfo info;
    uintptr_t node = 0u;
    uint32_t session = 0u;
    uint32_t status;

    memset(&info, 0, sizeof(info));
    if (device == NULL ||
        !astra_vfs_host_transport_init(&transport, device->handle, NULL,
                                       NULL, NULL))
        return verdict(ASTRA_STATUS_UNSUPPORTED, 0u, length);
    status = astra_vfs_host_claim(&transport, transport.generation, 0);
    if (status == ASTRA_STATUS_OK) {
        status = astra_vfs_host_session_open(
            &transport, transport.generation, 0u,
            ASTRA_HOST_FS_VOLUME_SYSTEM, "/",
            ASTRA_HOST_FS_SESSION_READ_ONLY, &session);
        if (status == ASTRA_STATUS_OK) {
            (void)astra_vfs_host_init(&backend, &transport,
                                      transport.generation, session);
            status = ops->open(&backend, MANIFEST, ASTRA_VFS_OPEN_READ,
                               ASTRA_VFS_MODE_DEFAULT, &node, &info);
            if (status == ASTRA_STATUS_OK) {
                status = compare(node, info.size, expected, length);
                (void)ops->close(&backend, node);
            }
            (void)astra_vfs_host_session_close(&transport,
                                               transport.generation,
                                               session);
        }
        (void)astra_vfs_host_claim(&transport, transport.generation, 1);
    }
    astra_vfs_host_transport_destroy(&transport);
    return verdict(status, info.size, length);
}
```

The status values are shared:
`ASTRA_VFS_OK` and `ASTRA_STATUS_OK` are both 0, and the VFS error codes
are the system statuses (`vfs_service.h:326-368`).

Confirm `astra_vfs_host_transport_destroy` releases the channel the lane
opened, not only the DMA area. A supervisor that kept a host channel would
hold a slot of `ASTRA_HOST_CHANNEL_COUNT` for its lifetime. Check the
`host.channel.*` metrics before and after boot (`metrics` in a terminal)
against a boot without the probe. If a channel stays open, close it the way
the transport's lane teardown does, in `astra_vfs_host_transport_destroy`
itself, so every user of the transport benefits.

`loader.c` `supervisor_loader_start`, between the parse and the
deallocation (`:2659-2662`):

```c
    status = supervisor_manifest_parse(manifest_text, manifest_length,
                                       manifest) ? ASTRA_STATUS_OK :
                                       SUPERVISOR_LOADER_FAIL_MANIFEST;
    (void)supervisor_bootstrap_probe(startup, manifest_text,
                                     manifest_length);
    astra_runtime_deallocate(manifest_text);
```

Add `#include <bootstrap_host.h>`, and `src/bootstrap_host.c` to `SOURCES`
in `sw/userspace/supervisor/Makefile`. The supervisor already links
`libastravfs.a`, which holds the host backend and transport
(`sw/userspace/vfs/Makefile:59-62`). The ROM embeds the supervisor.
`make -C sw/boot` checks the ROM budget (`docs/MEMORY_MAP.md`), and the
build must still pass it.

- [ ] **Step 3: Seed the system volume in the gate**

`qemu_runtime.root_provider` (Task 2) gains `system=None`. When it names a
file, the provider image is made with a system volume holding that file
as `startup/system`. `mkfs.ext4 -d` populates a new filesystem from a
directory, so nothing has to be mounted to seed it:

```python
def root_provider(work, size_mib=256, system=None):
    ...                                    # unchanged up to mkfs
    command = ["/usr/sbin/mkfs.ext4", "-q", "-F", "-O", "^orphan_file",
               "-E", "root_owner=%d:%d" % (os.getuid(), os.getgid())]
    if system is not None:
        seed = os.path.join(work, "seed")
        os.makedirs(os.path.join(seed, "system", "startup"))
        shutil.copyfile(system,
                        os.path.join(seed, "system", "startup", "system"))
        command += ["-d", seed]
    subprocess.run(command + [image], check=True)
    ...                                    # unchanged
```

Add `import shutil`. The wrapper pre-creates `work` and `sound` only (Task
2), so a provider made without `system` has no system volume, which is the
board's case.

`test-fs-semantics.py` passes `system=manifest` (Task 10's `MANIFEST` file)
and adds:

```python
def check_bootstrap(machine, mount):
    """The supervisor read its manifest through the provider (FS09)."""
    lines, _ = machine.said_with_syslog()
    found = [line for line in lines if "bootstrap-provider" in line]
    if not any("bootstrap-provider match" in line for line in found):
        fail("bootstrap: %r" % found)
    print("PASS bootstrap %s" % found[-1])


CHECKS["bootstrap"] = check_bootstrap
```

The supervisor logs this before the events service exists, so the line
is in the kernel ring at boot. `said_with_syslog` also searches the host
syslog in case the ring wrapped. If neither has it, the event is not
reaching either. Check how `test-events.py` finds supervisor lines and use
the same source.

- [ ] **Step 4: Run, perturb, full suite, commit**

Sync, then build userspace, `sw/boot` (watch the ROM budget line), QEMU and
images, and run `test-hostfs.py` and `test-fs-semantics.py`. Expected:
`PASS bootstrap bootstrap-provider match bytes=...`.

Perturb (restore and re-sync each time):
1. Seed the system volume with the manifest plus one extra byte. Expected:
   `bootstrap-provider mismatch`, and the check fails.
2. In `supervisor_bootstrap_probe`, skip the CLAIM_RELEASE. Expected: HostFS
   cannot claim, so no `/work`, and `fssem basic` and most gates fail. This
   shows the release is what lets HostFS in.

`~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-task12.log`. Expected: green.
The other gates boot without a system volume and log `absent`, which is
not a failure in Phase 1.

```bash
git add sw/include/astra/host.h emu/qemu/qemu-9.2/hw/m68k/astra68.c \
  sw/userspace/supervisor emu/qemu/test-hostfs.py emu/qemu/test-fs-semantics.py \
  emu/qemu/qemu_runtime.py
git commit -m "feat(supervisor): read the startup manifest through the Linux provider before any service (FS09 proof)"
```

---

### Task 12b: Warm commands run on the vCPU (fast path; decision 2026-10-08)

With the lane, the guest's share of a warm `stat` is the same as the old
direct path's. Two thirds of its 35 µs is the host handoff:
- the kick;
- a thread-pool wakeup;
- the `fstatat`;
- the main-loop bottom half;
- the interrupt;
- the guest's idle wake.

The investigation measured that running a bound channel's command inline
on the vCPU thread, as `io_uring` issues before punting to a worker, takes a
warm stat to 12.4 µs (0.36x) and a 4 KiB read to 5.8 µs (0.18x)
(`.superpowers/sdd/fs-perf-investigation.md`, C1). The prototype did it
unconditionally, which is valid only for a warm cache: a cold miss on the
vCPU stalls the only CPU.

This task does it properly. A warm command runs inline only through calls
Linux promises will not block:
- `openat2` with `RESOLVE_CACHED`;
- `preadv2` with `RWF_NOWAIT`;
- attributes of an already-resolved node.

Anything that would block answers `EAGAIN`. The command then goes to the
thread pool exactly as before, with nothing done, published or counted.
Only bound channels run inline: client lanes, never HostFS's control
channel.

**Files:**
- Modify: `emu/qemu/qemu-9.2/hw/m68k/astra68.c` (inline execution, punt, counters)
- Modify: `emu/qemu/qemu-9.2/hw/m68k/astra68-resolve.c`, `astra68-resolve.h` (`ASTRA_RESOLVE_CACHED`)
- Test: `emu/qemu/tests/test-astra68-resolve.c`, `emu/qemu/test-hostfs.py` (`inline`)
- Modify: `emu/qemu/fs-probe.py` (`--qemu-env`, per-phase inline counts), `tools/fs-probe-compare.py` (prints them)

**Interfaces:**
- Consumes: Task 3b's bound channels; Task 4's prologue; Task 6's
  `astra_resolve`; Task 7's `astra_host_publish_node`; Task 8's READ and
  SEEK.
- Produces: resolver flag `ASTRA_RESOLVE_CACHED (1u << 1)`. A resolution
  that would leave the dentry cache answers `-EAGAIN`.
- Produces (QEMU): QOM `astra-host-inline-commands` and
  `astra-host-inline-punts` (read). Environment `ASTRA_HOSTFS_INLINE=0`
  turns inline execution off, for measurement only; it is on by default.
  It is never compiled off, and never on outside `CONFIG_LINUX`.
- Produces (tools): `fs-probe.py ... [--qemu-env NAME=VALUE]...`; each
  phase record gains `inline_per_op`.

- [ ] **Step 1: The resolver learns to stay in the cache**

`astra68-resolve.h`:

```c
/** Resolve only through the dentry cache (openat2 RESOLVE_CACHED): a
 * component that is not cached answers -EAGAIN instead of reading the
 * disk. For the vCPU thread, which must never block (Task 12b). */
#define ASTRA_RESOLVE_CACHED 2u
```

`astra68-resolve.c`: every `openat`/`openat2` the resolver makes goes
through one helper. Replace each call with a call of it:

```c
/* One component (or, on the fast try, a whole path) beneath `dirfd`. With
 * ASTRA_RESOLVE_CACHED the lookup may not leave the dentry cache. */
static int resolve_open(int dirfd, const char *name, int flags,
                        uint64_t resolve, uint32_t mode)
{
#ifdef __linux__
    struct open_how how = {
        .flags = (uint64_t)(flags | O_CLOEXEC),
        .resolve = resolve |
                   ((mode & ASTRA_RESOLVE_CACHED) != 0u ? RESOLVE_CACHED : 0u),
    };

    return (int)syscall(SYS_openat2, dirfd, name, &how, sizeof(how));
#else
    if ((mode & ASTRA_RESOLVE_CACHED) != 0u) {
        errno = EAGAIN;
        return -1;
    }
    return openat(dirfd, name, flags | O_CLOEXEC);
#endif
}
```

A per-component `openat(dirfd, name, flags)` becomes
`resolve_open(dirfd, name, flags, RESOLVE_NO_MAGICLINKS | RESOLVE_NO_SYMLINKS | RESOLVE_BENEATH, mode)`.
The `RESOLVE_BENEATH` fast try keeps its own `resolve` bits. `mode` is
`astra_resolve`'s `flags`. An `EAGAIN` from any of them ends the resolution
with `-EAGAIN`, after it closes what it holds.

`emu/qemu/tests/test-astra68-resolve.c`: run the existing table a second
time with `ASTRA_RESOLVE_CACHED` added to every case's flags. The tree is
hot, because the first pass just walked it, so every answer must be
identical. Any `-EAGAIN` fails, with its case named. Run:
`make -C emu/qemu/tests && emu/qemu/tests/build/test-astra68-resolve`.
Expected: `ASTRA68 RESOLVE PASS` twice, the second line naming the cached
pass. (Task 6 Step 1 wrote the binary's path; use it if it differs.)

- [ ] **Step 2: Failing provider test**

`test-hostfs.py`, in the first boot (which has the QMP socket from Task 10):

```python
def qmp_get(path, prop):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.connect(path)
        stream = sock.makefile("rw")
        json.loads(stream.readline())
        value = None
        for command in ({"execute": "qmp_capabilities"},
                        {"execute": "qom-get",
                         "arguments": {"path": "/machine",
                                       "property": prop}}):
            stream.write(json.dumps(command) + "\n")
            stream.flush()
            reply = json.loads(stream.readline())
            while "return" not in reply and "error" not in reply:
                reply = json.loads(stream.readline())
            assert "return" in reply, reply
            value = reply["return"]
        return value


def inline(qtest, root, qmp_path):
    """Warm commands on a bound channel complete on the vCPU; a cold read
    and an open that could block go to the pool -- and the vCPU keeps
    answering while one is held."""
    generation = qtest.read32(HACC_GENERATION)
    assert claim(qtest, generation) == STATUS_OK
    work = open_session(qtest, generation)
    with open(os.path.join(root, "work", "warm"), "wb") as out:
        out.write(b"w" * 4096)
    big = os.path.join(root, "work", "cold")
    with open(big, "wb") as out:
        out.write(os.urandom(4 * 1024 * 1024))
    assert configure_scope(qtest, generation, CONFIG_OPEN, 15, 81, work,
                           SCOPE_BOUND, 0x1001, owner=0x5005,
                           byte_size=8192 + 4096 + 4096) == 0
    inline_before = qmp_get(qmp_path, "astra-host-inline-commands")
    punts_before = qmp_get(qmp_path, "astra-host-inline-punts")
    os.stat(os.path.join(root, "work", "warm"))           # the dentry is hot
    warm = scoped(qtest, 15, 1, Command(generation, FS_STAT, "/warm",
                                        session=work))
    assert status(warm) == STATUS_OK
    assert qmp_get(qmp_path, "astra-host-inline-commands") == \
        inline_before + 1, "a warm STAT did not run inline"
    opened = scoped(qtest, 15, 2, Command(generation, FS_OPEN, "/cold",
                                          flags=OPEN_READ, session=work))
    assert status(opened) == STATUS_OK
    handle = get32(opened, 16)
    # Drop the file's pages: a read must now miss, and must punt.
    fd = os.open(big, os.O_RDONLY)
    os.posix_fadvise(fd, 0, 0, os.POSIX_FADV_DONTNEED)
    os.close(fd)
    cold = scoped(qtest, 15, 3, Command(generation, FS_READ, handle=handle,
                                        offset=2 * 1024 * 1024,
                                        capacity=4096, session=work))
    assert status(cold) == STATUS_OK
    assert qmp_get(qmp_path, "astra-host-inline-punts") >= punts_before + 1, \
        "a cold read ran on the vCPU"
    # An open that would block (a FIFO with no writer) punts, and the vCPU
    # still answers while the pool holds it.
    fifo = os.path.join(root, "work", "inline-fifo")
    os.mkfifo(fifo)
    held = scoped_post(qtest, 15, 4, Command(generation, FS_OPEN,
                                             "/inline-fifo", flags=OPEN_READ,
                                             session=work))
    time.sleep(0.3)
    assert qtest.read32(HACC_GENERATION) == generation  # the vCPU answers
    writer = os.open(fifo, os.O_WRONLY)
    try:
        assert status(scoped_wait(qtest, 4, held)) == STATUS_OK
    finally:
        os.close(writer)
    assert configure_scope(qtest, generation, CONFIG_CLOSE, 15, 81,
                           owner=0x5005) == 0
    print("PASS inline")
```

Run it against the Task 12 QEMU. Expected: it fails at "a warm STAT did not
run inline", because the counter property does not exist yet: QMP answers
an error, and `qmp_get` asserts.

- [ ] **Step 3: Inline execution and the punt**

`astra68.c`:

```c
/* On the vCPU thread, running a bound channel's command inline: every call
 * that could block must instead answer EAGAIN (Task 12b). */
static __thread bool astra_host_nowait;
/* Set by a command that hit such a call: it did nothing, and goes to the
 * thread pool. */
static __thread bool astra_host_punted;
/* An internal status, never published: "run me in the pool". */
#define ASTRA_HOST_STATUS_PUNT 0xFFFFFFFFu
```

`AstraHostState` gains `uint64_t inline_commands; uint64_t inline_punts;
bool inline_enabled;`. `astra68_init` sets
`inline_enabled = g_strcmp0(g_getenv("ASTRA_HOSTFS_INLINE"), "0") != 0` on
`CONFIG_LINUX`, and `false` elsewhere. It registers both counters with
`object_property_add_uint64_ptr(..., OBJ_PROP_FLAG_READ)` as
`astra-host-inline-commands` and `astra-host-inline-punts`.

Eligibility is decided before anything runs. These are the commands that
can complete without waiting, and only on a bound channel:

```c
/* STAT; a read-only OPEN; a positioned READ; SEEK. Everything else --
 * creating, writing, renaming, directory streams, sequential reads that
 * move a shared position -- goes to the pool, as before. */
static bool astra_host_inline_eligible(const uint8_t *command)
{
    uint16_t operation = lduw_be_p(command + HOST_FIELD(operation));
    uint16_t flags = lduw_be_p(command + HOST_FIELD(flags));

    if (lduw_be_p(command + HOST_FIELD(service)) !=
        ASTRA_HOST_SERVICE_FILESYSTEM)
        return false;
    switch (operation) {
    case ASTRA_HOST_FS_STAT:
    case ASTRA_HOST_FS_SEEK:
        return true;
    case ASTRA_HOST_FS_OPEN:
        return (flags & (ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                         ASTRA_VFS_OPEN_TRUNCATE | ASTRA_VFS_OPEN_APPEND |
                         ASTRA_VFS_OPEN_DIRECTORY)) == 0u;
    case ASTRA_HOST_FS_READ:
        return (flags & ASTRA_HOST_FS_IO_SEQUENTIAL) == 0u;
    default:
        return false;
    }
}
```

`astra_host_channel_kick`, in the loop that makes a job per position,
replaces the unconditional `thread_pool_submit_aio`:

```c
        if (channel->bound && s->host.inline_enabled &&
            astra_host_inline_eligible(base + ASTRA_HOST_CHANNEL_HEADER_SIZE +
                (position & (channel->command_capacity - 1u)) *
                    ASTRA_HOST_COMMAND_SIZE)) {
            int ret;

            astra_host_nowait = true;
            astra_host_punted = false;
            ret = astra_host_channel_worker(job);
            astra_host_nowait = false;
            if (!astra_host_punted) {
                qatomic_inc(&s->host.inline_commands);
                astra_host_channel_complete(job, ret);
                continue;
            }
            qatomic_inc(&s->host.inline_punts);
        }
        thread_pool_submit_aio(astra_host_channel_worker, job,
                               astra_host_channel_complete, job);
```

`astra_host_channel_complete` is the pool's completion callback, which the
prototype also called inline. It already publishes the completion and
raises the interrupt the guest waits on. Read it once to confirm it is safe
on the vCPU thread with the iothread lock held. The prototype ran it there
for every scoped command.

In `astra_host_execute_fs`:
- `astra_host_status_from_errno(EAGAIN)` answers `ASTRA_HOST_STATUS_PUNT`
  when `astra_host_nowait` is set. The status mapping is the one place every
  failed call passes through.
- Every call of `astra_resolve` passes `ASTRA_RESOLVE_CACHED` too when
  `astra_host_nowait` is set. A `-EAGAIN` becomes `errno = EAGAIN` and so
  the punt status.
- STAT's attributes. In nowait mode `astra_host_publish_node` with a
  non-empty `path` first opens the node with
  `openat2(directory, path, O_PATH | O_NOFOLLOW | O_CLOEXEC, RESOLVE_CACHED | RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS)`.
  It then `statx`es that descriptor with `AT_EMPTY_PATH`, and closes it.
  An `O_PATH | O_NOFOLLOW` descriptor names a link itself, so this is the
  same no-follow answer. A plain `statx` by name could read a directory
  block, while a cached `O_PATH` open cannot.
- OPEN. In nowait mode the final open adds `O_NONBLOCK`, so a FIFO or a
  device cannot hold the vCPU. If `fstat` then says it is neither a regular
  file nor a directory, close it and answer the punt status: the pool opens
  it the blocking way, as before. Otherwise clear `O_NONBLOCK`
  (`fcntl(fd, F_SETFL, flags & ~O_NONBLOCK)`) before `astra_host_publish_open`.
- READ (positioned). In nowait mode use
  `preadv2(file->fd, &vector, 1, offset, RWF_NOWAIT)`. A short count that
  does not end at the file's size (`fstat`) is a partial cache hit. Answer
  the punt status without publishing anything: the data span holds bytes
  nobody reads, and the pool reads it all.
- At `done:`, before anything is published:

  ```c
      if (status == ASTRA_HOST_STATUS_PUNT) {
          /* Nothing happened: undo the count, publish nothing; the pool
           * runs the same command, whose inputs are untouched. */
          astra_host_punted = true;
          qatomic_dec(&s->host.operation_counts[operation_index]);
          if (file != NULL)
              astra_host_file_unlock(s, file);
          if (session != NULL)
              astra_host_session_release(s, session);
          g_free(path);
          g_free(path2);
          return;
      }
  ```

  The prologue's zeroing touched only result fields (status,
  `result_length`, `result_value`, the node record), so the pool sees the
  same inputs. A punt can come only from a call that did nothing: a failed
  lookup, a refused open, an `EAGAIN` read. Every one of them happens
  before any write to Linux or to the guest's result fields. That is why
  the eligible set excludes every mutation.

Rebuild QEMU and run `test-hostfs.py`. Expected: `PASS inline` and every
earlier check.

- [ ] **Step 4: Measure: inline against the lane, interleaved; the held miss**

`fs-probe.py` gains `--qemu-env NAME=VALUE` (repeatable). It applies them to
`os.environ` before the machine starts. It also reads
`astra-host-inline-commands` through `machine.qmp` at each phase's
`fsprobe: begin` and `fsprobe: end` lines, and records
`inline_per_op = delta / n` in the phase. `fs-probe-compare.py` prints
`inline_per_op` beside the other counters and does not gate on it.

On beast, with this tree built (Task 3b Step 16's `build` and QEMU
commands):

```sh
OUT=/mnt/Documents/astra68/builds/fs-phase1-task12b-$(date +%Y%m%d)
mkdir -p "$OUT"
for run in 1 2 3 4 5; do
  ~/astra-mg/locked python3 emu/qemu/fs-probe.py ~/Git/astra68 $Q "$OUT/lane-$run.json" --n 5000 --label l$run --qemu-env ASTRA_HOSTFS_INLINE=0
  ~/astra-mg/locked python3 emu/qemu/fs-probe.py ~/Git/astra68 $Q "$OUT/inline-$run.json" --n 5000 --label i$run
done
python3 tools/fs-probe-compare.py $(ls $OUT/lane-*.json | paste -sd,) $(ls $OUT/inline-*.json | paste -sd,) \
  | tee "$OUT/inline-vs-lane.txt"
for run in 1 2 3; do
  ~/astra-mg/locked python3 emu/qemu/test-fs-semantics.py $Q $R ~/astra-mg/base.img --bench t12b-$run "$OUT/qemu-t12b-$run.json"
done
python3 tools/fs-baseline-compare.py "$OUT/qemu-t12b-1.json,$OUT/qemu-t12b-2.json,$OUT/qemu-t12b-3.json" \
  "$OUT/qemu-t12b-1.json" > /dev/null
```

The last line only checks that the three records load. Task 13 compares
against the baseline.

Expected:
- `inline-vs-lane.txt` ends `PROBE PASS`;
- the inline side's stat and read medians are well under the lane's. The
  prototype measured 0.36x and 0.18x. Record the ratios;
- the inline side's `inline_per_op` is at least 0.9 for both phases, which
  shows the fast path is the one taken;
- the bench records carry no `"failed"` workloads, and `held-miss` keeps
  its floor (`fs-baseline.py`'s guard, 20 MB/s). A cached read beside a
  cold one would collapse if the cold read ever ran on the vCPU.

If `inline_per_op` is low, the warm path is punting. Count `inline-punts`
over a probe and find which call answered `EAGAIN`. Do not widen the
eligible set to fix it.

- [ ] **Step 5: Perturb, full suite, commit**

Perturb. Restore and re-sync each time; rebuild QEMU.

1. **A cold read inline.** Drop `RWF_NOWAIT` from the nowait READ.
   Expected: `inline` fails "a cold read ran on the vCPU".
2. **A blocking open inline.** Drop the `O_NONBLOCK` and the type check.
   Expected: `inline` hangs at the FIFO open. The qtest socket's 5 s
   timeout fails it with `socket.timeout`: the vCPU blocked.
3. **The counters lie.** Increment `inline_commands` before the punt check.
   Expected: `inline` fails at the punt assertion, because the cold read
   counts as inline.

No test here drops the dentry cache, because that needs root (`drop_caches`).
So the resolver's `RESOLVE_CACHED` path relies on the kernel's contract. A
perturbation that ignored it would stay green, and the Self-Review lists
that as a gap.

`~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-task12b.log`. Expected: green.
The whole suite now runs with inline execution on.

```bash
git add emu/qemu/qemu-9.2/hw/m68k/astra68.c emu/qemu/qemu-9.2/hw/m68k/astra68-resolve.c \
  emu/qemu/qemu-9.2/hw/m68k/astra68-resolve.h emu/qemu/tests/test-astra68-resolve.c \
  emu/qemu/test-hostfs.py emu/qemu/fs-probe.py tools/fs-probe-compare.py
git commit -m "perf(host): warm lane commands run on the vCPU; cold ones punt to the pool (RESOLVE_CACHED, RWF_NOWAIT)"
```

---

### Task 13: Closing measurement and the record (D8, Phase 1 exit)

Run Task 1's harness again on the finished tree, on the board and in QEMU,
and compare it with the baseline against the PRD budget. The budget is at
most 10% worse median and p95, and at least 90% of the throughput. Then
check the lane's structure. On the data path, cross-address-space switches
per operation and HostFS CPU per operation must stay near zero, as they
were before the relay. Each side is at least five runs, *interleaved*: a
baseline run, then a final run, then a baseline run, and so on. The
investigation measured +-10% between single runs of one build, so medians
of three taken at different times are not enough to resolve a 10% budget.
Store the evidence and bring the documents in line with what now runs.

**Files:**
- Modify: `tools/fs-baseline-compare.py` (runs per side)
- Modify: `docs/FILESYSTEM_PRODUCTION_PRD.md`, `docs/STORAGE_AND_VFS.md`, `docs/ABI.md`, `docs/FILESYSTEM_KIT.md`, `ndk/docs/source/filesystem.md`, `docs/CURRENT_STATE.md`

**Interfaces:**
- Produces: `fs-baseline-compare.py BASE[,BASE...] NEW[,NEW...] [--budget 0.10]`.
  Each side is the per-workload median over its runs. A workload that
  failed in any run fails.
- Consumes: `fs-probe.py` and `fs-probe-compare.py` (Task 3b, with Task
  12b's `inline_per_op`).

- [ ] **Step 1: Compare medians of runs**

In `tools/fs-baseline-compare.py`, replace `workloads` and its docstring
line:

```python
"""Compare fs-baseline records against the PRD's release budget.

usage: fs-baseline-compare.py BASE[,BASE...] NEW[,NEW...] [--budget 0.10]

Each side may be several runs, comma-separated; a side's value for a
workload is the median over its runs. ...           (rest unchanged)
"""

import argparse
import json
import statistics
import sys


def workloads(spec):
    runs = []
    for path in spec.split(","):
        with open(path) as source:
            record = json.load(source)
        runs.append(record.get("guest", record)["workloads"])
    merged = {}
    for name in set.intersection(*(set(run) for run in runs)):
        entries = [run[name] for run in runs]
        merged[name] = {
            "median_ns": statistics.median(e["median_ns"] for e in entries),
            "p95_ns": statistics.median(e["p95_ns"] for e in entries),
            "failed": any(e.get("failed") for e in entries),
        }
        rates = [e.get("throughput_bps") for e in entries]
        if all(rates):
            merged[name]["throughput_bps"] = statistics.median(rates)
    return merged
```

Extend the self-check from Task 1 Step 5 with a five-run case:

```sh
python3 - <<'EOF'
import json, subprocess, tempfile, os
d = tempfile.mkdtemp()
base = {"guest": {"workloads": {"w": {"median_ns": 100, "p95_ns": 200,
        "throughput_bps": 1000, "failed": False}}}}
slow = {"guest": {"workloads": {"w": {"median_ns": 111, "p95_ns": 200,
        "throughput_bps": 1000, "failed": False}}}}
for name, value in (("b", base), ("s", slow)):
    json.dump(value, open(os.path.join(d, name), "w"))
tool = "tools/fs-baseline-compare.py"
b, s = d + "/b", d + "/s"
assert subprocess.run(["python3", tool, b, b]).returncode == 0
assert subprocess.run(["python3", tool, b, s]).returncode == 1
assert subprocess.run(["python3", tool, b, ",".join((b, b, b, s, s))]).returncode == 0
assert subprocess.run(["python3", tool, b, ",".join((s, s, s, b, b))]).returncode == 1
print("COMPARE SELF-CHECK PASS")
EOF
```

Expected: `COMPARE SELF-CHECK PASS`.

- [ ] **Step 2: QEMU closing runs, interleaved, with the structural check**

The QEMU baseline is the tree `133dc6bc`
(`/mnt/Documents/astra68/builds/fs-phase1-baseline-20261007-133dc6bc`, Task
3's report, correction 5). Rebuild it beside this tree, so both sides run
on beast at the same time of day. Add fsprobe to it as Task 3b Step 16
added it to `60f2d30a`. On the Mac:

```sh
git archive 133dc6bc | rtk proxy ssh beast 'rm -rf ~/fsperf/base && mkdir -p ~/fsperf/base && tar -x -C ~/fsperf/base'
rtk rsync -az sw/userspace/commands/fsprobe/ beast:fsperf/base/sw/userspace/commands/fsprobe/
```

On beast, using Task 3b Step 16's `build` function and its two `sed` lines
(with `base` for `direct`):

```sh
build ~/fsperf/base
(cd ~/fsperf/base && ASTRA_QEMU_WORK_ROOT=$HOME/.cache/astra68/qemu-9.2.4 ~/astra-mg/locked emu/qemu/build.sh host)
QB=$(tail -n1 /tmp/qemu-host-build.log); RB=~/fsperf/base/sw/boot/build/astra_boot.bin
build ~/Git/astra68
(cd ~/Git/astra68 && ASTRA_QEMU_WORK_ROOT=$HOME/.cache/astra68/qemu-9.2.4 ~/astra-mg/locked emu/qemu/build.sh host)
Q=$(tail -n1 /tmp/qemu-host-build.log); R=~/Git/astra68/sw/boot/build/astra_boot.bin
SHA=$(cat ~/Git/astra68/.source-commit 2>/dev/null || echo "$(date +%Y%m%d)")
OUT=/mnt/Documents/astra68/builds/fs-phase1-final-$(date +%Y%m%d)-$SHA
mkdir -p "$OUT"
cd ~/Git/astra68
for run in 1 2 3 4 5; do
  ~/astra-mg/locked python3 emu/qemu/test-fs-semantics.py $QB $RB ~/fsperf/base/base.img --bench qbase$run "$OUT/qemu-base$run.json"
  ~/astra-mg/locked python3 emu/qemu/test-fs-semantics.py $Q $R ~/astra-mg/base.img --bench qfinal$run "$OUT/qemu-final$run.json"
done
python3 tools/fs-baseline-compare.py \
  $(ls $OUT/qemu-base*.json | paste -sd,) $(ls $OUT/qemu-final*.json | paste -sd,) \
  | tee "$OUT/qemu-vs-baseline.txt"
for run in 1 2 3 4 5; do
  ~/astra-mg/locked python3 emu/qemu/fs-probe.py ~/fsperf/base $QB "$OUT/probe-base$run.json" --n 5000 --label b$run
  ~/astra-mg/locked python3 emu/qemu/fs-probe.py ~/Git/astra68 $Q "$OUT/probe-final$run.json" --n 5000 --label f$run
done
python3 tools/fs-probe-compare.py \
  $(ls $OUT/probe-base*.json | paste -sd,) $(ls $OUT/probe-final*.json | paste -sd,) \
  | tee "$OUT/probe-vs-baseline.txt"
```

beast's tree has no `.git`, so write the commit you synced into
`.source-commit` before the run (`git rev-parse --short HEAD > .source-commit`
on the Mac, then sync), and name the directory after it. The file is
untracked: delete it on the Mac afterwards, and never stage it. The test driver
is this tree's for both sides. In `--bench` mode it runs only the image's
`fsbench`, which each image built from its own tree.

Expected:
- `qemu-vs-baseline.txt` ends `BUDGET PASS`;
- `probe-vs-baseline.txt` ends `PROBE PASS`. The structural assertions are
  cross/op at most the baseline's + 0.05 (stat) and + 0.5 (read),
  `hostfs ns/op <= 500`, and syscalls/op at most the baseline's + 0.1, for
  both phases; the latency ratios include Task 12b's fast path;
- the final probe's `inline_per_op` is at least 0.9 for both phases.

- [ ] **Step 3: Board closing runs, interleaved**

Publish this tree as Task 1 Step 6 did
(`~/astra-mg/locked emu/qemu/publish-de25-release.sh`). The baseline
release is the one Task 1 measured. Its identity is in its own record:

```sh
BASE=/mnt/Documents/astra68/builds/fs-phase1-baseline-20261007-fa1026ad
BASE_ID=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["identity"]["release"])' "$BASE/fs-base1.json")
FINAL_ID=$(ssh root@192.168.1.52 'basename "$(readlink -f /var/lib/astra/current)"')
ssh root@192.168.1.52 "test -d /var/lib/astra/releases/$BASE_ID" ||
  { echo "baseline release $BASE_ID was pruned"; exit 1; }
select_release() {
  ssh root@192.168.1.52 "python3 /var/lib/astra/current/bin/astra-release.py select /var/lib/astra current $1"
}
for run in 1 2 3 4 5; do
  select_release "$BASE_ID" &&
  ssh root@192.168.1.52 "python3 /var/lib/astra/current/bin/fs-baseline.py base$run /data/fs-base$run.json --restart" &&
  scp root@192.168.1.52:/data/fs-base$run.json "$OUT/de25-base$run.json"
  select_release "$FINAL_ID" &&
  ssh root@192.168.1.52 "python3 /var/lib/astra/current/bin/fs-baseline.py final$run /data/fs-final$run.json --restart" &&
  scp root@192.168.1.52:/data/fs-final$run.json "$OUT/de25-final$run.json"
done
select_release "$FINAL_ID"
ssh root@192.168.1.52 "python3 /var/lib/astra/current/bin/fs-baseline.py budget /data/fs-budget-final.json --budget --restart" &&
  scp root@192.168.1.52:/data/fs-budget-final.json "$OUT/"
ssh root@192.168.1.52 'cat /proc/$(pidof qemu-system-m68k-astra)/limits; free -k; uname -a' > "$OUT/board.txt"
cp /tmp/t9-task*.log "$OUT/"
python3 tools/fs-baseline-compare.py \
  $(ls $OUT/de25-base*.json | paste -sd,) $(ls $OUT/de25-final*.json | paste -sd,) \
  | tee "$OUT/de25-vs-baseline.txt"
```

If the baseline release was pruned, stop and report it. Rebuilding
`fa1026ad` and publishing it without making it current is a step the user
should choose, because `publish-de25-release.sh` activates what it
publishes.

Expected: every record without `"failed"` workloads, and
`de25-vs-baseline.txt` ending `BUDGET PASS`. The structural check is
QEMU's (Step 2). The guest code and the binding are the same on the board,
and only the host's speed differs.

If the board's comparison ends `BUDGET FAIL`, do not tune numbers or drop
workloads to pass. Record which workloads failed and by how much in
`$OUT/de25-vs-baseline.txt`, and stop for the user. The lane removed the
relay's server hop, and the fast path removed most of the host handoff. So
a failure here is a board-specific cost to find, and it is not a cue to
add acceleration.

- [ ] **Step 4: The documents**

`docs/FILESYSTEM_PRODUCTION_PRD.md`, the status paragraph (`:3-6`):

```markdown
Status: implementation PRD. The architectural direction was agreed on
2026-10-06. Phase 1 (baseline and trusted-boundary prototype) is complete:
plan `docs/superpowers/plans/2026-10-07-filesystem-phase1.md`, evidence
`/mnt/Documents/astra68/builds/fs-phase1-baseline-*` and
`fs-phase1-final-*`. Phase 2 has not started.
```

`docs/STORAGE_AND_VFS.md`, a new section before "Current implementation
(to be replaced)":

```markdown
## Phase 1 trusted contract (2026-10)

What the host-volume path guarantees now, ahead of the Phase 2 cutover:

- **Authority and data are separate.** HostFS, the only holder of
  `HOST_FILESYSTEM`, opens a rooted provider session per grant and mints
  its *scope*; HELLO on the grant's port hands the client a duplicate
  (storage protocol 28-29). The client drives its own host channel, which
  the kernel binds to its scopes (`HOST_SCOPE_ATTACH`); QEMU runs only
  filesystem commands there, beneath the session root, with the scope's
  rights, accepting only scopes the claimed owner created. HostFS is never
  on the data path.
- **Grants are ports.** A holder can only derive a narrower grant
  (`GRANT_DERIVE`); a scope travels further only when its grant is
  delegable; revoking a grant detaches its scope from every channel.
- **Linux resolves.** `.`, `..` and symlinks are resolved in order beneath
  the session root. Leaving the root answers ESCAPED with a continuation
  the Filesystem Kit follows in the namespace (protocol 30).
- **Identity.** Metadata carries the volume identity, inode, generation and
  a validity mask; same-volume aliases compare equal (protocol 31).
- **Descriptions.** The position lives in the provider's Linux open file
  description: dup, fork (by a provider-held share token) and exec share
  it, `O_APPEND` is atomic (protocol 32).
- **cwd.** A held directory; relative names survive a rename; `getcwd`
  asks the provider and answers ENOENT when there is no path
  (protocol 33).
- **Loss.** A provider reset or HostFS death answers LOST (`ESTALE`).
  After a reset a client's next use reconnects and HostFS re-mints its
  scopes; after HostFS dies the supervisor restarts it and the managed
  services holding its grants, while applications keep ESTALE until
  restarted (Phase 2: live namespace).
- **Budgets.** QEMU admits descriptors and directory streams up to
  `ASTRA_HOSTFS_MAX_DESCRIPTORS`/`ASTRA_HOSTFS_MAX_DIRECTORIES`, and limits
  each process per scope (`owner_files`, stamped by HostFS); HostFS limits
  sessions per process and each grant's queue; a process holds up to 16
  host lanes.
- **Fast path.** A warm command on a client lane runs on the vCPU
  (`RESOLVE_CACHED`, `RWF_NOWAIT`); anything that could block goes to the
  thread pool.
- **Bootstrap.** The supervisor reads `/startup/system` through the
  provider before any service starts and logs whether it matches.
```

`docs/ABI.md`:
- the storage protocol paragraph (`:526`): replace "version 8" with
  "version 33", and add a sentence pointing to
  `sw/include/astra/vfs_service.h` for the version history (27-33 are
  listed there);
- add, to the host section: host command record 3, host transport
  `0x00010011` with capability bits 10-14, and channel configuration 3;
- add the kernel scope ABI: syscalls 109-112, `ASTRA_RIGHT_HOST_SCOPE` and
  `KERNEL_OBJECT_HOST_SCOPE`;
- add statuses 20 (`ESCAPED`) and 21 (`LOST`) wherever `status.h` values
  are listed (`grep -n "READ_ONLY\|19" docs/ABI.md`).

`docs/FILESYSTEM_KIT.md` (`:3`) and `ndk/docs/source/filesystem.md` (`:4`):
`filesystem.library` 8.2, linked as `filesystem.library.8`. In the NDK page
add a "Behavior" list:

```markdown
- A relative path is relative to the process's current directory, which
  is held open: renaming it does not change what relative names mean.
- `getcwd` fails with `ENOENT` when the current directory has been removed
  or can no longer be named from its binding.
- A file whose service was lost (provider restart) answers `ESTALE` from
  then on; open it again.
- `fork` shares open files' positions with the child; `pread`/`pwrite`
  never move a position; `O_APPEND` writes are atomic.
```

`docs/CURRENT_STATE.md`, "Runtime and storage boundaries" (`:1239`), a
paragraph after the first:

```markdown
Filesystem Phase 1 (2026-10) closed the HostFS bypass without a server on
the data path: HostFS mints kernel-bound scopes per grant, clients drive
their own channels, and QEMU enforces root, rights and session on every
command; ordered resolution, identity, shared descriptions, a held cwd,
defined provider loss, measured host budgets, a vCPU fast path for warm
commands, and a bootstrap read through the provider. Details:
`docs/STORAGE_AND_VFS.md`, "Phase 1 trusted contract". Evidence:
`/mnt/Documents/astra68/builds/fs-phase1-final-*`. Follow-up: keeping
QEMU's softmmu TLB across address-space switches (an ASID-style tag) is a
separate investigation; the filesystem path no longer depends on it.
```

- [ ] **Step 5: Commit**

```bash
git add tools/fs-baseline-compare.py docs/FILESYSTEM_PRODUCTION_PRD.md \
  docs/STORAGE_AND_VFS.md docs/ABI.md docs/FILESYSTEM_KIT.md \
  ndk/docs/source/filesystem.md docs/CURRENT_STATE.md
git commit -m "docs(fs): Phase 1 closed; DE25 and QEMU measured against the baseline, interleaved"
```

---

## Self-Review

### Phase 1 exit evidence -> tasks

| PRD Phase 1 exit item | Where it is proven | Test |
|---|---|---|
| Raw-request confinement | Task 3b (kernel binding, provider enforcement), Task 4 (sessions, creator check), Task 5 (grant ports, scopes per grant) | `test_host_scope`; `test-hostfs.py` `scopes`, `scope_revocation`, `claims_and_sessions`; fssem `raw-lane`, `raw-readonly`, `raw-cross-grant`, `raw-subroot`, `raw-delegate`, `raw-retire` (raw lanes and raw port messages, no Kit) |
| Symlink traversal | Task 6 | `test-astra68-resolve` (host); fssem `symlink-in-volume`, `symlink-cross-volume`, `dotdot-at-root`, `escape-relative`, `link-edges` |
| cwd/rename | Task 9 | fssem `cwd-rename`; `test-hostfs.py` `paths` |
| Same-volume aliases | Task 7 | fssem `alias-identity`; `raw-subroot` identity equality |
| Shared descriptions | Task 8 | fssem `shared-description`, `append-race`, `exec-inherit`; `test-hostfs.py` `descriptions` |
| Provider loss | Task 10 | `test-hostfs.py` `loss`; gate checks `provider-reset`, `hostfs-kill` (with the holders' restart) |
| Target baseline recorded | Task 1 (DE25 before any data-path change), Task 13 (after, interleaved) | `fs-baseline.py` records on the NAS; `fs-baseline-compare.py` |
| Host memory budgets measured | Task 11 | gate `budgets`, `flood`; `test-hostfs.py` `limits`, `scope_files`; DE25 `--budget` record |
| Bootstrap availability | Task 12 | gate `bootstrap`; `test-hostfs.py` `volumes` |
| Gate infrastructure (D11, D12) | Task 2 | `fs-gates.sh` in the beast suite from Task 2 on (Task 3b adds `fs-hostbench`) |
| FS43 consumers of the shared host command header | Task 4 | entropy, audio, metrics, remote desktop updated and their gates run in the full suite |
| FS44 bypass closed, at the direct path's cost | Task 3 (relay), Task 3b (lane) | `test_hello_attaches_the_scope_and_never_the_device`; hostbench `SCOPE MINT REFUSED`; `fs-probe-compare` `PROBE PASS` against `60f2d30a` and (Task 13) `133dc6bc` |
| Lane is the path taken, server-free | Task 3b, Task 13 | gate `lane` (scoped-command count); fsprobe cross-AS/op <= 0.05 and HostFS ns/op <= 500 |
| Security, decision 2026-10-08 | see the list below | |

The security tests the decisions of 2026-10-08 ask for:
- **Forged volume or session.** In record 2 the provider refuses a volume
  the kernel did not attach (`scopes`, `raw-lane`). In record 3 it refuses
  such a session (`raw-lane`, `raw-cross-grant`). The command's owner field
  does not exist: the kernel stamps the owner (`test_host_scope`).
- **A scope minted by a non-filesystem holder.**
  - The kernel refuses it (`test_host_scope`, a device handle without
    `ASTRA_RIGHT_HOST_SCOPE`).
  - The supervisor grants that right only as `HOST_FILESYSTEM` (hostbench
    `SCOPE MINT REFUSED`).
  - The provider refuses a scope whose creator is not the claimed owner
    (`scopes`, Task 4).
- **A read-only scope refuses mutations.** The proof does not need the
  desktop to run read-only:
  - the qtest attaches a read-only scope (`scopes`);
  - the kernel stamps `READ_ONLY` (`test_host_scope`);
  - a raw derived grant's lane on the booted machine (`raw-readonly`), with
    both layers (scope flag and session flag) perturbed separately.
- **Revocation.**
  - A command that has started when its scope is detached finishes, and
    one that has not is refused (`scope_revocation`, with one pool worker
    and a FIFO).
  - Later commands fail too (`scope_revocation`, `raw-retire`).
  - The creator's death revokes its scopes (`test_host_scope`).
  - Provider loss re-mints them (`provider-reset`).
- **Delegation never widens.**
  - A non-delegable scope arrives without TRANSFER (`test_host_scope`,
    `raw-delegate`).
  - DELEGATE and WRITE cannot be derived from a grant without them (port
    test, `raw-delegate`).
  - No duplicate gains ADMINISTER (`test_host_scope`).
- **fork and exec.**
  - A fork child drives its own lane on the inherited scope, and an exec'd
    image resumes it (`lane-fork-exec`).
  - A fork child adopts shared descriptions through its own lane
    (`shared-description`, `descriptions`).

Each task ends with `t9-verify.sh` green and at least one perturbation that
turns its new test red.

### What survives from Task 3 (`62b72fc2`)

- **HELLO never carries the host device.** It carries the grant's scope
  instead.
- **hostbench's layer benchmark stays deleted**, with the gate fixes of
  `6d4e6a2c`.
- **Storage protocol 27 and Kit 5 remain in history.** 28 and 6 are new.
- **The relay data path is deleted, with no fallback.** So are the HostFS
  workers and their `workers` setting, and the four-worker DMA bound. The
  bound's cause, four DMA buffers per process, is fixed for clients in
  Task 11. Task 3b's table gives the reasons.

### Deviations from the investigation, with reasons

1. **A channel carries up to eight scopes, and the command's word selects
   one.** The investigation bound a channel to one scope and ignored the
   command's volume. But the kernel gives a thread one channel (one
   doorbell page per thread slot), and a process holds several grants. The
   provider now *refuses* a forged volume or session rather than ignoring
   it. The owner is still kernel-stamped and never read from the command.
2. **One scope per grant, never revoked at BYE.** A fork child shares its
   parent's scope, so revoking it at the parent's BYE would cut off the
   child. The investigation's "revoke at BYE" becomes "revoke at grant
   retirement, provider loss, or the creator's death".
3. **Revocation detaches the scope; it does not close the channel.** That
   follows from 1: the channel's other scopes must keep working.
4. **The creator check has two layers.**
   - A kernel right on the device handle, `ASTRA_RIGHT_HOST_SCOPE`, granted
     only as `HOST_FILESYSTEM`, stops other device holders minting at all.
     That is what the decision asks.
   - QEMU's `scope_owner == claimed owner` check, which the investigation
     proposed, stays as the provider's own layer.
5. **Delegation is enforced on arrival.** The kernel's port transfer needs
   TRANSFER on the sent handle, so HostFS cannot send a TRANSFER-less
   scope. A generic import-rights hook in the handle layer strips TRANSFER
   from a non-delegable scope as it is received.
6. **`GRANT_DERIVE` returns a grant port, not a scope.** HELLO on the new
   port hands out its scope. There is one place that mints, and a derived
   grant can be given to a child as a namespace binding, which a bare
   scope cannot.
7. **Per-process limits move into QEMU, per scope.** HostFS no longer sees
   opens. The limit rides the scope (`HOST_SCOPE_LIMIT`) into every channel
   carrying it.
8. **Task 7's volume identity is computed from the device number, not
   numbered per transport.** Every client runs the backend now, and
   per-process numbering would give one file two `st_dev` values.
9. **Task 8's shares live in QEMU.** The descriptions are QEMU handles
   keyed by the opening process, so only QEMU can duplicate one for another
   process. The service core's share table and HostFS's entropy seed go.
10. **Task 10 disconnects handles, as Haiku does**, rather than keeping a
    service-core epoch. The lane fails its client on a lost scope, and the
    next use reconnects through HELLO, where HostFS recovers.
11. **The DMA "4-lane cap" is fixed in the kernel**, with 16 buffer records
    placed by page in the existing 32 MiB window. The investigation
    suggested lane sharing or smaller lanes, but a channel is bound to one
    thread and to one DMA buffer (one DMA token), so lanes cannot be
    shared.
12. **Task 12b is in Phase 1**, as the decision says, before Task 13. The
    investigation placed it after Task 13 or in Phase 2.

### Gaps, stated rather than hidden

1. **Storage-protocol long paths** (FS18, D7) are deferred to Phase 2
   (Decisions). The host wire takes 4096-byte paths from Task 4, but client
   requests stay at 192 bytes. A cwd with a longer path answers
   `ENAMETOOLONG` from `getcwd`.
2. **A running application after a HostFS restart** keeps LOST for its old
   grants until it is restarted (Task 10). The supervisor restarts managed
   services that hold the grants. Live namespace publication (FS16) is
   Phase 2.
3. **A supervisor-held host-volume client after a provider reset** recovers
   only when its service is republished. The supervisor does no data
   operations on host volumes, and its derives go through the grant ports,
   which survive (Task 10).
4. **Rename across two grants of one volume** answers `EXDEV` (Kit
   `CROSS_DEVICE`), as it does across volumes.
5. **Directory entries carry no inode** (Task 7 gives `stat` identity, not
   `readdir`'s `d_ino`).
6. **A dirfd-relative escape** (`openat(dirfd, "../..")` leaving the grant)
   answers `EIO`. Only the cwd can rebase (Task 9).
7. **Grant recovery by path after a provider reset** reopens a grant at
   the root path it was made with. A grant whose root was renamed fails to
   recover and answers LOST (Task 10, logged).
8. **macOS `desktop` QEMU profile** compiles, but answers `UNSUPPORTED` for
   the Linux-only facilities and never runs a command inline.
9. **A thread that drives the host device cannot also use a host volume.**
   A thread has one channel (Global Constraints). No shipped service does
   both.
10. **`RESOLVE_CACHED` in the resolver** is trusted to the kernel's
    contract. Without root no test can drop the dentry cache, so a
    resolver that ignored the flag would pass every gate (Task 12b).
11. **The kernel scope pool is 64.** HostFS caps its grants at 48. A
    process holds at most eight distinct scopes on one thread's lane.

### Placeholder scan

Searched the plan for TBD, TODO, "implement later", "similar to Task" and
"add appropriate". None remain. Three places leave a value to measurement
on purpose, each with the command that produces it:
- `run-arty.sh`'s two admission counts (Task 11 Step 8);
- the noise floor (Task 1 Step 7), which Task 13 now answers with five
  interleaved runs per side;
- Task 12b's inline ratios, recorded, not gated.

### Type and name consistency

- **Host operations, dense:**
  - 1-19 (Task 4);
  - `SEEK` 20, `SHARE` 21 and `ADOPT` 22 (Task 8);
  - `PATH` 23 (Task 9).

  `ASTRA_HOST_FS_MAX` follows the last. `ASTRA_HOST_METRICS_VERSION` is 3,
  4 and 5 at Tasks 4, 8 and 9.
- **Host transport:** `0x0001000d` (3b), `0x0001000e` (4), `0x0001000f`
  (6), `0x00010010` (8), `0x00010011` (9). The capability bits are
  `FS_SCOPE` 10, `FS_SESSIONS` 11, `FS_RESOLVES` 12, `FS_DESCRIPTIONS` 13
  and `FS_PATHS` 14. Task 12 adds a volume and no version.
- **Channel configuration:** 2 (3b: scope words, ATTACH, DETACH) and 3
  (11: `scope_files`).
- **Kernel ABI:** syscalls 109 `HOST_SCOPE_CREATE`, 110 `HOST_SCOPE_REVOKE`,
  111 `HOST_SCOPE_ATTACH` (3b) and 112 `HOST_SCOPE_LIMIT` (11);
  `KERNEL_OBJECT_HOST_SCOPE` 17; `ASTRA_RIGHT_HOST_SCOPE` (1u << 9);
  `ASTRA_DMA_BUFFER_MAX` 16 (11). `runtime.library` 1.14 (3b) and 1.15
  (11).
- **Storage protocol:** 27 (3), 28 (3b), 29 (5), 30 (6), 31 (7), 32 (8),
  33 (9).
  - Operation codes: 36 (`GRANT_DERIVE`), 37-39 (`SEEK`, `SHARE`,
    `ADOPT`), 40 (`DIRECTORY_PATH`).
  - HELLO feature bits 0-4: `GRANTS`, `RESOLVES_PATHS`, `NODE_META`,
    `DESCRIPTIONS` and `DIRECTORY_PATH`, at Tasks 5, 6, 7, 8 and 9.
  - Grant rights: `READ`, `WRITE`, `DELEGATE` (bits 0-2).
- **Statuses:** `ESCAPED` 20 (Task 6), `LOST` 21 (Task 10).
- **Filesystem Kit:** 5 (3), 6 (3b), 7 (5), 8 (7), 8.1 (8), 8.2 (9).
- **Shared names:**
  - `astra_host_session_file(s, owner, session_id, handle)`,
    `astra_host_publish_open(s, owner, session_id, command, fd, flags)`
    and `astra_host_insert_file(s, owner, session_id, fd, directory, flags)`
    (Task 3b with a volume tag, renamed in Task 4) are used by Tasks 8, 10,
    11 and 12b with these signatures.
  - Task 11 makes `insert_file` answer 0 when admission refuses.
- `AstraVfsHostBackend.session` (Task 4, from `lease.scope` on a lane),
  `.scope` (Task 3b) and `.lost` (Task 10) are what the lane, recovery and
  the bootstrap client use.
- `astra_vfs_host_transport_begin(context, scope, data_capacity, request)`
  (Task 3b) has every caller passing a scope, 0 for a device transport.

### For the implementer to confirm before relying on it

These are facts the plan uses but could not check without building:
1. `astra_wait_multiple` reports the index of a port whose last sender
   closed (`PEER_DEAD`) on a receive in the wait set (Task 5's grant
   retirement).
2. `sizeof(KernelProcess)` stays within its 4608-byte static assert with
   sixteen DMA records (Task 11). If not, stop and report.
3. QEMU 9.2's `-object main-loop,thread-pool-max=1` bounds the pool that
   `thread_pool_submit_aio` uses from the vCPU thread (Task 3b's revocation
   qtest).
4. `astra_host_channel_complete` is safe on the vCPU thread under the
   iothread lock (Task 12b). The prototype ran it there for every scoped
   command.
5. `astra_posix_startup()` is declared where `file.c` can call it in the
   fork child (Task 8).
6. `install_test_command` installs `fssem` at `/commands/fssem` (Tasks 3b
   and 8).
7. The fake backend and fixture names in `test_vfs_service_core.c` and
   `test_vfs_host_backend.c` match what Tasks 5, 7, 8, 9, 10 and 11 extend.
8. `astra_vfs_host_transport_destroy` releases the channel slot as well as
   the DMA area (Task 12; with one channel per thread, the supervisor's
   later lane depends on it).
9. The kernel's in-use status is named `ASTRA_SYSCALL_BUSY` (Task 11).
10. The kernel fixture can back an 8 MiB DMA buffer (Task 11's window
    test).

### Open questions for the user

1. **Multi-scope channels.** A thread's one channel now carries up to
   eight scopes. The command's session selects one, and the provider
   refuses any other value instead of ignoring it (deviation 1). The
   alternative is one channel per scope per thread. That means a larger
   per-thread doorbell region and more DMA buffers per thread, which is a
   kernel VM layout change. Is eight per thread enough, and is "refused"
   acceptable where the decision said "ignored"?
2. **`GRANT_DERIVE` returns a grant port; the scope comes at HELLO**
   (deviation 6). The decision text says "HELLO/GRANT_DERIVE return
   scopes". If the derived scope should also come back directly, the
   derive reply would carry two handles. That costs one less HELLO for the
   supervisor's narrowing, and adds a second place that mints.
3. **Board interleaving needs the baseline release still installed**
   (Task 13 Step 3). The plan stops if it was pruned, rather than
   republishing `fa1026ad`, because that would make it current. Is
   re-publishing the baseline acceptable there?
