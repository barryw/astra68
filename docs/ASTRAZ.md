# AstraZ interpreter

Status, 2026-10-28: AstraZ GUI bundle runs on DE25. Real Zork I Z3 displays
its opening scene, takes commands at an inline `>` prompt, and updates its
fixed room/Score/Moves status line after a turn. Deadline Z3 displays the
room/Time status variant. The private Frotz engine also passed native DE25
save/restore and Z5 Adventure `look` gates. Synthetic Z1–8 and Blorb content
probes passed. Glulx execution and Z6 picture/sound rendering remain open.
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
| Z4–5 | Timed input, styles, colour, mouse, undo, Unicode, files | Z5 Adventure played one `look` turn on DE25; Z4 probe only |
| Z6 | Pixel dimensions, windows, font metrics, pictures, sound resources, mouse | Synthetic header probe only; no picture/sound host yet |
| Z7–8 | Z5-style display and input, larger story addressing | Synthetic header probe only |

Host must advertise only capabilities it implements. `make_menu` remains
disabled because pinned Frotz core always returns failure for that optional
opcode. The GUI composes Interface Kit Toolbar, Container, Grid View, ScrollView,
TextView, and Field controls. Inline Field shares ordinary Field editing and
platform underline caret behavior without a box. Frotz window 7 feeds the fixed
Z1–3 status line, inverted through the shared Text View appearance API. It
keeps room, score/moves, or time current while transcript scrolls. Inline Field
and Text View center text within their rows so the caret aligns with `>`.
The shared Grid View keeps upper-window cursor positions, erase and scroll
operations, style, and standard colors separate from the transcript. DE25
displayed Beyond Zork Z5's colored Character Setup upper window on 2026-10-28;
the empty-grid layout regression was fixed in Interface Kit 7.9. Evidence:
Beast `/home/barry/astra68/build/audit-de25-20261028/audit-more2.png`.
V6 picture/sound callbacks still need GUI work.
Haiku references: `src/kits/interface/TextControl.cpp` owns editable text in
a common control; `src/kits/interface/ScrollView.cpp` wraps one scrolling
target; `src/apps/terminal/TermView.cpp` separates caret presentation from
terminal content. Astra adapts those patterns to its native control tree and
uses one TextSurface underline caret in Terminal and Field.

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
