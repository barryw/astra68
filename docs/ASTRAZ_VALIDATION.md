# AstraZ acceptance matrix

Status: validation in progress, 2026-10-29. This document records test inputs
and observations. The private Infocom files are local test material only and
must never enter the repository or a distributable image.

| VM | Story | Primary gate |
| --- | --- | --- |
| Z3 | `Zork1.z3` | Opening, West of House status, `look`, quit |
| Z4 | `Trinity.z4` | Palace Gate status, `look`, quit |
| Z5 | `Sherlock.z5` | Opening, prompt, `look`, quit |
| Z6 | `Zork0.z6` with adjacent `Zork0.blb` | Title picture, screen text, input, quit |
| Z8 | IF Archive `oneroom.z8` | Bathroom opening, `look`, quit |

The Z8 test file is [One Room on IF Archive](https://ifarchive.org/if-archive/games/zcode/oneroom.z8),
113,528 bytes, SHA-256
`c64c595e773c70d055eccfa0dd98c79d84b426e33bd90b46a93d9a08bc4f9858`.
Its opening names the Bathroom; `look` describes the room, and `quit` exits
cleanly under the reference Frotz interpreter.

The deterministic fixture at `emu/qemu/astraz-media/fixture.py` creates a bare
Z6 story that executes `draw_picture(7,1,1)` and `sound_effect(3,2)` before
quitting. Companion Blorbs contain a 64 × 64 PNG and either a one-second
AIFF tone or a valid four-channel ProTracker MOD. `emu/qemu/astraz-media/probe.c`
runs the native private engine, validates the picture resource and SDL_image
decode, then checks that SDL_mixer playback starts and remains active. MOD is
a required acceptance case for the generic
Codec Kit tracker provider. The fixture follows the
[Z-machine opcode table](https://www.inform-fiction.org/zmachine/standards/z1point1/sect14.html)
and [V6 routine entry rule](https://www.inform-fiction.org/zmachine/standards/z1point1/sect05.html).

Each DE25 run must show the visible scene and prompt, accept input, exit
cleanly, and leave `astra.service` active with zero restarts, no new kernel
panic, and no audio fault during playback. The story and media probes are
installed only for a focused trial; screenshots and logs go to Beast before
the board scratch and trial bundles are removed.
Announce the AIFF and MOD audio windows before launching their audible probes.

## Baseline before V6 integration

- On DE25, `Trinity.z4` opened in the GUI with Palace Gate status and scene;
  `look` returned the room text and a new inline prompt.
- `Zork0.z6` with adjacent Blorb stopped immediately at `Picture resource
  unavailable`; no title image appeared. This is a confirmed V6 media gap in
  the prior selected AstraZ build, not a result for the candidate now being
  installed.
- Screenshots are archived on Beast under
  `/mnt/Documents/astra68/astraz-validation-20261029/`. The trial files and
  RFB captures were removed from DE25; `astra.service` remained active with
  zero restarts and 8.1 GiB free.
- The Beast host QEMU fixture reached stage 8 but then failed to launch its
  remote-desktop service (`status=16`). It did not start the synthetic media
  test application. This is a host fixture failure; no AstraZ media conclusion
  can be drawn from it.

## First native media trial

The generic Codec Kit tracker worker and disposable probe bundles were
installed in one focused DE25 restart. The synthetic picture event reached
the host and decoded as PNG in both cases. The SDL kit installed in that trial
contained an older SDL2_mixer binary because the installer used the directory
`build/SDL.kit` as its make target. The actual build target is
`build/.SDL.kit.stamp`; the Beast mixer and packaged kit have since been
rebuilt and their SHA-256 values match. The board has not yet received that
rebuild.

- AIFF failed before the sound callback: `status=6 pictures=1 sounds=0`,
  `Sound resource unavailable`. Frotz's Blorb library reports a `FORM` chunk's
  FilePos at its header and counts that header in its length; AstraZ's existing
  resource loader assumed a payload position and length. The engine owner has
  corrected the loader locally; this trial is evidence for that fix.
- MOD reached the sound callback: `status=6 pictures=1 sounds=1`, then the
  audio adapter rejected it. The installed mixer was stale, so this does not
  determine whether the new generic MOD provider or current AstraZ dispatch
  works. Both must be installed together and the probe rerun.
- Trial logs, audio-host journal and screenshots are archived on Beast under
  `/mnt/Documents/astra68/astraz-validation-20261029/`. The board's Astra,
  audio-host and remote-desktop services remained active, Astra restart count
  stayed zero, and 8.2 GiB remained free. Disposable probe bundles and Zork0
  WORK files are retained only for the next focused retest.

## Zork Zero first-frame trace

The first V6 window and colored picture appeared on DE25 after the shared
draw-list fix. The large text area was blank, so this is not yet a passing
opening-scene gate. The engine owner paused the GUI for user inspection.

An independent host build of the same private engine ran `Zork0.z6` with its
adjacent Blorb and stopped at the first input request. It emitted 721 events:
14 pictures and 611 text characters. The first frame draws PNG #5 (320 × 34)
at row 1, column 1; #497 (36 × 166) at row 35, column 1; and #498 (37 × 166)
at row 35, column 284. Window 0 text cursor advances from row 40 to row 376.
The GUI's blank text area therefore cannot be attributed to a lack of engine
text events. Full coordinates are archived on Beast in
`/mnt/Documents/astra68/astraz-validation-20261029/zork0-host-trace.txt`
(SHA-256 `b2043ebcb5c7a9f97b95c2195ff8802883f18057e559f0f147179df85a14715e`).

At the candidate's corrected 320 × 200 V6 geometry with 8 × 8 cells, the same
story reaches first input after 557 events, including 14 pictures and 449
characters. Every picture and cursor position stays in bounds; the opening
uses fonts 1 and 4, with no font 3 selection. That trace is
`zork0-host-trace-320x200.txt` (SHA-256
`acc7f21e0afbd92e40c033dd641773c030bb6aa0f59255434901a46e1f9b9a86`).

The host trace harness also ran both synthetic Blorbs against the current
engine. AIFF's `FORM` resource reached the sound callback with 8,046 payload
bytes, and MOD reached it with 2,140 bytes. Each run emitted its PNG picture
and exited with `ASTRAZ_OK`. This verifies companion discovery, resource
position normalization, and engine event dispatch; SDL playback still needs
the board gate. Logs are archived next to the Zork Zero trace as
`media-aiff-host-trace.txt` and `media-mod-host-trace.txt`.

At native V6 geometry, Journey's first three input requests produced 313
events, one picture and 104 characters; Shogun produced 767 events, four
pictures and 614 characters. Neither selected graphics font 3 or drew a
picture outside 320 × 200. Both repeatedly selected default color 1/1. One
Journey window-0 cursor moved to row 201, the position immediately after the
last row; the host must clip it. Evidence is archived as
`journey-host-trace-3input.txt` and `shogun-host-trace-3input.txt`.

The same private engine harness also reached first input on the representative
Z3/Z4/Z5/Z8 stories without an engine error: Zork I (425 events), Trinity
(187), Sherlock (233), and One Room (789). Those host traces establish engine
progress only; the native GUI opening/input/exit gates still require DE25.
