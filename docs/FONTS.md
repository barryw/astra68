# Astra Fonts and Hardware Glyph Rendering (draft v0.2)

> AFNT remains the native hardware-ready font contract. The Arty graphics
> target adds A8 coverage and XRGB8888 destinations as required by
> [`GRAPHICS_ARCHITECTURE.md`](GRAPHICS_ARCHITECTURE.md); existing
> INDEX8/RGB565 Astraea details below describe the implemented ULX3S path until
> the register-level Arty revision is written.

This document defines the native font direction for Astra 68 and the boundary
between font files, the font/display services, and Astraea. The graphical OS
must not draw bitmap glyphs with CPU pixel loops.

## 1. Locked architecture

- The native font container is **AFNT** (Astra Font), a passive, versioned,
  big-endian bitmap-font format.
- AFNT contains one or more designed bitmap strikes. Runtime scaling is not
  required for the initial OS.
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
expensive per-pixel work from the 68030.

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
- The planned source is the BSD-2-Clause Spleen 8x16 bitmap design, packaged as
  Astra Rescue Mono with its copyright and license retained.
- The existing 8x8-doubled POST font remains the implementation until the new
  bank has visual and hardware acceptance.

The BRAM face is intentionally small and monochrome. It is an emergency output
facility, not the normal desktop typography system.

### 2.2 System faces in the ROM image

The immutable system ROM image contains AFNT resources for:

- **Astra Sans**, derived from Atkinson Hyperlegible Next, as the proportional
  UI and reading face.
- **Astra Mono**, initially the complete Spleen 8x16 bitmap repertoire, for
  code, terminals, tables, and diagnostics. Atkinson Hyperlegible Mono remains
  the planned coverage-font family when native A4 strikes land.

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
| `FACE` | weight, style, stretch, and face-wide defaults |
| `CMAP` | sorted Unicode scalar to glyph-ID mapping |
| `STRK` | pixel size, ascent, descent, line gap, format, and atlas references |
| `GLYP` | source rectangle, bearings, advances, and glyph flags |
| `KERN` | optional sorted glyph-pair adjustments |
| `PALT` | optional color palette and transparent entry |
| `BITM` | bitmap payload for one or more strikes |

Metrics use signed 26.6 fixed point. Character maps use 32-bit Unicode scalar
values and 32-bit glyph IDs rather than an 8-bit contiguous character range.
Each strike records horizontal and vertical pixel size separately. The primary
1920x1080 mode uses square pixels; independent metrics preserve imported and
future-mode fidelity without changing AFNT.

The exact disk record sizes are frozen only when the AFNT reader, writer,
validator, and malformed-file tests land together. Version 0 files are not an
application ABI.

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
compact anti-aliased UI-text path. `A8` is required by the Arty graphics
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

The implemented ULX3S low-level batch is an array of 16-byte big-endian
descriptors.
Each descriptor carries a source offset relative to the strike bitmap base,
unsigned source `(y,x)`, signed destination `(y,x)`, and unsigned
`(height,width)`. Astraea validates every descriptor before using it and keeps
the palette cached for the whole command. `ASTRAEA.md` is the normative ULX3S
MMIO and descriptor contract; `GRAPHICS_ARCHITECTURE.md` defines the Arty
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

The service imposes per-layout byte, line, run, and glyph limits. Editors and
large documents retain paragraph or viewport layouts rather than constructing
one unbounded object for an entire file.

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

The source-level 0.1 definitions are frozen in `ndk/include/astra/font.h`; that
header is the normative field and initializer contract. Family names, UTF-8,
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
Role matching returns an `AstraFontFace`; `AstraFontRequest` then resolves a
draw-ready strike-backed `AstraFont`. A returned match reports the actual strike
size and format so software never assumes that a bitmap was scaled.

Applications do not receive mutable glyph tables, atlas addresses, Astraea
descriptors, or a direct hardware queue. An advanced API may enumerate glyph
IDs and metrics for editors or font tools, but rendering still enters through a
validated draw list. A plain-text convenience function may create a temporary
layout, but retained UI text should use an explicit reusable layout.

The NDK 0.1 header and symbols are published with ABI-size assertions,
argument-validation tests, automatic-cleanup tests, a checked example, and an
explicit unavailable backend. That direct-MMIO backend returns
`ASTRA_ERROR_NOT_PRESENT` when opening a face because it has no font service;
it does not simulate successful font behavior. The OS implementation, live
handle/stale-handle tests, service protocol, and draw-list bridge must land
together before the service advertises itself as present.

## 7. Amiga import compatibility

Amiga fonts are useful content, not the Astra ABI. The importer accepts classic
bitmap disk fonts without executing them and converts:

- the strike bitmap and per-character location/width table;
- proportional spacing and kerning arrays when present;
- baseline, nominal size, and style flags;
- `ColorTextFont` bitplanes and palette into an AFNT indexed-color strike.

Classic files are relocatable 68k load modules containing a `DiskFontHeader`
and pointer-based `TextFont` or `ColorTextFont` data. The importer therefore
uses a bounded hunk parser in a sandboxed user process. It does not call
`LoadSeg`, run embedded return code, trust relocation addresses, or pass planar
data to Astraea.

Imported glyphs receive Unicode mappings through an explicit source-encoding
choice such as Amiga Latin-1 or a supplied mapping table. Unknown encodings do
not silently become Unicode.

Host and native tooling should share the same AFNT writer and golden files.
Amiga import is tested with monochrome, proportional, kerning, missing-glyph,
and color-font fixtures.

## 8. Legacy ULX3S hardware status and resource gate

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
- Atkinson Hyperlegible Mono:
  <https://github.com/googlefonts/atkinson-hyperlegible-next-mono>
