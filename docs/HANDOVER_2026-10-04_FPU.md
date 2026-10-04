# Handover 2026-10-04: scanout fixed, soft-float measured, hardware FPU next

Read `CLAUDE.md`, `AGENTS.md`, then this page. Previous:
`HANDOVER_2026-10-03_BOARD.md` (full detail of the scanout work, audio
notes, board tooling) and `docs/USERSPACE_FPU.md` (the FPU design).

**Nothing from these sessions is committed.** The working tree on the Mac
holds all of it (list below). Beast `~/Git/astra68` is a checksum rsync of
that tree.

## Owner decisions (2026-10-04)

- Measure before deciding anything about the hardware FPU.
- Bump the major version of every shared library whose ABI returns floats
  when the hard-float ABI lands (libc, compiler, cxx, lua, SDL2*); do not
  rely on a flag day alone.
- Keep the 68040 `Blit1to4` SDL blitter: any measured performance gain
  ships.
- Get started on the hardware FPU; Astra needs all the hardware help it
  can get.

## State of the board

- Bitstream: rbf `b61dbda8` (bundle `/var/lib/astra/boot-incoming-fifo`).
  Rollbacks in `/var/lib/astra/`: `boot-incoming-qos`, `-health`, `-scene`,
  `-arb`. Scanout under Doom and four TestDraw2 windows: 0 underruns.
- Software: release with the fast soft-float helpers was being published
  when this was written (`~/astra-mg/pub-sf.log` on beast; check
  `readlink /var/lib/astra/current` on the board). Previous releases:
  `1db703fa` (68040 blitter), `72daf6dc` (before this work).
- Board scripts in `/data`: `health.py SECONDS` (scanout counters 0x254-
  0x264), `fd-astat.py SECONDS` (audio daemon underruns/gaps),
  `ab-run.sh RELEASE` (switch release, restart, launch Doom, measure 30 s),
  `fd-chan.py` (left/right/same/opposite-phase tones), `peek.py`,
  `regs.py`, `scene.py`, `lat.py`, `arstall.py`, `busylat.py`,
  `doomcheck/drag.py X0 Y0 X1 Y1` (RFB drag), `prof/` (profiling QEMU,
  plugin, `ctl.py start LABEL|stop|status`).
- Profiling on the board: a runtime drop-in
  `/run/systemd/system/astra.service.d/profile.conf` sets
  `QEMU=/data/prof/qemu-system-m68k-astra` and adds the plugin. It lives in
  `/run` and is gone after a reboot; remove it and `systemctl daemon-reload`
  to stop profiling without rebooting. Build the profiling QEMU with
  `emu/qemu/build.sh de25-profile` on beast.

## Done this session (all uncommitted)

1. Scene line builder streams (`fpga/arty/graphics/astra_framebuffer_line_builder.sv`):
   11-span line 2,933 -> 1,259 cycles at latency 100. Bench gains
   `MODEL_LATENCY`, a board-shaped 11-span case and a 37-span ring/4 KiB
   containment case; `run_tests.sh` runs latency 100 and 300.
2. Beat FIFO 2 -> 8 bursts (4 KiB read credit): fixed the Doom bars
   (render commands raise fb round trips to ~300-500 cycles).
3. Scanout health registers, bank 9 0x254-0x264 (`astra_graphics_control.sv`,
   `astra_graphics_pipeline.sv`, Gray-code CDC for underruns, Linux header
   names). Benches check them.
4. Scanout read QoS (fb/scene ARQOS priority 3 through the bridges;
   `add_lpddr4b.tcl`, `patch_vendor_top.py`, contract test). Measured no
   effect; harmless. Arbitration shares from the previous session likewise.
5. `TIMING_CLOSURE.md` has an entry for the `rtl-scene` build only; the
   `rtl-health`, `rtl-qos`, `rtl-fifo` builds are described in
   `HANDOVER_2026-10-03_BOARD.md`. Add their entries before committing.
6. SDL 68040 `Blit1to4` (`sw/userspace/sdl2/prepare_source.py`), checked
   by `tests/video_probe.c`; a broken scale factor fails the gate.
7. Fast soft-float `__mulsf3`/`__divsf3` in the toolchain patch
   (`toolchain/patches/gcc-16.2.0-astra.patch`, `fpgnulib.c`
   ASTRA-MUL-DIV-SINGLE block; lb1sf68 bodies guarded by `__astra__`).
   MULU.L/DIVU.L, ~45 instructions a multiply against ~187. Bit-exact
   against IEEE over 11.1M cases: `toolchain/test-fpgnulib-muldiv.sh
   PATCHED/libgcc/config/m68k/fpgnulib.c`. Installed on beast
   (`~/astra-toolchain`, backup `backup-20261004-muldiv`); SDL_mixer and
   Chocolate Doom QEMU gates pass (`~/astra-mg/sf-gate.sh`).
8. `docs/USERSPACE_FPU.md`: hardware FPU design, not reviewed.

Gates: beast `run_tests.sh` passed for the FPGA work; the Mac's Icarus 14
rejects pre-existing use-before-declare in the render command processor
and its bench (beast's Icarus 12 is the gate).

## The measurement that matters

Board profile of Doom (release `1db703fa`, before the fast helpers):
74% of guest instructions in `compiler.library` soft-float, 54% of it the
old `__mulsf3` bit loop. The caller is audio: SDL_mixer
`_Eff_position_s16msb` float panning per sample and `SDL_MixAudioFormat`.
Doom ran at 16.6 frames/s with ~18 audio gaps/s, identical with and
without the blitter (A/B with `ab-run.sh`).

### Next, in order

1. When the fast-helper release is live, run `ab-run.sh` against
   `1db703fa` (twice each) and profile Doom on the board again. Record
   frames/s, gaps/s, and the soft-float share.
2. Phase 0 of `USERSPACE_FPU.md`: measure hardware FPU against the fast
   helpers **on the DE25**. The design's 2.7x came from stock QEMU 8.2 on
   x86 against the old lb1sf68 helpers, so it overstates the remaining gap.
   A guest microbenchmark (float multiply/add loops, an SDL_mixer-style
   panning loop) built both ways under the Astra QEMU on the board answers
   it.
3. Then decide; if go, phase 1 (kernel FPU context behind `test-fpu.py`,
   written first and failing on today's kernel) is useful regardless of
   the ABI flip.

## Answers given to the owner (2026-10-04)

**SIGFPE.** Astra has no synchronous fault-to-signal path today: POSIX
signals are delivered by libc (`sw/userspace/posix/src/signal.c`), and a
CPU exception kills the process with its vector recorded. SIGFPE therefore
means building that path: the kernel turns an exception into a resumable
user event for the faulting thread (save the full context including FPU
state, enter the handler on a frame, return through sigreturn). Once it
exists, SIGSEGV, SIGILL, SIGBUS and integer divide-by-zero (vector 5) come
with it, which is the real value. Floating-point exceptions only trap when
a program enables them in FPCR (IEEE default is non-stop, masked), so few
programs depend on SIGFPE itself. Estimate: a few days including gates;
best done after phase 1, since the signal frame must carry FPU state.

**FPGA floating point.** Not worth it, and there is little room. The
MC68040 is QEMU on the A76, not fabric: an FP operation sent to the FPGA
crosses the HPS-to-FPGA bridge, about a microsecond round trip, while the
A76 does the same operation in nanoseconds. The fabric is also at 87% of
ALMs and 89% of RAM blocks. The hardware that helps is the A76's own FPU:
QEMU's m68k FPU emulation computes in softfloat `floatx80`, so a fast path
using host float/double arithmetic for single and double precision
operations (when the rounding mode and precision match) is the
"hardware float" lever, alongside the guest hard-float ABI. Bulk work
already belongs off the guest: the audio host mixes and resamples on the
A76, and the render engine draws in fabric.

## Board and repo facts learned

- The capture taps `pipeline_rgb`, same as HDMI; captures drop frames under
  DDR load, so a clean capture does not prove a clean frame.
- Doom's fullscreen present is one source span per row; it commits about
  every 60 ms (~16.6/s), promotions land in vblank.
- `compiler.library` loads at `0x3ff00000` on the board; unresolved
  `0x3ff0xxxx` blocks in a profile are libgcc builtins.
- Chocolate Doom music is OPL in the guest; effects through SDL_mixer
  chunks with per-channel float panning.
- The TV sometimes drops centred audio (effects) while the Cam Link plays
  it; it varies by boot. `fd-chan.py` tells which channels a sink keeps.

## Continued (still uncommitted)

Steps 1 and 2 of "Next" are done; results in `docs/USERSPACE_FPU.md` §6
(phase 0 result, host-float fast path).

- A/B on the board: the fast helpers gave Doom +5% and **more** audio gaps
  (+14%). The vCPU is saturated; soft-float is 62% of guest instructions.
- `toolchain/bench-fpu-ops.{c,sh}`: phase-0 timing on the DE25 (user-mode
  `qemu-m68k` for AArch64, pinned to CPU 3; stop `astra.service` first).
  Unpinned runs land on an A55 and are ~3x slower.
- `emu/qemu/qemu-9.2/target-m68k-host-float.patch` (applied by
  `prepare-source.sh`): A76 float/double for the FS/FD arithmetic and exact
  conversions. Hard float vs the Astra helpers is now 4.7x on the panning
  loop (was 2.7x). Gate: `emu/qemu/test-host-float.sh 2000000
  root@192.168.1.52` -- bit-identical to the unpatched emulator on x86 and
  the A76. The overlay change rebuilds every QEMU profile next time; the
  board's shipped system QEMU does not have it yet (nothing uses it until
  the guest is hard float).
- Phase 1 gate landed first: `sw/userspace/commands/fpucheck` (a
  TEST_COMMAND) and `emu/qemu/test-fpu.py QEMU ROM --image IMG`. On today's
  kernel: `self`, `dirty`, `privilege` pass; `threads`, `processes`, `fork`,
  `signals`, `fresh` fail with register mismatches -- `fresh` shows the
  previous process's pattern, the cross-process leak. Interrupt-load
  subtest (5.2 item 3) not written yet.
- Board perf: `/usr/lib/linux-tools/5.15.0-191-generic/perf` works as root
  (`/usr/bin/perf` is a wrapper that refuses the 6.12 kernel). beast's perf
  is blocked (`perf_event_paranoid` 4).

Next: phase 1 kernel FPU context (`USERSPACE_FPU.md` §2) until
`test-fpu.py` passes, then the switch-cost measurement.

## Phase 1 kernel FPU context: done (uncommitted)

`USERSPACE_FPU.md` "Phase 1 result" has the detail. `test-fpu.py` passes;
perturbations fail where expected; kernel host suite passes (new FPU cases in
`test_thread` and `test_process`); all 16 `~/astra-mg/gates.sh` gates pass;
`tools/check_kernel_float_free.py` runs at every kernel link. QEMU stores
`fmovem.l` control registers FPIAR-first -- the kernel moves them singly.
Not done: switch-cost measurement, `KERNEL_AUDIT` owner check,
interrupt-load subtest.

`third_party/odfs` vendored for a future CDFS (`ASTRA_VENDOR.md`); not built.

Next for Doom: phase 2 (toolchain hard float) and phase 3 (flag day).

## Phase 3 flag day: done in QEMU, blocked on the board (2026-10-04)

- Committed on branch `fpu-hard-float` (`86a00ab1 feat!: userspace is hard
  float`). Toolchain installed on beast (soft prefix kept as
  `~/astra-toolchain/backup-20261003-softfp`; `prefix-hardfp` is now a
  symlink to `prefix` because the gcc build tree records that path).
  `verify-then-publish.sh` passes from fresh build trees.
- Published release `e7f15c72` (hard float, QEMU with host-float + F-line
  patches). The deploy hung until the leftover profiling drop-in
  `/run/systemd/system/astra.service.d/profile.conf` was removed: the
  deployer waits for the running QEMU to be the release's own.
- **Blocker:** on the board, launching Doom from the desktop
  (`/data/ab-run.sh`, double-click at 71,100) kills the supervisor: `***
  user fault: process 0x00000001 ... pc 0x00113084 address 0x00000048
  vector 2`, symbolized to `astra_vfs_close` (`sw/userspace/vfs/src/
  vfs_client.c:352-361`, the `call = begin(client); call->request...` path,
  so suspect a NULL call state from `call_acquire`). Every hard-float boot
  that launches Doom does it; booting alone does not (state is per release,
  so this is a fresh volume, not stale soft-float apps). Every QEMU gate
  passed, including Chocolate Doom, so reproduce the desktop double-click
  launch under QEMU first (test-desktop-apps.py launches apps differently).
  Disassemble 0x113084 in `sw/userspace/supervisor/build/m68k/
  astra_supervisor.elf` (the `.image.elf` printed nothing).
- The A/B so far: `635131e4` (soft) gen 495-500, gaps 538-550 per 30 s;
  `e7f15c72` measured nothing (supervisor dead). The board is back on
  `635131e4` (working Doom); switch with `/data/ab-run.sh RELEASE`.
- `docs/CHOCOLATE_QUAKE_PORT.md` (agent-drafted plan, memory corrected: the
  board runs `-m 512M`). `CLAUDE.md`'s stale "128 MB" fixed.
