# Handover 2026-10-01 (2): Doom performance on the DE25

Read `CLAUDE.md`, `AGENTS.md`, then this page. Previous:
`HANDOVER_2026-10-01_DOOM.md`.

## Direction (user)

Debug Doom's performance; everything that should run on hardware (or the
Linux host) does; keep as much as possible off the MC68040. Music: "now is
the time to implement our synth wave system": loadable SoundFonts with a
default set; polyphony 256 (FluidSynth default) is fine; Doom's soundtrack
must sound great.

## Measured (DE25, before this session's work)

| Case | First frames | timedemo demo1 |
|---|---|---|
| as shipped (OPL music) | 317-328 s | 6.3 fps |
| `-nomusic` | 192 s | 15.9 fps |

Board vCPU profile (host `perf`): 71% JIT code, ~15% TB lookup, devices
negligible -- frame rate is guest instruction count. Guest profiles:

- Start, music on: Doom's own samples ~75% Nuked OPL3 (`OPL3_*`) running in
  SDL's audio callback, ~15% soft-float resampling.
- Start, `-nomusic`: ~60% soft-float SFX resampling (`SDL_ConvertAudio`,
  U8 11025 mono -> S16 44100 stereo, `__mulsf3`/`__addsf3`).
- Frame (beast, `~/astra-mg/doom-prof.sh`): `Blit1to4` 21.9% (SDL 8bpp ->
  ARGB palette expansion), R_DrawSpan 18%, R_DrawColumn 12%.

## Done and committed

- `4d6acbee` POSIX: a signal that ran no handler no longer EINTRs a call
  (fsstress flake: SIGCHLD interrupted a pipe read).
- `4ea367e6` picolibc `fast-bufio` (fread was one getc per byte).
- conversion commit: host-side PCM conversion (host audio protocol 3,
  `astra_audio_convert.c`, pcm.library 2.2 `astra_pcm_convert`, SDL
  `SDL_BuildAudioCVT` host filter). Doom gate requires host conversions.
- Full verify green; release published to the DE25.
- **DE25 after: first frames at 38 s (from 317 s), music still OPL.**

## In progress (uncommitted): SoundFont synth system

Host: FluidSynth v2.6.1 pinned (`~/Git/fluidsynth`, `mk/build-fluidsynth.sh`
host|de25, static, no drivers/glib), `astra_audio_synth.c` (render thread +
lock-free ring; feeder never blocks), `astra_sha256.c`, daemon fonts store
(`StateDirectory=astra/soundfonts`, content-addressed) and MIDI voices,
host audio protocol 4 (adds `capacity`, reply `value`, FONT_* and MIDI_*
ops), QEMU generic pass-through. Guest: PCM wire 4, media service MIDI
sessions (default fonts from `/system/media/soundfonts/index`, `SYSTEM:r`),
pcm.library 2.3 `astra_midi_*` (`ndk/include/astra/midi.h`), SDL_mixer
native MIDI backend (`sw/userspace/sdl2/mixer/native_midi_astra.c`,
`MUSIC_MID_NATIVE`), Doom bundle `default.cfg` `snd_musicdevice 8`.
Default set: GeneralUser GS v2.0.3 (default) + TimGM6mb (system), fetched
pinned by `sw/userspace/services/media/fetch_soundfonts.py`.

Still to do: build everything on beast; daemon self-test
(`make -C fpga/arty/linux PLATFORM=de25 test-host`); QEMU stand-in
(`emu/qemu/test-sdl-audio.py` AudioHost) needs FONT/MIDI ops via ctypes
to the synth module; Doom gate should require a MIDI voice with sound;
check image capacity for 38 MB of fonts; tune synth gain on the board
(FluidSynth default 0.2 is quiet); docs (AUDIO_ARCHITECTURE, NDK pcm).

## Next after the synth

1. `Blit1to4` to hardware: INDEX8 palette LUT in the copy-burst pixel
   mover (serial blitter palette path is ~50-200 cycles/px, too slow), then
   builder/service/NDK palette plumbing and an SDL deferral of an INDEX8
   blit into a locked streaming texture (write-only by SDL contract).
2. zsh "crashed: pc 0 vector 0" in Terminal whenever an app opened with
   `open` from it exits (reproducible on the board).
