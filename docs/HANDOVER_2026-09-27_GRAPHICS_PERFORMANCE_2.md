# Handover 2026-09-27 (2): graphics performance, second session

Read `CLAUDE.md`, `AGENTS.md`, then `docs/HANDOVER_2026-09-27_GRAPHICS_PERFORMANCE.md`
(the bar and the plan), then this page. Nothing below is committed.

## Measured on the board (1 TestDraw2, release `be7df004…`)

| Stage | TestDraw2 fps | TestDraw2 batch in helper | Compose batch (0 cmds) |
|---|---|---|---|
| Release `25727bf9` + override helper (last session) | 24 | 15–17 ms | 12 ms FPGA (15 BLITs) |
| `be7df004` (option B, completion wake) | 23.6 | copy 1.8 + FPGA 10.1–10.7 + 3.7 other | 6.3 ms |
| + WC arena driver (override helper `…wc`) | 28.4 | copy 0.3–0.46 + FPGA ~10 + 3.7 other | 1.4–2.1 ms |

Nothing on the host is saturated (QEMU vCPU 44–62 %, helper 12–37 %): the
pipeline is serial and latency-bound. Per frame on the board, the guest is
about 18 ms, the FPGA about 10 ms, the helper about 6 ms.

## Done this session

1. **Option B finished** (display service, GUI v16, `ASTRA_GUI_PRESENT_DISCARD`,
   three content banks, overlay drawn only on change): an SDL compose is 0
   GPU commands. All gates green. See the option-B agent notes in
   `docs/MANAGED_GRAPHICS.md` §5a and `docs/PRESENTATION.md`.
2. **Completion wake, display mailbox 1.6.** QEMU no longer polls the mailbox
   every 1 ms: the helper `FUTEX_WAKE`s `completion_sequence`, and a QEMU
   thread turns that into a main-loop event (`astra68.c`,
   `astra_display_completion_thread`). `emu/qemu/test-display-mailbox.py QEMU
   ROM IMAGE` now also runs a fake helper and requires a completion → next
   request gap under 500 µs (old QEMU: 1136 µs min; new: 1–2 µs; completions
   per 20 s 10.4 k → 22.9 k). Helper self-test covers the wake. Both
   perturbations (no wake / old poller) fail the gate.
3. **Write-combining arena on the DE25.** `fpga/de25/linux/astra_graphics_arena.c`
   exposes media RAM (`0x40000000`, 512 MiB) as `/dev/astra-graphics-arena`
   mapped Normal-NC; the DE25 helper maps the arena only through it
   (`ASTRA_GRAPHICS_ARENA_DEVICE`). `/dev/mem` gave Device-nGnRnE: ~18 MB/s.
   Module installed and loaded on the board (`extra/`, depmod,
   `/etc/modules-load.d/astra-graphics-arena.conf`). README updated.
4. **Completion records not read on success.** The helper compares
   `RENDER_COMMANDS_FAILED`/`COMPLETED` before and after; records are read
   only if a command failed or `ASTRA_DISPLAY_PROFILE_COMMANDS` is set.
   Host-tested; **not yet run on the board** (see below).
5. **Render engine RTL (perf3), integrated into the main tree** by the RTL
   agent: command prefetch (≤32/burst), 3-slot descriptor cache, posted
   completions, rewritten copy engine (8 reads/16 writes in flight), fills
   through the burst engine, 1 Bresenham step/clock. Sim: synthetic TestDraw2
   frame 1.61 M → 253 k cycles at L25, 3.35 M → 338 k at L200 (board latency
   ≈ L150). Full `run_tests.sh` green. Latent copy-engine abort/handshake bug
   fixed with a directed test. Descriptor-lifetime rule documented in
   `docs/GRAPHICS_ARCHITECTURE.md` §9. **DE25 route closed first try**
   (render clock +0.361 ns, 1,048 ALMs free); artifacts in
   `beast:~/astra-mg/rtl-main/build/de25-render-perf3/astra-shell/output_files/`
   (`golden_top_boot.core.rbf` `4983ff7f…`, `astra68.hps.jic` `693346c3…`),
   recorded in `fpga/de25/TIMING_CLOSURE.md` as not deployed, not certified.
   Build with `DISPLAY` unset (qsys-script dies on a forwarded X display).
6. **Guest profiling on beast.** `emu/qemu/build.sh host-profile` builds the
   x86 QEMU with the `astra_profile` plugin; `tools/astra-prof record/control/
   report` then work without the board. Steady-state TestDraw2 script:
   `beast:~/astra-mg/prof-draw2.py QEMU ROM IMAGE OUT.aprof [warm] [span]`
   (env `PROBES=",probe=0xADDR"` records callers).
7. **Soft-float conversions.** `__floatsisf`/`__floatunsisf`/`__fixsfsi` went
   through double (m68k `fpgnulib.c`), which was 21 % of all guest
   instructions under TestDraw2 (SDL's int API converts every coordinate to
   float and back). New single-precision versions in
   `toolchain/patches/gcc-16.2.0-astra.patch`, exhaustively checked by
   `toolchain/test-fpgnulib-conversions.sh FPGNULIB.C` (all 2³² inputs,
   perturbations fail). Installed on beast by editing
   `~/astra-toolchain/src/gcc-16.2.0/libgcc/config/m68k/fpgnulib.c` and running
   `tools/build-libgcc-shared-archives.sh build/gcc/m68k-astra/libgcc`
   (backup: `~/astra-toolchain/backup-20260927-libgcc-shared/`). Only the
   shared archives were reinstalled; static `libgcc.a` (from `build/gcc-68040`)
   still has the old routines. Compiler-runtime share 21 % → 6.6 %, ~25 % more
   TestDraw2 work per guest second.
8. **Power-cut gate** (`emu/qemu/test-storage-power-cuts.py`): every wait is
   bounded (600 s) and the gate installs the `posix` test command into its own
   template image. The old run hung 80 min because the probe command was not
   in the image and the waits were unbounded. Build the command first:
   `make -C sw/userspace/commands build/m68k/posix`.
   **Root cause of the "ABSENT" failure: the probe wrote to hostfs.** Under
   the emulator `/work` is served by hostfs (`astra_image.py:161` manifest
   `serves WORK:rw`; `astra_assign_bind` replaces the ext4 binding), and each
   QEMU run has its own `ASTRA_HOSTFS_ROOT`. The probe now writes
   `/home/durability-cut.txt` (ext4) and the gate fails if the payload never
   reaches the block image. ext4 fsync is durable, name included (verified).
   Sweep: synced=10 total=21, cuts 1-16 PASS. **Open:** cut 17 fails
   intermittently (2 of 4) on recovery boot with initial-image status 0x22
   (a required service's child exited during startup); the same image boots
   by hand, so it looks like a boot race. Also open: should WORK be hostfs at
   all, and `ls -l /` shows `work -> /system` wrongly
   (`filesystem_library.c:28-57`).

## Blocked on the user (board writes)

The permission classifier stopped remote writes to the board after the arena
module install. Still to do on `astra-de25`:
- Copy `beast:~/astra-mg/wake/build/de25-graphics/linux/astra-terminal-display`
  (sha256 `7e3bc5c3…`, counter-check change) to `/data/cert/…wc`, restart with
  a fresh storage image, measure. Or publish a release from the current tree.
- Install the **posted-writes** FPGA (supersedes perf3):
  `beast:~/astra-mg/rtl-main2/build/de25-render-posted/astra-shell/output_files/`
  (`astra68.hps.jic` `4cee9f43…`, `golden_top_boot.core.rbf` `e859b476…`;
  render clock +0.354 ns, 1,189 ALMs free; sim TestDraw2 frame ~1.5 ms at any
  latency). Boot bundle procedure in
  `docs/HANDOVER_2026-09-26_SDL_HARDWARE.md` §Install), certify with
  `astra-render-certify`, then measure. Expected: FPGA per TestDraw2 frame
  ~10 ms → ~2 ms.
- Runtime overrides currently active (systemd manager env, lost on reboot):
  `ASTRA_TERMINAL_DISPLAY=/data/cert/astra-terminal-display.wc`,
  `ASTRA_DISPLAY_PROFILE=1`.

## Where the guest goes (TestDraw2, after the conversion fix)

display.elf 21 %, SDL2 20 %, kernel 15 %, runtime.library (memset/memcpy)
14 %, system.library (draw list) 11 %, compiler.library 7 %. That is about
2,000 guest instructions per primitive. The bar (10 × TestDraw2 at 60 fps on a
~70 MHz 040) needs roughly 150: per-primitive work must go, not be tuned.
Next: batch primitives end to end (SDL point/rect arrays → one draw-list op →
one validated array → one multi-rect FILL command; the RTL agent is estimating
that opcode). TRIANGLES is not the vehicle: 4–10× slower than FILL per quad
(serialized vertex fetch).

## Known hazards (pre-existing, not fixed)

- Display allocator frees a resized/closed window's extents immediately while
  the on-screen scene may still read them (one-frame glitch). Needs epoch-
  retired extents (free after two committed composes).
- Menu overlay surface is single-banked; a hover redraw writes it on screen.

## In flight

- Posted render writes: integrated in the main tree, suite + ordering bench
  green, routed (`TIMING_CLOSURE.md` 2026-09-27 entry). FILL_RECTS (multi-rect
  fill) estimated at ~35–40 cycles/rect, 150–250 ALMs; awaiting go-ahead.
- Full verify on beast with the rebuilt toolchain: `/tmp/v-nopc3.log`.

---

## Session 3 (2026-09-27): deployed on the board

**On astra-de25 now:** FPGA `rtl-main6` (rbf `819ddadb…`; `verify_running_shell`
PASS; `astra-render-certify` 9/9 PASS) + release `7525796b…` (stage 8, no
runtime overrides). WC arena module installed and loaded at boot.

| Board measurement | Session start | Now |
|---|---|---|
| 1 TestDraw2 | 23.6 fps | **60.0 fps** (vsync-locked; 120 batches/s) |
| TestDraw2 batch in helper | 16 ms | 2.7 ms (FPGA 2.4 ms) before batching; less now |
| 10 TestDraw2, aggregate | 43 frames/s | 54.5 frames/s (5.5 each), QEMU vCPU 71 % |

What landed (all uncommitted, all gates green: `/tmp/v-nopc5.log` on beast):
- RTL (fpga/arty/graphics, TIMING_CLOSURE.md entries): posted pixel writes,
  FILL_RECTS (opcode 4, blend option bit 1 = pipelined DSP source-over),
  LINES (opcode 262), boot-text overlay reclaim (−2,280 ALMs), overlap-aware
  blend drain. Now 44,558 / 46,800 ALMs, 4,666 / 4,680 LABs: **the next FPGA
  feature must reclaim area first** (texture steppers are the reserve).
- Software: ADLT 1.5 (FILL_RECTS, LINES), NDK `astra_draw_rectangles` /
  `astra_draw_lines` (system.library 2.6), render_builder lowering, SDL
  renderer merges consecutive same-paint SDL calls into one array. Guest
  instructions per TestDraw2 frame 1,082k → 648k.
- Power-cut gate: probe on `/home` (ext4; `/work` is hostfs), per-run
  classification (SYNCED seen ⇒ EXACT owed), skips for unreached cuts, idle
  settle 60 s, ring decoded on failure. 8 recent full sweeps: 7 PASS; the
  eighth was the pre-fix nondeterminism. Consider adding it to verify.
- Diagnostics: supervisor logs `launch-failed <path> status=`; kernel dumps
  the trace tail when the initial image exits; loader logs the real cause of
  a dependency failure (`LOADER_REFUSED_*`).
- `/work` and `/cwd` served by hostfs now list as mounts, not `-> /system`.

Open:
1. **Transport is single-slot and serial** (guest submit → QEMU → helper →
   FPGA → completion); 10 windows are latency-bound (vCPU 71 %). Next: a
   multi-slot mailbox/ring so several batches are in flight, and no 100 KiB
   minimum copy per batch.
2. Guest per frame: SDL2 224k and kernel/other 227k instructions remain
   (upstream SDL per-call cost; syscalls per frame). Profile with
   `emu/qemu/build.sh host-profile` + `beast:~/astra-mg/prof-draw2.py`.
3. Rare boot flake (seen twice): desktop dies loading `pcm.library.2` right
   after `media` fails; now self-describing — next occurrence names the cause.
4. Display allocator frees extents still on screen (one-frame glitch);
   menu overlay single-banked.
5. Static `libgcc.a` still has the old double-routed conversions.

---

## Session 4 (2026-09-27): peak throughput, bottom tier first

Direction from the user: not "10 × TestDraw2 at 60", but the ceiling of the
graphics path before the CPU is taxed, fixed in the lowest tier (kernel,
emulator, NDK, display service, SDL), never per application. Remove every
unnecessary latency.

**Benchmark.** `sw/userspace/sdl2/tests/frame_bench.c` (`SDLFrameBench`,
`make -C sw/userspace/sdl2 frame-bench-bundle`) is the software-renderer path
games take: a window-sized streaming texture, `SDL_UpdateTexture` +
`SDL_RenderCopy` + present every frame, no vsync, pre-drawn frames.
`emu/qemu/bench-frame.py QEMU ROM IMAGE --fake-helper [--profile X.aprof]
[--probe ADDR --probe-register d0]` boots it (image: `astra_image.py
--test-app build/SDLFrameBench.app`) and prints `SDL_FRAME_BENCH fps=…`.
Use `--fake-helper`: it serves the real mailbox with the display gate's
stand-in helper (transport cost, no FPGA time).

| 640x480 ARGB8888 streaming, beast | fps |
|---|---|
| session start, host-only QEMU (1 ms per request) | 200 |
| session start, `--fake-helper` | 519 |
| no 1 ms stall, kernel audits off hot path | 546 |
| zero-copy upload (attachment) | 720 |
| + `pthread_self` cached, SDL monotonic clock | **838** |

Landed (uncommitted; full verify green, `/tmp/v-s4.log` + reruns):
1. **Emulator latency.** Host-only display requests completed after a fixed
   1 ms and block requests after 20 µs; both now complete from a timer armed
   for *now* (still asynchronous to the submitting store). Non-Linux mailbox
   polls at 50 µs. Mailbox protocol collapsed to 1.6 only.
2. **Board helper waits** (`astra_graphics_poll_pause`): every FPGA wait spins
   (`yield`) for 2 ms before 50 µs sleeps; timer slack 1 ns. Host-tested only:
   **not yet on the board.**
3. **Kernel audits** (`sw/kernel/audit.h`, `KERNEL_AUDIT`): whole-pool
   validators ran after every IRQ/device/CLOSE syscall and on every deadline
   expiry (the deadline check is O(n²)). Host suites keep them; the 68040
   image checks O(1) health; `make KERNEL_AUDIT=1` restores them on the
   machine. `kernel_handle_available` is a SWAR popcount.
4. **Zero-copy texture upload**: render-batch attachment
   (`docs/MANAGED_GRAPHICS.md`, "No service copy"). Vesta `DISPLAY_REQ_ATTACH`
   at 0x728, `ASTRA_DISPLAY_HOST_CAP_ATTACHMENT`, request ABI 40 bytes.
5. `pthread_self` cached in libc TLS (every mutex lock/unlock trapped twice);
   SDL built with `HAVE_CLOCK_GETTIME` (its counters read the monotonic
   clock, not `gettimeofday`).
6. `test-desktop-apps.py` read completions before submissions; at full speed
   that reads as "fell behind". Fixed read order.

Where a streaming frame goes now (guest, `--fake-helper` profile): one
MC68040 copy of the caller's pixels into staging (`memcpy`, ~48% of guest
instructions) and the kernel (~40%, ~58 syscalls per frame, ~3,000
instructions each). Syscall mix per frame: RECEIVE_TRY 8, SEND_TRY 7,
CLOSE 7, IRQ_READ 6, CLOCK_REALTIME 5 (before the SDL fix), WAIT 6,
IRQ_ACK 3, PORT_CREATE 3, DISPLAY_SUBMIT/COLLECT 3+3.

Next, in value order (both are kernel design work; not started):
1. **Kernel loan of caller pages** (read-only area over a user range, held
   for one transfer) so `astra_surface_write` / `SDL_UpdateTexture` copy
   nothing on the 68040. Chocolate Doom and DevilutionX use UpdateTexture.
2. **Kernel call/reply IPC** (seL4-style CALL + REPLY_RECV with a per-thread
   reply endpoint). Today a client call is ~6 traps (PORT_CREATE, SEND,
   RECEIVE_TRY, WAIT_ONE, RECEIVE_TRY, CLOSE) and the service ~4.
3. Display service IRQ drain after every collect (IRQ_READ ×2 + ACK).
4. A user-readable monotonic timebase (clock reads are traps).
5. Board: deploy helper + QEMU from this tree, measure SDLFrameBench and
   TestDraw2 on the DE25.
