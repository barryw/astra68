# DE25-Nano Timing Closure

## 2026-10-04: line-builder beat FIFO 8 bursts (routed, deployed, rbf `b61dbda8`)

- Built on `beast` with `build_astra_shell.sh` from `~/astra-mg/rtl-fifo`,
  on top of `rtl-qos`. Installed as `/var/lib/astra/boot-incoming-fifo`.
- Cause. The scanout health counters (`rtl-health`) showed bursts of 25-27
  underruns each starting within 0.2 ms of a ~0.5 ms render command, with
  line builds stretched to 4,344 cycles. Framebuffer read round trips during
  render work reached p99 321 / max 496 cycles (idle p99 69). The beat FIFO,
  the line builder's read credit, held 2 bursts (1 KiB) and could not cover
  four bytes a clock at that latency. This was Doom's black bar.
- Change. Beat FIFO 2 -> 8 bursts (4 KiB read credit). Bench at latency 300:
  2,403 -> 1,883 cycles. Board under Doom with four TestDraw2 windows, 15 s:
  0 underruns, 0 overruns, 0 failed lines, max build 2,611 cycles.
- Routed: **40,854 / 46,800 ALMs (87 %)**; 3,640,768 / 7,331,840 block-memory
  bits, 317 / 358 RAM blocks, 67 / 376 DSP blocks.
- No negative slack in any corner. Graphics clock (`pixel_pll outclk1`)
  setup +0.262 ns; worst setup +0.130 ns (`ASTRA_HDMI_PIXEL_CLOCK`); worst
  hold +0.001 ns (`pll_inst outclk0`).

## 2026-10-04: scanout read QoS (routed, rbf `b52db26a`; no effect)

- `rtl-qos`, on top of `rtl-health`: framebuffer and scene reads carry
  ARQOS priority 3 through the HPS bridges (`add_lpddr4b.tcl`,
  `patch_vendor_top.py`, contract test). Underruns under render load were
  unchanged, which ruled out queue priority. Kept: harmless.
- Routed: 40,713 ALMs (87 %); 3,616,192 bits, 317 RAM, 67 DSP. No negative
  slack; graphics clock setup +0.367 ns; worst setup +0.010 ns
  (`ASTRA_HDMI_PIXEL_CLOCK`); worst hold +0.000 ns (`pll_inst outclk0`).

## 2026-10-04: scanout health registers (routed, rbf `ed9f5cf9`)

- `rtl-health`, on top of `rtl-scene`: bank 9 0x254-0x264 report lines
  built, lines failed, scheduler overruns, pixel underruns (Gray-code CDC
  from the pixel domain) and the maximum framebuffer build cycles
  (`astra_graphics_control.sv`, `astra_graphics_pipeline.sv`, Linux header
  names in `fpga/arty/linux/astra_graphics_hw.h`). The benches check them.
- Routed: 40,773 ALMs (87 %); 3,616,160 bits, 317 RAM, 67 DSP. No negative
  slack; graphics clock setup +0.272 ns; worst setup +0.092 ns
  (`ASTRA_HDMI_PIXEL_CLOCK`); worst hold +0.001 ns.

## 2026-10-04: streamed window-scene lines, scanout arbitration shares (routed, deployed)

- On `beast`, Quartus Pro 26.1.1 Build 130 completed a full production
  synthesis, fit, route, signoff and programming-file build with
  `build_astra_shell.sh` (`DISPLAY` unset) from a checksum rsync of the Mac
  working tree at `~/astra-mg/rtl-scene`
  (`ASTRA_DE25_BUILD_ROOT=build/de25-scene`). First attempt. The source is
  the `rtl-arb` build (scanout `arbitrationPriority` 8, render and capture
  1; installed 2026-10-03 and did not change the black bands) plus this
  change.
- Cause. With four overlapping windows the desktop's compiled scene has 11
  spans on rows 417-795; the remote-desktop capture (which taps
  `pipeline_rgb`, as HDMI does) showed a solid black band on rows 430-957
  in every frame. `astra_framebuffer_line_builder.sv` built a scene line
  span by span: a round trip for the span record, one for the segment, and
  a full drain before the next record. Under render load an LPDDR4B round
  trip is ~85-100 build cycles (`FB_AXI_RESPONSE_STALL_CYCLES` sampled
  300k times: p90 63, p99 84 cycles into a wait, tail 964), so 960 write
  cycles plus ~17 round trips passed the 2,442-cycle line period and the
  scheduler's 4-entry queue fell behind until the spans thinned out.
- Change. A scene line is a stream: span records are requested in bursts
  (up to 16 records, split at 4 KiB) and decoded in a four-stage pipeline
  into a 16-entry descriptor ring; an issue walker requests each source
  span's pixels under the beat-FIFO credit, and the writer fills solid
  spans and writes source spans behind it. A per-burst tag routes record
  beats to the decoder and pixel beats to the FIFO; metadata takes no
  credit. The per-span checks are unchanged, plus an even source pitch
  (pixels must not straddle beats) and at most `OUTPUT_WIDTH` spans a
  line. An aborted line keeps its address valid until accepted and drains.
  `fpga/arty/graphics/README.md`, "Pipeline and memory boundary".
- `run_tests.sh` passes on `beast` (Icarus 12). The line-builder bench
  gains a pipelined fixed-latency memory (`MODEL_LATENCY`) and an
  eleven-span line shaped like the board's: **2,933 cycles before, 1,259
  after** at latency 100 (budget 1,520); 2,120 at a sustained 250. A
  37-span line (more than the ring, several record bursts, 4 KiB splits)
  is pixel-exact at 64 and 128 bits, and a discontinuous or out-of-surface
  span mid-line is refused, drained, and the line then rebuilds. Removing
  the record 4 KiB cap, the ring occupancy limit, or the surface-bound
  check each fails the bench.
- Routed resource use: **40,733 / 46,800 ALMs (87 %)** (+19 over
  `rtl-scale`); 3,616,160 / 7,331,840 block-memory bits, 317 / 358 RAM
  blocks, 67 / 376 DSP blocks.
- All production clocks are constrained and pass; no negative slack in any
  corner. The 165 MHz graphics clock (`pixel_pll outclk1`, Fmax 186.19 MHz)
  closes at **+0.689 ns** setup. Worst setup overall is +0.070 ns
  (`ASTRA_HDMI_PIXEL_CLOCK`), worst hold +0.001 ns
  (`ASTRA_HDMI_PIXEL_CLOCK`).
- Source SHA-256: line builder
  `905dc03d561fce0d42d374afa1d6608fd3ee04c6f4392554fdc0e88b93014916`.
  Build-input checksum manifest SHA-256
  `570298132dd31a6b9981f72211024985cb820e42e78fe43ddaa98968cba07a83`; boot
  core RBF SHA-256
  `effd9c420b61fe10ce1aea9954f7525ec745c8396cb9f9a6607f9dde739cf63c`;
  `astra68.hps.jic` SHA-256
  `26e3a0d602e0427c11da12b7fad09de09933518881bcc3c4709749ef4978aa66`
  (not programmed). Artifacts:
  `beast:~/astra-mg/rtl-scene/build/de25-scene/astra-shell/output_files/`.
- Deployed 2026-10-04 with `install_boot_bundle.sh` from
  `/var/lib/astra/boot-incoming-scene`; rollback bundle
  `/var/lib/astra/boot-incoming-arb`. Same four-window layout (spans per
  row identical, rows 417-796 at 11), same memory latency (p99 74, tail
  913): **10 of 10 captures have no black rows** (6 of 6 had the 528-row
  band before). Doom over it: 6 of 6 clean.

## 2026-09-30: scaled BLITs in pixel mode (routed, deployed)

- On `beast`, Quartus Pro 26.1.1 Build 130 completed a full production
  synthesis, fit, route, signoff and programming-file build with
  `build_astra_shell.sh` (`DISPLAY` unset) from a checksum rsync of the Mac
  working tree at `~/astra-mg/rtl-scale`
  (`ASTRA_DE25_BUILD_ROOT=build/de25-scale`). First attempt. The source is
  the `rtl-noerr` build below plus this change.
- Change. The copy burst mover's pixel mode samples its source
  nearest-neighbour with the blitter's Q24 steps, so a BLIT whose
  destination is at least as wide as its source (any height ratio) no
  longer falls to the serial blitter. Each chunk reads exactly its source
  span; with `step_x <= 1.0` that span is never wider than the chunk, so
  the unscaled slot and page bounds hold. The per-pixel source offset
  advances by 0 or 1 pixel from a 24-bit fraction, and each row advances
  the source by `floor(frac_y + step_y)` rows. The planner spends two more
  cycles per chunk (span) and one per extra source row (vertical
  downscale). `docs/GRAPHICS_ARCHITECTURE.md`, "Pixel-stream BLIT".
- `run_tests.sh` passes on `beast` (Icarus 12). `tb_astra_render_blitter`
  adds 72 random scaled cases, with and without stalls, clips and blending,
  against a Q24 reference. The serial path (4-mod-8 pitch, horizontal
  downscale) meets the same reference. Dropping the per-pixel carry, or the
  multi-row source advance, fails the bench. Perf case `4000` (408x167
  ARGB8888 onto 640x480 RGB565, testscale's background): **1.04 cycles
  per pixel**, plain or blended. The unscaled `2000` case is unchanged at
  1.03.
- Routed resource use: **40,714 / 46,800 ALMs (87 %)** (+605); 3,615,040 /
  7,331,840 block-memory bits, 315 / 358 RAM blocks, 67 / 376 DSP blocks
  (+1, the chunk step product).
- All production clocks are constrained and pass; no negative slack in any
  corner. The 165 MHz graphics clock (`pixel_pll outclk1`, Fmax 177.65 MHz)
  closes at **+0.431 ns** setup. Worst setup overall is +0.154 ns
  (`ASTRA_HDMI_PIXEL_CLOCK`), worst hold +0.000 ns
  (`pll_inst|iopll_0_outclk0`).
- Source SHA-256: copy burst
  `dc2d778602efaff70693b99e05140553ccc0e2cd88e7790d8d0607e2d85b94fd`,
  blitter `a4f54d8f6895d57b6ca646dcdc9b345a8845f140ef086d587135bff85bda2201`.
  Build-input checksum manifest SHA-256
  `f536a58ccc0c92d1b06f28e9acff91f52949e9a4b3d4327f41e4a4a8941b626e`; boot
  core RBF SHA-256
  `a13c8ed0ed63204d5a4ae666e319ccb68a5cb70ef1da7a30270dfed3d0ba2ba1`;
  `astra68.hps.jic` SHA-256
  `5da13bde45d7c0291479519edae128abc738a81fac794d5c8b7e432145a7d257`
  (not programmed). Artifacts:
  `beast:~/astra-mg/rtl-scale/build/de25-scale/astra-shell/output_files/`.
- Deployed 2026-09-30 with `install_boot_bundle.sh` from
  `/var/lib/astra/boot-incoming-scale`; rollback bundle
  `/var/lib/astra/boot-incoming-noerr`. `astra-render-certify` 10/10,
  including the new `ASTRA_RENDER_SCALED` case (the testscale background,
  blended, checked against the model byte for byte): **484,473 cycles,
  1.58 cycles per pixel**, 2.9 ms. Board demos, `run_bench.sh` 10 s:
  testscale 11 to **60** render batches/s, testrendertarget 27 to **102**,
  testsprite2 unchanged (47.6).

## 2026-09-28: no AXI error responses to the CPU, access-fault records, host-burst split (routed, not deployed)

- On `beast`, Quartus Pro 26.1.1 Build 130 completed a full production
  synthesis, fit, route, signoff and programming-file build with
  `build_astra_shell.sh` (`DISPLAY` unset) from a checksum rsync of the Mac
  working tree at `~/astra-mg/rtl-noerr`
  (`ASTRA_DE25_BUILD_ROOT=build/de25-noerr`). First attempt. The source is
  the F2SDRAM host-read build below plus two changes.
- No slave answers a CPU access with SLVERR or DECERR. The HPS turns either
  one into an asynchronous SError and Linux panics, which a store to
  `RENDER_HOST_APERTURE_BASE` while the engine was enabled did on the
  board. Graphics control, Copper, display capture, audio and the front
  panel now drop a refused store and return 0 for an unmapped load, both
  answered OKAY, and count them in a shared sticky record
  (`astra_access_fault_record.sv`): `ACCESS_FAULT_COUNT`/`FIRST` at
  `0x24c/0x250`, `0x4038/0x403c`, `0x503c/0x5040`, `0x6034/0x6038`,
  `0x7034/0x7038` (layout in `docs/GRAPHICS_ARCHITECTURE.md`, section 14).
  `CAPABILITIES` becomes `0x3ff7` on the DE25: bit 12 is the record, bit 13
  the host aperture. Graphics control no longer aliases offsets above
  `0x3ff`. Platform Designer's lightweight-bridge routers send unmapped
  addresses to a default slave (the vendor peripheral subsystem, then its
  button PIO), so the 2 MiB window has no interconnect DECERR holes. The
  H2F window maps `0x40000000-0x7fffffff` to LPDDR4B in full.
- Host-burst split. On the board, a narrow 64-bit read burst of 136 beats or
  more through the host aperture (F2SDRAM) never returns; 128 beats and
  fewer do. The render engine's ring prefetch reads up to 32 entries, 256
  beats, so the first desktop batch with 17+ queued commands wedged at
  intake (`SUBMITTED=1`, `COMPLETED=0`, consumer 0, busy). The
  host-read bench, at 30 beats or fewer, never hit it.
  `astra_render_host_reads` now issues host bursts as pieces of at most 32
  beats and passes RLAST on the last piece only; Media RAM bursts are
  unchanged. In the perf tb with the host port at 8 outstanding reads and
  300 cycles of latency, 32 and 64 beats run in identical cycles and 16 is
  slower (640x480 host-source BLIT 398,906 vs 319,900 cycles). A latency
  model option now loses bursts over 128 beats: the router bench stalls on
  the old router and passes on the new one. A replay of the captured
  61-command batch through the command processor and router reproduces
  the board wedge on the old router and completes command 1 on the new one.
- `run_tests.sh` (graphics and audio) passes on `beast` (Icarus 12). The
  Mac's Icarus 13 and 14 reject declaration-after-use that predates this
  change in `astra_render_command_processor.sv` and its bench. Every bench
  touched here also passes on the Mac. Restoring an error response
  in each slave, or dropping the fault recording, fails its bench.
- Routed resource use: **ALMs needed 40,109 / 46,800 (86 %)** (-184 from
  the build below); 3,615,040 / 7,331,840 block-memory bits, 315 / 358 RAM
  blocks, 66 / 376 DSP blocks.
- All production clocks are constrained and pass. Worst setup, hold,
  recovery and removal slacks are **+0.124 ns** (`ASTRA_HDMI_PIXEL_CLOCK`),
  **+0.000 ns** (`pll_inst|iopll_0_outclk0`), **+2.672 ns** and **+0.000 ns**
  (`ASTRA_AUDIO_SAMPLE_CLOCK`). The 165 MHz graphics clock
  (`pixel_pll outclk1`, Fmax 175.56 MHz) closes at **+0.364 ns** setup,
  +0.005 ns hold. The 148.5 MHz pixel clock (Fmax 176.03 MHz) closes at
  +1.053 ns.
- Source SHA-256: graphics control
  `f704d6dc16b37d07a97103072875e16dadfa3266e17c985e03776b9603bc5cc3`,
  router `9c6f0047c881acc5496f0309d44405092bc72ae356db196b54972b09be34a3c4`,
  fault record
  `4993c039bed16fcabbd775f70035ba4e663505a70466626f2f53a90fbee768f1`, DE25
  graphics top `4e86cb85db10946d626c1cf60f525a0dd07f5df8ba13c0350c251be22a6a5796`.
  Build-input checksum manifest SHA-256
  `4ec7132cc84d16ef5a753a009790999f830b285cfe5be16ce1099c5dd589c5ed`; boot
  core RBF SHA-256
  `68ce7bb2cea6d86c4aa180f90a664c9356c453fd6a98a23bce10c32d48f5f888`;
  `astra68.hps.jic` SHA-256
  `20544848faad9095e007b510d892f2b73300dea1f781c2a8f496a72bad9ef1cb`.
  Artifacts: `beast:~/astra-mg/rtl-noerr/build/de25-noerr/astra-shell/output_files/`.

## 2026-09-28: F2SDRAM host reads, debug-master reclaim and line-scheduler fix (routed, not deployed)

- On `beast`, Quartus Pro 26.1.1 Build 130 completed a full production
  synthesis, fit, route, signoff and programming-file build with
  `build_astra_shell.sh` (`DISPLAY` unset) from an rsync of the Mac working
  tree at `~/astra-mg/rtl-f2s` (`ASTRA_DE25_BUILD_ROOT=build/de25-f2s`).
  Third attempt; the failures are below.
- Source change over the pixel-stream BLIT checkpoint:
  - Host aperture. Render-engine reads of arena offsets
    `[0x00800000, 0x01000000)` (the render batch window) go to HPS DDR when
    the new register `RENDER_HOST_APERTURE_BASE` (0x248, HPS physical, 4 KiB
    aligned, 0 = off; writable only while the engine is idle) is set:
    host address = base + (offset - 0x800000). Writes (the completion
    ring) and every other read stay in Media RAM. With base 0 the design
    behaves as before. The aperture is defined in
    `protocol/astra_render_v1.json`.
  - `astra_render_host_reads.sv` sits on the render AXI read channels:
    one registered AR slot, and a 16-entry, 1-bit FIFO of which port each
    burst went to. R is taken only from the port of the oldest burst, so
    both ports keep reads in flight and responses stay in request order
    without a data buffer. A drain-on-switch first version cost 60 % on
    host-source blends in simulation and was replaced.
  - Platform. `astra_host_bridge` (read-only AXI4, 64-bit at 165 MHz)
    connects to `subsys_hps.f2sdram_adapter_axi4_sub` (256-bit at 100 MHz).
    Platform Designer adds the width adapter and a dual-clock FIFO. The
    vendor adapter declared read acceptance 1, which allowed one burst in
    flight. `patch_vendor_f2sdram_adapter.py` raises it to 8 in the hw.tcl,
    the .ip and both cached .qsys copies (patching hw.tcl alone is ignored).
    The adapter forces AxCACHE non-cacheable; F2SDRAM is not behind the
    SMMU (`f2sdram_SMMU=false`).
  - Area reclaim. The vendor JTAG debug masters (`subsys_debug`: `fpga_m`,
    `hps_m`, `hps_f2sdram`), the ACE5-Lite translator and its `fpga2hps`
    interconnect, and `ext_hps_f2sdram_master` are removed; no Astra tool
    used them. FPGA2HPS is disabled in the HPS IP (`disable_fpga2hps.tcl`,
    `f2s_data_width` 256 to 0), and `hps_subsys` is regenerated, because
    the vendor archive ships a pre-generated `hps_subsys.v` that still
    wires the removed ports. PMON is kept.
  - Includes the line-scheduler PREPARE/quiesce fix from the hotfix entry
    below.
- Failed attempts in this series: EXIT=1, the new `golden_top.qsf` cleanup
  grep did not match because the vendor qsf is CRLF (the grep and sed now
  accept `\r`); EXIT=3, Error 129015: `F2SOC_RDATA` on the MPFE not legally
  connected, because FPGA2HPS was left enabled and undriven; then, in a
  synthesis-only check, Error 13305 from the stale pre-generated
  `hps_subsys.v`.
- `run_tests.sh` passes on the same source, including the new router bench
  (18,508 beats, 516 bursts to Media RAM, 585 to the host port) and the
  0x248 control checks. Breaking the router on purpose fails the bench in all
  five cases: R taken from both ports, wrong translation, no window limit,
  no base-0 bypass, FIFO advanced per beat. Perf case `2000` (640x480
  ARGB8888 to RGB565) with the ring, descriptors and source in the host
  window, at host read latency 60: at most +140 cycles per frame over Media
  RAM (+0.04 %), 1.03-1.05 cycles per pixel in every mode. Router OOC: 58
  ALUTs, 31 registers.
- Routed resource use: **ALMs needed 40,293 / 46,800 (86 %)** (-4,239);
  4,620 / 4,680 LABs partially or completely used (4,432 logic, 188
  memory), packing difficulty High; 3,615,040 / 7,331,840 block-memory
  bits, 314 / 358 RAM blocks, 66 / 376 DSP blocks, 5 / 11 PLLs. Router 100
  ALMs, `astra_host_bridge` 88, its master agent 14. `subsys_debug`, the
  translator and `ext_hps_f2sdram_master` are absent from the fit report.
- All production clocks are constrained and pass, with no unconstrained
  clocks or ports. Worst setup, hold, recovery and removal slacks are
  **+0.012 ns** (`ASTRA_HDMI_PIXEL_CLOCK`), **+0.002 ns**, **+2.662 ns** and
  **+0.031 ns**. The 165 MHz graphics clock (Fmax 180.83 MHz) closes at
  **+0.530 ns**; the 148.5 MHz pixel clock (Fmax 165.04 MHz) at +0.675 ns.
- Source SHA-256: router
  `fd5e65868aff4a848acdeeb0064bc9d015b00170cf1cc14c76eebc84e1e2c456`, line
  scheduler `4cb5ce92bdb78f918e997bdc42b464d841333eba5ecbde5e3780c2475b7cffda`,
  graphics control
  `acf9efcc169f96d0e9b91b30fee263ee2c1b3ca4a108123a379e99befafc4486`, DE25
  graphics top `c5ed6aca6175ab40cd741d7a821a168bbe48c1da5790e4d17457e0d2cd926cf8`.
  Build-input checksum manifest SHA-256
  `8dbe3225e5d50988c92e7340de425895f1073d428f2440589dbf5c9ca6176c48`; boot
  core RBF SHA-256
  `3cb067558345445720d888b67a90d2d6cd5ff77bfbb8b13b42c4084b9f5a93fe`;
  `astra68.hps.jic` SHA-256
  `e1237549c9b468be7e39d9400784445e80752f4d72036678077169d48e0dd85e`.
  Artifacts: `beast:~/astra-mg/rtl-f2s/build/de25-f2s/astra-shell/output_files/`.
- On the board, still to prove: that F2SDRAM reaches HPS DDR at the
  physical address the host-arena module reports (a read-only JTAG probe
  through the old span extender was inconclusive), and that U-Boot and Linux
  boot normally with FPGA2HPS absent from the handoff.
- Disposition: routed; awaiting install after the line-scheduler hotfix
  soak.

## 2026-09-28: scene-commit wedge is a line-scheduler latch (hotfix)

- Symptom: under a sustained SDLFrameBench soak on `f14ab71c` the helper
  logs `graphics scene commit timed out` (COMMIT `0x12370007`, then
  `0x17e30007`). After that, every commit is `rejected` until a power cycle.
- Board registers on the live wedge: COMMIT pending=1 and DEFERRALS
  +60/s, so `frame_boundary` is live and `commit_quiesce` is set every
  frame. FB_AXI_STATUS `0x200`: the builder is idle, nothing is
  outstanding, and the AR/R counters are frozen with no stalls.
  SPRITE_STATUS has every busy bit clear. Render is idle. Nothing is
  moving, so this is not DDR starvation: `commit_safe` is held low by
  `scheduler_idle` alone.
- Cause: in `astra_line_scheduler.sv` the quiesce block cleared
  `line_prepare_valid` unconditionally. A frame-boundary quiesce that
  arrives while a launched line sits in `SCHED_PREPARE` withdraws the
  request. The Copper answers only a presented request
  (`astra_copper_beam_scheduler.sv`, `line_prepare_ready` requires
  `line_prepare_valid`), so the scheduler stays in PREPARE for good.
  `scheduler_idle` never rises, and the commit is deferred on every frame.
  `commit_pending` clears only when the commit activates or on
  `build_reset`. `scene_changed`, the only other thing that resets the
  scheduler, needs a commit or Copper to be enabled, so the latch
  survives helper restarts. Render load makes the race more likely: slower
  line builds push the last bootstrap line's PREPARE onto the vblank
  boundary.
- Fix: quiesce no longer withdraws a presented preparation request. The
  launched line finishes, the scheduler idles, and the commit applies at
  the next boundary. The tearing invariant is unchanged, because quiesce
  still stops every new launch. The change is one deleted assignment, with
  no area cost. QoS was not changed, because the board showed no display
  starvation.
- Test: `tb_astra_line_scheduler` "quiesce during preparation" raises
  quiesce while a line waits for preparation, with the tb's ready gated by
  valid as the real Copper does. It fails with the old code (state=2
  PREPARE, valid=0) and passes with the fix. Full `run_tests.sh` passes.
- Helper: `astra_terminal_display` now drains a leftover pending commit
  (`astra_graphics_scene_commit_drain`, 1 s) before its first commit. If
  the commit stays held, it reports `graphics scene commit held by
  hardware ... power-cycle the board` once and stays up, instead of
  exiting into a restart loop.
- Route (hotfix-only fallback, certified `f14ab71c` source plus the fix):
  `c1e9f2e7...` closes every clock with no negative slack (worst 0.000).
  Not installed; the combined F2SDRAM route carries the same fix.

## 2026-09-27: HPS-to-Media-RAM bandwidth is HPS-issue-bound (measured, no build)

- Board: routed shell `f14ab71c...` (unchanged), Astra stopped. Probe
  `/data/cert/astra-arena-bandwidth` (write-combining arena): store64,
  memcpy and 64-byte `stp q` all 133 MB/s; load64 13-15 MB/s; load_q64
  59 MB/s on A55, 257-296 MB/s on A76.
- PMON (`pmon_capture.sh`, configs `basic_lat`, `wo`, `ch_bp`, `ch_eff`)
  during the store phases: every write is one AW of 4 x 128-bit beats
  (64 bytes, the line the CPU merged); 124,828 AW / 499,202 W / 124,828 B,
  none missing. AW-to-B latency is 24 cycles at 100 MHz (average 24.0,
  maximum 24-25). AW backpressure 0.0 %, W backpressure 2.1 % (about one
  cycle per burst). AW-to-AW spacing 48.2 cycles, AW efficiency 2.1 %.
  The fabric accepts each write at once and answers in a fixed 24 cycles;
  the HPS issues the next write only about 24 cycles after it gets the B.
  The next AW is never issued before the B: one write outstanding.
- The limit covers the whole MPU and does not depend on the store width
  or the core type. A55 and A76 give the same 133 MB/s, and 2 or 4 cores
  storing at once still give 2.1 % AW efficiency, so the combined CPU
  write rate stays about 133 MB/s. Reads: 32-35-cycle fabric latency;
  single 8-byte loads are serial (58 cycles each); 64-byte A76 loads
  overlap about two.
- The fabric is not the limit. HPS DMA (dw-axi-dmac,
  `dmaengine_prep_dma_memcpy` from Linux RAM to `0x50000000`, measured with
  a probe module outside the repository) also issues 64-byte bursts with one
  transaction outstanding per channel. Throughput scales with channel count
  at an unchanged 23-24-cycle fabric latency:
  1 channel 152 MB/s, 4 channels 606-617 MB/s (5 of 5 runs verified byte-exact through
  the arena), 8 channels over both controllers 1,148 MB/s (144 MB/s
  each). Reads by DMA: 1/2/4 channels 131/256/493 MB/s.
  The second controller (`10dc0000`) is not usable. Its media reads time
  out, one of five write runs timed out, and afterwards the controller stayed
  wedged until a cold reset. A first probe that mapped the arena through the
  wrong controller's IOMMU hung the HPS. Reloading `golden_top_hps.sof` over
  JTAG recovered it; the HPS was up again after 30 s.
- Root cause: the Agilex 5 HPS allows one outstanding transaction per
  master on this path, and only one for all CPU writes together. The
  bandwidth is therefore 64 B / (fabric latency + HPS turnaround), which is
  64 B / 48 cycles at 100 MHz. Widening the path, adding outstanding capacity,
  deepening PMON or relaxing burst acceptance in the fabric cannot help: none
  of them is exercised. The one fabric-side lever is the 24-cycle round trip.
  Even with zero fabric latency, CPU writes cannot exceed about 264 MB/s,
  so 1 GB/s from CPU stores is not reachable with any FPGA change.
- Disposition: no RTL or platform change. The paths that do reach 1 GB/s
  are either many HPS masters writing at once (DMA channels; only one
  reliable 4-channel controller exists, and the display capture already uses
  it) or the fabric reading the source itself from HPS DDR over
  F2SDRAM/FPGA-to-HPS. The fabric then chooses the burst length and the
  number of outstanding transactions. After restore: `verify_running_shell`
  PASS, stage 8, and the probe unchanged at 133 MB/s.

## 2026-09-27: pixel-stream BLITs and texture stepper reclaim (deployed)

- On `beast`, Quartus Pro 26.1.1 Build 130 completed a full production
  synthesis, fit, route, signoff and programming-file build with
  `build_astra_shell.sh` (`DISPLAY` unset) from an rsync of the Mac working
  tree at `~/astra-mg/rtl-blit` (`ASTRA_DE25_BUILD_ROOT=build/de25-blit`).
  First attempt.
- Source change over the overlap-aware FILL_RECTS checkpoint below:
  - Burst mover pixel mode. An unscaled, unreflected BLIT with no flags or
    only `FLAG_BLIT_ALPHA`, between RGB565, XRGB8888 and ARGB8888 (any pair
    except a plain same-format copy, which stays on the byte path), with
    8-byte-aligned pitches, now streams through the mover at one pixel per
    clock: source chunks (and destination chunks when blending) are read
    ahead into the eight slots, each pixel is expanded, composited (a' =
    m(a, opacity), eight DSP products, the blitter's divide) and packed, and
    whole W beats are merged; the next chunk's AW is issued while the last
    one drains. Unblended conversion is the same datapath with a' = 255 and
    a zero destination (exact: m(x, 255) = x). A chunk whose source pixels
    are all opaque at opacity 255 skips its destination read; XRGB8888 and
    RGB565 sources are always opaque. Other BLITs keep the per-pixel path,
    so the old blend FSM stays (scaling, reflection, key, mask, palette and
    ROP still use it).
  - Area reclaim in the texture engine: the six attribute steppers no longer
    keep `step - 2A` copies or a registered wrapped remainder; the commit
    cycle subtracts 2A from the registered sum and selects between `Q` and a
    precomputed `Q + 1`. Same cycle schedule (the 194-case bench costs
    2,494,005 cycles before and after), bit-exact.
  - Quartus OOC (render engines): mover 1,856 to 2,703 ALUTs and 1,271 to
    1,968 registers; texture engine 8,788 to 8,208 ALUTs and 7,052 to 6,236
    registers; all engines 25,517 to 25,789 ALUTs and 23,136 to 23,018
    registers.
- `run_tests.sh` passes on the same source. The blitter bench adds 90
  random pixel-stream BLITs (every format pair, plain and blended, random
  lanes, pitches, clips, opacity and alpha, random read and write stalls)
  checked byte for byte against a reference model over the whole surface,
  18 more forced onto the per-pixel path with a 4-mod-8 pitch against the
  same reference, and 24 where only a row's first or last pixel is
  translucent. Perturbations
  (inverse alpha off by one, destination reads always skipped, wrong beat
  end, 200-byte chunks, either partial-beat alpha mask ignored, texture carry
  swapped) all fail.
- Routed resource use: **ALMs needed 44,532 / 46,800 (95 %)** (-26);
  4,673 / 4,680 LABs partially or completely used, packing difficulty High;
  3,615,520 / 7,331,840 block-memory bits, 313 / 358 RAM blocks (+2), 66 /
  376 DSP blocks (+4), 5 / 11 PLLs.
- All production clocks are constrained and pass. Worst setup, hold,
  recovery and removal slacks are **+0.168 ns** (`ASTRA_HDMI_PIXEL_CLOCK`),
  **0.000 ns**, **+2.480 ns** and **+0.022 ns**. The 165 MHz graphics clock
  (Fmax 177.78 MHz) closes at **+0.435 ns**; the 148.5 MHz pixel clock at
  +0.896 ns.
- Source SHA-256: copy mover
  `b512e19a44fad888fd8ee574fee57359284783e1e2fa5faee1843d089faa39bb`,
  blitter `61e18f8383af2b99833604c3a0f65349e2e25c4f8be589848c9dec741b60c6b2`,
  texture engine
  `68bb35fd34ef233667650fe91c20df923f328d87659b58cef075e04600c56aff`;
  command processor unchanged. Build-input checksum manifest SHA-256
  `4febdfaa96149d2732572eea1b4ad04a20a146cbc893e3da05110c9bbc9ddac1`; boot
  core RBF SHA-256
  `f14ab71c02adfa6b32f4c28ff2146fe790df5a9ef6b0e8173f2328d04685f50e`;
  `astra68.hps.jic` SHA-256
  `deb5dae0dba9174db3d5a91faa3048b293bcb0b2955badd41f08774ddc89c621`.
- Simulation (`perf/run_perf.sh`, case `2000`): one 640x480 ARGB8888 to
  RGB565 BLIT costs, at 25 / 150-cycle latency, 318,794 / 319,544 cycles
  unblended (1.04 per pixel), 318,796 / 319,546 blended with opaque alpha,
  and 319,020 / 324,330 blended with random alpha (1.04 / 1.06). The
  per-pixel path it replaces costs 25.0 / 87.5 cycles per pixel unblended
  and 76.3 / 170.0 blended (measured over 640x48 rows: about 7.7M / 26.9M
  and 23.4M / 52.2M cycles for the full frame); the board measured 97 ms
  and 230 ms on it.
- Hardware: installed on astra-de25 with runtime release `748e81fb...`
  (boot bundle via `install_boot_bundle.sh`; the previous boot files are in
  `/var/lib/astra/boot-rollback-main6-748e81fb`). `verify_running_shell`
  PASS, POST PASS, stage 8, `astra-render-certify` (`.g` build) PASS in every
  phase, and Astra back at stage 8 afterwards.
- Disposition: deployed.

## 2026-09-27: overlap-aware blended FILL_RECTS records (not deployed)

- On `beast`, Quartus Pro 26.1.1 Build 130 completed a full production
  synthesis, fit, route, signoff and programming-file build with
  `build_astra_shell.sh` (`DISPLAY` unset) from an rsync of the Mac working
  tree at `~/astra-mg/rtl-main6`
  (`ASTRA_DE25_BUILD_ROOT=build/de25-blend-overlap`). First attempt.
- Source change over the blended FILL_RECTS checkpoint below: a blended
  record no longer waits for every earlier write. The command processor
  keeps the unclipped rectangles of the last four records, each with the
  engine write count at which its writes were all issued, and a record waits
  only while an overlapping record still has unanswered writes, or while all
  four are unanswered. A first prototype kept one union bounding box and
  gained little (the box grew over undrained records); it was replaced.
  `run_tests.sh` passes (21.3 min); the ordering bench adds a record that
  overlaps an earlier, not the latest, record. Perturbations (no overlap
  check, marks ignored, only the latest record checked) all fail it.
- Routed resource use: **ALMs needed 44,558 / 46,800 (95 %)** (+323);
  4,666 / 4,680 LABs partially or completely used (4,451 logic, 215 memory),
  packing difficulty High; 311 / 358 RAM blocks, 62 / 376 DSP blocks, 5 / 11
  PLLs.
- All production clocks are constrained and pass. Worst setup, hold,
  recovery and removal slacks are **+0.116 ns** (`ASTRA_HDMI_PIXEL_CLOCK`),
  **0.000 ns**, **+3.410 ns** and **+0.036 ns**. The 165 MHz graphics clock
  (Fmax 183.52 MHz) closes at **+0.611 ns**; the 148.5 MHz pixel clock at
  +1.124 ns.
- Source SHA-256: command processor
  `6f27e818ab600318235c4e408bad94c36e9e4949dff1330386a3011d3ee94754`; other
  render sources unchanged. Build-input checksum manifest SHA-256
  `8eab8cbee0688f42ec514683bc5cd254fe5c939a230020c07d0d4d635f1ad261`; boot
  core RBF SHA-256
  `819ddadb17afcbd333fb6632f1285e68db39f020948dfe72aa5424d87ce32b9c`;
  `astra68.hps.jic` SHA-256
  `aa33cb7f17151f05ffc17c21e7534376822b6c596d7bffd40b055b857ca56d64`.
- Simulation (`perf/run_perf.sh`): 8x8 blended records cost 164 / 241 /
  345 cycles at 25 / 100 / 200-cycle latency (were 184 / 336 / 540). What
  remains is one read round trip per record: the mover reads the
  destination before blending it.
- Disposition: capacity and timing checkpoint; not programmed or certified.

## 2026-09-27: blended FILL_RECTS (not deployed)

- On `beast`, Quartus Pro 26.1.1 Build 130 completed a full production
  synthesis, fit, route, signoff and programming-file build with
  `build_astra_shell.sh` (`DISPLAY` unset) from an rsync of the Mac working
  tree at `~/astra-mg/rtl-main5` (`ASTRA_DE25_BUILD_ROOT=build/de25-blend`).
  First attempt.
- Source change over the LINES checkpoint below: FILL_RECTS option bit 1
  (`BLEND`): the burst mover reads the destination and writes it back
  composited under a straight-alpha ARGB color through three pipeline
  stages (twelve 8x8 channel products in DSP blocks), one 64-bit beat per
  clock; blended records wait for the previous record's writes. Semantics
  and constraints: `docs/GRAPHICS_ARCHITECTURE.md` §9. `run_tests.sh` passes
  on the same source (21.3 min), including equivalence with sequential
  `FLAG_BLIT_ALPHA` BLITs in three formats and the slow-memory ordering bench.
- Routed resource use: **ALMs needed 44,235 / 46,800 (95 %)** (+835 over
  the LINES route); 4,644 / 4,680 LABs partially or completely used (4,429
  logic, 215 memory), packing difficulty High; 3,599,136 / 7,331,840
  block-memory bits, 311 / 358 RAM blocks, **62 / 376 DSP blocks** (+9), 5 /
  11 PLLs.
- All production clocks are constrained and pass. Worst setup, hold,
  recovery and removal slacks are **+0.043 ns** (`ASTRA_HDMI_PIXEL_CLOCK`),
  **0.000 ns**, **+2.584 ns** and **+0.054 ns**. The 165 MHz graphics clock
  (Fmax 183.65 MHz) closes at **+0.615 ns**; the 148.5 MHz pixel clock at
  +0.860 ns.
- Source SHA-256: command processor
  `8b50168d2866d8148e42226c7bbe7f45f3a77e702192af28ea14267d4bef3980`,
  copy mover `4115ca7f2ca58bf039e15e09d9b23f479d956f850b9386cbd96898cb61e9a36c`,
  blitter `eb9cfd08b083ef61938f2d7f71358e0492db8a3a6b50a7a05cae437f00dcf300`,
  protocol header
  `2a4c2a772a0fd66a41ed320c03df9f6de20604cf9fe99cdde3824f62b3f9277b`.
  Build-input checksum manifest SHA-256
  `7ef0cc41f0dd67f7429395a4fec92d0f52072db52e4a5287149605ff206feba0`; boot
  core RBF SHA-256
  `b01eec665d54c9e0568d26f3809e5e5ee46202215f62b637e1f68ab362e54edb`;
  `astra68.hps.jic` SHA-256
  `2fff026252753688c8a1df48618657fc6b757beda19c27716f24c6c4a1d50928`.
- Simulation (`perf/run_perf.sh`): a full 640x480 blended record costs 0.33
  / 0.34 / 0.35 cycles per pixel at 25 / 100 / 200-cycle latency; 8x8
  blended records cost 184 / 336 / 540 cycles each, dominated by the wait
  for the previous record's writes (the per-pixel blitter path it replaces
  costs about 40 cycles per pixel).
- Disposition: capacity and timing checkpoint; not programmed or certified.

## 2026-09-27: boot-text area reclaim and LINES (not deployed)

- On `beast`, Quartus Pro 26.1.1 Build 130 completed a full production
  synthesis, fit, route, signoff and programming-file build with
  `build_astra_shell.sh` (`DISPLAY` unset) from an rsync of the Mac working
  tree at `~/astra-mg/rtl-main4`
  (`ASTRA_DE25_BUILD_ROOT=build/de25-reclaim-lines`). First attempt.
- Area reclaim: the fitted FILL_RECTS image spent 2,432 ALMs in
  `boot_text_i`. Its 32 Kbit font was a distributed (LUT) ROM and its two
  cell banks were refused as MLAB for read-during-write, so they became
  4,600 registers. The font is now block ROM (two M20K) read in the same
  stage as the cell (unchanged latency), and the banks are MLAB with
  `no_rw_check` (a bank is written only while inactive and an inactive
  bank's read data is never used). Quartus OOC for the overlay: 2,053 to 199
  ALUTs, 3,040 to 152 registers. The overlay bench now checks every pixel
  of four cells against the font file. The texture engine was not changed,
  so TRIANGLES cycles are unchanged.
- LINES (opcode 262): a segment array run through the geometry engine's
  LINE, sharing the FILL_RECTS record loop; `docs/GRAPHICS_ARCHITECTURE.md`
  §9. `run_tests.sh` passes on the same source (20.5 min) with LINES
  equivalence against sequential LINE commands and rejection cases.
- Routed resource use: **ALMs needed 43,400 / 46,800 (93 %)**, down 2,280
  from the FILL_RECTS route (45,680); 4,643 / 4,680 LABs partially or
  completely used (4,428 logic, 215 memory; the fitter spreads logic when
  it can, so this count stays near the total); packing difficulty still
  reported High. 3,599,136 / 7,331,840 block-memory bits, 311 / 358 RAM
  blocks, 53 / 376 DSP blocks, 5 / 11 PLLs.
- All production clocks are constrained and pass. Worst setup, hold,
  recovery and removal slacks are **+0.100 ns** (`ASTRA_HDMI_PIXEL_CLOCK`),
  **0.000 ns**, **+2.937 ns** and **+0.030 ns**. The 165 MHz graphics clock
  (Fmax 173.97 MHz) closes at **+0.312 ns**; the 148.5 MHz pixel clock, which
  now reads the font from M20K, closes at +1.104 ns.
- Source SHA-256: command processor
  `e7579ea1ba65474d81e30291ee2319fd933c3a5abe901a7637870616c416310c`,
  boot-text overlay
  `c9d91537c52063542ef95411390d9dc212df11ee2d89e51fbb812cfa23275360`,
  protocol header
  `517a496f81f265ef97e17a1bdcc7c521b474685a62c9ec55a4be8fa17def1867`.
  Build-input checksum manifest SHA-256
  `03d51d4c6074da99f8bd6402440037ce4add83a8cc62b1f163d608e1b3d0b69e`; boot
  core RBF SHA-256
  `2ce38c20261658eb5113872c7aa2846b18c5751dd077861b54967920d7571aee`;
  `astra68.hps.jic` SHA-256
  `220cd68df649e06f4c5676be8a3361dffef55aab287ba3e67e5ab0a2c0833eeb`.
- Simulation (`perf/run_perf.sh`): an 11-pixel LINES segment costs 51 / 52
  / 55 cycles at 25 / 100 / 200-cycle latency against 118 / 125 / 127 per
  LINE command; the testdraw2 frame in four commands (clear, FILL_RECTS,
  LINES, FILL_RECTS) costs 208k / 209k / 212k cycles.
- Disposition: capacity and timing checkpoint; not programmed or certified.

## 2026-09-27: FILL_RECTS (not deployed)

- On `beast`, Quartus Pro 26.1.1 Build 130 completed a full production
  synthesis, fit, route, signoff and programming-file build with
  `build_astra_shell.sh` (`DISPLAY` unset) from an rsync of the Mac working
  tree at `~/astra-mg/rtl-main3`
  (`ASTRA_DE25_BUILD_ROOT=build/de25-fill-rects`).
- Source change over the posted-writes checkpoint below: the `FILL_RECTS`
  opcode (4; `docs/GRAPHICS_ARCHITECTURE.md` §9), a command-processor record
  loop that reads up to 64 records per page-bounded burst into the local RAM
  and runs each as a blitter FILL, validated once as an array command. The
  completion-record queue went from eight entries to four. `run_tests.sh`
  passes on the same source (20.4 min), including FILL_RECTS equivalence
  with sequential FILLs, rejection cases and the ordering bench; the Linux
  helper's `render_batch_valid` accepts the opcode (host self-test passes).
- First attempt, eight-entry queue: **fitter failure**, "requires 4689
  LABs, the device contains only 4680" (45,942 ALMs placed). Disposition:
  the queue was trimmed to four entries and the record loop stores the
  record's point and extent straight into the command words; Quartus OOC for
  the render engines fell from 24,551 to 24,179 ALUTs (command processor
  4,563 to 4,132), below the posted-writes image that fitted. Second attempt
  passed.
- Routed resource use: **45,680 / 46,800 ALMs (98 %)**, 3,566,368 /
  7,331,840 block-memory bits, **309 / 358 RAM blocks**, 53 / 376 DSP
  blocks, 5 / 11 PLLs.
- All production clocks are constrained and pass. Worst setup, hold,
  recovery and removal slacks are **+0.045 ns** (`ASTRA_HDMI_PIXEL_CLOCK`),
  **0.000 ns**, **+2.853 ns** and **+0.160 ns**. The 165 MHz graphics clock
  (`iopll_0_outclk1`, Fmax 175.44 MHz) closes at **+0.360 ns**; the 148.5
  MHz pixel clock at +0.924 ns.
- Source SHA-256: command processor
  `b3263210a202c8fb78840d306aa9702424ca0fb536e96bb15073d11e47a311b4`,
  protocol header
  `351501995c2d3ed66c62dccf9488207b74343b0245edd710c44540b663cff1e8`; copy
  mover, pixel writer and blitter unchanged. Build-input checksum manifest
  SHA-256 `80b13cac307ab8a7efa1704de15a5f3578ca1b39eba7964295b9a7f5eeaa6008`;
  boot core RBF SHA-256
  `632b5c73e81aec4df8d1034a95b1b1989f67ada98317b1c2005bada3eff401df`;
  `astra68.hps.jic` SHA-256
  `b14a2e2c7f3d72ad7186230ea135d57db74fc4324352b18f0f6a39f12b6145dc`.
- Simulation evidence (`perf/run_perf.sh`): a 1x1 FILL_RECTS record costs
  46 / 48 / 50 cycles at 25 / 100 / 200-cycle DDR latency against 113 / 116
  / 121 per FILL command; the testdraw2 frame with its points and rectangles
  batched costs 214k / 216k / 219k cycles in 103 commands against 241k /
  243k / 247k in 506.
- Disposition: capacity and timing checkpoint. The DE25 was not programmed;
  POST, storage, graphics, HDMI, repeated boot and `astra-render-certify`
  have not been run on this image. Remaining capacity is 1,120 ALMs by count,
  but the first attempt shows the LAB packing limit is reached near 45,950.

## 2026-09-27: posted render writes (not deployed)

- On `beast`, Quartus Pro 26.1.1 Build 130 completed a full production
  synthesis, fit, route, signoff and programming-file build with
  `build_astra_shell.sh` (`DISPLAY` unset) from an rsync of the Mac working
  tree at `~/astra-mg/rtl-main2`
  (`ASTRA_DE25_BUILD_ROOT=build/de25-render-posted`).
- Source change over the render-perf3 checkpoint below, in
  `fpga/arty/graphics`: engines finish once their last write is issued; every
  engine write uses one AXI ID; the command processor counts write responses
  and keeps completion records in an eight-entry in-order queue, writing each
  record between engine bursts once the writes before it are answered and
  retiring it on its own response; reads (descriptor misses and pixel-reading
  engines) wait for all earlier writes; write errors are charged to the
  issuing command. Ordering rules: `docs/GRAPHICS_ARCHITECTURE.md` §9.
  `run_tests.sh` passes on the same source (19.5 min), including the new
  `perf/run_ordering.sh` bench (writes visible only at their response,
  reordered completion responses, an injected SLVERR).
- First attempt failed in synthesis (no fit): the new fault-detail capture
  indexed `m_axi_bid[5:0]`, but the DE25 instantiates the processor with
  `AXI_ID_WIDTH = 3`. The ID is now zero-extended to eight bits first; an
  iverilog elaboration at width 3 is clean. The second attempt passed.
- Routed resource use: **45,611 / 46,800 ALMs (97 %)**, 3,566,464 /
  7,331,840 block-memory bits, **309 / 358 RAM blocks**, 53 / 376 DSP
  blocks, 5 / 11 PLLs: 141 fewer ALMs and one more RAM block than the
  render-perf3 route (45,752 ALMs).
- All production clocks are constrained and pass. Worst setup, hold,
  recovery and removal slacks are **+0.079 ns** (`ASTRA_HDMI_PIXEL_CLOCK`),
  **0.000 ns**, **+2.146 ns** and **+0.024 ns**. The 165 MHz graphics clock
  (`iopll_0_outclk1`, Fmax 175.25 MHz) closes at **+0.354 ns** (was
  +0.361 ns); its worst path is in the unchanged framebuffer line builder,
  the worst render-processor path (`last_fault_detail` enable) has
  +0.407 ns. The 148.5 MHz pixel clock closes at +1.004 ns.
- Source SHA-256: command processor
  `879c493a1efcef0ce8b0a823f6ceb9e4ebab685e23fff533bee6a0140e53bc03`,
  copy mover `eedb06c5adcadf23c2ee25f9655049c0f7d73c8e1b88725b40258cac39c47b17`,
  pixel writer
  `0cfcb950328c8d0f19faf4d828bde7109fa3a029b9982d1e46ba0814abda176a`,
  blitter `47cf76c348808d2f1ab8996e84fc829cad86719641a376337163d4adf0336705`,
  geometry unchanged. Build-input checksum manifest SHA-256
  `3e6f878b67b593d062f47bf55296714bb720fab4bf486ab5342d653228dfcee9`; boot
  core RBF SHA-256
  `e859b4768af9bd214c18cf5370382e955d84c9f928235ffcfe5c0f2cdc476010`;
  `astra68.hps.jic` SHA-256
  `4cee9f434e7d0bd1e0a7fc3fd1f369cdc82bf9c458d866f06fff9d6891393dac`.
- Simulation evidence (`perf/run_perf.sh`): a small FILL costs 113 / 116 /
  121 cycles at 25 / 100 / 200-cycle DDR latency (render-perf3: 138 / 216 /
  319); one SDL testdraw2 frame 241k / 243k / 247k cycles (render-perf3:
  253k / 289k / 338k).
- Disposition: capacity and timing checkpoint. The DE25 was not programmed;
  POST, storage, graphics, HDMI, repeated boot and `astra-render-certify`
  have not been run on this image.

## 2026-09-27: render transport and burst mover (render-perf3, not deployed)

- On `beast`, Quartus Pro 26.1.1 Build 130 completed a full production
  synthesis, fit, route, signoff and programming-file build with
  `build_astra_shell.sh` from an rsync of the Mac working tree at
  `~/astra-mg/rtl-main` (`ASTRA_DE25_BUILD_ROOT=build/de25-render-perf3`).
  First attempt; no iteration was needed. (A first launch failed before
  synthesis only because `qsys-script` inherited an ssh-forwarded `DISPLAY`;
  run the build with `DISPLAY` unset.)
- Source change, all in `fpga/arty/graphics`: command prefetch (up to 32
  commands per AXI read into a 512x65 M20K), a three-slot surface-descriptor
  cache with the per-doorbell rule of `docs/GRAPHICS_ARCHITECTURE.md` §9,
  posted completion records retired on their write response, skipped
  disabled range-check pairs, a read-ahead burst mover (eight chunk slots in
  M20K) for fills and plain copies, one Bresenham step per clock, and 128
  outstanding pixel-writer writes. The copy mover's AR/AW now stay valid
  until their handshake across an abort (a latent baseline bug: an abort in
  the handshake cycle abandoned an accepted address).
  `fpga/arty/graphics/run_tests.sh` passes on the same source (21 min).
- Routed resource use: **45,752 / 46,800 ALMs (98 %)**, 3,566,272 /
  7,331,840 block-memory bits, **308 / 358 RAM blocks**, 53 / 376 DSP
  blocks, 5 / 11 PLLs. Against the texture-engine route (45,925 ALMs, 304
  RAM blocks) that is **173 fewer ALMs** and 4 more RAM blocks: the new
  mover is smaller than the one it replaces (Quartus OOC: 1,158 vs 1,666
  ALUTs, 847 vs 1,710 registers).
- All production clocks are constrained and pass. Worst setup, hold,
  recovery and removal slacks are **+0.138 ns** (`ASTRA_HDMI_PIXEL_CLOCK`),
  **0.000 ns**, **+2.962 ns** and **+0.002 ns**. The 165 MHz graphics clock
  (`pixel_pll_i|iopll_0_outclk1`, 6.060 ns, Fmax 175.47 MHz) closes at
  **+0.361 ns** (was +0.364 ns); its five worst paths all start in the
  texture engine (`texture_i|vertex_x_q[1][4]`), none in the changed logic.
  The 148.5 MHz pixel clock closes at +1.051 ns.
- Source SHA-256: command processor
  `2b3c1814ff8f2f929bdd8799ee315b034ec6f0c13af6dc7d7e5b41947695a8a6`,
  copy mover `45d0097ddfe90bb336dabd72cda817d4cc2c6dce5f27f6fd81542a34fd8658af`,
  blitter `a87b4589a3885ba9db0127743925a124c1ab901b5232316f81cd17cfd98017d9`,
  geometry `ec933d2488c9d735c97f6e8b142aca5f4bf41ccc2b407941ab7fc903c4c9ba76`,
  texture engine and pipeline unchanged. Build-input checksum manifest
  SHA-256 `fa5cf2aa3dd1c907759559a5a47dfadbd7de36203f54481fdb979e666ff65097`;
  boot core RBF SHA-256
  `4983ff7fb6869a2adb362a511dc74cfd8ac0c447b680304142c8db93218b0b8b`;
  `astra68.hps.jic` SHA-256
  `693346c3b9c0927aa6f98f850582eb8f9c7034e9a0d45e261298a79fc4cd85f9`.
- Simulation evidence (`fpga/arty/graphics/perf/run_perf.sh`, latency model,
  RGB565 640x480): one SDL testdraw2 frame costs 253k / 289k / 338k cycles
  at 25 / 100 / 200-cycle DDR latency, against 1.61M / 1.89M / 3.35M for the
  baseline; fill and plain copy run at 0.30–0.34 cycles per pixel at any of
  those latencies.
- Disposition: capacity and timing checkpoint. The DE25 was not programmed;
  POST, storage, graphics, HDMI, repeated boot and `astra-render-certify`
  have not been run on this image.

## 2026-09-26: texture engine (TRIANGLES) and ARGB8888 destinations (not deployed)

- On `beast`, Quartus Pro 26.1.1 Build 130 completed a full production
  synthesis, fit, route, signoff and programming-file build in
  `build/de25-texture-engine/astra-shell` (`build_astra_shell.sh`,
  `ASTRA_DE25_BUILD_ROOT=build/de25-texture-engine`). The source adds
  `astra_render_texture.sv` (docs/TEXTURE_ENGINE.md) behind the command
  processor, ARGB8888 destinations for geometry, flood and glyph, and
  straight-alpha `FLAG_BLIT_ALPHA`. The full graphics suite (including the
  194-case model-checked texture bench) passes on the same source. First
  attempt; no iteration was needed.
- Routed resource use: **45,925 / 46,800 ALMs (98 %)**, 3,516,608 /
  7,331,840 block-memory bits, **304 / 358 RAM blocks**, **53 / 376 DSP
  blocks**, 5 / 11 PLLs. The texture engine alone is about 5,006 ALMs
  (+4,848 ALMs, +2 RAM blocks, +7 DSP blocks over the tile-control-free
  route). Only **875 ALMs** remain: the next FPGA feature must first reclaim
  logic. The engine's largest cost is six parallel 50-bit attribute
  steppers; time-multiplexing them through one adder is the known reduction.
- All production clocks are constrained and pass; no failing path. Worst
  setup, hold, recovery, removal and minimum-pulse slacks are **+0.051 ns**
  (`ASTRA_HDMI_PIXEL_CLOCK`, output path), **0.000 ns**, **+2.967 ns**,
  **+0.092 ns** and **+0.220 ns**. The 165 MHz graphics clock
  (`pixel_pll_i|iopll_0_outclk1`, 6.060 ns) closes at **+0.364 ns** (was
  +0.801 ns); its worst path starts in the texture engine's shared multiplier
  bank (`texture_i|bank_a_q[4]`). The 148.5 MHz pixel clock closes at
  +0.937 ns.
- Source SHA-256: texture engine
  `126c8eed22b639b6e13f89d9b5c59991c7797ee643f594683a4ddf4f8e7de554`,
  command processor
  `2f024a5f02674794bae4b72a9a16377243f4d1e37123252eedf9ed1a93cd663b`,
  blitter `9bd9f6b9a68634fc719bb940e4bb49086c9028eeb8fe5f1bc408160a153297be`,
  glyph `2c91d78bc3d2bffda52dd9ec5adc1938cb23665e37b5caf14fbbb8e2003dd2b3`,
  pipeline (unchanged)
  `d7252bdf4751ad403bc5de78e84713701f02dc16452f89238f227fe506b81ba6`.
  Build-input checksum manifest SHA-256 is
  `82a2dc1df9e2cf3e0ac4889d8cc906c8fadd41aac70677c4e503ff72da948c4f`; boot
  core RBF SHA-256 is
  `3325d8ffbe45686bf28ea3720487953692fe3d5f38d4a0e7376008ff9ff00ca9`.
- Disposition: capacity and timing checkpoint only. The DE25 was not
  programmed; POST, storage, graphics, HDMI, repeated boot and
  `astra-render-certify` (`ASTRA_TEXTURE PASS`) have not been run on this
  image.

## 2026-09-25: retired tile controls and scene arbiter (not deployed)

- On `beast`, Quartus Pro 26.1.1 Build 130 completed a full production
  synthesis, fit, route, signoff, and programming-file build from the exact
  tile-control-free source in `build/de25-sdl-no-tile-controls/astra-shell`.
  The graphics suite, DE25 build contract, ARM host binaries, and Linux host
  tests pass. Tile register writes now return SLVERR, tile register reads
  return DECERR, retired Copper targets are rejected, and the sprite AXI
  client connects directly to the scene bridge. This is an isolated capacity
  checkpoint, **not** a deployed or hardware-qualified release.
- Routed resource use: **41,077 / 46,800 ALMs**, **3,516,472 / 7,331,840
  block-memory bits**, **302 / 358 RAM blocks**, **46 / 376 DSP blocks**, and
  **5 / 11 PLLs**. Remaining nominal capacity is 5,723 ALMs, 56 RAM blocks,
  and 330 DSP blocks; fit and timing, not raw counts, remain the acceptance
  gate for any SDL datapath.
- All production clocks are constrained and timing passes. Worst setup, hold,
  recovery, removal, and minimum-pulse slacks are **+0.012 ns**, **0.000 ns**,
  **+2.732 ns**, **+0.041 ns**, and **+0.220 ns**. No failed timing cone. The
  worst setup path is `graphics_i|hdmi_tx_d[22]` to `HDMI_TX_D[22]` on
  `ASTRA_HDMI_PIXEL_CLOCK`; the 165 MHz graphics clock's setup slack is
  +0.801 ns. The tiny HDMI output margin means future FPGA work still needs
  an exact full route, even if it does not touch scanout logic.
- Source SHA-256: pipeline
  `d7252bdf4751ad403bc5de78e84713701f02dc16452f89238f227fe506b81ba6`,
  control `5c21c22fd743ccd7233d327c78d21067a5b6599ac83190650b237cdca1e2daf1`,
  Copper registers
  `5f093a892e9b7d4aaec3d02c2e37fe0a0061a6c49221dad4c2e881bc1983e22f`,
  Copper structural state
  `bf432bfcb8f4a55c521bf3130049833bc0760c4b642cd8f5a3c1c9a23ae7872c`,
  scheduler `b011ca1074fb2b3f11dd56cf611791d56e13052acae9ca9e5567c9fc1a21b251`,
  and DE25 top
  `505770f8b70cf7dd72cff11d3e7c35286d6d63e4aeae91e49ffd25f11c00cb3c`.
  Full build-input checksum manifest SHA-256 is
  `76a4ffc0a20170a37c6f8387fef9f25ae8f9b4459766d27a56ba6602fbd1617a`;
  boot core RBF SHA-256 is
  `da1ecee8a62ef96ab898e07d992b27528c08d0784decd61240a6b32da18150ed`.
  The DE25 remains on the previous release; POST, storage, graphics, HDMI,
  and repeated boot gates have not been run on this image.

## 2026-09-25: tile-free compositor and palette capacity route (not deployed)

- On `beast`, Quartus Pro 26.1.1 Build 130 completed the isolated full
  production build in `build/de25-sdl-final-cut/astra-shell`. This source has
  no instantiated tile-line builders, no tile palette storage, and a
  framebuffer/sprite-only three-stage pixel compositor. The integrated
  framebuffer scroll/wrap tests and exact-source full graphics suite pass.
  Tile control/Copper contracts
  are not yet retired, so this is **not** a releasable image.
- The full route uses 42,158 / 46,800 ALMs, 3,516,488 / 7,331,840 block
  bits, 302 / 358 RAM blocks, and 46 / 376 DSP blocks. Setup, hold, recovery,
  removal, and minimum-pulse timing all pass, with worst slacks +0.054 ns,
  0.000 ns, +3.247 ns, +0.027 ns, and +0.220 ns. That leaves 4,642 ALMs
  and 56 RAM blocks before device limits; it does **not** prove the remaining
  SDL2 rendering operations will fit or meet timing.
- Pipeline, palette, and compositor source SHA-256 values are
  `e99efd188105ecea3cbbc42ac54b8a9b09dd7613a901b46ab1e3ed9216177585`,
  `4c011f3047efe3df0f68e997b1183e0ffcde97371b14354e1ee9af9c509a88ae`,
  and `bd3bcd6bca4dcaaead2a4ae2a13dd0a4001e723db9765a5d0307087b87946bf5`.
  Full build-input checksum manifest SHA-256 is
  `f4f6bd892e5485fd40c863b25cf372e3eb30b6e92dade8213f8d6db86dff37ef`;
  RBF SHA-256 is
  `8558da2a65bee69ee24c85e2310955139f78dede3794a5e1e8696ac23cb1947f`.
  The DE25 was not changed; hardware POST and visual gates remain open.

## 2026-09-25: framebuffer-only palette capacity experiment (not deployed)

- On `beast`, Quartus Pro 26.1.1 Build 130 completed a full isolated
  production route in `build/de25-sdl-nopalette/astra-shell`. This source
  removed the 4096-entry tile-palette baseline/active storage and restores
  only the 256 framebuffer entries. The focused palette test passed host and
  Copper writes, restore/backpressure, and the 1500-clock upper bound; restore
  took 781 control clocks versus 13,068 in the previous tile-palette test.
  Removed tile-palette MMIO writes and Copper target validation are negative
  tested. The later three-stage compositor/prefetch cut is **not** in this
  artifact and requires its own route.
- Full-route resources are 42,241 / 46,800 ALMs, 3,516,488 / 7,331,840
  block bits, 302 / 358 RAM blocks, and 46 / 376 DSP blocks. Worst setup,
  hold, recovery, removal, and minimum-pulse slack are +0.087 ns, 0.000 ns,
  +3.039 ns, +0.080 ns, and +0.220 ns. Against the preceding explicit
  tile-builder-removal route, that is 185 fewer ALMs and eight fewer RAM
  blocks; the two fitter results are separate complete routes.
- Pipeline, palette, and compositor source SHA-256 values in this build are
  `7d47f05cdc44339050bb1633c409d50d9d450b6ae4221f11dc45d7bad8649d7c`,
  `4c011f3047efe3df0f68e997b1183e0ffcde97371b14354e1ee9af9c509a88ae`,
  and `0905f1a7a08231a97f8451c38b668076cafc8fdcb9aa83228048100439b491a2`.
  Full build-input checksum manifest SHA-256 is
  `177b075a9dfc3b359c0ee0426060702bdd37aed859ce3b6563b68251331d0c18`;
  RBF SHA-256 is
  `6b419a87b74ae63ac8629f052fbc0f8fd9f015668179480f2ef19b1563fa32a6`.
  No DE25 install or hardware release gate was performed.

## 2026-09-25: explicit tile-builder removal (not deployed)

- On `beast`, Quartus Pro 26.1.1 Build 130 completed the entire production
  build in isolated `build/de25-sdl-notile-v2/astra-shell`. The source now has
  no tile-line-builder instances. Its framebuffer builder still receives
  independent viewport X/Y and wrap X/Y controls; the integrated scroll/wrap
  simulation and no-tile-AXI negative check pass. The full Beast graphics
  simulation suite exits zero against this source revision, including the
  1920-pixel integrated pipeline and render-command processor.
- Full-route resources: 42,426 / 46,800 ALMs, 3,647,576 / 7,331,840 block
  bits, 310 / 358 RAM blocks, and 46 / 376 DSP blocks. Worst setup, hold,
  recovery, removal, and minimum-pulse slack are +0.079 ns, 0.000 ns,
  +2.711 ns, +0.039 ns, and +0.220 ns. The 166-ALM difference from the
  tied-off predecessor is fitter variation; RAM use is unchanged.
- Pipeline SHA-256 is
  `06bebbad4638df13c57ba32f48583f1fd27dcba248c12f96602874222348f707`;
  full build-input checksum manifest SHA-256 is
  `d7217f009489b03293e0e581276aaa9bfb6315dd933ebd93ec9e54c3a8583e5c`;
  published RBF SHA-256 is
  `c19ae261997d5e37930948aa27acae09fbe9020a86cb881723715dc589b9b108`.
  This is an isolated capacity checkpoint, not a release: tile palette and
  low-level control/Copper contracts remain, SDL rendering is not yet
  hardware-backed, and the DE25 hardware release gate has not been run.

## 2026-09-25: tile-disabled SDL capacity experiment (not deployed)

- On `beast`, Quartus Pro 26.1.1 Build 130 completed the full production
  synthesis, fit, route, signoff, assembly, checksum, and publication flow in
  isolated `build/de25-sdl-audit/astra-shell`. The experimental pipeline ties
  both tile builders and tile compositor inputs off while leaving framebuffer
  viewport X/Y scrolling and wrapping active. The integrated graphics test
  checks scrolled/wrapped pixels and rejects tile AXI traffic with both legacy
  tile-enable controls set; the complete graphics simulation suite passes.
- Full-route resources: 42,260 / 46,800 ALMs; 3,647,576 / 7,331,840 block
  bits; 310 / 358 RAM blocks; 46 / 376 DSP blocks. Worst setup, hold,
  recovery, removal, and minimum-pulse slack: +0.078 ns, 0.000 ns, +2.974 ns,
  +0.002 ns, and +0.220 ns. Relative to the latest documented active capture
  route (45,797 ALMs, 354 RAM blocks), this experiment has 3,537 fewer ALMs
  and 44 fewer RAM blocks; it is not a controlled single-change comparison
  against that historical source revision.
- Source identity: pipeline SHA-256
  `32b82112177989ef2f9b20f6e78df46f3dcaa4e71c31bca9e52cfdca2a89aea8`;
  full build-input checksum manifest SHA-256
  `19496e9574feac6bdaad4735014e0e4c278ecf37234e1cc16573b6e79294ede6`.
  Published RBF SHA-256 is
  `867b7314a328437a3d0b2ad546c5edbb753293f4091b378ecb36f8bddc2347d6`.
  The build was not installed on the DE25: tile control/register and software
  contracts still need coherent retirement, and the SDL renderer additions
  still need a separate full-route and hardware release gate.

## 2026-09-14: native-width framebuffer deadline closure

- The preceding 64-bit scene-reader route was not physically acceptable.
  Its ideal three-span 1920-pixel row took 2,234 of the 2,444 available build
  clocks, and Terminal exposed missed rows as a repeated striped line. A
  focused regression also proved that scanline replay treated an unavailable
  source row as valid scaling input. Requiring explicit source availability
  removes that stale-row interpretation; a second consecutive miss now
  produces an unavailable row rather than unbounded replay.
- The remaining black band was a real construction deadline miss, not bad
  source pixels, scene data, commit ordering, or Media RAM overlap. The DE25
  LPDDR4B controller is natively 128-bit, but the framebuffer bridge narrowed
  it to 64 bits while the builder and line store serialized one pixel per
  165 MHz clock. The retained path uses a native 128-bit framebuffer AXI
  manager and two parity-banked line-store writes per clock. Scene and render
  managers remain 64-bit because they do not share this scanline deadline.
- The target-clock regression requires the steady production-shaped row to
  finish within 1,500 clocks, preserving 944 clocks (38.6%) of the physical
  line for real LPDDR latency and concurrent traffic. The old implementation
  failed at 2,234 clocks. The retained implementation passes at 1,164 clocks,
  3,856 transferred bytes, and 12 physical AXI requests for the measured row.
  Bootstrap takes 1,198 clocks. Both 64- and 128-bit suites pass exact pixel
  checks for INDEX8, RGB565, XRGB8888, unaligned starts, clipping, wrapping,
  4 KiB splits, malformed responses, scene validation, timeout drain/reuse,
  and both halves of a 128-bit line-table beat. The 1920-wide integrated
  pipeline, scheduler, replay, and DE25 shell-contract tests also pass.
- A clean full production build on `beast` with Quartus Pro 26.1.1 Build 130
  passed platform qualification, synthesis, fit, final multicorner timing,
  Design Assistant, assembly, checksum verification, and atomic publication.
  Every constrained clock has zero setup and hold failing endpoints. Worst
  setup, hold, recovery, removal, and minimum-pulse slack are +0.076 ns,
  0.000 ns, +2.518 ns, +0.006 ns, and +0.220 ns. Resources are 45,080 / 46,800
  ALMs (96%); 82,135 registers; 4,189,680 / 7,331,840 block-memory bits (57%);
  342 / 358 RAM blocks (96%); 60 / 376 DSP blocks (16%); and 5 / 11 PLLs
  (45%).
- Relevant source SHA-256 values are
  `a40d0bd714e268ea56a55f423a616840a1eeeed2e85b3d12e6b9fd972b7de3cf`
  (`add_lpddr4b.tcl`),
  `27fd909096cf7ccf8a85da0837dbfba54d0c3c9bcf8d131afeffbeab3cf658fe`
  (`astra_de25_graphics.sv`),
  `1f1117b380239a09925845daa044dd8a1a44a90edabf2c79e06382a2b88f9f3f`
  (`astra_graphics_pipeline.sv`),
  `fe6059dd3ad304a0c3f4935f648439c837845ec5f71c135ef881d51de4c36108`
  (`astra_framebuffer_line_builder.sv`),
  `ec29e5b1a86838a29f4d54eaec179ad8df610497a125af723b4726740d74fa2e`
  (`astra_framebuffer_line_store.sv`), and
  `637dd9bf35c37ca33abb42c2b126eb00d32061597634e39120f77a5df88477a3`
  (`astra_scanline_replay.sv`). Published artifact SHA-256 values are
  `99c03a1487863bfe84b4f793a75bde2b8b6bfd3c1f3c3fd1e73023bec5204b62`
  (`golden_top_boot.core.rbf`),
  `b4488ef352786cbd7f974cd176d26e9d107eb2ea093d268950b51d171f77e995`
  (`astra68.hps.jic`),
  `87863a17edde9ddfec7f302b5f1f2dc9232ddedcc083dee86be36b96702dd276`
  (`golden_top.sof`), and
  `327df009545cc50f1533f1ca64a23055143534287e248d9e3ee7d0642b82702e`
  (`golden_top_hps.sof`).
- The checksum-qualified bundle was atomically installed and cold-booted.
  POST, storage verification, kernel startup, required services, and stage 8
  passed with zero service restart. Physical Cam Link evidence includes a
  clean idle frame, a clean active Terminal frame, 901 frames across repeated
  Terminal output/scrolling, and 481 frames during injected pointer/drag input.
  Black-band detection over regions outside the moving window found zero
  events, and both contact sheets contain no striped, repeated, or black
  scanlines. The retained captures are
  `/private/tmp/astra-terminal-activity-99c03a14.mp4` (SHA-256
  `a28f2bd81ec6ad701182f247e18710a96f453710c1a649def66ea268e3a64576`)
  and `/private/tmp/astra-terminal-drag-99c03a14.mp4` (SHA-256
  `67ffeace60c555b4e1b0ab0d9fd86a39e2ac1f65c6e33d6feef82ef8db00750c`).
  This is the retained production display checkpoint.

## 2026-09-14: window-scene scanline deadline correction

- The physical black band was localized to the scanline scheduler replaying
  its previous valid slot after the compiled window-scene builder missed the
  next native-line deadline. The existing scheduler counter and behavior
  matched the observed stale rows; source surfaces and compiled spans were
  independently dumped and were correct.
- A target-clock regression now runs the production 1920-pixel, three-span
  scene against the real 2,444-cycle native line budget derived from 2,200
  pixel clocks at 148.500 MHz and a 165 MHz build clock. The unmodified path
  failed at 2,464 cycles, 3,904 bytes, and 35 AXI requests per bootstrap row.
- The retained correction caches the already validated immutable scene header
  until the existing atomic `scene_changed` event and uses the DE25 HPS AXI4
  bridge's supported 32-beat bursts. Header caching alone measured 2,412
  cycles and was rejected as insufficient margin. Together, the bootstrap row
  measures 2,284 cycles and the steady row measures 2,234 cycles, 3,840 bytes,
  and 19 requests: 210 cycles (8.6%) of native-line headroom.
- The default 16-beat builder contract remains covered for the Arty/shared
  configuration. The complete graphics simulation suite and DE25 shell and
  boot contract tests pass.
- The exact full production route passed synthesis, fit, final multicorner
  timing, Design Assistant, assembly, programming-file generation, checksum
  verification, and atomic publication on `beast` with Quartus Pro 26.1.1.
  Every constrained clock has zero setup and hold failing endpoints. Worst
  setup, hold, recovery, removal, and minimum-pulse slack are +0.054 ns,
  0.000 ns, +2.563 ns, +0.064 ns, and +0.220 ns. The external HDMI pixel
  interface closes at +0.054 ns setup/+0.003 ns hold; the internal 148.500 MHz
  pixel clock closes at +0.074 ns setup/+0.017 ns hold. Design Assistant has
  zero unwaived high-severity violations.
- Resources are 44,468 / 46,800 ALMs (95%); 81,747 registers; 4,185,592 /
  7,331,840 block-memory bits (57%); 340 / 358 RAM blocks (95%); 60 / 376 DSP
  blocks (16%); and 5 / 11 PLLs (45%). Relevant source SHA-256 identities are
  `11fb776f5020f5447195dc83b3b6890f194b3132bc0cf04f6772f1498d431dbc`
  (`astra_de25_graphics.sv`),
  `770bebada6e929189398725032fdf49397f7cf0053ea9d80bb39b9c097f4708d`
  (`astra_graphics_pipeline.sv`), and
  `b858033439ea55c1f906bee6b1c0d0db2d7f0cefd5995fa4c6d4d9749b0836be`
  (`astra_framebuffer_line_builder.sv`).
- Published artifact SHA-256 values are
  `7fa3f05f515df6ccd68c19acc33f65c1b6febc2cb08d88be5bff71f137817677`
  (`golden_top_boot.core.rbf`),
  `3942be2d781599bb1bbf70f1e143cbd55acb8fde712cb0267b91aa973964f0f6`
  (`astra68.hps.jic`),
  `c02e70cc8f6c01ca45c80c2d0413678107ec2567d313c08094963492d1c37a99`
  (`golden_top.sof`), and
  `bcd2130e03a0957ab553d411cb040546a5efe847f3a946dc017dcd7c626b2a9f`
  (`golden_top_hps.sof`). Physical HDMI acceptance remains pending; no prior
  bitstream is accepted for this correction.

## 2026-09-14: retained-window-scene production route

- Host/tool: `beast`, Quartus Pro 26.1.1 Build 130; exact mirrored source,
  complete production feature set, and the pinned vendor inputs whose SHA-256
  values are `554af27603855b37840e930bd075c34141c85d2f753cfa45df2eb5114f0f4c73`
  for the DE25 resource package and
  `5da65306fbb1689fb12c9d4bd3721e6be683192e899315df55718eb5df6083e5`
  for the GHRD archive.
- Architecture: the display service retains immutable window presentation
  surfaces and submits an ordered scene instead of rebuilding a full-screen
  scanout for pure movement. The host validates and compiles the scene into
  bounded per-line span records in Media RAM. The existing hardware line
  builder consumes those records without changing the VFS, application draw,
  window ownership, or atomic-vblank presentation contracts.
- Result: the complete graphics regression, synthesis, fit, route, final
  multicorner timing, Design Assistant, assembly, programming-file generation,
  staged checksum verification, and atomic publication all passed. Every
  constrained clock has zero setup and hold failing endpoints. Worst setup,
  hold, recovery, removal, and minimum-pulse slack are +0.046 ns, 0.000 ns,
  +1.673 ns, +0.026 ns, and +0.220 ns. The external HDMI pixel interface closes
  at +0.046 ns setup and +0.004 ns hold; its internal 148.500 MHz pixel clock
  closes at +0.605 ns setup and +0.024 ns hold.
- Design Assistant: zero unwaived high-severity violations.
- Resources: 44,480 / 46,800 ALMs (95%); 81,591 registers; 4,183,544 /
  7,331,840 block-memory bits (57%); 340 / 358 RAM blocks (95%); 60 / 376 DSP
  blocks (16%); and 5 / 11 PLLs (45%).
- Relevant source SHA-256 identities are
  `dfb00375ac13cf341c843bc6da878ad25236cc6bcd0caf8e5a8b152cb21db341`
  (`astra_de25_graphics.sv`),
  `174c68a8d9fd642343171266bb39d92cfefe6b45dd54d5ad7bd978f3de84d22c`
  (`astra_graphics_pipeline.sv`),
  `f60da815b148c54983fcc3088ecbfcace9f0135fb21151a7336bbf315921e4cd`
  (`astra_graphics_control.sv`),
  `4eae13c7a1e6d7b1e4b33f2448240036d9903e81e6322f6745e1af612f9bcfea`
  (`astra_framebuffer_line_builder.sv`), and
  `c1c02f32c4599967cbc57bfb4e27a3ed3f9879d6632a146e40cf2334b2481dc0`
  (`astra_window_scene_protocol.vh`).
- Published artifact SHA-256 values are
  `0b56b9ff5a05802697f3b5ba91eed26e3aae48b5b6938fe06d22d62a1ec525d7`
  (`golden_top_boot.core.rbf`),
  `b9414911d9c0aa07258c8c3f11035f3662b091c77e271c6b9491cf9778ed15e2`
  (`astra68.hps.jic`),
  `6715c9308fda511fa1609fba6c41679aba99dacaaa475dbb71f2b3a5e69c014b`
  (`golden_top.sof`), and
  `d4fe8930189ba3e2cd1e4c86744b3586f6fe3e0feff31544e93c8f2e94aff3a4`
  (`golden_top_hps.sof`).
- Disposition: retained as the routed production checkpoint. Physical HDMI,
  drag-rate, pointer responsiveness, and immutable-release acceptance remain
  required before it becomes the deployed production release.

## 2026-09-10: scene epoch route and compositor surface correction

- The retained full production route adds an explicit pixel/build scene-epoch
  acknowledge and registers the boot-overlay glyph lookup pipeline. Focused
  scheduler, pipeline, and overlay simulations pass. Quartus Pro 26.1.1 on
  `beast` completed the exact full route with setup +0.009 ns, hold 0.000 ns,
  recovery +2.479 ns, removal +0.032 ns, and minimum pulse width +0.220 ns.
  Resources are 43,611 / 46,800 ALMs and 340 / 358 RAM blocks. The epoch
  handshake is retained as the correct scene CDC contract, but physical HDMI
  measurement proved it was not the black-band root cause.
- Measured pre-fix frames alternated at the 500 ms cursor period between a
  clean desktop and a full-width black band at y=580..781. The geometry led to
  the display service's fixed media map: its 1920x1004 RGB565 desktop needs
  3,855,360 bytes, but window content slots were only 2 MiB apart. Terminal's
  772,800-byte black content surface therefore began 2,097,152 bytes into the
  desktop, exactly at desktop row 546 / screen y=580, and overwrote through
  screen y=781.
- The correction uses the existing 4 MiB native scanout stride as the single
  surface-slot contract. Scanouts, render workspace, window caches, and window
  content now have compile-time non-overlap proofs. The shared render builder
  requires external surfaces to declare slot capacity and rejects oversized
  registrations. Its temporary surfaces also start after the 256 KiB render
  batch; a focused regression caught that latent overlap. Graphics and display
  tests pass normally and under ASan/UBSan.
  This describes that retained release; the current builder supersedes the
  fixed split with opposing allocators across the physical 8 MiB workspace.
- Immutable release
  `b20178fb3f52a997bbcde9501fb435867d95cc3e25c232c51c764306a3f26066`
  verified before and after installation, reached stage 8, and remained active
  with the real Terminal. The display service image SHA-256 is
  `aad710b5adb0e9b4418934fad958e4a888d1a8761b97a81983938e64a5632ba8`;
  the AArch64 renderer is
  `fe1dcc00777894c5ca8515705ccbe0e7d852687e2da47c98c16816e9f45c3943`.
  An eight-second physical HDMI capture covered 463 frames and multiple cursor
  transitions: the former band crop was identical in every frame at YAVG
  35.9862 while the cursor crop varied from 16.0 to 39.0451. After dragging the
  Terminal, a second 300-frame capture kept the exposed band region at YAVG
  39.0 with zero black frames. Capture SHA-256 values are respectively
  `022e455481e1d8af287775820de8f995c062d39e62e5b3263c92d633fbf32272`
  and `23e1a4b39ff733aaccb93d33df64ec0c72414cd615eb12f994486b400ddc5fe3`.
  This closes the cursor-blink/window-drag corruption as a software ownership
  defect; no masking, reduced feature set, or timing exception is involved.

## 2026-09-09: complete 1080p scaler/pointer release route

- Host/tool: `beast`, Quartus Pro 26.1.1 Build 130; exact mirrored source,
  complete production feature set, and pinned vendor inputs from
  `fpga/de25/platform.json`.
- Architecture: fixed VIC 16 1920x1080x60 output at 148.500 MHz; hardware
  nearest-neighbor logical scaling and composed-scanline replay; copper sees
  the logical beam; framebuffer, tiles, sprites, copper, and boot overlay share
  the scaled plane; the double-buffered 32x32 ARGB pointer remains a native,
  unscaled output plane. Display configuration crosses from the build clock as
  an atomic request/acknowledge snapshot and becomes active only at physical
  vertical blank.
- Result: the complete graphics regression, synthesis, fit, route, final
  multicorner timing, Design Assistant, assembly, programming-file generation,
  staged checksum verification, and atomic publication all passed. Every
  constrained clock has zero failing endpoints and zero TNS. Worst setup,
  hold, recovery, removal, and minimum-pulse slack are +0.035 ns, 0.000 ns,
  +2.826 ns, +0.036 ns, and +0.220 ns.
- Design Assistant: zero unwaived high-severity violations in the final
  snapshot. The remaining medium findings are 67 underutilized-RAM notices,
  two reset-synchronization notices, one clock-target notice, one partial
  min/max-delay notice, and one ignored/overridden-constraint notice; no broad
  display or pointer CDC exception is present.
- Resources: 43,495 / 46,800 ALMs (93%); 80,964 registers; 4,183,520 /
  7,331,840 block-memory bits (57%); 340 / 358 RAM blocks (95%); 59 / 376 DSP
  blocks (16%); 5 / 11 PLLs (45%).
- Routed source identities: `astra_de25_graphics.sv`
  `dfb00375ac13cf341c843bc6da878ad25236cc6bcd0caf8e5a8b152cb21db341`,
  `astra_graphics_pipeline.sv`
  `efda442d6b6a268a7558cf1fb4446f0f4dd47a68220f9afc756b78824b091e26`,
  `astra_graphics_control.sv`
  `253e0fb2db9a20a3f1f95867aef25bf9dfd2b0475673893270ec6e5f9f272bbd`,
  `astra_display_axis_scaler.sv`
  `2143ae8974a9d1fa08de178cd955e44618f59059e4ae0f435cd0f130380b6e5f`,
  `astra_scanline_replay.sv`
  `b1cf424fb4b7b5a1a91b099eb333c36575268e1c77247da924ad3ed834659f08`,
  and `astra_hardware_pointer.sv`
  `63aebe147931f24a7241e421f7441d6030fd05a08e964f73ce218fcec1edce96`.
- Retained manifest identity: `BUILD_SHA256SUMS`
  `df405e0d3ab6a38c7d56db12181ad63b079c7455bf71e89740b9fc0bd1308294`.
  Retained output identities: `golden_top.sof`
  `02d49a23480704d47359a0323a59d019b1d2038e315728b258a2a7391efff23a`,
  `golden_top_boot.core.rbf`
  `cd4f8eedd3bb14a03d929a21767582d71aaa43c5f1d1758741985bb9a7518902`,
  and `astra68.hps.jic`
  `823969d7d4037b5d46a9a81bd8e9ab594bc2537c3bbbd6ddd012e954c8a2b7ad`.
- Disposition: retained as the production implementation checkpoint. Physical
  cold boot and exact JTAG `design_link` passed. Native 1920x1080 presentation
  passed with capabilities `0x00000fff`, zero commit deferrals, and verified
  4,147,200-byte splash CRC32 `639de5a9`. The complete renderer suite passed;
  320x200 hardware presentation selected the expected 5x integer viewport at
  `160,40+1600x1000` and restored the prior scene. The complete 64-sprite
  variable-geometry sweep passed with zero AXI and deadline errors; copper,
  HDMI audio at 48 kHz, and POST status updates also passed. Linux remained
  healthy throughout. The normal runtime was then restored and reached stage
  8 at 70.192 MHz effective with the correct wall clock.

The sprite release gate exposed and localized a separate control-path defect.
An instrumented physical run completed stress, hidden, clipping, and grid
phases, then Linux reported an asynchronous HPS SError at the instruction after
the sprite-scene commit write and panicked. Symbolized AArch64 code placed the
fault in `astra_graphics_scene_commit`, before its status read. PMON recorded
exactly 213,760 accepted and completed AR, R, AW, W, and B transactions with
zero overflow, proving the Media RAM transfer itself completed.

The root cause was a stale scene validator limit of 1,024 destination pixels.
The descriptor ABI has 11-bit destination extents and therefore supports 1..2047;
the certifier's first 1920x1080 dimension phase was rejected. The control block
then translated that recoverable semantic rejection into AXI `SLVERR`, which
the Agilex HPS delivered as a fatal asynchronous SError. The shared ABI limit
is now 2,047, the redundant RTL limit is removed, and exhaustive simulation
passes all 262,016 source/destination scale pairs plus native 1920x1080 and
2047x2047 boundaries. Semantic commit rejection now increments the existing
commit-error counter and returns AXI `OKAY`; true transport, decode, and
alignment faults retain AXI error responses. This is a permanent HPS boundary
rule: software-recoverable validation failures must never be encoded as bus
faults. The replacement full route and physical sprite rerun both pass, closing
this release gate.

The physical investigation also replaced byte-at-a-time Media RAM verification
with the shared explicit 64-bit device-memory copy primitive. Generated AArch64
contains direct `ldr`/`str` operations and no libc or `DC ZVA`; PMON transaction
counts fell from 1,406,336 to 213,760. This was not the panic root cause, but it
is retained as the correct common device-memory path.

Failed measured experiments retained:

1. The first complete implementation inferred both pointer image banks as
   registers because the conditional read prevented block-RAM inference. Fit
   rejected 8,205 required LABs against 4,680 available; the pointer alone used
   24,312 logic cells and 66,134 registers. A synchronous read from both banks
   followed by a registered mux now infers 65,536 block-memory bits.
2. The corrected pointer-RAM route met timing but final Design Assistant found
   two pointer-toggle CDC violations and two display-configuration CDC
   violations. The pointer synchronizer reset was made asynchronous, matching
   the established safe toggle protocol, and the structural backdrop source
   was corrected to the baseline rather than live copper-mutated state.
3. The next exact route met timing but retained one optimized display backdrop
   crossing despite preservation attributes. More importantly, the continuously
   sampled 71-bit configuration bus could tear. Attributes were rejected as a
   workaround; the final implementation uses the atomic held snapshot protocol
   described above and closes every high-severity CDC rule.
4. A requested 150 MHz build-clock experiment generated 148.500 MHz while
   Platform Designer still described 150 MHz. It was rejected: requested PLL
   values are not clock evidence. Generated PLL output, Platform Designer
   metadata, RTL timing parameters, TimeQuest clocks, and physical behavior
   must agree before a build can be retained.
5. The raw HDMI configuration `READY` bit was deliberately synchronized into
   the build domain, but TimeQuest still timed the asynchronous source to the
   first synchronizer stage and reported impossible setup/hold failures. A
   surgical false path covers only that structurally verified source-to-first-
   stage arc. No broad clock or hierarchy exception is permitted.
6. The renderer deadline multiplier still used the old 200-cycles/us default
   after the build PLL changed. Forwarding the exact 165-cycles/us parameter
   removed the false timing pressure. Every elapsed-time constant must come
   from the actual domain clock; duplicated clock assumptions are release
   defects.

## 2026-09-09: native 1920x1080x60 source-synchronous closure

- Host/tool: `beast`, Quartus Pro 26.1.1 Build 130; complete production
  feature set and the pinned vendor inputs from `fpga/de25/platform.json`.
- Change: launch HDMI D, DE, HS, and VS on the preceding rising edge instead
  of the falling edge. This gives the ADV7513 input a full 6.734 ns pixel
  period without changing its rising-edge capture contract.
- Result: synthesis, fit, route, Timing Analyzer, assembly, and atomic
  publication all passed. Every constrained clock has zero setup and hold
  failing endpoints. The 148.500 MHz internal pixel clock closes at +0.057 ns
  setup/+0.015 ns hold; the external HDMI interface closes at +0.120 ns
  setup/+0.001 ns hold.
- Resources: 42,149 / 46,800 ALMs; 78,611 registers; 4,060,120 / 7,331,840
  block-memory bits; 290 / 358 RAM blocks; 58 / 376 DSP blocks; 5 / 11 PLLs.
- Routed source identities: `astra_de25_graphics.sv`
  `369237922c8ec2271950b1f678f16752d5e988945183f286513aaa588f86603b`,
  `astra_graphics_pipeline.sv`
  `8f273c611d247948fca6f38dea23e54a897926f4a73dc7d7fe0ae7fde799005c`,
  `astra_graphics_control.sv`
  `63086d9a53c572df90b8898af5f50fe92fa43f3df75f4661b0c68d26b5dfe37e`,
  and `astra_display_axis_scaler.sv`
  `2143ae8974a9d1fa08de178cd955e44618f59059e4ae0f435cd0f130380b6e5f`.
- Published `golden_top.sof` SHA-256:
  `9eaf00becf84d380a9d822e48226b30db291a1b1b438e718844e078071a61669`.
- Disposition: retained as the first timing-clean native mode checkpoint. It
  is not the final scaler release because the composed-line replay and
  hardware-pointer plane were not in this route.

## 2026-09-09: first native 1920x1080x60 route

- Host/tool: `beast`, Quartus Pro 26.1.1 Build 130; exact pinned vendor GHRD
  and DE25 resource archives from `fpga/de25/platform.json`.
- Change: VIC 16 timing at 2200x1125 and a verified 148.500 MHz HDMI pixel
  PLL. The complete graphics feature set was retained.
- Result: synthesis, fit, route, Timing Analyzer, and assembly completed. The
  internal 148.500 MHz pixel domain closed with +0.138 ns setup slack, proving
  the compositor and line-store path can sustain one native pixel per clock.
- Resources: 42,089 / 46,800 ALMs; 77,996 registers; 4,060,120 / 7,331,840
  block-memory bits; 290 / 358 RAM blocks; 58 / 376 DSP blocks; 5 / 11 PLLs.
- Release disposition: rejected. The 27 source-synchronous HDMI outputs missed
  setup by -2.239 ns (TNS -54.875 ns). Every failing endpoint was an HDMI data,
  DE, HS, or VS pin launched on the falling pixel edge; the half-period budget
  was 3.367 ns while the worst-corner output buffer delay alone was 5.352 ns.
  The internal pixel clock had no failing endpoint.
- Corrective architecture: register the complete HDMI bundle on the preceding
  rising edge, giving the receiver a full pixel period while preserving the
  ADV7513 rising-edge capture contract. The next full route must close both
  setup and hold before hardware deployment.

## 2026-09-02: qualified vendor baseline

- Host: `beast`
- Board: DE25-Nano USB serial `TRWJGOUZ`
- Device: `A5EB013BB23BE4SCS`
- Quartus: Pro 26.1.1 Build 130
- Vendor source: `DE25-Nano_GHRD_QP25.3.1.qar`, SHA-256
  `5da65306fbb1689fb12c9d4bd3721e6be683192e899315df55718eb5df6083e5`
- Required migration: Quartus-supported upgrade of every IP component from the
  25.3.1 archive to 26.1.1 before synthesis.
- Build: `fpga/de25/build_vendor_baseline.sh` restores into a staging
  directory, verifies the pinned source inputs, performs the supported IP
  upgrade, compiles, gates timing, hashes the products, verifies those hashes
  before and after atomic publication, and only then replaces the retained
  checkpoint.
- Result: fit, assembly, fully constrained multicorner timing, HPS programming
  file generation, JTAG programming, and HPS SD/UART boot all pass.
- Resources: 9,160 / 46,800 ALMs; 18,358 registers; 2,098,688 / 7,331,840
  block-memory bits; 131 / 358 RAM blocks; 0 DSPs; 2 / 11 PLLs.
- Timing: setup slack +1.662 ns; hold slack 0.000 ns with zero failing
  endpoints. Setup and hold are fully constrained.
- Output identities from the retained reproducible checkpoint:
  - `golden_top.sof`:
    `33bcb4d0337b79d00e3a6eb6d9185b1deac5a9ae252d822951a3a5b4dfe64d15`
  - `golden_top_hps.sof`:
    `0b00f0da071b2079edb512e1fd69080e788791d1bea99100d1c7fffedbf80b13`
  - Quartus programmer checksum: `0x06304EE6`
  - running JTAG design hash:
    `DA18469719F071C3A242520C2C19D4CF9F12B44D37136703A38AFE5273D4D554`
- Physical boot evidence: LPDDR4A calibration and 1,024 MiB size check pass;
  U-Boot loads the kernel and device tree from SD; Linux 6.12.11 brings up all
  four ARM64 cores, recovers and mounts the ext4 root, starts networking and
  SSH, and reaches the serial login prompt.

Failed checkpoints retained:

1. Direct 26.1.1 compilation of the untouched 25.3.1 archive failed because
   HPS, HPS EMIF, and on-chip-memory IP required upgrade.
2. The first upgraded fit failed because `quartus_agilex5e` was installed but
   the local Quartus license file was not supplied to the tool process.
3. Cleaning the vendor `output_files/` also removed its required HPS SPL hex.
   The build now verifies that input, moves it out of the generated-output
   directory, and only then cleans.
4. The first automated publication wrote staging-directory paths into its
   checksum manifest. Verification from the published directory rejected it.
   The producer now records relocatable paths and verifies them on both sides
   of the atomic rename.

The vendor baseline is an integration oracle, not the Astra platform. It uses
HPS LPDDR4A, leaves FPGA LPDDR4B disabled, and contains demo OCM/JTAG/peripheral
logic that the Astra shell will replace.

## 2026-09-02: AArch64 Astra runtime gate

- Physical HPS: Ubuntu 22.04, AArch64, glibc 2.35, four cores, 1 GiB LPDDR4A.
- Build host: Beast, using an Ubuntu 22.04 AArch64 sysroot rather than Beast's
  Ubuntu 24.04 cross-libc headers and libraries.
- QEMU 9.2.4 SHA-256:
  `52ed189975d15019d696f99635081016499d989f549f4d9b55804ea172c5744e`.
  Its newest required libc symbol is `GLIBC_2.34`.
- Astra ROM SHA-256:
  `308b170b14113c9c327776ad01f607b767e561567c6904869877390ba27d0614`.
- Prepared 64 MiB image SHA-256:
  `4050e73454f43e171fbce52f409946018dec2c94d474f03a5c5d38d0068c7337`.
- The Terminal executable extracted from that image matched the current build
  at SHA-256
  `d25f29d03073beb9a63e8a3c01cd92cc690f6af006643e6e223b5ae5016b4369`.
- Physical result: the focused performance gate and complete 70-command gate
  pass on the DE25 HPS. The complete run covered durable storage, VFS and
  namespace operations, POSIX TCP fork/exec, Lua, process reporting, events,
  redirects, configuration assigns, and host-synchronized year 2026. Measured
  Enter-to-ready command completion was 0.29--4.28 seconds.

Two failed checkpoints are retained because they changed the release gate:

1. The first AArch64 binary linked against Beast's glibc 2.39 development
   environment and required `GLIBC_2.38`; the board rejected it. The DE25
   profile now requires the Jammy sysroot, uses its headers and libraries, and
   keys its build directory by the build contract.
2. Output silence was previously treated as shell readiness. The Cortex-A55
   run proved that a command can print before its process is reaped, and an
   `events:` query can echo an old readiness string. The shell now emits a
   structured informational ready event after drawing each prompt, and the
   gate matches the exact live event record rather than text or elapsed time.

The active hardware blocker is now the Astra FPGA shell and LPDDR4B graphics
integration; the retained vendor route remains the board and HPS oracle.

## 2026-09-03: first Astra LPDDR4B shell fit

- Host/tool: `beast`, Quartus Pro 26.1.1 Build 130.
- Source: exact vendor GHRD and DE25 resource archive pinned in
  `fpga/de25/platform.json`; the build rejected hashes taken from a mutable
  exploratory extraction and now verifies immutable ZIP members.
- Architecture: HPS full AXI directly to a 1 GiB EMIF window at `0x40000000`,
  with the vendor calibration driver and no Astra data-path logic between the
  HPS and EMIF.
- Synthesis passed with zero errors and 33,669 pre-fit resources.
- Fit failed before placement with four illegal EMIF byte lanes. The selected
  `Qsys_emif_io96b_lpddr4_0.ip` is bank A despite its name; Terasic's own
  `Qsys.qsys` maps bank B to the confusingly named
  `Qsys_emif_lpddr4a_0.ip`. The failure is retained because it establishes the
  exact physical cause rather than a placement or timing problem.
- Disposition: select the bank-B component by the vendor system mapping and
  immutable member hash `e0e37791846ef287f91776dd8a781ec5729b6d3351cd57f9883f47b28145c2c0`,
  then rerun the unchanged production fit gate.

## 2026-09-03: routed Astra LPDDR4B shell

- Host/tool: `beast`, Quartus Pro 26.1.1 Build 130.
- Inputs: pinned vendor GHRD SHA-256
  `5da65306fbb1689fb12c9d4bd3721e6be683192e899315df55718eb5df6083e5`
  and pinned DE25 resource archive SHA-256
  `554af27603855b37840e930bd075c34141c85d2f753cfa45df2eb5114f0f4c73`.
- Correct bank-B component: immutable archive member SHA-256
  `e0e37791846ef287f91776dd8a781ec5729b6d3351cd57f9883f47b28145c2c0`.
- Result: synthesis, fit, route, signoff timing, assembly, HPS programming-file
  generation, staged checksum verification, and publication pass with zero
  errors.
- Resources: 11,940 / 46,800 ALMs; 24,709 registers; 2,107,072 / 7,331,840
  block-memory bits; 138 / 358 RAM blocks; 0 DSPs; 3 / 11 PLLs.
- Timing: setup slack +1.341 ns; hold slack 0.000 ns with zero failing
  endpoints. Setup and hold are fully constrained and timing requirements are
  met.
- Design Assistant: zero high-severity violations. The three medium findings
  are one generated interconnect reset-polarity conflict, the vendor system's
  multiple reset synchronizers in the 100 MHz domain, and a min-only delay in
  the generated LPDDR4B IP SDC. They remain visible and unwaived.
- Retained output identities:
  - `golden_top.sof`:
    `6254c0c0091662c58ae47a01a9ff0f995766c20fa50838c8a50af455d9843224`
  - `golden_top_hps.sof`:
    `df1d8cd90d9748f0708d565664a525d519daf290468acf5ec3351eb560608e1d`
- The generated `qsys_top.sopcinfo` and Quartus
  `sopc-create-header-files` output independently identify the HPS-visible
  LPDDR4B window as base `0x40000000`, span `0x40000000`, end `0x7fffffff`.
- This checkpoint proves implementation and address-map closure, not physical
  memory operation. The next release gate is software-readable calibration
  status followed by physical calibration and destructive full-window memory
  tests on the DE25.

The first status-register rebuild failed during elaboration because the patch
used the nested-subsystem PIO port suffix `external_connection_export` for a
directly exported top-level PIO. Platform Designer's
`get_interface_ports astra_lpddr4b_status` command returned the authoritative
port name `astra_lpddr4b_status_export`; the binding and its regression
contract now use that generated name.

## 2026-09-03: routed shell with software-readable calibration status

- Host/tool: `beast`, Quartus Pro 26.1.1 Build 130.
- Inputs and bank-B component are unchanged from the routed shell above.
- Result: synthesis, fit, route, signoff timing, assembly, HPS programming-file
  generation, staged checksum verification, and publication pass with zero
  errors.
- Resources: 12,254 / 46,800 ALMs; 25,205 registers; 2,107,072 / 7,331,840
  block-memory bits; 138 / 358 RAM blocks; 0 DSPs; 3 / 11 PLLs.
- Timing: setup slack +1.380 ns; hold slack 0.000 ns with zero failing
  endpoints. Setup and hold are fully constrained and timing requirements are
  met.
- Design Assistant: zero high-severity violations and the same three visible,
  unwaived medium findings as the prior route.
- The HPS lightweight bridge exposes a 3-bit read-only PIO at relative address
  `0x20000`: bit 0 is calibration passed, bit 1 is calibration failed and
  latched, and bit 2 is the EMIF data interface ready. The official generated
  headers give it span 16 and end `0x2000f`.
- The same generated headers retain the LPDDR4B data window at base
  `0x40000000`, span `0x40000000`, end `0x7fffffff`.
- Retained output identities:
  - `golden_top.sof`:
    `9b9a6f456d71d780073d925e7b7920f7b28d44a4fd225ba30cc746efedb88fd2`
  - `golden_top_hps.sof`:
    `e0b19e35ff5083b2672a08e63722b0f3f76032e003e2ae01f9f570c42c77948b`
- The remaining gate is physical: cold-boot the HPS, derive the lightweight
  bridge's physical base from the running board device tree, program this exact
  SOF, require calibration pass and data-ready, then destructively verify the
  full LPDDR4B window. No memory access is permitted before readiness.

The first hardware load used `golden_top_hps.sof` at JTAG device 2. Quartus
reported programming checksum `0x0641F945`, running design hash
`001F9CDE36D0A705DAB8F0FEA0FCAC61652A15BFD8A13C1F5F727DB7CD48631D`,
and successful configuration with zero errors. Loading the HPS-bearing image
stopped the already-running Linux host at `192.168.1.52`; it did not reboot the
HPS. The serial console remained silent. This is not a memory-test failure.
Disposition: cold-boot the existing SD host, load the fabric-only
`golden_top.sof`, verify that Linux remains reachable, then perform the gated
status and LPDDR4B tests. Do not use HPS reachability as an implicit result of
fabric programming.

That disposition was tested and rejected with direct evidence. Quartus PFG
converted the exact HPS-bearing SOF to `golden_top.rbf` at SHA-256
`b38b877bd1daf0e2f94db8658d50928a313f1165161aa3a4b039ad918ab127d7`.
Programming that RBF preserved the HPS/CoreSight chain and running Linux, but
the reconfiguration disabled the HPS-to-FPGA bridges and interrupted the HPS
Ethernet path. Restarting NetworkManager restored Ethernet; a root `/dev/mem`
read of the status PIO at physical address `0xff420000` then raised `SIGBUS`.
The address is the generated `0x20000` offset plus Agilex 5's documented
`0xff400000` lightweight-bridge base. The SD boot script independently shows
the required order: `bridge enable` runs before Linux starts. Therefore the
fault is a disabled bridge, not a calibration result, and post-boot JTAG
reconfiguration is not the production path. The retained boot contract must
load the Astra RBF before U-Boot enables the bridges.

The board also contained runtime residue from its previous Ultimate128 role.
The disabled `c128-input.service`, five `/usr/local/bin/c128-input*` binaries,
`/var/lib/ultimate128`, the Ultimate128 Avahi advertisement, and all matching
boot backups were removed. The active DTB was restored to its clean
pre-Ultimate SHA-256
`d9bd893fc94e45359fb442ca06741aa63c5881e9376473fb9397ebf6d6be4d13`,
and the boot script to SHA-256
`b38747a4b43abcc04d84b8f46ea514594e9cc59c8188633e4d24ba36da3728ef`.
The board is now named `astra68`. A warm Linux reboot stopped after the HPS
reset request and required reloading the verified vendor HPS SOF, checksum
`0x06304EE6`, to cold-start the HPS. The recovered boot used no `mem=` override,
reported only the vendor `svcbuffer@0` reserved-memory node, exposed 934 MiB to
Linux, started no retired service, brought up networking and SSH, and reached
the `astra68` serial login. The warm-reset limitation is separate from the
removed software and remains part of the production cold-boot gate.

## 2026-09-05: instrumented production route and first stalled capture

- Host/tool: `beast`, Quartus Pro 26.1.1 Build 130. The exact-mirror source
  build passed the complete DE25 contract suite, exhaustive graphics/audio
  simulation, synthesis, placement, route, signoff timing, assembly, staged
  checksum verification, and atomic publication.
- Architecture: Intel Performance Monitor 4.0.1 is a 128-bit AXI4
  pass-through between `subsys_hps.hps2fpga` and LPDDR4B, with 48-bit counters,
  advanced latency support, and its official System Console JTAG endpoint.
- Resources: 43,863 / 46,800 ALMs; 3,818,904 / 7,331,840 block-memory bits;
  290 / 358 RAM blocks; 58 / 376 DSP blocks; 5 / 11 PLLs.
- Timing: setup slack +0.743 ns; hold slack 0.000 ns; zero TNS and zero failing
  endpoints. Setup and hold are fully constrained and timing requirements are
  met.
- Design Assistant: zero high-severity violations in the final snapshot.
- Retained output identities:
  - SD core RBF:
    `4141732e603b4ed10f88333d2529e9015495e32c603deb97d74a2286d7ac25c0`
  - HPS JIC:
    `4189b10a5cf211299ba686b4629342e04ce681217d6617f20173b373afaf6144`
  - full SOF:
    `4e7601a94586488960ff818bbecf4fd30870293488378b637d161b7b0b082add`
  - boot script:
    `c3b2c12bca140b889058668fad454bf238b64199c62dd794efb780a75e2d50ca`
  - source-manifest file:
    `d33c2a09a89a8b52ef5dbfbe85054fc460f460dac9928262408fc3d6faa4c4eb`
- The board verified the complete boot-bundle manifest before the installer
  atomically replaced the RBF and boot script. A warm reboot loaded the new
  image, returned Linux to the serial login, and restored `192.168.1.52` with
  0.2--0.4 ms host latency. This is one successful warm reboot, not yet grounds
  to remove the earlier failed warm-reset observation.
- Astra autostart was disabled by removing the single identified
  `multi-user.target.wants/astra.service` symlink. This prevents the known
  workload from racing Linux recovery boots; the unit itself and its data were
  retained.
- Hardware workload: AArch64 probe SHA-256
  `7fde94a5ab304d3304f0c03ac1ec0cfd0b4c004436250d85dbc14cb341b77f42`
  copied and displayed one 1280x720x16-bit frame, then issued volatile 64-bit
  reads across 1,843,200 bytes. Before the HPS read, the framebuffer endpoint
  reported 80 accepted AXI bursts, 1,280 accepted response beats, and zero
  response-stall cycles. The HPS workload did not reach its first 64 KiB
  progress report and Linux stopped answering Ethernet and serial input.
- The official PMON diagnostic capture was frozen during the failure. It
  reported 357 AR transactions, 357 R transactions, and 357 expected R
  transactions; 115,229 AW, W, B, and expected W transactions; 4,997,468
  traffic cycles; and no counter overflow. Every transaction accepted at this
  monitored boundary completed. The long Linux stall therefore occurs with no
  missing accepted LPDDR4B response; an upstream request blocked before its
  handshake remains possible.
- The serial kernel log subsequently identified CPU 1 as stalled and reported
  starvation of the RCU grace-period thread plus a possible timer-softirq
  handling issue on CPU 2. Magic SysRq produced no additional backtrace while
  the machine was stalled.
- Disposition: on the next recovery boot, use PMON's read-only efficiency and
  backpressure configuration on the unchanged routed image, reproduce the same
  workload, and freeze the counters. Do not alter the memory data path until
  that capture distinguishes AR acceptance backpressure from response-side
  backpressure.

## 2026-09-05: out-of-order production route and physical qualification

- Root cause: Platform Designer's HPS and graphics master connections into
  LPDDR4B did not enable the interconnect's native out-of-order support. The
  retained change sets `qsys_mm.enableOutOfOrderSupport` on the PMON path and
  the framebuffer, scene, and render connections. No custom ordering adapter
  was added.
- Host/tool: exact-mirror source on `beast`, Quartus Pro 26.1.1 Build 130.
  The complete DE25 contract, graphics/audio simulation, synthesis, fit,
  route, signoff, assembly, Design Assistant, staged hashes, and atomic
  publication passed. `BUILD_SHA256SUMS` has SHA-256
  `3b94e21eef925df7e56260e8d3a4f3b1f1a06a846351254b998504ef0b1f8e42`.
- Resources: 41,860 / 46,800 ALMs; 3,818,968 / 7,331,840 block-memory bits;
  290 / 358 RAM blocks; 58 / 376 DSP blocks; 5 / 11 PLLs.
- Timing: setup +0.671 ns, hold 0.000 ns, recovery +2.661 ns, removal
  +0.024 ns, and minimum pulse width +0.220 ns. Every production clock is
  fully constrained and meets timing. Design Assistant reports zero
  high-severity violations.
- Retained identities:
  - SD RBF:
    `2486427470514318a8f669f61dbeb766b2f18501eb6569b0fa13562cda0ce8c4`
  - HPS JIC:
    `c61af281078482027fdbd0c0a94130bee83e11daba8acf1abe5814ae344c18df`
  - full SOF:
    `633a7bfa76139900cba5e9a1bc4c490fcef3170fe636558b0a5c8be18353d562`
  - HPS SOF:
    `c102e120e4278269d32f4666cf2f32bf02e25a9ba0b86b75b8a100a7c9a26417`
  - generated Platform Designer system:
    `1b0bcb19ed9d0fa2a608979ec138c6b95b48fced56ece4f684f454ee2aaced5e`
  - Quartus programmer checksum: `0x073F0338`
- A stale programmed shell was caught before testing: System Console reported
  physical design hash `06CFF481957C313BF2C4`, while the retained SOF was
  `B685A4711883D751E0A6`. `verify_running_shell.sh` now verifies the complete
  build manifest, loads the exact SOF metadata, discovers exactly one DE25,
  and requires `design_link` to the physical JTAG device. This gate passed
  after the retained RBF was installed and again after a physical power cycle.
- Physical contention gate: 30 alternating same-address/cross-address tests,
  one initial four-core test, and five more four-core rounds completed 54
  workloads and 99,532,800 bytes with zero hangs. PMON counters 0, 1, and 5
  each reported `0xbdd800`, exactly 12,441,600 response beats and exactly
  54 times the expected 230,400 beats. No counter overflow occurred.

The first production runtime then failed independently in the AArch64 Linux
terminal-display helper. A captured core showed `SIGBUS` at `__memset_zva64`,
PC `0x4143c0`, on `dc zva, x3` while clearing the `/dev/mem` framebuffer.
Device-mapped memory cannot use libc's cache-line zeroing path. The shared
graphics hardware library now owns device-memory fill and copy primitives;
all device-memory callers use them. AArch64 disassembly proves their aligned
loops contain explicit `str`/`ldr` instructions with no libc call and no
`DC ZVA`.

The exact fixed terminal display has SHA-256
`fe62b9c45b45aa493e6eb4f3fd0554d95f54f0c2b168ac1140238f7c7a78fb87`.
Immutable release
`d7b5b035315219fcdc47bd34fa1c2726685904c9663e68a06c09533f01204344`
verified before and after installation. It reaches `POST PASS`, keeps
`ASTRA_TERMINAL_DISPLAY READY` resident, recovers and verifies storage, starts
hostfs/network/NTP in order, and reaches stage 8. The complete 70-command gate
passes on the DE25 against this release.

Three consecutive physical hardware sweeps pass the full splash transfer and
CRC32 `611029ee`, all sprite phases, the complete renderer/blitter/geometry/
AFNT/flood/compositor suite, dual-bank copper, and 48-kHz HDMI audio. Every
sweep reports zero AXI errors and zero renderer backpressure. Remaining release
gates are a second cold boot of this installed runtime and direct physical HDMI
observation; the Arty remains the active rollback machine until both pass.

The next physical power cycle passed `design_link` against the same exact FPGA
shell and started release `d7b5b035...`, but correctly failed the wall-clock
release criterion. Linux began Astra at `2026-09-05T15:27:23Z`; its first NTP
synchronization occurred at `15:55:35Z`. `network-online.target` had completed,
but `time-sync.target` only established service ordering and did not represent
completed synchronization on this Ubuntu image.

The first proposed correction polled `timedatectl`'s `NTPSynchronized`
property. A physical negative test rejected it: disabling NTP left that
property at `yes`, so it is a historical synchronization state rather than a
reliable completed-sync barrier. No custom polling code was retained.

The DE25 unit now uses systemd's existing synchronization primitive instead:
it pulls in `systemd-time-wait-sync.service` and orders Astra after that service
and `time-sync.target`. The native service waits with
`TimeoutStartSec=infinity` for the kernel clock synchronization state. The
installed unit SHA-256 is
`f7fec4c9bdbd6d17364061a5a06a1f7f5f4f7822b462a4120ba8d71bd0d8a6bb`;
the byte-verified runtime remains
`d7b5b035315219fcdc47bd34fa1c2726685904c9663e68a06c09533f01204344`.
A service restart through the active native barrier started Astra at
`2026-09-05T16:04:54Z`, matching the synchronized host clock. The remaining
cold-boot gate must prove the same ordering from power-on.

The final physical cold boot passed that gate. The wait service began while
the board still reported its stale 2023 RTC, then systemd-timesyncd performed
its initial network synchronization at `2026-09-05T16:11:25Z`. The infinite
wait service completed and Astra started in the same second, proving there was
no pre-NTP guest execution. POST passed at `16:11:29Z`; Axiom reported wall
clock `2026-09-05T16:11:29Z`. Storage block round-trip, partition discovery,
journal recovery, write/read verification, hostfs, network, NTP, and stage 8
all passed. The terminal display and QEMU remained resident with no Linux
stall, AXI error, `SIGBUS`, or kernel warning.

The exact-shell `design_link` gate passed again after this power cycle. The
installed release and unit retained identities
`d7b5b035315219fcdc47bd34fa1c2726685904c9663e68a06c09533f01204344`
and `f7fec4c9bdbd6d17364061a5a06a1f7f5f4f7822b462a4120ba8d71bd0d8a6bb`.
The live display mailbox recorded a successful desktop render request and
completion at graphics generation 4. Live register reads reported device ID
`0x41535452`, version `0x00010006`, capabilities `0x000003ff`, graphics
generation 4, arena `0x40000000..0x7fffffff`, scanout base `0x40200000`, pitch
2560, size 1280x720, and framebuffer control 3. Commit errors, response-stall
cycles, renderer failures, and renderer backpressure were all zero.

The HDMI control path also passed directly on the physical shell. With the
link request asserted, status became 3: requested and transmitter-ready after
the vendor I2C configuration completed with acknowledgements. Clearing the
request returned status to zero. Combined with the exact-shell gate, complete
cold boot, live scanout, terminal residency, and three exhaustive hardware
sweeps, this closes the DE25 migration release gate without an external video
capture device. The DE25 is the active Astra machine; the Arty is the rollback
platform. The next independently measured project is the MC68040 migration.

## 2026-09-08: 512 MiB Media RAM route and runtime

- Host/tool: exact-mirror source on `beast`, Quartus Pro 26.1.1 Build 130.
- Architecture: the HPS-visible Media RAM arena is
  `0x40000000..0x5fffffff`; the upper half of LPDDR4B is outside the published
  graphics/audio arena. Astra guest RAM is independently 512 MiB in QEMU.
- The complete production build passed synthesis, fit, route, signoff,
  assembly, manifest verification, boot-bundle installation, and physical
  `design_link` verification. Every production clock is constrained.
- Timing: setup +0.740 ns, hold 0.000 ns, recovery +2.771 ns, removal
  +0.085 ns, and minimum pulse width +0.220 ns, with zero TNS and zero failing
  endpoints.
- Resources: 41,788 / 46,800 ALMs; 3,818,968 / 7,331,840 block-memory bits;
  290 / 358 RAM blocks; 58 / 376 DSP blocks; 5 / 11 PLLs.
- Retained identities:
  - `BUILD_SHA256SUMS`:
    `f6c0be98bd8ae461f6a5ae170437263762c42a229b051f6c15d4b02ba911bdf4`
  - SD RBF:
    `a2dbcc47dffc872e44c22d4c512b4d931ded14928154ecfe4182e7c6b60fe2ab`
  - HPS JIC:
    `db9dbdf8fb4e8897f671b26c0aa1351bafcee10dc1ccd80df89b731d2fd8641e`
  - full SOF:
    `926b86653e6e8eb4859f62b652fa0e1a448807072da985b44f4eb38d27c598e6`
  - HPS SOF:
    `8d8dc330cf9418315ff9aadff1543a29214f7db4897f6ca03aca15b072776dc6`
- Physical Media RAM passed stuck-address, random-value, XOR, subtract,
  multiply, divide, OR, AND, sequential-increment, and all 64 solid-bit
  patterns over the full 512 MiB window with zero mismatches. The next
  block-sequential algorithm was intentionally stopped and is not claimed.
- Graphics calibration/readiness passed, the splash read back at CRC32
  `611029ee`, and the hardware reported the expected 512 MiB arena.
- Immutable runtime release
  `b05969f66fe1a4ce0b70d1fe595c4a0ea40dc32a8bbb0ad5aa079955649a11c4`
  passed full-range 512 MiB guest POST, reported 131,072 physical pages,
  mounted and verified storage, launched every service, and reached stage 8.
  The physical MC68040 measured 70.117 MHz effective; QEMU settled at 553,940
  KiB RSS and `astra.service` remained active with zero restarts.

## 2026-09-15: final-stream capture route and DMA readback

- Host/tool: exact-mirror source on `beast`, Quartus Pro 26.1.1 Build 130.
  The complete production shell routes with capture connected to the final
  physical RGB stream after all compositor layers and the native pointer.
- Timing: setup +0.024 ns, hold 0.000 ns, recovery +2.645 ns, removal
  +0.046 ns, and minimum pulse width +0.220 ns. Every production clock is
  constrained and meets timing.
- Resources: 45,797 / 46,800 ALMs and 354 / 358 RAM blocks. The retained SD
  RBF SHA-256 is
  `01d05c63af51d1d78acbfa3acb47292f0ba7848f0b05cdb1b9f4f693c2107524`.
- A retained all-layer internal specimen contains desktop/window surfaces,
  hardware glyph and line output, 16 sprites, four Copper-driven palette
  bands, and the native pointer. Its raw RGB and PNG SHA-256 values are
  `b418b7d37820b5d6cb88f1a0810e6a165ba1cef8a51e2728e92f75da68900784`
  and
  `72b3b41fdc6a3c09d2d774e250c1748bfc4c12a2b740c7e9c4ea27d5dc51208f`.
- The normal runtime's internal RGB capture is byte-identical through direct
  and four-channel DMA readback at SHA-256
  `38e690b4f69e57fead7bdce21142d9f5f1420c7e95ce1d8dcd0fd0c4791de476`.
  Its PNG SHA-256 is
  `6636321c89dd50b0470bbfcdbde75ab29b115d6dbd53dfe0432ead9a4b9dff87`.
  A direct Cam Link frame of the same normal desktop has SHA-256
  `5093d751b893498d4b5eb0256c759b6f5feb9103c06c106aa0f1c700b157682b`.
- The exact Linux 6.12.11 source driver binds both existing DesignWare AXI DMA
  controllers. One channel verified 100 MiB at 156,206 KiB/s with zero data
  failures. Four channels moved the real 6,220,800-byte frame in 15,081,176 ns
  in the reachability probe; the retained production device completed it in
  14,570,053 ns. No RTL change was required for DMA readback.
- The first production DMA frame was black because the Media RAM resource had
  been mapped before the external capture producer wrote it. Direct readback
  proved acquisition was correct. Mapping only after capture completion and
  unmapping immediately after DMA restored byte identity and enforces the
  Linux DMA ownership contract. A second gate caught non-page-aligned mmap
  accounting; the driver now allocates and maps exactly `PAGE_ALIGN(frame)`
  while exposing only the meaningful RGB bytes. Writable mappings are rejected.
- Keep `dw_axi_dmac_platform` loaded until shutdown. Its upstream remove path
  does not unregister the DMA device, leaving stale channel-class objects and
  causing `EEXIST` on reinsertion. The Astra capture module is independently
  unload-safe while closed and owns its channels and coherent frame only for
  the duration of an open remote-display session.
