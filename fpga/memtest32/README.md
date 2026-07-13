# SDRAM32 ULX3S hardware gate

This controller-only image tests the native Astra 32-bit request path without
the CPU, PMMU, caches, or HDMI. It writes and reads distinguishing 32-bit
patterns, then checks all 15 nonzero byte-enable masks. The result repeats over
the FTDI UART so it is not lost while the device node returns after JTAG use.

Build and run on a host with the open FPGA toolchain and ULX3S attached:

```sh
bash build.sh 3
python3 capture.py build/latency3/astra_sdram32_hwtest.bit
```

The optional build argument selects `SDRAM_READ_LATENCY` (`1`, `2`, or `3`). At
75 MHz on the ULX3S ECP5, latency 3 passes all 38 operations. Latency 2 returns
`0x00010000` after writing `0x00000001`; latency 1 returns `0x00010001`.
