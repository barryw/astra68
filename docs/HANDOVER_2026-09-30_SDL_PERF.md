# Handover 2026-09-30: SDL performance, then the remaining SDL gates

Read `CLAUDE.md` and `AGENTS.md`, then this page. `docs/HANDOVER_2026-09-29_IPC.md`
has the IPC call work and its end-of-session updates; `docs/IPC_CALL.md` has
the design as built.

## Direction (user)

1. **Now:** good performance across the board, starting with the two slow
   upstream demos, `testscale` and `testrendertarget`.
2. **Then** finish the remaining SDL gates, in order: input (`testkeys`,
   `testmouse`), timers (`testtimer`, `testthread`), `loopwave` audio
   playback, then Chocolate Doom.

Standing rules: fix the lowest tier (kernel, emulator, NDK, display service,
graphics libraries, SDL), never an individual program. No band-aids. Gates
stay green. Commit and push when a piece is verified. Watch the board and
report problems immediately.

## State at handover

**Git.** `main` is pushed at the commit that adds this page.

| Commit | What |
|---|---|
| `f3845d04` | Kernel `PORT_CALL` and one-shot reply capabilities, NDK `astra_port_call`, 7 helpers migrated (ABI `0x0001003A`) |
| `fd5a085e` | One build at a time on beast, enforced by a lock |
| `d81e7c83` | SDL filesystem backend; upstream demos shipped as applications |
| `ff55d480` | Display capture judged by its own counters (remote desktop fix) |

**Board (astra-de25, 192.168.1.52 via beast).**
- Release `5089c991...` (from `ff55d480`), bitstream unchanged (`68ce7bb2`).
- Rebooted 2026-09-30 04:25 to clear the stuck capture state.
- `astra_display_capture.ko` replaced (hash in `CURRENT_STATE.md`); the old
  module is `/root/astra_display_capture.ko.pre-sticky-fix`.
- Remote desktop RFB passes, including while testsprite2 runs.

**Verify.** The last full verify (`~/astra-mg/verify-then-publish.sh`) was
READY-TO-PUBLISH. It now also gates the SDL image probe and the four upstream
demos.

## The performance problem

Board, each demo opened from the live desktop, 10 s,
`~/astra-mg/hw/run_bench.sh /apps/NAME.app 10`:

| Demo | Render batches/s (~fps) | Blits/s | Notes |
|---|---|---|---|
| testsprite2 | ~48 | ~4,980 | 100 sprites; fine |
| testgeometry | ~53 | 0 | triangles on the texture engine; fine |
| testrendertarget | **~27** | ~27 | renders into a texture, then presents it |
| testscale | **~11** | ~22 | scaled copies of `sample.bmp` and `icon.bmp` |

Both slow ones use render targets or scaled copies of large textures. First
questions to answer, with measurements, not guesses:

- What each frame is on the board: `ASTRA_DISPLAY_PROFILE=1` in the systemd
  environment, restart astra, sum `render profile copy_us= hardware_us=`
  from the journal, then unset it. Batches per frame and hardware time per
  batch.
- Whether scaled BLITs fall off the pixel-stream path (~1.04 cycles/px for
  unscaled converting/blending BLITs) onto the texture engine, and what the
  texture engine costs per pixel there.
- Whether `SDL_SetRenderTarget` costs a full surface copy, readback, or an
  extra batch per switch in `render/SDL_astrarender.c` and the display
  service's `list_submit` (`sw/userspace/services/display/window_graphics.c`).
- On beast, `emu/qemu/bench-frame.py --fake-helper --profile` works for any
  test app image; guest instructions per frame show the CPU side.

What was learned about board frame time (SDLFrameBench, see the IPC handover):
- A board frame is not CPU-bound; the guest runs ~14 M instructions/s at 57
  fps.
- Upload into Media RAM is capped at ~133 MB/s by the HPS bridge.
- The display service waits for each render batch before replying, and a
  streamed frame costs about three serialized batches (surface write, list
  submit, compose) of ~2.2 ms hardware each.

Replying before the hardware finishes (ordered by fences), or fewer copies per
frame, are the known levers. They may matter more for render-target programs.

## Diagnosis (measured 2026-09-30, board, `ASTRA_DISPLAY_PROFILE=1`)

Both slow demos have one root cause: **scaled BLITs run on the serial
blitter.**

- **testscale.** One render batch per frame, **80-91 ms of hardware time**
  (CPU copy ~20 us) for two BLITs, a window-sized scaled background and a
  scaled sprite. That is ~28 cycles/px.
- **testrendertarget.** Render batches average **~30 ms** of hardware. Every
  copy it makes is a full-surface `SDL_RenderCopy(..., NULL, NULL)` between
  surfaces of different sizes (background into the target, target into the
  window), which makes them scaled BLITs too.
- **Why it is slow.** Unscaled BLITs run in the copy burst mover's pixel mode
  at ~1.04 cycles/px (`GRAPHICS_ARCHITECTURE.md`, "Pixel-stream BLIT").
  Scaled, reflected, color-keyed, masked, palette and ROP BLITs still use the
  serial blitter, at 25-170 cycles/px.
- **The texture engine is no way out:** 47 cycles/px nearest and 74 bilinear
  (`TEXTURE_ENGINE.md`).

**The fix is in RTL: nearest-neighbour scaled BLITs in pixel mode.** Per
destination row:
- read the source row once (source width in beats) into a line buffer;
- emit destination pixels at one per clock, picking
  `buffer[x_acc >> 16]` with a 16.16 step;
- reuse the buffer for every destination row that maps to the same source
  row.

For upscales that is about (source row + destination row) beats per
destination row, near 1-1.5 cycles/px against today's 25-170. Downscales
read whole source rows, so they cost more, but remain far below the serial
path. Reflection is a negative step, and alpha blending reuses the existing
pixel-mode blend.

- **Budget.** 875 ALMs free, per the sprites plan; logic may need
  reclaiming.
- **Procedure.** `fpga/de25/TIMING_CLOSURE.md` for the procedure and record.
  Model tests first (`tb_astra_render_*` style), then full route, board
  certification (`astra-render-certify`), and a new render-certify case
  comparing scaled BLIT pixels against the model.
- **Linear filtering.** SDL's linear scale mode currently goes to the texture
  engine. A bilinear pixel mode is a later step.
- **Re-measure after the fix.** testscale and testrendertarget with
  `run_bench.sh`, and SDLFrameBench, which uses no scaling and should not
  change.

## Done: scaled BLITs in pixel mode (bitstream `a13c8ed0`, deployed)

- Scope: nearest-neighbour, destination at least as wide as the source,
  any vertical ratio, plain or `BLIT_ALPHA`. Horizontal downscales,
  reflection, key, mask, palette and ROP still take the serial blitter.
- Sim: 1.04 cycles/px for the testscale background (perf case `4000`).
  Board: `ASTRA_RENDER_SCALED` 1.58 cycles/px, 2.9 ms. The serial path
  took 80-91 ms.
- Board demos (render batches/s): testscale 11 to 60, testrendertarget 27
  to 102, testsprite2 unchanged. `astra-render-certify` 10/10, and remote
  desktop passes.
- Record: `fpga/de25/TIMING_CLOSURE.md` (2026-09-30).
- Next levers if a demo is still slow: the per-batch serialization
  described above (reply before the hardware finishes), then horizontal
  downscale in pixel mode (the chunk cap needs `floor(cap / step)`).

## The remaining SDL gates

From `sw/userspace/sdl2/README.md`. Upstream test programs live in the SDL2
checkout's `test/` directory (`~/Git/SDL2` on beast, `/Users/barry/Git/SDL2`
on the Mac, pinned revision in `prepare_source.py`). Ship each unchanged as
an application the way the demos are:
- `SDL_DEMOS` and `SHIPPED_APPS` in `sw/userspace/sdl2/Makefile`;
- a manifest in `sw/userspace/sdl2/apps/`;
- images and gates in `~/astra-mg/images.sh` and `gates.sh`.

- **Input:** `testkeys`, `testmouse`. The SDL video backend translates native
  mouse, keyboard, text, wheel and window events. A gate needs injected input
  through QMP (`terminal_gate.Machine.qmp.key`, the desktop gate's pointer
  injection) and a check of what SDL reported.
- **Timers and threads:** `testtimer`, `testthread`. `SDL_TIMER_UNIX` with
  `HAVE_CLOCK_GETTIME`; threads are pthreads over Astra's native mutex.
- **Audio:** `loopwave`. The SDL audio backend runs over `pcm.library.2`
  (48 kHz stereo S16BE). QEMU has no PCM provider, so playback is a DE25 gate
  (the board runs `astra-audio-host.service`); the QEMU side can only check
  that the device opens and PCM backpressure is honored.
- **Chocolate Doom:** the real-game integration gate. Needs a WAD (the
  shareware `doom1.wad` is freely redistributable) and SDL_mixer and SDL_net,
  both already shipped.

## Tools and loops

- **Beast builds:** every entry script takes `~/astra-mg/build-lock.sh`; run
  ad-hoc work as `~/astra-mg/locked CMD`. `ASTRA-BUILD-BUSY` means wait.
- **Full verify:** `~/astra-mg/verify-then-publish.sh`, log `/tmp/v-s5.log`.
  It continues after a failed step; it is done only when the shell exits.
- **Publish and deploy:** `~/f2s-pub.sh`.
- **SDL demos alone:** `~/astra-mg/sdl-demos.sh` builds them, makes images
  and runs their gates.
- **Board app bench:** `~/astra-mg/hw/run_bench.sh /apps/NAME.app SECONDS`.
  It opens the app from the desktop icon and prints display rates. Restart
  astra afterwards to close the app.
- **Board screenshot:** copy `fpga/de25/linux/test_remote_desktop.py` to the
  board and run it with `--password-file /etc/astra/remote-desktop.password`
  (add `--double-click X Y` to open an icon first). It writes raw RGB888
  1920x1080, which PIL on the Mac turns into a PNG.
- **Capture registers:** read only `0x20105000` through `0x20105040`.
  Offsets beyond the window are counted in the access-fault record.

## Hazards learned this session

- **Display capture's status overflow bit is sticky** (`CLAUDE.md`). Judge a
  capture by its counters. Under heavy rendering most captures lose their
  frame: capture bandwidth against DDR load is an open RTL problem.
- **`pgrep -f` inside an `ssh ... 'cmd'` matches its own command line.**
  Wait loops written with it never end. Earlier sessions left seven of them
  polling for work that had finished; they were killed this session.
- **`verify-nopc.sh` keeps going after a failure.** Starting another run
  because the first one printed a failure is how two builds collided.
- **Editing a running bash script in place corrupts it.** Replace scripts
  under `~/astra-mg` atomically: write a `.new` file, then `mv`.
