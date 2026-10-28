# Debugging Astra

What a developer has for diagnosing a program on this machine, and what each
thing is for. Everything here works today; the gaps are named at the end
rather than left to be discovered.

The split worth holding in mind: **the diagnostic surface is a build
decision**, `ASTRA_KERNEL_DEBUG_SURFACE` in `sw/kernel/process.h`. It is on in
every build today. Turning it off removes the serial monitor and stops any
process being granted `ASTRA_RIGHT_DEBUG` over itself. Logging does not
depend on it: a program can always write to the system log. One switch,
because a machine in the field
should not be one forgotten `#ifdef` away from answering strangers on a UART
with its page tables.

---

## 1. A debugger, with source

Every m68k object is compiled with `-g`. It costs nothing in what ships: the
kernel payload is a raw binary and the user image is stripped before packing,
and `pack_payload.py` refuses a payload carrying `.debug` sections so that
stays true.

```sh
cd sw/boot && make astra_boot.bin          # on Beast
QEMU=/tmp/qemu-final-build/qemu-system-m68k ./emu/qemu/debug.sh --image /tmp/part.img
```

The machine starts stopped with gdb attached and all three symbol tables
loaded — the ROM at `0xffe00400`, the kernel at its link address, the user
image at `0x00100000`. `continue` boots it.

```
(gdb) break console_session_run_backend
Breakpoint 1 at 0x1011d6: file console_session.c.
(gdb) break kernel_process_on_fault
Breakpoint 2 at 0x205a29c: file process.c, line 5109.
```

The machine is three separate ELFs, which is why the script loads three symbol
tables. A gdb pointed at one of them can name a third of the system.

`--no-gdb` leaves the stub listening and prints how to attach by hand — for
attaching from another host, or a different debugger.

## 2. A program can say something

Print, or call the log. Every line ends up in one file, the system log, in
time order with every other program's, the services' and the kernel's
(`docs/SYSTEM_LOG.md`):

- `printf` and `write(1, ...)` with no terminal attached are recorded at
  `notice` on stream `stdout`; stderr is recorded at `warning` on `stderr`.
  In a terminal they go to the terminal only.
- `astra_log("text")`, `astra_log_failure` and `astra_log_line(level,
  stream, text, length)` are recorded wherever the program runs. A native
  program's `assert` comes through here too; a POSIX program's is picolibc's
  and goes to stderr.

```
2026-10-05T14:03:22.117-04:00 notice chocolate-doom[10000011] stdout: V_Init: allocate screens.
```

Local time with its offset, level, the program's name and its process id in
hex as `ps` prints it, the stream, the text. The name is the image's
`ASTRA_PROGRAM` name, else the basename of argv[0].

Nothing gates the write and nothing waits on it. The first line sets up a
ring shared with the log service; every later line is a store into it, with
no syscall. A program that writes faster than the service drains loses
lines, and the file says so: `[N lines dropped]`, at the place they were
lost. `debug` lines are not written to the file.

Reading it:

| Where | How |
|---|---|
| In the guest | `log show`, filtered by `--boot N` (0 this boot, -1 the one before), `--last 10m`, `--level L` (that level and above), `--program P`, `--pid HEX`, `--stream S`, `--grep TEXT`; `log stream` with the same filters follows it until a line is entered. `cat`, `grep` and `tail` work on `EVENTS:log/syslog` |
| On the board, from Beast | `ssh root@192.168.1.52 tail -f /var/lib/astra/hostfs/work/.astra/syslog` |
| Under a QEMU gate | `<work>/hostfs/.astra/syslog`, the run's host directory |

The file rotates at 1 MiB or at the first line on a new local date, keeping
`syslog.1` to `syslog.3`. `log show` reads those too, oldest first.

Rules worth knowing before they surprise you:

- One line is one record of at most 1,944 bytes; a longer one is truncated.
- Control bytes are written as `\xNN` and a backslash as `\\`, so a line
  cannot forge another line or clear a terminal. UTF-8 passes through.
- A program can only write in its own name: the kernel stamps the process id
  on the ring's setup.
- The log service itself never logs. A line that cannot reach a ring (no
  `LOG` capability, the service gone) goes to the kernel event ring, which
  the service also drains into the file.

The gate is `emu/qemu/test-system-log.py QEMU ROM IMAGE`: cost per line,
bursts that overrun the ring, the doorbell wake, ten programs exiting at
once, a fault, `log show`, `log stream`, and Doom's banner. `--no-sweep`
runs it against a build of the service with no timed wake, so only the
doorbell can wake it; `--only CHECK` runs a subset.

## 3. Assertions carry evidence

`assert()` in a native program (the runtime's `assert.h`) writes
`file:line: expression` to the system log at `error`, then exits with `ASTRA_ASSERT_STATUS_TAG | line` as it always did.

Both halves exist because they reach different readers: the status reaches
whoever waits on the process, and a status is one word that cannot say which
of two files had an assertion on line 231.

## 4. A fault says where and what

A user fault reports itself before the process is retired:

```
*** user fault: process 0x10000011 thread 0x20000010
    pc 0x0010044A  address 0x70000004  vector 2
    the address is this thread's stack guard page: the stack ran past its reservation
    symbolize with tools/symbolize.py
```

The last line before the hint is the part the kernel knows and you do not: it
owns every stack and guard page. An address in the stack arena but outside this
thread's own stack is said as that; anything else is not classified at all,
because a confident wrong answer sends you somewhere else entirely.

Then:

```sh
python3 tools/symbolize.py 0x0010044A 0x70000004
0x0010044a  user: astra_main at src/main.c:258
0x70000004  (no image covers this address)
```

It routes each address to the image that can explain it and runs `addr2line`
there. Piping a whole report in works too — it picks the addresses out.

## 5. The live monitor

The kernel answers commands on the UART while the machine runs:

```
help  build  threads  runq  current  waits  mem  pages  maps
handles  ports  irqs  perf  trace  mmu  faults  devices
```

`trace` is the ring the panic path dumps the tail of: 2047 records of PMMU
faults, syscalls, context switches and VM maps. `threads` and `current` are
where a hung thread's state is. `mem` and `pages` are where a leak shows.

This is the surface that closes with `ASTRA_KERNEL_DEBUG_SURFACE`.

## 6. Panic display and retained report

The final 90x30 panic screen is deliberately a summary, not the raw trace
stream. It keeps the reason, build and hardware identity, last supervisor IRQ,
the faulting process/thread/PC/address when present, and six recent trace
records visible without scrolling. The complete serial report remains at
`/data/astra/log/panic-latest.log`; use the matching unstripped ELF with the PC
shown on screen:

```sh
m68k-linux-gnu-addr2line -f -e <program.elf> 0x001064F0
```

Userspace programs share a virtual address range, so the matching program ELF
matters. Do not symbolize a service fault against the supervisor ELF merely
because both use addresses beginning at `0x00100000`.

## 6a. Boot splash and the progress record

On the DE25 the display helper shows `share/astra_boot_splash.rgb565` with a
bar from power-on to the guest's first frame. The bar is driven by the boot
progress record (`sw/include/astra/boot_progress.h`): two big-endian words at
text-page offset `0xFE0`, written by the ROM during POST and by the kernel
after handoff. Two phases, as Haiku's boot_splash: the 18 fixed steps every
boot takes (10 ROM, 4 kernel, supervisor stages 1, 5, 6, 7) fill the first 30% in
equal slices; the boot services fill the remaining 70% in N equal slices once
the supervisor announces N (`Boot services ....... N to start` on serial);
stage 8 is 100%. Stage 8 is DONE only if all 18 fixed steps and exactly N
service reports arrived; anything else is FAILED.

Service failure is tier-aware, as Haiku's launch_daemon only logs a job that
fails. A `required` or `critical` service that cannot start marks the record
FAILED. An unmarked (user-tier) service that fails -- `media` and `sampler` in
the manifest, and every `/config/services` definition such as `ramfs` and
`remote-desktop` -- is reported SKIPPED: its slice still fills, the bar
completes, the record ends DONE, and the failure is visible only in the system
log (`launch-failed /services/...`) and on serial.

The helper shows the text page instead when the record says FAILED (a POST
failure or ROM miscount, a required or critical service that failed to start,
a degraded or panicked kernel), after a panic, or when the record has a
version it does not know.

**A failure after the desktop has drawn is not on screen.** Once the guest
presents its first frame the helper hands the display over, and only a panic
takes it back. On the board the desktop presents before the last boot
services report (`SPLASH-HANDOFF-NOTE`), so a FAILED that arrives after that
stays in the record (state FAILED) and the system log. Check both when a
service is missing. A display helper that restarts after the handoff draws the
splash (or the text page, if the record says FAILED) over the desktop until
the guest next presents.

Read it on the board: `od -A n -t x1 -j 4064 -N 8 /run/astra/post-text.bin`
(`42 50 00 01`, then state:4 announced:1 fixed:6 0:1 count:10 done:10).
Under QEMU: `xp /2wx 0xFFF22FE0`. Gate: `emu/qemu/test-boot-progress.py`.

---

## 7. Where the time goes, on the board

`astra-top` answers "what is using the machine" on the DE25 in one command,
guest and host together. It ships in every release:

```sh
/var/lib/astra/current/bin/astra-top 10          # a 10-second window
/var/lib/astra/current/bin/astra-top 10 --perf   # plus host hot spots
/var/lib/astra/current/bin/astra-top 10 --json   # for A/B scripts
```

- **Guest**: CPU per Astra process from the kernel's own runtime
  accounting, idle, context switches per second (cross- and same-address
  space) and why the CPU changed hands, one cause per switch (block, yield,
  quantum, deadline, preempt, exit, idle; they sum to the total). The `sampler` service copies PROC:snapshot
  and PROC:scheduler to `WORK:.astra/sample` once a second
  (`/var/lib/astra/hostfs/work/.astra/sample` on the board).
- **Host**: CPU per thread of QEMU and the helpers. The vCPU thread is the
  68040; when it is near 100% the guest is CPU-bound.
- **Output**: display presents per second and audio underruns and gaps.
- **`--perf`**: the hottest host symbols in the vCPU thread -- TCG-generated
  code, softmmu refills, helpers: the emulator's share, which no guest
  counter shows.

For "which guest function", use the instruction profiler: `/data/prof`
(`ctl.py start LABEL|stop`) with `tools/astra-prof report`. It counts
guest instructions per block, not time, so read it beside `astra-top`.

Measure before and after a change, on the same workload, interleaved:

```sh
/var/lib/astra/current/bin/de25-ab.sh -n 2 OLD-RELEASE NEW-RELEASE
```

switches release, restarts, opens Doom over the remote desktop
(`de25-rfb.py`, which can also type text into the guest with `--keys`),
and takes idle / 125 Hz pointer motion / idle `astra-top` windows for each,
A B A B, one JSON object per window. Releases stay in
`/var/lib/astra/releases`, so a release published before a change is the
baseline for the one after it.

## The rest of the kit

| Want | Use |
|---|---|
| What every process is consuming | `ps` or `cat PROC:<pid>/status` |
| Where host I/O is bottlenecked | `metrics` (fixed records are also at `METRICS:snapshot`) |
| Does the terminal still work end to end | `python3 emu/qemu/test-terminal.py <qemu> sw/boot/build/astra_boot.bin --image /tmp/part.img` (refreshes the workspace ROM first) |
| Where the boot time went | `python3 emu/qemu/time-boot.py ... --budget 1.0` |
| What the kernel suites actually cover | `cd sw/kernel && make coverage` |
| Memory errors in userspace, on the host | `cd sw/userspace && make sanitize` |
| The static analyzer (GCC only, so Beast) | `cd sw/userspace && make analyze` |
| Reading the character plane from outside | `xp /2700xb 0xFFF22000` in the QEMU monitor |
| Reading the boot progress record | `xp /2wx 0xFFF22FE0` in the QEMU monitor; `od -A n -t x1 -j 4064 -N 8 /run/astra/post-text.bin` on the board |

## Known gaps

- **The monitor's gate is the build, not the connection.** A build with a debug
  surface answers whoever reaches the UART. Per-connection authority would be
  `ASTRA_RIGHT_DEBUG` held by an operator process; the right exists and the
  monitor does not consult it.
- **The trace ring does not survive a reset**, so a crash in the field leaves
  nothing behind.
- **Nothing symbolizes a kernel stack**; the fault report gives one program
  counter, not a backtrace. gdb will walk it, so this matters only for a
  machine you cannot attach to.
