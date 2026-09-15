# Astra Sans source

`AtkinsonHyperlegibleNext-Regular.ttf` is the complete regular face from the
official Google Fonts Atkinson Hyperlegible Next repository at commit
`7925f50f649b3813257faf2f4c0b381011f434f1` (version 2.001). The source is
licensed under the SIL Open Font License 1.1 in `OFL.txt`.

The build-time importer rasterizes native A8 AFNT strikes. Astraea expands
those strikes in hardware; the MC68040 never parses or rasterizes this outline.

Source SHA-256:
`88ed5c31a71584c7772963b02d04bef1eb7e3d2e9c8b9cb204339b1f82cf432c`.

From the repository root, regenerate the resident face with the pinned font
tool environment described by `tools/fonts/afnt.py`:

```sh
tools/fonts/afnt.py import-outline \
  --family "Astra Sans" --style Regular --license OFL-1.1 \
  --source-revision 7925f50f649b3813257faf2f4c0b381011f434f1 \
  --strike 11:8 --strike 13:10 \
  --output sw/userspace/graphics/fonts/astra-workbench.afnt \
  sw/userspace/graphics/fonts/atkinson-hyperlegible-next/AtkinsonHyperlegibleNext-Regular.ttf
```
