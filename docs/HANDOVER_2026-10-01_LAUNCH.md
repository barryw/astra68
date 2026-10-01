# Handover 2026-10-01 (4): the launch panel

Read `CLAUDE.md`, `AGENTS.md`, then this page. Previous:
`HANDOVER_2026-10-01_SOUND.md`.

## Done and committed (full verify green)

- **Launch panel.** Opening an application shows its icon and name at
  once, over the windows, until it presents its first frame or its
  session ends. Doom needs no change.
  - The supervisor names the GUI session it opens for an application
    launch: `AstraGuiOpenSession` carries the bundle's `name` (cut to a
    whole UTF-8 character at 48 bytes) and its AICON in a second handle.
    GUI protocol version 17. Only `pump_launch` names a session; boot
    launches and services show nothing.
  - The display draws the panel once into its own Media RAM surface: the
    64x64 strike blended over the panel in one RGB565 upload, the name,
    and "Opening". It is a scene layer above the windows and below the
    menu. The first `WINDOW_PRESENT` of a window of that session, or the
    session's end, removes it. Pointer input passes through it.
  - One panel: a launch started while another loads takes it over
    (`ponytail:` note in `DisplayState`).
- **`ndk/Makefile` now includes the dynamic objects' `.d` files.** A
  header change never rebuilt `system.library.2`, so the GUI version bump
  booted a desktop still speaking version 16 (desktop status 49, then the
  supervisor and display exit). Any header change under `sw/include` was
  exposed to this.
- **Display: a content reset clears every bank's stale rectangle.** A
  window back from full screen carried a 1920x1080 stale region into the
  copy-forward blit from an 800x600 bank. QEMU clips it; the DE25 blitter
  refuses it (`BAD_RANGE`) and Doom's screen froze on its first frame.
  `CLAUDE.md` has the trap and the stall-dump procedure.

## Board, release `f0acd064`

- `~/astra-mg/hw` has no launch-panel script yet; the run used
  `splash_board.py` (beast `/tmp`, not kept): double-click Doom, capture
  at 1.5 s and 9 s, wait for its window, capture, quit.
- The panel is visible in the capture started 1.5 s after the
  double-click and still up at 9 s. Doom's window after 19.5 s (18.4 s in
  the previous handover; within run-to-run noise, not separated yet).
  Doom then renders; no refused requests in `journalctl -u astra`.

## Open findings

- On beast QEMU, `media` and `remote-desktop` exit with status 16 at
  every boot (host helpers absent). Already the case at `dd0e37fd`; not
  fatal, but it floods the trace ring and hides early lines from
  `said()` -- drain it while the machine runs.
- The panel capture bound is loose: the capture takes 2-4 s. A precise
  click-to-panel latency needs a display-side timestamp.
- Carried over: the HostFS direct-path grant hole, picolibc
  `ftrylockfile`, per-run bundle-font upload, music gain.

## Next, in order (user direction)

1. **Cut Doom's 19.5 s start** with IO, kernel, OS and SDL changes.
   Profile on the board first; `~/astra-mg/hw/doom_startprof.py` exists.
2. The centralized logging design in `docs/SYSTEM_LOG.md` (uncommitted)
   is for the user to review.
3. Terminal access to SOUND: -- `SOUND:rw` in the Terminal manifest, the
   ceiling and `console_session.c`'s mount list together.
4. Put `test-hostfs.py` in `~/astra-mg/verify-nopc.sh`.
