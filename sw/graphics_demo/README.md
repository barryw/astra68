# Astra graphics hardware diagnostic

This freestanding ROM is a destructive visual acceptance test for the complete
Vega/Astraea path. It is not the production boot ROM and does not replace the
SD-card boot flow.

The scene exercises INDEX8 framebuffer scanout and color keying, all four
blitter modes, overlapping sprites and collision state, copper raster writes,
clipped geometry, pattern and flood fills, and MASK1/A4 glyph expansion. A
success build leaves LED pattern `0x5a`; an error leaves bit 7 set with a stage
code in the low bits and switches Vega to a red backdrop. The independent FTDI
diagnostic UART also reports `GFX PASS` or `GFX FAIL nn`; this UART is not an
ESP32 transport.

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
`stress-rgb565`. The `hardware-map` mode runs the same complete path with the
production stage-0 address decode and linker. Pixel-granular X/Y framebuffer
scroll and wrap are covered by the directed Vega regression.

`blit-config` is the focused command-capture/range-validation diagnostic. It
runs one-row and multi-row fills with zero and normal pitch, records the exact
MMIO fields/status/completion fence for each command, and repeats the report so
an FTDI rebind cannot lose the result:

```sh
sw/graphics_demo/run_sim.sh blit-config
```

For a standalone temporary image, build with `SD_BOOT_ENABLE=0`,
`ASTRA_HOST_ENABLE=0`, and `ROM_WORDS=1024`. For release-route qualification,
first run `make -C sw/graphics_demo verify-stage0`; this links the diagnostic
for the production stage-0 aperture at `0xFFFC0000` and rejects any reset PC
other than `0xFFFC0400`. Synthesize it with the exact production feature
parameters, then use `fpga/soc/oss_flow/make_route_probe_bitstream.py` to
transplant only the validated `rom.*` initializers into the accepted routed
configuration. Do not place or route a separate diagnostic design.

Capture the UART result while loading the temporary image into SRAM with:

```sh
python3 sw/boot/check_hardware.py --bit astra-graphics.bit --expect-graphics
```

The diagnostic image must never be written to persistent flash.
Reload the production SD-boot image after capturing the diagnostic result.
