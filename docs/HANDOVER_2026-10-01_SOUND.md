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

## Done, not yet committed (verify was running)

All pieces below were green in their own gates. A full verify was running
when this was written.

**HostFS volumes.** One Linux directory with a subdirectory per volume
(`work/`, `sound/`).
- `AstraHostCommand.reserved0` became `volume`; command version 2; host
  interface `0x1000c`.
- QEMU resolves each path `RESOLVE_BENEATH` that volume's fd.
- The hostfs service serves `WORK:rw METRICS:r SOUND:rw`. SOUND has no
  direct accelerator path.
- `run-arty.sh` moves a pre-volume root into `work/` once, and installs
  shipped fonts the SOUND volume lacks.
- `test-hostfs.py` was stale on version and caps and is fixed. It is not
  in verify; add it.

**SoundFonts on SOUND:.**
- The daemon reads `sound/soundfonts/` in place through a FluidSynth loader
  callback. `sound:NAME` names are opened with `openat2` (`RESOLVE_BENEATH`,
  no symlinks).
- `soundfonts/default` lists the default stack.
- The media service no longer reads guest fonts and no longer needs
  `SYSTEM:r`.
- Fonts are no longer installed into the guest image.
- SDL native MIDI stacks a bundle's `resources/soundfonts/*.sf2` (uploaded).

**MIDI API, host audio protocol 5 and PCM wire 5.**
- New host operations: `FONT_LIST`, `MIDI_PRESETS`, `MIDI_EVENTS`,
  `MIDI_SET`. `MIDI_STATUS` returns a record.
- pcm.library 2.4 adds `astra_midi_fonts`, `astra_midi_presets`,
  `astra_midi_send`, `astra_midi_set` and `astra_midi_status`, plus inline
  note, program, control and bend helpers.
- `AstraMidiSong` was renamed `AstraMidiSynth`.
- The daemon self-test covers all of it. The pcm.library unit test only
  covers the renamed API so far; add fake-service cases for the new calls.
- Still to do: build on beast, then run `make -C fpga/arty/linux
  PLATFORM=de25 test-host`, `make -C sw/userspace test`, and the loopwave
  and Doom gates. Then full verify, commit, and publish.

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
