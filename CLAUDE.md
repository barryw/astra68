# Astra 68 — read this first, every session

This file is loaded automatically. `AGENTS.md` defines the execution topology;
`docs/CURRENT_STATE.md` defines the current engineering state.

`docs/INVENTORY.md` is the complete audit: every machine, board, subsystem,
toolchain and build command. Read it when you need detail. This page is the
orientation you need before touching anything.

---

## The machines

| Host | Address | What it is | Use it for |
|---|---|---|---|
| Mac | local | Apple Silicon, this session's cwd `/Users/barry/Git/astra68` | Editing, userspace + ELF work, host tests |
| `beast` | 192.168.1.3 | 32 core, 61 GB, Ubuntu | **Everything that must build or run for real**: kernel, boot ROM, m68k gates, QEMU, analyzer |
| `astra-de25` | 192.168.1.52 | **The board.** DE25-Nano, AArch64, Linux | Running Astra on hardware |
| `nas.lan` | 192.168.1.5 | Storage | Durable evidence under `/mnt/Documents/astra68/` |

`astra-de25` is reachable from `beast`, not from the Mac. Two hops.

The Mac **cannot** build the kernel image or `test_process` (Mach-O section
attributes). It has `m68k-elf-gcc`, `mke2fs` and `lz4`, but **no `e2fsck` and no
`m68k-linux-gnu`**. When something does not build locally, that is expected —
go to `beast`, do not treat it as a project blocker.

## The boards

**DE25-Nano — the active target.** An Intel Agilex 5 SoC: four AArch64 cores
running Linux, with the graphics design in the fabric. Its JTAG/UART is on
`beast`; it is also a networked Linux host (`astra-de25`).

**The MC68040 is emulated by QEMU TCG on the DE25's Cortex-A76 core. It is not
in the FPGA fabric.** A board run is physical evidence for the complete host,
storage, graphics and SD path. Beast runs are development evidence only.

## The emulator

**`emu/qemu` — the Astra QEMU 9.2.4 fork. It is the only emulator.** It carries
the astra68 machine and the Vesta block and input models. Build with
`emu/qemu/build.sh {host|desktop|arty}`; `arty` cross-compiles for the board.

There is no alternative CPU or emulator implementation in the repository.

## Traps that have each cost real time

- **The board's shipped QEMU predates the block device model.** Symptom:
  `AstraHost runtime ... not present` and `0 granted capabilities` even with a
  drive attached. Check with
  `strings <qemu> | grep -c "Astra68 storage image"` — zero means too old.
  Rebuild with `emu/qemu/build.sh arty` on `beast`.
- **The initial user image has no software size quota.** Since boot ABI 0.7 it
  begins after the fixed kernel reservation and may grow to the next physical
  reserved aperture, or RAM end when no such aperture exists. Firmware reserves
  only the page-rounded image and returns all remaining pages to the allocator.
  Do not reintroduce a guessed maximum: the old 48 KiB and 256 KiB holes both
  turned incidental layout into policy and eventually blocked a valid build.
- **The board is BusyBox**: no `truncate`, `timeout`, `pkill`; `losetup` takes
  `-o OFS LOOPDEV FILE`. `/` is read-only, only `/data` is writable.
- **QEMU's cycle counter is the guest monotonic timebase**, not a count of
  physical MC68040 clocks. Use it for guest latency and deadline behavior;
  report physical DE25 throughput separately.
- `qemu-user` on `beast` is the **armhf** package and cannot execute on x86_64.
- **`pytest` is not installed on `beast`**, so `sw/boot`'s Python half only runs
  on the Mac.
- **`ASTRA_VFS_OPEN_CREATE` without `ASTRA_VFS_OPEN_TRUNCATE` used to
  truncate.** lwext4 has three mode strings and the protocol has four states,
  so `ext4_backend_open` now tries `"r+b"` and falls back to `"wb"` only on
  `ENOENT`. It surfaces as a file that is the right length for the last write
  and the wrong length for the file -- which reads like a short write and not
  like an open flag.
- **A storage image killed mid-run will not boot again until it is fsck'd.**
  No clean shutdown leaves a dirty ext4 journal; lwext4 replays it and returns
  bad bytes. It surfaces as `astra_launch:2: failed` on the next service read,
  supervisor exit `8`, and a kernel panic saying *initial user image exited* —
  nothing points at storage. The file on the volume is byte-identical; only the
  metadata is wrong. `e2fsck -fy` the sliced-out volume, or splice a fresh image.
- **`.tables` is a `NOLOAD` region and the ROM does not zero it.** `entry.S`
  clears it now, right after BSS, because every pool that moved out of BSS was
  written against BSS's zeros — POST leaves `0x5AA55A5C` there otherwise and
  the first pool validity check fails, which surfaces as `initial user image
  rejected, status 9` and points at nothing. Anything added to `KERNEL_TABLES`
  inherits that clear; a *new* NOLOAD region would not.
- **The kernel links no libgcc and no C library**, which is normal for a
  freestanding kernel and means it carries its own subset, the way Linux keeps
  `lib/string.c`, `lib/vsprintf.c` and `lib/div64.c`. Astra's is
  `kernel_bytes_*` for memory, `kernel_format`/`console_printf` for output, and
  `astra_divide_u64` for 64-bit division -- use them rather than the operator
  or the libc name. A variable 64-bit shift calls `__ashldi3`, a 64-bit divide
  calls `__udivdi3`, and GCC turns `= {0}` on a four-word array, and a loop
  that fills every word of one, into `memset`. All three link as undefined
  references, which reads like a missing linker script rather than an
  arithmetic choice.
- **One build at a time on `beast`, enforced.** Builds share `build/`
  directories (`sw/kernel/build` between host tests and the ROM, the ncurses
  tree, QEMU's work root), and two concurrent runs corrupt each other in ways
  that read as real failures. Every entry script in `~/astra-mg`, `~/ipc-prof.sh`
  and `~/f2s-*.sh` sources `~/astra-mg/build-lock.sh`; run anything ad hoc as
  `~/astra-mg/locked <command>`. A second build exits 75 with
  `ASTRA-BUILD-BUSY` and names the holder -- wait for it, never work around
  it. `verify-nopc.sh` keeps going after a failed step, so a failed verify is
  still running until its shell exits.
- **Display capture's status overflow bit is sticky.** It is the pixel
  FIFO's overflow flag, set from the first overflow until the pixel domain
  resets. A driver that treated it (or LAST_DROPPED) as the current capture's
  failure failed every capture after one frame was lost under render load,
  until reboot: remote desktop dropped with `capture Astra display:
  Input/output error`. Judge a capture by the counters CLEAR_COUNTERS zeroed
  for it; a lost frame is EAGAIN. Under heavy rendering most captures lose
  their frame (64-entry FIFO against DDR load).
- Stale objects are indistinguishable from kernel bugs. Exit status 127 from a
  user image means a stale object first, not a kernel fault. Never source-sync
  with plain `rsync -a`: use the checksum/non-mtime command in `AGENTS.md` so a
  changed source arrives newer than its remote object. Generated products stay
  under excluded `build/` directories and finished artifacts move separately.
- **Passing gates are not evidence that a new path is taken.** A change can
  leave all five green and still be inert. Perturb the thing deliberately --
  break the value, revert the fix -- and see the failure you expect, or you
  have measured nothing.
- **The ROM embeds the supervisor, and the supervisor links userspace static
  libraries (`libastravfs.a`, `libastra.a`, `libastrart.a`) it has no rule to
  rebuild.** `make -C sw/boot` before `make -C sw/userspace` relinks the ROM
  against the previous build's libraries: an NDK or VFS change (a manifest
  directive, say) reaches every program but the supervisor, and surfaces as
  an application that silently never launches. Build userspace first, as
  `emu/qemu/publish-de25-release.sh` does.
- **The emulator is not rebuilt by the gates.** After editing
  `emu/qemu/qemu-9.2/hw/m68k/astra68.c`, run `emu/qemu/build.sh host` or you
  are testing the previous binary.
- **`sw/kernel/build/` is shared by the host tests and the m68k build.** Build
  the ROM and then run `make test` in the same tree and the host binaries are
  linked from m68k objects: `./build/test_mmio: cannot execute binary file`.
  `make clean` between the two.
- **`make clean && make test` in `sw/userspace` does not work**: the test
  target does not build the runtime it links against, so it stops at
  `No rule to make target '../../runtime/build/m68k/crt0.o'`. Run `make -j8`
  first, then `make test`.
- **The machine's date comes from the host**, through a Vesta register the
  emulator fills from `QEMU_CLOCK_HOST`. There is no software clock and no
  synchronisation protocol here: fix the host's clock, and Astra's is fixed.
  `docs/TIME.md` is the whole chain. A machine whose `RTC_STATUS` is invalid
  says so at every layer rather than answering 1970. The **timezone** comes the
  same way -- an offset and a name, not a rule set, because the host already
  applied the rules. Under the emulator that is the QEMU process's own `TZ`:
  `TZ=America/New_York` and the machine is in EDT.
- **`emu/qemu/build.sh` puts its work under `/mnt/Documents` (the NAS) by
  default, and the NAS clock runs ahead of `beast`'s.** meson then refuses with
  `Clock skew detected ... 0.03s in the future` and the build never starts. Set
  `ASTRA_QEMU_WORK_ROOT=$HOME/.cache/astra68/qemu-9.2.4` to build on local disk.
- **Measure graphics throughput with `emu/qemu/bench-frame.py --fake-helper`.**
  Without a mailbox QEMU has no helper and completes display requests itself;
  the fake helper puts the real mailbox copy and wake in the path. The
  release kernel skips whole-pool audits (`sw/kernel/audit.h`); build with
  `make KERNEL_AUDIT=1` to run them on the machine.
- **On the DE25, an AXI error response to a CPU access panics Linux.** SLVERR
  or DECERR on the HPS-to-FPGA bridges arrives as an asynchronous SError. It
  surfaces as a board that stops answering, with nothing in the journal, and
  reads like an HPS or bridge hang. Since `68ce7bb2` the fabric slaves answer
  OKAY and count refusals in a fault record, but an older bitstream or an
  unmapped vendor address still panics. The board has `kernel.panic = 10`
  (`/etc/sysctl.d/90-astra-panic-reboot.conf`), so it reboots itself. There
  is no serial console on beast; a hang leaves no text.
- **F2SDRAM loses narrow read bursts longer than 128 beats.** The read never
  returns, and the render engine sits BUSY until the FPGA is reconfigured.
  `astra_render_host_reads` splits host bursts to 32 beats. Any new FPGA
  master to HPS DDR must do the same.
- **QEMU's renderer clips a blit the DE25 blitter refuses.** A BLIT whose
  source rectangle runs past its source surface draws fine under every
  QEMU gate and comes back `BAD_RANGE` (fault detail `0x00010001`) on the
  board, which the display reports as `display request refused by the
  device:2` and the client as a failed present. Find the command with
  `ASTRA_DISPLAY_STALL_DUMP=/data/stall` in a drop-in for `astra.service`;
  the dump is the refused batch. A window that left full screen once
  carried its old size this way and froze Doom's first frame.
- **Acknowledge a shared completion interrupt before scanning, never
  after.** QEMU's host-channel acknowledgement clears every channel's
  pending bit, so a completion published between a scan and a later ack
  was erased and its waiter slept until an unrelated interrupt. It
  surfaced as audio gaps (a stalled media service) about once a minute on
  the DE25, with the vCPU idle and every host command fast.
- **A kernel store into a copy-on-write page used to be refused.** After
  `fork()`, a syscall writing into a private page still shared with the
  parent (a `read()` or receive into a heap buffer the child had not touched)
  failed: `kernel_vm_private_commit_range` answered `NOT_OWNED` for a
  read-only mapping instead of breaking COW. It hid while freed heap pages
  were always decommitted (fresh mallocs were unmapped pages) and surfaced,
  once the heap retained them, as `test-terminal.py` hanging at `posix -R
  ... never answered with 'POSIX RAW PASS'` with the kernel idle. It reads
  like code depending on malloc returning zeros; zeroing in userspace "fixed"
  it only by breaking COW first.
- **The worker runs with interrupts enabled, and an interrupt can make a
  thread current.** The reaper's `kernel_vm_deactivate` compared URP with
  the dying space and then switched to the empty root; an interrupt between
  the two installed a woken thread's root, the switch overwrote it, and the
  worker resumed that thread with no switch. It surfaced (~1 boot in 500) as
  the supervisor faulting on its own code (`pc 0x00142D1A`, vector 2,
  `System degraded`) right after `remote-desktop` exited, and as
  `test-terminal.py` hanging forever in `input_events` -- the walk of the
  faulting page was valid; URP was the empty root. Any read-compare-switch
  of `current_user_root` outside an interrupts-off region has the same hole.
- **The loader reads only `DT_GNU_HASH`.** The compiler driver adds
  `--hash-style=gnu` to every link (`LINK_SPEC` in
  `toolchain/patches/gcc-16.2.0-astra.patch`, installed on `beast` from
  `~/astra-toolchain/build/gcc-68040` with `make all-gcc install-gcc`). A
  toolchain without it links SysV-only images, which the loader refuses as
  unsupported, so it fails to launch. Check
  with `m68k-astra-gcc -dumpspecs | grep hash-style`; after changing it,
  relink everything (move the `build/` trees aside) and regenerate images.
- **The profile plugin appends.** `bench-workloads.py OUT.aprof` adds its
  intervals to an existing file, and `astra-prof` then sums both runs. Use a
  fresh name or delete the file first.
- **QEMU stores `fmovem.l %fpcr/%fpsr/%fpiar` as FPIAR, FPSR, FPCR** from
  the lowest address. The kernel's FPU switch moves each control register
  with its own `fmove.l` so `KernelFpuContext.control` is FPCR, FPSR, FPIAR
  whatever the emulator does. A save/load pair in either order passes
  every round-trip test; it shows only when code writes one field (the
  signal handler's FPCR reset did). `docs/USERSPACE_FPU.md`, phase 1.
- **The kernel is float-free by a link-time check.**
  `tools/check_kernel_float_free.py` fails the kernel link on any FPU
  instruction outside `_kernel_restore_user_context` and
  `kernel_fpu_flush`, the only code that touches user FPU state.
- **The qualification kernel is a second ROM**, built with
  `make KERNEL_K1_QUALIFICATION=1` in `sw/boot`, with no debug surface and no
  initial user image. `emu/qemu/test-qualification.py` is its gate. It
  overwrites `sw/boot/build/astra_boot.bin`, so rebuild the normal ROM afterwards.

## Where to read next

| Question | File |
|---|---|
| Complete inventory of everything | `docs/INVENTORY.md` |
| Project-wide continuation map | `docs/CURRENT_STATE.md` |
| Storage / filesystem line of work | `docs/FILESYSTEM_CONCURRENCY.md`, `docs/FILESYSTEM_KIT.md` |
| Memory topology and budgets | `docs/MEMORY_BUDGET.md`, `docs/MEMORY_MAP.md` |
| Kernel design and active certification | `docs/KERNEL_ARCHITECTURE.md`, `docs/CURRENT_STATE.md` |
| ROM budget and memory layout | `docs/MEMORY_MAP.md`, `sw/include/astra/boot.h` |
| The wall clock, and what has a date | `docs/TIME.md` |
| Debugging a program on the machine | `docs/DEBUGGING.md` |
| FPGA timing closure | `fpga/de25/TIMING_CLOSURE.md` |

## Standing instructions

- **No throwaway code.** Build the real long-term piece even if incomplete.
- **Modern methods, sized for this machine.** An MC68040 with an MMU, 128 MB and
  one core. Take the *idea* a modern system expresses, not its implementation —
  and drop what does not apply, because no SMP deletes most of an allocator's
  complexity. Never the 1980s answer: no fixed partitions, no ceilings compiled
  into an image, no handle-and-lock discipline pushed onto every caller. Where
  the capability model can do better than Unix, do better. See
  `docs/MEMORY_BUDGET.md`.
- Profile everything; regressions must be visible.
- Broken tests are an emergency. Never commit with failing tests.
- Git holds source, authored docs and deterministic generators — never build
  products or captures. See `docs/ARTIFACT_POLICY.md`.
