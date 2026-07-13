# Astra68 open synth/PnR flow (yosys + nextpnr, macOS/oss-cad-suite)

Local ECP5 flow for the ULX3S. **nextpnr is the honest oracle** — it hard-refuses
combinational loops (rc=255) instead of silently cutting them like Diamond/Synplify.

## Verified 2026-07-07
Core is **combinational-loop-free** (the 3 prior control.vhd fixes cleared every
source-level loop; the residual "sqmux" loops were Synplify-symbolic-FSM-only, absent
in yosys). Checks: yosys `check` (behavioral) 0 problems, `check -assert` (mapped) 0
problems, nextpnr rc=0 fully routed — no `--ignore-loops`.

## Build
- `run.sh`          — full build + loop report (yosys check + nextpnr).
- `mkbit.sh <hex> <tag>` — swap ROM (rom_init.hex), synth+pnr+ecppack -> astra.bit.
- `PNR_SEED=<n>` selects and records the deterministic nextpnr route seed. Include
  it in firmware build-ID arguments whenever it is overridden.
- ROM hexes come from beast: ~/astra_st (selftest -> rom_init.hex ; banner -> rom_banner.hex).
- Needs GHDL_PREFIX=/opt/homebrew/oss-cad-suite/lib/ghdl (set in scripts).

For an SRAM-only load followed by a timed UART POST gate:

```sh
python3 ../../../sw/boot/check_hardware.py --bit astra.bit
```

The command exits nonzero on a POST failure or timeout. It does not write SPI
flash.

For fast controller-only ULX3S read-sampling and byte-enable validation, use:

```sh
cd ../../../fpga/memtest32
bash build.sh 3
python3 capture.py build/latency3/astra_sdram32_hwtest.bit
```

At 75 MHz, latency 3 is the accepted ECP5 sample point. Latencies 1 and 2 fail
with deterministic halfword signatures; changing the SDRAM clock or output
phase requires repeating this hardware sweep.

## Flash + UART (macOS FT231X gotcha)
ULX3S has ONE FT231X shared by JTAG (openFPGALoader, libusb bitbang) and UART (VCP).
After `openFPGALoader --board ulx3s astra.bit` the chip may not return to async-UART
mode cleanly -> UART reads 0 bytes. **Fix: physically replug USB (or power-cycle), OR
USB-reset it** (pip install pyusb; d=usb.core.find(idVendor=0x0403,idProduct=0x6015); d.reset()).
Then read: `python3 serread2.py 8` (115200 on /dev/cu.usbserial-*).

Liveness without UART = the LEDs: {hb[23]=~5s blink, ~AS, ~RW, uart_busy, adr[3:0]}.
