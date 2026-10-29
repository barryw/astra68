# Quake rendering offload assessment

Status: measured design checkpoint, 2026-10-28. No RTL or production runtime
changed. Usability gate is sustained 30 FPS at default 320×200 on DE25, with
no audio gaps or multi-second pauses.

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
