# Astra Mono system source

`JetBrainsMono-Regular.ttf` is the complete regular face from the official
JetBrains Mono 2.304 release, tag object
`cd5227bd1f61dff3bbd6c814ceaf7ffd95e947d9`. The source is licensed under the
SIL Open Font License 1.1 in `OFL.txt`.

JetBrains Mono is the normal terminal/code face. The independent Spleen 8x16
face remains the FPGA-resident rescue font used by POST and panic handling.

Source SHA-256:
`a0bf60ef0f83c5ed4d7a75d45838548b1f6873372dfac88f71804491898d138f`.

From the repository root, regenerate the resident face with the pinned font
tool environment described by `tools/fonts/afnt.py`:

```sh
tools/fonts/afnt.py import-outline \
  --family "Astra Mono" --style Regular --license OFL-1.1 \
  --source-revision v2.304 --monospaced --strike 16:13 \
  --output sw/userspace/graphics/fonts/astra-mono.afnt \
  sw/userspace/graphics/fonts/jetbrains-mono/JetBrainsMono-Regular.ttf
```
