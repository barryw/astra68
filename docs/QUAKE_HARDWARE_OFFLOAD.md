# Quake rendering offload assessment

Status: measured design checkpoint, 2026-10-29. No RTL or production runtime
changed. Usability gate is sustained 30 FPS at default 320×200 on DE25, with
no audio gaps or multi-second pauses.

## Fixed-demo DE25 baseline, 2026-10-29

The selected release ran Quake's built-in `+timedemo demo1` from Astra's native
Terminal with `open /apps/ChocolateQuake.app -condebug +timedemo demo1`.
The on-screen result was **969 frames in 62.7 seconds, 15.5 FPS**. A 30-second
window during this run had zero audio underruns and software gaps. The timedemo
exercises one fixed scene sequence; its average is the first same-workload
baseline, not a sustained-playability pass. A second run was started but its
result was not captured, so run-to-run variance is still unknown.

An AArch64 `perf` capture of the selected QEMU during a later `demo1` run
recorded 995 samples with none lost. Named QEMU helpers accounted for 3.17%
`helper_fsmul`, 2.91% `helper_fsadd`, 1.78% `helper_ftst`, and 1.66%
`helper_fsround`; translated code appears mostly as anonymous JIT addresses.
These host shares do not replace guest function attribution. They show no
single named QEMU helper can by itself close the roughly 1.94× throughput gap
between this timedemo and 30 FPS. The earlier guest-PC captures still point
to spans, visibility, depth, and surface-cache work together. Profiling the
fixed demo and measuring a generic graphics operation against its software
oracle remain next steps; speculative RTL has no route or timing evidence.

Evidence: `/mnt/Documents/astra68/quake-timedemo-20261029/` on Beast contains
the result screenshot, 30-second telemetry, host `perf.data`, and report.
Quake's `-condebug` log lives in its app-private STORE. Terminal correctly
cannot read that namespace, so automated result extraction remains open. The
board returned to the selected release at stage 8, zero restarts, 8.1 GiB
free; temporary trial files were removed after archival.

An isolated `-O3 -DNDEBUG -fno-strict-aliasing` build passed the NDK dynamic
dependency check and a focused DE25 install. The same 969-frame `demo1` took
57.7 seconds, **16.8 FPS**, about 8% faster than the one captured `-O2` run.
The candidate is still 44% below the 30 FPS gate, and the two runs do not
establish variance. The compiler trial was not retained. Its debug log was
read through a read-only loop while `astra.service` was stopped, then the loop
was detached and the original Quake binary was focused-installed. Doom's
startup window had one audio gap and 1,201 underruns, matching the earlier
startup transient; its following 20-second window reached 35.06 presents/s
with zero gaps and underruns. DE25 ended at stage 8, zero service restarts,
8.2 GiB free. Candidate binary, log, Doom telemetry, and screenshots are in
the same Beast evidence directory. A repeated A/B/A timedemo is required
before adopting `-O3`; even a confirmed 8% gain leaves the main rendering
bottleneck open.

## MC68040 software trials

`D_DrawSpans8` spends about 18–21% of sampled Quake time in perspective-corrected
world spans. GCC emits an FPCR save, truncate-mode switch, and restore for each
float-to-integer cast. A `FINTRZ` trial removed those switches but faulted at
`D_DrawSpans8+0x136` on DE25: `FINTRZ` takes the F-line vector on MC68040 and
Astra deliberately has no Motorola FPSP. The trial was removed. A second trial
used supported FPCR instructions to convert both texture coordinates under one
mode switch. It passed host conversion tests but produced 19.68 and 19.31
presents/s in two 30-second DE25 live-demo windows, with zero audio faults.

The compiled pixel loop also performs one signed multiply per pixel for
`(t >> 16) * cachewidth`. A row-cache trial multiplied only when the integer
texture row changed. Address-equivalence checks passed for six cache widths,
six step sizes, and span lengths 1–8. DE25 windows measured 19.21 and 22.64
presents/s with zero audio faults. The original path measured 24.35 before the
trials and 20.90 after restoration; scenes changed between windows, so these
numbers establish no repeatable gain. Quake's `demo1` timedemo ran, but its
result scrolled out of the visible console before capture. Both software
trials were discarded. Restored Quake launched and ran; Doom still rendered at
34.62 presents/s with zero audio faults. No Quake, SDL, or RTL optimization
from these trials remains in source or on DE25.

Evidence: `/mnt/Documents/astra68/quake-fintrz-20261028/` on Beast. Next
performance step needs an automated, fixed-demo result capture before more
hot-loop changes. Existing Astraea/SDL paths remain preferred where measured
cost warrants them; FPGA capacity remains too tight for speculative RTL.

## SDL/Astraea indexed-texture trial

The first ranked experiment ran on DE25. A temporary SDL2 `INDEX8` streaming
texture accepted palette updates and uploaded Quake's one-byte pixels directly.
Astraea expanded it to an ARGB GPU surface at source size, then used its normal
scaled blit. Shared renderer host tests, SDL Kit build, NDK distribution check,
and an on-board palette-change/readback probe passed. Captured demo frames
showed correct colors. Two 30-second live-demo windows measured 17.88 and
16.96 presents/s, with zero audio gaps or underruns. Quake used 76.0% and
79.6% guest CPU; guest idle was 12.7% and 9.9%.

After restoring the original ARGB binaries, a third 30-second live-demo
window reached 23.38 presents/s with zero audio faults. Earlier ARGB gameplay
windows reached 21.78 and 24.1 presents/s. Scenes vary, but all three ARGB
windows beat both indexed windows. Directly scaling INDEX8 through the palette was much
slower: a separate live sample showed only 3.33 presents/s with the guest
mostly idle. Expansion at source size fixed that severe hardware stall, but
its extra GPU pass still cost more than the roughly 4% CPU conversion it
removed. This candidate was rejected and the prior SDL/Quake binaries restored.
The temporary probe was removed from the board. Restored SDL ran Doom at
35.03 presents/s with zero steady-state audio faults over 20 seconds; its
first 20-second window during startup had one software gap and 1,326
underruns. That transient needs separate audio investigation if reproducible.
Trial patch, probe image, gameplay frame, and telemetry are archived under
`/mnt/Documents/astra68/quake-indexed-20261028/` on Beast. None of the trial
API or renderer changes is retained in source.

Haiku review for this experiment: `headers/os/add-ons/graphics/Accelerant.h`
keeps palette and blit capabilities distinct, and
`src/add-ons/accelerants/intel_extreme/engine.cpp` batches hardware drawing.
Astra used its existing palette write, blit, and draw-list contracts; the
measured cost argues against retaining an indexed present path for Quake.
The existing `astra_render_copy_burst.sv` pixel mode converts direct color
formats at one pixel per clock, but excludes INDEX8 palette lookup; the
general blitter's palette path is much slower. A palette extension to that
burst mover would reuse its AXI read-ahead and write machinery, yet would
need a palette cache, validation, simulation, full route, and Doom regression
gate. It would remove only the ~4% conversion cost, so it is not by itself a
credible route from 23 to 30 FPS. The texture engine's six parallel attribute
steppers are a possible area-sharing target, but serializing them could cut
throughput; no synthesis evidence establishes reclaimable capacity. The
selected route's fitter report attributes about 4,681 ALMs to texture and
3,856 to blitter. Both serve existing operations, so neither is disposable
RTL. Spare DSP count is high, but ALM packing and +0.007 ns worst-clock setup
slack constrain any change. Existing hardware and software paths get measured
before an RTL proposal.

Existing SDL frame benchmark on restored DE25 library ran 59–61 FPS for
640×480 ARGB/RGB565 streaming textures. ARGB upload averaged 13.8–15.1 ms,
copy enqueue 0.14–0.18 ms, and present 1.3–2.3 ms; it omits Quake's software
world rendering and full-screen clear, so it cannot predict Quake FPS directly.
It does show ordinary SDL upload/copy/present can exceed 30 FPS without RTL
changes. Log: `quake-indexed-20261028/framebench.log` in Beast evidence
directory above. Next software experiment should measure Quake's frame stages
with a repeatable demo and target its software span, depth, and surface-cache
work. Shared SDL changes need a measured bottleneck and Doom regression gate.

## Measured bottlenecks

Selected release `072a2352ee14a0398170fb0a1fa3b7685c823891cfc74447d1ff601a9d4f4325`
ran at 21.78 presents/s over 30 seconds and 24.1 presents/s over another 20
seconds. The latter used 79.6% guest CPU in Quake, 11.9% in Display, and left
6.4% idle. Audio counters reported zero gaps and underruns. Host vCPU used
97.8% of one core; 82.6% of its samples ran translated guest code.

Two 20-second live guest-PC captures contain 3,001 and 2,370 Quake user
samples. Function shares differ with demo scene:

| Work | First | Second |
|---|---:|---:|
| `D_DrawSpans8` perspective-corrected world texture spans | 21.3% | 17.6% |
| `R_GenerateSpans` | 7.1% | 6.5% |
| `D_DrawZSpans` | 5.3% | 4.4% |
| `R_DrawSurfaceBlock8_mip0/1` surface cache | 9.1% | 3.0% |
| `R_RenderFace`, `R_RecursiveWorldNode`, `R_EmitEdge` | 10.1% | 16.6% |
| `Turbulent8` | below top 25 | 6.2% |
| SDL `Blit1to4` indexed-to-ARGB conversion | 4.0% | 4.4% |
| `S_PaintChannels` audio mix | 1.6% | 1.4% |

Captures and symbolized reports: `/mnt/Documents/astra68/quake-profile-20261028/`
on Beast. QMP PC sampling perturbs execution and attributes a translated
block to its entry PC. These shares identify functions, not exact cycle cost
within each function. Before changing a hot loop, use the existing profile
plugin and a repeatable Quake timedemo for instruction and frame baselines.

## Existing hardware and gap

Quake writes an INDEX8 framebuffer in software. Its SDL video path calls
`SDL_LowerBlit` to expand indices to ARGB every frame, uploads that texture,
then asks SDL/Astraea to scale and present it. Astraea already expands INDEX8
sources through palettes and accelerates blits. Shared SDL window surfaces use
that path, but Quake's renderer uses a logical-size streaming texture; current
SDL renderer accepts ARGB8888, RGB888, and RGB565 textures, not INDEX8 plus a
mutable palette. A reusable palette-aware indexed renderer source could remove
`Blit1to4` and shrink uploads. That is a small, measurable first offload, not
the 30 FPS solution by itself.

Astraea also has an affine textured-triangle engine. It cannot directly draw
Quake's world: `D_DrawSpans8` corrects perspective every eight pixels, world
surface caches carry indexed lighting, and alias/sprite paths write depth.
Breaking each span into triangles would add large setup and command traffic;
visual equivalence and throughput must be proved, not assumed. Existing
triangle model measured 47 cycles per nearest-filtered pixel for a small quad.
At 48,640 world pixels × 30 FPS, that is roughly 69 million of 165 million
graphics cycles per second before triangle setup, DDR stalls, depth, lighting,
other drawing, or display work. A plain `SDL_RenderGeometry` rewrite is not a
qualified path to 30 FPS.

Installed full route uses 40,754/46,800 ALMs, 317/358 RAM blocks, and
67/376 DSP blocks. Worst constrained setup slack is only +0.007 ns. DSPs are
available; logic packing and timing are limits. Any new command needs full
feature route, clock closure, and repeated DE25 graphics/HDMI gates.

## Ranked experiments

1. **Indexed present, no RTL.** Add a palette-aware indexed source to shared
   Graphics Kit/SDL renderer while preserving logical scaling, then adapt
   Quake through that shared API. Compare exact visual output, upload bytes,
   guest CPU, and presents/s. Keep only measured improvement. Existing SDL
   indexed window-surface code and Astraea palette BLIT are the starting point.
2. **Measure span transport before RTL.** Count Quake's eight-pixel segments,
   surface-cache bytes changed, depth writes, and command bytes per frame in
   a repeatable timedemo. Use those numbers to bound CPU descriptor work and
   Media RAM traffic. Profile plugin must split span setup from pixel loop.
3. **Generic indexed textured-span candidate.** Reuse Astraea texture fetch
   and command ownership; accept a bounded array of fixed-point span records,
   with validated INDEX8 source/destination and exact clip/order semantics.
   Quake would keep visibility and perspective setup on CPU and submit pixel
   work to hardware. Add depth or light-lookup support only when measurements
   prove it is needed and a reference model can preserve output. This is a
   general graphics operation, not a Quake-specific device command.
4. **Simulate, route, certify.** Compare candidate pixels and depth against
   software oracle across scenes and edge cases. Require projected 30 FPS
   with headroom before full route. Then run full production clocks, same-scene
   DE25 timedemos, frame pacing, audio, input, level transitions, and panic
   checks against selected baseline.

Haiku reference: `headers/os/add-ons/graphics/Accelerant.h` exposes optional
`B_FILL_SPAN`; `src/add-ons/accelerants/intel_extreme/engine.cpp` batches span
records into one command queue. Astra should adopt capability-gated batching
and hardware ownership, while its indexed textured semantics remain native to
Astraea's validated surface/command contract.
