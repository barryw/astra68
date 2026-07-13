# hdl-util/hdmi Vendor Snapshot

Source: https://github.com/hdl-util/hdmi
Snapshot: fbade3d11a58b885a6084ec75eae25339623355d
License: MIT OR Apache-2.0

Local change:

- `serializer.sv` has a `LATTICE_ECP5` synthesis branch that serializes the
  HDMI TMDS words with ECP5 `ODDRX1F` cells. The upstream serializer does not
  ship a Lattice synthesis path.
- Floating-point video-rate parameters were replaced with integer hertz /
  millihertz parameters so Verilator and Yosys both constant-fold the audio
  clock regeneration logic.
- `hdmi.sv` uses blocking assignments in one combinational sync block.
- `tmds_channel.sv` suppresses Verilator's ordering warning around the
  upstream sequential TMDS q_m construction.
- Module ports that carried unpacked arrays were converted to packed arrays
  because Yosys rejects unpacked array ports during ECP5 synthesis.
