# Arty graphics timing closure

This file is the continuation record for timing-sensitive Arty Z7-20 graphics
RTL. A result is retained only when its source identity, tool, device,
constraints, functional regression, routed timing, and disposition are known.

## Authority and method

The implementation method follows AMD's Vivado documentation:

- [UG949, Vivado Design Methodology](https://docs.amd.com/r/en-US/ug949-vivado-design-methodology/Vivado-Design-Suite-User-and-Reference-Guides): pipeline excessive logic levels and diagnose high-fanout control paths from routed reports.
- [UG901, Vivado Synthesis](https://docs.amd.com/r/2022.2-English/ug901-vivado-synthesis/RAM-HDL-Coding-Techniques): preserve the intended synchronous block-RAM and asynchronous distributed-RAM behavior.
- [UG906, Design Analysis and Closure](https://docs.amd.com/r/en-US/ug906-vivado-design-analysis/Analyzing-Specific-Paths): use routed path delay, logic levels, and route contribution rather than placement estimates.
- [Arm AMBA AXI4, IHI 0022H](https://developer.arm.com/documentation/ihi0022/h/): read responses using one AXI ID are returned in issue order. The tile builder relies on that rule for its bounded response-tag queue.

The working rule is: identify an exact failing routed path, predict the
structural effect of one change, run the complete functional tests, and route
once. Seed search is not accepted as an RTL fix.

## HDMI feasibility gate

The originally locked 1920x1080p60 direct-HDMI mode is outside the exact
`XC7Z020-1` characterized clock limits and cannot be a production target on
this board:

| Mode | Pixel clock | TMDS lane rate | 10:1 DDR serializer clock | Disposition |
|---|---:|---:|---:|---|
| 1920x1080p60 | 148.5 MHz | 1.485 Gb/s | 742.5 MHz | Rejected for production qualification |
| 1280x720p60 | 74.25 MHz | 742.5 Mb/s | 371.25 MHz | Selected; transport route and physical HDMI pass |
| 1920x1080p30 | 74.25 MHz | 742.5 Mb/s | 371.25 MHz | Candidate; route and hardware qualification required |

AMD PG230 explicitly gives the 1080p60 line rate and clock. UG471 requires an
OSERDESE2 high-speed clock through BUFIO or a matched MMCM/PLL clock-buffer
arrangement. DS187 gives the `-1` maxima as 600 MHz for BUFIO and 464 MHz for
BUFG, and publishes a fully characterized 950 Mb/s DDR LVDS transmitter result
using OSERDES. It publishes no 1.485 Gb/s SelectIO TMDS production value for
this part. The Arty source connector is wired directly to PL SelectIO.
Digilent's demonstration designs are useful implementation examples, but they
do not supersede AMD's production data-sheet limits.

The product mode is now fixed at 1280x720p60. The integrated transport
checkpoint below qualifies that exact clock, reset, timing-generator, TMDS,
OSERDES, pin, and board-output path. It does not qualify a framebuffer or any
graphics engine.

## Tool and constraints

- Host: `beast`
- Tool: Vivado 2024.2, SW build 5239630
- Device: `xc7z020clg400-1`
- Build clock: 5.000 ns, 200.000 MHz
- Pixel clock: 13.468 ns, 74.250 MHz
- Clock relationship: asynchronous
- Durable artifacts:
  `/mnt/Documents/astra68/builds/arty-720p-20260729/ooc-tile-line/`

The out-of-context clock ports do not have final `HD.CLK_SRC` placement, and
top-level input/output delays are intentionally absent. There is no
pixel-clock register-to-register path in this isolated checkpoint. Therefore
the build-clock paths are valid component evidence, but only an integrated
routed shell can sign off clock insertion, CDC, AXI interconnect, and HDMI I/O.

## Historical OOC functional gate

The exact source used by the retained OOC route passed `run_tests.sh` on Beast.
Coverage included:

- all eight horizontal phases for 8x8 INDEX8 and all sixteen phases for 16x16
  INDEX4;
- signed scroll, independent X/Y wrap, nonwrapped clipping, X/Y reflection,
  transparency, and descriptor validation;
- AXI request and response stalls, AXI response failure, malformed
  descriptors, and invalid configuration;
- maximum 241-span and 121-span walker cases with backpressure;
- the 1280-pixel performance case in 1,338 build clocks against the
  4,444-clock line deadline, leaving 3,106 clocks of measured margin.

That historical test required exactly 1,288 map bytes and 1,288 pattern bytes
in the performance case. A line slot was published only after all reads and
writes completed successfully. At 200 MHz the measured build took 6.69
microseconds inside a 22.22-microsecond 720p line period. The integrated
checkpoint below supersedes this functional result with its final source.

## Tile-line route history

All entries below use the same device and exact 5.000 ns build constraint.

| Checkpoint | WNS | TNS | Failing endpoints | Measured limiting cone | Disposition |
|---|---:|---:|---:|---|---|
| Initial complete builder | -1.173 ns | -273.821 ns | 693 | 12-bit compositor span-end arithmetic | Replaced with a bounded five-bit pixel countdown. |
| `ooc-line-countdown` | -0.981 ns | -33.060 ns | 164 | Pattern-address arithmetic | Registered descriptor index/row before address generation. |
| `ooc-line-address-pipeline` | -0.971 ns | -13.878 ns | 81 | Active tag through asynchronous FIFO into descriptor write | Separated the active response tag from the waiting ring. |
| `ooc-line-active-tag` | -0.386 ns | -2.204 ns | 21 | Variable pattern-byte select through palette/transparency logic | Added a registered byte-selection stage. |
| `ooc-line-byte-stage` | -0.112 ns | -0.213 ns | 9 | Tag queue head update/control | Isolated the active-head register update. |
| `ooc-line-head-enable` | -0.098 ns | -0.187 ns | 7 | Tile-X to pixels-to-edge/final-span selection to screen-X advance | Registered the bounded span advance. |
| `ooc-line-registered-advance` | +0.005 ns | 0.000 ns | 0 | Five levels from active tag head to descriptor RAM address | Historical 1080-line component checkpoint. |
| `ooc-tile-line` 720p retarget | **-0.085 ns** | **-0.129 ns** | **2** | Active tag head to descriptor RAM input; raw tile Y to map-byte offset | Carry into full PS/AXI integration as explicit timing risk. |

The retained OOC checkpoint has +0.027 ns worst hold slack and zero hold
failures. Resource use is:

| Resource | Used | XC7Z020 capacity | Percent |
|---|---:|---:|---:|
| Slice LUTs | 1,794 | 53,200 | 3.37% |
| Slice registers | 1,428 | 106,400 | 1.34% |
| BRAM36 | 6 | 140 | 4.29% |
| DSP48 | 0 | 220 | 0.00% |

The worst setup path has 4.666 ns data delay: 1.874 ns logic and 2.792 ns
routing from the replicated active tag head to a descriptor RAM input. The
second failing path has 5.022 ns data delay from raw tile Y to a map-byte
offset register. The OOC shell intentionally omits final external I/O delays
and real PS clock placement. Its methodology report contains 497 expected
`TIMING-18` I/O-delay warnings, two distributed-RAM mapping warnings, and six
RAM output-register warnings. The 85 ps miss is retained as a real integration
risk, not waived.
The full PS/AXI route below subsequently closes this component gate.

## Exact source identity

The routed OOC source snapshot had these SHA-256 values. The integrated source
manifest below supersedes them; they are not claims about the current files:

| File | SHA-256 |
|---|---|
| `astra_tile_span_walker.sv` | `23d5934c7c7f78f32fdf0dddfc6f934ae6a854289ae6e14903b145f90ca649a6` |
| `astra_tile_line_store.sv` | `08852e456e65b64f9f904c6f78bed3e92e17f50b07e5033fd92cf6bb429c7140` |
| `astra_tile_line_builder.sv` | `fad8b80826cd2dcc6b134fda764249a45a43bf2e2d4b5200a2e5b2024836352f` |
| `synth_tile_line_ooc.tcl` | `cf42c19babd060dd8bfaaf4a471a68ab8e574d890266b0830942b9022618b8d6` |
| `tile_line_ooc.xdc` | `562d8aad3a3dce8955b50ebd271f119d62024977f57ef7543836c89d13895da0` |
| `tb_astra_tile_line_builder.sv` | `67c2089b2e5875d81b069760632527c2b5f4b8f45e1e931babc5768669e96dfc` |
| `tb_astra_tile_line_builder_perf.sv` | `3005f9181212174916733e0988450ec537ac949163f86a630ad1ee68688f2773` |

## Integrated 720p transport checkpoint

The fixed shell combines the Zynq PS, 100 MHz FCLK0, exact MMCM clocking,
720p timing generator, TMDS encoder, OSERDESE2 serializers, output pins, reset
synchronization, and a deterministic test raster. The HDMI encoder is the
MIT/Apache-licensed `hdl-util-hdmi` snapshot at commit
`fbade3d11a58b885a6084ec75eae25339623355d`.

The MMCM uses a 37.125 feedback multiplier, divide-by-five input path, and a
742.5 MHz VCO. Dividers two and ten produce 371.25 MHz and 74.25 MHz exactly.
The final reset structure asserts asynchronously from the MMCM `LOCKED` output
and releases through a four-cycle pixel-clock synchronizer; no LUT drives the
asynchronous reset pins.

Beast Vivado 2024.2 fully routes the exact shell with these results:

| Gate | Result |
|---|---:|
| WNS / TNS | +5.393 ns / 0.000 ns |
| WHS / THS | +0.160 ns / 0.000 ns |
| Pulse-width slack | +0.538 ns |
| Failing setup / hold / pulse endpoints | 0 / 0 / 0 |
| Routable nets | 447 / 447 complete; 0 errors |
| Methodology findings | 0 |

Resource use is 202 LUTs, 102 registers, no BRAM, no DSPs, four BUFGs, one
MMCM, and eight OSERDESE2s. Exact identity:

| Artifact | SHA-256 |
|---|---|
| `astra_arty_720p_top.sv` | `1be8c50bbf5e319e0fff94679c7f9295642a5671aca257fa16d8fc76b5f0eae5` |
| `astra_720p_pattern.sv` | `2a7223163aaa3c47a29cb8fd2ba489200a79387be182860c0c8c2d227b2ec570` |
| `astra_arty_720p.xdc` | `901db9f8a7ddb847c47cdae2a79d07ee65ae47777b9d9e7680ae526f05dcb916` |
| `build_720p.tcl` | `881d780768c69e429f09eb135c7264f2f4c93621b6d7271b14a017f90217b982` |
| `astra_arty_720p.bit` | `f8db5c827b32f202500a201e7d8ba4f01e21cdbc55259d867bbeb8c45a1e778a` |
| `timing_summary.rpt` | `19fccede5a4b614f0be2f2e7a34d4dad00288d20482c152a2a2b7a3c9ba81f42` |
| `utilization.rpt` | `d3425b47fb8505d787ae199a05a606b51706decf7fe332e42fe3b37111a4482f` |
| `methodology.rpt` | `a2edb629ed55b8d91c12fac98cd3c79e22e261c61209f83135fa85c590cd6d1b` |
| `route_status.rpt` | `083e9cc33d111b1024357fe810064fea65df3489f6ecd4ea85c284e1ed3a325a` |
| `clock_utilization.rpt` | `82fc1ddf455690e7523a2e8daa082ae4a292d1ec8b68bdce7fdb4c595f5f6351` |

The exact bitstream boots on the Arty and produces the stable full-frame raster
in `docs/evidence/astra-arty-720p60-hdmi-20260729.png`, SHA-256
`a5ca652d6cbc075b018f0b7f4f08d414f9ebbac6edaf81460d4fc3b8f1d3f12d`.
This closes the transport checkpoint.

## Integrated PS/DDR graphics checkpoint

Checkpoint `full8` replaces the pattern generator with the complete first
scanout datapath. It contains the Zynq PS, GP0 AXI4-Lite control path, three
independent 64-bit HP read paths, RGB565/INDEX8/XRGB8888 framebuffer builder,
two INDEX4/INDEX8 tile builders, palettes, ordered compositor, four-line
scheduler, frame-boundary scene promotion, counters, and the qualified HDMI
transport. The exact source snapshot is
`/mnt/Documents/astra68/snapshots/arty-graphics-20260729-full2/source`; the
26-file source manifest is
`docs/evidence/astra-arty-graphics-full8-source-manifest-20260730.sha256`,
SHA-256
`11e8e6de0f889d21a780ffc84db73142bd85f3fe07593ddfa0c34240cc061ea8`.

The final source passes all seven directed graphics simulations on Beast. The
tests cover tile span and line construction, framebuffer formats and byte
order, 4 KiB AXI boundaries, independent X/Y wrap and clipping, palette and
alpha composition, scheduler failure/recovery, validated atomic control-plane
promotion, and the integrated pipeline. The worst 1280-pixel INDEX8 tile line
takes 1,346 clocks against the 4,444-clock deadline, leaving 3,098 clocks or
15.49 microseconds at 200 MHz. The retained 2,973-byte test log is
`full8/functional-tests.log`, SHA-256
`2b6faa34afcc170bd9f4372436e01617aca28033be764c79d3665b9f4ced0670`.

Beast Vivado 2024.2 fully routes the exact `xc7z020clg400-1` design with the
`Performance_ExplorePostRoutePhysOpt` strategy:

| Gate | Result |
|---|---:|
| Overall WNS / TNS | +0.049 ns / 0.000 ns |
| Overall WHS / THS | +0.016 ns / 0.000 ns |
| Pulse-width slack | +0.538 ns |
| 200 MHz build-domain setup / hold | +0.049 ns / +0.016 ns |
| 74.25 MHz pixel-domain setup / hold | +3.019 ns / +0.022 ns |
| Failing setup / hold / pulse endpoints | 0 / 0 / 0 |
| Routable nets | 22,716 / 22,716 complete; 0 errors |

This closes the isolated tile builder's 85 ps setup miss in the actual PS/AXI
placement. Resource use is:

| Resource | Used | XC7Z020 capacity | Percent | Free |
|---|---:|---:|---:|---:|
| Slice LUTs, total | 12,485 | 53,200 | 23.47% | 40,715 |
| LUT as logic | 10,731 | 53,200 | 20.17% | 42,469 |
| LUT as memory | 1,754 | 17,400 | 10.08% | 15,646 |
| Slice registers | 12,677 | 106,400 | 11.91% | 93,723 |
| BRAM36-equivalent tiles | 29.5 | 140 | 21.07% | 110.5 |
| DSP48 | 5 | 220 | 2.27% | 215 |

The methodology report contains 87 warnings and no critical warning: four
intentional timing-driven distributed RAM mappings, 28 unmerged RAM output
register advisories, 50 small inferred multipliers, four external-port delay
advisories, and one generated-clock reference advisory. The CDC report records
85 recognized single-bit synchronizers and eight multi-bit synchronized buses;
the latter are the frame handoff payloads paired with stable valid/toggle
state. These findings remain explicit regression items as the remaining
engines are integrated.

Durable artifacts are under
`/mnt/Documents/astra68/builds/arty-graphics-20260729/full8`:

| Artifact | SHA-256 |
|---|---|
| Generated PS block design | `f87acdc4624dec66b259d58ad2f2c4372f2c874fd021c9ab953954ea9872e667` |
| Routed DCP | `28b7674ff70a29fd312e3f35c02800f42d1392c8d8eb00589292a8ab5dfe54ce` |
| Timing report | `e745e5580471959ec5dd15ece097605757a7cc4b9123e223ab9c98fba324d71a` |
| Utilization report | `0ce2fc30f70c53514a7928cbc895dd35b140cb2682af12212bd7dabcc27f87a9` |
| Methodology report | `751573f0c4509de33c98438a63b5f6de5a9fab19a0f251519411a0973dbeb7f8` |
| Route-status report | `3120655d2edf50e104e24ac0085df5938018a6904a1129cd7da16f3bec509edb` |
| CDC report | `dec8751e22b5e1ccbc2bfc0108feeb752773db14506239df2fac47217d24e489` |
| Bitstream | `6c278fbec92dcf89805501c4e4e1703956e845c12c89cc9fbd5d2fbf9233c7df` |
| XSA | `006873fa1424cad6b25c5961c07b09b4019ee834a98a19696ab946ad68743ef8` |

The exact XSA generated a fresh FSBL with 100 MHz FCLK0 and 200 MHz FCLK1.
The new device tree removes the retired Nova reservations and reserves
`0x18000000..0x1fffffff` as a 128 MiB `no-map` graphics arena. The release
retains the existing Linux kernel and U-Boot, and installs the exact RGB565
boot splash. Active hardware evidence is:

| Gate | Result |
|---|---|
| `BOOT.BIN` | `245d1ae71b82d2eecc9f0e1326c56e2ae193520f87958e6d6c09de1f3c4b7efd` |
| FIT `image.ub` | `e9ef016f059cb3bc71138edf2a5ae47646a0e11b3dab3b81f7362f592b97b542` |
| Device tree | `dc04732176732a9fa6fa4bb1293bc64d877308d80f56403c3f5dbf720cd22147` |
| Static ARM loader | `0a1194cd9727f796d10ed9ba2c04c395cec266b5b6a9698cba90a734f4e19d24` |
| Source PNG | `b491053d6fa08c0e0dba248db43be4e3a475df69a719c136909a987ed5f93a31` |
| RGB565 splash | `4ee690d8a421716043f209afcff28b09bf9e67880c0ccc4fc322e704d93f6293` |
| Loader readback | 1,843,200 bytes, CRC32 `8db14556`, pass |
| Graphics control | generation 1 active, capabilities `0x0000003f`, zero deferrals |
| Linux ownership | System RAM ends at `0x17ffffff`; arena excluded |
| FPGA manager | `operating` |

The retained UART transcript is
`docs/evidence/astra-arty-graphics-full8-boot-20260730.log`, SHA-256
`5e961f8298d7fa69d187a925d83c9b8042fb459b4fc7d487f67cd8b4588ec600`.
It proves the exact FIT hashes, reserved arena, read-only root, writable data
volume, first-boot loader completion, network, and login. Physical HDMI shows
the exact supplied splash without corruption. Retained evidence
`docs/evidence/astra-arty-graphics-full8-splash-hdmi-20260730.png` has SHA-256
`c687f3c0f577e61cc8523a8bbcfd7c7b21f7848f30a512ba29a20544a1782cf8`.
This closes and promotes the `full8` checkpoint.

## Integrated dynamic boot-text checkpoint

Checkpoint `boot-text6` adds a boot-only hardware text plane over the blank
1280x720 splash. It contains 36 columns by four rows of CP437 cells. Each cell
selects one of four fixed boot colors, and the checked-in 8x8 CP437 font is
expanded to 16x16 pixels. The ARM writes an inactive cell bank through a
bundled-data toggle mailbox. A commit swaps banks only at vertical blank and
then clones the visible bank into the new shadow bank, so a later one-row
update preserves the other rows without tearing.

The exact source snapshot is
`/mnt/Documents/astra68/snapshots/arty-graphics-20260730-boot-text6/source`.
Its 28-file build manifest is
`docs/evidence/astra-arty-boot-text6-source-manifest-20260730.sha256`, SHA-256
`86ce3c1f474331877548b9c4531d1e2b7d07ea88c0de7e3100e3cee5268bac5b`.
All nine programs in `graphics/run_tests.sh` pass on Beast, including the
standalone boot plane, mailbox backpressure, shadow isolation, vblank-only
promotion, bank cloning, CP437/color rendering, AXI4-Lite control behavior,
registered-read response stability under backpressure, rejection of a second
outstanding read, and the complete integrated scanout pipeline. The static ARM
loader and live status utility build with strict warnings, pass GCC
`-fanalyzer`, and pass the host formatter/geometry tests.

The route history is retained because each failed result identified a distinct
structural issue:

| Checkpoint | WNS | WHS | Measured limiting cone | Disposition |
|---|---:|---:|---|---|
| `boot-text` | not routed | n/a | Two cell banks inferred as 4,608 flip-flops because both banks had multiple procedural write paths. | Rewrote each bank with one write port; synthesis-only checkpoint rejected. |
| `boot-text2` | -0.401 ns | +0.021 ns | Pixel coordinate through cell RAM and font ROM to HDMI, 15 logic levels. | Added a registered cell-RAM stage. |
| `boot-text3` | -0.412 ns | +0.051 ns | Cell RAM through font ROM to glyph visibility, 13 logic levels. | Added a second font stage and used the documented two-pixel HDMI lookahead. |
| `boot-text4` | -0.624 ns | +0.016 ns | Pixel path passed by +2.343 ns; a 32-bit saturating boot-cell selector formed four CARRY4s in the 200 MHz domain. | Narrowed the selector to the required eight bits while preserving zero-extended MMIO readback. |
| `boot-text5` | -0.061 ns | +0.009 ns | Nine GP0 `ARADDR` endpoints crossed six LUT/MUX levels directly into the 32-bit control readback register; worst data path 4.840 ns. | Registered the AXI read address, adding one control-read cycle without changing the ABI. |
| `boot-text6` | **+0.002 ns** | **+0.019 ns** | Final worst path is timing-clean after post-route physical optimization. | Accepted for hardware boot. |

Beast Vivado 2024.2 fully routes the exact `xc7z020clg400-1` design using
`Performance_ExplorePostRoutePhysOpt`:

| Gate | Result |
|---|---:|
| Overall WNS / TNS | +0.002 ns / 0.000 ns |
| Overall WHS / THS | +0.019 ns / 0.000 ns |
| Pulse-width slack | +0.538 ns |
| 200 MHz build-domain setup / hold | +0.002 ns / +0.019 ns |
| 74.25 MHz pixel-domain setup / hold | +0.003 ns / +0.035 ns |
| Failing setup / hold / pulse endpoints | 0 / 0 / 0 |
| Routable nets | 23,261 / 23,261 complete; 0 errors |

Resource use is 13,096 total LUTs, 12,892 registers, 29.5 BRAM36-equivalent
tiles, and five DSPs. Relative to `full8`, the complete dynamic boot plane and
control path add 611 LUTs and 215 registers; BRAM and DSP use are unchanged.
The two cell banks and font remain distributed RAM: LUT-memory use increases
from 1,754 to 1,843 rather than becoming thousands of flip-flops.

The methodology report retains the same 87 noncritical advisories as `full8`:
four timing-selected distributed RAM mappings, 28 RAM output-register
advisories, 50 small multipliers, four LED output-delay advisories, and one
generated-clock naming advisory. The CDC report recognizes 91 single-bit
synchronizers and reports ten multi-bit stable payload buses; the added cell
index/data buses are held constant from request until mailbox acknowledgement.
Directed tests exercise backpressure and prove that a bank cannot be promoted
until all requested writes complete.

Durable artifacts are under
`/mnt/Documents/astra68/builds/arty-graphics-20260730/boot-text6`:

| Artifact | SHA-256 |
|---|---|
| Routed DCP | `0f6597027d0186f981d2d5d33494407a3ba7ecb8e39ac2b21916ed35a16b26eb` |
| Timing report | `c4450241e36e96eae1ff994cf4c3709abbab86e372460ef9d899171e92901817` |
| Utilization report | `30b6e16abbcb15ae8cb2a1fd2cdf5a280549f38b7861ae8daf222a4ea4397812` |
| Methodology report | `fc3abfa8ae304c8e3e1e6a9795936d17e030cb539365f28dc66e8e431d53a1aa` |
| Route-status report | `0d2a77bedbec181f5d44dada462edfc831ac98a0873f745d6915457f4cde1963` |
| CDC report | `1fc64e005d58976da38809795b5450322f2ac2188a41c681a130fac2d28a6ee7` |
| Bitstream | `869b0b4917135486376ab868f5599963dced75a2f8cfa76b2261fe01d0439cf4` |
| XSA | `043cc136ff20e395be77d1e25de42bd414bb8d3248df96b7b78d378ee99c4951` |

The exact XSA generated a fresh FSBL and boot package. After an unattended
reboot the board reports FPGA state `operating`, preserves read-only `/` and
writable `/data`, reads back all 1,843,200 splash bytes with CRC32 `611029ee`,
and advances boot-text generation to two. A subsequent row-only hardware
update succeeds and advances generation to three while status remains
write-ready, commit-ready, and active (`0x00000007`). Active release hashes
are:

| Artifact | SHA-256 |
|---|---|
| `BOOT.BIN` | `c1f27b95741d2e0b0208c936d30287b6aa6e3d77a461a1d90f2fa18e9ba0831f` |
| FIT `image.ub` | `e9ef016f059cb3bc71138edf2a5ae47646a0e11b3dab3b81f7362f592b97b542` |
| Static ARM loader | `cd66124f9fc64a62db4c59dcbe37d2fb4b9ff98486851caa42eaf0f27bf6c2e8` |
| Live status utility | `13ea40e954610e10437ce8a96c223e1e24266805a641b5a958f1381bc135aa3f` |
| Blank RGB565 splash | `86eb30739db77b85f4deb1915fb9cb9263ab4755ae318ffb1b7a4a95b7017ba4` |

The retained machine-readable hardware transcript is
`docs/evidence/astra-arty-boot-text6-hardware-20260730.log`, SHA-256
`225a1687fca361288de748fb7f705f74f54e6b2136d3ad7da8da13f4e1982d0a`.
Direct monitor confirmation passes: all four dynamic rows are visible,
correctly colored, aligned inside the lower panel, and clear of the Astra OS
badge. The retained frame is
`docs/evidence/astra-arty-boot-text6-hdmi-20260730.png`, SHA-256
`e2c00ecb090a4ac6eb5e93a48cf5562976e8ff1868932552672e9f877c13d0ae`.
No capture device was enumerated on the Mac or Beast, so the frame came from
the user's direct monitor observation. It establishes visible output; the
machine-readable transcript separately establishes internal state.

## Integrated 64-sprite timing checkpoint

Checkpoint `sprite64-full2` integrates all 64 128x128 INDEX8 sprites, sixteen
independent 256-entry ARGB palette banks, front/behind sprite planes,
collision reporting, descriptor validation and atomic scene promotion into
the complete `boot-text6` production design. The exact source snapshot is
`/mnt/Documents/astra68/snapshots/arty-graphics-20260730-sprite64-23/source`.
Its source manifest is
`docs/evidence/astra-arty-sprite64-full2-source-manifest-20260730.sha256`,
SHA-256
`133fdd1ea9b7617b7fb17aae8c721e1113d7a033e4f17603f1faf433ffa03ab0`.

The complete directed suite passes on Beast. It includes exact scaling over
131,072 source/destination pairs; scene-store cloning and promotion;
premultiplied blending; clipping, palette-bank, alpha and ordering cases; all
64 maximum-size sprites; deadline, overflow and AXI-error behavior; all-pairs
collision reporting; and the existing framebuffer, tile, compositor,
scheduler, control and boot-text regressions. The maximum 64-way collision
case completes in 4,087 of 4,300 build clocks, leaving 213 clocks of margin.
Out-of-context Vivado 2024.2 routes the scene store at +0.207 ns setup and the
sprite line builder at +0.048 ns setup / +0.027 ns hold at 200 MHz.

The full `xc7z020clg400-1` production design routes all 42,118 routable nets
with no routing errors and passes hold, but it does not yet pass setup:

| Gate | Result |
|---|---:|
| Overall WNS / TNS | -0.266 ns / -11.919 ns |
| Overall WHS / THS | +0.006 ns / 0.000 ns |
| 200 MHz failing setup endpoints | 119 |
| Routable nets | 42,118 / 42,118 complete; 0 errors |

Post-route physical optimization recovered 0.400 ns WNS and 11.222 ns TNS
from the initially routed result. A checkpoint query reporting every negative
path, rather than the standard top 50, gives this complete endpoint census:

| Measured cone | Failing endpoints | Worst observed slack |
|---|---:|---:|
| AXI control read/write response | 33 | -0.266 ns |
| Sprite preparation/control distribution | 24 | -0.219 ns |
| Working-line BRAM to published-line BRAM | 23 | -0.214 ns |
| Scheduler queued-line launch | 22 | -0.109 ns |
| Sprite scene transfer | 9 | -0.195 ns |
| Sprite blend output | 4 | -0.118 ns |
| Framebuffer deadline control | 2 | -0.213 ns |
| Generated PS AXI path | 1 | -0.017 ns |
| Residual hidden endpoint | 1 | -0.001 ns |

The worst control path is six logic levels from captured write data through
the address-specific validation mux to `BRESP`; the read paths are six levels
from captured `ARADDR` through the expanded register mux to `RDATA`. The
sprite preparation failures are route-dominated state decodes into the HP AXI
interface. The line-publication failures are direct synchronous working-BRAM
outputs feeding the second BRAM boundary. These are structural register-boundary
problems, not clock-chain or routing-completeness failures.

Resource use at this rejected checkpoint is 22,045 total LUTs, 22,651
registers, 85.5 BRAM36-equivalent tiles and 51 DSPs. Relative to `boot-text6`,
the complete sprite subsystem adds 8,949 LUTs, 9,759 registers, 56
BRAM36-equivalent tiles and 46 DSPs. The result is retained for diagnosis but
must not be packaged or installed as a release.

Durable artifacts are under
`/mnt/Documents/astra68/builds/arty-graphics-20260730/sprite64-full2`:

| Artifact | SHA-256 |
|---|---|
| Timing summary | `6e31e7544c9bb64086c48f64a94a1968011f823e8bb7b49cefee0211bc9f07ed` |
| All failing paths | `4e4ac080c4788e6c1ffaece4bd3d67e76235dfb26422ff4d8b0974b69e77b5fc` |
| Utilization report | `64eab65ac18eef5d8e9d4236bdd157ebf7e4120a0aa1d4c9f84ecb42e194163e` |
| Methodology report | `d4ddd3bc729811bde90bd8369ea874995cae5460fe0ed64276f8c4443b7a0aaf` |
| Route-status report | `4519dc106346d038985fbd55e121ab778c41433244fa42499ba95b2d25f8dd4c` |

### Structural timing corrections after `sprite64-full2`

Snapshot `sprite64-26` retains the qualified behavior while inserting or
preserving register boundaries at every measured failing cone. The exact
source is
`/mnt/Documents/astra68/snapshots/arty-graphics-20260730-sprite64-26/source`.
Its 100-file manifest is
`docs/evidence/astra-arty-sprite64-full3-source-manifest-20260730.sha256`,
SHA-256
`923dcbc8f79988bac0712ea13ee422c3ebd7542b26683dbdceb2024138a4838e`.

The retained changes register static AXI write validation and bank the AXI
read mux, register the sprite HP read-channel controls, split working-line and
published-line BRAM transfers, stage queued scheduler launches, continuously
capture scene-transfer payloads, and replace monotonic deadline comparators
with exact terminal-count comparisons. The premultiplied blend keeps the
effective-alpha divide and channel multipliers on opposite sides of the
existing `alpha_q` pipeline boundary. Without that preservation Vivado
retimed the two DSP operations into one 4.482 ns data path and the isolated
block missed setup by 0.016 ns at six endpoints.

The complete directed suite passes unchanged on Beast. Maximum-size 64-sprite
rendering completes in 4,079 of 4,300 build clocks, and all-pairs 64-way
collision rendering completes in 4,152 clocks, leaving 221 and 148 clocks of
margin respectively. The exact routed out-of-context measurements at 200 MHz
are:

| Block | Setup slack | Hold slack | Routed nets | Result |
|---|---:|---:|---:|---|
| AXI graphics control | +0.322 ns | nonnegative | complete | Pass |
| Sprite scene store | +0.241 ns | +0.039 ns | complete | Pass |
| Sprite line builder | +0.054 ns | +0.012 ns | complete | Pass |

Out-of-context timing is structural evidence only. Snapshot `sprite64-26`
is not releasable until the exact complete design passes setup and hold and
then passes the board-level boot and visible-sprite gates.

### Timing-clean integrated 64-sprite candidate

Exact full-system checkpoint `sprite64-full3`, built from immutable snapshot
`sprite64-26` on Beast with Vivado 2024.2 and
`Performance_ExplorePostRoutePhysOpt`, meets every timing constraint:

| Gate | Result |
|---|---:|
| Overall WNS / TNS | +0.029 ns / 0.000 ns |
| Overall WHS / THS | +0.014 ns / 0.000 ns |
| Pulse-width slack | +0.538 ns |
| 200 MHz build-domain setup / hold | +0.029 ns / +0.014 ns |
| 74.25 MHz pixel-domain setup / hold | +0.280 ns / +0.070 ns |
| Failing setup / hold / pulse endpoints | 0 / 0 / 0 |
| Routable nets | 41,485 / 41,485 complete; 0 errors |

Resource use is 21,786 total LUTs, 22,763 registers, 85.5
BRAM36-equivalent tiles and 51 DSPs. Relative to the hardware-qualified
`boot-text6` base, the complete sprite subsystem adds 8,690 LUTs, 9,871
registers, 56 BRAM36-equivalent tiles and 46 DSPs. Relative to rejected
`sprite64-full2`, the structural timing corrections remove 259 LUTs while
adding 112 registers; BRAM and DSP use are unchanged.

The methodology report contains 203 advisories and no errors: four
timing-selected distributed RAM mappings, 84 unregistered RAM-output
advisories, 110 small multipliers, four board-output delay advisories and one
generated-clock naming advisory. The CDC report recognizes 92 single-bit
synchronizers and retains ten stable multi-bit mailbox payloads. These are
the same reviewed classes used by the qualified scanout design, extended by
the sprite memories and arithmetic.

Durable artifacts are under
`/mnt/Documents/astra68/builds/arty-graphics-20260730/sprite64-full3`:

| Artifact | SHA-256 |
|---|---|
| Routed DCP | `97a27a76fac07c0576a96f9f32ce0c9e9127eae1530debaae029d73489b5ac1d` |
| Timing report | `04734c20d6973e1f4b9498df26156721d88655687b97882781d4e54ed1900fbb` |
| Utilization report | `72cf255aaf4561085651d1a969c7a9c522494da47086e152b95538605c203d67` |
| Methodology report | `c567c039ad789b7bf87e0300bf3e976f970784c35d68c4e2c669201c2900a4fe` |
| Route-status report | `c404cacb972b12dc5ed3ecdc070c8f3f7a08be0effb65c85e80c9671c5c987ce` |
| CDC report | `bdf51fbd6145fa87722acf21a31d65028c4fc9cdaa88042cc5bce8173925d278` |
| Bitstream | `f8bbf708311961de4ea76b4955e6dcdb6998b6ffdb5fee110fc537c729ef8884` |
| XSA | `b73424e428b1ebf067c002dd65e05fb052b02048b97f960143c91ebb698ca0ce` |

This is a timing-clean release candidate, not yet a hardware-qualified
release. Packaging, unattended board reboot, DDR/readback checks, regression
of the existing splash and boot text, and a visible 64-sprite scene remain.

### Hardware-qualified line-publication correction

Physical testing of the first packaged sprite build exposed intermittent
scanline-wide flicker affecting multiple sprites on the same row. Sprite DDR,
deadline, overflow, drop and AXI-error counters remained zero. The exact routed
CDC report identified the remaining scheduler publication mailbox: each line
slot's tag and success payload was synchronized independently from its toggle,
and the pixel domain captured both in the cycle it first observed the toggle.
That allowed one stale tag to reject an otherwise complete line for the whole
scanline.

Immutable RTL snapshot
`/mnt/Documents/astra68/snapshots/arty-graphics-20260731-sprite64-38/source`
adds one pixel-clock capture-delay state per slot. Toggle observation arms the
capture; the already synchronized stable payload is consumed on the following
pixel clock. Scene-epoch invalidation clears pending captures and no longer
publishes a second slot toggle that could coalesce with a real line completion.
The CDC report still identifies the bundled multi-bit payloads, as expected;
their ordering is now an explicit tested protocol. Its source manifest is
`docs/evidence/astra-arty-sprite64-cdc-rtl-source-manifest-20260731.sha256`,
SHA-256
`a5aa5d63dc3ec223a289055af64254c0ac9940d6ddfbf368d7f81e5e3ad4a3b7`.

The complete directed suite passes on Beast. A deterministic scheduler test
forces a publication toggle one pixel clock before its tag, proves that the
stale tag is not captured, then presents the stable payload and requires the
correct line. Sprite-line tests also prove fully off-screen descriptors on the
left, right, top and bottom issue no source reads and admit no pixels. Existing
coverage remains intact, including all 131,072 scale pairs, 64 maximum-size
sprites, overflow, AXI-error and deadline containment, 4 KiB splits, all-pairs
collision, composition, control, scheduling and the complete graphics
pipeline. The maximum 64-way render and collision cases complete in 3,810 and
3,868 build clocks respectively.

Exact full-system checkpoint `sprite64-cdc-full2`, built from that snapshot on
Beast with Vivado 2024.2, meets every production constraint:

| Gate | Result |
|---|---:|
| Overall WNS / TNS | +0.024 ns / 0.000 ns |
| Overall WHS / THS | +0.034 ns / 0.000 ns |
| Pulse-width slack | +0.538 ns |
| Failing setup / hold / pulse endpoints | 0 / 0 / 0 |
| Routable nets | 41,778 / 41,778 complete; 0 errors |

Resource use is 21,954 total LUTs, comprising 17,454 logic LUTs and
4,500 memory LUTs, plus 23,003 registers, 85.5 BRAM36-equivalent tiles and
51 DSPs. Durable artifacts are under
`/mnt/Documents/astra68/builds/arty-graphics-20260731/sprite64-cdc-full2`:

| Artifact | SHA-256 |
|---|---|
| Routed DCP | `fc62049a79e5888be3f647ab18f316e91cdbe0a5b4e857b200e297df06838af8` |
| Timing report | `0c4e0ee95dccf8348d899395bcf6ec9760b8fd72389f0d302e31d921874c68e3` |
| Utilization report | `aceed68e0b3c0fc35c2627066d4ec4d01d6abc2e5eb5070ec387ef508e019599` |
| Methodology report | `6dd6ac3a7049bdfdbcb34451a29aa1a6710ed51b3ce90b00149924dd299850a2` |
| Route-status report | `8734e3c678ccad9a9bb4790736da0056f7d68ea4945ccfbd8785dd6bdd36bdce` |
| CDC report | `8df3ee0c989c7caf023ed22826a973531b4e9447dfbf6cd04fbe0c01f187d6bd` |
| Bitstream | `4b1cea2a4c97b96c6fda0d04d883c16f118cec95e35249a171020ee4e33380b2` |
| XSA | `85d17400e1bd2ce27bd43abbc0c9fa6e8d4bae74554b3e5390a4af1733117739` |

The exact XSA generated FSBL
`21006b31dced9f430859d69155f56c28d1398d0239ae406ea7b7a8c6b459e17c`.
Active `BOOT.BIN` is
`b88b142cc4624ea70dafc65b0aec900d506bcf17f90fc1c7ea6f5f834d8098a5`;
the unchanged FIT remains
`e9ef016f059cb3bc71138edf2a5ae47646a0e11b3dab3b81f7362f592b97b542`.
The board boots with FPGA manager `operating`, read-only `/`, writable `/data`,
and the exact 1,843,200-byte splash readback CRC32 `611029ee`.

The first off-screen hardware run also found a software measurement error:
counter baselines were taken before scene promotion, so the handoff included
one old 64x128x720 frame, exactly 5,898,240 admitted pixels. Final software
snapshot `sprite64-39` moves baselines after the active-generation
acknowledgment and passes strict cross-compilation, GCC static analysis and the
host test. Its manifest is
`docs/evidence/astra-arty-sprite64-cdc-source-manifest-20260731.sha256`,
SHA-256
`afd974484f28822bf2184054410716cc66dbf556c61fb0ca315c1260ff440289`.

Hardware certification passes the 64-way stress, fully off-screen, clipped and
aligned-grid scenes with zero dropped pixels, overflow, AXI errors or deadline
errors. The hidden scene reports zero reads and zero admitted pixels. Direct
HDMI inspection confirms all 64 aligned sprites remain stable with no
scanline flicker. The exact transcript is
`docs/evidence/astra-arty-sprite64-cdc-hardware-20260731.log`; the retained
frame is `docs/evidence/astra-arty-sprite64-cdc-hdmi-20260731.png`, SHA-256
`3c1702cd31ecfd0e4beaddcc80797f8ed5192f510a7a7820c2354af4bc99e0ee`.
This closes and promotes the 64-sprite checkpoint.

### Rejected command-processor/basic-blitter route

Checkpoint `basic-blitter-route-1` integrates the bounded DDR submission and
completion rings, descriptor validation, timeout/reset handling, the shared
pixel writer, and basic clipped fill plus overlap-safe same-format copy. The
surface-validator, writer, blitter, exhaustive 24-command processor, control,
and complete graphics-pipeline simulations pass. The integrated pipeline test
programs the engine through MMIO and observes the exact fill through the AXI
memory model. Linux cross-build, GCC `-fanalyzer`, and host tests also pass for
the renderer certification utility.

The exact full-system Beast Vivado 2024.2 route completed all 54,974 routable
nets and generated a bitstream, but is rejected because setup timing fails:

| Gate | Result |
|---|---:|
| Overall WNS / TNS | -7.828 ns / -65,764.492 ns |
| Overall WHS / THS | +0.006 ns / 0.000 ns |
| Failing setup endpoints | 33,225 |
| 74.25 MHz pixel-domain setup | +0.252 ns |
| Routable nets | 54,974 / 54,974 complete; 0 errors |

The exact worst path is from
`render_command_i/blitter_i/clip_left_q_reg[1]/C` to
`render_command_i/blitter_i/destination_row_product_q_reg/CEB2`. It is a
12.603 ns data path with 21 logic levels: 13 `CARRY4`, two `LUT2`, two `LUT4`,
and four `LUT6`, comprising 4.797 ns logic and 7.806 ns routing. Signed
destination/source clipping, effective-coordinate derivation, and the
data-dependent transition into the DSP-backed row multiply collapsed into one
clock-enable cone. Similar paths terminate at source-row and last-row products.
This is a measured planner-pipeline defect, not a routing-completeness,
pixel-clock, or hold failure. The next retained RTL must register the clipping
stages and isolate the row-product launch before another complete route.

Resource use at this rejected checkpoint is 29,363 total LUTs, comprising
24,233 logic LUTs and 5,130 memory LUTs, plus 30,565 registers, 85.5
BRAM36-equivalent tiles, 65 DSPs, and 1,096 unique control sets. Relative to
the qualified sprite release this adds 7,409 LUTs, 7,562 registers and 14 DSPs
with no BRAM growth. Capacity is acceptable; timing is not.

The exact source snapshot is
`/mnt/Documents/astra68/work/render-v1/source`; the rejected route is
`/mnt/Documents/astra68/work/render-v1/basic-blitter-route-1`. Key source
identities are:

| Source | SHA-256 |
|---|---|
| Render blitter | `439f0fd67b3ce9e0cabb467ec24038a8194faa1ee52c089ed4c36bcc4281bb0d` |
| Command processor | `253ab8855e89d1b6d0c1a1cead8337bda67432226a4edb30b0ce09b4aaabbcfe` |
| Pixel writer | `6c6d2bc638d691d12cd7b42ea9424c94dc827822f1a86f3cfbc536ebe8c207c7` |
| Graphics pipeline | `eae9a8d8d8f24e1612c0656455dc7f4ac5d00629c33e3903a8808106f3204e9d` |
| Graphics control | `eed48c8d5c45406caca8a78a23ff0af643a238548fea0a5613e40c356951af14` |
| Arty top | `d1b1d932267a657dfe898b27b2a586424fd45e0060475f7cc6cce43bc893c04d` |
| Vivado build script | `a64569a63a316a695485186140ac8af06fcd11c0de0ebe95c9b07f1906f03056` |

| Artifact | SHA-256 |
|---|---|
| Routed DCP | `acdec89023ef0513a654d310de1b69f397e21a47e1decea23867f46403c6d52e` |
| Timing report | `0994d2a3845f519a67f2bef6a7eb2902b1c011216ac5ef6414e509c71a20df0a` |
| Utilization report | `7b775746ebfd066743bd17afaa5601294e3350c559a03ed00fb75d94287498c0` |
| Methodology report | `36f163f253f4f57cdac372dbc6309033a670205f2eef836bc8477407e0782faf` |
| Route-status report | `6c7b35492cf4911dcfcb6c6ff2fd8bd30d2dd0ac024e630789a0ca0ff7762aa8` |
| Rejected bitstream | `26695956b7f4fc9fe88c57db913547c96db298f82d5ea502d8928ee279f69708` |
| Rejected XSA | `d5c54267557a733615ebc60f6f985762e5b847a744c87b10235e4262b8c11441` |

The generated bitstream and XSA are diagnostic artifacts only and must not be
packaged, installed, or used as hardware evidence.

### Retained basic-blitter timing repair

Focused routed OOC checkpoints on Beast with Vivado 2024.2 isolated and
removed the rejected full-route cone before another integrated build. All use
the production 200 MHz render clock constraint and `xc7z020clg400-1`:

| Checkpoint | Setup slack | Measured critical cone | Retained change |
|---|---:|---|---|
| Registered clipping planner | -1.710 ns | columns remaining to destination pixel address | Register signed clipping limits and coordinate/count derivation |
| Registered row advance | -0.408 ns | command height to FSM transition | Separate end-of-row address and pixel selection |
| Split admission planner | -0.115 ns | effective source X to first-row address | Separate command dimensions/format admission and nominal address stages |
| Distinct address registers | -0.005 ns | destination first-row address to destination pixel address | Preserve surface-base, row-base, and X-offset pipeline boundaries in the synthesized netlist |
| Split execution address | +0.017 ns | options register to fault-detail clock enable | Separate reverse-row selection from endpoint-byte selection |

The first nominal address split repeatedly updated one register, which Vivado
legally folded back into a 48-bit carry chain. Distinct surface-base, row-base,
and final-address registers are required to preserve the intended boundaries.
The final execution split widened the internal state encoding to six bits and
adds one setup cycle per command. No per-pixel cycle was added; row advance
adds one cycle per output row.

The exact final focused checkpoint is
`/mnt/Documents/astra68/work/render-v1/execution-boundary-1`. The routed OOC
uses 1,376 LUTs, 1,688 registers, zero BRAM tiles, and 8 DSPs. The complete
directed graphics regression on the exact final checkpoint passes, including
all 131,072 sprite scale pairs, integrated MMIO fill, pixel writer, blitter,
and exhaustive 24-command processor cases. A transaction-level comparison
confirmed that successful commands retain identical AXI address/strobe
sequences; only an intentionally timed-out command may retire a different
number of partial writes, as permitted by its finite deadline.

| Focused artifact | SHA-256 |
|---|---|
| Final render blitter RTL | `b99b7cf363e6be5f0850a2646ced1b80820bce445ed048efe88440b815c80f38` |
| Timing summary | `1ba34c4c1bdf0556d1507141d72794596ef34f1257275cda4e6f9fe22dbd1b84` |
| Timing paths | `838ee00dca1760a1f93e2284ba00e7bfeef399f2ea4bc3d0928621297e48e778` |
| Utilization | `a69f8203a2020914c34f8342b7cf824319d5aa17ab8b3c2d04a5bab964a41d4c` |
| Routed OOC DCP | `b428644f4fc6fb2a105da67c38da69edee8443039e24506be82cd1ef3be92e1c` |

### Rejected corrected integrated route

The exact corrected source was then routed as `full-route-2` on Beast with
Vivado 2024.2 and `Performance_ExplorePostRoutePhysOpt`. All 54,913 routable
nets completed with zero errors, but the checkpoint is rejected because the
200 MHz render domain still fails setup:

| Gate | Result |
|---|---:|
| Overall WNS / TNS | -5.236 ns / -42,494.301 ns |
| Overall WHS / THS | +0.014 ns / 0.000 ns |
| Failing setup endpoints | 32,797 |
| 74.25 MHz pixel-domain setup | +0.150 ns |
| Routable nets | 54,913 / 54,913 complete; 0 errors |

Post-route physical optimization recovered 0.693 ns of WNS but did not alter
the diagnosis. The exact worst path is from
`render_command_i/destination_data_offset_q_reg[12]/C` to
`render_command_i/completion_fault_q_reg[12]/R`. It is a 9.657 ns data path
with 15 logic levels: six `CARRY4`, two `LUT2`, one `LUT4`, one `LUT5`, and
five `LUT6`, comprising 3.855 ns logic and 5.802 ns routing. Destination range
comparison, protected/control overlap selection, command-state selection, and
the blitter-result fault selector have collapsed into one completion-register
control cone. The focused blitter remains timing-clean; the next retained RTL
must register range-validation results and isolate completion-result capture
inside the command processor.

This route uses 28,900 total LUTs, comprising 23,770 logic LUTs and 5,130
memory LUTs, plus 31,146 registers, 85.5 BRAM36-equivalent tiles and 65 DSPs.
The source snapshot is
`/mnt/Documents/astra68/work/render-v1/execution-boundary-1/source`; the
rejected route is
`/mnt/Documents/astra68/work/render-v1/execution-boundary-1/full-route-2`.

| Artifact | SHA-256 |
|---|---|
| Command processor RTL | `253ab8855e89d1b6d0c1a1cead8337bda67432226a4edb30b0ce09b4aaabbcfe` |
| Timing report | `d81908102249a03c5c235ff87d7a466ef72d182c373cd81613d53f3db7a357ed` |
| Utilization report | `011367833b66fc0c5ed2c4c472802350a77128e2e12842562749db735495ac42` |
| Route-status report | `732d3e22c32c5f56541ee03f69e4d5f099e5fe48daf3614b9793250137159e28` |
| Routed DCP | `c7379472b47c06d935e5077e64552fc08c43e55727a3903345082782f2872e42` |
| Rejected bitstream | `c33a843ddf474c7a677786a75b9f2bb13ec9352eb9b4b01ba9ab11e20edc4d71` |
| Rejected XSA | `30e31e1d1edc4934944065e14d8c5859ae0f6b34ff7738f545d5eff2181bdc31` |

The generated bitstream and XSA are diagnostic artifacts only and must not be
packaged, installed, or used as hardware evidence.

### Integrated path-boundary repair

Registering validation and completion results removed the `full-route-2`
command-result cone, but subsequent exact routes exposed smaller independent
boundaries rather than one remaining monolithic renderer path. The last
rejected checkpoint, `path-boundary-2/full-route-8`, completed every net and
reduced the failure to 24 setup endpoints at -0.148 ns WNS and -1.333 ns TNS;
hold passed at +0.015 ns. The measured endpoint groups were:

- command state to the HP2 SmartConnect read-payload FIFO address;
- tile command state to tag-FIFO write enable;
- tile span offset through an 11-level carry path to the HP1 read-address
  register;
- HP3 SmartConnect write-address state to its AXI register slice;
- sprite `render_remaining` to `render_compatible` clock enable;
- one clone-response and four tile span-memory write enables.

The retained `path-boundary-3` changes address those measured groups directly:

- an explicit AXI4 read-only register slice now isolates the renderer from the
  HP2 SmartConnect; its AR and R channels are registered;
- the HP3 write-address and data input channels use registered-input mode;
- each tile line builder has a one-entry registered AR launch stage and
  reserves tag capacity before issue, so command state no longer drives the
  tag FIFO write enable or the SmartConnect address input;
- sprite compatibility is loaded unconditionally in the idle state, removing
  the `render_remaining` clock-enable dependency.

The tile launch stage still accepts one request per build clock. Directed
simulation measures 1,179 clocks for the exact tile workload versus 1,346 at
the previous integrated baseline, a 12.4% improvement. Focused routed OOC
checks at the production 5.000 ns constraint pass at +0.057 ns for the tile
line builder and +0.024 ns for the sprite line builder. The complete directed
graphics suite passes, including all 131,072 sprite scale pairs, all seven
sprite modes, the integrated pipeline, and all 25 command-processor cases.

### Qualified Stage 1 renderer

Exact production checkpoint `path-boundary-3/full-route-9`, built on Beast
with Vivado 2024.2 from the immutable source snapshot at
`/mnt/Documents/astra68/work/render-v1/pixel-credit-1/path-boundary-3/source`,
meets every production constraint with the complete framebuffer, two tile
layers, 64 sprites, boot text, command/fence transport, surface validation,
shared pixel writer, and basic fill/copy blitter enabled:

| Gate | Result |
|---|---:|
| Overall WNS / TNS | +0.003 ns / 0.000 ns |
| Overall WHS / THS | +0.013 ns / 0.000 ns |
| Pulse-width slack | +0.538 ns |
| Failing setup / hold / pulse endpoints | 0 / 0 / 0 |
| 74.25 MHz pixel-domain setup | +1.462 ns |
| 200 MHz renderer-domain setup | +0.003 ns |
| Routable nets | 55,816 / 55,816 complete; 0 errors |
| Combinational latch loops | 0 |

The limiting setup path is no longer in the command engine, tile engine, AXI
boundary, or sprite render FSM. It is the existing sprite descriptor metadata
BRAM through one LUT6, MUXF7, and MUXF8 into validation read data: 4.793 ns,
including 2.920 ns logic and 1.873 ns routing. The methodology report contains
only advisory synthesis, I/O-delay, derived-clock and control-set warnings;
none has a related timing violation. The CDC report retains the documented
stable-payload/toggle warnings covered by directed skew tests.

Resource use is:

| Resource | Used | Device percent | Physical free |
|---|---:|---:|---:|
| Slice LUTs, total | 28,549 | 53.66% | 24,651 |
| LUT as logic | 23,525 | 44.22% | 29,675 |
| LUT as memory | 5,024 | 28.87% of LUT-RAM capacity | 12,376 |
| Slice registers | 33,087 | 31.10% | 73,313 |
| Occupied slices | 10,982 | 82.57% | 2,318 |
| BRAM36-equivalent tiles | 84.5 | 60.36% | 55.5 |
| DSP48E1 | 61 | 27.73% | 159 |

The route source manifest is
`docs/evidence/astra-arty-render-basic-source-manifest-20260801.sha256`.
Durable artifacts are under
`/mnt/Documents/astra68/work/render-v1/pixel-credit-1/path-boundary-3/full-route-9`:

| Artifact | SHA-256 |
|---|---|
| Routed DCP | `6b9dcfc1281abdb3d4cce4723f9512c0f17ef91e263d0291b43b91a56d5e1bd0` |
| Timing report | `9a3ce74f142503d41847f810aef1a407708d917ebb52da121be28d80d32100fa` |
| Utilization report | `0c62fb5cfd2fa5455470cac0906cd553a2fe158922070b5e1cb946ee93435f31` |
| Methodology report | `93460bc8d3ff133350b763d606f132db546981c206ff213a38009d89f87a0bdd` |
| Route-status report | `b3e22dff7c3d5fa8d6092d09b5082ff914ae31414f9187aaf726560405408438` |
| CDC report | `a5b05d3af8afc2a75eaab5cbc1565c247b29c7bb847e7073c1abf960cb588f3a` |
| Bitstream | `fbfd7f80572dd9b0783e94d61cacda4388453083c8a8cae39ffc131628eef2aa` |
| XSA | `cec3bbcdcba33d2ab3510168172526202b15f44ddaf51044ac6c8a5fba4fe160` |

The exact XSA generated FSBL
`9dab28e9d1b9b06d065e8e0ee9e0df93636ba9da4a5f09f1736c67d2115ddea8`.
Active `BOOT.BIN` is
`c118b5a9aa88b1d5d682ce92553b8b45b9133aa9efe1a8b8d5c0432ecd137509`;
the unchanged FIT remains
`e9ef016f059cb3bc71138edf2a5ae47646a0e11b3dab3b81f7362f592b97b542`.
The deployment transaction now hash-checks and atomically installs the
renderer certifier as well as the boot, status, and sprite tools.

After reboot, FPGA manager reports `operating`, `/` is read-only, `/data` is
read-write, graphics capabilities are `0x000001ff`, and the complete splash
readback remains 1,843,200 bytes with CRC32 `611029ee`. Six consecutive basic
renderer runs each execute six fenced commands and verify 1,196,608 resulting
pixels with zero backpressure, failures, timeouts, or resets. Total execution
varies by only 331 clocks around 3.600 million clocks. A 60-second scene
promotion and restore advances generations 6 and 7. The complete sprite
hardware regression then passes all stress, hidden, clipped, and grid phases
with zero dropped pixels or engine errors. Exact output is retained in
`docs/evidence/astra-arty-render-basic-hardware-20260801.log`.

No automated HDMI capture device was exposed on Mac, Beast, or Nuc during
this run, so no new renderer screenshot is claimed. The DDR result buffer is
verified byte-for-byte and scene promotion/restore is hardware-confirmed; a
new physical capture remains useful release evidence but is not represented
as already retained.

### Variable-dimension sprite certification

The existing sprite RTL accepts source width and height independently from 1
through 128; no RTL or routed-bitstream change was required to expose smaller
or nonsquare images. Certification now proves the complete boundary. The
scene-store test maps the existing 131,072 exact horizontal scaling cases over
all 16,384 source width/height pairs and explicitly rejects zero and 129 on
either axis. A dedicated line-builder mode fetches the first and last row for
all 128 widths paired with all 128 heights, alternates X/Y reflection, uses the
minimum legal 64-byte pitch through width 64 and 128-byte pitch above it, and
asserts admitted pixels, rounded AXI bytes and every request's allocation
bounds.

The Arty certifier retains the original 64-way maximum stress and adds two
packed 64-sprite dimension scenes: odd widths with even heights, then even
widths with odd heights. Together they exercise every discrete value from 1
through 128 on both axes while scaling every sprite through the full 720-line
output. A quiescent off-screen scene is promoted before CPU writes to shape
storage and on every failure path, preserving the ACTIVE-resource ownership
rule and making repeated invocations safe. The first measurement caught the
old certifier violating that rule after an intentionally failed run; the board
was rebooted to clear the sticky diagnostic, and three subsequent runs,
including a no-reboot repeat, pass with zero deadline errors.

The unchanged `path-boundary-3/full-route-9` bitstream reports an exact 4,352
AXI bytes per line in both dimension phases, approximately 3,070 maximum build
clocks, no drops, overflow, AXI errors or deadlines, and a 327,680-byte live
packed final scene. There is no LUT, register, BRAM, DSP or timing delta.
Persistent evidence is under
`/mnt/Documents/astra68/work/sprite-v1/variable-dimensions-1`.

| Artifact | SHA-256 |
|---|---|
| Scene-store test | `91b5c5836b4b821d3ee6ef9263f91119b468e6bfd48a1ec217ba430bafce8013` |
| Line-builder test | `c70bc65a1deeec9be28db1b6ae9187495623740b1d164fdbced79ba03c860940` |
| Hardware certifier source | `302e1062f7578204223ea9ba96c59a9f3a7b0622ad0d5071de4325bf128c0c0e` |
| Installed hardware certifier | `4692f723917d2589085580f7222c55804da6653e6db1cd77797abfad12b77f3a` |
| Complete graphics regression | `f074f620419a2392bab91aded927f5523abd9a0b99683546bbfc0eaa4c629be3` |
| Hardware log | `7955bfb2199dc64af4c36b6cfe12c474a63d999b0355c701dc8d3116fbc44657` |

### Complete-blitter focused timing work

The complete-blitter implementation now passes the exact directed graphics
suite on Beast. Its implemented contract includes nearest-neighbor scaling in
both directions, X/Y reflection, clipping with source-sampling preservation,
INDEX8/RGB565/XRGB8888/ARGB8888 conversion, source keying, all sixteen ROP
truth tables, premultiplied constant-opacity source-over, palette attachment,
MASK1 attachment, combined palette/MASK1/alpha operation, and overlap-safe
same-format 1:1 copies. Command admission also rejects missing auxiliary
surfaces, invalid auxiliary formats, alpha/ROP combinations, and overlapping
palette ranges without dispatching the blitter or modifying the destination.

Focused OOC routes on Beast with Vivado 2024.2 and the exact 5.000 ns render
constraint have reduced, but not closed, the implementation:

| Checkpoint | Setup slack | Measured critical cone | Retained change |
|---|---:|---|---|
| `ooc-blitter-1` | -5.271 ns | source X through mapping/address/cache/request into state | Initial complete feature set |
| `ooc-blitter-2` | -4.392 ns | source cache decode through palette address into state | Register effective source X for MASK1 lookup |
| `ooc-blitter-3` | -3.556 ns | source pixel address through lane/decode/key into pixel-value enable | Register palette pixel address |
| `ooc-blitter-4` | -2.580 ns | source X phase/mapping and row address into source pixel address | Add a source-decode stage |
| `ooc-blitter-5` | -2.280 ns | destination ARGB through a LUT/carry alpha product | Separate next-source address formation |
| `ooc-blitter-6` | -1.787 ns | command reflection flag through effective X into source-row address | Register alpha operands and force the 8x8 product into DSP logic |
| `ooc-blitter-7` | -1.453 ns | auxiliary row base through MASK1 beat-address/cache-hit control into pixel-value enable | Reuse the registered effective source X when forming each new source-row address |
| `ooc-blitter-8` | -1.388 ns | vertical scale step through Q24 phase advance, Y mapping and row-product logic | Register the MASK1 byte address before cache/state dispatch |

Checkpoint 8 removes the MASK1 address/cache/state-control cone and passes the
complete directed graphics regression. Focused utilization is 2,651 LUTs,
3,043 registers and 13 DSPs. The newly exposed 6.109 ns data path has fourteen
logic levels: ten `CARRY4`, one `LUT2`, one `LUT4` and two `LUT5`, comprising
3.470 ns logic and 2.639 ns routing. It begins at `scale_step_y_q_reg[1]` and
ends at `auxiliary_row_base_q[31]_i_2_psdsp/D`. The route crosses the 40-bit
Q24 phase addition, reflected Y mapping and row-product logic because physical
synthesis retimes the existing effective-Y register into the DSP pipeline.
The next structural change must preserve a register boundary between phase
advance and Y mapping/multiplication; placement directives or seed changes are
not justified by these reports.

Checkpoint 8 and its exact source snapshot are under
`/mnt/Documents/astra68/work/render-v1/complete-blitter-1/ooc-blitter-8`.

| Artifact | SHA-256 |
|---|---|
| Render blitter RTL | `4bfa73428d0a59309eeff94c572117e70551f06f92cbcfe6c2c6baea194801e9` |
| Command processor RTL | `5a6975056612d1a5a21a2f0c56f154f70bf23df01fd4af46c5666faadf29e55b` |
| Timing summary | `95b16eb686c196fdca3384ccff65166d65de9f45b39282b8a29cab2e5a052fcd` |
| Timing paths | `4e9525f4ba00c58ddebcf828bc3428863c2b88db868c54fd68a70eec68db4f75` |
| Utilization | `c18b1645b9e88726a95d2a4e091c0870df4620809ad7bbd19ae8a8505ab6fd68` |
| Routed OOC DCP | `649ff3f7cbc9e4433388f7989b3d42af931ef3550994aa35d633c386d5568f72` |

This checkpoint is functional and reproducible historical evidence, not a
release bitstream. The closure sequence below supersedes it.

### Complete-blitter closure and hardware certification

The remaining timing work followed exact focused and integrated reports. No
seed search, reduced-feature build, or timing waiver was used. The retained
structural checkpoints after OOC checkpoint 8 were:

| Checkpoint | Focused result | Retained disposition |
|---|---:|---|
| `checkpoint-44-validator-split` | command processor +0.055 ns setup / +0.096 ns hold | Split descriptor header, geometry, access, and storage validity so synthesis cannot recreate the combined admission cone. |
| `checkpoint-45-blit-format-split` | blitter -0.111 ns setup / +0.122 ns hold | Split flag, format, palette, auxiliary, and overlap contracts; rejected because synthesis merged the intended boundary. |
| `checkpoint-46-blit-format-keep` | blitter +0.085 ns setup / +0.134 ns hold | Preserve the five contract registers explicitly; complete blitter OOC closes. |
| `checkpoint-47-route-boundaries` | control +0.267 ns setup / +0.096 ns hold | Capture render-busy at AXI write admission and insert an explicit registered HP0 framebuffer read boundary. |
| `checkpoint-48-sprite-transfer-payload` | sprite scene +0.269 ns setup / +0.039 ns hold | Capture scale-step and order with the descriptor transfer rather than rereading the pending bank at activation writeback. |
| `checkpoint-49-palette-capture-stage` | sprite line +0.176 ns setup / +0.044 ns hold | Insert an explicit palette-BRAM capture stage before the independent blend and collision DSP consumers. |

Every retained checkpoint passes the complete directed graphics suite. The
final sprite-line OOC uses 5,061 LUTs, 6,226 registers, 24 BRAM36-equivalent
tiles, and 46 DSPs. Its former palette-BRAM-to-DSP path is closed; pulse-width
slack is +1.116 ns and there are no failing endpoints.

The exact full-system route history after command-processor closure was:

| Route | WNS / TNS | Hold | Failing setup endpoints | Measured limiting path | Disposition |
|---|---:|---:|---:|---|---|
| `full-route-21-checkpoint-44` | -0.091 ns / -0.351 ns | +0.010 ns | 7 | tile line-store BRAM to palette BRAM address, 70.5% route delay | Rejected. |
| `full-route-22-checkpoint-47` | -0.007 ns / -0.007 ns | +0.025 ns | 1 | global reset synchronizer to boot-text glyph state, 96.3% route delay | Rejected. |
| `full-route-23-checkpoint-48` | -0.090 ns / -0.280 ns | +0.006 ns | 5 | global reset synchronizer to compositor/palette state, 96.3% route delay | Rejected. |
| `full-route-24-checkpoint-49` | **+0.013 ns / 0.000 ns** | **+0.051 ns** | **0** | 200 MHz renderer domain | **Qualified.** |

Route 24 is a clean nonincremental Beast Vivado 2024.2 build using
`Performance_Explore` and the retained Route-20 QoR suggestions. It meets the
74.25 MHz pixel domain at +2.620 ns and the 200 MHz renderer domain at
+0.013 ns. Pulse-width slack is +0.538 ns. All 59,647 routable nets are fully
routed with zero errors. The methodology report has 207 advisory warnings and
no critical warning or error: four `SYNTH-5`, 87 `SYNTH-6`, 110 `SYNTH-9`,
four `TIMING-18`, one `TIMING-28`, and one `ULMTCS-1`.

Resource use for the complete framebuffer, two tile layers, 64 sprites,
boot text, command transport, and complete blitter is:

| Resource | Used | Device percent | Physical free |
|---|---:|---:|---:|
| Slice LUTs, total | 30,185 | 56.74% | 23,015 |
| LUT as logic | 25,161 | 47.30% | 28,039 |
| LUT as memory | 5,024 | 28.87% of LUT-RAM capacity | 12,376 |
| Slice registers | 36,050 | 33.88% | 70,350 |
| Occupied slices | 11,695 | 87.93% | 1,605 |
| BRAM36-equivalent tiles | 84.5 | 60.36% | 55.5 |
| DSP48E1 | 66 | 30.00% | 154 |

The immutable source is
`/mnt/Documents/astra68/work/render-v1/complete-blitter-1/checkpoint-49-palette-capture-stage/source`.
Its 146-file source manifest is stored beside Route 24 and has SHA-256
`3c9deda82b3101c609b3ff4725564f3bb85ee4f1bc06b2d50143fb83eb268267`.
Durable artifacts are under
`/mnt/Documents/astra68/work/render-v1/complete-blitter-1/full-route-24-checkpoint-49`:

| Artifact | SHA-256 |
|---|---|
| Bitstream | `96c98a4dadb5703efcc93121b3d6c6226dc319c52e9054697de98f1e8cca17a0` |
| XSA | `8f185db10c09cd5988e59cb542e456afbf608dff0c3c320c6365762d58a8c508` |
| Routed DCP | `7736a2eee80d22690b98fde52ccc6b7d266eea126dd6187c3025d2a506627217` |
| Timing report | `f6d5a0a8e352d5de9c3762eb54dcbbfb1e2b4d71ce45d0d059ed1114c6d629cc` |
| Utilization report | `087ba2a019a4bbf1a1c8aaa315701838e0f04a8dc1e42a1094b28fc5a0335590` |
| Route-status report | `793d4a45ab7d2c04246f92d131cc67a45f946b43f92fb52ce879fb4cc7d9f3ce` |
| Methodology report | `5aa0c07326cd41b051911ac47419279c184f8cc6046dc8d01c352d3476df0d41` |
| Fresh FSBL ELF | `2ffd0216b5857402619cc38a5fcfff153969d9c815696502da765d7944e31e6a` |
| Active `BOOT.BIN` | `dfd34dd31bafd199889d7d2cc1f9f2682b72636b296e4f4b3a1964d4ef6acbaa` |

The Route-24 package is active on Arty. FPGA manager reports `operating`, `/`
is read-only, `/data` is read-write, the FIT remains
`e9ef016f059cb3bc71138edf2a5ae47646a0e11b3dab3b81f7362f592b97b542`,
and boot readback passes all 1,843,200 splash bytes with CRC32 `611029ee`.

Hardware checkpoint 50 extends the existing renderer certifier rather than
adding a parallel harness. Its first run correctly returned `BAD_DESCRIPTOR`
for source descriptor `0x00418040`: the certifier had reused a palette-attached
descriptor for a non-palette command, while the RTL contract requires a zero
palette offset unless `BLIT_PALETTE` is set. The test now uses separate plain
and palette-attached descriptors. No RTL or bitstream changed for that repair.
The corrected source has SHA-256
`387a4a2d74ebec3e0d61056e3a24847e3d79ffddf6f8d6aacb745057dc75f8ea`;
the installed static ARM binary is
`c1ea9c75827c5de62a930ed5119b3ce72e26358146bb98b1c3e6783204f01c5d`.

Ten consecutive hardware runs each retire 29 fenced commands, verify exactly
1,196,651 destination pixels, and pass scaling, X/Y reflection, clipping,
source keying, all 16 ROPs, RGB565/XRGB8888/ARGB8888 conversion,
premultiplied source-over with opacity, INDEX8 palette expansion, MASK1 write
suppression, and overlap-safe copy. Every run reports zero backpressure and
the counter gates reject any failure, timeout, reset, stale fence, or bad
completion. Total command time ranges from 3,619,139 to 3,619,778 build clocks.
Persistent evidence is
`/mnt/Documents/astra68/work/render-v1/complete-blitter-1/checkpoint-50-hardware-certifier/evidence/complete-blitter-hardware-10x.log`,
SHA-256
`b644db5a8c14da9cd9469615e7a29286fc924a308f6b95b0dd75768c7c00c295`.
The full 64-sprite stress/hidden/clip/grid certifier then passes with zero
drops, overflow, AXI errors, or deadlines, and a subsequent complete-blitter
run passes unchanged. Those cross-subsystem logs have SHA-256
`8d8f5ac7ac92057b5694d7c791b59d1e497b894987292f19e8dad9855d9f659e`
and `7697b2efc595c5d01b21a963c1229186ff672d4c7b01d7d145e09d5c1162888d`.
The preserved cross-build, GCC analyzer, and host-test log has SHA-256
`b4e1bcb2d882f4626904aabfa047f71f95116399b1466bac7ec10517fc2f1167`.

### Virtual-sprite grouping decision and certification

Virtual sprites use consecutive ordinary `BLIT` commands into a hidden
surface. The final command sequence is the group fence; the graphics service
may present or reuse the surface only after every completion through that
fence succeeds. This preserves per-command validation, deadlines, reset,
failure reporting, and bounded-ring backpressure without adding another
descriptor engine or command-processor batch state machine.

A rejected diagnostic implementation added a parent `BLIT_BATCH` command and
eight command-processor states. It passed directed simulation, including the
256-child inclusive bound, but its focused 5.000 ns OOC route failed at
-0.289 ns. The 5.158 ns critical path was 79.1% routing delay through the
expanded one-hot completion-entry cone. Compact sequential encoding worsened
slack to -0.638 ns. Splitting completion entry across two registered lanes
also failed at -0.332 ns and moved the failure to a state clock-enable cone.
All three experiments were rejected; their reports remain under
`/mnt/Documents/astra68/work/render-v1/virtual-sprites-1`.

Checkpoint 53 restores the exact qualified command processor, SHA-256
`5a6975056612d1a5a21a2f0c56f154f70bf23df01fd4af46c5666faadf29e55b`.
Its directed command regression passes 30 submissions and 20 intentional
failures. A fresh Beast Vivado 2024.2 OOC route meets 200 MHz at +0.002 ns
setup slack with no unrouted nets. It uses 5,291 LUTs, 8,636 registers, and
2,845 slices. The retained route is
`/mnt/Documents/astra68/work/render-v1/virtual-sprites-1/checkpoint-53-grouped-blit-baseline-ooc`.

The ARM certifier now submits a second bounded phase of 64 ordinary RGB565
BLITs. Each scales a 2x1 source to a verified 16x16 virtual sprite in the
hidden 1280x720 framebuffer. It validates all 64 independent completions,
16,384 output pixels, renderer accounting, and final fence 93 before making
the existing optional presentation path eligible. GCC cross-compilation and
`-fanalyzer` pass. On the unchanged Route-24 production bitstream, ten
consecutive Arty runs pass with zero backpressure, failure, timeout, or reset.
Group execution ranges from 179,099 to 179,737 renderer clocks.

| Artifact | SHA-256 |
|---|---|
| Static ARM certifier | `da68530b0386da1d021214bac2c6faa0c8182b7ab49d711b48c89b270e44ed17` |
| Ten-run hardware log | `330f56f573a1eaff2d99cad10dfa088f7f75f08311142483496c52b52068bc14` |
| Active `BOOT.BIN` | `dfd34dd31bafd199889d7d2cc1f9f2682b72636b296e4f4b3a1964d4ef6acbaa` |

## Geometry producer OOC progression

The first geometry producer implements clipped aliased lines, outlined and
filled rectangles, circles and ellipses, and transparent or opaque 8x8 pattern
fills. It emits backpressured coordinates into the qualified shared pixel
writer and does not own AXI. Bounded flood fill is not part of this checkpoint
and remains required before the geometry stage can close.

Directed simulation reconstructs the destination framebuffer under randomized
producer backpressure and compares exact pixels for line octants, clipping,
reversed rectangle corners, circle and ellipse outline/fill, a degenerate
ellipse, and signed transparent/opaque pattern origins. The current source
passes with `ASTRA RENDER GEOMETRY PASS` on Beast.

All routes below use Vivado 2024.2, `xc7z020clg400-1`, and the exact 5.000 ns
render-clock constraint. Durable reports are under
`/mnt/Documents/astra68/work/render-v1/geometry-1/`.

| Checkpoint | WNS | Limiting cone or experiment | Disposition |
|---|---:|---|---|
| 1 | -10.203 ns | Combinational implicit ellipse with 15 DSPs | Rejected; replaced by iterative multiply. |
| 2 | -3.711 ns | Circle midpoint arithmetic | Split update phases. |
| 3 | -3.397 ns | Iterative multiplier final add | Pipelined product/sum/compare. |
| 4 | -2.186 ns | Rectangle endpoint into line setup | Split endpoint, delta, and error setup. |
| 5 | -0.960 ns | Span X into ellipse binary-search state | Changed FSM to explicit one-hot. |
| 6 | -0.979 ns | Scan-X comparison into row transition | Split scan termination and row advance. |
| 7 | -0.853 ns | Pattern lookup into emitted color | Registered the selected pattern bit. |
| 8 | -0.870 ns | Circle Y/error update | Split Y and error phases. |
| 9 | -0.988 ns | Circle error feedback into decrement decision | Registered the decision in a separate phase. |
| 10 | -0.556 ns | Vector-case priority enable into line state | Superseded by reverse-case one-hot. |
| 11 | -0.556 ns | Parallel hint on vector case | Rejected; no netlist or timing effect. |
| 12 | -0.600 ns | Disabled CE extraction on line registers | Rejected as an overall regression. |
| 13 | -0.670 ns | Disabled CE extraction on emit registers | Rejected as an overall regression. |
| 14 | -0.789 ns | Disabled CE extraction on scan registers | Rejected as an overall regression. |
| 15 | **-0.185 ns** | Reverse-case one-hot; residual state-to-circle-error CE | **Retained best source; integration timing risk remains open.** |
| 16 | -0.594 ns | Parallel hint on reverse-case one-hot | Rejected; pre-route and routed timing regressed. |
| 17 | -0.312 ns | Disabled CE extraction on circle error | Rejected; exposed a line-error arithmetic failure. |
| 18 | -0.552 ns | Moved circle registers into a separate sequential block | Rejected; worsened the state-to-circle control cone. |
| 19 | -0.149 ns | Post-route `Explore` physical optimization on checkpoint 15 | Rejected; still fails setup, hold remains +0.169 ns. |
| 20 | -0.149 ns | Post-route `AggressiveExplore` physical optimization | Rejected; identical result to checkpoint 19. |
| 21 | -0.171 ns | Post-route `AggressiveFanoutOpt` physical optimization | Rejected; worse than `Explore`. |
| 22 | -0.171 ns | Post-route `AlternateReplication` physical optimization | Rejected; no improvement over checkpoint 21. |
| 23 | -0.435 ns | Disabled CE extraction across the geometry datapath | Rejected; moved failure to the ellipse-radius-square data path. |
| Integrated 24 | -5.307 ns | Unregistered geometry row multiply and address path into the shared writer | Rejected; proved the integration boundary required a real pipeline. |
| 24 pipeline OOC | -0.518 ns | Two-stage address pipeline; unrelated `end_x_q` to emit-register CE cone | Diagnostic; the original DSP-to-writer path was removed. |
| Integrated 25 | -0.965 ns | Submission word 11 through unlatched geometry Y into the row-multiply DSP | Rejected; latch the complete geometry command at start. |
| Integrated 26 | -0.873 ns | Scan-X feedback through next-state logic into the X-register CE | Rejected; register the scan continuation decision. |
| Integrated 27 | -0.768 ns | State-derived synchronous reset of the per-row ellipse lower bound | Rejected; targeted reset extraction was measured separately. |
| Integrated 28 | -0.887 ns | Reset extraction moved failure to coordinate arithmetic into the DSP | Rejected; restored normal reset inference. |
| Integrated 29 | -1.009 ns | Coordinate/address pipeline in the monolithic FSM perturbed the state-to-emit CE cone | Rejected; separate pipeline sequential control from the geometry FSM. |
| Integrated 30 | -0.806 ns | Clip comparison through elastic valid control into the row-multiply DSP enable | Rejected; preserve a coordinate register boundary. |
| Integrated 31 | -1.955 ns | Preserved coordinate stage exposed a combinational two-DSP 16-by-32 multiply cascade | Rejected; split pitch into parallel registered 16-by-16 products. |
| Integrated 32 | -0.829 ns | Rectangle min/max comparison and scan-X load in the same dispatch cycle | Rejected; register rectangle bounds before scan setup. |
| Integrated 33 | -0.733 ns | Emit-Y clipping comparison into `emit_valid_q` | Rejected; add an explicit registered clip-classification phase. |
| Integrated 34 | **-0.688 ns** | Geometry state into the iterative multiplier accumulator reset | **Current diagnostic source; functional gates pass, timing remains open.** |
| Integrated 35 | -0.871 ns | Multiplier isolated behind registered start/done; state-qualified abort decode now limits emit-register CE | Functional improvement retained; remove completion-state fanout from abort. |
| Integrated 36 | -0.517 ns | One-shot abort latch removes the state-qualified abort cone; ellipse outline compare now limits emit-register CE | Retained. |
| Integrated 37 | -0.632 ns | Registered ellipse second-pixel decision removes that cone; opcode decode into line-setup CE becomes worst | Retained functionally; predecode opcode. |
| Integrated 38 | **-0.283 ns** | Registered opcode predecode; residual scan-row Y comparison into X-register CE | **Current retained source; geometry without flood passes all functional gates.** |
| Integrated 39 | -0.497 ns | Registered scan-row continuation removed the residual cone but perturbed placement into `command_is_geometry_q` to writer data | Rejected; restored checkpoint 38. |

Checkpoint 15 is fully routed with no routing congestion and uses 1,983 LUTs,
1,122 registers, no BRAM, and two DSP48s. It does not pass the isolated 200 MHz
setup gate and is not independently timing-qualified. The documented Vivado
physical-optimization directives and selective clock-enable controls are now
exhausted without closure. The next measurement is a diagnostic integration
with the exact qualified command processor and shared writer: this determines
whether production placement recovers the residual 185 ps or exposes a new
cross-module cone. That integration is not a production promotion unless the
exact complete route passes. Exact retained source identity:

| File | SHA-256 |
|---|---|
| `astra_render_geometry.sv` | `bc7002931da50cfad73a53cacc0368e951e7fc3d2e1f85f60e05a1a37295e628` |
| `tb_astra_render_geometry.sv` | `dfef80ef9cc985a95d4c07998e69e57989e38ffbc45aa3df99682ef99e3ad354` |
| `synth_render_geometry_ooc.tcl` | `46eda439874e9bff7f7b8d2bb55a9e4f0ae4f9bb202954e2800d1c543c48f3c1` |
| `render_command_ooc.xdc` | `fe7757e2c745900a1f8f3c5621e4fce6431c500b4c465fd431bbc710ccd9231d` |

The integrated experiments above all route every net with zero routing
errors. Checkpoint 38 is the retained source: WNS/TNS is -0.283/-11.195 ns,
hold slack is +0.096 ns, and utilization is 7,453 LUTs, 10,487 registers, and
18 DSPs. Its geometry source SHA-256 is
`8c1be03c01f2b5c7f9e95e9f36b9c9dfee7829611d6ef7f2cecdc008820a27c4`;
the integrated command-processor SHA-256 remains
`65315a752d86d861f7b1f7e985df86a788b075eb7d5f36ff5eb2a0d0e3388c4b`.
The exact routed checkpoint is
`/mnt/Documents/astra68/work/render-v1/geometry-1/checkpoint-38-opcode-predecode-ooc`.
Focused randomized geometry and the 37-command shared-writer regression both
pass. The multiplier, abort, ellipse-decision, and opcode-dispatch cones are
now registered. Bounded flood fill remains missing, so this is not a geometry
release checkpoint and its residual setup failure must be reassessed after
the complete geometry engine is integrated.

## Bounded flood integration and full-route checkpoint

Bounded scanline flood fill is now implemented through the shared pixel
writer. Caller-provided validated workspace stores the bounded pending-seed
stack; overflow reports `WORK_OVERFLOW` and aborts without unbounded storage.
Directed simulation passes a 60-pixel normal fill and an eight-pixel overflow
case. The complete graphics regression also passes all sprite dimensions,
scanout, composition, command transport, blitter, geometry, flood, and the
integrated 40-command test (24 intentional validation failures, 360 reads,
130 writes).

The flood timing investigation used Beast Vivado 2024.2,
`xc7z020clg400-1`, and the exact 5.000 ns render constraint. Checkpoints 8-28
are retained under `/mnt/Documents/astra68/work/render-v1/flood-1/`. The
measured progression rejected one-hot state encoding, command-selector writer
arbitration, preserved operand-register attributes, fanout attributes, manual
writer-selector copies, and ineffective `EXTRACT_ENABLE` attributes. Binary
state encoding, staged read classification, push preparation, a free-running
address pipeline, logic-based row-product summation, and a registered ellipse
decision are retained.

The first exact full PS/DDR/HDMI route with bounded flood is
`full-route-1`. It routes every net with no overlaps or failed nets, but fails
the 200 MHz setup gate at WNS/TNS `-0.784/-300.222 ns`; hold passes at
`+0.002 ns`, pulse width at `+0.538 ns`, and the 74.25 MHz pixel domain has
`+2.380 ns` setup slack. The exact worst path is
`flood_i/active_y_q_reg[2]_replica` to `flood_i/state_reg[4]_rep`: the
neighbor-row add, bounds comparison, and state transition form nine levels
(`5 CARRY4`, `LUT4`, `3 LUT6`) with 5.672 ns data delay.

Checkpoint 28 splits that operation at a real register boundary:
`ST_NEIGHBOR_ROW` registers `active_y +/- 1`, and
`ST_NEIGHBOR_ROW_CLASSIFY` performs the bounds decision one cycle later. The
complete functional suite remains green. The focused OOC worst path moves out
of flood to surface validation/completion accounting and improves from the
retained `-0.659 ns` baseline to `-0.625 ns`.

The next exact routes measured the remaining flood cones rather than changing
implementation seeds. `full-route-2` moved the exact worst path to seed bounds
and activation at `-0.722 ns`. Checkpoint 29 registered seed activation; its
focused route moved the worst path out of flood, while `full-route-3` exposed a
12-level four-operand flood pixel-address carry chain at `-0.778 ns`. Checkpoint
30 now computes surface base, row address, and column address in separate
registered stages. The complete regression remains green, including all
131,072 sprite scaling checks and the flood normal/overflow cases.

`full-route-4` is the exact checkpoint for the staged address source. It routes
every net with no overlaps or failed nets and improves WNS from `-0.778 ns` to
`-0.657 ns`; hold passes at `+0.014 ns`. Flood is absent from the leading
failing paths. The exact worst path is now geometry FSM decode from
`geometry_i/state_reg[18]` (`ST_MUL`) to the replicated clock enable for
`state_reg[13]` (`ST_ELLIPSE_EMIT`): five LUT levels, 5.307 ns data delay, and
77.1% routing delay. The next timing edit therefore belongs to geometry state
transition/control, not flood or physical seed exploration.

A focused attempt to disable clock-enable extraction on the complete one-hot
geometry state register is rejected. Geometry and flood simulation pass, but
the routed command-processor OOC result regresses from `-0.620 ns` to
`-0.930 ns` and creates a new path from the writer flush state into geometry
state D. Reports remain in `cp31-state-noce`; the attribute is not retained.

A second focused experiment removes multiplier waiting from the one-hot state
vector and parks the FSM behind a dedicated wait bit. Geometry simulation
passes, but routed OOC WNS is `-0.745 ns`; the limiting path becomes writer
flush into geometry state reset. Reports remain in `cp32-multiply-wait`. This
change is also rejected without an integrated route. The measurements show
that the next retained change must partition geometry control so unrelated
writer, multiplier, and ellipse transitions do not share one state D/CE cone.

Checkpoint 33 performs that partition. Ellipse and multiplier progression use
an independent 11-state one-hot controller while the compacted 23-state main
controller remains in one `ELLIPSE_ACTIVE` state except when it borrows the
shared scan producer. The complete graphics suite passes; its NAS log SHA-256
is `0c14fa0d0fc9d3296d10f568cf639123270795211ad8e42d945d2e58ad218c50`.
The routed geometry OOC result improves from the retained `-0.620 ns` class to
`-0.296 ns`, and the `ST_MUL` to global-state-enable cone disappears. Reports
are retained in `cp33-ellipse-partition`.

Checkpoint 34 registered the 34-bit line-end comparison before line stepping.
Focused simulation passed and the intended comparator cone disappeared, but
OOC WNS regressed to `-0.514 ns` by exposing the geometry pixel-address path.
The line-end register is rejected and reports remain in `cp34-line-finished`.

Checkpoint 35 instead inserts a real address boundary: one stage combines row
and column offsets and the following stage adds the destination surface base.
Focused geometry and integrated 40-command regressions pass with unchanged
accounting. The nine-level address path disappears and geometry OOC improves
to `-0.099 ns`. Its remaining leading paths are shallow, placement-sensitive
main-state/line-setup control (`-0.099 ns`), circle-step arithmetic
(`-0.097 ns`), and line/scan control (`-0.095 ns`).

`full-route-5` is the exact integrated checkpoint for that source. It routes
every net and improves setup from `-0.657/-741.865 ns` to
`-0.464/-192.061 ns`; hold remains clean at `+0.018 ns`. The exact worst path
is the 34-bit line-end comparison from `geometry_i/x_q_reg[9]` to the
replicated `line_error_q_reg[3]/CE`: five logic levels and 5.115 ns data delay.
Resource use is 33,305 LUTs, 39,012 registers, 12,222 slices, 84.5 BRAM36
tiles, and 70 DSPs. The routed DCP SHA-256 is
`489ded39b4deabc4b97c9f4a09ebc8a0504ff07425d00b91ff04b372c4250477`.

Checkpoint 36 registers the line-end result, but synthesis absorbs the
boundary and recreates the raw comparator-to-control path. Checkpoint 37
preserves that single intended register with a narrow `keep` attribute; the
line-end cone then disappears, and geometry OOC reports `-0.282 ns` on the
now-exposed midpoint-circle decrement decision. Checkpoint 38 replaces
`2 * (error - x) + 1 > 0` with its exact integer equivalent `error >= x`.
Focused geometry and integrated command tests pass with unchanged accounting,
and the complete 22-program graphics regression passes. Its durable traced log
SHA-256 is
`745a627d589187a416d94082d5f24ea8a03473afcf5add5683ced25ce8b5f63d`.
The checkpoint-38 OOC result is `-0.307 ns` on circle-coordinate generation;
the 25 ps difference from checkpoint 37 is placement-scale and the targeted
shift/add cone is absent.

`full-route-6` is the exact integrated checkpoint for checkpoint 38. It routes
every net but improves setup only to `-0.453/-87.944 ns`; hold is `+0.015 ns`
and pulse width is `+0.538 ns`. The preserved line-end boundary is present and
the former worst path is gone. The new exact worst path is the Bresenham
advance decision from `geometry_i/line_dy_q_reg[3]` to replicated
`geometry_i/x_q_reg[*]/CE`: six levels (`3 CARRY4`, `LUT4`, `2 LUT6`), 5.093 ns
data delay, and 62.4 percent routing. Resource use is 33,352 LUTs, 38,998
registers, 12,223 slices, 84.5 BRAM36 tiles, and 70 DSPs. The routed DCP and
failing-path table SHA-256 values are
`987707deb43637533d51ca73be27d1f2387771e5cba38dd543427db4a422d203`
and
`b956f41d248fdcc2a7ac19966583ba4ecf477f574463de472f709a6411fcbf63`.

Checkpoint 39 separates line decision from line
application: one cycle registers X/Y advance predicates and the next applies
coordinate and error updates. Focused geometry and integrated 40-command tests
pass. Geometry OOC confirms that the measured Bresenham control cone is gone;
its new `-0.359 ns` path is an unrelated, 81.2-percent-routing state-to-emitter
clock enable. The complete 22-program graphics regression passes with explicit
exit status zero; its durable log SHA-256 is
`834f40db7d683ce88595d8ce76c939fe3f35941b4064e3fae1f159dae33db0a9`.
Exact `full-route-7` rejects this 24-state form. Every net routes, hold passes
at `+0.019 ns`, and pulse width passes at `+0.538 ns`, but setup regresses to
`-0.486/-103.113 ns`. The former line-decision cone remains absent; the new
worst path runs from `geometry_i/y_q_reg[2]` through seven levels into
replicated `geometry_i/y_q_reg[*]/CE`, with 5.138 ns data delay and 58.4 percent
routing. The route uses 33,247 LUTs, 38,949 registers, 12,163 slices, 84.5
BRAM36 tiles, and 70 DSPs. This is a placement/control regression rather than
a capacity overflow. Timing-report SHA-256 is
`14e4a482fd0ecf05a33f1d647b5508720e1b2227bdc84a486884416bb5a4ce58`.
The next candidate retains the registered X/Y decisions but reuses the existing
line-error state with a phase bit, avoiding expansion of the global one-hot
controller.

Checkpoint 40 tests that direct reuse and is rejected before integration.
Initialization and iterative error updates then share one destination mux,
creating a seven-level, four-`CARRY4` path from `line_error_q` back to itself at
`-0.848 ns` OOC. Checkpoint 41 instead reuses `ST_LINE_SETUP` for the apply
phase while leaving `ST_LINE_ERROR` initialization-only. Focused geometry and
integrated command tests pass. Geometry OOC improves to `-0.035 ns`; the
line-decision and merged-error cones are both absent, and the remaining worst
path is a five-LUT, 76.2-percent-routing ellipse enable. The complete
22-program graphics regression passes with explicit exit status zero; its
durable log is
`/mnt/Documents/astra68/work/render-v1/flood-1/cp41-setup-line-decision/full-functional.log`
with SHA-256
`834f40db7d683ce88595d8ce76c939fe3f35941b4064e3fae1f159dae33db0a9`.
Exact integrated routing remains pending before retention.

`full-route-8` is the exact integrated result for checkpoint 41. Every one of
64,564 routable nets completes and hold passes at `+0.050 ns`, but setup
regresses to `-0.575/-272.767 ns` across 2,031 endpoints. The two leading
geometry paths identify independent structures: an unregistered DSP A input
from `coordinate_y_q` at `-0.575 ns`, and a nine-level circle-center/radius
arithmetic plus emission-mux cone from `p0_x_q` to `emit_x_q` at `-0.570 ns`.
The route uses 33,325 LUTs, 38,945 registers, 12,310 slices, 84.5 BRAM36
tiles, and 70 DSPs. Routed DCP, timing summary, and failing-path table SHA-256
values are
`f4f54ca8694af87f198866b6ac896e6769618e0b1ba343a888909b874462b1c1`,
`977f961ab5bcadd7fc88d3a72330aba7578001f33f31e1e1707b91d5a71f589c`,
and
`bb93b16453c8124744645c208cd5b4e21ea53d17d74e93f80e0a8dc7217d1e11`.

Checkpoint 42 stages circle point/span coordinates behind a phase bit in the
existing circle state. Focused geometry and integrated command tests pass and
the routed circle emission cone disappears. Checkpoint 43 adds one elastic
operand stage to the shared geometry address pipeline. Direct routed-DCP
inspection proves that both inferred geometry DSPs change from `AREG=0` to
`AREG=1` while preserving `BREG=1` and `PREG=1`. Geometry OOC is
`-0.042 ns` on unrelated reset control. The complete 22-program regression
passes with explicit status zero; its log SHA-256 is
`d6d0a109fb29f57293c26a4b1ea4e43e604243989d57570329f59680931d3451`.

Exact checkpoint-43 `full-route-9` routes all 64,672 routable nets and improves
setup to `-0.440/-96.858 ns` across 850 endpoints; hold is `+0.014 ns` and
pulse width is `+0.538 ns`. Both full-route-8 geometry cones are absent. The
new leading path is HP3 write-ready through the shared writer/barrier control
into command state at `-0.440 ns`, followed by unregistered flood barrier and
seed-admission control. Resource use is 33,296 LUTs, 39,096 registers, 12,221
slices, 84.5 BRAM36 tiles, and 70 DSPs. Routed DCP, timing summary, and
failing-path table SHA-256 values are
`42846d5881cf30276afd931fcf40ede79b58617340d01c18edd84c71b343d61f`,
`31770212eb962a0fcb83f94af0e09c873e95ec6937e8b22a2af3e96405daf77e`,
and
`de6bc1440e824ad690735e9b08ffa00740f219b1227081b65d7b765acd4a70ff`.

Checkpoint 44 registers writer-barrier readiness inside flood and registers
the seed-admission predicate before activation. Focused flood and integrated
40-command tests pass. Command-processor OOC confirms that both measured
control cones are absent; its new `-0.376 ns` WNS is stack-count control. The
complete 22-program regression passes. Exact `full-route-10` routes every net
and is the retained geometry timing baseline at `-0.444/-61.104 ns` across 685
endpoints; hold is `+0.008 ns` and pulse width is `+0.538 ns`. The exact worst
path is `command_is_flood_q` into completion-fault clock enable, with effective
source fanout 185. The route uses 33,323 LUTs, 39,114 registers, 12,399 slices,
84.5 BRAM36 tiles, and 70 DSPs. Routed DCP, timing summary, and failing-path
table SHA-256 values are
`6350867a8a29ea1a747d58f6287c327b5b3acc828d477b4a633836b6c435f0fe`,
`1b20e3bc01bc916636fcc3594df54c0817b6bd34b5fbbf7400b76fd1caffa7e2`,
and
`ad9bc13d022a495fe7893b60b6c2d0491daae969e0919156fe198563c17fff28`.

Checkpoints 45-48 test the measured classifier cone and are rejected. Registering
all engine result buses every cycle removes the target OOC but exact
`full-route-11` regresses to `-0.881/-377.950 ns` across 2,234 endpoints, led by
flood active-coordinate control. Flood-classifier `max_fanout=8` creates 23
replicas and is rejected without an integrated route; `max_fanout=32` creates
six replicas, but `full-route-12` still regresses to `-0.488/-116.639 ns` across
1,114 endpoints and moves the worst path into ellipse control. Selecting
completion data from the asserted engine-done pulse also removes the target
OOC and preserves all functional tests, but `full-route-13` regresses to
`-0.616/-108.141 ns` across 1,022 endpoints. Its worst path is flood
`active_x_q[1]` into `state[3]`; hold remains `+0.018 ns` and pulse width
`+0.538 ns`. The route uses 33,345 LUTs, 39,132 registers, 12,254 slices, 84.5
BRAM36 tiles, and 70 DSPs. Its DCP, timing, and failing-path SHA-256 values are
`8f09a23e8f05f0c30890661714e9e4924abce92e054e8f03c8ee3dadda723fe2`,
`c7867b80c2dba052abc33b093d33da99a8f5f120d7a4fc29afeb91978f722cae`,
and
`6414b81b60de1554c2594329905ae6c068b2e0562de97b0cb8cc2e4e134d56de`.
These experiments prove that removing one local classifier cone does not
improve the integrated floorplan. The checkpoint-44 source and full-route-10
remain authoritative.

Post-route `-routing_opt -critical_pin_opt` on full-route-10 reports exactly
zero WNS or TNS gain and leaves timing at `-0.444/-61.105 ns`; Vivado explicitly
reports no improvement for the flood-classifier and completion-fault nets. A
clean route of the same checkpoint-44 RTL with the documented
`Performance_ExplorePostRoutePhysOpt` strategy is materially better:
`full-route-14` reaches `-0.249/-38.962 ns` across 456 endpoints, with hold at
`+0.008 ns` and pulse width at `+0.538 ns`. The failure population is broad:
401 non-sprite, 40 other-sprite, and 15 sprite-blend endpoints. No source owns
more than 32 failures, and pixel-writer abort, HP1 interconnect, blitter loop
control, framebuffer control, flood stack count, and geometry control are all
represented. The route uses 33,329 LUTs, 39,114 registers, 12,399 slices, 84.5
BRAM36 tiles, and 70 DSPs. DCP, timing, and failing-path SHA-256 values are
`7c5b23d935c6f7d8d6694e7e57a3171ebb30492a8c379e95c3da2551f8a0e966`,
`307d00e75a43f9799fa287c02d9d9347e87722328463362b289bb25f1c0101ed`,
and
`768e521750da4753a825cb1254522586b6737e02b9a53bc389066b527b13c9c2`.
This is the measured-best integrated route, but it is not releasable at
200 MHz. `Performance_ExtraTimingOpt` is also rejected: placement aborts
because more than five percent of movable instances cannot be placed, naming
cells across geometry, flood, blitter, scheduler, and command control. At
93.23% slice occupancy, the remaining timing failure is an integrated physical
density problem rather than a single local RTL cone.

| Exact full-route resource | Used | Available | Utilization |
|---|---:|---:|---:|
| Slice LUTs | 33,329 | 53,200 | 62.65% |
| Slice registers | 39,114 | 106,400 | 36.76% |
| Physical slices | 12,399 | 13,300 | 93.23% |
| BRAM36-equivalent tiles | 84.5 | 140 | 60.36% |
| DSP48E1 | 70 | 220 | 31.82% |

| Retained file | SHA-256 |
|---|---|
| `astra_render_flood.sv` | `074413bfeda42f91b40b45d70f5f1079a9d2d13c0296f39f048e5b13334223b8` |
| `astra_render_geometry.sv` (checkpoint-43 candidate) | `3c65979c29e14e9dce18515544f9ce6447d24241a8983aa4e2bba0f8077c073e` |
| `astra_render_command_processor.sv` | `43d8d062e64f0bd10ada6aaf20066252d5b2a343bf41d0637970bffe41548b32` |

### Geometry clock decision and hardware qualification

`full-route-16` tests a requested 185 MHz renderer clock. Zynq clock
quantization produces exactly 187.5 MHz; the complete design routes but fails
setup at `-0.218/-12.770 ns` across 176 endpoints. It uses 32,449 LUTs,
39,111 registers, and 12,060 slices, so the result is a timing failure rather
than a capacity failure.

`full-route-17-166m667` requests 175 MHz and the generated processing-system
clock quantizes to exactly 166,666,672 Hz. The exact complete route passes
setup at `+0.060 ns`, hold at `+0.016 ns`, and pulse width at `+0.538 ns`, with
zero failing endpoints. It uses 32,207 LUTs, 39,098 registers, 12,344 slices,
84.5 BRAM36-equivalent tiles, and 70 DSPs. The full 22-program source
regression passes with explicit status zero.

| Exact artifact | SHA-256 |
|---|---|
| Bitstream | `b2599c5c3b00f312fc4a8b149944243c0885741f5df061f91d521009ce24472b` |
| XSA | `fb6edf558f12bb2fdb5cd00d0fbaa5e72c3b1706954946ca0ca76f470ad75b6c` |
| Routed DCP | `0104487fa0cdbdde121408d64e26a5df341fcf3ad4b1c51002b479fd1b4a571d` |
| Timing summary | `11046c801cd50b5d9a260a4f24744970fe669200bddcdf8898421809aa061ad0` |
| Utilization report | `8851026885b96a73ca89575d1607ca0fcb0c6c1864c42d3185f78878075540eb` |
| Full functional log | `9c218ae54273fc04f85d88f67d98a618777482cbfc9eaf1edca56c2844d5da7c` |
| `BOOT.BIN` | `08e188e2747ec801df151e517c50c126029b388ba048af6a95cd22549f24b3c9` |

The exact FSBL and device tree both select 166,666,672 Hz FCLK1. After
hash-verified atomic deployment and reboot, the FPGA manager reports
`operating`, `/` remains read-only, and `/data` remains writable. Ten
consecutive executions of certifier SHA-256
`ca83f3c564613fe88e0cf15399d94b6a5b2200c118b2a53e8202bb5cfdea7d2c`
pass complete blitting, the 64-command virtual-sprite group, all five geometry
primitive classes, exact 60-pixel flood containment, and explicit
`WORK_OVERFLOW`. Geometry uses 18,901 through 19,299 clocks; overflow uses
1,597 through 1,804 clocks; every run reports zero backpressure. Logs and a
SHA-256 manifest are retained under
`/mnt/Documents/astra68/work/render-v1/flood-1/cp49-postroute-strategy/full-route-17-166m667/hardware-cert`.

### AFNT glyph qualification and AXI inter-beat correction

AFNT glyph expansion shares the renderer's validation, clipping, source-over
blending, pixel writer, completion, timeout, and reset paths. Directed tests
cover MASK1, A4, A8, INDEX4, and INDEX8 glyph runs plus malformed input and
fault-drain behavior. The first timing-clean candidate,
`full-route-2-fault-drain-166m667`, passed simulation and initially booted, but
is rejected by hardware evidence: two of ten repeated certifications returned
`BAD_RANGE` with detail `0x00050000` on descriptor zero.

The failure was traced to the two-beat AXI descriptor receiver in
`astra_render_glyph.sv`. It shifted descriptor storage every renderer clock,
including cycles where `RVALID` was low. A legal gap between AXI read beats
could therefore replace the first descriptor beat with invalid bus data. The
simulation model had supplied contiguous beats and did not exercise that
case. Descriptor beats are now captured only on `RVALID && RREADY`. The
focused testbench inserts a deterministic three-cycle gap between accepted
descriptor beats and passes with 49 reads and 12 writes. The complete
22-program graphics regression also passes; its log is
`/mnt/Documents/astra68/work/render-v1/afnt-1/full-functional-interbeat-gap-fix.log`,
SHA-256
`b86e49a0c27c552e79abdd3c92e9d8c3933227dfb5fad1cfd4a0346d2450cf0b`.

Exact replacement route
`full-route-3-interbeat-gap-166m667` uses the generated 166,666,672 Hz FCLK1
and the `Performance_ExplorePostRoutePhysOpt` strategy. Beast Vivado 2024.2
routes all 68,601 routable nets with zero errors. Setup slack is `+0.078 ns`,
hold slack is `+0.015 ns`, and pulse-width slack is `+0.538 ns`, with no
failing endpoint.

| Resource | Used | XC7Z020 capacity | Percent | Physical free |
|---|---:|---:|---:|---:|
| Slice LUTs | 34,379 | 53,200 | 64.62% | 18,821 |
| LUT as memory | 5,025 | 17,400 | 28.88% | 12,375 |
| Slice registers | 40,952 | 106,400 | 38.49% | 65,448 |
| Physical slices | 12,674 | 13,300 | 95.29% | 626 |
| BRAM36-equivalent tiles | 84.5 | 140 | 60.36% | 55.5 |
| DSP48E1 | 80 | 220 | 36.36% | 140 |

| Exact artifact | SHA-256 |
|---|---|
| `astra_render_glyph.sv` | `c35279e93c40829b9a15843e2e9fc5d1f59aee769ea34b2d331e5351ef53c76c` |
| `tb_astra_render_glyph.sv` | `cd12ed49fa683f205732639e5c086013ae9372f92fbeff96f4ece396a7f64a4f` |
| Bitstream | `6e60685bf7bf322f701b3fb385306a5b71d05e97e0f0731623752f126bcec9d8` |
| XSA | `dd28ac29fdb4be8cc4b3787106f3904ce584cb28036f03e050640fb56bd930f8` |
| Routed DCP | `c9c9aed4e93ea648473d631e3939a5489ea56f63ff6212cd50284ad60d7a0793` |
| Timing summary | `b3e3505899f3518efc829b2d8882311164c98e887d68611a33baca13ba4e1df8` |
| Utilization report | `e545203a7d7dab2cef8c21c51cd2530007b779f5a4bd9aa5b79a9826e3fa99f2` |
| Route-status report | `0f17568afea0d2161ec715c21f75de102a4719c65d889c008415fe695ddafc28` |
| `BOOT.BIN` | `bcd0fa1d105b2055ff1cf9de149b82a45d129fc90960e70784a14c064d33d8f5` |

After hash-verified deployment and reboot, FPGA manager reports `operating`,
`/` remains read-only, `/data` remains writable, and splash readback passes
1,843,200 bytes with CRC32 `611029ee`. Twenty consecutive complete hardware
certifications pass all five AFNT formats, the expected rejected glyph, the
complete blitter, 64-command virtual-sprite group, geometry, and bounded flood
fill with zero backpressure. Five additional sprite certifications pass. The
audited logs are
`afnt-certification-route3-20x.log` and
`sprite-certification-route3-5x.log` beside the route artifacts, with SHA-256
`e228abf15a6790fb2d0fc80936e7e2c5f7fe9e265bc8dd4076770dda6861668f`
and
`aa91a85a0ee8bdb0302d254b96484036e04d0122424e3f5599ba3f31a6dce0fd`.
This closes and promotes the AFNT gate.

## Next release gate

The 64-sprite, command transport, complete-blitter, virtual-sprite group,
geometry/flood, and AFNT gates are closed in simulation, exact routed timing,
and repeated Arty hardware. The next graphics stage is dual-bank copper with
4096 instructions per bank, beam WAIT/SKIP, validated MOVE, IRQ, command
dispatch, and hardware-enforced register timing classes. It requires focused
simulation, an exact full route, and hardware certification before promotion.

### Copper integration checkpoint 1

The standalone dual-bank engine, AXI4-Lite control wrapper, address splitter,
register whitelist, and scheduler preparation handshake are retained as the
first integration checkpoint. The copper store routes out of context as
exactly 16 RAMB36, zero LUTRAM, 635 logic LUTs, and 545 registers. At the
production 6.000 ns renderer period it meets setup at `+0.142 ns` and hold at
`+0.131 ns`; artifacts are under
`/mnt/Documents/astra68/work/render-v1/copper-1/ooc-production`.

The production pipeline now routes the legacy and copper control apertures
through the independently verified one-outstanding AXI4-Lite splitter. Copper
exports an execution-coordinate tag: pre-WAIT effects are `(0,0)`, and effects
after a reached WAIT use that WAIT's exact X/Y coordinate. A dedicated virtual
beam controller prevents line builders from launching until copper has stopped
or is waiting beyond that line's inherited state. It also gates line zero on
baseline restoration and advances through `(1649,749)` after line 719 is
prepared, so blanking-period WAITs are reachable.

Focused copper, control, register, AXI split, and beam-scheduler tests pass.
The complete pre-existing graphics suite and integrated pipeline still pass;
the pipeline reports 256 checked pixels and 18 built lines with the render
fill test included. MOVE and DISPATCH remain conservatively rejected at the
pipeline boundary until their timing-class destinations are connected; this
checkpoint is not a release candidate and has not been synthesized as a full
system.

| Retained integration source | SHA-256 |
|---|---|
| `astra_copper.sv` | `ec2f9cc9602e771a6faad68914f5ae0f25174849cc403fae0964fdee24dda718` |
| `astra_copper_control.sv` | `cbaa4ef467bb20cb952c8ceae6ed232759eda10460f74690f223ccfa59276802` |
| `astra_copper_beam_scheduler.sv` | `d6e2047c9afdff42d65d8fa53f5ed6c2955e3257713248dc1de6a828d49a8a28` |
| `astra_axi_lite_1to2.sv` | `f8659c0601b8cfc1345d1e1ddcf8bfaa7f9fd58270aba1e08a0f278120b2435e` |
| `astra_graphics_pipeline.sv` | `625d95ee30c195b13f3a3da0e2236a94f67525d8b64cab3f78eb8ef587ce1142` |

### Copper completion, exact route, and hardware qualification

The retained implementation completes the copper contract rather than the
checkpoint-1 subset. It provides two BRAM-backed 4096-instruction banks,
WAIT/SKIP, validated MOVE, IRQ, render-command dispatch, and pixel-boundary,
next-scanline, and next-vblank timing classes. Effects and PC changes retire
through a registered stage, and structural running-state history breaks the
former live decode/scheduler timing cone. Focused copper and structural tests
pass. The frozen complete integration-13 graphics regression passes with
SHA-256
`cfc377a5f0144fda2da9cb68c93a770cee1e1192ec74c8da91bc2227feb5e27c`.

The first exact `Performance_ExplorePostRoutePhysOpt` implementation reached
`+0.036 ns` setup and `+0.016 ns` hold after post-route physical optimization,
but that step left one AXI address connection incomplete. Bit generation
correctly rejected it with `RTSTAT-1`. AMD documents `route_design -preserve`
as preserving completed routing while completing incomplete connections. The
recovery applied that command, retained the same timing, produced zero failed,
unrouted, partially routed, or overlapping nets, passed DRC, and generated the
hardware-qualified bitstream.

The production build flow now installs
`fpga/arty/scripts/repair_postroute_routes.tcl` as the
`STEPS.POST_ROUTE_PHYS_OPT_DESIGN.TCL.POST` hook. A clean from-source build in
`full-route-2-reproducible` exercised the normal flow, completed bit generation
without manual intervention, and independently reported `+0.036 ns` setup and
`+0.016 ns` hold.

| Exact full-route resource | Used | Available | Utilization | Physical free |
|---|---:|---:|---:|---:|
| Slice LUTs | 37,534 | 53,200 | 70.55% | 15,666 |
| LUT as memory | 5,025 | 17,400 | 28.88% | 12,375 |
| Slice registers | 44,655 | 106,400 | 41.97% | 61,745 |
| Physical slices | 13,036 | 13,300 | 98.02% | 264 |
| BRAM36-equivalent tiles | 118 | 140 | 84.29% | 22 |
| DSP48E1 | 83 | 220 | 37.73% | 137 |

Global route utilization is approximately 29.4% vertical and 34.7%
horizontal. BRAM is the primary remaining resource limit; physical packing is
also tight. Copper itself uses 16 RAMB36 and no LUTRAM in the focused exact
6.000 ns implementation.

| Retained source or artifact | SHA-256 |
|---|---|
| `astra_copper.sv` | `e84518a7b4d805ca0f1d49474a3fff99ce3c91ade05401c5bda5bbe965cb1e42` |
| `astra_copper_structural_state.sv` | `b3e4732c83e5621844ce38fa753fe03be28b1bb4e110ebce3e28696f664d5df3` |
| Recovery bitstream, hardware qualified | `6281d7cd544e279edf693d1fe41a7e47259845afdc3c3c0d0045e18c04e27879` |
| Recovery routed DCP | `2bcbfdb3c85cc50ba2a7824c95cfcacfba276e01ec93549913821c153f4c9196` |
| Clean-flow bitstream | `7fdda9ab456d8df7c8d6eacf3c2b337d1409f47bc0ea0888ac51eaa76a125f0c` |
| Clean-flow XSA | `4bd0e76e7a46966d537261d8233fb792e6408f7feffad21bdb1fa30a7a5b05e9` |
| Clean-flow routed DCP | `6ff9ac600ccc116dd0fa5ba505c358699234d16e1e3bcb6aa388172c51b1c6b5` |
| Clean-flow timing report | `79705ad343eb801b364018c6689753789689a7cf82a99242b10777e4034bcff5` |
| Clean-flow utilization report | `cf40a864dce8e5a963320955f7bcd1d2278dba2612d4ceec650c22fa1cab36ce` |
| Clean-flow route-status report | `f6a1c80908c21495ba8ad1e17de1c01924a8c09fe54beed37c07cb14b8ef8039` |
| Clean-flow Vivado log | `f6b1804159926b8438e47d49f0604bc12d7d128e5810e0adbce0a76b87767072` |
| Active `BOOT.BIN` | `9637e1035acb9d1bd6d2bd0eec2e3cf9ca5c13023560af8d2b4f27a546444504` |

After hash-verified atomic deployment and reboot, FPGA manager reports
`operating`; splash readback passes 1,843,200 bytes with CRC32 `611029ee`.
Ten consecutive copper certifications pass with IRQ value `0xcafe`, dispatch
endpoint zero, and aligned forbidden-target containment. Ten complete renderer
runs pass all 29 commands, 1,196,651 checked pixels, 64 grouped virtual-sprite
commands, all geometry operations, bounded flood overflow, and all five AFNT
formats. Ten sprite runs pass the 64-sprite maximum, dimensions 1 through 128
on both axes, hidden/off-screen placement, clipping, and stress with no dropped,
overflow, AXI, or deadline errors.

| Hardware evidence | SHA-256 |
|---|---|
| `fpga-state.log` | `1e5bd3291a8eed6477b8668296e617daa4b11c21c4289020a2a8df6a9ff6284c` |
| `boot-status.log` | `5b729a8260693875de80ea20f495ca7342f74d5150bda368cc70481d3be933d3` |
| `copper-10x.log` | `a744bc29769a7280f31dab980972b1298a81ee0c93aa78ca975383cb4909b994` |
| `render-10x.log` | `3fc82d3ffc28db1fea01a89e061eeee85c559b725de78f646c859133ff014328` |
| `sprite-10x.log` | `30c82e1d6b8f202cc6e950e343e49b72a9317b675577f1031125f4155f9c4900` |

This closes the requested command transport, complete blitter, geometry,
AFNT, virtual-sprite, and copper graphics implementation gates. Further PL
graphics work is optimization or a separately specified feature, not an open
item in this release objective.

### Linux graphics-arena containment checkpoint (2026-08-09)

The retained RTL, routed bitstream, clocks, timing results, resources, and
failed-cone disposition above are unchanged. Hardware testing exposed a host
device-tree defect instead: Linux still owned RAM through `0x1fffffff` while
the renderer wrote the graphics arena beginning at `0x18000000`. The `no-map`
node alone did not remove that range from System RAM on the deployed kernel.

Beast rebuilt the FIT from source commit
`381d15306ff6b0077d8042fe975f426b7cf4f173` plus working-tree
`build_device_tree.sh`
`f187779708ad0f561c4208dedd3ccfa7e6789d875e06633644f3a03eea0246ee`
using the pinned `fdtput`, `dtc`, `dumpimage`, and `mkimage` tools. The device
tree is
`422c7d48554512f313f19d2e750d19ed2a426b46c53befcbe3e3e4c80ed9cfc4`;
the FIT is
`c9a77be0f5085ce048860d12bd88ce7a246b813cf76c20339e8c18b7f9358944`
with `SOURCE_DATE_EPOCH=1786326984`. The active `BOOT.BIN` remains
`9637e1035acb9d1bd6d2bd0eec2e3cf9ca5c13023560af8d2b4f27a546444504`.

After atomic FIT deployment and reboot, the Arty reports Normal RAM only
through `0x17ffffff`, reports `0x18000000..0x1fffffff` as the separate `no-map`
arena, reaches the terminal `WORK:>` prompt with QEMU and the renderer resident,
and leaves all three ARM QEMU libraries hash-stable during rendering. This is
a retained hardware pass with no synthesis, placement, route, timing, or
capacity change; there is therefore no new timing cone or resource table.

### Direct RGB565 copy performance checkpoint (2026-08-11)

Hardware profiling of a representative window drag isolated the dominant cost
to unscaled, same-format RGB565 blits. One 29-command repaint already represented
the complete coalesced drag update; the largest individual copy moved 185,920
pixels. The directed 64x16 identity-copy regression first failed at 9,878
cycles against a 9,000-cycle ceiling. The retained blitter bypasses scaling,
format conversion, and general dispatch only when flags are zero, formats and
dimensions match, and the copy is therefore provably direct. The regression
now passes at 5,822 cycles, a 41% reduction, while the complete directed
graphics suite passes unchanged.

Exact Beast Vivado 2024.2 build `direct-copy-ded15f7e/full-route-1` uses the
complete production design at 166,666,672 Hz. It routes all 74,818 nets and
all 152,192 timing endpoints with zero failures: setup is `+0.001 ns`, hold is
`+0.019 ns`, and pulse width is `+0.538 ns`.

| Exact full-route resource | Used | Available | Utilization | Physical free |
|---|---:|---:|---:|---:|
| Slice LUTs | 37,547 | 53,200 | 70.58% | 15,653 |
| Slice registers | 44,643 | 106,400 | 41.96% | 61,757 |
| Physical slices | 13,035 | 13,300 | 98.01% | 265 |
| BRAM36-equivalent tiles | 118 | 140 | 84.29% | 22 |
| DSP48E1 | 83 | 220 | 37.73% | 137 |

| Retained source or artifact | SHA-256 |
|---|---|
| `astra_render_blitter.sv` | `ded15f7ed765211fbf9838c09e8243cefcf82753ea1a0c6f1896170aebf81448` |
| `tb_astra_render_blitter.sv` | `2af134e875d1dc1efb1cb119dc87382ae72ab341398966a38425c74258fe094b` |
| Bitstream | `24dcb07f1641a449930d7c4856f3d10d294e44a10b5494c8741673c6d901c548` |
| XSA | `65960a0b25b2d24f10aa2351bdcf7d2ecfdc7cadcfc21f8076a2651f56689d31` |
| Routed DCP | `f50cafa830019eaab3f061eea7038b88234dbcee82969f473076299071503e41` |
| Timing report | `5508d894633aa44ae0b22ee83b2636977187a363643b79322bcf0c69d24af024` |
| Utilization report | `d47b0d363d4079c9337069eef1820444fc33ea3c95d6cded04f7ed0cf5dcb2e3` |
| Route report | `5e0723217bfc774d75d1062e4a1fc2ae99214beb295270798723ff4afa1c1a99` |
| FSBL | `069b6eba6b71eafc0e03c592f70421378bf5db467f52ce8ea56afb5f8a047866` |
| Active `BOOT.BIN` | `ac4dea6b90b562edf753d18378b9d8e5521cc26e5544b176b4bba1ad5a79df10` |

After atomic deployment through Beast, FPGA manager reports `operating`, Linux
Normal RAM remains bounded at `0x17ffffff`, and the FIT remains
`c9a77be0f5085ce048860d12bd88ce7a246b813cf76c20339e8c18b7f9358944`.
Warm 29-command drag repaints measure 25.1--26.1 ms in hardware versus the
previous 29--40 ms range. Ten complete renderer certifications and three each
of sprite and copper certification pass without dropped work, overflow, AXI,
or deadline errors.

### HDMI audio and Arty front-panel integration candidate (2026-08-11)

The candidate adds one bounded 48 kHz, signed 24-bit stereo PCM sink at
`0x43c06000` and reuses the shared `PNL0` front-panel block at `0x43c07000`.
Linux owns source mixing, resampling, gain, and effects. PCM playback,
wavetable synthesis, speech synthesis, and later producers therefore converge
on the same stereo stream; adding a producer does not alter the HDMI
packetizer or FPGA interface.

The Arty profile exposes its two switches and four LEDs. LEDs 0--2 retain the
video-lock, build-reset, and scene-active diagnostics until software claims
them. LED 3 is an independent storage-activity overlay with a programmable,
100 ms default hold. QEMU optionally maps the physical panel and triggers that
activity register on each admitted AstraHost block request, while the existing
guest NDK still sees `PNL0` at `0xfff01000`.

Focused shared-panel and AXI-wrapper simulations pass, the HDMI audio test
passes with the expected five deliberate underflows, and the complete graphics
regression passes. Both 128 MiB and 32 MiB QEMU block profiles pass with a
mock physical panel, including switch readback, LED writes, and block-activity
triggering. The runtime-supervisor test also passes.

Three exact Beast Vivado 2024.2 routes retain all production graphics
features. None meets the 5.000 ns build-domain constraint, so none was flashed:

| Candidate | WNS / TNS | Failing setup endpoints | Hold | Physical slices | Disposition |
|---|---:|---:|---:|---:|---|
| `full-route-11-writer-start-snapshot` | -0.011 / -0.011 ns | 1 | +0.010 ns | 13,086 / 13,300 | Rejected |
| `full-route-12-control-ready-snapshot` | -0.042 / -0.100 ns | 4 | +0.010 ns | 13,090 / 13,300 | Rejected |
| `full-route-13` with front panel | -0.960 / -1734.552 ns | 7,239 | +0.049 ns | 13,061 / 13,300 | Rejected |

Route 13 has zero failed, unrouted, partially routed, or overlapping nets and
+0.538 ns pulse-width slack. Its limiting path runs from copper
`execute_w0_q[4]` through the beam/control ready logic to the line scheduler
state enable. The path has six logic levels and 5.649 ns data delay, of which
3.961 ns is routing. This is a packing/congestion failure in the existing
copper/scheduler cone, not an audio-clock or front-panel cone. A generated
bitstream exists because Vivado writes it before the project timing gate; it is
explicitly rejected and must not be deployed.

| Route-13 resource | Used | Available | Utilization | Physical free |
|---|---:|---:|---:|---:|
| Slice LUTs | 39,839 | 53,200 | 74.89% | 13,361 |
| Slice registers | 46,308 | 106,400 | 43.52% | 60,092 |
| Physical slices | 13,061 | 13,300 | 98.20% | 239 |
| BRAM36-equivalent tiles | 119 | 140 | 85.00% | 21 |
| DSP48E1 | 83 | 220 | 37.73% | 137 |

The exact route-13 source snapshot is
`/mnt/Documents/astra68/work/audio-v1/integration-8/source`; artifacts are in
the adjacent `full-route-13` directory.

| Retained source or artifact | SHA-256 |
|---|---|
| `astra_arty_graphics_top.sv` | `01d215edab361db19bbd31517a498c1fad98badd96ef583b37c5e5dde6c56966` |
| `astra_front_panel_axi.sv` | `89bd7aac463d4ab885e52b4b8e7cb2abfac58982888972fb62aea2b49152ef02` |
| `astra_front_panel.sv` | `e3171ae9601fb65cbf2c6c4eb0e1f30d27a7db4dee9de7ac92c71f2f0e01dd4b` |
| `astra_hdmi_audio.sv` | `530d20eb69a15a8ce5e3fae54989a7ded5cb532eec3296ac039211ae0cd26b29` |
| Timing report | `571f469d9a6b1a110f6c9cf5fee75c12e1fb6858d58eabb34bf5e96c66963fd6` |
| Utilization report | `a4b1eb2078e86c652001e867366cbcc5d76da7dff7b45f3762887c27eda167fe` |
| Route-status report | `b0f6ad2b8c5b32557b7922388684995624828db00812310c2d9ddfc3ef2b7fbc` |
| Rejected bitstream | `9650b417eef4cdd545693dd03853b312622b42e828f1e32e01b7ebeeca3ec3dd` |
| Routed DCP | `512437594682698c09090c9e74fae3dd011859346a6fbcb4d922dd90f350b497` |
| XSA | `4de0717de14b06dbcdafae9e8b24282ff5d0fe5f7e183efa93eebadb86bbd25a` |

### Route 14 AXI fabric reduction checkpoint (2026-08-11)

AMD's AXI Protocol Converter supports direct AXI4-to-AXI3 conversion, and its
unprotected translation mode removes the machinery for splitting bursts longer
than 16 beats. The framebuffer, renderer-read, and renderer-write masters are
already bounded to at most 16 beats. The integrated pipeline regression now
asserts those limits, and the complete graphics regression passes.

Route 14 replaces only the one-master/one-slave HP0, HP2, and HP3
SmartConnect instances with direct protocol converters in translation mode 0.
The three-master HP1 SmartConnect remains because it performs real arbitration.
All graphics, HDMI audio, and front-panel features remain present.

| Exact full-route resource | Route 13 | Route 14 | Delta |
|---|---:|---:|---:|
| Slice LUTs | 39,839 | 37,317 | -2,522 |
| Slice registers | 46,308 | 43,829 | -2,479 |
| Physical slices | 13,061 | 13,004 | -57 |
| Unique control sets | 1,236 | 1,076 | -160 |
| BRAM36-equivalent tiles | 119 | 119 | 0 |
| DSP48E1 | 83 | 83 | 0 |

The LUT and register reduction is real, but physical occupancy remains
13,004/13,300 slices (97.77%) because LUTRAM and control-set compatibility still
limit packing. The exact Beast Vivado 2024.2 `Performance_Explore` route has
zero routing errors, but fails the 5.000 ns build-domain constraint at
WNS/TNS `-1.064/-1559.217 ns` across 7,368 endpoints. Hold is `+0.045 ns` and
pulse width is `+0.538 ns`. Its worst path is an existing copper fault-capture
cone from `execute_w0_q[1]` to `fault_pc[6]`: seven logic levels and 5.918 ns
data delay, of which 4.326 ns is routing. The rejected bitstream was not
flashed.

The exact source snapshot is
`/mnt/Documents/astra68/work/audio-v1/integration-9/source` at base commit
`19bed057d47958b1ed6ffce993848b8403ff55ae`; artifacts are in the adjacent
`full-route-14b-protocol-converters` directory.

| Retained source or artifact | SHA-256 |
|---|---|
| `build_graphics.tcl` | `926d80b0f9ce0b49555c5cafa71ed51fae51def8d76604818c88472c7f68352c` |
| `tb_astra_graphics_pipeline.sv` | `fb941169a4286bdfb99b022d1d4e49deb90b0447f1544566e734d374835d543d` |
| Routed DCP | `2619e2e8462187859b216e84e250db74bd0d5a6c43a31eba8691b3254c5499ee` |
| Timing report | `e7e5ab8eb5fcc73fb094d2a2eb95dae7edc03c05dfb1fd39ef04004c496a984c` |
| Utilization report | `61bfc88346bb62a5b258c60752c59b7ab12cddcf61af0a0e8ea284b05008a9b0` |
| Route-status report | `a10d516480dd087250938d8b89396861b2f91ed7563db7d7a0401d829c341bec` |
| Rejected bitstream | `c09ce0dee1e6ce174e2d660b1f32d2c3e193217a17631b585083e3fdd98f3872` |
| XSA | `5bf011b70762c01d6b77bc34878eef1f031af5fcac37a2a2b7ce683a8c82bb47` |

### Route 15 HP1 arbitration reduction checkpoint (2026-08-11)

Route 15 replaces the remaining three-client HP1 SmartConnect with a small
read-only round-robin arbiter in Astra RTL plus the same direct AXI4-to-AXI3
converter used on the other HP ports. The arbiter rewrites the three fixed-zero
client IDs to distinct output IDs and demultiplexes responses by RID. It can
accept one request every cycle, retains independently outstanding requests and
interleaved responses, and does not serialize the tile or sprite engines. A
focused simulation proves consecutive requests from all three clients,
round-robin fairness, request and response backpressure, and interleaved
response delivery. The complete graphics regression also passes.

| Exact full-route resource | Route 14 | Route 15 | Delta |
|---|---:|---:|---:|
| Slice LUTs | 37,317 | 34,528 | -2,789 |
| Slice registers | 43,829 | 40,121 | -3,708 |
| Physical slices | 13,004 | 12,571 | -433 |
| Unique control sets | 1,076 | 876 | -200 |
| BRAM36-equivalent tiles | 119 | 119 | 0 |
| DSP48E1 | 83 | 83 | 0 |

Physical occupancy falls to 12,571/13,300 slices (94.52%), leaving 729 free.
The exact Beast Vivado 2024.2 `Performance_Explore` route connects all 68,686
routable nets with zero routing errors. It still fails the 5.000 ns domain at
WNS/TNS `-0.967/-1540.748 ns` across 7,745 endpoints; hold is `+0.007 ns` and
pulse width is `+0.538 ns`. The limiting cone has moved away from copper fault
capture to the existing flood renderer: `active_x_q[3]` reaches the
`row_high_product_q` DSP enable through six logic levels with 5.250 ns data
delay, of which 3.281 ns is routing. The rejected bitstream was not flashed.

The exact source snapshot is
`/mnt/Documents/astra68/work/audio-v1/integration-10/source`; artifacts are in
the adjacent `full-route-15-hp1-arbiter` directory.

| Retained source or artifact | SHA-256 |
|---|---|
| `astra_axi_read_3to1.sv` | `57db5ec0c42377e2df0b7cd44104e3fd9864a2ebf00f32d3e181d4e2e1c8eeb1` |
| `tb_astra_axi_read_3to1.sv` | `5329dba86c711d492b8627b4fd9c314e9b0b129ea6b85311ecf7a05598b90e32` |
| `astra_arty_graphics_top.sv` | `71d39500584c47cea40c20d10293bb68a0e01e2ae010995a35b129d82745ca2a` |
| `build_graphics.tcl` | `a0b9987317864413eee3d3ea89c03358c761b5dcd2a299f8a20d68b41b1939b9` |
| Routed DCP | `2de8646f526e58c6418e22ec32a807922dc7b5d2b88c3f648f5514452e28787c` |
| Timing report | `0216a350c1101e7caa3302283c4569873799bec636663e6c009d1ce1af70d688` |
| Utilization report | `ba5077cfa6834cfd439a1551200d6dbe7c6e6da3ab5378f23ddf4e789bba3292` |
| Route-status report | `585a5bf4a8fff36c64cc21e9b57eedffa6ca1027e722576554b1bfeeac7608dc` |
| Rejected bitstream | `8feacac378e9782804585b997c494f3293793098ced3c61ce2ab79fa0b045487` |
| XSA | `f8947c643f322030e71781ad6345e8f332e08a2cf7cfce2c63c388d95c3d087d` |

### Route 16 rejected flood enable attribute experiment (2026-08-11)

Route 16 tested `extract_enable="no"` on the flood renderer's inferred DSP
operand and result registers. Vivado absorbed those registers into the same
DSP48E1 input and output stages and reconstructed the same data-dependent
`CEA2` cone. Synthesis, placement, routing checksums, resources, and routed
timing matched Route 15; the exact result remained WNS/TNS
`-0.967/-1540.748 ns`, hold `+0.007 ns`, with 34,528 LUTs, 40,121 registers,
12,571 occupied slices, 876 unique control sets, 119 BRAM36-equivalent tiles,
and 83 DSP48E1s. The failed experiment is not retained in RTL and its rejected
bitstream was not flashed.

Artifacts are in
`/mnt/Documents/astra68/work/audio-v1/integration-10/full-route-16-flood-enable`.

| Rejected source or artifact | SHA-256 |
|---|---|
| `astra_render_flood.sv` with ignored attributes | `69d8ce90121ab24be0c185d73324e41da8bf242d932fc6953ce0a5b30a34d6fa` |
| Routed DCP | `b8f688ec85f74fa79700e627a8f742251b947ef479b8bda4fccd6fbd8162b64c` |
| Timing report | `599495e6ad16eab59dfe5792980b69f730e51fd3282155ac0bcafe7eb85289e1` |
| Utilization report | `844a45bb7aff3b0c9c77be0517a067b93e2bee6496ed01b5894ca18716565885` |
| Route-status report | `585a5bf4a8fff36c64cc21e9b57eedffa6ca1027e722576554b1bfeeac7608dc` |
| Rejected bitstream | `b1fb81723d9847a8e29abc7948621e3f5015cf1573f7915dfde339c07362c333` |
| XSA | `3945928771dea2d7eebdac5c011459c16868f2bfa247e084ba1dfe070eddac64` |

### Route 17 registered flood arithmetic checkpoint (2026-08-11)

Route 17 replaces the ignored flood synthesis hint with explicit use of the
existing registered address-valid pipeline: operands capture on
`address_start_q`, the two DSP products update on
`address_operand_valid_q`, and the row sum updates on `address_valid_q`. The
focused flood test and complete graphics regression pass. The former
six-level `active_x_q` to DSP `CEA2` cone is absent from routed critical paths.

| Exact full-route resource | Route 15 | Route 17 | Delta |
|---|---:|---:|---:|
| Slice LUTs | 34,528 | 34,522 | -6 |
| Slice registers | 40,121 | 40,078 | -43 |
| Physical slices | 12,571 | 12,496 | -75 |
| Unique control sets | 876 | 891 | +15 |
| BRAM36-equivalent tiles | 119 | 119 | 0 |
| DSP48E1 | 83 | 83 | 0 |

The exact Beast Vivado 2024.2 `Performance_Explore` route connects all 68,686
routable nets without error and improves WNS by 63 ps to `-0.904 ns`. It still
fails release timing: TNS is `-2089.857 ns` across 9,317 endpoints, hold is
`+0.015 ns`, and pulse width is `+0.538 ns`. The new worst path starts at the
HP1 PS-side response slice SRL, crosses four LUT levels of RID/client response
decode, and reaches tile 1 pattern BRAM `ENBWREN`: 5.319 ns data delay, of
which 3.217 ns is routing. The rejected bitstream was not flashed.

Artifacts are in
`/mnt/Documents/astra68/work/audio-v1/integration-10/full-route-17-flood-valid`.

| Retained source or artifact | SHA-256 |
|---|---|
| `astra_render_flood.sv` | `cd185fc515e3fabda53f9a5daf43a8a3ca1557a5050dcdf7dd2d41cc4fa2e4ad` |
| Routed DCP | `9caa0308b1268e9388b39edbf064b2d2306747a785e6af0871b1c4b61ba0396e` |
| Timing report | `639a1acfb109458b7c51dd853a5f1d7d7ab348c9e8bbd6b003ec463e834c7761` |
| Utilization report | `7ca8243275eac2fb66ec364742721a637872402fb11f8bde69ab0d3183aeafbf` |
| Route-status report | `5bb8773e6a567848560d1b9736b7eb20890a342867e79ea8c5183a04f501b78a` |
| Rejected bitstream | `4bea47e16c390fef370c8814dbf246aa107512a10432a1e44fc9a87531aeaa9d` |
| XSA | `c6f2bb1f81f8eaf5034d0388f94a8775755b40d293928e74c6fb66ccc0c79fc5` |

### Route 18 HP1 response-slice checkpoint (2026-08-11)

Route 18 changes only HP1's PS-side read-response channel from AMD AXI
register-slice mode 9 to mode 1. AMD's installed IP source identifies mode 9
as an SRL-backed source-interface FIFO and mode 1 as the fully registered
forward/reverse channel. Mode 1 retains one-beat-per-cycle throughput and AXI
backpressure while removing the measured 1.606 ns SRL clock-to-Q boundary.
Vivado block-design validation passes.

| Exact full-route resource | Route 17 | Route 18 | Delta |
|---|---:|---:|---:|
| Slice LUTs | 34,522 | 34,279 | -243 |
| Slice registers | 40,078 | 40,205 | +127 |
| Physical slices | 12,496 | 12,584 | +88 |
| Unique control sets | 891 | 878 | -13 |
| BRAM36-equivalent tiles | 119 | 119 | 0 |
| DSP48E1 | 83 | 83 | 0 |

The exact Beast Vivado 2024.2 `Performance_Explore` route connects all 68,573
routable nets without error. WNS improves 45 ps to `-0.859 ns`; TNS improves
by 1,561.306 ns to `-528.551 ns`, and failing endpoints fall from 9,317 to
4,044. Hold is `+0.007 ns` and pulse width is `+0.538 ns`. The former HP1
SRL-to-tile-BRAM path is absent. The new worst path is a seven-LUT copper
execute/retire enable cone from `execute_w0_q[2]` to
`retire_beam_action_q[0]/CE`, with 5.432 ns data delay dominated by 4.108 ns
of routing. Physical occupancy rises by 88 slices despite the LUT reduction,
so Route 18 is retained for its large timing gain, not as an area win. The
timing-failed bitstream was not flashed.

Artifacts are in
`/mnt/Documents/astra68/work/audio-v1/integration-10/full-route-18-hp1-reg1`.

| Retained source or artifact | SHA-256 |
|---|---|
| `build_graphics.tcl` | `81f550ac9edc47532990b873c5300af3b5381660f52197523273fc37f53bade9` |
| Routed DCP | `00d318c790a804b37ccdd6fbcde7c9f516f188adede68ee84c8fa03bab85a9d5` |
| Timing report | `577cae1e0e89910f80e05fce212268b99a191f822e60216b19e690318a8847c9` |
| Utilization report | `f33d3a3ec9e952df4c86447fbee8c56da2c1b628b01819619789186bb2c89d8a` |
| Route-status report | `6a5e995293fc36edd9fb177916fe92ee52cb1645c49b9ca1d772d6f8321a2f21` |
| Rejected bitstream | `3ef71eb3be5fafa6809fd8228a5caacd1fcc04cc80863356805aff58c72eee1f` |
| XSA | `34d754ca12089248e628e08b65ca3a1b8f1b4a52ddab8beb38839423fa506200` |

### Routes 19-26 production timing checkpoints (2026-08-11 to 2026-08-12)

All eight checkpoints are clean, nonincremental Beast Vivado 2024.2
`Performance_Explore` routes of the complete 200 MHz production design from
`/mnt/Documents/astra68/work/audio-v1/integration-10/source`. HDMI audio,
front-panel MMIO, and every graphics engine remain enabled. Each route failed
the 5.000 ns release constraint and its bitstream was rejected and not flashed.

| Route artifact | WNS / TNS | Failing endpoints | Hold | LUTs | Registers | Slices | Control sets | BRAM36 | DSP48 | Disposition |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| `full-route-19-copper-retire` | -0.845 / -1059.367 ns | 6,104 | +0.017 ns | 34,277 | 40,101 | 12,531 | 876 | 119 | 83 | Retained Copper retire predecode; fault-enable cone remained. |
| `full-route-20-dispatch-completion` | -0.755 / -457.076 ns | 2,863 | +0.019 ns | 34,353 | 40,142 | 12,497 | 885 | 119 | 83 | Retained dispatch completion register; command-to-glyph enable became worst. |
| `full-route-21-ready-validation-pipes` | -0.701 / -541.596 ns | 3,926 | +0.009 ns | 34,395 | 40,205 | 12,552 | 889 | 119 | 83 | Retained ready and structural-validation pipes; glyph blend became worst. |
| `full-route-22-glyph-copper-status` | -1.469 / -3037.325 ns | 9,965 | +0.014 ns | 34,325 | 40,199 | 12,514 | 874 | 119 | 86 | Rejected glyph DSP formulation; three added DSPs and worse glyph blend path. |
| `full-route-23-glyph-product-stages` | -0.642 / -618.298 ns | 4,295 | +0.021 ns | 34,239 | 40,160 | 12,645 | 873 | 119 | 83 | Retained registered glyph products; blitter pixel formation became worst. |
| `full-route-24-blitter-hp1-buffers` | -0.511 / -141.603 ns | 1,410 | +0.014 ns | 34,333 | 40,214 | 12,604 | 883 | 119 | 83 | Retained blitter stages and HP boundaries; flood DSP enable became worst. |
| `full-route-25-shared-boundaries` | -0.683 / -679.573 ns | 4,042 | +0.009 ns | 34,325 | 40,296 | 12,616 | 877 | 119 | 83 | Retained shared boundary registers; Copper BRAM-to-execute path became worst. |
| `full-route-26-pipelined-boundaries` | -0.508 / -155.965 ns | 1,591 | +0.018 ns | 34,308 | 40,452 | 12,646 | 899 | 119 | 83 | Retained boundary pipes; active-list range fault enable became worst. |

The Route 26 worst path is
`active_end_q[7]` to `fault_pc[1]/CE`, through the active-list PC range
predicate. Other repeatedly failing groups were glyph source-state selection,
flood stack-capacity/state selection, sprite baseline-to-active palette
restore, and blitter state/address selection. Route 27 pipelines those measured
boundaries only; its exact artifact directory is
`full-route-27-fault-glyph-flood-palette`.

### Route 27 measured boundary pipeline checkpoint (2026-08-12)

Route 27 is the exact full Beast Vivado 2024.2 `Performance_Explore` route in
`full-route-27-fault-glyph-flood-palette`. It connects all routable nets with
no route or DRC errors, but fails release timing at WNS/TNS
`-0.581/-228.464 ns`; hold is `+0.014 ns`. Utilization is 34,322 LUTs, 40,501
registers, 12,630/13,300 slices (94.96%), 880 control sets, 119 BRAM36 tiles,
and 83 DSP48E1s. The rejected bitstream was not flashed.

The former Route 26 range-fault, glyph source-state, flood capacity, and sprite
palette-restore boundaries are absent from the top paths. The new measured
leaders are Copper instruction-to-state decode (`-0.581 ns`), flood
span-end-to-read-enable classification (`-0.558 ns`), and Copper visual-target
decode to framebuffer-key enables (`-0.555 ns`). Route 28 splits those three
existing transitions without changing the production feature set.

### Route 28 decode, span, and visual-commit checkpoint (2026-08-12)

Route 28 is the exact full Beast Vivado 2024.2 `Performance_Explore` route in
`full-route-28-decode-span-commit`. It keeps the complete 200 MHz production
feature set and connects every routable net without error, but fails release
timing at WNS/TNS `-0.635/-315.945 ns` across 2,451 endpoints; hold is
`+0.018 ns`. Utilization is 34,519 LUTs, 40,477 registers, 12,547/13,300
slices (94.34%), 890 control sets, 119 BRAM36-equivalent tiles, and 83
DSP48E1s. The rejected bitstream was not flashed.

The three Route 27 leaders are absent. The new worst path starts at a sprite
validation-bank BRAM and ends at `validate_w1_q` (`-0.635 ns`); the next
measured groups are glyph state to blend-DSP enable, ellipse state to Y
capture, and flood seed-Y to neighbor bounds. Hierarchical utilization showed
that the sprite collision history alone occupied 864 LUTRAM cells while 21
BRAM tiles remained free, motivating Route 29.

### Route 29 collision-history BRAM checkpoint (2026-08-12)

Route 29 is the exact full Beast Vivado 2024.2 `Performance_Explore` route in
`full-route-29-collision-bram`. The focused functional sprite, exhaustive
64-way collision, and integrated graphics-pipeline simulations pass. The
published collision history moves to synchronous BRAM, raising utilization to
127/140 BRAM tiles while retaining every production feature.

The route connects every net without error but regresses to WNS/TNS
`-0.933/-911.430 ns` across 4,654 endpoints; hold is `+0.020 ns`. Utilization
is 34,618 LUTs, 40,942 registers, 12,628/13,300 slices (94.95%), 127
BRAM36-equivalent tiles, and 83 DSP48E1s. The rejected bitstream was not
flashed. Every leading path starts at a collision-history BRAM output, crosses
the eight-bank selector, and reaches an AXI read-data register in the same
5.000 ns cycle. Route 30 restores the existing registered read boundary and
pipelines collision-table address/read/commit handling. Its focused routed
sprite checkpoint is timing-clean at setup/hold `+0.096/+0.027 ns` with 36
BRAM primitives.

### Route 30 collision pipeline checkpoint (2026-08-12)

Route 30 is the exact full Beast Vivado 2024.2 `Performance_Explore` route in
`full-route-30-collision-pipeline`. It retains the complete 200 MHz production
feature set and connects every routable net without error, but fails release
timing at WNS/TNS `-0.438/-287.335 ns` across 2,397 endpoints; hold is
`+0.050 ns` and pulse-width slack is `+0.538 ns`. Utilization is 34,568 LUTs,
41,556 registers, 12,531/13,300 slices (94.22%), 911 control sets, 127
BRAM36-equivalent tiles, and 83 DSP48E1s. The rejected bitstream was not
flashed.

The collision BRAM paths that dominated Route 29 are absent. The measured
leaders are the graphics-control commit state through same-cycle AXI-Lite
splitter completion (`-0.438 ns`), duplicate Copper runtime MOVE validation
(`-0.389 ns`), commit state through Copper frame start (`-0.384 ns`), renderer
flush/abort handshakes (`-0.383/-0.363 ns`), and the geometry line-error update
(`-0.331 ns`). Route 31 registers or removes only those measured paths.

| Route 30 artifact | SHA-256 |
|---|---|
| Routed DCP | `233544ad3f2a664056fd009f3d8e5d0e44148caaed011b1b485b122e1d4d9bc7` |
| Timing report | `daf4fca55d6fae8e6c2baef92094f1b7595972e62c38616e59934e2faafb939b` |
| Utilization report | `00db4c354076b9f78ea45cf8a9477a2c8f581f39b2573b0ba2c7d2026a2bc9c5` |
| Route-status report | `94334fd7323afd66471889dd6f6b634870e1b7f2bae6858326899ba97851b90f` |
| Rejected bitstream | `969d4f1d853ff82f866835bfd6681faa090e5265f7511194bd0d5c0c65fa3f20` |
| XSA | `2252c61ba6f98ab44086f5ef674a562be24a0badee9d284721b54042e5be0cea` |

### Route 31 pre-route command-engine checkpoint (2026-08-12)

Route 31 retains only measured boundary cuts: registered AXI-Lite split
completion, Copper validation/frame-start stages, renderer admission/abort
stages, parallel one-hot geometry decode, queued geometry color, and the
compact glyph state path. The first attempt incorrectly registered the
writer's flush `ready` handshake; the integrated command test exposed a lost
flush and timeout after recovery. That change was removed. The live
ready/valid contract is restored, and the exact synchronized graphics
regression passes, including all 45 command-processor submissions.

The retained Vivado 2024.2 command-engine OOC route is
`render-command-ooc-route31g`: WNS `-0.206 ns`, 10,650 LUTs, 13,686
registers, and 29 DSP48E1s. Its remaining path is mask-cache address compare
to AXI read request. Two measured experiments were rejected: sequential
blitter FSM encoding routed at `-0.340 ns`, and an extra mask-compare pipeline
routed at `-0.423 ns`. Neither is in the production source. OOC placement is
diagnostic; the next authority is the exact complete Route 31 production
route.

| Route 31 OOC artifact | SHA-256 |
|---|---|
| Routed DCP | `91728fd57c1515300239ec0436c465bc7dcfac051a179a3c5892e53af27ef02d` |
| Timing summary | `5c4d9c062b392b9c08f46c46791476b06c51dedbed865f2a44e0494e5078d725` |
| Timing paths | `47d09c5c0716f78a577699f7704ae3c6a5bb5bb227b7719aada6b9a371760ff3` |
| Utilization | `adbd6b8504a047ad48993d93b7df54078b4c42ed6829d6c69b6f1c8563cd5455` |
| Route status | `cb3a16a85b2d893b3fd0529774d7d09fd0a7b2445a58dafbb923f6ac7d541148` |

### Route 31 full measured-boundaries checkpoint (2026-08-12)

Route 31 is the exact full Beast Vivado 2024.2 `Performance_Explore` route in
`full-route-31-measured-boundaries`. The synchronized complete graphics
regression passes and all 69,656 routable nets connect without error. Release
timing fails at WNS/TNS `-0.552/-153.474 ns` across 1,358 endpoints; hold is
`+0.039 ns` and pulse-width slack is `+0.538 ns`. Utilization is 34,170 LUTs,
41,714 registers, 12,710/13,300 slices (95.56%), 916 control sets, 127 BRAM36
tiles, and 83 DSP48E1s. The generated bitstream was rejected and not flashed.

The new worst path is the pixel writer's `barrier_pending` state through the
raw flush-ready contract into geometry state (`-0.552 ns`). The next measured
groups are sprite descriptor validation to LUTRAM write enable (`-0.398 ns`)
and Copper dispatch completion to the graphics submission-producer enable
(`-0.385 ns`). Route 32 must buffer the writer flush request without presenting
a stale `ready`, then address only the remaining measured producer boundaries.

| Route 31 full artifact | SHA-256 |
|---|---|
| Routed DCP | `a0075bdd9033c74df88b43a01d8718f0910795b1d9e3e6cf12fcecbb8be80be1` |
| Timing summary | `f7352820aa51fab4fe3d135af8d7d0506fe96d7d8df6c63763fd4ceba7fa89dd` |
| Utilization | `43099833b7219cddf5a62429c62dbdc8b17386b681018f101db2758ac27d6080` |
| Route status | `3cf3e85a65dc685eccd62d15be6365a2ddc90abc739c39e34b70f433e52b0fae` |
| Rejected bitstream | `f77cfca7d07014b8327c9d5a6a579d788b6c65a3a9a47a3f0fc65ac6adf9dca8` |
| XSA | `c0c0f17e1f6e8e17c858ada9486c24ef3d2f1376e528a3f3014250fcc8aa1c3e` |

### Route 32 pre-route registered-boundary checkpoint (2026-08-12)

Route 32 targets only Route 31's three measured path groups. A one-entry
request buffer holds writer flush until queued pixels drain and the writer
accepts it, so engine completion no longer depends combinationally on writer
barrier state. Sprite validation now branches into separate accept/reject
states before descriptor RAM writes. Copper endpoint range results and the
published producer endpoint are registered before the renderer update.

The exact Beast graphics regression passes, including 131,072 sprite scaling
pairs, the integrated graphics pipeline, and all 45 render-command processor
submissions. The authoritative next checkpoint is the complete production
Route 32 build; no Route 32 image may be flashed unless all constrained clocks
pass.

| Route 32 retained source | SHA-256 |
|---|---|
| Render command processor | `8cccd9a4780f08b937db2ba29af166805249373173a5fba6f62aee89188160da` |
| Sprite scene store | `d43c6af3150a8b520e542a525f98b99bdfa9b0a55883fc652031583077823830` |
| Graphics control | `c1a97a6067202f5a105d05a21ee17af83a063b350d568849c2fe15d7d36d0673` |
| Graphics-control test | `8ae98e748eef0e4f2024468064bb31fc2ea59d7fbde7b4d936db954462b56c60` |

### Route 32 full registered-boundaries checkpoint (2026-08-12)

Route 32 is the exact full Beast Vivado 2024.2 `Performance_Explore` route in
`full-route-32-registered-boundaries`. The complete graphics regression passes
and all 69,584 routable nets connect without error. It improves release timing
to WNS/TNS `-0.298/-64.437 ns` across 877 endpoints; hold is `+0.006 ns` and
pulse-width slack is `+0.538 ns`. Utilization is 34,199 LUTs, 41,526
registers, 12,540/13,300 slices (94.29%), 895 control sets, 127 BRAM36 tiles,
and 83 DSP48E1s. The generated bitstream is rejected and was not flashed.

Route 31's three leading cones are gone. The new measured leaders are the
Copper event FIFO full comparison into BRAM write enable (`-0.298 ns`), pixel
writer error completion into geometry state (`-0.298 ns`), sprite blend alpha
into DSP input (`-0.293 ns`), and flood neighbor-row range evaluation
(`-0.292 ns`). Route 33 targets only these measured boundaries.

| Route 32 full artifact | SHA-256 |
|---|---|
| Routed DCP | `dafe8b65bae95401aea24e611b1d8c34bdefc20fffa03fb4e2117c0ee8736cb6` |
| Timing summary | `e61ba06d639721cff9923fd92d597eddafb3ce67f764e09ad18745ba1952f626` |
| Utilization | `ce5f7fcacd0984d2f1a71cc90caa94133521951a3767a38fb2cfc9fab3773c05` |
| Route status | `c6414907332b00ab8382c2fac08355de8354331e090946b65da139f1c1c81f41` |
| Rejected bitstream | `2303cac50397b79dc55b34cd44197f9b8fd5402cac596edb89bf8258605bf546` |
| XSA | `3975f6aee0911b156d2ed0719ebe73b87a5b537043955d84301afb3efcc25075` |

A post-route `Explore` physical-optimization experiment on the Route 32 DCP
improved the live WNS only to `-0.280 ns` before Vivado 2024.2 terminated with
signal 11. It produced no checkpoint and is rejected. Route 33 therefore uses
structural boundaries rather than further post-route or seed experiments.

### Route 33 full registered-hotpaths checkpoint (2026-08-12)

Route 33 registers the asynchronous FIFO full decision, renderer writer
completion, inverse blend alpha, and flood neighbor-row decision identified by
Route 32. The complete graphics regression passes, including HDMI audio FIFO
capacity/overflow, Copper pixel events, sprite blending, flood fill, the
integrated pipeline, and all 45 render-command submissions.

The exact full Beast Vivado 2024.2 `Performance_Explore` route is
`full-route-33-registered-hotpaths`. All 69,743 routable nets connect without
error. Release timing improves slightly to WNS/TNS `-0.271/-18.549 ns` across
344 endpoints; hold is `+0.019 ns` and pulse-width slack is `+1.116 ns`.
Utilization is 34,286 LUTs, 41,674 registers, 12,781/13,300 slices (96.10%),
914 control sets, 127 BRAM36 tiles, and 83 DSP48E1s. The generated bitstream is
rejected and was not flashed.

Route 32's four leading cones are gone. The new leaders are graphics-control
AXI write data through response validation (`-0.271 ns`), the buffered writer
flush request into glyph state (`-0.204 ns`), Copper runtime validation
(`-0.188 ns`), and Copper endpoint arithmetic through dispatch completion
(`-0.175 ns`).

| Route 33 full artifact | SHA-256 |
|---|---|
| Routed DCP | `0ba47181914b68b1173d34cd933cbe36f96973e6193b1680bc855cea3a211f6a` |
| Timing summary | `113faa61c39663270b7f1df4337b74c957b24cef6779cc8aaac84a38ec171149` |
| Utilization | `0489c2eee9de94b0d821fc1a2d098ccdabdeb48a51c0450599e76683f75aad58` |
| Route status | `d469ea33579c01c66fdd7e5ef3c7318bdc23c6075f6f4adeb6ff50bff533c614` |
| Rejected bitstream | `6644a7005ccf31bb3e0190e00f73e1eac62ab067c33e717ddb1c57e921e615a3` |
| XSA | `2b8c8adc8673b9c8f30989d12915ac060c4859eb53565f5e63c945f11be2ca4c` |

| Route 33 retained source | SHA-256 |
|---|---|
| Async FIFO | `f36c68d091a7030a58dbec23ef7c6cf03747e23260282ce5d13a0cedbdab3a2b` |
| Render command processor | `ddc95c467480430b302fa3bb6b1496af493cb43e3cb99bb4c3076ba5e90f7afe` |
| Premultiplied blend | `370dd7501c51b3ccab1810e8a201acb70cb1634b8809d68734519e525f56b5a7` |
| Flood renderer | `bea7b2a9121a503f4382a4954130b03640b0656fe9347a66309cc81d371fcdb5` |

A retained post-route `AggressiveExplore` checkpoint made no placement,
routing, setup, or hold change: WNS/TNS stayed `-0.271/-18.549 ns` and hold
stayed `+0.019 ns`. Vivado explicitly reported that the graphics-control AXI
response cone could not be improved. Route 34 therefore breaks that protocol
path structurally rather than searching another implementation seed.

### Route 34 pre-route protocol-boundary checkpoint (2026-08-12)

Route 34 publishes AXI write responses from a separate registered stage,
returns Copper endpoint validation as a registered `ready`/`allowed` result,
splits Copper instruction checking from its action decision, and makes all
renderer flush requests held-valid until accepted. The complete synchronized
graphics regression passes, including success/rejection dispatch contracts,
all exhaustive sprite cases, the integrated pipeline, and all 45 renderer
submissions. The next authority is the exact complete Route 34 production
route; no Route 34 image may be flashed unless all constrained clocks pass.

| Route 34 retained source | SHA-256 |
|---|---|
| Graphics control | `ae6ce2f18c62a3a4c40fe8f8d6ff65711ee34661599c8f00568d81825671ebea` |
| Copper control | `b35e81c6988868c882e2de703302abe7cf9ad4b45eeed8608795ec6dfd3ef47a` |
| Copper core | `bec2826e41bd45995616e5f3b8e00991b2d83dad1e01d64af149ccba55023a6e` |
| Glyph renderer | `6cbeb2fb87770c71e3106a6f86ca2866536cfd264e6ba624e7200c0020a3ebe5` |
| Flood renderer | `e32fa29e04734f05331026eab0256973a330056a8fbc0360a2ab5e9c3b088fb5` |
| Geometry renderer | `4e757410a2a42560d8e89d4b0c539871dfdcba680ae912a16d763878c14206ab` |

### Route 34 full protocol-boundary checkpoint (2026-08-12)

The exact full Beast Vivado 2024.2 `Performance_Explore` route is
`full-route-34-protocol-boundaries`. All 70,210 routable nets connect without
error, but setup regresses to WNS/TNS `-0.364/-24.525 ns` across 372 endpoints;
hold is `+0.018 ns` and pulse-width slack is `+0.538 ns`. Utilization is 34,390
LUTs, 41,839 registers, 12,749/13,300 slices (95.86%), 911 control sets, 127
BRAM36 tiles, and 83 DSP48E1s. The generated bitstream is rejected and was not
flashed.

The former graphics-control response path is absent from the leading group,
but the registered boundary shifts routing unfavorably. The new worst paths
are tile-map response tag/transport/bounds validation (`-0.364/-0.316 ns`), a
sprite working-RAM read-address mux (`-0.302 ns`), Copper execute decode
(`-0.300 ns`), and flood stack-count enables (`-0.239 ns`). Route 35 first
splits the measured tile-map validation cone without reducing response
throughput.

| Route 34 full artifact | SHA-256 |
|---|---|
| Routed DCP | `f2d6172f8448e116f480dde0ac1f3dee60addccac339117cfbbab0dde90dce5` |
| Timing summary | `2e602516163ede57009648a61237ff10c1112a516e7883ab9e87ebef7cd95590` |
| Utilization | `a9e121c46e57216da2586b95b4dc83fb964dcb0cedfb49af578ed96f2b0b70a6` |
| Route status | `15ebe66848554ab2174ef2da3bf838d71490a710113c93cea463b5c7dd2af84c` |
| Rejected bitstream | `960d6019095e4448ab101bf854abdcabc3664dda30d2c0be1364f24f7ec630e0` |
| XSA | `879225093523e597065aff659d8e1501dadea837bd3c5855014ca39c8c9b2599` |

### Route 35 rejected tile-response pipeline (2026-08-12)

Route 35 adds a third tile-map response stage after the existing capture
boundary. The complete regression passes, including the 1,180/4,444-cycle
tile deadline, exhaustive sprite cases, integrated pipeline, and all renderer
submissions. The exact full Beast Vivado 2024.2 `Performance_Explore` route is
`full-route-35-tile-response-pipeline`. It connects all 70,001 routable nets,
but regresses setup to WNS/TNS `-0.417/-60.621 ns` across 667 endpoints; hold
is `+0.007 ns` and pulse-width slack is `+0.538 ns`.

The Route 34 tile validation cone is gone, but the extra stage changes packing
and exposes tile pattern-BRAM output (`-0.417 ns`), Copper dispatch result
(`-0.339 ns`), glyph blend input (`-0.335 ns`), and blitter cache qualification
(`-0.316 ns`). Utilization is 34,433 LUTs, 41,721 registers, 12,634/13,300
slices (94.99%), 918 control sets, 127 BRAM36 tiles, and 83 DSP48E1s. The
generated bitstream is rejected and was not flashed. The added stage is not
retained; the next experiment reuses the existing capture boundary and removes
the redundant descriptor-result registers.

| Route 35 rejected artifact | SHA-256 |
|---|---|
| Source tile builder | `ea307088ab0336d1feeaa4fb5946b3cadc70470941d87884ba38237c22046f19` |
| Routed DCP | `f9036ca751e10e02c0b229b31356fa8e40f2d4fdefc666a3762456e401406ea4` |
| Timing summary | `7cda02cf261c123e10585c8682631b23d40ddead3b4306afd1052c903261067f` |
| Utilization | `6984a2c323d41c1ffa67f26428b76de068c24678dcf67d3cd3c063ba7bdfbe0d` |
| Route status | `e81631abe9628d2780088a14f6410d6a120ac21d67d58f2431b8f5fdd342d51b` |
| Rejected bitstream | `b6be44edfcaec2e8e38c4f9c898d2dab1ae15a56c90f12ee069b983d497b2eab` |
| XSA | `5de9a089e46b303578dbeec9613f924072ae895e4ff69ce94dabfe7f48bccc1c` |

### Route 36 pre-route existing-capture validation (2026-08-12)

Route 36 removes Route 35's third response stage and the two redundant
descriptor-result registers. Bounds validation now consumes the already
registered map entry and transport result on the existing descriptor-write
cycle, sustaining one response per clock. The complete regression passes; the
tile performance case improves from 1,180 to 1,179 clocks against the 4,444
clock deadline. The exact full route is the next authority.

| Route 36 retained source | SHA-256 |
|---|---|
| Tile line builder | `0d0af748fd5cc4100680ef315ee4158b2ee19422f340a89b754ec48f5b56fc55` |

### Route 36 rejected existing-capture validation (2026-08-12)

The exact full Beast Vivado 2024.2 `Performance_Explore` route is
`full-route-36-existing-capture-validation`. All 69,772 routable nets connect
without error, but setup regresses to WNS/TNS `-0.480/-385.768 ns` across
3,370 endpoints; hold is `+0.016 ns` and pulse-width slack is `+0.538 ns`.
Utilization is 34,337 LUTs, 41,613 registers, 12,700/13,300 slices (95.49%),
127 BRAM36 tiles, and 83 DSP48E1s. The generated bitstream is rejected and was
not flashed.

The tile-response validation cone is absent, but the packing change exposes a
broad group led by render-command admission state into the manager AXI read
address enable (`-0.480 ns`), blitter blend result (`-0.432 ns`), graphics
control write decode into shadow framebuffer enables (`-0.389 ns`), and Copper
register write decode (`-0.374 ns`). The worst path is 78.1% routing delay, so
another tile-response pipeline change is not justified by this result.

| Route 36 rejected artifact | SHA-256 |
|---|---|
| Source tile builder | `0d0af748fd5cc4100680ef315ee4158b2ee19422f340a89b754ec48f5b56fc55` |
| Routed DCP | `4e3c13428219be0609ef07789c2dd889aaedeba2f58fc97e4e867476184acfa8` |
| Timing summary | `e8e05968a1b94cb00dc5dbdc76a808e0cc8f111da1389b83bf40cb1e5bc2c364` |
| Utilization | `bc0c9dfb10b58b9910ffe096baf9e02cbf014becaa0a0310cbba82225fb689f7` |
| Route status | `8b22d50503a18d6c22e273e99b226b037c40c91dd575a2ab23add767fbf97606` |
| Rejected bitstream | `5b512a1e9b19ea0f42db88d6a4666b40e73053ded4c735629205831e78ecd9b3` |
| XSA | `c4e466a2b1457d5a310b549252af60eb42dfc96fefa50eefeb212bec1c50ad12` |

### Route 37 rejected pixel-FIFO RAM inference (2026-08-12)

Route 37 removes reset initialization from the renderer pixel writer's FIFO
payload arrays while retaining reset on every validity, pointer, count, abort,
and error state. The focused pixel-writer test and complete graphics regression
pass. Vivado infers the 16x64-bit data array as one RAMB36E1 and the address and
strobe arrays as distributed RAM.

The exact full Beast Vivado 2024.2 `Performance_Explore` route is
`full-route-37-pixel-fifo-ram`. All 68,221 routable nets connect without error,
but setup remains short at WNS/TNS `-0.396/-39.477 ns` across 477 endpoints;
hold is `+0.014 ns` and pulse-width slack is `+0.538 ns`. Utilization falls to
33,854 LUTs, 39,917 registers, 12,570/13,300 slices (94.51%), 904 control sets,
128 BRAM36-equivalent tiles, and 83 DSP48E1s. The generated bitstream is
rejected and was not flashed.

The register and slice reduction is retained, but consuming a complete BRAM36
for 1,024 payload bits is not: it leaves only twelve BRAM36-equivalent tiles
free and imposes a hard placement on a shallow writer FIFO. The next measured
experiment keeps the reset-free RAM contract and directs only the 16x64-bit
data array to distributed RAM. Route 37 is led by geometry state to emit-valid
(`-0.396 ns`, 78.2% routing), flood right-bound to row-product enable
(`-0.311 ns`), and geometry state to multiply-operand reset (`-0.264 ns`).

| Route 37 rejected artifact | SHA-256 |
|---|---|
| Source pixel writer | `089c43bf7a5e0a520c52f620cd92ac2b6df97d0b99c3be2751b55368f1c7f531` |
| Routed DCP | `93a06e0556134e3927b75bab51ccaa240d4aa960d9c53421705f48cf52c1ff8a` |
| Timing summary | `4a7a3ddd291dafaffa50098ef668d894635a144656266072a87aadc76f8c42ed` |
| Utilization | `00e89ce36ce706b5623ad3050a95d62621225d251bced809aa31b9f55ef8233c` |
| Route status | `75a15c8418a7b1bfb51f0b9d738faa0a624caca58346ac3a62deb627b0d6c95b` |
| Rejected bitstream | `6f071f0e79beb67d517fd7b7cf9288816f391269e3057af372f9267521b9d55b` |
| XSA | `5942fff8a06d69e404a009f52a23aa013544bd605e1861c754e679c1024e26b5` |

### Route 38 rejected pixel-FIFO distributed RAM (2026-08-12)

Route 38 adds only a `ram_style="distributed"` synthesis directive to the
reset-free 16x64-bit pixel FIFO data array. The complete regression passes and
synthesis implements it as eleven RAM32M primitives, returning the accidental
RAMB36E1 consumed by Route 37.

The exact full Beast Vivado 2024.2 `Performance_Explore` route is
`full-route-38-pixel-fifo-lutram`. All 68,387 routable nets connect without
error, but setup remains short at WNS/TNS `-0.368/-84.862 ns` across 1,015
endpoints; hold is `+0.009 ns` and pulse-width slack is `+0.538 ns`.
Utilization is 33,914 LUTs, 40,120 registers, 12,609/13,300 slices (94.80%),
885 control sets, 127 BRAM36-equivalent tiles, and 83 DSP48E1s. The generated
bitstream is rejected and was not flashed.

The route returns the BRAM and improves WNS by 28 ps, so the distributed-RAM
directive remains the active source. It does not close timing: the leading
group is glyph range-multiply state into the 48-bit last-row register enable
(`-0.368 ns`, 80.9% routing), followed by blitter DSP blend output
(`-0.360 ns`) and Copper IRQ-event FIFO fullness (`-0.322 ns`). The next
experiment removes the final-step qualification from that glyph register's
enable by updating the partial row product on every multiply step; only the
last value is consumed.

| Route 38 rejected artifact | SHA-256 |
|---|---|
| Source pixel writer | `9315d4e5c84b70533d0798bb5d27ad5bc70101e9707f888eb8caa24e741285d2` |
| Routed DCP | `8d6f3fafbcf3aa2834f4cb4670a5b03e361b7d8999007ef49b6a66cd06d6cc00` |
| Timing summary | `f1db59a5f041be5007a280ea2da193bfb5fca1aba7fb7e0d4a735be8c9a0e66c` |
| Utilization | `5bc73d1ddad6e7089a2d6a6c0770d94584937a8682c4b4a764e4ed9453f72440` |
| Route status | `1c29211987a43c12ba399b4ecd8f7c39e4ed6785a6d5387ce51e900f415d8df0` |
| Rejected bitstream | `52bf437c3284bc9d36b2b110f41fcde03f26c9858decb138e1d13a22c1271860` |
| XSA | `2303c68aa38f2e5cdc898e68cb2261bc50366fe621075870c9c8b7c4517f7df1` |

### Route 39 rejected glyph row-enable removal (2026-08-12)

Route 39 updates the glyph descriptor partial-row product on every multiply
step, removing the Route 38 final-step state qualification from its 48-bit
register enable; only the final value is consumed. The complete graphics
regression passes. The exact full Beast Vivado 2024.2 `Performance_Explore`
route is `full-route-39-glyph-row-enable`. All 68,312 routable nets connect
without error, but setup remains short at WNS/TNS `-0.383/-78.332 ns` across
722 endpoints; hold is `+0.009 ns` and pulse-width slack is `+1.116 ns`.
Utilization is 33,903 LUTs, 40,084 registers, 12,438/13,300 slices (93.52%),
880 control sets, 127 BRAM36-equivalent tiles, and 83 DSP48E1s. The generated
bitstream is rejected and was not flashed.

The Route 38 last-row enable cone is gone. The new measured leaders are glyph
source-sample classification into state (`-0.383 ns`), flood state into the
pixel-format constant select (`-0.373 ns`), blitter direct-copy qualification
(`-0.357 ns`), and blitter cache qualification (`-0.342 ns`). The next
experiment registers only the glyph sample classifications already separated
by the source-decode stage, removing the two leading sample/transparent-index
comparators from the state path.

| Route 39 rejected artifact | SHA-256 |
|---|---|
| Source glyph renderer | `feda37ed2b99d51026309fa84db6666c2097319b8cdcfd2abfce48b2aa589aed` |
| Routed DCP | `d9dcb63dee15c06a3af82244b90acd3c38bb1343424ef86ed3da26eeda15fdb5` |
| Timing summary | `4943d35f76c48e38ee5028ab4d7b7e4b70324c35fb50889f69ba4bc5d61d1c89` |
| Utilization | `605cb12fd7768a366e61439fee2a27684ca8e3f2f3b63c8d8254d98944883db9` |
| Route status | `3f269f986741d0431bb2238c48ae2b742b5a0ae634a3c0d6ca5911d1e4f2f99b` |
| Rejected bitstream | `9f571b9b30ae6a54f07782b388f17c20907f11abff3ccfb17c93aca002828275` |
| XSA | `4a4b594c02bdba4f71e542aad358130f73ce60f96bb1deb7976e55907eee01d6` |

### Route 40 rejected glyph sample-classification boundary (2026-08-12)

Route 40 registers the glyph source sample's zero, format-specific full, and
transparent-index classifications in the existing source-decode stage. This
removes the Route 39 sample and transparent-index comparisons from the FSM
transition cone without adding a new state. The complete graphics regression
passes, including AFNT glyphs, integrated graphics, exhaustive sprite
dimensions, and all 45 render-command submissions.

The exact full Beast Vivado 2024.2 `Performance_Explore` route is
`full-route-40-glyph-sample-classify`. All 68,546 routable nets connect without
error. Setup improves to WNS/TNS `-0.305/-45.555 ns`; hold is `+0.007 ns` and
pulse-width slack remains positive. Utilization is 34,044 LUTs, 40,143
registers, 12,590/13,300 slices (94.66%), 920 control sets, 127
BRAM36-equivalent tiles, and 83 DSP48E1s. The generated bitstream is rejected
and was not flashed.

The Route 39 source-sample FSM cone is gone. The new measured leader is glyph
range state into the step and multiplicand clock enables (`-0.305/-0.288 ns`),
followed by flood read state into the shared HP2 response-slice enable
(`-0.302 ns`). The next experiment uses the glyph module's existing
`extract_enable="no"` pattern only on those measured range registers.

| Route 40 rejected artifact | SHA-256 |
|---|---|
| Source glyph renderer | `80534bddd711ba9104ebc8b5306edc94091d28a7b9a990b0e06e73bbe0b44840` |
| Routed DCP | `9c9215d0fc531a9f630beee1a65ff7673ba71b16c4f6e984bb7985cafd0c0b99` |
| Timing summary | `f422e4f546bfe28617f0d4e5f8d672738ed43de18285fd9bcacd2149f8fdd51c` |
| Utilization | `3e2b859e1fe317ccdbd74e5ee40e9532bd7d8ee162e159295ae67955dfe8afe7` |
| Route status | `2cf6e9b9a3bbaedb46aa3efcd19b6c783af2af377c98abf6f368a3112cce9b7e` |
| Rejected bitstream | `d91472b2e2ee4b53d39f21acd98e9cb9365f77f2e1d9163a237336c2c142ed04` |
| XSA | `5f9e2ee357b25e40f00e779e0421aee08a26a525c0ddbadc8ae68bf18632e006` |

### Route 41 rejected glyph range no-enable mapping (2026-08-12)

Route 41 applies the glyph module's existing `extract_enable="no"` synthesis
pattern to the descriptor range step and multiplicand registers reported by
Route 40. The exact complete graphics regression passes and the targeted CE
paths disappear, but the altered data-mux packing is a physical regression.

The exact full Beast Vivado 2024.2 `Performance_Explore` route is
`full-route-41-glyph-range-no-enable`. All 68,395 routable nets connect without
error, but setup regresses to WNS/TNS `-0.502/-16.127 ns`; hold is `+0.006 ns`.
Utilization is 33,999 LUTs, 40,047 registers, 12,536/13,300 slices (94.26%),
907 control sets, 127 BRAM36-equivalent tiles, and 83 DSP48E1s. The generated
bitstream is rejected and was not flashed.

The new leader is flood right-bound into the row-product DSP enable
(`-0.502/-0.339 ns`), followed by Copper dispatch endpoint validation
(`-0.255 ns`). Because Route 41 is 197 ps worse than Route 40 despite removing
the targeted glyph cone, both no-enable attributes are removed. The next
experiment retains Route 40 and removes the glyph hold/enable function directly
by allowing the otherwise-dead range step and shifted multiplicand to advance
outside the 16-cycle multiply window; the range-row state still reloads both
before use.

| Route 41 rejected artifact | SHA-256 |
|---|---|
| Source glyph renderer | `adec30bd6ef090eb1aadfed78795ae16fa11a5510f16e99ec5b65c4eec954307` |
| Routed DCP | `d7c620a62c634efc50baacd8eac11a4b4f57f48159e9efbd83cb5837b6b8f2c7` |
| Timing summary | `7d6577622d91421a9fd5b4a07f3a3a051883b8aa5215db2bdee74ff3b4a05349` |
| Utilization | `c7e65d9d534d7b59d30716ef3b8e103dc72066b6f626346fc9f7702ab75bde74` |
| Route status | `b9e7df45c9e821351579c65378ff1dc9a8b26039cacdfe9843766d03a9cfd6de` |
| Rejected bitstream | `e0464b1f52d5a20a89c998634069cff6ea84e0aa045d52c84d74fb1ba1394f48` |
| XSA | `3ea04c18e39da03c6372a66be231087b6fbfd95cd566f0f3d463a2fe2ebce7e9` |

### Route 42 rejected glyph range free-running mapping (2026-08-12)

Route 42 removes the descriptor range step and shifted-multiplicand hold
function structurally: both advance every clock outside reset and are reloaded
before each 16-cycle multiply. The complete graphics regression passes, but
the physical result is worse than the retained Route 40 baseline.

The exact full Beast Vivado 2024.2 `Performance_Explore` route is
`full-route-42-glyph-range-freerun`. All 68,367 routable nets connect without
error, but setup is WNS/TNS `-0.395/-32.417 ns` across 531 endpoints; hold is
`+0.005 ns` and pulse-width slack is `+1.116 ns`. Utilization is 33,998 LUTs,
40,090 registers, 12,413/13,300 slices (93.33%), 895 control sets, 127
BRAM36-equivalent tiles, and 83 DSP48E1s. The generated bitstream is rejected
and was not flashed.

The glyph range CE paths disappear, but the new route is led by Copper dispatch
producer validation (`-0.395 ns`), glyph state into the destination-row DSP
enable (`-0.333 ns`), flood fault-detail enable (`-0.230 ns`), and glyph source
color/classification (`-0.227/-0.202 ns`). Because free-running regresses the
retained Route 40 WNS by 90 ps, the change is removed. Route 40 remains the
active structural baseline; the next experiment leaves glyph range mapping
alone and targets the independent measured flood/HP2 response boundary.

| Route 42 rejected artifact | SHA-256 |
|---|---|
| Source glyph renderer | `7b0d7eca91764ae30ea90eb77706e93b7c436d287a8090764030b9a9e2498f90` |
| Routed DCP | `78f4b13290852db1a6d61d68a9066a056b0945f0b6496b9eb96a67b6f1bbb864` |
| Timing summary | `a581bc777ca52a00161d015b79657efffcc724501d8e8f57aeda59d7975f52d6` |
| Utilization | `45200c4f59df6cbf5a697c3e6d5e052de00781cf415682771f30caf87e206047` |
| Route status | `8922ff275dd3d53e1ad97a4c93db901a4bfc8526b772b32f8704ade38fd10b30` |
| Rejected bitstream | `55647a607151b747ed0c0c4b71bc740d0853064f1f23e790ce979e2cdfc47a97` |
| XSA | `5977eafa40802d1a4b064608fc25cab3812789bc45c2eaa93181915f0c1dfe07` |

### Route 43 rejected flood response-ready simplification (2026-08-12)

Route 43 preasserts the flood engine's AXI read-response ready signal. Flood
issues only single-beat reads, has at most one outstanding transaction, and
always consumes the response into its existing data register, so no state or
buffer is required to provide backpressure. A focused regression first proves
the former state-qualified signal is not preasserted, then passes with the
constant-ready contract; the complete graphics regression also passes.

The exact full Beast Vivado 2024.2 `Performance_Explore` route is
`full-route-43-flood-rready`. All 68,382 routable nets connect without error,
but setup is WNS/TNS `-0.319/-41.884 ns` across 541 endpoints; hold is `+0.014
ns` and pulse-width slack is `+1.116 ns`. Utilization is 34,024 LUTs, 40,065
registers, 12,586/13,300 slices (94.63%), 896 control sets, 127
BRAM36-equivalent tiles, and 83 DSP48E1s. The generated bitstream is rejected
and was not flashed.

The Route 40 flood-state-to-HP2-response-slice path is gone. WNS is 14 ps worse
than Route 40, but the one-line AXI simplification removes four levels of
engine/owner/slice control coupling and is retained. The new route is led by
geometry state into emit classification (`-0.319 ns`), sprite request count
into clear bookkeeping (`-0.274/-0.258 ns`), glyph output selection (`-0.257
ns`), and framebuffer fill enable (`-0.253 ns`). The next experiment targets
only the measured geometry classification boundary.

| Route 43 rejected artifact | SHA-256 |
|---|---|
| Source flood renderer | `4ed738447a039eab9246627180c245a864ae5d808a9324b5250f135993ea3242` |
| Routed DCP | `3a345f78a843e5d04a599241224b7394d6047582e7f29e58c2a846a3ad1ac1bc` |
| Timing summary | `f453660854db87a60377964f247eeab4981c4f7745575651af71d6b57a7e4777` |
| Utilization | `d248f36db1ed6a3a6fa09051daefc1c9070eb815cc699031dbb64b695453b957` |
| Route status | `17c9a1b86b5797f3247392eb6bb2566ce48bc904a6f6b0e415043517f9c83895` |
| Rejected bitstream | `7b92f16a4babc65d79d7627fe824f77afeb523082ec66d329a45f6ed1921f081` |
| XSA | `341aba441d68f9debbb1b7d066ba6e1ad632ed5f9836b371fead442a825691c5` |

### Route 44 rejected geometry classification pipeline (2026-08-12)

Route 44 makes the geometry emit-classification bit the one-cycle delayed
valid signal it already represented, removing the geometry FSM and queue
priority mux from that register's input. A focused assertion fails against the
former implementation and passes after the change. The complete graphics
regression passes, including 131,072 sprite scaling pairs, every 1..128 source
width and height, the integrated pipeline, geometry, flood, AFNT glyphs, and
all 45 render-command submissions.

The exact full Beast Vivado 2024.2 `Performance_Explore` route is
`full-route-44-geometry-classify`. All 68,253 routable nets connect without
error. Setup improves to WNS/TNS `-0.211/-12.291 ns` across 233 endpoints;
hold is `+0.008 ns` and pulse-width slack is `+0.538 ns`. Utilization is 33,956
LUTs, 40,010 registers, 12,511/13,300 slices (94.07%), 886 control sets, 127
BRAM36-equivalent tiles, and 83 DSP48E1s. The generated bitstream is rejected
and was not flashed.

The Route 43 geometry classification cone is gone. The new measured leader is
the glyph green-channel rounded divide-by-255 carry chain (`-0.211 ns`, eight
logic levels), followed by renderer fault-detail enables (`-0.157 ns`), a
blitter source-address qualification path (`-0.154 ns`), and sprite descriptor
validation (`-0.142 ns`). The next experiment targets only the measured glyph
blend arithmetic.

| Route 44 rejected artifact | SHA-256 |
|---|---|
| Source geometry renderer | `2faa954e627d66afb73153e7cf639bee69dab82c9e6363ddcde9e5d738c5e31a` |
| Focused geometry test | `e831dc70b528ce5c9505a3039cb3b2dd99b11a2d61d6fe53bba62a022c29421a` |
| Routed DCP | `3c41bac596358bf27c1eadb9394675bc4d335b22dd6a46c8bc1a0332ef639c70` |
| Timing summary | `03d37b30696a989df98422562247d2af75a0f308de5b2e3fc09ff2db90864020` |
| Utilization | `37eceeea64779f22b3e5bdf8108e2b972fe678f9cd7ddde77f16242347768807` |
| Route status | `9c212f23e0b7bff1f21af67afec835afbad6d74ae3fb1bec757b5e79979fcacf` |
| Rejected bitstream | `7d03b62996186d6af8fb5e10596294d461c74f4673f2ddf4d9a825ae32c04757` |
| XSA | `ad0082ee2b3d4157aa576579d7fcb401e4912a29ee1fda4cb307951c611a539f` |

### Route 45 rejected glyph rounding register bank (2026-08-12)

Route 45 splits the exact rounded divide-by-255 across the existing glyph
normalization and divide states by adding three 18-bit rounding registers.
The focused glyph test and complete graphics regression pass, but the added
register bank worsens packing and moves the critical path rather than closing
the design.

The exact full Beast Vivado 2024.2 `Performance_Explore` route is
`full-route-45-glyph-round-pipeline`. All 68,346 routable nets connect without
error. Setup regresses to WNS/TNS `-0.550/-95.201 ns` across 1,131 endpoints;
hold is `+0.011 ns` and pulse-width slack is `+0.538 ns`. Utilization is 33,981
LUTs, 40,096 registers, 12,493/13,300 slices (93.93%), 889 control sets, 127
BRAM36-equivalent tiles, and 83 DSP48E1s. The generated bitstream is rejected
and was not flashed.

The Route 44 glyph carry-chain leader disappears, but placement now leads with
the Copper prepared-line comparison into the scheduler state enable (`-0.550
ns`, five logic levels, 3.663 ns routing). Because a 54-register bank regresses
the retained Route 44 WNS by 339 ps, the change is removed. The next experiment
reuses the glyph's existing divide pipeline for both 15- and 255-level coverage
instead of adding storage.

| Route 45 rejected artifact | SHA-256 |
|---|---|
| Source glyph renderer | `60db7f379294d074a82dc868eea31ec92679962a9eae448517d308dbb9bbc05b` |
| Focused glyph test | `cd12ed49fa683f205732639e5c086013ae9372f92fbeff96f4ece396a7f64a4f` |
| Routed DCP | `2a83a6c823f18523d3f76deb2561e4e333ab7be82f4452a1538fc7bd370ba1c7` |
| Timing summary | `314df9f6aa092819dfbb2673f421f29592613c558e68d1855ea02c86b58b6472` |
| Utilization | `f14057b7a44fdce411c750c74e491f1002a516f1d8994f0360dc3f055423fab5` |
| Route status | `e1e1bd9ea0b8222dc0899995c0acc460f39ea430ef4d025d05690e1f07ce78f8` |
| Rejected bitstream | `68048b6d5ad154cd4e877df916535527d4931a941cc20b9d2cb0bd6f9af54fbe` |
| XSA | `3de6b826038c4790905dd1739254bfc4083dcdda1af884cd008b86473eeb3d27` |

### Route 46 rejected shared glyph divide pipeline (2026-08-12)

Route 46 removes Route 45's added registers and reuses the existing glyph
divide pipeline for both coverage scales: `(n + 8) * 273 >> 12` for 15-level
coverage and `(n + 128) * 257 >> 16` for 255-level coverage. Exhaustive host
checking proves the latter equals the existing rounded divide for every legal
0..65025 numerator. The focused glyph test and complete graphics regression
pass.

The exact full Beast Vivado 2024.2 `Performance_Explore` route is
`full-route-46-glyph-shared-divide`. All 68,244 routable nets connect without
error. Setup is WNS/TNS `-0.433/-97.492 ns` across 1,076 endpoints; hold is
`+0.028 ns` and pulse-width slack is `+0.538 ns`. Utilization is 33,969 LUTs,
40,064 registers, 12,556/13,300 slices (94.41%), 883 control sets, 127
BRAM36-equivalent tiles, and 83 DSP48E1s. The generated bitstream is rejected
and was not flashed.

The exact rounded-divide carry chain is gone, but the new route is led by glyph
state/format decoding into a blend DSP clock enable (`-0.433 ns`, 76% routing),
then command range validation (`-0.403 ns`). This is 222 ps worse than Route 44.
The shared-pipeline change is removed and Route 44 is restored as the best
measured structural baseline. After 46 full-route experiments, no further
single-cone iteration is authorized without an architectural timing decision.

| Route 46 rejected artifact | SHA-256 |
|---|---|
| Source glyph renderer | `fc2a0138728ff873c462345743cc4a56a3ab638c94cc03644209e8d2cc60d07f` |
| Focused glyph test | `cd12ed49fa683f205732639e5c086013ae9372f92fbeff96f4ece396a7f64a4f` |
| Routed DCP | `71c878ded3331358f1fa606af45b7a9ef814b2fe024888939a693f5f4414b2b6` |
| Timing summary | `989d526f52eb507aeb64a9b0cea4d92f2925b97e58cc954940f1df30927a3d66` |
| Utilization | `2d3d7ca7752da8ec9ad1072d3d735ab05a863ac33fb0de15714202143abf2ab4` |
| Route status | `e306a23404285d34f2f4f13330cbceeccc598ba8b0aab8f2a789abe002b41159` |
| Rejected bitstream | `7df164780f0217329fe2a67a8ca104ffd94bf30f9708268693a031dda8f61fcb` |
| XSA | `b563abb05b40012ea235f4fd0873b6cf444cb8f2372c139ca88029ef7ef48fee` |

### Route 44 structural-pressure analysis and bounded-route policy (2026-08-12)

Vivado 2024.2 analysis of the restored Route 44 checkpoint on Beast found no
placement or initial-router congestion window above level 5. The failure is
instead packing pressure: 33,956 LUTs and 40,010 registers occupy 12,511 of
13,300 slices because the design contains 2,590 LUTRAMs, 666 SRLs, and 886
control sets. The sprite builder and scene store account for 1,754 LUTRAMs;
their control/address nets include fanouts from 332 through 741. Vivado also
reports 2,174 unused register locations inside already occupied slices.

Full production routes are now limited to five attempts per structural
optimization campaign. Functional regression and exact full-design synthesis
must pass first, and a candidate that does not materially reduce hierarchy or
control-set pressure is rejected before placement. Route 46 is the last route
of the previous local-cone campaign; no seed search or Route 47 is authorized.

| Route 44 analysis artifact | SHA-256 |
|---|---|
| Analysis script | `b9342ebcffaa4428cf03c26b51fdde7e9dc745a1b6dee217ac95a755ce2d1458` |
| Hierarchical utilization | `d646ce369a294991fdc8bdb0eab70601508042de07aeceebd474aa40e686650f` |
| Control sets | `2b33b1af4f764df17a0e780d5ba659beff50fb55aeb457d20a83d025a31cb016` |
| High-fanout nets | `69e6d692c980a706bb17dc40bf986a16342cddd50ae5756fcb35da8b94ba084c` |
| Congestion analysis | `1ed10a86caf8fd93f30fa2c51b425eaa816f4ca194ffb6e1f2c1bd2f39f70fa8` |
| QoR suggestions | `71f9402bb3efbaaf8ffd0a9335d415d5095e938bca49a45eb1425cdf611893bd` |
| QoR suggestions file | `25993f2faf50a1b70c8834c39f273a9ff7650d6bb2f9e2e292dd4c191ee198ad` |

### Structural synthesis checkpoint 1: shared tile validators (2026-08-12)

The host graphics-control and Copper structural commit paths each used two
identical tile validators for an off-hot-path atomic validation operation.
Each path now reuses one validator sequentially for tile 0 and tile 1. The
complete graphics regression passes, including explicit shared-validator
commit rejection and Copper structural-state coverage.

Exact full-design synthesis on Beast with Vivado 2024.2 is
`full-synth-validator-share-1`: 34,304 LUTs, including 2,592 distributed-RAM
LUTs and 773 SRLs, 39,840 registers, 851 control sets, 127
BRAM36-equivalent tiles, and 83 DSP48E1s. The host and Copper shared validator
instances synthesize to 230/194 and 234/194 LUT/register pairs respectively.
This is a retained synthesis checkpoint only; it spends no full-route attempt.

| Shared-validator synthesis artifact | SHA-256 |
|---|---|
| Utilization | `eafb82135a99cb1ce3a72a91fdc72c9f1520754a3b694de09f70a64e0d93b337` |
| Hierarchical utilization | `08cc6a1485fdfd50999da868308e703d428e5962c8aebd81d0f90194db172fa9` |
| Control sets | `61c9915c83c3447c4556710e450a3b7a084ebe5e060b2c6e8845189524d1e824` |
| Synthesized DCP | `1bad5185ad103f0a408f8e5235ce3b58b976e504179e0754e2112b7ed1feb91a` |

### Structural synthesis checkpoint 2: tile metadata BRAM (2026-08-12)

Each tile line builder stored 256 span records and 256 decoded descriptors in
distributed RAM even though map writes and pattern/compose reads occur in
separate phases. The two memories now share one synchronous metadata read
address and map to one RAMB18 each per builder. One capture cycle per span
raises the measured worst-case tile build from 1,179 to 1,347 cycles, still
well below the 4,444-cycle regression limit. The complete graphics regression
passes.

Exact Beast Vivado 2024.2 synthesis
`full-synth-tile-metadata-bram-1` proves four RAMB18s were inferred. Relative
to checkpoint 1, the design removes 672 LUTs: 576 distributed-RAM LUTs and 96
logic LUTs. It also removes 30 registers and 16 control sets. Each tile builder
falls from 1,525 to 1,189 LUTs and from 304 to 16 LUTRAMs. The full design is
33,632 LUTs, 39,810 registers, 835 control sets, 129 BRAM36-equivalent tiles,
and 83 DSP48E1s. This retained checkpoint spends no full-route attempt.

| Tile-metadata synthesis artifact | SHA-256 |
|---|---|
| Utilization | `313e40ae71a80db90375325b01df68da6f4cb734b3d4c327dcf64fee5250915e` |
| Hierarchical utilization | `38c8d6c4bb44d6244c29a017be1bbd6cbbf2176eb1647faaa2ce63b4c491096c` |
| Control sets | `54abf0949dc41eb4312f74831041f0b87bda6d510fcf449fafb2f74ee8589ec4` |
| Synthesized DCP | `7918bd4c9583cf15584b96547989bdd9112d966ce5511383a323d3085fb50bbe` |

### Structural route attempt 1: rejected unregistered BRAM output (2026-08-12)

The first exact production route of the retained validator-sharing and tile
metadata-BRAM source is `full-route-structural-1` on Beast with Vivado 2024.2
`Performance_Explore`. All 66,979 routable nets connect. Physical use falls to
32,874 LUTs, 39,503 registers, 12,401/13,300 slices (93.24%), 855 control sets,
129 BRAM36-equivalent tiles, and 83 DSP48E1s. This returns 110 physical slices
relative to Route 44 while retaining every feature.

Setup nevertheless regresses to WNS/TNS `-0.680/-141.162 ns` across 1,609
endpoints; hold is `+0.015 ns` and pulse-width slack is `+0.538 ns`. The exact
leader starts at the new tile-1 span RAMB18 output and passes through three
source-coordinate LUTs in one cycle. The 5.494 ns data path is 2.826 ns logic
and 2.668 ns routing. The candidate bitstream is rejected and was not flashed.
No second route is authorized until the BRAM read data is registered and the
change passes regression plus synthesis. Campaign route budget used: 1/5.

| Structural route 1 rejected artifact | SHA-256 |
|---|---|
| Routed DCP | `a1e3e59ed7f957dd193456f641fb0dd0d551a794d24d12f822d68f6c2f6df16f` |
| Timing summary | `225d7a616b5746825886e99a0e835a56e9e7029b0d8953da6b8ed2c90ed82fb6` |
| Utilization | `48b8fbfa662588d1441c7bb2587320fab93a076f6362f8930972387c01ec29e2` |
| Route status | `1dfb6286df71ee3404a66fa1bc550472a443fc227cddcda0a26124ca27431597` |
| Rejected bitstream | `ccb0f415500ec6fe10df000b27571f77417bdfb8fbdcc9b14bb9f5d45ca14269` |
| XSA | `fa09b4b9a4449cc3bb5c155a1f7389547c0081fb084ef9db4f82d7ab49f6e9c2` |

### Structural synthesis checkpoint 3: registered tile prefetch (2026-08-12)

The tile compose schedule now prefetches span, descriptor, and pattern data
through a register bank before source-coordinate and pixel-selection logic.
The complete graphics regression passes, including every INDEX4/INDEX8 span
boundary, clipping, descriptor containment, 4 KiB split, and variable sprite
dimension case. Worst-case tile build remains within budget at 1,348 of 4,444
clocks.

Exact Beast Vivado 2024.2 synthesis
`full-synth-tile-prefetch-register-1` uses 33,624 LUTs, 40,124 registers, 837
control sets, 129 BRAM36-equivalent tiles, and 83 DSP48E1s. Relative to the
metadata-BRAM checkpoint this removes eight LUTs while adding 314 registers
and two control sets. Each tile builder uses 1,185 LUTs. Netlist timing proves
that the tile-1 span RAMB18 now drives the prefetch FF bank directly with zero
LUT levels; the synthesized estimate is +1.485 ns setup slack. The Route 1
three-LUT BRAM-to-coordinate path therefore cannot recur unchanged. This
checkpoint spends no full-route attempt and authorizes structural route
attempt 2.

| Registered-prefetch synthesis artifact | SHA-256 |
|---|---|
| Utilization | `7c728904bd20f76fbdfc88c814500c88a2b4a9f966fe415d3d14c6428d44b077` |
| Hierarchical utilization | `06ae86c5d6ac00fbea41582281d3b19364b36c884e0f1ea4e435238f96cd0ab5` |
| Control sets | `2627d129111d4fc892e1a21e507d3e209b0df8e827d5dd01f05483e3468ca246` |
| Synthesized DCP | `5090bb9ade34ac81a5296756843269c347ca7fa3a1b250abb0d6b980b04a6293` |

### Structural route attempt 2: rejected after registered prefetch (2026-08-12)

The exact `Performance_Explore` production route
`full-route-structural-2` connects all 67,723 routable nets. The registered
prefetch removes the Route 1 BRAM-to-coordinate leader, but setup still fails
at `-0.373/-35.002 ns` across 452 endpoints; hold passes at `+0.010 ns` and
pulse width at `+0.538 ns`. Physical use is 32,883 LUTs, 40,066 registers,
12,432/13,300 slices (93.47%), 853 control sets, 129 BRAM36-equivalent tiles,
and 83 DSP48E1s.

The new leader is an HP2 response-error path through five LUTs into the glyph
`fault_detail` clock enable: 4.983 ns data delay, comprising 1.076 ns logic
and 3.907 ns routing. The rejected image was not flashed. No seed or immediate
reroute is authorized; the next candidate must first make another measured
packing/control reduction. Campaign route budget used: 2/5.

| Structural route 2 rejected artifact | SHA-256 |
|---|---|
| Routed DCP | `7b818abb5eb267448f368a7cbd68720c838aed997a305ca933b4e93bb4f34129` |
| Timing summary | `9232ecb1129e9d525d52cef8a809fdc4a8c08641eb4814134d70bb5e22163cb1` |
| Utilization | `d9f8a3c4d89ac2e7a01e4af16b4f477c7611a3ea1c77da9c5afc2ab3c58f7daf` |
| Route status | `4fcaf8839d3f79fc5a27c62ca5da909ba12d3dd11968f7a1ea0930304aca187c` |
| Rejected bitstream | `82b022f7329ff4b7434c93b341097b33917007b57e992a4a65b9aa524490c1ea` |
| XSA | `a6728c774333f5537d4f97feb554ae9cf51d3ce59be1e479c326143b96103576` |
| Failing-path classification | `c93ca090fb6f857fa96c19dd80c070a24c16e8f9a5837cfabe488a1d531cf743` |
| Detailed failing paths | `7e0f31633f5bbc9ea9b512503607e962fa01bfa9888cf5fcaa5799c0b02077c2` |

### Structural synthesis checkpoint 4: sprite admission record BRAM (2026-08-12)

The sprite builder now stores each admitted sprite as one 242-bit record. An
initial inferred-RAM checkpoint was rejected before routing: Vivado reported
the requested block style infeasible, built 81 RAM64M primitives, and reduced
the design by only 44 LUTs. Splitting the read and write processes according
to the UG901 simple-dual-port template produced the same result. No route was
spent on either weak checkpoint.

The retained implementation uses the native `xpm_memory_sdpram` contract with
one-cycle synchronous read latency. The complete graphics regression passes,
including all eight sprite modes and exhaustive widths/heights 1..128. Cycle
counts remain 341 functional, 3,860 worst-case, 3,933 64-way collision, and
469 4-KiB split.

Exact Beast Vivado 2024.2 synthesis `full-synth-sprite-record-xpm-1` maps the
64x242 store to one RAMB18 plus three RAMB36 primitives. The complete design
uses 33,262 LUTs, including 2,457 LUTRAMs, 40,106 registers, 836 control sets,
132.5 BRAM36-equivalent tiles, and 83 DSP48E1s. Relative to registered tile
prefetch this removes 362 LUTs and 135 LUTRAMs while retaining 7.5 free BRAM
tiles. Structural route attempt 3 is authorized; campaign route budget used
remains 2/5 until it completes.

| Sprite-record synthesis artifact | SHA-256 |
|---|---|
| Utilization | `d41c61a74120c5c8a66ab0f733deed205fb0cc32ff5d9d6e5ec0c3746ef3d3ee` |
| Hierarchical utilization | `e296c00bb12eb369b9c3a487e0d7079b8cbe71fc28131feedf7d7e2fe4a3ca02` |
| Control sets | `ac0539b32cad8ebecd12f3c1eb59f3dd157a48fba1079ea571bbf238fa8b9522` |
| Synthesized DCP | `4544fb28578c638e4ca37bd13b9bdc3c8032679f1d79ab05b1734124c2d94550` |

### Structural route attempt 3: rejected wide sprite record (2026-08-12)

The exact `Performance_Explore` production route
`full-route-structural-3` connects every routable net. Setup fails at
`-0.390/-88.937 ns` across 779 endpoints; hold passes at `+0.012 ns` and pulse
width at `+0.538 ns`. Physical use is 32,661 LUTs, including 2,348 LUTRAMs,
40,079 registers, 12,464/13,300 slices (93.71%), 851 control sets, 132.5
BRAM36-equivalent tiles, and 83 DSP48E1s.

The leader is an HP2 response-ID path through five LUTs into the flood
fault-detail clock enable: 5.012 ns data delay, comprising 1.212 ns logic and
3.800 ns routing. Endpoint classification finds 746/779 failures outside the
sprite subsystem; 439 terminate in the glyph engine. The wide sprite record
therefore reduces LUTRAM but worsens hard-BRAM placement pressure without
helping the dominant region. The image is rejected, was not flashed, and will
not be rerouted. Campaign route budget used: 3/5.

| Structural route 3 rejected artifact | SHA-256 |
|---|---|
| Routed DCP | `a331bd1ee8833e122f5507a3ad11be35eeb64fe1d7cc714050d99641f8fa4ca6` |
| Timing summary | `7b14116539bfc6ac3d36d1d57efd884271bc81395d547a05e9843957ff4c213c` |
| Utilization | `deaf81600b42295dd0102fb711850bce151b2749909fe294d1c1d40f526d10ba` |
| Route status | `0e5e378253819a74dcf9942d5c372f3eabd1f5308cbc6ad86cf026171eb959ce` |
| Rejected bitstream | `6de62f84924f34239f5bcdc1683fd26eb1f6cc0aa9da53a05894994d31990590` |
| XSA | `f5933589df4637d9491e9d5921aca95f651007b7692ccb282508446c7a9061a2` |
| Failing-path classification | `2bc6e144e050a33d0b29dbd6b6d180055806b8120c4749b56956493c601eb457` |
| Detailed failing paths | `b4371e6b26edf3996b9d7aedb4c9c673e997c64548ebefbb24e3ff0a68b26bc1` |

### Structural synthesis checkpoint 5: narrow sprite admission record (2026-08-12)

The sprite admission store now retains only sprite index, clipped screen X,
and span. The scene descriptor remains authoritative and is reread during
preparation, eliminating 214 duplicated bits per admitted sprite. A required
one-cycle metadata settle state was found by the focused sprite test before
the full regression; the final counts are 341 functional, 3,861 worst-case,
3,934 64-way collision, and 470 4-KiB split cycles. Every 1..128 source width
and height and all 131,072 scaling pairs pass.

Exact Beast Vivado 2024.2 synthesis
`full-synth-sprite-narrow-record-1` maps the 64x28 XPM to one RAMB18. The full
design uses 33,309 LUTs, including 2,457 LUTRAMs, 40,072 registers, 837 control
sets, 129.5 BRAM36-equivalent tiles, and 83 DSP48E1s. Relative to the wide
record checkpoint this returns three BRAM36-equivalent tiles for 47 LUTs while
retaining the full 135-LUTRAM reduction. No route is spent; campaign use
remains 3/5. A declaration-order warning found in this checkpoint is corrected
before the next combined synthesis.

| Narrow-record synthesis artifact | SHA-256 |
|---|---|
| Utilization | `3eff445a0873ffc3b635dd4182da50dd4ae6b53db3eeaa42892dcb945f732655` |
| Hierarchical utilization | `2cb6c59d40c332e50ecb007c89a00fdaaa33bc09853843d0bcb8abaf4787edb5` |
| Control sets | `6e6cbd80e3441609ef32e3b107d67f46ff6e32a9b7e43472e243871f6ca08f62` |
| Synthesized DCP | `811ba8bea0eb3edf9e49bc7241c1eca49fe99bd258c248aa1651f6307a62e4fa` |

### Structural synthesis checkpoint 6: registered render ingress (2026-08-12)

Route attempts 2 and 3 both led with raw HP2 response metadata crossing the
render-command mux and engine error decode into fault-detail enables. The
existing command processor now captures engine responses once in a shared
73-bit elastic register and feeds glyph, flood, or blitter from that local
boundary. Simultaneous consume/capture preserves back-to-back throughput. Only
valid bits reset; payload reset wiring is deleted because invalid payload is
never observed. The complete graphics regression passes, including all 45
render submissions, injected read errors, deadlines, aborts, and reset cases.

Exact Beast Vivado 2024.2 synthesis
`full-synth-narrow-record-engine-ingress-1` uses 33,348 LUTs, including 2,457
LUTRAMs, 40,157 registers, 838 control sets, 129.5 BRAM36-equivalent tiles,
and 83 DSP48E1s. Relative to checkpoint 5 the boundary costs 39 LUTs, 85
registers, and one control set. It preserves the three returned BRAM tiles and
directly breaks the repeated route leader, so one exact route 4 is authorized.
Campaign use remains 3/5 until that route completes.

| Registered-ingress synthesis artifact | SHA-256 |
|---|---|
| Utilization | `bcc3f79f26bebc9a9dee95706e7cc45bbf169b4f853c8162891e3291057bc3ca` |
| Hierarchical utilization | `744c9ffa304c6c3245214bfccd69b62ff4467966483f5d1ba88c36b4b9e4eade` |
| Control sets | `be89f6cf8c68a09e042937a8b833971a5f5d9c1e439d84b4b61cf05054bdde59` |
| Synthesized DCP | `3cf4a63690baf1ba8e4c7ec89b5d507354a0b5e9c041fd8ad687c92dad86a89e` |

### Structural route attempt 4: rejected admission read cascade (2026-08-12)

The exact `Performance_Explore` production route
`full-route-structural-4` connects every routable net. Setup fails at
`-0.639/-476.709 ns` across 3,108 endpoints; hold passes at `+0.012 ns` and
pulse width at `+0.538 ns`. Physical use is 32,672 LUTs, including 2,347
LUTRAMs, 40,002 registers, 12,329/13,300 slices (92.70%), 875 control sets,
129.5 BRAM36-equivalent tiles, and 83 DSP48E1s.

The registered render ingress removes the HP2 response leader from attempts 2
and 3. The new leader is a two-level, 5.402 ns data path from the narrow
admission XPM RAMB18 clock pin through the scene store's distributed active
descriptor lookup into `descriptor_collision_compatible_reg[3]`. The same
missing RAM-output address boundary accounts for the worst sprite-scene paths;
endpoint classification contains 1,057 sprite paths and 2,038 non-sprite paths,
showing that the bad constraint on placement also damages the dense design
globally. The image is rejected, was not flashed, and will not be rerouted.
Campaign route budget used: 4/5.

| Structural route 4 rejected artifact | SHA-256 |
|---|---|
| Routed DCP | `645aecd2bcfe6837e80f11ba2bcc243e17258e8b238f8b6977d8fa97e3f88ea8` |
| Timing summary | `a05de47e0cd601cf7e5f9db2f36b3a00cf41642ab651f6ffaf780bc5798607a3` |
| Utilization | `4cf49e5ec870efdc1b54ee3c8efd80a646aa1311bff9f6e667c8548442b50636` |
| Route status | `36624526c916196d08bb092615821c0d0b088bfc313a478f22dfd6e07d2714dd` |
| Rejected bitstream | `efb5e91dc8513873f1fbfb664c3147648d71ad405c7009ae008d414f7235ff94` |
| XSA | `99435e5d78b1f0bac2003da8936f30751ffb017a40258bb346cb143648992111` |
| Failing-path classification | `29993962b171884a055b7899b4f1a0887b1f9edb56fddde48ca0ff1e9257f5a3` |
| Detailed failing paths | `9ec4c0e8d3593897a573131961049c16bef701cd68dfffc563dd38456a1a18cf` |

The correction reuses the existing preparation FSM to capture the admitted
sprite index before issuing the active-descriptor read. A following existing
metadata pipeline is extended by one settle state, so the former
RAMB18-to-LUTRAM cascade is split at a real register boundary without adding a
new data store or removing a feature. The full Beast graphics regression
passes. Worst-case sprite build rises from 3,861 to 3,877 cycles, 64-way
collision from 3,934 to 3,935, and the 4-KiB split case from 470 to 471, all
within the 4,300-cycle gate.

Exact Beast Vivado 2024.2 synthesis
`full-synth-shared-scene-transfer-1` uses 33,349 LUTs, including 2,457
LUTRAMs, 40,157 registers, 838 control sets, 129.5 BRAM36-equivalent tiles,
and 83 DSP48E1s. The admission RAM now reaches only the six registered sprite
index bits: all six paths have zero LUT levels and +1.485 ns synthesized
slack, and there is no admission-RAM-to-scene-register path. Clone and
activation also share their identical descriptor and palette capture banks;
Vivado had already merged those registers, so the source cleanup changes no
net resources. This checkpoint breaks the measured Route 4 cone without
increasing the retained resource profile and authorizes the fifth and final
route. Campaign route budget remains 4/5 until it completes.

| Admission-boundary synthesis artifact | SHA-256 |
|---|---|
| Synthesized DCP | `befce14f50692dfc1c44a5e684f6f869f09b962ddc31395f066032a197c1a49c` |
| Utilization | `caf6be9aa4731dafd2d95e9ffb918c73aa8bb6a20178308c460cd3cbbd9aba08` |
| Hierarchical utilization | `ef67993c951f786056d4deb17bd4f382c9b180c0372aca83a7f77a4552885bc1` |
| Control sets | `a6d02c6854352ea157d5bce1d8c8e2d0abb714d8b8b25b3bf3066ae89a6bf63b` |
| Admission-boundary timing gate | `9bf060d6f0de0bc735530b338593a535ec8961c474d388e35090b5355872f542` |

### Structural route attempt 5: rejected broad render control (2026-08-12)

The fifth and final exact `Performance_Explore` route
`full-route-structural-5` connects every routable net. Setup fails at
`-0.320/-53.449 ns` across 807 endpoints; hold passes at `+0.016 ns` and pulse
width at `+0.538 ns`. Physical use is 32,657 LUTs, including 2,348 LUTRAMs,
40,049 registers, 864 control sets, 129.5 BRAM36-equivalent tiles, and 83
DSP48E1s. The image is rejected and was not flashed.

The Route 4 admission-RAM leader is gone. Failures are now broad but strongly
concentrated in render-command control: 476 endpoints terminate in that
subsystem, comprising 173 glyph, 125 command-core, 77 flood, 54 blitter, and
47 geometry endpoints. The new leader is a three-LUT path from replicated
`command_is_geometry_q` through blitter abort selection to the shared pixel
writer's flush-pending register. It is 5.031 ns, of which 4.141 ns is routing.
The next independent groups are a PS HP3 address path at -0.242 ns, glyph DSP
clock-enable paths at -0.228 ns, and tile-0 compose enables at -0.226 ns.

The five-route campaign is exhausted. No Route 6 or seed rerun is authorized.
The next campaign must first remove the shared render control/abort mux from
the measured hot path and prove a materially better synthesized structure.

| Structural route 5 rejected artifact | SHA-256 |
|---|---|
| Routed DCP | `fe30af2b85f0c389235e64ab6348795f769d4022d87c652f944c7e021bf74bc0` |
| Timing summary | `535b73180cdb21e431860e102fb5ad3aefdeaf542fd03c448be692d0591af322` |
| Utilization | `4b1ed7cb4616d2269307b1f4bd47cdada36215d877efd6b5ac6a237fd8d7ce99` |
| Route status | `3571937cbc8174f4397e4580b28c838483c274954eaf87b5bd4301e629a77d1e` |
| Rejected bitstream | `637abad6579afd42bf0579632737a4e09ddbe80ee498afc46c22087f4b15292e` |
| XSA | `51936008b69e8f609c6898cc204f691b116e367f19ba09b97a14536ac2e72ce5` |
| Failing endpoints | `56439753c0ed6fb312ead324d96d6a657978b8f4ace55d9babf7c0350c1b2523` |

### Writer-control synthesis checkpoint 1 (2026-08-12)

The new campaign removes the unnecessary command-type priority mux from the
shared pixel writer's start, abort, and flush inputs. Glyph, flood, geometry,
and blitter already emit mutually exclusive registered pulses, so their pulses
are combined directly. The complete graphics regression passes, including all
45 render submissions and injected abort, error, deadline, and reset cases.

Exact Beast Vivado 2024.2 synthesis `full-synth-writer-control-or-1` uses
33,391 LUTs, 40,140 registers, 838 control sets, 129.5 BRAM36-equivalent tiles,
and 83 DSP48E1s. It removes 17 registers but adds 42 LUTs relative to the prior
checkpoint. More importantly, the exact Route 5 leader is structurally absent:
there are zero command-type-to-writer-flush paths, and the new worst path into
the flush register has +1.572 ns synthesized slack. One exact production route
is authorized. New campaign route budget used: 0/5.

| Writer-control synthesis artifact | SHA-256 |
|---|---|
| Synthesized DCP | `fc47021aa4f5e7d67a38c7e76c51e8cdd5be65ac0495a64698c423e1a98ba02c` |
| Utilization | `a312c3c9ec62897b09c4e95d77515955e9c90c13d71aeb8d7947ecead93c2765` |
| Hierarchical utilization | `7b50b0d6e67c3d94319a85140677a87c1938f2a83e633bf1d775e17a1a8552ae` |
| Control sets | `91191be4d2a230cc3c1985419b427ef47552f23da305e45a496ce933a73c73ba` |
| Writer-control timing gate | `10afd6c6d4ca5728b6dad6491db7c474245366d5d648f6bf819d1424debf32a0` |

### Writer-control route attempt 1: rejected (2026-08-12)

The exact candidate reached post-physical-optimization estimated timing of
`-0.240/-138.531 ns`, then completed routing from that saved checkpoint. Every
net routes, hold passes at `+0.011 ns`, but setup fails at
`-0.325/-32.435 ns` across 409 endpoints. Physical use is 32,714 LUTs, including
2,348 LUTRAMs, 40,021 registers, 12,355/13,300 slices, 881 control sets, 129.5
BRAM36-equivalent tiles, and 83 DSP48E1s.

The endpoint count improves over Route 5's 807, but occupied slices increase by
37 and the leaders move to sprite scheduler-start/read-enable routing
(`-0.325 ns`) and blitter cache-address/pixel-value control (`-0.309 ns`). This
is not convergence margin. The direct-OR change is removed after attempt 1/5;
no seed or second route is authorized and no bitstream was written or flashed.

| Writer-control route 1 rejected artifact | SHA-256 |
|---|---|
| Placed DCP | `3fac26133ef1189d401180e183858b53960021a135f4e97c6e7a7136e51b55f3` |
| Physical-optimization DCP | `27d49a0b3c2e6686557a83f8b1f1e98b8273ef09bc18ad439a238d3198b215f2` |
| Routed DCP | `145993816d74ed3abe41d4a4342f9682575fa0f0481ab02eef9acef5dbc84b7f` |
| Timing summary | `90e3b24850971dcd36803a663ca21260ec0eeea465beee789311467afcb06f74` |
| Utilization | `b942fd8cf11ee9d013214600ab32365fe0950bf804798ed564254ab5c5f0ac37` |
| Route status | `60b760fdc0067b2dcdf29821eb7a5ed38c8256158db1cf17a3f47a75dc1d1c0c` |

### Command-classifier replication removal: rejected at placement (2026-08-12)

Route 5's exact QoR report identifies its leader's source as
`command_is_geometry_q_reg_rep__3`. The RTL forced all five command classifiers
to `max_fanout=16`, even though AMD's 7-series methodology recommends leaving
control-net replication to placement and physical optimization because forced
replicas create equivalent control sets and congestion. Historical checkpoints
also showed `max_fanout=8` and `32` classifier experiments regressing integrated
timing; the directives were stale local-cone tuning.

The five attributes are removed with no replacement logic. The complete
graphics regression passes, including exhaustive sprite dimensions and all 45
render submissions. Exact Beast Vivado 2024.2 synthesis
`full-synth-no-command-maxfanout-1` uses 33,289 LUTs, 40,106 registers, 837
control sets, 129.5 BRAM36-equivalent tiles, and 83 DSP48E1s. Relative to the
Route 5 source synthesis this removes 60 LUTs, 51 registers, and one control
set.

The exact placement gate rejects the candidate before routing. Post-placement
timing is `-0.580/-299.333 ns` across 2,182 setup endpoints, materially worse
than the retained source's approximately `-0.337 ns` placement. The placed
design uses 32,393 LUTs, 39,800 registers, 12,252/13,300 slices, 842 control
sets, 129.5 BRAM36-equivalent tiles, and 83 DSP48E1s. The five attributes are
restored. No route or bitstream was produced and route budget use remains 0/5.

| Classifier-replication synthesis artifact | SHA-256 |
|---|---|
| Synthesized DCP | `89d77a5bebff2e9c08967e7deb0fddf27bf02029f3cd432f4833bd0c47dd2759` |
| Utilization | `917504fead2280a878c421676f3cc01d27f2afcbaa5cad82bd8d227a4306e6df` |
| Hierarchical utilization | `a4107e77f57441555c361101aa97c1cdb52699fe4637c22bb5ec0f6fd7060825` |
| Control sets | `d2a0a70331567fbbf921f4186aa7e4c08f9b5dee3866e1a6eb09c2bf3d245c2b` |
| Placed DCP | `ae188203e26f760eec7d62d48fe66f4c36e4441c9a2c07f94ea8dbfaa1ff1c3a` |
| Placement timing | `ada1a0ad19054b15e10599453687d69254b3cc499624bcfa7bc7e7e1331152fc` |
| Placement utilization | `762b3efd6290475e08433629bf1fd493a4ff26e27480b5c467068f02d809166b` |
| Placement control sets | `e8985f5832d29c818b9a201a35ee1834bb69fa70b3050be4e4b637d3e4efd07d` |

### Global control-set threshold: rejected at placement (2026-08-12)

AMD's documented `synth_design -control_set_opt_threshold 16` option reduces
the synthesized design from 838 to 537 control sets, but costs 1,106 LUTs.
Exact placement uses 33,721 LUTs and 12,379/13,300 slices and regresses to
`-0.492/-400.471 ns` across 2,726 endpoints. The option is removed before
routing; route budget use is 0/5.

| Control-threshold rejected artifact | SHA-256 |
|---|---|
| Synthesized DCP | `20e8836bb179ed2d030d31032197201ac92f784908f7387af1d5be29a792f3f1` |
| Synthesis utilization | `7183454235c6fab7cb5e3753ef9cacb2b1b690e7affab01c98a61f01688447c8` |
| Synthesis control sets | `625d46b916ba0c6d4f63778d216dfd94cde80bc0abe2e650b53bb1dd0a5633a6` |
| Placed DCP | `7fa557c977f149c0ff79d20a228ad5d11a96b34fff600c078cede37e6419c6ba` |
| Placement timing | `7bd319ea18c347f09de9c072e3b5ce80d8b18fdf16f37bbbc82e4ec46394886c` |
| Placement utilization | `e818b2cf5ee431a74f95fd80ca6b61b02785301d1ffbf2c34f1352523ce14bc1` |
| Placement control sets | `2a27cdcb233106c652982648c96b8170740aa6870c9883ae05284751899a821b` |

### Compositor DSP mapping: rejected before routing (2026-08-12)

The retained routed hierarchy report identifies the seven-layer compositor as
4,217 LUTs with zero DSPs while 137 DSP48E1s are unused. Moving all 8x8 blend
multiplies into DSPs and removing resets from validity-masked datapaths passes
the complete graphics regression. OOC use falls from 4,253 LUTs, 2,870
registers and 1,571 slices to 874 LUTs, 1,052 registers and 424 slices, with
setup improving from `+3.791` to `+5.327 ns`.

Full synthesis falls from 33,349 to 29,969 LUTs and from 40,157 to 38,385
registers, using 140/220 DSPs. Exact placement uses 29,027 LUTs and
11,293/13,300 slices, but regresses to `-0.718/-1259.868 ns`. An exact Explore
physical-optimization gate improves TNS to `-376.185 ns` but leaves WNS at
`-0.716 ns`. The all-DSP mapping is rejected without routing; route budget use
remains 0/5. The next checkpoint retains only the datapath-reset cleanup.

| All-DSP rejected artifact | SHA-256 |
|---|---|
| Full synthesized DCP | `2784271546ba467c5457e129b5c9e66540b1a7b948c775cbb65985d697db64ef` |
| Full synthesis utilization | `65b45d8dbea1de0982fb24050dbf02e4f1bdbc874149bd87d614af83e430cb29` |
| Placed DCP | `16d492a88e4b0057c0b40dc0fcade294df096cded0876621e124c04ffe07ea10` |
| Placement timing | `aba5b5d864bf32e7fafe8a12997f4b2a4dfffc04ab13b0f7a66d50fcf375cf55` |
| Physical-optimization DCP | `cccbeb37471e3868cd7e162ec9d60a0cd9386f8f9a57eebb8c56a19a66f21bb5` |
| Physical-optimization timing | `f0604ad014a1e8f7737beeb621bee7f4c90932ef19390e776d6df7301ee9f243` |
| Retained hierarchy analysis | `56d06e0f1e157413848d68c860b4490f7e748ec67b242b5a4eeeb6ec54fa21b6` |
| Retained high-fanout analysis | `462600b0b996572dc566e340afb20123374b7b2c5cba912b0d7966c017ff3372` |

The first reset-only compositor checkpoint was contaminated by three forced-DSP
attributes left on each premultiplied opaque blend instance. OOC used 3,603
LUTs, 2,323 registers, 1,297 slices and 12 DSPs at `+4.337 ns`; the full design
used 32,715 LUTs, 39,610 registers and 95 DSPs, then placed at
`-0.503/-356.347 ns` across 2,615 endpoints in 31,822 LUTs and 39,325
registers. It was rejected without physical optimization or routing.

Removing the stray attributes produced the intended zero-DSP OOC candidate:
4,378 LUTs, 2,777 registers, 1,550 slices and `+4.101 ns`. Full synthesis used
33,493 LUTs, 40,064 registers, 838 control sets, 129.5 BRAM tiles and the
retained 83 DSPs. Exact placement recovered to `-0.311/-203.484 ns` across
1,721 endpoints, but occupied 12,395/13,300 slices. The 0.026 ns WNS change
from the retained approximately `-0.337 ns` placement is not material and the
packing regression is contrary to the campaign objective. The reset removal
is rejected without routing; the unintended DSP attributes remain removed.
Route budget use remains 0/5.

| Reset-only zero-DSP rejected artifact | SHA-256 |
|---|---|
| OOC routed DCP | `035dba01f570500a0d1adf1fed07629e72cacace5c5d285648a870f4d5f01410` |
| OOC utilization | `0ccb2c4164a4b2fbbe084bbdc527afa1f0940ff3e81fd70f1ec6ea84b3097cf2` |
| OOC timing | `b45fbe0e79dec88c4cb0d65368f0ce450799a45078e34f3e2c1199ea2cab20e0` |
| Full synthesized DCP | `256eb3e6ada9c96be5359790632d6bfab0627ef18f3147f39c827ec93db33fcf` |
| Full synthesis utilization | `5ef6087ee85991c6da62cc879eb588c0dfe3df69653d48a8cea33f73b7e4ff34` |
| Placed DCP | `a82ecd048078e698754c224038fdcc4ecd00a389d034aebed18377e5b94b6107` |
| Placement timing | `9aaead6a43fab372903ac2820f837a436938100fb6541761765af6b873bdcda2` |
| Placement utilization | `7277aedbb51a5a66103b53e86bf2eda3f12f9404d1474f571d379ffad65367bc` |
| Placement control sets | `849f6ce297d59a9cc505d54e8187922f730b3706ff1d7cbebe7f0fccb14c9728` |

### Collision published-bank LUTRAM gate (2026-08-13)

Each of the eight collision banks stored only 8x64 published bits in a whole
RAMB36. Changing only that published bank to distributed RAM passes the full
graphics regression, including 64-way collision and all variable dimensions.
The exact sprite-line OOC route recovers eight BRAM tiles (32.5 to 24.5), keeps
setup/hold positive at `+0.078/+0.028 ns`, and costs 350 LUTs and 161 slices
(4,741/2,281 to 5,091/2,442). This is not production-route evidence; it earns
one full synthesis/placement congestion gate. Campaign route use remains 0/5.

The full gate rejects the trade. Synthesis is 33,695 LUTs, 40,658 registers,
839 control sets, 121.5 BRAM tiles, and 83 DSPs. Exact placement regresses to
`-0.700/-394.231 ns` across 2,276 setup endpoints, with hold also failing at
`-0.195/-11.535 ns`; it occupies 12,425/13,300 slices and 857 control sets.
The LUTRAM implementation creates 1,200--1,400-fanout collision commit nets,
so the published banks are restored to block RAM. No physical optimization or
production route is authorized; route use remains 0/5.

| Collision LUTRAM OOC artifact | SHA-256 |
|---|---|
| Routed DCP | `dcab9d144434a11b06e7bd05ad79054eb56e7138b14affccc5df54a5973ea156` |
| Utilization | `85947612a84211991b6ff2fffe672e34748af29ceb65ce293e33a3f0769ef15f` |
| Timing | `9436144e4d2f1460ca1f8b0cedd9f5f9128a136bd735fe0098568fcb5e2c7fc8` |
| Route status | `6b61b620efae3ef3762a195028d0582d5756b12904d190bb66dfe7c9cb45d14c` |
| Methodology | `cab1265a1b355fab04786b91a4acd868a5b0035e2f89de1d1751c8dac73c9d75` |
| Full synthesized DCP | `2174e4eed3e6ff516f70b74b146c8a797eac507e89184b05e3ce8648ce5f38d4` |
| Full synthesis utilization | `7a040f0982e354677b96d6fc387d6a79a2a80822bd814e5aef50dc61b68b79db` |
| Placed DCP | `7ba15432702798d156b4b6c82f86c5e16424a2ed9e575223175c5a3d36f3887c` |
| Placement timing | `41bc04a66ddb46b4e5d4d991e2fb1dfa5411fea866dbdc1c8f90d6a8ffbd4954` |
| Placement utilization | `9cc20720959fd8c666c4f3564cb486121f7fe70d5f9e2e05a7a6a83d72cd2cc8` |
| Placement control sets | `0d5b24dcef5e719de4e3e044029538ba1beafc3ac6176f5bdc533ebb7be6b32d` |

### Shared collision publication BRAM gate (2026-08-13)

The retained eight current-frame banks still accept eight symmetric updates in
parallel. Their completed-frame publication is now serialized across the first
64 cycles of the existing 320-cycle line-clear window into one 64x64
simple-dual-port BRAM. The feature set, visible read latency, frame atomicity,
and measured build cycles are unchanged. The complete graphics regression
passes, including the 64-way collision case at the original 3,935 cycles.

Exact sprite-line OOC routing recovered seven BRAM tiles (32.5 to 25.5), reduced
registers by 12, and passes setup/hold at `+0.100/+0.027 ns` versus the retained
`+0.085/+0.027 ns`. The cost is 188 logic LUTs and 82 slices (4,741/2,281 to
4,929/2,363), with LUTRAM unchanged at 931.

The full gate rejects the candidate. Synthesis uses 33,509 LUTs, 40,149
registers, 852 control sets, 122.5 BRAM tiles, and 83 DSPs. Exact placement
uses 32,577 LUTs, 39,856 registers, 12,420/13,300 slices, 859 control sets,
122.5 BRAM tiles, and 83 DSPs, but fails setup at `-1.285/-844.057 ns` across
3,296 endpoints and hold at `-0.237/-4.229 ns` across 97 endpoints. The worst
setup paths are zero-logic blitter blend-register-to-`divide_255_round16` DSP
connections whose delay is 77--79% routing. Removing the seven published-bank
BRAM anchors destabilized DSP locality elsewhere in the renderer; it did not
create a collision fanout cone. The shared publication is removed without
physical optimization or routing. Production route use remains 0/5.

| Shared collision BRAM OOC artifact | SHA-256 |
|---|---|
| Routed DCP | `9e99584296b9c0efc66bfd9bc90a9f19c9946a5ee74bf597994964e120fb078a` |
| Utilization | `53edd5ed28f89e547a6cd2cc000a3bc4c69db2f39804066e2840687099e52c42` |
| Timing | `e68c7878c2bac92dfc6bd1c5723d7ba0f970bfa88b8805abd482ce54326f3c63` |
| Route status | `b8ee574823e1cf34ee0d0251e76b73cf7df493635a33e90dc195c571c052c0eb` |
| Methodology | `b934ca96f43e371fbc70f4284224ffd3d3359f874bf6498b78259e3a141f4c00` |
| Full synthesized DCP | `1bd3556216ae4dc2c5323376aff5787b3b2e97d88699da20aed7fe458b1e9d31` |
| Full synthesis utilization | `f2bbd1ee8975af6ab46c86c18cab3864c72551b969cdfc687bdfceaed636c228` |
| Full synthesis hierarchy | `04f3546c558dd069348d16f6dbfe41f5b9dda29d0347184ca0c96256f5786fbc` |
| Full synthesis control sets | `c63d1827e025b866a7330ae3d3cd710f9e2d40d0944df29c71c5362acb18780c` |
| Placed DCP | `1804ea392db16aa8e0bef891569d9a32a433aea1013b318837e8b62efbfc12bb` |
| Placement timing | `4d03ee54cd329c97b87d42452d3c6222f1c3596d203ddcaada09f3e792e9dfac` |
| Placement utilization | `78334f5024777f48f6efd8b3c0ae6b6ae65afb9fefbc153d22ae7071533b85c6` |
| Placement control sets | `ff05b9d1b953427571653b6777282b60f3e08b21ca0cdb642aec3603ce7dc7ed` |

### Blitter blend-divider structural gate (2026-08-13)

The shared-collision placement exposed the blitter's exact source-over blend
path: Vivado implemented the serialized 8x8 multiply plus round-to-255
reduction as three DSP48E1s, and the leading failures ended at the duplicated
divider multiply. Removing reset and preservation attributes from the two
operand registers is rejected. Synthesis absorbed those registers into the
DSPs, but placement pushed nine registers back into fabric and the exact
blitter OOC route regressed from `+0.010 ns` to `-0.129 ns` across four
endpoints. No full-design placement or production route was run.

The replacement retains the one 8x8 multiplier DSP and expresses the
round-to-255 reduction as two explicit 17-bit carry additions. The source
pixel address is initialized before every use, so its otherwise unused reset
is also removed to recover packing on the newly exposed FSM/address cone. The
behavioral oracle remains 5,822 cycles. Exact OOC routing improves to
`+0.071/+0.122 ns`, while use falls from 2,668 LUTs, 3,138 registers and 13
DSPs to 2,644 LUTs, 3,121 registers and 11 DSPs. The complete graphics
regression passes.

The first full synthesis was rejected as contaminated before placement: it
showed that the previous collision-bank restoration had changed only the
`ram_style` text while leaving the published read asynchronous, so Vivado
ignored the block-RAM request and rebuilt the rejected LUTRAM candidate. The
published read is now synchronous inside each bank, while the parent bank mux
preserves the visible one-cycle read contract. Directed functional and 64-way
collision tests pass at the original 3,935 cycles; exact sprite-line OOC maps
32.5 BRAM tiles and routes at `+0.117/+0.027 ns`. This correction is part of
the retained baseline, not a collision optimization candidate. Production
route use remains 0/5.

| Blitter/collision gate artifact | SHA-256 |
|---|---|
| Retained blitter OOC DCP | `de7999dd8012a47337f309f0dea8c353036585c222c7f559f411a450d86347e6` |
| Retained blitter OOC timing | `94dd74fd9766f4a04bbe2f02643630e95c3620a2a15bcf39ece1fd47a6f3696c` |
| Rejected absorption OOC DCP | `3b1a40bed32ef05920b6e0d7f2cd08b7366e2f660a6704ddac981c9f44cf5459` |
| Rejected absorption OOC timing | `dba771bdb41c5ece4d9902594f78771b8395e00ff67365728a8f7e7d6c873c0e` |
| LUT-divider OOC DCP | `949a38b85d8e4618d33a28ab2a3e43536d3fd75bd9075bcd8dc4bf6e7ae863c0` |
| LUT-divider OOC timing | `668d0bd9549883fb352e005d7a676f61d76ec32523e5f12ba2476f860189dbc1` |
| LUT-divider OOC utilization | `3f59d57f7717c8e5b99bac519ad1ce62fe0b2dc1acc322b6d2a69e7b3eb8cf34` |
| Restored collision OOC DCP | `c8894e1b4c4a5e2b2f49f4c704dee31645bb3e96baddcc24c337250e03d999d9` |
| Restored collision OOC timing | `34a0931b3b21209aa30f15cec209a4df456efa4d76721a7adc9df6ccdfa28287` |
| Restored collision OOC utilization | `f5ae5f3b866ddd4faff242203b9c0aacbdb7d38ee97763843f99b2dc3953981a` |

### Production integration convergence campaign (2026-08-13)

The retained completion-stream register, glyph clip registers, synchronous
palette restore address, and registered command ownership reduced the first
full production route to `-0.290/-9.653 ns` across 107 setup endpoints; hold
passed at `+0.043 ns` and every net routed. Its worst path was the flood
engine's right bound through a row-product DSP clock enable. This was route
attempt 1/5 and was not flashed.

The flood arithmetic pipeline is now free-running; its valid pipeline alone
controls consumption. The Copper range check has its own state. Registering
the WAIT beam result removed that timing cone and produced exact placement of
`-0.289/-169.499 ns` across 1,518 setup endpoints, with hold at
`-0.222/-3.977 ns`, but the integrated pipeline test proved the registered
`waiting` result could release a prepared line one cycle too early. That
candidate is rejected before routing despite its timing improvement.

The framebuffer counter now supplies two registered control facts:
pixels-active and last-pixel. It retains its accounting role but no longer
drives mapped-write or the FSM transition through a wide equality tree. The
directed framebuffer suite passes with unchanged cycle counts, and its exact
routed OOC gate passes setup at `+0.244 ns`. Complete graphics regression and
a new full synthesis/placement gate are required before route attempt 2.

The corrected Copper split keeps the line scheduler's `waiting` answer
combinational, registers the same comparator only for internal execution, and
retires WAIT/SKIP directly because their actions are fixed. Focused Copper and
the integrated raster test pass, including the exact `(32,2)` MOVE. Copper OOC
routes at `+0.864 ns` with all 16 RAMB36s. A direct-comparator-to-execution
intermediate was rejected at full placement `-0.726 ns` with an 8x8 congestion
region; it was not routed. The split is the next full placement candidate.

The exact split placement uses 32,972 synthesized LUTs, 39,057 registers,
129.5 BRAM36-equivalent tiles, and 81 DSPs. It is rejected at `-0.460 ns`
setup and `-0.102 ns` hold; the flood right-bound-to-DSP-enable cone returned
because synthesis merged its nominally free-running row operand back into the
conditionally written coordinate register. No route was run.

An intentional retained flood operand boundary prevents that merge. Exact
render-command OOC routing proves both flood row DSPs have `AREG=0` and a
constant `CEA2`; the old bounds/FSM-to-DSP-enable cone is absent. Full
synthesis `full-synth-fb-flags-copper-split-flood-boundary1` uses 33,008 LUTs,
39,122 registers, 129.5 BRAM36-equivalent tiles, and 81 DSPs. Exact placement
is nevertheless rejected at `-0.510 ns` setup and `-0.193 ns` hold. Its new
leader is a Copper program-bank RAMB36 output crossing three LUT levels into
`program_read_data`; no production route was run and campaign use remains
1/5.

`program_read_valid` already qualifies that output, so the next structural
gate removes only the redundant hold-data enable mux by loading
`program_read_data` continuously. Focused Copper and Copper-control tests pass.
Copper OOC retains all 16 RAMB36s and routes at `+0.762 ns`. This candidate
must clear a new exact full placement before route attempt 2.

| Flood-boundary placement artifact | SHA-256 |
|---|---|
| Synthesized DCP | `80e6f741713b011b89a075f75b3a28bda36329b6acfe0311f16bf9f7f0fb0353` |
| Synthesis utilization | `893b71be897161cbd5e1197c6f9114cf33ba56c88cdab247ad7928a91e1fb753` |
| Placed DCP | `3df31d81a473238f446605dbb8b72bca0b56faa0527343d1251aea4df447f627` |
| Placement timing | `89cdcb8ae9740b57889c1c25aa0264b208e0361f693fcc87424ddae66241fc82` |
| Placement paths | `be046c7a4e18702a665157f5e60c03f939ddb281e6bc65e63de7e27a03145fe9` |
| Placement utilization | `5809ba1cc0b77ec53fa1505cd17f8eed8e88014a2e27911bad972c2d1ed75d96` |

The continuous Copper program-read load is resource-neutral at 33,008 LUTs,
39,122 registers, 129.5 BRAM36-equivalent tiles, and 81 DSPs. Exact placement
`full-synth-copper-read-free1` improves setup to `-0.299 ns` and removes the
program-read cone, but hold is `-0.261 ns`; it was rejected without routing.
The new setup leader reduced `command_words[12:14]` repeatedly to validate the
flood layout. One registered seven-bit word-presence vector now shares those
facts across validation. The 45-command behavioral regression passes, and the
exact render-command OOC route contains no `layout_bad_flood` or
`layout_word_nonzero` timing path; its remaining `-0.156 ns` path is an
unrelated glyph DSP-enable cone. Exact full placement is the next gate.
Production route use remains 1/5.

| Copper-read/layout-validation artifact | SHA-256 |
|---|---|
| Copper-read synthesized DCP | `1ee113a5351f53b4504aa2b86e68dff3ca90b8bbaf93faffbc435e43678093ff` |
| Copper-read synthesis utilization | `c544b7318d388708600fac426d032eda497f19501e94e7a0c2d2c8aa08afe544` |
| Copper-read placed DCP | `4030bf27bdec632322cefd8887a184f9f7772660a73c2ae3e52947f834bd7d1f` |
| Copper-read placement timing | `e5d34bf6e139511be2e8bf6c29e5b11538404c8ca7b444a4298ffbda48957508` |
| Copper-read placement paths | `1f22a0791f31f8bf55944a83cccb0612f66981d5ecebaccdb1a87cbd7807052e` |
| Copper-read placement utilization | `423f6678b834e0ab65f374b66c85e29b875415803da73095ea91e27ff29c362e` |
| Copper-read control report | `d087710644a995f805cb2f7d82ef31f4a40faa585def9e403edb09f69cc025e8` |
| Layout-validation OOC DCP | `fb5eb2e7b8455ae17074f4edc6f25ac11673ec797774b44b90bc9932d3ef82a9` |
| Layout-validation OOC timing | `d4cd9085dfe7d22487cefcac164b0bf1c24bd213b30af05989887c834a701161` |
| Layout-validation OOC paths | `72accd8e0261291ebac9856df1257a6fe6cc72d682bdaae3a741c630668f7249` |
| Layout-validation OOC utilization | `c6e43a40c178e4a8c9ad31b0f22e269a59c081818842e4e376b837d37ffbdae6` |
| Layout-validation OOC route status | `e378f11cd1a673aa7b230b6c34c00eb6fa354db7ca83b127f1cdee527cd55168` |
| Layout-validation OOC methodology | `7e5cb46c9d84f49e57882049ef1c3c996579d60dfe11ed1c558646ee55e1f544` |

Full synthesis of the shared layout facts uses 33,073 LUTs, 39,127 registers,
129.5 BRAM36-equivalent tiles, and 81 DSPs. Exact placement is rejected at
`-0.504 ns` setup and `-0.186 ns` hold; no route was run. The validation cone
is absent. The new leader is the flood right-edge comparison from `active_x_q`
through three carry levels and state decode. A registered right-scan-exhausted
fact now updates only when `active_x_q` is set or incremented, preserving the
scan cadence while removing the comparison from `ST_RIGHT`. Focused flood and
the complete 45-command behavioral regressions pass. Exact render-command OOC
routing contains no active-x-to-state path; the remaining leader is the same
unrelated glyph DSP-enable path at `-0.156 ns`. Full placement is the next
gate; production route use remains 1/5.

| Layout-placement/flood-fact artifact | SHA-256 |
|---|---|
| Layout-facts synthesized DCP | `3ae1a08292fb815e3fda1d151457c3dfdfe257d80f35bc9f1440943592f2a830` |
| Layout-facts synthesis utilization | `48f6b9728a0f04a7aa3c8a452d4795e6187797e975ea69532dc961768591d51a` |
| Layout-facts placed DCP | `780c5bbfaf21a6ab90970e34c64917136c3121b9f7f8fb0621ae1f4dde6908e7` |
| Layout-facts placement timing | `66180a4bccbcc1ff576cc73cd52f588de07aa6bd75b44cf4918b1ffdc04d8bde` |
| Layout-facts placement paths | `516d2fab702d9ea15e1d557bdb81f7fce6bfe539208aa01bbcfdcba4a1ce9270` |
| Layout-facts placement utilization | `195403128687b41794b24a40f952118093f5356eca18d9a63ba9fe7b7713f11c` |
| Layout-facts placement control sets | `5972c511c3f065d5d9fd6d80b52942003fa3ec38380e42e117910e653c2e262c` |
| Flood-fact OOC DCP | `21f612c5a14add9bfe68d494d7e20a588c7209584e8db88200dada0723892b0c` |
| Flood-fact OOC timing | `225f6da05b585a3b364df08802462e363275ecde43f6d5c058d42faa0ffb70a2` |
| Flood-fact OOC paths | `000bbc6d4d61a2cdbf48a7ebda797344b62d586631a14d7ca0370c215aa2a987` |
| Flood-fact OOC utilization | `a37ef571cc576e1a1d1debc55048ce7d17a378eb8339754bb8e07330adcba9a8` |
| Flood-fact OOC route status | `e378f11cd1a673aa7b230b6c34c00eb6fa354db7ca83b127f1cdee527cd55168` |
| Flood-fact OOC methodology | `147965b8dad7d0b9943eef1f2df4672d5f948436019ac34c3bf2b4abf321c061` |

### Five-gate stop and structural cut (2026-08-13)

The first flood-fact OOC and full-placement artifacts above are contaminated:
Beast still had pre-change `astra_render_flood.sv` hash `588ae...`, so those
results reproduced the prior netlist and are not evidence for the registered
right-scan fact. They remain recorded to explain the discarded work. After an
explicit source sync, render-command OOC routed at `-0.282 ns` with the target
active-x path absent; exact full placement used 33,052 LUTs, 39,129 registers,
129.5 BRAM tiles, and 81 DSPs, but rejected at `-0.719/-0.281 ns`, led by the
glyph source-format output path. No route was run.

Registering the glyph source-application kind removed that cone. Its OOC route
improved to `-0.133 ns`; exact full placement used 33,088 LUTs, 39,129
registers, 129.5 BRAM tiles, and 81 DSPs, but rejected at `-0.447/-0.120 ns`.
The leader moved to command dispatch, followed by geometry's line-delta carry
chain. A two-bit centralized engine selector was tried only at OOC, regressed
to `-0.405 ns`, and was immediately reverted without a full gate.

Registered geometry direction facts removed the eight-CARRY4 line-delta path
and routed geometry OOC at `-0.032 ns`. Full placement gate 4/5 reduced the
synthesized design to 32,798 LUTs, 39,120 registers, 129.5 BRAM tiles, and 81
DSPs, but rejected at `-0.500/-0.343 ns`; its leader was blitter command height
through repeated equality logic into `direct_copy_q`. Three command-time
facts removed that path, passed blitter and all 45 command tests, and routed
blitter OOC at `+0.053 ns` using 2,658 LUTs, 3,187 registers, and 11 DSPs.

The resulting exact full placement was gate 5/5. It used 32,843 LUTs, 39,124
registers, 129.5 BRAM tiles, and 81 DSPs and rejected at `-0.698/-0.393 ns`.
The setup leader was flood pixel data crossing the centralized two-entry
dispatch FIFO into its payload register: only two LUT levels but 4.298 ns of
routing. The campaign is closed at 5/5. No sixth placement, production route,
or flash is permitted from that campaign; production route use remains 1/5.

The measured structural cut removes the redundant two-entry dispatch FIFO.
The shared pixel writer already has a registered ingress slot, a two-entry
pixel stage, and a 16-entry write FIFO, so producer backpressure now terminates
at that existing ingress. All 45 command tests pass with unchanged outcomes
(`reads=389`, `writes=139`). The first new-campaign OOC route removes the
dispatch path and drops render-command registers to 12,014, but rejects at
`-0.285 ns` on a pre-existing blitter state-to-source-address path. It is not
advanced to full placement. New campaign use is 1/5 cheap OOC gates and 0/5
full placements.

| Corrected convergence artifact | SHA-256 |
|---|---|
| Synced flood OOC DCP | `1d7a11a86711c35892d153b32f6b539213551dee976fc92a85ed9c6d561180d4` |
| Synced flood OOC timing | `c4194d003a03b87224dab116697b5f8564957706743bbd5144f5552db2953687` |
| Synced flood placed DCP | `7c6643f2a6618443b0356f422c2a0fde9ebd3cd381151a57882a35717d471791` |
| Synced flood placement timing | `1736e6c111a776266a979019a99dd49358c0713cb31a40f4dd7ebfa17d119f25` |
| Glyph-fact synthesized DCP | `4a050e4b1e2d0761917948e61da9dd2e7afad74da45c01f747c7b7318398213d` |
| Glyph-fact placed DCP | `82b48edd1c54c6ed9d9b392d0290490ece7d8d994d8cb88afb37a5db7e9a472f` |
| Glyph-fact placement timing | `0bf6e595864430921d7d942728d05131a9812aadf790c832413934e7c87f504c` |
| Geometry-fact synthesized DCP | `f49ad0822da91643619cf1c8844b5a1ed95b66709ab458fc5436b620f289a390` |
| Geometry-fact placed DCP | `338fcff1f3e1e25d396a88734af63babdaf0b66942fbc34459ba2c2175059fb1` |
| Geometry-fact placement timing | `a21ce4dd3bfba41824933b923915a4d45d921ade526393d79cb40e9d500722ec` |
| Blitter-fact OOC DCP | `7e1a2fe1be6dfcf8758ae15e0e089e8dfdf9758062188301047d17690ae02702` |
| Gate-5 synthesized DCP | `03c21d31714943830aedf01a6712418223cd9b83a4408ae492e0a6b5009e3f05` |
| Gate-5 placed DCP | `6761ab0acd5abff7b7f15922728b2856126bcef1f1ceb9ce2bc44d6fd449099a` |
| Gate-5 placement timing | `eb2cb6ad3eec8717abad879d1b1d605fe00426ac76978b5149e0a9dfbf806431` |
| Dispatch-cut OOC DCP | `6ae9c15e9e1b7a57e969e73ff5efe34fa9980cbca09e328975583a1bcfd74894` |
| Dispatch-cut OOC timing | `753363d41ac23c8d1955c739dab613930a2a5af6841566e6c748cd7959d35d6f` |

### Dispatch removal and Copper boundary campaigns (2026-08-13)

The dispatch-removal campaign stopped early at gate 4/5. A registered blitter
source-address commit removed the gate-1 leader; Copper ownership feedback was
also deleted because the engines are mutually exclusive and the writer already
provides backpressure. Gate 2 OOC rejected at `-0.303 ns` on glyph destination
format decode. Registered glyph destination-format facts plus raw, rather than
preconverted, pixel-writer ingress improved gate 3 OOC to `-0.084 ns`; the
targeted glyph-format and flood-ingress families were absent. Exact full
synthesis then used 32,711 LUTs, 38,971 registers, 129.5 BRAM tiles, and 81
DSPs. Gate 4 placement improved the prior campaign to `-0.404/-0.262 ns`, but
its setup leader was Copper bank-1 word 1 BRAM clock-to-out through the bank mux
into `validate_w1_q`. No route was run, and gate 5 was deliberately not spent.

| Dispatch-removal artifact | SHA-256 |
|---|---|
| Gate-2 OOC DCP | `e5c19c9d5cd1ee4943db7d4a582fec73c406534e63638fe85a9d6ebd7cc258a3` |
| Gate-2 OOC timing | `c5a7e3ca8241468e0583bfcfd8d74310f978f3c4535704d069a1a7545c00ffa1` |
| Gate-3 OOC DCP | `8c77e7a35796455c3afc3e0d58e4f0cb929debbaa426df2de5161a0bbb84dca6` |
| Gate-3 OOC timing | `290307d29d94fdb540d995fd8ac8f52b473cfbb6dbcfa8f123c7bad55d40c8ed` |
| Gate-4 synthesized DCP | `3d46ddda6ff6b10929c9b485909beb89342c855284640cec284732e3c857a046` |
| Gate-4 synthesis utilization | `a969a5143813b1b747c59928fa169a682ae5d71013ac90a9e10a62f295e6cc4d` |
| Gate-4 placed DCP | `c2af723113aa771cc20d494525282a112f22cdb5fd1d6b10e1b800c08ec7e600e` |
| Gate-4 placement timing | `6ba6d5d6d2bab1c22962989162171fa5ae7bd7692e6beaeb7fea32ee123f4df5` |
| Gate-4 placement paths | `de4742cdd4259df3f48c12a477ca843acbd226ee17b349f0a042dd12391b56c7` |

The next campaign reused the execution path's existing four-word BRAM capture
stage for validation, keeping it warm every cycle and moving bank selection
after that register boundary. Gate 1 Copper OOC routed at `+0.880 ns`, retained
all 16 RAMB36s, and removed the exact validator path. Exact gate-2 full
synthesis used 32,685 LUTs, 38,869 registers, 129.5 BRAM tiles, and 81 DSPs;
placement improved to `-0.350/-0.209 ns`, led by the other direct Copper BRAM
mux into `program_read_data`.

Readback now uses two registered two-way selection stages and its existing
valid handshake, rather than one four-way BRAM-output mux. Direct Copper and
AXI-control tests pass. Gate 3 OOC routed at `+0.415 ns`; exact gate-4 full
synthesis used 32,697 LUTs, 38,935 registers, 129.5 BRAM tiles, and 81 DSPs.
Placement reached `-0.285/-0.126 ns`, with both Copper BRAM leaders absent and
glyph state-to-DSP-enable exposed. This was route-credible, but the required
full regression caught a real integration fault before routing: moving the
old permission wait ahead of capture removed the structural permission
pipeline, so validation rejected the first MOVE.

The retained correction has separate pre-capture and post-capture states. It
passes direct Copper, Copper-control, and integrated graphics-pipeline tests.
Gate 5/5 Copper OOC routes at `+0.838 ns`, uses 595 LUTs, 662 registers, and all
16 RAMB36s. The campaign is closed. Gate-4 placement is invalid for the final
source, no production route or bitstream was generated, nothing was flashed,
and production route use remains 1/5. The next campaign starts at 0/5 on the
measured glyph state-to-DSP-enable cone.

| Copper-boundary artifact | SHA-256 |
|---|---|
| Gate-1 OOC DCP | `e6707c60fa5e2e14fe3ab6b96ca31ea923a83f7c2d266425c907af4248157f49` |
| Gate-1 OOC timing | `7a5b55afe48a1a80a5aa24ebb256f710a1f4a775bbad131f45f3878749b9cd89` |
| Gate-2 synthesized DCP | `d1594d0279f2f40b96f3b5aae1d3b56d4767fa8e684e886da619bfa49e9c0d00` |
| Gate-2 placed DCP | `e51fbea26604e6d1a81dee723c8a084ca5357cb0b00ea6f13f20e6bbdf127dcf` |
| Gate-2 placement timing | `2a250cd65783cc592c6bbfe5306b2f2ea1b15d965c3a03f867d5d4a7e500a3a5` |
| Gate-3 OOC DCP | `4186c36d460fd2d4650b56d53a8fe5857c1ac9b2918eab0cb6164e05045a5f17` |
| Gate-3 OOC timing | `2774d14fafc32c4f322b3966c1bdaa9ca47dffc8232d7c557f101a6c90959f3e` |
| Gate-4 synthesized DCP | `db249e43dfcd58b69e8232a4f98d53a0900d206d09b0fdef472fc8ae3a743793` |
| Gate-4 placed DCP | `c97e8df550801274febee488c8e457ae6ab623ee37967ad1f2c9f72a0324a4f1` |
| Gate-4 placement timing | `c8acd2574116128675de41bd1be6c3e108d7ea37a1cba21627033812f080ddcf` |
| Gate-5 OOC DCP | `fbb0b85ea430f16e64e5828ace7a0d6a470f121ef422f58568c75892865806bf` |
| Gate-5 OOC timing | `9c517263ebfd7af7f5fa9e6b655c7ff2c86a975199a4740633303a5f50fbcee9` |
| Gate-5 OOC utilization | `a72e1ecadb37d803954761baca75b4290aa766580fb5112d81eda08d7519f9b7` |

### Glyph, AXI-ready, and blitter boundary campaign (2026-08-13)

This campaign stopped at gate 4/5. Preserving the glyph row-multiply operand
registers removed the measured glyph-state-to-DSP-enable path. Exact
render-command OOC then rejected at `-0.303 ns` on command ownership feeding
glyph state. AXI read ownership already gates `ARVALID` and the address mux,
so returning shared `ARREADY` directly to each engine removed that feedback
without changing a handshake. The next OOC route improved to `-0.137 ns` and
moved to the blitter's product-to-ARGB divide path.

Exact full synthesis used 32,056 LUTs, 38,701 registers, 129.5 BRAM tiles,
and 81 DSPs. Placement rejected at `-0.605/-0.184 ns`; the setup leader was
the same blitter path: nine logic levels, including six CARRY4s, between the
DSP product and result register. The source blend phase now reuses the
destination phase's existing registered `/255` boundary. The blitter test
passes at the unchanged 5,822-cycle identity-RGB565 checkpoint. Gate 4 OOC
proves the product-to-result path absent, but rejects at `-0.227 ns` on an
unrelated engine-done/fault clock-enable cone. It was not advanced to another
full placement. No production route, bitstream, or flash was generated;
production route use remains 1/5.

| Campaign artifact | SHA-256 |
|---|---|
| Gate-1 OOC DCP | `8af46d164314c36cfefb42f4e37ceedbdd8c438053b31c6d2a0ec9eb65515cb4` |
| Gate-1 OOC timing | `20f028feae636b0a03e4918d3fdca57c0b158c204da786629a183965506f4bc6` |
| Gate-2 OOC DCP | `f63e55f8f8ab22bffbeee2d262bcb5c009fe264d1bf6f7f8df8a80050147efda` |
| Gate-2 OOC timing | `3377c82b9755d051a063bd6aa16567e63bdfe43d8722f6b95430fd9d8847b769` |
| Gate-3 placed DCP | `3075d1cd766e704f115947907afb327b4eaece54ad8b4c3b9d374cfba3c9be2d` |
| Gate-3 placement timing | `9c5596ba885de9ace808055b7cfac18777112c18e9031418d732a0668a7fa816` |
| Gate-3 placement paths | `526942c5bcd7ac4a4d96e8264800e401b2c7e0d2ef95e1a91e558fdf903492e0` |
| Gate-3 placement utilization | `e4cde180295be40908221c680fbc65e960095193e99fee9ec3fa2b07cc053cb3` |
| Gate-4 OOC DCP | `7b73a0c17b7ec0a5a57d79f4769b15d284492315ab7e5b96212435926918a187` |
| Gate-4 OOC timing | `a113ebb396eefd4dc6f3ebb5a047ab9dac7746c6e97b99f94c283fd97980a918` |
| Gate-4 OOC paths | `206e2b3787baef470a829454ac0f248e499637259437498b80e7aaf1d03e2690` |
| Glyph RTL | `318c0348b475cb904bfd9baeb6583a8dd1d1d8fe73432105ef6f0beb28662ab3` |
| Command RTL | `1b6ee075b87d08b9f19867ad5382f0fee61e45d92b6a2bed3edd869c3ef924ae` |
| Blitter RTL | `4844e9088af53bd1858aa65eb9b08a4af78932657dc379975c56179708836633` |

### Completion boundary campaign and implementation-flow reset (2026-08-13)

This campaign is closed at gate 4/5 without spending another full placement,
production route, bitstream, or flash. Registering engine completion capture
removed the prior done/fault clock-enable cone and improved exact
render-command OOC setup from `-0.227 ns` to `-0.076 ns`. Reusing a registered
stack-nonempty fact removed the next flood counter-to-state cone, but moved the
OOC leader to glyph reset extraction. Applying the existing no-reset-extraction
policy to the glyph FSM removed that structural path and produced `+0.006 ns`
OOC setup. The remaining leader was a zero-logic flood operand-register to DSP
input route. An extra operand stage failed its stated mapping criterion:
Vivado retained `AREG=0`, so the run was stopped and the source was reverted.

The retained source hashes are command processor
`24a47a3d3359c1d4947f4bf617a3e47131e4f49fa5ae959d1b0a992058be1b11`,
flood
`eda502149156503f948cf77d3b9a3e7f67f4aee96d028b771248861d83ab8a78`,
glyph
`6d612a5e4c205dce605b745003248289ca238005c4246c7db8da4f344228aaae`,
and blitter
`4844e9088af53bd1858aa65eb9b08a4af78932657dc379975c56179708836633`.
The 45-command regression passes (`reads=389`, `writes=139`), the flood
regression passes (`normal_pixels=67`, `overflow_pixels=8`), and the blitter
identity-RGB565 checkpoint remains 5,822 cycles.

| Completion-boundary artifact | SHA-256 |
|---|---|
| Gate-1 OOC DCP | `435923570b3475f927753d1beaf5fe91794f1dc9533ce8b9ab15bdafeb2ba619` |
| Gate-1 OOC timing | `3f0b3ad9f9459e797e2dca839976243637b96063cb4a8218f6e169eb6fa83969` |
| Gate-1 OOC paths | `55958e3da535436466d718712a34e88d2e6ceaada4d6388ef6d7439241ff8937` |
| Gate-2 OOC DCP | `043ae4d1cbcd2ed369db66fce3af90a9270f288dad5d755a1490e5c254589f63` |
| Gate-2 OOC timing | `04ca1937a557c1755fe7f89a6726d820708a8afae7d4a8218f6e169eb6fa83969` |
| Gate-2 OOC paths | `f5b921ab2f618135e12004c28e8deccae4e06c0d118683685dbc9bd7c801104d` |
| Gate-3 OOC DCP | `ccd222dd3fb846014a9a06f4f88b798947d5c8c2bcbb14cf395206c54bbff826` |
| Gate-3 OOC timing | `ad770dca2aebee33706d89fa0075cd3f199c1cb2629ec9f7872189acfc6d5e8c` |
| Gate-3 OOC paths | `f8d113835939f7fa32365408a3d7a4e19b053430b970fd67a458daab2fdf79d7` |

The prior OOC loop is no longer an admission gate. These OOC designs have no
top-level `HD.CLK_SRC`, physical partition, contained routing, or fixed
partition pins, so they are useful for proving a cone disappeared but cannot
predict placement of the complete 129.5-BRAM/81-DSP design. That mismatch is
why a local improvement repeatedly exposed a different integrated path.

The next closure work is build-flow work, not another RTL edit. Vivado's
existing incremental-checkpoint hook in `build_graphics.tcl` will first be
qualified against a preserved full routed checkpoint. Stable reusable units
will then be piloted one at a time with nonoverlapping Pblocks,
`CONTAIN_ROUTING`, clock-root placement, fixed partition-pin regions, and
explicit interface delay budgets. The intended freeze boundaries are the
whole `render_command_i`, the sprite line builder, and Copper control/events;
the blitter, glyph, flood, and geometry engines are not independent physical
partitions because they share command, AXI, and pixel-writer infrastructure.
An unchanged partition is not reopened for a downstream feature campaign.

The release sequence is now fixed: behavioral regression; contextual routed
partition checkpoint; one incremental full integration; exact timing and
route-status gate; then bitstream and hardware qualification. A checkpoint is
promoted only after the integrated route passes. Any campaign still stops by
gate 5, but repeated unconstrained leaf OOC edits no longer consume the path
to a production bitstream.

### Incremental implementation pilot (2026-08-13)

The first incremental campaign is closed at five controlled invocations. Two
invocations stopped before synthesis while the Vivado 2024.2 project-run step
name was qualified; they are still counted. No sixth invocation is permitted.
The build now runs every enabled implementation step, writes route, timing,
reuse, and utilization evidence, and refuses to create a bitstream unless all
routable nets are complete and both setup and hold slack are nonnegative.

A stale pre-pilot reference reused 71.14% of routed nets but regressed to
`-0.986/+0.020 ns`; it proved that preserving an obsolete render/HP2
neighborhood is counterproductive. A clean exact-current-source
`Performance_Explore` route then established the new reference at
`-0.397/+0.010 ns`, with all 66,191 routable nets complete and zero route
errors. Its unconstrained block footprints overlap heavily, so no bad Pblock
or route was frozen merely to claim a partition.

The clean reference exposed the campaign's actual worst path in the shared
asynchronous FIFO: audio AXI address state crossed pointer increment, Gray
conversion, and the full comparison in one cycle. The retained FIFO registers
full state and updates it from the already-computed next pointer. HDMI audio,
both AstraHost simulations, and the complete graphics regression pass,
including all 45 render commands, exhaustive 1..128 sprite dimensions,
Copper, palette, and the other shared-FIFO users.

The final exact incremental route matches 99.75% of cells, initially reuses
99.77% of placement, and finishes with 98.71% cell and 92.60% net reuse. It is
fully routed (66,216/66,216, zero errors), with `+0.010 ns` hold, but setup is
still `-0.283 ns` across 525 endpoints (`TNS=-42.693 ns`). The audio path is no
longer a leading failure. The leaders are now glyph cache-to-blend DSP input
(`-0.283 ns`, 73.3% routing), command-kind-to-blitter palette/cache enables
(`-0.273 ns`, 82.7% routing), and Copper structural MOVE target enables
(`-0.235 ns`, 76.2% routing). Post-route physical optimization skipped setup
work because the automatic incremental target inherited the reference's
negative `-0.397 ns` WNS; the next campaign must use a zero-slack timing-closure
target rather than `RuntimeOptimized` against a failing reference.

No bitstream was generated and nothing was flashed.

| Incremental-pilot artifact | SHA-256 |
|---|---|
| Clean current-source routed DCP | `2b94af5a67bcc73684f7e893a955c0f0ce185d9651baa22118a75ef6974b0b54` |
| Clean current-source timing | `851d5ba9ba50123109bc286c82f4f563df47e4261fbb762148c5279432577cac` |
| Final incremental routed DCP | `c4a5c860ad48cbb5457fd1697610ff1f91db43af44bc86d8d5b3147226b28a5a` |
| Final incremental timing | `e3338ca0d85aa4d274c3e038768e2bb00bb98391ff68679dd9c10d175df38104` |
| Final incremental route status | `4fb763578c187f820f117033f28f94dd5109d95a5b556303062431943423e5fa` |
| Final incremental utilization | `fd6e6d9824dfeba6982aecb9d02955776a45e7ced2eacc1c503f86322f3a4323` |
| Final incremental reuse | `d88b19bea13262981e12d8fcdc91de20cc555b3e535a821a2d2cb075abf1ba35` |

### Zero-slack convergence campaign (2026-08-13)

The two leading pilot structures now terminate locally. The blitter response
valid uses its own busy state instead of reconstructing ownership through the
global flood/glyph command classification, and the glyph destination beat is
decoded into an existing state boundary before the blend DSP pipeline. The
complete graphics regression passes, including all 45 render commands, the
5,822-cycle RGB565 blitter checkpoint, glyph rendering, Copper, and exhaustive
1..128 sprite dimensions. The retained RTL hashes are command processor
`548ab839587e2906b0e0a659307739abbafc44416bcfefa6a6ce4eaefa48fac3`
and glyph
`92310f60a07e7dce99204e62b0d96a6ffe7a1df54860d2f50b0b5951d7778a9c`.

Vivado 2024.2 exposes the native run property
`INCREMENTAL_CHECKPOINT.DIRECTIVE`; the build now sets it to
`TimingClosure`. Run 1/5 proves the generated implementation command is
`read_checkpoint -directive TimingClosure -incremental ...`, with target WNS
`0.0`. It also records a rejected strategy-control experiment: plain
`Performance_Explore` omitted the pilot's required post-route physical-
optimization stage. The route completed all 66,540 nets with zero errors and
hold at `+0.010 ns`, but setup was `-0.582 ns` across 1,269 endpoints. The old
glyph-cache-to-DSP and command-kind-to-blitter paths are absent; the new route
leader is pixel-writer FIFO count to barrier completion. Resource use is
32,330 LUTs, 38,942 registers, 129.5 BRAM tiles, and 81 DSPs. No bitstream was
written. Run 2/5 retains the same RTL and restores the pilot's exact
`Performance_ExplorePostRoutePhysOpt` strategy.

| Convergence run-1 artifact | SHA-256 |
|---|---|
| Routed DCP | `764ec365f565c1736647ffd99a606b89eb34dd45aecb925e12787a3f143d57d7` |
| Timing summary | `7c3bd14325a09f35ec8b3224cb84f2410d8bdc75da1c7cee097e4b392342fc0f` |
| Route status | `489de5ce6fcc9cb397aedd49b5644c53c880cbf4d7440f4578af8090bf286a4a` |
| Incremental reuse | `9f36bd4f9c312e512deb133f89c716e3b4352af9278d120955eb0acf115fb8e1` |
| Utilization | `175ea53e82535ce0b2c3e124e4f1abc93a8377e1a22ae468375333a106c18ff1` |

Run 2/5 restores the pilot's exact `Performance_ExplorePostRoutePhysOpt`
strategy against the same final-pilot reference and uses the native
`TimingClosure` target of `0.0`. All 66,517 routable nets complete with zero
errors. Route timing is `-0.671/+0.010 ns`; post-route physical optimization
recovers 64 ps to `-0.607/+0.010 ns`, with TNS `-136.809 ns` across 1,114
endpoints. The checkpoint is rejected and no bitstream is written.

The first 38 setup endpoints are one structural family: command-kind decode
still crosses engine response consumption and the HP2 renderer-facing slice's
payload enable. The worst two are `-0.607 ns`, with 80.6% of the 4.899 ns data
path spent routing. The next independent leader is registered response ID into
glyph response/error state at `-0.584 ns`. This evidence authorizes one shared
AXI response-boundary correction before run 3; it does not authorize another
strategy sweep.

| Convergence run-2 artifact | SHA-256 |
|---|---|
| Routed DCP | `7416782e9fc85890d46598b2a52de0cfcc3b2e5281e5af52bbbf79ed63aee80d` |
| Timing summary | `b040cbaf92d03297c93100fd3cd1082fdcc042d44a75c7088cc19fe7231a5eea` |
| Route status | `7c4b0b2a012773e818401a10e281386df1f863ffc93456d035e3f3e52d895c7c` |
| Incremental reuse | `bd2d1152c81fd1e731305bcf27dd069fbcf419deea39fc56c74f70b9e93fab30` |
| Utilization | `c169dab7084513f79bd4bd5032a07fbdd40868fe7ff0550f3da693219fca2558` |
| Failing-path classification | `926eeaedfe8fff838d56155ea2d652dccd31366f99d746ecacc41375506c4a80` |
| Detailed failing paths | `d8293103ccc8a98bfaafcedc6b95090dea16ed7ce47d95b516dd9daad679460f` |

Run 3 is authorized by two boundary tests that failed before the RTL change
and pass afterward. The engine read-response boundary now exposes registered
two-beat capacity to HP2 and predecodes response metadata once; the pixel
writer now reports its registered ingress capacity independently of pending
flush/barrier/abort policy. The complete graphics regression passes, including
all 45 render commands and exhaustive sprite dimensions.

The exact render-command OOC diagnostic improves from `-0.303 ns` to
`-0.080 ns`. Both integrated run-2 response-control families and the first
diagnostic's flush-pending-to-glyph path are absent; the remaining diagnostic
leader is local glyph state to blend-stage enable. OOC remains diagnostic
because its clock root and partition pins are absent. No further OOC edit is
authorized before exact full integration run 3/5.

| Run-3 pre-route evidence | SHA-256 |
|---|---|
| Command processor RTL | `12321eb247c4cbd48a5a13d1305a4fca53482182323210b45627eceab1e5d0b3` |
| Pixel writer RTL | `c4fb4e6d831094524ad5ac0d0992f81169ae85c46befe424d74cb8488fd4f89e` |
| OOC routed DCP | `0dd902792138521d2d1206563d2cbac08edcd23e85c31e10d4e6c0add7219887` |
| OOC timing summary | `5289e790beba9fd1683cc08b9f163e1603827a56cbed9da08270381388193f4d` |
| OOC route status | `893966bd7da77190172088626a2b96c3b5c53a7e824fa277257f9fbc6f6bd7c8` |

Run 3/5 is rejected. The exact full design routes all 66,760 nets with zero
errors and `+0.010 ns` hold, but post-route physical optimization stops at
`-0.500 ns` setup with 1,297 failing endpoints. Resource use is 32,325 LUTs,
39,001 registers, 129.5 BRAM tiles, and 81 DSPs. No bitstream is written.

The path census identifies three leading shared boundaries: pixel-writer
flush policy still leaked through internal ingress readiness into flood state
(`-0.500 ns`); glyph source format was reclassified on every sample
(`-0.492 ns`); and queue occupancy controlled all 32 command-address enables
(`-0.434 ns`). Three boundary tests fail on the run-3 RTL and pass after the
correction. The exact command-block diagnostic then routes at `-0.050 ns`;
all three integrated families are absent and the diagnostic leader is local
flood state to active-X enable. Full run 4 will use run 3's exact-current-
source checkpoint rather than reopen the older pilot placement.

| Convergence run-3 artifact | SHA-256 |
|---|---|
| Routed DCP | `377afab01d7fa3166b9035eec7db492ef62ad6d2e50e9577bb9d76bffc9e9c2f` |
| Timing summary | `0ae6fe22d1948a2533ae52e9bfce7e1a18998f17f16882b188db737b6d2f5ee1` |
| Route status | `f30cb10a71a2a9b4beeb2f1cc5633fd3e4858403593b127c520823abdb902069` |
| Incremental reuse | `6641905028e32da9bcf80dfbc3934b2a08eb1e543ed6a4c4f7b7476c60fd19e4` |
| Utilization | `8291cfbfc5d0b7f265b3ba1caa835691ffa49e70b2983d9207323a9744cabafa` |
| Failing-path classification | `61136d854580071085efb27e34b303499fdae2dfaffcac91ae3780098159f47f` |
| Run-4 candidate OOC DCP | `6922de814d95f413c50fcb19fdaab3a52998255b94e89cba808f3ba9982d5e65` |
| Run-4 candidate OOC timing | `f736a8ee67592a9d04dad235159deb98780ff76ee5efdca5a281e2f838519e18` |
| Run-4 candidate OOC route status | `30fb1f7c527625e49a5aef5e2206a861b12ae38a5ece51d54f9cb9e80862dc77` |

Run 4/5 is rejected. The exact full design routes all 66,804 nets with zero
errors and `+0.010 ns` hold. Incremental re-placement improves routed setup
from `-0.669 ns` to `-0.375 ns`, and post-route physical optimization reaches
`-0.347 ns`; 886 setup endpoints remain, so the fail-closed gate writes no
bitstream.

The failure is now distributed across command control (403 endpoints), glyph
(255), flood (215), and blitter (13). The worst paths spend roughly 75--81%
of their data delay routing, while the exact command OOC diagnostic was only
`-0.050 ns`. Vivado reports no congestion window above level 5, but identifies
92% BRAM use and generates automatic placement-time replication suggestions
for the long command/glyph/flood control nets. Run 5 is therefore gated on the
tool's reusable QoR suggestion file and a clean, non-incremental integration;
another leaf RTL edit or inherited failing placement is not authorized.

| Convergence run-4 artifact | SHA-256 |
|---|---|
| Routed DCP | `4b714b5f72dcb6a433806f2262d402620603c5c99ac4ce626469a13625329bf8` |
| Timing summary | `add09d51bee3116b5f4bab3e14ffffb1cd0a1c4649daf0472f16882d46899986` |
| Route status | `752f8f2c47bff9f5b0317b91df5d56f665f79de84da84798b2d1370ba8a6dad3` |
| Incremental reuse | `dc3655e9ed913d482ce5f1dd381b5e4ecf846c7bfe9135dde4bf602d37d0a74e` |
| Utilization | `07940490e4df1a7bfad99ff48d999ca5b1f5119e663db7543fe0a340f2e379aa` |
| Failing-path classification | `837f67f459e55131503f4252437dcf9eec51e79f6d513a973fe5ecd6d23d3d83` |
| Detailed failing paths | `ffed6767a87b0aa6ae16f0c513d219ee074652b43c79168e03286aa397aae6aa` |
| QoR suggestions report | `4542d13e651dd03a264982eafae7ed85705d24cbdf745c268d0a9748f6fcd869` |
| QoR suggestions file | `244dddca1a80a45515e1387e80de28e84ebabd1c1fcaca9f161fa4bdce63ec32` |
| Congestion report | `9a284e176a4e823e0b28f0f85287659421f5fdcb767d3068148da65f56168d9f` |

Run 5/5 is rejected and closes the zero-slack campaign. A synthesis-only
qualification first proved that the run-4 QoR suggestions were attached to
both `synth_1` and `impl_1`; the accepted resource mapping reduced BRAM from
129.5 tiles (92.50%) to 105 tiles (75.00%), with 37,471 LUTs, 40,547
registers, and 81 DSPs. The exact clean, non-incremental full implementation
then routed all 69,178 routable nets with zero route errors and passed hold at
`+0.017 ns`, but setup stopped at `-0.277 ns` across 153 endpoints. The
fail-closed gate wrote no bitstream.

The residual is no longer a broad command-placement failure: 99 endpoints are
in sprite clear/copy and working-line memory control (worst `-0.277 ns`) and
54 are outside the sprite builder (worst `-0.225 ns`, led by command deadline
state). The leading sprite families are preparation state to clear-quad
control, sprite completion to scheduler slot enables, validation priority to
order-memory inputs, and clear/palette quad selection to working-line memory.
This evidence starts a new structural campaign at 0/5; it does not authorize a
sixth strategy or seed attempt.

The run used a temporary remote output directory that was removed after the
intentional nonzero timing-gate exit. Consequently the path census and exact
gate values were captured from the retained session output, but the routed
DCP and reports are no longer available for honest SHA-256 provenance. This
is a build-evidence defect, not a timing waiver: every new campaign output must
be a persistent remote path and must be hashed before cleanup or replacement.

### Sprite clear/copy structural campaign (2026-08-13)

The first candidate separates the working-line clear address from the later
line-store copy address. A directed assertion fails on the prior RTL when copy
resets and advances `clear_quad_q`; it passes after copy owns a distinct read
counter and enters through a local initialization cycle. All eight sprite
simulation modes pass through the exhaustive 1..128 width/height and pitch
matrix.

Gate 1/5 is an exact 200 MHz sprite OOC route. It retains 37 block-RAM
primitives, routes completely, and passes hold at `+0.028 ns`. Setup rejects by
4 ps across three endpoints (`TNS=-0.011 ns`), so it is not admitted to full
integration. The run-5 clear/copy families are absent. The sole OOC leader is
instead render-slot selection crossing buffer-ready decode into the phase
register clock enable, with 80.4% routing delay. A second directed assertion
fails before and passes after using one otherwise-free render state to capture
slot payload locally; that gate-2 candidate is regression-gated before route.

| Sprite campaign gate-1 artifact | SHA-256 |
|---|---|
| Routed DCP | `c8aadc47d5d18b529b0e9f6ef864ce5fd9696b30f1795ccd3c9f322e7aca86c0` |
| Timing summary | `1fd58ed6c3652103c3c0ed2b0adea9cf1733cab5ae0800f86dce3717ca0fb023` |
| Route status | `9c3533dc1fddf6936019654802a26f0f7a20ee3430442940898ed78f82c1a392` |
| Utilization | `f7811afd15a13082718938184f65b72d7bdde0ff4d77179e2e9aa36dab2d5b42` |

The first local-load candidate was rejected before gate 2 because its extra
cycle per sprite exceeded the existing worst-case contract: 3,927 cycles
against a 3,900-cycle limit. The retained implementation instead preloads the
selected slot while render state is idle; readiness only advances state and
does not gate the payload registers. Its directed preload test fails before
and passes afterward, and worst-case performance is 3,878 cycles.

Gate 2/5 passes exact sprite OOC routing at `+0.147 ns` setup with all routes
complete and all 37 BRAM primitives retained. The complete graphics regression
also passes: every sprite failure mode, 64-way collision, exhaustive 1..128
dimensions, all compositor/Copper/control/pipeline tests, and all 45 render
commands. RTL SHA-256 is
`6a2faadc5c794885a903225dfded5ecbff31751c060319a632d8cd0df0d85bff`;
the directed test SHA-256 is
`8efa96b629a3ab036a2331844c20577e92847e2151de5d62c142b842f95425fa`.
The candidate is admitted to one clean exact full integration using the
qualified run-4 QoR file and persistent output; campaign use is 2/5.

| Sprite campaign gate-2 artifact | SHA-256 |
|---|---|
| Routed DCP | `8fe288233413b91e309de09c94ae1acc10f963dd13f7e32dc8254e58e2d32b59` |
| Timing summary | `9f7c449b3abd35dea4c5ce33c7d4b2b936f5c07385e36fbc24e3969a80d226c0` |
| Route status | `a31c5dd3eac98a2c5ba66ff2e219715b8d4ba171047f01de378234c49d9c4cb6` |
| Utilization | `3aea6f4d186738c62c789d6ea4f598f5c8c7a3ec8679c00500dd860366446d40` |

Gate 3/5 stopped before synthesis because the Beast source mirror did not
contain the untracked `fpga/arty/audio` RTL directory. It produced no netlist,
timing result, bitstream, or capacity evidence. The invocation still counts.
The corrective action is one complete nondeleting sync of `fpga/arty`, followed
by explicit source/hash checks; gate 4 must use a fresh
persistent output directory rather than resume the partial project.

Gate 4/5 is the exact clean full production implementation after that source
sync. It retains the complete feature set and the qualified run-4 QoR file,
routes all 69,166 routable nets with zero errors, and passes hold at
`+0.010 ns`. Setup improves by 69 ps over the closed campaign to `-0.208 ns`
with `TNS=-6.098 ns` across 109 endpoints, so the fail-closed gate writes no
bitstream. Exact use is 36,664 LUTs (68.92%), 40,375 registers (37.95%), 105
BRAM tiles (75.00%), and 81 DSPs (36.82%).

The retained path census contains 27 blitter endpoints, 26 sprite endpoints,
and 56 other endpoints. The five worst endpoints are one blitter family from
`blend_divided_q` into `blend_result_argb_q`; the leader is `-0.208 ns` with
5.016 ns data delay, of which 3.128 ns is routing. Its source register is
marked `dont_touch` despite driving eight result loads, and its leading net
alone consumes 2.614 ns. Gate 5 is therefore limited to allowing synthesis to
replicate that register with the existing local fanout pattern, after the
blitter and complete graphics regressions pass. No new seed, strategy, feature
cut, or unrelated RTL change is authorized.

| Sprite campaign gate-4 artifact | SHA-256 |
|---|---|
| Routed DCP | `666f5fb6f73f49eb93c51744e3361f69007079ced9691a5b83897ea16af07d9f` |
| Timing summary | `8478358278fa7bfa068177dd51fa7ad908146f711f87126e4cf960493af9721d` |
| Route status | `eb07d3e4f2da481eb26868997ee2cc852f4eeff50cb10c471df4c3be2ca2971a` |
| Utilization | `594b838edd02cc2a9fdda086761fb6002eb47b106f2b6ef297cec395101e3e74` |
| Failing-path classification | `2dec548b9547488d36f6df79b3cca034111c47563f573a9b815b828eef66334e` |
| Detailed failing paths | `2f5ea98bdc38d0b318255d146737a3d69a6f8fd8674d145b1be00bdb961c69f9` |

Gate 5/5 permits synthesis to replicate `blend_divided_q` using the same
`keep,max_fanout=1` contract as the adjacent multiplier inputs. The complete
graphics regression passes before implementation. The exact full route
connects all 69,287 routable nets with zero errors and passes hold at
`+0.013 ns`; setup improves again to `-0.191 ns`, with `TNS=-6.549 ns`
across 123 endpoints. The fail-closed gate writes no bitstream and closes this
campaign. Exact use is 36,714 LUTs (69.01%), 40,412 registers (37.98%), 105
BRAM tiles (75.00%), and 81 DSPs (36.82%).

The corrected blitter family falls to seven endpoints and no longer leads.
The new census is 43 sprite and 80 non-sprite endpoints. The five worst paths
are synchronous reads from sprite working-line memories: the leader is
`palette_stage_quad_q` to `working_front3_i/read_data_reg[19]` at
`-0.191 ns`, with only 0.952 ns logic but 4.173 ns routing. The qualified QoR
file explicitly applies `RAM_STYLE distributed` to all eight 512x32 working
memories despite their RTL `ram_style="block"` contract; synthesis consequently
maps each to 55 RAM64M primitives. Exact sprite OOC, where that QoR override is
absent, maps these memories to block RAM and passes at `+0.147 ns`.

A new working-memory mapping campaign starts at 0/5. It retains the QoR file's
other measured transformations but changes the shared array name so its stale
literal `*/memory_reg` target cannot override these eight memories. Gate 1 is
a full synthesis-only mapping check: all eight must be block RAM, BRAM use must
remain below device capacity, and the rest of the qualified mapping must stay
intact. No full route is authorized before that check and regression pass.

Gate 1/5 passes the full synthesis mapping check on Beast with Vivado 2024.2
and the same qualified run-4 QoR file. All eight 320x32 working memories map to
one RAMB18E1 each, while the complete design uses 109 of 140 BRAM tiles
(77.86%). The complete graphics regression also passes. This proves the stale
literal QoR target was the cause of the distributed mapping and admits one
exact full production route as gate 2; no seed, strategy, or feature change is
authorized.

| Working-memory campaign gate-1 artifact | SHA-256 |
|---|---|
| Synthesized DCP | `6b2e6f4a5ce77cf2dfaf77ae64dcb8bb27142eda19b1f1381c547e40355c9e9e` |
| Synthesis utilization | `3a790a501f02dcb7e9008d4b35dbef740970280b0d07ae7aec076d737b31b246` |
| Hierarchical utilization | `7e9651a5d55566e30b82c4ffebd5170e24454d42595749bd90c79ed2ffddb063` |
| Control sets | `480a411234cbae8b2022a105ebec93f22fb8c15178b97ba6b50ede9be7b1a39a` |

Gate 2/5 is the exact full production route. It retains every feature, routes
all 67,466 routable nets with zero errors, and passes hold at `+0.050 ns`.
Setup rejects at `-0.171 ns` with 148 failing endpoints, so no bitstream is
written. Exact use is 34,303 LUTs (64.48%), 40,065 registers (37.66%), 109
BRAM tiles (77.86%), and 81 DSPs (36.82%). Only ten failures remain in sprite
logic and their worst slack is `-0.056 ns`, proving the working-memory repair.

The new leader is a glyph start-state decode into the clock enables of the
four effective-clip registers: 4.792 ns data delay is 82.7% routing, and the
shared decoded enable fans out to 201 loads. Gate 3 is limited to applying the
module's existing `extract_enable="no"` policy to those conditionally loaded
clip registers, then proving the glyph cone absent in regression and exact
render-command OOC. Another full route is not authorized until that proof.

| Working-memory campaign gate-2 artifact | SHA-256 |
|---|---|
| Routed DCP | `f9acaf19fa72c8979a636a21f70fad948efc3d53ba8425eb9764d3fa80ca4cee` |
| Timing summary | `469962d2e96ae922f49f9d4b9fe475b30b448076ee91d9181831eac1a68e6734` |
| Route status | `4bf3128e91275f24cde4d42caaf21c7dff82e798a3018e3394df22fd54773662` |
| Utilization | `e7a88b422c8085a35fcc7c270c9ac302ab0eb33e2d2417dd5058328c8dfa8955` |
| Methodology | `a319e4975a831ee5542f89ab8549f185fab1301180abacfcc3f174f212d4882e` |
| Failing-path classification | `f4d875a668bb45f1a3d2d5489a618848096bfc67e213eee7a28aa9d9a5d9930d` |
| Detailed failing paths | `e4206c0f35a31d075aa09586190b46f305e0c97fd6732b8b71bf4b1a0b87b6d5` |

Gate 3/5 passes the complete graphics regression and removes the targeted
effective-clip clock-enable family from the exact routed render-command OOC
top 50. All 19,289 OOC nets route with zero errors. The isolated block still
reports its known missing-context reset path at `-0.075 ns` from
`local_engine_reset` to a glyph DSP reset pin; it is diagnostic, not a full
integration rejection, and was not a gate-2 full-design leader. Gate 4 is one
exact clean full route with the unchanged strategy and QoR file.

| Working-memory campaign gate-3 artifact | SHA-256 |
|---|---|
| Routed OOC DCP | `ee8a628e03bac6848df9729624d0ab819e4e2780619a1ff9cf266c17a368bfdd` |
| OOC timing summary | `5ed34455984a60ff31862df4a223ffb74b33e5a17ab2e3749b3bb0758888d528` |
| OOC timing paths | `4c6fb6ddb0ed1f6ca0e606942f8a9b508cbd2e8074be81ae7930da92816dd1fc` |
| OOC utilization | `115e444187b402b8c4bb7edfa9335235056b6352a8f45a39b4f91cdca24d74ff` |
| OOC route status | `600fa59094d40391e403dad8adcb7120981047e9c5888ce0511617765552c751` |
| Glyph RTL | `e7f1573d757fb7fa7f00c9be7ed9b539cbc52bb275b6321ebe7a9ba8dc0966d8` |

Gate 4/5 is the next exact clean full route. It routes all 67,525 nets with
zero errors and passes hold at `+0.033 ns`, but setup rejects at `-0.150 ns`
across 112 endpoints; no bitstream is written. Exact use remains 109 BRAM
tiles and 81 DSPs. Only two sprite endpoints fail, at worst `-0.008 ns`.

The leading 64 endpoints are another glyph state-decode clock-enable family
covering descriptor dimensions, range operands, and pixel row/column operands.
The next seven are Copper AXI address decode into the indexed dispatch-table
write enable at `-0.080 ns`. Gate 5 is limited to disabling enable extraction
on those reported glyph registers and staging the already validated Copper
table write by one cycle. Targeted regressions are required before the exact
full route; no seed, strategy, QoR, or feature change is permitted.

| Working-memory campaign gate-4 artifact | SHA-256 |
|---|---|
| Routed DCP | `1c21c6253a4fb4a316ed0bb5527900570a2394caaf3862d6b60ed521ff9f5411` |
| Timing summary | `36ff2ac04eda07ecd99b696e539c41f94ee6907785e9d3adbb1fc3701bde7690` |
| Route status | `919ef2a138c508f47d55316e743ccad049c9eeffc979f1384ce188e45218bc29` |
| Utilization | `3003cea048d1c713bb5c4a2ec53501bf469c27035f689480a7b7473752fe2793` |
| Methodology | `72d2a6c5bc7fb9a08ae447759c8ada472bf226e01b448f93632a913fa9255fe0` |
| Failing-path classification | `61c46a0e3c47b11b7bee0ea523211a66e6d9d8742592118d8047ee645c70f84e` |
| Detailed failing paths | `4242e7c47c2060bb188fe021bc09f65923c6dbdf91bd922a26add691544d594c` |

Gate 5/5 passes the targeted Copper, glyph, and 45-command regressions and
routes all 67,539 nets with zero errors. Hold passes at `+0.005 ns`, but setup
regresses to `-0.245 ns` across 134 endpoints, so no bitstream is written and
the campaign is closed. Exact use is 34,390 LUTs (64.64%), 40,108 registers
(37.70%), 109 BRAM tiles (77.86%), and 81 DSPs (36.82%). The staged Copper
table-write path is absent and retained. Broad glyph enable suppression is
rejected: it reached `+0.018 ns` after placement physical optimization but
the final router lost 263 ps and rebuilt the controls as long D paths.

The exact routed leader is now the engine-response boundary through six logic
levels into the glyph FSM at `-0.245 ns`, followed by glyph state decode and
blitter/flood residuals. Reassessment finds that command, blitter, geometry,
and flood all use the established one-hot FSM policy; glyph alone used
sequential encoding across 55 states. A new glyph-FSM campaign starts at 0/5:
the rejected broad register attributes are removed, the proven clip attributes
and Copper staging remain, and gate 1 must prove one-hot glyph encoding and
remove the sequential-state family in exact render-command OOC before any full
route.

| Working-memory campaign gate-5 artifact | SHA-256 |
|---|---|
| Routed DCP | `6de60f9472c90bfe017f779c3ad08c3ed34810212bf9f507501bfaed2174af14` |
| Timing summary | `f2deaaabd52b58cb8816d5d1d60e8183ad806ac89bfa22768e6b682f06c5354d` |
| Route status | `7faf32fc8dc67804eeebbad5c03ea7ec73de7b886ff52ecc099e37956c08b9d1` |
| Utilization | `ba71d2781b6ccb813e9d0e59f51214931fbd6a84d20153ea052dec576a75d1b6` |
| Methodology | `3bad0f24f95744db65b61da2fef492b3234a52701c7c98a7d03ddccf2e03c862` |
| Failing-path classification | `a11429b0b33c2c89004d724aed6895642bf9347e72d107e949b77a24e4bfc0f7` |
| Detailed failing paths | `9429984419b8ace85fd86f2538d40c8d6d187b7c57341aff8516edd09ee3a37c` |

Glyph-FSM gate 1/5 is rejected before full integration. Targeted glyph and
45-command regressions pass and all 19,099 OOC nets route, but the synthesis
log does not infer a glyph FSM and therefore does not apply the requested
one-hot encoding. The OOC leader is an unrelated geometry path at `-0.333 ns`;
the prior sequential glyph family is absent, but an unenforced attribute is
not acceptable evidence. Gate 2 makes the same 55 states explicitly one-hot in
RTL and must repeat behavior, register-shape, and OOC path checks.

| Glyph-FSM campaign gate-1 artifact | SHA-256 |
|---|---|
| Routed OOC DCP | `e03a92f675d5b31665b95cd8844d13859f038d4922caecb865081d67c226e1b4` |
| OOC timing summary | `eac029bb18ca1fa0771846cb19bd8c1a3c7e086785f05d34b51d8a84b9a134af` |
| OOC timing paths | `e8cf6ebe1366375eca6d0fdb2cb436c15f71c2b3f59de39fceda098f3ea1e76c` |
| OOC utilization | `fb3da8055e0c93339362c112b0042364f41a39e1152fff057edcb5713d6156b3` |
| OOC route status | `74ac68279aa03d0bbd204de1352b3871e1d6cf33d3274c2c4848d2fa3cf260b6` |

Glyph-FSM gate 2/5 is rejected before full integration. Making all 55 states
explicit one-hot preserves the targeted glyph and 45-command regressions and
routes all 19,735 OOC nets, but setup collapses to `-1.841 ns`. The leader is
the new glyph `state[23]` fanout into the fault-detail clock enable; the broad
55-bit state vector therefore makes the control-routing problem materially
worse. Diagnostic OOC use is 10,912 LUTs, 12,193 registers, and 27 DSPs.

Gate 3 restores the compact six-bit state register and changes only state
ownership: the shared failure task no longer assigns the state register, and
each of its seven call sites transitions to `ST_FAIL` in the main sequential
process. The gate must show Vivado both inferring the glyph FSM and applying
one-hot encoding before another full integration is authorized.

| Glyph-FSM campaign gate-2 artifact | SHA-256 |
|---|---|
| Routed OOC DCP | `89045ac1f307d5f21b7ecc5e62c10653426a634cdd9afb0d830dcebe08019f16` |
| OOC timing summary | `258af6601dbee9c890e1acc5a867789c2085e97b31417135f16bc787d4e34ff1` |
| OOC timing paths | `131d537a91b311b476204ad89846b03f576f3b94a154a01b2dfad85660b0cf3a` |
| OOC utilization | `82b95fb07ac0291ac67e81fd38addd01d` |
| OOC route status | `6755ac7755fc792876d3c536e69630090980d16f98661c1fc1bfdac7e48ec21a` |

Glyph-FSM gate 3/5 passes the targeted glyph and 45-command regressions but is
rejected after synthesis. Moving the failure transition into the main
sequential process does not make Vivado infer the glyph FSM: the synthesis log
still lists only the validator, blitter, and command-processor state machines.
The run was stopped before placement completed, so it produced no routed timing
or capacity claim. The remaining state-register difference is the glyph-only
`extract_reset="no"` attribute; gate 4 removes only that extraction inhibitor
and repeats the OOC proof. A full integration remains unauthorized.

| Glyph-FSM campaign gate-3 artifact | SHA-256 |
|---|---|
| Interrupted-after-synthesis Vivado log | `e403cf0a4888d3cf9085cb59a123831411d890114b20e4830b3ace22411b4545` |
| Glyph RTL | `5ccd6630630e4efce28cc6fed3b9222a70138a93cc4d1e955641acf6b5d7034e` |

Glyph-FSM gate 4/5 also passes targeted behavior and is rejected after
synthesis. Removing `extract_reset="no"` does not change FSM recognition; the
glyph remains absent from Vivado's inferred/encoded FSM report. The run is
again stopped before routing and supplies no timing or capacity claim.

Comparison with the same synthesis log shows the compact flood FSM is likewise
not inferred, while the blitter and command processor are. Glyph and flood both
assign state in a priority abort branch outside `case (state)`; the inferred
machines keep transitions inside their state case. Gate 5 preserves the glyph
abort priority and outputs but nests that guard under the state case. This is
the campaign's final diagnostic; no seed, strategy, or full route is permitted.

| Glyph-FSM campaign gate-4 artifact | SHA-256 |
|---|---|
| Interrupted-after-synthesis Vivado log | `c0efc899ca811eb92274934ac564b0c8e0a618bbde16e07cfccc8e38845469cc` |
| Glyph RTL | `65833505b86c3279ddaf33eef4661530a1cf9e7e287a535aeef38b1868dc146d` |

Glyph-FSM gate 5/5 passes targeted behavior but again fails the synthesis
recognition gate; nesting the abort guard under `case (state)` does not make
Vivado infer the glyph machine. The run is stopped before route, writes no
bitstream, and closes the campaign. The FSM experiments are rejected and the
compact sequential state, original failure/abort ownership, and
`extract_reset="no"` contract are restored.

The retained gate-5 full checkpoint already identifies a narrower boundary:
`engine_response_valid_q` fans out to 25 loads and spends 0.862 ns reaching the
glyph before six levels of next-state decode. A new engine-response locality
campaign starts at 0/5. Gate 1 applies the command processor's existing
`max_fanout` replication policy to that single registered valid bit, then must
pass targeted behavior and exact render-command OOC while proving a local
replica feeds glyph. No full route is authorized before that evidence.

| Glyph-FSM campaign gate-5 artifact | SHA-256 |
|---|---|
| Interrupted-after-synthesis Vivado log | `681eedfd924d3c70f19d33d2e0d6ad7f3cafc5f3c12f13e49551d586ac218b3d` |
| Glyph RTL | `b5089cad8b18eb316120688235364c66238c37cd74a8b83a2c744eeedf374a41` |

Engine-response gate 1/5 passes the complete graphics regression, including
all 45 render commands, exhaustive sprite dimensions, abort/error paths,
Copper, control, and pipeline integration. Exact render-command OOC routes all
19,215 nets with zero errors and `+0.028 ns` hold. Its overall setup result is
`-0.320 ns` on the documented missing-context command fault-detail enable, so
that isolated WNS is not a full-design release claim.

The measured locality contract passes: Vivado creates seven response-valid
register instances with fanout at most five, and glyph loads are fed from local
replicas rather than the former 25-load source. The prior response-valid to
glyph-state family is absent from the routed top 50. Gate 2 is therefore one
clean exact full production route using the unchanged 200 MHz clock,
`Performance_Explore`, and qualified run-4 QoR file. No seed or unrelated RTL
change is authorized.

| Engine-response campaign gate-1 artifact | SHA-256 |
|---|---|
| Routed OOC DCP | `1dd970459fce24953851a726359711efe6b22284d01b7a4327298f00da0f08bb` |
| OOC timing summary | `949d8e73948a04fd0f0dec6a651245b35ffc6f13b5c7a962cd7a7bf0d299315d` |
| OOC timing paths | `b6ac424097ffa9365bb8a0303e0c3b28210bef8fba6f41b56dbf747be6813233` |
| OOC utilization | `05f4ff3b83b7e59e4be6962b75d8f5f8b15428dc404820fbb66e74f6279fd6ad` |
| OOC route status | `c8d547a0acc2c4083e962f0c91ede465d1f81338a5e83aa90b0c586334f2ba16` |
| Command RTL | `74afde164a69ebce943df6a59bc94c7098ac98a91975d1f127d0307333b2a3d5` |
| Glyph RTL | `ede6f229decdb7b02a5ba72984938b5ba141a66beed45884c2ca0fe9d923b71d` |

Engine-response gate 2/5 is the exact full production route. It retains every
feature, routes all 67,615 nets with zero errors, and passes hold at
`+0.041 ns`. Setup improves by 60 ps over the prior full checkpoint to
`-0.185 ns` across 102 endpoints, so the fail-closed gate writes no bitstream.
Exact use is 34,294 LUTs (64.46%), 40,168 registers (37.75%), 109 BRAM tiles
(77.86%), and 81 DSPs (36.82%). The response-valid/glyph-state family is gone,
so the bounded replication is retained.

The new leader is eight bits of `shadow_tile1_control`: registered AXI write
address decode crosses four logic levels and 3.598 ns of routing into register
clock enables. The CE setup arc alone costs 0.407 ns. Gate 3 applies
`extract_enable="no"` only to that measured 32-bit register, then requires the
graphics-control regression and exact control OOC to prove the CE family gone.
No full route is authorized before that proof.

| Engine-response campaign gate-2 artifact | SHA-256 |
|---|---|
| Routed DCP | `09dc66bc77dcac4e82082a08412e840898fa714cf316561ae6bb7e3273598ccf` |
| Timing summary | `6e102a2afad116f2f889b1a5067b84af39d7e18ca4f54022b8a63b1ebaf1c451` |
| Route status | `b851a7da5c37509d6e6b9e6299864d3f99746849e9f57f74a89b372a62b6c609` |
| Utilization | `4c8e93df4c3864ca4b74c7e89f2f0a26c3e733cd43fb99be504fe0651c373921` |
| Methodology | `6a58bc85d5937d1f9f4346cb48170736352dec2817f0c71d93a6cd3cdf4f862c` |
| Failing-path classification | `a276250ee895ed83ceb39cf97433d4c1c5391d30fe970c346b5dc913d015512b` |
| Detailed failing paths | `32a54f84c3ed17c3799e3a827d68f4c5b48c57e39267716e8a004e6baef53c29` |

Engine-response gate 3/5 passes the graphics-control regression and exact
control OOC at `+0.331 ns` setup. All 3,462 routable nets complete with zero
errors. Netlist inspection proves every bit of `shadow_tile1_control` has CE
tied to VCC, so the measured address-decode-to-CE family is structurally gone.
Diagnostic OOC use is 1,544 LUTs, 3,418 registers, zero BRAM tiles, and three
DSPs. Gate 4 is one clean exact full production route with no other change.

| Engine-response campaign gate-3 artifact | SHA-256 |
|---|---|
| Routed control OOC DCP | `2898156c1dd12b3a1553e5ec743a70d22ea25bd80d22b67e082f2e9f016f72e8` |
| OOC timing summary | `4a94a09efafe7dac35241bd42fd12d02e7a69485826ef9fe2a4abe935263ad12` |
| OOC utilization | `4d7374e5dc3fb8658b75c5d1dd71d7d15cca522c92c43222c4efdcaf44c0e33a` |
| OOC methodology | `3a8c8edec28ca7ba32a3bc9fc94064eebc512e587ef1f74a514916f6171822a4` |
| OOC route status | `2f1360f4ea3ee7d2f092cf58f1d7c107a717b32f8791a42ea5f35edc56c1741e` |
| Graphics-control RTL | `3a5c80c17dd079da1c539c253722e4901505c86a121b3b489cd1be0eaae5c6e5` |

Engine-response gate 4/5 routes all 67,641 nets with zero errors and passes
hold at `+0.050 ns`, but setup regresses to WNS/TNS `-0.265/-15.767 ns`
across 241 endpoints. Exact use is 34,378 LUTs (64.62%), 40,172 registers
(37.76%), 109 BRAM tiles (77.86%), and 81 DSPs (36.82%). The original
`shadow_tile1_control` CE family is absent, but forcing data-mux enables across
the whole register perturbs packing and exposes 40 blitter product-to-divide
paths; the leader is `-0.265 ns`. The broad attribute is rejected and no
bitstream is written.

Gate 5 restores the gate-2 mapping and targets only the eight architecturally
meaningful transparent-index bits that formed every gate-2 leader. Those bits
are split from the other 24 stored control bits and alone receive the proven
no-enable mapping. Graphics-control regression and exact control OOC must prove
read/write equivalence and removal of only that CE family before the final full
route. No seed, strategy, QoR, clock, or feature change is authorized.

| Engine-response campaign gate-4 artifact | SHA-256 |
|---|---|
| Routed DCP | `4f56b60485d384fd7aabf862e9a72e2dc0f6fa1d4a252407a01c509f40f79164` |
| Timing summary | `4402f21f55b54afe03830f6a381ec22f5dcdaf8841469a6d6509334af80e8ba6` |
| Route status | `cad9ec71367e826b6c753c26ad6abcfbe8c48513ec9b0bacf0151eb7158bc4f1` |
| Utilization | `92eb0c01966a90ffced8db8176f66b0a729f9d4e05d40f0b92ecdf97bb4aa40e` |
| Methodology | `e71f14e076bc714236ea61ab4adce2197610ffb8dcfd6477625a59e41aba0b23` |
| Failing-path classification | `4430698cd512700cdb6dbad4a5b8040e5823f80ad0709a81f84d5f2efbbac30c` |
| Detailed failing paths | `b2043ce08b3c88f4bbdce2832211ff92c8f89912566f283a1633785fd63649e4` |

Engine-response gate 5/5 passes the complete graphics regression and exact
control OOC structural gate. The isolated control routes all 3,477 nets with
zero errors and positive setup/hold; setup is `+0.018 ns`. Read/write behavior
is unchanged. Netlist inspection proves all eight transparent-index registers
have CE tied to VCC while the other 24 tile-control registers retain their
shared conditional CE. OOC use is 1,573 LUTs, 3,418 registers, zero BRAM, and
three DSPs. This authorizes the campaign's one final exact full production
route; no further campaign edit, seed, strategy, QoR, clock, or feature change
is allowed.

| Engine-response campaign gate-5 OOC artifact | SHA-256 |
|---|---|
| Routed control DCP | `9157d99ceeba3b8386c87155748a290f7fcd82703cf152a137531bcd6ed3db23` |
| OOC timing summary | `2ecafdca49601f234ef61ee295ddd2b3c42cb8b1923d0d516cedd5571e23495a` |
| OOC utilization | `018a8aea28c8e9306421a46556a13ea596c20e26fb5747fbd0650df527e51324` |
| OOC methodology | `a69f5829c9833fec3f27524b0bcbee3be4cc7045aa0c4f08ba8a378133868a13` |
| OOC route status | `66d10e8c9ace19e00c7479ec3824ef4b7ec33573983429022aaaaa7fb18f5e36` |
| CE mapping | `f59b75ca9633d06c73bcca27bad542e6e43649352cd4e3154ab4bf9f28c9199d` |
| Graphics-control RTL | `3f01359d71797fe0d0b15b0dfd15a742adeb294997d5094e7128c8a60647597e` |

The gate-5 exact full route connects all 67,228 nets with zero errors and
passes hold at `+0.012 ns`, but setup rejects at WNS `-0.294 ns` across 149
endpoints. Exact use is 34,204 LUTs (64.29%), 40,148 registers (37.73%), 109
BRAM tiles (77.86%), and 81 DSPs (36.82%). No bitstream is written. The
targeted CE family is absent, but the required `dont_touch` boundary prevents
global placement from recovering; the new leader is Copper execute-word decode
into `exec_state` at `-0.294 ns`, followed by glyph sample classification at
`-0.252 ns`. The split-register/no-enable experiment is rejected and fully
removed. The bounded response-valid replication remains retained.

This closes the engine-response campaign at 5/5. The authoritative retained
full checkpoint is gate 2 at `-0.185 ns`. A new MMIO-predecode campaign starts
at 0/5 from that source: gate 1 registers the existing tile-1-control address
comparison alongside `write_execute_q`, then uses that one-bit staged select to
drive the unchanged 32-bit write. This removes the measured four-level AXI
address decode from the register CE without forcing data muxes. Behavior and
exact control OOC locality are required before any full route.

| Engine-response campaign gate-5 full artifact | SHA-256 |
|---|---|
| Routed DCP | `c13cd292faf797ef182f0b803a8f6dec89643c9c349b96a6c2616cf058b88584` |
| Timing summary | `ffc73dd05f0769c93b458adc12289261027e7d09b922df880e4779444e2823ad` |
| Route status | `b8f00198bb71e175d9b492ea463466d0fbf7b59b21e05789ce1d63edb1f67730` |
| Utilization | `522454cfa38fb9cbca7685fd5a14d7e5f5d5039ee40e08d396bf66ddd270414e` |
| Methodology | `5b25cc2044c9c884edee50d99b6f983e9a3832bd821263e93fa072728bf7d67a` |
| Failing-path classification | `0d03a18b1530d14b4d69c03b8d9c6f6891ae985d5f5bdb2502bcba57e344e8e6` |
| Detailed failing paths | `1bb6a2d236c22563dd186dcebb9a60fd33ed5f09231b6cc665767cf94428f528` |

MMIO-predecode gate 1/5 passes graphics-control behavior and exact control OOC.
All 3,474 routable nets complete without error and setup is `+0.136 ns`.
Vivado groups the 32 tile-control CEs into four byte-local drivers; the staged
one-bit select has fanout five, and there are zero timing paths from
`awaddr_q` to any target CE. OOC use falls to 1,517 LUTs with 3,419 registers,
zero BRAM, and three DSPs. The predecode is retained. The complete graphics
regression must pass before gate 2 performs one exact full production route.

| MMIO-predecode campaign gate-1 artifact | SHA-256 |
|---|---|
| Routed control DCP | `a6331708f32fe2d8f5be77cc79f4e26dd9d1c13950570898e9c2ef8e681f1ea1` |
| OOC timing summary | `234821a2f16fc47e19fd6fc1e7c93f917830038dfef002f30cb4d4ceb151d13d` |
| OOC utilization | `d0a413384bb1bf12d4cbbfef5802e8610ef3864e6eac9447065389f303e634df` |
| OOC methodology | `cd68ec0bb2cfff3c62b265485fea13a209d98f89668dc4a9e80f2c0266d56631` |
| OOC route status | `9042fe14807e9d8552c097528008ac2e018abd17c3eb46ac9fc3cb3401f9f92f` |
| CE mapping | `bc2ab10f784a4942054b2b8645e9d281cc419157c788354fd1ab49e77f22a597` |
| Address-to-target path proof | `f4a0b0eff39e3ef973c30055ce914532200cbb49b1cdeb8de2a4cb5e1946df64` |
| Graphics-control RTL | `d8f12e0360fb0ad34a4ecc85bc2ccb1614176da5e93caa3fefe945d9495bae48` |

MMIO-predecode gate 2/5 retains the complete feature set and routes all 67,080
nets with zero errors. Hold passes at `+0.017 ns`, while setup rejects at
WNS/TNS `-0.111/-0.772 ns` across 23 endpoints, so the fail-closed build writes
no bitstream. Exact use is 34,141 LUTs (64.17%), 40,073 registers (37.66%),
109 BRAM tiles (77.86%), and 81 DSPs (36.82%). The former AXI-address-to-tile-
control CE family is absent and the checkpoint improves 74 ps over the retained
engine-response gate-2 authority, so the predecode is retained.

The new leader is a five-LUT, 4.992 ns path from the 16-bit command opcode into
geometry-layout selection. The command has already been classified as one of
five consecutive geometry opcodes, so gate 3 replaces only those repeated
full-width comparisons with the opcode's three-bit geometry subcode. Exact
render-command OOC and the complete graphics regression are required before
another full route.

| MMIO-predecode campaign gate-2 artifact | SHA-256 |
|---|---|
| Routed DCP | `e676e65f5afdd5ea41aa87651fe83b566f710fbd1c710107666e7e1b3dd6a032` |
| Timing summary | `8ac334fb6b810b6961485869683d011ef87b5279677b64ea271c25b2a505b2e3` |
| Route status | `903111f0b1facbb9e45e167c31db9066c93ab0048d09fa85a2e1024c63694381` |
| Utilization | `5ff189655d3d01c1e93ebf565d2195e4a827e6c778b4bafe15b44858bfb18d0c` |
| Methodology | `5e05c46ece896506d5bfbb4410d1f4f168f544a03f402c97052fb8691d5e5531` |
| Failing-path classification | `7cb5e372da8ccf9e05a6040365569b51ba342008f8faec6fac2fc388f7eb319f` |
| Detailed failing paths | `f4e70d27f9809be378de7f82a1ffc48688fc777132e5207a238eb179c97545c7` |

MMIO-predecode gate 3/5 passes the complete graphics regression, including all
45 render commands and exhaustive sprite dimensions. Exact render-command OOC
routes all 19,193 nets with zero errors. Its overall `-0.239 ns` result is the
known isolated blitter-address residual; the measured geometry endpoint now
has only two logic levels and `+1.821 ns` slack. Only opcode bits 0--2 reach it,
so the former five-level full-width opcode family is structurally gone.

Gate 4 is therefore one clean exact full production route using the unchanged
200 MHz clock, `Performance_Explore`, and qualified run-4 QoR file. No seed,
strategy, clock, feature, or unrelated RTL change is authorized.

| MMIO-predecode campaign gate-3 artifact | SHA-256 |
|---|---|
| Routed render-command OOC DCP | `9e617591801b7c724f2a5eb49aa1b9444371356ceb69cfae1aaacd2c70ba00a2` |
| OOC timing summary | `2570fd97f0f38e9f2375a23cb059489c65c7e35ce87cd8bfc7f7e6a9d622eb54` |
| OOC timing paths | `276e1ec311c78afb28a4079047e9bf22e7fc745838ee46a2c2bc6333459248cb` |
| OOC utilization | `7a71d11e4abc84c5b5203759494af673ce940df4c5e64056fa45cfe750c0505c` |
| OOC methodology | `b865171c674b454688f7ec802f0eb67c6dbfa7335ad7f0df28621ee57da32448` |
| OOC route status | `371016487cf9f4722f6588bdc1a6fc9824d885d652959a07548f19cf8e5e5926` |
| Geometry-path proof | `490c94a0d785c8e2681ddba0038735e2b52bbf7bdee928066e39fbad0c7f0a71` |
| Command RTL | `59a1dd73cbcece01c2a016617203673787f48f001436b1d61220845b2a3011ee` |

MMIO-predecode gate 4/5 routes all 67,162 production nets with zero errors and
passes hold at `+0.013 ns`. Setup improves only 10 ps to WNS/TNS
`-0.101/-0.604 ns` across 15 endpoints, so no bitstream is written. Exact use
is 34,179 LUTs (64.25%), 40,159 registers (37.74%), 109 BRAM tiles (77.86%),
and 81 DSPs (36.82%). The geometry path is absent from the failing set.

The new leader is an eight-level, 5.055 ns Copper validation-start path. It
unnecessarily computes the range end and consumes that same unregistered sum
to set `validate_range_ok_q`, even though the existing `VALID_RANGE` state runs
on the next clock. Gate 5 registers only the range inputs/end at start and lets
that existing state decide from the registered values. Copper regression and
exact OOC cone proof precede the campaign's final full route.

| MMIO-predecode campaign gate-4 artifact | SHA-256 |
|---|---|
| Routed DCP | `959ed4f70a3c2b52955ce776e88416ee19aba93f52393089e303b6b6a69a9cdc` |
| Timing summary | `c471c5919b7562c12acf1014c43eafb82aa02355919a99c66bea1d96d8a23be5` |
| Route status | `385d268fa65cf88f710cee244ce43ae83701dfc7b1956a856cab7eeb48dab25b` |
| Utilization | `3c811287014e2b3e8088cb7b83a633038fe98d864e8ef590b9c5475f1b850a9f` |
| Methodology | `dffcc623a0bb8e4c106faf43c1854bc8b060fc667ae719551fbc177900b2dfa4` |
| Failing-path classification | `adb5cdc2b9b4dbc3514ffeb9882009fb8e781eb398c2798a35600feeea794329` |
| Detailed failing paths | `04328111b56c022222bf5e043d126523f62d7e149db9df44d04eb7e6f6d9cf94` |

MMIO-predecode gate 5/5 passes the complete graphics regression and exact
Copper OOC. Copper retains all 16 RAMB36s, routes at `+0.762 ns` setup, and
uses 590 LUTs plus 662 registers. Structural inspection finds zero paths from
the 13-bit validation-count input to `validate_range_ok_q`; that endpoint's
remaining instruction-range path has four levels and `+1.471 ns` slack.

The campaign's final exact full production route is authorized with no further
source, seed, strategy, QoR, clock, or feature change. It alone can authorize
bitstream generation and hardware qualification.

| MMIO-predecode campaign gate-5 OOC artifact | SHA-256 |
|---|---|
| Routed Copper DCP | `397c277e5de63de07e882a7c3f736294ec31925f524ca69e9d472403edc2b4d2` |
| OOC timing summary | `ade0b531fe23cca3c45816c48806de5d78ccdf61b779ef578d2c5726a54dc7d4` |
| OOC utilization | `06900e1420e45deec28bd35ffdca61985be3bdb9dc30fd51c0c321de516de053` |
| OOC methodology | `02bad24f5a136508eecc47b84f0d0aa4f7ad87e4c3144868ad5c77dde357cca3` |
| OOC route status | `2f57c21a77969aab89d8874beb0d7060b1681268952bf7c3464beedc99d43912` |
| Range-path proof | `aa5ecc0ea80cd3bae3b28b9d255c7a1f7ee2f78a958dcd6d25b21ba77bd51351` |
| Copper RTL | `46d8a86e050d28b63773de75eddad50d169e52c562f20c6df8ff41df47537282` |

The MMIO-predecode gate-5 exact full route connects all 67,130 production nets
with zero errors and passes hold at `+0.007 ns`, but setup rejects at WNS/TNS
`-0.230/-3.809 ns` across 40 endpoints. Exact use is 34,152 LUTs (64.20%),
40,177 registers (37.76%), 109 BRAM tiles (77.86%), and 81 DSPs (36.82%).
The fail-closed build writes no bitstream. The Copper validation family is
absent; the new leader is a four-level, 4.849 ns response-error control path
into glyph fault-detail clock enables at `-0.230 ns`, followed by scheduler
slot-tag clock enables at `-0.131 ns` and completion-ring descriptor validation
at `-0.123 ns`.

This closes the MMIO-predecode campaign at 5/5. Its component-local changes
remain retained because every targeted family is structurally removed and
behavior is proved, while gate 4 remains the best exact full timing checkpoint
at `-0.101 ns`. The next campaign starts only from the measured response-error
to glyph fault-detail enable family; exact glyph/render-command OOC proof must
precede another full production route. No seed, strategy, QoR, clock, or
feature change is authorized.

| MMIO-predecode campaign gate-5 full artifact | SHA-256 |
|---|---|
| Routed DCP | `eb8a7d16209bceccf1ef7bc1029a45691541e10f38368fb742148b1551fcee91` |
| Timing summary | `09705827bbf90c463ce3e4bd30274d4f31598b59202b8cf2fa4c6fdfae3b2a66` |
| Route status | `3b01fb4e4632d98abf16d5dd663e222bcbff64e58cab9cffde054b64a3d0ee84` |
| Utilization | `34d07c4bc356b148e844f586404bb2484444c1dea6d3734ec99b16fb096acc69` |
| Methodology | `c37e9e213656bd30def5419b73a071a0406b4d10ab468155dd752b13492fc46e` |
| Failing-path classification | `ec32c4575bdad55bc3d2819823e44b0f58a2c9a78582d0157f33e87fd92cffd6` |
| Detailed failing paths | `e0f215c576f68f06625b64d4f54f84c2404f635e5055999449bdbe62e6b77a85` |

### Full-feature 166.667 MHz hardware baseline (2026-08-14)

The exact current production feature set routes at the existing qualified
PS7 FCLK1 point of 166,666,672 Hz. All 66,837 routable nets complete with zero
errors; setup passes at `+0.144 ns` and hold at `+0.019 ns`. Exact use is
33,409 LUTs (62.80%), 40,069 registers (37.66%), 109 BRAM tiles (77.86%), and
81 DSPs (36.82%). The block-design report proves requested and generated FCLK1
are both exactly 166,666,672 Hz. Vivado writes the bitstream successfully and
the XSA contains that exact bitstream.

The original wrapper then exited after artifact creation because it queried
timing with no routed design open. The redundant post-bitstream query is
removed: the same fail-closed setup/hold values captured immediately before
`write_bitstream` remain authoritative, and artifact generation cannot mutate
the routed checkpoint. This build is eligible for FSBL/device-tree generation
and hardware qualification; it does not replace the separate 200 MHz closure
target.

| 166.667 MHz baseline artifact | SHA-256 |
|---|---|
| Bitstream | `c545cb3bb25419b77697003e5549d8fdc306e29791fc49193d36e5aeafe9f9ce` |
| XSA | `fb8c554a5214b4b085e57873f877e9f0e8d99c298dab0ecdae918f575dbb6774` |
| Routed DCP | `4a9582c04e0397153c9aa169ead8afd774261ee068dd8e645286dfa22bd355d7` |
| Timing summary | `1807140e577f8ee2565756a168bff934de5f85cd170e330e284a15e796d3408c` |
| Route status | `d85932cb5ce0f23d4175a7a57727f6b58b0318822a8f716c87468bfac9540493` |
| Utilization | `c085bcef45fa88ebbf0a14662759a7fba14bdeb0bb56f4cda4e70511eb862bcc` |
| Methodology | `b3c1e58bc19ac785d4a31bb5800a18dd03e086b5e135adcf360287632ace7486` |
| Block-design clock report | `81013c0d7fea1624d6f7009d960fe4232d5daeecd0851e34e66cc2b56755e70a` |
| Corrected build script | `b1231c6cf74333ceced178df6fd56acb0e0e644757ed2304fe2b38dea8382cf0` |

| Sprite campaign gate-5 artifact | SHA-256 |
|---|---|
| Routed DCP | `bba90d18885dabf7d4a5a6b3be78253178a8e2234fcd5e6019b8a43da26d9203` |
| Timing summary | `b6a8b80e63a61ede7d79fce9f1cb0f3da1c506ae02b6c4eaf9f3f859c16a055c` |
| Route status | `3c8f6617b349a988b851071fc338551f3a85d7f90bd2a0222e04405a6ef7d5f4` |
| Utilization | `21bcabb8f4916381f048ffacdffff78810a40532eb519bff101247e2a7c4d18d` |
| Methodology | `7e283372730db19bcd58b0fa129e70d749724eab2c37b48e86c10936444e4842` |
| Failing-path classification | `b75b8146034e4e1e083f523927307893c33be327a9e924753fa0ab305e5bc333` |
| Detailed failing paths | `d818aca63697eb99f15f20738b62b308192550fffaab3519373cc751ada5374e` |

### Sixteen-sprites-per-scanline 200 MHz campaign (2026-08-14)

The retained sprite policy keeps all 64 global descriptors but admits at most
16 fully visible sprite spans per scanline. Admission is topmost-first, so
lower-priority spans are dropped deterministically and reported in the existing
overflow status/bitmap. A separate 2,048-destination-pixel budget is required
because scaling can expand one 128-pixel source span to 1,024 destination
pixels. The NDK publishes both limits; no sprite size, scaling, collision, or
descriptor capability was removed.

The complete graphics regression passes. Focused evidence includes exhaustive
1--128 pixel dimensions, a 17-sprite admission-limit test, and a 64-by-128
case that admits IDs 48--63, emits 2,048 pixels, drops 6,144 pixels, and reports
overflow bitmap `0x0000ffffffffffff`. The 16-by-128 case completes in 1,562
renderer clocks, versus 3,865 clocks for the former unrestricted 64-sprite
line. A 16-way collision case completes in 1,584 clocks, below the retained
2,000-clock regression budget.

Two measured local timing fixes are also retained. Glyph `fault_detail` and
blitter `source_pixel_address_q` no longer use conditional clock enables. The
former removes its measured glyph-enable leader; the latter passes exact
blitter OOC timing at `+0.204 ns`. Both preserve behavior under the complete
graphics and focused blitter regressions.

Five exact full production attempts used the complete release ROM, nonzero
build identity, 200 MHz renderer constraint, and the full feature set:

| Attempt | Routed nets | Setup | Hold | Disposition |
|---|---:|---:|---:|---|
| 1 | 66,415 | `-0.211 ns` | `+0.020 ns` | Rejected; exact blitter `/255` rounding led |
| 2 | 66,613 | `-0.293 ns` | `+0.044 ns` | Rejected; prior blitter leader removed |
| 3 | 66,742 | `-0.427 ns` | `+0.011 ns` | Rejected; completion status to fault-detail CE led |
| 4 | 66,407 | `-0.141 ns` | `+0.010 ns` | Rejected; glyph and blitter CE families led |
| 5 | 66,393 | `-0.152 ns` | `+0.050 ns` | Rejected; zero route errors, no bitstream |

Attempt 5 uses 32,289 LUTs (60.69%), 38,942 registers (36.60%), 129.5
BRAM36-equivalent tiles (92.50%), and 81 DSPs (36.82%). Its remaining setup
population is distributed: glyph state to blend-DSP enable is `-0.152 ns`,
surface-validation result is `-0.145 ns`, sprite-preparation state enable is
`-0.138 ns`, glyph range state is `-0.135 ns`, and AXI/Copper dispatch is
`-0.133 ns`. The original glyph fault-detail and blitter source-address CE
leaders are absent. At this route's 5.152 ns critical delay, the observed
equivalent frequency is about 194.1 MHz; that is diagnostic only, not evidence
that any lower target closes.

The campaign is closed at 5/5. The scanline cap materially reduces sprite work
and is retained, but it does not produce a timing-clean 200 MHz bitstream.
The active blocker is now distributed full-design placement/timing rather than
sprite scanline throughput. Nothing from this campaign was flashed; the board
continues to run the qualified 166,666,672 Hz release with build identity
`0x18EBE2E1`.

| Sixteen-sprite campaign artifact | SHA-256 |
|---|---|
| Attempt-5 routed DCP | `bfb0f1600504dc2e136d70898ca575f1c0fea117977c1122c882e0e7ccc9b41e` |
| Attempt-5 timing summary | `4688df9df0288f6ffd8974c1354a9d9073c81d066e611cf1474c84604a23817f` |
| Attempt-5 utilization | `b18ca5a6b94d057f74af08425427e36714ba96cfceac7483ba4278d87058a38e` |
| Attempt-5 route status | `ff01f3899473705aa4872f836e1f87005dc56110c7c29b1dac582369b047627e` |
| Render-command OOC DCP | `2eccfd493909ae35ed93154f4a9c5ef595dadff1485acb39c26c8edbacb02365` |
| Blitter OOC DCP | `fb3cdab0c9ac195f5628e082a6ba70b367159e2eba10f611b3a3e9007cdb628e` |

### Full-feature 187.5 MHz release candidate (2026-08-14)

The exact production design now closes at an actual PS7 FCLK1 rate of
187,500,000 Hz without removing a feature. The successful implementation used
a 200 MHz placement/route target, then restored the exact generated 187.5 MHz
clock before the fail-closed release reports and bitstream gate. This is an
implementation margin technique only: the block design, FSBL, device tree, and
runtime clock remain exactly 187.5 MHz.

Five bounded attempts were consumed:

| Attempt | Result | Disposition |
|---|---:|---|
| 1 | `-0.116/+0.025 ns` | Rejected; `Performance_Explore` |
| 2 | `-0.084/+0.025 ns` | Rejected; post-route physical optimization |
| 3 | placement rejected | Stopped before route; incremental 200 MHz checkpoint harmed placement |
| 4 | constraint rejected | Stopped before route; conditional XDC is unsupported and was removed |
| 5 | `+0.173/+0.009 ns` | Accepted at the exact 187.5 MHz release gate |

Attempt 5 routes all 66,520 routable nets with zero errors. Pulse-width slack
is `+0.538 ns`. Exact use is 31,957 LUTs (60.07%), 39,070 registers (36.72%),
12,263 slices (92.20%), 129.5 BRAM36-equivalent tiles (92.50%), and 81 DSPs
(36.82%). The block-design report proves FCLK1 is 187,500,000 Hz; the
implementation-margin hook is 200,000,000 Hz and is not present in the shipped
PS clock configuration.

The release package is complete and host-tested. The exact XSA generated an
FSBL with FCLK1 mask `0x00200400`, the device tree assigns 187,500,000 Hz, the
FIT payload was extracted and compared byte-for-byte, and the Linux graphics
tools pass `make all analyze test-host`.

Hash-verified atomic deployment replaced the qualified 166.667 MHz image while
preserving rollback copies. Three consecutive boots verify the exact BOOT/FIT
hashes, FPGA-manager `operating`, `0x05f5e100,0x0b2d05e0` assigned clock rates,
read-only `/`, writable `/data`, 378,380 KiB Linux memory over System RAM
`0x00000000..0x17ffffff`, 128 MiB preallocated Astra RAM, MC68030/PMMU and
full-range SDRAM POST, filesystem round-trip, terminal display ready, and
initial-image stage 8. Build identity remains nonzero `0x18EBE2E1`; graphics
generation 1 exposes capabilities `0x000003ff` and reads back the exact
1,843,200-byte splash with CRC32 `8db14556`.

Ten consecutive renderer, Copper, sprite, and HDMI-audio certifications pass,
followed by one complete sweep after the third boot. The renderer checks all
29 commands, 1,196,651 pixels, 64 virtual sprites, geometry, five AFNT formats,
and bounded flood overflow. Copper verifies both banks, IRQ `0xcafe`, dispatch,
and invalid-target containment. Sprite hardware verifies all 64 descriptors,
1--128-pixel shapes, and the retained 16-span/2,048-pixel scanline policy with
deterministic admitted, dropped, and overflow accounting. Audio delivers ten
48,064-frame 48 kHz runs without underrun or overflow.

Hardware qualification exposed two stale software checks, not RTL defects.
The sprite certifier now consumes the shared NDK scanline limits instead of
asserting the removed unlimited policy. The audio certifier now stops within
its 64-frame silence tail rather than polling a 32-frame threshold at a 48-
frame interval. Both failures were reproduced before their focused fixes;
analysis, host tests, and repeated physical tests pass afterward. No bitstream,
FSBL, device-tree, or FIT rebuild was required. Beast exposes no HDMI capture
device, so retained evidence is register/readback and certification output
rather than a new captured frame.

| 187.5 MHz candidate artifact | SHA-256 |
|---|---|
| Bitstream | `573dee97ad12b49686d1bb33576028271ac7402ad097cdec1fecf08c467c2a7c` |
| XSA | `187df6f04d08e73275725dc44f3b38fb6c28f1ab5633e312eb9f276382ed5363` |
| Routed DCP | `a23858ba14d4d64ef69a1689a971fdfe0e9dd606418d9d40ddc156f3d69d23a6` |
| Timing summary | `1e9a5dcc21dd366413586f29d2d38f057406e75dfb01d569df1ed69acc442b68` |
| Route status | `fe12c4a84c019b833df64f79798e3a7306f88e19f4de6b2ecc1716f13f62d49b` |
| Utilization | `1bbd5f686ef1d52d46fbfa1338ea159dc0e940b83d2a997686be9db5a7d18b33` |
| Block-design clock report | `bdd200c66f5724b03caa3cbbc3cadfffb7b41801b6d1295e6396ce7e93f38a0d` |
| FSBL ELF | `9684172a884c4d4ba5539a521ec267c3f0c755a5c595b0657844e0272459d167` |
| Device tree | `300eda3fff27734866fe6abb3c42f2e57d261e56f80ce79301089a6011e1d46b` |
| BOOT.BIN | `3010d5f5fe5b19c9ef094823e8fbedd0d87fb8105c9c848c8ff65f6cd64be555` |
| FIT image | `cbf7ce8615c49c6f9d959b48d57326a8de3d3ec4b721a4ca7c5b781a47123679` |
| Qualified sprite certifier | `329e7cf4ac96b4edf3aa860b2a0981ea610d0f8e521d822cb639cdd2274daa92` |
| Qualified audio certifier | `3f788469c59e3e115c0131e1f40337b419db467acd394d73537ccb2226a487ce` |
| Hardware evidence manifest | `a1fba8e644ca5538fed5387279ecb980768beeafb8a56b706b2d3fa3a5e00d5a` |
| Three-boot summary | `d94434e523e920a19c9166f0523fab0e7cc2cbe87d1ee912da28adfb75731d31` |

### Front-panel and reset-qualified 187.5 MHz release (2026-08-15)

The exact production design retains every graphics and HDMI-audio feature and
adds the Arty front panel: four mono LEDs, the three channels of the RGB LED,
four buttons, two switches, software LED ownership, atomic set/clear/toggle,
and an HDD-activity hold timer. Production run 1/5 routes all 66,515 nets with
zero errors and passes setup, hold, and pulse width at
`+0.250/+0.010/+0.538 ns`. Runtime FCLK1 remains exactly 187,500,000 Hz; the
200 MHz implementation-margin target is not shipped in the PS clock setup.
Exact use is 31,904 LUTs, 39,006 registers, 12,203 slices, 129.5 BRAM tiles,
and 81 DSPs.

The front-panel hardware ID/version/capability registers read
`0x504e4c30`, `0x00010000`, and `0x1f020407`. All seven output channels pass
ownership and data readback; atomic set, clear, and toggle produce
`0x57`, `0x56`, and `0x50` from the directed pattern, and the activity timer
asserts after a combined activity write. The test restores LED data,
ownership, and the 100-cycle default hold value. Firmware POST now resets and
probes the panel along with the existing graphics chips. Physical operation of
all four buttons and both switches produces debounced assert/release samples
and the exact accumulated change mask `0x030f`.

Hardware qualification found one ordering defect outside RTL. Asserting the
global FCLK1 fabric reset before Linux's first RTL8211F attach left the PHY's
ALDPS access timing out; increasing the PHY reset delay to one second did not
change it. Minimal-init experiments proved that one normal Linux
`ip link set eth0 up` before the existing chip reset makes every subsequent
attach reliable. The shared reset helper now performs that idempotent PHY
prime before asserting reset. The final cold boot negotiates 1 Gbit/s, has no
ALDPS timeout, reports FPGA manager `operating`, passes full MC68030/PMMU,
SDRAM, front-panel, filesystem, and terminal POST, and reaches initial-image
stage 8.

The live QMP reset gate then exposed `-no-reboot` in the existing launcher:
QEMU correctly emitted a shutdown event but the flag converted reset into
process exit. The flag is removed with a failing-before/passing-after launcher
test. On the board, QMP now emits `RESET`, QEMU remains at the same PID, and a
new POST/stage-8 boot completes. A Linux evdev pointer detach/reattach also
keeps that PID alive and restores the `astra-pointer` input object. Physical
Apple-keyboard hotplug creates its evdev node and `astra-keyboard` QMP object
while the QEMU PID and POST count remain unchanged. Ten
consecutive renderer, Copper, sprite, and 48 kHz HDMI-audio sweeps pass on the
exact cold-booted release, followed by another complete sweep after the live
reset. Linux software `reboot` still reaches `System halted` instead of
resetting the Zynq; power-cycle and JTAG reset are qualified independently.

| Front-panel/reset release artifact | SHA-256 |
|---|---|
| Bitstream | `3fca60ea1af0aca2110633fe21561154e40f48d5d5713c8981b8388ca3c52afc` |
| XSA | `20eeb703f08feb7e76a96f54cc2934dcedf698be342a9c4d04108b2173ac491e` |
| Routed DCP | `fa5fde5621acdf59b1abb3ab5bca3acbdeeb832d1eb097eee728e9fb642e7b72` |
| Timing summary | `ba503c4774f6850dcdf16ec81bb3a2d2afbdacd42803e94d5d2da9a9497fed18` |
| Route status | `feaaa37cd59bf7b0ea59bd29ea452c6893373c590352dbe185ba2ff97e4b2e76` |
| Utilization | `e5535ab5568fc7d70b4c2ac7f5a8334b6939e631c7f62fab6ac5c593b134ed41` |
| Methodology | `0ef947c0a15f9218733a17f7f26b2f3c13e77517dd708494fcfb57b5a015d7db` |
| Block-design clocks | `bdd200c66f5724b03caa3cbbc3cadfffb7b41801b6d1295e6396ce7e93f38a0d` |
| FSBL ELF | `63ba80537dc7041f3972e4d9c5730ce4730454a6f5222d84656cf136e3b2a6f9` |
| FSBL PS initialization | `282766b007cd85e0142ebfba99a04e2ccc9dae4bec925c3246b6d7fe7035c6ab` |
| Device tree | `2f2066836cefa517e15a75c6f79aca18fe301383135cdb29e4d841b0bd919c8c` |
| BOOT.BIN | `545f0ccb259972bc7fc26c08f9080dc7033ef7627693ff1ff03085c98a9e3d9c` |
| FIT image | `74838cdca1f45205bd2d69e6fba51f59b5fae43c2de39fde3e8f9cdc4ed4eb2d` |
| ARM QEMU | `a534f8f7af75743c3cfd71ef5854a57dc75a4bdfafb6a1f5bedcb668ad768220` |
| Chip-reset helper | `4ea0c4abca850d339c20d4b5668adde450ac5d59e7eb070f5c413ccd2d8892a6` |
| Reset-capable launcher | `5a4e35f1929773d27e5d55ed3bbefdf5b9caa8d47a8819c83f355b3d9e1d9400` |

### Rejected cached-RGB565 copy stream (2026-08-20)

The bounded experiment reused the blitter's existing 64-bit source cache and
pixel writer; it added no text engine, FIFO, memory, or application-specific
registers. Three forms were rejected before the final candidate:

| Form | Local result | Full result | Disposition |
|---|---:|---:|---|
| all-format stream | 3,470 cycles | OOC `-1.289 ns` | Removed |
| RGB565 stream in `ST_PIXEL` | 3,470 cycles, OOC `+0.002 ns` | `-0.262/+0.017 ns`; post-route `-0.130/+0.017 ns` | Removed |
| dedicated stream state | 3,470 cycles | OOC `-0.220 ns` | Removed |
| output-register RGB565 stream | 3,470 cycles, OOC `+0.090/+0.110 ns` | Initial `-0.021/+0.048 ns`; recovered `+0.122/+0.048 ns` | Routed, hardware-rejected |

The output-register form's exact full route connects all 66,652 nets with zero
errors and passes pulse width at `+0.538 ns`. Post-route
`AggressiveExplore` added no cells and improved setup from `-0.021` to
`+0.122 ns`; the fail-closed recovery flow then wrote the bitstream. Exact use
is 32,123 LUTs, 39,069 registers, 12,346 slices, 129.5 BRAM36-equivalent tiles,
and 81 DSPs. The isolated blitter uses 2,906 LUTs, 3,215 registers, and 11 DSPs.

The original 64x16 RGB565 copy moved 1,024 pixels in 5,822 cycles. The stream
moved them in 3,470 cycles (`-40.4%`). A retained test derived from the live
desktop batch covers 1280x3 pixels at 2,560-byte pitch: qualified RTL takes
20,734 cycles and the stream took 11,782 (`-43.2%`). Both forms are bit-exact
in simulation.

The candidate was packaged with the unchanged qualified FSBL, U-Boot, DTB,
and FIT, loaded through the Zynq FPGA manager, reset through the shared fabric
reset, and initialized by `astra-graphics-boot`. Renderer, Copper, sprite, and
48 kHz HDMI-audio certification passed. The live desktop then exposed a
release-blocking visual failure: its valid, single 1280x644 RGB565 compositor
BLIT wrapped near mid-screen. Live scanout readback still reported base
`0x18200000`, pitch 2,560, and 1280x720 dimensions, so the submitted software
stride was not the fault. Physical correctness overrides the simulated speed
and timing-clean route.

The candidate RTL was removed. Atomic rollback restored BOOT.BIN
`545f0ccb259972bc7fc26c08f9080dc7033ef7627693ff1ff03085c98a9e3d9c`
and unchanged FIT
`74838cdca1f45205bd2d69e6fba51f59b5fae43c2de39fde3e8f9cdc4ed4eb2d`;
the live qualified PL binary is
`1c5b715f45af007946fe7e087027480d6c9578d644263ae8ec1612c762ca80ec`
and FPGA manager reports `operating`.

| Rejected candidate artifact | SHA-256 |
|---|---|
| RTL source | `c06f5052bb245d07a36f29f71ecc06248e553450b8175249eeb6c9f80b9a7a36` |
| Bitstream | `cc1ad8a7092ff757582eb9fac10c28b8bfad1600c4248485ac324337493ebcf9` |
| FPGA-manager binary | `53151de8970d156cee283be3372066031a9e5eb3c98d6e669f537d030b183ba2` |
| Routed DCP | `c53d8f547c04b6f886808064b1ee7e8b053b9b768d0b129c133e357ccb17eca0` |
| Timing summary | `3d224a321fb871cee256f3cad4e5c22bde008ae75ead7fb40537c1a9b0a04b4d` |
| Utilization | `e7fb4285bd690da5c1a159b77aaeb4fa65393dd668eb07958cc11b206a400267` |
| Route status | `398d7af809eae63a64ff7e260720d98c447b25b66157e942c36d7afc083c92c8` |
| Methodology | `dff1daca3f7c006b89a4d1cb665fb4f64c597dc223df3390e584b38cb5e9aab0` |
| Candidate BOOT.BIN | `194f8381c42ea6cf738fd209b2c9adee2fb3b1ce6916a0d40ea6edabca8b761d` |

### Screen-offset regression and rejected live signature (2026-08-20)

The retained regression covers the two paths involved in the observed failure
without adding production logic:

- `tb_astra_render_blitter.sv` performs the exact 1280x644 overlapping RGB565
  desktop copy at 2,560-byte pitch in a 1280x720 surface. Its coordinate-unique
  pattern does not repeat at 640 pixels; all 921,600 destination and untouched
  pixels are checked. Qualified RTL completes the copy in 4,401,758 cycles.
- `tb_astra_graphics_pipeline.sv` now has a second production-width run at
  1,280 active pixels and 1,650 total pixels. It compares every final pre-HDMI
  RGB pixel across four lines and fails if its oracle ever repeats at the known
  640-pixel wrap distance. The separate blitter gate covers all 720 rows.
- The hardware render certifier runs the same 1280x644 copy and reads back the
  complete 1280x720 surface. The qualified PL passed in 11,917,253 cycles. The
  rejected stream candidate also passed readback in 9,406,057 cycles, proving
  that framebuffer readback alone cannot certify a downstream scanout fault.

The final Beast run reported `ASTRA SCREEN OFFSET PASS pixels=5120 width=1280
height=4` after checking all four production-width lines; wall time was about
81 seconds. The ordinary 64x4 integrated pipeline test also passed unchanged.
The exact blitter regression remains 4,401,758 cycles, and the complete graphics
suite passed around these focused reruns.

A final-pixel signature was evaluated as a physical diagnostic, but every form
consumed timing margin that the release does not have:

| Form | Route directory | Final setup/hold (ns) | Disposition |
|---|---|---:|---|
| CRC32 | `screen-signature-20260820/release-1` | `-0.208/+0.009` | Rejected |
| 32-bit one-adder hash | `screen-signature-20260820/release-2` | `-0.101/+0.025` | Rejected |
| 32-bit one-bit LFSR | `screen-signature-20260820/release-3` | `-0.241/+0.039`; post-route `-0.113/+0.039` | Rejected |
| incremental LFSR | `screen-signature-20260820/release-4` | `-0.363/+0.010` | Rejected |
| compact LFSR | `screen-signature-20260820/release-5` | `-0.169/+0.015`; post-route `-0.055/+0.015` | Rejected |
| CRC16 | `screen-signature-20260820/release-6` | `-0.229/+0.021` | Rejected |

All runs used Vivado 2024.2 on Beast and fully routed the production design;
none wrote a release bitstream. The final CRC16 route connected all 66,345
nets, passed pulse width at `+0.538 ns`, and used 32,287 LUTs, 39,070 registers,
12,423 slices, 129.5 BRAM tiles, and 81 DSPs. The checker RTL, MMIO registers,
and proposed version 1.0.6 were removed. Production remains ABI 1.0.5 and the
qualified front-panel/reset bitstream remains the release authority.

### Standards-based HDMI startup: rejected 200 MHz checkpoint (2026-08-21)

The first full route after adding the Arty Z7 native PS7 I2C0/EMIO DDC path,
active-low EMIO GPIO HPD path, DVI-safe reset state, vertical-blank HDMI mode
transition, and retained 48 kHz stereo audio was deliberately run at an actual
200,000,000 Hz FCLK1. Vivado 2024.2 on Beast routed all 66,445 nets with zero
routing errors, but the fail-closed gate rejected setup/hold at
`-0.185/+0.033 ns`; pulse width passed at `+0.538 ns`. No bitstream was written
and the board was not changed.

The worst setup cone is the existing glyph blend divide-by-255 carry path,
`blend_numerator_b_q_reg[7]` to `blend_result_b_q_reg[5]`, at eight logic
levels and 5.175 ns data delay. The remaining failures are existing glyph
sample-decode and render-engine reset fanout paths. HDMI startup/control is not
in the failing set. Exact use is 32,245 LUTs, 39,004 registers, 12,299 slices,
129.5 BRAM36-equivalent tiles, and 81 DSPs.

This is an over-target timing experiment, not a release candidate. The current
production architecture remains an exact 187,500,000 Hz runtime FCLK1 with the
documented 200 MHz implementation-margin target. The next checkpoint uses that
exact production configuration and retains every feature.

Source identity is base `22c1656cdde8365f554a22033786e04a90d9bc5b` plus
workspace binary diff SHA-256
`3ce2e1f7fe7711d9b11bf8db53486bae753f2224847ac195124ce7e7880b67b5`.

| Rejected 200 MHz artifact | SHA-256 |
|---|---|
| Routed DCP | `4618549937fe4e5ef05afffd44719c9df684423b2b1af11477eab52e8b283fa9` |
| Timing summary | `0cd4a5f213a4e467dfb649c76019587125995a31f178e3d799536668f7dbfcdb` |
| Utilization | `404070aa0738f3f836007ce47e885187b4bb0a1361a6387113cbb3f13df7b34d` |
| Route status | `727ffbc4668ee6be671cfe1c22c72e0bbd66fea2039ad5b1d254fed9e6397df0` |
| Methodology | `69428fd5371b213ab3ea36bcf32d20912a80c94286f638b729a47794afecc458` |
| Block design | `d5c1ceaa4a375fa91df1e3ea4d5daca3c67479ea123a5149c7c1fcbfdf4ecf84` |

### Standards-based HDMI startup: production-clock route (2026-08-21)

The exact production candidate keeps the complete graphics, front-panel, and
48 kHz 24-bit stereo HDMI-audio feature set. Vivado 2024.2 on Beast generated
the PS7 fabric clock at exactly 187,500,000 Hz and routed all 66,591 routable
nets with zero routing errors. The release-clock gate passes setup, hold, and
pulse width at `+0.055/+0.034/+0.538 ns`; a bitstream and XSA were written.

The same placement and route were deliberately driven with the established
200 MHz implementation-margin constraint. That optional margin missed setup at
`-0.278 ns` while hold and pulse width passed at `+0.034/+0.538 ns`. This is
recorded as margin evidence, not as permission to run FCLK1 at 200 MHz. The
shipped clock remains exactly 187.5 MHz.

Exact use is 31,951 LUTs (60.06%), 39,064 registers (36.71%), 12,249 slices
(92.10%), 129.5 BRAM36-equivalent tiles (92.50%), and 81 DSPs (36.82%). Source
identity is base `22c1656cdde8365f554a22033786e04a90d9bc5b` plus workspace
binary diff SHA-256
`e61e90766cbd72f74c8c3f9f0d64375f44a3df57adc4c87886ea4e9414b6c5d6`.
Fresh packaging from the exact XSA passes: the FSBL contains the documented
187.5 MHz PS7 mask, the device tree enables I2C0 at 100 kHz and assigns
100/187.5 MHz fabric clocks, the FIT extraction compares byte-for-byte, and
all ARM tools pass strict compilation, static analysis, and host self-tests.
The fresh ARM build found and removed one dead register-read helper left by the
discarded fabric-HPD experiment; it had no caller or runtime behavior. The
board has not yet been changed; physical HPD, EDID, warm-load, visible-screen,
and audible-audio qualification remain release blockers.

| Production-clock candidate artifact | SHA-256 |
|---|---|
| Bitstream | `2864ce9049d3e581ebe5d68cb8fc2e8519623f66705d44eae24f3b80549d6384` |
| XSA | `1a074aae5f55f3a61d6c900b6947568382a47260e49f64619974da358c124bcb` |
| Routed DCP | `9be666dc8697bfc81d37fb339d984d7f1053da2866c7e54c357b54207e6b7eb7` |
| Release timing summary | `4fbedaad91109023343c7fe3f0994682997f2df01c7f6619cdefe82e83b2e4d3` |
| 200 MHz margin timing summary | `be9563217dd234b89c9fe9b3c7e365d2136b0f5988484c9d0716f0ac1f1256ef` |
| Utilization | `86335252aec5a5e60a0837c34043d29861e518e092b67ba87200fa6f5619b56d` |
| Route status | `62f8fbb8b69d4caf06cf65fb66517256ed76b08e2e23235cfa920131852c6c1a` |
| Methodology | `ae95b1737f63e513ccde7dfd8e3ae9cb97e46e6ff33a5221a8e2fb0511066df1` |
| Block design | `53d9bbaf2381629666b1c4db172aea954e4d433b26632e2d7c49f47e75477b9b` |
| CDC report | `4c3d983c2225b1268ec4cb8b0b14690f4e606766d31e78fda462bc9a1c455749` |
| FSBL ELF | `b47d97d551d276934cb6b4b7fbc3e9ed429f3ea08b5ab46d1a3efd723717dfad` |
| FSBL PS initialization | `282766b007cd85e0142ebfba99a04e2ccc9dae4bec925c3246b6d7fe7035c6ab` |
| Device tree | `d60c82c0a0cdccfab49adf03cda541d115a2f81de23ab36b9fe2a977949bfb98` |
| FIT image | `3fee66d1d33080b238509169ce459bbcae853d895a192b20de89d4f25df85dea` |
| BOOT.BIN | `67d03fb4f1205236bad2bf870083d289fee135f428831bcaf21a1b0553dcb3fc` |
| HDMI link manager | `134a1a2041c4679fc52204a69b3879bad419316852a8423e1608d68b3c535870` |
| HDMI link manager source | `a78374e4041df24adc751a56317eea387fb30c2a327b80ca5b99386024d243a7` |

Hash-verified atomic deployment through Beast installed the candidate BOOT/FIT,
manager, and first-boot script on the Arty SD card. The prior qualified
BOOT/FIT remain byte-exact at
`BOOT.BIN.rollback-545f0ccb2599` and
`image.ub.rollback-74838cdca1f4`; the immutable root was restored read-only.
The running PL is still the prior qualified image. A physical power cycle is
required before the candidate can be tested.

### Warm-reload SLCR ownership repair (2026-08-21)

The candidate cold-booted on the Arty with HDMI request `1`, active status `3`,
and successful 720p60 E-EDID negotiation. With Linux runtime power forced on,
the Cadence I2C0 controller matched the exact Xilinx driver initialization:
control `0x0000310e`, timeout `0x000000ff`. A hash-verified full reload through
the Linux FPGA manager preserved those values, proving that FPGA programming
itself did not damage the PS I2C controller.

The following `astra-chip-reset` pulse reproduced the warm failure. Its final
write to `SLCR_LOCK` prevented Linux from setting documented
`APER_CLK_CTRL.I2C0_CPU_1XCLKACT` bit 18. Linux runtime PM reported `active`,
but `APER_CLK_CTRL` remained `0x00d00444` rather than `0x00d40444`; I2C control
and timeout both read zero and E-EDID timed out. AMD UG585 states that
`SLCR_LOCK` blocks writes to all SLCR registers. The exact deployed Xilinx
Linux source, commit `2b7f6f70a62a`, unlocks SLCR in
`zynq_early_slcr_init()` and does not relock it because the clock framework
continues to update those registers.

The retained helper removes only the incorrect lock write. It still unlocks
SLCR, asserts FCLK1 reset, releases FCLK1 reset, verifies every Astra hardware
identity/reset register, and asks the HDMI manager to revalidate E-EDID. The
mocked helper test requires that exact three-write sequence, and the graphics
build's pre-synthesis HDMI contract runs the test before invoking Vivado.

On the physical board, the unchanged manager resumed I2C0 after the corrected
helper, read E-EDID, reported `HDMI 720p60 audio=2ch-LPCM-48k-24bit`, and
restored link status `3`. The hardware audio certifier passed 48,064 frames at
48 kHz with a 440 Hz tone. Visible alignment and audible output await operator
confirmation. The complete Beast graphics regression passed, including
`ASTRA SCREEN OFFSET PASS pixels=5120 width=1280 height=4` and the exact
1280x644 overlap blit in 4,401,758 cycles. Linux host tests and ARM GCC
`-fanalyzer` also pass.

| Warm-reload repair source | SHA-256 |
|---|---|
| Installed `astra-chip-reset` | `6ad793945c05d5c49c720e5d185f9ca1f612c78e82c07c7fe667e9c4de258395` |
| Reset-helper behavioral test | `ebc882b8a829eac53c72960c1ae3a286582d5598bc0e48537cff2751a38be7b9` |
| Pre-synthesis HDMI contract | `10cc69d3a8495d67c4790ba627407362bf229d5938d060c5c87c18ea57828e5b` |

No RTL, constraints, clocks, placement, or route changed in this repair. The
production-clock route and its `+0.055/+0.034/+0.538 ns` release timing remain
the implementation authority; rerouting would provide no evidence for this
Linux SLCR-ownership defect.

### Rejected capture-line compensation (2026-08-21)

A narrow dark column observed at the left edge of Cam Link captures was first
misidentified as a fixed Astra source-coordinate delay. A temporary candidate
advanced the RGB source coordinates by five pixel clocks. The focused RTL
tests passed. A fresh full route connected every net but failed the release
gate at `-0.148/+0.016 ns`; an incremental route passed at
`+0.061/+0.034/+0.538 ns` and produced bitstream SHA-256
`e8be8634d3d8fcfd6775fdb7cac939ea4e564fbd4d9877b0562d3d08f3e16522`
and binary SHA-256
`21a0515f468f34a3f31f9c711f82537958046d746896462f7f7c6d56b6780935`.
It used 32,020 LUTs, 39,064 registers, 129.5 BRAM36-equivalent tiles, and 81
DSPs. A live FPGA-manager load did not complete cleanly and the board required
a physical power cycle. The candidate was never promoted and its temporary
board, NUC, and Mac deployment copies were removed.

The physical diagnosis then disproved the coordinate hypothesis. The same
column remained in the Cam Link output after the HDMI source was changed from
the Arty to an unrelated C64 Ultimate. Resetting only the Cam Link USB device,
while leaving the C64 connected, cleared the column. With the receiver reset,
the current production Astra image remained clean through all of these
separate checks:

- DVI video with the HDMI manager stopped and link control `0`;
- true HDMI enabled manually, with requested/active link status `3`;
- the manager's disable, E-EDID read, and HDMI-enable sequence;
- a physical Arty power cycle with HDMI and Cam Link USB left connected; and
- a physical HDMI unplug/replug with Cam Link USB left connected.

Therefore the dark column was retained Cam Link receiver state, not an Astra
framebuffer, blitter, raster-coordinate, HDMI-mode, cold-start, or hot-plug
fault. The exact earlier event that put the Cam Link into that state was not
reproduced, so no narrower cause is claimed. The five-pixel compensation and
its dedicated source-coordinate test were removed. HDMI HPD/E-EDID handling,
DVI-safe reset, vertical-blank HDMI mode changes, and audio were retained.

The complete Beast graphics regression after removal passed, including the
HDMI source contract, HDMI mode transition, `ASTRA SCREEN OFFSET PASS` over
5,120 pixels at 1,280-pixel width, and the exact 1,280x644 overlap blit in
4,401,758 cycles. No new route is required because the rejected compensation
was removed and the source returned to the already-routed production HDMI
implementation. The `+0.055/+0.034/+0.538 ns` production-clock route remains
the release authority.

## Generic AXI framebuffer copy timing campaign (2026-08-21)

The retained renderer path is a 64-bit, 16-beat overlap-safe AXI memory mover
behind the existing BLIT command. Transactions are INCR bursts, never cross a
4 KiB boundary, and complete all accepted beats. Same-format direct copies
with eight-byte-aligned pitches and matching byte lanes use the mover; every
other BLIT retains the established per-pixel implementation. This preserves a
single public command and adds no Terminal-specific RTL.

The exact production-width simulation changed as follows:

| Copy | Previous clocks | Retained clocks | Change |
|---|---:|---:|---:|
| 64x16 identity RGB565 | 5,822 | 1,254 | 4.64x faster |
| 1280x644 overlap RGB565 | 4,401,758 | 824,550 | 5.34x faster |

The AXI memory oracle fails any renderer burst that crosses 4 KiB. The full
graphics suite passes the exact 1280x644 overlap copy, all 921,600 destination
and untouched pixels, and `ASTRA SCREEN OFFSET PASS pixels=5120 width=1280
height=4`.

All production builds used the full HDMI, 48 kHz stereo audio, front-panel,
and renderer feature set with Vivado 2024.2 on Beast. The implementation
target remained 200 MHz and the fail-closed release gate rechecked the routed
design at the actual 187.5 MHz clock.

| Checkpoint | Exact result | Worst evidence | Disposition |
|---|---|---|---|
| `production-route` | setup/hold `-1.701/+0.012 ns` | new chunk setup/strobe cone | Rejected; no bitstream |
| `production-route-rowbytes-registered` | `-0.109/+0.011 ns`; 32,596 LUTs, 39,579 registers, 12,344 slices | existing flood/glyph enables after mover controls were registered | Rejected; no bitstream |
| `production-route-incremental-qualified` | `-0.716/+0.022 ns` | displaced global reset/build-reset fanout | Rejected; incremental flow |
| beat-counter planner | OOC 3,578 LUTs, 3,637 registers, 1,252 slices versus 3,473/3,614/1,221 | larger despite identical 824,550-clock behavior | Reverted before integration |
| `production-route-extra-timing-opt` | placement could not place more than 5% of movable instances | dense 92% slice design | Rejected; no route |
| `production-route-explore-postroute` | 67,514/67,514 nets, `-0.205/+0.011 ns` | surface validator combined two 32-bit compares and all flags in one cycle | Rejected; no bitstream |
| `production-route-validator-pipelined` | 67,294/67,294 nets, `+0.070/+0.011 ns` | prior validator path absent from tight-path list | Retained candidate |

The retained timing repair reuses the surface validator's unused eighth state
to register palette-range and pitch comparisons before combining validation
flags. It adds one 187.5 MHz setup clock per surface descriptor, does not add a
module, and leaves block-copy clocks unchanged at 824,550. The focused
command-processor route uses five fewer LUTs and fifteen fewer slices, while
the full route improves the comparable release result from `-0.109` to
`+0.070 ns`.

Exact retained resources are 32,548 LUTs, 39,402 registers, 12,253 slices,
129.5 BRAM36-equivalent tiles, and 81 DSPs. Source identity is base
`f9f36db7cadf598e9f071a20012dee7215d3bea8` plus implementation diff SHA-256
`a9de1584e1c639863eae98b38ba87b4df605ee3108a2a3190aa74cccb6d9dab3`.

| Retained candidate artifact | SHA-256 |
|---|---|
| Bitstream | `f3ccce904124714d77b3f936debdad195a29c5f089ffb0c0783c195397369bb4` |
| XSA | `106742e55da35f47cb60011317fb2af5bbbcee3d17577ab30c23cb6961ee4ad8` |
| Routed DCP | `e0aa57ce8c681f7d8beef3357adfbe72180f1aa69c773ee8604dca98ed20bcc1` |
| Timing summary | `44e33debace96ca5ea204a245f37d21be63355d3aa6ff36433f1af6c904fcd3e` |
| Utilization | `d4b5964e33e828ba51c821ea38231cc06866ba1b8313f99da61c2275021de4c8` |
| Route status | `1816ba97dc54306b8f8186084c3f2f9e51a65176f50efb458d1c420f7d42d10f` |
| Methodology | `b28d7c3db535a653599168320be00fce57cc6477c9388fc76deea3757f662e1e` |
| CDC report | `e703e8a7ba444e09710fe4089190e25f93a4c7e47832f4a1a1930915e887278e` |

### Physical framebuffer and Terminal qualification (2026-08-21)

The exact routed bitstream above is active on the Arty through Beast. The board
completed the 1280x644 overlapping RGB565 copy in 1,431,536 clocks versus
11,917,253 clocks on the prior implementation (`8.32x`), or 7.63 ms at the
187.5 MHz production memory clock. The production-width screen-offset gate,
framebuffer checks, repeated cold boots, HDMI unplug/replug, and audible
48 kHz stereo output pass. The HDMI manager reports
`HDMI 720p60 audio=2ch-LPCM-48k-24bit`.

The retained application-side change uses the existing Terminal event loop to
defer child-output presentation for no more than 16,666,667 ns. Keyboard input
and child completion still present immediately, and the stream capacities
remain 4/2 messages with a four-message pump budget. Beast built a matching
ROM and storage pair from base `f9f36db7cadf598e9f071a20012dee7215d3bea8`
plus Supervisor diff SHA-256
`892f20c8d4463ef1fcf12d1e66f8e15903cd753bf6c1207dfbf07030971e2c9c`.
The installed ROM is
`e07e648f347e2a522ce8297f67af213a2281ff4f6cecb504ec1ad19e7670b07e`;
its clean storage image is
`b033561aeb0b3728301a6ada6fdf84aef7d32e499a1e848462ed79d197ab2352`.

A controlled physical 10-run A/B of `ls -l COMMANDS:` measured the old pair at
1,848.384 ms and eight median presentation batches, and the retained pair at
1,594.976 ms and six batches: `13.7%` lower latency and `25%` fewer
presentations. A second 10-run retained gate passed at 1,668.367 ms and six
batches. Twenty-five measured candidate runs completed at stage 8 with the
runtime processes alive and no new panic. `tools/measure-terminal-text.py`
now accepts `--max-batches`; the hardware regression uses six. No RTL,
constraints, route, resources, HDMI, or audio configuration changed during
the application experiment, so the `+0.070/+0.011 ns` route and resource table
above remain exact. A physical cold boot of the exact installed application
pair subsequently passed full POST, initial-image stage 8, and a fresh
five-run `ls -l COMMANDS:` gate at 1,622.439 ms and six median presentations.
The physical framebuffer and application release gates are closed.

## AXI lane-realignment campaign (2026-08-22--23; rejected physically)

Per-command board profiling isolated the next framebuffer bottleneck. The
Terminal text-box self-scroll is already a hardware copy: 816x420 pixels in
about 658,818 render clocks. The following compositor cache update copies
816x440 RGB565 pixels from a 1,632-byte pitch on byte lane 0 to a 1,640-byte
pitch at a two-pixel inset on byte lane 4. The released mover's equal-lane gate
rejects this case, producing about 5,465,036--5,513,144 board clocks per copy
through the pixel fallback. The update occurs about six times during the
measured listing. A physical four-to-sixteen-message Terminal queue experiment
did not improve the release median and was reverted, so no queue change is in
this candidate.

The candidate removes only the equal-lane restriction from the shared direct
same-format BLIT admission and extends `astra_render_copy_burst.sv` to realign
source bytes onto destination lanes. AXI transactions remain aligned,
full-width, at most 16 beats, INCR, and split at 4 KiB. The mover captures the
complete source chunk before writing, retains forward/reverse memmove order,
and uses WSTRB for destination edge bytes. The focused regression covers all
64 source/destination lane pairs, outside-byte preservation, the existing
same-surface right shift, and the exact 816x440 compositor geometry.

| Simulation checkpoint | Identity | Exact mismatch | Desktop overlap | Transactions | Disposition |
|---|---:|---:|---:|---:|---|
| Released equal-lane gate | 1,254 | 1,918,638 | 824,550 | 89,760 mismatch | Baseline |
| uniform 120-byte chunks | 1,518 | 423,574 | 927,582 | not retained | Rejected: regressed aligned copies |
| lane-aware chunks | 1,254 | 378,974 | 824,550 | 6,224 mismatch | Functionally retained before timing repair |
| pair-register pipeline | 1,254 | 423,950 | 824,550 | 6,224 mismatch | Retained candidate; focused and complete suites pass |

Planner timing experiments were measured rather than accumulated. The initial
200 MHz out-of-context route failed at `-0.574 ns` from the byte cursor into
the remaining-limit register. Adding an explicit remaining-byte register moved
but did not fix the path (`-0.618 ns`). Registering the lane-dependent forward
chunk capacity passed the 200 MHz out-of-context route at `+0.081 ns`, using
3,981 LUTs, 3,732 registers, 1,362 slices, 128 LUT-memory bits, and 11 DSPs.

A fresh full-feature Vivado 2024.2 production implementation on Beast used no
incremental checkpoint and routed 67,779/67,779 nets. At the build's default
200 MHz stress clock it failed setup/hold at `-1.539/+0.051 ns`, with 7,233
failing setup endpoints and -1,949.722 ns total setup violation. The worst
path was
`fast_copy_i/write_index_q_reg[0]_replica/C` through the realignment network to
`hp3_render_slice/.../skid_buffer_reg[9]/D`: 6.384 ns data delay, six logic
levels, and 5.184 ns of routing. Resources were 33,533 LUTs, 39,553 registers,
129.5 BRAM tiles, and 81 DSPs. Output directory:
`/mnt/Documents/astra68/work/buttery-scroll-20260821/repo/build/arty-graphics/production-route-dre-capacity`.
The gate rejected the build and wrote no bitstream.

The retained local mover revision inserts a 128-bit source-pair register
between the distributed-RAM word selection and the lane-dependent byte shift.
Equal-lane copies retain their original data path. Mismatched copies add one
setup clock per chunk and preload the next pair only after an accepted AXI
write beat, so W-channel backpressure holds data and strobes stable. The
focused blitter regression completed with exit status zero at 1,254, 423,950,
and 824,550 clocks for the identity, exact mismatch, and desktop cases. The
complete graphics suite then completed with exit status zero and the same
counts, including HDMI, AXI, tile, sprite, compositor, screen-offset, and all
blitter tests.

The exact candidate is based on
`d27d6be762cd6335ae366a597b49c2092b6e1bd5`; its source hashes are:

| File | SHA-256 |
|---|---|
| `astra_render_blitter.sv` | `cd1e035e1abd6ca757845c7e627e314ef6457f0a31d706c3c011f29a036b09d9` |
| `astra_render_copy_burst.sv` | `b4d167a3399b73f7b0acf7263d71f0f0ed95929a19b5338f3a6af447dc7e825a` |
| `astra_sprite_line_builder.sv` | `f6043926c47b290137f78fb920619984006ce89e029d32524a8bab993a7b4963` |
| `sim/tb_astra_render_blitter.sv` | `c23c58b7893a5d55d46996a7e7313207b34c0434e927b0548e3b1aaa9c9318c8` |

Four successive Vivado 2024.2 OOC routes on Beast measured the realignment
cone rather than accepting simulation alone:

| Revision | 200 MHz WNS | Worst cone | Disposition |
|---|---:|---|---|
| state-selected shift | -1.644 ns | mover state to realigned write data | Rejected |
| write-index preload | -0.328 ns | mover write index to realigned write data | Rejected |
| dedicated realignment index | -0.300 ns | realignment index to realigned write data | Rejected |
| source-pair pipeline | -0.005 ns | existing blitter mask-cache address to source ARVALID | Mover cone closed; retained |

The retained OOC output is
`build/arty-graphics/render-blitter-ooc-pair-pipeline`. Its isolated clock has
no final `HD.CLK_SRC`, so the unrelated five-picosecond placement estimate is
not accepted as a reason to alter mask-cache RTL.

Two clean full-feature production implementations used a 200 MHz
implementation target, restored the exact 187.5 MHz release constraint before
the gate, and used no incremental checkpoint:

| Strategy | Stress WNS | Release setup / hold | Failing setup endpoints | Route | Disposition |
|---|---:|---:|---:|---:|---|
| `Performance_Explore` | -0.392 ns | -0.059 / +0.046 ns | 4 | 68,062 / 68,062, 0 errors | Rejected; no bitstream |
| `Performance_ExplorePostRoutePhysOpt` | -0.363 ns | -0.030 / +0.013 ns | 3 | 68,062 / 68,062, 0 errors | Rejected; no bitstream |

The default output is
`build/arty-graphics/production-route-lane-realign-pair`; the bounded retry is
`build/arty-graphics/production-route-lane-realign-pair-postroute`. The latter
has `-0.066 ns` total setup violation. Its worst path is
`pipeline_i/scheduler_i/client_start_reg/C` to
`pipeline_i/sprite_builder_i/prep_state_reg[1]/CE`: 4.930 ns data delay,
0.952 ns logic, 3.978 ns route (80.689%), and four LUT levels. The remaining
two `-0.018 ns` paths end at
`sprite_builder_i/admission_position_q_reg[1]/CE` and `[3]/CE`. The mover is
absent from all failing endpoints. Post-route physical optimization attempted
the exact cone and made no timing change.

Resources for the best full route are 33,202 LUTs, 39,741 registers, 12,435
slices, 129.5 BRAM tiles, and 81 DSPs. Evidence hashes are:

| Artifact | SHA-256 |
|---|---|
| Routed DCP | `52cb936058af96a0664b51c4a654fad62df4375b0779205fba01ad94d4889316` |
| Timing summary | `fd2d69c09e5bf65f1f528c373f8ba3030eadbb9c5745e550087d66551fa9bf5d` |
| Utilization | `b38c520ef7b716254e2038e4c1d31a30d41c9be4bc803a8ad8584581f6cabecb` |
| Route status | `4b4ea8a0170ffa4b470e2ee9d116ea127a0a0fc7afb80bc6fb2360e0b5808aa4` |

The retained timing repair adds a local one-cycle input pipeline for the sprite
builder's start pulse, build slot, and line number. It breaks the exact
scheduler-to-sprite route cone without changing scheduler policy. All nine
focused sprite modes pass with exit status zero: functional 342 clocks,
16x128 worst case 1,562, overflow 398, SLVERR 398, deadline 300, 16-way
collision 1,584, 4 KiB split 472, variable dimensions 1..128, and the
16-sprite count limit 863. The complete graphics suite also passes with exit
status zero, including the 5,120-pixel screen-offset gate and the unchanged
blitter counts of 1,254, 423,950, and 824,550 clocks.

The clean full production route uses Vivado 2024.2 on Beast, no incremental
checkpoint, `Performance_ExplorePostRoutePhysOpt`, a 200 MHz implementation
target, and the restored 187.5 MHz release constraint. It routes
68,015/68,015 nets with zero errors and passes setup/hold at
`+0.022/+0.018 ns`; pulse-width slack is `+1.416 ns` and there are no failing
endpoints. The 200 MHz stress result is `-0.311/+0.018 ns`. The worst release
setup path is now the existing glyph destination-row operand register to its
DSP input: 1.363 ns data delay, 0.419 ns logic, and 0.944 ns route. The former
scheduler-to-sprite cone and the mover are absent from the limiting paths.

Output directory:
`build/arty-graphics/production-route-lane-realign-sprite-start`. Resources
are 33,176 LUTs, 39,686 registers, 12,296 slices, 129.5 BRAM tiles, and 81
DSPs. The build wrote the timing-clean candidate bitstream and XSA:

| Artifact | SHA-256 |
|---|---|
| Bitstream | `baf8a6d9524125409ef0d0004272cb06dfa22d6144f1ad444e798b07c8e93b70` |
| XSA | `07d831107976d478ce651f8bd81931db3a1650689917d8b8b787930f9fef055f` |
| Routed DCP | `3bb79de79c6d598a17e800d49775e429a39e775be97fe9513a51e56c1fd3a607` |
| Timing summary | `3fa37371cfd26e63eab42c1d2de6d89d785dc517d80daf70978f0ea220531782` |
| Utilization | `bdd79d6be99b8a0d38451130126f9e1f6934d8d9953944cf3ba1f78ca0f2a4d2` |
| Route status | `2316ad9da5839272aca8865480398383916e3734d59079f8e1a39a93329b115a` |
| Methodology | `e265e2c68ace83cc250d2ad835bc39249a6625cc95cae6319c6104554554371e` |
| CDC | `1bc42c13a9af3ae05728f0430616f72a24e1837badf1742bc8adfb16f466d29e` |

### Physical lane-realignment qualification and rejection (2026-08-23)

The raw bitstream was converted for Linux FPGA Manager by the exact operation
proven against the prior production pair: strip the 119-byte `.bit` header,
reverse every 32-bit payload word, and append the final NOOP word. Raw input
remains
`baf8a6d9524125409ef0d0004272cb06dfa22d6144f1ad444e798b07c8e93b70`;
the 4,045,568-byte manager image is
`63b3a8e158ede638245be470fdacb6ef78ffab01700bf96af5e73268a89c42b9`.
The board verified that hash before the volatile load and reported FPGA
manager `operating`, flags `0`, plus graphics/audio/front-panel identities
`0x41535452`, `0x41554430`, and `0x504e4c30` after the installed
`astra-chip-reset` helper ran.

The existing `astra-render-certify` was extended with 64 one-byte-format
source/destination lane pairs and the exact compositor geometry instead of
creating a separate test tool. Its source SHA-256 is
`b5b8f54f4eb63801ba533c787b4c92b8bfe8ff11d20e6906902b0f8065530f90`;
the strict-warning, `-fanalyzer`, host-test, and static ARM build passes, and
the deployed binary is
`0f74fd2bf9a9a5d758dcd6bd93a748eac8f27b02fc11227188a4f41b4d45e828`.
Physical results are:

| Gate | Result |
|---|---|
| Complete renderer | PASS, 29 commands, 1,196,651 pixels, zero backpressure |
| 64 lane pairs | PASS, 37 bytes each, 40,940 total clocks, neighbor bytes preserved |
| Exact compositor | PASS, 816x440, 1,632-byte source pitch to 1,640-byte destination pitch, 705,006 / 3,125,000 clocks |
| Screen offset | PASS, 1280x644, 1,432,002 clocks |
| Sprite | PASS, 64 sprites, 1,564-clock hardware maximum, zero AXI/deadline errors |
| Copper | PASS, two banks, eight instructions, invalid target contained |
| Audio | PASS, 48,000 Hz, 48,064 frames, 440 Hz tone |

The clean application pair was restored before testing: ROM
`e07e648f347e2a522ce8297f67af213a2281ff4f6cecb504ec1ad19e7670b07e`
and pre-boot storage
`b033561aeb0b3728301a6ada6fdf84aef7d32e499a1e848462ed79d197ab2352`.
Two independent candidate boots pass POST, full-range SDRAM BIST, and
initial-image stage 8 without a panic. Terminal was opened through the
canonical desktop double-click before measurement.

Application qualification rejects the candidate. The five-run
`ls -l COMMANDS:` gate measured 1,612.009 ms and seven median presentation
batches. A ten-run repeat measured 1,562.989 ms and 6.5 batches. Both fail the
retained six-batch maximum. A controlled ten-run A/B after volatile reload of
the prior production manager image
`e5f8a45e0010be57b409bc8157f7fef493cb94f9edd7bc9e404b90677bff9851`
(raw bitstream
`f3ccce904124714d77b3f936debdad195a29c5f089ffb0c0783c195397369bb4`)
passes at 1,676.396 ms and six batches. The lane candidate lowers median
latency by 6.8%, but its extra presentation work is repeatable and is not
waived.

HDMI hot-plug was not run: the Arty is attached to Beast, whose local JTAG scan
sees the Cortex-A9 and XC7Z020, but Beast enumerated no `/dev/video` capture
device during this checkpoint. The board's manager correctly held link status
`0` with no sink. The prior production manager image is active again, state
`operating`, flags `0`; persistent boot files were not changed. Candidate and
control log SHA-256 values are:

| Log | SHA-256 |
|---|---|
| Renderer certification | `d176ea22637381949c0f3285949891d3920dba4525bbb647bddbb7b7649e2909` |
| Sprite/Copper/audio certification | `1e3258efff1d134785b0d054cb15dc9cebfc9e38ebdef5b44c90cd0865f5516b` |
| Candidate five-run Terminal | `183683d4745df1b8d7b30fb63065599ab9cf6d6bd66fc5720b9cc5cf1030fce4` |
| Candidate ten-run Terminal | `ba24fe51076f555172aef8bfd3c7fe53767e4af92ec52929d726a4cd24f2ae83` |
| Prior-route ten-run control | `d98081b596a6cd549f5a242c81193111b97f893e8d4b77937163d08071b9f6d8` |

The active blocker is the candidate-specific 6.5-batch Terminal median. Fix it
without weakening the one-frame presentation contract, then repeat all
physical gates with an HDMI sink present.

A corrective Beast-local checkpoint confirmed the physical topology directly:
the Digilent FT2232H exposes `/dev/ttyUSB0` and `/dev/ttyUSB1`, and the local
JTAG chain contains the Cortex-A9 and XC7Z020. A fresh candidate FPGA-manager
reload from a Beast-to-`astra-arty` session dropped Ethernet before any
certifier produced a result; this aborted run adds no qualification evidence.
Beast then programmed the exact prior raw bitstream
`f3ccce904124714d77b3f936debdad195a29c5f089ffb0c0783c195397369bb4`
through the physical Digilent JTAG link and issued a scoped XSDB APU system
reset. The board returned after a clean persistent boot with FPGA manager
`operating`, flags `0`, graphics/audio/front-panel identities
`0x41535452`/`0x41554430`/`0x504e4c30`, the clean ROM hash above, and the HDMI
manager, display bridge, and QEMU resident. All further Arty programming,
UART, capture, and board qualification must originate on Beast.

### Combined lane-realignment qualification (2026-08-23)

No RTL or route changed after the timing-clean lane-realignment checkpoint.
The exact source remains base
`d27d6be762cd6335ae366a597b49c2092b6e1bd5`, raw bitstream
`baf8a6d9524125409ef0d0004272cb06dfa22d6144f1ad444e798b07c8e93b70`,
and FPGA-manager image
`63b3a8e158ede638245be470fdacb6ef78ffab01700bf96af5e73268a89c42b9`.
Beast's full non-incremental implementation routed 68,015/68,015 nets and used
33,176 LUTs, 39,686 registers, 12,296 slices, 129.5 BRAM36-equivalent tiles,
and 81 DSPs. The exact 187.5 MHz setup/hold/pulse-width result remains
`+0.022/+0.018/+1.416 ns`.

The prior 6.5-batch application rejection is superseded by the minimum
software root-cause fix. STOR v9 writes the existing bounded alternating-bank
event snapshot through one shared-area request instead of one request per
record; `ls` uses one stdio buffer; and `readdir_batch` packs actual name
lengths. Host tests, strict warnings, static analysis, and the complete
MC68030 userspace build pass on Beast. The exact pre-boot storage image is
`e6d6f7379bf53303065bf954f44c359e96ba62bf71affd741bf55c9c0bf5c3e2`.

The physical Arty completed 20/20 exact `ls -l COMMANDS:` runs at a
779.731 ms median with exactly three renderer submissions/completions, 66
commands, and 16 glyph commands in every run. Evidence SHA-256 is
`1831c03c759864f4b1e04b8e3917410e0481018a71e43a4aea9f906d0cd52f49`.
The directory-list phase separately improved from 311.339 to 267.310 ms. A
4-to-16 message/pump experiment retained three batches but regressed the
20-run median to 843.464 ms; it was reverted, with rejected evidence
`b93d1fd30d3d582d9fe4cf7ed48d4de277e0162705cb8e183a998fb42847670`.

The exact expanded certifier
`0f74fd2bf9a9a5d758dcd6bd93a748eac8f27b02fc11227188a4f41b4d45e828`
passes 29 commands, 1,196,651 pixels, and zero backpressure; all 64 lane pairs;
the 1280x644 offset copy in 1,431,179 clocks; and the exact 816x440 compositor
copy in 703,962 of 3,125,000 clocks. Sprite passes with a 1,564-clock hardware
maximum and zero AXI/deadline errors; Copper passes two banks and invalid-target
containment; audio passes 48,064 frames at 48 kHz. Repeated POST, full-range
SDRAM BIST, and initial-image stage 8 pass. A storage restart followed by
`events --boot -1` recovered the previous boot; ring and decoded-trace hashes
are `765995b39389ef4e0744e79686a118d5c0156a7a8cae2c4e18790ea334ad8491`
and `7a594a3790e3a0e8971aef1aa5f92a226ca37882171f8e5922d6af319238396a`.

The candidate remains a volatile FPGA-manager load with state `operating` and
flags `0`; persistent boot files were not changed. The active application and
hardware blocker is closed. Beast has no `/dev/video` HDMI sink, so repeat
hot-plug and visual inspection remain the only physical release gate.
