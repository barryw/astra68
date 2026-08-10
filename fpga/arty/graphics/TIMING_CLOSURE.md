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
