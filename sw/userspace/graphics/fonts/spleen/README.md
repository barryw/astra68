# Astra Mono source

`8x16.bdf` is Spleen 8x16 from upstream commit
`57f9219328c9f5873085320fe8bc8f7dd34b8791`. Spleen is Copyright
Frederic Cambus and distributed under the BSD 2-Clause license in `LICENSE`.

`8x8.bdf` is a derived, fixed-cell strike. Each pair of vertical source
scanlines is OR-combined so one-pixel strokes survive. It retains Spleen's
complete Unicode repertoire and 8-pixel advance. The bold and italic sources
derive from the same rows by one-pixel emboldening and upper-row slanting.
They are designed AFNT faces in one family, so the installed-font catalog and
hardware glyph renderer share the same style choices. Regenerate the
checked-in sources and hardware glyph-engine AFNT resources with:

```sh
python3 tools/fonts/halve_bdf_height.py \
  sw/userspace/graphics/fonts/spleen/8x16.bdf \
  sw/userspace/graphics/fonts/spleen/8x8.bdf
python3 tools/fonts/halve_bdf_height.py \
  sw/userspace/graphics/fonts/spleen/8x16.bdf \
  sw/userspace/graphics/fonts/spleen/8x8-bold.bdf --bold
python3 tools/fonts/halve_bdf_height.py \
  sw/userspace/graphics/fonts/spleen/8x16.bdf \
  sw/userspace/graphics/fonts/spleen/8x8-italic.bdf --italic
python3 tools/fonts/halve_bdf_height.py \
  sw/userspace/graphics/fonts/spleen/8x16.bdf \
  sw/userspace/graphics/fonts/spleen/8x8-bold-italic.bdf --bold --italic
uv run --with monobit==0.53 tools/fonts/afnt.py import-bitmap \
  --family "Astra Pixel Mono" --style Regular --license BSD-2-Clause \
  --source-revision 57f9219328c9f5873085320fe8bc8f7dd34b8791 \
  --unicode-labels --monospaced \
  --output sw/userspace/graphics/fonts/astra-pixel-mono.afnt \
  sw/userspace/graphics/fonts/spleen/8x8.bdf
```

For each styled BDF, use the same AFNT command with `--style Bold`,
`--style Italic`, or `--style "Bold Italic"` and the matching
`astra-pixel-mono-*.afnt` output. The source revision and license stay the
same across the family.

The source strike is complete: 1,001 Unicode glyphs covering its published
Latin, CP437, box-drawing, block, Braille, and Powerline repertoire. Regenerate
the hardware-ready AFNT resource with:

```sh
uv run --with monobit==0.53 tools/fonts/afnt.py import-bitmap \
  --family "Astra Mono" --style Regular --license BSD-2-Clause \
  --source-revision 57f9219328c9f5873085320fe8bc8f7dd34b8791 \
  --unicode-labels --monospaced \
  --output sw/userspace/graphics/fonts/astra-rescue-mono.afnt \
  sw/userspace/graphics/fonts/spleen/8x16.bdf
```
