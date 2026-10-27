# DevilutionX on Astra: port audit

Status: source audit plus NDK CMake/C++23 DE25 smoke (2026-10-26); no game
build or target workload yet. DevilutionX
`1.5.1-1453-g4138a829e` (1.6.0-dev) at `~/Git/devilutionx`. Items that match
the Chocolate Doom audit are cited as CD#n (`CHOCOLATE_DOOM_PORT.md`).
Upstream already has an Amiga m68k CI job (68040, SDL 1.2, 8 bpp), so
big-endian m68k and 2-byte alignment are exercised upstream.

The audited DevilutionX checkout remains clean at `4138a829e991`. On Beast,
GCC 16.2 compiled an MC68040 C++23 probe using `std::format`,
`std::expected`, and `std::filesystem` with `-O2 -Wall -Wextra -Werror`.
The installed NDK now builds a C++23 smoke program with CMake/Ninja and
dynamic Kit libraries; its `std::format`, `std::expected`, and
`std::filesystem` path ran on DE25. SDL2 package discovery and a small SDL
smoke executable also ran on DE25. An actual DevilutionX workload still needs
qualification. No game performance conclusion
can be drawn from the compile-only probe.

## Beyond what Chocolate Doom needs, by blocking severity

1. **SDL joystick and game controller subsystems (SDL port).**
   DevilutionX calls `SDL_Init(VIDEO|JOYSTICK|AUDIO|GAMECONTROLLER)` and
   exits on failure (`utils/display.cpp:559-580`); Astra's SDL keeps
   `SDL_config_minimal.h`'s `SDL_JOYSTICK_DISABLED`. Enable the dummy and
   virtual joystick drivers now, a native gamepad driver later.
2. **C++23 delivery (toolchain + NDK): DONE for generic build/runtime.**
   Installed NDK includes CMake toolchain, `Platform/Astra.cmake`, exact
   dynamic startup/link targets, and SDL2/`sdl2-image` config packages.
   C++23 `std::format`, `std::expected`, and `std::filesystem` smoke built
   from extracted archive and ran on DE25. DevilutionX itself remains unbuilt.
3. **Float in the audio path.** DevilutionX mixes through SDL_audiolib, a
   float pipeline (decode, gain, pan with `std::pow`, conversion) that
   cannot be configured away; under soft float it was likely the largest
   CPU cost. Userspace is hard float now (`USERSPACE_FPU.md`).
4. **Scaled present (RTL + renderer).** CD#4 with worse defaults: linear
   upscale plus fit-to-screen, about 1 s per frame on the texture engine.
5. **Palettized present (port integration).** The game draws INDEX8; with an
   8-bit window surface it writes straight into it (`engine/dx.cpp:75-94`).
   Managed graphics and SDL now offer an INDEX8 window framebuffer whose
   palette expands in Astraea. An isolated DE25 SDL probe passed physical HDMI
   capture; select that standard SDL mode in the port and qualify actual game
   presentation before claiming its CPU or bandwidth benefit.
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
  message box, clipboard. Window framebuffer supports RGB565 and opt-in INDEX8.
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
3. Where the port calls standard `SDL_SetWindowDisplayMode(INDEX8)` before
   creating its window surface; the existing RGB565 default remains intact.
4. Lua 5.4 as a second shared library, or vendored static.
5. Scope: `NONET` first; Hellfire; `devilutionx.mpq` vs loose assets.
6. Bundle defaults (`diablo.ini`) or make upstream defaults fast.
