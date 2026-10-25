# ScummVM on Astra: port audit

Status: audit only (2026-10-06). Nothing was built for Astra and nothing ran
on Astra. Target: ScummVM **v2026.3.0** (`fed42f20`, 2026-06-20), the latest
stable tag. v2.9.1 was checked first. Its configure, backend and plugin facts
are the same except where this audit says otherwise. Paths below are relative
to the ScummVM tree unless they start with `docs/`, `sw/`, `ndk/` or `emu/`.
CD#n and DX#n refer to items in `CHOCOLATE_DOOM_PORT.md` and
`DEVILUTIONX_PORT.md`.

How each fact was established:

- **Verified:** read in source, or measured in a scratch tree in the job
  directory.
- **Measured (m68k code size):** every object of a 7-engine configuration was
  compiled with `m68k-elf-g++ 16.2 -m68040 -O2` on the Mac. Headers came from
  Astra's picolibc and GCC 16.2's libstdc++, and `config.h` was big-endian.
  Nothing was linked.
- **Inferred:** a conclusion from reading, not a run. Each one is marked.

Precedents for m68k exist upstream:

- an Atari backend (`backends/platform/atari`, 68040/060, no SDL);
- the FireBee SDL build;
- an m68k ELF plugin loader (`backends/plugins/elf/m68k-loader.cpp`).

So big-endian code, 2-byte-aligned m68k and 68040-class speed are all
exercised upstream.

## Verdict

**Feasible, and cheaper than DevilutionX.** ScummVM is C++11 with no
exceptions and no STL. It needs no OpenGL and no third-party library.

The stock SDL2 backend asks for nothing that Astra's SDL lacks *fatally*:

- the joystick init failure is only a warning;
- the texture it streams is RGB565, which the Astra renderer takes natively.

The work is:

1. a small configure host entry;
2. a thin `OSystem` subclass for paths, because desktop apps get no `HOME`;
3. a MIDI driver over the host FluidSynth, so the guest does not emulate an OPL
   chip.

That is the Haiku model (`configure:3463-3469`): stock SDL posix backend plus
a few defines.

**First milestone:** a single-game bundle running *Beneath a Steel Sky* (floppy
1.3, freeware), with the engines `sky,queen,lure` statically linked.

## 1. Build system

| Item | ScummVM needs | Evidence | Astra |
|---|---|---|---|
| configure | Hand-written shell script, not autoconf. Unknown `--host` values go through `config.sub` (`configure:2009`) | `./config.sub m68k-astra` gives `OS 'astra' not recognized` (tested) | **Patch:** add a `m68k-astra*)` host case like `m68k-atari-mint*)` (`:1861`). Set `_host_os=astra`, `_host_cpu=m68k` |
| POSIX detection | Decided by host name. An unknown OS gets `_posix=no` (`:4910-4930`), which drops the POSIX fs factory and `posix-main` | | **Patch:** add `astra*` to the POSIX list (`:4915`) |
| C++ standard | **C++11**: `-std=${std_variant}11 -pedantic` (`:2378-2391`, `:2579`) | | GCC 16.2. Add `astra*` to the `gnu++` list (`:2540`), as for mint/amigaos, so that picolibc's `__STRICT_ANSI__` does not hide `strdup`/`strcasecmp` (inferred risk) |
| Exceptions | None in core or the candidate engines. No `try` outside Android Java and Lua. Atari passed `-fno-exceptions` in 2.9.1 (2.9.1 `configure:3706`) | | Use `-fno-exceptions` |
| RTTI | **Required.** `dynamic_cast` in `common/file.cpp:240`, `common/stream.cpp:578`, `common/ptr.h:345` and in `lure/sound.cpp` and `sci/` | | Tested in QEMU (`posix/tests/cxx_runtime.cpp`) |
| STL / iostreams | Own containers. `<iostream>`/`<fstream>` only in `audio/softsynth/mt32` (`--disable-mt32emu`) | | Not needed |
| 64-bit ints | `uint64` throughout (savefiles, streams) | | Comes from `compiler.library` helpers |
| Endianness | Tested by `strings` on a compiled object (`:2803`), so it is cross-safe | | Big-endian |
| Alignment | `m68k` is in the "unaligned OK" list, so `SCUMM_NEED_ALIGNMENT` is undefined (`:2872`). Unaligned reads become packed/`may_alias` structs (`common/endian.h:208-237`) | | Under TCG an unaligned access costs nothing extra (inferred) |
| SDL discovery | `pkg-config sdl2` or `sdl2-config` (`find_sdlconfig`, `:4465-4500`). Then `cc_check` **links** a test program against `SDL_LIBS` | | **Gap:** Astra ships no `sdl2-config`/`.pc` (CD#3). Workarounds: a shim `sdl2-config` printing `2.32.10`, the include path and the Doom-style link line (`ports/chocolate-doom/Makefile:21-28`) through `SDL_CONFIG=`. Better: ship it in the SDL Kit |
| Link | `LD=$CXX`. Default static plugins (`_plugins_default=static`, `:240`) | | The g++ driver still links statically by default (`HANDOVER_2026-10-04_NDK.md:141-144`). Use the Doom recipe: crt0, `-T`, `-l:cxx.library.2` and the SDL/libc libraries in `LIBS` |
| m68k tuning | Atari defines `SCUMM_OPTIMISED_CODE` (an m68k-friendly costume renderer, `engines/scumm/base-costume-optimised.cpp`), `-fomit-frame-pointer` and `-ffast-math -fno-unsafe-math-optimizations` (`:3879-3899`) | | Take the same flags, plus `-O2` (Doom's rationale: emulated instructions cost, bytes do not) |

The new `astra*)` block in the `$_host_os` case should:

- define `-D_GNU_SOURCE`;
- leave `SYSTEM_NOT_SUPPORTING_D_TYPE` unset, because Astra fills `d_type`
  (`sw/userspace/posix/src/file.c:1719`);
- set `_port_mk`;
- force off `_sdlnet _libcurl _cloud _enet _alsa _seq_midi _sndio _timidity
  _tts _fribidi _discord _mt32emu _lua _opengl_mode=none _tinygl`, as the Atari
  block does (`:3905-3936`).

That is about 30 lines of configure, carried by `prepare_source.py` like the
Doom port's patches until it goes upstream.

## 2. Backend: SDL2 (`backends/platform/sdl`, `backends/graphics/surfacesdl`)

v2026.3.0 also has an SDL3 backend. configure prefers SDL1/2 when both exist
(`:4465-4475`). Astra is SDL 2.32.10.

**Video path.** The engine draws 8-bit. The backend does:

1. `SDL_BlitSurface` of the 8-bit screen into the 16-bit `_tmpscreen`
   (`surfacesdl-graphics.cpp:1944`), which is SDL's `Blit1to2`;
2. the "normal" scaler into the RGB565 `_hwScreen` (`:1963`);
3. `SDL_UpdateTexture` of the **whole** texture (`:3153`);
4. `SDL_RenderCopy` into the window viewport.

The streaming texture is `SDL_PIXELFORMAT_RGB565` (`:3033`, `:3120`). It asks
for `PRESENTVSYNC` and retries without it (`:3086-3102`). The mouse cursor is
drawn in software into `_hwScreen` (`:2448`) and the system cursor is hidden
(`sdl-graphics.cpp:314`). `--opengl-mode=none` is a supported configuration;
Atari, FireBee and others use it (`:3921`).

| ScummVM uses | Astra SDL (`sw/userspace/sdl2`) | Gap |
|---|---|---|
| `SDL_Init(VIDEO)`; TIMER/AUDIO subsystems; `SDL_AddTimer(10ms)` (`timer/sdl/sdl-timer.cpp`) | Present: `SDL_TIMER_UNIX`, pthreads | none |
| Joystick + GameController init when `joystick_num>=0`, default 0 (`base/commandLine.cpp:386`) | `SDL_JOYSTICK_DISABLED` | **Not fatal:** `warning()` and return (`events/sdl/sdl2-events.cpp:83-90`). Set `joystick_num=-1` in the defaults to silence it. DX#1 is the real fix |
| Renderer + RGB565 streaming texture + nearest scale | Astra renderer: ARGB8888/RGB888/**RGB565**, streaming, vsync, nearest/linear on the texture engine (`SDL_astrarender.c:139-141,1340-1350`) | none. Keep `filtering=false`: linear is the slow path (CD#4) |
| 8-bit `SDL_Surface` + `SDL_SetPaletteColors` + blit to 16-bit | Generic upstream C. Only `Blit1to4` has 68040 asm (`sdl2/prepare_source.py:131-187`) | **Perf gap:** `Blit1to2` (8 to RGB565) is unoptimised C on every dirty rect |
| `SDL_CreateWindow(RESIZABLE)`, `FULLSCREEN_DESKTOP`, `SetWindowSize`, `GetDesktopDisplayMode` | Present. Desktop is 1920x1080 RGB565 | none |
| `SDL_SetRelativeMouseMode`, `SetWindowGrab`, `SetWindowMouseRect` (2.0.18), `WarpMouseInWindow` | Done 2026-10-06 (`0c1467c7`) | none |
| `SDL_TEXTINPUT`, keyboard, wheel | Present (`SDL_astravideo.c:785-798`) | none |
| `SDL_GetWindowWMInfo` (`sdl-window.cpp:289`; consumers are only mac/win/maemo) | Core symbol; returns false | none (link only; export unverified) |
| `SDL_SetClipboardText` / `GetClipboardText` | In-process only; no driver hook | minor (GUI copy/paste stays in-app) |
| `SDL_ShowSimpleMessageBox` (fatal-error popup) | No hook, so it fails | minor (CD#10) |
| `SDL_SetWindowIcon`, `SDL_OpenURL` | No-op / fails | none |
| Audio: `SDL_OpenAudio`, `AUDIO_S16SYS` (S16MSB here), 44100 Hz by default (`mixer/sdl/sdl-mixer.cpp:37,101,216`), callback; reopens with SDL conversion if the format differs | S16BE taken as is; any 8-192 kHz rate; host resamples (`SDL_astraaudio.c:35-47,157-163`) | none |
| `SDL_net` (`USE_SDL_NET`), libcurl/cloud | SDL_net exists | disable (`--disable-sdlnet --disable-libcurl --disable-cloud`) |

**Paths (the one real platform gap).** `OSystem_POSIX` builds the config path
from `$HOME`/`$XDG_CONFIG_HOME` and otherwise falls back to the bare
`scummvm.ini` (`platform/sdl/posix/posix.cpp:114-162`). Saves, cache and logs
work the same way (`:224-346`, `saves/posix/posix-saves.cpp`). Applications
started from the desktop get **no environment** (`supervisor/src/loader.c:2508`)
and start in the read-only `resources/`. So the config cannot be written.

Fix it with a small `OSystem_Astra : OSystem_POSIX`, modelled on
`amigaos/amigaos.cpp` or `riscos/`:

- config `/store/scummvm.ini`, saves `/store/saves`, log to `SDL_Log`/syslog;
  the paths come from `SDL_GetPrefPath`, which is `/store/` with `STORE:rw`;
- `DATA_PATH="/app/resources"`, so themes and engine data are found;
- `logMessage` goes to syslog;
- `openUrl`/`displayLogFile` return false. `posix.cpp:376-408` forks
  `xdg-open`/`xterm`; on Astra, `fork` exists but those programs do not.

That is about 120 lines, upstreamable as `backends/platform/sdl/astra/`. The
manifest `environment` directive (CD#2) would avoid it, but the subclass is
the right long-term owner.

**Launch.** `common/system.cpp:149-182`: a file `scummvm-autorun` found by
`Common::File` makes ScummVM read extra argv lines from it, or auto-detect `.`
if it is empty. Desktop launches pass no argv, so a single-game bundle can
ship `resources/scummvm-autorun` with `sky`, or `--path=...` plus `sky`. That
needs no supervisor change. Whether `Common::File::exists` sees the cwd at that
point is inferred, not verified.

**MIDI (the biggest performance lever).** Sky, Queen and Lure all call
`detectDevice(MDT_ADLIB | MDT_MIDI | MDT_PREFER_MT32)`
(`sky/sky.cpp:355`, `queen/music.cpp:45`, `lure/sound.cpp:52`). With no MIDI
port the default is OPL emulation on the guest. The Atari maintainer measured
synthesis at "as much as 50% of all used CPU time" on a CT60
(`backends/platform/atari/readme.txt:371-376`).

Astra already has live MIDI to the host's FluidSynth:
`astra_midi_open`/`astra_midi_send` (`ndk/include/astra/midi.h:132,236`) take
channel-voice messages and ignore SysEx. The precedent is ScummVM's `stmidi.cpp`
(4 KB): a `MusicPluginObject` whose `MidiDriver::send(uint32)` forwards bytes.
An `backends/midi/astra.cpp` of about 150 lines therefore makes GM music cost
the guest nothing.

- **Limitation:** MT-32-specific SysEx is dropped. Set `native_mt32=false` and
  the GM device, so engines use their GM mapping. Sky has `gmmusic.cpp`.
- Doom shows the host synth path works (`test-chocolate-doom.py` checks MIDI).

## 3. Dependencies

**All optional.** The minimal build tested here used `--disable-` for zlib,
png, jpeg, gif, freetype2, fluidsynth/lite, vorbis/ogg/tremor, flac, mad,
theoradec, vpx, faad, libcurl, sdlnet, cloud, enet, mt32emu, lua, tts, fribidi,
discord, taskbar, system-dialogs, gtk, readline, hq/edge scalers, translation,
eventrecorder, sonivox, alsa, sndio, bink, 16bit and highres. configure
succeeded, and the 7 engines built and linked natively on the Mac.

- **zlib:** not required. Without it, `common/compression/unzip.cpp:73-80` and
  `gzio.cpp:1333` use a built-in inflate. Themes (`*.zip`) still load, and the
  built-in theme (`gui/themes/default.inc`) needs nothing. Savegames are then
  written uncompressed. When `third_party/zlib` lands (system-log phase 2,
  `docs/SYSTEM_LOG.md:349`), enable it.
- **Worth adding later:**
  - zlib (compressed saves, faster theme load);
  - libpng (PNG thumbnails and screenshots: BMP without it);
  - FreeType (TTF fonts for some GUI scripts and a few engines).

  Astra has none of these as public libraries. SDL_image's stb/miniz and
  SDL_ttf's FreeType are private.
- **Compressed game audio** (MP3/OGG/FLAC for talkie CD versions, Drascula
  music packs): needs libmad/libvorbis(tremor)/libFLAC, or the planned Codec
  Kit (`docs/CODEC_ARCHITECTURE.md`). Atari omits real-time MP3/OGG/FLAC
  entirely. Not needed for floppy versions.

## 4. Engines

Atari "Full" runs every engine except Cine, Director, Hugo, Ultima, Pink and
WAGE (`build-release.sh`; `readme.txt:529-549`). Its minimum hardware is a
Falcon with a 68040 and 32 MB. 16bpp, scalers and software synths are omitted
(`readme.txt:44-49`). Its slow cases are named: Gobliiins, SCI Gabriel
Knight/Larry 2/7, and Sherlock, which redraw too much (`:346-353`), plus
640x480 intros.

Astra's guest rate:

- about 14-70 M guest instructions/s on the DE25 (`HANDOVER_2026-09-30_SDL_PERF.md:71`;
  `CHOCOLATE_QUAKE_PORT.md:125`);
- Doom runs at a 35 fps cap (`HANDOVER_2026-10-05_VIDEO.md:205`).

That places it in the 68040/40 MHz to 68060 class the Atari port targets
(inferred).

| Tier | Engines (configure names) | Notes |
|---|---|---|
| M1: first light | `sky`, `queen`, `lure` | 320x200, 8-bit, small. All three games are freeware (below) |
| M2: classics | `scumm` (v0-v6: Maniac, Zak, Indy3/4, Monkey 1/2, Loom, DOTT, Sam&Max), `agi`, `sci` (SCI0-1.1), `drascula`, `dreamweb`, `cge` (Soltys), `agos`, `kyra` | All 8-bit 320x200, run on Atari. SCUMM v5/v6 CD music and speech need codecs, or come as WAV/raw. Expect SCI's heavy titles to be slow |
| Defer | `scumm_7_8` (COMI 640x480, SMUSH), `he` (needs `16bit`, `highres`, `bink`), `gob`, `sherlock` | Heavy redraw or video; not "realistic first" |
| Out | `sci32`, `wintermute`, `grim`/`myst3`/`stark`/`tetraedge`/`playground3d` (3D, need TinyGL/GL), `ags`, `director`, `ultima` (hires parts), `sword25`, `titanic` | 16bpp/hires/3D, or explicitly dropped even on Atari |

**Test game for the QEMU gate: *Beneath a Steel Sky*, floppy 1.3.**

- Download: `https://downloads.scummvm.org/frs/extras/Beneath a Steel Sky/BASS-Floppy-1.3.zip`
- 7.3 MiB; SHA-256 `d0bac1bd61747a67e885fa44b78c78887bf2b15d3dfa2790c483fad651078818`
  (downloaded and hashed).
- Contents: `sky.dnr`, `sky.dsk` (8.8 MB) and `readme.txt`.
- Licence (`readme.txt`, verified): "You may distribute this game for free on
  any medium… may distribute it in aggregate as part of a larger & possibly
  commercial software distribution". The readme must stay intact, and the game
  may not be sold on its own. That is the same posture as shipping
  `doom1.wad`, and it is cleaner.
- Needs `sky.cpt` (410 KB, `dists/engine-data/`, GPL with ScummVM).

Alternatives:

| Game | Download | Contents | Extra data | Licence (verified) |
|---|---|---|---|---|
| *Lure of the Temptress* | `lure-1.1.zip`, 5.4 MiB, SHA-256 `f3178245…0c1890108` | | `lure.dat` (751 KB) | `LICENSE.txt`, same terms |
| *Flight of the Amazon Queen* floppy | `FOTAQ_Floppy.zip`, 6.8 MiB, SHA-256 `2e59de85…fde761e0` | `queen.1`, 22 MB | `queen.tbl` (1.1 MB) | Same terms |

A gate like `test-chocolate-doom.py` would:

1. boot;
2. launch `BASS.app` (`scummvm-autorun`: `sky`);
3. require the render batches, no SDL errors and a non-silent S16BE voice;
4. require host MIDI events once the Astra MIDI driver exists;
5. press Esc to skip the intro and wait for the first room;
6. open the control panel with F5 and save to `/store`;
7. quit with Ctrl-Q, which should give exit 0.

## 5. Size and memory

m68k `.text` + `.rodata` summed over compiled objects. These are measured
upper bounds: not linked, so there is no COMDAT folding and no unreferenced
archive members are dropped.

| Part | KiB |
|---|---|
| common | 618 |
| base | 130 |
| gui | 1023 |
| graphics | 928 |
| audio | 687 |
| image | 208 |
| video | 261 |
| math | 71 |
| backends | 219 |
| engine glue | 155 |
| **Core subtotal** | **about 4.3 MiB** |
| sky | 159 |
| queen | 228 |
| lure | 251 |
| drascula | 175 |
| agi | 494 |
| scumm (v0-6) | 1842 |
| sci (16-bit only) | 1372 |

The largest single objects are `graphics/VectorRendererSpec.o` (241 KiB) and
`gui/ThemeEngine.o` (177 KiB). The built-in theme is about 149 KB of source
text.

**Estimates (inferred from the above):**

- M1 executable (core + sky/queen/lure) about **3.5-4.5 MiB** linked.
- All seven engines about **7-8 MiB**.
- Data files add 2-3 MB: themes, `sky.cpt`, `lure.dat`, `queen.tbl`.

**Plugins: static.** Astra has no `dlopen` (`docs/SHARED_LIBRARY_FORMAT.md:130-134`;
`CODEC_ARCHITECTURE.md:41`), and the `posix` provider needs it
(`backends/plugins/posix/posix-provider.cpp:58`). Another route exists:
ScummVM's own ELF loader (`USE_ELF_LOADER`), with an **m68k relocator**
(`backends/plugins/elf/m68k-loader.cpp`, `R_68K_32/16/8/PC*`, RELA only). It
links plugins with `--just-symbols` against the executable (`configure:5233-5243`,
mintelf).

That fits Astra's `ET_EXEC` programs (`SHARED_LIBRARY_FORMAT.md:71`), but it is
a second loader. The standing rule says one identity resolver, and calls from a
plugin into shared `.library` code through the executable's PLT are
unverified. **Recommend static** until the NDK exposes runtime loading. Detection
can stay static (`--disable-detection-full`).

**RAM.** Atari Full runs in 32 MB of TT-RAM. A 320x200 game holds a few MB of
resources plus about 1 MB of surfaces (8-bit screen, 16-bit tmp and overlay
copies, the texture). Expect 8-30 MB resident (inferred). 512 MiB is not a
constraint.

## 6. Risks

1. **Guest CPU in the presentation path.** Each update costs three CPU copies:
   - `Blit1to2` palette expansion (unoptimised C);
   - the normal1x scaler copy;
   - a whole-texture `SDL_UpdateTexture`, even for small dirty rects
     (`:3153`).

   Default `scale_factor` is 2 when scalers are compiled (`:659-661`). That
   means a 640x400 CPU scaler, so build with `--disable-scalers` or default
   `gfx_mode=normal`/scale 1 and let the renderer's nearest scaling enlarge
   (CD#4).

   Astra SDL now has an INDEX8 window path with hardware palette expansion,
   physically tested in an isolated DE25 probe. ScummVM still expands indices
   into its RGB565 streaming texture, so its port must adopt that path before
   it saves this CPU work. A 68040 `Blit1to2` is a fallback performance option
   only if the indexed route cannot preserve ScummVM presentation behavior.

   A native Astra graphics manager writing INDEX8 straight into managed
   graphics, as the Atari backend did to skip SDL, is a later option.
2. **Audio mix cost.** `Audio::MixerImpl` resamples every channel to the output
   rate on the guest (fixed-point `audio/rate.cpp`). The Atari readme's advice
   (`:411-420`) applies: set `output_rate` to 11025 or 22050 and let the host
   resample. A good per-bundle default. OPL emulation is the big one (section 2).
3. **Configure link checks.** `cc_check` links test programs, so the Astra
   link recipe must work through `$CXX $LDFLAGS ... $LIBS` from the first check.
   CD#3 (a dynamic-by-default driver) removes this risk for every port.
4. **picolibc and `-std=c++11 -pedantic`.** POSIX declarations hidden under
   strict ANSI. Mitigation: use `gnu++11` (section 1). Inferred, not seen.
5. **File system.**
   - The POSIX fs node uses `opendir/readdir/closedir`, `stat`, `access`,
     `mkdir`, `remove`, `getcwd` and `d_type` (`backends/fs/posix/posix-fs.cpp`).
   - stdio streams use `fopen/fseeko/ftello/rename/fflush`
     (`backends/fs/stdiostream.cpp`).

   All of these are present. `realpath`, `uname`, `mmap` and `getpwuid` are
   **not used** (grepped).

   Astra-specific limits:
   - `readdir` costs about 7.5 ms per two entries, so game detection over a
     large directory or "Mass add" is slow;
   - `PATH_MAX` is 192, and ScummVM's `MAXPATHLEN` checks follow it;
   - the dirty-ext4-after-kill trap in CLAUDE.md applies to saves.
6. **Float.** Present in the GUI vector renderer and in MAME OPL table setup.
   Userspace is hard-float now (`USERSPACE_FPU.md`); not a blocker. Atari uses
   `-ffast-math -fno-unsafe-math-optimizations`.
7. **Threads.** The SDL audio callback thread plus the SDL timer thread. They
   synchronise through `SDL_CreateMutex` (`backends/mutex/sdl`). Standard
   pthread usage that Doom already exercises.
8. **Input.** Keyboard (scancodes plus `SDL_TEXTINPUT`), mouse buttons and
   wheel, relative mode for some engines: all present since 2026-10-06. Gamepad
   is absent (DX#1), which is acceptable.
9. **ELF-plugin temptation.** See section 5. Static avoids a second loader and
   an icache-flush question: user mode cannot `CPUSHA`, and no Astra cache-flush
   call was found (unverified).

## 7. Milestones

| # | Milestone | Size | Contents |
|---|---|---|---|
| 0 | Shared prerequisites (also unblock DevilutionX and every port) | S-M | **(a)** An `sdl2-config` and `sdl2.pc` in the SDL Kit (CD#3). **(b)** The g++ driver owns the dynamic link (NDK handover: "Not started"). **(c)** Optional: 68040 `Blit1to2`. None is strictly required: (a) and (b) have Doom-style workarounds |
| 1 | Configure host and build skeleton | S | `sw/userspace/ports/scummvm/`: `prepare_source.py` pins `v2026.3.0` and applies the `m68k-astra*` host, POSIX-list, `gnu++` and `astra*` define patches; out-of-tree `configure --host=m68k-astra --backend=sdl --enable-release --disable-all-engines --enable-engine=sky,queen,lure --opengl-mode=none --disable-scalers --disable-16bit --disable-highres` with every library disabled; `SDL_CONFIG` shim; Doom link recipe; `check_dynamic_executable.py` |
| 2 | Astra platform glue | S | `OSystem_Astra` (prefs and saves in `/store`, `DATA_PATH=/app/resources`, syslog logging), `backends/midi/astra.cpp` over `astra_midi_send`, `scummvm.ini` defaults (`joystick_num=-1`, `gfx_mode=normal`, `fullscreen=true`, `filtering=false`, `output_rate=22050`, `music_driver=astra`, `native_mt32=false`) |
| 3 | First engine boots in QEMU | M | `BASS.app`: GUI, PCM, `STORE:rw`, `LIBS:r`, SDL2 plus libc/runtime/compiler/cxx requires; `resources/` holds `sky.dnr`, `sky.dsk`, `sky.cpt`, the readme (licence), the built-in theme only and `scummvm-autorun`. Fetch it pinned and checksummed into `build/`, as `fetch_wad.py` does. Expect some debugging time in configure's cross checks and in first-run C++ static initialisers (there are many `REGISTER_PLUGIN_STATIC`s) |
| 4 | QEMU gate | S-M | `emu/qemu/test-scummvm.py` modelled on `test-chocolate-doom.py`: render batches, no SDL errors, an S16BE voice, host MIDI events, Esc past the intro, save, then reload from `/store`, quit with exit 0, cursor restored. Perturb it: delete `sky.cpt` and the gate must fail with ScummVM's missing-engine-data error |
| 5 | DE25 board run | M | `publish-de25-release.sh`; measure presents/s and `astra-top`/`bench-frame.py --profile` CPU split (blit, mixer, engine). Expect to need `output_rate` tuning and the `Blit1to2` asm |
| 6 | Breadth | M-L | Add `scumm`, `agi`, `sci`, `drascula`, `dreamweb`, `cge`. The launcher bundle (one `ScummVM.app` plus a user games folder) needs DX#7 ("how users put data into `STORE:`"). zlib and libpng when available. Codec Kit for CD-talkie audio |

**Overall:** about 2-3 weeks of focused work to M5 for the first three engines,
and M-L more for breadth (inferred, at the pace of the Doom port).

### Riskiest unknowns

1. Configure's link-based checks against Astra's non-default dynamic link.
2. Real DE25 frame cost of the three-copy SDL path plus mixing at about
   20-70 MIPS.
3. Whether ScummVM's large static-constructor and plugin tables hit any
   loader or RELRO edge. ScummVM is far larger than any C++ program run on
   Astra so far.
4. Exports of rarely used SDL symbols from `SDL2.library.3`
   (`GetWindowWMInfo`, `HasAVX2`, …): a link failure would show this at once.

### Do these first in Astra's own layers (they also help DevilutionX)

1. SDL Kit: `sdl2-config`/`.pc`, and the g++ driver dynamic link (CD#3).
2. Port integration: use SDL's now-available INDEX8 present path where
   ScummVM can preserve its scaler and cursor behavior (DX#5). Qualify a real
   workload before claiming the CPU-copy saving.
3. SDL joystick: dummy and virtual drivers (DX#1), which removes ScummVM's
   warning and is fatal for DevilutionX.
4. Manifest `environment`, or an "app gets `HOME=/store`" rule (CD#2). With it,
   ScummVM's POSIX paths work unmodified, and ports that read `HOME` stop needing
   per-port glue.
5. A user-data path into `STORE:` (DX#7), needed for the multi-game launcher.
6. zlib in `third_party` (system-log phase 2).
