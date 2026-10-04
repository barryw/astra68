# Chocolate Quake on Astra: port plan

Status: survey only (2026-10-03). Nothing has been built for Astra or run.
The only build evidence is a trial compile on the Mac described under
"Evidence method". Target: Chocolate Quake 2.1.0 (tag `chocolate-quake-2.1.0`,
`8ee22175e3a5613e94ad2f9776aadc428b948cf3`, 2026-02-22). Upstream HEAD
`edb82093` is 14 commits later. The model is `CHOCOLATE_DOOM_PORT.md`:
upstream source is built unmodified, and every missing piece is implemented
once, in the Astra layer that owns it.

Upstream paths below are relative to the Chocolate Quake tree. Astra paths are
relative to this repository.

## What it is

- **Lineage.** id Software's 1999 GPL release of WinQuake (Quake 1.09),
  restructured into one CMake library per subsystem (`src/*/CMakeLists.txt`).
  Sound and input code is adapted from QuakeSpasm Spiked (`README.md`,
  Credits). Its stated aim is the behaviour of 1.09 and the DOS releases with
  no hardware acceleration (`README.md`).
- **Licence.** Source files are GPL-2.0-or-later (for example
  `src/renderer/src/d_scan.c:5-8`). The distribution is GPL-3.0
  (`LICENSE`, `CMakeLists.txt:13`).
- **Build.** CMake 3.21 or newer, C99 (`CMakeLists.txt:3-5`). No compiler
  flags are set: no `-ffast-math`, and no architecture or optimisation flags
  beyond CMake's build type.
- **Dependencies** (`CMakeLists.txt:17-33`, `vcpkg.json`):
  - SDL2 2.26.5 or newer through `find_package(SDL2 CONFIG REQUIRED)`.
  - SDL2_net 2.2.0 or newer through a CMake config package, used for UDP
    (`src/net/src/net_udp.c`).
  - libvorbis and libvorbisfile, libmad and libFLAC, all `REQUIRED`. They
    decode external music tracks (`id1/music/track02..11`, `README.md`
    "Music"). They replace CD audio. The four codecs are compiled and
    registered unconditionally (`src/sound/CMakeLists.txt`,
    `src/sound/src/snd_codec.c` `S_CodecInit`).
  - libm.
  - Not used: SDL_mixer, threads (except SDL's audio callback thread), mmap,
    OpenGL.
- **Renderer.** Software only: the 8-bit span and edge renderer
  (`src/renderer`, 30 files). There is no GL renderer, so the absence of
  OpenGL on Astra does not matter.
- **Assembly.** None. The tree contains no `.s`, `.S` or `.asm` file, and no
  `id386`, `__i386__` or inline `asm`. `src/renderer/src/nonintel.c` is the C
  path that id shipped for non-x86 builds.
- **Size.** 58,287 lines of `.c` and `.h`, including about 6k lines of
  generated end-screen font tables. The trial `-O2` objects hold 373 KB of
  text, 11 KB of data and 594 KB of BSS, without the codecs.

## Requirements matrix

| Area | Chocolate Quake uses | Astra | Verdict |
|---|---|---|---|
| Build | CMake config packages for SDL2 and SDL2_net; `find_library` for vorbis, vorbisfile, mad and FLAC | No Astra CMake toolchain file or SDL config packages (`CURRENT_STATE.md:198-204`, `DEVILUTIONX_PORT.md` item 2); none of the four codec libraries is ported. SDL_mixer uses stb_vorbis, minimp3 and dr_flac (`sw/userspace/sdl2/Makefile:276-277`) | MISSING |
| libc via SDL.h | Upstream relies on `<SDL.h>` to pull in `<stdlib.h>`, `<math.h>` and `<string.h>`. The trial compile fails on `rand`, `exit`, `atof`, `sin` and `sqrtf` without them | Astra's public `SDL_config_minimal.h` defines only `HAVE_STDIO_H`, `HAVE_SIGNAL_H` and `HAVE_SIGACTION`. `HAVE_STDLIB_H` is passed only to SDL's own build (`sw/userspace/sdl2/Makefile:133`, `prepare_source.py:64-66,101-104`) | MISSING (SDL Kit header) |
| errno | `<sys/errno.h>` for `errno` (`src/net/src/net_udp.c:33,168`) | picolibc's `sys/errno.h` defines the E* values but not `errno`. The trial compile fails there | MISSING (libc header) |
| Video | Window `FULLSCREEN_DESKTOP` (`vid_window.c:88`). Renderer `TARGETTEXTURE` only (`:73`). 8-bit `SDL_Surface`, `SDL_LowerBlit` into a locked ARGB8888 streaming texture (`vid_buffers.c:28,241-242`), then `RenderCopy` under `SDL_RenderSetLogicalSize(320,240)` (`vid_window.c:51`). End screen asks for `PRESENTVSYNC` and sets linear scaling (`es_window.c:42`, `es_buffer.c:130`) | The same present path as Chocolate Doom: the 68040 `Blit1to4` (`a0c4e9b6`), the `astra` renderer, nearest BLIT to the window. Fullscreen (`SDL_astravideo.c:306-323`) and vsync (`SDL_astrarender.c:1184-1195`) are now present | PRESENT |
| Input | Keyboard. `SDL_SetRelativeMouseMode`, `SDL_GetRelativeMouseState`, `SDL_WarpMouseInWindow` (`in_mouse.c:49-59,145`, `vid_window.c:134`). Game controller | Keyboard present. No relative mode or warp in the video backend. Joystick disabled | keyboard PRESENT, mouse look MISSING |
| Audio | `SDL_OpenAudio(desired, NULL)` at `snd_mixspeed` (default 44100), S16SYS stereo, its own integer mixer (`snd_sdl.c:129-162`, `snd_dma.c:87`). Sound effects are 11025 Hz (`snd_dma.c:86`). A **float FIR low-pass** runs on every mixed sample when sfx are 11025 Hz and the mix is 44100 Hz (`snd_mix.c:441-442,248-288`) | The device takes S16BE at the app's own rate and the host resamples. No SDL float conversion is involved. The mix rate is settable only by `-mixspeed` on the command line (`snd_dma.c:168-171`), because `S_Init` (`host.c:832`) runs before `exec quake.rc` (`:838`) | PRESENT; default rate costly (see Performance) |
| Files | `basedir = SDL_GetPrefPath("", "chocolate-quake")` (`sys.c:320`), game dir `basedir/id1` (`com_fs.c:682`). Config and saves are written into the game directory. `Sys_mkdir` is empty (`sys.c:144`), so directories must already exist | `SDL_GetPrefPath` returns `/store/` and needs `STORE` (`SDL_astrafilesystem.c:55-63`). Store seeding copies only top-level files of `resources/defaults` (`loader.c:280`), each read whole into memory (`:217`). It cannot create `/store/id1/pak0.pak`. The application's CWD is `bundle/resources` (`loader.c:1024-1040`), but Quake does not search CWD | MISSING (data placement) |
| Memory | `malloc(256 MiB)` for the hunk, fixed. There is no `-mem` (`sys.c:310,330-331`). The minimum check is 5.3 MiB (`quakedef.h:39`, `host.c:789`) | The guest is 512 MiB: `MEMORY_BUDGET.md:5`, and the board's QEMU runs `-m 512M` (checked 2026-10-03; `CLAUDE.md`'s "128 MB" is stale). The hunk fits. **Unverified:** whether Astra's malloc grants one 256 MiB request, and what it leaves for everything else | measure in phase 1 |
| Network | `SDLNet_Init` and a UDP control socket at startup. Failure to open it is `Sys_Error` (`net_udp.c:82-104`). `-noudp` skips it | SDL2_net 2.4.0 shipped, QEMU gate passes. Sockets need the `NETWORK` capability. Listening needs `NETWORK_LISTEN` (`NETWORKING.md:31`). **Unverified:** whether a UDP bind on 26000 counts as listening | PARTIAL (manifest must grant `NETWORK`, or the game cannot start) |
| Timing | `SDL_GetPerformanceCounter` (double). The main loop never sleeps (`main.c`, `host.c:505`). Frames are capped at 72/s and frame time is clamped to 0.1 s (`host.c:505,515`) | clock_gettime timer present | PRESENT; below 10 frames/s the game runs slower than real time |
| libc | stdio, `fscanf`, `strerror`, `atof`, `rand`, `setjmp`/`longjmp`, `sigaction`, `gethostname`, `open`/`write`/`unlink`; libm `sin`, `cos`, `tan`, `atan`, `atan2`, `sqrt`, `pow`, `powf`, `floor`, `ceil` (from the trial objects' undefined symbols) | `libc.library` exports picolibc's standard API and the POSIX list (`posix/Makefile:55-61`). `gethostname`, `open`, `write`, `unlink`, `sigaction` and `mkdir` are listed in `libc.posix.exports` | PRESENT, confirm at link |
| argv | `-mixspeed`, `-basedir`, `-condebug`, `+timedemo`, `-noudp` | Desktop launch passes no arguments. `open APP args` does (`CHOCOLATE_DOOM_PORT.md`). Service definitions already carry `argument` lines (`service_definition_store.c:64-78`); application manifests do not | PARTIAL |
| Data | `id1/pak0.pak` (shareware) | git holds no third-party binaries (`ARTIFACT_POLICY.md`) | see Data |

## Portability findings

- **Endianness: handled.**
  - Byte order is detected at run time (`com_byte.c:73-93`).
  - Every on-disk read goes through `LittleLong`, `LittleShort` or
    `LittleFloat`. That covers 92 call sites in `model.c` (BSP, alias,
    sprite), 18 in `pr_edict.c` (progs), and `wad.c:85-94`, `com_fs.c:563-593`
    (pak), `snd_mem.c` and `snd_wave.c` (16-bit samples swapped,
    `snd_mem.c:74`, `snd_wave.c:210`), `cl_demo.c:79-125` and `snd_vorbis.c:122`.
  - Network messages are assembled byte by byte (`com_msg.c:59-141`).
  - Saves are text.
  - Risk is in untested paths, not missing swaps; the QEMU gate's demo
    playback exercises the BSP, MDL, SPR, WAV, progs and demo loaders together.
- **Struct layout: no difference.** Every on-disk type was compiled with
  `-m68040` with and without `-malign-int`: 36 types from `bspfile.h`,
  `modelgen.h`, `spritegn.h`, `pr_comp.h` and `wad.h`. All sizes are identical
  and equal the x86 sizes, for example `dheader_t` 124, `dmodel_t` 64,
  `mdl_t` 84 and `dprograms_t` 60. The m68k 2-byte `int` alignment default
  does not change these formats.
- **Pointer size.** The target is 32-bit. 2.1.0 moved to fixed-width types
  (`CHANGELOG.md` v2.1.0).
- **Alignment.** The 68040, and QEMU, accept misaligned word and long
  accesses. File structures are naturally aligned in the formats anyway.
- **Aliasing.** Type punning through `i32 *` appears in `com_msg.c:73,152`,
  `mathlib.h:45` (`IS_NAN`) and `net_vcr.c`, and GCC warns about it. Build
  with `-fno-strict-aliasing` as a precaution. It costs nothing measurable
  here.
- **FPU instructions.** The trial `-O2` build has 13,011 FPU instructions
  and none that the 68040 lacks: no `fsin`, `fcos`, `fintrz`, `fmovecr`,
  `fetox` or `flog*`. `(int)x` is `fmove.l` with an FPCR round-to-zero save
  and restore, as `USERSPACE_FPU.md` 3.3 says. There are 27 FPCR references
  in `D_DrawSpans8` alone. Transcendentals are picolibc C calls. Upstream
  sets no `-ffast-math`. Astra's driver makes it safe (`USERSPACE_FPU.md`
  phase 2 result), so it can be tried. Timedemos replay server state, not
  input, so a float change cannot desynchronise a demo.
- **Hot float paths:**
  - perspective correction every 8 pixels in `D_DrawSpans8`, `Turbulent8`
    and `D_DrawZSpans` (one divide, multiplies, two truncations);
  - edge projection and clipping (`r_edge.c`, `r_draw.c`);
  - alias-model vertex transforms (`r_alias.c`);
  - QuakeC, which is all float (`pr_exec.c`);
  - collision traces (`sv_world.c`);
  - the audio low-pass above.
- **Threads.** The game is single-threaded. The SDL audio callback copies
  from the DMA ring under `SDL_LockAudio` (`snd_sdl.c:30-62`).
- **Upstream bug, harmless here.** `vid_buffers.c:220-221` sets `rect->x`
  twice and never resets `y` after a palette change. Do not patch it; report
  it upstream.

## Performance expectation

**Historical reference.** id's published minimum was a Pentium 75 with 8 MB,
and the renderer's hot loops were hand-written x86 (`id386`), now gone. The
Macintosh release required a PowerPC; this is recalled, not verified here.
The 68040 was not a Quake machine.

**What this machine is.**

- **Integer rate.** About 70 M guest instructions/s for user code on the A76
  (owner's figure). A kernel- and storage-bound start measured about 18 MIPS
  (`HANDOVER_2026-10-02_DOOM_START.md:10`). The table therefore gives frame
  rates at both 70 and 40 M/s.
- **Cost of an FPU instruction.** With the host-float fast path it costs
  18-40 ns of A76 time (`USERSPACE_FPU.md` section 6: `muladd` 91 ns per
  element, about 40 ns per instruction in floatx80). An average guest
  instruction costs about 14 ns at 70 M/s. In host time, then, one FPU
  instruction is worth about 2-3 average instructions.
- **What hard float changes.** It makes Quake's float work affordable, and
  that work is everywhere in Quake. Under soft float every one of those
  operations was a 45-190-instruction libgcc call (`HANDOVER_2026-10-03_BOARD.md`).
  Quake was not a candidate before the flag day (`83401eb0`).

**Measured from the trial object.** `D_DrawSpans8` spends 16 instructions
per textured pixel in its inner loop (`d_scan.c`), including a `muls.l` by
`cachewidth` loaded from memory. Its per-8-pixel setup holds about 12 FPU
instructions.

**Estimate per frame**, 320x200, `viewsize 100`, so the 3D view is 320x152
(48,640 px). Unmeasured; trust it to about 2x:

| Component | Integer instr | FPU instr |
|---|---:|---:|
| World spans (24/px including setup) | 1.2 M | 75 k |
| Z spans, surface-cache building | 0.3-0.7 M | small |
| Edges, BSP, clipping, projection | 0.2-0.5 M | 20-60 k |
| Alias models, sprites, particles | 0.1-0.4 M | 10-40 k |
| Server physics, QuakeC, client | 0.3-1.0 M | 10-50 k |
| 2D, `Blit1to4` of 64,000 px, SDL, present IPC | 0.3-0.6 M | 0 |
| **Total** | **2.4-4.4 M** | **115-225 k** |

That is about 2.7-5 M instruction-equivalents per frame:

| | at 70 M/s | at 40 M/s |
|---|---:|---:|
| 320x200, mix at 11025 Hz, ceiling | 14-26 frames/s | 8-15 frames/s |
| 320x200, upstream default 44.1 kHz | about a third lower | about a third lower |
| 640x480 (276k view px, 5.7x the per-pixel work) | 3-6 frames/s | 2-4 frames/s |

**The audio default is the largest single cost.** The 44.1 kHz low-pass
(`snd_mix.c:248-288`) runs a 128-tap kernel with 4x zero-stuffing. The
trial object shows 12 FPU instructions per inner iteration and 8 iterations
per channel, so about 200 FPU instructions per stereo frame. At 44.1 kHz
that is about 8.8 M FPU instructions/s, or about 20-25 M
instruction-equivalents/s: roughly a third of the vCPU. The integer mix is
also 4x the work of 11025 Hz. `-mixspeed 11025` turns the filter off
(`snd_mix.c:441`). That is the DOS default, and the Linux audio host
resamples it to 48 kHz at no guest cost.

**Expectations.**

- Single digits to the low teens of frames/s at 320x200, measured by
  `timedemo demo1`.
- Below 10 frames/s, Quake's 0.1 s frame clamp (`host.c:515`) makes the game
  run slower than wall time. `_snd_mixahead 0.1` (`snd_dma.c:104`) also
  under-runs audio at under 10 frames/s. It is an archived cvar, so a
  default `config.cfg` can raise it; the DMA ring holds about 0.37 s.
- Chocolate Doom ran at 16.6 frames/s on the board while 62-74% of the vCPU
  went to soft-float audio (`USERSPACE_FPU.md` section 6). Quake's per-frame
  work is several times Doom's.

**Recommended start: 320x200 fullscreen**, upstream's default mode 3
(`vid_modes.c:54,73`):

- The logical size is 320x240. The `astra` renderer scales it with a nearest
  BLIT to 960x720 in the 1280x720 output, with no guest pixel work.
- Mix at 11025 Hz.
- `viewsize` is the in-game knob for a smaller 3D window.
- Do not plan on anything larger until timedemo numbers exist.

## Data

- **Shareware `pak0.pak`** (episode 1; about 18 MB) cannot be shipped
  extracted.
  - id's shareware licence, `WinQuake/data/SLICNSE.TXT` in id's GPL source
    release, allows end users to give copies "free of charge only, the
    Software as a whole".
  - It allows providers to distribute "by electronic means only ... only in a
    compressed format" (clause 6).
  - It forbids derivative works (clause 3) and any commercial purpose
    (clause 2).
  - A pak lifted out of `quake106.zip` and placed in an OS image or
    application bundle is none of the permitted forms. Doom's `doom1.wad` had
    a Debian package to pin (`fetch_wad.py`); Quake's shareware does not.
- **Options:**
  - (a) The user supplies `pak0.pak`, shareware or registered, through a
    store import. That is the DevilutionX `diabdat.mpq` problem
    (`DEVILUTIONX_PORT.md` item 7).
  - (b) Developers and gates only: a build script fetches `quake106.zip`,
    pins it by SHA-256, and extracts the pak into excluded `build/` for QEMU
    images. It is never published in a release. Whether that use is within
    clause 6 needs a reading.
  - (c) LibreQuake, free replacement `id1` data with BSD-licensed art. Its
    exact licence, and whether its maps fit vanilla 1.09 limits and protocol
    15, are unverified.
- **Where it lives.** Quake reads and writes only `basedir/id1`, so the data
  must sit at `/store/id1/` beside `config.cfg` and saves. Pointing it at
  read-only `/app/resources` would lose config and saves.

## What blocks a first build and run, in order

1. **SDL Kit public config** (`sw/userspace/sdl2/prepare_source.py`).
   Define `HAVE_STDLIB_H`, `HAVE_STRING_H`, `HAVE_MATH_H`, `HAVE_CTYPE_H`
   and `HAVE_STDINT_H` in the shipped `SDL_config_minimal.h`, matching
   SDL's own build. Any SDL program that relies on `SDL.h` for libc
   prototypes needs it. Gate: a program calling `rand` with only `SDL.h`
   compiles.
2. **picolibc `sys/errno.h`** declares `errno`, as glibc and BSD do, recorded
   in `ASTRA_VENDOR.md`.
3. **CMake integration (toolchain + NDK).** Ship an Astra toolchain file,
   `Platform/Astra.cmake`, and `SDL2Config.cmake`/`SDL2_netConfig.cmake` with
   the SDL Kit, using the dynamic link that Chocolate Doom's Makefile builds
   by hand. DevilutionX needs the same.
4. **Music codecs.** libvorbis/vorbisfile, libmad and libFLAC are hard
   requirements (decision 2).
5. **Application arguments (manifest + supervisor).** An `argument` key for
   applications, as service definitions already have. Needed for
   `-mixspeed 11025` (performance), `-condebug` (gates), and `-basedir` if
   chosen.
6. **Data placement (supervisor).** Store seeding that creates
   subdirectories and streams large files, or a different arrangement
   (decision 3).
7. **Manifest:** `GUI PCM STORE:rw NETWORK LIBS:r`. Without `NETWORK` the UDP
   control socket fails and Quake exits at startup (`net_udp.c:103`).
8. **Relative mouse and warp (GUI protocol + SDL backend).** This is item 6
   of the Doom audit. Keyboard play works without it.

## Phased plan

**Phase 0: platform gaps.** Items 1-3 and 5-7, each with its own gate in the
owning layer.

**Phase 1: build.**

- Add `sw/userspace/ports/chocolate-quake/`, laid out like the Doom port.
- `prepare_source.py` pins `8ee22175`, refuses a modified tree and copies it
  into `build/vendor`.
- The Makefile runs upstream CMake with the Astra toolchain file.
  - `CFLAGS`: `-O2 -fno-strict-aliasing`, no `-Os`, with the reason Doom's
    Makefile gives.
  - `make -C ../../sdl2 shared net-shared` first.
  - `check_dynamic_executable.py` checks the `--needed` libraries.
- Bundle `ChocolateQuake.app`, with `resources/defaults/` holding a
  `config.cfg` that sets `_snd_mixahead`.
- Gates:
  - the build;
  - the unimplemented-FPU scan (`USERSPACE_FPU.md` 5.3);
  - an ABI-tag check.

**Phase 2: QEMU application gate, `emu/qemu/test-chocolate-quake.py`.**
Model it on `test-chocolate-doom.py`.

- Start the bundle with no arguments, as the desktop does.
- Quake must find `id1` and play its startup demo loop (`startdemos` in
  `quake.rc`).
- The display helper must see render-only batches steadily.
- The audio daemon must see an S16BE stereo voice at the mix rate.
- No process may fault.
- Then play from the keyboard: Escape, Single Player, New Game, Escape,
  Quit, `y`. The end screen waits for a key, and the game must exit 0.
- Perturbations that must fail the gate:
  - remove the pak;
  - remove `NETWORK`;
  - drop the PCM grant.

**Phase 3: QEMU measurement on beast** (development evidence only).

- `+timedemo demo1`, `demo2` and `demo3` with `-condebug`.
  - Read `/store/id1/qconsole.log` from the image. `cl_demo.c:310` prints
    "N frames S seconds F fps".
  - Run each under the profile plugin for instructions per frame by function.
  - Use a fresh `.aprof` name each run; the plugin appends.
- `timerefresh` (`r_misc.c:70-97`, 128 frames) isolates the renderer from
  game logic.
- Matrix: `-mixspeed 11025` against 44100, and 320x200 against 640x480.
- Record the numbers here.

**Phase 4: DE25 measurement** (physical evidence).

- Use the release path (`publish-de25-release.sh`).
- Run the same timedemos.
- Record:
  - frames/s;
  - audio underruns and gaps (`/data/fd-astat.py`);
  - vCPU idle;
  - the core named.
- The phase is done when this file states measured frames/s at 320x200 for
  both mix rates.

**Phase 5: platform speed, only where the phase 3 and 4 profiles point.**

- An INDEX8 window framebuffer converted by hardware (`DEVILUTIONX_PORT.md`
  item 5). It removes `Blit1to4` and three quarters of the upload, for every
  palettized SDL game.
- A `-ffast-math` A/B.
- QEMU FPCR-write and FTST helper cost, which comes from the truncation dance
  in every span (`USERSPACE_FPU.md` section 6, remaining helper overhead).
- 68040 span drawers belong in a decision of their own (decision 6). They
  would break "unmodified upstream".

**Phase 6:** mouse look (item 8), then music (decision 2).

## Decisions needed

1. **Data.** User-supplied pak only, a gate-only fetch of `quake106.zip`
   into `build/`, or LibreQuake as the shipped default? The shareware pak
   cannot be shipped as Doom's WAD was (`SLICNSE.TXT` clauses 2, 3, 6).
2. **Music codecs.** Port libvorbis, libmad and libFLAC as Astra libraries,
   or propose an upstream CMake option that makes music codecs optional?
   Shareware has no music tracks. Recommended: the upstream option first,
   because it is a small change of general use; port the libraries only if
   upstream declines or music is wanted.
3. **Where `id1` lives.** Recursive, streaming store seeding from
   `resources/defaults/id1/`, a read-through union of bundle resources under
   the store, or a user import into `/store/id1/`?
4. **Memory.** Settled: the guest is 512 MiB (the board runs `-m 512M`), so
   Quake's fixed 256 MiB hunk (`sys.c:310`) fits. Phase 1 measures whether
   Astra's malloc grants it whole and what remains for the desktop; a
   `-mem` option upstream is the fallback.
5. **Launch arguments.** An application-manifest `argument` key (recommended;
   needed for `-mixspeed 11025`), or a per-application default the
   supervisor applies?
6. **Scope of "unmodified".** If phase 4 shows the renderer below 10
   frames/s, is a 68040 span-drawer patch acceptable, as id386 once was for
   x86? Or is Quake accepted as a measured demonstration rather than a
   playable game?
7. **Pin.** Pin the 2.1.0 tag (recommended), or HEAD `edb82093`?

## Evidence method

- Cloned upstream at `edb82093` (depth 50) on the Mac.
- Compiled every `.c` file with Homebrew `m68k-elf-gcc` 16.2.0:
  `-m68040 -ffixed-a4 -D__astra__=1 -O2 -std=gnu99`.
- Headers: picolibc headers from `third_party/picolibc/libc/include`, Astra's
  vendored SDL2 headers, a stub `picolibc.h`, a stub `config.h`, and a stub of
  the SDL2_net 2.x API subset.
- Results:
  - 102 of 106 files compile once the five `HAVE_*` macros are defined;
  - `net_udp.c` fails on `errno`;
  - the three codec files fail on their missing headers.
- This is not the Astra driver (`m68k-astra-gcc`). FPU code generation
  matches it only where `USERSPACE_FPU.md` says the patched driver differs:
  `sin` and `cos` patterns under `-ffast-math`, and `-ffast-math` was not
  used. Nothing was linked or run.
