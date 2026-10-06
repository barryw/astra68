# System log: one place for every program's output

Status: design, approved for planning 2026-10-05. Supersedes the 2026-10-01
proposal (segments, LZ4, query protocol), which is in git history.

## The problem

A program started from the desktop has nowhere to speak. Its stdout and
stderr fall back to a log descriptor, each line becomes `astra_log` records of
24 bytes in the kernel trace ring, the events service drains that ring once a
second, and keeps 256 records in total for the whole machine. Chocolate Doom's
start-up banner alone wraps the ring. The output was written and then
overwritten, which is why Doom cannot be debugged from its logs.

Beside that:

- stdout and stderr share one log description and cannot be told apart
  (`sw/userspace/posix/src/console.c:1009-1017`); the line buffer is static and
  unlocked, and a partial line is never flushed at exit.
- `open App.app` from Terminal hands the bundle no streams (APPL v4,
  `supervisor/src/loader.c:2476`), so it logs instead of printing.
- `astra_log` and `SDL_Log` (patched in `sdl2/prepare_source.py:202-222`, cut at
  256 bytes) always go to the log, even with a terminal attached.
- Records carry no wall-clock time. The only reader is the `events` command.
- Every log line is a syscall, and the events service runs at priority 20,
  above every application: logging costs the program that does it.

## Requirements

1. **One logging API.** A program never chooses a destination. The same call
   prints in a terminal and is recorded when there is no terminal.
2. **Logging never blocks and never slows a program down.** No syscall or IPC
   per line, no lock, no waiting on the log service or on another thread. When
   the system cannot keep up, lines are dropped and the drop is recorded; the
   caller does not wait.
3. **Everything goes to one place**: applications, services, supervisor and
   kernel, in one file with one format, ordered by time.
4. **Easy to find and search**: a `log` command with filters, plain text that
   `grep` reads, and a copy on the host while the machine runs.
5. **Rotated and compressed like a normal system**: closed files are gzip
   (`syslog.1.gz` …), readable by `zcat`, `zgrep` and `less` on any host.
6. **The last lines of a crashed program are kept.**

## How Haiku does it, and where this differs

Haiku (`~/Git/haiku`) has one daemon with one port and one record:

- `struct syslog_message` (`headers/private/syslog_daemon/syslog_daemon.h:22-29`)
  is sent by both POSIX `syslog()` and the kernel.
- `syslog()` (`src/system/libroot/posix/syslog.cpp:131-185`) sends one port
  message per call. `LOG_PERROR` also writes the line to stderr (`:141-145`).
- The kernel keeps a syslog ring, `syslog_write` (`src/system/kernel/debug/debug.cpp:1302-1333`).
  A sender thread forwards it to the daemon (`:1223-1298`), and overflow is
  marked `<DROP>` (`:1250-1256`).
- The daemon appends plain text to `/var/log/syslog`
  (`src/servers/syslog_daemon/syslog_output.cpp:68-98`), rotated at 512 KB with
  one history file (`:36-37, :74-91`).
- Time is read without a syscall from `real_time_data`, a kernel page mapped
  into every team (`src/system/kernel/real_time_clock.cpp:108`,
  `src/system/libroot/os/time.cpp:29`). It is double-buffered behind a version
  word (`src/system/libroot/os/arch/m68k/time.c:31-41`).
- There is no viewer; the answer is `tail` and `grep`.

Astra takes the shape unchanged: one service, one record, kernel and userspace
in one file, plain text with size rotation, `LOG_PERROR`-style echo, and the
time page. It differs in three places, each for a stated reason.

| Haiku | Astra | Reason |
|---|---|---|
| Apps launched from Tracker inherit `/dev/null` (`src/servers/launch/LaunchDaemon.cpp:2091-2103`) | Their stdout and stderr go to the log | This loss is the problem being fixed |
| One `write_port` per `syslog()` call | Per-process shared ring, one doorbell per burst | Requirement 2: a port write per line is a syscall per line |
| No viewer | `log` command now, Console.app later | Requirement 4 |
| Rotations kept plain (`syslog.1`) | Rotations gzip-compressed | Requirement 5 |

## 1. The rule

| Call | Program has a terminal | No terminal |
|---|---|---|
| `printf`, `write(1, …)` | terminal | log: `notice`, stream `stdout` |
| `fprintf(stderr, …)`, `write(2, …)` | terminal | log: `warning`, stream `stderr` |
| `astra_log*`, `syslog()`, `SDL_Log`, `assert` | log, and echoed to stderr | log |

"Has a terminal" means the fd is a terminal stream (`isatty`). In a terminal,
plain stdout and stderr are not copied to the log: `cat` output does not belong
in the system's record. An explicit log call is always recorded.

The echo to stderr is a try-write. When the terminal stream is full the echo is
skipped and the record still reaches the log, so a slow terminal cannot stall a
logging call.

## 2. The API

- **Native**, in the runtime (no formatter there):
  `astra_log_line(level, text, length)`. The existing `astra_log`,
  `astra_log_write`, `astra_log_debug`, `astra_log_failure` and
  `astra_log_assertion` keep their signatures and route through it.
- **POSIX**, in `sw/userspace/posix`: `openlog`, `syslog`, `vsyslog`,
  `setlogmask` and `closelog`. They format with picolibc into a stack buffer
  and call `astra_log_line`. `LOG_PERROR` is accepted, and the terminal echo
  in §1 already does what it asks. Priorities map as: `LOG_DEBUG` → debug,
  `LOG_INFO` → info, `LOG_NOTICE` → notice, `LOG_WARNING` → warning,
  `LOG_ERR` and above → error.
- **SDL**: the `SDL_log.c` patch calls `vsyslog` with SDL's priority mapped the
  same way. The 256-byte cut goes.
- **The log descriptor** (fd 1 or 2 with no stream): fd 1 and fd 2 get separate
  descriptions with their own level and stream tag. Each thread assembles its
  own line in a `_Thread_local` buffer (`_Thread_local` works in `libc.library`,
  `posix/src/pthread.c:44`). A line is emitted at a newline or when the buffer
  holds 240 bytes. This buffering is needed because picolibc's stderr is
  unbuffered and calls `write` once per character
  (`third_party/picolibc/libc/stdio/posixiob_stderr.c`), and one record per
  character would spend a slot on each byte. Nothing is shared between
  threads, so nothing is locked. The exiting thread's partial line is flushed
  by a `destructor` that runs after stdio's own flush. A partial line that has
  no newline when the program faults is lost, and nothing else is.

The **ident** names the program in every line. It is the `ASTRA_PROGRAM` name
when the image declares one, otherwise the basename of argv[0]. `openlog`
overrides it.

## 3. Transport: a lock-free ring per process, no syscall per line

Each logging process has one **log ring**: a bulk ring
(`docs/SHARED_AREAS_AND_BULK_RINGS.md`) in a 64 KiB shared area it owns. The
log service is the consumer.

It is one ring per process, not one per thread, for a measured reason.
Per-thread state needs thread-local storage, and the shared `runtime.library`
has none (`runtime/src/syscall.c:25-27`: it is the dynamic loader and cannot
relocate its own TLS). A4, the thread pointer, is 0 in any program without a
TLS segment (`sw/kernel/process.c:3817`).

**Writers never wait for each other.** The ring has one producer endpoint, but
the writers inside the process coordinate without a lock:

1. **Reserve.** A writer claims the slots it needs with one compare-and-swap
   on a process-local `reserved` counter. The claim succeeds only if
   `reserved + slots - consumer_position <= capacity`, using the consumer
   position in the shared header. If it fails, the ring is full: the line is
   dropped and the process's `dropped` counter is incremented with a CAS.
2. **Write.** The writer copies the record into its slots, and then stores its
   slot's commit word (the slot's position + 1) after a release fence.
3. **Publish.** A writer advances the shared `producer_position` over every
   slot whose commit word is set. It does this with a CAS on the header word,
   so several writers publishing at once stay monotonic.

A writer preempted between reserve and commit holds back publication of later
slots until it runs again, but no writer ever spins or sleeps on it: the
others return at once. CAS uses the explicit compare-exchange loop the NDK
mutex already uses (`ndk/include/astra/runtime.h:796`), because of the MC68040
GCC atomic-exchange defect recorded in `docs/CURRENT_STATE.md`.

A thread killed between reserving and committing leaves its slots uncommitted
forever; publication stops there and every later line is dropped and counted
until the process exits. This is an accepted limit: Astra does not kill
threads asynchronously, and a fault ends the whole process rather than one
thread.

**Setup.** On the process's first log call, the runtime:

1. creates the area and ring with `astra_rt_area_create` and
   `astra_rt_ring_create`;
2. maps the area read-write and keeps the producer endpoint;
3. sends the consumer endpoint and a `READ|MAP` duplicate of the area handle to
   the `LOG` port in **one** `astra_port_send`.

`astra_port_send` never blocks: a full queue returns `WOULD_BLOCK`
(`sw/kernel/port.c:917-922`). The kernel stamps the sending process's id on the
message (`sw/kernel/port.c:946`), so the service learns the pid from the kernel,
and a program cannot log in another program's name. Setup is claimed with one
CAS, so a second thread that logs during it does not wait: it uses the
`LOG_WRITE` fallback for that line. If the send is refused, that line also goes
to `LOG_WRITE`, and setup is retried after 64 further log calls.

**Record.** A record is one or more consecutive 252-byte slots, reserved
together, so a record is never interleaved with another. Each slot begins with
its commit word. The record header carries:

- a per-process sequence number
- monotonic and wall time from the time page (§4)
- level and stream
- the dropped-line count at the time of writing
- the text length

There is no thread id: reading one costs a syscall in the shared runtime.

**Doorbell.** A 4-byte message to the `LOG` port, sent with the
non-blocking `astra_port_send`. A writer sends it only when its record begins
where the consumer had stopped, that is, when the ring was empty as far as the
service knew. During a burst the service is not running, so its position does
not move and no further doorbells are sent: that is one syscall per burst, not
per line.

A full `LOG` queue means a wake is already pending, so `WOULD_BLOCK` is
ignored. Several writers sending at once is harmless, so no flag serializes
them. The service also sweeps every ring once a second as a backstop.

The kernel ring object is never notified, by either side. It exists so that
the service's consumer endpoint reports `PEER_DEAD` when the producing process
dies. Notifying through it would be wrong here: the service reads published
slots straight from the shared header, and a consumer position reported beyond
the kernel's producer shadow fails the ring (`sw/kernel/ring.c`, consumer
branch of `kernel_ring_notify`).

**Full ring.** The line is dropped and counted; the caller returns at once. The
next record carries the count, and the service writes `[N lines dropped]`
before it.

**Crash.** After the producer dies, the consumer may still drain published
elements. The service drains the ring, then sees `PEER_DEAD` and frees it.

**Priority.** The log service drops below applications (from 20 to 12). Waking
a lower-priority thread never preempts the running one
(`sw/kernel/process.c:1519-1528`), so the service drains only in time nobody
else wants. If an application never yields, its ring fills and lines are
dropped and counted, never delayed. That is requirement 2 applied as stated.

**What is not ours to unblock.** A `printf` waits on picolibc's `FILE` lock
when another thread of the same program is inside `printf` on the same stream.
That is stdio's contract in every C library, and it sits above logging.
`syslog()`, `SDL_Log` and `astra_log*` do not use `FILE`.

**The kernel ring stays** for kernel events, typed `ASTRA_EVENT*` records, and
`LOG_WRITE` from code that runs before or without the log service: early boot,
the supervisor before the service starts, and fault reports. The service
drains it too and writes its text and events into the same file.

This replaces `docs/OBSERVABILITY.md`'s rule that all user records share the
trace ring. Measured, the rule makes program text evict kernel events and costs
a syscall per 24 bytes.

## 4. The time page

This is Haiku's `real_time_data` for Astra: one kernel page mapped read-only at
`ASTRA_TIME_PAGE_ADDRESS` (`0x48000000`) in every process. It is a new region,
carved from the reserved window, which `docs/MEMORY_MAP_AND_PMMU.md:42-46` names
as the place for new fixed facilities. The frame is `KERNEL_FRAME_SHARED`, owned
by the kernel, and mapped through `kernel_vm_map_shared_page` with a new alias
class. It is mapped at launch and into the replacement space at exec; fork
copies it as it copies every shared frame. Every RAM mapping on this machine
is write-through and the MC68040 caches are physically tagged, so a kernel
store through the identity alias is what the next user read sees
(`docs/MEMORY_MAP_AND_PMMU.md:133-149`). The page begins with a magic word; a
reader that does not find it falls back to the clock syscalls. It holds:

- a version word
- two copies of {monotonic ns, wall-clock ns since the epoch, UTC offset,
  zone name}

The kernel writes the inactive copy and then bumps the version, on every timer
tick and every context switch. A reader loops until the version is unchanged
across its read. Resolution is at most one quantum (5 ms,
`sw/include/astra/process.h:110`) and usually better.

The source is the one `CLOCK_MONOTONIC` and `CLOCK_REALTIME` already use
(`sw/kernel/platform.c:328-360`). `clock_gettime` keeps its syscall, because
programs that need 80 ns resolution need the register. The page is for callers
that need a cheap time: the logger first, then `SDL_GetTicks` and frame pacing
if measurement says so.

## 5. The store

- **The file.** The events service, as the log service, appends one line per
  record to `STORE:syslog`. That is `/events/syslog` on the boot volume. The
  format is:

  ```
  2026-10-05T14:03:22.117-04:00 notice chocolate-doom[10000011] stdout: V_Init: allocate screens.
  ```

  The fields are local wall time with offset, level, then ident `[pid]` with
  the pid in hex as `ps` prints it,
  then the stream (`stdout`, `stderr`, `log` or `kernel`), then the text.
  Non-printable bytes are written as `\xNN`, so one record is always one line.
- **Order.** Each drain pass merges the heads of all rings and the kernel ring
  by monotonic time, so the file is in time order to within one quantum.
- **Rotation.** The log rotates when `syslog` reaches 1 MiB, or at the first
  write after local midnight, whichever comes first. Rotating renames `syslog`
  to `syslog.1` and shifts older files up (`syslog.1.gz` → `syslog.2.gz` …).
  The service then compresses `syslog.1` to `syslog.1.gz` in the background.
- **Compression.** Rotated files are gzip, produced by zlib (vendored unmodified
  in `third_party/zlib`). They are written to `syslog.1.gz.tmp`, synced and
  renamed, and only then is `syslog.1` removed, so a crash leaves either the
  plain file or the complete `.gz`, never a torn one. At start the service
  compresses any plain `syslog.1` left behind.
  - Compression runs at the service's priority (§3), below every application, a
    block at a time between drains, so incoming lines are never held behind it.
  - Text logs typically compress four to eight times; the plan measures both
    ratio and cost per MiB on the DE25.
- **Retention.** Compressed files are kept until any limit is reached, oldest
  deleted first:
  - 14 files
  - 16 MiB in total, compressed
  - 5% free on the volume. Below it, the service stops writing, records one
    `log: paused, volume full` line, and resumes when space returns.
  The limits live in `CONFIG:services/events/settings.conf`.
- **Boot.** Each boot begins with
  `--- boot 2026-10-05T14:03:20-04:00 build <id> ---`.
- **Host copy.** The service is granted `WORK:rw` and appends the same lines to
  `WORK:.astra/syslog`. That is `/var/lib/astra/hostfs/work/.astra/syslog` on
  the DE25, which can be read from Beast with `tail -f` while the machine runs.
  The host copy rotates with the same rule and is not compressed by the guest:
  the host has the CPU for that and the guest does not.
- **Writes are batched.** Each drain pass appends its lines in one write per
  file.
- **`debug` is not written.** It is dropped at drain and counted, as the event
  store does today. Terminal emits every line it prints at `debug`
  (`runtime/src/log.c:198-206`), and that is commentary, not record.
- **Kept as it is:** the 256-record tiered store, which holds notification
  history.

## 6. Reading

- `log show [--boot N] [--last 10m] [--level L] [--program P] [--pid N]
  [--stream S] [--grep TEXT]` prints matching lines from `syslog` and its
  rotations, oldest first, inflating `.gz` files as it reads. It skips a
  rotated file whose first and last timestamps fall outside `--last`. `--boot -1` means the previous boot, found by its
  marker.
- `log stream [same filters]` follows the file.
- The files are readable directly through the existing `EVENTS:` tree as
  `EVENTS:log/syslog` and `EVENTS:log/syslog.N.gz`, so `grep`, `less` and
  `tail` work on the current file. A `zcat` command (zlib) is added for the
  rotations.
- `log` replaces `events` for reading text. `events` stays for typed records.
- Console.app reads the same files with the same filters. It gets its own spec.

## 7. Grants and launch

- **`LOG` for every process.** `LOG` is the send endpoint the log service
  publishes.
  - `build_grants` gives it to every entry, as it does `LIBS:`
    (`supervisor/src/loader.c:1017`).
  - Terminal's `launch_grants` adds it
    (`services/terminal/console_session.c:98`).
  - It joins the application ceiling (`sw/include/astra/application_policy.h`).
  - zsh children inherit it through exec.
- **APPL v5.** A launch request may carry STDIN, STDOUT and STDERR handles.
  - They are granted only when the requester transferred them, so the ceiling
    stays manifest-defined.
  - `open` passes its fds 0–2 when they are a terminal. The desktop passes
    none.
  - A terminal-started bundle joins the terminal's foreground process group.
    `open -W` waits for it to exit.

## 8. Testing

Host tests:

- the time page reader against a writer that changes the copy mid-read
- the record encoder and decoder, including continued lines and the drop count
- the line formatter, including escapes
- rotation at the size boundary and at midnight
- compression: a `.gz` the host's `gunzip` accepts and that matches the input
  byte for byte; a crash between each step of rotate, compress, sync, rename and
  remove leaves a readable set
- retention by count, by total size, and the free-space pause
- the merge order across rings
- POSIX routing for every cell of §1, the fd 1 and fd 2 levels, and the
  partial-line flush at exit
- `syslog` priority mapping

QEMU gates:

- Launch Chocolate Doom from the desktop. Every line of its start-up banner must
  be in `EVENTS:log/syslog` with ident `chocolate-doom`, stream `stdout` and the
  right order.
- A program writes 5000 lines as fast as it can. Its wall time with logging must
  be within 5% of the same run with logging compiled out, and the file must
  hold all 5000 lines or one exact drop count.
- A program logs a line and faults. The line must be in the file.
- `open` from Terminal prints the bundle's output in the terminal.

Perturbation, so a green gate means the path is really taken:

- Shrink the ring to 2 slots and require the drop marker.
- Stop the service draining and require the burst test's wall time to stay
  within the bound.
- Run the burst from four threads at once and require the same bound: no
  thread waits on another.
- Remove the doorbell and require lines to arrive late, then not at all.

Board: run Doom on the DE25 and read the banner from Beast with
`tail -f /var/lib/astra/hostfs/work/.astra/syslog`.

## 9. Phases

1. **Doom can be debugged.**
   - the time page
   - the log ring and `astra_log_line`
   - fd 1 and fd 2 log descriptions
   - the `LOG` grant
   - the service drain, the file, size and midnight rotation and the host copy
   - the priority change
   - `log show` and `log stream`
   - Doom gate and burst gate
2. **Compression and retention.** zlib, `.gz` rotations, the retention limits,
   `zcat`, and `log show` reading `.gz`.
3. **One API everywhere.** `syslog()`, the SDL_Log route, the terminal echo,
   and APPL v5 with `open` passthrough.
4. **Console.app**, on its own spec.

## Resolved during planning

- **A try-send exists:** `astra_port_send` never blocks.
- **Per-thread state:** there is none in the shared runtime, so the design uses
  one ring per process with CAS (§3).
- **Slot size:** capacity must be a power of two and the 64-byte header shares
  the area, so the ring is 256 slots of 252 bytes: 64 + 256 x 252 = 64,576
  bytes in a 65,536-byte area.
- **Publishing `LOG`:** the manifest's `serves` list maps names to
  `astra_service_ready` handles by position (`supervisor/src/loader.c:1505`).
  `LOG` is a raw capability, granted by name like `ENTROPY`.
- **zlib's cost on the DE25** is measured in phase 2.
