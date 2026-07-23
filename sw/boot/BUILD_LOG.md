# Astra 68 Boot Images

| Build ID | Bitstream SHA-256 | Date | Configuration | Result |
|---|---|---|---|---|
| `0x77B3CDC8` | `56f768b2d78801f6cc93a7c518643f1012e30f48241e0d77be8250f97c1c2755` | 2026-07-23 | Exact reset-corrected K1 Kernel Platform v1, TG68K.C 68030 MMU2, CPU 12.5 MHz, SDRAM 60 MHz, complete graphics/Vesta/OHCI/AstraHost feature set, normal SD ROM CRC32 `EB1B381F` | Zero SCCs; resource and every exact timing gate PASS at 14.179972 MHz CPU and 61.270760 MHz SDRAM or better; 66,513 TRELLIS_COMB, 101 DP16KD, and 18 multipliers; exact normal Musashi/full-RTL and direct/guard panic RTL diagnostics PASS; three SRAM hardware boots, physical K1 HDMI, persistent programming, automatic reset-from-flash K1 boot, and physical direct/guard panic HDMI/log PASS; physical lifecycle soak retains the 7,987-page baseline through cycle 1,000; the 500,000-cycle board run was stopped as an unsuitable O(total-RAM) acceptance workload, and exact normal ROM/firmware/bitstream restoration passes in 2.111 seconds |
| `0x6C0D0CA3` | `61538d09ef255b94206500185b31008fc242004ac954356365e0b9053c88e2d1` | 2026-07-22 | Exact committed Kernel Platform v1 with repaired multi-row Astraea validation, TG68K.C 68030 MMU2, CPU 12.5 MHz, SDRAM 60 MHz, complete graphics/Vesta/OHCI/AstraHost feature set, SD ROM CRC32 `0fd82996` | Zero SCCs; font-ROM, protected-LUT, resource, and every exact timing gate PASS at 13.972139 MHz CPU and 63.403500 MHz SDRAM or better; route-preserving focused and complete graphics diagnostics PASS on ULX3S; four SRAM boots including AstraHost restart/SPI recovery PASS; physical CP437 HDMI shows exact kernel provenance, `K0 ENTRY PASS`, and `KERNEL IDLE`; the identical bitstream is programmed in persistent FPGA flash and its automatic reset boot passes exact identity, full POST/32 MiB BIST, DMA, and kernel entry |
| `0xB1F9E60D` | `05b9e84d2413c9390163a38f77c4d8ad08600a6adb619e69ebb25c56ae0e4eae` | 2026-07-22 | Exact committed Kernel Platform v1, TG68K.C 68030 MMU2, CPU 12.5 MHz, SDRAM 60 MHz, complete graphics/Vesta/OHCI/AstraHost feature set, SD ROM CRC32 `ceafeee9` | Zero SCCs; font-ROM, protected-LUT, resource, and every exact timing gate PASS at 13.646847 MHz CPU and 65.789474 MHz SDRAM or better; three consecutive SRAM reloads reached exact identity, full POST/32 MiB BIST, and `K0 ENTRY PASS`; physical HDMI text confirmation and persistent FPGA flash remain pending |
| `0x60000002` | `ded87a3e3c5daef55d82e280f71d05d8a605be3e1e2135ec59a2a285072dc870` | 2026-07-21 | Full Kernel Platform v1, TG68K.C 68030 MMU2, CPU 12.5 MHz, SDRAM 60 MHz, legal split-route ECP5 LUT permutations, SD ROM CRC32 `b645d379` | Protected-LUT, exact timing, and resource gates PASS; route probe PASS; three SRAM production boots reached complete POST and `K0 ENTRY PASS`, including two independent automated reloads and one gate requiring exact FPGA and SD-ROM identities; persistent FPGA flash deferred pending committed-source rebuild and HDMI confirmation |
| `0x18EBE2E1` | `8dd57df392cde918a7a5f8859d2dbc0fd17a41b5fa9c6528cb7e231c4a16eff8` | 2026-07-13 | TG68K.C 68030 MMU2, ROM v0.3 with UTC/full-Git provenance, CPU 12.5 MHz (`CPU_CLK_DIV_BIT=0`), SDRAM 75 MHz/32 MiB, Astraea DMA, 720x480 HDMI, nextpnr seed 4 | No-SDRAM and pin-level SDRAM simulations PASS; SRAM hardware POST PASS; verified persistent SPI-flash write followed by reset boot POST PASS; UART identity matched version, date, Git revision, and build ID |
| `0xC6FECBC8` | `82d0ac4837752e8af946d7b93291ee22ac7fd0b01d6327a23bbe933403f7b403` | 2026-07-13 | TG68K.C 68030+PMMU, compact margined HDMI POST summary with verbose UART diagnostics, CPU 3.125 MHz (`CPU_CLK_DIV_BIT=2`), SDRAM 75 MHz/32 MiB, Astraea DMA, 720x480 HDMI, nextpnr seed 2 | SRAM hardware PASS followed by persistent SPI-flash write and reset-only flash boot PASS; complete CPU/cache/Astraea/full-range POST in 1.36-1.40 s |
| `0xA0086302` | `a2ff4bd888ab130c00c94822c9b2618489c965a6155a2c7ed4ae2d46a403d8ea` | 2026-07-12 | TG68K.C 68030+PMMU, full-cycle I/D cache hits, one ordered posted write, CPU 12.5 MHz, SDRAM 75 MHz/32 MiB, Astraea DMA, 720x480 HDMI, nextpnr seed 2 | SRAM hardware PASS on 3/3 reconfiguration boots: complete CPU/cache/Astraea/full-range POST in 1.127-1.128 s; 16 KiB pointer loops reached 1.388/1.183 MB/s at 8-bit, 2.777/2.247 MB/s at 16-bit, and 4.542/4.935 MB/s at 32-bit write/read |
| `0x7E17F8DC` | `ef2638b4670ba8b1b6005aa255087ec843cf624ee9f76fe1343a5e8bc414f30b` | 2026-07-12 | TG68K.C 68030+PMMU, CPU 12.5 MHz, SDRAM 75 MHz/32 MiB with read latency 3, Astraea DMA, 720x480 HDMI, nextpnr seed 1 | SRAM hardware PASS on 3/3 cold reconfiguration boots: all CPU width/alignment/address/cache checks, Astraea fill/copy, and four-sweep full-range BIST; 1.122 s per POST; DMA fill 118.36 MB/s, copy 51.40 MB/s |
| `0x7FB5A559` | `f1c5d22955797e4a98e58401627823d59fdc3bd3b16f4bf48e1a104633928075` | 2026-07-12 | TG68K.C 68030+PMMU, CPU 12.5 MHz, SDRAM 75 MHz/32 MiB with read latency 3, 720x480 HDMI, nextpnr seed 1 | SRAM hardware PASS on 5/5 reconfiguration boots: all CPU width/alignment/address/cache checks and four-sweep full-range BIST; 1.098-1.100 s per POST |
| `0xA2F01925` | `e589b417089036fa5790e93f4dd1b1e1c32bf89e544339e319d11c72dafe2ef1` | 2026-07-11 | TG68K.C 68030+PMMU, CPU 12.5 MHz, SDRAM 75 MHz/32 MiB, 720x480 HDMI, nextpnr seed 2 | SRAM hardware PASS: CPU lane/alignment/address tests plus complementary full-range write/verify in 28.703 s; repeating UART build ID matched |

For `0x7E17F8DC`, post-route utilization is 34,779 `TRELLIS_COMB`, 8,288
`TRELLIS_FF`, 136 `DP16KD`, and 13 `MULT18X18D`. Final maximum frequencies are
13.64 MHz CPU, 84.78 MHz SDRAM, 71.14 MHz pixel, and 300.12 MHz TMDS serializer.
Yosys reported zero SCCs and nextpnr completed normally.

For `0xA0086302`, post-route utilization is 36,935 `TRELLIS_COMB`, 9,147
`TRELLIS_FF`, 136 `DP16KD`, and 13 `MULT18X18D`. Final maximum frequencies are
13.16 MHz CPU, 80.13 MHz SDRAM, 78.33 MHz pixel, and 268.67 MHz TMDS serializer.
Yosys reported zero SCCs; nextpnr reported no combinational loops and completed
normally. The complete final-source TG030 core regression passed after 953 ms
of simulated CPU time.

For `0xC6FECBC8`, post-route utilization is 36,856 `TRELLIS_COMB`, 9,147
`TRELLIS_FF`, 136 `DP16KD`, and 13 `MULT18X18D`. Final maximum frequencies are
13.21 MHz CPU, 81.42 MHz SDRAM, 83.07 MHz pixel, and 252.84 MHz TMDS serializer.
Yosys reported zero final SCCs and nextpnr completed normally. This is the
first image in this table written to persistent ULX3S SPI flash; the earlier
images were loaded into SRAM only. The 13.21 MHz route result was timing
capability, not its runtime clock: the divider selected 3.125 MHz, as reported
by the ROM on hardware.

For `0x18EBE2E1`, post-route utilization is 37,971 `TRELLIS_COMB`, 9,396
`TRELLIS_FF`, 136 `DP16KD`, and 13 `MULT18X18D`. Final maximum frequencies are
12.86 MHz CPU, 91.13 MHz SDRAM, 80.41 MHz pixel, and 295.60 MHz TMDS serializer.
Yosys reported zero SCCs before and after ECP5 synthesis, and nextpnr completed
normally. The runtime CPU clock is 12.5 MHz. This is the second image in this
table verified in persistent ULX3S SPI flash.
