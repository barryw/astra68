# Handover 2026-10-04: userspace is hard float; Doom measured on the DE25

Read `CLAUDE.md`, `AGENTS.md`, then this page. Detail lives in
`docs/USERSPACE_FPU.md` (design and every phase result). Earlier pages:
`HANDOVER_2026-10-04_FPU.md`, `HANDOVER_2026-10-03_BOARD.md`.

Everything is committed and pushed to `main`.

## Where things stand

- **Userspace is hard float.** The kernel keeps each thread's MC68040 FPU
  state; the compiler defaults to the FPU and tags every object's float
  ABI; libraries that return float/double got new majors (compiler 2,
  libc 3, cxx 2, lua 6, SDL2 3, SDL2_mixer 3).
- **QEMU** computes FS/FD arithmetic and exact conversions on the host
  FPU, bit-identical to floatx80 (`emu/qemu/test-host-float.sh`), and
  raises F-line for the instructions the 68040 lacks.
- **Board:** release `2764d8f6` is live. Doom with four TestDraw2
  windows, 30 s: frames 646/631 (soft float: 507/515), audio gaps 0/0
  (soft float: 540/545).
- **Toolchain on beast:** hard float, installed in `~/astra-toolchain/prefix`.
  The soft prefix is `~/astra-toolchain/backup-20261003-softfp`.
  `prefix-hardfp` is a symlink to `prefix` (the GCC build tree in
  `build/gcc-hardfp` records that path; leave it).

## Gates

- `~/astra-mg/verify-then-publish.sh` passes from fresh build trees
  (kernel and userspace tests, every gate, desktop, service policy, power,
  remote desktop, filesystem stress, NDK, SDL, display, ext4).
- FPU-specific: `emu/qemu/test-fpu.py QEMU ROM --image IMG` (per-thread
  state, fork, signals, fresh state, F-line, privilege),
  `emu/qemu/test-host-float.sh [PAIRS] [BOARD]`,
  `toolchain/test-gcc-driver.sh`, `tools/check_kernel_float_free.py` (runs
  at every kernel link).
- Publish: `emu/qemu/publish-de25-release.sh` on beast (under the lock).
  A/B on the board: `/data/ab-run.sh RELEASE` (two runs each).

## Traps learned this round (also in CLAUDE.md)

- Rebuilding GCC does not rebuild `libgcc_builtins_shared.a`,
  `libgcc_unwind_shared.a` or `libstdc++_shared.a`; rerun
  `tools/build-libgcc-shared-archives.sh` and
  `tools/build-libstdcxx-shared-archive.sh` after any toolchain rebuild.
- QEMU stores a multiple `fmovem.l` FPIAR-first; the kernel moves control
  registers singly.
- A profiling drop-in in `/run/systemd/system/astra.service.d/` makes the
  deployer wait forever (it waits for the release's own QEMU). Remove it
  and `systemctl daemon-reload` before publishing.
- QEMU's stack contents differ from the board's: an uninitialised read
  passed every gate and crashed the supervisor on the board
  (`bf1d9c97`). A board run is the evidence.

## Open items

1. **chocolate-quake port** -- next port, owner's choice.
   `docs/CHOCOLATE_QUAKE_PORT.md` is the plan (agent-drafted, upstream tag
   2.1.0, frame rates estimated). Phase 0: SDL config macros
   (`HAVE_STDLIB_H`, `HAVE_MATH_H`), `errno` in picolibc's `sys/errno.h`, an
   Astra CMake toolchain file and SDL config packages (DevilutionX needs
   them too). Owner decisions listed at the end of the plan: game data
   (shareware `pak0.pak` cannot ship in the image), music codecs, where
   `id1` lives, a manifest key for launch arguments (`-mixspeed 11025`),
   and whether a 68040 span-drawer patch is acceptable.
2. FPU follow-ups (`USERSPACE_FPU.md`): switch-cost measurement
   (`~/ipc-prof.sh`), `KERNEL_AUDIT` live-owner check, interrupt-load
   subtest in `test-fpu.py`, ELF scan for unimplemented FPU opcodes,
   check the `fmovem.l` order against the Motorola manual before userspace
   `fenv` relies on it.
3. QEMU: what remains per FPU instruction is helper-call overhead (~20
   cycles, ~9 helpers per audio sample); inline TCG would remove it.
4. Board: Doom profile on the hard-float release to see the new hot spots
   (`/data/prof/`, build the profiling QEMU with `build.sh de25-profile` --
   it must carry both FPU patches).
5. `third_party/odfs` is vendored for a future CDFS; adapter not started
   (`third_party/odfs/ASTRA_VENDOR.md`).
6. `docs/SYSTEM_LOG.md` is a design proposal from an earlier session, not
   built.
