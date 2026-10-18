# Astra Fonts and Hardware Glyph Rendering (draft v0.3)

> AFNT remains the native hardware-ready font contract. The DE25 graphics
> target provides A8 coverage and XRGB8888 destinations as required by
> [`GRAPHICS_ARCHITECTURE.md`](GRAPHICS_ARCHITECTURE.md).

This document defines the native font direction for Astra 68 and the boundary
between font files, the font/display services, and Astraea. The graphical OS
must not draw bitmap glyphs with CPU pixel loops.

## 1. Locked architecture

### Active font-information work (2026-10-18)

Source `f4fb7263bea50dea60ec620c99774c3830e8842f` clean-builds as private
release `ecd9020a954d0b9f7d5ca935f5c109f30b74c73ae6408922555078a55d941b83`.
Its first full DE25 cycle passes native malformed-frame refusal and repair on
three boots, Iosevka import/information/copy/live selection/saved restart/
restoration/uninstall, and the previously panicking long-license workflow.
Actual HDMI inspection confirms viewport clipping, selection, source/hash,
license URL, original-byte fallback and reachable Back action. Evidence is
checksum-verified on Beast under
`/var/tmp/astra-font-information-evidence-20261018/cycle1/`. Production selector
and stopped storage bytes remain unchanged; all services restore active with
zero restarts. A second full functional cycle also passes, and restored
production HDMI exactly matches its known-good frame. Fourteen of fifteen
candidate captures match across cycles. The differing frame contains another
test-probe instance: `required` correctly restarts that finite probe, so the
harness now selects existing `restart=never` policy and checks one launch per
boot. Corrected one-shot qualification passes all three boots, and all fifteen
HDMI frames match the clean second-cycle frames exactly. Corrected evidence is
checksum-verified under `one-shot/` and `warm-one-shot-production/` in the same
Beast root. Final production storage remains byte-identical; warm HDMI again
matches its known-good frame, with services active and zero automatic restarts.
No Display teardown change is indicated. This component passes its correctness
gates; the candidate is not activated and broader Fonts workflow/release work
remains open.

Font Kit 4.2 adds owned immutable information snapshots through FONTS protocol
7. The service reuses installed-face lookup, AFNT validation and user-object
content-identity verification, but copies only NAME metadata into an exactly
sized read-only area. Direct Fonts-service AFNT delivery remains restricted to
FONTS_RENDER; GUI context separately supplies the active font for client metrics.
Applications use typed string fields and original SFNT notice identifiers;
Font Kit owns JSON decoding, including complete multiline strings, missing
optional fields and exact original byte records. No JSON parser lives in Fonts.

The existing AFNT name decoder is replaced by the common counted JSON parser.
It rejects malformed unknown values, invalid separators/numbers, invalid UTF-8,
unpaired UTF-16 escapes and nesting beyond its explicit 32-container bound.
AFNT NAME remains format 0.2, an object bounded to 1 MiB. Single-line family and
style extraction retains its stricter control-character policy. Standalone
strings preserve counted embedded NUL; copies measure first and never truncate.

Read-only Haiku references: `src/kits/shared/Json.cpp` (`ParseObject`,
`ParseArray`, `ParseStringEscapeSequence`, `ParseString`, `ParseNumber`) for
grammar-aware parsing and escape validation; `src/kits/interface/Font.cpp`
(`GetFamilyAndStyle`, `GetTunedInfo`) for service-owned font queries. Astra
adapts these patterns to bounded allocation-free C spans and capability-scoped
immutable snapshots. No Haiku source was changed.

Long-notice qualification also exposed oversized logical background fills:
the virtual Text View can exceed 65,535 pixels while its viewport stays small.
Shared Surface fill now clips logical bounds before queuing a hardware command;
text and metadata remain complete. Haiku read-only references are
`src/servers/app/drawing/DrawingEngine.cpp:FillRect` (clip before drawing) and
`src/servers/app/ServerWindow.cpp:AS_DRAW_STRING` (bad input/allocation failure
ends that drawing request). Astra uses its existing GPU command path and
committed content banks. A malformed retained client list now follows existing
frame refusal rather than terminating Display; its previous content stays
visible and other windows continue. Native malformed geometry/header/opcode
refusal and repair pass, as does the formerly panicking long-license workflow.
Batch-resource refusal also covers RGB content; focused tests, sanitizers,
analyzers and corrected DE25 component gates pass (Plane ASTRA-60). Overall
Fonts release acceptance remains open.

Fonts' Source and license action switches the existing ScrollView's single
content slot from List to shared Text View. Back preserves the selected style;
Meta+C copies selected text through Clipboard Kit. TextModel computes exact
arena requirements; Fonts supplies its document-owned allocations and retains
no independent text, layout or rendering implementation. Allocation or metadata
failure leaves font management available. Coherent userspace/ROM, packaged NDK
and native Iosevka information/copy/select/restart/uninstall checks pass for
this checkpoint. Native long-text, long-style and unavailable-preview gates also
pass. Corrected DE25 component qualification also passes; overall Fonts release
acceptance remains open;
production remains unchanged.

The Fonts pane uses shared multiline text.
Interface Kit 7.1 adds a read-only Text View composed from TextModel,
TextSurface and ScrollView. Its first presentation is fixed-grid with horizontal
overflow; original UTF-8, tabs and CRLF survive selection/copy. Pointer and
keyboard selection share the existing Field machinery. List and Text View
also share rectangle reveal through the existing scroll model. Interface
Gallery's Text page hosts the new control. Existing public control/context
layouts are unchanged; the Kit minor and its two new symbol versions advance
together.

Haiku references: `headers/os/interface/TextView.h` and
`src/kits/interface/TextView.cpp` (`Draw`, `MouseDown`, `MakeEditable`,
`SetWordWrap`). Adopt independent selectability for read-only text, visible-line
drawing and ordinary scrolling composition. Adapt to Astra's caller-owned
piece table and hardware draw lists rather than copying Haiku's view hierarchy
or CPU drawing. Haiku `src/kits/interface/Font.cpp` service queries also inform
the Font Kit metadata query above.

TextSurface owns both glyph emission and exact command/payload accounting.
Text View uses that shared run walk for draw-list preflight, including selection
runs, UTF-8 chunking and trimmed spaces. A 30-line, 80-column document exposed
over-reservation against the retained window's 128-command limit; the regression
fails in `/var/tmp/astra-text-view-capacity-red-20261018.log` and passes with
ordinary and cross-line selections in
`/var/tmp/astra-text-view-capacity-green-20261018.log`. This fixes capacity
correctness without changing the draw protocol or adding application budgeting.
The native Gallery example now uses a 420-pixel viewport to exercise a realistic
document. The Clipboard Kit snapshot is inspected byte-for-byte by the existing
native NDK `text_clipboard --inspect` example, using shared AstraString hex
formatting. Interactive Terminal paste and native stdout redirection were
separately found unsuitable as oracles; the observed product defects are Plane
ASTRA-58 and ASTRA-59. Ordinary handled PMMU faults are not mistaken for crashes.

Native worker metadata advances to `astra-font-worker/0.3`. Outline imports
retain every SFNT copyright, license and license-URL record in `NAME.notices`,
including duplicates and languages. Each record carries name/platform/
encoding/language IDs and exact `raw_hex`; supported UTF-16BE and Macintosh
Roman records additionally carry complete UTF-8 `text`. Invalid, embedded-NUL
and unsupported encodings retain original bytes without guessed text. Format-1
language tags retain original bytes and decoded text when valid. FreeType's
public language-tag accessor rejects valid first tag ID `0x8000`; the importer
uses bounded `FT_Load_Sfnt_Table` reads for all language tags instead.
The counted-string contract is documented in the
[FreeType SFNT names API](https://freetype.org/freetype2/docs/reference/ft2-sfnt_names.html);
the accessor's exclusion is visible in
[`ftsnames.c`](https://github.com/freetype/freetype/blob/VER-2-13-3/src/base/ftsnames.c#L110).

The shared AFNT validator now names its metadata resource bound explicitly:
`ASTRA_AFNT_NAME_MAX`, 1 MiB. Complete notices plus encoded originals can exceed
the previous 64 KiB bound. Other file/glyph bounds remain enforced; imports
that exceed them fail instead of truncating legal text. AFNT stays format 0.2;
checked-in font objects are unchanged. Converter regression compares every
original notice byte and tests a 32 KiB source record, Unicode, newlines, tabs,
Mac Roman, malformed UTF-16, embedded NUL, unknown encoding and first language
tag. The common codec separately checks the exact metadata bound and one byte
beyond it.

Native Iosevka import, live switching, restart, built-in restoration and
uninstall pass in `/var/tmp/astra-font-notices-native-ui-final-20261018.log`.
While stopped, the installed AFNT validates against its original source hash
and six complete notices, including exact original bytes and decoded Unicode
text. No production image or board selector is changed. Plane ASTRA-56 tracks
Text View, ASTRA-57 tracks notice retention, and ASTRA-36 remains in progress
for the remaining Settings shell, import workflow and release acceptance.

- The native font container is **AFNT** (Astra Font), a passive, versioned,
  big-endian bitmap-font format.
- AFNT contains one or more designed bitmap strikes and may retain a sanitized
  scalable source for sizes not already materialized. The host font worker
  rasterizes a missing size into a protected cache; the MC68040 never runs an
  outline rasterizer or scales glyph pixels.
- Software performs UTF-8 decoding, character-to-glyph mapping, fallback,
  kerning, shaping, and line layout.
- Astraea expands already-positioned glyph runs into RGB565 or INDEX8 drawing
  surfaces. Hardware never parses a font file, Unicode, or filesystem data.
- Glyph rendering shares Astraea's clipping, exact pixel port, registered local
  memory owner, and completion fences. It is not a second framebuffer writer.
- Amiga bitmap fonts are supported through an importer. Their executable,
  pointer-bearing, planar disk representation is never a native or hardware
  input format.

This split keeps complex and untrusted parsing out of RTL while removing the
expensive per-pixel work from the 68040.

## 2. Guaranteed resident fonts

Astra has two availability tiers.

### 2.1 Rescue face in FPGA BRAM

**Astra Rescue Mono** is a true 8x16, one-bit monospaced face stored in the FPGA
image. It is available before SDRAM initialization and when SD, SDRAM, the
system ROM image, or the font service has failed.

- Used by reset, POST, recovery, and the kernel panic console.
- Covers printable ASCII, Latin-1 characters needed by diagnostics, CP437 box
  drawing, arrows, and a replacement glyph.
- Exactly 256 glyphs x 16 bytes = 4096 bytes for the bitmap bank.
- The source is the BSD-2-Clause Spleen 8x16 bitmap design, packaged as Astra
  Rescue Mono with its copyright and license retained.
- POST uses a dedicated validated `astra-rescue-mono.afnt` and generates its
  256-entry CP437 hardware image during the FPGA build. The cell contract
  is an 8-pixel advance, 12-pixel ascent, and 4-pixel descent; integer hardware
  enlargement scales the bitmap and every metric together.

The DE25 POST/panic renderer consumes those NDK metrics directly. Physical
release `00cb50981461fabb6b10ddc6a99b6b38a9cdc1b94df400f5cd4117aa635e2095`
measured exact 8-pixel advances across repeated narrow and wide glyphs; release
creation force-rebuilds the renderer so an older hard-coded advance cannot be
packaged again.

The BRAM face is intentionally small and monochrome. It is an emergency output
facility, not the normal desktop typography system.

### 2.2 System faces in the ROM image

The immutable system ROM image contains AFNT resources for:

- **Astra Sans**, derived from Atkinson Hyperlegible Next, as the proportional
  UI and reading face.
- **Astra Mono**, derived from JetBrains Mono, for code, terminals, tables, and
  diagnostics.

Both upstream families use the SIL Open Font License 1.1. Rasterized AFNT data,
the copyright notices, license, source revision, conversion command, and build
tool version ship together. The bundled faces use Astra names so the system is
not presenting converted builds as untouched upstream font binaries.

The first ROM set contains only measured, native-size strikes appropriate for
1920x1080. At minimum it provides regular UI, emphasized UI, and regular mono
text plus Basic Latin, Latin-1, common punctuation, arrows, box drawing, and a
replacement glyph. Exact strike sizes are selected from HDMI specimens on the
real panel, not from desktop preview alone. The ROM font payload has a separate
size report and may not displace recovery code.

These faces require neither the writable filesystem nor package services. The
font service can use their immutable SDRAM-backed ROM data directly when its
alignment is DMA-safe, or create a protected runtime cache once during boot.
Additional families and language coverage live in the normal filesystem.

Built-in faces are located through a versioned ROM resource directory, not
fixed linker addresses exposed to applications. Each directory entry carries a
type (`AFNT`), semantic role, ROM-relative offset, byte length, and CRC32. The
firmware and font service validate the directory against the ROM bounds before
publishing resident-face handles. The exact general ROM resource header is a
boot-format contract to freeze with its reader/writer tests; `AstraBootInfo`
already supplies the containing ROM base and size.

## 3. AFNT file model

AFNT is a data container, not a dump of C structures.

- Magic: `AFNT`.
- Explicit major/minor version and total file length.
- Fixed big-endian integer encoding with no native pointers or implicit
  structure padding.
- A bounds-checkable directory of typed, 4-byte-aligned chunks.
- CRC32 for the header/directory and each payload chunk.
- Unknown optional chunks are skipped; unknown required chunks reject the
  file.
- Every count, offset, multiplication, and range is validated before memory is
  allocated or data is exposed to DMA.

Initial chunk types:

| Chunk | Purpose |
|---|---|
| `NAME` | UTF-8 family, style, author, license, and provenance strings |
| `FACE` | weight, style, stretch, units-per-em, and face-wide defaults |
| `CMAP` | sorted Unicode scalar to glyph-ID mapping |
| `STRK` | x/y pixel size, ascent, descent, line gap, cap/x height, underline/strikeout metrics, format, and atlas references |
| `GLYP` | source rectangle, horizontal/vertical bearings and advances, and glyph flags |
| `KERN` | optional sorted glyph-pair adjustments for legacy/simple faces |
| `BASE` | optional script baseline coordinates for horizontal and vertical layout |
| `OTL ` | optional sanitized OpenType GDEF/GSUB/GPOS shaping data |
| `OUTL` | optional normalized OpenType outline source for host-side size generation |
| `PALT` | optional color palette and transparent entry |
| `BITM` | bitmap payload for one or more strikes |

Metrics use signed 26.6 fixed point. Character maps use 32-bit Unicode scalar
values and 32-bit glyph IDs rather than an 8-bit contiguous character range.
Each strike records horizontal and vertical pixel size separately. Its normal
horizontal baseline is `line_top + ascent`; this is derived state, not a second
stored coordinate that can disagree with ascent. A glyph's top-left ink origin
is placed from that baseline using its signed bearings. `BASE` supplies the
additional named script baselines needed to align Latin, ideographic, hanging,
and mathematical runs, and vertical glyph metrics provide the corresponding
vertical pen geometry.

Typographic ascent/descent/line gap determine default line spacing. Separate
ink/clipping bounds prevent accents and decorations from being clipped. Cap
height, x-height, maximum advances, underline position/thickness, and
strikeout position/thickness are preserved when the source provides them.
Missing optional legacy metrics are represented as unavailable, never guessed
into the file without provenance.

The primary 1920x1080 mode uses square pixels; independent metrics preserve
imported and future-mode fidelity without changing AFNT. OpenType glyph IDs are
preserved whenever shaping tables are retained, so shaped output addresses the
same bitmap records. A variable OpenType face is instantiated at explicit axis
coordinates for cached strikes while retaining those coordinates in
provenance.

AFNT 0.2 is the checked-in bitmap-strike format used by the graphics build. Its
`STRK` record carries pixel width/height, bitmap format, signed 26.6 ascent,
descent, line gap, cap height, x-height, maximum advance, underline and
strikeout positions/thicknesses, glyph range, record size, flags, and reserved
space. The shared writer, reader, malformed-file tests, and C-table emitter own
that layout together. Version 0 files remain an internal build format rather
than an application ABI.

## 4. Native bitmap formats

The AFNT container can describe more formats than a particular Astraea build
accelerates. The runtime reports capability bits; the font service selects a
supported strike or converts it once into a protected cache.

| Format | Required behavior |
|---|---|
| `MASK1` | One bit per pixel; select draw color or transparent/background |
| `A4` | Four-bit coverage; blend one draw color into RGB565 or XRGB8888 |
| `A8` | Eight-bit coverage; blend one draw color into RGB565 or XRGB8888 |
| `INDEX4` | Four-bit color index with a transparent index |
| `INDEX8` | Eight-bit color index with a transparent index |

`MASK1` is mandatory for rescue and baseline hardware. `A4` is the normal
compact anti-aliased UI-text path. `A8` is required by the DE25 graphics
contract for full-coverage text into direct-color surfaces. Implementations may
time-multiplex arithmetic only when measured command and scanout deadlines
remain satisfied.

For an `A4` coverage value `a` in 0..15, each RGB565 channel is computed in its
native 5- or 6-bit range as:

```text
out = (foreground * a + destination * (15 - a) + 7) / 15
```

Coverage 0 must leave the destination bit-exact and coverage 15 must write the
foreground bit-exact. Intermediate rounding follows the formula above in both
the software model and RTL.

`INDEX4` and `INDEX8` provide hardware color fonts with a palette cached for the
job. Binary transparency remains available. The Arty path also accepts palette
alpha and composes it using the graphics architecture's exact source-over rule.
A layered color glyph can also be represented as multiple positioned `MASK1`
runs with different colors without CPU rasterization.

Rows have explicit byte pitch. Packed bits/nibbles are most-significant first,
and unused tail bits are zero. Astraea accepts arbitrary source, palette,
destination, and pitch alignment while preserving neighboring bytes exactly;
the font service may still align caches when that improves throughput.

## 5. Runtime glyph-run contract

The font service validates AFNT and owns font faces, strikes, metrics, and
caches. The display service owns drawing surfaces and Astraea submission. An
application receives opaque handles, never font-file pointers or DMA addresses.

The path is:

```text
AFNT/imported font -> validated face/strike -> layout -> positioned glyph run
                   -> validated display job -> Astraea -> destination surface
```

A glyph job supplies one source strike and palette plus:

- destination surface base, pitch, format, and dimensions;
- mandatory clipping rectangle;
- draw color and transparent/opaque background policy;
- a bounded array of source rectangles and signed destination positions;
- completion fence/event and optional damage rectangle.

The low-level batch is an array of 16-byte big-endian
descriptors.
Each descriptor carries a source offset relative to the strike bitmap base,
unsigned source `(y,x)`, signed destination `(y,x)`, and unsigned
`(height,width)`. Astraea validates every descriptor before using it and keeps
the palette cached for the whole command. `ASTRAEA.md` is the MMIO and
descriptor contract; `GRAPHICS_ARCHITECTURE.md` defines the Arty
command boundary. Applications only see the protected draw-list API.

Layout is deliberately outside hardware. Complex-script shaping, fallback, and
bidirectional text evolve in software without changing RTL. Raster expansion,
clipping, palette lookup, coverage blend, and destination writes remain in
hardware.

The kernel/display service copies validated commands into owned DMA memory.
Astraea has no IOMMU and must never follow user-controlled addresses. Jobs are
bounded and yield between short bursts so text cannot starve scanout, audio, or
another client.

## 6. NDK object and class model

The AFNT disk representation, font-service objects, and public NDK structures
are separate contracts. An AFNT chunk record is never cast into an application
object or sent directly to hardware.

### 6.1 Object graph

| Object | Meaning | Mutability and ownership |
|---|---|---|
| `AstraFontFace` | Loaded family/style plus available strikes | Immutable process handle; shareable and transferable by rights |
| `AstraFont` | Resolved face, strike, size, style, and fallback chain | Immutable draw-ready instance |
| `AstraTextLayout` | Copied UTF-8, line breaks, metrics, and positioned runs | Immutable and reusable after creation |
| `AstraDrawList` | Ordered graphics commands targeting a surface | Mutable until submission, then sealed |
| `AstraFence` | Completion and error result for submitted work | Signaled once; waitable through the common event model |

Internally, a face owns validated metadata and strike records. A strike owns or
references glyph metrics, bitmap storage, and an optional palette. A layout
holds strong references to every font instance used by its fallback runs. A
submitted draw pins every referenced strike/cache until its fence signals.
Closing the application's face or font handle therefore cannot invalidate an
existing layout or an in-flight draw.

The font service may share immutable faces and strike caches between processes,
but process handles carry rights and accounting independently. Cache eviction
requires no live layout reference and no queued/in-flight fence reference.
Process death closes all handles; the service releases the corresponding strong
references without relying on application cleanup code.

The service stores each immutable layout as one offset-based arena rather than
a graph of allocator pointers:

```text
LayoutHeader
  text_offset, text_bytes
  line_offset, line_count
  run_offset, run_count
  glyph_offset, glyph_count

Line
  source byte range, run range, baseline, advance, logical/ink bounds

Run
  font-instance reference, glyph range, direction/script, rendering format

PositionedGlyph
  glyph_id, x/y (26.6), source cluster byte range, flags
```

All offsets and counts are 32-bit, relative to the start of the arena, checked
with overflow-safe arithmetic, and immutable after construction. This makes a
layout cheap to validate once, cache, serialize for service IPC, walk for hit
testing, and translate into bounded hardware runs. Glyph bitmap rectangles and
DMA addresses remain in the referenced strike cache, not duplicated into every
layout.

Layouts grow until allocation or the 32-bit arena representation is exhausted;
there is no smaller product-policy cap on bytes, lines, runs, or glyphs.
Editors may retain paragraph or viewport layouts to reduce reflow work, but
that is an optimization rather than an acceptance limit. Hardware glyph-run
records are emitted in consecutive commands when a layout exceeds the
per-command protocol field.

### 6.2 Public C shape

NDK resource objects remain one-handle wrappers, consistent with the rest of
`libastra`:

```c
typedef struct AstraFontFace   { AstraHandle _private_handle; } AstraFontFace;
typedef struct AstraFont       { AstraHandle _private_handle; } AstraFont;
typedef struct AstraTextLayout { AstraHandle _private_handle; } AstraTextLayout;

#define ASTRA_FONT_FACE_INIT   { ASTRA_INVALID_HANDLE }
#define ASTRA_FONT_INIT        { ASTRA_INVALID_HANDLE }
#define ASTRA_TEXT_LAYOUT_INIT { ASTRA_INVALID_HANDLE }
```

The public `astra/font.h` header supplies typed `ASTRA_AUTO_FONT_FACE`,
`ASTRA_AUTO_FONT`, and `ASTRA_AUTO_TEXT_LAYOUT` cleanup declarations. C++ can
later receive optional header-only, move-only RAII wrappers around these C
functions. No C++ object layout, exception, RTTI, allocator, or name-mangling
ABI crosses the library or service boundary.

### 6.3 Versioned value structures

Value structures start with `uint32_t size`, use fixed-width fields, and reserve
zeroed words for compatible growth. Layout dimensions and font metrics use
signed 26.6 fixed point; hardware glyph origins are deterministically rounded
to integer pixels only when a draw list is built.

The first API requires these value types:

- `AstraFontRequest`: pixel height/width, weight, style, match flags, language,
  and exact-versus-nearest strike policy;
- `AstraFontInfo`: family/style identity, provenance, capabilities, and selected
  strike/bitmap format;
- `AstraFontMetrics`: ascent, descent, line gap, cap/x height, maximum advance,
  underline position, and thickness;
- `AstraTextLayoutOptions`: maximum width/height, wrapping, alignment,
  direction, line spacing, tab policy, and overflow behavior;
- `AstraTextMetrics`: logical bounds, ink bounds, baseline, line/glyph counts,
  and consumed UTF-8 byte count;
- `AstraTextHit`: UTF-8 byte offset, line, leading/trailing edge, and caret
  position;
- `AstraTextPaint`: RGB color for mask/coverage glyphs and embedded-color policy.

The intended logical fields are:

```c
typedef int32_t AstraFixed26_6;

typedef struct AstraFontRequest {
    uint32_t size;
    uint32_t match_flags;
    AstraFixed26_6 pixel_height;
    AstraFixed26_6 pixel_width;       /* 0 = preserve strike aspect */
    uint16_t weight;                  /* 1..1000 */
    uint16_t stretch_percent;         /* 100 = normal */
    uint32_t style_flags;
    uint32_t reserved[5];
} AstraFontRequest;

typedef struct AstraFontMetrics {
    uint32_t size;
    AstraFixed26_6 ascent;
    AstraFixed26_6 descent;
    AstraFixed26_6 line_gap;
    AstraFixed26_6 cap_height;
    AstraFixed26_6 x_height;
    AstraFixed26_6 max_advance;
    AstraFixed26_6 underline_position;
    AstraFixed26_6 underline_thickness;
    uint32_t reserved[4];
} AstraFontMetrics;

typedef struct AstraTextLayoutOptions {
    uint32_t size;
    uint32_t flags;
    AstraFixed26_6 max_width;         /* 0 = unbounded */
    AstraFixed26_6 max_height;        /* 0 = unbounded */
    AstraFixed26_6 line_height;       /* 0 = font default */
    uint16_t alignment;
    uint16_t direction;
    uint16_t tab_columns;
    uint16_t reserved16;
    uint32_t reserved[5];
} AstraTextLayoutOptions;
```

These value layouts are proposed; `ndk/include/astra/font.h` does not exist yet.
It becomes the normative field and initializer contract when the native font
service lands. Family names, UTF-8,
and language tags are passed as explicit pointer + byte-count arguments to the
NDK library and marshalled into service messages; raw process pointers never
appear in the wire protocol. Optional style-span arrays remain a compatible
future extension rather than part of the initial layout call.

Language tags use the ASCII BCP 47 spelling envelope: nonempty alphanumeric
subtags separated by single hyphens. The NDK rejects malformed spans; the font
service decides whether a syntactically valid language or locale is supported.

Byte offsets, rather than code-point indices, identify positions in the source
UTF-8. APIs return boundaries only at validated scalar/grapheme positions. The
layout retains its own immutable copy of text and style spans, so callers may
release their source buffers immediately after successful creation.

Direction is a layout property, never a request to reverse source bytes or
Unicode scalars. The font service applies the Unicode bidirectional algorithm,
script shaping, directional punctuation mirroring, and line breaking before it
emits visual glyph runs. The immutable layout retains logical-to-visual and
visual-to-logical mappings so hit testing, caret movement, selection, and
clipboard extraction remain correct for right-to-left, left-to-right, and mixed
text. Clipboard text is always returned in logical UTF-8 order. Controls consume
this one mapping rather than implementing script-specific behavior.

Text direction and application-layout mirroring are separate properties. A
right-to-left field may live in a left-to-right interface, while an RTL locale
may mirror container flow and alignment without changing stored text. Both are
resolved when layout inputs change and cached; neither adds per-frame MC68040
work.

### 6.4 API responsibilities

The C API groups operations by object:

```text
astra_font_face_open_system / astra_font_face_open_family
astra_font_face_get_info / astra_font_face_get_string / astra_font_face_close
astra_font_create / astra_font_get_info / astra_font_get_string
astra_font_get_metrics / astra_font_close
astra_text_layout_create / astra_text_layout_measure
astra_text_layout_hit_test / astra_text_layout_get_caret
astra_text_layout_close
future draw_list_text / draw_list_submit -> fence
```

`font_face_open_system` uses stable semantic roles such as UI, UI emphasis,
mono, and rescue instead of hard-coding bundled family names in applications.
Users may replace desktop defaults without changing software; recovery code can
request the non-replaceable rescue role explicitly.

The initial role identifiers are `UI`, `UI_EMPHASIS`, `MONO`, and `RESCUE`.
The system font preference selects an installed face and its designed weight;
component roles retain their own pixel sizes from the shared theme. Menu text,
desktop labels, window titles, and controls need not share one size. Terminal
may choose its own face in its private application settings. The scoped
Config Kit ownership and failure rules are in [SETTINGS.md](SETTINGS.md).
For the draw-command boundary, reviewed Haiku
`src/servers/app/drawing/DrawingEngine.cpp` and
`src/servers/app/drawing/interface/remote/RemoteDrawingEngine.cpp`: clients
express drawing operations while a backend owns raster output. Astra adapts
that separation with Astraea as its production UI pixel writer.
Role matching returns an `AstraFontFace`; `AstraFontRequest` then resolves a
draw-ready strike-backed `AstraFont`. A returned match reports the actual strike
size and format so software never assumes that a bitmap was scaled.

Applications do not receive mutable glyph tables, atlas addresses, Astraea
descriptors, or a direct hardware queue. An advanced API may enumerate glyph
IDs and metrics for editors or font tools, but rendering still enters through a
validated draw list. A plain-text convenience function may create a temporary
layout, but retained UI text should use an explicit reusable layout.

The NDK 0.1 header and symbols must be published with ABI-size assertions,
argument-validation tests, automatic-cleanup tests, and a checked example.
The draw-ready object graph, live handle/stale-handle tests, and draw-list
bridge must land together before the service advertises drawable imported
faces. Font Kit 2.4 now exposes the separate installed-font catalog and
selection client API; its catalog view does not claim a drawable face handle.

TextSurface is a consumer of this object graph, not another font system. Grid,
code, and flow modes must take resolved fonts/layouts and returned metrics;
they must not select private resident arrays from a family name, requested
weight, or nominal size. The current resident grid renderer is the bootstrap
path only. Replacing it requires the OS font backend and a draw-list strike
reference to land together so applications never observe a false-success font
handle or an MC68040 bitmap-scaling fallback.

## 7. Import and style compatibility

The first Fonts tool import scope is native AFNT, static TrueType (`.ttf`),
OpenType (`.otf`), TrueType/OpenType collections (`.ttc`), variable TrueType and
OpenType at default and named-instance axis coordinates, and classic
monochrome Amiga disk fonts. Current native runtime supports AFNT, static
TTF/OTF/TTC, default plus distinct named instances from variable outlines,
and bounded monochrome Amiga disk-font conversion. Native service/QEMU import,
AFNT validation, catalog reload, and restart gate pass. Applications can draw
an installed face by catalog ID and exact strike size using
`astra_draw_font_text`; display resolves validated AFNT through its privileged
Fonts capability and submits glyph masks to Astraea. The disposable Amiga
probe draws through ordinary GUI capability without raw-font access. Physical
release gate remains open.

Fonts marks each installed style as able or unable to serve as the system
font. Catalog v3 derives `BASIC_TEXT` from exact CMAP entries for printable
ASCII (U+0020 through U+007E); the service also requires every fixed UI-role
size before advertising `SELECTABLE` or accepting a saved choice. A symbol
font remains installable for applications but cannot replace system text.
The shared AFNT exact-coverage query reuses its existing CMAP lookup and never
counts U+FFFD fallback as coverage. Haiku reference:
`~/Git/haiku/src/servers/app/ServerFont.cpp:GetHasGlyphs` distinguishes exact
coverage from optional fallback; Astra applies that distinction to its passive
AFNT data. Changing style/size selection updates existing Interface Kit
controls in place, preserving keyboard focus and scroll position, following
the retained-control pattern in Haiku's `FontSelectionView.cpp`.
Coverage/policy host and sanitizer tests and the full userspace suite pass on
Beast. The disposable native GUI gate also passes TTF import, family details,
keyboard style selection, saved system choice, restart, and uninstall with
catalog v3. These checks do not replace the open physical release gate.
Detail rows allocate from catalog style lengths with checked size arithmetic,
and place eligibility and size before the style name. A 127-byte valid style
previously imported successfully but exceeded the row buffer when its family
opened. `test-fonts-ui.py --long-style` now covers import, details, selection,
restart, and uninstall for that case; the gate failed before the fix and passes
afterward. Haiku's `FontSelectionView.cpp:UpdateFontsMenu` passes complete
style names to owned `BMenuItem` labels; Astra retains its List/AstraString
controls and sizes their backing storage from the validated catalog.

Display passes its selected UI font into the shared `CONTENT_SURFACE` render
builder, so ordinary Desktop/Atlas draw-list text inherits the same face as
window chrome. Named application faces still use the existing resolver. A
regression using the real Astra Mono AFNT failed before propagation: glyph
advances still came from the built-in proportional face. It now checks both
the selected advances and submitted bitmap rows, plus built-in recovery when
the selected font is absent. Display host, ASan/UBSan, MC68040 and analyzer
gates pass on Beast. The current-build native QEMU GUI gate also passes TTF
import, family details, saved choice, restart and uninstall. Haiku reference:
`src/servers/app/drawing/DrawingEngine.cpp:SetFont` applies the current
`ServerFont`/`DrawState` to its painter; Astra applies its validated AFNT context
to the existing hardware render builder. Live refresh, coherent client
measurement, and designed-style resolution remain open.

Compositor text layout now queries that same render context for width and
scalar-safe fitting. Window titles, system-bar title/menu-heading placement and
launch text use the existing renderer's strike/glyph resolution, including its
AFNT lookup, style selection and fallback. No second face parser or renderer
was added. A selected-mono `IIW` regression fails when measurement uses the
built-in proportional face; AFNT advances, UTF-8 prefix boundaries, replacement
glyphs, unsupported sizes and invalid style checks now pass. The compositor's
long launch-text test also checks clipping against the selected glyph advance.
Haiku reference: `src/kits/interface/Font.cpp:BFont::GetStringWidths` sends the
font's family, style and size with measurement, rather than measuring a separate
default font. Astra retains fixed UI-role sizes and measures through its
existing validated hardware command builder. This fixes compositor layout;
application-side Font Kit/Interface Kit measurement still needs the matching
selected context before live switching can be accepted.
Graphics and Display host/sanitizer, Display MC68040/analyzer, and the native
QEMU GUI import/detail/choice/restart/uninstall gates pass on Beast. This change
does not alter a public ABI or the FPGA protocol and is not yet qualified on
DE25 as a complete font release.

Font Kit 3.1 and GUI protocol 22 now close the native client measurement gap.
`astra_fonts_use_system` obtains the active immutable AFNT snapshot and its
catalog ID from Display, without a filesystem grant. Common width/fitting
calls read that snapshot, and surface-view UI text uses the existing named-face
command with its exact ID. `astra_fonts_use_snapshot` supplies the shared
adoption path for Display and advanced consumers. Updates happen on the UI
thread between frames; read/adopt operations use the existing native mutex.
Native Desktop, Atlas, Fonts, Gallery and Terminal initialize before layout.
Terminal's monospaced text remains independent. Popup menus resolve the same
named faces, and Display reuses its active area for current-face commands.

Haiku read-only references: `src/kits/interface/Font.cpp:_init_global_fonts_`
initializes process-wide plain/bold/fixed identities, and `GetStringWidths`
measures the specified family/style/size. Astra retains fixed theme role sizes,
shares validated passive AFNT bytes for local metrics and binds text to the
same ID. Pixel rendering remains in Astraea. Selected designed weight/slant
now stays in its family; resolving semantic emphasis to other installed family
styles remains unfinished. The API does not claim live refresh yet.

Built-in tables and lookup code moved unchanged to `graphics/src/font_data.c`.
This keeps HPS command-renderer helpers independent of native client locking,
area ownership and software surface drawing. Existing mutex tests own sleeping
lock behavior; a small shared host stub asserts that single-threaded graphics
tests never enter a futex wait. No new mutex mechanism was introduced.
Snapshot ownership, malformed replies, invalid AFNT/read-write mappings,
selected advances, UTF-8 fitting and named command checks pass, as do
Graphics/Display/Interface/Desktop host/sanitizer, full userspace `all test`,
MC68040/analyzer, NDK `dist-check`, HPS display self-test and AArch64 helper
links. The fresh coherent-ROM native GUI gate passes import, family details,
saved system choice, restart and uninstall. Physical release qualification,
live refresh and designed family-style resolution remain open.

Display now retains immutable named faces through its private `font_face`
module. Its registry has weak membership: the active default, delivered GUI
session contexts, retained window documents, attached graphics lists and popup
menus own references. Looking up an already retained ID does not reopen its
catalog source. A successful document replacement releases unused faces;
rejected replay drops only references acquired by that attempt. Closing a
window, graphics list, menu or session releases its references. A failed GUI
context reply leaves the previous session context intact. The last owner
closes the existing read-only area and removes the registry entry. No new
kernel lifetime mechanism or public font-object API was added.

Read-only Haiku references: `src/servers/app/font/FontManager.cpp:_RemoveFont`
delists a style separately from its lifetime, and `FontStyle.cpp:~FontStyle`
releases the face data at final destruction. Astra adapts that separation to
its shared AFNT areas. Hardware batches copy glyph masks during lowering;
their existing storage lifetime protects submitted bytes, so the FPGA never
retains an AFNT pointer. This ownership checkpoint prepares live switching;
selection notifications, client reflow and designed family-style resolution
remain open.
Host, sanitizer, MC68040/analyzer and native GUI
import/detail/choice/restart/uninstall checks pass for this ownership checkpoint.

GUI protocol 23 now distinguishes context delivery from adoption. Font Kit
includes its actually adopted face ID in each context request, read through
the same private mutex-protected state used by measurement and text commands.
Each Display session retains at most two snapshots: confirmed and pending.
The next request confirms the adopted one and retires the unused one before
preparing a new pending reply. A client that cannot map or validate a reply
reports its previous ID on retry; no confirmed reference is lost. Recovery's
empty ID follows the same rule. This needs no acknowledgement race, new
kernel mechanism or unbounded history. Session close releases both slots.
The old header-only request is removed; every GUI producer and consumer uses
the new protocol version. Host and sanitizer checks cover retry after failed
mapping/validation, stable reference counts, malformed IDs, old request/version
rejection, recovery adoption and final session cleanup. Public Font Kit ABI
stays 3.1; the changed wire contract is versioned by GUI protocol 23.
Contexts belong to their GUI session. Supervisor already retains an
application's `gui_session` in its process record until process retirement;
Font Kit does not duplicate a nondelegable GUI capability or invent another
lease. Owners of manually created sessions must keep them alive while using
their system context. The coherent GUI 23 ROM, full userspace build/tests,
NDK contract/docs/installed example checks and native GUI workflow pass on
Beast. GUI-header dependencies now also invalidate System window/graphics,
Supervisor loader-recovery and Display host tests; forced-header dry runs and
their host/sanitizer checks verify that boundary.

The live-update implementation advances Font Kit to ABI 4.0, GUI to protocol
24, window events to version 9 and the Fonts service to protocol 6. The old
one-argument `astra_fonts_use_system` and Font Kit 3 artifacts are superseded.
Initial setup passes a null event; refresh passes the delivered
`FONT_CHANGED` event. Its explicit window ID is carried in the context query,
validated against the GUI session and acknowledged independently. A shared
process context cannot acknowledge a sibling window whose controls have not
handled the notice. Confirmed/pending face ownership still follows the same
bounded adoption rule.

Fonts prepares a validated read-only selected snapshot before the Config Kit
write. Failed preparation or persistence retains the old choice. The service
keeps the selected snapshot and serves read-only duplicates without reopening
its file. Existing areas survive retirement of that selection. A native
auto-reset `FONTS_CHANGED` event is a wake hint; Display queries the catalog
for authoritative state. Refresh failures retain the previous rendering
context and retry. Window notices retry every 250 ms until the actual adopted
ID is reported for that window, including after queue backpressure or failed
client mapping. Startup recovery likewise retries the selected context.

Supervisor now grants a private GUI session to every GUI client. Applications
retain their named launch panel; services use unnamed sessions. Shared GUI
service contexts must not let Desktop and Terminal overwrite each other's
adoption state. Session-creation failure is reported rather than falling back
to the shared endpoint. Supervisor retains those sessions for process lifetime.

Clients fetch the context before passing a font-change event to Interface Kit.
The common retained-control handler reuses its saved layout, marks the entire
view damaged, and preserves focus/scroll state. Common modal alerts initialize
and refresh the same way. Desktop's existing safe-removal canvas reuses one
row-measurement path for initial layout and font changes, updating wrapping
and scroll extent without rereading privileged holder data. Monospaced
Terminal content remains independent. Read-only Haiku references:
`src/servers/app/DesktopSettings.cpp` broadcasts system-font changes,
`src/kits/interface/Window.cpp` dispatches `B_FONTS_UPDATED`, and
`src/kits/interface/View.cpp:_FontsUpdated` propagates to retained children.
Astra adapts this pattern to its versioned capabilities and common controls.

Selection failure/retirement host and sanitizer tests, window-event validation,
per-window acknowledgement/authentication and backpressure retry tests,
retained-control focus/scroll and modal-alert tests, MC68040 builds and Display
analyzer pass on Beast. Coherent build and strengthened native GUI live-update
qualification are in progress; no new font software is deployed to DE25 yet.

The first live GUI qualification exposed an area-ownership error: Astra's
identical area maps are idempotent, not independently reference counted.
Closing a previous duplicate mapping therefore invalidated the newly adopted
one. Client refresh now reuses an unchanged immutable ID without remapping;
direct snapshot adoption preserves a common mapping while closing the old
handle. Display's common-control metrics borrow its retained registry face's
validated view. Its active default owns that view until replacement, and
retained documents keep older registry mappings alive independently. No extra
AFNT copy or kernel mapping mechanism is introduced. The borrowed-context
API rejects replacing an owned snapshot, keeping ownership explicit.
Alias/borrowed retirement checks extend the existing real-AFNT host tests.
The corrected coherent build and native GUI gate pass: Desktop and Fonts
adopt the same imported ID live, Config Kit saves that exact ID, restart
restores it, and switching back to the built-in face permits uninstall.
Beast log: `/var/tmp/astra-font-live-map-ui-20261017.log`. Complete physical
qualification remains open.
The final full userspace test suite and refreshed NDK distribution with
installed example links and HTML/PDF manuals also pass on Beast in
`/var/tmp/astra-font-live-map-sdk-final-20261017.log`.

Retained reflow now refreshes intrinsic measurements as well as positions.
Initialization and font changes share one measurement pass, followed by the
existing layout. A wider-font regression fails before the correction and
passes afterward, including scrolled List focus/offset preservation. This
adapts Haiku's `src/kits/interface/View.cpp:InvalidateLayout` invalidation of
cached size constraints and child layouts. Interface/Display host, sanitizer,
MC68040 and coherent build gates pass; the native Iosevka Regular import,
live Desktop/Fonts adoption, Config Kit persistence, restart and uninstall gate
passes in `/var/tmp/astra-font-live-remeasure-iosevka-ui-20261017.log`.
The earlier clean release is withheld pending replacement with this correction
and complete physical qualification.
Full userspace tests and updated NDK distribution/examples pass in
`/var/tmp/astra-font-live-remeasure-sdk-20261017.log`.

Physical Iosevka qualification found two further stale-pixel paths despite
successful client adoption: Desktop submitted redraw commands without asking
for presentation, and Display retained its menu-bar cache after font changes
when no popup was open. Desktop now uses the existing posted-frame presentation
contract for every paint; its initial synchronous presentation still supplies
the startup visibility barrier. Display invalidates the system-surface cache
alongside its window caches. The existing content-bank copy-forward mechanism
preserves pixels across consecutive presentations. No new buffering path is
introduced. Haiku's read-only references are
`src/servers/app/Desktop.cpp:RebuildAndRedrawAfterWindowChange` (dirty pixels
must reach screen redraw) and the font-change propagation references above.
The real-AFNT cache regression fails before invalidation and passes afterward.
Display/Desktop host and sanitizer gates, MC68040 builds, Display/Fonts
analyzers and coherent userspace/ROM pass in
`/var/tmp/astra-font-live-present-green-20261017.log` on Beast.
The native import test now waits for a fresh successful UI import completion
instead of sleeping eight seconds; a retained-log regression covers readiness.
The native Iosevka GUI gate passes import, live Desktop/Fonts adoption,
persisted choice, restart, built-in restoration and uninstall in
`/var/tmp/astra-font-live-present-ui-20261017.log`. Physical replacement
qualification remains required.

The corrected `c6cd2e7` DE25 candidate now visibly switches Desktop labels,
menu headings and Fonts to Iosevka without restart. The ineligible Astra Mono
detail view shows "Cannot be system font", a requirements hint and a disabled
selection button. The first restoration interaction timed out without changing
the saved Iosevka choice. An unchanged-runtime diagnostic replay passes built-in
restoration and uninstall, with correct pixels throughout, empty inventory,
no imported AFNT object, no trial panic and no retained provider. Its stopped
production image hashes match before/after. Evidence on Beast:
`/var/tmp/astra-font-live-evidence-20261017b/`; the failed first restoration is
retained under `restore-attempt1/`. Release remains withheld until that
interaction failure is explained or resolved. Passing replay alone does not
establish repeatable acceptance.

Font previews now extend the shared Label control with an explicit installed
face and size. Ordinary system labels retain their fixed nominal role sizes;
their line boxes use actual ascent/descent/gap metrics. The Fonts detail view
composes that Label with its existing List and ScrollView. Changing a row
updates the preview font in place, preserving List focus and scroll state.
Face and strike indices share one detail-source record rather than parallel
mapping arrays. No preferences-only text renderer is introduced.

Font Kit 4.1 / GUI 25 add bounded installed-face measurement through Display's
existing immutable font registry and renderer metric loop. UTF-8 spans use
256-byte chunks without splitting scalars; fractional advances remain 26.6
across chunks and round only at the final client boundary. Reply validation
rejects malformed prefixes, out-of-range advances, inconsistent line heights
and unexpected handles. Measurement never changes the active system context
or gives clients raw AFNT/glyph memory. Named-face surface text reuses the
existing ADLT emitter and refuses CPU pixel views. A missing strike returns an
error rather than drawing another face. The retained draw document owns its
existing font references independently of each metric query.

Interface Kit 7 replaces the control ABI coherently across all in-tree clients,
manifests, exports and NDK examples. Label text/font updates share one measured
replacement and layout-rollback path. Draw-list budgets account for each text
command's actual font-ID prefix. The existing context-ID reader moved to the
public Font Kit header; its private header/API spelling was removed. An initial
overly conservative prefix estimate rejected a valid dense menu; that failed
check remains in `/var/tmp/astra-font-label-preview-focused-20261017.log`.
Corrected focused host, ASan/UBSan, MC68040 and analyzer gates pass in
`/var/tmp/astra-font-label-preview-focused-final-20261017.log` on Beast.
Tests cover UTF-8/fractional chunking, real AFNT advances, absent strikes,
preview/default Label layout, failure rollback, named submission, unchanged
system context and capacity rejection before drawing. The first metrics draft
also passes native Amiga import/measurement/hardware draw/restart in
`/var/tmp/astra-font-independent-metrics-amiga-20261017.log`; current coherent
UI gates now pass Iosevka import/live choice/restoration/removal, long AFNT
styles, and native Amiga import/exact measurement/hardware draw/restart in
`/var/tmp/astra-font-label-preview-{iosevka-ui,long-style-ui,amiga-native}-20261017.log`.
Full userspace/ROM and NDK distribution gates pass in
`/var/tmp/astra-font-notice-focus-green-20261017.log`. Complete physical preview
qualification remains pending.

A valid AFNT with an unsupported bitmap format remains manageable: Fonts
shows an explicit unavailable caption using its ordinary Label, rather than
drawing a misleading sample in another font or exiting. Selection and initial
detail layout share this same update path. The native regression fails before
the fix in `/var/tmp/astra-font-preview-unsupported-red-20261017.log` and passes
in `/var/tmp/astra-font-preview-unsupported-final-20261017.log`. The first fixed
run successfully removed the font but exposed a test assertion distinguishing
absent from empty inventory; the final gate normalizes those equivalent states.

Read-only Haiku references: `src/preferences/appearance/FontSelectionView.cpp`
uses a standard text control with a chosen font; `src/kits/interface/Font.cpp`
obtains string metrics from app_server; `src/kits/interface/StringView.cpp`
invalidates preferred size/layout on font changes and measures font height.
Astra adapts these patterns to counted IDs, exact AFNT strikes, native
capabilities and hardware draw lists.

The `4a0925b7` clean release `77115da1` passes its first DE25 import/live
choice/restart/restoration/removal workflow, but remains withheld. Visual
inspection finds action buttons clipped below the default window. Captures
nominally taken before selecting Iosevka still show the family list: fixed
delays stopped the guest before detail initialization finished. Failed evidence
is retained and checksum-compared at
`/var/tmp/astra-font-preview-evidence-20261017/cycle1/` on Beast. The real
Fonts controller linked to common controls reproduces the layout failure in
`/var/tmp/astra-font-preview-layout-red-20261017.log`. The existing ScrollView
now starts at its minimum viewport height and uses existing flex growth for
remaining room; its width follows the current window. No layout/control
mechanism changed. Host/sanitizer checks cover family, eligible/ineligible
details, another strike, unavailable previews, confirmation and source browsing,
including a narrower, taller window. Native UI gates wait for fresh details
and preview completion notices and use the visible last button for removal.
Read-only Haiku reference: `FontSelectionView.cpp` composes a measured preview
with standard layout space/glue; Astra similarly leaves remaining room to its
existing flexible viewport. Corrected native and physical gates remain pending.
Final focused host/sanitizer/MC68040 analyzer and readiness checks pass in
`/var/tmp/astra-font-preview-layout-final-focused-20261017.log`. Presentation
completion notices share the existing application presentation function and
emit only after success, including confirmation and removal; observers no
longer assume a fixed removal delay. Freshness tests cover all five notices.

Corrected source `616129a2` clean-builds as release `c06c1a34`, ROM
`d48a988f65445011b145502a9cb28f4f784bb7b43a8362214f2f485b44c52529`.
Final current-release native Iosevka, unsupported-preview, long-style and
readiness gates all pass in the `astra-font-preview-final-*-20261017.log`
files on Beast. The first corrected physical cycle passes as well. Inspected
1920x1080 HDMI captures prove 11- and 13-pixel Iosevka previews before changing
the system face, all action buttons visible, live system adoption, persistence
after restart, built-in restoration and removal. Complete checksum-compared
evidence is `/var/tmp/astra-font-preview-evidence-20261017-layout/cycle1/`.
Production image SHA-256 remains
`96fcad0293f7994584fe19cd9e6a6a4d4f6265a9ba67934483b2d063630163c4`
before/after; services restore active with zero automatic restarts. A second
full disposable-image cycle passes with production image before/after SHA-256
`b18658e0174cd11220d96c779089a69d6fbdf38d471a0bcc622d94e6ccb39e4e`;
its evidence is under `cycle2/` in the same Beast root. All eight corresponding
HDMI captures are byte-identical between cycles. The final warm production
frame again matches
`37d1bbfe3df5fbb5b0c3a8d6b278724f1222aefd572bd6e52cd3aa1534b3b091`,
at 25 glyph commands and 49/49 submissions/completions, with all three services
active and zero automatic restarts. Shared Label preview and repeated
restoration/removal correctness pass; this does not close the remaining Fonts
program or release gates. Production remains on `57078599`.

The restoration investigation also reproduces a real notification defect:
backpressured `FONT_CHANGED` delivery used the generic lossy-event path even
though the generation remains pending and is retried. A later event therefore
reset the common controls' focus and cancelled button activation. Font notices
now reuse `prepare_event`/`deliver_event` without declaring a retry to be lost
input. A genuinely recorded earlier loss is still delivered and resets focus.
The Display host fixture dispatches the retried notice through real Interface
Kit controls, proving that focus and Return activation survive; it also checks
genuine-loss recovery. The old code fails the focus assertion in
`/var/tmp/astra-font-notice-focus-red-linked-20261017.log`. Initial attempts
needed the public Interface header and its existing TextModel/Scroll sources
to link the real event handler; their compiler/linker failures remain in the
earlier `-red`/`-red-behavior` logs. No fake UI policy was introduced.
Read-only Haiku references: `src/servers/app/ServerWindow.cpp` forwards
`B_FONTS_UPDATED` as a state notice; `RequestRedraw` retains its state counter
independently of notification queue pressure. Corrected host/sanitizer gates
pass in `/var/tmp/astra-font-notice-focus-green-20261017.log`; physical
qualification remains required before claiming the restoration gate closed.

The shared renderer also makes AFNT a dependency of HPS display helpers.
`fpga/arty/linux/Makefile` now includes the existing AFNT, CRC32, and UTF-8
sources for every renderer consumer. Host and AArch64 display self-tests and
the AArch64 host-read helper link pass; this repairs the clean publisher's
previous unresolved `astra_afnt_*` references without a second font reader.

This covers the common current macOS outline containers listed by
[Apple Font Book](https://support.apple.com/en-gb/guide/font-book/fntbk1000/mac)
and Amiga bitmap fonts. A collection is one input but contributes all its
faces to the managed
installation transaction. Every installed face becomes validated AFNT before
the catalog exposes it; applications and Astraea never parse source formats.
Color glyph formats, including OpenType-SVG, need a separate color-strike
conversion path. The monochrome A8 outline importer rejects them rather than
silently dropping color. Legacy Mac font suitcases, resource-fork `.dfont`,
PostScript Type 1, WOFF/WOFF2, BDF, and PCF are outside this first scope.

The target installation architecture uses one AFNT conversion and validation
path. Import is an installation/build operation, not an application rendering
path:

- TTF, OTF, and TTC enter through the outline rasterizer and OpenType parser;
- classic Amiga disk fonts enter through the bounded importer below;
- every import records source hashes, source format, face/collection index,
  variation axes, selected strike sizes, converter versions, license, and
  source character mapping.

The build-time development tool supports one TTF, OTF, or selected TTC face per
invocation, plus monochrome Amiga disk strikes. The native HPS worker converts
one outline face per request; the resident Fonts service streams every
collection face and distinct named variable instance through that worker and
commits one validated inventory update. It skips named instances whose
coordinates duplicate the default or an earlier instance and records exact
16.16 design-axis coordinates and source instance index in AFNT metadata.
Custom axis coordinates are not yet selectable in Fonts. The current limit is 32 MiB
per source and per resulting AFNT face; larger valid faces return a capacity
error without publishing a partial catalog.

`sw/common/Makefile` builds host, MC68040, and AArch64 `libastraafnt.a`
variants for shared C validation and canonical packing of AFNT 0.2
at the native import boundary; host and sanitizer tests run it against shipped
faces, Iosevka and Amiga spike output, and malformed records. The HPS C
outline converter in `fpga/de25/linux/astra_font_convert.c` reuses that codec,
FreeType, UTF-8 validation, CRC32 and SHA-256; static Iosevka TTF and Source
Sans OTF conversion tests pass. Build-time Python remains a development asset
pipeline and oracle. It is not an Astra user-facing importer or a runtime
dependency. Users import through the native Fonts UI and resident service.

The first internal HPS worker executable now wraps that converter with a
versioned, bounded `FNTC`/`FNTR` byte protocol. One child handles one face and
exits; requests carry source bytes, display name, license, and collection face
index, never a host path. The worker only returns validated AFNT bytes and the
collection face count. Beast host, ASan/UBSan, AArch64 QEMU-user, and physical
DE25 conversion of Atkinson Hyperlegible Next Regular pass, including malformed
outline and oversized-request checks. The physical test ran from
`/var/tmp/astra-font-worker-20261017` without installing or selecting a new
production release. The QEMU host transport now invokes this worker for native
Fonts imports. Disposable QEMU gates pass TTF, OTF, two-face TTC, and four-style
STIX Two Text variable conversion,
catalog publication, system selection and restart persistence; the AFNT import
and damaged-inventory gate still passes. A later-face TTC failure rolls back
newly created AFNT objects before the inventory commit; disposable QEMU checks
the unchanged catalog and empty installed-object directory. If Config Kit
reports a failed commit, objects remain because its durable outcome may be
ambiguous. The native UI uses its existing file
browser and Import action. A fresh disposable QEMU gate now drives the desktop
icon, imports a TTF through that browser, checks the AFNT object and Config Kit
inventory, opens family details, and saves that imported face as system choice.
The saved `user:<token>` matches the published AFNT object. The gate found a Flex
break flag that moved the List outside the client area; the flag was removed.
A complete DE25 release has not been qualified. Display loads the saved
system-face choice at startup; live change notification and render-context
refresh remain open.
The variable fixture is unmodified STIX Two Text 2.13 under OFL 1.1. Host and
AArch64 worker tests verify default/Medium/SemiBold/Bold conversion, reject a
fifth face, and check source-instance provenance. ASan/UBSan and native QEMU
import/restart gates pass. Haiku's
`~/Git/haiku/src/servers/app/font/FontManager.cpp:306-352` keeps designed
styles within a family and advances catalog revision; Astra applies that catalog
pattern after converting FreeType named instances into immutable AFNT faces.

The shared C codec now exposes validated AFNT family/style strings and strike
metrics for the future service catalog and Fonts detail view. Its NAME reader
decodes JSON escapes, rejects malformed Unicode and duplicate requested fields,
and leaves source buffers owned by the caller. It also resolves Unicode scalars
to borrowed glyph rasters and metrics in validated strikes, falling back to
U+FFFD without a second file parser. Beast host, sanitizer, MC68040, and
AArch64 codec builds pass. The HPS C converter now prepares
11, 13, 16, and 20-pixel A8 strikes, including every current proportional UI
role size from `astra/theme.h`. Beast host, sanitizer, MC68040, AArch64, and
outline-conversion checks pass. This does not install fonts or change the active
system font.

A bounded, versioned native catalog snapshot now serializes face IDs, family,
designed style, and available strikes. Shared C code extracts these fields from
validated AFNT into caller-owned storage; shipped AFNT round trips through the
catalog on Beast under host and sanitizer tests, and the codec builds for
MC68040 and AArch64. A resident `fonts` service draft scans immutable system
AFNT and its protected `CONFIG_SYSTEM/installed` directory, keeps read and
administrative request ports separate, and validates theme role sizes before
persisting a choice through Config Kit. Its service/policy build and focused
tests pass. An isolated disposable QEMU boot launched the service with status
zero. Font Kit 2.4 now opens a validated, immutable catalog snapshot through
the FONTS capability, exposes counted family/style names and strike sizes,
and sends selection through the separate FONTS_ADMIN capability. MC68040
library/link contracts and host malformed-reply/catalog tests pass. This is
client infrastructure, not a Fonts application or live typography change.
The disposable display image now starts the service and installs a native
Fonts bundle. The first UI reads catalog snapshots, groups installed faces by
family, and opens a style/bitmap-size detail view through the shared List
control. A disposable QEMU autostart confirms bundle launch and hardware glyph
submissions; a focused GUI gate covers family-detail interaction.
The draft installed-font store now assigns every AFNT object its full SHA-256
digest in canonical base32. Config Kit's single `installed` string lists those
tokens, so the multi-face TTC importer publishes one complete inventory
with one atomic settings commit after writing its objects. Service startup
accepts only referenced objects whose bytes match their names and whose AFNT
metadata validates. It bounds each read to 32 MiB. A damaged or unavailable
user inventory remains on disk and falls back to built-in fonts without
blocking system startup. The native Fonts UI can select AFNT, TTF, OTF, or TTC
files from DH0. The admin-only service validates converted AFNT, writes and syncs a
content-addressed object, commits its token through Config Kit, and publishes
a new catalog generation. Duplicate imports return the existing ID; loose
files in the protected directory are not installed. The UI also sends a
system-face choice through the admin port, which persists in Config Kit.
MC68040 builds, host policy/client tests, full userspace tests, static analysis,
and a disposable QEMU Fonts autostart pass. A test-only native Astra probe
passes AFNT import, catalog publication, duplicate-idempotence, selected-face
save, and catalog/choice reload after reboot on a disposable QEMU image. A
fourth boot with a valid Config Kit document containing a damaged installed-font
inventory and unavailable saved choice confirms built-in fallback and refusal
to overwrite the damaged inventory. It found and fixed an import port-call
handle-order bug. A two-boot GUI gate imports a TTF, selects it, selects a
shipped face again, confirms uninstall, and checks that the catalog and AFNT
object are removed. A native probe checks active and shipped-face rejection,
retained-face catalog integrity after compaction, and restart persistence.
Physical DE25 qualification remains open.
Font service protocol 4 publishes `FONTS_RENDER` separately from catalog and
administration. It rereads and validates an installed AFNT, checks the full
content token for user faces, and transfers an immutable area. Font Kit 2.7
maps that area read-only. A native QEMU probe compares delivered bytes with
the built-in and newly imported source, including after restart and damaged
inventory fallback. Normal startup grants this port only to display. Display
now opens the selected AFNT area at startup and feeds its validated view into
the shared render builder. Available regular proportional strikes become
Astraea glyph-run commands; the host pixel-oracle checks exact AFNT bitmaps
and positions. The disposable QEMU Fonts autostart gate passes with current
services installed and submits hardware glyph commands. Missing strikes and
styled requests still use the built-in UI face, while monospaced text keeps its
independent font. The current-ROM native AFNT import probe also passes
selection, restart, and damaged-inventory fallback. Display does not yet watch
for selection changes, and clients still lay out text with built-in
metrics. Static TTF/OTF/TTC now enter through the HPS worker and native Fonts
service; a disposable two-face TTC import/restart gate passes. Imported faces
can be removed only after choosing another system face. Config Kit commits the
inventory change before the service retires the catalog entry and unlinks its
AFNT object. If unlink fails, the unlisted object remains on disk and the
service logs the cleanup error; it does not restore a removed catalog entry.
This follows Haiku's delist-and-revision pattern in
`~/Git/haiku/src/servers/app/font/AppFontManager.cpp:101-106` and
`~/Git/haiku/src/servers/app/font/FontManager.cpp:354-370`, adapted to Astra's
content-addressed store and Config Kit transaction. Production DE25 still
uses the previous selected release. Live updates, coherent metrics and
styles, Amiga runtime import, removable-volume browsing, and physical gates
remain open.

Haiku reference for this catalog boundary: `src/servers/app/font/FontManager.cpp`
groups families and styles, `src/servers/app/font/FontFamily.cpp` keeps designed
styles distinct, `src/servers/app/font/GlobalFontManager.cpp` owns installed
font discovery and default fallback, and `src/preferences/appearance/FontView.cpp`
keeps user preference controls separate from inventory. Astra follows that
ownership split while deriving its user-visible strike list from validated
AFNT rather than exposing source-format records. Also reviewed
`src/servers/app/drawing/DrawingEngine.cpp`: Haiku resolves the font centrally
and passes it to its drawing engine; Astra passes the selected AFNT view to
its shared Astraea command builder.

Beast format checks on 2026-10-15: Adobe Source Sans 3 static OTF validated
with 1,615 cmap entries; its variable TTF and OTF each validated with 1,616
entries and recorded default `wght=200`; a two-face test TTC imported its
second face; AROS ISOHelvetica 11- and 13-pixel Amiga strikes validated with
225 cmap entries and two strikes. The official Adobe sources and results are
isolated in `/home/barry/astra-font-spike-20261015/` on Beast. These prove
build-time conversion, not installed-font runtime behavior.

Importers render requested MASK1/A4/A8 or indexed strikes, then feed the same
AFNT writer and validator. Common UI sizes can ship prebuilt. For a scalable
face, an absent size is generated by the host worker and cached by source hash,
face index, axes, size, hinting mode, bitmap format, and synthesis parameters.
Applications still open an AFNT face and never know which source format was
installed.

Designed weight and italic/oblique faces are always preferred. If the caller
allows synthesis and no designed face exists, the font service may create a
cached emboldened or slanted strike from the installed face; packages do not
carry duplicate glyph sets for synthetic styles. Underline and strikeout are
drawn as decorations from the strike metrics and never alter or duplicate
glyph bitmaps. The resolved `AstraFontInfo` reports synthetic weight/slant so a
typographically exact application may refuse it.

The current `font.library` 2.3 draw-list path prepares built-in synthetic bold,
italic, and combined strikes from resident AFNT during the build. Bold uses a
deterministic horizontal embolden, italic a baseline-relative shear, and
underline/strikeout use AFNT metrics. Runtime selects a prepared strike and
submits glyph descriptors; Astraea expands glyph coverage. Installed scalable
faces still need the font-service cache before advertising synthetic matches
through `AstraFontInfo`.

### 7.1 Settings drag-import transaction

The Fonts pane in Settings is the system installation surface. A user may drag
one or more font resources into it from Atlas, a local volume, removable media,
or an SMB/NFS mount. The drop transfers read handles plus typed resource
metadata; a pathname is descriptive and is not the authority used for import.

One drop is one all-or-nothing transaction:

1. identify each input by bounded content inspection rather than its suffix;
2. expand collections into their individual faces and report the complete
   candidate list in a progress sheet;
3. run the selected format handler in an unprivileged, restartable Astra C
   worker on the DE25 HPS, reached only through the versioned native font
   service protocol;
4. convert every requested native strike and retain complete source provenance;
5. validate every AFNT, name/style collision, bitmap range, and catalog entry;
6. durably store immutable content-addressed font objects; and
7. atomically publish one new catalog generation only after every item passes.

Progress and actionable errors are reported per source and face while the seat,
window server, and settings UI remain responsive. Cancellation before catalog
publication discards the transaction. A failed source, worker crash, lost
network mount, storage error, or duplicate-resolution rejection publishes
nothing from the batch. Durable objects left by interruption are unreachable
and may be collected later; they are never mistaken for installed faces.

The catalog is the single commit point because a sequence of file renames
cannot make a multi-font batch atomic. Existing `AstraFontFace` and
`AstraFont` handles keep strong references to their old immutable objects;
new opens observe the new generation. Importing never silently changes UI,
title, terminal, table, or code roles. Those assignments remain separate
Settings transactions.

The Settings pane and reusable Fonts system panel are clients of the same font
service transaction API. They do not parse formats, write AFNT files, or edit a
private font registry. AFNT dropped directly follows the same full validator
and transaction path without lossy reconversion.

The build-time outline importer converts Unicode cmaps from TTF, OTF, and a
selected TTC face into A8 AFNT strikes using pinned fontTools and Pillow
versions. It is the behavioral oracle for the service writer; wiring the host
worker, all-face collection expansion, progress protocol, and catalog
publication is required before Settings advertises drag installation.

### 7.2 Amiga import compatibility

Amiga fonts are useful content, not the Astra ABI. The production importer
must accept classic bitmap disk fonts without executing them and convert:

- the strike bitmap and per-character location/width table;
- proportional spacing and kerning arrays when present;
- baseline, nominal size, and style flags;
- `ColorTextFont` bitplanes and palette into an AFNT indexed-color strike.

Classic files are relocatable 68k load modules containing a `DiskFontHeader`
and pointer-based `TextFont` or `ColorTextFont` data. The production importer
therefore needs a bounded hunk parser in a sandboxed user process. It must not call
`LoadSeg`, run embedded return code, trust relocation addresses, or pass planar
data to Astraea.

The build-time `import-amiga` uses pinned monobit 0.53 as a development oracle.
Native HPS C code validates one monochrome HUNK_CODE/HUNK_DATA disk font without
executing or relocating it, converts its fixed bitmap strike into MASK1 AFNT,
and validates the packed AFNT. The checked-in AROS ISOHelvetica 11- and
13-pixel fixtures pass bounded-parser, conversion, sanitizer, host-worker, and
AArch64 worker checks. Color Amiga conversion remains pending; the first Fonts
release accepts monochrome classic disk fonts only. The user-facing Fonts
importer requires an explicit ISO-8859-1 or ISO-8859-2 choice for these files.

Imported glyphs receive Unicode mappings through that explicit choice. Unknown
encodings do not silently become Unicode. System-choice eligibility requires
11-, 13-, and 16-pixel strikes; fixed-size Amiga fonts remain installed and
catalog-visible when they lack those sizes. The catalog carries the service's
eligibility decision, and Fonts labels each style accordingly. Applications
can use `astra_draw_font_text` to draw an imported Amiga face independently
of the system face through display's validated resolver and Astraea.

Read-only implementation references: Haiku
`src/servers/app/font/FontManager.cpp` for catalog/style ownership and
`src/preferences/appearance/FontView.cpp` for preference presentation; Amiga
NDK `graphics/text.h` and `libraries/diskfont.h` for source metrics and bit
flags; monobit 0.53 `storage/fontformats/amiga.py` for disk-font layout.
Astra retains its AFNT validator and catalog as the native contracts.

Host and native tooling should share the same AFNT writer and golden files.
Amiga import is tested with monochrome, proportional, kerning, missing-glyph,
and color-font fixtures.

## 8. Hardware status and resource gate

The glyph path uses the Astraea draw engine and exact SDRAM pixel port documented
in `ASTRAEA.md`. The first implementation now provides:

1. `MASK1` expansion with clipping and transparent/opaque background.
2. Batched glyph runs and completion fences.
3. `A4` RGB565 coverage blend.
4. `INDEX4`/`INDEX8` palette expansion with binary transparency.

The complete geometry, glyph, and bounded flood engine currently synthesizes
standalone to 4,722 LUT4, 1,243 CCU2C, 3 MULT18X18D, 2 DP16KD, and 3,390
flip-flops. Counting two LUT primitives per carry cell gives a 7,208-primitive
draw budget, so it crosses the original 4,000-LUT review threshold; that is a
measured scope decision, not a waiver. The second block RAM is an exact,
registered A4 quotient table that replaces a timing-failing combinational
divider in the 60 MHz domain. Final acceptance depends on integrated
place-and-route, exact retained headroom for the remaining chipset, shared-model
tests, and real HDMI output. The A4 datapath time-multiplexes channels; the
additional multipliers also serve dynamic pitch/address arithmetic rather than
three parallel coverage channels.

## 9. Acceptance

- Directed RTL tests cover every source/destination format, clipping edges,
  pitch/alignment combinations, transparent modes, palette lookup, malformed
  runs, exact A4 pixels, and fences. Shared software-model differential tests
  remain a release gate.
- Pixel output is compared exactly for `MASK1` and indexed formats and against
  the specified integer blend rule for `A4`.
- ROM fonts render identically in the emulator and RTL at every bundled strike.
- The rescue font remains readable during forced SDRAM and SD failure tests.
- Real-HDMI specimens verify ambiguous pairs, punctuation, code, dense UI text,
  and the 8:9 pixel aspect before a bundled strike is frozen.
- Font parsing and Amiga import fuzz tests run without FPGA hardware.

## 10. Reference provenance

- Amiga Developer CD, "How an Amiga Font Is Structured in Memory":
  <https://amigadev.elowar.com/read/ADCD_2.1/Libraries_Manual_guide/node03DE.html>
- Amiga Developer CD, "But What About Color Fonts?":
  <https://amigadev.elowar.com/read/ADCD_2.1/Libraries_Manual_guide/node03DF.html>
- Amiga Developer CD, "Composition of a Bitmap Font on Disk":
  <https://amigadev.elowar.com/read/ADCD_2.1/Libraries_Manual_guide/node03E0.html>
- Spleen bitmap fonts: <https://github.com/fcambus/spleen>
- Atkinson Hyperlegible Next:
  <https://github.com/googlefonts/atkinson-hyperlegible-next>
- JetBrains Mono: <https://github.com/JetBrains/JetBrainsMono>
