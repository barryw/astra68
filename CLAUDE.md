# Astra 68 — read this first, every session

This file is loaded automatically. `AGENTS.md` is **not** — it is for Codex-style
agents and parts of it are out of date. Where the two disagree, this file and
`docs/CURRENT_STATE.md` win.

`docs/INVENTORY.md` is the complete audit: every machine, board, subsystem,
toolchain and build command. Read it when you need detail. This page is the
orientation you need before touching anything.

---

## The machines

| Host | Address | What it is | Use it for |
|---|---|---|---|
| Mac | local | Apple Silicon, this session's cwd `/Users/barry/Git/astra68` | Editing, userspace + ELF work, host tests |
| `beast` | 192.168.1.3 | 32 core, 61 GB, Ubuntu | **Everything that must build or run for real**: kernel, boot ROM, m68k gates, QEMU, analyzer |
| `nuc` | 192.168.1.2 | 8 core, 31 GB, Ubuntu | ULX3S work (historical), FPGA tooling |
| `astra-arty` | 192.168.1.188 | **The board.** Arty Z7-20, ARMv7, Linux 6.6-xilinx | Running Astra on hardware |
| `nas.lan` | 192.168.1.5 | Storage | Durable evidence under `/mnt/Documents/astra68/` |

`astra-arty` is reachable **as root from `beast`**, not from the Mac. Two hops.

The Mac **cannot** build the kernel image or `test_process` (Mach-O section
attributes). It has `m68k-elf-gcc`, `mke2fs` and `lz4`, but **no `e2fsck` and no
`m68k-linux-gnu`**. When something does not build locally, that is expected —
go to `beast`, do not treat it as a project blocker.

## The boards

**Arty Z7-20 — the active target.** A Xilinx **Zynq**: two ARM Cortex-A9 cores
running Linux, with the graphics design in the PL. Its JTAG/UART is on `beast`;
it is also a networked Linux host (`astra-arty`).

**The MC68030 is emulated by QEMU on those ARM cores. It is not in the FPGA
fabric.** A board run is real hardware for storage, graphics and the SD path,
but the CPU is still TCG — 68030 timing is no more real there than on `beast`.
Accepted CPU baseline is roughly 30 MHz equivalent.

**ULX3S — historical.** ECP5, attached to `nuc`. The ULX3S + TG68K FPGA CPU line
is superseded; see the override below.

## Active vs historical — the thing that causes the most confusion

`docs/CURRENT_STATE.md` carries an **Active Arty migration override
(2026-07-30)**. It states that any claim of ULX3S, 32 MiB SDRAM, 16 sprites,
720x480, the FPGA TG68K core, NUC attachment, or AstraHost/ESP transport as the
*active production boundary* is **historical** unless the override repeats it.
Much of that document sits below the override and reads as current. It is not.

Currently true:

- Target is the Arty Z7-20; the m68k runs under the **Astra QEMU backend**.
- **Musashi and the retained TG68K RTL core are conformance oracles**, not
  emulators. `conformance/` runs the same architectural case against
  `musashi-68030` and `rtl-tg68k030-mmu2` and compares them. Deleting either
  removes the CPU verification oracle.
- The FPGA MC68030 is **not** in the active Arty PL budget.

## The emulator

**`emu/qemu` — the Astra QEMU 9.2.4 fork. It is the only emulator.** It carries
the astra68 machine and the Vesta block and input models. Build with
`emu/qemu/build.sh {host|desktop|arty}`; `arty` cross-compiles for the board.

AstraVM, a second Rust machine on Musashi, was removed on 2026-08-05. Musashi
itself stays, as a conformance oracle only — see above.

## Traps that have each cost real time

- **The board's shipped QEMU predates the block device model.** Symptom:
  `AstraHost runtime ... not present` and `0 granted capabilities` even with a
  drive attached. Check with
  `strings <qemu> | grep -c "Astra68 storage image"` — zero means too old.
  Rebuild with `emu/qemu/build.sh arty` on `beast`.
- **The initial user image ceiling is `ASTRA_USER_IMAGE_MAX_SIZE`**, the hole
  between `0x02004000` and the kernel. It was 48 KiB and is **256 KiB since
  boot ABI 0.4**; overrunning it is `POST FAIL: user image exceeds its
  reservation`, which is a RAM-layout refusal and not a ROM budget one.
  Firmware reserves only the pages the image fills, so the ceiling costs
  nothing unused. Anything hardcoding the old layout is a latent bug — seven
  kernel tests did.
- **The board is BusyBox**: no `truncate`, `timeout`, `pkill`; `losetup` takes
  `-o OFS LOOPDEV FILE`. `/` is read-only, only `/data` is writable.
- **QEMU's cycle counter is TCG bookkeeping**, not 68030 time. Any `N cycles`
  from emulation is meaningless — and that applies on the board too.
- `qemu-user` on `beast` is the **armhf** package and cannot execute on x86_64.
- The astra68 QEMU fork **cannot build a `m68k-linux-user` target** without
  guarding `INSN(pmmu030, ...)` in `target/m68k/translate.c`.
- **`pytest` is not installed on `beast`**, so `sw/boot`'s Python half only runs
  on the Mac.
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
- Stale objects are indistinguishable from kernel bugs. Exit status 127 from a
  user image means a stale object first, not a kernel fault. **`rsync -a`
  preserves mtimes**, so restoring a file on `beast` can hand `make` a source
  older than an object built from something else, and it keeps the stale
  object. `make clean` before believing a result you have gone back and forth
  on.
- **Passing gates are not evidence that a new path is taken.** A change can
  leave all five green and still be inert. Perturb the thing deliberately --
  break the value, revert the fix -- and see the failure you expect, or you
  have measured nothing.
- **The emulator is not rebuilt by the gates.** After editing
  `emu/qemu/qemu-9.2/hw/m68k/astra68.c`, run `emu/qemu/build.sh host` or you
  are testing the previous binary.
- **`sw/kernel/build/` is shared by the host tests and the m68k build.** Build
  the ROM and then run `make test` in the same tree and the host binaries are
  linked from m68k objects: `./build/test_mmio: cannot execute binary file`.
  `make clean` between the two.
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
- **The qualification kernel is a second ROM**, built with
  `make KERNEL_K1_QUALIFICATION=1` in `sw/boot`, with no debug surface and no
  initial user image. `emu/qemu/test-qualification.py` is its gate. It
  overwrites `sw/boot/astra_boot.bin`, so rebuild the normal ROM afterwards.

## Where to read next

| Question | File |
|---|---|
| Complete inventory of everything | `docs/INVENTORY.md` |
| Project-wide continuation map | `docs/CURRENT_STATE.md` (mind the override) |
| Storage / filesystem line of work | `docs/HANDOVER-userspace-bringup.md` |
| **Current resume point** | `docs/HANDOVER-boards-and-usb.md` — the qualification gate is green (USB included); storage/input provocation and the DE25 Nano are what is left |
| The memory work in full, with its numbers | `docs/HANDOVER-memory-and-modernity.md` — §5 is done; §6.2 onward is the record |
| Boot fix, the shell gate, commands and the POSIX half | `docs/HANDOVER-boot-and-shell-gate.md` — every gate green |
| The libc, the commands, and the kernel limits | `docs/HANDOVER-libc-and-limits.md` — its §10.1 and §10.2 are done |
| Compositor and launch latency | `docs/HANDOVER-launch-latency.md` — the blitter's `arlen` is the item left |
| Storage/VFS resume point before that | `docs/HANDOVER-union-assigns.md` |
| Debug surface, and how the namespace got started | `docs/HANDOVER-debug-and-namespace.md` |
| Service protocols and thread stacks | `docs/HANDOVER-vfs-and-stacks.md` |
| ROM budget and memory layout | `docs/MEMORY_MAP.md`, `sw/include/astra/boot.h` |
| The wall clock, and what has a date | `docs/TIME.md` |
| Debugging a program on the machine | `docs/DEBUGGING.md` |
| What is implemented vs planned | `docs/STATUS.md` |
| FPGA timing closure | `fpga/soc/oss_flow/TIMING_CLOSURE.md` |

## Standing instructions

- **No throwaway code.** Build the real long-term piece even if incomplete.
- **Modern methods, sized for this machine.** A 68030 with an MMU, 128 MB and
  one core. Take the *idea* a modern system expresses, not its implementation —
  and drop what does not apply, because no SMP deletes most of an allocator's
  complexity. Never the 1980s answer: no fixed partitions, no ceilings compiled
  into an image, no handle-and-lock discipline pushed onto every caller. Where
  the capability model can do better than Unix, do better. See
  `docs/HANDOVER-memory-and-modernity.md` §1.
- Profile everything; regressions must be visible.
- Broken tests are an emergency. Never commit with failing tests.
- Git holds source, authored docs and deterministic generators — never build
  products or captures. See `docs/ARTIFACT_POLICY.md`.
