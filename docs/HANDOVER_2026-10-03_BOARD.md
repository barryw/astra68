# Handover 2026-10-03: playing Doom on the DE25 -- black bars and audio

Read `CLAUDE.md`, `AGENTS.md`, then this page. Previous:
`HANDOVER_2026-10-02_SPAWN.md` (kernel cost analysis is at its end of
session, summarised below).

## State

- Software release `72daf6dc` (HEAD `f8de7ce6` + nothing) published to the
  DE25 with `~/f2s-pub.sh`. Doom plays; keyboard and menu work.
- Bitstream `rtl-arb` (scanout arbitration, below) built on beast in
  `~/astra-mg/rtl-arb/build/de25-arb`, timing clean (worst slack +0.000 ns,
  no negative), 40,833 / 46,800 ALMs. Installed with `install_boot_bundle.sh`
  from `/var/lib/astra/boot-incoming-arb` (rbf `525133e2...`); rollback
  bundle `/var/lib/astra/boot-incoming-scale`. **Not yet accepted on HDMI.**
- Uncommitted: `fpga/de25/add_lpddr4b.tcl` + `test_astra_shell_contract.py`
  (arbitration shares). Commit after the physical check passes.

## Black bars on HDMI (fixed, deployed, verified by capture)

- Symptom: full-width black bands and repeated (ghosted) scanlines, worse
  with each extra TestDraw2 window, in Doom too.
- **The arbitration-share bitstream (`525133e2`) is installed and booted
  and did not change it.** Arbitration was not the cause.
- **The fabric capture is not clean.** Six remote-desktop captures with
  four TestDraw2 windows open all show one solid black band, rows 430-957,
  at the same rows every time. The capture taps `pipeline_rgb` exactly as
  HDMI does, so the bands are made in the line pipeline.
- **The band is where the compiled window scene has many spans.** Read
  from DDR (`fb_base`, `FB_WINDOW_SCENE_BYTES`): rows 330-416 have 7 spans
  and display; rows 417-795 have 11 and 798-896 have 9 and are black
  (from 430: four lines of queue slack, then a backlog); rows below 899
  have 1 and recover by 958.
- Cause: `astra_framebuffer_line_builder.sv`'s scene path is fully serial
  per span: one AXI round trip for the span record, then one or more for
  the segment, then `ST_SEGMENT_DRAIN` waits for the last beat before the
  next span's record is requested. Writing the line costs 960 cycles at
  two pixels a clock. The line period is 2,442 build cycles.
  `FB_AXI_RESPONSE_STALL_CYCLES` sampled 300k times under load gives a
  typical round trip of ~85-100 cycles (tail 964). 960 + 17 round trips
  passes the line period, the 4-entry request FIFO backs up, every later
  line completes after its deadline, and the band runs until the spans
  thin out. The Sep 14 closure measured 3 spans against a 12-15 cycle
  simulated latency, which is why its 39% margin did not hold.
- Fix (deployed 2026-10-04, rbf `effd9c42...`, bundle
  `/var/lib/astra/boot-incoming-scene`, rollback `boot-incoming-arb`): the
  scene path streams -- span records in bursts into a 16-entry descriptor
  ring, segment reads issued ahead under the beat-FIFO credit, writer
  behind. Bench: 11 spans at latency 100 went from 2,933 to 1,259 cycles.
  Board, same 11-span layout and same latency: 10/10 captures clean (was
  6/6 banded). Record in `fpga/de25/TIMING_CLOSURE.md` (2026-10-04).
- Not yet seen on the TV or Cam Link (the Cam Link showed NO SIGNAL from
  the Mac this session); the capture taps the same `pipeline_rgb` as HDMI.
- Uncommitted: the builder, its bench and `run_tests.sh`, the README, the
  arbitration shares (`add_lpddr4b.tcl`, shell contract test, in the
  installed bitstream), and this record.
- **Doom still shows a black bar** (user, on HDMI): ~40-50 rows high, at
  random rows, with four TestDraw2 windows behind fullscreen Doom (1 span
  a row, scene banks alternating at ~58 commits/s, no deferrals). 20 fabric
  captures showed no black rows, but captures drop frames under DDR load,
  so they cannot rule it out. Next bitstream (`rtl-health`) exposes the
  scheduler counters at 0x254-0x264 (lines built/failed, overruns,
  underruns via Gray-code CDC, max framebuffer build cycles) to measure it.
- **Doom bar fixed (2026-10-04, rbf `b61dbda8`, bundle
  `/var/lib/astra/boot-incoming-fifo`).** The new counters showed bursts
  of 25-27 underruns, each starting within 0.2 ms of a ~0.5 ms render
  command; builds stretched to 4,344 cycles. fb read round trip during
  render work: p99 321 / max 496 cycles (idle p99 69). The line builder's
  beat FIFO (its read credit, 2 bursts = 1 KiB) could not cover four bytes
  a clock at that latency. EMIF read QoS (`rtl-qos`, rbf `b52db26a`) changed
  nothing, which ruled out queue priority. Beat FIFO raised to 8 bursts
  (4 KiB): bench at latency 300 went 2,403 -> 1,883 cycles; board under
  Doom 15 s: 0 underruns, 0 overruns, 0 failed, max build 2,611. The QoS
  and counters stay (counters are the measurement; QoS is harmless).
- Follow-up: the line scheduler still builds queued lines that are already
  past the beam, so one late line costs several (the band ran ~60 lines
  past the last heavy row). Dropping stale requests would recover in one.
- The Mac's Icarus 14 (devel) rejects use-before-declare in
  `astra_render_command_processor.sv` (`engine_writes_drained`) and its
  bench; beast's Icarus 12 accepts. Pre-existing; the gate host is beast.
- Still missing: `lines_failed`, `scheduler_overruns`, `pixel_underruns`
  are tied off in `astra_de25_graphics.sv` (`unused`). Expose them (CDC
  needed for the pixel-domain counter) so this is measurable, not visual.

## No HDMI audio on a TV (open)

- 2026-10-04, on the scene-stream bitstream: the TV now plays audio, but
  Doom's sound effects are missing and the music is soft and laggy (four
  TestDraw2 windows were also running and taking the vCPU). Not yet
  investigated; nothing in the scene change touches audio.
- Same stream through the Cam Link had the sound effects; the TV did not.
  Doom's effects are centred and its music is spread, so the TV behaved like
  a sink cancelling L=R content (consistent with the silent direct tone,
  which is L=R). After the next reboot (rbf `b52db26a`, QoS) the TV played
  everything, so it varies by boot, not by stream content: suspect HDMI
  audio setup at link bring-up. `/data/fd-chan.py` on the board plays
  left / right / same-phase / opposite-phase tones (440/660/550/330 Hz) to
  tell which channels a sink keeps when it recurs.

- The Cam Link captures the board's HDMI audio (Doom audible, stuttering);
  a Sony TV that plays audio from a Mac over HDMI plays nothing from the
  board, including a direct tone written to the daemon
  (`/data/fd-tone.py SECONDS` on the board) that bypasses the guest.
- FPGA side verified: I2S feed drains at real 48 kHz; MCLK 12.288 MHz
  (256 fs, matches 0x0A default); HDMI_MCLK pinned (PIN_CF1).
- ADV7513 LUT is Terasic's HDMI_TX demo
  (`~/de25-resource-revA/Demonstration/FPGA/HDMI_TX/V_HDMI/I2C_HDMI_Config.v`)
  with `patch_vendor_hdmi.py`'s four 2-channel changes (0x0C 0x84, 0x14
  0x0B, 0x73 0x01, 0x76 0x00). Guides: `~/Downloads/ADV7513_Programming_Guide.pdf`,
  `~/Downloads/ADV7513_Hardware_User_Guide.pdf` (analog.com blocks scripted
  downloads).
- Plan: one bitstream with four LUT variants chosen by DIP switch (HDMI
  replug re-runs the LUT): A current; B Terasic demo exactly; C 0x0B=0x0E
  (MCLK internally generated); D CTS manual 148500 (0x0A=0x80,
  0x07..0x09 = 0x02 0x44 0x14). Also consider the ADI mandatory fixed
  registers 0xE0=0xD0 and 0xF9=0x00, absent from the vendor LUT.

## Doom CPU on the DE25 (2026-10-04)

- SDL `Blit1to4` in 68040 asm (`sw/userspace/sdl2/prepare_source.py`
  patch, checked by `tests/video_probe.c`): beast profile 288.5M -> 114.7M,
  Doom total -15%. Published as release `1db703fa`. A/B on the board: no
  change in Doom frames (16.6/s) or audio gaps: not the board's limit.
- **Board profile** (`de25-profile` QEMU in `/data/prof`, runtime drop-in
  `/run/systemd/system/astra.service.d/profile.conf`, gone on reboot;
  `~/astra-mg/doom-prof/doom-board.aprof`): during Doom **74% of guest
  instructions are compiler.library soft-float**: 54% lb1sf68 `__mulsf3`
  bit loop (~187 instructions a call), 20% conversions/pack. Caller is
  audio (SDL_mixer `_Eff_position_s16msb` float panning per sample,
  `SDL_MixAudioFormat`). The beast profile missed it: no audio host.
- Fast helpers done, not deployed: `__mulsf3`/`__divsf3` in C in the
  toolchain patch (`fpgnulib.c` ASTRA-MUL-DIV-SINGLE block, lb1sf68 bodies
  guarded `__astra__`), MULU.L/DIVU.L, ~45 instructions. Bit-exact vs IEEE
  over 11.1M cases (`toolchain/test-fpgnulib-muldiv.sh`; mutations fail).
  Installed in beast's toolchain (backup
  `~/astra-toolchain/backup-20261004-muldiv`); SDL mixer and Chocolate Doom
  QEMU gates pass (`~/astra-mg/sf-gate.sh`).
- The structural fix under discussion: userspace hardware FPU (kernel FP
  context switch, hard-float ABI rebuild). `KERNEL_ARCHITECTURE.md` LOCKS
  `-msoft-float`; `DEVILUTIONX_PORT.md` records userspace FPU as allowed.

## Audio stutter (open)

- Audio daemon counted 1,466 underruns and 16,552 software gaps during a
  Doom session: the guest does not produce audio in real time while Doom
  uses the vCPU. Suspect SDL's 44.1 -> 48 kHz soft-float resampler in the
  guest (cf. loopwave, `HANDOVER_2026-09-30_SDL_PERF.md`). Profile next.

## Kernel performance (the larger plan, not started)

Measured 2026-10-02 (workload suite + probes, see the SPAWN handover):

- Kernel is 52% of all guest instructions in the suite.
- Null syscall ~325 kernel instructions (fine). An IPC round trip is
  ~12k kernel instructions (queued port machinery: allocation, handle
  transfer staging, wait-set registration, scheduler pick, ~60 functions).
  `PORT_CALL` only merged client traps; VFS/POSIX/streams clients do not
  use it; services have no reply-and-wait. Plan: synchronous handoff fast
  path (L4/QNX style) + reply-and-wait + migrate hot clients.
- fork+exec: ~570 pages COW-cloned per `ls` from zsh, ~680 torn down,
  ~300 COW faults after; each page op thousands of instructions (owner
  ledger, reverse map). Plan: share page tables at fork.
- `PROCESS_INFO` scans every thread slot 5x per call (Lua `os.clock`).

## Tools added this session

- `bench-workloads.py --register d0 --probe <_kernel_syscall_entry>` gives
  a syscall histogram per workload.
- beast `~/astra-mg/fdgatepar.sh RUNS WORKERS MODE`: parallel
  `test-terminal.py` power gates with ROM refresh stubbed and a SIGTERM
  dump of serial + trace (found the reaper URP race).
- beast `/tmp/shot.sh NAME`: board screenshot via remote desktop to PNG.
