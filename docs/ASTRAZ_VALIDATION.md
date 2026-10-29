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

The same generator creates `font3.z6`: it requests graphics font 3 with
`set_font`, prints `A`, restores font 1, prints `B`, then quits. Run it through
`astraz-host-trace` and require a font 3 event followed by the first character,
then a font 1 event followed by the second. In the final GUI gate, the font 3
character must use the standard adjacent 8 × 8 graphics glyph, rather than the
ordinary letter A. This follows the [Z-machine Standard's font 3 bitmap and
spacing contract](https://www.inform-fiction.org/zmachine/standards/z1point1/sect16.html).
`font3-display.z6` uses the same sequence but waits at `read_char` so DE25 can
capture both glyphs before the story exits.
Run `sh emu/qemu/astraz-media/test-font3-host.sh` to generate, compile and
assert the event order. The native-engine host trace now passes with two
characters and `font3=1` when the host advertises 8 × 8 font metrics. This
establishes engine selection only; the AFNT glyph drawing path still needs a
DE25 screenshot gate.

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

The first three input requests select bold once in Zork Zero and once in
Shogun. Journey selects reverse style 40 times and Shogun once. No emphasis or
graphics font 3 appears in these openings. A V6 bitmap text path must preserve
those bold and reverse uses. Logs are `zork0-style-3input.txt`,
`journey-style-3input.txt`, and `shogun-style-3input.txt` on Beast.

The same private engine harness also reached first input on the representative
Z3/Z4/Z5/Z8 stories without an engine error: Zork I (425 events), Trinity
(187), Sherlock (233), and One Room (789). Those host traces establish engine
progress only; the native GUI opening/input/exit gates still require DE25.

## Corrected DE25 media and Z3 gates

The focused installer rebuilt `build/.SDL.kit.stamp`, installed the matching
MOD-capable mixer and codec worker together with disposable media probes, and
restarted Astra once. The three production services remained active with
`astra.service` `NRestarts=0` and 8.2 GiB free. The synthetic AIFF test logged
`ASTRAZ_MEDIA_AIFF_PASS`; MOD logged `ASTRAZ_MEDIA_MOD_PASS` and Codec music
`underflows=0`. The audio daemon's status after both tests was zero FIFO
underruns, zero overflows and two software gaps since its last start. A
pre-test counter was not taken, so the gaps cannot be attributed to these
probes. HDMI was disconnected, so this is a digital playback gate only, not
an audible-user confirmation.

Zork I opened on DE25 with the `West of House` status line, complete opening
text and inline prompt. `look` returned the room text, advanced the move count
from zero to one, and showed another prompt. `quit` reached the score/end text,
and the window was closed. Screenshots `z3-scene.png` and `z3-look.png` are
archived on Beast under `/mnt/Documents/astra68/astraz-validation-20261029/`
with SHA-256 values `287172952ff2126b940322905b0339a3ece9ee64b05eab1384edaed1d5e16bc3`
and `3bbe653fcd6dfb38aad95eff926eecb99578ba60c39d22a683c678ad9ada6c93`.
The remaining Z4/Z5/Z6/Z8 GUI gates follow the 8 × 8 V6 font visual trial.

## Five-version GUI trial before scroll correction

The focused 8 × 8 V6 font candidate rendered Zork Zero's top banner, side
pillars, bitmap art, and tightly spaced prose on DE25. The matrix then found:

| VM | DE25 observation | Result at this checkpoint |
| --- | --- | --- |
| Z3 Zork I | West of House/status/prompt, `look` response and new prompt | Pass on prior candidate; final candidate needs regression check |
| Z4 Trinity | Palace Gate opening and prompt, `look` accepted but response clipped below window | Fail: scroll geometry |
| Z5 Sherlock | 221-B Baker Street/status/prompt, `look` response and new prompt visible | Pass |
| Z6 Zork Zero | Blorb art and prose visible, More advances, `look` accepted; stale duplicate More prompt appears below canvas | Partial: More presentation |
| Z8 One Room | Bathroom opening and prompt, `look` accepted but response clipped; stale More marker remains in transcript | Fail: scroll and More presentation |

Evidence is archived under `/mnt/Documents/astra68/astraz-validation-20261029/`
on Beast. Key PNG SHA-256 values: Trinity `z4-look-after.png`
`bb88635fd717908affbf1ac086f9aef59960c6dc6feac9b98449cfae3e03fe0f`,
Sherlock `z5-look-after.png`
`a9cae4592f4297c1ccfa3bf70a61bebf55a0add5b4ee10128c512ad60237a456`,
Zork Zero `z6-look-after.png`
`7eea8fcbd0b674725b7e1b1c19750cf021fd13ad270db91d301272779c9ef1c5`,
and One Room `z8-look-after.png`
`c21658622b1ee141161e6ba8198b2bd2a32e3d1f53a6fbe129633f5041e9b1e9`.
The UI owner has prepared a relayout after setting the scroll offset and a
transient More marker/overlay. Both require focused board retest.

## Scroll, More and prompt retest

The focused candidate with the ScrollModel relayout, transient More display,
and native 8 × 8 style faces ran on DE25. The independent follow-up verified:

- Z3 Zork I: `look` returned West of House text, advanced Moves to one and
  displayed the cursor. The frame also contains an invalid `open ...` command
  accidentally sent by the RFB harness after an immediate stale capture; this
  is trial input, not game behavior. Screenshot `z3-independent-settled.png`,
  SHA-256 `3ec6b8895cf34e25b71627eaece82d50ea82cf50241a9916b5ffb118ba600193`.
- Z4 Trinity: `look` returned the full Palace Gate description and a visible
  next prompt at the bottom of the window. Screenshot
  `z4-independent-settled.png`, SHA-256
  `83bcf37c2374738a3d3a13ca4f196435c1ec851385ff8d24d8ccc24e88f17483`.
- Z8 One Room: `look` returned the full Bathroom description after the scroll
  and a visible input cursor. Screenshot `z8-independent-settled.png`, SHA-256
  `7318fe40a7b657b93dc42e6d12fce74ec4c325facc75e9093f3f98f503188b43`.
- Z6 Zork Zero: title art, pillars and text remained visible. `look` returned
  Banquet Hall text; More appeared as a transient canvas overlay and cleared
  on Enter. Screenshots `z6-independent-typed.png` (SHA-256
  `f66cbe446850e16ed8e3303042fb09177a6649465f11143ec450391b860cd6b6`)
  and `z6-independent-more-clear.png` (SHA-256
  `8cd99258d18d9e1af2d9aa3ad815c592f31d433ca2cac77ce16474218e6b89be`).

All files are archived under `/mnt/Documents/astra68/astraz-validation-20261029/`
on Beast. The Astra, audio-host and remote-desktop services were active with
`Result=success NRestarts=0`; `/var/lib/astra` had 8.0 GiB free. The AstraZ
window was closed and board RFB scratch removed. Trial WORK stories and media
probe bundles remain only for the next focused candidate and must be removed
after its final gate. No HDMI listening conclusion is possible because the
board is disconnected from HDMI.

**UI defect at that checkpoint:** Z6 shows a story-canvas `>` and a separate lower `>_`
input; Z8 can show a story `>` above that lower input. Typing while Z6 More is
visible consumes the first character as the paging key, leaving the rest in
the lower input. The lower prompt must be integrated into each story's text
surface so the cursor appears after a single prompt. The scroll and media
gates above do not close this UI defect. Z5 passed on the preceding 8 × 8
candidate but was not rerun here; it needs a final regression check.

## Inline-input candidate on DE25

The focused AstraZ binary with the Z6 canvas input and text-version prompt
deduplication passed an independent `look` gate for every requested version.
Each screenshot shows one current prompt/cursor, and the response is visible:

| VM | Result | Beast screenshot SHA-256 |
| --- | --- | --- |
| Z3 Zork I | West of House status; Moves 1; complete `look` response | `z3-inline-settled.png` `1b0eda45ca67b6d0226f772f9534c368b6a01055def7baaaf339d27167a18d2b` |
| Z4 Trinity | Palace Gate status; complete `look` response after scroll | `z4-inline-settled.png` `b5979fdc6472d26eb2fae0b2952cf2e84d9a4a9e792bb80088335abb1666bfb9` |
| Z5 Sherlock | 221-B Baker Street status; time 5:01; complete `look` response | `z5-inline-settled.png` `67f132e13bd8b8cb595b12fff98da9bf8da60fe1ff68b3c6f8984194e7c36874` |
| Z6 Zork Zero | In-canvas `>look`, Banquet Hall graphics/text, transient More; one in-canvas `>` and adjacent underline after paging | `z6-inline-settled.png` `8faaa650e358ca2b9ade11761ce544610ee7b3d2d5232f529548b57c2dcaa77c`; `z6-inline-more-settled.png` `df86abee8d59b9ef71c6eb46485aff4deafe4c9b15d19fe375c294863fd7160a` |
| Z8 One Room | Bathroom text; complete `look` response after scroll | `z8-inline-settled.png` `9ad53d08687c6d3865dff1f7d68e547c9e4b561da8b7de918b0160fabad95096` |

Screenshots and `inline-matrix-journal.txt` are under the Beast archive path
above; journal SHA-256 is
`b5e3f88dbce1cd07932bb0ccd3151f0b86f321469c9ff078debd296ffa7df2f2`.
The recent journal excerpt contains no panic, AstraZ failure, display dropped
frame, or sound underflow line. Astra, audio-host and remote-desktop services
were all active with `Result=success NRestarts=0`; root filesystem had 7.9 GiB
free. The app was closed and board RFB scratch removed. WORK stories and
disposable media probe bundles remain for the font 3 hardware gate and final
cleanup.

## Font 3 and inline editing on DE25

The synthetic waiting `font3-display.z6` story rendered a graphics-font `A`
beside normal-font `B` through the protected AFNT face. The graphics glyph
appeared as the expected solid horizontal block. Beast screenshot
`font3-display-settled.png` has SHA-256
`de696218530a0e46bbb9270f5779fb85fde64c76858de36a3cc8e8d60e9121ff`.

In Zork Zero's visible canvas prompt, the RFB trial typed `loox`, pressed
Backspace, typed `k`, then Return. The settled frame showed exactly `>look`
inside the canvas, the Banquet Hall response and the transient More overlay;
there was no lower input field. Beast screenshot `z6-edit-settled.png` has
SHA-256 `d3370d45d8536216412c3d618bd10c5d764ff038a05b973544e3ca9eaef9e1c3`.
The immediate Backspace capture was stale: RFB reported a frame before Astra
presented the edited text. The settled response proves the edit reached the
interpreter. The RFB harness now sends the X11 BackSpace keysym for `\b`.

AstraZ was closed and board RFB scratch removed. Astra, audio host and remote
desktop services were active with `Result=success NRestarts=0`, and the board
had 8.1 GiB free. WORK stories and disposable probe bundles remain for the
final window/resize gates; remove only those exact trial paths afterward.
HDMI was disconnected, so audible playback certification remains open.

## Semantic transcript reflow host gate

The validator's private-engine trace now builds Frotz with the same
`ASTRAZ_FROTZ_TRANSCRIPT` definition as AstraZ and records semantic text and
newline events. On isolated Beast source, the AstraZ host tests and synthetic
font 3 trace passed. A Trinity Z4 opening trace kept paragraphs longer than
80 columns on single logical lines, with explicit paragraph breaks; the
`look` input produced a second Palace Gate response. The semantic stream is
therefore independent of Frotz's physical 80-column screen wrapping. The
standalone Frotz `stream.c` also compiled without the AstraZ definition.

Code review confirms live command echo is app-owned, while the new Frotz
input echo path runs only for replay input. Replay echo still needs an
executable fixture. DE25 resize/reflow evidence is recorded in
`docs/CURRENT_STATE.md`.

The first DE25 semantic candidate revealed a marker-boundary defect: a
style/font argument of zero was mistaken for a word terminator and could
drop the first letter of a room heading. After the engine's marker-aware
scan fix, `test-semantic-real-stories.sh` passed on isolated Beast with Z8
One Room and Z4 Trinity. One Room's opening and `look` response each contain
the complete `Bathroom` heading; the corresponding physical character
events also start with `B` (`bathroom_headings=2 bathroom_physical_B=2`).
Both stories retain lines longer than 120 characters as single logical
paragraph lines and preserve explicit blank lines. The host trace stops at
the third input by design (`ASTRAZ_CANCELLED`, status 7), with no engine error.

## Selected release on DE25

Integrated release `68829a6ba2332b829409749d33deeef5086b2717e8ecb9b755c03195768e9187`
passed package verification and system-image migration, then was deployed and
verified as selected on DE25. Desktop volume and Applications icons rendered.
Atlas opened Applications; its AstraZ icon launched selected bundle. Terminal
opened Z8 One Room from test WORK volume. Released AstraZ rendered wrapped
story text, complete `Bathroom` heading, and paging prompt. Beast archive
`/mnt/Documents/astra68/astraz-validation-20261029/astraz-selected-z8-settled.png`
has corresponding raw-frame SHA-256
`3b5bf4722d3bcc94cefd7685b11abf98eb9bad913f01e82401359d482c325b14`.
Earlier focused DE25 captures in `docs/CURRENT_STATE.md` cover normal,
maximized, response, and restored reflow plus Z6 artwork geometry.

## Lower-window text styles

Interface Kit 8.0 Text View accepts a caller-owned appearance callback at each
UTF-8 scalar's document byte offset. AstraZ retains style and palette metadata
with its semantic transcript, including when old paragraphs are trimmed. Frotz
style bits map bold to the bold AFNT face, emphasis to italic, and reverse to
swapped foreground/background through TextSurface. Fixed-width text already
uses the monospaced AFNT face. The Z-machine does not define an underline or
blink style; those remain general Astra TextSurface attributes.

Run `sh emu/qemu/astraz-media/test-style-host.sh` for the libre Z5 style
fixture and native-engine event gate. Beast Interface Kit host tests cover the
Text View callback, bold/italic attributes, reverse colours, and
rejected invalid callback output; AstraZ host tests cover split Frotz style
markers and UTF-8 byte alignment. `make -C ndk dist-check` and the MC68040
AstraZ build pass with `interface.library.8`.

DE25 selected release
`23bc412a98a28cd13d74882eaae47a8f7a77ef7eab5b50b1d8ae9a9b06bfa3a9`
shows all six fixture lines distinctly, including combined bold and reverse
and red text on a white background. Trinity Z4 shows the bold `Palace Gate`
heading below its inverted status line. Z8 One Room opens, presents bold room
headings, accepts `look`, and returns to a usable prompt. Raw RFB frames and
PNGs are archived on Beast under `/mnt/Documents/astra68/astraz-style-20261029/`.
The final style and colour fixture raw frame SHA-256 is
`b29e329d26e7a1f7240d150196b049ca8f96a27b4e7133753142e67373844de4`.
Private story and libre trial fixtures were removed from board WORK after the
captures. Installed release verification passes; Astra, audio, and remote
desktop services remain active with zero Astra restarts and 8.2 GiB free.
