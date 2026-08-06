# Debugging Astra

What a developer has for diagnosing a program on this machine, and what each
thing is for. Everything here works today; the gaps are named at the end
rather than left to be discovered.

The split worth holding in mind: **the diagnostic surface is a build
decision**, `ASTRA_KERNEL_DEBUG_SURFACE` in `sw/kernel/process.h`. It is on in
every build today. Turning it off removes the serial monitor and stops any
process being granted `ASTRA_RIGHT_DEBUG` over itself, which closes the
diagnostic channel to everyone. One switch, because a machine in the field
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
(gdb) break console_shell_run
Breakpoint 1 at 0x1011d6: file src/console_shell.c, line 530.
(gdb) break kernel_process_on_fault
Breakpoint 2 at 0x205a29c: file process.c, line 5109.
```

The machine is three separate ELFs, which is why the script loads three symbol
tables. A gdb pointed at one of them can name a third of the system.

`--no-gdb` leaves the stub listening and prints how to attach by hand — for
attaching from another host, or a different debugger.

## 2. A program can say something

`astra_log("text")` puts a bounded line on the console the operator reads,
prefixed with the writing process's id:

```
[log 10000011] volume mounted at /
```

It is a capability, not a channel. The kernel gates the write on a process
handle carrying `ASTRA_RIGHT_DEBUG`; the runtime binds that handle when
`astra_startup_validate()` accepts the startup block. **Every call can be
refused**, and the status is returned rather than acted on — a build without a
debug surface refuses all of them, and a program that stops working when its
diagnostics are turned off is a program with a bug in it.

Rules worth knowing before they surprise you:

- At most `ASTRA_LOG_MAX_BYTES` (128) per line. `astra_log()` cuts a longer
  string rather than splitting it, because half a line arriving under another
  process's prefix is worse than a line that ends early.
- Bytes that are not printable arrive as dots. The console is shared with the
  kernel's own output; a program that could write escape sequences could clear
  the screen or dress its next line up as a panic.
- A handle to *another* process is refused even when it carries DEBUG. That is
  a debugger's authority over a process, not the authority to write lines in
  its name.

## 3. Assertions carry evidence

`assert()` in userspace now writes `file:line: expression` on the diagnostic
channel, then exits with `ASTRA_ASSERT_STATUS_TAG | line` as it always did.

Both halves exist because they fail in different circumstances: a build with no
debug surface has only the status, and a status is one word that cannot say
which of two files had an assertion on line 231.

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

---

## The rest of the kit

| Want | Use |
|---|---|
| Does the terminal still work end to end | `python3 emu/qemu/test-terminal.py <qemu> sw/boot/astra_boot.bin --image /tmp/part.img` |
| Where the boot time went | `python3 emu/qemu/time-boot.py ... --budget 1.0` |
| What the kernel suites actually cover | `cd sw/kernel && make coverage` |
| Memory errors in userspace, on the host | `cd sw/userspace && make sanitize` |
| The static analyzer (GCC only, so Beast) | `cd sw/userspace && make analyze` |
| Reading the character plane from outside | `xp /2700xb 0xFFF22000` in the QEMU monitor |

## Known gaps

- **The monitor's gate is the build, not the connection.** A build with a debug
  surface answers whoever reaches the UART. Per-connection authority would be
  `ASTRA_RIGHT_DEBUG` held by an operator process; the right exists and the
  monitor does not consult it.
- **The metrics registry still has no reader.** `astra_metric_register` is
  called by nothing and `--gc-sections` collects it. Every performance number
  in this repo comes from outside the machine.
- **The trace ring does not survive a reset**, so a crash in the field leaves
  nothing behind.
- **Nothing symbolizes a kernel stack**; the fault report gives one program
  counter, not a backtrace. gdb will walk it, so this matters only for a
  machine you cannot attach to.
