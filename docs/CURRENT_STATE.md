# Astra 68 current engineering state

This is the short continuation map for the active machine. It records decisions
and validated boundaries that are easy to lose across long sessions. Detailed
contracts remain authoritative in the linked documents; historical handovers
and old resource tables are not current status.

The platform is **Astra 68**, its kernel is **Axiom**, and the complete
user-facing system is **Astra OS**. The Astra NDK is the stable developer
surface; Axiom's internal interfaces are not a module ABI.

The kernel's normative implementation contracts are
`KERNEL_ARCHITECTURE.md`, `MEMORY_MAP_AND_PMMU.md`, `ABI.md`,
`LOCKING_AND_PREEMPTION.md`, `RESOURCE_OWNERSHIP_AND_FAILURES.md`,
`MEMORY_BUDGET.md`, `SHARED_AREAS_AND_BULK_RINGS.md`,
`K9_MEMORY_PRESSURE.md`, `K10_DEVICE_AND_OBSERVABILITY.md`, and
`TEST_AND_FAULT_INJECTION_PLAN.md`; `STATUS.md` separates implemented evidence
from planned work.

The product-level operating-system direction is `OS_VISION.md`. Focused
userspace design and the provisional kernel driver boundary live in
`USERSPACE_ARCHITECTURE.md`,
`DRIVER_AND_SERVICE_ARCHITECTURE.md`, `DESKTOP_AND_UI.md`, `TERMINAL_AND_POSIX.md`,
`APPLICATION_AND_KIT_MODEL.md`, `USERSPACE_BUDGET.md`, and
`RESOURCE_MODEL.md`. These are not evidence that services, a desktop, a POSIX
personality, zsh, or Vim currently run.

## Userspace bring-up line (2026-08-05)

`docs/HANDOVER-userspace-bringup.md` is the resume point for the userspace,
storage, and loader work aimed at a shell on Astra. Implemented and gated: the
observability contract (`docs/OBSERVABILITY.md`), a bounded userspace
allocator, a QEMU Vesta block service that lets `sw/kernel/block.c` run in
emulation for the first time, a strict big-endian MC68030 ELF acceptance
profile with a transactional loader, and a capability-gated process-info
syscall at ABI `0x00010008`.

**Astra runs a real user program at boot.** Boot ABI 0.3 carries
`user_image_base`/`user_image_size`; firmware embeds the linked supervisor ELF
in the ROM, copies it to `0x02004000`, verifies the copy, and reserves only the
pages it fills. The kernel loads it with `kernel_process_create_executable()`,
and the process validates its startup block, queries the syscall ABI, reads
`PROCESS_INFO` on its own handle, and exits with a tagged status the kernel
reports and gates on. Verified in QEMU end to end; 30 kernel suites, 6
userspace suites, sanitizers, `-fanalyzer`, both QEMU certifiers, and the
MC68030 kernel image all pass.

The initial image is 1,306 bytes of MC68030 text (6,468-byte ELF). The ROM file
is now 237,868 of 262,144 bytes, leaving 24,276 bytes of ROM headroom — the
next thing added to the ROM must account for it.

lwext4 is qualified big-endian behind three one-line upstream fixes but is
neither vendored nor adopted. There is no VFS and no terminal.

## Driver substrate candidate (2026-08-04)

The working tree contains a provisional Axiom device-lease substrate: an
8-entry sealed registry, 8 exclusive generation-tagged leases, a 2-lease
per-process limit, read/transfer/administer rights, trusted bootstrap grants,
query/reset/revoke and bounded input-read syscalls at ABI `0x00010007`, and
owner-death quiesce/reset before handle closure. It reuses handles, ports,
shared areas, rings, IRQ
endpoints, and user-copy; no generic I/O queue or class policy was added.
Focused device and process tests and the freestanding MC68030 link pass on
Beast. Full qualification and physical device registration remain pending.

## Input transport candidate (2026-08-04)

The working tree defines input ABI `0x00010001` in `sw/include/astra/input.h`.
It carries 20-byte big-endian physical keyboard and pointer records. Keyboard
values are USB HID Keyboard/Keypad Usage IDs; pointer records carry signed
relative/absolute axes or normalized button IDs. The Vesta queue has 32 slots,
31 usable records, independent 16-bit keyboard and pointer sequences, a host
generation, sticky overflow, and IRQ source 5. Axiom registers this controller
as exclusive physical device `0x494e0001` when present and supplies bounded
quiesce/reset/drain operations.

The exact QEMU 9.2.4 host build passes `emu/qemu/test-input.py` on Beast. The
active Arty ARMv7 build SHA-256 is
`54468714d702eb807237ecf12865c1cf88c2956637cd14d5ec753fcebe31b517`
and passes the same certifier on the Arty plus the unchanged deployed Axiom ROM
through `ASTRA68-QEMU READY`. The prior active emulator is retained under its
hash-qualified rollback name. The certifier covers keyboard usage
mapping, signed X/Y motion, pointer buttons, FIFO ordering, independent device
sequences, full-queue overflow, pop/overflow acknowledgement, and IRQ
assertion/deassertion. The Arty Linux kernel has USB HID, input, keyboard,
mouse, and evdev support built in. No physical keyboard or mouse event node was
present at the checkpoint, so direct evdev hardware-event qualification remains
pending. Protected userspace can drain physical events through the input-device
lease. The cross-built, allocation-free input service core now implements a
replaceable keymap with a built-in US map, separate key/text events, modifiers
and Caps Lock, bounded repeat,
integer pointer acceleration and clipping, eight-client focus routing,
generation/overflow reset repair, and bounded 56-byte application-port
  messages. Beast functional, sanitizer, GCC analyzer, MC68030 cross-build, and
  kernel regression gates pass. The target library is 3,095 bytes of text with a
  fixed 368-byte state object. It is not yet running as an Astra process: the
  base syscall runtime now exists, but the production ELF loader, supervisor,
  and launch-time transfer of its device lease, IRQ endpoint, and ports remain
  unimplemented. Physical evdev qualification is also still pending because no
  keyboard or mouse event node was present.

## Active Arty migration override (2026-07-30)

The active machine target has moved from ULX3S to the Arty Z7-20 attached to
`beast`. The Zynq processing system runs Linux and executes the unchanged
big-endian MC68030/PMMU machine through the Astra QEMU backend. The exact Axiom
K1-K10 image reaches the required markers there, and the accepted current CPU
performance baseline is approximately 30 MHz equivalent. Musashi and the
retained RTL core remain behavioral and conformance oracles; the MC68030 CPU is
not part of the active Arty PL resource budget.

The Arty provides 512 MiB DDR. The active device tree reserves the physically
contiguous 128 MiB range `0x18000000..0x1fffffff` as `no-map` graphics memory;
Linux System RAM ends at `0x17ffffff`. The remaining memory is available to
Linux, QEMU, the Astra guest, and non-graphics services. Read-only `/`, writable
`/data`, persistent SSH state, DHCP, and the shared Mac-directory service all
survive the graphics boot-package replacement.

[`GRAPHICS_ARCHITECTURE.md`](GRAPHICS_ARCHITECTURE.md) is the normative Arty
Vega/Astraea contract. It locks 1280x720p60, INDEX8/RGB565/XRGB8888 scanout,
two-axis ring scrolling, two INDEX4/INDEX8 tile layers with pixel scrolling,
64 INDEX8 sprites up to 128x128 with sixteen independently selected 256-entry
palette banks, sprite 0 as the optional desktop cursor, fenced scene promotion,
copper, blitter/virtual sprites, geometry, pattern and flood operations, AFNT
glyph expansion, bounded command rings, and the ARM/PL coherence and release
gates.

The output decision is based on PG235, UG934, PG230, UG471, DS187, and the
Digilent board manual. CEA 720p60 uses 1280x720 active, 1650x750 total, a
74.25 MHz pixel clock, 742.5 Mb/s TMDS lanes, and a 371.25 MHz DDR serializer
clock. The rejected 1080p60 proposal would require 1.485 Gb/s lanes and a
742.5 MHz serializer clock, beyond the device's characterized clock limits.

The qualified transport shell remains the rollback checkpoint. Physical HDMI
shows its complete 1280x720 test raster; retained screenshot
`docs/evidence/astra-arty-720p60-hdmi-20260729.png` has SHA-256
`a5ca652d6cbc075b018f0b7f4f08d414f9ebbac6edaf81460d4fc3b8f1d3f12d`.
That shell established the exact 74.25 MHz pixel and 371.25 MHz serializer
path before DDR integration.

Integrated checkpoint `boot-text6` supersedes the earlier `full8` base. It includes
the Zynq PS, GP0 control, three 64-bit HP DDR read paths, INDEX8/RGB565/
XRGB8888 framebuffer scanout, both tile layers, palette stores, ordered
composition, four-line scheduling, atomic frame-boundary scene promotion,
counters, HDMI, and a four-row double-buffered CP437 boot-text plane. All nine
directed simulation programs pass; the final 1280-pixel INDEX8 tile test takes
1,346 of 4,444 available 200 MHz clocks. ARM software also passes strict
cross-compilation, GCC static analysis, and host unit tests.

The exact full-system Beast Vivado 2024.2 route meets every constraint with
+0.002 ns setup, +0.019 ns hold, and +0.538 ns pulse-width slack; all 23,261
routable nets complete without error. It uses 13,096 total LUTs, 12,892
registers, 29.5 BRAM36-equivalent tiles, and five DSPs. Bitstream SHA-256 is
`869b0b4917135486376ab868f5599963dced75a2f8cfa76b2261fe01d0439cf4`.
The boot-text route history records and fixes the initial flip-flop inference,
two pixel/font cones, oversized selector carry chain, and direct GP0 readback
cone. Exact source, report, methodology, CDC, resource, and artifact identities
are in `fpga/arty/graphics/TIMING_CLOSURE.md`.

The sprite-qualified XSA generated its own FSBL and boot package. That
`BOOT.BIN` SHA-256 is
`b88b142cc4624ea70dafc65b0aec900d506bcf17f90fc1c7ea6f5f834d8098a5`;
active `image.ub` SHA-256 remains
`e9ef016f059cb3bc71138edf2a5ae47646a0e11b3dab3b81f7362f592b97b542`.
The previous boot files remain on the card under hash-qualified rollback
names. U-Boot and the Linux kernel payload are unchanged.

The text-free 1280x720 PNG is checked in exactly, SHA-256
`cdf001bb70e130c9267f5205261eb3855f1b74cbbba832dbcd22fb6d66f77ff9`.
The deterministic big-endian RGB565 image is 1,843,200 bytes, SHA-256
`86eb30739db77b85f4deb1915fb9cb9263ab4755ae318ffb1b7a4a95b7017ba4`,
and CRC32 `611029ee`. On Arty, the static ARM loader wrote and read back every
byte, promoted graphics generation 1 with zero deferrals, and published the
final text bank at generation 2. A live row-only update advances generation 3;
MMIO reports capabilities `0x000000ff`, including the sprite engine.
The FPGA manager reports `operating`, `/` remains read-only, and `/data`
remains writable. Hardware evidence is
`docs/evidence/astra-arty-boot-text6-hardware-20260730.log`. Direct monitor
confirmation passes: all four dynamic rows are visible, correctly colored,
aligned inside the lower panel, and clear of the Astra OS badge. The retained
frame is `docs/evidence/astra-arty-boot-text6-hdmi-20260730.png`, SHA-256
`e2c00ecb090a4ac6eb5e93a48cf5562976e8ff1868932552672e9f877c13d0ae`.

Historical hardware release `sprite64-cdc-full2`, now superseded by the
complete renderer below, replaced `boot-text6` and the earlier
`sprite64-full3` candidate. It provides 64 INDEX8 shapes up to
128x128, sixteen per-sprite-selected ARGB palette banks, front/behind planes,
alpha and opacity, scaling, signed positioning and clipping, atomic scene
promotion, and all-pairs collision reporting. A bundled-data CDC correction
delays pixel-domain line-slot capture until one clock after the synchronized
publication toggle; a directed skew test presents the toggle before its tag
and proves that stale line metadata cannot be consumed.

The complete directed suite passes. The exact Beast Vivado 2024.2 production
route completes all 41,778 routable nets with +0.024 ns setup, +0.034 ns hold,
and +0.538 ns pulse-width slack. It uses 21,954 LUTs, 23,003 registers, 85.5
BRAM36-equivalent tiles and 51 DSPs. On Arty, the 64-way stress, fully hidden,
edge-clipped and aligned-grid phases all pass with zero dropped pixels,
overflow, AXI errors or deadline errors. Fully off-screen sprites issue zero
source reads and admit zero pixels. Physical HDMI inspection confirms the
aligned 8x8 grid has no scanline flicker. Exact evidence is in
`docs/evidence/astra-arty-sprite64-cdc-hardware-20260731.log`; the retained
frame is `docs/evidence/astra-arty-sprite64-cdc-hdmi-20260731.png`.

The bounded command/fence transport, descriptor validation, timeout/reset
handling, shared pixel writer, basic clipped fill, and overlap-safe same-format
copy were hardware-qualified at Stage 1 and are retained in the complete
renderer below. Exact Stage 1 checkpoint
`path-boundary-3/full-route-9` routes all 55,816 nets and meets every constraint
at +0.003 ns setup, +0.013 ns hold, and +0.538 ns pulse-width slack. It uses
28,549 LUTs, 33,087 registers, 84.5 BRAM36-equivalent tiles, 61 DSPs, and
10,982 of 13,300 physical slices. The complete directed suite still passes;
the tile workload improves from 1,346 to 1,179 build clocks.

Stage 1 `BOOT.BIN` SHA-256 was
`c118b5a9aa88b1d5d682ce92553b8b45b9133aa9efe1a8b8d5c0432ecd137509`;
the FIT was and remains
`e9ef016f059cb3bc71138edf2a5ae47646a0e11b3dab3b81f7362f592b97b542`.
After reboot, FPGA manager reports `operating`, capabilities are `0x000001ff`,
`/` is read-only, and `/data` is writable. Six consecutive basic-renderer
hardware runs each execute six fenced commands, verify 1,196,608 result pixels,
and report zero backpressure or engine errors. Visible scene promotion and
restore reach generations 6 and 7, and the complete 64-sprite hardware
regression passes unchanged. Evidence is
`docs/evidence/astra-arty-render-basic-hardware-20260801.log`; exact route and
source identities are in `fpga/arty/graphics/TIMING_CLOSURE.md`.

Independent sprite source width and height from 1 through 128 are now an
explicitly certified contract, not merely permissive descriptor fields. The
scene-store regression accepts all 16,384 source-size pairs while preserving
all 131,072 horizontal scaling checks and rejects zero or 129 on either axis.
The line-builder regression performs real first/last-row fetches for every
width and every height, both reflection axes, minimum legal 64/128-byte pitch,
admitted-pixel and AXI-byte accounting, and allocation-bound checks. The public
NDK now reports 64 descriptors, the 8,192-pixel line budget, INDEX8 sources,
independent 1..128 source extents and 1..1024 destination extents.

No sprite RTL or bitstream changed for this qualification. On the Stage 1
`c118b5a9...` Arty release, three consecutive certifier runs, including a
no-reboot repeat, cover every width and height across two packed 64-sprite
scenes. Both dimension phases fetch exactly 4,352 AXI bytes per line, peak near
3,070 build clocks, and report zero drops, overflow, AXI errors or deadlines.
The retained visible scene uses 327,680 bytes rather than the 1 MiB maximum
shape set. That Stage 1 certifier's SHA-256 is
`4692f723917d2589085580f7222c55804da6653e6db1cd77797abfad12b77f3a`;
the complete graphics-regression log is
`/mnt/Documents/astra68/work/sprite-v1/variable-dimensions-1/graphics-regression-20260801.log`
(SHA-256 `f074f620419a2392bab91aded927f5523abd9a0b99683546bbfc0eaa4c629be3`);
the persistent hardware log is
`/mnt/Documents/astra68/work/sprite-v1/variable-dimensions-1/hardware-20260801.log`
(SHA-256 `7955bfb2199dc64af4c36b6cfe12c474a63d999b0355c701dc8d3116fbc44657`).

The complete blitter is now hardware-qualified. Exact production checkpoint
`full-route-24-checkpoint-49` includes framebuffer and tile scanout, boot text,
64 independently sized sprites, command/fence transport, and the complete
blitter. Beast Vivado 2024.2 routes all 59,647 routable nets with +0.013 ns
setup, +0.051 ns hold, +0.538 ns pulse-width slack, and no failing endpoint.
The 74.25 MHz pixel domain has +2.620 ns setup slack. The route uses 30,185
LUTs, 36,050 registers, 84.5 BRAM36-equivalent tiles, 66 DSPs, and 11,695 of
13,300 physical slices.

Active `BOOT.BIN` SHA-256 is
`dfd34dd31bafd199889d7d2cc1f9f2682b72636b296e4f4b3a1964d4ef6acbaa`;
the FIT remains
`e9ef016f059cb3bc71138edf2a5ae47646a0e11b3dab3b81f7362f592b97b542`.
FPGA manager reports `operating`, `/` is read-only, `/data` is read-write, and
the complete splash readback still passes 1,843,200 bytes with CRC32
`611029ee`. Ten consecutive complete-blitter hardware runs each retire 29
fenced commands and verify exactly 1,196,651 pixels with zero backpressure or
engine errors. Coverage includes scaling, X/Y reflection, clipping, keying,
all 16 ROPs, format conversion, premultiplied source-over/opacity, palette
expansion, MASK1 suppression, and overlap-safe copy. Installed certifier
SHA-256 is
`c1ea9c75827c5de62a930ed5119b3ce72e26358146bb98b1c3e6783204f01c5d`.
Exact closure history, source identity, route artifacts, and NAS hardware
evidence are in `fpga/arty/graphics/TIMING_CLOSURE.md`.

Virtual sprites are now certified as bounded groups of ordinary BLIT commands
into hidden surfaces. This deliberately reuses each command's validation,
deadline, completion, and reset contract; the final sequence is the group
fence, and presentation is forbidden unless every completion through it
succeeds. A parent-command hardware batch sequencer was rejected after
behavioral success because it regressed focused 200 MHz timing. Restoring the
qualified command processor reroutes at +0.002 ns setup slack. On the unchanged
Route-24 bitstream, ten consecutive Arty runs each complete 64 scaled RGB565
virtual sprites, verify 16,384 pixels and fence 93, and report zero
backpressure or engine errors. Exact failed experiments and retained evidence
are in `fpga/arty/graphics/TIMING_CLOSURE.md`.

Geometry command validation and dispatch now cover lines, outlined/filled
rectangles, circles, ellipses, transparent/opaque 8x8 pattern fills, and a
bounded scanline flood fill through the shared writer. Flood workspace is
caller-provided and validated; exhaustion returns `WORK_OVERFLOW`. The complete
graphics suite passes, including the focused 60-pixel fill, eight-pixel
overflow case, and integrated 40-command regression.

Geometry is now hardware-qualified at an actual 166,666,672 Hz renderer
clock. The exact checkpoint-44 `full-route-17-166m667` route meets setup at
`+0.060 ns`, hold at `+0.016 ns`, and pulse width at `+0.538 ns`, with zero
failing endpoints. It uses 32,207 LUTs, 39,098 registers, 84.5
BRAM36-equivalent tiles, 70 DSPs, and 12,344 of 13,300 slices. The exact
bitstream SHA-256 is
`b2599c5c3b00f312fc4a8b149944243c0885741f5df061f91d521009ce24472b`;
active `BOOT.BIN` is
`08e188e2747ec801df151e517c50c126029b388ba048af6a95cd22549f24b3c9`.
The unchanged FIT remains
`e9ef016f059cb3bc71138edf2a5ae47646a0e11b3dab3b81f7362f592b97b542`.

The complete 22-program graphics regression passes against the exact source.
Ten consecutive Arty hardware runs then pass the complete blitter, 64-command
virtual-sprite group, line/rectangle/circle/ellipse/pattern batch, exact
60-pixel bounded flood, and one-entry workspace overflow. Every run reports
zero backpressure; geometry takes 18,901 through 19,299 renderer clocks and
overflow containment takes 1,597 through 1,804 clocks. Installed certifier
SHA-256 is
`ca83f3c564613fe88e0cf15399d94b6a5b2200c118b2a53e8202bb5cfdea7d2c`.
Evidence is retained under
`/mnt/Documents/astra68/work/render-v1/flood-1/cp49-postroute-strategy/full-route-17-166m667/hardware-cert`.

The 200 MHz `full-route-14` result remains rejected at
`-0.249/-38.962 ns` across 456 endpoints. A requested 185 MHz build quantizes
to 187.5 MHz and also fails at `-0.218/-12.770 ns` across 176 endpoints. These
measurements establish the 166.667 MHz point rather than a waiver. The
measured 200 MHz path population is distributed across command, writer,
blitter, geometry, flood, framebuffer, sprite, and PS interconnect logic.
Post-route routing/critical-pin optimization gives zero improvement, while
`Performance_ExtraTimingOpt` cannot place more than five percent of movable
instances. Those failed experiments remain relevant if a future feature again
pressures timing; they are not open geometry work.

AFNT glyph expansion is now hardware-qualified for MASK1, A4, A8, INDEX4, and
INDEX8. The first timing-clean candidate exposed a real intermittent AXI
failure on hardware: two of ten runs lost descriptor beat zero when legal
inter-beat backpressure occurred. The receiver now captures each descriptor
beat only on `RVALID && RREADY`, and a focused three-cycle-gap regression
guards that contract. The exact replacement route at 166,666,672 Hz meets
setup at `+0.078 ns`, hold at `+0.015 ns`, and pulse width at `+0.538 ns`; all
68,601 routable nets complete without error.

The complete graphics release now includes dual-bank copper. Each bank holds
4096 instructions in BRAM; WAIT, SKIP, validated MOVE, IRQ, command dispatch,
and hardware-enforced register timing classes are connected. Focused copper
tests and the frozen complete graphics regression pass. Ten consecutive Arty
copper certifications pass bank switching, execution, IRQ delivery, command
dispatch, and containment of an aligned forbidden MOVE target. Ten complete
renderer runs and ten sprite runs also pass with zero backpressure, timeout,
reset, dropped sprite, overflow, AXI, or deadline error.

The exact complete route at 166,666,672 Hz meets setup at `+0.036 ns` and hold
at `+0.016 ns`. It uses 37,534 LUTs, 44,655 registers, 118 BRAM36-equivalent
tiles, 83 DSPs, and 13,036 of 13,300 physical slices. All routable nets are
complete. The first post-route physical-optimization checkpoint left one AXI
address connection incomplete despite meeting timing; `route_design
-preserve` completed it without changing the `+0.036/+0.016 ns` result. The
normal build now runs the same documented repair as a post-route hook and a
clean from-source build independently completed, generated a bitstream, and
passed the exact timing gate.

The hardware-qualified recovery bitstream SHA-256 is
`6281d7cd544e279edf693d1fe41a7e47259845afdc3c3c0d0045e18c04e27879`;
active `BOOT.BIN` SHA-256 is
`9637e1035acb9d1bd6d2bd0eec2e3cf9ca5c13023560af8d2b4f27a546444504`.
The clean reproducibility build bitstream SHA-256 is
`7fdda9ab456d8df7c8d6eacf3c2b337d1409f47bc0ea0888ac51eaa76a125f0c`.
Exact route, regression, deployment, and hardware evidence is retained under
`/mnt/Documents/astra68/work/render-v1/copper-1/integration-13` and recorded in
`fpga/arty/graphics/TIMING_CLOSURE.md`.

The inherited root image still emits nonfatal read-only volatile-directory,
unclean FAT, interface-rename, and resolver warnings. Those host-image cleanup
items are separate from the now-correct graphics memory map.

The older sections below retain ULX3S qualification history and rollback
evidence. Statements below that call ULX3S, 32 MiB SDRAM, 16 sprites, 720x480,
the FPGA TG68K core, NUC attachment, or AstraHost/ESP transport the active
production boundary are historical unless repeated in this override. They
must not be used to infer the Arty architecture.

## Locked architecture

- The sole RTL CPU is the repaired TG68K.C MC68030 integer core with integrated
  PMMU in `fpga/cpu/tg68k_c_030_mmu2`. There is no selectable fallback core,
  FPU, or cycle-accuracy requirement.
- Motorola MC68030 documentation is the architectural authority. Emulator,
  Amiga, MiSTer, upstream, and test behavior may expose a bug but never justify
  an Astra-specific CPU semantic. See [MC68030_COMPLIANCE.md](MC68030_COMPLIANCE.md).
- The core is a strong development baseline, not an unconditional production
  claim. SCC elimination, shared conformance, boot, SDRAM, and retained hardware
  tests pass substantial coverage. The previously open PMMU restart and
  fault-on-stacking cases now pass shared and focused RTL simulation. Exact
  candidate `F4DC1E18` remains historical evidence. K-HW3 now asserts RMC for
  the exact non-idle walker lifetime; K-HW4 snapshots Vesta's IACK result for
  the full CPU transaction and gives timer restart/expiry races a deterministic
  contract. Both deltas pass directed simulation and are integrated into exact
  production build `25D9CB8E`, which is fully routed without timing waivers and
  repeatedly qualified on ULX3S. Its hardware-profile coretest also passes
  translated reads/writes, invalid-descriptor fault recovery, and write
  protection.
- Exact K9 build `7DDD9C03` independently reroutes the complete production
  feature set from source `03660014d7af6d3662504fc076700f04929117ab`, passes
  every constrained clock without a waiver, and passes two ULX3S boots plus
  physical HDMI. Persistent FPGA flash remains the qualified `25D9CB8E`
  rollback; K9 was loaded into volatile SRAM and is not claimed as a flash
  update.
- Portable CPU/PMMU expectations live in `conformance/` and run through the
  canonical Musashi and RTL adapters. Do not create separate expected results
  in an adapter. White-box RTL tests remain complementary for implementation
  invariants that Musashi cannot observe.

## Locked platform boundaries

- The production clocks are 12.5 MHz CPU and 60 MHz SDRAM. The 60 MHz memory
  clock is an accepted architecture decision; HDMI remains 60 Hz, USB remains
  48 MHz, and the CPU clock is unchanged. Correctness and the retained feature
  set are not traded for routing.
- The machine has 32 MiB of SDRAM at `0x02000000..0x03ffffff`. The 68030 logical
  address space remains 32-bit; the PMMU provides process isolation and virtual
  memory. The 32 MiB value is physical RAM, not a 24-bit CPU address limit.
- The production ROM image is `/ASTRA68.ROM` on the existing FAT/exFAT volume.
  AstraHost reads only that file and preserves unrelated card data. Stage 0 is
  the small immutable FPGA loader; the ROM executes from its SDRAM-backed
  overlay. Astra OS will read FAT-family compatibility volumes without making
  them its writable system format. The native journaled filesystem is AstraFS;
  it will use a separate GPT partition through a versioned generic block
  service, and exact version-1 on-disk structures remain to be specified.
- Every ESP32-to-FPGA application, boot, storage, network, input, and control
  transaction uses AstraHost SPI. UART is not a fallback transport. The FPGA
  FTDI diagnostic UART and ESP32's own logging console are independent and may
  be used for POST and bring-up.
- Production audio is assigned to the ESP/AstraHost side and will use the same
  versioned SPI transport. No audio service is implemented yet. Lyra is not
  instantiated or advertised; `0xFFF30000` remains reserved, and `LYRA.md` plus
  `sw/include/lyra.h` are dormant proposal/reference material only.
- CPU-visible MMIO assignments are centralized in
  [MEMORY_MAP.md](MEMORY_MAP.md). Software uses NDK interfaces and resource
  ownership rather than baking register addresses into applications.
- The locked extension policy is versioned protected user-space driver services
  with explicit IRQ, MMIO, and bounded DMA capabilities; that service boundary
  is not implemented yet. There is no public binary kext ABI. The rare code
  that cannot be isolated is source-integrated, rebuilt, and qualified with
  Axiom. Because the MC68030 PMMU cannot constrain FPGA bus masters, raw
  user-programmed DMA remains forbidden until FPGA-enforced bounds or
  kernel-submitted descriptors are qualified. See `KERNEL_ARCHITECTURE.md`.

## Userspace design direction

- Astra OS is GUI-first with a first-class terminal and development
  environment. It takes the Amiga's immediacy, compactness, visible resources,
  message-oriented consistency, and hardware/software fit as inspiration while
  retaining protected address spaces, preemption, bounded services, typed
  handles, and process-level fault containment.
- The native application model remains explicit-spawn, capability-based, and
  service-oriented. A userspace POSIX personality is the planned compatibility
  route for upstream zsh and Vim; Unix file descriptors, signals, process
  groups, and `fork()` do not become the native kernel identity. The exact
  process-clone mechanism is still open and requires a `KERNEL_SPEC.md`
  decision before implementation.
- The planned desktop uses a protected display/compositor service, off-screen
  application surfaces, hardware blits/glyphs, fenced vblank presentation,
  one pointer sprite while the workspace is active, and protected fullscreen
  Scenes. The first userspace acceptance slice is a responsive workspace and
  terminal that survive an unrelated process hang/crash.
- Vega's production contract is exactly 16 sprites with framebuffer X/Y
  scrolling and no tile layers. This reduction from the earlier 32-sprite/tile
  draft was required to fit and route the complete ULX3S design. The draft NDK
  graphics API in `ndk/include/astra/graphics.h` still advertises 32 sprites and
  tile objects; it is stale and must be reconciled with Vega v0.5 before any OS
  graphics ABI is published.
- Small binaries, shared immutable Kits after ABI stabilization, bounded
  service count, and continuous size/launch/resident-memory measurement are
  product requirements. `USERSPACE_BUDGET.md` records provisional first
  envelopes. The first permanent userspace substrate now exists under
  `sw/userspace/runtime`: a versioned 64-byte startup contract, MC68030 `crt0`,
  C-callable trap veneer, typed baseline wrappers, and freestanding byte
  primitives. Its canonical Beast build passes host and sanitizer tests and
  measures 660 bytes of archive-object text plus a separate 46-byte `crt0`,
  with no data or BSS. No ELF loader, supervisor executable, heap, stdio, or
  standards libc exists yet; `USERSPACE_RUNTIME.md` records the boundary and
  next acceptance slice.
- The first reusable storage substrate now exists in `sw/userspace/storage`.
  Its bounded block facade and caller-owned memory/image backend track
  generations, media state, deadlines, errors, sectors, and total/maximum
  latency for query/read/write/flush. A 100,000-operation Beast stress run
  verifies 847,243 sectors against an independent oracle and passes
  ASan/UBSan, GCC `-fanalyzer`, and the MC68030 cross-build. Target size is
  1,492 bytes of text with no data/BSS. `STORAGE_AND_VFS.md` records the exact
  route to protected VFS and shell boot; no filesystem handler, VFS service,
  Arty guest disk, or block syscall surface exists yet.

## Hardware and build topology

- The ULX3S is physically attached to `nuc` at `barry@192.168.1.2`. Flashing,
  FTDI access, HDMI capture, SD maintenance, and hardware acceptance happen
  there. Do not probe Beast or the Mac for the board.
- Beast is the primary high-throughput synthesis/simulation host. The Mac and
  NUC are useful for independent placement/router coverage. Transfer immutable
  artifacts with `rtk proxy rsync`; access remotes through `rtk proxy ssh`.
- Do not treat `/home/barry/astra68` on either remote as a clean canonical
  checkout. On 2026-07-15 NUC's copy was a dirty historical `harte-harness`
  branch, and Beast's path was not a Git worktree. Do not pull, reset, clean, or
  release from either path. Build and test pushed `main` from a fresh immutable
  `/tmp/astra68-<checkpoint>` bundle, and keep the older remote state intact.
- Beast has the intended GCC, m68k cross compiler, OSS CAD, and static
  analyzer, but its GHDL binary is not on the non-login SSH path. Prefix that
  path with `/home/barry/oss-cad-suite-install/oss-cad-suite/bin`; its system
  Python still lacks `pytest` and it has no Docker. NUC provides both GHDL at
  `/home/barry/oss-cad-suite/bin` and `pytest`, and is the verified host for the
  complete shared architecture/Harte target. The
  Mac has Homebrew `m68k-elf-gcc` 16.1.0. Shared Makefile detection selects
  `m68k-elf-` there and `m68k-linux-gnu-` on Linux while preserving explicit
  `CROSS=...` overrides. Beast's Linux cross compiler remains the canonical
  release compiler. The Mac's pinned Docker/OrbStack path is the
  verified NDK documentation builder; do not weaken the GCC analyzer or docs
  gate to fit the wrong host.
- OSS CAD revisions differ between hosts. A route made with another nextpnr or
  Yosys revision is useful diversity, not a controlled same-seed comparison.
  Record the exact host and tool identities with every retained artifact.
- Never clone a mutable build directory with hard links or `rsync --link-dest`.
  Yosys and ROM staging overwrite generated files in place and can silently
  mutate an earlier checkpoint. Snapshot sources independently and keep routed
  JSON artifacts immutable.
- Do not copy generated binaries between Mac and Linux work trees. A first NUC
  conformance attempt inherited current-looking Mach-O Musashi and GHDL
  executables from a Mac workspace and failed with `Exec format error`. Clean
  both targets or exclude build outputs before rebuilding natively; the clean
  historical NUC run passes all 90 unit tests, its 28-case shared matrix, and
  both Harte smoke targets.
- The checked-in ESP32 maintenance bridge is SRAM-only infrastructure. Board
  revision 3.0.8 uses the proven v3.1 ESP route. FPGA pin J3 is shared by ESP
  GPIO2 and SD D0/MISO, so the bridge may drive it low only while FTDI DTR
  requests download mode and must otherwise release it. A permanently driven
  low J3 permits ESP programming but breaks SD; an always-high-Z J3 permits SD
  but makes ESP flashing unreliable. See `fpga/maintenance/README.md`.

## Current integration state

- The unpromoted working tree based on
  `4579138eb3d30e26aef658252fba28b15dd33420` adds the native boot splash and
  hardware-rendered status text. The canonical image is 720x480 INDEX8 with at
  most 252 image colors; palette entries 252-255 are reserved for status text.
  Its framebuffer, BGRA palette, and 8x16 MASK1 font occupy 350,720 bytes raw
  and 86,654 bytes as a reproducible legacy-LZ4 payload. For text, firmware
  builds fixed glyph descriptors only; Astraea executes the MASK1 draws, while
  Vega presents the double-buffered scene at vblank.

  Integration exposed and fixed an Astraea MMIO bug: one MC68030 longword read
  arrives as 16-bit beats at register offsets `+0` and `+2`, but the special
  register decoder previously matched only the first byte address. Aligned
  word-index decoding now returns both halves of ID, capability, IRQ, and
  copper-source registers, and the directed chip test models the real two-beat
  bus contract.

  Exact Beast snapshot `/tmp/astra68-splash-hwglyph-20260727` passes the full
  directed graphics suite, both CPU blitter diagnostics, the C LZ4 test, and
  all 32 Python boot tests. A fresh Verilator 5.047 full TG68K/SDRAM/HDMI boot
  executes the 222,732-byte ROM with CRC32 `3429C216`, reports splash startup
  in 8,823,984 CPU cycles, passes POST, loads the kernel, and exits zero at
  `Starting Axiom kernel` with
  `ASTRAHOST BOOT SPLASH PASS blit=1 draw=24 MASK1=24`. The test observes
  Astraea draw-master writes to the sampled `OK` pixel in both framebuffers;
  CPU software rasterization cannot satisfy it.

  Exact build `320CAE59` then routed the complete production feature set with
  zero SCCs and passed every clock, including 14.044352 MHz CPU and 66.600067
  MHz SDRAM. Its first ULX3S boot rejected generation 1 at `Graphics splash`
  while the cleanup generation 2 completed. The hardware-only phase exposed
  that Vega treated every routine baseline-to-active scene copy as a shadow
  lock. A simple unlock was also incorrect because palette and sprite RAMs
  share the copy port and would silently lose overlapping writes. The retained
  fix leaves scalar shadow state editable and pauses a non-present baseline
  copy for one cycle whenever a palette or descriptor write needs that port;
  pending and committing generations remain immutable.

  Fixed Beast snapshot `/tmp/astra68-splash-hwglyph-vega-lock-20260727`
  passes a forced scalar/palette/sprite overlap test, the full directed graphics
  suite, and an exact release-ROM TG68K/SDRAM/HDMI boot. The deterministic
  223,004-byte ROM has CRC32 `84E611A6`, a fixed
  `2026-07-27T21:02:28Z` timestamp, and revision
  `4579138eb3d30e26aef658252fba28b15dd33420-dirty-c53e68b7`. It reports
  8,800,670 splash cycles and exits zero in 539.667 seconds with
  `ASTRAHOST BOOT SPLASH PASS blit=1 draw=24 MASK1=24`. The exact 25-file
  source manifest is
  `docs/evidence/astra68-splash-source-manifest-20260727.sha256`, whose SHA-256
  is `c53e68b7f9be69917c07aa31a66a9b78552254013b822bf889974c52fbd026d1`.
  Build `320CAE59` is rejected. Exact fixed build `C53E68B7` now routes the
  complete production feature set on Beast with zero SCCs and no timing
  waiver. Yosys 0.64+159 reports 53,641 LUT4s, 25,991 mapped FFs, 103
  DP16KDs, and 18 multipliers; nextpnr 0.10-45-g98c18d7f packs 67,295
  TRELLIS_COMB and 26,024 TRELLIS_FF cells. The exact route reaches
  14.127087 MHz CPU and 67.971725 MHz SDRAM against the required 12.5 MHz and
  60.002399 MHz constraints, and every input, SD, USB, pixel, and HDMI shift
  constraint also passes. Bitstream SHA-256 is
  `9c6a1f575596bf612fa9649940a3c3a65758aa7e55684cebf3b42ba62c576b46`.
  Repeated SRAM-only ULX3S boots and physical HDMI qualification remain before
  release; persistent flash remains rollback build `25D9CB8E`.
- K10 device and observability support is the active pre-route candidate. It
  adds 16 generation-safe IRQ endpoints with four fixed records each, one
  bounded common Vesta dispatcher, typed MMIO accessors, first-fault bus
  diagnostics and target timeouts, a 64 KiB allocation-free retained trace,
  eight deferred-work classes on the existing guarded worker, and one bounded
  monitor parser shared by FTDI and AstraHost SPI. The user ABI is
  `0x00010005`; arbitrary source binding remains privileged and no device
  policy moved into Axiom.

  The candidate passes all 28 host suites normally, under GCC
  ASan/UBSan/leak checks, and under GCC `-fanalyzer`; the MC68030 cross-build,
  NDK SDK and generated HTML/PDF documentation, Rust, shared architecture,
  Harte smoke, focused USB, bus-fault, SDRAM-timeout, framebuffer-guard, and
  all 29 hardware-checker cases also pass. Fresh Beast Verilator 5.047 snapshot
  `/tmp/astra68-k10-final-sim-20260727b` completes the full HDMI-enabled
  pin-level run in 2,051.123 seconds and exits zero after
  `ASTRAHOST KERNEL ENTRY PASS`; its 154,905-byte durable transcript is
  `/tmp/k10-final-hostboot-20260727b.log`. It covers 32 MiB BIST, PMMU,
  scheduler/fault containment, five required device sources delivered and
  acknowledged, exact owner-death cleanup, and AstraHost-SPI monitor output.
  Removing only Verilator's `UART: ` transcript prefix makes the simulated
  output pass the strict raw-serial `--expect-k10-device` gate. Exact committed
  rerun, production route, immutable artifact identities, and repeated ULX3S
  checks are still release gates. K10 must not be described as
  hardware-qualified; K9 remains the current hardware-qualified release and
  rollback.

  Exact routed candidate `E671488A` subsequently passed all release timing
  constraints but is rejected on ULX3S: K10 repeatedly receives 4/5 required
  sources, with only USB `0x80` absent. Exact routed-net inspection found the
  system, USB, and video PLLs operating at 5, 4, and 5 MHz PFD respectively,
  below Lattice's current 10 MHz ECP5 minimum. The local experimental PLL
  calculator had incorrectly accepted 3.125 MHz. The corrected three-PLL
  topology preserves exact 60 MHz SDRAM, 48 MHz USB, and 27/135 MHz video
  clocks while using 20-25 MHz PFDs and 480-675 MHz VCOs. A mandatory
  synthesized-primitive gate now verifies those ranges and all output dividers.
  Exact Beast synthesis and focused USB tests pass; corrected placement, route,
  and SRAM-only board confirmation remain pending. See the latest section of
  `fpga/soc/oss_flow/TIMING_CLOSURE.md`. Persistent flash remains exact
  `25D9CB8E`.
- The hardware-qualified K9 memory-pressure release is exact implementation
  commit `03660014d7af6d3662504fc076700f04929117ab`, built reproducibly with
  `SOURCE_DATE_EPOCH=1785033792` (`2026-07-26T02:43:12Z`). Its immutable source
  archive SHA-256 is
  `db884481ef58f27ed4be2823c57a43089b24d140c160d1f512b89f89547151a5`
  and is independently extracted as `/tmp/astra68-k9-0366001` on Beast and
  NUC.

  K9 places every live fixed object behind one typed cache authority, records
  stable allocation-site IDs 1-23, and makes all 22 external allocation sites
  injectable through both global-Nth and site-Nth selectors. It reserves
  exactly 32 physical pages for fault, cleanup, and future monitor work;
  ordinary allocators cannot consume them. The boot allocator retires once,
  every allocation is charged to a subsystem ledger, and teardown remains a
  bounded zero-allocation path. The existing object, queue, and process limits
  are unchanged, and K9 deliberately does not add a general kernel heap.

  All 21 host suites pass normally, under GCC ASan/UBSan/leak checks, and under
  GCC `-fanalyzer`. NDK host, sanitizer, analyzer, MC68030, HTML/PDF, Rustfmt,
  Clippy, all 15 Rust tests, all 90 shared tests, all 30 architecture adapter
  executions, and both Harte smoke adapters pass. Normal Musashi reaches K1-K8
  with the reserve at 32/32 in 28,000,288 cycles. The exact 1,000-iteration
  workload completes in 603,007,142 of 675,000,000 allowed cycles, returns to
  its 7,954-free-page baseline, and reports zero overruns. A clean Beast
  Verilator 5.047 build and pin-level SDRAM run pass 64 KiB BIST at
  115.02 MB/s, exact allocation cleanup, every K1-K8 marker, and all 20 fixed
  cycle gates. No performance budget was raised.

  The exact 90,132-byte kernel SHA-256 is
  `6d9397b044e133bb9e04750d78cd46e3b32ea1e418d553e44c9e97f958b6d823`.
  The 101,544-byte payload has CRC32 `8E57D4DA` and SHA-256
  `996f2305477067b2793e678ec93a8a536aba10c677b343d747a05f22d445456d`;
  the 101,576-byte package SHA-256 is
  `989e02cdea3722eb7a53037815d6d6bf6b67f97869f29c46cab354473e1bcddd`.

  The complete exact route reports build `7DDD9C03`, zero SCCs, 53,079 LUT4s,
  25,532 mapped FFs, 101 DP16KDs, and 18 multipliers. It packs 66,523
  TRELLIS_COMB and 25,565 FFs and passes at 15.058201 MHz CPU, 66.907532 MHz
  SDRAM, 79.693970 MHz USB, 53.267990 MHz pixel, and 289.771088 MHz HDMI shift.
  The retained placement-only estimate misses CPU/SDRAM at 12.03/52.23 MHz;
  it is diagnostic, and the exact route resolves both domains without a source,
  constraint, feature, or floorplan change.
  Exact bitstream SHA-256 is
  `cf1adbe78cb9f486b3d2fbae36ada91023fda36d8cb4b0ffec7df5828e3c6bf1`.
  NUC preserved the existing 244,016 MB card, changed only `/ASTRA68.ROM`,
  independently verified it, and restored exact read-only AstraHost. Two
  independent volatile ULX3S loads pass full 32 MiB POST/BIST, exact source and
  ROM identities, the 32/32 reserve, the allocation ledger, K1-K8, and every
  cycle gate with zero overruns in 4.022 and 3.914 seconds. The second run also
  has retained physical HDMI evidence. Persistent FPGA flash remains exact
  build `25D9CB8E`. K9 is current; K8 is its qualified rollback.
- The hardware-qualified K8 shared-area and bounded bulk-ring release is exact
  commit `56bd1770c834205a4dccc42efb61552a77647988`, built reproducibly with
  `SOURCE_DATE_EPOCH=1785023940`. Its immutable source archive SHA-256 is
  `b0db820437d5526b9157815c5a280770e59e21930c39d48dddb6d21389d5cb49`
  and is extracted as `/tmp/astra68-k8-56bd177` on Beast and NUC.

  K8 adds eight fixed shared areas, 32 mappings, 16 SPSC bulk rings,
  reduced-right handle duplication, real page commit, transactional mapping,
  one logical address per area across processes, batched notifications,
  wait-multiple composition, and exact peer/creator-death cleanup. The current
  profile permits 1-16 committed 4 KiB pages per area, 128 area pages globally,
  64 per creator, and four mappings per process. Ring payload remains in the
  area and consumes no kernel payload queue.

  All 20 host suites pass normally, with GCC ASan/UBSan/leak checks, and under
  GCC `-fanalyzer`; the NDK host, sanitizer, MC68030, HTML, and 106-page PDF
  gates pass. Normal Musashi reaches K1-K8 in 24,750,250 cycles. The exact
  1,000-iteration workload completes in 576,508,485 of 675,000,000 allowed
  cycles with 7,986 free pages and zero overruns. A clean Beast Verilator 5.047
  build and pin-level SDRAM run pass 64 KiB BIST at 115.03 MB/s, every K1-K8
  marker, exact cleanup, and all 20 cycle gates. Pin-level area
  create/map/unmap/ring-notify maxima are 37,762/56,097/71,283/29,390 cycles.

  The exact 81,768-byte kernel SHA-256 is
  `ea879e760c48342f535ee9aee65bf1bab97e855c2e576579c2ab80ef615ba55b`.
  The 93,180-byte payload has CRC32 `BE5F5D5D` and SHA-256
  `a70ae3323884ffb3eccaa70b4e4c6be34a2e0a4aa044cee99a632375b6faba2a`;
  the 93,212-byte package SHA-256 is
  `ae6bab5ab9a249211ef0b7f1daccb7e00ddb9e683facbe7ecfd7b0d6307d17a8`.
  NUC preserved the existing 244,016 MB card, changed only `/ASTRA68.ROM`,
  independently verified it, and restored exact read-only production
  AstraHost. Two independent ULX3S loads of unchanged production bitstream
  `25D9CB8E` pass full 32 MiB POST/BIST, exact source/ROM identity, K1-K8,
  every lifecycle count, and all 20 budgets with zero overruns. Hardware run 1
  measures create/map/unmap/notify at 37,763/56,091/71,263/29,416 cycles; run 2
  measures 37,742/56,106/71,263/29,416. No RTL, route, resource, clock, or
  persistent FPGA-flash result changed. K8 is the hardware-qualified rollback
  for K9; K7 is its predecessor.
- The hardware-qualified K7 bounded message-port rollback release is based on
  commit `1529496c168975cee0fc46c7955f98ab4a1b8d2b` plus implementation patch
  SHA-256
  `815347c8d094a1507b94ac5f8acb7636903d8eaf6fe8457e2f7a0d641763906e`.
  The exact source identity is
  `1529496c168975cee0fc46c7955f98ab4a1b8d2b-dirty-815347c8d094`; its source
  archive SHA-256 is
  `79965cebb6da2aefb435d655ee279cd37f8f98bb1fb1dc4ef7c5efa53ebe6e09`.
  K7 adds 16 receiver-owned ports, 32 fixed copied-message records, and a
  256-entry detached-authority pool. Messages are 24-280 bytes and may move up
  to eight handles. Per-port limits are 1-8 messages and 24-2,240 bytes;
  per-owner limits are four ports, 16 queued messages, 4,480 queued bytes, and
  128 detached authorities. All capacity is fixed and receiver-charged.

  Send and receive are transactional. Export reserves and validates every
  source handle before one commit; failed sends preserve source authority.
  Receive reserves hidden destination slots, completes user copyout, then
  publishes handles and dequeues. A copy fault consumes neither the message nor
  authority. Final receive close and owner death wake peers and release queued
  messages exactly once. Raw syscalls are nonblocking; the NDK composes
  blocking and absolute-deadline operations through K6 wait-multiple. The
  failed-send sequence covers both count and byte capacity, preventing a lost
  wake between observing backpressure and blocking.

  Current MC68030 sizes are 472 bytes/process, 180 bytes/thread, 36
  bytes/synchronization object, 64 bytes/port, 324 bytes/message, and 24 bytes
  per detached authority. The incremental K7 fixed state is 17,872 bytes under
  a 20 KiB ceiling. The hard development limits remain 4 processes, 16 global
  threads, 16 handles/process, 16 wait-set members, 32 synchronization objects,
  32 timer-heap entries, 16 ports, 32 queued messages, and 256 detached
  authorities.

  All 18 exact host suites pass. The same implementation passes analyzers,
  sanitizers, NDK and MC68030 builds, generated HTML/PDF documentation,
  Rustfmt, Clippy `-D warnings`, all 15 Rust tests, all 90 shared tests, all 30
  adapter executions, and both Harte smoke adapters. Normal Musashi reaches
  every K1-K7 marker. The exact 1,000-iteration workload completes in
  563,507,415 cycles against the 675,000,000 cap with 7,986 free pages and zero
  overruns. A fresh full-SoC Verilator/pin-level SDRAM run passes 64 KiB BIST at
  115.04 MB/s, exact K7 counters, all K1-K7 markers, and all sixteen cycle
  budgets. Port send/receive maxima are 12,256/25,000 and 17,817/30,000 cycles.

  Two rejected profiling checkpoints found exhaustive detached-pool/object
  validation in production maintenance and a 256-entry high-water scan on
  successful export. The retained implementation uses transition-maintained
  O(1) live/high-water counters and corruption latches in production while
  preserving exhaustive validators in host, maintenance-diagnostic, and
  milestone checks. No performance budget was raised. The final 69,496-byte
  kernel SHA-256 is
  `4c9d3807c95a701d8e2f16f52b1321fd97c0393798656ea589e6197c9dfccd4e`.
  The 80,944-byte boot payload has CRC32 `B124CB22`; the 80,976-byte package
  SHA-256 is
  `4abbc4471f8a84eaec770cab3758f27cb98e1a0768c6ef85295033834b70fd81`.

  NUC mounted the existing 244,016 MB card without formatting, changed only
  `/ASTRA68.ROM`, verified the installed payload independently, and restored
  exact read-only AstraHost. Two independent loads of unchanged production
  bitstream `25D9CB8E` pass full 32 MiB POST/BIST, PMMU/user-copy isolation,
  exact K7 counts, every K1-K7 marker, all sixteen cycle gates, and zero
  overruns. Run 1 measures port send/receive at 12,256/17,883 cycles; run 2
  measures 12,257/17,880. FPGA RTL, resources, route, clocks, persistent flash,
  and bitstream SHA-256 remain unchanged. K7 is the predecessor to K8 and K9;
  K6 is its predecessor.
- The hardware-qualified K5 thread-lifecycle predecessor is based on commit
  `0208cb516801fe452bf59ef053d6daa0a118ee7e` plus qualified implementation
  patch SHA-256
  `1a234d1e5099e31f11e0288bc3e0e40e499fc31de1d6c1333a2138a25aa79e41`.
  The exact ROM identity is
  `0208cb516801fe452bf59ef053d6daa0a118ee7e-dirty-1a234d1e5099`. K5 adds
  bounded transactional `THREAD_CREATE`, caller-only `THREAD_EXIT`, and
  level-triggered thread-death `WAIT_ONE` to the K4 synchronization base. A
  live thread owns one zeroed 4 KiB user-stack frame, one unmapped logical
  guard, one fixed guarded 8 KiB supervisor stack, one 180-byte thread record,
  and one process-local generation-safe handle. A dead thread's user mapping is
  reclaimed by the guarded worker; its record and supervisor slot remain only
  while an open handle makes the exit status observable. Exit, timeout,
  cancellation, final close, and process death complete a death wait exactly
  once. There is no asynchronous thread kill.

  Thread creation is split into interruptible preparation and a no-allocation
  commit. Publication, ready-queue insertion, and process/scheduler accounting
  occur together with interrupts masked. Final audit superseded patch
  `020f9460a270...`: its syscall enabled interrupts while publishing, allowing
  a nested supervisor timer to enqueue another thread into the same intrusive
  queue. A deterministic host test injects that timer at the last possible
  enable-to-disable boundary and proves both enqueue operations survive.

  All 17 host suites pass normally, under GCC `-fanalyzer`, and under
  ASan/UBSan/leak checks. NDK checks, all 15 Rust tests, rustfmt, Clippy, all 90
  shared tests, all 30 Musashi/RTL executions, and both Harte smoke adapters
  pass. A forced clean MC68030 build produces and verifies the qualified
  artifacts. Exact
  normal Musashi reaches all K1-K5 markers in 20,000,212 cycles. Its
  1,000-cycle workload completes in 600,506,981 cycles under the fixed
  675,000,000 cap, retains exactly 7,986 free pages, and has zero overruns.
  Exact artifacts are a 48,420-byte kernel with SHA-256
  `260bbcf82fbf955cee42d5798054e6d6549daa8921462d7216a241a685095e03`,
  a 59,804-byte payload with CRC32 `11BE3620` and SHA-256
  `8dec0a9ae9ce03f19d5be8c5e8f016dc52684d53828e5acf849a4e76f992527c`,
  and a 59,836-byte package with SHA-256
  `6f4c3376597884ac1ff1d544a62b3af97b2e8502731b751eae964fc598d6bdb9`.
  The package hash was verified after transfer to both Mac and NUC.

  The fresh pin-level Verilator 5.047 run passes 64 KiB BIST at 115.04 MB/s,
  every K1-K5 marker, and all cycle gates in 296.331 seconds;
  create/exit/reap measure 137,193/14,804/47,560 cycles. NUC preserved the
  existing SD contents, changed only `/ASTRA68.ROM`, and restored a freshly
  built read-only AstraHost. Two independent volatile loads of exact build
  `25D9CB8E` pass CRC32 `11BE3620`, full physical 32 MiB POST/BIST,
  PMMU/user-copy isolation, exact K5 lifecycle counts, every K1-K5 marker, and
  all twelve performance gates with zero overruns. Run 1 measures
  137,107/14,816/47,534 lifecycle cycles; run 2 measures
  137,101/14,812/47,534. K4 is the rollback checkpoint. K5 changes production
  software and acceptance only: SoC synthesis, placement, routing, resources,
  constrained clocks, persistent FPGA flash, and bitstream SHA-256 remain
  unchanged. It is the K6 rollback checkpoint.
- The hardware-qualified K3 predecessor is represented on `main` by
  `3787d820e1140f49ba31623ccc578bb274a631cc`. Its retained target artifact was
  developed from `8929c063cdd24c8f4f526be330549e2eb5038fc8-dirty`, retains the K2
  process/thread split and adds exact 5 ms one-shot quanta plus one fixed
  16-entry deadline heap. Vesta is programmed to the earlier of the running
  thread's 62,500-cycle quantum and earliest absolute wait deadline. Ordinary
  syscalls do not renew the quantum. Timeout, signal, close, and process death
  withdraw a waiter exactly once; a higher-priority signal or timeout preempts
  immediately. All-blocked operation retains the deadline while the guarded
  supervisor worker idles. No timer or deadline path allocates.

  Seventeen host suites, GCC `-fanalyzer`, ASan/UBSan/leak checks, canonical
  m68k verification, every Rust gate, all 90 framework tests, all 30 shared
  executions, both Harte smoke adapters, and the directed Vesta timer/IACK race
  test pass. Exact normal Musashi reaches K3 in 17,250,229 cycles. The exact
  1,000-cycle workload completes in 596,507,297 cycles against the unchanged
  675,000,000 cap, 49,251,790 cycles faster than K2. The kernel is 41,020 bytes
  with SHA-256
  `6ab38364d2ef5e67b6f5e8c7fb691cbf45291624562d7a0203f812c2e648e61d`.
  The 52,444-byte HDMI payload has CRC32 `BAEF4D0B`; the 52,476-byte package
  SHA-256 is
  `b73964d87904994a570c3b5e2b931602f8eb7878f0b531c0ac7e775050919ab1`.

  The complete pin-level model passes with an intentional 64 KiB simulated
  BIST and a 6,163/20,000-cycle deadline maximum. NUC preserved the existing
  card, changed only `/ASTRA68.ROM`, independently verified the installed file,
  and restored read-only AstraHost. Two independent loads of exact production
  bitstream `25D9CB8E` pass the real 32 MiB BIST, PMMU isolation, one-shot
  preemption, timeout handoff, all K1/K2/K3 markers, and every performance gate;
  each measures 6,177/20,000 deadline cycles with zero overruns. This is a
  software-only checkpoint: persistent FPGA flash remains `25D9CB8E`, and no
  synthesis, placement, route, timing, or utilization result changes. NUC has
  no HDMI capture device; the unchanged HDMI pipeline retains exact physical
  K1 qualification, while a K3 screen photograph remains visual evidence only.
- The minimum kernel-boot hardware is now implemented rather than stubbed:
  Vesta provides a 32-source programmable vectored interrupt controller and
  two timers; AstraHost provides queued runtime block I/O and normalized input
  events over SPI; an integrated OHCI controller provides one low/full-speed
  USB host port for keyboard and mouse; every platform IRQ is wired through
  Vesta to TG68K; and the kernel starts a checked 5 ms one-shot scheduler timer
  before entering user mode.
- POST, front-panel MMIO, cache coherence, byte/address lanes, 32 MiB SDRAM
  BIST, Astraea DMA, SDRAM-backed ROM handoff, Vesta interrupt acknowledge,
  runtime block/input transport, and kernel entry pass focused and full
  pin-level simulation. The full boot reaches `K0 ENTRY PASS` and
  `KERNEL IDLE`; the retained 60 MHz BIST measures roughly 115 MB/s in
  simulation.
- Astraea and Vega retain the framebuffer, pixel-granular X/Y scrolling and
  wrap, 16 sprites, copper, blitter, line/rectangle/ellipse/flood/pattern
  drawing, shadow-scene presentation, and scanout paths. Tile layers are
  retired. At 60 MHz, directed tests pass and the integrated normal, INDEX8,
  and RGB565 workloads remain below the 1906-clock scanline deadline at maxima
  of 505, 1103, and 1429 clocks.
- The shared Musashi/RTL matrix passes all 30 executions from an exact source
  snapshot, and both adapters pass the retained Harte smoke targets. Focused
  Vesta, AstraHost runtime/service, SDRAM bridge, ESP host, boot, NDK, and OSS
  release tests pass. This is enough for supervisor-mode kernel development,
  not a waiver for the remaining protected-multitasking DMA/IRQ dependencies.
- The 2026-07-22 K1 PMMU checkpoint closes the two exception/restart blockers
  in simulation. Separate-SRP/CRP `MOVES.B` absolute, postincrement, and
  predecrement writes plus a postincrement read each fault, enter vector 2,
  repair translation, return through an unmodified format-B `RTE`, update
  their address registers once, and perform one target transfer. A cold table
  walk for the translated supervisor exception stack also completes without a
  second fault or CPU halt. Beast passes 90 framework tests, all 30 shared
  Musashi/RTL executions, both Harte smoke targets, and the 137-variant Questa
  inventory at 111 clean, 18 classified failures, 3 stale compile failures,
  and 5 unscored diagnostics. The repaired source is committed as
  `5798c5575a5bf6d5ca37eae2fbe63cb0528ad6e8`. Exact production candidate
  `F4DC1E18` routes every constrained clock, and three independent SRAM reloads
  match its build and ROM identities while passing complete POST, 32 MiB BIST,
  DMA, runtime/input initialization, and `K0 ENTRY PASS`. This proves that the
  repaired netlist did not regress the nontranslated platform. A subsequent
  hardware-profile coretest passes with sum `74A6EC6D`, including PMMU register
  access, cold table walks, translated read/write, invalid-root access-fault
  recovery, and write-protection/no-write checks. Persistent hardware remains
  `6C0D0CA3`. The follow-on table-walk arbitration fix passes component and full
  coretest simulation but has not yet been synthesized, routed, or exercised
  on the board; HDMI and persistent-programming gates also remain.
- The follow-on K1 software checkpoint in independent Beast snapshot
  `/tmp/astra68-k1-p27` boots with the PMMU enabled and exercises production
  user-copy recovery rather than a synthetic handler. The kernel now has a
  bounded physical-frame allocator, owned DMA/block submission models, 4 KiB
  10/10/12 SRP/CRP tables, per-owner address spaces, guarded user-range checks,
  exact format-0/1/2/9/A/B exception decoding, and SFC/DFC `MOVES` user copies.
  Its startup check creates a real CRP, maps a process-owned page, verifies
  copy-in/copy-out, deliberately faults both read and write access to an
  unmapped page, returns through the vector-2 format-B fixup, and proves every
  temporary frame was reclaimed. The exact 29,140-byte boot binary
  (`382eddc628a7e2bebf416d31b9441f0ba2e7fc863e5a360dc96d12da6415fc4a`)
  and 17,860-byte kernel
  (`979347f21105a865243b5076b4ef9b04a9a3212fa386a2aebaf43f4177dae0ab`)
  reach `K0 ENTRY PASS` on both the full pin-level RTL/SDRAM model and AstraVM's
  vendored Musashi 68030/PMMU backend. Host unit tests, GCC `-fanalyzer`,
  ASan/UBSan, Rust unit tests, formatting, and Clippy `-D warnings` pass.
  This is simulation evidence only. Process/handle tables, trap ABI, saved
  contexts, user-mode entry, 100 Hz preemption, process-local fault death,
  teardown/soak, exact full routing, and board promotion remain K1 work.
- Independent Beast snapshot `/tmp/astra68-k1-p28` completes the K1 process
  path in simulation. It adds typed process handles, saved MC68030 contexts,
  the trap-based syscall ABI, two isolated ROM-packaged user programs, 100 Hz
  timer preemption, process-local fault death, and owner teardown. Bounded
  process-context maintenance now completes teardown that was deferred while
  device DMA was pinned without doing that work in the hard timer path, and the
  last process may exit or fault into an interruptible supervisor idle loop
  instead of causing a kernel panic. A fault
  found only after repeated trap/RTE and timer traffic exposed an RTL
  exception-entry defect: `arch_abort_pending` suppressed the A7 bank switch,
  so a user data-fault format-B frame was stacked on USP. The retained kernel
  fix permits only the exception-entry mode-switch write while an access fault
  is active and prevents a zero-wait fault response from being mistaken for a
  second stacking fault. Focused Questa runs pass with both inserted memory
  wait states and zero-wait responses. The supervisor tree now contains an
  exact unmapped stack guard; a deliberate access reaches the retained panic
  oracle with a format-A fault at `0x02028000`. Whole-space teardown invalidates
  caches before frame reuse, a fixed one-bit-per-frame ledger rejects duplicate
  cached user aliases, and two processes execute different code/data at the
  same logical addresses. The CACR command decoder now preserves independent
  instruction/data commands when both are written together. The production-form
  35,260-byte boot image
  (`7fa58266c26a3b3d679254235c3be2ad079f5f8710174940e6246e52e820022b`)
  runs unchanged on AstraVM/Musashi and the complete pin-level RTL/SDRAM model.
  Both enable the PMMU, start two user processes, preempt at 100 Hz, reap only
  the deliberate offender, reclaim its owned state, and reach
  `K1 PROTECTED ENTRY PASS` with scratch status `K1OK`. Host tests,
  `-fanalyzer`, ASan/UBSan, Rust tests, rustfmt, Clippy, all 90 shared framework
  tests, all 30 shared Musashi/RTL executions, both Harte smoke adapters, all
  directed graphics tests, and the 139-variant CPU inventory at 113 clean with
  the unchanged 3/18/5 classified buckets pass. A deliberate panic also reaches
  the console and retained early log in the full SoC model; both direct panic
  and exact supervisor-guard panic have independent full-SoC checks. The final
  committed normal image is 23,912 kernel bytes, runs unchanged on both models,
  and reports three context switches on each before `K1OK`. Source commit
  `66d6094f9339469313fefb70b259d07a7c2272ce` supplies the full ROM and kernel
  identity. Its exact full synthesis and placement now pass, but it still needs
  a completed strict route,
  repeated ULX3S qualification and long allocation, syscall, fault, and
  context-switch soaks before K1 release.
- Exact follow-on commit `470bf123cf24bbadf3525f91307e3d9aebe92006`
  adds the shared K1 lifecycle soak: each cycle faults and reaps one offender,
  verifies resource baselines, and relaunches it while the survivor remains
  schedulable. The immutable NUC snapshot has Git-archive SHA-256
  `b5db0e133ee04605fc1e18e4a159e1893893ca5c90c54df1c2ad8bcfc0c64fa5`.
  Host analyzer/sanitizer gates, an exact 100-cycle Musashi run, and the exact
  full pin-level RTL workload pass. RTL completes four post-milestone teardown
  cycles with 11 context switches, 23 timer ticks, 96 syscalls, and the exact
  7,987-free-page baseline. Independent release-duration Musashi runs on Beast
  and NUC both complete all 500,000 lifecycle cycles without drift. Beast
  reaches virtual cycle 1,252,807,889,504 after 12,416.334 seconds; NUC reaches
  virtual cycle 1,252,809,374,217 after 46,333.788 seconds with 1,000,001
  context switches, 2,022,386 timer ticks, a nonzero syscall total, and the
  exact 7,987-free-page baseline. Routed-hardware completion remains open and
  must not be inferred from simulation.
- Motorola-directed reset correction
  `c599f921cb35dcc7e8d2988ba253769341311516` separates ECP5 configuration
  initialization from MC68030 processor reset with one configuration-initialized
  bit. Its first released clock clears scalar PMMU state and invalidates the
  ATC; processor reset cannot re-arm it. `RESET` clears only TC/TT enable bits
  and preserves roots and valid ATC entries until software flushes them. The
  focused regression retains a deliberately stale ATC entry, checks
  CRP/SRP/control-field preservation, executes the same pre-enable `PFLUSHA`
  invariant used by K1 boot, and proves the next access walks to the changed
  descriptor. Beast's strict Questa inventory is 140 total and 114 clean; its
  prior 3 compile, 18 simulation, and 5 unscored classifications are unchanged.
  GHDL 7.0 generates the core without the aggregate-initializer crash or an ATC
  payload-wide startup mux. A byte-identical pre-commit reduced-BIST full
  pin-level SoC run passes POST, two-process 100 Hz preemption, offender-only
  fault containment, and four lifecycle cycles at the exact 7,987-page
  baseline. The already-
  running `66D6094F` route predates the correction and cannot be the release
  image; corrected exact synthesis, strict routing, and board reset/boot
  qualification are mandatory.
- Immutable corrected snapshot `77b3cdc8fddb984850073a2c2cb5998bbbe1d857`
  has archive SHA-256
  `678f4bb31a8c652615675b871274c992fde08d648a0e6f0a2e135361d168dbb5`.
  The exact normal image passes unchanged on Musashi and the complete
  pin-level RTL/SDRAM model. The latter runs full 32 MiB BIST at 115.06 MB/s,
  enables the PMMU, starts two isolated processes, preempts at 100 Hz, reaps
  only the deliberate offender, reports three context switches, and reaches
  `K1 PROTECTED ENTRY PASS`. All 90 framework tests, all 30 shared adapter
  executions, and both Harte smoke adapters pass. NUC
  full-chip synthesis reports zero SCCs, 53,073 LUT4s, 25,532 GSR-enabled FFs,
  101 block RAMs, and 18 multipliers. Exact seed-4 placement finishes normally,
  packing 66,513 TRELLIS_COMB and 25,561 TRELLIS_FF cells with checksum
  `0x7c9a8594`. Its uninterrupted no-waiver strict router1 route finishes
  normally with checksum `0x09264110`; every production clock passes, including
  14.179972 MHz CPU against 12.5 MHz and 61.270760 MHz SDRAM against
  60.002399 MHz. The `kernel_platform_v1` physical-capacity gate passes at
  66,513/83,640 TRELLIS_COMB, 101/208 block RAMs, and 18/156 multipliers. The
  production bitstream SHA-256 is
  `56f768b2d78801f6cc93a7c518643f1012e30f48241e0d77be8250f97c1c2755`;
  manifest SHA-256 is
  `0593ba251da7b467e413126539d1e863ca19ef00f63843ed5f0cc6d32913b74e`.
  Exact direct-panic and supervisor-guard diagnostics also pass the full SoC
  model, with the latter faulting at `0x02028000`. Beast and NUC independently
  completed all 500,000 lifecycle-soak cycles without frame-count drift. The
  soak snapshot and routed candidate have identical `sw/kernel` and `sw/boot`
  sources. NUC atomically provisions only the exact
  ROM payload (`EB1B381F`) on the existing 244,016 MB card, restores normal
  read-only AstraHost firmware, and performs three independent SRAM reloads of
  the already-hashed bitstream. All three match build and ROM identities, pass
  complete POST and 32 MiB BIST, enable the PMMU, recover user-copy faults,
  preempt two isolated processes at 100 Hz, reap only the offender, and reach
  `K1 PROTECTED ENTRY PASS` in 2.127-2.147 seconds. Physical HDMI then shows
  exact build `77B3CDC8`, the complete PMMU/process/fault result, and
  `K1 PROTECTED ENTRY PASS`; the retained screenshot is
  `docs/evidence/k1-77b3cdc8-sram-hdmi.png`, SHA-256
  `8b6d0d57bf7f029aa63506c348976830079ccabcdeb6a3cf38cad51d3365b051`.
  NUC programmed that already-hashed bitstream into FPGA flash and reset the
  board in the same operation. The automatic flash boot again matches build
  `77B3CDC8` and ROM CRC32 `EB1B381F`, passes the complete hardware gate, and
  reaches K1 entry in 2.132 seconds. Its retained log is
  `docs/evidence/k1-77b3cdc8-flash-reset.log`, SHA-256
  `deeaba2d4acdb5fbc5115085b4f751796ce11079cc68ded319c43117d17b0e97`.
  At that checkpoint, persistent FPGA flash contained exact K1 candidate
  `77B3CDC8`. The
  physical direct-panic and supervisor-guard diagnostics now also pass.
  At that checkpoint hardware lifecycle qualification was partial: 1,000
  physical teardown cycles retained the exact resource baseline, while bounded
  interrupt latency and the time-boxed mixed release burn-in remained open.
  Routing, normal boot,
  persistent promotion, and panic qualification do not waive those gates.
- The one-shot physical diagnostic identities are pinned before use.
  Their AstraHost application SHA-256 values are
  `1c579fa99a2041e82342839ac7f6372e11ccc896ed10e7dcb5ce2a5b07fc35fe`
  for direct panic,
  `6217a1b56163cbe78ab74d4c4e60da33725e2f585a929f5df6b86d654bb53067`
  for supervisor guard, and
  `5e3fc8691da085da408fa8baeb2548143a02b4c7de2c3c10986fb7bd4f13c7c9`
  for lifecycle soak. Independently extracted embedded packages have SHA-256
  values `2de9f718b8db67bbc5b015aae23f67bdf53cd65dc65f597bc76ac7314aca6635`,
  `bb0089aaf7f1248a74d3491e400bd8a383df548892fbc322add18a43cb309733`,
  and `cb55d88f5d16a9c2ec8e6548c051bb6ba96551b939643225574c56c969ad9c83`,
  with payload CRC32 values `FD4FC2AB`, `6AAAEE00`, and `B138EB36`,
  respectively. Reconstructing every package through `package_rom.py` proves
  its header CRC, payload CRC, size, load address, and reset vectors. The normal
  read-only AstraHost application remains
  `b4ec0fe43ffc7012758024576757df11892be0005e8e68fc282879448de962c2`
  and contains no provisioning package. The extracted direct-panic and guard
  packages are byte-identical to Beast's `k1_panic.rom` and `k1_guard.rom`
  artifacts used by the passing exact full-RTL diagnostics. NUC provisioned the
  direct-panic package without formatting the existing card, reporting exact
  35,040-byte payload CRC32 `FD4FC2AB`; provisioner-log SHA-256 is
  `0945eadf79a820ed5fbfa87075877a648dd9c4a465721bf4c5b15748ed66020f`.
  After normal read-only AstraHost was restored, the unchanged production
  bitstream passed complete POST and reached the ordered deliberate panic plus
  `SYSTEM HALTED` in 1.816 seconds; checker-log SHA-256 is
  `e6297e0b7adb8e2cc0352fc1c6575d6c02dbc09312059ee66ca1206ad5b8114a`.
  Physical HDMI shows the same panic, exact Git and build identities, and halt;
  `docs/evidence/k1-77b3cdc8-direct-panic-hdmi.png` has SHA-256
  `639785017f2691b7e4cebc493289f0e0f15d89762aed34c4994c869bce17a8de`.
  NUC next provisioned the exact 35,112-byte supervisor-guard payload at CRC32
  `6AAAEE00`; provisioner-log SHA-256 is
  `1584d6dbee2fcf0c4c903f0d7dd3cc0ccdaed66d34dc3e1e4110a2b81dfc78be`.
  With normal AstraHost restored, the same bitstream passes complete POST and
  reports a format-A vector-2 exception, SSW `0x0105`, and exact guard address
  `0x02028000` before `SYSTEM HALTED` in 1.821 seconds. Checker-log SHA-256 is
  `01aa5fd5d578ad94291a82f9f771df89395274c0ac7a9a42cf702784d9abc0d0`.
  Physical HDMI independently shows every field;
  `docs/evidence/k1-77b3cdc8-guard-hdmi.png` has SHA-256
  `d7289448fb1453fee1e6be617eaad00d458d267f68183416f83ebfa1a827dce1`.
  NUC provisioned the
  exact 36,288-byte soak payload at CRC32 `B138EB36`; provisioning-log SHA-256
  is `483f77b140d083cf5658fc076240d88fa99aaad9764c08e6d2477f454f5e3cde`.
  A 100-cycle hardware qualification reaches K1 entry and reports cycles 4,
  10, and 100 at the unchanged 7,987-page baseline in 29.440 seconds; its log
  SHA-256 is
  `59cb09b9a8a0b4b253d9ae8cd661718c82bead9d7bcbdf7568f6f8ced9cfeb27`.
  That run exposed a host-checker partial-line match, not a kernel failure:
  commit `a363c7c` requires a terminated checkpoint and exact equality with the
  announced baseline, while `254d0f6` streams each complete UART line durably.
  All 21 boot-tool tests pass. A planned 500,000-cycle run started on NUC at
  2026-07-23 15:35:12 EDT as user service `astra-k1-soak-500k`, invocation
  `e03e0b123fd548eca5d5892cc5c74aef`, using checker SHA-256
  `7ab14afacde4cb80fe90d35045d3966b15779b18c9fec1953ad139658fae0784`.
  It was stopped intentionally after preserving complete cycles
  4, 10, 100, and 1,000 at exactly 7,987 free pages. Cycle 1,000 reports 2,003
  switches, 5,536 ticks, and syscall count `0x5717`; retained snapshot
  `docs/evidence/k1-77b3cdc8-soak-1000-hw.log` has SHA-256
  `4229a2e698707d4892d5e13797a496596f426ae8bd5457586135ae77a667893b`.
  Source inspection explains the roughly 0.29-second lifecycle: the user-fault
  path runs at IPL 7, scans a 1,024-entry root plus populated 1,024-entry page
  tables, poisons whole 4 KiB pages, and makes two full 8,192-frame owner scans.
  Vesta has one pending expiration bit, so masked 10 ms periods coalesce and
  the 5,536 count measures delivered interrupts rather than elapsed 100 Hz
  periods. Exhaustive 500,000-cycle coverage remains on independent Musashi
  hosts; physical qualification uses bounded candidate and 30-minute mixed
  burn-in gates.

  NUC then atomically restored only the exact normal payload CRC32 `EB1B381F`,
  normal read-only AstraHost application SHA-256
  `b4ec0fe43ffc7012758024576757df11892be0005e8e68fc282879448de962c2`,
  and production bitstream SHA-256
  `56f768b2d78801f6cc93a7c518643f1012e30f48241e0d77be8250f97c1c2755`.
  The complete normal gate passes again in 2.111 seconds. Retained log
  `docs/evidence/k1-77b3cdc8-normal-restored-after-soak.log` has SHA-256
  `4505cb1b81c6b030df02d7ddf1997c16b532ecdb44c43f35403142da8413a150`.
  At that checkpoint the board and FTDI port were available, and persistent
  FPGA flash remained exact `77B3CDC8`.
- Exact software follow-on `bbb1616a1e65ef56619bffb11cb21e9ea1bc5202`
  closes the diagnosed lifecycle latency path without changing RTL or the
  production bitstream. User-fault dispatch now marks the offender `EXITING`,
  switches to a runnable or empty CRP, and queues teardown; address-space
  destruction, handle close, page poisoning, and frame release run later with
  interrupts enabled. The allocator uses 64 fixed owner ledgers and two 16-bit
  links per physical frame, making owner release O(frames owned) instead of two
  scans over all 8,192 frames. Pinned release is atomic, ledger exhaustion is a
  pre-publication allocation failure, and empty ledgers are reusable. The
  33,280-byte static cost moves normal `.noinit` to 102,016 bytes while keeping
  `_kernel_memory_end` at `0x02034000` inside the 512 KiB reservation.

  Beast passes all 11 kernel suites, GCC `-fanalyzer`, ASan/UBSan/leak checks,
  the canonical m68k compiler gate, all 90 shared framework tests, the complete
  30-execution Musashi/RTL matrix, and both Harte smoke adapters. The optimized
  100-cycle Musashi run reaches virtual cycle 77,501,092 in 0.814 seconds with
  a 4,482-cycle masked-fault maximum. The full pin-level SoC/SDRAM model
  completes 13 teardown cycles in 355.123 seconds with an 8,866-cycle maximum
  and the exact 7,987-page baseline.

  NUC atomically installed exact soak package SHA-256
  `a8acc504ac7b58e19896b3811533e6c31ea795843d5f6ad272f3786887b6ebf4`
  (payload CRC32 `18776505`) while preserving the 244,016 MB card, restored the
  known read-only AstraHost application, and loaded unchanged production
  bitstream SHA-256
  `56f768b2d78801f6cc93a7c518643f1012e30f48241e0d77be8250f97c1c2755`.
  Hardware reaches cycle 100 in 8.219 seconds with 205 switches, 636 delivered
  ticks, syscall count `0x689`, exactly 7,987 free pages, and an 8,834-cycle
  (706.72 us) masked-fault maximum against the 125,000-cycle gate. Retained log
  `docs/evidence/k1-77b3cdc8-bbb1616-soak-100-hw.log` has SHA-256
  `928fdd5414aacb237c5818293a464c3860ffcd0c7cf6d0a48f2fbcf200f0fb5e`.

  NUC then atomically restored exact normal package SHA-256
  `262130dcb7880f8f72ae2da96a9ad3dd2806a6b51b129c78df3991757eb0c23d`
  (payload CRC32 `C030B951`), restored the same read-only AstraHost application,
  and reloaded the same production bitstream. Complete POST, PMMU/user-copy,
  process isolation, offender-only fault containment, and
  `K1 PROTECTED ENTRY PASS` complete in 1.931 seconds. Retained log
  `docs/evidence/k1-77b3cdc8-bbb1616-normal-hw.log` has SHA-256
  `6197aeeeb3a55ea1d8366025a6c64f9d2a424b17791bb60d3ab72d8ec6916b86`.
  The board was left in that normal state. No synthesis, placement, route,
  resource, or constrained-clock result changed. At this checkpoint the
  30-minute mixed hardware burn-in was the next K1 release gate.
- Exact hardware-qualification follow-on
  `853ae66e300232dcbdf5f69903747faa42521114`, archive SHA-256
  `c416c1bfb0720ac2bf9fb94898a99e66e1e7b6040f215706a661462ea493f7ad`,
  adds a coherent 64-bit FPGA cycle-counter snapshot and binds elapsed-time
  acceptance to complete K1 checkpoints. The kernel reads the low word first
  to latch the counter, then the high word; the soak computes wrap-safe elapsed
  cycles without a compiler runtime helper. The checker rejects partial lines,
  counter regressions, baseline drift, zero activity, excessive latency, and
  checkpoints below the requested elapsed-cycle threshold. All 11 kernel
  suites pass normally, under GCC `-fanalyzer`, and with ASan/UBSan/leak
  checks; all 22 boot-tool tests, 15 Rust tests, rustfmt, Clippy, the exact
  m68k builds, and a 1,000-cycle Musashi run pass.

  NUC mounted the existing 244,016 MB card without formatting and atomically
  replaced only `/ASTRA68.ROM` with soak package SHA-256
  `9f6953911f10d726d51b861eeb5d42a4a54c15841e385125ecbd7f1257b8ab53`,
  37,384-byte payload CRC32 `91E30139`. A second provisioner boot verified the
  file already matched; retained log
  `docs/evidence/k1-77b3cdc8-853ae66-soak-provision.log` has SHA-256
  `28815aa647d9aacb15f8c93bde6df97e1ef169ce3e0874f0c2cf8d5e8189ad75`.
  With normal read-only AstraHost restored and unchanged production bitstream
  SHA-256
  `56f768b2d78801f6cc93a7c518643f1012e30f48241e0d77be8250f97c1c2755`,
  the routed candidate gate passes at cycle 5,000 after 317.246 host seconds
  and `0x00000000EAE8411F` FPGA CPU cycles. It reports 10,005 switches, 31,533
  delivered ticks, syscall count `0x15288`, exactly 7,987 free pages, and an
  8,809-cycle masked-fault maximum. Retained log
  `docs/evidence/k1-77b3cdc8-853ae66-candidate-5m-hw.log` has SHA-256
  `db9ad4900951e3cc61ae20d8078bd714a20089bcd1880f0f77dc58d34f64dbf6`.

  A separate reset then passes the release burn-in at cycle 29,000 after
  1,830.658 host seconds and `0x000000055263857F` coherent FPGA CPU cycles,
  exceeding both the 30-minute `0x000000053D1AC100` threshold and 5,000-cycle
  minimum. It reports 58,005 switches, 182,861 delivered ticks, syscall count
  `0x7AD6B`, exactly 7,987 free pages, and the unchanged 8,809-cycle maximum.
  Retained log `docs/evidence/k1-77b3cdc8-853ae66-release-30m-hw.log` has
  SHA-256
  `71d2c3a766bc1cd25a58f6e81ca9c904517b0df74322d2d3130279a0e1ffa489`.

  NUC finally restored normal package SHA-256
  `696afc6ecf9d5df31acc76966aeea0fe190b44479c4af61a2fbf16f8866f7d05`,
  36,292-byte payload CRC32 `BBAB0AA1`, and verified an exact second-boot match.
  Provisioning-log SHA-256 is
  `5be77adb8627fbd0ca4a6ede6601c92d362d44bf0394dce3f31fad8f0c398929`.
  Read-only AstraHost application SHA-256
  `b4ec0fe43ffc7012758024576757df11892be0005e8e68fc282879448de962c2`
  was restored before the same production bitstream was loaded. The final boot
  reports exact build `77B3CDC8`, ROM CRC32 `BBAB0AA1`, full Git identity
  `853ae66e300232dcbdf5f69903747faa42521114`, complete POST/BIST, PMMU and
  fault containment, and `K1 PROTECTED ENTRY PASS` in 1.955 seconds. Retained
  log `docs/evidence/k1-77b3cdc8-853ae66-normal-hw.log` has SHA-256
  `14b69338b1c429def6fa0a13067bff6e00f087dae0dc7a05a3a463e7a107f09c`.
  At that checkpoint the board was left in that normal state; persistent FPGA
  flash remained the same exact `77B3CDC8` image. No RTL, synthesis,
  placement, route, resource, or constrained-clock result changed during this
  qualification.
- CPU correction `9a977e13f560b4c85eafc7835d88aad437314491` and guarded
  worker `42f4bb55ebd5ac47d057162322e293e4999a2661` form the current exact
  guarded-worker release. The CPU now preserves M in the MC68030 section 8.1.9
  format-1 throwaway frame, avoids a second MSP postadd, and settles
  restored-PC fetch before chained `RTE` retirement. The kernel runs process
  reclamation with interrupts enabled on a dedicated guarded 8 KiB MSP;
  exception and IRQ entry
  retain the separate guarded 8 KiB ISP. Work and retry queues are each one
  bounded bit, and panic output includes exact worker/maintenance state.

  Beast passes 12 kernel suites normally, with GCC `-fanalyzer`, and with
  ASan/UBSan/leak checks; canonical m68k verification, 15 Rust tests, rustfmt,
  Clippy, all 90 framework tests, all 30 shared executions, and both Harte
  smoke adapters pass. The complete strict Questa inventory is 141 total, 115
  clean, and the existing 3 compile, 18 simulation, and 5 unscored buckets are
  unchanged. Musashi reaches normal K1 and completes 1,000 lifecycle cycles at
  virtual cycle 640,260,129 with 2,001 switches and 7,987 free pages. The
  complete pin-level RTL/SDRAM model reaches normal K1 in 130.017 seconds and a
  stable worker soak checkpoint in 191.959 seconds, both with 115.03 MB/s BIST.
  Exact full Beast synthesis has zero SCCs and maps 53,079 LUT4s, 25,536
  GSR-enabled FFs, 101 block RAMs, and 18 multipliers. The exact no-waiver
  seed-4 heap/router1 route packs 66,523 TRELLIS_COMB and 25,565 FFs and passes
  at 15.058201 MHz CPU, 66.907532 MHz SDRAM, 79.693970 MHz USB, 53.267990 MHz
  pixel, and 289.771088 MHz HDMI shift. Exact build `25D9CB8E` bitstream
  SHA-256 is
  `78cd218f12feb72ccbdcb6bb141d19908c961f3438b6b559bf99b60d1c9d6940`;
  normal ROM CRC32 is `D21EF603`.

  NUC mounted the existing 244,016 MB card without formatting and changed only
  `/ASTRA68.ROM`. Three independent SRAM loads pass exact build/ROM identity,
  complete POST and 32 MiB BIST, PMMU/user-copy checks, the guarded MSP worker,
  100 Hz preemption, offender-only fault containment, and K1 protected entry.
  The exact soak ROM then passes 5,000 worker/fault cycles over 302.531 host
  seconds and `0x00000000DFEAD7D7` CPU cycles with 10,003 switches, 30,057
  delivered ticks, 48,709 syscalls, an unchanged 7,987-page baseline, and a
  9,376-cycle maximum masked-fault interval. Normal ROM and read-only AstraHost
  were restored before a fourth SRAM boot passed. Exact release
  `25D9CB8E` was then written to FPGA flash; automatic reset-from-flash passed
  the same normal gate in 2.008 seconds. Persistent hardware now contains
  `25D9CB8E`. Physical HDMI visibly reports the full `e108a371...` Git
  identity, guarded MSP worker, PMMU, preemption, fault containment, and
  `K1 PROTECTED ENTRY PASS`. Retained screenshot
  `docs/evidence/k1-25d9cb8e-e108a37-flash-hdmi.png` has SHA-256
  `e6e654d6ad0c9f5dead16f9116ab622d7a5ba731fc2fafc1ff7ba324c08128a4`.
- Exact `F4DC1E18` canonical Beast mapping reports 52,943 LUT4s, 25,522
  synthesized FFs, 101 block RAMs, and 18 multipliers with zero SCCs. Its
  strict seed-4 heap/router1 route packs 66,377 TRELLIS_COMB cells, 25,555 FFs,
  101 block RAMs, and 18 multipliers. It passes at 14.015417 MHz CPU,
  66.423111 MHz SDRAM, 72.432274 MHz USB, 55.673088 MHz pixel, and
  307.125305 MHz HDMI shift. Bitstream SHA-256 is
  `bf6b86079227e042676ef495903162212a19092ab28fa83a7a09fbd261381d35`;
  routed-JSON SHA-256 is
  `cac12300397576b12ce9a386762d2d06c2f805ff6ebb98d1b36aa963f03a84e3`.
- The exact committed `6C0D0CA3` release synthesizes to 52,728 LUT4s and packs
  66,144 of 83,640 TRELLIS_COMB cells, 25,525 FFs, 101 block RAMs, and 18
  multipliers. Its strict router1 result passes every exact constraint at
  13.972139 MHz CPU and 63.403500 MHz SDRAM or better. Physical capacity, not
  an artificial utilization cap, is the release limit. See
  [FPGA_RESOURCE_BUDGET.md](FPGA_RESOURCE_BUDGET.md).
- A focused board diagnostic found one remaining hardware-only Astraea defect
  in that release: every multi-row blit was rejected with range error 1 while
  a one-row command passed. Zero-pitch and nonzero-pitch commands failed
  identically, and the CPU-visible command fields plus completion fences were
  correct. The failure is isolated to the unregistered DSP-backed
  `(height - 1) * pitch` range-validation path. The retained replacement uses
  a deterministic 16-step unsigned shift/add validator; directed graphics,
  integrated normal/INDEX8/RGB565 graphics, full CPU coretest, and complete
  HDMI-enabled AstraHost boot through `K0 ENTRY PASS` all pass. Canonical Beast
  synthesis has zero SCCs and maps 52,728 LUT4s, 25,492 FFs, 101 block RAMs,
  and 18 multipliers. Exact committed build `6C0D0CA3` routes the complete
  feature set at 13.972139 MHz CPU and 63.403500 MHz SDRAM, packing 66,144
  TRELLIS_COMB cells, 25,525 FFs, 101 block RAMs, and 18 multipliers. A
  route-preserving focused board image repeatedly passes all four one-row,
  multi-row, zero-pitch, and 720-byte-pitch commands; the complete graphics
  board image also reports `GFX PASS`. Four exact production reloads pass,
  including one after an independent AstraHost restart. Physical HDMI shows
  the exact kernel provenance, initialization results, `K0 ENTRY PASS`, and
  `KERNEL IDLE` with the corrected CP437 font.
- The 256 GB card and its existing GBA data are preserved. The one-shot
  maintenance firmware atomically replaced only `/ASTRA68.ROM` and reported
  the exact `6C0D0CA3` payload CRC32 `0fd82996`; normal AstraHost firmware is
  restored and mounts the FAT/exFAT boot volume read-only. It intentionally
  exposes no runtime media until the card has exactly one CRC-valid,
  non-overlapping Astra GPT partition. The bounded AstraHost input queue and
  the independent OHCI USB host are both integrated.
- Candidate promotion repeated that controlled procedure for `F4DC1E18`. The
  one-shot firmware mounted the existing 244,016 MB card without formatting
  and atomically replaced only `/ASTRA68.ROM` with the 17,652-byte payload,
  CRC32 `c7162f5a`, package SHA-256
  `58c29486d387fa8d8087252a1a238c4af766fe3144643e56c7b81a6b9299ccc6`.
  Exact normal read-only AstraHost firmware was restored and remounted the
  card. Three independent FPGA SRAM reloads then reached build `F4DC1E18`, ROM
  CRC32 `C7162F5A`, complete POST, full-range BIST, Astraea DMA, the 100 Hz
  timer, runtime/input initialization, and `K0 ENTRY PASS` in 1.615-1.627
  seconds. The card was then temporarily provisioned with the clean hardware
  coretest package (64,372 payload bytes, CRC32 `2FA3100C`); exact candidate
  `F4DC1E18` reported `CORETEST PASS sum=74A6EC6D` in 3.864 seconds. The
  production package and normal read-only AstraHost were restored immediately,
  and a final candidate reload again passed exact identity, complete POST,
  full-range BIST, DMA, and `K0 ENTRY PASS` in 1.616 seconds. At that
  checkpoint persistent FPGA flash was intentionally still `6C0D0CA3`.
  K-HW3 table-walk locking was repaired and tested in source, but required a
  new route and board pass along with physical HDMI confirmation. K-HW4
  IACK/timer races were likewise closed in controller and focused CPU
  simulation: the selected vector could not change
  in flight, spurious results remain spurious for that transaction, edge/level
  clearing is ordered, and one-shot restart cannot lose a simultaneous expiry.
  Hardware timer-race and interrupt-latency qualification remained open.
- Before the focused diagnostics, the ULX3S contained the exact `B1F9E60D`
  rollback release in volatile SRAM. Its bitstream SHA-256 is
  `05b9e84d2413c9390163a38f77c4d8ad08600a6adb619e69ebb25c56ae0e4eae`;
  its `/ASTRA68.ROM` SHA-256 was
  `2693a912e98a0fc1211b54b62dd80f8bed0544a3ac904d5b24d320c2be986423`.
  Three consecutive FPGA-only reloads reached exact build and ROM identity,
  complete POST, 32 MiB full-range BIST, and `K0 ENTRY PASS`. At that
  checkpoint the board contained exact production build `6C0D0CA3` in
  persistent FPGA flash. Its
  bitstream SHA-256 is
  `61538d09ef255b94206500185b31008fc242004ac954356365e0b9053c88e2d1`;
  `/ASTRA68.ROM` SHA-256 is
  `9daede67d0e4aa233018425a64060f09d2b045897c0d46fcf417831712dc7c6a`.
  Four reloads match both identities and pass complete POST, full-range BIST,
  Astraea DMA, timer/runtime/input initialization, and `K0 ENTRY PASS` in
  1.570-1.603 seconds. The fourth followed an independent AstraHost restart
  and proves normal SD remount plus FPGA SPI-link recovery. The identical
  bitstream was then programmed with `-f -r`; its reset-from-flash boot matched
  both identities and repeated complete POST, BIST, DMA, and kernel entry in
  1.630 seconds.
- The legal full route and board-boot blocker is resolved. The corrected Beast
  router1 run refreshed all 66,566 placed combinational cells, completed after
  7,924.67 seconds, and passed the protected-LUT gate with zero violations. It
  meets every production clock at 14.544609 MHz CPU and 66.409882 MHz SDRAM,
  with all USB, SD, board, pixel, and HDMI-shift constraints also passing. The
  earlier route's 12,184 forbidden CCU2/distributed-RAM permutations remain a
  rejected historical checkpoint and explain its hardware-only CPU failure.
- P54 still passes the focused real-surface BERR/no-write test, full CPU
  coretest, full POST and kernel entry, directed graphics, and all integrated
  graphics modes with unchanged scanline maxima. Its mapped design has zero
  SCCs and GSR enabled on all 25,424 FFs. Those functional results remain
  necessary but do not waive the corrected physical-route gate.
- P55 removed the P54 route's 15.058 ns AstraHost boot/runtime ownership cone.
  A positive-control mapped-netlist check finds the old path and proves it is
  absent from P55. Focused AstraHost, complete boot, directed graphics, and all
  integrated graphics regressions pass at the locked 60 MHz SDRAM clock. The
  exact P55 route is timing-clean and repeatedly hardware-boots. Its four Y46
  font BRAM slices nevertheless read effective bank 3 while RTL selected bank
  0. Config-only all-ones, bytecode, bank marker, and CP437-copy experiments
  isolated that historical fault to font initialization/address handling
  rather than HDMI, text RAM, CPU writes, or Vega control.
- The source-level font correction is implemented and passes focused render,
  route-probe, and complete HDMI-enabled AstraHost boot simulations. Beast
  synthesis maps the exact 2 KiB CP437 image into one 2048x9 `DP16KD`, with all
  11 logical address bits connected, and reduces the complete design from 104
  to 101 block RAMs. The mapped checkpoint has zero SCCs, deterministic GSR on
  all 25,420 FFs, 52,565 LUT4s, 5,099 CCU2Cs, and 19 multipliers. The exact
  `B1F9E60D` route and three board reloads now pass; a mandatory mapped-netlist
  gate rejects multiple font blocks, constant logical address pins, or the
  wrong physical width.
- Release promotion is complete for exact bitstream `6C0D0CA3`: strict route,
  focused and complete graphics diagnostics, SD ROM installation, four SRAM
  production boots, physical CP437 HDMI confirmation, persistent programming,
  and reset-from-flash boot all pass. Future releases must preserve the rule
  that no rebuild or repacking is permitted between SRAM acceptance and
  persistent programming.
- The canonical entry points agree on divider 0, 12.5 MHz CPU, 60 MHz SDRAM,
  heap timing weight 20, plain router1, and the measured critical floorplan.
  The split flow clears the placement-only waiver before release routing,
  checks every final clock, enforces resource policy, and binds stage 0 plus
  `/ASTRA68.ROM` in its manifest. Blind seed hunting remains unjustified; every
  retained route must record its exact cone in
  [TIMING_CLOSURE.md](../fpga/soc/oss_flow/TIMING_CLOSURE.md).

## Release boundary

A placement estimate, reduced-feature build, zero build ID, or
`--timing-allow-fail` route is diagnostic only. A usable release requires one
exact committed source revision and ROM identity to:

1. pass shared architecture, directed graphics, integrated graphics, boot, and
   SDRAM gates;
2. synthesize with zero SCCs and pass the enforced resource profile;
3. route every constrained clock with the complete feature set;
4. be packaged and flashed through NUC; and
5. pass repeated POST, SDRAM, kernel-entry, and HDMI checks on the board.

Record every structural route result immediately in `TIMING_CLOSURE.md`,
including the failed cone. A failed experiment with a measured reason is part
of the design record; an unrecorded seed hunt is lost work.
