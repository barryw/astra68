# Astra Fonts and Hardware Glyph Rendering (draft v0.3)

> AFNT remains the native hardware-ready font contract. The DE25 graphics
> target provides A8 coverage and XRGB8888 destinations as required by
> [`GRAPHICS_ARCHITECTURE.md`](GRAPHICS_ARCHITECTURE.md).

This document defines the native font direction for Astra 68 and the boundary
between font files, the font/display services, and Astraea. The graphical OS
must not draw bitmap glyphs with CPU pixel loops.

## 1. Locked architecture

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
