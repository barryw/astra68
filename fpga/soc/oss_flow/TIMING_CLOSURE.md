# Astra68 timing-closure playbook

This file records the measured lessons from closing the complete Astra68 SoC on
the ULX3S-85K. It is a continuation point, not a claim that a release build is
finished. Update the result table and release checkpoint whenever the mapped
design or physical constraints change.

## Non-negotiable target

The production bitstream must satisfy all of these at the same time:

- TG68K.C 68030 plus PMMU, AstraHost SPI boot, SD stage 0, OHCI USB host,
  Astraea, tile-free Vega, and the 32 MiB SDRAM system are enabled.
- CPU is constrained to 12.5 MHz and the SDRAM controller to 60 MHz.
- Every other generated and external clock is explicitly constrained by
  `astra_clocks.sdc`.
- Yosys reports no combinational SCCs or other `check -assert` failures.
- nextpnr completes routing with no timing waiver in the release result.
- The `kernel_platform_v1` resource profile passes. Physical device capacity
  is the production limit; there is no artificial utilization ceiling.
- Directed graphics, integrated 68030 graphics, SDRAM, boot, and CPU regression
  tests remain exact.
- The exact release ROM, feature parameters, build ID, router, seed, and
  floorplan are used for the final synthesis and route.
- The packed bitstream passes repeated SDRAM POST and HDMI checks on the ULX3S
  attached to `nuc`.

Do not close timing by weakening the architecture. In particular, do not lower
the 60 MHz SDRAM clock, lower the 12.5 MHz CPU clock, disable a production
feature, add a false path to a synchronous path, or accept `--timing-allow-fail`
as a release result. `--timing-allow-fail` is useful only to obtain a routed
diagnostic report.

## 2026-07-20: 60 MHz Kernel Platform v1

The production target is now 12.5 MHz CPU and 60 MHz SDRAM with no artificial
utilization cap. Tile layers are retired and the sprite count is 16. The
retained machine still includes the MC68030/PMMU, 32 MiB SDRAM, AstraHost,
Vesta, OHCI USB, HDMI, framebuffer X/Y scroll and wrap, shadow/fenced
presentation, sprites, copper, blitter, line/rectangle/ellipse/flood/pattern
drawing, and active-surface protection. HDMI remains 60 Hz, USB remains
48 MHz, and SD remains 20 MHz.

The corrected 60 MHz simulation baseline passes the directed USB, graphics,
SDRAM, AstraHost, Vesta, POST, and kernel-entry suites. Aligned SDRAM measures
115.71 MB/s write and 114.24 MB/s read; BIST measures 115.05 MB/s; blitter copy
and fill measure 39.54 and 92.88 MB/s. Integrated normal, 16-sprite INDEX8,
and 16-sprite RGB565 scanlines peak at 505, 1103, and 1429 memory clocks against
a 1906-clock deadline. The full SPI pin-level boot reaches `POST PASS`,
`K0 ENTRY PASS`, and `KERNEL IDLE`.

The first complete tile-free 60 MHz route packed 67,211 of 83,640
`TRELLIS_COMB` cells and reached 12.580 MHz CPU, 69.691 MHz SDRAM, and
78.16 MHz USB while passing both HDMI clocks. It did not start the CPU on the
board. Post-route inspection found `GSR=DISABLED` on every packed FF, so HDL
initial values were not guaranteed at configuration completion. The retained
control image still passed through the same NUC loader and UART path; the
candidate was rejected and persistent flash was not touched.

The root cause was synthesis ordering: an early `proc; opt` ran before the
ECP5 primitive library was loaded and removed the input-only top-level `GSR`
instance. Removing that redundant pre-pass lets `lattice_gsr` resolve the
flattened top. Two reset-release modules intentionally retain hierarchy, so
the flow then uses Yosys `setparam`, not `chparam`, to enable their mapped FFs
while retaining exactly one physical GSR primitive. `check_por.py` now rejects
zero or multiple physical GSRs and scans every retained module; the release
tests cover both failure modes. The controller's `SDRAM_MHZ` default was also
corrected from 75 to 60 so initialization, command timing, and refresh are
computed from the real clock rather than stretching the refresh interval.

The corrected Beast mapping artifact
`astra_release60-gsr-setparam-synth.json` has SHA-256
`3bd53384ef0cb812458e9e2431d033d6324c5a7290186c5b7633e1bed3672cab`.
Yosys 0.64+159 reports zero SCCs, 53,295 LUT4s, 25,409 synthesized FFs,
5,069 CCU2Cs, 104 DP16KDs, and 19 multipliers. nextpnr packs 66,765
`TRELLIS_COMB` cells before placement. Its strict Beast seed-4 route passes all
seven clocks at 13.454604 MHz CPU, 68.526001 MHz SDRAM, 82.925613 MHz USB,
128.452148 MHz SD, 38.880249 MHz board, 58.910160 MHz pixel, and
231.803421 MHz HDMI shift. The routed JSON SHA-256 is
`2ad4029d14bc98b85def0662dd01956e6729dc923b50cf66cad164058cd352d3`;
the report SHA-256 is
`56b72faf3358846e39fbf98a5c83f3be564bec0ba22745eb76041671570437a3`;
and the production bitstream SHA-256 is
`993a526bff26d0955d50896dd050ba4fb0916ed74de5f909c8962b80746a6af3`.

That timing pass is not a hardware pass. The checked route-probe transplant
changed exactly `rom.0.0` and `rom.0.1`, corresponding to physical BRAM blocks
32 and 33, and produced bitstream SHA-256
`66ce82887c34655bb455a776641ba2a53a942fb1c994a12cca31ce4b0d4406be`.
It emitted zero UART bytes after an SRAM load on NUC. The retained known-good
`astra_post-v0_3-12m5-seed4.bit` control then passed complete POST in 1.064 s
through the same loader, board, and UART path. The board path is therefore
healthy and the corrected route is rejected for production use. Persistent
flash was not touched and the known-good control was restored to SRAM.

Post-route inspection identifies the weak boundary. The 74.32 ns CPU path
starts at TG register index `rdindex_a[3]`, crosses architectural register
data, logical-address carry logic, the external data-cache lookup/return, and
TG commit/next-state control. Its 13.4546 MHz result leaves only 7.1% physical
margin over the real 12.5 MHz CPU clock. P54 removes that full-cycle
asynchronous data-cache return: data-cache hits are captured while TG remains
stalled and acknowledged one CPU clock later; instruction-cache hits retain
their existing immediate response. This changes no Motorola-visible behavior
or production clock. Historical isolated measurements showed about 6% on a
mixed cache workload; actual software cost depends on data-cache hit density.

The complete P54 platform passes the full CPU coretest, full SDRAM boot through
`POST PASS`, `K0 ENTRY PASS`, and `KERNEL IDLE`, every directed graphics test,
and all integrated normal/INDEX8/RGB565 workloads. BIST remains 115.03 MB/s at
136,733 clocks and the graphics maxima remain exactly 505, 1103, and 1429
SDRAM clocks per scanline. The independent route-probe simulation also passes
with an advancing CPU-cycle record.

The P54 wrapper SHA-256 is
`ec8f1e5b9a09f38a3ea102fe3e01c20abb1079e38c904ffa97d140cea89447f9`.
Canonical Beast synthesis for build ID `0x60000002` has zero SCCs, enables GSR
on all 25,419 mapped FFs, and reports 52,728 LUT4s, 5,080 CCU2Cs, 104 DP16KDs,
and 19 multipliers. The mapped JSON SHA-256 is
`57607fc2de060ef0495b318759c8edff3741a4e5c2ecea413340c6ad7d4f3166`.
cells. The first Beast seed-4 route passes every production clock at
12.833676 MHz CPU, 62.169727 MHz SDRAM, 82.406258 MHz USB, 113.378685 MHz SD,
40.278728 MHz board, 61.481712 MHz pixel, and 266.880157 MHz HDMI shift. It
fails the intended 14 MHz CPU guardband and is not a hardware candidate.

This result exposed a build-control error in the guardband experiment. The
diagnostic SDC SHA-256
`e0885788663b4ab5b633f84d94d6bbe0ed182fc7e1a6c43f6710c1b3b4fa49fc`
constrained the final report to 14 MHz, but `mkbit.sh` still passed its default
`--freq 12.5` to heap placement. The placer therefore optimized the CPU for
12.5 MHz, not 14 MHz. NUC seed 57 continues as independent placement diversity,
but it inherited the same placement target and is diagnostic only unless its
measured route independently clears 14 MHz. The corrected Beast build sets
`TARGET_FREQ_MHZ=14` for both placement and routing. Its first placement startup
then exposed a second override: `astra_soc.lpf` explicitly declared the CPU net
at 12.5 MHz, so nextpnr still reported `constraining clock net 'clk' to 12.50
MHz`. That placement was stopped immediately. The true guardband LPF changes
only the implementation constraint to 14 MHz, has SHA-256
`500ad2af71863f8cc7d8d1406414c859dba720fb840506b91eb7113cad7df2bf`,
and nextpnr now reports the CPU placement constraint as 14.00 MHz. The generated
CPU clock remains 12.5 MHz and SDRAM remains 60 MHz.

Re-running placement on the unchanged P54 netlist with the corrected 14 MHz
LPF and SDC produced the same placement checksum, `0x8e180abb`, as the earlier
12.5 MHz-started run. A diagnostic CPU box at `(62,8)..(112,52)` matched 5,057
cells and changed the checksum to `0x38b77de8`, but that route was stopped after
the actual structural path was isolated. The 77.92 ns P54 CPU path spent 57.47
ns in routing and ran from the active-framebuffer address/range comparisons
through BERR, acknowledge, and clock-enable control back into TG state. That
same-cycle protection return, rather than the 60 MHz SDRAM domain, was the
measured blocker.

The guarded candidate captures the prospective active-framebuffer CPU write
fault during TG's address-setup phase and holds it through the active bus
cycle. BERR still arrives before any SDRAM side effect, but the range compare
can no longer return through TG's same-cycle clock-enable cone. The focused
integrated test configures a real INDEX8 front surface, verifies exactly one
guard-generated BERR, skips the faulted data cycle, and proves the protected
word remains unchanged. Full CPU coretest, full boot through `KERNEL IDLE`, all
directed graphics, normal/INDEX8/RGB565 integrated graphics, USB OHCI, Vesta,
AstraHost runtime/service, both SDRAM bridge implementations, and route-probe
simulation also pass. Graphics maxima remain 505, 1103, and 1429 clocks and
BIST remains 115.03 MB/s.

The `astra_soc.sv` SHA-256 for this boundary is
`163bc884231158db84fea65df19537bfef26233ee65d3b38e0ee223574c44563`.
Canonical Beast synthesis for build ID `0x60000002` has zero SCCs, enables GSR
on all 25,424 mapped FFs, and reports 53,190 LUT4s, 25,420 synthesized FFs,
5,034 CCU2Cs, 104 DP16KDs, and 19 multipliers. The mapped JSON SHA-256 is
`d2f9db04157ecacab6d1f3b451e3f263556cdca2e6d51662a9c9364896e4df4c`.
The exact full-feature Beast seed-4 placement uses 14 MHz in both LPF and SDC,
packs 66,566 TRELLIS_COMB and 25,453 TRELLIS_FF cells, and completes with
checksum `0x88a327e1`. The placed JSON SHA-256 is
`21aa6b5d06d5941cf1fb2ee28e395637ba1a344a812e8b94192228966772a89e`;
the placement report SHA-256 is
`b9ed1a46c08e8171e177de9321bece79bb9ade8fb0e73b7fd673fd620e4a9e8d`.
Unrouted estimates are 12.58 MHz CPU and 43.35 MHz SDRAM and are not acceptance
results. The strict route input has SHA-256
`491a63bd67cd03433e9cb2ce41b0dcc74b68d10a29e288573800c90899c9ce79`;
The corrected Beast route reuses that immutable placement without a timing
waiver. The superseded pre-guard NUC diagnostic route was stopped once the
guarded placement existed.

The independently synthesized route-probe mapped JSON has SHA-256
`b1b38443c8c9daca9c73a42295cda7c0463341758a31f8b5fb059fd36fba6d9f`
and exactly matches production resources and SCC/POR results. Its route-probe
hex SHA-256 is
`62a4e4b9ec2a27ceb58f412b87a0ef4e1bb4fc3d25ec6e681cfc5de68e246aa9`.
Only a strict route and repeated NUC hardware checks can promote the candidate.

## 2026-07-21: split-route ECP5 LUT-permutation defect

The guarded P54 route was statically clean but did not begin its first CPU bus
cycle on hardware. Controlled text-configuration edits, without resynthesis or
rerouting, isolated the failure to carry cell
`$nextpnr_CCU2C_651$CCU2_COMB0` at `X29/Y52/SLICEA.K0`. That cell has logical
INIT `0x000A` and its A input is tied high. The route connected physical D0 to
logical A0, and the emitted configuration contained:

```text
SLICEA.K0.INIT 0000111100000000
```

Changing that one word to `0000111100001111` made the TG68K core reach its
first bus transaction. The current diagnostic bitstream SHA-256 is
`deb4ef9de5c3eb9ee5a3387b3c99519e74fe77ed860bf20f432261d5759ada33`.
On the ULX3S attached to NUC, the original route showed the two-LED pre-bus
state; the corrected image repeatedly shows the intentional all-eight-LED
flashing BUS_ASSERT marker. This is proof of the failed carry feed, not a POST
or production-boot result. Persistent flash remains untouched.

The defect is in the split nextpnr flow, not TG68K reset or bus sequencing.
With Beast nextpnr `0.10-45-g98c18d7f`, JSON import calls `bindBel()` before
ECP5 `assignArchInfo()` reconstructs `combInfo.flags`. `bindBel()` consequently
caches `LutPermRule::ALL` for every reloaded slice. Router1 can then use input
permutations that ECP5 normally forbids: CCU2 permits only swaps within the
A/B and C/D pairs, while distributed RAM permits no LUT input permutation.
Generic bitstream LUT permutation does not preserve those mode-specific
semantics.

The rejected routed JSON SHA-256 is
`3c1705552894a64d55e875e0f57916efc47701a0f0dbfc15b3688779d190101d`.
The new `check_ecp5_lut_permutation.py` release gate rejects it with 12,184
CCU2 and distributed-RAM violations, proving that the one observed TG failure
was one instance of a route-wide correctness problem. Every occupied physical
slice in this placement contains one mode only: 6,228 CCU2 slices, 300 DPRAM
slices, 150 RAMW_BLOCK slices, and 29,366 ordinary logic slices.

`refresh_ecp5_lutperm.py` is now a mandatory `--pre-route` hook. It rebinds all
placed `TRELLIS_COMB` cells after architecture metadata has been reconstructed,
restoring the intended per-slice rules. `mkbit.sh` runs the independent routed-
JSON gate before timing checks or `ecppack`, and the release manifest records
that the refresh was active. The OSS release suite covers a legal CCU2 A/B
swap, an illegal cross-pair CCU2 route, and any distributed-RAM permutation.

The first corrected Beast router1 experiment reused the exact 66,566-cell P54
placement and refreshed all 66,566 combinational cells. It began with 280,492
unrouted arcs, fell to about 70,300, then oscillated around 72,000 to 73,000
after roughly 900 seconds. It was deliberately stopped after the high-density
route appeared to stop converging; the remote CAD PID was terminated and
verified gone. No routed artifact was retained. Subsequent line-by-line
comparison proved that stop was premature: at 899.87 seconds the earlier
completed router1 route still had 73,969 arcs remaining, then eventually
finished after 4,820.49 seconds. The matching 900-second trajectories neither
prove nor disprove routability with restored rules. Do not repeat the short
cutoff on this placement.

A corrected router2-alt experiment then used all seven exact release clock
constraints and refreshed the same 66,566 cells. It reduced overused resources
from 70,547 at iteration 1 to 26,070 at iteration 29 with zero architecture
failures. It was stopped without an output artifact after the historical
router1 comparison established a proven completion strategy and earlier
same-density router2 runs showed long nonconvergent plateaus.

The exact corrected router1 run then reused route-input SHA-256
`491a63bd67cd03433e9cb2ce41b0dcc74b68d10a29e288573800c90899c9ce79`,
refreshed all 66,566 combinational cells, and completed normally on Beast with
nextpnr `0.10-45-g98c18d7f`. The route required 7,924.67 seconds and finished
with checksum `0x1c45ca62`. Its original 100-minute watchdog was suspended at
79 minutes while only the watchdog process was stopped; the nextpnr child
continued unchanged through `Routing complete` and `Program finished
normally`. This is operational evidence that a fixed 100-minute cutoff is too
short for the protected high-density route, not a router waiver.

The protected-LUT gate passes 13,356 checked cells and 17,583 routed inputs
with zero violations. The route passes every exact clock at 14.544609 MHz CPU,
66.409882 MHz SDRAM, 74.755180 MHz USB, 112.069931 MHz SD, 44.397087 MHz board,
55.803570 MHz pixel, and 315.059845 MHz HDMI shift. It packs 66,566
`TRELLIS_COMB`, 25,453 `TRELLIS_FF`, 104 `DP16KD`, and 19 `MULT18X18D` cells.
The retained artifact hashes are:

| Artifact | SHA-256 |
| --- | --- |
| routed JSON | `596da5766b792bb54cad03175043afe0d91a1d56504a9ad96211fee630a9f54e` |
| text configuration | `4cd3c2c8101b2c0a4cd0b8dd3ca3226a6a0f7060b505c710e0bf2f05f7be30fb` |
| route report | `73c210043596d6bd68a506f38273aff05545da3b808db92860d1005df43e50e6` |
| route log | `e1b0c89b44079b051ab7df5416ba85c75f5e9abbb6806529af341ebe0fbe8ca7` |
| production bitstream | `ded87a3e3c5daef55d82e280f71d05d8a605be3e1e2135ec59a2a285072dc870` |

The BRAM-only route probe changed exactly production ROM cells `rom.0.0` and
`rom.0.1`, physical BRAM blocks 32 and 33. Its bitstream SHA-256 is
`a77b6fe66cb9fc4ef64d185986afa967d1588fa69e89707385c9a69d61d1c4fc`.
On the NUC-attached ULX3S it emitted the complete record
`ASTRA ROUTE PROBE id=56535441 sys=0000003F mem=00000000 err=00000000
host=00000080 cycles=013840DE`, proving reset, TG68K execution, the first and
subsequent bus cycles, Vesta MMIO, and diagnostic UART on the legal route.

The first production-ROM load then exposed a firmware POST error rather than
an RTL or route error. The one-row 64 KiB Astraea benchmark programmed
`BLIT_DST_PITCH=65536`, but pitch is a 16-bit hardware field. Astraea correctly
returned `DONE | INVALID_CONFIG`, status `0x00000102`. The ROM now programs
zero pitch for those one-row fill/copy commands, where pitch is unused. The
replacement `/ASTRA68.ROM` has package CRC32 `b645d379` and SHA-256
`28966f4c0a311382662ff3dc573952f74d8986b563eb5ebb263234afa780017e`.
The one-shot maintenance path validated and atomically replaced only that file
on the existing 256 GB card, then normal read-only AstraHost firmware SHA-256
`8d4cda1f0289f1fea1da46a028aa2dbbc60c7467f42e1b017fcfdd033cb73d52`
was restored.

The unchanged production bitstream subsequently passed three complete SRAM
boots. Two independent automated reloads completed in 1.614 and 1.595 seconds;
a final strengthened gate completed in 1.612 seconds while requiring FPGA
build ID `0x60000002`, ROM CRC32 `B645D379`, `POST PASS`, and `K0 ENTRY PASS`.
Every run passed SDRAM initialization, front-panel, byte/address, cache,
Astraea fill/copy, stage-0 full-range BIST, kernel image, VBR, 100 Hz timer,
AstraHost runtime, and input queue checks. Persistent FPGA flash is still
untouched: the test ROM was built from a dirty source snapshot and reports an
unknown embedded-kernel Git identity, and no HDMI capture device is currently
enumerated on NUC. Commit/rebuild provenance and physical HDMI confirmation
remain release gates even though the legal route and board boot are proven.

## 2026-07-22: P55 AstraHost ownership cone cleanup

The legal P54 route's worst SDRAM path is 15.058 ns. It starts at
`host_boot_busy_mem` near `(30,73)`, crosses the AstraHost boot/runtime memory
arbitration, and reaches `runtime_dma_position[4]` near `(32,85)`. Only 1.47 ns
is logic; 13.59 ns is routing. Boot DMA and runtime DMA are protocol-mutually
exclusive, but the runtime ownership expression redundantly depended on the
high-fanout boot lock and carried that signal into the runtime request and
response progress cones.

P55 makes runtime ownership depend only on the registered runtime lock and
adds a simulation-time fatal assertion if boot and runtime locks ever overlap.
This preserves every legal transaction while removing `boot_busy` from the
runtime DMA datapath. A mapped-netlist graph check detects the four-cell path
in the P54 routed JSON and finds no combinational path in P55, providing a
positive-control-backed structural check rather than relying only on RTL
inspection.

The focused AstraHost service test passes under both Icarus and Verilator. The
complete pin-level AstraHost boot reaches `POST PASS`, `K0 ENTRY PASS`, and
`KERNEL IDLE`. Directed controller, blitter, copper, draw, chip, sprite, and
25-frame Vega tests pass, as do the integrated normal, INDEX8, and RGB565
graphics workloads. P55 remains a 60 MHz checkpoint; no production clock was
changed.

Canonical Beast synthesis for build ID `0x60000003` reports zero SCCs and GSR
enabled on all 25,421 mapped FFs. It uses 52,615 LUT4s, 25,417 top-level
`TRELLIS_FF` cells plus four retained reset FFs, 5,075 CCU2Cs, 104 DP16KDs,
and 19 multipliers. This is 575 fewer LUT4s than P54. The AstraHost service
source SHA-256 is
`25521b83818e4bb6b577418f5309f6867155c72559acfc81a1c16a7158c287da`;
the mapped JSON SHA-256 is
`75091050a11bb7b0250f5eadf0e045e96d369c9a2e16732354cdbc7272beaf41`;
and the Yosys log SHA-256 is
`4b3af1cac554646e64801d5563c41cd949235a64d42272b0838140a94c89dd62`.
P55 was subsequently rebuilt from committed source
`e9fb3e20a27100dd1ea9e4456365788db233e05d` with the exact release ROM and
nonzero build identity. Beast Yosys `0.64+159` emitted mapped JSON SHA-256
`4d87da24b192014ef1a0febcd8fbc1d0840f5733201e1042343ff7764496e97b`.
The protected split router1 flow completed with checksum `0x7b2e5fbc`, zero
protected-LUT violations, 66,095 `TRELLIS_COMB`, 25,450 `TRELLIS_FF`, 104
`DP16KD`, and 19 `MULT18X18D` cells. Every exact release clock passes:
14.09 MHz CPU, 64.02 MHz SDRAM, 78.73 MHz USB, 101.26 MHz SD, 45.17 MHz
board, 54.60 MHz pixel, and 318.88 MHz HDMI shift. The routed JSON SHA-256 is
`fd020246625875a4cbc5c59afe904eb5442cb64f1cfeadfa08373b7c1c3ee089`;
the text configuration is
`b0973f494c90c861bce06e989b93e0c6c02e29457332bc7b9c7394bb937c101d`;
and the unmodified production bitstream is
`2005a21db790491fbffa69a707e3607bd2f3c11050747c03b8d4df1588723fd4`.

Repeated NUC SRAM loads of that route pass the complete UART gate with FPGA
build `E9FB3E20`, ROM CRC32 `E2B97D4A`, full 32 MiB POST/BIST, Astraea DMA,
`POST PASS`, and `K0 ENTRY PASS`. HDMI timing and the POST background were
stable, but the normal sparse font produced no visible glyph pixels. This is
therefore a hardware-booted and timing-clean route, but not yet an accepted
release image.

The HDMI failure was isolated without synthesis or routing. A direct stage-0
video probe repeatedly wrote and read `ASTRA VIDEO PROBE` through the real
Vega text aperture while reporting the expected Vega ID and capabilities over
UART. Replacing only font BRAM initializer blocks 101 through 104 with all
ones made the complete console white. An address-coded font populated across
all four 2 KiB banks produced the expected 90x30 grid, and an all-`A` text RAM
produced the expected `0x41` stripe pattern. Conversely, forcing only bank-0
glyph `A` solid while every text cell contained `A` remained dark. Banks 1 and
2 contain nonzero `A` bits in every two-bit slice; only bank 3 is all zero.
The four P55 font BRAM slices therefore read the effective bank-3 initializer
contents even though RTL selects bank 0. P54 placed the same initialized font
BRAMs on the Y22 EBR row and displayed them correctly; P55 placed all four on
the Y46 EBR row. The correct source fix is to remove this dependence on unused
constant high font-address bits rather than compensate software or accept a
route-specific data permutation.

As a diagnostic recovery only, copying CP437 bank 0 into bank 3 of the exact
P55 production configuration changes only BRAM blocks 101 through 104. Its
configuration SHA-256 is
`dc5676a51aeac7bc8d96fe0a4ea003a2ef5cb4b67a44f68fa38dadb51ea59d7f`
and bitstream SHA-256 is
`a9135f8a398806c1f0b52ad4fd9333240e35e216f1905c1a45bbf675f3384b21`.
That image passed the complete hardware gate in 1.598 seconds and was
temporarily loaded in volatile FPGA SRAM to confirm the diagnosis. It is not a
source-level fix and has been superseded by the exact `B1F9E60D` image.

The source correction now bounds both `post_fonts.hex` and `font_rom` to the
single 2048-byte CP437 bank that the console addresses. The lookup uses all 11
logical address bits directly. A focused dual-clock renderer passes with an
explicit 2048-byte depth assertion. The independent route-probe simulation
passes CPU text-aperture readback, retained text RAM, and real foreground pixel
observation. The complete HDMI-enabled AstraHost simulation passes full POST,
SPI ROM handoff, kernel entry, and foreground pixel observation; it completed
in 1602.393 seconds on Beast.

Beast synthesis of the complete feature set with diagnostic build ID
`0xF07F0001` reports zero SCCs, GSR enabled on all 25,420 mapped FFs, 52,565
LUT4s, 5,099 CCU2Cs, 101 DP16KDs, and 19 multipliers. The font maps to exactly
one `DP16KD` in 2048x9 mode, and its 11 logical address bits drive 11 unique
physical address pins. `check_post_font_rom.py` now enforces that invariant
before placement; all 29 OSS release-flow tests pass. The mapped JSON SHA-256
is `78bc466da94233d2bc322da8268c1eb19b81ed985f613e2520b113ff626f26e7`
and the Yosys log SHA-256 is
`b7aca220d9a87fe1b401144c2f55f437ff9b0e16fad2112de58e36520899dc9d`.
This retained checkpoint is synthesis-only from an uncommitted diagnostic
snapshot. It proves the structural correction but is not route or release
evidence.

## 2026-07-22: B1F9E60D exact font-corrected release

Beast built a fresh immutable clone of committed source
`b1f9e60d388082a5f10e044ef3f7f94e8eee4d70` with the exact release ROM,
nonzero build identity `B1F9E60D`, CPU divider 0, 60 MHz SDRAM, seed 4, heap
placer, timing weight 20, plain router1, and the complete production feature
set. Yosys `0.64+159` maps 52,565 LUT4s, 25,420 FFs, 5,099 CCU2Cs, 101
DP16KDs, and 19 multipliers with zero SCCs and deterministic GSR coverage.
The font gate finds exactly one 2048x9 `DP16KD`, all three unused physical low
address pins constant, and all 11 logical address bits live and unique.

The strict split route completed normally without a timing waiver and passed
the protected-LUT gate over 13,508 protected cells and 17,715 protected
inputs. Route checksum is `0xd6904812`. Final packed use is 66,093 of 83,640
`TRELLIS_COMB`, 25,449 `TRELLIS_FF`, 101 of 208 `DP16KD`, and 19 of 156
`MULT18X18D`. Every exact clock passes:

| Clock | Constraint | Achieved |
| --- | ---: | ---: |
| CPU | 12.500000 MHz | 13.646847 MHz |
| SDRAM | 60.002399 MHz | 65.789474 MHz |
| USB PHY | 48.000767 MHz | 72.072075 MHz |
| SD | 20.000000 MHz | 120.816711 MHz |
| board | 25.000000 MHz | 40.154194 MHz |
| pixel | 27.000029 MHz | 56.271454 MHz |
| HDMI shift | 135.025650 MHz | 337.381927 MHz |

The limiting 15.200 ns SDRAM path begins at AstraHost service state bit 14
near `(2,77)`, crosses the service-state output mux, and ends in its register
bank near `(6,78)`. It has 1.466 ns margin to the exact SDRAM period. The
limiting 73.277 ns CPU path remains inside TG68K, from `rdindex_a[0]` through
logical-address/index logic to imported core state, with 6.723 ns margin to
the 80 ns CPU period. Neither is a failed cone; both are retained as the next
measured optimization boundaries.

Release identities are:

- synthesis JSON SHA-256
  `1b0c523ddda514404344f709b61b32938cb012c507ed467d1aadb39bdfd7a29f`;
- routed JSON SHA-256
  `a4e3b80a98127ded53e8c55fccfc139238f9d7b680d342daa42e0841f21a23ef`;
- configuration SHA-256
  `ac600c7c8fb4dfca5fea899827603ab3184c7d8b2a6dee082422f969e162868c`;
- bitstream SHA-256
  `05b9e84d2413c9390163a38f77c4d8ad08600a6adb619e69ebb25c56ae0e4eae`;
- `/ASTRA68.ROM` SHA-256
  `2693a912e98a0fc1211b54b62dd80f8bed0544a3ac904d5b24d320c2be986423`,
  payload CRC32 `CEAFEEE9`.

On NUC, a one-shot AstraHost maintenance build atomically replaced only the
managed `/ASTRA68.ROM` file on the existing 256 GB card and reported the exact
`CEAFEEE9` payload CRC. Normal read-only AstraHost firmware was then restored.
The normal and provisioning application binaries have SHA-256 identities
`9e471a9b12963c3bcb8d51bd03f8b0eb339eef5eb1ec1c88028c96539bc1db3d`
and `dc1e79c1eb248c4baa7f090ec25fdc13ee02efa4b4e747338eaea048bc2dc76c`
respectively.

Three consecutive FPGA-only SRAM reloads of the exact bitstream completed in
1.604, 1.582, and 1.593 seconds after loader exit. Every run required build
`B1F9E60D`, ROM CRC32 `CEAFEEE9`, full 32 MiB POST/BIST, Astraea DMA,
`POST PASS`, and `K0 ENTRY PASS`; all passed. This also proves that normal
AstraHost re-identifies and re-serves the ROM after FPGA reconfiguration.
Persistent FPGA flash remains untouched until the exact SRAM image receives
physical confirmation of normal CP437 POST text over HDMI.

## Canonical synthesis configuration

Timing experiments must start from the production feature set:

| Parameter | Value |
| --- | ---: |
| `CPU_CLK_DIV_BIT` | `0` (12.5 MHz) |
| `SD_BOOT_ENABLE` | `1` |
| `ASTRA_HOST_ENABLE` | `1` |
| `ROM_WORDS` | `1024` |
| stage-0 image | 509 words at the P36 checkpoint |
| synthesis mapping | checked-in Yosys ECP5 flow with `-abc2` |

The ROM contents and `SOC_BUILD_ID` affect logic mapping. A zero build ID is
acceptable for iteration, but it is not timing proof for the release image.
After the source commit exists, rebuild and reroute with the real ROM identity.

`astra_clocks.sdc` must be supplied to both placement and split routing. PLL
generated-clock constraints are not preserved when a placed JSON is saved and
loaded by a later nextpnr process.

### Reproducible release flow

The P48 promotion resolves the earlier entry-point and identity hazards:

- `astra_soc.sv`, `sw/boot/Makefile`, and `mkbit.sh` now default to divider 0,
  12.5 MHz CPU, seed 4, heap timing weight 20, plain router1, and the
  measured critical floorplan.
- `mkbit.sh` performs the proven split placement and routing sequence. Placement
  may retain a timing estimate with a waiver; final routing has no waiver and
  packages no bitstream when diagnostic `PNR_TIMING_ALLOW_FAIL=1` is selected.
- `BUILD_CONFIG` includes every supported synthesis, placement, floorplan,
  router, resource-profile, and timing-ripup control. Stage-0 sources are part
  of the identity, and changing any retained control changes the build ID.
- A successful build manifest records the source revision, host, exact tool
  versions, complete configuration, and hashes of stage 0, the system ROM,
  synthesis, placement, route, reports, packed configuration, and bitstream.
  A packageable SD-boot build requires `ASTRA_SYSTEM_ROM` so `/ASTRA68.ROM`
  cannot be omitted from that evidence.

These flow changes are committed in `db606335e3a707dfd33eee9883306ca20bfab549`
and make the release rerun reproducible. They do not make the zero-build-ID P48
route release evidence. The exact nonzero-ID attempt is recorded below and was
rejected without packaging a bitstream.

### Toolchain and host identity

The build host and OSS CAD Suite revision are part of the experiment. As of
2026-07-15, the installed tools are:

| Host | Yosys | nextpnr-ecp5 | Tool path |
| --- | --- | --- | --- |
| Mac | `0.64+68` (`413169663-dirty`) | `0.10-33-ge6ecd8fa` | shell `PATH` |
| `nuc` | `0.64+68` (`413169663`) | `0.10-33-ge6ecd8fa` | `/home/barry/oss-cad-suite/bin` |
| Beast | `0.64+159` (`5197b9c8c`) | `0.10-45-g98c18d7f` | `/home/barry/oss-cad-suite-install/oss-cad-suite/bin` |

P38 was synthesized on Beast, then the immutable JSON was placed and routed on
all three machines. A seed routed by different nextpnr revisions is useful
diversity, but it is not a controlled same-seed comparison. Record both Yosys
and nextpnr identities in the final build manifest and use one pinned toolchain
for the release-identical rerun.

The Mac also has Homebrew `m68k-elf-gcc` 16.1.0 as of 2026-07-15. Invoke Mac
software builds with `CROSS=m68k-elf-`; Beast's `m68k-linux-gnu-` toolchain
remains the release compiler. Never transfer generated executables from a Mac
workspace into Linux test trees. One NUC conformance run inherited Mach-O
Musashi and GHDL targets, failed immediately with `Exec format error`, then
passed 90 unit tests, all 28 shared matrix cases, and both Harte smoke targets
after those outputs were cleaned and rebuilt natively.

The ULX3S is attached only to `nuc`. Use Beast for long synthesis/place/route
jobs, transfer immutable artifacts with `rsync`, and perform packaging,
flashing, HDMI capture, and hardware checks through `nuc`. When running split
place/route jobs remotely, use absolute paths for the JSON, LPF, SDC, pre-place
script, report, and log; this avoids depending on the SSH login directory.

Closing or killing the local command session is not proof that the remote CAD
child exited. A rejected P45 static route survived its closed session and
continued competing with the active P46 route on Beast until found with
`pgrep -af nextpnr`. Audit the remote process list before and after long jobs;
match the full artifact path before terminating anything so an active retained
candidate is never mistaken for an orphan.

Likewise, retain every command-runner session ID and poll that exact session to
an exit status. During P48 validation, an integrated simulation continued on
Beast after its local wrapper returned without showing the retained session;
the test had completed successfully, but it had to be rerun with an explicitly
retained session and log to make the evidence trustworthy. Never infer remote
completion from a quiet wrapper. Require the runner exit status plus the
expected report, log, or output hash.

Do not clone a mutable build directory with `rsync --link-dest` or hard links.
Yosys and the ROM-staging `cp` commands overwrite files such as `astra.json`
and `rom_init.hex` in place, so a later build can silently mutate the earlier
checkpoint's inode. Use a source-only snapshot with independent generated
outputs, or explicitly unlink every generated destination before building.
Keep the synthesis JSON used by active placement jobs immutable.

For split routing, reload the placed JSON with `--no-pack --no-place` and pass
`astra_clocks.sdc` again. The generated PLL constraints are not serialized into
the placed JSON. Early SDC warnings about not finding packed `$glbnet$...` names
can occur before packing; the final placement and route report must still list
all generated clocks at their exact constraints. Never infer success from the
absence or presence of those early warnings alone.

## Measured progression

All frequencies below are post-route nextpnr results, not placement estimates.

| Checkpoint | Packed `TRELLIS_COMB` | Best CPU | Best SDRAM | Result and exposed path |
| --- | ---: | ---: | ---: | --- |
| P30 | 53,696 | pass | 68.18 MHz | Blitter configuration-size logic and sprite-priority selection dominated. |
| P34 | 54,303 | pass | 64.19 MHz | Draw command BRAM metadata mux and Vega owner/lock selection dominated. Seed 23 reached 58.33 MHz; seed 33 reached 64.19 MHz. |
| P35 | 53,825 | pass | 70.93 MHz | The prior paths were gone. Seed 23 reached 67.37 MHz; seed 33 reached 70.93 MHz, only 0.767 ns short. New paths ran from blitter FSM state into SDRAM owner and write-data logic. |
| P36 | 54,162 | 13.35 MHz | 70.55 MHz | Owner acquisition and blitter write-data selection removed both P35 paths. Seed 23 reached 70.55 MHz but its CPU placement reached only 12.20 MHz; seed 33 passed CPU at 12.71 MHz and reached 66.56 MHz SDRAM. Router2-alt seed 7 passed CPU at 13.35 MHz and reached 68.87 MHz SDRAM. The new measured paths were Draw range checks after command BRAM, Vega enqueue arbitration feeding registered lock, and sprite render-prep address generation. |
| P37 | 53,903 | 13.73 MHz | 68.63 MHz | Six Draw range checks moved to the CPU staging snapshot, and Vega lock derives queued work directly from registered occupancy. Seed 33 reached 66.60 MHz SDRAM; its path ran from Draw FSM decode through a 17-bit geometry add/sub into `emit_y`. Seed 23 reached 68.63 MHz; its path ran from encoded DMA ownership through request/ready selection into the blitter pointer enable. Both passed CPU at 13.70 MHz or better. |
| P38 | 54,321 | 13.98 MHz | 69.74 MHz | Draw prepares registered circle/ellipse operands in existing predecessor states, and system DMA ownership is registered one-hot with parallel masked request selection. Seed 23 reached 69.74 MHz SDRAM and 13.46 MHz CPU; the path ran from the live blitter phase decode into the SDRAM controller owner FSM. Seed 33 reached 64.73 MHz SDRAM and 13.98 MHz CPU; its path ran from the same blitter phase decode into `issue_dst_ptr_mem` enable logic. NUC seed 57 reached 68.74 MHz SDRAM and 12.70 MHz CPU, exposing sprite state through Vega request enqueue and queue occupancy. The targeted P37 cones are gone, but the binary 27-state blitter phase decode is now structural. |
| P39 | 54,003 | 12.75 MHz | 68.65 MHz | The direct grant-selected DMA mux recovered most of P38's area, but did not close SDRAM. Beast seed 23 passed CPU at 12.75 MHz and reached 68.65 MHz SDRAM; its path ran from Draw's registered pixel result through the glyph-source address add and validation. Mac seed 33 reached 12.11/67.47 MHz and exposed sprite state through Vega enqueue and queue occupancy. |
| P41 | 54,439 | 14.08 MHz | 62.47 MHz | Added a 15-bit registered one-hot bus-phase vector alongside the binary blitter control state. Yosys retained all 15 phase FFs and mapped 43,643 LUT4s/18,259 FFs with zero SCCs. Packing is 73 cells over the `core_graphics` limit. Beast seed 23 proved that the old live blitter-state cone was gone, but exposed a 16.01 ns registered Vega-lock-to-SDRAM-owner path. Mac seed 33 and NUC seed 57 were intentionally stopped after this area-and-timing rejection so those hosts could route P44. |
| P44 | 54,399 | 14.23 MHz | 70.90 MHz | Replaced the 15 mutually exclusive phase labels with 11 registered interface facts that intentionally overlap when one state implies several facts. All directed graphics, integrated 68030 graphics, boot, SDRAM, DMA, and kernel-entry references remain exact. Beast seed 23 exposed `rows - 1` feeding the validation multiplier; Mac seed 33 repeated the Vega-lock path; NUC seed 57 repeated sprite qualification through Vega arbitration. Packing remains 33 cells over profile, so P44 is rejected. |
| P45 | 54,345 | 13.85 MHz | 72.01 MHz | The existing row counter now holds rows after the current row and captures `height - 1` before validation. This removes the subtract from the multiplier cycle without adding state, registers, or transfer cycles. Exact directed, integrated, boot, DMA, POST, and kernel-entry references pass. Beast seeds 23 and 4 expose the chunk-count request comparator; timing ripup cannot repair it. NUC seed 57 exposes Draw's shared 48-bit ellipse ALU. Packing is 21 cells under profile, but all routes fail timing. |
| P46 | 54,191 | 14.01 MHz | 70.20 MHz | Removes the redundant `issue_count_mem < chunk_count_mem` gate from request valid. Exact tests pass and the P45 comparator cone disappears. Beast seed 23 exposes Draw glyph decode, Mac seed 33 exposes the SDRAM row-hit path, and NUC seed 57 exposes tile/Vega request qualification. P46 is rejected. |
| P47 | 53,966 | 14.09 MHz | 69.01 MHz | Glyph-only states decode the contiguous opcodes 8..11 from their two low mode bits. Exact tests pass and packing drops by 225 cells. Beast seed 23 exposes internal SDRAM target-state decode, Mac seed 33 exposes tile/Vega arbitration, and NUC seed 57 exposes shared-owner ready feedback. All routes fail timing. |
| P48 | 53,957 | 13.22 MHz | 75.93 MHz | The SDRAM core consumes the already registered target state. The first route misses SDRAM by 0.021 ns on Draw's shared ellipse ALU; same-placement timing ripup passes every clock and moves the worst path to Draw pixel-result/writeback selection. Release-identical rerun remains required. |

P36 uses 64.76% of the ECP5 fabric, 80/208 block RAMs, and 17/156
multipliers. It passes the `core_graphics` profile with only 204 packed logic
cells of profile headroom, although 29,478 physical logic cells remain. Treat
the profile margin as real: timing fixes should trade or remove logic where
possible instead of casually replicating wide datapaths.

P37 removes 191 mapped LUT4s relative to P36 while adding 14 FFs. It packs 259
fewer `TRELLIS_COMB` cells, increasing profile headroom to 463 and physical
headroom to 29,737 cells. Mapped LUT count and packed `TRELLIS_COMB` are not
interchangeable.

P38 adds 448 mapped LUT4s and 101 FFs relative to P37. It packs 54,321
`TRELLIS_COMB` cells, leaving only 45 cells under the 65% `core_graphics`
profile. That is sufficient for a timing experiment, not acceptable evidence
of comfortable remaining chipset capacity. If P38 fixes timing, reduce the
one-hot request-selection cost before treating its resource result as settled.

P39 performs that area correction without restoring encoded ownership. It
maps 43,285 LUT4s, 334 fewer than P38 and only 114 more than P37, and packs
54,003 `TRELLIS_COMB`, leaving 363 cells under the `core_graphics` profile.
The direct grant-selected mux is functionally identical because the grants are
registered one-hot. Beast seed 23 routed at 12.75 MHz CPU and 68.65 MHz SDRAM;
Mac seed 33 routed at 12.11/67.47 MHz. Area recovery alone was not a release
result.

P41 is the first explicit registered bus-phase experiment. It keeps the
five-bit control state for sequencing, updates a separate 15-bit one-hot phase
vector in the same `set_state_mem` task, and uses that vector only for the
SDRAM-facing request/lock/pointer facts that appeared in both P38 critical
paths. This is intentionally narrower than duplicating all 27 control states.
The mapped netlist contains 15 distinct `TRELLIS_FF` cells on
`request_phase_mem`; Yosys did not fold the boundary back into live state
decode. Its 358-LUT4 cost over P39 must be judged against post-route timing and
the packed resource profile. P42 replaced one final, case-local
`state_mem == ST_WRITE_ISSUE` comparison with the registered phase fact. That
functionally exact one-line change perturbed global mapping to 43,917 LUT4s,
274 more than P41, without addressing a measured external path. It was reverted
after exact directed, integrated, boot, SDRAM, DMA, and kernel-entry gates
passed. That P42 expression was removed before the P44 work.

The P41 Mac seed-33 and NUC seed-57 routes were stopped with roughly 16,000
and 8,800 unrouted arcs remaining. Do not restart them: additional seed
diversity cannot repair a netlist that is over the enforced resource profile,
and P44 preserves the useful registered-boundary experiment with lower packing.

P44 tests whether the timing boundary needs one registered bit per control
phase. It instead registers the interface truths consumed downstream:
read/copy-write/fill issue, read response, key/mask read and write issue,
key/mask bus active, memory lock, address update, key/mask preparation, and
key/mask next-element update. Several truths can be set by one control state,
so this is not a one-hot vector. The exact-source hash used for the first P44
checkpoint is `f3a23fe040620cf6b762b0523bd00f909b7d10873a9acc497d7079a109fc2911`
for `astraea_blitter.sv`; the matching `astra_soc.sv` hash is
`f82eb8a617d74cd4af6736eb4fa14ab585b858d1e96fd193f91d3b030047043a`.
The frozen Beast source passed 1506/2369/2060 integrated line-cycle maxima,
144.48 MiB/s boot BIST in 136,077 cycles, DMA fill/copy counts 210/327, and
kernel entry. Fewer registered bits did not imply fewer LUTs: P44 maps 106 more
LUT4s than P41 and packs only 40 fewer cells. Optimize measured logic, not RTL
line count or register count. Beast seed 23 routed at 14.23 MHz CPU and 70.90
MHz SDRAM. Its 14.10 ns SDRAM path starts at `cfg_dim_mem[17]`, performs the
16-bit `rows - 1` carry chain, crosses the 16x16 validation multiplier, and
ends at the validation-state register mux. This is a deep arithmetic cone, not
the P41 Vega-lock placement path. Mac seed 33 routed at 13.60/70.02 MHz and
again exposed the registered Vega lock. A controlled same-netlist, same-seed
placement constrained only eight lock-bridge cells; it moved the worst path
entirely inside the SDRAM controller but reached only 13.18/70.22 MHz. The
0.20 MHz SDRAM gain does not justify the 0.42 MHz CPU loss or a permanent
constraint. NUC seed 57 passed CPU at 13.26 MHz but reached only 68.98 MHz
SDRAM; its path ran from sprite-builder state through Vega owner selection and
request assembly into the tile tag FIFO. That is the same broad client-to-Vega
qualification boundary exposed by P38/57 and P39/33.

P45 is the measured response to P44/23. It initializes the existing
`rows_remaining_mem` register to `height - 1` during command launch, uses that
registered value as the row-offset multiplier operand, and changes row
completion from `rows == 1` to `rows_after_current == 0`. This preserves every
cycle and external result while removing the subtract-to-multiply cascade. The
exact `astraea_blitter.sv` hash is
`ba62cf35e736e49f560e394e09c55784e76e2926497e33c16da72d9c7744a0c6`;
the frozen Beast synthesis JSON hash is
`81206822dfa8bdc91ecf5b2c2e8cf2f38ff3f33ddd2c0494d1fcceb333770fc0`.
It maps 160 fewer LUT4s than P44 and packs 54 fewer `TRELLIS_COMB`, enough for a
21-cell profile pass. That margin remains too small for comfort and does not
substitute for a timing-clean route.

P46 removes the comparator that dominates two independent P45 Beast routes.
The exact `astraea_blitter.sv` hash is
`54048a48899f5cd07366b2560ba1a1a98f6b38bf63bf6d00c85fbdeff51dc104`;
the frozen Beast synthesis JSON hash is
`ddf7d330bce70e7c00b5a7cffbb601e97b7f67b8ee215626facb18131a9bdae8`.
The complete production-feature netlist uses the exact 509-word AstraHost
stage 0 and zero diagnostic build ID. Beast Yosys `0.64+159` maps 43,435 LUT4s,
18,253 FFs, 80 block RAMs, 17 multipliers, and zero SCCs. Packing uses 54,191
`TRELLIS_COMB`, 154 fewer than P45 and 175 cells inside the `core_graphics`
profile. The frozen source passes the 1506/2369/2060 integrated line-cycle
maxima, 144.48 MiB/s boot BIST in 136,077 cycles, DMA fill/copy counts 210/327,
POST, and kernel entry. Timing-weight-20 corrected-floorplan placement
estimates are 11.49/60.24 MHz on Beast seed 23, 10.87/56.28 MHz on Mac seed 33,
and 11.62/56.23 MHz on NUC seed 57. These are not acceptance results; all three
placed artifacts were routed independently. Beast seed 23 passes CPU at
14.0087 MHz and reaches 70.2001 MHz SDRAM. Its failed path starts at Draw's
registered configuration opcode, crosses the full-byte glyph-operation decode,
and ends at Draw state selection. Mac seed 33 passes CPU at 13.2642 MHz and
reaches 69.1419 MHz SDRAM. Its failed path starts at the outer controller's
registered request, crosses the selected-bank/open-row equality and
`STATE_READ_WAIT` fast-path logic, and ends at the SDRAM core's delayed-state
clock enable. NUC seed 57 passes CPU at 13.6567 MHz and reaches 68.8705 MHz
SDRAM. Its failed path starts at tile-builder state, crosses Vega's selected
owner and request qualification, and ends at a request-queue update enable.
All three routes prove the comparator removal worked, but none is a production
route, so P46 is rejected.

P47 is the measured response to the Beast P46 route. Glyph opcodes are
contiguous values 8..11, and the affected decode states are reachable only for
those validated operations. The RTL therefore carries `cfg_op_cpu[1:0]` as the
glyph mode instead of reconstructing full-byte opcode equality. The exact
`astraea_draw.sv` hash is
`6ebebac87d14507e0cc3fc288619607c0ceceb499bdd77719e9333b9d4d1e31c`;
the frozen Beast synthesis JSON hash is
`2e1e9ac38aa1198caac6f33d57348547b7c8338b1ad7e90e6fc2d857282f6846`.
The immutable source is `/tmp/astra68-p47-src1` on Beast. Beast Yosys
`0.64+159` maps 43,290 LUT4s, 18,253 FFs, 3,857 CCU2Cs, 80 block RAMs, and 17
multipliers with zero SCCs. Packing uses 53,966 `TRELLIS_COMB`, 225 fewer than
P46 and 400 cells inside the profile. Directed graphics, integrated
normal/INDEX8/RGB565 cycle maxima 1506/2369/2060, 144.48 MiB/s boot BIST in
136,077 cycles, DMA fill/copy counts 210/327, POST, and kernel entry all remain
exact. Timing-weight-20 corrected-floorplan placements complete at 11.47/54.45
MHz on Beast seed 23 and 11.25/54.17 MHz on Mac seed 33. These estimates are
not acceptance results. Beast seed 23 completes at 13.893520 MHz CPU and
66.684448 MHz SDRAM. The intended P46 glyph decode is absent. Its replacement
15.00 ns SDRAM path starts at the controller state register at `(100,49)`,
crosses combinational `target_state_r[3]`, and ends at a row-open register at
`(124,61)`, with 11.525 ns routing and 3.471 ns logic. In ACTIVATE and
PRECHARGE, that combinational target is exactly the default alias of the target
captured in `target_state_q` before the command sequence. P47 therefore worked
structurally but is rejected by its first route; independent Mac seed-33 and
NUC seed-57 routes remain useful diversity until complete. Mac seed 33
completes at 14.088277 MHz CPU and 69.008347 MHz SDRAM. Its 14.491 ns path
starts at tile-builder `state[3]` at `(72,4)`, crosses Vega's selected-owner
logic, and ends at sprite-builder `row_outstanding[6]` at `(17,2)`, with 11.330
ns routing and 3.161 ns logic. This independently repeats the broad
client-to-Vega arbitration boundary seen in P46/57; P47 does not repair it.
NUC seed 57 completes at 13.381866 MHz CPU and 67.042099 MHz SDRAM. Its
14.916 ns path starts at Astraea's registered `mem_owner[0]` at `(82,47)`,
crosses shared client ready/selection logic, and ends at blitter
`issue_dst_ptr_mem[1]` at `(69,89)`, with 12.267 ns routing and 2.649 ns logic.
The routed checksum is `0x54d52f0f`; the routed JSON hash is
`664fbb25a6748b06efad7421d66e6c1662246990608bdcc41cfa1a26fc1c04cc`
and the JSON report hash is
`293122d7a2719284a5d1c62c449b572ebf6d4bc4172ab2e66733cbe86dab3a7e`.
All three P47 diversity routes are complete and rejected.

P48 is the cycle-neutral response to that measured P47 controller path. In the
ACTIVATE next-state decision and both PRECHARGE refresh decisions,
`sdram_axi_core.v` now consumes registered `target_state_q` instead of the
equivalent combinational `target_state_r`. The exact file hash is
`5d8137d598195c605d82b4542fc50798ca9da114f5f9782fac1c69aa0ba73ba4`;
the immutable Beast source is `/tmp/astra68-p48-src1`, and its synthesis JSON
hash is `582215fe0f4d334a5b4f3285742a5766b7e7b25b48f0eace7c3d16cd498b853d`.
Directed SDRAM remains 145.30/143.64 MiB/s in 8457/8555 cycles; directed
blitter copy/fill remains 51.23/118.70 MiB/s with completion counts 1499/1294;
integrated normal/INDEX8/RGB565 maxima remain 1506/2369/2060; boot BIST remains
144.48 MiB/s in 136,077 cycles; DMA remains 210/327; POST and kernel entry
pass. Beast Yosys `0.64+159` maps 43,365 LUT4s, 18,252 FFs, 3,826 CCU2Cs, 80
block RAMs, and 17 multipliers. The final synthesis check reports zero SCCs.
Beast seed-23 placement packs 53,957 `TRELLIS_COMB`, nine fewer than P47 and
409 cells inside the `core_graphics` profile; the placed JSON hash is
`fca523c42e2ed4d30dd3bb0fbd297c4c8d8361365fa2cb27e8bede1850a3b803`.
Every enforced region passes the floorplan capacity invariant. Its 11.17/58.04
MHz placement estimate is diagnostic only. The normal router1 route completes
at 13.206027 MHz CPU and 74.889542 MHz SDRAM against 75.007500 MHz. The miss is
0.021 ns, so it is not rounded to a pass. The 13.353 ns path starts at Draw
`state[5]` at `(108,77)`, crosses five operand-selection LUTs and the shared
48-bit ellipse add/sub carry chain, then ends at `ellipse_dx[47]` at `(103,78)`;
routing is 7.540 ns and logic is 5.813 ns. The route proves that P48 removed
the targeted internal SDRAM path. A controlled same-placement `--tmg-ripup`
route then rips 65 negative-slack arcs from the first pass (WNS -0.59 ns,
TNS -27.11 ns), completes a second congestion route, and reports zero
remaining negative-slack arcs. It passes every constrained clock at
13.219120 MHz CPU and 75.930145 MHz SDRAM; the latter has roughly 0.16 ns of
period margin over the exact 75.007500 MHz constraint. The new 13.17 ns SDRAM
path starts at Draw `pixel_result_mem[10]` at `(86,63)`, crosses
`port_pixel_value` and the state/writeback mux, and ends at `(108,80)`, with
10.27 ns routing and 2.90 ns logic. Its routed checksum is `0xe94ebfca`; the
routed JSON hash is
`585e42500958f9d27bece6652b6a3301b503f8cf2805c4dadf6feeffbbac6769`
and the JSON report hash is
`8b80162c6d7fb1c729e5c462961ac146837662f6669636435659201c563ad9cf`.
This is the first complete-graphics diagnostic timing pass. It is not a
release result because the netlist has a zero build ID and the exploratory
split command is not yet the canonical reproducible flow.

The canonical release rerun uses immutable commit
`db606335e3a707dfd33eee9883306ca20bfab549`, build ID `0xe8a97ceb`, and the
exact 509-word stage 0. The source archive SHA-256 is
`a9cb4aa83f34dd7a67d38df91c4583cdceaa94069d87fccd5e028991667af904`.
The 16,604-byte system ROM SHA-256 is
`0103284a741b2808be1b0264f3017b20220813eb7df412f013ed375169b9eb18`;
its payload CRC32 is `2c62c09a`. Beast Yosys `0.64+159` maps 43,343 LUT4s,
18,254 FFs, 3,863 CCU2Cs, 80 block RAMs, and 17 multipliers with zero SCCs and
zero final check errors. The synthesis JSON SHA-256 is
`82ae511eab918e034bd03665f73f0fee19173ce4bafb15d9eb5192d8f1696e6c`.

Seed-23 placement is legal, enforces every floorplan region, and packs 54,023
`TRELLIS_COMB`, leaving 343 cells under the `core_graphics` profile. Its
checksum is `0x06699802`; the placed JSON SHA-256 is
`4afa4fb8ce6c5dad95664b1da662ceb661a363f5d69ab01e035e44419c28ee9d`.
This differs materially from the zero-ID diagnostic placement, which packed
53,957 cells and had checksum `0xe94ebfca` only after routing. The nonzero-ID
route begins timing ripup at WNS -1.81 ns, TNS -333.60 ns, and 323
negative-slack arcs, versus 65 arcs and WNS -0.59 ns for the zero-ID route.
The router then oscillates: it repeatedly reduces roughly 30,000 unresolved
arcs to 300-600 and tears the route back above 30,000. It was stopped after 57
minutes and more than 1.76 million reroutes. The 3,264-line route log SHA-256 is
`a83b9041d9637e41718df9c885c6564eba96ddf1d7737bc4c1e0c2bbd36d7ad7`.
No routed JSON, routed report, packed configuration, or bitstream exists. This
is a measured release failure and confirms the flow packages fail closed.

The direct `SOC_BUILD_ID` constant changes synthesis topology and global
placement when its bits change. P50 replaces that direct constant with 32
retained ECP5 `LUT4` cells. An initial implementation selected `INIT=0000` or
`ffff` per bit. It retained all 32 cells and equal aggregate resources, but the
set of parameterized LUT modules differed by value and shifted Yosys-generated
cell and net names throughout the design. Equal counts were not enough, so that
form was rejected before placement.

The accepted P50 form gives every cell the same `INIT=aaaa` identity function
and connects `SOC_BUILD_ID[bit]` only to its A input. Zero and `e8a97ceb`
syntheses now have identical module, cell, and net-name sets. After normalizing
only the top parameter and those 32 constant A connections, their complete top
modules compare equal. Both map 43,342 LUT4s, 18,254 FFs, 3,879 CCU2Cs, 80
block RAMs, and 17 multipliers with zero SCCs; the checked flow asserts that
exactly 32 named identity LUTs survive synthesis.

Controlled Beast seed-23 placements both pack 54,054 `TRELLIS_COMB`, 18,283
packed FFs, 80 block RAMs, and 17 multipliers. This costs 31 packed cells versus
the failed `db60633` release placement and leaves 312 cells under the 65%
profile. Every packed cell has the same BEL in both placements, and their JSON
placement reports are byte-identical with SHA-256
`b632784919297650cc9f15d3b123a41d1b435da566f913921fe53f61da891ced`.
P50 therefore passes the value-independent mapping and placement gate. Commit
it and route the exact new nonzero-ID release; no seed search is warranted.

The exact committed P50 release is
`19d7040943546bda5ad63646707a2765aa50cbe4`, build ID `0xaade208e`.
Its immutable source archive SHA-256 is
`24212560c4b3b0a48dd28e0f5ac7b1c2b0bf0df09319ae46a83dbefade9cd4ae`;
the exact 16,604-byte system ROM SHA-256 is
`0d8540ca91a6bba32f48f353cf79edc497cc91e998cd9463e3f2a5661cafcf50`.
All 90 conformance tests, the 28-case shared Musashi/RTL matrix, both Harte
smokes, directed graphics, integrated 1506/2369/2060 workloads, and full boot
gate pass. Exact synthesis maps the paired-proof resource counts above; its
JSON SHA-256 is
`82adcb3c9bae49f755563b1e20e4d83040496a86faecc77d57d20185026ff058`.
Placement again packs 54,054 `TRELLIS_COMB`; its JSON SHA-256 is
`2f418d5d51948bf368dfa8c400930d6df7f174ccf03a0ba09bfa605dec58f58c`
and its report is the byte-identical `b6327849...` paired proof.

The P50 route does **not** meet production timing. Timing ripup starts at 172
negative-slack arcs, WNS -1.54 ns, and TNS -137.33 ns. It then oscillates for
50 checkpoints between a better 85-91 arc band near WNS -0.63 ns/TNS -29 ns
and a worse 100-108 arc band near WNS -0.85 ns/TNS -50 ns. The final report
passes CPU at 14.332808 MHz but reaches only 71.556351 MHz SDRAM against
75.007500 MHz. The 13.975 ns worst SDRAM path starts at sprite-builder
`state[3]` at `(35,5)`, crosses request qualification and
`vega_i.mem_selected_owner[0]`, and ends at
`vega_i.request_count_next[1]` at `(71,24)`. It contains 11.070 ns routing and
2.905 ns logic. The route log SHA-256 is
`28dd5651a06b563c074257de8c062fb86c8e83406db0b05e34c12d729ae75a86`;
the report SHA-256 is
`70f7223fd1ff751276540ee427402c8523d3f0558f1d6c3664e85e0a3c6cdb15`.

This run exposed a release-flow defect. Placement's
`--timing-allow-fail` was serialized as `timing/allowFail=1` in the placed
JSON, then inherited by the route-only process even though
`PNR_TIMING_ALLOW_FAIL=0` supplied no command-line waiver. nextpnr therefore
returned success with a warning, and the wrapper packaged an invalid
`aa1cccb5...` bitstream without parsing the report. That image is quarantined
and was never loaded on hardware. Production routing now writes a separate
route-input JSON with the serialized waiver forced to zero, then
`check_timing.py` independently requires all six reported clocks to meet both
their reported constraints and Astra's architectural minimum frequencies
before `ecppack`. Unit tests cover waiver clearing, an all-pass report, a
missing clock, a weakened reported constraint, and the measured SDRAM miss.

P51 targets only the measured P50 cone. The sprite builder now advances two
registered request facts in lockstep with its existing 23-state controller;
simulation asserts exact fact/state equivalence. This removes live controller
decode from Vega arbitration without adding a request cycle. Directed sprite
results remain 267 requests and 209 cycles; every directed graphics result,
the integrated 1506/2369/2060 references, and full boot BIST/DMA/POST/kernel
entry remain exact. Beast Yosys `0.64+159` maps 43,172 LUT4s, 18,254 FFs,
3,852 CCU2Cs, 80 block RAMs, and 17 multipliers with zero SCCs. The registered
facts survive in the final JSON. Seed-23 placement packs 53,834
`TRELLIS_COMB`, leaving 532 cells under the active profile. Its strict timing
ripup route improved from 600 negative-slack arcs at WNS -1.88 ns/TNS
-402.40 ns to a 328-461 arc plateau near WNS -1.13..-1.16 ns and TNS
-141..-198 ns, then was stopped at the declared bound. A complete non-ripup
route passes CPU at 13.306012 MHz but reaches only 66.067657 MHz SDRAM. The
P50 sprite path is absent. Its replacement 15.136 ns path starts at tile
builder `state[3]` at `(48,6)`, crosses `vega_i.mem_selected_owner`, and ends
at a request-control FF enable at `(46,13)`, with 11.503 ns routing and
3.633 ns logic. P51 therefore fixes its target but is rejected.

P52 applies the same cycle-neutral registered-boundary technique to the two
tile stream states. All directed tests, the 12-frame video test, integrated
1506/2369/2060 references, 144.48 MiB/s boot BIST in 136,077 cycles, DMA
210/327, POST, and kernel entry remain exact. The one-hot map/pattern fact form
perturbs global ABC mapping to 43,693 LUT4s, 18,256 FFs, and 3,846 CCU2Cs;
placement packs 54,327 `TRELLIS_COMB`, leaving only 39 profile cells. Its
non-ripup route still had 22,897 arcs unresolved after 886.96 seconds and was
stopped once the smaller equivalent P53 netlist existed. P52 is rejected for
area and routability, not function.

P53 encodes the tile boundary as `stream-active + pattern-select`, factors the
shared issue and lock qualification, and retains the same two transition-
registered bits. The tile RTL SHA-256 is
`441eaa7ee0c92942c060d78f394cd6b08475d08ed6af3728c1866f84d457fa00`;
the zero-ID Beast synthesis JSON SHA-256 is
`5f5a0eff40eedca22c3616514fbb002d09b7e0bf8e326b52d031caf9e341ddf9`.
Every P52 functional and cycle reference remains exact. Beast Yosys
`0.64+159` maps 43,324 LUT4s, 18,256 FFs, 3,881 CCU2Cs, 80 block RAMs, and 17
multipliers with zero SCCs. Seed-23 placement packs 54,038 `TRELLIS_COMB`,
leaving 328 profile cells. It places registered `stream_active` at `(78,12)`
and owner-selection logic at roughly X74..78/Y14, replacing the old
state-to-owner physical span.

Beast seed-23 router1 completes after 758,686 iterations in 2588.08 seconds.
It passes CPU at 13.656725 MHz but reaches only 72.087662 MHz SDRAM. Its
13.87 ns path runs from Astraea `mem_owner[1]` into blitter
`issue_src_ptr_mem[5]`, with 11.22 ns routing and 2.65 ns logic. The routed
JSON/report/log SHA-256 values are
`dcd90af366b85867263b081bcb3405ee2ece615f3b225cbbf37385909979734b`,
`d1b133b1958ff25739c873aa9adbead332ca5330962b2526c80b7e086ac8e099`,
and `1373e9b6741bca990dc8185c82ed9fb2e2cda87c91de2a06ed49ca56099e6cdc`.
A Mac route of the same placement reaches 13.544263 MHz CPU and 72.632195 MHz
SDRAM on a different Draw clip-validation path. Inspection of its routed
settings proved that the placed JSON's serialized `router1` and RNG state
overrode the attempted route-time router and seed arguments. It was tool-
version diversity, not the intended router2 diversity.

NUC nextpnr `0.10-33-ge6ecd8fa` routes the independent Beast seed-4 placement
to completion after 857,631 iterations in 2754.47 seconds. All six clocks pass:
CPU is 12.827913 MHz and SDRAM is 77.471336 MHz. The 12.908 ns SDRAM path runs
from tile `tag_count[4]` at `(56,4)` to `vega_mem_addr[20]` at `(58,19)`, with
9.727 ns routing and 3.181 ns logic. The route checksum is `0x3fb1716e`;
route-input/routed/report/log SHA-256 values are
`ba551c79b7afca5b16363ee09fbf9c2530cdf840f6b7cd7241aae23a93480cd6`,
`2597d04961520513d69d1470f30a4698f4b4d9c5923a5046f005b2226f7873a4`,
`aacea539c43c077d682c8458c8e540efb0ec2041c69ceed6b99cd0c7dec797ab`,
and `376e5f1ec861d0ee09387c6721b28e44f6c587a12bc22f0cfae7636ba054a742`.
The independent timing and resource gates pass at 54,038 `TRELLIS_COMB`, 80
block RAMs, and 17 multipliers.

The split flow now removes serialized router-mode controls before routing so
the explicit router is honored. It deliberately preserves the post-placement
RNG state and no longer supplies a misleading route-time seed. Production
clears the serialized timing waiver; explicit diagnostic mode preserves it;
both paths are unit tested. Seed 4 is the canonical release configuration.

The exact committed P53 release is
`ca26765a6d4d198d3b37b5457a70d732f9311a72`, build ID `0x25b55c0a`.
NUC passes all 90 architecture unit tests, all 28 shared Musashi/RTL matrix
cases, and both Harte smoke gates. Directed graphics, the 12-frame Vega video
test, integrated normal/INDEX8/RGB565 workloads, and full SDRAM boot simulation
all pass. The integrated maxima remain 1506/2369/2060 cycles. Boot reports BIST
at 144.46 MiB/s in 136,097 cycles, DMA 210/327, complete POST, and kernel entry.
The exact 16,604-byte system ROM SHA-256 is
`c44c5736fa4ffdc7a0c9e1e3f20571f35eca7f9a4a6faf0fd151c79f00013659`;
its payload CRC32 is `d0dc84a4`. The 509-word stage-0 hex SHA-256 is
`7de247f66f2840b26692962118778cddf074f818f08dc61966a0e153439a1820`.

Beast synthesis maps 43,324 LUT4s, 18,256 FFs, 3,881 CCU2Cs, 80 block RAMs,
and 17 multipliers with zero final SCCs. Seed-4 placement packs 54,038
`TRELLIS_COMB`, leaving 328 cells under the `core_graphics` profile. NUC
nextpnr `0.10-33-ge6ecd8fa` completes the strict exact route and passes all six
architectural clocks at 12.703252 MHz CPU and 76.569679 MHz SDRAM. The routed
JSON/report/configuration/bitstream SHA-256 values are
`780e712f9efb87f50318f12f19ea47053e33ae95dc126647960ee694cddd0c64`,
`f57bf822b7c91dcbfb8361e5e097fd36293e5ca0def51f15ece997e3c9e0a3c1`,
`26532993367b5f3816c71919ab992512fbc649235c5582a60f4acaba7fc6abac`,
and `2b7efafe0db45f89a1d314d8f1e92ec0391b5e3433f528fffd920cbce87dc7e5`.

Static acceptance was not sufficient. Two independent SRAM loads of that exact
image produced zero FTDI UART bytes and no POST events. The retained hardware
control image `astra_post-v0_3-12m5-seed4.bit`, SHA-256
`8dd57df392cde918a7a5f8859d2dbc0fd17a41b5fa9c6528cb7e231c4a16eff8`,
passed complete POST through the same board, cable, loader, and UART checker in
1.142 seconds.

The first diagnostic repack had the intended route-probe bytes in physical
BRAM blocks 35 and 36, but comparison against the production text
configuration later showed that its `ecpbram` path also duplicated tile names
through hundreds of EBR and DSP `.tile_group` records. It was therefore not a
valid BRAM-only control. `make_route_probe_bitstream.py` now compares the
production and diagnostic `rom.*` DP16KD cell sets, widths, modes, and
non-INIT parameters, requires the production initializers to match the routed
JSON, re-emits the diagnostic initializer, and copies only the changed
`.bram_init` sections into the original text configuration. Unrelated
synthesis modules are deliberately ignored because their data is not copied
and their inventory varies between Yosys revisions. Unit gates cover
mismatched ROM mapping, malformed sections, unexpected block counts, and
preservation of original non-BRAM text.

The corrected P53 diagnostic changes exactly BRAM blocks 35 and 36. Its
configuration SHA-256 is
`e11acb5a1ef7cf1e18d8ca7968c367fadc4f70b06f298aa014a08972acec8` and
its bitstream SHA-256 is
`bb6043d2d498065579d9fd07dfd962a8b2ed107842d55e123f4185a074543426`.
It still produced zero bytes in a 15.037-second SRAM-only capture. The probe
independently passes RTL simulation with Vesta ID `0x56535441`, the reset
overlay present, quiescent disabled-service status, and an advancing CPU cycle
counter. Reloading the retained control immediately passed complete SDRAM POST
in 1.125 seconds through the same hardware path. The exact route is therefore
rejected for hardware use; it was never written to persistent FPGA flash.

The exact CPU path is 78.720 ns: register-file index `rdindex_a[3]` at `(93,23)`
crosses register data, logical address, the external cache lookup/return, and
`clkena_lw` before reaching next-microstate control at `(112,67)`. It contains
82 routed nets, 61.986 ns routing, and 16.734 ns logic, leaving only 1.280 ns
of static period margin. The exact SDRAM path is 13.060 ns from tile-builder
`stream_issue_done` at `(53,4)` through Vega owner selection to tag-FIFO write
data at `(47,5)`, with 9.879 ns routing and 3.181 ns logic. Independent Beast
and NUC reroutes reproduce the CPU path and approximately the same 12.67/76.46
MHz margin. Hardware acceptance now requires deliberate implementation margin,
not merely a report at or just above the architectural clocks.

An isolated P54a experiment consumed the legal one-hot Astraea `mem_owner`
bits directly to remove the equality comparator from the Beast critical path.
All directed graphics references, including the 12-frame video test, remain
exact. A controlled Mac Yosys `0.64+68` comparison maps P53 to 43,446 LUT4s,
18,246 FFs, and 3,857 CCU2Cs, but P54a maps 43,750 LUT4s, 18,253 FFs, and
3,850 CCU2Cs. The simpler source expression causes a 304-LUT global mapping
regression and is rejected before placement. Its `astraea_chip.sv` SHA-256 is
`b79a77e6d8fc8b03a353901b6cbe9b6656b68d91fcabc85f32ca164303204231`;
do not repeat that decode-only change.

P49 tests the next measured Draw lever without changing a cycle: replace the
six-bit `state` selection feeding the shared 48-bit ellipse ALU with the
existing two-bit ALU phase and one transition-registered X/Y step selector.
The exact `astraea_draw.sv` hash is
`c5d79c5f161a7655b388e087027f286da15d5c7c11fee2576de361546815b20d`.
Directed graphics, integrated 1506/2369/2060 line-cycle maxima, boot BIST at
144.48 MiB/s in 136,077 cycles, DMA 210/327, POST, and kernel entry all remain
exact. The exact zero-ID 509-word-stage-0 synthesis JSON hash is
`97bb80e991bac2feef8cdc9da6ddaa5d89b40ba0d22df435c99ac1d1d67c44eb`,
with zero final SCCs. However, Beast Yosys `0.64+159` maps 43,728 LUT4s,
18,253 FFs, and 3,883 CCU2Cs: 363 LUT4s, one FF, and 57 carry cells more than
P48. The smaller source expression creates worse mapped reconvergence. P49 is
rejected before placement and the RTL is reverted to P48. Functional
equivalence alone is not enough when the remaining chipset budget is a release
requirement.

The P45 physical matrix uses that one immutable JSON. Its canonical Beast copy is
`/tmp/astra68-p45-src1/fpga/soc/oss_flow/astra.json`; byte-identical copies are
`/private/tmp/p45-astra.json` on the Mac and
`/tmp/astra68-p45-route/p45-astra.json` on NUC. Every candidate uses timing
weight 20, the `critical` floorplan, and explicit enforcement of only
`host_io`, `astraea_blitter`, `astraea_blitter_cdc`, and
`astraea_blitter_control`.

The first P45 launch exposed an impossible physical constraint rather than a
slow placer. The new registered row operand made the validation
`MULT18X18D` match `astraea_blitter_control`, whose original Y60..Y88 rectangle
contained zero multiplier sites. Every pre-place report said
`MULT18X18D=1/0`, but heap, static, and SA placers continued trying to legalize
it indefinitely. The Beast, Mac, and NUC runs were stopped without a placed
artifact after this was confirmed. P44 did not expose the error because its
subtract output separated the multiplier from the directly matched row-counter
net; its multiplier legally landed at X62/Y58.

The corrected control rectangle begins at Y56 and contains 12 multiplier
sites, including the measured Y58 row. The pre-place script now rejects any
enforced region whose constrained bucket demand exceeds its physical capacity,
so this class of error fails before placement. Corrected artifact names begin
with `p45f-`. Corrected placement completed in roughly one to four minutes,
confirming that the earlier hour-long runs were impossible legalization rather
than normal congestion. Placement estimates for the immutable netlist were
11.72/59.14 MHz on Mac seed 33, 10.92/62.12 MHz on Beast seed 23,
11.53/50.79 MHz on Beast seed 4, and 11.60/51.17 MHz on NUC seed 57. The Beast
static placement reached only 10.87/37.16 MHz. These estimates are useful for
rejecting the static variant, but they are not timing acceptance evidence.

The first corrected full route is Beast seed 23, router1, report
`/tmp/astra68-p45-src1/fpga/soc/oss_flow/p45f-seed23-tw20-router1-route.rpt`.
It completes with no routing errors and passes CPU at 13.8165 MHz, but SDRAM
reaches only 71.7360 MHz against the 75.0075 MHz production constraint. The
0.608 ns miss proves that P45 removed its intended P44 path but does not close
the machine. Timing-driven ripup on the same placement repeats the exact source
and destination registers and improves only to 72.0098 MHz. Independent Beast
seed 4 repeats the comparator cone at 65.6125 MHz. NUC seed 57 reaches
66.0284 MHz on a different state-select-to-48-bit-ellipse-ALU path. P45 is
therefore fully measured and rejected. Timeout, static, threaded-pack,
parallel-refine, timing-ripup, and router2 variants remain physical
experiments, not release defaults. Route
every retained candidate with `--no-pack --no-place`, reload the SDC, and
record even failed cones here. Do not promote a faster placement merely
because it completed; only a full all-clock route and release-identical rerun
can select the production flow.

### Routed critical-path evidence

| Checkpoint/seed | Source to destination | Routing | Logic | Interpretation |
| --- | --- | ---: | ---: | --- |
| P35/23 | blitter `state_mem[3]` at `(82,87)` to SDRAM owner state at `(96,49)` | 11.210 ns | 3.630 ns | Live blitter state crossed system arbitration. |
| P35/33 | blitter `state_mem[0]` at `(81,83)` to SDRAM request data at `(99,78)` | 11.194 ns | 2.905 ns | Live state decoded across every write-data bit. |
| P36/23 | Draw command EBR at `(71,46)` to configuration-valid state at `(86,64)` | 7.400 ns | 6.774 ns | EBR output, bank mux, and high-bit equality were in one cycle. |
| P36/33 | tile-builder state at `(43,2)` to Vega registered outbound lock at `(51,33)` | 11.718 ns | 3.306 ns | Enqueue arbitration fed `request_count_next`, then lock. |
| P36/7 router2-alt | sprite render state at `(2,26)` to render-prep row address at `(35,5)` | 9.199 ns | 5.321 ns | Chunk-size, reverse-crossing, cursor selection, and `+1` remained one cone. |
| P37/33 | Draw state at `(105,74)` to `emit_y[14]` at `(89,75)` | 10.554 ns | 4.462 ns | FSM and circle/ellipse slot decode selected shared geometry operands before a 17-bit add/sub chain. |
| P37/23 | encoded DMA owner at `(96,72)` to blitter `issue_dst_ptr_mem[3]` enable at `(77,83)` | 11.922 ns | 2.649 ns | Owner decode, request selection, controller ready, and the returned blitter ready signal remained one combinational handshake path. |
| P38/23 | blitter `state_mem[2]` at `(80,65)` to SDRAM controller owner-state input at `(97,41)` | 10.745 ns | 3.593 ns | Live decoding of the binary 27-state blitter FSM drove lock/request arbitration into the controller FSM. |
| P38/33 | blitter `state_mem[3]` at `(70,67)` to `issue_dst_ptr_mem[5]` enable at `(71,87)` | 11.855 ns | 3.593 ns | The same broad phase decode drove pointer-update enables; intermediate LUTs spread as far as X96 despite nearby endpoint registers. |
| P38/57 | sprite-builder `state[1]` at `(17,12)` to Vega request-queue occupancy at `(54,21)` | 11.663 ns | 2.885 ns | Sprite request qualification crossed Vega owner selection, enqueue logic, and queue-count update. This independent NUC route passed CPU at 12.70 MHz but reached only 68.74 MHz SDRAM. |
| P39/23 | Draw `pixel_result_mem[1]` at `(93,28)` to Draw glyph-source/state input at `(112,29)` | 9.610 ns | 4.960 ns | A returned glyph descriptor offset crossed a 25-bit source-base add, range validation, and state/register selection. Beast passed CPU at 12.75 MHz but reached 68.65 MHz SDRAM. |
| P39/33 | sprite-builder `state[3]` at `(35,7)` to Vega queue/update enable at `(65,19)` | 11.300 ns | 3.520 ns | The independent Mac route again put sprite qualification, Vega owner selection, and enqueue occupancy in one cycle. It reached 12.11 MHz CPU and 67.47 MHz SDRAM. |
| P41/23 | registered `vega_mem_lock` at `(53,21)` to SDRAM owner/state input at `(96,80)` | 13.100 ns | 2.900 ns | The registered blitter boundary removed its target cone, but placement left Vega's registered outbound lock across the die from the SDRAM edge and the lock still traversed zero-cycle grant/accept logic. CPU passed at 14.08 MHz; SDRAM fell to 62.47 MHz. |
| P44/23 | blitter `cfg_dim_mem[17]` at `(61,76)` to validation state/register input at `(68,79)` | 7.581 ns | 6.524 ns | `cfg_dim_mem[31:16] - 1` fed the 16x16 row-offset multiplier in the same cycle. The registered interface facts removed the prior live-state and Vega-lock paths; CPU passed at 14.23 MHz and SDRAM reached 70.90 MHz. |
| P44/33 | registered `vega_mem_lock` at `(58,20)` to SDRAM owner/state input at `(97,62)` | 11.377 ns | 2.905 ns | The default Mac placement repeated the zero-cycle Vega-lock arbitration path and reached 13.60/70.02 MHz. |
| P44/33 lock bridge | SDRAM controller state logic at `(98,49)` to controller state input at `(116,71)` | 10.403 ns | 3.838 ns | Constraining only the eight matched Vega-lock bridge cells removed that path from the top of the report, but the replacement internal-controller path limited SDRAM to 70.22 MHz and CPU fell to 13.18 MHz. |
| P44/57 | sprite-builder `state[3]` at `(14,16)` to tile tag-FIFO write input at `(43,6)` | 11.376 ns | 3.121 ns | Sprite request qualification crossed Vega owner selection and shared request assembly before reaching the tile client. NUC passed CPU at 13.26 MHz and reached 68.98 MHz SDRAM. |
| P45/23 | blitter `chunk_count_mem[4]` at `(77,81)` to SDRAM owner/state input at `(96,40)` | 10.074 ns | 3.866 ns | The P44 subtract-to-multiplier cone is absent. The replacement 13.940 ns path crosses `issue_count_mem < chunk_count_mem` at `astraea_blitter.sv:620`, then shared request qualification and owner/state selection. Beast passes CPU at 13.82 MHz but reaches only 71.74 MHz SDRAM. |
| P45/23 timing ripup | Same registers and coordinates as P45/23 | 10.021 ns | 3.866 ns | Timing-driven ripup saves only 0.053 ns from the same structural comparator path. Beast reaches 13.85/72.01 MHz and still fails SDRAM by 0.555 ns. |
| P45/4 | blitter `chunk_count_mem[0]` at `(80,75)` to blitter/shared-request input at `(87,74)` | 11.347 ns | 3.894 ns | An independent Beast placement repeats the same line-620 comparator cone. CPU passes at 13.60 MHz, while SDRAM reaches only 65.61 MHz. |
| P45/57 | Draw `state[3]` at `(104,74)` to ellipse result register input at `(97,83)` | 8.789 ns | 6.356 ns | State decode selects operands for the shared 48-bit `ellipse_alu_result` add/sub carry chain. The independent NUC toolchain passes CPU at 13.97 MHz but reaches only 66.03 MHz SDRAM. |
| P46/23 | Draw configuration opcode at `(86,79)` to Draw state input at `(99,83)` | 11.340 ns | 2.905 ns | Full-byte glyph operation decode crossed state selection. Beast passes CPU at 14.01 MHz and reaches 70.20 MHz SDRAM. The P45 request comparator is absent. |
| P46/33 | outer SDRAM request register at `(96,49)` to SDRAM-core delayed-state enable at `(117,59)` | 10.594 ns | 3.869 ns | Dynamic bank selection, active-row equality, and the zero-cycle `STATE_READ_WAIT` row-hit fast path remain in one cycle. Mac passes CPU at 13.26 MHz and reaches 69.14 MHz SDRAM. |
| P46/57 | tile-builder `state[3]` at `(30,12)` to Vega request-queue update enable at `(23,24)` | 10.887 ns | 3.633 ns | Tile request qualification crosses `mem_selected_owner` and queue-update logic. NUC passes CPU at 13.66 MHz and reaches 68.87 MHz SDRAM. |
| P47/23 | SDRAM controller state at `(100,49)` to row-open state at `(124,61)` | 11.525 ns | 3.471 ns | ACTIVATE/PRECHARGE rebuilt the already captured target through combinational `target_state_r`. The P46 glyph path is absent. Beast passes CPU at 13.89 MHz but reaches only 66.68 MHz SDRAM. |
| P47/33 | tile-builder `state[3]` at `(72,4)` to sprite `row_outstanding[6]` at `(17,2)` | 11.330 ns | 3.161 ns | Tile request state crosses Vega selected-owner arbitration into sprite response state. Mac passes CPU at 14.09 MHz but reaches only 69.01 MHz SDRAM. |
| P47/57 | Astraea `mem_owner[0]` at `(82,47)` to blitter `issue_dst_ptr_mem[1]` at `(69,89)` | 12.267 ns | 2.649 ns | Registered ownership crosses shared client ready/selection before returning to the blitter pointer enable. NUC passes CPU at 13.38 MHz but reaches only 67.04 MHz SDRAM. |
| P48/23 | Draw `state[5]` at `(108,77)` to `ellipse_dx[47]` at `(103,78)` | 7.540 ns | 5.813 ns | Five operand-select LUTs feed the shared 48-bit ellipse add/sub carry chain. CPU passes at 13.21 MHz; SDRAM reaches 74.89 MHz and misses by 0.021 ns. The P47 internal SDRAM path is absent. |
| P48/23 timing ripup | Draw `pixel_result_mem[10]` at `(86,63)` to a state/writeback register at `(108,80)` | 10.27 ns | 2.90 ns | Timing-driven ripup passes every clock at 13.22/75.93 MHz and moves the worst path away from the shared ellipse ALU. This is a zero-build-ID diagnostic pass, not the release-identical route. |
| P48/33 | Draw `state[4]` at `(89,83)` to a Draw next-state register at `(89,84)` | 11.212 ns | 4.085 ns | Mac router1 crosses a deep state-dependent next-state mux and reaches 13.53 MHz CPU but only 65.37 MHz SDRAM. Routed JSON SHA-256 is `a36f606d...`; report SHA-256 is `72f5f526...`. The route is complete and rejected. |
| P53 exact/4 CPU | TG register-file `rdindex_a[3]` at `(93,23)` through logical address, external cache return, and `clkena_lw` to next-microstate control at `(112,67)` | 61.986 ns | 16.734 ns | The 82-net path reaches only 12.703 MHz. Static timing passes by 1.280 ns, but both the exact release and a BRAM-only route probe are silent on hardware. |
| P53 exact/4 SDRAM | Tile `stream_issue_done` at `(53,4)` through Vega owner selection to tag-FIFO write data at `(47,5)` | 9.879 ns | 3.181 ns | The exact route reaches 76.570 MHz SDRAM. Together with the CPU path this is insufficient physical margin for board acceptance. |

Coordinates vary by placement. RTL source and cone shape are the stable identity
of a path; do not floorplan from coordinates copied from another seed.

## What has worked

### Remove the logic cone, not its symptom

Each useful iteration changed the logic represented by the routed critical
path. Merely rerunning seeds did not remove a repeatable structural path.

Changes with measured benefit:

- Precompute and register blitter row-byte count, word mode, and total-unit
  count in cycles that already exist in the command setup sequence. This
  removes multiply/shift/compare work from issue-time control.
- Encode sprite first-priority masks as distributed one-hot state. This avoids
  rebuilding a serial priority chain for every selected sprite.
- Carry Draw command owner and valid metadata through the same registered BRAM
  read as the command word. Do not use a later dynamic index to recover metadata
  from the whole command array.
- Derive Vega's aggregate memory lock as the OR of the stable client lock bits.
  Re-decoding the selected round-robin owner put arbitration logic on the
  SDRAM-domain path.
- Let the SDRAM owner FSM acquire a locked client directly from its lock signal.
  A locked requester already reserves arbitration before `valid`; waiting for
  the fully selected request and data mux added avoidable logic and routing.
- Select blitter write data from command-stable one-hot mode bits. The data is
  sampled only in matching issue states, so decoding the live five-bit FSM on
  every write-data bit was unnecessary.
- Use distributed RAM for the blitter chunk buffer. It avoids a large register
  bank and dynamic read mux without consuming another block RAM.
- Preserve explicit yield points between fully retired blitter chunks. The
  display client needs bounded access without weakening blitter correctness.
- Remove a request comparator only when a registered protocol state already
  defines the exact valid window, and enforce that equivalence with a
  simulation assertion. P46 removed a repeated routed cone and saved 154 packed
  cells without changing a cycle.
- Use compact mode bits directly in operation-specific states when the command
  validator and state reachability prove the high opcode bits are invariant.
  P47's glyph decode is an exact example; this is not permission to alias
  opcodes at a general command boundary.

The general rule is to move validation, decoding, multiplication, wide compare,
and dynamic selection into an existing setup cycle, then carry a small stable
fact into the transfer cycle.

### Use a restrained floorplan

The useful P35/P36 floorplan is:

```text
ASTRA_FLOORPLAN_MODE=critical
ASTRA_FLOORPLAN_ENFORCE=host_io,astraea_blitter,astraea_blitter_cdc,astraea_blitter_control
```

The default critical plan anchors the HDMI serializer, TG68K cache, and SDRAM
controller/BIST response island near their physical resources. The explicit
regions keep only the known blitter CDC and control paths near the SDRAM side.

Draw, sprite, tile, copper, broad CPU-memory, and broad video regions remain
report-only. The reports are valuable for understanding density, but enforcing
all of them overconstrains unrelated logic and makes hard-resource placement
less flexible. Tighten a region only after a routed report identifies both
endpoints of a failing path.

When RTL registers are renamed or added, update the cell matchers in
`place_hdmi_serializer.py`. A region that silently matches zero cells provides
no floorplanning benefit; the pre-place report must show matched and constrained
cell counts.

### Separate synthesis, placement, and routing during exploration

Synthesis is deterministic for one exact source and parameter set. Save the
checked JSON once, place several seeds, and route the placed candidates in
parallel on the Mac, Beast, and NUC. This makes seed/router comparisons use the
same netlist and turns long route time into useful coverage.

Current P47/P48 route diversity is:

- P47 router1, seed 23 on Beast: complete and rejected at 13.89/66.68 MHz
- P47 router1, seed 33 on the Mac: complete and rejected at 14.09/69.01 MHz
- P47 router1, seed 57 on NUC: complete and rejected at 13.38/67.04 MHz
- P48 router1, seed 23 on Beast: complete at 13.21/74.89 MHz; rejected by
  0.021 ns
- P48 router1 timing ripup, same seed and placement on Beast: complete and
  timing-clean at 13.22/75.93 MHz; diagnostic only until the canonical,
  release-identity rerun
- P48 router1, seed 33 on the Mac: complete and rejected at 13.53/65.37 MHz

Keep `--timing-allow-fail` during diagnosis so a near miss still produces the
complete report. Remove the waiver from the release acceptance criteria.

A controlled P37 placement experiment increased
`--placer-heap-timingweight` from 10 to 20 without changing seed 57. The CPU
estimate improved from 10.36 to 11.80 MHz while SDRAM held at 53.82 MHz. On
seed 33, the same change improved CPU from 11.09 to 12.09 MHz and SDRAM from
46.95 to 56.17 MHz. P48's all-clock route now makes timing weight 20 part of
the canonical release flow; the release-identical rerun must still prove it.

## What has not worked

- Placement timing is not predictive at this density. P36 placement estimated
  the SDRAM domain at roughly 49-54 MHz while previous full routes reached more
  than 70 MHz. Only a completed route is an acceptance measurement.
- Post-pack `$glbnet$...` clock names do not exist when nextpnr first reads SDC
  constraints from synthesis JSON. Adding 13/80 MHz constraints only for those
  names therefore leaves placement unchanged. Adding both raw and global names
  still does not work in the one-pass placer because generated divider/PLL
  constraints subsequently restore 12.5/75 MHz. The weight-40 diagnostic
  placement was physically identical to P53 seed 4; its report retained SHA-256
  `dc4b7daef708111d81ecad68f3127b8a9f31b114dfa02dbd04ca8d83567c39f5`.
- Packing without placement and then reloading the packed JSON under 13/80 MHz
  constraints is conceptually correct, but Beast nextpnr
  `0.10-45-g98c18d7f` throws `std::out_of_range: dict::at()` as the heap placer
  starts. It fails both with floorplan constraints serialized during packing
  and with the pre-place hook deferred to the placement pass. Do not repeat the
  two-stage placement experiment on that tool revision without fixing nextpnr.
- Seed hunting is not a substitute for RTL work. Seeds provide useful spread
  after the repeating critical cone has been removed.
- Broad or aggressive floorplanning consumes placer freedom and can make timing
  and hard-resource placement worse. Constrain the communicating island, not an
  entire subsystem by name.
- Router2 alternate weights are useful diversity, not a known faster path. On
  this design it can take substantially longer than router1.
- Moving logic toward SDRAM without reading the actual source and destination
  coordinates is guesswork. A path with much more logic delay than routing
  delay needs RTL work first.
- Optimizing a simulation-only or reduced-feature netlist gives misleading
  utilization and timing. Host SPI, SD boot, ROM depth, graphics, PMMU, and
  build identity all matter.
- Treating a logically equivalent wide mux as free because only one mode is
  valid does not help synthesis unless the exclusivity is represented directly
  in registered state.
- Implementing a one-hot mux as replicated masks plus a wide OR is not free on
  ECP5. P38's masked DMA request datapath added roughly 334 avoidable LUT4s;
  P39's direct grant-selected mux recovered them while retaining registered
  one-hot ownership and exact cycle behavior.
- Registering top-level DMA ownership does not isolate a client whose `lock`,
  `valid`, and pointer enables still decode a large binary FSM live. P38 removed
  the owner-decode path, then two independent routes exposed the blitter's
  27-state phase decode at the next boundary.
- Reducing a registered control vector from 15 one-hot labels to 11 overlapping
  facts is not automatically an area win. P44 uses six fewer FFs but maps 106
  more LUT4s than P41 and remains 33 packed cells over the profile. Measure the
  packed result before claiming that a source-level simplification saved room.
- Replacing Draw's six-bit ellipse-ALU state selection with a two-bit phase and
  one registered X/Y selector is cycle exact but maps 363 more LUT4s and 57
  more carry cells. P49 is rejected before placement; fewer RTL branches do not
  guarantee a smaller ECP5 network.
- A parameter constant exposed directly through a wide system-data mux is not
  physically neutral. Changing `SOC_BUILD_ID` from zero to `0xe8a97ceb` changes
  mapping, adds 66 packed cells, and destroys reproducibility of the otherwise
  identical seed-23 placement. Release metadata needs fixed topology before a
  diagnostic route can be promoted.
- Equal synthesis resource totals do not prove equal topology. The rejected
  P50 `INIT=0000`/`ffff` build-ID bank retained 32 LUTs and equal counts, but
  created different parameterized module sets and shifted autogenerated names
  across the design. Compare complete normalized structure and packed BEL
  assignments before calling a metadata representation physically neutral.
- Timing ripup is not guaranteed to converge. On the exact `db60633` release
  netlist it repeatedly destroyed near-complete routes and rebuilt tens of
  thousands of arcs for 57 minutes. Stop and record a stable oscillation after
  its structural cause is clear; a long-running router is not evidence of
  progress.
- A timing boundary can work and still make the complete route worse. P41
  removed both live blitter-state paths, then Beast seed 23 fell to 62.47 MHz
  on a registered Vega-lock path with 13.10 ns of routing. Record the new cone;
  do not call the boundary a timing success merely because its old path moved.
- A precise floorplan can remove its target without improving the machine.
  P44's eight-cell Vega-lock bridge changed the worst path to the SDRAM core,
  but improved SDRAM by only 0.20 MHz and reduced CPU by 0.42 MHz in the
  controlled Mac seed-33 comparison. Keep it experimental; do not broaden or
  promote it from placement estimates alone.
- A floorplan capacity report is an invariant, not decoration. P45 initially
  constrained one hard multiplier into a rectangle with zero multiplier BELs;
  several placers consumed almost an hour while attempting an impossible
  legalization. Enforced bucket demand must never exceed region capacity, and
  the pre-place script now raises an error when it does.
- Relaxed constraints, false paths, disabled features, and lower clocks can
  produce a bitstream but cannot produce the Astra68 bitstream.

## Reading a failed route

Start with the routed report's `fmax` object, then inspect the worst SDRAM-domain
path. Record:

- source and destination cell/register
- source and destination coordinates
- total routing delay and total logic delay
- every RTL source location attached to the path
- whether the same cone appeared in another seed

For the current nextpnr report schema, the SDRAM-domain path is normally entry
5 after the five other constrained clocks. Verify the clock name instead of
assuming the index forever. A useful diagnostic query is:

```sh
jq '.fmax' pnr-report.json
jq '{start: .critical_paths[5].path[0].from,
     end: .critical_paths[5].path[-1].to,
     route_delay: ([.critical_paths[5].path[] |
       select(.type == "routing") | .delay] | add),
     logic_delay: ([.critical_paths[5].path[] |
       select(.type != "routing") | .delay] | add),
     sources: ([.critical_paths[5].path[].sources[]?] | unique)}' \
  pnr-report.json
```

Use the result to choose one lever:

1. A repeated deep logic cone gets a registered fact, predecode, or additional
   transaction state.
2. A shallow path with excessive distance gets a small endpoint-specific
   region or local register replication.
3. A high-fanout stable control gets localized copies, subject to the resource
   profile.
4. A path that changes substantially between good placements gets more
   seed/router coverage after the structural fixes.

Do not combine several speculative changes in one checkpoint. A single-purpose
change makes the next critical path and any functional regression attributable.

## Required regression gates

Run the exact gates from the repository root on Beast unless a checkpoint says
otherwise:

```sh
rtk make -C conformance test
rtk fpga/soc/sim/run_graphics_tests.sh
rtk sw/graphics_demo/run_sim.sh all
rtk sw/boot/run_sdram_sim.sh
rtk python3 fpga/soc/oss_flow/check_resource_budget.py \
  <nextpnr-report.json>
```

At minimum, every timing RTL checkpoint must pass the integration gates below.
The shared architecture matrix may be reused only when the CPU, PMMU, wrapper,
cache boundary, and memory-visible semantics are byte-identical to the retained
matrix manifest; the release revision runs it again:

- `fpga/soc/sim/run_graphics_tests.sh`
- integrated 68030 graphics demo, normal: maximum line cycles `1506`
- integrated 68030 graphics demo, stress INDEX8: `2369`
- integrated 68030 graphics demo, stress RGB565: `2060`
- direct SDRAM controller test with exact read/write data and byte lanes
- Yosys final `check -assert` with zero SCCs
- `check_resource_budget.py` against the packed nextpnr report

P36 measured direct-controller throughput at 145.30 MiB/s write and 143.64
MiB/s read in simulation. The blitter directed test measured 51.23 MiB/s copy
and 118.70 MiB/s fill, with exact copy and fill completion counts of 1499 and
1294. These are regression references, not hardware bandwidth claims.

## Current structural experiment and next levers

P50's exact committed route proves the metadata topology fix but fails on the
sprite-to-Vega request boundary. P51 removes that path with transition-
registered request facts and preserves every exact reference; its completed
route instead measures the tile-to-Vega request boundary. P52 proves that the
same cycle-neutral technique works there, but its one-hot fact encoding leaves
only 39 profile cells and does not route acceptably. P53 retains the registered
boundary with a compact two-bit encoding, restores 328 profile cells, and
places the fact beside owner selection. Its exact behavior, synthesis, and
placement are frozen above. Beast and Mac seed-23 routes fail on distinct
paths; the independent NUC seed-4 route passes every clock. The next
measurement is the exact nonzero-ID release.
Remaining legitimate levers, in order, are:

1. Commit the proven seed-4 source and configuration, derive that commit's
   build ID and ROMs, rerun every release gate, then route the exact nonzero-ID
   release netlist with the measured tool boundary.
2. Load hardware only after the exact release report and resource gate pass;
   verify identity, POST, HDMI, and repeated complete boots before persistence.
3. If the exact release misses timing, change only its newly measured
   post-route boundary. Do not add speculative floorplans or search seeds
   without a structural hypothesis.
4. Preserve the existing CPU, SDRAM, graphics behavior, cycle references, and
   resource profile in every timing change. A slower clock, removed feature,
   or relaxed constraint is not a solution.
5. If several structurally distinct revisions plateau on the same boundary,
   deliberately pipeline that boundary and update its protocol assertions and
   exact tests. That is the point to change the partition.

The completed P38 routes satisfied the condition for the first lever. Yosys
left the blitter's 27 active states in its five-bit source encoding, while both
routes began at `state_mem` and traversed broad phase decode. P40 briefly tried
an `fsm_encoding="one-hot"` attribute, but Yosys refused to extract the state
register, reported that it did not look like a proper FSM, and warned of a
possible simulation/synthesis mismatch. The attribute was removed immediately;
do not retry or ship that coercion.

P41 implements the explicit registered one-hot **bus-phase** vector instead.
It covers only request, lock, wait, and pointer-update phases and deliberately
does not duplicate the full control FSM. Directed graphics tests pass exactly,
including copy/fill completion counts of 1499/1294 and the 12-frame Vega test.
The integrated 68030 runs also retain maximum line counts of 1506, 2369, and
2060 for normal, stress INDEX8, and stress RGB565. The P42 equivalence run
completed the full boot/SDRAM gate at 144.48 MiB/s and 136,077 BIST cycles,
with DMA fill/copy counts of 210/327 and successful kernel entry. Its only RTL
difference was the area-regressing local comparison described above. P41
packed to 54,439 cells, 73 over the active profile. Its Beast route no longer
starts at `state_mem`, so the experiment worked structurally, but that route
reached only 62.47 MHz SDRAM on the registered Vega lock. P44 is the
smaller-fact follow-up and passes the same exact functional gates, but packs to
54,399 cells, still 33 over profile. Its three default routes expose three
real boundaries: validation arithmetic, Vega lock-to-grant, and sprite request
qualification. The eight-cell lock floorplan removed its target but did not
materially improve timing. P45 registers the validation multiplier operand in
the existing row counter, preserves every exact cycle reference, maps 160
fewer LUT4s, and packs to 54,345 cells. Its first route removes the intended
P44 cone but tops out at 72.01 MHz SDRAM on the chunk-count request comparator.
P46 removes only that redundant gate and has exact functional and cycle results;
its three routes remove the intended cone but fail on Draw glyph decode, SDRAM
row-hit logic, and tile/Vega request qualification. P47 removes the measured
Draw decode and saves another 225 packed cells. P48 removes the resulting
internal SDRAM decode and is the release candidate after its timing-ripup route
passes every clock. Do not add a blind broad Vega region or lower either
production clock.

One P36 router2-alt path already has a cycle-neutral candidate if it survives
P38: register the next sprite `prep_count` in `ST_ROW_SETUP` and the preceding
line-write state, then let `ST_RENDER_PREP` consume that stable count. For the
reverse case, derive row B directly as `cursor + !crosses` instead of computing
`(cursor - crosses) + 1`. This splits/removes the measured screen-X, minimum,
crossing, subtract, and increment chain without adding a render cycle. Do not
apply it unless a P38 routed report still identifies that cone.

The work is not considered stuck while each structural revision eliminates its
targeted path and the post-route result moves. It is stuck when multiple such
revisions expose the same boundary with no measurable timing change.

## Historical P53 release checkpoint (superseded)

P53 preserves all exact functional and cycle references with the compact
registered tile boundary. Exact commit `ca26765` passes conformance, Harte,
graphics, boot, synthesis, resource, and strict static timing gates. Its
12.70/76.57 MHz route is nevertheless rejected because both the production
image and a simulation-proven BRAM-only route probe produce zero UART bytes on
the ULX3S. The known-good control image passes on the same hardware path.
Persistent FPGA flash remains untouched.

At this checkpoint, production AstraHost was restored on the ESP32. The
existing SD-card data was intact and `/ASTRA68.ROM` contained the exact
`ca26765` payload with CRC32 `d0dc84a4`. Volatile FPGA SRAM then contained the
retained passing control image after the corrected P53 route-probe test;
persistent flash was untouched. Independent NUC and Beast routes were running
against diagnostic 13 MHz CPU and 80 MHz SDRAM implementation constraints;
runtime PLL/divider clocks remained exactly 12.5/75 MHz.

The next candidate at that checkpoint had to clear this sequence:

1. Complete routing with meaningful CPU and SDRAM margin and no timing waiver.
2. Replace only stage-0 BRAM with the proven route probe and require repeated
   UART output after an SRAM-only load.
3. Restore the exact stage-0 contents in the same route and require expected
   build ID, full POST, kernel entry, and correct HDMI output.
4. Repeat complete boot after at least two SRAM reload or power cycles.
5. Canonicalize every proven constraint/router control, commit, derive the new
   build ID and ROM, and rerun all functional and physical gates.
6. Write persistent FPGA flash only after that exact committed image repeats
   the complete board acceptance result.

The board is attached to `nuc`. Do not waste time probing for it on Beast or the
Mac.

## P53-P58 margin investigation (2026-07-17)

The exact P53 source was rerouted on the Mac against diagnostic 13 MHz CPU and
80 MHz SDRAM constraints. It completed at 12.58/77.07 MHz. That route meets
the actual 12.5/75 MHz runtime clocks, but it does not provide the required
implementation margin and does not change P53's failed hardware evidence.

P54 registers only data-cache hit responses in the TG68K wrapper. Instruction
hits retain their zero-wait path. The full 68030/PMMU core test, directed and
integrated graphics, and SDRAM boot all pass; simulation is roughly six
percent slower overall because data-cache hits consume one additional CPU
cycle. P54 synthesizes to 43,522 LUT4s, 18,275 FFs, 3,872 CCU2Cs, 80 BRAMs,
and 17 multipliers, and packs to 54,228 cells with 138 profile cells free. Its
CPU route improves to 14.43 MHz on Beast and 14.25 MHz on NUC, but SDRAM falls
to 72.21 and 70.68 MHz. Both hosts identify the same long SDRAM request/control
boundary, so P54 is evidence for the cache pipeline but not a release.

P55 constrained 93 request-queue cells near the SDRAM controller. Placement
estimates degraded from 11.84/61.54 MHz to 10.36/56.63 MHz, so it was rejected
before routing. Do not repeat that local floorplan: the queue participates in
global arbitration and the constraint displaces more valuable logic.

P56 keeps P54's cache pipeline and replaces the SDRAM controller's dynamic
`active_row_q[addr_bank_w]` read with four parallel bank comparisons and a
one-hot bank select. Every direct controller count, graphics result, CPU memory
cycle, cache count, DMA count, BIST count, POST result, and kernel-entry result
is unchanged. The complete 68030/PMMU core test also passes. P56 synthesizes to
43,335 LUT4s, 18,273 FFs, 3,880 CCU2Cs, 80 BRAMs, and 17 multipliers, then
packs to 54,053 cells with 313 profile cells free. Its Beast route reaches
14.61 MHz CPU but only 71.54 MHz SDRAM. The old row-hit mux is gone from the
critical path; the new 13.98 ns path runs from Vega tile `stream_pause`, through
live owner selection and `ready`, into the tile tag FIFO write port. This is a
successful structural isolation even though the complete route still fails.
The independent NUC route reaches 14.50/70.12 MHz and instead ends in Astraea
Draw result muxing. P56 therefore exposes placement-dependent graphics cones,
not one remaining SDRAM-controller defect.

P57 registers Vega's selected owner before allowing a client to observe
`ready`. It removes the measured combinational path, and directed graphics plus
the normal integrated workload pass. The handoff bubble raises normal maximum
line work from 1506 to 1521 clocks and stress INDEX8 to 2438 clocks, causing a
real scanline underrun. P57 was rejected and its in-progress synthesis was
cancelled. Registered ownership is not an acceptable timing trade because it
reduces required graphics throughput.

P58 returns to P56's zero-bubble arbitration and stages only accepted tile-tag
metadata for one clock before writing distributed RAM. A same-address bypass
preserves a legal one-cycle response. Directed graphics, all 12 Vega frames,
and integrated normal/INDEX8/RGB565 workloads pass with the exact retained
1506/2369/2060 maxima. Full SDRAM boot also retains 144.46 MiB/s, 136,094 BIST
cycles, all CPU memory/cache/DMA counts, POST, and kernel entry. P58 has zero
SCCs and synthesizes to 43,398 LUT4s, 18,289 FFs, 3,871 CCU2Cs, 80 BRAMs, and
17 multipliers. Seed-4 placement packs 54,102 cells, leaving 264 under the
`core_graphics` ceiling. The first Beast route reaches 14.08 MHz CPU and
74.39 MHz SDRAM. An independent NUC route reaches 14.08/76.20 MHz. The NUC
result clears the real 12.5/75 MHz clocks, but neither route clears the required
13/80 MHz diagnostic margin, so neither is a hardware candidate. A Beast
timing-ripup route remains in progress.

P59 tests a direct ten-state one-hot encoding of the SDRAM command FSM. It
preserves the exact direct-controller throughput, 12-frame video result,
integrated 1506/2369/2060 graphics maxima, complete POST counts, BIST count,
and kernel entry. It also has zero SCCs. However, widening the active, target,
and delayed state registers together synthesizes to 43,921 LUT4s and packs
54,705 cells, 339 above the active profile. P59 was rejected before routing;
duplicating every stored destination is not an acceptable area trade.

P60 keeps only the active ten-state command register one-hot. Its target state
uses two bits and its delayed destination uses three bits, with explicit
lossless conversions at the two boundaries. It passes the same exact direct,
video, integrated graphics, full POST, BIST, and kernel-entry gates as P58.
P60 has zero SCCs and synthesizes to 43,424 LUT4s, 18,294 FFs, 3,886 CCU2Cs,
80 BRAMs, and 17 multipliers. Seed-4 placement packs 54,162 cells, only 60
more than P58 and 204 below the `core_graphics` ceiling. Strict routes from the
same immutable placement are in progress on Beast, NUC, and the Mac against
the 13/80 MHz diagnostic constraints.

The durable lesson from this sequence is to pipeline the measured endpoint,
not a broad subsystem boundary. Each accepted change must preserve the exact
graphics line maxima and boot-cycle references. A path moving after a targeted
change is progress; a local floorplan that worsens both placement domains or a
pipeline that causes underrun is a measured rejection, not a reason to search
blind seeds. When state encoding is the measured lever, encode only the active
decision register broadly; compact stored destinations avoid paying the same
area cost several times.

## Runtime platform integration (2026-07-18)

The runtime platform adds a 32-source vectored Vesta interrupt controller, two
CPU-clock timers, AstraHost request/completion/state/input CDC queues, coherent
host SDRAM DMA, SPI raw-block service, and all CPU IRQ/IACK wiring. The same
image retains the complete MC68030/PMMU, SDRAM, Astraea, Vega, and HDMI feature
set. Focused Icarus and Verilator tests, the full CPU coretest, and the complete
pin-level AstraHost boot all pass. The integrated boot reaches full SDRAM BIST,
POST, a vectored 100 Hz timer interrupt, runtime storage negotiation, input
queue discovery, and `K0 ENTRY PASS`.

Runtime-10 replaced Vesta's serial 32-source priority loop with per-IPL masks
and a logarithmic lowest-source tree, and pipelined AstraHost packet capture.
It synthesized with zero SCCs to 49,371 LUT4s and packed 61,015 combinational
cells. Its strict route reached 13.397 MHz CPU but only 63.408 MHz SDRAM. The
15.771 ns SDRAM path crossed a 32-bit active-request identity comparison before
entering the broad AstraHost service-state mux.

Runtime-11 registered packet identity, generation, CRC, retry, ordering, and
completion-validation facts before dispatch. It synthesized to 49,320 LUT4s
and packed 60,976 combinational cells. The intended wide comparison path was
removed, but independent strict routes still reached only 64.863 MHz on Beast
and 64.51 MHz on the Mac. The new 15.417 ns boundary ran from the one-cycle ROM
writer `issue_request` pulse through `writer_idle` into the service-state mux.
That dependency was real: the independent writer does not consume the pulse
until the following clock edge, so the command engine had been using the pulse
itself to prevent a premature drain decision.

Runtime-12 adds explicit one-cycle data-drain and commit-wait arm states. After
the arm cycle, command control depends only on registered `request_valid` and
`wait_response`; `issue_request` no longer enters the state mux. This preserves
every boot and SPI protocol result and adds only one 75 MHz cycle per host ROM
chunk. Canonical synthesis has zero SCCs and zero structural problems at
49,522 LUT4s, 21,815 FFs, 4,247 CCU2Cs, 103 BRAMs, and 17 multipliers. Seed-4
placement packs 61,146 combinational cells, leaving 1,584 under the active
`complete_chipset` planning ceiling. Its routes did not close SDRAM timing, so
the sequence continued with one measured structural change at a time. No
retained experiment uses a reduced clock or reduced feature set. A timing
waiver is used only to preserve a failing route report and never to package a
bitstream.

### Runtime-13 through runtime-28 route record

The following table is the compact experiment ledger. Frequency pairs are
CPU/SDRAM MHz. Multiple pairs are independent hosts or placements; an omitted
resource value means the experiment was rejected before a useful complete
report rather than silently accepted.

| Revision | Structural experiment | Mapping / packing | Routed result |
|---|---|---|---|
| runtime-13 | Add a one-byte response skid boundary. | retained | 13.387 / 63.496, fail |
| runtime-14 | Capture the SDRAM delayed destination. | retained | 13.893 / 59.256, fail |
| runtime-15 | Split SPI stream handling into header, data, and CRC phases. | retained | 13.734 / 57.504, fail |
| runtime-16 | Register the Draw glyph source sum at its response boundary. | retained | 12.879 / 63.187, fail |
| runtime-17 | Register the asynchronous FIFO full fact. | retained | 13.507 / 63.211, fail |
| runtime-18 | Make writer availability local to its consumer. | rejected before complete report | no retained route |
| runtime-19 | One-hot encode the complete AstraHost service state. | area/control regression | no retained route |
| runtime-20 | Replace serial one-hot state decisions with parallel cases. | retained | 12.864 / 61.222, fail |
| runtime-21 | Consolidate the accepted protocol boundaries. | 49,787 LUT4, 22,069 FF, 4,251 CCU2C; 61,419 packed | 13.29 / 69.79 and 13.56 / 71.42, fail |
| runtime-22 | Remove the shared ellipse operand mux from its old cone. | 50,040 LUT4, 22,070 FF, 4,258 CCU2C; 61,696 packed | 14.02 / 67.86 and 14.03 / 68.86, fail |
| runtime-23 | Register SPI decode facts at transfer dispatch. | 49,880 LUT4, 22,168 FF, 4,254 CCU2C; 61,524 packed | 13.654 / 64.152, fail |
| runtime-24 | Retain the cycle-exact SPI boundary and remap. | 50,066 LUT4, 22,168 FF, 4,237 CCU2C; 61,650 packed | 13.3316 / 69.4879, fail |
| runtime-25 | Separate SDRAM read return from AstraHost CRC accumulation. | 49,514 LUT4, 22,202 FF, 4,230 CCU2C; 61,088 packed | 14.344 / 70.842, fail |
| runtime-26 | Register AstraHost response bytes before CRC consumption. | 49,733 LUT4, 22,234 FF; 61,325 packed | 14.95 / 67.98, fail |
| runtime-27 | Duplicate the Draw format fact near its consumers. | 49,394 LUT4, 22,205 FF, 4,267 CCU2C; 61,068 packed | 12.9729 / 66.6978 and 14.0430 / 70.2741, fail |
| runtime-28 | Capture Draw format once per command and register flood-fill match facts when pixel data returns. | 49,865 LUT4, 22,223 FF, 4,241 CCU2C; 61,481 packed | strict routes active on Beast, Mac, and NUC |

Runtime-21 was the first substantial recovery after the service-control work,
but independent routes proved that congestion, not one unlucky seed, remained.
Runtime-22 removed its targeted ellipse selection cone; the replacement was a
shared 48-bit ellipse add/sub path. Runtime-23 removed that path, after which
the critical boundary moved through SPI decode and transfer dispatch.
Runtime-25 removed the SPI control cone and exposed the SDRAM-read-to-CRC path.
Runtime-26 removed that CRC path by making a response byte a registered
producer/consumer boundary. The replacement path began at Draw
`pixel_result_mem`, crossed format normalization and the flood comparison, and
entered the broad Draw state mux.

Runtime-27 attempted to place local format copies near those consumers, but
Yosys legally merged the equivalent registers despite `keep`; Beast and Mac
then reported the same path. Runtime-28 changes the actual dependency instead:
one accepted-command format register feeds execution, the fill value is
captured at configuration time, and target/fill comparisons are registered in
`ST_PORT_WAIT` from the normalized return data. `ST_FLOOD_PROCESS` consumes
only those registered facts. Directed graphics, integrated normal/INDEX8/
RGB565 maxima of 1506/2369/2060, full boot, and the route-probe simulation are
cycle exact. The complete image still has zero SCCs and remains 1,249 packed
cells under the 75% planning ceiling.

The reusable lesson is that a registered producer/consumer boundary must be
represented in control sequencing, not reconstructed by feeding a launch pulse
back into a large next-state cone. Capture wide protocol facts before dispatch,
and spend an explicit local control cycle when a separate sequential writer
consumes an issue pulse one edge later. When a datapath return already has a
registered response boundary, derive and register the narrow decision facts
there instead of carrying a wide value through normalization, comparison, and
a subsystem-wide state mux. Equivalent register copies are not a structural
fix unless synthesis is required to keep them distinct.

### Runtime-29 through runtime-34 physical implementation

Runtime-29 was the first complete runtime-platform image to produce a routed
candidate. The Mac seed-23 timing-ripup route packed 61,629 combinational cells
and reported 13.6739 MHz CPU and 75.6773 MHz SDRAM. Those numbers clear the
architectural clocks but do not provide useful SDRAM margin. More importantly,
the checked BRAM-only route-probe image produced zero diagnostic UART bytes after
an SRAM load. The retained control image passed complete POST immediately over
the same board path. Runtime-29 was rejected for hardware use and persistent
FPGA flash was not touched.

Runtime-31 retained every functional result and isolated the next repeatable
SDRAM cone. Its Mac plain route reached 13.41 MHz CPU but only 64.56 MHz SDRAM.
The 15.49 ns critical path began at `request_addr[18]` near `(99,52)`, crossed
the open-row comparison and command-state decision logic, and ended at
`state_q[0]` near `(122,89)`; 3.59 ns was logic and 11.90 ns was routing.
Timing-ripup routes from the same design oscillated around a 1.2 ns negative
slack and were stopped. This was a physical fanout/locality problem around the
captured request address, not evidence for reducing the 75 MHz clock.

Runtime-32 added a kept request-address copy for row-hit decisions, but post-
synthesis inspection proved that Yosys merged every bit back into the
architectural request register. A retained name was not a retained physical
boundary, so Runtime-32 was rejected before placement.

Runtime-33 gives the comparison copy a distinct reset-only value while loading
both registers identically on every valid request. Assertions require equality
whenever `request_valid` is true, so active behavior, latency, bandwidth, and
the command address are unchanged. Synthesis preserves 25 distinct comparison
bits and adds 23 FFs after constant folding. Beast maps 49,636 LUT4s, 22,319
FFs, 4,184 CCU2Cs, 105 block RAMs, and 17 multipliers with zero SCCs; seed 4
packs 61,100 combinational cells. The independent Mac mapping packs 60,973.

The Mac seed-23 plain route completes at 13.4436 MHz CPU and 70.7714 MHz SDRAM.
The old device-spanning request-address cone is absent. The replacement 14.13 ns
path starts at Draw `ellipse_dx[3]`, traverses the 48-bit
`ellipse_e2 >= ellipse_dx` comparison, then crosses the broad Draw state mux
into `state[2]`. It contains 6.447 ns logic and 7.683 ns routing. The independent
Beast seed-4 route reaches 12.9425 MHz CPU and 73.1689 MHz SDRAM. Its 13.667 ns
path starts at AstraHost `rx_buffer_data[4]`, crosses response decode and the
service state mux, and ends in the service register bank; 3.593 ns is logic and
10.074 ns is routing. Runtime-34
captures both ellipse step decisions in dedicated registers during the existing
final-slot cycle and consumes them in the already-existing `ST_ELL_STEP` cycle.
Directed graphics, integrated normal/INDEX8/RGB565 maxima of 1506/2369/2060,
and full pin-level boot remain exact. Beast maps 49,662 LUT4s and seed 4 packs
61,178 combinational cells. The Beast route passes CPU at 13.5718 MHz but
reaches only 65.206 MHz SDRAM; the 15.336 ns path starts at
`vega_mem_rdata[7]` and ends on a blitter accumulator clock enable. The Mac
route reaches 13.81/69.76 MHz on a separate host-boot-to-blitter-control path.
NUC seed 57 reaches 13.46/66.52 MHz and repeats the Beast
SDRAM-read-to-blitter-enable cone at 15.032 ns. Runtime-34 is rejected.

### Runtime-35 through runtime-38 physical implementation

Runtime-35 removes two redundant masked-copy accumulator clears from
`ST_KM_MASK_WAIT`. Both legal entry paths already clear the same registers, so
this changes neither cycles nor behavior; all 8/16/32-bit masked-copy tests,
the complete directed graphics suite, the three integrated graphics modes, and
full host boot remain exact. Beast maps 49,550 LUT4s, 22,322 FFs, 4,230 CCU2Cs,
105 block RAMs, and 17 multipliers, with zero SCCs. Seed 4 packs 61,132 cells.
The route passes CPU at 13.38 MHz but reaches only 67.56 MHz SDRAM. The old
video-data-to-blitter-enable cone is absent. Its replacement 14.801 ns path
starts at `request_compare_addr[15]`, crosses the open-row comparison and state
decision, and ends at the command-state register. Runtime-35 is rejected.

Runtime-36 registers the AstraHost lock at the internal DMA-owner boundary.
The raw service request still begins cache maintenance before ownership; the
registered acquisition/release adds at most one 75 MHz clock per ownership
interval and does not change burst throughput. Full pin-level boot reaches
`KERNEL IDLE`. Canonical Beast synthesis improves to 49,113 LUT4s, 22,321 FFs,
4,231 CCU2Cs, 105 block RAMs, and 17 multipliers with zero SCCs. Seed 4 packs
60,697 cells, 2,033 below the 75% planning ceiling.

The independent Mac route passes CPU at 14.21 MHz but reaches 67.63 MHz SDRAM;
12.37 ns of its 14.79 ns path is routing across a placement-specific
device-spanning cone. The controlled Beast seed-4 route passes CPU at
13.31 MHz but reaches 68.51 MHz SDRAM. Its repeatable 14.60 ns path starts at
`request_compare_addr[20]` at `(98,54)`, reaches the row comparator at
`(112,74)`, and ends at the state register near `(121,83)`; 3.59 ns is logic
and 11.00 ns is routing. Runtime-36 is retained as the functional and mapping
baseline but is not packageable.

Runtime-37 adds one measured physical constraint, not a logic change. The
77-cell `sdram_compare` region contains exactly 25 comparison-address FFs and
52 directly connected comparator LUTs in `(108,68)..(126,95)`, beside the
existing SDRAM command island. The Mac seed-23 route passes CPU timing at
13.05 MHz and improves SDRAM to 69.85 MHz. The constrained comparison cone is
absent from the failing paths. The replacement 14.32 ns path starts at
AstraHost `active_host_generation[6]`, crosses the generation-valid decision,
and controls a completion-generation register clock enable; 3.05 ns is logic
and 11.27 ns is routing. The broad region therefore fixes its intended cone
but exposes a separate service boundary on that placement. NUC seed 57 passes
CPU timing at 13.77 MHz and reaches 70.71 MHz SDRAM. Neither the comparison nor
host-generation path remains; its 14.14 ns path runs from sprite-builder
`render_ctrl[17]` into `collision_bitmap_mem[13]`, with 2.88 ns logic and
11.26 ns routing. The duplicate Beast route was stopped after more than an hour
in a high-conflict basin because it exercised the same unchanged netlist and
the NUC control was progressing materially better.

Runtime-38 is the controlled routing-freedom comparison. It uses the same box
and netlist but constrains only the 25 FFs, leaving the comparator LUTs under
the broader existing `sdram_edge` region. Mac seed 23 passes CPU timing at
13.61 MHz but reaches only 66.91 MHz SDRAM. The source FF moves into the target
box while its comparator falls back to `(102,50)` and the state destination
remains near `(121,84)`, recreating the 14.94 ns open-row comparison path.
The narrow constraint is insufficient and is rejected. Its duplicate Beast
route was stopped once the Mac result proved the physical mechanism.

Runtime-39 addresses the service boundary exposed by Runtime-37. State 17 now
registers the host-generation and combined media/host-generation predicates;
state 18 consumes those one-bit facts instead of allowing the 64-bit equality
cone to control the wide completion register bank directly. A successful
request already traversed states 17 and 18, so its response latency and all
burst throughput are unchanged. Only a stale or reset-generation rejection is
deferred by one 75 MHz clock. The focused AstraHost service test and complete
pin-level boot pass through full SDRAM BIST, POST, system-ROM load, Vesta at
100 Hz, `K0 ENTRY PASS`, and `KERNEL IDLE`.

Canonical Beast Runtime-39 synthesis has zero SCCs and maps 49,529 LUT4s,
22,325 FFs, 4,214 CCU2Cs, 105 block RAMs, and 17 multipliers. Both predicate
registers remain distinct in the mapped JSON. Placement packs 61,061
combinational cells, leaving 1,669 under the 75% planning ceiling. Beast seed
4 and Mac seed 23 route the same synthesis JSON with the proven broad 73-cell
comparison region (25 FFs and 48 comparator LUTs after the remap).

The Mac seed-23 plain route passes CPU timing at 13.39 MHz and reaches
70.71 MHz SDRAM. Its 14.14 ns path begins at a tile-pair block-RAM output,
crosses tile composition, and ends at a line-buffer write register; 6.81 ns is
logic and 7.33 ns is routing. The Beast seed-4 plain route passes CPU timing at
13.15 MHz and improves SDRAM to 71.92 MHz. Its 13.90 ns path begins at Astraea
masked-copy destination block RAM, crosses the byte merge and shared write-data
mux, and ends in the SDRAM controller request registers; 2.67 ns is logic and
11.24 ns is routing. Neither completed route contains the constrained row
comparison or the Runtime-37 host-generation path. Both still fail the strict
75 MHz requirement, so neither is packageable. Independent NUC and alternate
Mac routing strategies remain active against the identical mapped design.

The independent NUC seed-57 route also fails, passing CPU timing but reaching
only 67.78 MHz SDRAM. Its 14.75 ns path begins at Draw
`glyph_source_x[0]`, crosses glyph byte addressing and the broad Draw control
mux, and ends at a Draw register; 3.38 ns is logic and 11.38 ns is routing.
This is a third placement-specific replacement cone rather than a recurrence
of either Runtime-39 target. The old-nextpnr NUC route and Mac timing-ripup
route remain active.

Runtime-40 tested whether two unkept, separately captured host and media
generation predicates would map more economically. Focused Icarus and
Verilator service tests pass, but canonical synthesis grows to 49,901 LUT4s
while retaining 22,325 FFs, 4,186 CCU2Cs, 105 block RAMs, and 17 multipliers.
The 372-LUT regression provides no functional or timing-boundary advantage over
Runtime-39, so Runtime-40 is rejected before placement.

Runtime-42 registers masked-copy/key destination write data at the existing
destination-response boundary. All directed graphics tests and the integrated
normal, INDEX8-stress, and RGB565-stress workloads pass with unchanged maxima
of 1506, 2369, and 2060 SDRAM clocks per line. Canonical Beast synthesis maps
49,832 LUT4s, 22,355 FFs, 4,218 CCU2Cs, 103 block RAMs, and 17 multipliers;
seed 4 packs 61,376 combinational cells. The route passes CPU timing at
14.40 MHz but regresses SDRAM to 66.50 MHz. Its 15.04 ns critical path begins
at Astraea Draw `state[2]`, crosses a placement-stretched Draw state/output
mux, and ends at a Draw register; 3.59 ns is logic and 11.44 ns is routing.
The targeted masked-copy path is absent, but the 303-LUT mapping increase and
additional congestion expose a substantially worse independent cone.
Runtime-42 is rejected.

## 2026-07-22 Astraea multi-row hardware diagnosis

The exact `B1F9E60D` release remained timing-clean and repeatedly passed POST,
full SDRAM BIST, Astraea's 1 KiB POST DMA check, and kernel entry, but the full
graphics ROM stopped on its first 720x480 clear with `GFX F41`: blitter error
1, invalid configuration/range. A focused production-map ROM tested four
commands against the same accepted route:

| Tag | Width x height | Destination pitch | Readback barrier | Hardware result |
|---|---:|---:|---|---|
| A | 720 x 1 | 0 | no | pass, fence 1 |
| B | 720 x 480 | 0 | no | error 1, fence 2 |
| C | 720 x 480 | 0 | yes | error 1, fence 3 |
| D | 720 x 480 | 720 | yes | error 1, fence 4 |

The diagnostic records showed stable CPU-visible destination, pitch,
dimensions, operation, status, and submitted completion fence for every
command. Requiring both sticky `DONE` and the submitted fence eliminated a
false completion from the prior command. The identical zero-pitch failure
eliminated command ordering, barriers, nonzero-pitch arithmetic, and SDRAM
traffic: rejection occurs before the first memory request. The only
height-dependent range-validation datapath was the unregistered unsigned
16x16 product at `astraea_blitter.sv`, mapped to one `MULT18X18D`. The accepted
route's diagnostic transplant changed only logical `rom.0.0`/`rom.0.1` and
physical BRAMs 32/33. Its bitstream SHA-256 was
`91ad5c47ece7bd1ab3f973a5ebe8f9536260253bc12cfc6130c2ed6e6d7e0871`, so
placement, routing, and every non-ROM configuration bit remained controlled.
The exact production image was restored after capture and again passed POST,
32 MiB BIST, DMA, ROM identity, and `K0 ENTRY PASS`; persistent flash was not
touched.

The retained candidate replaces that product with a 16-cycle unsigned
shift/add validator. It adds at most 16 memory clocks per validated surface,
or 48 clocks for a masked copy, without changing transfer throughput. Beast
directed graphics pass at 38.59 MB/s copy and 91.84 MB/s fill; integrated
normal, INDEX8-stress, and RGB565-stress modes pass at maxima of 505, 1103,
and 1429 clocks against the 1906-clock scanline deadline; the focused
production-map diagnostic passes; full CPU
coretest passes; and complete HDMI-enabled AstraHost boot reaches `POST PASS`,
`K0 ENTRY PASS`, and `KERNEL IDLE`.

Canonical Beast synthesis (`-abc2`, full production feature set, 12.5 MHz CPU,
60 MHz SDRAM) reports 52,728 LUT4s, 25,492 FFs, 5,055 CCU2Cs, 101 block RAMs,
and 18 multipliers. It has zero SCCs, GSR enabled on all 25,496 physical FF
cells including reset-release synchronizers, and the font-ROM structural gate
passes. The blitter multiplier is absent from the mapped JSON. This checkpoint
is retained for diagnosis and mapping only; exact strict routing and fixed-RTL
hardware acceptance remain pending.
