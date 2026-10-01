# Handover 2026-10-01: Chocolate Doom bring-up

Read `CLAUDE.md`, `AGENTS.md`, then this page. The previous handover is
`HANDOVER_2026-09-30_SDL_PERF.md`; the audits are `CHOCOLATE_DOOM_PORT.md`
and `DEVILUTIONX_PORT.md`.

## Direction (user)

Get Doom running, then make it correct, then fast. Unmodified upstream
source; every missing piece built once, reusably, in the Astra layer that
owns it. Decisions taken:

- **No software rendering, ever.** The 68040 cannot do it.
- **Apps are self-contained bundles** (like the Mac): Doom's bundle carries
  the game, `doom1.wad` (shareware), and its config.
- **Dynamic linking**, newer library versions backward compatible. The
  existing model (`requires NAME ABI MINIMUM.VERSION`, provider index picks
  the newest compatible payload per ABI major) is Amiga-style minimum
  version plus an explicit ABI break boundary; keep it.
- **Float:** fix SDL float hot spots first; userspace may use the 040 FPU
  (kernel FPU context + QEMU FPU still to do).
- **Every app starts in its bundle** and may reach `LIBS:` and `CONFIG:`.
- **Terminal commands may open windows.**
- **Audio 44.1 kHz.** Doom only until the kinks are out; then polish and
  UX before onboarding the next program, so the next port is smoother.
- DevilutionX was audited alongside (`DEVILUTIONX_PORT.md`).

## Done in this session

- **SDL renderer vsync** (`sdl2/render/SDL_astrarender.c`): the driver
  advertises `PRESENTVSYNC`; `SetVSync` subscribes the window to VBLANK and
  a present waits for the first vblank since the previous one (100 ms cap).
  Host test covers it.
- **No software renderer** on Astra (`prepare_source.py` drops it from
  SDL's driver list). The render probe asks for "software" by name and must
  fail, then asks for vsync and must get "astra"; `test-sdl-render.py` PASS.
- **Apps start in their bundle** (`supervisor/src/loader.c`): every bundle
  launch gets `CWD` = the bundle's `resources/` (read-only) and its private
  `CONFIG:`; `LIBS:` was already universal. Proven by Doom finding
  `./doom1.wad` with no arguments.
- **Terminal forwards GUI** (and PCM) to child commands.
- **stdout/stderr without a stream go to the system log**
  (`posix/src/console.c`, `POSIX_DESCRIPTOR_LOG`, line-buffered: one log
  call per line). An application's diagnostics are no longer lost; Doom's
  startup banner is ~35 lines, once.
- **`tools/aicon.py` reads PNG** (upstream ports ship PNG icons); checked
  pixel-for-pixel against `sips`.
- **SDL fullscreen**: see "Resolved" below (a display window state, not a
  window type fixed at creation).
- **Chocolate Doom port** (`sw/userspace/ports/chocolate-doom/`): pinned
  upstream 3.1.1, `autoreconf` + upstream `configure` + upstream Makefiles,
  unmodified; dynamic link against SDL2, SDL2_mixer, SDL2_net, libc,
  runtime, compiler; `doom1.wad` fetched from Ubuntu's doom-wad-shareware
  package, SHA-256 pinned (canonical md5 `f0cefca4...`), licence shipped.
  `ChocolateDoom.app` is in the default image (`astra_image.DEFAULT_APPS`).
  Workaround with a named exit: upstream repeats LDFLAGS in LDADD and the
  linker takes one `-T` and one crt0, so both ride in LIBS until the
  compiler driver applies Astra's dynamic link itself (audit item 3).
- **Gate** `emu/qemu/test-chocolate-doom.py`: boots with the app, stand-in
  display helper and audio daemon, requires render batches, a 44.1 kHz
  stereo voice with sound, no faults; reports Doom's own log on failure.

## Where Doom stands

**Doom runs in QEMU.** `test-chocolate-doom.py` PASS: ~175 render batches/s
over 30 s, 44.1 kHz stereo audio with sound, no faults. The gate is in
beast's `~/astra-mg/images.sh` (`doom.img` = base + `--test-app
ChocolateDoom.app`) and `gates.sh`. Not yet run on the DE25.

## Resolved in the second half of the session

- **Display service died when a window client exited without closing its
  window** (`critical service display exited: status 34`). Not a vblank
  problem: the kernel's event signal already coalesces. `astra_wait_multiple`
  returns `PEER_DEAD` with the index when a waited port's peer dies, and the
  serve loop treated every non-OK wait as fatal, so `receive_command`'s
  existing close-on-`PEER_DEAD` path was unreachable. Now a terminal status on
  a client-owned source (window control port, pending event send) goes to its
  handler; the display's own sources stay fatal
  (`display_wait_client_ended`, host-tested). The display also logs the
  status of each fatal wait.
- **Render probe**: SDL2's `SDL_HINT_RENDER_DRIVER` is a preference; an
  unknown name falls back to any driver. Asking for "software" therefore gets
  "astra" (software is compiled out), and the probe now requires exactly that.
  The old expectation (NULL) made the probe exit early, which is what exposed
  the display bug.
- **Doom's `Astra window creation failed (-9)`**: SDL2 does not pass
  `SDL_WINDOW_FULLSCREEN` to `CreateSDLWindow` (it is not in `CREATE_FLAGS`);
  it sizes the window to the display and calls `SetWindowFullscreen` after.
  Astra got a standard 1920x1080 window at y=0, `valid_open` refused it, and
  the display's private status 37 reached the client as `ASTRA_ERROR_IO`.
  Fix: **fullscreen is now a display window state** (`ASTRA_GUI_WINDOW_FULLSCREEN`,
  `ASTRA_WINDOW_STATE_FULLSCREEN`, `astra_window_fullscreen`, system.library
  2.7). The window becomes the FULLSCREEN type while it lasts (whole display,
  no chrome, above the bars, raised); `RESTORE` returns its type, state and
  frame. SDL creates the native window at `window->windowed` and implements
  `SetWindowFullscreen`, so runtime toggling works the same way.

## State at handover (read first)

Committed in logical pieces after a green full verify (see `git log`).

## Order of work for the next session

1. Board: publish, run Doom on the DE25.
2. "Correct": compiler driver dynamic default + SDL `.pc`/CMake packages
   (removes the LDFLAGS workaround), Alt-Enter check on the machine, relative
   mouse, bundle-shipped default config, audio latency bound. A service
   replying with a program-private status (>= 32) reaches clients as
   `ASTRA_ERROR_IO`; the display's open refusals should use the shared
   vocabulary (`ASTRA_STATUS_INVALID`/`LIMIT`) and say which check refused.
3. "Fast": linear/downscale BLIT in pixel mode, SDL float hot spots
   (SDL_mixer panning, first-play conversion), userspace FPU.

## Other results from this session

- **Logging volume** (user asked): Doom logs its ~35-line startup banner
  once, then nothing during play; each line is one `astra_log` call (the
  log descriptor buffers to newline, so Doom's dot-per-flush progress lines
  are one call each). The "trace ring wrapped" gate errors were a bug in
  `test-sdl-runtime.py`'s `Log`: it judged wraps by the newest text record,
  while the ring also carries kernel events (mostly the remote desktop being
  relaunched every few seconds under QEMU). Fixed: it tracks the newest
  record of any kind. The Doom gate uses `Log(strict=False)`.
- **DevilutionX audit**: `docs/DEVILUTIONX_PORT.md`. Its hard SDL gap is
  the joystick and game controller subsystems being compiled out (fatal at
  `SDL_Init`); then C++23 delivery (driver dynamic link, CMake toolchain
  file, SDL CMake packages), SDL_audiolib's float pipeline, scaled present,
  a palettized (INDEX8) window framebuffer, SDL cursors and warp.
- **User direction during the session**: SDL needs a real fullscreen mode
  (done: a display window state, so creation and Alt-Enter share one path).

## Tools

- Beast's `~/astra-mg/images.sh` builds `doom.img`; `gates.sh` runs the
  Doom gate with the others.
- Reading the live trace ring while a machine runs: `Machine.trace()` from
  `test-terminal.py`, polled until QEMU exits, shows every process's log
  lines (process id in lowercase hex). The panic dump on serial shows only
  part of the ring, and the event store on disk is written later than a
  crash, so neither replaces it.
- Chocolate Doom checkout: `~/Git/chocolate-doom` on both machines, clean
  at the pinned revision.
