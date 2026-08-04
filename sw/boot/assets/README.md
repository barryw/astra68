# Astra 68 Boot Splash

## Arty 1280x720 source

`astra_boot_splash_1280x720_blank.png` is the canonical Arty background. It is
exactly 1280x720 and deliberately contains no sample status text. The hardware
boot plane writes real machine state into the lower panel at runtime. Source
SHA-256 is
`cdf001bb70e130c9267f5205261eb3855f1b74cbbba832dbcd22fb6d66f77ff9`.

Regenerate its deterministic big-endian RGB565 payload with:

```sh
python3 sw/boot/pack_arty_splash.py \
  --image sw/boot/assets/astra_boot_splash_1280x720_blank.png \
  --output astra_boot_splash.rgb565
```

The result is exactly 1,843,200 bytes at a 2,560-byte pitch, CRC32
`611029ee`, and SHA-256
`86eb30739db77b85f4deb1915fb9cb9263ab4755ae318ffb1b7a4a95b7017ba4`.
The Arty boot text starts at `(264, 496)`, uses 36 columns by four rows, and
renders 16x16 CP437 glyphs on a 32-pixel row pitch. The ARM writes cells only;
it never paints text pixels into the framebuffer.

## ULX3S 720x480 source

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
  --font fpga/soc/post_fonts.hex \
  --output sw/boot/assets/astra_boot_splash.pal8.lz4
```

The payload is one legacy LZ4 block containing, in order:

- 345,600 INDEX8 framebuffer bytes;
- 1,024 bytes of BGRA palette data;
- 4,096 bytes of 256-glyph 8x16 MASK1 rescue-font data.

The font bank is generated from the same checked-in CP437 source used by the
FPGA POST console, with each 8-pixel row doubled vertically. Text pixels are
never rendered by the CPU: firmware submits batched glyph descriptors to
Astraea and presents completed buffers through Vega at vblank.
