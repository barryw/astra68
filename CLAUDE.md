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
- Stale objects are indistinguishable from kernel bugs. Exit status 127 from a
  user image means a stale object first, not a kernel fault.

## Where to read next

| Question | File |
|---|---|
| Complete inventory of everything | `docs/INVENTORY.md` |
| Project-wide continuation map | `docs/CURRENT_STATE.md` (mind the override) |
| Storage / filesystem line of work | `docs/HANDOVER-userspace-bringup.md` |
| **Current resume point** | `docs/HANDOVER-events.md` |
| Debug surface, and how the namespace got started | `docs/HANDOVER-debug-and-namespace.md` |
| Service protocols and thread stacks | `docs/HANDOVER-vfs-and-stacks.md` |
| ROM budget and memory layout | `docs/MEMORY_MAP.md`, `sw/include/astra/boot.h` |
| Debugging a program on the machine | `docs/DEBUGGING.md` |
| What is implemented vs planned | `docs/STATUS.md` |
| FPGA timing closure | `fpga/soc/oss_flow/TIMING_CLOSURE.md` |

## Standing instructions

- **No throwaway code.** Build the real long-term piece even if incomplete.
- Profile everything; regressions must be visible.
- Broken tests are an emergency. Never commit with failing tests.
- Git holds source, authored docs and deterministic generators — never build
  products or captures. See `docs/ARTIFACT_POLICY.md`.
