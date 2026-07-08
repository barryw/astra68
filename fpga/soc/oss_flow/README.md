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
- ROM hexes come from beast: ~/astra_st (selftest -> rom_init.hex ; banner -> rom_banner.hex).
- Needs GHDL_PREFIX=/opt/homebrew/oss-cad-suite/lib/ghdl (set in scripts).

## Flash + UART (macOS FT231X gotcha)
ULX3S has ONE FT231X shared by JTAG (openFPGALoader, libusb bitbang) and UART (VCP).
After `openFPGALoader --board ulx3s astra.bit` the chip may not return to async-UART
mode cleanly -> UART reads 0 bytes. **Fix: physically replug USB (or power-cycle), OR
USB-reset it** (pip install pyusb; d=usb.core.find(idVendor=0x0403,idProduct=0x6015); d.reset()).
Then read: `python3 serread2.py 8` (115200 on /dev/cu.usbserial-*).

Liveness without UART = the LEDs: {hb[23]=~5s blink, ~AS, ~RW, uart_busy, adr[3:0]}.
