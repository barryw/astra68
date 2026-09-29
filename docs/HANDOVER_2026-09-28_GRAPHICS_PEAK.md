# Handover 2026-09-28: graphics peak throughput (session 4–5)

Read `CLAUDE.md`, `AGENTS.md`, then
`docs/HANDOVER_2026-09-27_GRAPHICS_PERFORMANCE_2.md` (its "Session 4"
section), then this page. **Nothing is committed** — every change below is
in the Mac working tree only.

## Direction (user)

- Peak graphics throughput before the CPU is taxed; games (Doom, DevilutionX,
  SDL ports) are the target.
- Optimise the lowest tier only: kernel, emulator, NDK, display service,
  graphics libraries, SDL — never individual programs.
- Remove every unnecessary latency. No band-aids (HPS DMA upload was rejected:
  it would take the remote-desktop capture controller).
- FPGA refactoring to make room is approved ("juice is worth the squeeze").
- Standing permission to deploy anything to the board.
- **Watch the board and report problems immediately** (a monitor on board
  health; see "Board watch" below). Do not sit on a wedge.

## Board state at handover

- Power-cycled by the user at handover. It boots the **combined F2SDRAM
  bitstream** (rbf `3cb06755…`, boot script `c63d7d25…`) with the new
  `astra_graphics_arena.ko` (`36104eb9…`, provides `/dev/astra-host-arena`,
  8 MiB CMA at phys `0xbcd00000`) and runtime release **`8533ffa1…`**
  (mailbox 1.6). This combination reached stage 8 cleanly before; it is safe
  **as long as nothing programs render register 0x248** (the host aperture).
- Rollback to the certified pixel-stream bitstream f14ab71c: copy rbf +
  boot.scr.uimg from board `/var/lib/astra/boot-rollback-f14ab71c-20260928/`
  to mmcblk0p1, reinstall the old .ko from there, depmod.
- Serial console capture (receive-only) on beast: `/dev/ttyUSB0`
  (`…DE25-Nano…-if02-port0`) and `/dev/ttyUSB1`, 115200:
  `stty -F /dev/ttyUSB0 115200 raw -echo -hupcl clocal; timeout 1800 cat /dev/ttyUSB0 > /tmp/de25-console-if02.log &`
- **Never read or write FPGA registers via /dev/mem that the installed
  bitstream does not implement** — reads of 0x248 (absent) and the Copper
  window 0x20104000 hung the HPS today. HPS hard-IP registers (reset manager,
  NoC) are safe to read.

## Measured (board, SDLFrameBench 640x480 streaming, desktop icon)

| Mode | session start | pixel-stream BLIT | + copy engine (8533ffa1) |
|---|---|---|---|
| ARGB8888 + blend | 3 fps | 28 | **32** |
| ARGB8888 opaque | 7 | 29 | **33** |
| RGB565 | 40 | 43 | **46-47** |

Per ARGB frame now: SDL upload ≈19 ms, of which the helper's bridge copy
(journal `render profile copy_us=`) ≈9.3 ms for the 1.33 MB batch and FPGA
≈2.5 ms; present ≈10-12 ms. TestDraw2: 58 fps (1 window), 54 frames/s total
(10 windows). Beast `bench-frame.py --fake-helper`: ~1,150 fps ARGB, ~1,290
RGB565 (was 519 at session start).

## What landed (all uncommitted, gates green at each step)

Software (session 4, see previous handover for detail): SDLFrameBench +
`emu/qemu/bench-frame.py` (`--fake-helper`, `--profile`, `--probe`); QEMU
1 ms / 20 µs completion stalls removed; helper `astra_graphics_poll_pause`
(spin 2 ms, then 50 µs sleeps, timer slack 1 ns); kernel audits off hot path
(`sw/kernel/audit.h`, `make KERNEL_AUDIT=1`); render-batch **attachment**
(zero service copy, `DISPLAY_REQ_ATTACH` 0x728); `pthread_self` TLS cache;
SDL `HAVE_CLOCK_GETTIME`; `SDLFrameBench` shipped in `/apps` and cycles
modes argb-blend/argb/rgb565.

Session 5:
1. **Pixel-stream BLIT** (RTL, `astra_render_copy_burst.sv` pixel mode):
   converting/blending unscaled BLITs at ~1.04 cycles/px (was 25-170).
   Certified on board (f14ab71c). `docs/GRAPHICS_ARCHITECTURE.md` §9
   "Pixel-stream BLIT", TIMING_CLOSURE top entries.
2. **Copy engine**: `ASTRA_SYSCALL_AREA_COPY_IN` (101, ABI 0x00010039),
   `AstraAreaCopy` (`sw/include/astra/syscall.h`), kernel `area_copy_in` in
   `process.c` builds an `AstraCopyList` (`sw/include/astra/copy_engine.h`)
   that QEMU runs on the host (Vesta 0x72C COPY_ID, 0x730 COPY_LIST, 0x734
   COPY_STATUS; qom `astra-copy-lists`). `astra_surface_write` uses it: no
   68040 memcpy for uploads. Deployed in 8533ffa1.
3. **Bridge investigation** (TIMING_CLOSURE entry, `fpga/de25/pmon_capture.*`,
   `fpga/de25/linux/astra_arena_bandwidth.c`): the Agilex 5 HPS issues **one
   outstanding CPU write for the whole cluster** → CPU copies into Media RAM
   cap at ~133 MB/s (fabric answers in 24 cycles). Not fixable in fabric.
4. **Scene-commit wedge** found and fixed: `astra_line_scheduler.sv` cleared
   `line_prepare_valid` on `commit_quiesce` while in SCHED_PREPARE → parked
   forever → `commit_safe=0` → commit deferred every frame; pending state
   persists until power-on. One-line fix + `tb_astra_line_scheduler` case
   (fails reverted). Helper drains a stuck pending commit at start (≤1 s),
   else reports "scene commit held by hardware; power-cycle required" once.
   Hotfix-only fallback bitstream: beast `~/astra-mg/rtl-hotfix/build/de25-hotfix`
   rbf `c1e9f2e7…` (run_tests green, route exit 0, slack not re-checked).
5. **F2SDRAM host-read path** (the approved "option 2", combined build):
   render reads in the batch window go to HPS DDR via a new read master;
   mailbox **1.7** splits header (file) and payload (`/dev/astra-host-arena`),
   QEMU writes the batch straight into FPGA-readable memory, helper copies
   nothing on DE25 and programs RENDER_HOST_APERTURE_BASE (0x248). RTL
   `astra_render_host_reads.sv`; platform: subsys_debug + ACE5-Lite
   translator + FPGA2HPS removed (−4,239 ALMs → 40,293/46,800), F2SDRAM
   read acceptance 1→8. Route closes (worst setup +0.012 ns HDMI pixel),
   artifacts beast `~/astra-mg/rtl-f2s/build/de25-f2s/…`. run_tests green.
   The 1.7 runtime (QEMU+helper) passed the **full verify** on beast
   `~/Git/astra68` (`/tmp/v-s5.log` READY-TO-PUBLISH) but is **HELD**.

## Blocker: first F2SDRAM read hangs the HPS

`astra-host-read-bench` programmed 0x248 = 0xbcd00000 and BLITted from the
host window; the HPS stopped answering within ~90 s. Research (cited in the
agent's notes): the F2SDRAM address is the HPS physical address (Altera
agilex5-demo-hps2fpga-interfaces doc 09), the ATF DDR firewall admits
0x80100000..end non-secure — so the address was right. Leading suspect:
**F2SDRAM bridge still in reset / idle-requested**: `bridge enable` releases
BRGMODRST bit 3 and the sideband idle request only through the ATF SMC path,
and the board still runs the **old QSPI FSBL/handoff (JIC not programmed)**
built for the previous HPS configuration (which had FPGA2HPS). Alternative:
an AXI protocol issue in the new master.

Next, in order:
1. Board up (stage 8, old release). Read-only HPS hard-IP registers, each in
   its own process: BRGMODRST `0x10D1102C`, F2SDRAM sideband flags
   `0x18001014` / `0x18001058`, firewall `0x18000C00..0x1C`. Save
   `/data/cert/host-read-bench-f2s.log` and dmesg.
2. Decide: program the matching JIC (`astra68.hps.jic` `e1237549…`) so the
   FSBL/handoff matches the new HPS config, and/or ensure F2SDRAM0 is out of
   reset and not idle-requested (U-Boot `bridge enable` / ATF SMC) — cite the
   TRM/ATF source for whatever is changed.
3. Re-run `astra-host-read-bench` (serial capture running). Only then publish
   the 1.7 runtime (`emu/qemu/publish-de25-release.sh` from beast
   `~/Git/astra68`, already synced and verified), certify
   (`astra-render-certify.g`, POST, verify_running_shell, remote desktop),
   15-minute SDLFrameBench soak, before/after (`ASTRA_DISPLAY_PROFILE=1`,
   journal `render profile copy_us` should go ~9,300 → ~0).
4. If F2SDRAM cannot work: fall back to the hotfix-only bitstream (c1e9f2e7)
   with release 8533ffa1 and keep the 1.6 mailbox.

### Step 1 result (session 6): the bridge is enabled; the reset theory is refuted

Read-only on the board (stage 8, release 8533ffa1, combined bitstream), one
aligned 32-bit load per process (`/data/cert/rd32.py ADDR`, libc `mmap` of
`/dev/mem`, no hang):

| Register | Address | Value |
|---|---|---|
| RSTMGR BRGMODRST | `0x10d1102c` | `0x00000000`: every bridge, F2SDRAM0 (bit 3) included, out of reset |
| RSTMGR HDSKEN | `0x10d11010` | `0x00031e0d`: FPGAHSEN (bit 2) set |
| RSTMGR HDSKREQ / HDSKACK / HDSKSTALL | `0x10d11014..1c` | `0` / `0` / `0` |
| F2SDRAMMGR FLAGINSTATUS0 | `0x18001014` | `0` |
| F2SDRAMMGR FLAGOUTSET0 | `0x18001050` | `0` |

On Agilex 5, TF-A's `socfpga_bridges_enable` does not touch the F2SDRAM
sideband manager. Its F2SDRAM0 path is only the reset-manager handshake
(set FPGAHSEN, request and clear FPGAHSREQ and F2SDRAM0REQ, then pulse and
release BRGMODRST bit 3; `plat/intel/soc/common/soc/socfpga_reset_manager.c`,
FPGA2SDRAM block under `PLATFORM_MODEL == PLAT_SOCFPGA_AGILEX5`). The
sideband FLAGOUT/FLAGIN writes are in the `#else` (Stratix 10 / Agilex)
branch. The registers above are that sequence's end state, so `bridge enable`
did enable F2SDRAM0. The zero sideband flags carry no information here.
Offsets come from TF-A `socfpga_reset_manager.h`, `socfpga_f2sdram_manager.h`
and `agilex5/include/socfpga_plat_def.h`. `0x18001058` and `0x18000C00` have
no TF-A definition and were not read.

### Root cause (session 6): an SLVERR from our own register file panics Linux

**It was never F2SDRAM.** `astra_graphics_control.sv` answers a rejected
register store with SLVERR (`write_response_q <= 2'b10`, 27 sites). 0x248,
0x204, 0x210 and 0x21c reject stores while the render engine is enabled or
busy. `astra-host-read-bench` left the engine enabled after its Media RAM
pass, then wrote 0x248. On the DE25, an error response to a CPU store over
the bridge is an asynchronous SError. The kernel is non-RAS for it and
panics, and with `kernel.panic=0` the board sat dead until a power cycle.
With no serial console reaching beast, the panic text was never seen.
Reading an unimplemented register (0x248 on an older bitstream, the Copper
window) is the same class of failure.

Proven both ways on the board (combined bitstream `3cb06755`):
- Fixed bench (engine stopped and idle before the store): 16x1 and 640x480
  x 20 both PASS, byte-exact. Host BLIT **2,135 us (575 MB/s)** against
  Media RAM 2,458 us. CPU fill of the host arena 2,686 MB/s against 132 MB/s
  into Media RAM. F2SDRAM host reads work, with no FSBL/JIC change.
- Unfixed bench (the original binary): the board went down and **rebooted by
  itself** (uptime 22 s), then returned to stage 8.

Changes:
- Board: `/etc/sysctl.d/90-astra-panic-reboot.conf` sets `kernel.panic = 10`.
  Any panic now reboots the board instead of needing a power cycle.
- `astra_graphics_render_stop()` (`fpga/arty/linux/astra_graphics_hw.c`)
  clears RENDER_CONTROL and waits until STATUS shows neither BUSY nor
  ENABLED. The bench and `astra-terminal-display` call it before every
  store to 0x248 and to the ring/generation registers, and give up rather
  than store if the engine stays busy. Before this, the 1.7 helper wrote
  0x248 at startup without stopping the engine (a helper restart over an
  enabled engine would panic). Every batch also wrote 0x21c right after
  CONTROL=0 (after a stalled render, the next batch would panic).
- The bench announces each phase on stderr, and
  `ASTRA_HOST_READ_BENCH_PAUSE=N` spaces the phases apart. `stdbuf` does not
  work on the static binary, so stdout is buffered: read stderr.

Recommended RTL follow-up: answer rejected stores with OKAY and latch a
sticky error register instead of SLVERR, so a software ordering bug cannot
panic the host. (In progress: rejected accesses answer OKAY and are
recorded in a sticky fault record; capability bits advertise the fault
record and the host aperture.)

### 1.7 on the board: the first desktop batch wedges the engine (open)

Release `0cbcc15c` (1.7, the helper with the stop guard, full verify
green) was published and deployed. The first desktop batch then wedged the
render engine: `render batch stalled: commands=61 completed=0
status=0x11`. This reproduces on every boot, warm or cold. The guard
worked: the board did not panic. But BUSY never clears, and
`astra_render_host_reads` resets only on `build_reset`, so a render soft
reset cannot recover a lost host read. Recovery is a reboot.

- Captured with `ASTRA_DISPLAY_STALL_DUMP=DIR`, a new helper diagnostic
  that writes the first stalled batch and the render register bank. The
  capture is saved in the session scratch as `stall-batch.bin` and
  `stall-registers.txt`. At the stall: 0x22c=1 (fetched), 0x230=0
  (completed), SUB_CONSUMER=0. Command 1 is a full-screen **FILL**
  (opcode 1, 1920x1080). Its destination descriptor is at 0x819000 in the
  host window: base 0x400000 in Media RAM, RGB565, pitch 3840. The bench
  only ever used BLIT, which passes byte-exact.
- Refuted: the EMIF flush request (RSTMGR HDSKREQ bit 0 = EMIFFLUSHREQ).
  It is left set by a software reboot and clear after a power cycle, but
  the bench passes with it set, and clearing it did not release the stuck
  read.
- Board rolled back: `astra-release.py select /var/lib/astra current
  8533ffa1…`, then reboot. Stage 8, desktop working.
- **Cause, proven on the board: the F2SDRAM path loses narrow read bursts
  longer than 128 beats.** The host bridge is 64-bit, and the Merlin width
  adapter has `PACKING=0`, so every beat stays 8 bytes on the 256-bit
  port. With 61 commands queued, the command processor's first host read
  is a 32-entry ring prefetch: 256 beats (2 KiB) at `0xbcd01000`. The
  replay in `tb_astra_render_replay.sv` shows that command 1 makes exactly
  two host reads, this prefetch and one descriptor read. The bench never
  went past 29 beats. Sweep with `astra-host-read-bench 16 1 1 N`, where
  the 4th argument sets how many BLITs are queued (prefetch = min(N,32) x
  8 beats): 8, 24, 64 and 128 beats PASS; 136 beats stalls with
  status 0x19 and one read never returns. Whether the real limit is 128
  beats or 1 KiB is not separated yet.
- **Deployed 2026-09-29: bitstream `rtl-noerr`** (rbf `68ce7bb2…`, jic
  `20544848…` not programmed, 40,109 ALMs, timing met; entry in
  `fpga/de25/TIMING_CLOSURE.md`). Installed with `install_boot_bundle.sh`
  from `/var/lib/astra/boot-incoming-noerr`. Rollback bundle:
  `/var/lib/astra/boot-incoming-f2s` (`3cb06755`). On the board:
  - CAPABILITIES reads `0x3ff7`, and the running shell passes against the
    build.
  - `astra-render-certify`: 9/9 PASS.
  - Burst sweep with 17, 32 and 64 queued commands (136, 256 and 512 beats)
    passes byte-exact. A 640x480 host-source BLIT takes 2.13 ms, against
    2.40 ms from Media RAM.
  - A store to 0x248 while the engine is enabled, which panicked the board
    before, is dropped without a panic. The fault record reads COUNT=2 and
    FIRST=`0x80020248` (store, rejected, 0x248), and the unmapped load at
    0x3f0 returned 0 and was counted.
  - The 1.6 runtime (8533ffa1) runs unchanged on this bitstream.
- **1.7 display failures (release 477eb5cd on 68ce7bb2), root cause in
  the helper.** Each start failed its first batch with `render command N
  failed`: BAD_VERSION at 32 or 56, or BAD_GENERATION at 0. Then it
  restarted about 10 times a second. `execute_finished_batch()` sends
  batches the helper builds itself (`terminal_batch`: terminal text,
  cursor, `make_render_target_inactive`) through
  `execute_render_batch()`. Under `ASTRA_HOST_APERTURE`,
  `stage_render_batch()` was a no-op, so the engine executed whatever the
  guest last left in the host payload. Evidence:
  - The captured batches replay 61/61 and 56/56 through the aperture
    (`replay.py`, host and Media modes, generation 1 then 2).
  - The 1.7 helper with the aperture compiled out runs clean.
  - A 0.2 ms or 2 ms delay before the doorbell changed nothing.
  - The reordering theory was never shown, and the RTL experiment for it
    was reverted.
  Fix: `select_render_aperture()` in `astra_terminal_display.c`. A guest
  batch (the payload) runs with the aperture on and no copy. A helper
  batch is staged into Media RAM with the aperture at 0. The register is
  stored only when the choice changes, with the engine already stopped.
  The self-test covers the tracker. `ASTRA_DISPLAY_STALL_DUMP` now also
  dumps a failed batch. `astra-host-read-bench` checks every completion
  record.
- **Board, 1.7 with the fixed helper as an override on 68ce7bb2**
  (SDLFrameBench 640x480, `ASTRA_DISPLAY_PROFILE=1`): ARGB+blend
  **43 fps** (was 32), ARGB **45** (was 33), RGB565 **54** (was 46-47).
  The helper's `render profile copy_us` averages **37 us over 5,254
  batches** (was about 9,300 for an ARGB batch). No failures.
- **Deployed and soaked: release `b421dc7d`** (1.7 with the per-batch
  aperture fix, full verify READY-TO-PUBLISH) on bitstream `68ce7bb2`.
  15-minute SDLFrameBench soak with 245 samples: argb-blend avg 42.3 fps
  (min 41), argb 44.7 (min 44), rgb565 53.0 (min 52). 128,587 batches,
  helper copy_us 37 avg, hardware_us 1,371 avg, 0 failures, no helper
  restarts, no board reboot. The board is left on b421dc7d with no
  overrides and profiling off. `kernel.panic = 10` stays.
- Board certification of b421dc7d on 68ce7bb2 (2026-09-29):
  - **POST PASS**: every line OK (SDRAM, splash, front panel, data and
    address lines, cache coherence, kernel image, BootInfo, VBR, user copy
    with fault recovery, kernel worker, Vesta timer, input queue).
  - **Remote desktop PASS**: `fpga/de25/linux/test_remote_desktop.py` on
    the board against the live service, with VNC auth, a 1920x1080 frame
    (6,220,800 bytes, the real desktop) and `--pointer 400 300
    --verify-rfb-pointer`. The `--macos-format` capture also passes.
    Astra, remote desktop and audio host are all active with 0 restarts.
- Committed and pushed as `c5d59368..82ab9926` (kernel, emulator,
  userspace, FPGA, docs).
- Fix details (RTL, with the no-error-response change):
  `astra_render_host_reads` splits host-port bursts below the limit and
  passes RLAST only on the last piece. Media RAM bursts are unchanged. A
  router reset tied to the engine soft reset was rejected: a late host
  read would come back with no order entry and wedge the port.

Superseded notes from before the root cause:

No evidence of the original hang survives. The journal is volatile (one
boot), `/data/cert/host-read-bench-f2s.log` was never written, and every
beast serial capture from 2026-09-28 is 0 bytes.

Remaining suspects:
1. The FSBL/handoff mismatch: the QSPI still holds the pre-F2S JIC, whose HPS
   configuration has FPGA2HPS at 256 bits.
2. An AXI or platform problem on the new host path: the width adapter or
   dual-clock FIFO, the patched acceptance of 8 on a vendor adapter declared
   as 1, or AxPROT `000` (secure) from the render engine.

A static read found no burst that crosses 4 KiB (copy engine and command
prefetch split at page boundaries, and the aperture base is 4 KiB aligned),
and the host arena is `dma_alloc_coherent` (Normal-NC).

Next, the smallest discriminating test: `astra-host-read-bench 16 1 1` (one
short burst) with Astra stopped, `dmesg -w` streamed to beast and serial on
both ttyUSB ports. If a single burst hangs, suspect 1 or the platform;
otherwise shape the bursts. Either way a hang needs a physical power cycle.

## Other open items

- Kernel IPC is ~58 syscalls/frame at ~3k instructions: blocking receive is
  3 traps, each NDK request creates/closes a reply port. A kernel CALL /
  REPLY_RECV primitive is the next software lever.
- User-readable monotonic timebase (clock reads are syscalls).
- Commit in logical chunks once the board state is settled (kernel/copy
  engine, attachment, SDL/NDK, QEMU/helper latency, mailbox 1.7, FPGA pixel
  BLIT, wedge fix, F2SDRAM platform). Nothing from 2026-09-25 on is committed.

## Tools and hazards

- Beast board tools: `~/astra-mg/hw/run_bench.sh BUNDLE SECONDS [COUNT]`
  (forwards QMP, opens the app from its desktop icon, prints SDL_FRAME_BENCH
  lines + host CPU). SDLFrameBench keeps running afterwards — close it or it
  loads the board indefinitely.
- `pgrep -f`/`pkill -f` patterns match the invoking shell's own command
  line; use log markers, not process patterns, to wait.
- `~/astra-mg/verify-then-publish.sh` now stops at READY-TO-PUBLISH.
- Beast SSH can be slow under load (60 s+ per connection).
