# Fonts and Text Layout

The NDK exposes fonts as immutable service objects rather than font-file
structures. Applications open an {c:struct}`AstraFontFace`, resolve it to a
native-strike {c:struct}`AstraFont`, and create reusable
{c:struct}`AstraTextLayout` objects from copied UTF-8 text.

```text
system role or family -> font face -> resolved font -> immutable text layout
```

The font service owns AFNT validation, fallback, shaping, metrics, bitmap
storage, and cache lifetime. Applications never receive glyph-atlas pointers,
DMA addresses, or AFNT chunk records. A layout retains every face and strike it
uses, so closing the application's font wrapper cannot invalidate a layout or a
submitted draw.

## Current availability

The API is declared and linkable in NDK 0.1. The direct-MMIO backend does not
have a filesystem or font service, so {c:func}`astra_fonts_present` returns
false and face-open operations return {c:enumerator}`ASTRA_ERROR_NOT_PRESENT`.
This is a real availability state, not simulated font behavior. The future OS
backend implements the same source contract.

## System roles

Applications should prefer semantic system roles over bundled family names:

- `ASTRA_FONT_ROLE_UI` for ordinary interface text.
- `ASTRA_FONT_ROLE_UI_EMPHASIS` for emphasized interface text.
- `ASTRA_FONT_ROLE_MONO` for code, terminals, and aligned data.
- `ASTRA_FONT_ROLE_RESCUE` for the non-replaceable recovery face.

The immutable system theme publishes the native 16-pixel Astra Mono strike
height and its current 8-pixel cell advance. Applications request the semantic
role and use returned metrics when the OS font backend is available; they do
not embed Spleen's family name or private AFNT records.

Role selection lets users replace desktop defaults without requiring
applications to change. Explicit family and style lookup remains available for
documents and font-aware tools.

## Native strikes and styles

Astra fonts expose hardware-ready bitmap strikes. A request states pixel
dimensions, weight, stretch, slant, color preference, and whether an exact
strike is required. Designed bold and italic faces are preferred. If no
designed face exists and {c:enumerator}`ASTRA_FONT_MATCH_ALLOW_SYNTHESIS` is
set, the service may materialize and cache an emboldened or slanted strike;
{c:struct}`AstraFontInfo` reports the synthetic result. Applications do not
carry duplicate glyph sets and the MC68040 does not scale or transform glyph
pixels.

Underline and strikeout are paint decorations positioned from
{c:struct}`AstraFontMetrics`; they never select alternate glyph bitmaps. The
same metrics provide ascent, descent, line gap, cap height, x-height, maximum
advance, and the baseline-relative decoration positions needed by editors and
document applications.

## Layout boundaries

Layout creation copies and validates its explicit UTF-8 byte span. Source
positions are always UTF-8 byte offsets at valid scalar or grapheme boundaries,
which keeps editing APIs stable without exposing internal glyph runs. Layouts
support measurement, point hit testing, and caret lookup.

Text rasterization enters Astraea through {c:func}`astra_draw_text_layout` on
the common graphics draw-list and fence model. The font API intentionally does
not publish glyph-atlas addresses or a direct hardware queue.
