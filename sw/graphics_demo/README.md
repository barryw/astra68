# Astra graphics hardware diagnostic

This freestanding ROM is a destructive visual acceptance test for the complete
Vega/Astraea path. It is not the production boot ROM and does not replace the
SD-card boot flow.

The scene exercises INDEX8 framebuffer scanout and color keying, all four
blitter modes, two independently scrolling tile layers, overlapping sprites
and collision state, copper raster writes, clipped geometry, pattern and flood
fills, and MASK1/A4 glyph expansion. A success build leaves LED pattern `0x5a`;
an error leaves bit 7 set with a stage code in the low bits and switches Vega
to a red backdrop.

```sh
make -C sw/graphics_demo verify
```

Run the complete CPU, pin-level SDRAM, and HDMI simulation gate with:

```sh
sw/graphics_demo/run_sim.sh all
```

`all` compiles one RTL model and boots three ROMs: the normal diagnostic,
INDEX8 with 32 unrelated-row 32-pixel sprites and a 1024-pixel request, and
RGB565 with the same request. Hardware clamps the RGB565 workload to its
validated 512-pixel limit. The test rejects underrun/configuration status,
incorrect sprite admission or collision state, an unexercised copper, and any
scanline taking 2383 SDRAM clocks or more. Individual modes are `normal`,
`stress-index8`, and `stress-rgb565`.

At 75 MHz SDRAM, the current locked simulation results are 1506 clocks for the
normal scene, 2369 for INDEX8/1024, and 2060 for RGB565/512 effective. The two
stress modes keep both tile layers active.

Build a temporary hardware image with `SD_BOOT_ENABLE=0`,
`ASTRA_HOST_ENABLE=0`, and `ROM_WORDS=1024`. Reflash the production SD-boot
image after capturing the diagnostic result.
