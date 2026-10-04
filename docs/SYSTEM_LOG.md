# System log: one place for program output (design proposal)

Status: proposal, not built. It records the decisions to review before the
first line of code.

## The rule

A program's standard streams belong to whoever started it.

- **Started from a terminal** (typed at zsh, or `open App.app` from zsh):
  stdin, stdout and stderr are that terminal's. Output appears where the
  person typed, and ^C reaches the program.
- **Started from anywhere else** (the desktop, the service manager, a login
  item): stdout and stderr go to the **system log**, and stdin is empty.

No program chooses where it logs. There is one sink, one record format, one
retention policy and one viewer. A program that wants structured records
calls `astra_log*` and they land in the same store as its stdout.

## What exists today

From a survey of the tree, 2026-10-01:

- **POSIX fallback.** A program started with no `STDOUT`/`STDERR`
  capability gets fd 1 and fd 2 on a shared `POSIX_DESCRIPTOR_LOG`
  (`sw/userspace/posix/src/console.c:1028`). Each line becomes one
  `astra_log` NOTICE record, and the two streams are indistinguishable.
  This is the right default, and the rest of the design builds on it.
- **Bundles never get the terminal.** APP_LAUNCH (APPL v4) carries no stream
  handles, and the application grant ceiling has no `STDIN`/`STDOUT`/
  `STDERR`. A bundle opened with `open` from Terminal therefore logs
  instead of printing.
- **Every record goes through the kernel trace ring.** That is 2047 slots of
  32 bytes, and a line costs one slot per 24 bytes. The events service
  drains it once a second, at most 4 × 73 records per pass, and a burst
  wraps the ring and is lost. Chocolate Doom's start-up output alone is
  hundreds of slots.
- **The events store holds 256 records in total**, persisted as a whole
  snapshot rewrite (`/store/store.{0,1}`) on every change. It keeps the
  current boot and the previous one. It has no rotation, no compression, no
  time-to-live and no wall-clock time. `EVENTS:` does not rejoin multi-chunk
  lines.
- **No compression library** exists in userspace. The only one is the boot
  ROM's LZ4 decoder.
- **No viewer** beyond the `events` command.

## Design

### 1. Stream routing at launch

- APPL v5 adds optional `STDIN`/`STDOUT`/`STDERR` handles to a launch
  request. The application service grants them **only** when the requester
  transferred them, so the grant ceiling stays manifest-defined and a
  program cannot gain a terminal it was not handed.
- `open` passes its own fds 0–2 when they are a terminal, and the desktop
  passes none.
- A terminal-started bundle joins the terminal's foreground process group,
  so ^C and job control work as they do for zsh children. `open` waits for
  the program to exit only with `-W`, as on macOS. Without it, the program
  keeps the terminal streams after `open` returns.
- No change for programs zsh runs directly: they already inherit through
  fork and exec.

### 2. A log descriptor that writes to the log service, not the ring

Bulk program output must not share the kernel's 2 KiB-record ring with
kernel events.

- The LOG descriptor opens a session to the log service (the events service,
  renamed in role, not in code) and writes **batched lines** over a bulk
  ring (`docs/ABI.md`'s bulk-ring transport). One IPC carries many lines.
- Each record carries:
  - stream (stdout, stderr or log)
  - the program's bundle identifier or executable path
  - pid and thread
  - wall-clock time (UTC nanoseconds, through `CLOCK_REALTIME`)
  - monotonic time
  - level
- stdout is NOTICE and stderr is WARNING by default; `astra_log*` sets its
  own level.
- Partial lines are flushed at exit and on `fflush`. The descriptor gets a
  lock, since today it has a static buffer and is not thread-safe.
- The kernel ring stays for kernel events and for `astra_log` from programs
  without a log session (early boot, the supervisor, crash paths). The log
  service still drains it and stores both, with one ordering by wall time
  and sequence.

### 3. The store

- **Segments**: append-only files in the log service's `STORE:`
  (`/system/var/log/` on the volume). A segment holds framed records with a
  CRC per record. The active segment is `current`; it rolls at a size limit
  (default 1 MiB) or at local midnight, whichever comes first.
- **Compression**: a closed segment is compressed with LZ4 (block format,
  one block per 64 KiB) on the log service's idle thread. LZ4 is chosen
  because:
  - it is the only codec cheap enough to run on an MC68040 without being
    felt;
  - its decoder already exists in the ROM;
  - text logs compress 3–5×.
  The encoder is about 400 lines, carried in the system library with a
  host-test oracle against the reference implementation.
- **Retention** (`/config/services/events/settings.conf`):
  - `ttl` defaults to 7 days. Segments whose newest record is older are
    deleted.
  - `max_bytes` defaults to 32 MiB. The oldest segments go first when the
    total exceeds it.
  - `min_free` defaults to 5% of the volume. If free space falls below it,
    logging pauses and says so once rather than filling the disk.
  - Expunge runs at roll time and hourly.
- **Index**: each closed segment's header records:
  - first and last time
  - the level mask
  - the set of program identifiers
  The viewer skips segments that cannot match a filter without
  decompressing them.

### 4. Reading

- **Query protocol** on the log service. A request carries:
  - time range
  - minimum level
  - program
  - pid
  - stream
  - subsystem
  - substring
  - direction and limit
  A follow mode streams new records. The filter is evaluated in the service,
  so a viewer never pulls the whole store over IPC.
- **`log` command** (replaces `events` for reading), for example:
  `log show --last 1h --program org.chocolate-doom.doom --level warning`,
  `log stream --grep MIDI`, `log config ttl 14d`.
- **Console.app**:
  - a table of records with sortable columns: time, level, program, pid,
    stream, message
  - a filter bar (level, program, time range, text) and a live tail toggle
  - a sidebar of programs and boots
  It reads through the same query protocol.

## Measurement before it ships

- **Throughput.** Chocolate Doom's start-up output through the new
  descriptor, compared with today's per-line `LOG_WRITE`: guest instructions
  and records lost. The bar is zero lost records.
- **LZ4 cost** per MiB on the DE25 guest, and the compression ratio on real
  logs.
- **Store growth and expunge** in a fsstress-style gate. It must also show
  a crash mid-roll leaves a readable store, because segments are
  append-only and CRC-framed.
