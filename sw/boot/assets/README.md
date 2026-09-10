# Astra 68 Boot Splash

## DE25 1920x1080 source

`astra_boot_splash_1920x1080_blank.png` is the canonical DE25 background. It is
exactly 1920x1080 and deliberately contains no sample status text. The hardware
boot plane writes real machine state into the lower panel at runtime. Source
SHA-256 is
`db26450ae49471e81d90d4fdf554f1dd7226cf85f402af09d3f9e7e28cd2b687`.

Regenerate its deterministic big-endian RGB565 payload with:

```sh
python3 sw/boot/pack_arty_splash.py \
  --image sw/boot/assets/astra_boot_splash_1920x1080_blank.png \
  --output astra_boot_splash.rgb565
```

The result is exactly 4,147,200 bytes at a 3,840-byte pitch, CRC32 `639de5a9`,
and SHA-256
`b61e792ce65f76dd60343c48f2b94b2c43f28dcba187be1f090f2b56a7c349b7`.
The hardware boot text uses 36 columns by four rows. Its true 8x16 Spleen
strike is generated from `sw/userspace/graphics/fonts/astra-mono.afnt`; each
8-pixel advance and 12/4 ascent/descent line is enlarged 3x to a 24x48 physical
cell. The ARM writes cells only; it never paints text pixels into the
framebuffer.

The retained `astra_boot_splash_1280x720_blank.png` is the former Arty source.

## Archived 720x480 source

`astra_boot_splash.png` is the canonical 720x480 indexed source for the
firmware splash. It uses at most 252 image colors; palette entries 252 through
255 are reserved for hardware-rendered cyan, orange, success, and failure text.

The blank status-panel image was produced with OpenAI's built-in image editing
tool from the user-supplied Astra 68 concept, preserving its logo, frame,
tagline, and status panel while removing the sample status rows. It was then
downsampled to the native 720x480 HDMI mode and quantized without dithering.

Regenerate the checked-in payload from the repository root with:

```sh
python3 sw/boot/pack_boot_splash.py \
  --image sw/boot/assets/astra_boot_splash.png \
  --font assets/fonts/astra_8x16.hex \
  --output sw/boot/build/astra_boot_splash.pal8.lz4
```

The payload is one legacy LZ4 block containing, in order:

- 345,600 INDEX8 framebuffer bytes;
- 1,024 bytes of BGRA palette data;
- 4,096 bytes of 256-glyph 8x16 MASK1 rescue-font data.

The font bank is generated from the same checked-in CP437 source used by the
FPGA POST console, with each 8-pixel row doubled vertically. Text pixels are
never rendered by the CPU: firmware submits batched glyph descriptors to
Astraea and presents completed buffers through Vega at vblank.
