# Filesystem Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the existing HostFS path into the first increment of the
trusted Linux-backed filesystem contract: a recorded DE25 baseline, a closed
FS44 bypass, provider-enforced rooted grants, Linux-ordered path resolution,
stable identity, retained objects and cwd, shared open descriptions, a defined
provider-loss result, measured host budgets and a demonstrated bootstrap read
-- each proven by tests that run on every verify.

**Architecture:**

- **One trusted path.** Every filesystem request for a host volume goes
  client -> HostFS port -> QEMU. HostFS is the only process QEMU accepts
  filesystem commands from (it *claims* the service). The client-side direct
  path is deleted.
- **Grants are ports.** HostFS answers each grant on its own port. The kernel
  stamps both facts HostFS acts on: which port a message arrived on (the
  grant) and which process sent it (the session owner). A holder may only
  *derive* a narrower grant. Each grant maps to a QEMU *session* whose root is
  an `O_PATH` directory beneath the volume; QEMU refuses anything outside it.
- **Linux resolves, Astra routes.** QEMU walks storage paths in order
  (`.`, `..`, symlinks, trailing slashes) beneath the session root. When a
  path leaves the root (`..` at the root, an absolute or escaping symlink),
  QEMU says where, HostFS returns an *escape continuation*, and the
  Filesystem Kit continues in the Astra namespace. Neither libc nor the Kit
  folds `..` for a provider binding.
- **Linux objects carry the semantics.** Open files and directories are
  retained Linux descriptors; their position is the Linux open file
  description's. Fork shares it by duplicating the Linux descriptor; cwd is a
  retained directory handle.

Design inputs: `docs/FILESYSTEM_PRODUCTION_PRD.md` (Phase 1 row),
`.superpowers/sdd/fs-phase1-design-brief.md` (D1-D12),
`.superpowers/sdd/fs-phase1-map.md`, `.superpowers/sdd/fs-linux-audit.md`.

**Tech Stack:** C11 freestanding userspace (`m68k-astra-gcc` on `beast`),
picolibc POSIX layer, the Astra QEMU 9.2.4 fork (`emu/qemu`), Linux `openat2`,
`statx`, `pwritev2(RWF_APPEND)`, host tests with plain `assert`, qtest and
full-boot Python gates.

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
  build: `make -C sw/kernel clean` before `make -C sw/kernel test`.
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
  aliases, no fallbacks, no repurposed private fields.
- **Protocol versions in this plan.** Storage protocol (`ASTRA_VFS_VERSION`):
  Task 3 -> 27, Task 5 -> 28, Task 6 -> 29, Task 7 -> 30, Task 8 -> 31,
  Task 9 -> 32. Host command record: Task 4 sets `ASTRA_HOST_COMMAND_VERSION`
  3, an incompatible layout that every service validates for equality. The
  host *transport* (channels, submissions, `AstraHostLeaseInfo`) does not
  change, and the kernel requires transport major 1
  (`sw/kernel/platform.c:1336-1338`). So `ASTRA_HOST_VERSION` moves only its
  minor, and each new provider facility gets a capability bit: Task 4
  0x0001000d with `ASTRA_HOST_CAP_FS_SESSIONS`, Task 6 0x0001000e with
  `ASTRA_HOST_CAP_FS_RESOLVES`, Task 8 0x0001000f with
  `ASTRA_HOST_CAP_FS_DESCRIPTIONS`, Task 9 0x00010010 with
  `ASTRA_HOST_CAP_FS_PATHS`. Filesystem Kit (`filesystem.library.N`): each
  task that changes a public layout bumps the major. Task 3 -> 5
  (`AstraVfsClient` loses the direct-path fields), Task 5 -> 6 (it gains
  `features`, and `AstraVfsPortThreadState` gains `received_handle`).
  Task 7 -> 7 (`astra_vfs_lookup` gains the identity record; `AstraFileInfo`
  grows, size-gated). Task 8 -> 7.1 and Task 9 -> 7.2 (new functions only).
  The bump is the
  same mechanical edit each time: the file list in Task 3 Step 7, with the
  new number.
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
- **Linux-only host code.** `openat2`, `statx`, `O_PATH`, `RWF_APPEND` and
  `/proc/self/fd` are used under `#ifdef CONFIG_LINUX` in QEMU. The macOS
  `desktop` profile must still compile; there the provider answers
  `ASTRA_STATUS_UNSUPPORTED` for the operations it cannot honor.
- **Kernel rules** (untouched by this plan except header includes): no libgcc,
  no 64-bit `/`, no variable 64-bit shift, no `= {0}` on word arrays.
- **Haiku first.** Shared descriptions follow Haiku's `file_descriptor`
  (`~/Git/haiku/headers/private/kernel/fs/fd.h:56-67`: `ref_count`, `pos`)
  and fork inheritance (`src/system/kernel/fs/vfs.cpp:5026-5080`,
  `vfs_new_io_context` copies parent descriptors). The retained cwd follows
  `io_context->cwd` as a referenced vnode (`vfs.cpp:8343` `set_cwd`) and
  `getcwd` as `dir_vnode_to_path` (`vfs.cpp:8318`, `:2637`). Ordered resolution
  follows `vnode_path_to_vnode` (`vfs.cpp:2204-2378`): `..` at the IO
  context root stays put, a link restarts at the root when absolute, and
  `B_LINK_LIMIT` bounds following.
- **Evidence.** Measurements and run logs go to
  `/mnt/Documents/astra68/builds/fs-phase1-<checkpoint>-<shortsha>/` on the NAS
  (mounted on `beast`); never into git (`docs/ARTIFACT_POLICY.md`).
- **The board.** `root@192.168.1.52` from `beast` (the name `astra-de25` does
  not resolve on `beast`). It is Ubuntu 22.04, systemd 249, util-linux 2.37.2;
  QEMU runs as root with `LimitNOFILE` 524288 (measured 2026-10-07).

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
"concurrent cached reads under a held miss" metric. Task 3 gives HostFS
configured worker threads. The port layer already supports concurrent
workers (`sw/userspace/vfs/include/astra/vfs_port_transport.h:162-165`,
`tests/test_vfs_port_threads.c`).

**D10, which volume.** Phase 1 has no Linux-side system volume. Task 12 adds
an optional QEMU `system` volume (`$ASTRA_HOSTFS_ROOT/system`). The
supervisor reads `/startup/system` from it before any service starts and
compares the bytes with the lwext4 read. The provider client is kept for the
Phase 2 cutover; the comparison is replaced by it.

## File Structure

| File | Responsibility |
|---|---|
| `sw/userspace/commands/fsbench/fsbench.c` (new) | Guest filesystem benchmark: D8 workloads, JSON result |
| `emu/qemu/fs-baseline.py` (new) | Board-side baseline driver: opens Terminal over RFB, runs fsbench, adds host RSS/fd/CPU, writes the record |
| `tools/fs-baseline-compare.py` (new) | Compares two baseline records against the PRD budget |
| `tools/astra-qemu-root` (new) | Narrow root wrapper: loop-mounts a gate ext4 image and runs the built QEMU in a private mount namespace |
| `emu/qemu/qemu_runtime.py` | Gate helper for the wrapper |
| `emu/qemu/test-terminal.py` | `Machine` gains a launcher prefix |
| `sw/userspace/commands/fssem/fssem.c` (new) | Native + POSIX semantic suite (D12) |
| `emu/qemu/test-fs-semantics.py` (new) | Full-boot gate under the wrapper; fault orchestration |
| `emu/qemu/fs-gates.sh` (new) | The filesystem gate set run by the beast suite |
| `sw/include/astra/host.h` | Host transport minors 0x0001000d-0x00010010, command record 3, sessions, claim, node record, paths in the data span, system volume |
| `emu/qemu/qemu-9.2/hw/m68k/astra68.c` | Provider: claim, sessions, v3 commands, base handles, reset loss signalling, limits |
| `emu/qemu/qemu-9.2/hw/m68k/astra68-resolve.c`, `.h` (new) | Ordered path resolution beneath a session root |
| `emu/qemu/tests/test-astra68-resolve.c`, `Makefile` (new) | Host test and benchmark of the resolver |
| `sw/include/astra/vfs_service.h`, `sw/include/astra/status.h` | Storage protocol 27-32; LOST and ESCAPED statuses |
| `sw/userspace/vfs/src/vfs_service_core.c`, `include/astra/vfs_service_core.h` | Grant-bound sessions, per-session backend, escapes, descriptions, shares |
| `sw/userspace/vfs/src/vfs_port_transport.c`, `include/astra/vfs_port_transport.h` | Grant ports, derive, workers; direct path deleted |
| `sw/userspace/vfs/src/backends/host.c`, `vfs_host_transport.c` | v3 commands, sessions, escapes, sequential I/O, share |
| `sw/userspace/vfs/src/vfs_host_direct.c` (deleted) | FS44 bypass removed |
| `sw/userspace/vfs/src/vfs_union.c`, `vfs_assign.c`, `vfs_path.c` | Escape continuation; no lexical `..` for provider bindings |
| `sw/userspace/vfs/src/filesystem_library.c`, `vfs_client.c` | Features, identity, descriptions, cwd helpers |
| `sw/userspace/services/hostfs/main.c` | Claim, sessions, grants, workers, recovery, config limits |
| `sw/userspace/posix/src/file.c`, `path.c`, `process.c` | Raw paths, identity, fork sharing, retained cwd |
| `sw/userspace/supervisor/src/loader.c`, `bootstrap_host.c` (new) | Derived grants; bootstrap provider probe |
| `emu/qemu/astra_image.py` | HostFS manifest and settings |
| Docs | `AGENTS.md`, `docs/FILESYSTEM_PRODUCTION_PRD.md`, `docs/CURRENT_STATE.md`, `docs/STORAGE_AND_VFS.md`, `docs/ABI.md`, `docs/FILESYSTEM_KIT.md`, `ndk/docs/source/filesystem.md` |

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

### Task 4: Host command record 3 -- claimed provider, rooted sessions, path operands, node record (FS43, D2 provider half, D4 wire, D7 host wire)

One flag day for the shared host command record. Every service validates
`ASTRA_HOST_COMMAND_VERSION` for equality, so every consumer is rebuilt and
re-verified together: entropy, metrics, remote desktop, audio, hostbench and
the raw channel benchmark (FS43). The filesystem changes are these:

- QEMU accepts filesystem commands from exactly one *claimed* owner, so the
  entropy, media and remote-desktop services, which also hold `HOST_DEVICE`,
  cannot drive files.
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
- Produces (host.h): `ASTRA_HOST_COMMAND_VERSION 3`, `ASTRA_HOST_VERSION 0x0001000d`,
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
- Consumed by: Task 5 (sessions per grant), Task 6 (resolver), Task 7 (node
  record), Task 12 (bootstrap client).

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

- `ASTRA_HOST_VERSION` -> `UINT32_C(0x0001000d)`. Delete the unused revision
  constants `ASTRA_HOST_VERSION_1_1` through `ASTRA_HOST_VERSION_1_8`
  (`grep -rn ASTRA_HOST_VERSION_1_ sw emu fpga tools ndk` finds no user).
- After `ASTRA_HOST_CAP_AUDIO_STREAM`:

  ```c
  /** Filesystem commands use record version 3: one claimed owner, rooted
   * sessions, path operands in the data span and the AstraHostNode record. */
  #define ASTRA_HOST_CAP_FS_SESSIONS (1u << 10)
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
      status = astra_vfs_host_transport_begin(transport, (uint32_t)length,
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
CAP_FS_SESSIONS = 1 << 10
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
- The version check: `run` asserts `qtest.read32(HACC_VERSION) == 0x0001000D`
  and `HACC_CAPS == 511 | CAP_FS_SESSIONS`. The metrics snapshot asserts
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
`test_vfs_host_backend`), `PASS claims-and-sessions`, `ASTRA QEMU HOSTFS PASS`.

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

- [ ] **Step 9: Full suite, hop check, commit**

`~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-task4.log`. Expected: green.
The audio, entropy, remote-desktop, Doom, terminal and system-log gates
exercise every non-filesystem consumer of record 3.

Run one `--bench t4` as in Task 3 Step 8 and compare it with
`qemu-t3-1.json`. The record change must not move `stat-warm` or
`rand-read` by more than run-to-run noise. Record the comparison in the
commit message.

```bash
git add sw/include/astra/host.h emu/qemu/qemu-9.2/hw/m68k/astra68.c \
  sw/userspace/vfs sw/userspace/services/hostfs/main.c \
  emu/qemu/test-hostfs.py emu/qemu/test-filesystem-stress.py
git commit -m "feat(host): command record 3 -- claimed provider, rooted sessions, path operands, statx node"
```

---

### Task 5: Grants enforced at the trusted boundary (D2, corrected)

A grant becomes a HostFS port with a root and rights. HostFS keeps one
`AstraVfsService` per volume and answers each grant on its own receive port.
A session remembers the grant it was opened on and is refused on any other.
The session's backend is the grant's own provider session, so the provider
confines the root and a read-only grant is read-only in QEMU as well. A
holder narrows a grant with `GRANT_DERIVE`: a sub-root, read-only, or both.
The supervisor uses that for any manifest grant narrower than what it holds.
The raw confinement cases drive the port directly, without the Kit.

**Files:**
- Modify: `sw/include/astra/vfs_service.h` (version 28, `features`, `GRANT_DERIVE`, grant rights)
- Modify: `sw/userspace/vfs/include/astra/vfs_service_core.h`, `sw/userspace/vfs/src/vfs_service_core.c`
- Modify: `sw/userspace/vfs/include/astra/vfs_port_transport.h`, `sw/userspace/vfs/src/vfs_port_transport.c`
- Modify: `ndk/include/astra/vfs_client.h`, `sw/userspace/vfs/src/vfs_client.c`, `sw/userspace/vfs/filesystem.library.exports`
- Modify: `sw/userspace/services/hostfs/main.c`
- Modify: `sw/userspace/supervisor/src/loader.c`
- Modify: Kit major 6 (the same file list as Task 3 Step 7)
- Modify: `sw/userspace/commands/fssem/fssem.c`, `emu/qemu/test-fs-semantics.py`
- Test: `sw/userspace/vfs/tests/test_vfs_service_core.c`, `sw/userspace/vfs/tests/test_vfs_port.c`

**Interfaces:**
- Produces (wire, version 28):
  - `AstraVfsReply.features` replaces `reserved2`. HELLO answers
    `ASTRA_VFS_FEATURE_*`; every other operation answers 0.
    `ASTRA_VFS_FEATURE_GRANTS (1u << 0)` says the session is bound to a
    provider-enforced grant.
  - `ASTRA_VFS_OP_GRANT_DERIVE` (36), with `flags` a subset of
    `ASTRA_VFS_GRANT_READ (1u << 0) | ASTRA_VFS_GRANT_WRITE (1u << 1)` and
    `body.path` the new root, relative to the session's grant root (`""`:
    the same root). The reply carries one send handle.
- Produces (core):
  - `astra_vfs_service_dispatch_grant(service, owner, grant, backend_context, features, operation, request, reply)`
  - `astra_vfs_service_session_bound(service, session, owner, grant) -> int`
  - `astra_vfs_service_release_grant(service, grant)`
  - `astra_vfs_operation_mutates(operation, request) -> int`
  - `astra_vfs_service_read_path(service, session, path, ...)` and
    `astra_vfs_service_readdir_into(service, session, path, ...)` now name
    the session.
- Produces (port):
  - `AstraVfsPortGrant {receive, id, rights, backend_context}`
  - `AstraVfsPortDerive`
  - `astra_vfs_port_service_set_derive(host, derive, context) -> int`
  - `astra_vfs_port_service_grant_pump(host, worker, grant, budget) -> uint32_t`
- Produces (Kit):
  - `AstraVfsClient.features`
  - `astra_vfs_grant_derive(client, root, rights, &handle) -> status`
- Consumed by: Task 6 (escapes travel per session), Task 8 (shares are bound
  to a grant), Task 10 (grant re-registration after provider loss).

- [ ] **Step 1: Write the failing core test**

In `sw/userspace/vfs/tests/test_vfs_service_core.c`, record the backend
context the fake was called with. Add `static void *fake_context_seen;`,
and set `fake_context_seen = context;` as the first statement of
`fake_open`, in place of `(void)context;`. Then:

```c
/*
 * A session is bound to the grant it said HELLO on. Its backend calls get
 * that grant's context, and the same session id on another grant is refused
 * (docs/FILESYSTEM_PRODUCTION_PRD.md FS15).
 */
static void
test_sessions_are_bound_to_their_grant(void)
{
    AstraVfsService service;
    AstraVfsRequest request;
    AstraVfsReply reply;
    int first_grant;
    int second_grant;
    uint32_t session;

    service_start(&service);
    memset(&request, 0, sizeof(request));
    request.size = ASTRA_VFS_REQUEST_SIZE;
    request.version = ASTRA_VFS_VERSION;
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, &first_grant,
                                     ASTRA_VFS_FEATURE_GRANTS,
                                     ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(reply.features == ASTRA_VFS_FEATURE_GRANTS);
    session = reply.session;
    assert(astra_vfs_service_session_bound(&service, session, 7u, 1u));
    assert(!astra_vfs_service_session_bound(&service, session, 7u, 2u));
    assert(!astra_vfs_service_session_bound(&service, session, 8u, 1u));

    request.session = session;
    request.flags = ASTRA_VFS_OPEN_READ;
    strcpy((char *)request.body.path, "/a");
    fake_context_seen = NULL;
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, &first_grant, 0u,
                                     ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(fake_context_seen == &first_grant);
    assert(reply.features == 0u);

    astra_vfs_service_dispatch_grant(&service, 7u, 2u, &second_grant, 0u,
                                     ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_ACCESS);
    assert(fake_context_seen == &first_grant);

    astra_vfs_service_release_grant(&service, 1u);
    assert(!astra_vfs_service_session_bound(&service, session, 7u, 1u));
}
```

Use the file's existing service fixture. If its name differs from
`service_start(&service)`, find it with
`grep -n "astra_vfs_service_init(" sw/userspace/vfs/tests/test_vfs_service_core.c`
and call it the same way. Make sure the fake tree has `/a`; the fixture's
seeded nodes are listed beside `fake_create`. Register the test in `main`.

In `sw/userspace/vfs/tests/test_vfs_port.c` add the port-level cases:

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

/* Sends one standard request for `session` on `port` and pumps `grant`. */
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
test_grants_bind_sessions_refuse_mutation_and_only_narrow(void)
{
    AstraVfsPortService host;
    AstraVfsPortGrant writable = {0};
    AstraVfsPortGrant readable = {0};
    AstraVfsReplyMessage reply;
    uint32_t reply_handle;
    uint32_t reply_two;
    uint32_t received = 0u;
    uint32_t session;
    uint32_t mkdirs;

    mock_reset();
    service_start();
    writable.receive = mock_open(MOCK_QUEUE_MAX);
    writable.id = 1u;
    writable.rights = ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE;
    readable.receive = mock_open(MOCK_QUEUE_MAX);
    readable.id = 2u;
    readable.rights = ASTRA_VFS_GRANT_READ;
    assert(astra_vfs_port_service_init(&host, writable.receive, &service));
    assert(astra_vfs_port_service_set_derive(&host, mock_derive, NULL));
    reply_handle = mock_open(4u);
    reply_two = mock_open(4u);

    /* HELLO on the writable grant. */
    assert(grant_round_trip(&host, &writable, ASTRA_VFS_OP_HELLO, 0u, 0u,
                            "", reply_handle, reply_handle, &reply,
                            &received) == ASTRA_SYSCALL_OK);
    assert(reply.reply.status == ASTRA_VFS_OK && received == 0u);
    assert(reply.reply.features == ASTRA_VFS_FEATURE_GRANTS);
    session = reply.reply.session;

    /* The same session, on the read-only grant's port: refused unanswered. */
    assert(grant_round_trip(&host, &readable, ASTRA_VFS_OP_MKDIR, session,
                            0u, "/x", 0u, reply_handle, &reply,
                            &received) == ASTRA_SYSCALL_WOULD_BLOCK);
    assert(host.refused == 1u);

    /* Derive: never wider, and the new port comes back attached. */
    assert(grant_round_trip(&host, &readable, ASTRA_VFS_OP_HELLO, 0u, 0u,
                            "", reply_two, reply_two, &reply,
                            &received) == ASTRA_SYSCALL_OK);
    {
        uint32_t readonly_session = reply.reply.session;

        mkdirs = backend_mkdir_calls;
        assert(grant_round_trip(&host, &readable, ASTRA_VFS_OP_MKDIR,
                                readonly_session, 0u, "/x", 0u, reply_two,
                                &reply, &received) == ASTRA_SYSCALL_OK);
        assert(reply.reply.status == ASTRA_VFS_ERR_READ_ONLY);
        assert(backend_mkdir_calls == mkdirs);
        assert(grant_round_trip(&host, &readable, ASTRA_VFS_OP_GRANT_DERIVE,
                                readonly_session,
                                ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE,
                                "", 0u, reply_two, &reply,
                                &received) == ASTRA_SYSCALL_OK);
        assert(reply.reply.status == ASTRA_VFS_ERR_ACCESS);
        assert(received == 0u && derive_calls == 0u);
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

Count mkdir calls in the fake backend: add `static uint32_t backend_mkdir_calls;`
and `++backend_mkdir_calls;` at the top of `backend_mkdir`
(`test_vfs_port.c:861`). Reset it in `mock_reset`. Register the test in
`main`.

Run on beast: `~/astra-mg/locked make -C sw/userspace/vfs test`. Expected:
compile errors (the new functions and fields do not exist).

- [ ] **Step 2: The wire (`sw/include/astra/vfs_service.h`)**

- `ASTRA_VFS_VERSION` -> 28. Append to the history: "Version 28 binds a
  session to the grant port it said HELLO on. HELLO answers `features`.
  `GRANT_DERIVE` returns a narrower grant: a sub-root, read-only, or both;
  never wider."
- After `ASTRA_VFS_OP_STAT_FILE`:

  ```c
  /** Derive a narrower grant: `flags` a subset of the session's grant rights,
   * `body.path` the new root relative to its root ("" keeps it). The reply
   * carries the new grant's send handle. */
  #define ASTRA_VFS_OP_GRANT_DERIVE UINT32_C(36)
  /** Highest operation code this build defines. */
  #define ASTRA_VFS_OP_MAX          ASTRA_VFS_OP_GRANT_DERIVE

  /* Rights a grant carries, in GRANT_DERIVE's flags. */
  /** Read files, list directories, stat. Every grant has it. */
  #define ASTRA_VFS_GRANT_READ  (UINT32_C(1) << 0)
  /** Create, write, truncate, rename, unlink, chmod, link. */
  #define ASTRA_VFS_GRANT_WRITE (UINT32_C(1) << 1)

  /* AstraVfsReply::features, answered by HELLO from version 28. */
  /** The session is bound to a grant the service enforces. */
  #define ASTRA_VFS_FEATURE_GRANTS (UINT32_C(1) << 0)
  ```

- In `AstraVfsReply`, replace `/** Must be zero. */ uint32_t reserved2;` with:

  ```c
      /** HELLO, version 28 on: ASTRA_VFS_FEATURE_* the service offers this
       * session. Every other operation: zero. */
      uint32_t features;
  ```

- [ ] **Step 3: The core (`vfs_service_core.h`, `vfs_service_core.c`)**

`AstraVfsSessionSlot` gains, after `owner`:

```c
    uint32_t grant;         /* grant it said HELLO on, 0 for none */
    void *backend_context;  /* that grant's backend, NULL: the service's */
```

and `AstraVfsOpenFile` gains `void *backend_context;` after `node`. A file
keeps the context it was opened with.

In `vfs_service_core.c`:

```c
static void *
slot_context(const AstraVfsService *service, const AstraVfsSessionSlot *slot)
{
    return slot != NULL && slot->backend_context != NULL ?
        slot->backend_context : service->backend.context;
}

int
astra_vfs_operation_mutates(uint32_t operation, const AstraVfsRequest *request)
{
    switch (operation) {
    case ASTRA_VFS_OP_OPEN:
    case ASTRA_VFS_OP_OPEN_AT:
        return (request->flags & (ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                                  ASTRA_VFS_OPEN_TRUNCATE |
                                  ASTRA_VFS_OPEN_APPEND)) != 0u;
    case ASTRA_VFS_OP_WRITE: case ASTRA_VFS_OP_WRITE_AREA:
    case ASTRA_VFS_OP_TRUNCATE: case ASTRA_VFS_OP_MKDIR:
    case ASTRA_VFS_OP_UNLINK: case ASTRA_VFS_OP_UNLINK_AT:
    case ASTRA_VFS_OP_RENAME_FROM: case ASTRA_VFS_OP_RENAME_TO:
    case ASTRA_VFS_OP_RENAME: case ASTRA_VFS_OP_CHMOD:
    case ASTRA_VFS_OP_CHMOD_FILE: case ASTRA_VFS_OP_CHMOD_AT:
    case ASTRA_VFS_OP_SYMLINK_TARGET: case ASTRA_VFS_OP_SYMLINK_TO:
    case ASTRA_VFS_OP_SYMLINK: case ASTRA_VFS_OP_LINK:
        return 1;
    default:
        return 0;
    }
}
```

- Every backend call that passes `service->backend.context` passes the
  session's context instead. In `handle_open`, `slot_context(service, session_slot)`,
  stored into `service->files[index].backend_context` with the node. In
  `close_file`, `read_file`, `write_file`, and the SYNC, TRUNCATE,
  STAT_FILE, CHMOD_FILE, STAT_AT, UNLINK_AT, CHMOD_AT, OPEN_AT and
  `readdir_file_into` paths, `file->backend_context` (for OPEN_AT, the
  directory's). In every path-only case of `dispatch_from_unlocked` (STAT,
  READDIR, READDIR_BATCH, MKDIR, UNLINK, RENAME*, CHMOD, READLINK,
  SYMLINK*, LINK, path FILESYSTEM_INFO), `slot_context(service, slot)`.
  `handle_readdir` and `handle_readdir_batch` take the slot and use it.
  `astra_vfs_backend_filesystem_info_into` and `astra_vfs_backend_readdir_into`
  take an `AstraVfsBackend`; build `(AstraVfsBackend){service->backend.ops, context}`
  on the stack for them.
- `astra_vfs_service_read_path` and `astra_vfs_service_readdir_into` gain
  `uint32_t session` after `service`. They take the state lock, find the slot
  (`ASTRA_VFS_ERR_BAD_HANDLE` if none), read its context, release the lock,
  and call the backend helper with `(AstraVfsBackend){ops, context}`.
- `handle_hello` gains `uint32_t grant, void *backend_context, uint32_t features`.
  It stores `grant` and `backend_context` in the slot. When the agreed
  version is at least 28 it sets `reply->features = features`. The
  `reap_session` reset clears `grant` and `backend_context`.
- Rename `dispatch_from_unlocked` to `dispatch_unlocked` with the extra
  arguments `uint32_t grant, void *backend_context, uint32_t features`.
  HELLO passes them to `handle_hello`. For every other operation, right after
  the existing owner check:

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
                                   uint32_t grant, void *backend_context,
                                   uint32_t features, uint32_t operation,
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
      dispatch_unlocked(service, owner, grant, backend_context, features,
                        operation, request, reply);
      state_release(service);
  }

  void
  astra_vfs_service_dispatch_from(AstraVfsService *service, uint32_t owner,
                                  uint32_t operation,
                                  const AstraVfsRequest *request,
                                  AstraVfsReply *reply)
  {
      astra_vfs_service_dispatch_grant(service, owner, 0u, NULL, 0u,
                                       operation, request, reply);
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

Declare the new functions in `vfs_service_core.h` beside the old ones, and
update the `read_path`/`readdir_into` prototypes.

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
    void *backend_context;     /* the backend instance that confines it */
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

and `AstraVfsPortService` gains `AstraVfsPortDerive derive; void *derive_context;`.
`astra_vfs_port_service_init` clears them.

Source:
- Turn the body of `astra_vfs_port_service_worker_pump` into
  `static uint32_t pump(AstraVfsPortService *host, AstraVfsPortWorker *worker, uint32_t receive, const AstraVfsPortGrant *grant, uint32_t budget)`.
  Both public pumps call it:
  `astra_vfs_port_service_worker_pump(host, worker, budget)` passes
  `host->receive, NULL`, and `astra_vfs_port_service_grant_pump` passes
  `grant->receive, grant` (refusing `grant == NULL || grant->receive == 0u || grant->id == 0u`).
- Inside `pump`, with `uint32_t grant_id = grant != NULL ? grant->id : 0u;`
  `void *grant_context = grant != NULL ? grant->backend_context : NULL;`
  `uint32_t features = grant != NULL ? ASTRA_VFS_FEATURE_GRANTS : 0u;`:
  - receive from `receive` in both `astra_port_receive_from` calls;
  - `session_owned` uses
    `astra_vfs_service_session_bound(host->service, incoming->request.session, sender, grant_id)`;
  - the fused-HELLO dispatch and the final `else` dispatch use
    `astra_vfs_service_dispatch_grant(host->service, sender, grant_id, grant_context, features, ...)`;
  - the bulk paths pass the session: `astra_vfs_service_readdir_into(host->service, incoming->request.session, ...)`
    and `astra_vfs_service_read_path(host->service, incoming->request.session, ...)`;
  - before the operation branches (just after the activity adoption),
    refuse mutation on a read-only grant without reaching the backend:

    ```c
        if (grant != NULL && (grant->rights & ASTRA_VFS_GRANT_WRITE) == 0u &&
            astra_vfs_operation_mutates(operation, &incoming->request)) {
            outgoing->reply.status = ASTRA_VFS_ERR_READ_ONLY;
            goto answer_ready;
        }
    ```

  - add, before the final `else`:

    ```c
        } else if (operation == ASTRA_VFS_OP_GRANT_DERIVE) {
            uint32_t derived = 0u;

            if (incoming->request.version < UINT16_C(28) || grant == NULL ||
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

Client side, in `astra_vfs_port_transport`'s reply check, accept a handle on
GRANT_DERIVE only. The term from Task 3, `reply_handle_count != 0u`, becomes
`(reply_handle_count != 0u && wire_operation != ASTRA_VFS_OP_GRANT_DERIVE)`.
After `*reply = incoming->reply;`, hand it to the calling thread:

```c
    if (reply_handle_count == 1u)
        port_state(client)->received_handle = reply_handles[0];
```

In `ndk/include/astra/vfs_client.h`, `AstraVfsPortThreadState` gains
`uint32_t received_handle;` after `borrow_base`, and `AstraVfsClient` gains
`uint32_t features;` after `version`.

- [ ] **Step 5: The Kit (`vfs_client.c`)**

`astra_vfs_connect`: after a successful HELLO,
`client->features = call->reply.features;`. The lazy path publishes the
session in `port_client_publish_session`; record `features` there too, by
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
        (rights & ~(ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE)) != 0u)
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

`vfs_client.c` calls them through the client's transport only when
`client->transport == astra_vfs_port_transport`. A local client has no
grants, and the `features` test above has already returned UNSUPPORTED for
it. Declare `astra_vfs_grant_derive` in `ndk/include/astra/vfs_client.h`
with a doc comment, and add it to `sw/userspace/vfs/filesystem.library.exports`
in sorted position.

Kit major 6: repeat Task 3 Step 7 with `5` -> `6`
(`filesystem.library.6`, `ASTRA_DYNAMIC_LIBRARY("filesystem.library.6", 6, 0, 0, ...)`,
`'^filesystem.library.6 6.0.0 ABI 6.0$$'`).

- [ ] **Step 6: HostFS serves grants (`services/hostfs/main.c`)**

Replace the WORK and SOUND single-port services with a grant table.
`METRICS` keeps its plain port.

```c
#include <astra/vfs_host_backend.h>

/* A grant HostFS answers on its own port: the root and rights the provider
 * session enforces, and the service whose sessions it binds. */
typedef struct HostfsGrant {
    AstraVfsPortGrant port;
    AstraVfsHostBackend backend;   /* its provider session */
    AstraVfsService *service;      /* WORK's or SOUND's */
    uint32_t users;                /* workers inside its pump */
    uint8_t retiring;
} HostfsGrant;

static HostfsGrant *grants;
static uint32_t grant_capacity;    /* settings.conf `grants N` */
static uint32_t next_grant_id = 1u;
static _Alignas(4) uint32_t grant_lock;
static uint32_t grant_change;      /* semaphore: workers rebuild their waits */
static uint32_t **worker_waits;    /* per worker: handles to wait on */
static uint32_t **worker_which;    /* per worker: what each handle is */

enum { WAIT_SHUTDOWN_EVENT = UINT32_MAX - 3u, WAIT_GRANT_CHANGE,
       WAIT_METRICS, WAIT_SHUTDOWN };
```

The two `AstraVfsService` instances stay as they are (`service` for WORK and
`sound_service` for SOUND), with their session and file storage. The
`AstraVfsPortService port` stays as the shared reply-lane table for both.
Initialise it with `astra_vfs_port_service_init(&port, <WORK base grant's receive>, &service)`,
since the call requires a receive. A port host's reply lanes are keyed by
session, and sessions are unique per service. SOUND's grants therefore use
a second host, `sound_port`, initialised the same way with `sound_service`.
A grant knows which host serves it:

```c
static AstraVfsPortService *grant_host(const HostfsGrant *grant)
{
    return grant->service == &service ? &port : &sound_port;
}
```

Grant creation, shared by startup and derivation:

```c
/* Adds a grant over `session` with `rights`; answers its send handle. */
static uint32_t grant_add(AstraVfsService *owner, uint32_t session,
                          uint32_t rights, uint32_t *send)
{
    HostfsGrant *grant = NULL;
    uint32_t receive = 0u;

    *send = 0u;
    if (astra_rt_port_create(
            ASTRA_PORT_MESSAGES_MAX,
            ASTRA_PORT_MESSAGES_MAX *
                (uint32_t)sizeof(AstraVfsRenameRequestMessage),
            &receive, send) != ASTRA_SYSCALL_OK)
        return ASTRA_VFS_ERR_LIMIT;
    (void)astra_vfs_state_lock_acquire(&grant_lock);
    for (uint32_t index = 0u; index < grant_capacity; ++index)
        if (grants[index].port.receive == 0u) {
            grant = &grants[index];
            break;
        }
    if (grant != NULL) {
        memset(grant, 0, sizeof(*grant));
        grant->service = owner;
        grant->port.receive = receive;
        grant->port.id = next_grant_id++;
        grant->port.rights = rights;
        grant->port.backend_context = &grant->backend;
        (void)astra_vfs_host_init(&grant->backend, &transport,
                                  transport.generation, session);
    }
    astra_vfs_state_lock_release(&grant_lock);
    if (grant == NULL) {
        (void)astra_close(receive);
        (void)astra_close(*send);
        *send = 0u;
        return ASTRA_VFS_ERR_LIMIT;
    }
    if (worker_count > 1u)
        (void)astra_rt_signal(grant_change, worker_count - 1u, NULL);
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
        &transport, transport.generation, from->backend.session, 0u,
        provider_root,
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

/* The grant's last holder closed its handle: end its sessions and its
 * provider session. Called by the worker that saw PEER_DEAD. */
static void grant_retire(HostfsGrant *grant)
{
    (void)astra_vfs_state_lock_acquire(&grant_lock);
    grant->retiring = 1u;
    astra_vfs_state_lock_release(&grant_lock);
    for (;;) {
        uint32_t users;

        (void)astra_vfs_state_lock_acquire(&grant_lock);
        users = grant->users;
        astra_vfs_state_lock_release(&grant_lock);
        if (users == 0u)
            break;
        (void)astra_rt_thread_yield();
    }
    astra_vfs_service_release_grant(grant->service, grant->port.id);
    (void)astra_vfs_host_session_close(&transport, transport.generation,
                                       grant->backend.session);
    (void)astra_close(grant->port.receive);
    (void)astra_vfs_state_lock_acquire(&grant_lock);
    memset(grant, 0, sizeof(*grant));
    astra_vfs_state_lock_release(&grant_lock);
    if (worker_count > 1u)
        (void)astra_rt_signal(grant_change, worker_count - 1u, NULL);
}
```

Confirm the yield call's name with
`grep -n "yield" ndk/include/astra/runtime.h`. The retiring worker waits for
the others to leave the grant's pump; nobody enters it once `retiring` is
set.

The worker builds its wait set from the table each time round:

```c
static void hostfs_worker(uint32_t index)
{
    uint32_t *handles = worker_waits[index];
    uint32_t *which = worker_which[index];

    for (;;) {
        uint32_t count = 0u;
        uint32_t ready = ASTRA_WAIT_INDEX_NONE;
        uint32_t status;

        handles[count] = shutdown_event;
        which[count++] = WAIT_SHUTDOWN_EVENT;
        if (worker_count > 1u) {
            handles[count] = grant_change;
            which[count++] = WAIT_GRANT_CHANGE;
        }
        if (index == 0u) {
            handles[count] = metrics_port.receive;
            which[count++] = WAIT_METRICS;
            handles[count] = shutdown_receive;
            which[count++] = WAIT_SHUTDOWN;
        }
        (void)astra_vfs_state_lock_acquire(&grant_lock);
        for (uint32_t at = 0u; at < grant_capacity; ++at)
            if (grants[at].port.receive != 0u && grants[at].retiring == 0u) {
                handles[count] = grants[at].port.receive;
                which[count++] = at;
            }
        astra_vfs_state_lock_release(&grant_lock);
        status = astra_wait_multiple(handles, count, ASTRA_DEADLINE_FOREVER,
                                     &ready, NULL);
        if (status == ASTRA_SYSCALL_CANCELLED) {
            if (index == 0u && shutdown_requested != 0u)
                break;
            continue;
        }
        if (ready >= count) {
            (void)astra_log_failure("hostfs receive wait", status);
            __atomic_store_n(&worker_failed, 1u, __ATOMIC_RELEASE);
            break;
        }
        if (which[ready] == WAIT_SHUTDOWN_EVENT)
            break;
        if (which[ready] == WAIT_GRANT_CHANGE)
            continue;
        if (which[ready] == WAIT_SHUTDOWN) {
            AstraShutdownRequest request = {0};

            if (astra_shutdown_receive(shutdown_receive, &request,
                                       &shutdown_reply) != ASTRA_SYSCALL_OK)
                __atomic_store_n(&worker_failed, 1u, __ATOMIC_RELEASE);
            shutdown_transaction = request.header.transaction_id;
            shutdown_requested = 1u;
            break;
        }
        if (which[ready] == WAIT_METRICS) {
            (void)astra_vfs_port_service_worker_pump(&metrics_port,
                                                     &workers[index], 1u);
            continue;
        }
        if (status == ASTRA_SYSCALL_PEER_DEAD) {
            grant_retire(&grants[which[ready]]);
            continue;
        }
        if (status != ASTRA_SYSCALL_OK) {
            (void)astra_log_failure("hostfs receive wait", status);
            __atomic_store_n(&worker_failed, 1u, __ATOMIC_RELEASE);
            break;
        }
        {
            HostfsGrant *grant = &grants[which[ready]];
            int entered = 0;

            (void)astra_vfs_state_lock_acquire(&grant_lock);
            if (grant->port.receive != 0u && grant->retiring == 0u) {
                ++grant->users;
                entered = 1;
            }
            astra_vfs_state_lock_release(&grant_lock);
            if (!entered)
                continue;
            (void)astra_vfs_port_service_grant_pump(grant_host(grant),
                                                    &workers[index],
                                                    &grant->port, 1u);
            (void)astra_vfs_state_lock_acquire(&grant_lock);
            --grant->users;
            astra_vfs_state_lock_release(&grant_lock);
        }
    }
    if (index != 0u)
        astra_thread_exit(ASTRA_STATUS_OK);
}
```

A receive port whose last sender closed and whose queue is empty answers a
wait with `PEER_DEAD` (`sw/kernel/port.c:1266-1270`). Confirm that
`astra_wait_multiple` reports the index of the handle that answered on a
`PEER_DEAD` result: read the syscall's wait loop
(`grep -n "WAIT_MULTIPLE" sw/kernel/process.c`). If it does not, the worker
cannot tell which grant died; stop and report it, because that needs a
kernel change outside this plan.

Startup in `astra_main`, after the claim and the two root sessions
(Task 4):

- read `grants N` from `services/hostfs/settings.conf` the way
  `configured_workers` reads `workers` (`N` from 2 to
  `ASTRA_WAIT_MULTIPLE_MAX - 4`), and allocate `grants`, `worker_waits` and
  `worker_which` (each worker's arrays hold `grant_capacity + 4` entries);
- create `grant_change` with
  `astra_rt_semaphore_create(0u, worker_count, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL, &grant_change)`;
- add the base grants. Their send handles are what HostFS publishes for
  `WORK` and `SOUND`:

  ```c
      uint32_t work_send = 0u;
      uint32_t sound_send = 0u;

      if (status == ASTRA_STATUS_OK &&
          (grant_add(&service, work_session,
                     ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE,
                     &work_send) != ASTRA_VFS_OK ||
           grant_add(&sound_service, sound_session,
                     ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE,
                     &sound_send) != ASTRA_VFS_OK))
          status = ASTRA_STATUS_LIMIT;
  ```

- initialise `port` and `sound_port` over the base grants' receives, give
  both the state lock as before, and give both
  `astra_vfs_port_service_set_derive(&port, hostfs_derive, NULL)` (and the
  same for `sound_port`);
- publish `{work_send, metrics_send, sound_send}` in the ready message where
  `{send, metrics_send, sound_send}` were. The old `receive`/`send` and
  `sound_receive`/`sound_send` port pairs go: the grants' ports replace them.

`finish_hostfs` drains each live grant with
`astra_vfs_port_service_grant_pump(grant_host(&grants[i]), &workers[0], &grants[i].port, 1u)`
until it answers 0. It then shuts the two services down exactly as before.

Add `grants 32` to `services/hostfs/settings.conf` in `emu/qemu/astra_image.py`.

- [ ] **Step 7: The supervisor narrows what it grants**

In `sw/userspace/supervisor/src/loader.c`, the namespace member loop of
`build_grants` (`:997-1016`) grants `held->handle` with the wanted rights
in client flags only. When the provider enforces grants, grant a derived
one instead:

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
                               uint32_t *handle)
{
    AstraVfsClient *client = supervisor_vfs_client_for(held);
    uint32_t wanted = ASTRA_VFS_GRANT_READ |
        ((rights & ASTRA_RIGHT_WRITE) != 0u ? ASTRA_VFS_GRANT_WRITE : 0u);
    uint32_t status;

    *handle = held->handle;
    if ((held->rights & ~rights & ASTRA_RIGHT_WRITE) == 0u ||
        client == NULL ||
        (client->features & ASTRA_VFS_FEATURE_GRANTS) == 0u)
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

In the member loop, after the rights check, call
`status = narrowed_grant(held, wanted->rights, &handle); if (status != ASTRA_STATUS_OK) return status;`
and pass `handle` to `add_grant` in place of `held->handle`. In
`unpublish_owner`, call `narrowed_drop(service_handles[index]);` before
`(void)astra_close(service_handles[index]);`.

`held->root` is `""` for every host binding today: HostFS publishes volume
roots. Passing it keeps the call right for the carved-out bindings that
Phase 2 moves to the provider.

- [ ] **Step 8: The raw confinement cases (`fssem.c`, the gate)**

Add to `fssem.c`:

```c
#include <astra/metrics.h>
#include <astra/posix.h>
#include <astra/runtime.h>
#include <astra/vfs_service.h>

/* A storage-protocol client with no Kit: what a hostile program can send. */
typedef struct RawClient {
    uint32_t service;
    uint32_t reply_receive;
    uint32_t session;
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

/* host.fs.<operation> calls, from METRICS:; UINT64_MAX when not granted. */
static uint64_t host_calls(const char *operation)
{
    char group[ASTRA_METRIC_NAME_MAX];
    AstraMetricRecord record;
    uint64_t value = UINT64_MAX;
    int fd = open("/metrics/snapshot", O_RDONLY);

    if (fd < 0)
        return value;
    (void)snprintf(group, sizeof(group), "host.fs.%s", operation);
    while (read(fd, &record, sizeof(record)) == (ssize_t)sizeof(record))
        if (strncmp(record.group, group, sizeof(record.group)) == 0 &&
            strncmp(record.name, "calls", sizeof(record.name)) == 0) {
            value = astra_metric_record_value(&record);
            break;
        }
    (void)close(fd);
    return value;
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

/*
 * A read-only grant, derived and used raw: every mutation is refused by
 * HostFS before the provider sees it, and it cannot be widened again.
 */
static int case_raw_readonly(const char *directory)
{
    RawClient rw;
    RawClient ro;
    AstraVfsReplyMessage reply;
    uint32_t derived = 0u;
    uint64_t mkdirs;
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
    CHECK(raw_open(&ro, derived) == ASTRA_SYSCALL_OK, "HELLO on the derived grant");
    CHECK(raw_call(&ro, ro.service, ro.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE, "", &reply,
                   NULL) == ASTRA_SYSCALL_OK &&
          reply.reply.status == ASTRA_VFS_ERR_ACCESS,
          "widened a read-only grant: status %u", (unsigned)reply.reply.status);
    mkdirs = host_calls("mkdir");
    wire_path(wire, sizeof(wire), directory, "ro-made");
    CHECK(raw_call(&ro, ro.service, ro.session, ASTRA_VFS_OP_MKDIR, 0u, wire,
                   &reply, NULL) == ASTRA_SYSCALL_OK &&
          reply.reply.status == ASTRA_VFS_ERR_READ_ONLY,
          "read-only MKDIR: status %u", (unsigned)reply.reply.status);
    CHECK(raw_call(&ro, ro.service, ro.session, ASTRA_VFS_OP_OPEN,
                   ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE, wire, &reply,
                   NULL) == ASTRA_SYSCALL_OK &&
          reply.reply.status == ASTRA_VFS_ERR_READ_ONLY,
          "read-only OPEN for writing: status %u",
          (unsigned)reply.reply.status);
    CHECK(host_calls("mkdir") == mkdirs, "a refused MKDIR reached the provider");
    join(posix, sizeof(posix), directory, "ro-made");
    CHECK(stat(posix, &st) != 0 && errno == ENOENT, "read-only MKDIR made it");
    join(posix, sizeof(posix), directory, "readable");
    CHECK(write_text(posix, "ok", O_WRONLY | O_CREAT | O_EXCL) == 0,
          "seed readable");
    wire_path(wire, sizeof(wire), directory, "readable");
    CHECK(raw_call(&ro, ro.service, ro.session, ASTRA_VFS_OP_STAT, 0u, wire,
                   &reply, NULL) == ASTRA_SYSCALL_OK &&
          reply.reply.status == ASTRA_VFS_OK && reply.reply.node_size == 2u,
          "read-only STAT: status %u", (unsigned)reply.reply.status);
    (void)astra_close(derived);
    return 0;
}

/*
 * A session is refused on any port but its own grant's -- the same session
 * id sent on a read-only grant cannot borrow the writable grant's rights.
 */
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
    wire_path(wire, sizeof(wire), directory, "borrowed");
    /* The writable session's id, on the read-only grant's port. */
    CHECK(raw_call(&ro, ro.service, rw.session, ASTRA_VFS_OP_MKDIR, 0u, wire,
                   &reply, NULL) == ASTRA_SYSCALL_TIMED_OUT,
          "a foreign session was answered");
    /* A session id nobody opened. */
    CHECK(raw_call(&rw, rw.service, rw.session ^ 0x5A5Au, ASTRA_VFS_OP_MKDIR,
                   0u, wire, &reply, NULL) == ASTRA_SYSCALL_TIMED_OUT,
          "a forged session was answered");
    join(posix, sizeof(posix), directory, "borrowed");
    CHECK(stat(posix, &st) != 0 && errno == ENOENT,
          "a refused request made the directory");
    (void)astra_close(derived);
    return 0;
}

/*
 * A grant derived beneath a sub-directory sees that directory as its root
 * and cannot name its parent's tree or another volume.
 */
static int case_raw_subroot(const char *directory)
{
    RawClient rw;
    RawClient sub;
    AstraVfsReplyMessage reply;
    uint32_t derived = 0u;
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
    CHECK(raw_call(&sub, sub.service, sub.session, ASTRA_VFS_OP_STAT, 0u,
                   "/inside", &reply, NULL) == ASTRA_SYSCALL_OK &&
          reply.reply.status == ASTRA_VFS_OK && reply.reply.node_size == 2u,
          "sub sees inside: status %u", (unsigned)reply.reply.status);
    CHECK(raw_call(&sub, sub.service, sub.session, ASTRA_VFS_OP_STAT, 0u,
                   "/../outside", &reply, NULL) == ASTRA_SYSCALL_OK &&
          reply.reply.status != ASTRA_VFS_OK,
          "sub reached its parent through ..");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ, "../sound", &reply, NULL) ==
              ASTRA_SYSCALL_OK && reply.reply.status != ASTRA_VFS_OK,
          "derived a grant outside the volume");
    (void)astra_close(derived);
    return 0;
}
```

`wire + 1` drops the leading slash: a derived root is relative to the
parent's root. Register `{"raw-readonly", case_raw_readonly}`,
`{"raw-cross-grant", case_raw_cross_grant}` and
`{"raw-subroot", case_raw_subroot}`. In `test-fs-semantics.py`, extend
`CASES` with `"raw-readonly", "raw-cross-grant", "raw-subroot"`.

These cases call `astra_posix_startup` and `astra_startup_capability`. If
`<astra/posix.h>` does not declare the first, use the declaration
`file.c` uses (`grep -n "astra_posix_startup" sw/userspace/posix/include/astra/*.h`).
`astra_wait_one`'s deadline is absolute monotonic nanoseconds; confirm with
`grep -n -B6 "uint32_t astra_wait_one" ndk/include/astra/runtime.h`.

- [ ] **Step 9: Run, then perturb**

Sync, then on beast build userspace, run `make -C sw/userspace test`, build
`sw/boot`, and run the gate:

```sh
~/astra-mg/locked make -C sw/userspace -j32
~/astra-mg/locked make -C sw/userspace test
~/astra-mg/locked make -C sw/boot -j32
~/astra-mg/locked bash ~/astra-mg/images.sh
Q=$(tail -n1 /tmp/qemu-host-build.log); R=sw/boot/build/astra_boot.bin
~/astra-mg/locked python3 emu/qemu/test-fs-semantics.py $Q $R ~/astra-mg/base.img
```

Expected: the core and port tests PASS; `FSSEM PASS raw-readonly`,
`FSSEM PASS raw-cross-grant`, `FSSEM PASS raw-subroot`,
`ASTRA FS SEMANTICS PASS`.

Perturbations (restore and re-sync with the `--no-times` rsync after each):

1. HostFS boundary. Delete the read-only `goto answer_ready` block in
   `pump`. Expected: the port test fails on `backend_mkdir_calls == mkdirs`,
   and `raw-readonly` fails "a refused MKDIR reached the provider": QEMU's
   read-only session still refuses, but only after the round trip.
2. Provider boundary. With perturbation 1 still in place, open derived
   sessions without `ASTRA_HOST_FS_SESSION_READ_ONLY` in `hostfs_derive`.
   Expected: `raw-readonly` fails "read-only MKDIR made it". Both layers
   now provably refuse.
3. Session binding. In `astra_vfs_service_session_bound`, compare only the
   owner. Expected: `raw-cross-grant` fails "a foreign session was
   answered", and the core test fails on `!astra_vfs_service_session_bound(..., 2u)`.

- [ ] **Step 10: Full suite, measurement, commit**

`~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-task5.log`. Expected: green.
Run one `--bench t5` and compare it with `qemu-t3-1.json`; record the
`stat-warm` and `open-close-warm` ratios in the commit message.

```bash
git add sw/include/astra/vfs_service.h sw/userspace/vfs ndk/include/astra \
  sw/userspace/services/hostfs/main.c sw/userspace/supervisor/src/loader.c \
  sw/userspace/commands/fssem/fssem.c emu/qemu/test-fs-semantics.py \
  emu/qemu/astra_image.py sw/userspace/posix/Makefile sw/userspace/libraries.mk \
  sw/userspace/kits sw/userspace/config/Makefile sw/userspace/tests/test_boundaries.py \
  sw/userspace/lua/Makefile sw/userspace/apps/Terminal.app/manifest \
  sw/userspace/apps/InterfaceGallery.app/manifest ndk/docs/source/filesystem.md \
  tools/tests/test_check_bundle_dependencies.py docs/FILESYSTEM_KIT.md docs/ABI.md
git commit -m "feat(hostfs): grants are ports, enforced at HostFS and in the provider (FS15); storage protocol 28, Kit 6"
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
- Produces (host): `ASTRA_HOST_VERSION 0x0001000e`,
  `ASTRA_HOST_CAP_FS_RESOLVES (1u << 11)`, `ASTRA_HOST_FS_STAT_FOLLOW`
  (STAT flag), `ASTRA_HOST_FS_ESCAPE_PARENT`/`_SYMLINK`. On ESCAPED,
  `result_value` answers the bytes consumed and `value_lo` the kind.
- Produces (storage protocol 29): `ASTRA_VFS_FEATURE_RESOLVES_PATHS (1u << 1)`,
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

`sw/include/astra/host.h`: `ASTRA_HOST_VERSION` -> `0x0001000e`, and add:

```c
/** The provider resolves ".", ".." and symbolic links in order beneath a
 * session root, and answers ASTRA_STATUS_ESCAPED where a path leaves it. */
#define ASTRA_HOST_CAP_FS_RESOLVES (1u << 11)
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

- [ ] **Step 5: HostFS passes escapes up; the core carries them**

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
- `ASTRA_VFS_VERSION` -> 29. History: "Version 29 lets a provider resolve
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

`sw/userspace/vfs/src/backends/host.c` records the last escape per thread
(HostFS workers are threads of one executable, so `_Thread_local` is
available):

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

`vfs_service_core.c`:
- STAT accepts `request->flags == ASTRA_VFS_STAT_FOLLOW` when the
  session's version is at least 29, and routes it to `ops->stat_follow`
  (NULL: `ASTRA_VFS_ERR_UNSUPPORTED`). STAT_AT keeps refusing any flag here.
  Task 9 adds following to the directory-relative operations together.
- `handle_open` accepts `ASTRA_VFS_OPEN_NOFOLLOW` in the allowed flag set.
- At `done:`, before the stats update:

  ```c
      if (reply->status == ASTRA_VFS_ERR_ESCAPED &&
          (slot == NULL || slot->version < UINT16_C(29) ||
           service->backend.ops->escape == NULL ||
           service->backend.ops->escape(slot_context(service, slot),
                                        reply->payload,
                                        sizeof(reply->payload)) !=
               ASTRA_VFS_OK))
          reply->status = ASTRA_VFS_ERR_NOT_FOUND;
  ```

  A client that cannot continue an escape is told the name is not there,
  which is what the root's boundary means to it.
- The port layer's READ_PATH and READDIR_AREA (path form) answer ESCAPED
  the same way. On ESCAPED they call `ops->escape` into
  `outgoing->reply.payload`; an inline READ_PATH has no data on failure, so
  the payload is free.

`services/hostfs/main.c`: grants answer
`ASTRA_VFS_FEATURE_GRANTS | ASTRA_VFS_FEATURE_RESOLVES_PATHS`. In
`vfs_port_transport.c`'s `pump`, `features` becomes
`grant != NULL ? ASTRA_VFS_FEATURE_GRANTS | grant_features : 0u`. Give
`AstraVfsPortGrant` a `uint32_t features;` field set by HostFS to
`ASTRA_VFS_FEATURE_RESOLVES_PATHS`, so the port layer knows nothing about
paths. `astra_vfs_host_transport_init` requires `ASTRA_HOST_CAP_FS_RESOLVES`.

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
    if (flags != 0u && client->version < UINT16_C(29))
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

In `test_vfs_service_core.c`, add a case where the fake's `open` answers
`ASTRA_VFS_ERR_ESCAPED` and its `escape` fills a known record. Check that a
version-29 session receives the record in `reply.payload`, and that a
version-28 session receives `ASTRA_VFS_ERR_NOT_FOUND`.

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

`~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-task6.log`. Expected: green,
including the terminal gate's zsh paths, which use `..` in `cd`. Run one
`--bench t6` and compare it with `qemu-t3-1.json`.

```bash
git add emu/qemu/qemu-9.2 emu/qemu/tests sw/include/astra sw/userspace/vfs \
  ndk/include/astra sw/userspace/posix/src/path.c sw/userspace/posix/src/file.c \
  sw/userspace/services/hostfs/main.c sw/userspace/metrics sw/userspace/events \
  sw/userspace/supervisor/src/proc_tree.c sw/userspace/commands/fssem/fssem.c \
  emu/qemu/test-hostfs.py emu/qemu/test-fs-semantics.py emu/qemu/fs-gates.sh
git commit -m "feat(fs): ordered resolution beneath a grant root, escapes continue in the namespace (FS14); storage protocol 29"
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
- Modify: `sw/include/astra/vfs_service.h` (version 30, `AstraVfsNodeMeta`, `ASTRA_VFS_FEATURE_NODE_META`)
- Modify: `sw/userspace/vfs/include/astra/vfs_backend.h` (`AstraVfsNodeInfo` grows)
- Modify: `sw/userspace/vfs/src/vfs_service_core.c`, `sw/userspace/vfs/src/backends/host.c`, `sw/userspace/vfs/include/astra/vfs_host_transport.h`, `sw/userspace/vfs/src/vfs_host_transport.c`
- Modify: `ndk/include/astra/vfs_client.h`, `ndk/include/astra/vfs_union.h`, `ndk/include/astra/filesystem_library.h`, `sw/userspace/vfs/src/vfs_client.c`, `sw/userspace/vfs/src/vfs_union.c`, `sw/userspace/vfs/src/filesystem_library.c`, `sw/userspace/supervisor/src/loader.c`, `sw/userspace/vfs/filesystem.library.exports`
- Modify: `sw/userspace/posix/src/file.c` (`fill`)
- Modify: Kit major 7 (Task 3 Step 7's list)
- Modify: `sw/userspace/commands/fssem/fssem.c`, `emu/qemu/test-fs-semantics.py`
- Test: `sw/userspace/vfs/tests/test_vfs_host_backend.c`, `sw/userspace/vfs/tests/test_vfs_service_core.c`, `sw/userspace/vfs/tests/test_filesystem_library.c`

**Interfaces:**
- Produces (wire 30): `AstraVfsNodeMeta` (72 bytes) in the payload of OPEN,
  OPEN_AT, STAT, STAT_AT and STAT_FILE replies when `valid != 0`; the
  `ASTRA_VFS_META_*` bits; `ASTRA_VFS_FEATURE_NODE_META (1u << 2)`.
- Produces (Kit 7): `astra_vfs_stat_meta`, `astra_vfs_stat_at_meta`,
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
    assert(info.volume == 1u);              /* the first device seen */
    assert(info.generation == UINT64_C(7000000005));
```

A STAT answer with another device (`device_lo = 2u`) gives `volume == 2u`.
The first device again gives `1u` again.

`test_vfs_service_core.c`: the fake's `stat` fills
`info->valid = ASTRA_VFS_META_INODE | ASTRA_VFS_META_VOLUME | ASTRA_VFS_META_MODE`,
`inode = 99`, `volume = 3`, `mode = 0`. A version-30 STAT reply carries an
`AstraVfsNodeMeta` with those values and `ASTRA_VFS_META_MODE` set (a mode
of 0 that is real). A version-29 reply's payload is all zero.

Run: `~/astra-mg/locked make -C sw/userspace/vfs test`. Expected: compile
errors.

- [ ] **Step 2: The wire and the backend record**

`vfs_service.h`: version 30 ("Version 30 carries node identity, an
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
    if (version >= UINT16_C(30) && info->valid != 0u) {
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

The volume map belongs to the provider connection. Add to
`AstraVfsHostTransport` (`vfs_host_transport.h`):

```c
    /* Astra volume identities: index + 1 is the identity of the Linux
     * filesystem whose device number is stored there. Grown on demand;
     * cleared with the transport (a new provider generation, Task 10). */
    uint64_t *volume_devices;
    uint32_t volume_count;
    uint32_t volume_capacity;
```

and free `volume_devices` in `astra_vfs_host_transport_destroy`. In
`host.c`:

```c
/* The identity of the filesystem on `device`, assigned the first time it
 * is seen. Zero only when the table cannot grow. */
static uint32_t volume_identity(AstraVfsHostTransport *transport,
                                uint64_t device)
{
    uint32_t identity = 0u;

    if (transport->acquire != NULL &&
        !transport->acquire(transport->lock_context))
        return 0u;
    for (uint32_t index = 0u; index < transport->volume_count; ++index)
        if (transport->volume_devices[index] == device) {
            identity = index + 1u;
            break;
        }
    if (identity == 0u) {
        if (transport->volume_count == transport->volume_capacity) {
            uint32_t capacity = transport->volume_capacity == 0u ? 4u :
                                transport->volume_capacity * 2u;
            uint64_t *grown = astra_runtime_reallocate(
                transport->volume_devices, capacity * sizeof(*grown));

            if (grown != NULL) {
                transport->volume_devices = grown;
                transport->volume_capacity = capacity;
            }
        }
        if (transport->volume_count < transport->volume_capacity) {
            transport->volume_devices[transport->volume_count++] = device;
            identity = transport->volume_count;
        }
    }
    if (transport->release != NULL)
        transport->release(transport->lock_context);
    return identity;
}
```

`publish_info` takes the backend and fills the rest from the node:

```c
    const AstraHostNode *node = &command->node;
    uint32_t valid = node->valid;
    ...
    if ((valid & ASTRA_HOST_NODE_DEVICE) != 0u) {
        info->volume = volume_identity(
            backend->transport,
            ((uint64_t)node->device_hi << 32) | node->device_lo);
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

- [ ] **Step 4: The Kit and libc**

- `vfs_client.c`: `astra_vfs_stat_meta`, `astra_vfs_stat_at_meta`,
  `astra_vfs_stat_file_meta` and `astra_vfs_lookup` take
  `AstraVfsNodeMeta *meta` after the entry. When `meta != NULL`, zero it,
  and when `client->version >= 30` copy `call->reply.payload` into it.
  Update the six declarations, and every caller (`grep -rn "astra_vfs_stat_meta\|astra_vfs_stat_at_meta\|astra_vfs_stat_file_meta\|astra_vfs_lookup" sw ndk | grep -v /build/`):
  callers that do not need identity pass `NULL`.
- `vfs_union.c`: `astra_vfs_assign_stat` and `astra_vfs_assign_lstat` take
  `AstraVfsNodeMeta *meta` after `entry`, and pass it down to the lookup that
  finds the node (`assign_raw_lookup`, or `assign_raw_lstat`'s
  `astra_vfs_stat_meta`). `filesystem_locate` and `filesystem_locate_literal`
  pass a meta through.
- `filesystem_library.h`: append to `AstraFileInfo`:

  ```c
      /* Filesystem Kit 7: identity and the rest of the metadata, filled
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
- Kit major 7: Task 3 Step 7's edit with `7`.

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
git commit -m "feat(fs): node identity and validity on the wire; st_dev/st_ino from the provider (FS05, FS22); storage protocol 30, Kit 7"
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
(`vfs.cpp:5026-5080`) inherits `file_descriptor`s. Before the clone, the
parent asks HostFS to *share* each open file. HostFS duplicates the Linux
descriptor (`dup(2)`: the same open file description, so the same
position) and holds it under a random 64-bit token, bound to the grant.
The child *adopts* the token over its own session on the same grant. The
token lives in memory both processes have, so no pathname is ever
reopened. The parent drops unclaimed tokens when it reaps the child. Exec
needs nothing new (Decisions, D5): it is in place and already honors
`FD_CLOEXEC`.

**Files:**
- Modify: `sw/include/astra/host.h` (`SEEK`, `DUP`, `ASTRA_HOST_FS_IO_SEQUENTIAL`, capability, version)
- Modify: `emu/qemu/qemu-9.2/hw/m68k/astra68.c`
- Modify: `sw/include/astra/vfs_service.h` (version 31, `SEEK`, `SHARE`, `ADOPT`, `ASTRA_VFS_IO_SEQUENTIAL`, feature)
- Modify: `sw/userspace/vfs/include/astra/vfs_backend.h`, `sw/userspace/vfs/include/astra/vfs_service_core.h`, `sw/userspace/vfs/src/vfs_service_core.c`, `sw/userspace/vfs/src/vfs_port_transport.c`, `sw/userspace/vfs/src/backends/host.c`
- Modify: `ndk/include/astra/vfs_client.h`, `ndk/include/astra/filesystem_library.h`, `sw/userspace/vfs/src/vfs_client.c`, `sw/userspace/vfs/src/filesystem_library.c`, `sw/userspace/vfs/filesystem.library.exports` (Kit 7.1)
- Modify: `sw/userspace/services/hostfs/main.c` (share storage, entropy seed), `emu/qemu/astra_image.py` (`shares 64`)
- Modify: `sw/userspace/posix/src/file.c`, `sw/userspace/posix/src/process.c`, `sw/userspace/posix/include/astra/posix_descriptor.h`
- Modify: `sw/userspace/commands/fssem/fssem.c`, `emu/qemu/test-fs-semantics.py`, `emu/qemu/test-hostfs.py`
- Test: `sw/userspace/vfs/tests/test_vfs_service_core.c`, `sw/userspace/vfs/tests/test_vfs_host_backend.c`, `sw/userspace/vfs/tests/test_filesystem_library.c`

**Interfaces:**
- Produces (host): ops `ASTRA_HOST_FS_SEEK` (20) and `ASTRA_HOST_FS_DUP`
  (21), with `ASTRA_HOST_FS_MAX = ASTRA_HOST_FS_DUP`; READ/WRITE flag
  `ASTRA_HOST_FS_IO_SEQUENTIAL (1u << 14)`; `ASTRA_HOST_CAP_FS_DESCRIPTIONS (1u << 12)`;
  `ASTRA_HOST_VERSION 0x0001000f`; `ASTRA_HOST_METRICS_VERSION 4`.
- Produces (wire 31): `ASTRA_VFS_FEATURE_DESCRIPTIONS (1u << 3)`;
  `ASTRA_VFS_IO_SEQUENTIAL (1u << 1)` on READ, READ_AREA, WRITE and
  WRITE_AREA (the offset is ignored; `node_size` answers the position after);
  `ASTRA_VFS_OP_SEEK` (37: `file`, `offset` as int64, `flags` 0 set /
  1 current / 2 end; `node_size` answers the position);
  `ASTRA_VFS_OP_SHARE` (38: `file`; `cursor` answers the token);
  `ASTRA_VFS_OP_ADOPT` (39: `offset` the token; `file`, `kind` and
  `node_size` answer the adopted handle; flag `ASTRA_VFS_ADOPT_DISCARD`
  withdraws an unclaimed share instead).
- Produces (backend, optional): `read_sequential`, `seek`, `share`.
- Produces (core): `astra_vfs_service_set_shares(service, AstraVfsPendingShare *, capacity, seed)`.
- Produces (Kit 7.1): `astra_vfs_seek`, `astra_vfs_share`, `astra_vfs_adopt`,
  `astra_vfs_unshare`, `astra_vfs_port_read_bulk_position`,
  `astra_filesystem_share(AstraFile *, uint64_t *token)`,
  `astra_filesystem_adopt(AstraFile *, uint64_t token)`,
  `astra_filesystem_unshare(const AstraFile *, uint64_t token)`.
- Produces (libc): `astra_posix_file_share_all(uint64_t *tokens, uint32_t capacity, uint32_t *count) -> int`,
  `astra_posix_file_unshare(const uint64_t *tokens, uint32_t count)`.
- Consumed by: Task 9 (cwd crosses fork by the same share).

- [ ] **Step 1: Failing core test**

In `test_vfs_service_core.c`, give the fake backend `share` (it returns a
new node that refers to the same fake file, `*shared = node + 1000u`) and
`seek`, and provide share storage:

```c
static AstraVfsPendingShare shares[4];

static void
test_a_share_is_adopted_once_on_its_grant(void)
{
    AstraVfsService service;
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t parent;
    uint32_t child;
    uint32_t stranger;
    uint32_t file;
    uint64_t token;

    service_start(&service);
    assert(astra_vfs_service_set_shares(&service, shares, 4u,
                                        UINT64_C(0x68040)));
    memset(&request, 0, sizeof(request));
    request.size = ASTRA_VFS_REQUEST_SIZE;
    request.version = ASTRA_VFS_VERSION;
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_HELLO, &request, &reply);
    parent = reply.session;
    astra_vfs_service_dispatch_grant(&service, 8u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_HELLO, &request, &reply);
    child = reply.session;
    astra_vfs_service_dispatch_grant(&service, 9u, 2u, NULL, 0u,
                                     ASTRA_VFS_OP_HELLO, &request, &reply);
    stranger = reply.session;

    request.session = parent;
    request.flags = ASTRA_VFS_OPEN_READ;
    strcpy((char *)request.body.path, "/a");
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    file = reply.file;
    request.file = file;
    request.flags = 0u;
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_SHARE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    token = reply.cursor;
    assert(token != 0u);

    /* Another grant cannot adopt it, even knowing the token. */
    memset(&request.body, 0, sizeof(request.body));
    request.file = ASTRA_VFS_FILE_INVALID;
    request.offset = token;
    request.session = stranger;
    astra_vfs_service_dispatch_grant(&service, 9u, 2u, NULL, 0u,
                                     ASTRA_VFS_OP_ADOPT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_BAD_HANDLE);

    /* The child adopts it once: a new handle on the shared node. */
    request.session = child;
    astra_vfs_service_dispatch_grant(&service, 8u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_ADOPT, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(reply.file != ASTRA_VFS_FILE_INVALID && reply.file != file);
    astra_vfs_service_dispatch_grant(&service, 8u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_ADOPT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_BAD_HANDLE);

    /* An unclaimed share dies with its sharer's session. */
    request.session = parent;
    request.file = file;
    request.offset = 0u;
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_SHARE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    astra_vfs_service_release_session(&service, parent);
    request.session = child;
    request.file = ASTRA_VFS_FILE_INVALID;
    request.offset = reply.cursor;
    astra_vfs_service_dispatch_grant(&service, 8u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_ADOPT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_BAD_HANDLE);
    assert(fake_shared_open == 1u);   /* the adopted node is still open */
}
```

`fake_shared_open` counts the fake's shared nodes not yet closed: `share`
increments it and `close` of a node above 1000 decrements it. Register the
test. Expected: compile errors.

- [ ] **Step 2: The provider**

`host.h`:

```c
/** The provider keeps each handle's position (its Linux open file
 * description): SEEK, DUP and IO_SEQUENTIAL. */
#define ASTRA_HOST_CAP_FS_DESCRIPTIONS (1u << 12)
/** READ/WRITE flag: at the handle's position, which advances; offset ignored.
 * value_hi/value_lo answer the position after. */
#define ASTRA_HOST_FS_IO_SEQUENTIAL     (1u << 14)
```

and append `ASTRA_HOST_FS_SEEK` (`handle`; `offset_hi`/`offset_lo` a
signed offset; `flags` 0 set, 1 current, 2 end; `value_hi`/`value_lo`
answer the position) and `ASTRA_HOST_FS_DUP` (`handle`; `handle` answers a
new handle sharing the description) to the operation enum after `CLAIM`.
Set `ASTRA_HOST_FS_MAX` to `ASTRA_HOST_FS_DUP`, `ASTRA_HOST_VERSION` to
`0x0001000f` and `ASTRA_HOST_METRICS_VERSION` to 4. Register the two
operations (Global Constraints): `seek` and `dup` in the three lists.

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
  flag bit is INVALID. It answers the position with
  `lseek(file->fd, 0, SEEK_CUR)` for append and sequential writes, and
  `offset + moved` for positioned ones. On macOS the append path is a seek
  and a write under the handle's lock: atomic only per handle. It is a
  development convenience there, and the gates run on Linux.
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

- DUP:

  ```c
      case ASTRA_HOST_FS_DUP: {
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
          /* Same description, same flags; a directory gets its own stream
           * over the shared position. */
          status = astra_host_publish_open(s, owner, session_id, command, copy,
                                           file->flags);
          break;
      }
  ```

- The capability register adds `ASTRA_HOST_CAP_FS_DESCRIPTIONS`.

`test-hostfs.py` gains a check that two handles from DUP share a position.
Open, `WRITE flags=IO_SEQUENTIAL` "ab" on handle A, the same "cd" on DUP
handle B; READ positioned at 0 for 4 answers "abcd"; `SEEK flags=1 offset=0`
on A answers 4. Then a positioned WRITE of "X" at 0 on a handle opened with
`OPEN_APPEND` leaves the length at 4 and byte 0 "X". Before this task the
same command appended.

- [ ] **Step 3: The wire, the backend and the core**

`vfs_service.h`: version 31 ("Version 31 lets the service hold a handle's
position: IO_SEQUENTIAL, SEEK, SHARE and ADOPT"), and:

```c
/** HELLO feature: the service holds each open description's position. */
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

`vfs_backend.h`, three optional operations after `escape`:

```c
    /** Read at the node's own position, advancing it; *position answers it
     * after. NULL: the backend holds no positions. */
    uint32_t (*read_sequential)(void *context, uintptr_t node, void *buffer,
                                uint32_t length, uint32_t *moved,
                                uint64_t *position);
    /** Move the node's position (whence 0 start, 1 current, 2 end). */
    uint32_t (*seek)(void *context, uintptr_t node, int64_t offset,
                     uint32_t whence, uint64_t *position);
    /** A second node over the same open description (position shared). */
    uint32_t (*share)(void *context, uintptr_t node, uintptr_t *shared);
```

`write` already takes `flags` and answers `position`: the core passes
`ASTRA_VFS_IO_SEQUENTIAL` through, and the host backend maps it to
`ASTRA_HOST_FS_IO_SEQUENTIAL`. `host.c` implements `host_read_sequential`
(READ with the flag; `*position` from `value_hi`/`value_lo`), `host_seek`
(SEEK) and `host_share` (DUP), and adds them to `host_ops`.

`vfs_service_core.h`:

```c
/* A description held for another process of the same grant (SHARE), until
 * adopted (ADOPT), withdrawn, or its sharer's session ends. */
typedef struct AstraVfsPendingShare {
    uint64_t token;          /* 0 when the slot is free */
    uintptr_t node;          /* the backend's shared node */
    void *backend_context;
    uint32_t grant;
    uint32_t from_session;
    uint32_t flags;          /* the shared handle's open flags */
    uint16_t kind;
    uint16_t reserved;
} AstraVfsPendingShare;

int astra_vfs_service_set_shares(AstraVfsService *service,
                                 AstraVfsPendingShare *shares,
                                 uint32_t capacity, uint64_t seed);
```

and `AstraVfsService` gains `AstraVfsPendingShare *shares; uint32_t share_capacity; uint64_t share_seed; uint64_t share_sequence;`.

`vfs_service_core.c`:

```c
/* splitmix64: tokens nobody can predict without the seed. */
static uint64_t
share_token(AstraVfsService *service)
{
    uint64_t z;

    do {
        z = service->share_seed + ++service->share_sequence *
                                      UINT64_C(0x9E3779B97F4A7C15);
        z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
        z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
        z ^= z >> 31;
    } while (z == 0u);
    return z;
}

int
astra_vfs_service_set_shares(AstraVfsService *service,
                             AstraVfsPendingShare *shares, uint32_t capacity,
                             uint64_t seed)
{
    if (service == NULL || shares == NULL || capacity == 0u)
        return 0;
    memset(shares, 0, capacity * sizeof(*shares));
    service->shares = shares;
    service->share_capacity = capacity;
    service->share_seed = seed;
    return 1;
}

/* Called with the state lock held; returns with it held. */
static void
share_drop(AstraVfsService *service, AstraVfsPendingShare *share)
{
    uintptr_t node = share->node;
    void *context = share->backend_context;

    memset(share, 0, sizeof(*share));
    state_release(service);
    (void)service->backend.ops->close(context, node);
    (void)state_acquire(service);
}
```

Dispatch cases (each requires `slot->version >= 31` and the backend
operation, else `ASTRA_VFS_ERR_UNSUPPORTED`):

- SEEK: `find_file`; `file_acquire`; `ops->seek(file->backend_context, file->node, (int64_t)request->offset, request->flags, &position)`;
  `reply->node_size = position`. `flags > 2u` is INVALID.
- SHARE: `find_file`. Find a free share slot (none: `ASTRA_VFS_ERR_LIMIT`)
  and call `ops->share(file->backend_context, file->node, &shared)` outside
  the lock. Fill the slot with `token = share_token(service)`,
  `grant = slot->grant`, `from_session = slot->id` and the file's flags and
  kind. `reply->cursor = token`.
- ADOPT: scan for `token == request->offset && grant == slot->grant`.
  - With `ASTRA_VFS_ADOPT_DISCARD`: the match must also have
    `from_session == slot->id`; `share_drop` it, and answer OK.
  - Without: no match is BAD_HANDLE. A match becomes a new open file of
    this session through the same slot-claiming path `handle_open` uses:
    split out its "find a free file slot" block (`:772-803`) into
    `static int file_slot_claim(AstraVfsService *, uint32_t session, uint32_t *index, uint32_t *status)`
    and use it from both. Give the file `node`, `backend_context`, `flags`
    and `kind`, and clear the share slot without closing the node, which
    the file now owns. Answer `reply->file`, `reply->kind`, and
    `reply->node_size` from `ops->stat_node` when it succeeds.
- `reap_session` drops every share whose `from_session` is the reaped
  session (`share_drop`). `astra_vfs_service_shutdown` drops all.
- READ, READ_AREA, WRITE and WRITE_AREA with `ASTRA_VFS_IO_SEQUENTIAL`.
  `read_file` gains `uint32_t flags, uint64_t *position`, and
  `read_into_unlocked`, `astra_vfs_service_read_into` and `handle_read`
  pass them. A sequential read calls `ops->read_sequential` (NULL:
  UNSUPPORTED). `write_file` already passes `flags`: allow
  `ASTRA_VFS_IO_SEQUENTIAL` beside `ASTRA_VFS_WRITE_APPEND` in its check
  (`:1000-1001`), and answer `reply->node_size = position` for both kinds.
  In `vfs_port_transport.c`'s READ_AREA branch, pass
  `incoming->request.flags` and answer `node_size` with the position when
  the version is at least 31.

HostFS: allocate `shares` (settings `shares 64`, at least 1) for each of the
two services. Seed both from host entropy, through its own transport (the
entropy service is the host's, and HostFS holds the device):

```c
static uint64_t share_seed(void)
{
    AstraVfsHostRequest request;
    uint64_t seed = 0u;

    if (astra_vfs_host_transport_begin(&transport, sizeof(seed),
                                       &request) != ASTRA_VFS_OK)
        return 0u;
    memset(request.command, 0, sizeof(*request.command));
    request.command->size = sizeof(*request.command);
    request.command->version = ASTRA_HOST_COMMAND_VERSION;
    request.command->service = ASTRA_HOST_SERVICE_ENTROPY;
    request.command->operation = ASTRA_HOST_ENTROPY_FILL;
    request.command->generation = transport.generation;
    if (astra_vfs_host_transport_submit(&transport, &request, NULL, 0u,
                                        &seed, sizeof(seed)) != ASTRA_VFS_OK)
        return 0u;
    return seed;
}
```

A zero seed refuses startup (`ASTRA_STATUS_IO`). HostFS grants add
`ASTRA_VFS_FEATURE_DESCRIPTIONS`. `astra_vfs_host_transport_init` requires
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
    if (client->version < UINT16_C(31))
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
    if (client->version < UINT16_C(31))
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

The child's `AstraFile` still points at the client the fork copied. Task 3
made the fork child abandon every connected client and reconnect lazily
(`vfs_process.c`, `seed_process_vfs`), so the adopt's exchange opens the
child's own session on the same grant handle.

Declare the new functions with doc comments in `vfs_client.h`,
`vfs_port_transport.h` and `filesystem_library.h`, add them to
`filesystem.library.exports` in sorted order, and set
`ASTRA_FILESYSTEM_LIBRARY_ABI_MINOR` to `1u` (identity `7, 1, 0`; the
Makefile check `'^filesystem.library.7 7.1.0 ABI 7.1$$'`).

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
1. QEMU DUP: replace `fcntl(file->fd, F_DUPFD_CLOEXEC, 0)` with a fresh
   `openat` of the same path. Use `/proc/self/fd/N` on Linux:
   `open(g_strdup_printf("/proc/self/fd/%d", file->fd), O_RDWR)`.
   Expected: `shared-description` fails "the child's write did not move the
   parent's position", and `test-hostfs.py`'s DUP check fails.
2. QEMU append: call `write` instead of `pwritev2(..., RWF_APPEND)` for
   append writes. Expected: `append-race` fails with a short size or an
   interleaved line. Run it three times: a race can pass once.
3. Open with `O_APPEND` again in `astra_host_native_open_flags`. Expected:
   `shared-description` fails "pwrite on an append description appended".
4. libc: skip `astra_filesystem_adopt` in the fork child. Expected: the
   child's write uses the parent's handle on another session and is refused
   (BAD_HANDLE), so `shared-description` fails "child write failed".

`~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-task8.log`. Expected: green.
The terminal gate's zsh pipelines and `fsstress` (which forks) must still
pass. Run one `--bench t8` and compare it with `qemu-t3-1.json`;
`seq-read-warm`, `seq-write` and `append` are the workloads this task
touches.

```bash
git add sw/include/astra emu/qemu/qemu-9.2/hw/m68k/astra68.c sw/userspace/vfs ndk \
  sw/userspace/services/hostfs/main.c emu/qemu/astra_image.py sw/userspace/posix \
  sw/userspace/commands/fssem/fssem.c emu/qemu/test-fs-semantics.py \
  emu/qemu/test-hostfs.py emu/qemu/test-filesystem-stress.py
git commit -m "feat(fs): the provider holds positions; fork shares descriptions by token (FS20); storage protocol 31"
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
  (`vfs_service.h:224-234`). Version 32 lets MKDIR, RENAME, LINK, SYMLINK and
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
- Modify: `sw/include/astra/vfs_service.h` (version 32, `DIRECTORY_PATH`, feature, `STAT_AT` follow)
- Modify: `sw/userspace/vfs/include/astra/vfs_backend.h`, `sw/userspace/vfs/src/vfs_service_core.c`, `sw/userspace/vfs/src/backends/host.c`
- Modify: `ndk/include/astra/vfs_client.h`, `ndk/include/astra/filesystem_library.h`, `sw/userspace/vfs/src/vfs_client.c`, `sw/userspace/vfs/src/filesystem_library.c`, `sw/userspace/vfs/filesystem.library.exports` (Kit 7.2)
- Modify: `sw/userspace/posix/src/file.c`
- Modify: `sw/userspace/commands/fssem/fssem.c`, `emu/qemu/test-fs-semantics.py`
- Test: `sw/userspace/vfs/tests/test_vfs_service_core.c`, `sw/userspace/vfs/tests/test_vfs_host_backend.c`

**Interfaces:**
- Consumes: Task 4's base handles (`handle`, and `value_lo` for the second
  operand); Task 6's `note_escape`, `host_readlink_at` and
  `ASTRA_VFS_STAT_FOLLOW`; Task 8's `astra_filesystem_share/adopt/unshare`
  and `astra_posix_file_share_all/unshare`.
- Produces (host): `ASTRA_HOST_FS_PATH` (22) with `ASTRA_HOST_FS_MAX` set to it;
  `ASTRA_HOST_CAP_FS_PATHS (1u << 13)`; `ASTRA_HOST_VERSION 0x00010010`;
  `ASTRA_HOST_METRICS_VERSION 5`.
- Produces (wire 32): `ASTRA_VFS_FEATURE_DIRECTORY_PATH (1u << 4)`;
  `ASTRA_VFS_OP_DIRECTORY_PATH` (40: `file`; `length` the capacity;
  `payload`/`count` answer the path beneath the grant root). MKDIR, READLINK
  and SYMLINK (for its link path) take a base directory in `file`. RENAME
  and LINK take the source's base in `file` and the destination's in
  `offset`. `ASTRA_VFS_FILE_INVALID` (0) is the grant root with an absolute
  path, as before. A real base requires a relative, non-empty path. STAT_AT
  accepts `ASTRA_VFS_STAT_FOLLOW`.
- Produces (backend, optional): `mkdir_at`, `rename_at`, `link_at`,
  `symlink_at`, `readlink_at`, `stat_at_follow`, `directory_path`.
- Produces (Kit 7.2): `astra_vfs_mkdir_at`, `astra_vfs_rename_at`,
  `astra_vfs_link_at`, `astra_vfs_symlink_at`, `astra_vfs_readlink_at`,
  `astra_vfs_stat_at_follow_meta`, `astra_vfs_directory_path`;
  `astra_filesystem_mkdir_at`, `astra_filesystem_rename_at`,
  `astra_filesystem_link_at`, `astra_filesystem_symlink_at`,
  `astra_filesystem_readlink_at`, `astra_filesystem_stat_at_follow`,
  `astra_filesystem_directory_path`.

- [ ] **Step 1: Failing core test**

In `test_vfs_service_core.c`, give the fake backend `mkdir_at` (records the
node, path and mode it was called with in `fake_last_node`,
`fake_last_path` and `fake_last_mode`) and `directory_path` (answers
`"/d"`). Make its `open` answer `ASTRA_VFS_KIND_DIRECTORY` for any path
ending in `dir`. Then:

```c
static void
test_relative_mkdir_and_directory_path(void)
{
    AstraVfsService service;
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t session;
    uint32_t directory;
    uint32_t plain;

    service_start(&service);
    memset(&request, 0, sizeof(request));
    request.size = ASTRA_VFS_REQUEST_SIZE;
    request.version = ASTRA_VFS_VERSION;
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_HELLO, &request, &reply);
    session = reply.session;
    request.session = session;
    request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY;
    strcpy((char *)request.body.path, "/dir");
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    directory = reply.file;
    request.flags = ASTRA_VFS_OPEN_READ;
    strcpy((char *)request.body.path, "/a");
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_OPEN, &request, &reply);
    plain = reply.file;

    /* A base handle carries a relative path to the backend's _at form. */
    request.flags = 0u;
    request.file = directory;
    request.offset = 0750u;
    strcpy((char *)request.body.path, "sub/new");
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_MKDIR, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(fake_last_node != 0u && strcmp(fake_last_path, "sub/new") == 0 &&
           fake_last_mode == 0750u);

    /* ...and refuses an absolute path, a file as base, an old version. */
    strcpy((char *)request.body.path, "/abs");
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_MKDIR, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_INVALID);
    strcpy((char *)request.body.path, "x");
    request.file = plain;
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_MKDIR, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_NOT_DIR);

    /* Where the directory is now. */
    request.file = directory;
    request.length = ASTRA_VFS_PATH_MAX;
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_DIRECTORY_PATH, &request,
                                     &reply);
    assert(reply.status == ASTRA_VFS_OK && reply.count == 2u &&
           memcmp(reply.payload, "/d", 2u) == 0);
}
```

Register it. Expected: compile errors (`ASTRA_VFS_OP_DIRECTORY_PATH`, the
backend fields).

- [ ] **Step 2: The provider**

`host.h`:

```c
/** The provider reports where a handle is: ASTRA_HOST_FS_PATH. */
#define ASTRA_HOST_CAP_FS_PATHS (1u << 13)
```

Append `ASTRA_HOST_FS_PATH` after `ASTRA_HOST_FS_DUP` with the doc comment
"Where `handle` is now, beneath the session root, into the data span
(`result_length` bytes); NOT_FOUND when it has no such path". Set
`ASTRA_HOST_FS_MAX` to `ASTRA_HOST_FS_PATH`, `ASTRA_HOST_VERSION` to
`0x00010010` and `ASTRA_HOST_METRICS_VERSION` to 5. Register `path` in the
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

`vfs_service.h`: version 32 ("Version 32 lets a path operation name the
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
"`ASTRA_VFS_STAT_FOLLOW` (version 32) follows a final link".

`vfs_backend.h`, after `share`:

```c
    /* Version 32: path operations relative to an open directory node; a
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

`vfs_service_core.c`:

```c
/*
 * Version 32: the directory a path is relative to. ASTRA_VFS_FILE_INVALID
 * is the grant root, with the path as it always was. A real base is held
 * (file_acquire) for the call, and its path must be relative.
 */
static uint32_t base_node(AstraVfsService *service, AstraVfsSessionSlot *slot,
                          uint32_t handle, const uint8_t *path,
                          AstraVfsOpenFile **held, uintptr_t *node)
{
    AstraVfsOpenFile *file;
    uint32_t status;

    *held = NULL;
    *node = 0u;
    if (handle == ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_OK;
    if (slot->version < UINT16_C(32))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    if (path[0] == '/' || path[0] == '\0')
        return ASTRA_VFS_ERR_INVALID;
    file = find_file(service, slot->id, handle, &status);
    if (file == NULL)
        return status;
    if (file->kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_NOT_DIR;
    if (!file_acquire(service, file))
        return ASTRA_VFS_ERR_IO;
    *held = file;
    *node = file->node;
    return ASTRA_VFS_OK;
}
```

Dispatch changes. Each one, after its existing version check, calls
`base_node`. When it answers a base, the case calls the `_at` operation
(NULL: `ASTRA_VFS_ERR_UNSUPPORTED`) with `slot_context(service, slot)`
between `state_release` and `state_acquire`, then `file_release`s the base.
Without a base it runs the existing code unchanged.
- MKDIR: base from `request->file`; `mkdir_at(ctx, node, path, mode)`.
- READLINK: base from `request->file`; `readlink_at`, with the reply
  handling the path form already has.
- SYMLINK: base from `request->file`, checked against the link path
  `symlink->to`; `symlink_at(ctx, target, node, to)`.
- RENAME and LINK: two bases, `request->file` against `body.path` and
  `(uint32_t)request->offset` against `to`. The old form needs both bases
  to be 0, so a request with `offset` above `UINT32_MAX` is INVALID. When
  either base is real, call `rename_at`/`link_at` with 0 for the root side.
  Release both bases afterwards.
- STAT_AT: accept `request->flags == ASTRA_VFS_STAT_FOLLOW` when
  `slot->version >= 32`, and route it to `stat_at_follow`.
- DIRECTORY_PATH:

  ```c
      case ASTRA_VFS_OP_DIRECTORY_PATH: {
          uint32_t length = 0u;
          uint32_t capacity = request->length < ASTRA_VFS_PATH_MAX - 1u ?
              request->length : ASTRA_VFS_PATH_MAX - 1u;

          if (slot->version < UINT16_C(32) ||
              service->backend.ops->directory_path == NULL) {
              reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
              break;
          }
          file = find_file(service, slot->id, request->file, &status);
          if (file == NULL) {
              reply->status = status;
              break;
          }
          if (!file_acquire(service, file)) {
              reply->status = ASTRA_VFS_ERR_IO;
              break;
          }
          state_release(service);
          reply->status = service->backend.ops->directory_path(
              file->backend_context, file->node, (char *)reply->payload,
              capacity, &length);
          (void)state_acquire(service);
          file_release(service, file);
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
  `BUFFER_TOO_SMALL`, which `getcwd` reports as `ENAMETOOLONG`.

HostFS grants add `ASTRA_VFS_FEATURE_DIRECTORY_PATH` when the lease has
`ASTRA_HOST_CAP_FS_PATHS`. `astra_vfs_host_transport_init` does not
require it, because the macOS provider lacks it.

- [ ] **Step 4: The Kit**

`vfs_client.c`. Split the body of `astra_vfs_stat_at_meta` into
`static uint32_t stat_at_common(client, directory, path, flags, entry, meta)`,
which sets `call->request.flags = flags` and refuses a non-zero `flags`
below version 32. Then:

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
    if (client->version < UINT16_C(32))
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
    if (client->version < UINT16_C(32))
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
    if (client->version < UINT16_C(32))
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
    if (client->version < UINT16_C(32))
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
`relative_path_valid` check and the version-32 gate. Write it by moving
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
`7, 2, 0`; the Makefile check `'^filesystem.library.7 7.2.0 ABI 7.2$$'`).

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
   `stat` uses the parent's handle on another session (BAD_HANDLE, so
   `EBADF`), and `cwd-rename` fails "the fork child lost the cwd".

`~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-task9.log`. Expected: green.
The terminal gate's zsh `cd` paths are the main consumer. Run one
`--bench t9` and compare it with `qemu-t3-1.json`. Relative names now cost
the same as absolute ones, so `stat-warm` should not regress.

```bash
git add sw/include/astra emu/qemu/qemu-9.2/hw/m68k/astra68.c sw/userspace/vfs ndk \
  sw/userspace/services/hostfs/main.c sw/userspace/posix/src/file.c \
  sw/userspace/commands/fssem/fssem.c emu/qemu/test-fs-semantics.py \
  emu/qemu/test-hostfs.py emu/qemu/test-filesystem-stress.py
git commit -m "feat(fs): cwd is a held directory; getcwd asks the provider (FS19); storage protocol 32"
```

---

### Task 10: Provider loss (D6)

Two losses, one answer. A client whose files or service are gone gets
`ASTRA_VFS_ERR_LOST`, and POSIX reports it as `ESTALE`, the errno NFS uses
for the same situation. It never gets a wrong file, a hang, or
`ENOENT`. Each loss leaves a defined way back (FS35).

- **The provider loses its state** (QEMU's provider restarts; in Phase 1
  the seam is a QOM write, `astra-host-fs-reset`). Every handle and session
  goes, and so does the claim. Until HostFS claims again, its commands
  answer `ASTRA_STATUS_LOST` instead of `ACCESS`. HostFS then recovers in
  place: it claims again, reopens each grant's provider session from the
  grant's recorded volume and root, and advances its services' *epoch*.
  Every file opened before the epoch answers LOST, and new opens through the
  same grant work. The grant ports never closed, so no process loses its
  namespace. Reopening by recorded path is what a restarted Linux provider
  would have to do too. A grant whose root directory was renamed after it
  was made reopens at the old name, or fails. HostFS says so in the log
  (below).
- **HostFS dies** (killed, or faulted). The kernel closes its HOST_DEVICE,
  and QEMU drops its claim, sessions and handles (Task 4). The supervisor
  restarts it, and the new instance publishes new grant ports. Processes
  holding the old ports get LOST on every call: the Kit turns the transport's
  PEER into LOST. Processes launched afterwards get the new grants.
  Re-binding a running process's namespace needs Phase 2's live namespace
  publication (FS16). Until then, a running program reopens its files only
  by being restarted. That is an open question for the user, recorded in
  the Self-Review.

**Files:**
- Modify: `sw/include/astra/status.h`, `sw/include/astra/vfs_service.h` (`LOST`)
- Modify: `emu/qemu/qemu-9.2/hw/m68k/astra68.c` (lost owner, fs reset, QOM seam)
- Modify: `sw/userspace/vfs/include/astra/vfs_host_transport.h`, `sw/userspace/vfs/src/vfs_host_transport.c` (loss hook)
- Modify: `sw/userspace/vfs/src/backends/host.c` (stale session is LOST)
- Modify: `sw/userspace/vfs/include/astra/vfs_service_core.h`, `sw/userspace/vfs/src/vfs_service_core.c` (epoch)
- Modify: `sw/userspace/vfs/src/vfs_client.c`, `sw/userspace/vfs/src/vfs_port_transport.c` (PEER becomes LOST)
- Modify: `sw/userspace/services/hostfs/main.c` (recovery)
- Modify: `sw/userspace/posix/src/file.c` (`ESTALE`)
- Modify: `sw/userspace/commands/fssem/fssem.c`, `emu/qemu/test-fs-semantics.py`
- Test: `sw/userspace/vfs/tests/test_vfs_service_core.c`, `sw/userspace/vfs/tests/test_vfs_port.c`

**Interfaces:**
- Produces (status): `ASTRA_STATUS_LOST` (21) and
  `ASTRA_VFS_ERR_LOST ((uint32_t)ASTRA_STATUS_LOST)`.
- Produces (QEMU): QOM `astra-host-fs-reset` (write-only bool; `true`
  resets the filesystem provider) and `astra-host-fs-resets` (read,
  count).
- Produces (transport): `void astra_vfs_host_transport_set_lost(AstraVfsHostTransport *, void (*lost)(void *context, uint32_t losses), void *context)`;
  `AstraVfsHostTransport.losses`; `AstraVfsHostRequest.losses`.
- Produces (core): `void astra_vfs_service_lose(AstraVfsService *)`;
  `AstraVfsService.epoch`, `AstraVfsOpenFile.epoch`.
- Produces (Kit): a storage call whose service is gone answers
  `ASTRA_VFS_ERR_LOST` rather than `ASTRA_VFS_ERR_PEER`. The transport
  function `astra_vfs_port_transport` still answers PEER, as its contract
  says (`vfs_port_transport.h:44-53`); the mapping is one level up.
- Produces (gate): fssem `--loss-probe PATH SECONDS reopen|dead`; checks
  `provider-reset` and `hostfs-kill`.

- [ ] **Step 1: Failing tests**

`test_vfs_service_core.c`:

```c
static void
test_a_lost_epoch_answers_lost_and_new_opens_work(void)
{
    AstraVfsService service;
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t session;
    uint32_t old;
    uint32_t closes;

    service_start(&service);
    memset(&request, 0, sizeof(request));
    request.size = ASTRA_VFS_REQUEST_SIZE;
    request.version = ASTRA_VFS_VERSION;
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_HELLO, &request, &reply);
    session = reply.session;
    request.session = session;
    request.flags = ASTRA_VFS_OPEN_READ;
    strcpy((char *)request.body.path, "/a");
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_OPEN, &request, &reply);
    old = reply.file;

    astra_vfs_service_lose(&service);
    closes = fake_close_count;

    request.file = old;
    request.length = 4u;
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_READ, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_LOST);
    /* The provider's handle is gone; closing it must not reach the backend,
     * where the number could now name somebody else's file. */
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(fake_close_count == closes);
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_READ, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_BAD_HANDLE);

    /* The session itself survives: a new open works. */
    request.file = ASTRA_VFS_FILE_INVALID;
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
}
```

`fake_close_count` counts the fake backend's `close` calls; add it if the
fake has none. In `test_vfs_port.c`, the two assertions that a vanished
service answers `ASTRA_VFS_ERR_PEER` (`:1068` through `astra_vfs_open`,
`:1551` through the bulk read) now expect `ASTRA_VFS_ERR_LOST`. Update their
comments: the transport says PEER, and the caller is told LOST. Expected:
compile errors, then the two port assertions failing.

- [ ] **Step 2: Status and wire**

`status.h`, after `ASTRA_STATUS_ESCAPED`:

```c
/* The object, session or service a request named was lost with its
 * provider (restart or reset); reopen through a fresh grant. */
#define ASTRA_STATUS_LOST            21
```

`vfs_service.h`: `#define ASTRA_VFS_ERR_LOST ((uint32_t)ASTRA_STATUS_LOST)`
with the comment "The file or service was lost with its provider; old
handles stay lost, and new opens may succeed". This adds a status value
and changes no layout or operation, so no version bumps. A version-31
client that sees 21 maps it to `EIO` through its default case.

- [ ] **Step 3: The provider**

`astra68.c`. Add to `AstraHostState`:

```c
    uint32_t fs_lost_owner;  /* the claimant at the last fs reset, until it
                                claims again */
    uint64_t fs_resets;
```

Then:

```c
/*
 * The filesystem provider loses its state, as a restarted Linux provider
 * would: every session and handle goes, and the claim with them. The owner
 * that held the claim is told LOST, not ACCESS, until it claims again, so it
 * knows to recover rather than that it was never allowed in.
 */
static void astra_host_fs_reset(Astra68State *s)
{
    uint32_t owner = qatomic_xchg(&s->host.fs_owner, 0u);

    if (owner != 0u)
        qatomic_set(&s->host.fs_lost_owner, owner);
    astra_host_release_files(s, 0);
    astra_host_release_sessions(s, 0u);
    qatomic_inc(&s->host.fs_resets);
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

Registered beside the other host properties in `astra68_init`:

```c
    object_property_add(OBJECT(machine), "astra-host-fs-reset", "bool",
                        NULL, astra_host_fs_reset_set, NULL, s);
    object_property_add_uint64_ptr(OBJECT(machine), "astra-host-fs-resets",
                                   &s->host.fs_resets, OBJ_PROP_FLAG_READ);
```

In `astra_host_execute_fs`, the claim check becomes:

```c
    if (owner == 0u || owner != qatomic_read(&s->host.fs_owner)) {
        status = owner != 0u &&
                 owner == qatomic_read(&s->host.fs_lost_owner) ?
            ASTRA_STATUS_LOST : ASTRA_STATUS_ACCESS;
        goto done;
    }
```

`astra_host_fs_claim`'s success path runs
`qatomic_cmpxchg(&s->host.fs_lost_owner, owner, 0u);`.
`astra_host_release_owner` does the same, and so does `astra_host_reset`
(`qatomic_set(&s->host.fs_lost_owner, 0u)`), because a reset machine has no
survivors to tell.

The property is a test seam, like the existing read-write
`astra-host-execution-max-ns`. It is reachable only through QMP on the host
side, which the board's launcher binds to a root-owned socket
(`run-arty.sh:50`). The guest cannot reach it.

- [ ] **Step 4: The transport tells HostFS**

`vfs_host_transport.h`: `AstraVfsHostTransport` gains

```c
    /* Provider-loss recoveries completed; a command remembers the value it
     * began under, so many losses seen at once recover once. */
    volatile uint32_t losses;
    void (*lost)(void *context, uint32_t losses);
    void *lost_context;
```

and `AstraVfsHostRequest` gains `uint32_t losses;`.
`astra_vfs_host_transport_begin` sets `request->losses = transport->losses`.
Add:

```c
void astra_vfs_host_transport_set_lost(AstraVfsHostTransport *transport,
                                       void (*lost)(void *context,
                                                    uint32_t losses),
                                       void *context)
{
    transport->lost = lost;
    transport->lost_context = context;
}
```

In `astra_vfs_host_transport_submit`, at the point where it returns the
command's status, after the transport lock is released, add:

```c
    if (status == ASTRA_VFS_ERR_LOST && transport->lost != NULL)
        transport->lost(transport->lost_context, request->losses);
```

The handler runs on the worker that saw the loss, holding no transport or
service lock, so it may issue commands itself.

`host.c` `submit` maps the race where recovery swapped the session while
this command was in flight:

```c
    if (status == ASTRA_VFS_ERR_BAD_HANDLE &&
        request->command->session != backend->session)
        status = ASTRA_VFS_ERR_LOST;
```

- [ ] **Step 5: The core's epoch**

`vfs_service_core.h`: `AstraVfsService` gains `uint32_t epoch;` and
`AstraVfsOpenFile` gains `uint32_t epoch;`. Declare
`void astra_vfs_service_lose(AstraVfsService *service);`.

`vfs_service_core.c`:
- Every place a file slot is claimed (`handle_open`'s reservation, and
  Task 8's `file_slot_claim`) sets `files[index].epoch = service->epoch`.
- Split `find_file` into `find_file_any` (the existing body) and
  `find_file`. `find_file` answers `ASTRA_VFS_ERR_LOST` for a file of an
  older epoch:

  ```c
  static AstraVfsOpenFile *
  find_file(AstraVfsService *service, uint32_t session, uint32_t handle,
            uint32_t *status)
  {
      AstraVfsOpenFile *file = find_file_any(service, session, handle,
                                             status);

      if (file != NULL && file->epoch != service->epoch) {
          *status = ASTRA_VFS_ERR_LOST;
          return NULL;
      }
      return file;
  }
  ```

- CLOSE uses `find_file_any`. `close_file`, and the session reaper's
  close of each file, skip `ops->close` for a file of an older epoch: its
  provider handle no longer exists, and its number may now belong to
  another file.
- `handle_open`: if `service->epoch` changed while the backend call ran,
  the node came from the old provider. Free the slot and answer LOST.
- After every backend call made for a file (READ, WRITE, SYNC, TRUNCATE,
  SEEK, SHARE and the rest), a failure on a file whose epoch is now old is
  answered as LOST. Put it in one helper, used where each case sets
  `reply->status`:

  ```c
  static uint32_t
  file_status(const AstraVfsService *service, const AstraVfsOpenFile *file,
              uint32_t status)
  {
      return status != ASTRA_VFS_OK && file->epoch != service->epoch ?
          ASTRA_VFS_ERR_LOST : status;
  }
  ```

- The new function:

  ```c
  /*
   * The provider lost every node this service had open (FS35). Old files
   * answer LOST from now on, and are freed by CLOSE or with their session
   * without reaching the backend. Pending shares name dead nodes and go
   * the same way.
   */
  void
  astra_vfs_service_lose(AstraVfsService *service)
  {
      if (service == NULL || !state_acquire(service))
          return;
      ++service->epoch;
      if (service->shares != NULL)
          memset(service->shares, 0,
                 service->share_capacity * sizeof(*service->shares));
      state_release(service);
  }
  ```

- [ ] **Step 6: HostFS recovers**

`HostfsGrant` gains:

```c
    uint32_t volume;                          /* ASTRA_HOST_FS_VOLUME_* */
    uint16_t session_flags;                   /* READ_ONLY or 0 */
    char root[ASTRA_VFS_PATH_MAX + 1u];       /* absolute in the volume */
```

`grant_add` gains `uint32_t volume, const char *root, uint16_t session_flags`
and records them. The startup grants pass `"/"`. `hostfs_derive` passes
the parent's `volume`, the joined root (`from->root` plus `/` plus `root`,
with no doubled slash when the parent is `/`; longer than
`ASTRA_VFS_PATH_MAX` is `ASTRA_VFS_ERR_INVALID`), and the flags it opened
with. Then:

```c
static _Alignas(4) uint32_t recovery_lock;

/*
 * The provider lost everything (FS35): claim it again, reopen every live
 * grant's session where the grant says it is rooted, and retire every file
 * opened before. Runs on whichever worker first saw LOST; the others that
 * saw the same loss find it already handled.
 */
static void hostfs_provider_lost(void *context, uint32_t losses)
{
    uint32_t status;

    (void)context;
    (void)astra_vfs_state_lock_acquire(&recovery_lock);
    if (losses != transport.losses) {
        astra_vfs_state_lock_release(&recovery_lock);
        return;
    }
    status = astra_vfs_host_claim(&transport, transport.generation, 0);
    if (status == ASTRA_VFS_OK) {
        (void)astra_vfs_state_lock_acquire(&grant_lock);
        for (uint32_t index = 0u; index < grant_capacity; ++index) {
            HostfsGrant *grant = &grants[index];
            uint32_t session = 0u;

            if (grant->port.receive == 0u || grant->retiring)
                continue;
            if (astra_vfs_host_session_open(
                    &transport, transport.generation, 0u, grant->volume,
                    grant->root, grant->session_flags, &session) !=
                ASTRA_VFS_OK) {
                ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_FILESYSTEM,
                             ASTRA_EVENT_LEVEL_WARNING,
                             "grant %u root gone after provider loss",
                             grant->port.id);
            }
            /* 0 makes every later request on the grant LOST. */
            __atomic_store_n(&grant->backend.session, session,
                             __ATOMIC_RELEASE);
        }
        astra_vfs_state_lock_release(&grant_lock);
        astra_vfs_service_lose(&service);
        astra_vfs_service_lose(&sound_service);
        __atomic_add_fetch(&transport.losses, 1u, __ATOMIC_RELEASE);
        ASTRA_EVENT0(ASTRA_EVENT_SUBSYSTEM_FILESYSTEM,
                     ASTRA_EVENT_LEVEL_NOTICE,
                     "hostfs recovered from provider loss");
    }
    astra_vfs_state_lock_release(&recovery_lock);
}
```

Check the event subsystem constant's name in `sw/include/astra/event.h`
(`grep -n "ASTRA_EVENT_SUBSYSTEM_" sw/include/astra/event.h`) and use the
filesystem one, or the closest one listed there. A grant whose session is 0
sends session 0. QEMU refuses that as BAD_HANDLE, and Step 4's
`submit` mapping makes it LOST, because 0 differs from the session the
command began with only when recovery changed it. Make that explicit: in
`host.c`'s `submit`, a `backend->session == 0u` answers
`ASTRA_VFS_ERR_LOST` before anything is sent.

Register the handler after the transport is initialised:
`astra_vfs_host_transport_set_lost(&transport, hostfs_provider_lost, NULL);`.

The bootstrap probe (Task 12) and the metrics service share the
transport. A metrics snapshot is a different host service, which a
filesystem reset does not touch.

- [ ] **Step 7: The Kit says LOST**

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
caller directly (`astra_vfs_port_read_bulk_position`, `astra_vfs_port_read_bulk`,
`astra_vfs_port_write_bulk`, and any other `astra_vfs_port_*` entry point
listed in `vfs_port_transport.h` that returns a filesystem status to a
Kit caller) return through

```c
static uint32_t caller_status(uint32_t status)
{
    return status == ASTRA_VFS_ERR_PEER ? ASTRA_VFS_ERR_LOST : status;
}
```

Find them with `grep -n "^astra_vfs_port_" sw/userspace/vfs/src/vfs_port_transport.c`.
Leave `astra_vfs_port_transport` and the service-side pump alone.

`posix/src/file.c` `posix_errno`: `case ASTRA_VFS_ERR_LOST: return ESTALE;`.

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

Add `#include <time.h>`.

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


def check_hostfs_kill(machine, mount):
    """HostFS dies under a writer; a terminal opened afterwards sees /work."""
    loss(machine, "dead", lambda: machine.qmp.type_line(KILL_HOSTFS))
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
Confirm `terminal.BANNER` and `machine.wait_for_ready` are the names
`open_terminal` uses (`test-terminal.py:942-980`). Typed text goes to the
focused window, which is the new terminal after the double click.

The events service and the sampler hold WORK grants from before the kill.
Their writes answer LOST until they are restarted. The check does not
depend on them, and the Self-Review records this as a Phase 2 item.

- [ ] **Step 9: Run, perturb, full suite, commit**

Sync, then build and test userspace, build `sw/boot`, run
`emu/qemu/build.sh host`, rebuild images, and run `test-fs-semantics.py`.
Expected: `PASS provider-reset` and `PASS hostfs-kill`.

Perturb (restore and re-sync each time):
1. QEMU: answer `ACCESS` for the lost owner, as before this task. Expected:
   HostFS never recovers, the probe prints `FSSEM LOSS FAIL write errno=13`
   (EACCES), and `provider-reset` fails "loss probe (reopen)".
2. Core: make `find_file` skip the epoch check and `file_status` return
   its `status` unchanged. Expected: the old
   descriptor's write goes to the new session with a dead handle, the probe
   prints `FSSEM LOSS FAIL write errno=9` (EBADF), and `provider-reset`
   fails.
3. Kit: remove the PEER-to-LOST mapping in `exchange_request`. Expected:
   the probe prints `FSSEM LOSS FAIL write errno=5` (EIO) after the kill,
   and `hostfs-kill` fails; the port test's two assertions fail too.

`~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-task10.log`. Expected: green.

```bash
git add sw/include/astra emu/qemu/qemu-9.2/hw/m68k/astra68.c sw/userspace/vfs \
  sw/userspace/services/hostfs/main.c sw/userspace/posix/src/file.c \
  sw/userspace/commands/fssem/fssem.c emu/qemu/test-fs-semantics.py
git commit -m "feat(fs): provider loss is one answer, LOST/ESTALE; HostFS recovers in place (FS35)"
```

---

### Task 11: Host budgets and admission (D9)

Measure what each provider object costs the host, then make every pool
that can grow on a guest's say bounded by configuration (FS37, FS38):

| Resource | Owner | Bound | Configured by |
|---|---|---|---|
| Linux descriptors held for guests (sessions + handles) | QEMU | `ASTRA_HOSTFS_MAX_DESCRIPTORS` | the launcher's environment; default: QEMU's own `RLIMIT_NOFILE` less 256 for QEMU itself |
| Directory streams (each also a `DIR` buffer) | QEMU | `ASTRA_HOSTFS_MAX_DIRECTORIES` | the launcher; default: the descriptor bound |
| Open files per owner process | HostFS | `owner_files` | `services/hostfs/settings.conf` |
| Sessions per owner process | HostFS | `owner_sessions` | same |
| Queued requests per grant | HostFS (its grant port) | `queue_messages` | same, at most the kernel's `ASTRA_PORT_MESSAGES_MAX` |
| Grants, shares, workers | HostFS | `grants`, `shares`, `workers` | same (Tasks 3, 5, 8) |

No value is compiled into an image. The defaults come from the host's own
limit, and the board's launcher sets the measured ones (Step 7).
`owner_files 1024` is Linux's default soft `RLIMIT_NOFILE`, the per-process
expectation POSIX programs are written against. `owner_sessions 64` allows
one session per thread slot with room for library clients. Check
`ASTRA_PROCESS_THREAD_SLOT_COUNT` and raise it if the slot count is larger.

**Files:**
- Modify: `emu/qemu/qemu-9.2/hw/m68k/astra68.c` (admission, gauges)
- Modify: `sw/userspace/vfs/include/astra/vfs_service_core.h`, `sw/userspace/vfs/src/vfs_service_core.c` (owner limits)
- Modify: `sw/userspace/services/hostfs/main.c` (settings), `emu/qemu/astra_image.py` (settings values)
- Modify: `sw/userspace/commands/fsbench/fsbench.c` (`--hold`), `sw/userspace/commands/fssem/fssem.c` (`--hold-sessions`, `--flood`)
- Modify: `emu/qemu/fs-baseline.py` (`--budget`), `emu/qemu/test-fs-semantics.py` (`budgets`, `flood`), `emu/qemu/test-hostfs.py` (`limits`)
- Modify: `emu/qemu/run-arty.sh` (measured limits); `emu/qemu/test-run-arty.py` if it asserts the environment
- Test: `sw/userspace/vfs/tests/test_vfs_service_core.c`

**Interfaces:**
- Produces (QEMU): environment `ASTRA_HOSTFS_MAX_DESCRIPTORS`,
  `ASTRA_HOSTFS_MAX_DIRECTORIES` (positive decimal; anything else refuses
  to start); QOM gauges `astra-host-fs-descriptors`,
  `astra-host-fs-directories`, `astra-host-rss-kib`, `astra-host-fds`.
  Refusal is `ASTRA_STATUS_LIMIT`.
- Produces (core): `int astra_vfs_service_set_owner_limits(AstraVfsService *, uint32_t sessions, uint32_t files)`
  (0: no limit); refusals counted in the existing, so far unused,
  `owner_session_quota_denied` and `owner_quota_denied`
  (`vfs_service_core.h:83-84`).
- Produces (tools): `fsbench --hold file|dir COUNT`;
  `fssem --hold-sessions COUNT`; `fssem --flood DIR LIMIT`;
  `fs-baseline.py LABEL OUT.json --budget`.

- [ ] **Step 1: Failing core test**

```c
static void
test_owner_limits_refuse_the_next_open_and_session(void)
{
    AstraVfsService service;
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t first;

    service_start(&service);
    assert(astra_vfs_service_set_owner_limits(&service, 2u, 2u));
    memset(&request, 0, sizeof(request));
    request.size = ASTRA_VFS_REQUEST_SIZE;
    request.version = ASTRA_VFS_VERSION;
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_HELLO, &request, &reply);
    first = reply.session;
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_LIMIT);
    /* Another owner is not charged for owner 7. */
    astra_vfs_service_dispatch_grant(&service, 8u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);

    request.session = first;
    request.flags = ASTRA_VFS_OPEN_READ;
    strcpy((char *)request.body.path, "/a");
    for (int index = 0; index < 2; ++index) {
        astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                         ASTRA_VFS_OP_OPEN, &request, &reply);
        assert(reply.status == ASTRA_VFS_OK);
    }
    astra_vfs_service_dispatch_grant(&service, 7u, 1u, NULL, 0u,
                                     ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_LIMIT);
    assert(service.stats.owner_quota_denied == 1u);
    assert(service.stats.owner_session_quota_denied == 1u);
}
```

Register it. Expected: compile error.

- [ ] **Step 2: Owner limits in the core**

`vfs_service_core.h`: `AstraVfsService` gains
`uint32_t owner_session_limit; uint32_t owner_file_limit;`, and the
declaration from Interfaces.

`vfs_service_core.c`:

```c
int
astra_vfs_service_set_owner_limits(AstraVfsService *service,
                                   uint32_t sessions, uint32_t files)
{
    if (service == NULL)
        return 0;
    service->owner_session_limit = sessions;
    service->owner_file_limit = files;
    return 1;
}

/*
 * What one owner holds across its sessions. Called with the state lock
 * held. ponytail: a scan of the session table per HELLO and per open; keep
 * per-owner counters if a profile ever shows the scan.
 */
static void
owner_usage(const AstraVfsService *service, uint32_t owner,
            uint32_t *sessions, uint32_t *files)
{
    *sessions = 0u;
    *files = 0u;
    for (uint32_t index = 0u; index < service->session_capacity; ++index) {
        const AstraVfsSessionSlot *slot = &service->sessions[index];

        if (slot->id == ASTRA_VFS_SESSION_INVALID || slot->owner != owner)
            continue;
        ++*sessions;
        *files += slot->open_files;
    }
}
```

Use the session-in-use test HELLO already uses to find a free slot, if it
is not `id == ASTRA_VFS_SESSION_INVALID`, and the table's real capacity
field name. In the HELLO path, before a slot is claimed: when
`owner_session_limit != 0` and the owner already holds that many, answer
`ASTRA_VFS_ERR_LIMIT` and `++service->stats.owner_session_quota_denied`. In
`handle_open`, before the file-slot search, and in Task 8's
`file_slot_claim` (ADOPT): when `owner_file_limit != 0` and the session's
owner holds that many files, answer `ASTRA_VFS_ERR_LIMIT` and
`++service->stats.owner_quota_denied`. `owner == 0` (a local caller) is
never limited.

- [ ] **Step 3: HostFS settings**

Replace `configured_workers` and Task 5's `grants` reader with one
reader:

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
    uint32_t workers;
    uint32_t grants;
    uint32_t shares;
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
    status = setting(&config, "workers", 1u,
                     ASTRA_PROCESS_THREAD_SLOT_COUNT - 1u, &out->workers);
    if (status == ASTRA_STATUS_OK)
        status = setting(&config, "grants", 2u, UINT16_MAX, &out->grants);
    if (status == ASTRA_STATUS_OK)
        status = setting(&config, "shares", 1u, UINT16_MAX, &out->shares);
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

Keep the bounds Tasks 3, 5 and 8 gave `workers`, `grants` and `shares`
where they differ from these. Startup calls `read_settings` once and uses
the fields where those tasks used their readers. Call
`astra_vfs_service_set_owner_limits(&service, settings.owner_sessions, settings.owner_files)`
for both services. `grant_add` creates the grant port with
`astra_rt_port_create(settings.queue_messages, settings.queue_messages * (uint32_t)sizeof(AstraVfsRenameRequestMessage), ...)`.

`astra_image.py` `services/hostfs/settings.conf` gains:

```python
          "owner_files 1024\n"
          "owner_sessions 64\n"
          "queue_messages 16\n"
```

Use `queue_messages` = the kernel's `ASTRA_PORT_MESSAGES_MAX` if that is
below 16 (`grep -rn "define ASTRA_PORT_MESSAGES_MAX" sw/include`).

- [ ] **Step 4: QEMU admission and gauges**

`AstraHostState` gains:

```c
    /* Provider admission (FS37, FS38): Linux descriptors held for guests
     * (sessions and handles) and directory streams, under files_lock.
     * Bounds come from the launcher, or from QEMU's own descriptor limit;
     * never compiled in. */
    uint32_t fs_descriptors;
    uint32_t fs_directories;
    uint32_t fs_max_descriptors;
    uint32_t fs_max_directories;
```

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

Admission. `astra_host_insert_file` checks and charges under
`files_lock`, and answers handle 0 when full:

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

Each caller of `astra_host_insert_file` (via `astra_host_publish_open`),
on 0, closes the descriptor (`closedir` for a stream) and answers
`ASTRA_STATUS_LIMIT`. The final release of a file, where its descriptor is
closed, decrements both counts under `files_lock`. Session open charges
one descriptor the same way and answers `ASTRA_STATUS_LIMIT` when full.
Session release gives it back.

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

`test-hostfs.py`. Split `main`'s boot into a helper and boot a second
machine with small bounds:

```python
def booted(command, environment, qtest_path, body):
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


def limits(qtest):
    """MAX_DESCRIPTORS=4 (the session root and three handles),
    MAX_DIRECTORIES=1."""
    qtest.detect_endian()
    generation = qtest.read32(HACC_GENERATION)
    assert claim(qtest, generation) == STATUS_OK
    work = open_session(qtest, generation)
    handles = []
    for index in range(2):
        answer = execute(qtest, [Command(
            generation, FS_OPEN, "f%d" % index,
            flags=OPEN_READ | OPEN_WRITE | OPEN_CREATE, session=work)])[0]
        assert status(answer) == STATUS_OK, status(answer)
        handles.append(get32(answer.raw, 16))
    directory = execute(qtest, [Command(
        generation, FS_OPEN, "/", flags=OPEN_READ | OPEN_DIRECTORY,
        session=work)])[0]
    assert status(directory) == STATUS_OK
    full = execute(qtest, [Command(
        generation, FS_OPEN, "f9", flags=OPEN_READ | OPEN_WRITE | OPEN_CREATE,
        session=work)])[0]
    assert status(full) == STATUS_LIMIT, status(full)
    closed = execute(qtest, [Command(
        generation, FS_CLOSE, handle=handles[0], session=work)])[0]
    assert status(closed) == STATUS_OK
    second_directory = execute(qtest, [Command(
        generation, FS_OPEN, "/", flags=OPEN_READ | OPEN_DIRECTORY,
        session=work)])[0]
    assert status(second_directory) == STATUS_LIMIT
    again = execute(qtest, [Command(
        generation, FS_OPEN, "f9", flags=OPEN_READ | OPEN_WRITE | OPEN_CREATE,
        session=work)])[0]
    assert status(again) == STATUS_OK
    print("PASS limits")
```

Add `STATUS_LIMIT = 10`. Mirror whatever endian detection and setup `run`
does first, if it is not `detect_endian()`. In `main`, replace the inline
`Popen` block with `booted(command, environment, qtest_path, lambda qtest: (run(...), volumes(qtest, root)))`.
Then refuse `ASTRA_HOSTFS_MAX_DESCRIPTORS=0` the way the missing root is
refused (exit non-zero, and the message names the variable). Then boot
again with a fresh `qtest-limits.sock`, `ASTRA_HOSTFS_MAX_DESCRIPTORS=4`
and `ASTRA_HOSTFS_MAX_DIRECTORIES=1`, and run `limits`.

- [ ] **Step 5: The measuring tools**

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

Add `#include <unistd.h>` and `#include <sys/resource.h>` if absent.

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
 * pass when the refusal is EMFILE at HostFS's per-owner limit (FS38) --
 * not libc's, not QEMU's. A few of the owner's slots may already be in use
 * (its cwd, the namespace), so the count is LIMIT less at most 8.
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
```

Declare `long opened;` with the other locals. Add `#include <sys/resource.h>`. fssem links
the Kit for Task 5's raw cases; if the Makefile entry for `fssem` does not
list `filesystem.library`, add it there.

- [ ] **Step 6: The gate and the board driver**

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
    """A handle flood is refused at the owner's limit, and the terminal and
    the desktop still answer (FS38)."""
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
reads the marker through its own session, so which terminal removes it
does not matter. Its verdict is a line (`FSSEM FLOOD PASS` or `FAIL`),
because a background job's exit status is not reported where the gate
reads it.

`fs-baseline.py`, a `--budget` flag. When given, after opening Terminal:

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

- [ ] **Step 7: Measure and set the board's bounds**

On beast: sync, build userspace and `sw/boot`, build QEMU, rebuild images,
and run `test-hostfs.py` and `test-fs-semantics.py`. Expected: `PASS limits`,
`PASS budgets {...}`, `PASS flood`.

On the board, after publishing this build as Task 1 Step 7 does:

```bash
ssh root@192.168.1.52 "python3 /var/lib/astra/current/bin/fs-baseline.py budget /data/fs-budget.json --budget --restart"
scp root@192.168.1.52:/data/fs-budget.json /mnt/Documents/astra68/builds/fs-phase1-budget-$(date +%Y%m%d)-$(git rev-parse --short HEAD)/de25.json
cp /tmp/fs-budget-beast.json /mnt/Documents/astra68/builds/fs-phase1-budget-$(date +%Y%m%d)-$(git rev-parse --short HEAD)/beast.json
```

Run the `scp` and `cp` from `beast`, which reaches both the board and the
NAS. The bounds follow from the measurement and a host budget for provider
objects of 64 MiB (Self-Review, open question 3):

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
directory one, after `ASTRA_HOSTFS_ROOT=`). The two counts are the
printed numbers, not values chosen here. The comment names the evidence
directory, so the number can be traced. Update
`emu/qemu/test-run-arty.py` if it asserts the exact QEMU environment.

- [ ] **Step 8: Perturb, full suite, commit**

Perturb (restore and re-sync each time):
1. Core: make the owner-file check never refuse. Expected: the flood runs
   to a larger limit (QEMU's or the service's file table), and `flood`
   fails with `FSSEM FLOOD FAIL n=<more than 1024>`.
2. QEMU: skip the descriptor check in `astra_host_insert_file`. Expected:
   `test-hostfs.py` fails in `limits` at the `STATUS_LIMIT` assertion.

`~/astra-mg/t9-verify.sh 2>&1 | tee /tmp/t9-task11.log`. Expected: green.

```bash
git add emu/qemu/qemu-9.2/hw/m68k/astra68.c sw/userspace/vfs \
  sw/userspace/services/hostfs/main.c emu/qemu/astra_image.py \
  sw/userspace/commands/fsbench/fsbench.c sw/userspace/commands/fssem/fssem.c \
  sw/userspace/commands/Makefile emu/qemu/fs-baseline.py \
  emu/qemu/test-fs-semantics.py emu/qemu/test-hostfs.py emu/qemu/run-arty.sh \
  emu/qemu/test-run-arty.py
git commit -m "feat(fs): measured host budgets; provider and per-owner admission from configuration (FS37, FS38)"
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

### Task 13: Closing measurement and the record (D8, Phase 1 exit)

Run Task 1's harness again on the finished tree, on the board and in QEMU.
Compare it with the baseline against the PRD budget: at most 10% worse
median and p95, and at least 90% of the throughput. Store the evidence and
bring the documents in line with what now runs. Medians of three runs are
compared, not single runs, because Task 1 Step 7 measured the
run-to-run noise.

**Files:**
- Modify: `tools/fs-baseline-compare.py` (runs per side)
- Modify: `docs/FILESYSTEM_PRODUCTION_PRD.md`, `docs/STORAGE_AND_VFS.md`, `docs/ABI.md`, `docs/FILESYSTEM_KIT.md`, `ndk/docs/source/filesystem.md`, `docs/CURRENT_STATE.md`

**Interfaces:**
- Produces: `fs-baseline-compare.py BASE[,BASE...] NEW[,NEW...] [--budget 0.10]`.
  Each side is the per-workload median over its runs, and a workload failed
  in any run fails.

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

Extend the self-check from Task 1 Step 5 with a three-run case. Joining
`b,b,s` against `b` passes, because the median is `b`. Joining `s,s,b`
against `b` fails:

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
assert subprocess.run(["python3", tool, b, ",".join((b, b, s))]).returncode == 0
assert subprocess.run(["python3", tool, b, ",".join((s, s, b))]).returncode == 1
print("COMPARE SELF-CHECK PASS")
EOF
```

Expected: `COMPARE SELF-CHECK PASS`.

- [ ] **Step 2: QEMU closing runs**

On beast, after the full suite is green on this tree:

```sh
SHA=$(git rev-parse --short HEAD)
OUT=/mnt/Documents/astra68/builds/fs-phase1-final-$(date +%Y%m%d)-$SHA
BASE=$(ls -d /mnt/Documents/astra68/builds/fs-phase1-baseline-* | head -1)
mkdir -p "$OUT"
for run in 1 2 3; do
  ~/astra-mg/locked python3 emu/qemu/test-fs-semantics.py $Q $R ~/astra-mg/base.img --bench qfinal$run "$OUT/qemu-final$run.json"
done
python3 tools/fs-baseline-compare.py \
  "$BASE/qemu-base1.json,$BASE/qemu-base2.json,$BASE/qemu-base3.json" \
  "$OUT/qemu-final1.json,$OUT/qemu-final2.json,$OUT/qemu-final3.json" \
  | tee "$OUT/qemu-vs-baseline.txt"
```

`$Q` and `$R` are the QEMU and ROM paths Task 2 Step 9 used. `head -1`
takes the oldest baseline directory: the Task 1/Task 2 one.

- [ ] **Step 3: Board closing runs**

Publish this tree as Task 1 Step 6 did
(`~/astra-mg/locked emu/qemu/publish-de25-release.sh`), then from beast:

```sh
for run in 1 2 3; do
  ssh root@192.168.1.52 "python3 /var/lib/astra/current/bin/fs-baseline.py final$run /data/fs-final$run.json --restart" &&
  scp root@192.168.1.52:/data/fs-final$run.json "$OUT/"
done
ssh root@192.168.1.52 "python3 /var/lib/astra/current/bin/fs-baseline.py budget /data/fs-budget-final.json --budget --restart" &&
  scp root@192.168.1.52:/data/fs-budget-final.json "$OUT/"
ssh root@192.168.1.52 'cat /proc/$(pidof qemu-system-m68k-astra)/limits; free -k; uname -a' > "$OUT/board.txt"
git rev-parse HEAD > "$OUT/source-commit.txt"
cp /tmp/t9-task*.log "$OUT/"
python3 tools/fs-baseline-compare.py \
  "$BASE/fs-base1.json,$BASE/fs-base2.json,$BASE/fs-base3.json" \
  "$OUT/fs-final1.json,$OUT/fs-final2.json,$OUT/fs-final3.json" \
  | tee "$OUT/de25-vs-baseline.txt"
```

Expected: every record without `"failed"` workloads; `de25-vs-baseline.txt`
ending `BUDGET PASS`.

If it ends `BUDGET FAIL`, do not tune numbers or drop workloads to pass.
The likely cause is D1's extra hop, which every request now pays. Record
which workloads failed and by how much in `$OUT/de25-vs-baseline.txt`, and
stop for the user. The PRD allows a scoped acceleration path only with
measured need. Its design (a grant-scoped data lane that QEMU authenticates
by session) is a Phase 2 decision the user makes from this record. It is
not a Phase 1 fix. Self-Review, open question 1.

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

What the HostFS path guarantees now, ahead of the Phase 2 cutover:

- **One path.** Every request for a host volume goes client -> HostFS port
  -> QEMU. HELLO carries no capability (storage protocol 27); QEMU accepts
  filesystem commands only from the process that claimed the service.
- **Grants are ports.** Each grant is its own HostFS port, mapped to a
  QEMU session rooted at an `O_PATH` directory. A holder can only derive a
  narrower grant (`GRANT_DERIVE`). A session is bound to the
  kernel-stamped sender and the port it arrived on (protocol 28).
- **Linux resolves.** `.`, `..` and symlinks are resolved in order beneath
  the session root. Leaving the root answers ESCAPED with a continuation
  the Filesystem Kit follows in the namespace (protocol 29).
- **Identity.** Metadata carries the Astra volume identity, inode,
  generation and a validity mask; same-volume aliases compare equal
  (protocol 30).
- **Descriptions.** The position lives in the provider's Linux open file
  description: dup, fork (by share token) and exec share it, `O_APPEND` is
  atomic (protocol 31).
- **cwd.** A held directory; relative names survive a rename; `getcwd`
  asks the provider and answers ENOENT when there is no path
  (protocol 32).
- **Loss.** A provider reset or HostFS death answers LOST (`ESTALE`).
  HostFS recovers a reset in place; a HostFS restart serves new processes,
  while running ones keep LOST until restarted (Phase 2: live namespace).
- **Budgets.** QEMU admits descriptors and directory streams up to
  `ASTRA_HOSTFS_MAX_DESCRIPTORS`/`ASTRA_HOSTFS_MAX_DIRECTORIES`; HostFS
  limits each owner (`owner_files`, `owner_sessions`) and each grant's
  queue (`queue_messages`) from `services/hostfs/settings.conf`.
- **Bootstrap.** The supervisor reads `/startup/system` through the
  provider before any service starts and logs whether it matches.
```

`docs/ABI.md`, the storage protocol paragraph (`:526`): replace "version
8" with "version 32", and add a sentence pointing to
`sw/include/astra/vfs_service.h` for the version history (27-32 are listed
there). Add host command record 3 and host transport `0x00010010` to the
host section, and statuses 20 (`ESCAPED`) and 21 (`LOST`) wherever
`status.h` values are listed (`grep -n "READ_ONLY\|19" docs/ABI.md`).

`docs/FILESYSTEM_KIT.md` (`:3`) and `ndk/docs/source/filesystem.md` (`:4`):
`filesystem.library` 7.2, linked as `filesystem.library.7`. In the NDK page
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
Filesystem Phase 1 (2026-10) closed the HostFS bypass and made HostFS the
trusted boundary: grant ports, rooted QEMU sessions, ordered resolution,
identity, shared descriptions, a held cwd, defined provider loss, measured
host budgets, and a bootstrap read through the provider. Details:
`docs/STORAGE_AND_VFS.md`, "Phase 1 trusted contract". Evidence:
`/mnt/Documents/astra68/builds/fs-phase1-final-*`.
```

- [ ] **Step 5: Commit**

```bash
git add tools/fs-baseline-compare.py docs/FILESYSTEM_PRODUCTION_PRD.md \
  docs/STORAGE_AND_VFS.md docs/ABI.md docs/FILESYSTEM_KIT.md \
  ndk/docs/source/filesystem.md docs/CURRENT_STATE.md
git commit -m "docs(fs): Phase 1 closed; DE25 and QEMU measured against the baseline"
```

---

## Self-Review

### Phase 1 exit evidence -> tasks

| PRD Phase 1 exit item | Where it is proven | Test |
|---|---|---|
| Raw-request confinement | Task 4 (provider sessions, claim, path operands), Task 5 (grant ports) | `test-hostfs.py` `claims_and_sessions`; fssem `raw-readonly`, `raw-cross-grant`, `raw-subroot` (raw port messages, no Kit) |
| Symlink traversal | Task 6 | `test-astra68-resolve` (host); fssem `symlink-in-volume`, `symlink-cross-volume`, `dotdot-at-root`, `escape-relative`, `link-edges` |
| cwd/rename | Task 9 | fssem `cwd-rename`; `test-hostfs.py` `paths` |
| Same-volume aliases | Task 7 | fssem `alias-identity`; `raw-subroot` identity equality |
| Shared descriptions | Task 8 | fssem `shared-description`, `append-race`, `exec-inherit`; `test-hostfs.py` DUP check |
| Provider loss | Task 10 | gate checks `provider-reset`, `hostfs-kill`; core epoch test |
| Target baseline recorded | Task 1 (DE25 before any data-path change), Task 13 (after) | `fs-baseline.py` records on the NAS; `fs-baseline-compare.py` |
| Host memory budgets measured | Task 11 | gate `budgets`, `flood`; `test-hostfs.py` `limits`; DE25 `--budget` record |
| Bootstrap availability | Task 12 | gate `bootstrap`; `test-hostfs.py` `volumes` |
| Gate infrastructure (D11, D12) | Task 2 | `fs-gates.sh` in the beast suite from Task 2 on |
| FS43 consumers of the shared host command header | Task 4 | entropy, audio, metrics, remote desktop updated and their gates run in the full suite |
| FS44 bypass closed | Task 3 | `test_hello_attaches_no_capability`; hop measured against the baseline |

Each task ends with `t9-verify.sh` green and at least one perturbation that
turns its new test red.

### Gaps, stated rather than hidden

1. **Storage-protocol long paths** (FS18, D7) are deferred to Phase 2
   (Decisions). The host wire takes 4096-byte paths from Task 4, but client
   requests stay at 192 bytes. A cwd whose path is longer answers
   `ENAMETOOLONG` from `getcwd`.
2. **A running process after a HostFS restart** keeps LOST for its old
   grants until it is restarted (Task 10). Live namespace publication (FS16)
   is Phase 2. The events service and the sampler are such processes. They
   are restarted with the machine, not with HostFS.
3. **Rename across two grants of one volume** answers `EXDEV` (Kit
   `CROSS_DEVICE`), as across volumes. Linux could do it, but it would need
   HostFS to authorise both grants in one request. That is Phase 2, with
   the namespace work.
4. **Directory entries carry no inode** (Task 7 gives `stat` identity, not
   `readdir`'s `d_ino`). `ls -i`-style tools stat each entry.
5. **A dirfd-relative escape** (`openat(dirfd, "../..")` leaving the grant)
   answers `EIO`. Only the cwd can rebase, because a dirfd has no binding
   name (Task 9). Phase 2's live namespace gives handles a binding.
6. **Grant recovery by path after a provider reset** reopens a grant at
   the root path it was made with. A grant whose root was renamed
   afterwards fails to recover and answers LOST (Task 10, logged).
7. **macOS `desktop` QEMU profile** compiles, but answers `UNSUPPORTED` for
   `openat2`, `/proc/self/fd` and `RWF_APPEND` facilities. HostFS there does
   not advertise `DIRECTORY_PATH`, so cwd stays a string. The gates run on
   Linux only.

### Placeholder scan

Searched the plan for TBD, TODO, "implement later", "similar to Task" and
"add appropriate". None remain. Three places leave a value to the
measurement on purpose, each with the command that produces it:
`run-arty.sh`'s two admission counts (Task 11 Step 7), and the noise floor
that Task 13 compares against (Task 1 Step 7). These are measured, not
chosen.

### Type and name consistency

- Host operations, dense: 1-19 (Task 4), `SEEK` 20 and `DUP` 21 (Task 8),
  `PATH` 22 (Task 9). `ASTRA_HOST_FS_MAX` follows the last.
  `ASTRA_HOST_METRICS_VERSION` 3, 4 and 5 at Tasks 4, 8 and 9 (Task 6 adds
  no operation).
- Host transport versions `0x0001000d`-`0x00010010` with capability bits
  10-13. Task 12 adds a volume and no version.
- Storage protocol 27-32 at Tasks 3, 5, 6, 7, 8 and 9; operation codes 36
  (`GRANT_DERIVE`), 37-39 (`SEEK`, `SHARE`, `ADOPT`) and 40
  (`DIRECTORY_PATH`). HELLO features bits 0-4: `GRANTS`,
  `RESOLVES_PATHS`, `NODE_META`, `DESCRIPTIONS` and `DIRECTORY_PATH`, at
  Tasks 5, 6, 7, 8 and 9.
- Statuses: `ESCAPED` 20 (Task 6), `LOST` 21 (Task 10).
- Filesystem Kit: 5 (Task 3), 6 (Task 5), 7 (Task 7), 7.1 (Task 8), 7.2
  (Task 9).
- `astra_host_publish_open(s, owner, session_id, command, fd, flags)` and
  `astra_host_insert_file(s, owner, session_id, fd, directory, flags)`
  (Task 4) are used by Tasks 8 and 11 with these signatures. Task 11 makes
  `insert_file` answer 0 when admission refuses.
- `AstraVfsHostBackend.session` (Task 4) is what Task 10's recovery
  rewrites, and what Task 12's bootstrap client initialises.

### For the implementer to confirm before relying on it

These are facts the plan uses and could not check without building:
1. `astra_wait_multiple` reports the index of a port whose last sender
   closed (`PEER_DEAD`) on a receive in the wait set (Task 5's grant
   retirement).
2. `astra_wait_one`'s deadline semantics where Task 3's workers use a
   finite wait; `ASTRA_DEADLINE_FOREVER` elsewhere.
3. The runtime's yield call name (`grep -n yield ndk/include/astra/runtime.h`;
   Task 5).
4. `astra_posix_startup()` is declared where `file.c` can call it in the
   fork child (Task 8).
5. `install_test_command` installs `fssem` at `/commands/fssem` (Task 8's
   `execv`).
6. The fake backend and fixture names in `test_vfs_service_core.c`
   (`service_start`, the fake's counters) match what Tasks 5, 8, 9, 10 and
   11 extend.
7. A supervisor `ASTRA_EVENT` at boot reaches `said()` or the host syslog
   (Task 12's check).
8. `astra_vfs_host_transport_destroy` releases the channel slot as well
   as the DMA area (Task 12).
9. The session-in-use test and capacity field name in
   `vfs_service_core.c` (Task 11's `owner_usage`).

### Open questions for the user

1. **If the DE25 closing run misses the 10% budget** because every request
   now takes the HostFS hop (Task 13 Step 3), should Phase 2 design a
   grant-scoped data lane? That would be a direct QEMU channel per session,
   authenticated by session and not by a shared device. The alternative is
   to accept the cost. The plan stops and reports rather than adding
   acceleration in Phase 1.
2. **Running processes after a HostFS restart** keep LOST until restarted.
   Acceptable for Phase 1, or should the supervisor restart every holder
   of a HostFS grant when HostFS restarts (a blunt version of live
   namespace)?
3. **The host memory budget for provider objects on the DE25.** Task 11
   uses 64 MiB to turn the measured per-object cost into admission counts.
   The right number depends on what else the board's Linux must keep, which
   is a release-configuration decision (FS36/FS37).
4. **Publishing to the board.** Tasks 1, 11 and 13 publish this tree's
   release to the DE25 (`publish-de25-release.sh`), which changes the
   board's current release each time. Confirm that is acceptable, or name
   a board slot to use.
5. **The sudo wrapper on beast** (`/usr/local/sbin/astra-qemu-root`, Task 2)
   gives `barry` a root-run QEMU with a fixed argument shape. `barry`
   already has passwordless sudo on beast, so this narrows nothing there
   today. It exists so the gates use the board's posture, and for Phase 2's
   mounts. Confirm the sudoers entry is wanted.
