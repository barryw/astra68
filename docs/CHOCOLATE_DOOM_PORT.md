# Chocolate Doom on Astra: port audit

Status: audit only (2026-09-30). Nothing below has been built or run except
where stated. Target: Chocolate Doom 3.1.1 (`~/Git/chocolate-doom`, tag
`410d968`), built from unmodified upstream source. Every missing piece is
implemented once, in the Astra layer that owns it (SDL port, native library,
supervisor, toolchain, host), for every program that needs it.

Earlier evidence: `CURRENT_STATE.md` records that every chocolate-doom object
cross-compiled on beast against Astra SDL2, SDL_mixer and SDL_net; the link
failed, and nothing ran.

## Requirements matrix

| Area | Chocolate Doom uses | Astra | Verdict |
|---|---|---|---|
| Build | autotools or CMake; pkg-config for SDL2, SDL2_mixer, SDL2_net; optional libsamplerate, libpng, fluidsynth | SDL headers only under the port's `build/vendor`; no `.pc`, `sdl2-config` or CMake packages; `m68k-astra-gcc`'s specs default to a **static** hosted link | PARTIAL |
| Video | `SDL_RENDERER_TARGETTEXTURE \| PRESENTVSYNC` (`i_video.c:1271-1283`); 8-bit surface to ARGB8888 streaming texture, nearest into a target, **linear** copy to the window (`smooth_pixel_scaling`, on by default, `:133`); `FULLSCREEN_DESKTOP` by default | Astra renderer advertises `ACCELERATED \| TARGETTEXTURE` only (`SDL_astrarender.c:1145`), so SDL picks the **software** renderer (`SDL_render.c:1026`); nearest scaled BLIT in pixel mode (~1.6 cycles/px); linear scaling on the texture engine (~2.2 Mpx/s); no backend fullscreen | PARTIAL |
| Input | scancodes, text input, relative mouse (`i_input.c:443`), warp; joystick off by default | keys, text, wheel, buttons, window events; no relative mode or warp; joystick disabled | keyboard PRESENT, mouse PARTIAL |
| Audio | SDL_mixer at 44100 S16SYS stereo; `Mix_SetPanning` per sound; music defaults to OPL (Nuked OPL3, integer, in `Mix_HookMusic`); GENMIDI writes `/tmp/doom.mid` for SDL_mixer MIDI | device takes 44.1 kHz S16BE as is, host resamples; SDL_mixer has WAV/OGG/MP3/FLAC, **no MIDI**; SDL_mixer panning and SDL's first-play sample conversion run in float (soft-float on the guest) | SFX + OPL music PRESENT (CPU cost unmeasured); MIDI MISSING |
| Files | IWAD search (`d_iwad.c:636-770`: `DOOMWADDIR`, `DOOMWADPATH`, cwd, `$HOME`...); config and saves via `SDL_GetPrefPath`; stdio, stat, mkdir, remove, rename, opendir; `/tmp`; mmap optional | `SDL_GetPrefPath` = `/store/` with `STORE:rw`; POSIX covers every call; `/tmp` with `TMP:rw`; no mmap (Doom falls back to stdio) | saves PRESENT; **IWAD unreachable from a desktop launch** |
| Timing | `SDL_GetTicks`, `SDL_Delay`, SDL mutex/cond | testtimer 1.04 ms, pthreads | PRESENT |
| Network | SDL_net UDP for `-server`/`-connect` | SDL_net shipped, QEMU gate passes | PRESENT (DE25 networking unqualified) |
| libc | stdio, `strcasecmp`, `strdup`, `getenv`, `putenv`, `isatty`, `usleep`, math (`atan`, `sin`, `tan`, `ceil`, `log`) | every symbol exported by libc/runtime (checked against beast's export lists); only `mmap` absent, unused | PRESENT |
| argv / env | `argv`, `@responsefile`, `-iwad`, `getenv` | desktop launch passes no arguments; the supervisor **refuses any environment** for an application (`loader.c:2134`); `open APP args` passes arguments | PARTIAL |
| Other | big-endian via `SDL_BYTEORDER`; 16 MiB zone; ENDOOM uses textscreen (another SDL window); setup tool fork/execs the game | compiler defines big-endian; 512 MiB guest, ~476 MiB heap window | PRESENT; setup tool does not fit one-executable bundles |
| WAD | `doom1.wad`, `freedoom1.wad`, `freedoom2.wad` | bundle resources at `/app/resources`; git holds no third-party binaries | fetch pinned, checksummed, into `build/`, ship in `resources/` |

## What blocks a first playable build, in order

1. **Renderer vsync (SDL render backend).** Advertise `PRESENTVSYNC` and pace
   presents on the native VBLANK subscription. Without it any SDL program that
   asks for vsync (Doom, textscreen/ENDOOM) silently gets the software renderer
   and every pixel goes through the MC68040. Gate: fail if "software" is
   selected.
2. **IWAD reachable from the desktop (bundle manifest + supervisor).** A
   manifest `environment NAME VALUE` key passed at launch
   (`DOOMWADDIR=/app/resources`, `TMPDIR=/tmp`), or a rule that an
   application starts in `/app/resources`. Until then:
   `open /apps/ChocolateDoom.app -iwad /app/resources/doom1.wad -window`.
3. **Toolchain default link (compiler specs + NDK).** Default to the dynamic
   POSIX link (`crt0-dynamic`, `libc.library.3`, runtime, compiler) and ship
   SDL headers, `.pc` files and CMake packages with the SDL Kit, so upstream
   `configure`/CMake run unchanged. DevilutionX and every later port need the
   same.
4. **Scaled copy speed (RTL + renderer).** Linear and downscaling BLITs in the
   copy burst mover's pixel mode (the handover's named next step). Stopgap is
   configuration: `smooth_pixel_scaling 0`, `fullscreen 0`.
5. **Fullscreen (SDL video backend)** via the native `ASTRA_WINDOW_FULLSCREEN`.
6. **Relative mouse (GUI protocol + SDL backend):** pointer capture, warp.
7. **Guest float cost in audio.** SDL_mixer panning and SDL's audio conversion
   run soft-float per sample. Measure on the DE25 first (decision 1).
8. **Audio latency (SDL backend / pcm.library).** Bound what sits in the host
   queue by time, not by the full 4096 frames (~93 ms at 44.1 kHz, ~371 ms at
   11025 Hz).
9. **MIDI music:** host-side synth (planned in `AUDIO_ARCHITECTURE.md`) plus an
   SDL_mixer native-MIDI backend. Not needed while music is OPL.
10. Nice to have: `SDL_ShowMessageBox` (Doom's `I_Error` popup), keymaps,
    gamepad, read-only file mmap, routing GUI apps' stdout/stderr to the
    system log.

## Shortest path to first play

1. Item 1 with its gate.
2. Build `chocolate-doom` only: upstream `configure` in a disposable copy
   (the zsh port's pattern), or item 3 first and plain CMake. Options:
   `--without-libsamplerate --without-libpng --without-fluidsynth`.
3. `ChocolateDoom.app` with `GUI PCM STORE:rw TMP:rw LIBS:r` and the WAD in
   `resources/`; launch with `open` and `-iwad` until item 2.
4. Sound effects and OPL music then work as is; measure their CPU on the DE25.
5. Then items 2, 4, 5, 6, 8; MIDI after the host synth.

## Decisions needed

1. **Soft float.** Decided 2026-10-03: userspace uses the 68040 FPU
   (`USERSPACE_FPU.md`; kernel FPU context, hard-float ABI, QEMU's FS/FD
   arithmetic on the host FPU).
2. **WAD.** Shareware `doom1.wad` (redistributable unmodified; shipping it in
   an OS image deserves a licence read) or Freedoom (BSD)? In the app bundle
   or a separate data bundle?
3. **Launch environment.** Manifest `environment` key, or "an application
   starts in its resources directory"?
4. **GUI from the Terminal.** May commands ever get `GUI`, or are windowed
   programs always applications started with `open`?
5. **Default audio rate** 44.1 kHz, or guidance for 11025/22050 Hz (needs
   item 8)?
6. **Scope.** Heretic, Hexen, Strife, the server (as a command) later; no
   setup tool?
