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

## Done: guest-side overlap and QEMU jump cache (release `a49535f5`)

Measured first: after the RTL fix the board was guest-CPU bound, serially.
testrendertarget: vCPU thread 67% busy, hardware 23%, never together
(~20 ms guest + ~7 ms hardware per frame). Guest cost is ~85% kernel
(~15 syscalls, ~59k guest instructions per display request, beast profile).

- `3d0e3712` QEMU: a user-only TLB flush (every address-space switch) no
  longer empties the kernel's TB jump-cache entries, and a flush that
  cleaned nothing drops nothing. perf on the board had TB hash lookups at
  ~20% of the vCPU thread. +2-5%.
- `a4e526d8` display service: render-only batches are answered before the
  hardware finishes and collected before the next device use; fences need
  no kernel event (2 syscalls less per submit). testrendertarget +30%,
  FrameBench at the 60 fps display rate in every mode.
- Board, release `a49535f5` (bitstream `a13c8ed0`), render batches/s:
  testscale 60, testrendertarget 131, testsprite2 52, testgeometry 59.5,
  SDLFrameBench 59-60 fps. Remote desktop PASS, no render failures.

Open leads, in order of size:
- testsprite2's render-only batch spends ~2.3 ms before the engine starts
  (`copy_us`) against ~40 us for other batches. The helper's profile line
  now prints `bytes=`, `commands=` and `staged=` (uncommitted until
  verified) to tell a staged copy from a slow mapping.
- Per-request kernel cost (~4k guest instructions per syscall, 15 per
  request) and remaining TB hash lookups on user code after each switch.
- testrendertarget creates and destroys a 640x480 target every frame: a
  clearing FILL (2.1 ms hardware) and a 64 KiB draw-list area mapped by both
  processes, whose `kernel_area_map` walks ~2,600 mappings linearly.

Tools added: `~/astra-mg/sdl-prof.sh APP` (guest profile under the fake
helper), `probe.sh`, `sysc.sh` (syscalls by number), `/tmp/perfq2.sh` (board
perf of the vCPU thread; `/var/lib/astra/tools/perf`), and overrides for a
board A/B without publishing: `systemctl set-environment QEMU=...` or
`ROM=... ASTRA_BASE_STORAGE=... ASTRA_STATE_ROOT=<fresh dir>`.

## Done: batch validation, input gates (release `323ab243`)

- `9d02f09a` helper: guest batches are validated once, with word loads
  (the payload is non-cacheable; each byte load crossed the bus).
  testsprite2 52 -> 60, `copy_us` 2,300 -> 3.
- `36a4dae3` SDL: testkeys and testmouse ship as applications. `SDL_Log`
  goes to the system log. `test-sdl-input.py` gates both (testmouse through
  QMP drag, Shift-drag, wheel and close, checked in the decoded render
  batches); both run in `~/astra-mg/gates.sh` and pass in the full verify.
- Board, release `323ab243`: every demo at the 60 Hz display rate except
  testrendertarget (~45 fps, a full-size target created and destroyed
  every frame; see the open leads above). Remote desktop PASS.

**Next:** timers and threads (`testtimer`, `testthread`), then `loopwave`,
then Chocolate Doom. testtimer and testthread log through `SDL_Log` and
exit by themselves; `test-sdl-input.py`'s testkeys check (log lines, then a
clean exit after the last one) is the pattern. testthread waits for SIGTERM
after its first pass; read it before writing the gate.

## Done: timers and threads

- testtimer and testthread ship unchanged as applications (no window,
  no GUI capability). `emu/qemu/test-sdl-runtime.py --program NAME` gates
  both from their `SDL_Log` lines, collected across polls because the trace
  ring wraps under testtimer's callbacks; both run in `~/astra-mg/gates.sh`.
- QEMU, under the full parallel gate load: 1 ms timer 1.04 ms; 100/50/233 ms
  timers fired 99/299/64 times, none after removal; `SDL_Delay(1000)` read
  1000 ms by ticks, ticks64 and the performance counter. testthread: TLS per
  thread, 5 and 5 wake-ups, the self-raised SIGTERM handler slept, joined the
  second thread and exited 0. No port change was needed.
- testthread's handler line and thread #2's first line race (it raises
  SIGTERM right after creating the thread); the gate accepts either order.

## Done: loopwave in QEMU

- loopwave ships as an application with `sample.wav` and the PCM
  capability. `emu/qemu/test-sdl-audio.py` runs a stand-in for the Linux
  audio daemon on `ASTRA_AUDIO_HOST_SOCKET` (same socket protocol, 4096-frame
  voice queues drained at 48 kHz) and checks the captured voice against its
  own MS-ADPCM decode of the bundle's `sample.wav` (bit-exact with ffmpeg
  and SDL_wave.c). QEMU under full gate load: worst 0.5 s window
  correlation 0.991 across the loop boundary, +51 ppm, 0 queue underruns.
  Offline perturbations (one 1024-frame packet dropped, one repeated,
  samples byte-swapped) each fail.
- The +51 ppm is upstream SDL 2: `SDL_ResampleAudio` restarts phase and
  truncates the output length per chunk. Not an Astra fault.
- SDL plays silence while the device is paused; the gate aligns from the
  first non-silent frame.
- loopwave has no window. From the desktop it plays until killed; SDL's
  minimal config has no `HAVE_SIGACTION`, so SIGTERM is not SDL_QUIT.

## loopwave on the DE25: too slow (release `b16f86cb`)

- Capture: `fpga/de25/linux/audio_monitor.py` on the board records the
  daemon's monitor tap (the final mix, S16LE); `test-sdl-audio.py --heard
  FILE --wav WAV` checks it with the QEMU gate's comparison.
- Result: FAIL. Every 8,704 frames the mix is 8,192 frames of correct audio,
  then the daemon's 512-frame silent tail: an underrun about 5.5 times a
  second. The guest delivers ~94% of real time.
- Cause, profiled on beast (`test-sdl-audio.py --profile`): ~422 M guest
  instructions per second of audio. About 70% is libgcc soft-float
  (`__mulsf3` and `__addsf3` in compiler.library at 0x3ffdf000), called from
  SDL 2's float band-limited resampler (22050 to 48000 Hz). Userspace is
  `-m68040 -msoft-float` by contract, so every sample multiply-add is a
  library call.
- The documented direction (`AUDIO_ARCHITECTURE.md`) is that the Linux mixer
  converts and resamples, with formats that name rate and channels. Then SDL
  opens the device at the app's own rate and channels and does no float
  work on the guest. The alternatives are a hard-float userspace ABI (a
  locked contract; QEMU's FPU is itself softfloat) or patching SDL's
  resampler to integer arithmetic (changes upstream SDL).

## Done: conversion and resampling on Linux

User direction: do it all on Linux, over the existing channel, one
contract.
- `sw/include/astra/pcm_format.h` is the one contract (guest, QEMU, daemon).
  A format word is encoding | channels << 8 | rate << 12: every SDL2 sample
  format plus packed S24LE, 1-2 channels, 8-192 kHz. It rides OPEN's
  `value` through pcm.library, the media service and the AstraHost audio
  channel unchanged; no new transport. `ASTRA_AUDIO_HOST_FRAME_BYTES` is now
  `ASTRA_PCM_MAX_FRAME_BYTES` (8), and QEMU stages `pcm_format.h`.
- The daemon (`fpga/arty/linux/astra_audio_host.c`) decodes on WRITE and
  resamples each voice with a Kaiser-windowed sinc; 48 kHz stays bit-exact.
  Its self-test checks level and pitch at 8, 22.05, 44.1 and 96 kHz and
  rejection of 30 kHz in 96 kHz and 60 kHz in 192 kHz; a cutoff above
  Nyquist, a wrong phase step, or unnormalised taps each fail it.
- SDL's device takes the app's own spec. loopwave's QEMU gate is now
  bit-exact: S16BE mono 22050 Hz, exactly sample.wav looped.
- Guest cost of loopwave: ~422 M to ~20 M instructions per second of audio.

- DE25 (release `10102cba`): loopwave PASS, every 0.5 s window correlates
  at 0.992, +0 ppm, no FIFO underruns. But the daemon counted about one
  software gap (a 512-frame silent tail) per 60-100 s of playback.
  `/proc/<tid>/schedstat` sampled every 5 ms showed the vCPU thread idle
  (neither running nor runnable) for ~140 ms before a gap, so the guest
  stopped feeding; the host was not starving it.
- The guest's feed was BUSY-polling: SDL's WaitDevice waited for
  queued <= 4096 (the whole queue) and PlayDevice retried every 2 ms, each
  retry a full round trip. pcm.library 2.1 adds `astra_pcm_wait()`, which
  sleeps for the predicted drain time; SDL, the desktop chime and
  pcm-certify use it. Host requests for the same audio: ~15,000 to ~1,900;
  guest cost ~20 M to ~4 M instructions/s.

**Next:** re-measure the DE25 gap rate with `astra_pcm_wait`
(`fpga/de25/linux/audio_monitor.py` now prints the daemon's underruns,
overflows and gaps over the capture). If gaps remain, find what blocks the
guest for ~140 ms. Then Chocolate Doom.

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
