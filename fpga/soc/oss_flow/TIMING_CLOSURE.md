# Astra68 timing-closure playbook

This file records the measured lessons from closing the complete Astra68 SoC on
the ULX3S-85K. It is a continuation point, not a claim that a release build is
finished. Update the result table and release checkpoint whenever the mapped
design or physical constraints change.

## Non-negotiable target

The production bitstream must satisfy all of these at the same time:

- TG68K.C 68030 plus PMMU, AstraHost SPI boot, SD stage 0, Astraea, Vega, and
  the 32 MiB SDRAM system are enabled.
- CPU is constrained to 12.5 MHz and the SDRAM controller to 75 MHz.
- Every other generated and external clock is explicitly constrained by
  `astra_clocks.sdc`.
- Yosys reports no combinational SCCs or other `check -assert` failures.
- nextpnr completes routing with no timing waiver in the release result.
- The `core_graphics` resource profile passes. Passing the physical device
  capacity alone is not enough because more chipset logic remains to be built.
- Directed graphics, integrated 68030 graphics, SDRAM, boot, and CPU regression
  tests remain exact.
- The exact release ROM, feature parameters, build ID, router, seed, and
  floorplan are used for the final synthesis and route.
- The packed bitstream passes repeated SDRAM POST and HDMI checks on the ULX3S
  attached to `nuc`.

Do not close timing by weakening the architecture. In particular, do not lower
the 75 MHz SDRAM clock, lower the 12.5 MHz CPU clock, disable a production
feature, add a false path to a synchronous path, or accept `--timing-allow-fail`
as a release result. `--timing-allow-fail` is useful only to obtain a routed
diagnostic report.

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

### Known reproducibility hazards

These are open release-flow defects at the P38 checkpoint:

- `sw/boot/Makefile` defaults `CPU_CLK_DIV_BIT` to 2 and `PNR_SEED` to 1,
  while `mkbit.sh` defaults the seed to 2 and the SoC RTL also has reduced-speed
  simulation defaults. A production build must pass the canonical values
  explicitly until the defaults are unified.
- `mkbit.sh` defaults `TARGET_FREQ_MHZ` to 12 even though the production CPU
  target is 12.5 MHz. The SDC currently supplies the exact clock constraint,
  but the command-line target and recorded banner must agree with it.
- `BUILD_CONFIG` and the final build banner do not yet encode every synthesis,
  floorplan, router, and alternate-weight knob. Two differently mapped images
  can therefore claim the same identity.
- The exploratory split place/route commands live outside the canonical script.
  Once a route passes, promote its exact options into the checked-in flow before
  calling the result reproducible.

Resolve these before making the release-identical route. Do not silently change
the exploration netlist while comparing P36 seeds.

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
| P47 | 53,966 | 13.89 MHz | 66.68 MHz | Glyph-only states decode the contiguous opcodes 8..11 from their two low mode bits. Exact tests pass and packing drops by 225 cells. Beast seed 23 removes the intended glyph path but exposes an internal SDRAM target-state decode feeding row-open state. Mac and NUC routes remain active. |
| P48 | 53,957 | routing | routing | The SDRAM core consumes the already registered target state in ACTIVATE/PRECHARGE instead of rebuilding the same value through `target_state_r`. Exact tests and cycle references pass; mapping is 43,365 LUT4s/18,252 FFs with zero final SCCs. Beast seed-23 routing is active. |

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
NUC seed-57 routes remain useful diversity until complete.

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
MHz placement estimate is diagnostic only. Full route timing decides whether
P48 is retained.

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
- P47 router1, seed 33 on the Mac: active
- P47 router1, seed 57 on NUC: active
- P48 router1, seed 23 on Beast: synthesis complete; physical run active

Keep `--timing-allow-fail` during diagnosis so a near miss still produces the
complete report. Remove the waiver from the release acceptance criteria.

A controlled P37 placement experiment increased
`--placer-heap-timingweight` from 10 to 20 without changing seed 57. The CPU
estimate improved from 10.36 to 11.80 MHz while SDRAM held at 53.82 MHz. On
seed 33, the same change improved CPU from 11.09 to 12.09 MHz and SDRAM from
46.95 to 56.17 MHz. This makes timing weight 20 a useful P38 route candidate,
but it is not a canonical setting until a full route demonstrates a benefit.

## What has not worked

- Placement timing is not predictive at this density. P36 placement estimated
  the SDRAM domain at roughly 49-54 MHz while previous full routes reached more
  than 70 MHz. Only a completed route is an acceptance measurement.
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

P46 is fully measured on Beast, Mac, and NUC. All three routes remove the
targeted P45 comparator, proving that the protocol-state simplification
synthesized as intended, but they expose three independent boundaries and fail
75 MHz. P47 removes the measured Beast full-byte glyph decode with the exact
two-bit mode encoded by opcodes 8..11. Its first Beast route proves that target
gone, then exposes redundant internal SDRAM target-state decode. P48 replaces
that decode with the already registered target without adding a state or
changing a cycle; its exact source, functional references, and synthesis
identity are frozen above. Complete the active P48 physical run and the P47 Mac
and NUC diversity routes before choosing the next measured cone.
Remaining legitimate levers, in order, are:

1. Complete and compare the active Beast P48 route and Mac/NUC P47 routes from
   their immutable 509-word-stage-0 netlists; record every repeated critical
   cone.
2. If the Mac P46 SDRAM open-row-hit path repeats, compute active-row equality
   in parallel per bank before selecting the bank result. Preserve the exact
   zero-cycle row-hit fast path and measured controller throughput.
3. If the Draw shared-ellipse-ALU path repeats, move its state-dependent operand
   selection into an existing predecessor cycle before changing the ALU width
   or adding a geometry cycle.
4. If the sprite/Vega qualification path repeats, register or predecode that
   exact client-to-owner boundary while preserving steady-state request rate.
5. If the Vega-lock path repeats, change the zero-cycle lock-to-grant protocol
   boundary; the eight-cell placement constraint already proved that moving
   only the lock net cannot close the machine.
6. If another SDRAM core internal state path repeats, inspect that controller
   transition directly before changing another client or floorplan region.
7. If several structurally different revisions plateau on the same boundary,
   deliberately pipeline that boundary and update the protocol assertions and
   tests. That is the point to change the partition, not to keep seed hunting.

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
Draw decode, saves another 225 packed cells, and is the active physical
checkpoint. Do not add a blind broad Vega region or lower either production
clock.

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

## Release checkpoint

After a diagnostic route passes all clocks:

1. Make the proven floorplan, router, seed, and synthesis parameters the
   canonical checked-in defaults and include them in `BUILD_CONFIG`.
2. Commit the source so the ROM version, date, and git hash are final.
3. Build the exact 509-word stage 0 and nonzero `SOC_BUILD_ID` from that commit.
4. Resynthesize, replace, and reroute that exact release netlist. Do not reuse
   timing from the zero-ID diagnostic netlist.
5. Run `ecppack` only on a route that passes every clock and the resource check.
6. Provision only `/ASTRA68.ROM` on the existing FAT filesystem. Preserve the
   owner's unrelated SD-card data and restore production AstraHost firmware
   after maintenance provisioning.
7. Flash the ULX3S through `nuc`, run `sw/boot/check_hardware.py` with the exact
   expected build ID, and capture HDMI.
8. Verify POST build version/date/hash, all SDRAM tests, and the OS-loader-ready
   state across at least two complete power/reconfiguration cycles.

The board is attached to `nuc`. Do not waste time probing for it on Beast or the
Mac.
