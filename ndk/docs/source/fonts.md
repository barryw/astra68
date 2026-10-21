# Fonts and Text

Font Kit manages installed AFNT faces and the process's default UI font.
Astraea renders native UI glyphs; the MC68040 reads metrics and submits drawing
commands. The system theme fixes sizes by UI role. Selection chooses a designed
face and weight, independently of application-specific fonts.

## Source and license information

Font Kit provides `astra_fonts_information_open()` through the read-only
`FONT_CATALOG` capability. Initialize `AstraFontInformation` with
`ASTRA_FONT_INFORMATION_INIT`, open it with a counted installed face ID, then
use `astra_fonts_information_string()` for family/style, source names and hashes,
converter, encoding and complete copyright/license/URL records. Root fields use
index zero; source and notice fields use their corresponding record index.
`source_count` and `notice_count` report available records.

Pass NULL and zero capacity to measure a string, then provide the measured byte
count plus one for the terminator. Strings are counted UTF-8 and may contain
line breaks or embedded NUL; never infer their length with `strlen()`.
An absent optional field returns `ASTRA_ERROR_NOT_PRESENT`. Insufficient space
returns `ASTRA_ERROR_BUFFER_TOO_SMALL` without changing output; malformed
metadata returns `ASTRA_ERROR_IO`. No text is silently shortened.

`astra_fonts_information_notice()` supplies original SFNT name, platform,
encoding and language IDs. Unsupported source encodings can omit decoded text;
the raw hexadecimal fields retain exact original bytes. Missing notices do not
mean that a font is unrestricted. Close the owned snapshot with
`astra_fonts_information_close()`. It remains stable across uninstall and
catalog changes and exposes no font raster data or writable storage.

## Initialize native UI text

Before laying out controls, call {c:func}`astra_fonts_use_system` with the
process's `GUI` capability and `NULL` for the change event. Font Kit 4 obtains
Display's immutable, read-only AFNT snapshot and its catalog ID through GUI
protocol 24. No font-directory filesystem grant is needed.

{c:func}`astra_surface_ui_text_width` and {c:func}`astra_surface_ui_text_fit`
then use that face's metrics. Common controls submit text with the same face
ID. Call context changes on the UI thread between frames; failed snapshot
validation preserves the previous context.

On `ASTRA_WINDOW_EVENT_FONT_CHANGED`, pass that window's delivered event to
`astra_fonts_use_system(gui, &event)` before dispatching it to the retained
controls. Interface Kit reflows their existing layout and damages the full
view, preserving focus and scrolling. Dispatch the event even if refresh
reports an error: layout must follow the context actually active in Font Kit.
Display retries notices until that specific window reports its adopted ID.
Refreshing another window cannot acknowledge this one. Modal alerts perform
the same operation inside their common event loop.

Display keeps confirmed and pending snapshots separately. A failed mapping or
validation therefore leaves the previous font usable across retries. Every
supervised GUI client, including services, has a private session retained by
Supervisor for its process lifetime. Owners of manually created sessions must
keep them alive while using their system context.

{c:func}`astra_fonts_use_snapshot` accepts an owned, mapped, read-only AFNT
area and its exact installed ID. Adoption consumes the area. The caller must
keep every alias immutable and bind the ID to the bytes used by Display.
An empty snapshot selects the built-in recovery face.

Renderers already retaining a face use {c:func}`astra_fonts_use_borrowed` with
its immutable AFNT bytes and ID. The owner keeps that mapping valid until the
context is replaced. This path replaces a built-in or borrowed context;
replacing an owned snapshot returns `ASTRA_ERROR_BUSY`. Identical area maps
share one process mapping, so duplicating a handle does not create another
mapping owner. Ordinary application refresh reuses unchanged immutable IDs.

## Drawing text

- **In a draw list**, {c:func}`astra_draw_ui_text` draws one line of UTF-8 in
  the UI font with the glyph engine. The pixel height must be one the font
  has a native strike for.
- **Through a surface view**, {c:func}`astra_surface_ui_text` and
  {c:func}`astra_surface_ui_text_styled` draw the UI font, and
  {c:func}`astra_surface_mono_text` the monospaced one, into an
  {c:struct}`AstraSurfaceView`. Native UI uses draw-list views, so these calls
  append hardware commands. RGB565 helpers remain for software surfaces and
  host oracles. {c:func}`astra_surface_ui_text_width`
  measures a string and {c:func}`astra_surface_ui_text_fit` finds how much
  of it fits a width.

Styles (`ASTRA_TEXT_STYLE_*` in `astra/text_style.h`) add underline and
strikethrough decorations. The selected designed face currently carries its
weight/slant; resolving semantic bold/italic to other family members remains
open. Monospaced text stays independent of the system UI choice.

## Metrics and glyphs

{c:func}`astra_ui_font_strike` and {c:func}`astra_mono_font_strike` query built-in
strike metrics -- ascent, descent, line gap, cap height, x-height, and
advance, in 26.6 fixed point -- or NULL for a height the font does not have.
{c:func}`astra_ui_font_glyph` and {c:func}`astra_ui_font_bitmap` give one
glyph's metrics and its MASK1 or A8 bitmap, for a program that lays out text
itself. The system theme (`astra/theme.h`) names the heights the system uses.

## Installed fonts

`FONTS:` names the one installed-font directory: protected shipped AFNT in
`FONTS:system/` and service-managed imports in `FONTS:installed/`. It is
read-only for ordinary applications. Font Kit is the normal discovery API;
clients never need to inspect filenames or parse AFNT to list fonts.
Native Filesystem Kit paths use `/FONTS/system/...` and
`/FONTS/installed/...`; POSIX tools use `/dh0/fonts/...`.

Get the `FONT_CATALOG` capability from startup, initialize an
`AstraFontsCatalog` with `ASTRA_FONTS_CATALOG_INIT`, then call
`astra_fonts_catalog_open`. Iterate `face_count` with
`astra_fonts_catalog_face` for counted family, designed style, stable ID,
strike count, and system-choice eligibility flags. Iterate each face's strikes
with `astra_fonts_catalog_strike` for pixel width, height, and bitmap format.
Close the snapshot after use; names are borrowed until close. One snapshot is
internally consistent even if another process imports or removes a font.

```c
const AstraStartupCapability *reader =
    astra_startup_capability(startup, ASTRA_CAPABILITY_FONT_CATALOG);
AstraFontsCatalog catalog = ASTRA_FONTS_CATALOG_INIT;
if (reader != NULL &&
    astra_fonts_catalog_open(reader->handle, &catalog) == ASTRA_OK) {
    for (uint32_t i = 0; i < catalog.face_count; ++i) {
        AstraInstalledFont face;
        if (astra_fonts_catalog_face(&catalog, i, &face) != ASTRA_OK)
            break;
        /* face.family/style are counted; face.strike_count gives size count. */
        for (uint32_t j = 0; j < face.strike_count; ++j) {
            AstraInstalledFontStrike strike;
            if (astra_fonts_catalog_strike(&catalog, i, j, &strike) != ASTRA_OK)
                break;
            /* Use strike.pixel_height and strike.bitmap_format here. */
        }
    }
    (void)astra_fonts_catalog_close(&catalog);
}
```

`FONTS_ADMIN` permits the import session API, `astra_fonts_select`, and
`astra_fonts_uninstall`. Import accepts native AFNT and static TTF, OTF, or TTC
sources up to 32 MiB. The resident service converts outlines through the
host worker, validates every resulting AFNT face, and returns a stable ID.
Variable outlines install their default and distinct named styles as separate
AFNT faces. Native import also accepts bounded monochrome Amiga disk fonts with
an explicit supported ISO-8859 encoding. Custom axis coordinates remain open.
System eligibility requires exact printable ASCII and every fixed UI-role size;
Fonts labels each style and disables unsuitable system choices. Application-only
faces remain installable and drawable through {c:func}`astra_draw_font_text`.
Uninstall accepts only imported faces; select another system face first if the
face is active. Shipped faces cannot be uninstalled. The service commits the
new Config Kit inventory before retiring a face from the catalog, then removes
its unreferenced AFNT object from `FONTS:installed/`. A cleanup error leaves an
unlisted object on disk and is logged; the removed font is not restored to the
catalog.

Selection prepares and validates an immutable AFNT snapshot before persisting
the choice through Config Kit. A failed preparation or settings write leaves
the previous selection intact. The resident service's native `FONTS_CHANGED`
wake hint tells Display to read the authoritative catalog and refresh its
active face. Desktop, Atlas, Fonts, Interface Gallery, Terminal's UI and modal
alerts refresh live. Terminal's monospaced content remains independent.
Catalog IDs can be drawn through Display's validated named-face resolver.

Display alone receives `FONTS_RENDER` for arbitrary installed-face lookup.
`astra_fonts_face_open` returns an immutable, read-only area containing a
validated installed AFNT and its exact byte length. The caller closes the
area with `astra_area_close`. Display validates and uses the selected face for
supported proportional strikes. Ordinary GUI clients receive the selected
read-only snapshot through Font Kit for measurement; Display and Astraea still
own pixel rendering. Resolving semantic bold/italic to other designed family
members remains open.
