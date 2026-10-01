# Handover 2026-10-01 (3): SoundFonts on SOUND:, the MIDI API, Doom start

Read `CLAUDE.md`, `AGENTS.md`, then this page. Previous:
`HANDOVER_2026-10-01_DOOM_PERF.md`.

## Done and committed

- `a74665ea` host SoundFont synthesizer (FluidSynth), Doom music off the
  040. Root cause of the Doom title-screen hang found on the way:
  `native_midi_astra.c` swapped SDL_RWread's size/count, so every song
  load "failed", and it then closed the stream SDL_mixer still owned;
  Mix_LoadMUSType_RW's rewind locked the freed FILE mutex.
- Release `72fe25da` published to the DE25. Measured there
  (`~/astra-mg/hw/run_doom_board.sh`):
  - Doom window after **18.4 s** (was 38 s, 317 s before).
  - Demo 59.6 render batches/s (was 32), game 79.9 (was 50), windowed
    121.5 (was 69).
  - astra-audio-host uses 48% of an A76 core.

## Done and committed: `ab156ec2` (full verify green)

- **HostFS volumes.** One Linux directory, one subdirectory per volume
  (`work/`, `sound/`). The command's `volume` field is resolved
  `RESOLVE_BENEATH` that volume's directory; the command version is 2 and
  the host interface is `0x1000c`. hostfs serves `WORK:rw METRICS:r
  SOUND:rw`.
  - `run-arty.sh` moves a pre-volume root into `work/` once.
  - `test-hostfs.py` now passes and covers isolation. It is not in verify;
    add it.
- **SoundFonts in `SOUND:soundfonts`.**
  - The daemon reads them in place through a FluidSynth loader callback
    (`openat2`, no links), so a guest-side change is what the host plays.
  - `default` lists the default stack.
  - The release ships the set; the launcher installs only fonts the volume
    lacks.
  - Bundle fonts (`resources/soundfonts/*.sf2`) are sent by digest.
- **MIDI API.** Host audio protocol 5, PCM wire 5, pcm.library 2.4:
  `astra_midi_fonts`, `astra_midi_presets`, `astra_midi_send` with inline
  helpers, `astra_midi_set`, `astra_midi_status`. `AstraMidiSynth`
  replaces `AstraMidiSong`.
- **Board, release `3a0c8642`.**
  - `hostfs/work` was migrated and `hostfs/sound/soundfonts` installed.
  - Doom window after 18.4 s; music renders from SOUND (astra-audio-host at
    ~50% of a core).
  - Demo 62.8, game 78.6, windowed 118.7 render batches/s.

## Open findings

- **The HostFS direct path hands every WORK: client the raw host device**
  (`vfs_port_transport.c` HELLO duplicates `host->accelerator` with
  READ|WRITE). A `WORK:r` client can write, and any client can reach
  audio, remote desktop and metrics.
  - Read-only is enforced only in the client library, for every volume.
  - Fix: per-handle grant bits passed by the kernel to QEMU at channel
    open, with QEMU refusing services and volumes outside the grant. This
    needs the user's go-ahead; it is a kernel and ABI change.
- picolibc's `ftrylockfile` takes `__LIBC_LOCK`, but `funlockfile` releases
  the per-file mutex (`stdio-locking=true`), so the pair aborts. Nothing
  in the tree uses it yet.
- Bundle fonts are uploaded once per program run; there is no cache across
  launches.
- Music peak is 0.26 of full scale at FluidSynth's default gain 0.2;
  sound effects peak around 0.59. Tune on the board by ear.

## Next, in order (user direction)

1. **Immediate launch splash, no Doom changes.** The supervisor already
   opens a GUI session per app launch (`ASTRA_GUI_OPEN_SESSION`,
   `loader.c:701`). Carry the app name and bundle `.aicon` area with it.
   The display draws a launch panel (it already draws title icons:
   `draw_title_icon`) and removes it on the session's first
   `WINDOW_PRESENT`, or when the session ends.
2. **Cut Doom's 18.4 s start** with IO, kernel, OS and SDL changes. Profile
   on the board first; `~/astra-mg/hw/doom_startprof.py` exists. Earlier
   evidence: demand faults cost tens of ms each on the board.
3. The centralized logging design in `docs/SYSTEM_LOG.md` (uncommitted) is
   for the user to review.
4. Terminal access to SOUND:. This needs `SOUND:rw` in the Terminal
   manifest, the ceiling, and `console_session.c`'s mount list together;
   adding it to the ceiling alone broke PCM.
