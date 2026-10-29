# AstraZ interpreter

Status, 2026-10-29: AstraZ GUI bundle runs on DE25. Real Zork I Z3 displays
its opening scene, takes commands at an inline `>` prompt, and updates its
fixed room/Score/Moves status line after a turn. Deadline Z3 displays the
room/Time status variant. The private Frotz engine also passed native DE25
save/restore and Z5 Adventure `look` gates. Synthetic Z1–8 and Blorb content
probes passed. Z6 Blorb pictures now use SDL2_image decode, a shared Image View
hardware blit and an ordered Graphics Kit canvas. FORM/AIFF, OGGV and MOD
resources use the generic SDL2_mixer/Codec Kit media path. DE25 trials have
opened Z3 Zork I, Z4 Trinity, Z5 Sherlock, Z6 Zork Zero, and Z8 One Room.
Z4/Z5/Z8 `look` responses and new prompts remain visible after scroll; Z6
artwork, paging and inline `look` run in the native 320×200 canvas. AIFF and
MOD synthetic media callbacks passed DE25 probes. The independent synthetic
font-3 glyph story displayed graphics and normal text on DE25; V6 Backspace,
Return, and in-canvas `look` also passed. Glulx execution is open.
The status text and prompt now share centered TextSurface row metrics. Frotz
style/font markers consume their argument even when zero, so the opening
`ZORK I` title is complete.

## Engine boundary

Filesystem Kit owns shared file type identification for Atlas and Open With.
It recognizes bare Z-machine and Glulx headers and Blorb container headers.
It does not select an interpreter or parse Blorb executable resources. AstraZ
owns those choices. No Z-machine engine API belongs in the NDK.

`sw/userspace/ports/astraz/include/astraz/engine.h` is private to the app.
`astraz_engine_probe` identifies VM, version, container, and executable span.
`astraz_engine_run` invokes host callbacks synchronously on the caller's
worker thread. Host handles input, file selection, screen commands, picture
metadata and drawing, sound playback, and cancellation. Events carry Blorb
resource bytes only during their callback. Save, restore, undo, transcript,
and recording remain Frotz operations; host file chooser chooses their paths.
Private header defines file purposes, ZSCII keys, style bits, fonts, colours,
and restart stages so GUI code never includes upstream Frotz headers.
The GUI host file chooser stores save, restore, transcript, and recording
files under `STORE:saves/`, inside `HOME:/Application Data/org.astra.astraz/`.
It moves the former `HOME:/AstraZ/` directory on first use; if old and new
directories both exist, it refuses to choose one silently. Frotz remains
responsible for serialization and its story-specific filenames.
Z-machine `save` writes [Quetzal](https://www.inform-fiction.org/zmachine/standards/quetzal/)
(`FORM/IFZS`) through Frotz's
`save_quetzal`; `restore` reads it through `restore_quetzal` from the same
`STORE:saves/` path. A DE25 Zork I save contained `IFhd`, `CMem`, and `Stks`
chunks and restored the previous turn. Quetzal format does not apply to
Glulx, whose execution backend remains unimplemented.
Frotz's global state permits one active run per process. Blorb supplies an
executable and optional resources, not a third VM. Glulx probe returns
`ASTRAZ_VM_GLULX`; run returns `ASTRAZ_UNSUPPORTED_VM` until a private Glulxe
backend and Glk adapter are added. No NDK engine interface is exported.

| Version | Adapter surface | Current execution evidence |
| --- | --- | --- |
| Z1–3 | Text, status, split screen, input, save/restore, beep | Zork I and Deadline GUI on DE25; score/moves and time status variants; Z3 Mini Zork save/restore; Z1–2 probe only |
| Z4–5 | Timed input, styles, colour, mouse, undo, Unicode, files | Z4 Trinity and Z5 Sherlock display and accept `look` on DE25 |
| Z6 | Pixel dimensions, windows, font metrics, pictures, sound resources, mouse | Zork Zero draws Blorb artwork, pages, and accepts inline `look` on DE25; font-3 hardware glyph gate pending |
| Z7–8 | Z5-style display and input, larger story addressing | Libre Z8 One Room displays and accepts `look` on DE25; Z7 execution still needs a fixture |

Host must advertise only capabilities it implements. `make_menu` remains
disabled because pinned Frotz core always returns failure for that optional
opcode. The GUI composes Interface Kit Toolbar, Container, Grid View, ScrollView,
TextView, and Field controls. Inline Field shares ordinary Field editing and
platform underline caret behavior without a box. Frotz window 7 feeds the fixed
Z1–3 status line, inverted through the shared Text View appearance API. It
keeps room, score/moves, or time current while transcript scrolls. Inline Field
and Text View center text within their rows so the caret aligns with `>`.
For text stories the host removes Frotz's final standalone prompt before
showing one inline Field prompt, while retaining `> command` in history. For
Z6, Frotz's prompt stays in the story canvas; AstraZ edits input with TextModel
and sends its 8×8 glyphs and platform 8×2 underline to the hardware renderer
at that cursor. No lower Field is shown.
The shared Grid View keeps upper-window cursor positions, erase and scroll
operations, style, and standard colors separate from the transcript. DE25
displayed Beyond Zork Z5's colored Character Setup upper window on 2026-10-28;
the empty-grid layout regression was fixed in Interface Kit 7.9. Evidence:
Beast `/home/barry/astra68/build/audit-de25-20261028/audit-more2.png`.
V6 uses native 320×200 story coordinates and an 8×8 fixed-cell AFNT bitmap
strike in the protected `/FONTS/system` catalog. Text is drawn by Astraea's
hardware glyph engine into an ordered canvas alongside PNG/JPEG pictures,
fills and overlap-safe scroll blits. The completed canvas is scaled uniformly
to the largest centered rectangle that fits the window by one Image View
hardware blit. The logical Z-machine geometry stays 320×200 when the native
window is resized, maximized, or put into Distraction Free with F11. That mode
hides the toolbar and display chrome; another F11 restores the prior state.
The system font is checked at story start so
an absent strike yields a specific error instead of a dropped display frame.
The regular, bold and italic 8×8 strikes derive from the BSD-2-Clause Spleen
8×16 source with a reproducible build-time transform; they are installed AFNT
faces, not private renderers. A separate protected AFNT holds Frotz's
GPL-2.0-or-later Z-machine graphics font 3 bitmap for the same hardware text
path. It is not selectable as a system UI font. Haiku reference:
`src/servers/app/font/GlobalFontManager.cpp` scans central font directories
and exposes families/styles to clients. Astra keeps one protected FONTS catalog
and performs text rasterization in the FPGA rather than FreeType in the server.
Haiku references: `src/kits/interface/TextControl.cpp` owns editable text in
a common control; `src/kits/interface/ScrollView.cpp` wraps one scrolling
target; `src/apps/terminal/TermView.cpp` separates caret presentation from
terminal content. Astra adapts those patterns to its native control tree and
uses one TextSurface underline caret geometry in Terminal, Field and AstraZ's
hardware canvas input. `src/kits/interface/TextView.cpp` maintains selection
and edit boundaries in the text model; AstraZ uses native TextModel for those
same responsibilities and Graphics Kit for presentation.
`src/kits/interface/Window.cpp` delivers `FrameResized` to the owning view;
AstraZ likewise reacquires its native surface on resize and relayouts its
shared controls. Interface Kit 7.11 wraps the transcript within the actual
ScrollView viewport while preserving TextModel content and selection. The
engine's logical text geometry stays 80 columns during a story. AstraZ's
private Frotz stream hook supplies words and explicit story newlines before
Frotz wraps them to that logical width; the GUI transcript can then reflow
paragraphs at the new viewport width without changing the VM header. Upper
windows and Z6 graphics keep their exact screen coordinates.

Frotz source is pinned in `third_party/frotz/ASTRA_IMPORT.md`. Its common core
and Blorb support compile into one `astraz_engine` MC68040 static archive with
NDK CMake/Ninja. Frotz headers stay private to that target and test frontend.
CLI and contract programs build only as internal smoke targets; their DE25
execution gates the engine, not the AstraZ user interface. Frotz is GPL-2.0-or-later;
AstraZ distribution must comply with its terms. Glulxe upstream is MIT and
requires a Glk implementation. Import it when native Glk adapter is ready;
Frotz alone cannot execute Glulx.

## File types and association

AstraZ bundle declares `application/x-zmachine-story` for `.z1` through
`.z8`, `application/x-glulx-story .ulx`, and `application/x-blorb` for `.blorb`,
`.zblorb`, and `.gblorb`. Content always wins over extension hints. Blorb
Inside AstraZ, Blorb `Exec` chunk type selects Z-code (`ZCOD`) or Glulx
(`GLUL`); resource-only or
other VM Blorbs receive clear unsupported-story errors. Current bundle
registration and Open With use `handles` directives in `docs/BUNDLE_FORMAT.md`.

## Required gates

- Bare Z-machine story from each version 1–8; version read from header. V6
  picture and sound paths need separate evidence.
- Bare Glulx and Z-code/Glulx Blorb, including picture and sound resources.
- Invalid/truncated files, conflicting extension, Blorb with no executable,
  safe removal while open, and story on read-only media.
- Native GUI input/output, scrollback, save/restore, undo, transcript, and
  clean quit. Atlas double-click opens bundle and user default survives reboot.
- DE25 play, keyboard, audio, display, CPU/memory, and regression gates for
  every supported backend. Use freely distributable test stories; do not ship
  commercial assets.

Haiku reference: `src/kits/storage/MimeType.cpp` keeps sniffing centralized;
`src/kits/storage/AppFileInfo.cpp` keeps supported types on application. Astra
adopts separation while using native Filesystem Kit and bundle contracts.

Format sources: [Z-machine standard](https://ifarchive.org/indexes/if-archive/infocom/interpreters/specification/),
[Glulx specification](https://github.com/iftechfoundation/ifarchive-if-specs/blob/main/Glulx-Spec.md),
[Blorb specification](https://github.com/iftechfoundation/ifarchive-if-specs/blob/main/Blorb-Spec.md),
[Frotz](https://gitlab.com/DavidGriffith/frotz),
[Glulxe](https://github.com/erkyrath/glulxe).
