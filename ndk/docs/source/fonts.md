# Fonts and Text

The system has two bitmap fonts, built into `font.library`: the proportional
UI font that every window's chrome uses, and the monospaced font of the
terminal. Their strikes are hardware-ready bitmaps; the MC68040 never scales
or transforms glyph pixels.

## Drawing text

- **In a draw list**, {c:func}`astra_draw_ui_text` draws one line of UTF-8 in
  the UI font with the glyph engine. The pixel height must be one the font
  has a native strike for.
- **Into a CPU surface**, {c:func}`astra_surface_ui_text` and
  {c:func}`astra_surface_ui_text_styled` draw the UI font, and
  {c:func}`astra_surface_mono_text` the monospaced one, into an
  {c:struct}`AstraSurfaceView`. {c:func}`astra_surface_ui_text_width`
  measures a string and {c:func}`astra_surface_ui_text_fit` finds how much
  of it fits a width.

Styles (`ASTRA_TEXT_STYLE_*` in `astra/text_style.h`) select bold and italic
renderings and the underline and strikethrough decorations.

## Metrics and glyphs

{c:func}`astra_ui_font_strike` and {c:func}`astra_mono_font_strike` return a
strike's metrics -- ascent, descent, line gap, cap height, x-height, and
advance, in 26.6 fixed point -- or NULL for a height the font does not have.
{c:func}`astra_ui_font_glyph` and {c:func}`astra_ui_font_bitmap` give one
glyph's metrics and its MASK1 or A8 bitmap, for a program that lays out text
itself. The system theme (`astra/theme.h`) names the heights the system uses.

## Installed fonts

`astra/fonts.h` exposes an immutable catalog snapshot through the `FONTS`
capability. `astra_fonts_catalog_open`, `astra_fonts_catalog_face`, and
`astra_fonts_catalog_strike` return installed family, designed style, and
available AFNT sizes. Close the snapshot after use; its names are borrowed.

`FONTS_ADMIN` permits `astra_fonts_import`, `astra_fonts_select`, and
`astra_fonts_uninstall`. Import accepts native AFNT and static TTF, OTF, or TTC
sources up to 32 MiB. The resident service converts outlines through the
host worker, validates every resulting AFNT face, and returns a stable ID.
Variable outlines install their default and distinct named styles as separate
AFNT faces. Custom axis coordinates and Amiga disk fonts are not yet accepted
at runtime.
Uninstall accepts only imported faces; select another system face first if the
face is active. Shipped faces cannot be uninstalled. The service commits the
new Config Kit inventory before retiring a face from the catalog, then removes
its unreferenced AFNT object. A cleanup error leaves an unlisted object on
disk and is logged; the removed font is not restored to the catalog.

Selection persists through Config Kit. Display loads the selected AFNT at
startup for proportional draw-list text. Existing clients still lay out text
with built-in metrics, and running Display does not yet refresh after a change.
An installed catalog entry is metadata, not a drawable handle for applications.

Display alone receives `FONTS_RENDER` in the normal startup manifest.
`astra_fonts_face_open` returns an immutable, read-only area containing a
validated installed AFNT and its exact byte length. The caller closes the
area with `astra_area_close`. Display validates and uses the selected face for
supported regular proportional strikes, with built-in fallback for missing
strikes and styles.
