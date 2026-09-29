# Applications, SDL platform layer, and hardware sprites — design

Status: approved, 2026-09-26. Supersedes nothing yet; the sprite parts
correct stale statements in `GRAPHICS_ARCHITECTURE.md` and
`DESKTOP_AND_UI.md` (see §6).

## Why

An SDL program ran on the DE25 only inside a disposable `--test-app` image
whose startup manifest boots the display services and that one program — no
desktop, no remote desktop, no audio host. That is a gate harness, not a
system. The machine must run any installed application from the running
desktop with every service up, and the pieces that make that true must be
general: nothing below exists for one demo.

What is missing today (file:line facts from the survey):

| Gap | Where |
|---|---|
| Desktop shows exactly one hard-coded bundle | `services/desktop/main.c:31-32, 283-351, 452-466` |
| No enumeration of `/apps` anywhere | — |
| Image ships only `Terminal.app`, `InterfaceGallery.app` | `emu/qemu/astra_image.py:119` |
| Launch grants whatever a manifest names, including raw hardware capabilities the supervisor holds; unknown names are dropped silently | `supervisor/src/loader.c:528-547, 1905-1915` |
| Every bundle is a hand-copied make rule (seven in SDL alone) | `sdl2/Makefile:458-678` |
| SDL has no filesystem backend (`SDL_GetBasePath` = NULL) and reads files through POSIX stdio | `sdl2/prepare_source.py:64-67,125-134` |
| Upstream SDL demos needing `testutils.c` cannot link | vendor `Makefile.minimal:52` |
| Hardware sprite API is declared and stubbed; nothing owns the sprite engine | `ndk/src/graphics.c:1219-1257` |
| Sprites have no clip rectangle, no window binding, no wrap | `astra_sprite_line_builder.sv:219-227` |

## 1. Launch policy (supervisor)

The capability model should make installing an application safe, which the
current launcher does not.

- Two classes of entry: **system entries** (startup manifest: services,
  desktop) and **applications** (launched through `APP_LAUNCH`).
- An application may receive only application-class grants:
  `GUI CLIPBOARD PCM NETWORK NETWORK_LISTEN NTP POSIX_PROCESS APP_LAUNCH`
  and namespaces `APPS LIBS APP STORE HOME WORK TMP RAM CONFIG`
  (rights still capped by what the supervisor holds). Device, IRQ, clock,
  service-manager, event-control and raw-process capabilities are refused.
- A manifest that requests a refused or unknown capability fails the launch
  with `ASTRA_STATUS_ACCESS` and a logged reason naming the capability —
  never a silent drop.
- The same table is checked at image build time (`astra_image.py`) and by
  `astra-bundle check`, so a bad bundle never reaches a volume.
- One table, one header (`sw/include/astra/application_policy.h`), used by
  supervisor, bundle tool and image builder (generated for Python).

## 2. Application catalog (NDK)

`ndk/include/astra/application_catalog.h`, implemented once in the NDK:

- `astra_application_catalog_open(filesystem, &catalog)` lists `/apps`
  with `astra_filesystem_directory_*`, parses each `manifest`, keeps
  `kind application` bundles, sorts by display name.
- Entry: bundle path, id, display name, version, icon path (qualified).
- A malformed bundle is skipped and logged; it never hides the others.
- Consumers: desktop (icons), `open` (`open -l`, `open NAME`), and the
  Astra menu once it gains an application list. Rescanning on filesystem
  change notification is the upgrade path; bundles are installed at image
  time today, so a scan at desktop start is correct now.

## 3. Desktop

- Icon grid from the catalog; layout in `desktop_layout.h` generalised from
  one cell to rows × columns of the existing 80-px cells.
- Double-click launches that entry (`astra_application_launch`);
  failure alerts name the application.
- `workspace.new_terminal` stays as a command bound to the Terminal entry.
- Icons keep the existing coalesced-run drawing; a 64-px icon costs ≤128
  draw commands, so the grid is bounded by the window's draw-list capacity,
  which the layout checks.

## 4. Bundles and the image

- One reusable make function `astra_bundle` (in `tools/astra-bundle.mk`):
  name, executable, manifest, icon, resources list. Every app — Terminal,
  InterfaceGallery, SDL programs — uses it.
- `tools/aicon.py` gains a generic generated icon (monogram + colour from
  the bundle id) so an app without drawn art still gets a distinct icon.
- `astra_image.py` installs an explicit application set drawn from several
  build directories (system apps, SDL apps), validates each against the
  policy, and the release image includes them. `--test-app` remains for
  gates only.

## 5. SDL on Astra primitives

Overlay directories next to the existing `video/astra`, `render/astra`,
`audio/astra`:

- `filesystem/astra`: `SDL_GetBasePath` → `/app/` (the bundle, via the
  `APP` assign every launched application holds). `SDL_GetPrefPath` → the
  application's private `STORE` namespace (created per application by the
  supervisor), so preferences are isolated by capability, not by
  convention.
- `SDL_RWFromFile` → an Astra RWops over `filesystem_library` files
  (`astra_filesystem_file_*`: open, read, write, seek, size, close) instead
  of POSIX stdio. POSIX stays available to programs that ask for it.
- `SDL_test` gains the upstream `testutils.c`, so upstream demos build
  unmodified; they load `icon.bmp` and friends from `SDL_GetBasePath()`.
- Upstream demos shipped as ordinary applications: `testdraw2`,
  `testgeometry` (vertex-coloured geometry → texture engine),
  `testsprite2` (100 blended moving sprites → texture engine),
  `testrendertarget`, `testscale`. Resources copied into each bundle.

SDL's renderer draws into window surfaces through the blitter and texture
engine. That is "sprites inside a window" for SDL programs and needs no
hardware sprites.

## 6. Hardware sprites

Facts: 64 INDEX8 descriptors, 16 palette banks, priority, front/behind the
framebuffer, scaling, reflection, collision; clipped only to the whole
raster; coordinates are logical scene space. The pointer is a separate
32×32 ARGB plane (`astra_hardware_pointer.sv`), so all 64 sprites are free —
`GRAPHICS_ARCHITECTURE.md:426-436` and `DESKTOP_AND_UI.md:48-49`
("sprite 0 is the pointer") are stale and get corrected.

### Ownership and API
- The display service owns the sprite engine, its palettes and INDEX8 image
  memory in Media RAM, and allocates descriptors to clients.
- `graphics.h` sprite sets (already declared) get implemented: create a set
  of N sprites with images and palettes, update positions/flags in batches,
  present atomically with the window's frame. Descriptor indices never leave
  the display service.
- Transport: sprite updates ride the existing render batch / display
  mailbox to the ARM helper, which programs the descriptor registers at
  frame boundaries (the pending/active banks already exist in RTL).
  QEMU validates and counts them, like every other display request.

### Coordinate spaces
- **Screen** (default): logical scene coordinates, independent of windows.
- **Window-bound**: position is in the window's content coordinates. The
  display service translates by the window origin each frame, so the sprite
  moves with the window; it is clipped to the window's content rectangle.
  Optional **wrap** on X and/or Y makes the content rectangle a torus: a
  sprite crossing an edge reappears on the opposite edge.

### Hardware needed for window-bound sprites
- Per-descriptor clip rectangle: the line builder already clamps admission
  to `0..OUTPUT_WIDTH` and tests line visibility; the clamp takes the
  descriptor's rectangle instead. Descriptor word 7 (reserved zero) plus one
  new word hold left/top/right/bottom; a zero rectangle means "whole raster"
  so existing scenes are unchanged.
- Wrap: a sprite straddling an edge is emitted by the display service as up
  to four descriptors, each clipped to one side of the content rectangle.
  This spends descriptors rather than logic — the device has 875 ALMs free —
  and keeps the RTL change to the clip compare.
- Occlusion by other windows is the open design decision below.

## 6a. Shared foundations found missing on the way (phase 1)

- **String library** (`ndk/include/astra/string.h`, runtime.library
  `ASTRA_RUNTIME_1.9`, also in libastrart for the freestanding supervisor):
  a bounded builder (`AstraString`: append text, bytes, char, u64, i64,
  fixed-width hex; overflow recorded, cuts on UTF-8 scalar boundaries),
  whole-or-nothing copy and concat, prefix/suffix, ASCII case-folded
  compare, UTF-8 prefix. It replaced eleven private copies across the NDK,
  supervisor, POSIX layer, desktop, hostbench, audio-certify and the
  interface gallery, several of which wrote without a bound.
- **UI text in GPU draw lists**: `astra_draw_ui_text` (system.library
  `ASTRA_SYSTEM_2.5`) appends the system UI font to a session draw list,
  lowered to hardware glyph runs. The full `AstraFont`/`AstraTextLayout` API
  in `font.h` is still unimplemented and is its own project.
- **Icons as images**: `astra_aicon_strike_argb` expands an AICON strike to
  ARGB8888 for upload; the desktop blends icons from GPU surfaces instead of
  drawing them as colour runs, which could not fit more than one icon in a
  128-command retained list.
- **Application catalog** in filesystem.library (`ASTRA_FILESYSTEM_4.1`).

## 6b. Application identity, menus and project types (requested 2026-09-26)

User requirements:
- Every application has a menu while active. Today the menu bar shows
  "workspace" for any app, e.g. TestDraw2.
- An app that sets no menu items gets defaults. The application menu has at
  least "About {App Name}" (a standard About dialog, Mac-like) and
  "Quit {App Name}" (Alt-Q).
- The app's window title matches its menu name. The name is declared in one
  place in the app (the bundle manifest `name` is the natural source; the
  NDK/SDL take it from there rather than each program repeating it).
- Standard Astra project types (application, service, command-line
  program, kit) that a developer instantiates quickly, without an
  Xcode-scale project system: a template directory plus the shared make
  rules (`tools/astra-bundle.mk`, `program.mk`).

To design before building: where menu state lives (display service owns the
bar; the app publishes items through its window/GUI session), the default
item set and its protocol, how SDL apps get Quit as SDL_QUIT, and the
template layout.

## 7. Verification

- Host tests: policy table (every capability class, unknown names), catalog
  (malformed bundles, sorting, limits), desktop grid layout and hit testing,
  SDL filesystem/RWops against a fake filesystem, sprite translation/wrap
  splitting (every edge and corner).
- RTL: sprite clip rectangle in `tb_astra_sprite_*`, plus the sprite
  certification on hardware with clipped and wrapped scenes.
- QEMU gate with the **full desktop image**: boot, double-click an
  application icon through injected input, see it run while every service
  stays up, quit it, launch another.
- DE25: the release image, launched from the desktop, remote desktop
  connected, audio host running.

## 8. Order

1. Launch policy, catalog, desktop grid, reusable bundle rule, image
   application set. Gate, deploy.
2. SDL filesystem + RWops + testutils, upstream demos as applications.
   Gate, deploy, show.
3. Sprite ownership and screen-space sprite sets (no RTL change).
4. Window-bound sprites with clip rectangle and wrap (RTL + certify +
   timing closure), native sprite demo application.

## Decisions (2026-09-26)

- Window-bound sprites are occluded exactly like window content: drawn
  behind the framebuffer with the window scene providing the mask. This is
  the largest RTL change of phase 4; logic must be reclaimed first (875
  ALMs free — the texture engine's six attribute steppers are the first
  candidate).
- SDL demo applications ship in the release image.
- Order in §8 approved.
