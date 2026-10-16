# Graphics pixel-writer audit

Status: DE25 production activated, 2026-10-16; port-specific offload remains.
Font-service work waits for this audit's native-rendering gates. Goal: offload graphics work wherever Astraea has a
correct, useful path, while keeping CPU free for layout, game logic, and
work the current hardware cannot perform. Haiku reference reviewed read-only:
`src/servers/app/drawing/DrawingEngine.cpp`,
`src/servers/app/drawing/BitmapDrawingEngine.cpp`, and
`src/servers/app/drawing/interface/remote/RemoteDrawingEngine.cpp`.
Haiku separates drawing requests from backend execution; Astra keeps that
separation but makes Astraea the sole destination pixel writer for native UI.
Haiku's `src/servers/app/font/FontCache.cpp` and `FontCacheEntry.cpp` cache
glyphs by style and size. Astra prepares built-in style strikes when AFNT is
compiled into Font Kit, before frame construction.
For icon changes, `src/kits/tracker/IconCache.cpp` was reviewed read-only:
cache reusable icon sources, then draw from them rather than rebuild pixels
on each repaint. Astra retains Media RAM source surfaces for that purpose.

## Contract

The MC68040 computes layout, clipping, source selection, and command lists.
Astraea performs rasterization, coverage blending, blits, and writes to window
and scanout surfaces wherever its commands can do so correctly. Source-file
decoding and one-time conversion to AFNT or texture data prepare assets.
Remaining CPU pixel work needs a concrete hardware limitation and a measured
reason to retain it; it must not be a hidden fallback on native UI paths.

## Source inventory

| Path | Current work | Disposition |
| --- | --- | --- |
| `sw/boot/splash.c` | Astraea copy and text commands | Hardware path; retain |
| `sw/userspace/services/display/main.c`, `window_graphics.c` | Build/submit Astraea batches; accept draw lists and uploaded client frames | Native compositor hardware path; audit source uploads separately |
| `sw/userspace/services/desktop`, `terminal`, `applications/atlas`, `applications/interface-gallery`, Interface Kit alert | Draw-list window content | Hardware path; Desktop scrollbar converted |
| `sw/userspace/graphics/src/surface.c` | RGB565 software-surface primitives and draw-list emitters | Native UI uses draw lists; keep explicit software-surface compatibility and host oracle |
| `sw/userspace/interface/src/control.c`, `menu.c` | Draw-list-only production controls and menus | Converted; reject RGB565 destinations |
| `sw/userspace/graphics/src/render_builder.c` | Lowers commands to Astraea using prepared AFNT-derived bold/italic strikes | Converted; no per-frame coverage synthesis |
| `sw/userspace/graphics/src/aicon_surface.c` | Upload AICON indices and palette metadata; Astraea expands and blends desktop/Atlas icons | Converted |
| `sw/userspace/system/src/bundle.c`, `sw/userspace/services/display/main.c` icons | Parse AICON; upload index and palette source for title bars and launch panel | Converted; Astraea expands and blends both icon paths |
| `sw/userspace/sdl2/render/SDL_astrarender.c` | Hardware geometry, fills, lines, and blits; client texture uploads are source data | Retain general hardware drawing; audit each upload's producer and add palette-bearing indexed sources through managed graphics |
| `sw/userspace/sdl2/video/SDL_astravideo.c` | Exposes RGB565 or INDEX8 writable SDL framebuffer and prepares cursor image | INDEX8 palettes now expand in Astraea; retain RGB565 compatibility and measure residual CPU work |
| SDL clients that produce framebuffer pixels, including current Chocolate Doom | Upload finished CPU-rendered frames | Move feasible drawing to general SDL/Astraea commands; keep documented CPU work when hardware cannot reproduce it correctly |
| `sw/userspace/graphics/src/texture_reference.c` | CPU texture reference | Host tests only; never linked into target libraries |
| `fpga/de25/linux/astra_font_convert.c`, `tools/fonts/afnt.py` | Import/build-time font-to-AFNT conversion | Asset preparation; no frame rendering |

Chocolate Doom and SDL are in scope. No Doom-specific hardware or SDL command
is needed. SDL cannot recover scene geometry from a finished pixel buffer:
moving that work requires the client to submit operations through general
accelerated APIs. Existing gameplay remains available until a new path passes
equivalent visual and gameplay checks. Streaming texture uploads and writable
window surfaces stay supported: DevilutionX needs an indexed window surface,
while stock ScummVM streams RGB565 frames. Indexed presentation is the first
shared offload candidate. ScummVM's CPU INDEX8-to-RGB565 conversion remains
until its client path presents indexed pixels through standard SDL APIs; SDL
cannot infer palette indices after that conversion. The port audits record
both paths in `DEVILUTIONX_PORT.md` and `SCUMMVM_PORT.md`.

The first port-driven change needed no new FPGA block: managed graphics now
supplies Astraea's existing INDEX8 palette path, and SDL offers an indexed
window surface while retaining RGB565 streaming textures. Beast host, MC68040
and QEMU render-probe gates pass; current QEMU probe observed 17 palette blits
and 65/65 completed submissions, with its indexed-ready marker present. A
separate full-desktop QEMU gate opens Interface Gallery, Atlas, and SDL
TestDraw2 with indexed icon blits. The current 8-second frame workload records
11,433.9 display submissions/s against the prior 11,348/s baseline.
DE25 HDMI now passes the native UI and Doom candidate gates below. Actual
DevilutionX/ScummVM workloads remain unqualified. Scaling is a separate
bottleneck; qualify it on both ports before spending RTL resources. The retained DE25 route has
5,946 ALMs and 41 RAM blocks nominally free, but only +0.130 ns worst setup
slack, so resource counts alone do not qualify an expanded engine.

Font Kit 2.3 holds pre-rasterized built-in bold, italic, and combined strikes
derived from AFNT at build time. Builder selects matching metrics, copies
source bytes, and submits Astraea glyph jobs. Beast pixel-oracle, sanitizer,
analyzer, MC68040 library, and full userspace tests pass. Current font library
is 1.1 MiB; styled Gallery text renders correctly on the physical DE25.
Installed-font
style cache and user-selected roles remain separate Fonts work. Direct RGB565
methods remain for explicit software-surface clients and host reference tests;
there is no native UI fallback to them.

## Release gates

1. Production native UI sources use Astraea command paths for rendering;
   direct RGB565 methods serve only explicit software-surface clients and
   reference tests, with no hidden native UI fallback.
2. Styled draw-list text uses prepared AFNT strike data and Astraea glyph jobs;
   no per-frame CPU glyph bitmap synthesis.
3. Host protocol tests, sanitizers, MC68040 builds, and QEMU render tests pass.
4. DE25 HDMI captures and repeated Desktop, Atlas, Terminal, menu, and styled
   text interaction checks pass before release activation.
5. SDL keeps correct streaming/window-surface behavior while clients use
   general hardware drawing commands for feasible visible work. Current Doom
   gameplay, UI, transitions, and audio pass DE25 checks before its CPU frame
   work is replaced.
6. Indexed surfaces carry their palette to Astraea through a general managed
   graphics contract; SDL can present indexed window frames without expanding
   every pixel into RGB on CPU.

No production-image or user-data writes are part of this audit.

## 2026-10-16 physical candidate gate

Beast built and verified immutable release
`57078599bcd151e88eb01c3767635875d89e154cebf6183fb69d41b6b1286421`
from source manifest
`e1d6324b734baa6f888bc0fc9b542434ba08f9bff32274bba6ca419f54c9e96a`.
The board ran it with an isolated state image and exports, reached POST/stage 8,
and returned to the unchanged production selector after the trial. Physical
1920×1080 HDMI DMA captures show desktop, Atlas root, Gallery styled text,
and live Doom. Raw capture SHA-256 values are respectively
`37d1bbfe3df5fbb5b0c3a8d6b278724f1222aefd572bd6e52cd3aa1534b3b091`,
`eee5a2c566d248edd42f2b12fa7200718295d9e575ee542e0d440beb13e01a2b`,
`b2d82bd20cf0c2003c0e54cb1395615c15bf45e6920f4152a39bbed5879d60be`,
and `51a70304589e27c5106d8c8272da86cc3911bc53a3bd4864676427a50c5e7f6d`.
The final Doom trial closed normally and restored the desktop cursor. At exit,
hardware reported 23,244/23,244 display submissions/completions, 32 palette
blits, 178 glyph commands, and zero guest panics or degraded-system messages;
audio and remote host services had zero automatic restarts. A second isolated
run of the same candidate captured Terminal over animated SDL TestDraw2 and
the DH0 right-click menu; closing the SDL window settled at 17,512/17,512
hardware submissions/completions, again with zero panic or automatic host
service restarts. Raw Terminal/SDL and menu capture SHA-256 values are
`4fca412f38ba20ea6f49bc582d9f34ef69776984497ef6532299a09b94b3965c`
and `2707beb6832c54bdeb750fb3aeebcb55bf37a049eceff052a9e93cbfd88d253f`.
Evidence stays in `/var/tmp/astra-graphics-trial.wZ7G6h/` and
`/var/tmp/astra-graphics-trial.OfgQwT/` on DE25 and
`/var/tmp/astra-graphics-release-final-build.log` on Beast.

The SDL INDEX8 window path also passed a separate physical DE25 probe using a
disposable image and isolated exports. The guest emitted `SDL_INDEXED_READY`;
Astraea reported four triangle commands, three surface reads, 19 palette
blits, 62 render-only operations, and 77/77 submissions/completions while the
window was open. Pausing QEMU for a 1920×1080 HDMI DMA capture preserved the
live two-color window. Captured upper/lower pixels were `(24,223,57)` and
`(24,81,222)`, the RGB565-quantized output of requested `(30,220,60)` and
`(30,80,220)`. Raw frame SHA-256 is
`a2431151cea8c717b1962c8616b9b5d237059889eddc7c915b1cc52ffaae6ba3`;
evidence remains under `/var/tmp/astra-render-probe-20261016/` on DE25 and
`/home/barry/astra-graphics-cutover-20261016/` on Beast. The production
selector and user state were unchanged by the probe; all three production
services returned active at stage 8 with zero automatic restarts.

Animated direct capture occasionally returned `EAGAIN` under load; pausing
QEMU for DMA capture produced valid frames. One earlier trial with host audio
and remote services deliberately absent panicked after repeated guest remote
desktop exits; it is retained as a separate reliability defect, not counted as
a graphics pass. The normal-host trial and final trial showed no such panic.
Production activation and user-data migration passed. A stopped source-28
image and WORK/SOUND export backup were copied to Beast and matched by SHA-256.
Installing the candidate's system products into a clone retained all 25
existing `/home`, `/local`, and `/config` entries, including content and inode
metadata; the same check passed after a physical DE25 boot and clean offline
fsck. Normal production boot and warm restart reached stage 8 with runtime,
audio and remote services active and zero automatic restarts. QEMU remains
unprivileged (UID/GID 999, no groups/capabilities, `NoNewPrivs=1`). Final
1920×1080 HDMI DMA capture passed and matched the earlier desktop frame SHA-256
`37d1bbfe3df5fbb5b0c3a8d6b278724f1222aefd572bd6e52cd3aa1534b3b091`.
The source-28 rollback release and its state remain selected as `previous`;
backups are under `/var/backups/astra/graphics-cutover-20261016/` on DE25 and
`/home/barry/astra-graphics-cutover-20261016/` on Beast. The older rare
`WAIT_MULTIPLE` dispatch panic and intermediate-candidate deferred-worker panic
remain separate reliability investigations; neither recurred in the selected
release's bounded DE25 trials.
