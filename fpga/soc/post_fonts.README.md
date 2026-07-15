# POST Font Image

`post_fonts.hex` is copied from `NovaVM/e6502.FPGA/rom/fonts.hex`, the font image
already used by NovaVM's hardware-verified 720x480 text/OSD paths.

The 8192-byte layout is:

- `0x0000..0x07FF`: CP437, including standard printable ASCII
- `0x0800..0x0FFF`: PETSCII upper/graphics
- `0x1000..0x17FF`: PETSCII lower/upper
- `0x1800..0x1FFF`: reserved/zero

Astra's POST console addresses only the first bank and doubles each 8x8 glyph
row vertically to form an 8x16 cell. The additional banks remain inert; the OS
is not required to use this image and supplies its own bitmap fonts.

This is the current implementation, not the final font contract. The planned
replacement is the true 8x16 Astra Rescue Mono bank described in
`docs/FONTS.md`: 256 glyphs / 4096 bytes, derived from Spleen 8x16 and retained
in FPGA BRAM for reset, POST, recovery, and kernel panic output. Replacing this
image also removes vertical row doubling and requires visual plus hardware
acceptance before it becomes the default.
