# hdl-util/hdmi vendor snapshot

Source: <https://github.com/hdl-util/hdmi>

Snapshot: `fbade3d11a58b885a6084ec75eae25339623355d`

License: MIT OR Apache-2.0

This exact snapshot was previously exercised on the Arty Z7-20 HDMI source
port. Astra retains it instead of replacing the TMDS encoder and OSERDESE2
serializer with new, unqualified logic.

Local changes already present in the retained snapshot are:

- Retired ECP5 synthesis support is removed; Astra uses the upstream Xilinx
  OSERDESE2 implementation on the Arty.
- Floating-point video-rate parameters use integer hertz and millihertz values
  so Verilator and Yosys can constant-fold the audio calculations.
- Packed array ports and warning fixes preserve compatibility with the Astra
  simulation and synthesis toolchains.

The 720p shell selects CTA/CEA VIC 4 and the Xilinx synthesis branch. Any
future update must retain the licenses, record the new upstream identity, run
the HDMI simulations, close full routed timing, and pass repeated hardware
capture tests.
