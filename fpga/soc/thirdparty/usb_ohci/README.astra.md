# USB OHCI RTL provenance

`UsbOhciWishbone_Dw32_Pc1_Pf48000000.v` is an unmodified generated artifact
from `litex-hub/pythondata-misc-usb_ohci`:

- package repository commit: `17c1d3d6548ea267e19aec3cb6d2e64335a1bb2a`
- package data version: `1.0.1.post265`
- SpinalHDL source hash recorded by the generator:
  `8091de30187f924cf1ec122a4127175cb57bf16f`
- imported-file SHA-256:
  `ab0e0476fdf41e1581ccd952642fc62ea82dab3656517b17b816ef73f57d8228`
- package metadata license classifier: MIT License

The generated configuration is a 32-bit Wishbone OHCI 1.0 host with one
low/full-speed physical port and a 48 MHz PHY clock. Astra-specific clocking,
CDC, SDRAM arbitration, endian conversion, fault recording, and ULX3S pin
handling live outside this file. Do not patch the generated artifact; import a
new upstream artifact and update the provenance and integration tests instead.
