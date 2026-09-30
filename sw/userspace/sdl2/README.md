# SDL2 on Astra

The SDL Kit will publish the individual upstream SDL2 libraries (core,
image, mixer, ttf, and net) as separately versioned Astra libraries. The
upstream source checkouts remain unmodified. `prepare_source.py` verifies the
pinned SDL2 revision and builds a disposable source overlay; it refuses a
changed revision, dirty checkout, or shifted integration point.
Core, SDL_net, SDL_image, SDL_mixer, and SDL_ttf are published as separate
versioned libraries in `SDL.kit`.

## Files and upstream demos

`filesystem/SDL_astrafilesystem.c` is SDL's filesystem backend.
`SDL_GetBasePath` is `/app/resources/`: the application bundle's resources,
reached through the `APP` namespace the supervisor grants every launched
application, as SDL answers for a bundle on other systems.
`SDL_GetPrefPath` is `/store/`, the application's private `STORE`; the
manifest must ask for `STORE:rw`, and org and app do not take part. Outside a
bundle, or without `STORE`, each returns NULL with an SDL error. File I/O
stays on SDL's stdio RWops, which reach the native VFS through the POSIX
adapter.

Upstream `testsprite2`, `testgeometry`, `testrendertarget` and `testscale`
ship unchanged as applications, built with upstream `testutils.c`, and find
`icon.bmp` and `sample.bmp` in their bundles through `SDL_GetBasePath`.
`emu/qemu/test-sdl-draw2.py --program NAME` is each one's gate: it renders,
quits on Escape and exits clean. `test-sdl-image.py` checks both paths,
including a write and read back through the preference path. Removing
`icon.bmp` from testsprite2's bundle fails its gate.

Upstream `testkeys` and `testmouse` ship the same way. `SDL_Log` reaches the
system log (the port patches `SDL_LogOutput`, as Android sends it to
logcat), so `emu/qemu/test-sdl-input.py` reads testkeys' scancode table from
the trace and requires its clean exit. testmouse reports nothing; it draws.
Its gate drives it through QMP and decodes the render-only batches a
stand-in display helper receives. A left drag must draw a red line whose
vector is the drag's, Shift held through a drag a red filled rectangle of
the drag's size, one wheel notch the green wheel line, and the close gadget
must quit it clean. Expecting any other colour fails the gate.

Upstream `testtimer` and `testthread` ship too; they open no window, log
through `SDL_Log` and exit by themselves. `emu/qemu/test-sdl-runtime.py`
collects their lines across polls, since the trace ring wraps under
testtimer's callbacks. testtimer's 1 ms timer must average 0.9-1.5 ms, the
100, 50 and 233 ms timers must fire 95-101, 285-301 and 61-65 times and
never after removal, and a 1 s delay must read 1000-1050 ms by ticks,
ticks64 and the performance counter. testthread must keep thread-local data
per thread, join its first thread on a flag, and run the SIGTERM handler it
raises (a 5 s sleep, then stopping and joining the second thread) to a
clean exit.

Upstream `loopwave` ships as a command (`/commands/loopwave FILE.wav`, built
by this port and installed by `sw/userspace/commands`). SIGINT and SIGTERM
become `SDL_QUIT` here as on every Unix port (`HAVE_SIGACTION`), so Ctrl-C in
the Terminal ends it through SDL's own quit path. For SDL's `sample.wav`
(MS-ADPCM, mono, 22050 Hz), SDL decodes to S16BE mono 22050 Hz and the device
takes exactly that: the Linux audio host resamples. QEMU has no physical sink, so
`emu/qemu/test-sdl-audio.py` points QEMU's host audio provider at a
stand-in for the Linux audio daemon: the daemon's socket protocol and
4096-frame voice queues, each drained at its own rate. The gate installs
`sample.wav` as `/home/sample.wav`, types `loopwave /home/sample.wav` in the
Terminal, decodes the file itself, bit-exact with `SDL_wave.c`, and requires
loopwave's stream to be that format and exactly those samples, looped past
a loop boundary; then Ctrl-C must close the stream and exit 0. `--heard` checks a DE25 capture of the daemon's final
48 kHz mix (`fpga/de25/linux/audio_monitor.py`) by correlation, which also
catches an underrun's inserted silence.

## FPGA renderer

SDL drawing now reaches the FPGA through Astra's managed graphics API; see
`docs/MANAGED_GRAPHICS.md`.

**Windows.** Every SDL window is an `ASTRA_WINDOW_CONTENT_SURFACE` window,
and its content is a GPU surface. The window framebuffer path
(`SDL_GetWindowSurface`) renders into the display's staging area and writes
the damaged rectangle with one service copy.

**Renderer.** The `astra` render driver (`render/SDL_astrarender.c`) is
registered ahead of the software renderer. Textures are Media RAM surfaces in
ARGB8888, RGB888, or RGB565, and any of the three can be a render target.
Draws become ADLT v1.5 commands that execute as render-only batches:

| SDL operation | Lowered to |
|---|---|
| Clear, fill, points | FILL with the SDL blend mode |
| Opaque lines | LINE |
| Blended lines | Bresenham row spans of blended fills |
| Copy, and CopyEx at 0 or 180 degrees | BLIT: nearest or linear scaling, flips, color and alpha modulation, and the SDL blend mode |
| CopyEx at any other angle | two textured triangles around the center, mirrors applied to the texels |
| `SDL_RenderGeometry` | triangles: indices expanded, per-vertex colors, uv times the texture size, render scale |
| `SDL_RenderReadPixels` | `astra_surface_read` of the target; `SDL_ConvertPixels` only when another format is asked for |

`SupportsBlendMode` accepts NONE, BLEND, ADD, MOD, and MUL; composed custom
modes are refused so SDL reports them. Color modulation, ADD/MOD/MUL, linear
scale mode, rotation, and geometry run on the texture engine
(`docs/TEXTURE_ENGINE.md`); plain and alpha copies keep the blitter. A
visible vertex beyond +-32768 pixels fails the draw with an error rather than
being distorted; triangles wholly outside the target are culled.

The driver sets `SDL_HINT_RENDER_LINE_METHOD` to "2" at default priority, so
SDL does not expand lines into points.

Nothing falls back to MC68040 pixel work. Formats the driver does not list,
including YUV, are converted by SDL on upload.

**Tests.** Host tests `test_astravideo` and `test_astrarender` cover:

- viewport and clip
- culling
- blended line spans, including opaque ADD
- the 180-degree mirror and arbitrary rotation vertices
- geometry indices, colors, texel coordinates, and the vertex range
- blend-mode, modulation, and linear-filter routing
- readback with and without conversion
- texture locking and ARGB8888 targets

QEMU renders nothing, so the gates check submissions and command counters;
pixels need the texture-engine RTL and the DE25.

The audio backend runs over `pcm.library.2`. It
opens the app's native `PCM` capability and opens the device at the app's
own sample format, channels and rate (the Linux host converts and
resamples; SDL converts only more than two channels or a rate outside
8-192 kHz), and honors PCM queue backpressure by waiting for room with
`astra_pcm_wait()` rather than polling. `make test` on Beast checks
granted/missing/broken PCM cases, the unchanged upstream source, and MC68040 compilation. `make core` builds
the full upstream SDL2 core and test archives with Astra's PIC/ABI flags;
`make shared` links and checks the Kit library using upstream's export list.
`make build/m68k/testaudioinfo` links SDL's unchanged upstream audio-info
example to that library and checks its dynamic dependency contract.
The same build rule covers upstream `testver` and a small public-API
`sdlprobe`. In a fresh Beast-hosted QEMU image containing `SDL.kit`, upstream
`testver` exits zero and `sdlprobe` selects the `astra` audio driver through
the shared library. An image without the Kit fails to load the same probe
with status 34, even if a bare SDL2 ELF is copied into `/libs`.
Upstream `testaudioinfo` opens an audio device during setup; host QEMU has no
PCM provider, so its failure there is expected. Terminal now requests PCM so
audio commands can use a provider when one is available. Playback and a
physical DE25 runtime gate remain open.

The core probe now builds after adding native per-thread priority and POSIX
`pthread_getschedparam`/`pthread_setschedparam`. Per-thread signal
masks and handler stacks are implemented in Astra's native kernel, with POSIX
mask inheritance in `pthread_create`. POSIX thread attributes validate and
retain a requested stack size within Astra's guarded, demand-grown native
reservation; this changed the public pthread attribute layout, so libc and
dependent Kits use ABI 2. SDL uses its generic semaphore implementation over Astra-backed
pthread mutexes/conditions, so a separate POSIX semaphore implementation is
not needed for this port. SDL's optional realtime priority request still
needs target audio-thread admission/latency validation; it is not silently
treated as success. The native display service accepts RGB565 window surfaces
and submits damaged pixels as hardware blits. An Astra SDL backend now creates
windows, presents surface updates, and translates mouse, keyboard, text, wheel,
and window events. Beast host adapter tests and MC68040 compilation pass. An
unchanged upstream `testdraw2` builds against the shared library. The shared
POSIX entry adapter now acknowledges startup for long-running upstream apps;
otherwise Supervisor waits forever for an app that never sends its native
ready message. A fresh Beast-hosted QEMU image auto-launched a public-API SDL
video probe: it logged
`SDL_VIDEO_READY` after presenting its window, and the QEMU display model
completed all six submissions, including 63 blits. The same image with
unchanged upstream `testdraw2` rendered continuously, received Escape, and
exited with status zero. The gate rejects an older image in which SDL window
cleanup killed the display service. SDL now supplies the required `VideoQuit`
callback and updates its internal keyboard focus from native focus events;
the display service accepts closing a minimized window without pixel damage,
and presents the cursor for a window change only when the shape under the
pointer changed. QEMU counts commands but does not render a
framebuffer, so pixel appearance and physical DE25 performance remain
unverified. Initial `SDL_WINDOW_HIDDEN` creation also needs native visibility
support.

Upstream examples in the sibling SDL2 checkout's `test/` directory provide
progressive port gates without
changing SDL sources: `testver` for core linking (passed in QEMU), `testthread`
and `testtimer` for runtime behavior (both passed in QEMU), `testdraw2` for
window rendering,
`testkeys` and `testmouse` for input (both passed in QEMU), and `loopwave`
for PCM playback (passed in QEMU against a stand-in host daemon). Follow
them with `testsprite2` for moving graphics and Chocolate Doom for a real-game
integration gate. The DE25 visual, input, and audio gates remain open.

SDL_net 2.4.0 is pinned in `prepare_addon_source.py`; its clean upstream tree is
copied unchanged into a disposable build overlay. Unchanged upstream `chat`,
`chatd`, and `showinterfaces` build and link against the shared libraries; their
interactive runtime gates remain open. The POSIX `gethostbyname`,
`gethostbyaddr`, and `SIOCGIFCONF` adapters use Network Kit's native resolver
and interface operations. Network Kit delegates DNS, interface discovery, and
TCP/UDP sockets to the Linux host; Astra has no second IP stack. Beast host
positive/negative tests pass, and a fresh Beast-hosted QEMU image ran the
`SDLNetProbe.app` gate through forward and reverse DNS, interface discovery,
and UDP loopback (`SDL_NET_READY`). DE25 networking remains unqualified.

SDL_image 2.8.12 is pinned in `prepare_addon_source.py` and built unchanged as
`SDL2_image.library.2`. The Kit enables its dependency-free BMP, GIF, JPEG,
LBM, PCX, PNG, PNM, QOI, SVG, TGA, XCF, XPM, and XV readers, using upstream
stb_image for JPEG/PNG. AVIF, JXL, TIFF, and WebP require separate codec
dependencies and are not enabled. PNG and JPEG saving use upstream's built-in
miniz and tiny_jpeg encoders. SDL2's shared
stdio RWops use Astra's POSIX file adapter. A fresh Beast-hosted QEMU image
loaded upstream PNG, JPEG, and QOI fixtures, also loaded a PNG with a misleading
`.jpg` name, round-tripped PNG and JPEG through the save APIs, and rejected
invalid saves, corrupt data, and missing inputs. This is the SDL
compatibility API; Astra's planned generic Codec Kit remains separate so
native applications need not depend on SDL or understand per-format contracts.

SDL_mixer 2.8.2 is pinned in the same disposable add-on overlay and shipped
unchanged as `SDL2_mixer.library.2`. Built-in WAV/AIFF/VOC, minimp3, stb Vorbis,
and dr_flac readers avoid external codec libraries. A fresh Beast-hosted QEMU
image passed ten consecutive `SDLMixerProbe.app` runs that loaded and started
MP3, OGG, and FLAC music, rejected invalid WAV/music bytes, and started 16
simultaneous WAV channels using SDL's dummy audio device. That gate tests SDL's
mixing API, not the DE25 PCM output. MIDI instruments, tracker formats,
physical playback, and game-level audio remain unqualified. The gate caught
an intermittent deadlock in the shared POSIX mutex adapter; the adapter now
uses Astra's native mutex and has deterministic wakeup regressions.

SDL_ttf 2.24.0 is pinned with its upstream FreeType submodule and shipped as
`SDL2_ttf.library.2`. FreeType is cross-built unchanged, with its static code
private to the SDL_ttf library; HarfBuzz text shaping and optional FreeType
compression libraries are not enabled. The Beast-hosted QEMU probe rendered
the existing Atkinson Hyperlegible Next TTF through the public SDL_ttf API,
measured the result, and rejected missing and corrupt fonts. This does not
establish visual quality or font performance on the DE25.
Portions of SDL_ttf use the FreeType Project's font engine; the SDL Kit
includes the SDL_ttf and FreeType license texts.
