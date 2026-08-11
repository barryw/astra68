# Astra Workbench source

`11.amiga` and `13.amiga` are the complete 11- and 13-pixel
`ISOHelveticaPL` proportional bitmap strikes from the official AROS source at
commit `e7ab816b0da78300c6bbd9cc2cb21f8f545bc818`:

```text
workbench/fonts/AmigaPL/ISOHelveticaPL/11
workbench/fonts/AmigaPL/ISOHelveticaPL/13
```

The upstream author is Andrzej "Apis" Podgórski. `UPSTREAM.readme` grants
distribution under the AROS Public License; `LICENSE.AROS` contains that
license. The source files are preserved in their passive Amiga disk-font form
and are never executed by the Astra build.

Regenerate the native AFNT resource with:

```sh
uv run --with monobit==0.53 python tools/fonts/afnt.py import-amiga \
  --family "Astra Workbench" --style Regular \
  --license "AROS Public License 1.1" \
  --source-revision e7ab816b0da78300c6bbd9cc2cb21f8f545bc818 \
  --encoding iso8859_2 --first-code 32 --last-code 255 \
  --output sw/userspace/graphics/fonts/astra-workbench.afnt \
  sw/userspace/graphics/fonts/aros-isohelvetica/11.amiga \
  sw/userspace/graphics/fonts/aros-isohelvetica/13.amiga
```

The importer rejects either strike unless every source code from 32 through
255 and the default glyph are present. The AFNT file therefore contains 225
glyphs per strike and a sorted Unicode `CMAP`, including `U+FFFD`.
