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

Role selection lets users replace desktop defaults without requiring
applications to change. Explicit family and style lookup remains available for
documents and font-aware tools.

## Native strikes

Astra fonts contain designed bitmap strikes. A request states pixel dimensions,
weight, stretch, style, color preference, and whether an exact strike is
required. The service does not synthesize runtime bitmap scaling; callers query
{c:struct}`AstraFontInfo` to learn the actual selected dimensions and bitmap
format.

## Layout boundaries

Layout creation copies and validates its explicit UTF-8 byte span. Source
positions are always UTF-8 byte offsets at valid scalar or grapheme boundaries,
which keeps editing APIs stable without exposing internal glyph runs. Layouts
support measurement, point hit testing, and caret lookup.

Text rasterization enters Astraea through {c:func}`astra_draw_text_layout` on
the common graphics draw-list and fence model. The font API intentionally does
not publish glyph-atlas addresses or a direct hardware queue.
