# DevilutionX on Astra: port audit

Status: audit only (2026-09-30), read-only, nothing built. DevilutionX
`1.5.1-1453-g4138a829e` (1.6.0-dev) at `~/Git/devilutionx`. Items that match
the Chocolate Doom audit are cited as CD#n (`CHOCOLATE_DOOM_PORT.md`).
Upstream already has an Amiga m68k CI job (68040, SDL 1.2, 8 bpp), so
big-endian m68k and 2-byte alignment are exercised upstream.

## Beyond what Chocolate Doom needs, by blocking severity

1. **SDL joystick and game controller subsystems (SDL port).**
   DevilutionX calls `SDL_Init(VIDEO|JOYSTICK|AUDIO|GAMECONTROLLER)` and
   exits on failure (`utils/display.cpp:559-580`); Astra's SDL keeps
   `SDL_config_minimal.h`'s `SDL_JOYSTICK_DISABLED`. Enable the dummy and
   virtual joystick drivers now, a native gamepad driver later.
2. **C++23 delivery (toolchain + NDK).** GCC 16.2 compiles C++23 and
   `cxx.library.1`/`unwind.library.1` run exceptions, RTTI and threads in
   QEMU, but the g++ driver links statically by default (CD#3), there is no
   CMake toolchain file or `Platform/Astra.cmake`, and SDL.kit publishes no
   `SDL2Config.cmake`/`sdl2-image` packages. Gate: a C++23 program using
   `std::format`, `std::expected`, `std::filesystem` in QEMU.
3. **Float in the audio path.** DevilutionX mixes through SDL_audiolib, a
   float pipeline (decode, gain, pan with `std::pow`, conversion) that
   cannot be configured away; on `-msoft-float` it is likely the largest
   CPU cost. Userspace FPU (decided: allowed) or fast float helpers.
4. **Scaled present (RTL + renderer).** CD#4 with worse defaults: linear
   upscale plus fit-to-screen, about 1 s per frame on the texture engine.
5. **Palettized present (managed graphics + SDL video backend).** The game
   draws INDEX8; with an 8-bit window surface it writes straight into it
   (`engine/dx.cpp:75-94`). Wire INDEX8+palette to direct colour in managed
   graphics (the texture engine accepts it) and let the SDL backend offer an
   INDEX8 window framebuffer converted by hardware: a quarter of the upload
   and no CPU palette expansion, for every palettized SDL game.
6. **SDL cursors and warp (video backend over the display cursor):**
   `CreateColorCursor`, `ShowCursor`, `WarpMouseInWindow`. Without them the
   system pointer and the game's software cursor both show.
7. **User game data.** `diabdat.mpq` is always the user's own; there is no
   way yet to put a file into an application's `STORE:`.
8. **Shared compression and Lua libraries (optional):** zlib, bzip2, and
   Lua 5.4 (DevilutionX requires 5.4; Lua.kit ships 5.5.1). Vendored static
   builds work meanwhile.
9. **POSIX for asio (only for TCP play):** `FIONBIO`, `getnameinfo`,
   `socketpair`, `readv`/`writev`. Build with `-DNONET=ON` first; ZeroTier
   (a second IP stack) never.

## SDL gaps

- Joystick and game controller compiled out (fatal).
- Video backend: cursor create/show/free, warp, fullscreen (CD#5), grab,
  message box, clipboard; window framebuffer fixed at RGB565.
- Renderer: no API gap (streaming RGB888, logical size, integer scale);
  the gaps are scaled-copy speed and upload bandwidth
  (`-DDEVILUTIONX_DISPLAY_TEXTURE_FORMAT=SDL_PIXELFORMAT_RGB565` halves it).
- Audio: none at the API; latency (CD#8) and float cost.

## Feasibility

Game rendering is integer (a few guest instructions per pixel, 20 Hz logic).
At ~40 M guest instructions/s an unmodified default build is not plausible;
at ~400 M/s with nearest or no upscale, 640x480, and hard or fast float,
15-30 fps looks plausible. None of this is measured: profile a `NOSOUND`
build with `bench-frame.py --profile` first.

## Decisions needed

1. Licences: DevilutionX is under the Sustainable Use License
   (non-commercial, not OSI); `spawn.mpq` redistribution is unverified.
2. How users put `diabdat.mpq` into an application's `STORE:`.
3. How an application opts into the palettized framebuffer (SDL has no
   API): a manifest hint, or a per-application default.
4. Lua 5.4 as a second shared library, or vendored static.
5. Scope: `NONET` first; Hellfire; `devilutionx.mpq` vs loose assets.
6. Bundle defaults (`diablo.ini`) or make upstream defaults fast.
