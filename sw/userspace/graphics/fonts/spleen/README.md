# Astra Mono source

`8x16.bdf` is Spleen 8x16 from upstream commit
`57f9219328c9f5873085320fe8bc8f7dd34b8791`. Spleen is Copyright
Frederic Cambus and distributed under the BSD 2-Clause license in `LICENSE`.

The source strike is complete: 1,001 Unicode glyphs covering its published
Latin, CP437, box-drawing, block, Braille, and Powerline repertoire. Regenerate
the hardware-ready AFNT resource with:

```sh
uv run --with monobit==0.53 tools/fonts/afnt.py import-bitmap \
  --family "Astra Mono" --style Regular --license BSD-2-Clause \
  --source-revision 57f9219328c9f5873085320fe8bc8f7dd34b8791 \
  --unicode-labels --monospaced \
  --output sw/userspace/graphics/fonts/astra-mono.afnt \
  sw/userspace/graphics/fonts/spleen/8x16.bdf
```
