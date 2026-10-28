# AstraZ interpreter

Status, 2026-10-27: pinned Frotz core and a private native callback adapter
build as one AstraZ archive. Its native test client played Z3 on DE25, saved,
advanced a move, then restored the earlier move count. It also played a Z5
Adventure story through a `look` turn. A second native test
passed content probes for synthetic Z1–8 headers, Z-code/Glulx Blorbs, and
malformed files. AstraZ GUI does not exist yet; games cannot launch from an
AstraZ icon. No Glulx executor exists yet.

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
Frotz's global state permits one active run per process. Blorb supplies an
executable and optional resources, not a third VM. Glulx probe returns
`ASTRAZ_VM_GLULX`; run returns `ASTRAZ_UNSUPPORTED_VM` until a private Glulxe
backend and Glk adapter are added. No NDK engine interface is exported.

| Version | Adapter surface | Current execution evidence |
| --- | --- | --- |
| Z1–3 | Text, status, split screen, input, save/restore, beep | Z3 Mini Zork played, saved, restored on DE25; Z1–2 probe only |
| Z4–5 | Timed input, styles, colour, mouse, undo, Unicode, files | Z5 Adventure played one `look` turn on DE25; Z4 probe only |
| Z6 | Pixel dimensions, windows, font metrics, pictures, sound resources, mouse | Synthetic header probe only; no picture/sound host yet |
| Z7–8 | Z5-style display and input, larger story addressing | Synthetic header probe only |

Host must advertise only capabilities it implements. `make_menu` remains
disabled because pinned Frotz core always returns failure for that optional
opcode. Host callbacks still need Interface Kit, Graphics Kit, and Codec/Audio
Kit implementations before V6 graphics/sound or a GUI can be claimed.

Frotz source is pinned in `third_party/frotz/ASTRA_IMPORT.md`. Its common core
and Blorb support compile into one `astraz_engine` MC68040 static archive with
NDK CMake/Ninja. Frotz headers stay private to that target and test frontend.
CLI and contract programs build only as internal smoke targets; their DE25
execution gates the engine, not the AstraZ user interface. Frotz is GPL-2.0-or-later;
AstraZ distribution must comply with its terms. Glulxe upstream is MIT and
requires a Glk implementation. Import it when native Glk adapter is ready;
Frotz alone cannot execute Glulx.

## File types and association

AstraZ bundle will declare `application/x-zmachine-story` for `.z1` through
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
