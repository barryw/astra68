# Astra68 open synth/PnR flow (Yosys + nextpnr)

Local ECP5 flow for the ULX3S. **nextpnr is the honest oracle** — it hard-refuses
combinational loops (rc=255) instead of silently cutting them like Diamond/Synplify.

See [TIMING_CLOSURE.md](TIMING_CLOSURE.md) for the measured timing history,
known-good floorplan, failed approaches, regression gates, and release checklist.

## Historical loop baseline (2026-07-07)

The CPU core is **combinational-loop-free**: three prior `control.vhd` fixes
cleared every source-level loop, and the residual Synplify symbolic-FSM
`sqmux` reports are absent in Yosys. The earlier SoC passed behavioral and
mapped checks and fully routed without `--ignore-loops`. That old route is not
evidence that the current complete-graphics image meets timing; use the latest
checkpoint in `TIMING_CLOSURE.md` for current integration status.

## Build
- `run.sh`          — full build + loop report (yosys check + nextpnr).
- `mkbit.sh <hex> <tag>` — swap ROM (rom_init.hex), synth+pnr+ecppack -> astra.bit.
- `PNR_SEED=<n>` selects and records the deterministic nextpnr route seed. Include
  it in firmware build-ID arguments whenever it is overridden.
- `SYNTH_ONLY=1` stops after checked `astra.json` generation for resource
  iteration. AstraHost and direct-SD stage-0 builds reserve 1024 and 2048 ROM
  words respectively; `ROM_WORDS=<n>` overrides that capacity. The build
  rejects a hex image that does not fit instead of allowing `$readmemh` to
  truncate stage 0.
- The canonical ECP5 synthesis uses Yosys `-abc2`, which produces the accepted
  packed baseline. `SYNTH_ECP5_FLAGS` remains available for measured mapping
  experiments; release utilization must use the checked-in default.
- `PNR_ROUTER=router1|router2` selects the nextpnr router and is recorded in the
  build banner. `PNR_ROUTER2_ALT_WEIGHTS=1` enables router2's high-density
  weighting mode and requires `PNR_ROUTER=router2`. `PNR_THREADS=<n>` passes an
  explicit router thread count. Every route writes `pnr_<tag>.json` beside its
  text log.
- `astra_clocks.sdc` constrains every clock domain explicitly: CPU 12.5 MHz,
  SD input 20 MHz, SDRAM 75 MHz, pixel 27 MHz, HDMI shift 135 MHz, and the
  25 MHz board clock. Keep it on split placement/routing commands as well as
  canonical builds; generated PLL constraints are not retained in a saved
  placed JSON netlist.
- Every routed build runs `check_resource_budget.py`. The default
  `core_graphics` profile caps packed logic at 65% while Astraea and Vega are
  integrated. The checked-in `complete_chipset` profile defines the 75% target
  for the completed Vesta/Astraea/Vega/Lyra chipset. Independent 80% absolute
  limits preserve fabric, block RAM, and multiplier capacity for hardware that
  has not been specified yet. Advancing the canonical build to another profile
  requires a reviewed `resource_budgets.json` change; `mkbit.sh` has no
  command-line waiver.
- `ASTRA_FLOORPLAN_MODE=<mode>` selects the coarse placement tier and
  `ASTRA_FLOORPLAN_ENFORCE=<region,...>` adds named regions such as
  `sdram_edge`, `vega_sprites`, `vega_tiles`, `astraea_draw`,
  `astraea_blitter_control`, `astraea_blitter_cdc`, `astraea_blitter`, or
  `astraea_copper`. The pre-place report prints matched and constrained cells
  plus per-resource capacity for every region. The default critical plan keeps
  the HDMI serializer, TG68K cache, and SDRAM controller/BIST response island
  in their board-facing regions.
  The old `NOVA_FLOORPLAN_*` names remain accepted for existing automation,
  but Astra-named variables take precedence.
- ROM hexes come from beast: ~/astra_st (selftest -> rom_init.hex ; banner -> rom_banner.hex).
- Needs GHDL_PREFIX=/opt/homebrew/oss-cad-suite/lib/ghdl (set in scripts).

The ULX3S is attached to `nuc`, not the Mac or Beast. Transfer the immutable
artifact to NUC, then use the timed UART POST gate for an SRAM-only load:

```sh
rtk rsync -a astra.bit nuc:/tmp/astra68-release.bit
rtk proxy ssh nuc "cd ~/astra68 && python3 sw/boot/check_hardware.py \
  --bit /tmp/astra68-release.bit --expect-build <8-hex-build-id>"
```

The command exits nonzero on a POST failure or timeout. It does not write SPI
flash. Use `openFPGALoader --board ulx3s -f -r` on NUC only after the exact
SRAM-loaded image passes its build-identity and POST checks.

For fast controller-only ULX3S read-sampling and byte-enable validation, run on
NUC:

```sh
cd ../../../fpga/memtest32
bash build.sh 3
python3 capture.py build/latency3/astra_sdram32_hwtest.bit
```

At 75 MHz, latency 3 is the accepted ECP5 sample point. Latencies 1 and 2 fail
with deterministic halfword signatures; changing the SDRAM clock or output
phase requires repeating this hardware sweep.

## Flash + UART on NUC

The ULX3S has one FT231X shared by JTAG and the FTDI diagnostic UART. Linux
`ftdi_sio` on NUC rebinds `/dev/ttyUSB0` after `openFPGALoader`; the obsolete
Mac workflow that required USB replugging or power cycling is not the current
hardware procedure. Keep ESP32 application traffic on AstraHost SPI; this FTDI
UART is an independent POST and bring-up console.

Until software claims front-panel LED ownership, liveness without UART is
`{hb[23], ~AS, ~RW, uart_busy, address[3:0]}`.
