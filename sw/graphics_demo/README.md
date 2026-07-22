# Astra graphics hardware diagnostic

This freestanding ROM is a destructive visual acceptance test for the complete
Vega/Astraea path. It is not the production boot ROM and does not replace the
SD-card boot flow.

The scene exercises INDEX8 framebuffer scanout and color keying, all four
blitter modes, overlapping sprites and collision state, copper raster writes,
clipped geometry, pattern and flood fills, and MASK1/A4 glyph expansion. A
success build leaves LED pattern `0x5a`; an error leaves bit 7 set with a stage
code in the low bits and switches Vega to a red backdrop.

```sh
make -C sw/graphics_demo verify
```

Run the complete CPU, pin-level SDRAM, and HDMI simulation gate with:

```sh
sw/graphics_demo/run_sim.sh all
```

`all` compiles one RTL model and boots three ROMs: the normal diagnostic,
INDEX8 with all 16 unrelated-row 32-pixel sprites, and RGB565 with the same
sprite set. The test rejects underrun/configuration status, incorrect sprite
admission or collision state, an unexercised copper, and any scanline taking
1906 SDRAM clocks or more. Individual modes are `normal`, `stress-index8`, and
`stress-rgb565`. Pixel-granular X/Y framebuffer scroll and wrap are covered by
the directed Vega regression.

Build a temporary hardware image with `SD_BOOT_ENABLE=0`,
`ASTRA_HOST_ENABLE=0`, and `ROM_WORDS=1024`. Reflash the production SD-boot
image after capturing the diagnostic result.
