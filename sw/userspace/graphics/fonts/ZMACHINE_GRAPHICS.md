# Z-machine graphics font

`zmachine-graphics.bdf` is generated from the pinned Frotz
`third_party/frotz/src/sdl/sf_font3.c` bitmap at upstream commit
`042d7bcadc1e2a8090cf737c841d8b7fc14b6eff`. The source is licensed
GPL-2.0-or-later; see `third_party/frotz/COPYING`. It maps Z-machine graphics
font 3 codes 32–126 to their 8×8 cells. The asset is a protected AFNT in
`FONTS:system` and is not eligible for selection as the desktop font because
it has only an 8-pixel strike.

The native font service resolves the AFNT and the hardware glyph engine draws
it. To regenerate the asset:

```sh
python3 tools/fonts/frotz_font3_bdf.py \
  third_party/frotz/src/sdl/sf_font3.c \
  sw/userspace/graphics/fonts/zmachine-graphics.bdf
uv run --with monobit==0.53 tools/fonts/afnt.py import-bitmap \
  --family "Z-machine Graphics" --style Regular \
  --license GPL-2.0-or-later \
  --source-revision 042d7bcadc1e2a8090cf737c841d8b7fc14b6eff \
  --unicode-labels --monospaced \
  --output sw/userspace/graphics/fonts/astra-zmachine-graphics.afnt \
  sw/userspace/graphics/fonts/zmachine-graphics.bdf
```
