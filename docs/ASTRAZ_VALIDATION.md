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
quitting. Companion Blorbs contain a 64 × 64 PNG and either AIFF or a valid
four-channel ProTracker MOD. `emu/qemu/astraz-media/probe.c` runs the native
private engine, validates the picture resource and SDL_image decode, then
requires SDL_mixer playback. MOD is a required acceptance case for the generic
Codec Kit tracker provider. The fixture follows the
[Z-machine opcode table](https://www.inform-fiction.org/zmachine/standards/z1point1/sect14.html)
and [V6 routine entry rule](https://www.inform-fiction.org/zmachine/standards/z1point1/sect05.html).

Each DE25 run must show the visible scene and prompt, accept input, exit
cleanly, and leave `astra.service` active with zero restarts, no new kernel
panic, and no audio fault during playback. The story and media probes are
installed only for a focused trial; screenshots and logs go to Beast before
the board scratch and trial bundles are removed.

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
