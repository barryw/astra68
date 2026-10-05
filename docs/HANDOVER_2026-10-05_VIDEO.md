# Handover 2026-10-05: phase 2, video (posted present and pipelining)

Read `CLAUDE.md` (the **Haiku first** standing instruction), `AGENTS.md`,
then `docs/MEDIA_DATA_PLANE.md` -- the approved design; its Video section is
this phase. `docs/HANDOVER_2026-10-05_MEDIA.md` holds phase 0 and phase 1
in full, and its "Haiku VIDEO", "SDL2 Haiku backend" and "Astra DISPLAY
today" sections are the research this phase starts from (file:line).

## State

- `main` is pushed and ends at the commit that adds this page. Phase 1
  commits: `9669fb53` (switch counters), `1707179a` (audio data plane),
  `6a5f6f87` (board measurements), the clean-checkout build fix, and
  `f3180c7f` (media band for stream holders, media service at 16).
- The DE25 runs release `547e66fc` (from `f3180c7f`), with `a9cf49d2`
  (streams, no priorities) beside it. Both carry the new astra-top; a
  release from before `9669fb53` cannot be compared with it (the sample
  format changed).
- `verify-nopc.sh` on beast is green. Known flake: the display gate's
  "Terminal double-click launch failed: requests=31/32" once in 7
  parallel runs; it passes alone and on a parallel rerun.
- A clean checkout builds now: `assets/fonts/astra_8x16.hex` is tracked,
  and graphics and QEMU's source preparation generate
  `fpga/arty/linux/astra_render_protocol.h` from its spec.

## Phase 1 result (Chocolate Doom, de25-ab.sh, 3 rounds each)

| | before (`7387fd33`) | streams (`a9cf49d2`) | + priorities (`547e66fc`) |
|---|---:|---:|---:|
| presents/s idle | 18.6 | 21.4 | 21.2 |
| presents/s, 125 Hz motion | 13.9 | 15.9 | 15.3 |
| audio gaps in motion / 30 s | 6, 7, 1 | 2, 0, 0 | 0, 0, 0 |
| guest idle | 23.6% | 34% | 34% |
| cross-space switches/s | 807 | 331 | 334 |
| media service runs/s | 264 | 8 | 8 |

The priority change costs ~4% of frames in motion (SDL's audio thread at
24 preempts Doom's main thread as each buffer frees) and buys the last
gaps. Kept by the owner: gaps are what a player hears, and a busier game
would gap without it.

## Phase 2: what to do

From `docs/MEDIA_DATA_PLANE.md`, Video, items 1-3:

1. **Present is posted, not called.** SDL's game thread hands the frame
   over and continues; a newer present replaces an unprocessed older one
   (Haiku: `SDL_bframebuffer.cc:95-103` posts `BWIN_UPDATE_FRAMEBUFFER`;
   `SDL_BWin.h:452-462` drops stale ones). `SDL_RenderPresent`'s only wait
   is the existing vsync wait (`SDL_astrarender.c:1104-1107`).
2. **One submission per frame.** Draw lists travel in the shared list area;
   target changes mark points in it instead of each being a port call
   (today three LIST_SUBMITs a frame, `SDL_astrarender.c:916-1018`).
3. **The display service pipelines.** Building frame N+1 never waits for
   the device to finish frame N: drop the `settle()` in `builder_begin`
   (`window_graphics.c:210-226`, `main.c:4106-4113`), two batch buffers,
   a request queue of at least two through kernel, Vesta and mailbox
   (today one: kernel `display_dma_active`, `process.c:2944-2977`; Vesta
   `platform.c:523-527`), completion per request.

Expect: four of the five per-frame waits gone, and the 34% guest idle
turning into frames. Phase 0 for video first, as for audio: answer with
file:line before code -- how SDL's present reaches the display service
today and what blocks at each step; what a queue of two needs in the
kernel, QEMU and the helper; how Haiku's app_server takes
`DrawBitmapAsync` without a reply (`View.cpp:3126-3163`).

## How to measure

Publish from beast with `~/astra-mg/publish-prio.sh` (takes the build
lock; it builds, packs and deploys). The board keeps the current and the
previous release. Then on the board:

```
/var/lib/astra/current/bin/de25-ab.sh -n 3 <old> <new>
```

under `nohup`, output to `/data`. It interleaves the releases, restarts
Astra for each, opens Doom, and measures idle, 125 Hz motion and idle
again (presents/s, audio gaps, guest idle, switches by cause, per-process
CPU). `docs/DEBUGGING.md` section 7.

Gates prove a new path is taken, not only that it works: phase 1's audio
gates fail unless the voice is a stream. Do the same for posted presents.
