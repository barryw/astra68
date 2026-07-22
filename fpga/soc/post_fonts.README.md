# POST Font Image

`post_fonts.hex` contains the first 2048-byte CP437 bank from
`NovaVM/e6502.FPGA/rom/fonts.hex`, the font image already used by NovaVM's
hardware-verified 720x480 text/OSD paths.

Its layout is:

- `0x0000..0x07FF`: 256 CP437 glyphs, eight bytes per glyph

Astra's POST console instantiates exactly the first 2048-byte CP437 bank and
doubles each 8x8 glyph row vertically to form an 8x16 cell. Keeping both the
image and inferred ROM at their exact logical depth avoids unused constant high
address pins and maps the font into one ECP5 block RAM. The OS is not required
to use this image and supplies its own bitmap fonts.

This is the current implementation, not the final font contract. The planned
replacement is the true 8x16 Astra Rescue Mono bank described in
`docs/FONTS.md`: 256 glyphs / 4096 bytes, derived from Spleen 8x16 and retained
in FPGA BRAM for reset, POST, recovery, and kernel panic output. Replacing this
image also removes vertical row doubling and requires visual plus hardware
acceptance before it becomes the default.
