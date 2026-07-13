# Astra 68 Boot Images

| Build ID | Bitstream SHA-256 | Date | Configuration | Result |
|---|---|---|---|---|
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

The images were loaded into ULX3S SRAM only. Persistent SPI flash was not
changed.
