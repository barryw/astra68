# AstraZ interpreter

Status, 2026-10-27: story inspection API and pinned Frotz core build exist.
Frotz's test-only CLI played a Z3 story on DE25, reached the first prompt,
and advanced a move after typed input.
AstraZ GUI and playable native engine adapter do not exist yet. Do not list
AstraZ as installed or claim games are playable from its icon.

## Engine boundary

Filesystem Kit owns content identification. `astra_story_inspect()` opens one
file, identifies bare Z-machine versions 1–8 and Glulx by header, and finds
the `Exec` payload in a Blorb resource index. It returns VM, version, container,
and bounded payload offset/length. Filename does not choose engine. File Types
uses same inspection, so Atlas and AstraZ cannot disagree on known content.

AstraZ will use one app-private interpreter library with a small session API:
open, run until input or output event, submit input, save, restore, and close.
Frotz is Z-machine backend; Glulxe is Glulx backend. Blorb is container and
resource source for both, not a third VM. VM-global upstream state means
initial library permits one session per process. Native interface adapter
will feed shared Interface Kit text, field, scroll, and menu controls; graphics
and sound resources go through existing Graphics and Codec/Audio Kits.

Frotz source is pinned in `third_party/frotz/ASTRA_IMPORT.md`. Its common core
and Blorb library compile as MC68040 static archives with NDK CMake/Ninja.
CLI frontend builds only as internal smoke target; its Z3 execution is an
engine gate, not the AstraZ user interface. Frotz is GPL-2.0-or-later;
AstraZ distribution must comply with its terms. Glulxe upstream is MIT and
requires a Glk implementation. Import it when native Glk adapter is ready;
Frotz alone cannot execute Glulx.

## File types and association

AstraZ bundle will declare `application/x-zmachine-story` for `.z1` through
`.z8`, `application/x-glulx-story .ulx`, and `application/x-blorb` for `.blorb`,
`.zblorb`, and `.gblorb`. Content always wins over extension hints. Blorb
`Exec` chunk type selects Z-code (`ZCOD`) or Glulx (`GLUL`); resource-only or
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
