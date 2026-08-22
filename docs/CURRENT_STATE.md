# Astra 68 current engineering state

This is the short continuation map for the active machine. It records decisions
and validated boundaries that are easy to lose across long sessions. Detailed
contracts remain authoritative in the linked documents; historical handovers
and old resource tables are not current status.

The platform is **Astra 68**, its kernel is **Axiom**, and the complete
user-facing system is **Astra OS**. The Astra NDK is the stable developer
surface; Axiom's internal interfaces are not a module ABI.

## Systemic command/filesystem latency fix (2026-08-20)

The former `ls` symptom was process-wide startup work, not directory rendering.
Every fresh filesystem client searched all Kit manifests for
`filesystem.library`, read and mapped the library again, and then paid a VFS
`HELLO` before its first real operation. The retained fix is shared by commands
and applications; `ls` itself is unchanged.

- `astra_image.py` derives bounded `LIBS:.providers/` records from validated
  Kit manifests and embedded library identities. Old images retain the manifest
  sweep fallback.
- Syscall ABI `0x00010012` adds `LIBRARY_ATTACH` (56). Axiom caches exact
  library identities and initial pages; later processes share R/RX pages and
  receive private copies of initial writable pages without filesystem lookup or
  library-file I/O.
- `STOR` v7 returns small `READ_PATH` files inline. `STOR` v8 fuses session
  creation with the first path operation; older services negotiate normally and
  receive the operation in a second exchange.
- The common Terminal launcher uses the supervisor's existing one-request
  whole-image read path for every external command. Its permanent trace event
  is `command stages: image/spawn/run`; there is no command-output cache.

On `astra-arty`, the stock warm `filesystem.library` open was 794 ms. Cached
identity attachment reduced the best warm sample to 149 ms; the v8 fused first
operation reduced two consecutive no-output `ls -l` filesystem opens to 61 ms
and listings to 86/51 ms. The focused transport regression proves that lazy
connect plus the first `OPEN` is one service request. Repeated `which` and
`mkdir` runs exercise the same shared path. The disposable board candidate is
not the stock runtime; restore and final gate status are recorded in
`HANDOVER-launch-latency.md`.

The clean candidate ROM
`b5fabd384b1b5a8ab82aed8d064b22da0ea32b30a12cc94412a045898b39049a`
and pre-boot image
`ff7540bebc34bccd7a82a93426e1fd381c1359ea05dd3f56456b00fc673618f3`
passed POST, stage 8, and `ls`/`which`/`mkdir`/`rm`/missing-`cat` on the
physical board without panic. The board was then restored to the untouched
stock `astra_boot.bin` and `storage-terminal.img`; that pair also passed POST
and stage 8.

## Generic hardware framebuffer copy and text presentation (2026-08-21)

The renderer now has one generic overlap-safe 64-bit AXI block-copy path. It
uses legal AXI INCR bursts of at most 16 beats, splits every transaction at a
4 KiB boundary, and handles both copy directions. Unsupported formats and
alignments retain the established pixel blitter. The API is not Terminal
specific: Graphics Kit exposes `AstraTextBox` and
`astra_text_box_scroll()`, and Terminal is its first caller.

The exact 1280x644 RGB565 desktop copy improved from 4,401,758 to 824,550
renderer clocks (`5.34x`); the smaller identity copy improved from 5,822 to
1,254 clocks (`4.64x`). The complete graphics regression passes, including
the coordinate-unique 1,280-pixel screen-offset gate. A clean Vivado 2024.2
production route on Beast connects all 67,294 nets and passes the actual
187.5 MHz setup/hold gate at `+0.070/+0.011 ns`. It uses 32,548 LUTs, 39,402
registers, 12,253 slices, 129.5 BRAM tiles, and 81 DSPs.

The exact production bitstream is active on the Arty. Its 1280x644 overlapping
copy improved from 11,917,253 to 1,431,536 clocks (`8.32x`), or 7.63 ms at
187.5 MHz. Repeated cold boots, HDMI unplug/replug, the production-width offset
gate, framebuffer checks, and 48 kHz stereo audio all pass; the HDMI manager
reports `HDMI 720p60 audio=2ch-LPCM-48k-24bit`.

Terminal now coalesces child-output presentation for at most one 60 Hz frame.
It does not enlarge the stream queues, delay keyboard repaint, or change the
generic Graphics Kit/renderer path. On the physical board, a controlled
10-run A/B of `ls -l COMMANDS:` changed the median from 1,848.384 ms and eight
presentation batches to 1,594.976 ms and six batches (`13.7%` faster, `25%`
fewer presentations). A second 10-run candidate gate passed at 1,668.367 ms
and six batches. Twenty-five measured candidate runs completed without a new
panic. The measurement tool can now fail on a caller-selected maximum median
batch count; the retained hardware invocation uses six.

The installed matching pair is ROM SHA-256
`e07e648f347e2a522ce8297f67af213a2281ff4f6cecb504ec1ad19e7670b07e`
and pre-boot storage SHA-256
`b033561aeb0b3728301a6ada6fdf84aef7d32e499a1e848462ed79d197ab2352`.
A physical cold boot of this exact application pair passed full POST, stage 8,
and a fresh five-run `ls -l COMMANDS:` gate at 1,622.439 ms and six median
presentations. The framebuffer-copy and Terminal-presentation release gates
are closed.

## Shared retained-text control and rejected blitter stream (2026-08-20)

Graphics Kit draw lists now include a validated same-surface rectangular copy.
It reuses the ordinary overlap-safe Astraea BLIT and is available to any
retained text control, including editors and word processors. Terminal uses a
renderer-independent scroll callback, coalesces consecutive scrolls, copies
preserved rows in hardware, and redraws only exposed or damaged cells. Glyph
runs remain hardware commands; the MC68030 updates cell state and submits the
bounded draw list rather than rasterizing pixels.

On the physical Arty, clean `help` display latency fell from 616.034 to
400.276 ms (`-35.0%`), with render commands falling from 116 to 58 and glyph
runs from 43 to 12. Exact `ls -l COMMANDS:` latency fell from 2778.107 to
1898.428 ms (`-31.7%`); commands fell from 366 to 141 and glyph runs from 127
to 16. The candidate ROM/image pair is
`588b239643bc45685e11ac97ffad23ce9f2409ffd15a221fc54966a951dd648e`.

A follow-on cached-RGB565 blitter stream measured 5,822 to 3,470 cycles for
64x16 copy and 20,734 to 11,782 cycles for the real 1280-pixel compositor
width. It closed a complete production route at `+0.122/+0.048 ns`, but the
board displayed a mid-screen compositor wrap on the exact 1280x644 desktop
BLIT. That physical correctness failure rejects the RTL regardless of speed.
The 33-line stream was removed, the live PL and SD boot were restored to the
qualified front-panel release, and only the real-width simulation regression
was retained. Active BOOT.BIN is again
`545f0ccb259972bc7fc26c08f9080dc7033ef7627693ff1ff03085c98a9e3d9c`;
FIT remains `74838cdca1f45205bd2d69e6fba51f59b5fae43c2de39fde3e8f9cdc4ed4eb2d`.

The permanent screen-offset gate runs at the real 1,280-pixel width. Its
coordinate-unique pattern cannot hide the recurring 640-pixel wrap; it checks
production-width final pre-HDMI RGB lines and
separately verifies the exact 1280x644 overlapping compositor copy across the
whole 1280x720 surface in simulation and on the board. Qualified hardware
passed the board copy in 11,917,253 cycles; the final pre-HDMI gate passed all
5,120 pixels in four production-width lines. Six production final-pixel
signature forms fully routed but failed setup timing, so no checker RTL, MMIO
register, ABI bump, or resource cost is retained.

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

`docs/HANDOVER-userspace-bringup.md` is the historical record for the first
userspace slice; the current continuation is the Arty software-milestone
section below. Implemented and gated in that first slice: the
observability contract (`docs/OBSERVABILITY.md`), a bounded userspace
allocator, a QEMU Vesta block service that lets `sw/kernel/block.c` run in
emulation for the first time, a strict big-endian MC68030 ELF acceptance
profile with a transactional loader, and a capability-gated process-info
syscall at ABI `0x0001000e`.

**Astra runs a real user program at boot.** Boot ABI 0.3 carries
`user_image_base`/`user_image_size`; firmware embeds the linked supervisor ELF
in the ROM, copies it to `0x02004000`, verifies the copy, and reserves only the
pages it fills. The kernel loads it with `kernel_process_create_executable()`,
and the process validates its startup block, queries the syscall ABI, reads
`PROCESS_INFO` on its own handle, and exits with a tagged status the kernel
reports and gates on. Verified in QEMU end to end; 30 kernel suites, 6
userspace suites, sanitizers, `-fanalyzer`, both QEMU certifiers, and the
MC68030 kernel image all pass.

The initial image is 1,306 bytes of MC68030 text (6,468-byte ELF). The kernel
and that image now ship LZ4-compressed in ROM and are CRC-32 verified after
firmware decodes them into their load addresses, which took the ROM from 90.7%
to **72.1% used: 189,064 of 262,144, with 73,080 free**. `docs/MEMORY_MAP.md`
records the budget, the measured codec comparison, and the rule for what is
allowed to live in ROM — notably that lwext4 is not, because stage 0 reaches a
FAT boot volume in 2,020 bytes.

**Block admission is implemented and a user-mode service moves real data.**
Syscall ABI `0x0001000a` adds transfer memory (`DMA_CREATE`) and the three
block calls (`BLOCK_QUERY`, `BLOCK_SUBMIT`, `BLOCK_COLLECT`), all gated on a
device lease the initial image receives at launch. The service holds
process-owned, cache-inhibited, physically contiguous transfer memory, submits
a read naming a buffer handle rather than an address, and collects a completion
that distinguishes device errors, resets, and media changes. Every boot with
media attached proves the whole path:

```
Initial image ....... block round-trip verified, service resident
```

The block facade in `sw/userspace/storage` now has a lease-backed
`AstraBlockBackend`, so the initial image reads its boot sector through
`astra_block_read()` — the call a filesystem makes — over an interrupt-driven,
deadline-bounded transport rather than by driving syscalls itself.

The K1/K10 qualification pair is now `K1_QUALIFICATION=1`, not a boot workload;
that build remains the gate for the performance budget and the device-IRQ
report.

lwext4 is qualified big-endian behind three one-line upstream fixes but is
neither vendored nor adopted. **A VFS and an interactive terminal now exist**
— see "Software milestones on the Arty QEMU backend" beneath the override
below; this paragraph is otherwise unchanged from when neither did.

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
  kernel regression gates pass. The production launcher now starts the input
  core as a protected Astra process, transfers the input-device lease and IRQ,
  and publishes `INPUT_SERVICE`. The display process is its unique seat owner;
  pointer-only observers require explicit delegation. Physical evdev
  qualification is tracked by the active GUI pointer checkpoint below.

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
Linux System RAM ends at `0x17ffffff`. The Arty launcher preallocates 128 MiB
of normal cached RAM for the Astra guest, leaving a 256 MiB Linux/host-services
budget after QEMU starts. The physical ULX3S profile remains exactly 32 MiB;
QEMU accepts only that profile or the 128 MiB hosted profile. Read-only `/`,
writable `/data`, persistent SSH state, DHCP, and the shared Mac-directory service all
survive the graphics boot-package replacement.

The 128 MiB profile is hardware-retained at
`/data/astra/deploy/memory128-538563d8`. The active stripped ARM QEMU is
`538563d84b8e43ffd3e2d9cc149594c3ccd8a3b372bc0b32ef8336818fabc5ca`,
the ROM is
`970dd9dae9ddbfc07fa26fa696d76512a2fc2f78be509f347057dc219c5e878f`,
and the launcher is
`c8f7fdc36621e14332242b25ad6c300f34f676721c06273d08c4234c5e05f82a`.
On 2026-08-09 the exact board command line included the preallocated 128 MiB
backend; QEMU held 147,892 KiB RSS with no swap, POST reported 128 MiB, the
kernel reported 32,370 free of 32,768 physical pages, storage reached stage 8,
and the published 90x30 text plane contained a live `WORK:>` prompt. The
previous 32 MiB QEMU, ROM, and launcher are retained beside the new artifacts
in that deployment directory.

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

## Software milestones on the Arty QEMU backend (2026-08-09)

Five software milestones landed on top of the unchanged CPU/PL boundary the
override above establishes, not yet folded into the historical "Userspace
bring-up line" section above: **program launch** (`docs/HANDOVER-launch.md`),
**union assigns** (`docs/HANDOVER-union-assigns.md`), durable event history
(`docs/HANDOVER-events.md`), the protected-service loader, and a protected
terminal process. Together they are what "the shell" now means on this
machine.

- A program runs from the interactive prompt: typed by name, found by
  lookup, loaded through `ASTRA_SYSCALL_PROCESS_CREATE` (48), and its exit
  status reported. A launched child is handed grants for its three streams,
  `WORK:`, `COMMANDS:` and `EVENTS:` — the last three as port handles to
  protected services.
- `COMMANDS:` is an ordered two-member union: `local/commands` (read-write, a
  person's own directory) tried first, then `commands` (read-only, shipped).
  The first member that answers wins, so a name on the writable member
  shadows the shipped one; a listing shows both, and a launch records which
  member answered. A launched child resolves through the same union it was
  granted, at the same roots, because `AstraLaunchGrant` and
  `AstraStartupCapability` now carry a 64-byte mount-relative root
  (`docs/ABI.md`, `ASTRA_STARTUP_ABI_VERSION` 2).
- The four bounded event tiers survive reboot in alternating, versioned CRC-32
  snapshots under the state volume's `events/`. Startup accepts only a complete
  valid bank, exposes its boot ring as `EVENTS:boot/-1`, and renders numeric
  message ids if its catalog identity differs from the running build. One
  previous boot is retained; `boot/-2` remains deliberately absent.
- `/startup/system` is now an ordered, three-entry service manifest. The supervisor
  temporarily mounts the boot volume only to read that file and the first
  storage image, then masks its IRQ, releases bootstrap DMA, and closes its
  original block/IRQ handles once storage publishes. Protected
  `storage` mounts the volume and publishes `SYS:`; protected `events` receives
  `SYS:r` plus a private `STORE:rw` rooted at `events/` and publishes
  `EVENTS:r`. Protected `terminal` receives `DISPLAY`, `INPUT`, `INPUT_IRQ`,
  `WORK:rw`, the two `COMMANDS:r` members, `EVENTS:r`, and `EVENT_CONTROL`, then owns the
  existing shell and child-stream loop. Its manifest entry is explicitly
  `delegates`, allowing it to narrow and re-grant those handles to launched
  commands. All three services are independently scheduled processes; there
  is no registrar, the supervisor retains only lifecycle monitoring, and it
  closes its display/input/control copies after the terminal is ready. The
  fifth process slot is the bounded capacity required for a command alongside
  supervisor, storage, events, and terminal; shared-area alias accounting was
  raised to the same five-process ceiling and the configured-ceiling test
  covers it. The retained
  Beast build measures storage at 69,310 bytes text/5,251,076 BSS, events at
  17,341 text/12 data/49,678 BSS, and terminal at 19,872 text/8 data/96,868
  BSS. The initial supervisor image is 73,956 text/8 data/5,341,100 BSS; both
  ext4 owners carry the fixed 5 MiB allocator arena.
- `events --level-set <subsystem> <level>` is a temporary current-boot control
  operation, not a write to `EVENTS:`. Launched commands receive a distinct
  `EVENT_CONTROL` capability; the protected events service validates and
  forwards `EVCT` v1 to the supervisor runtime, which owns every current event
  call site. The runtime refuses unknown subsystems and levels before changing
  its fixed eight-byte threshold table. Add another registered target only
  when a protected service gains event call sites.
- The terminal gate (`emu/qemu/test-terminal.py`) is the acceptance evidence
  for these milestones: it boots twice against one scratch ext4 image, proves
  both protected services through ordinary paths and commands, proves
  `events --boot -1` reads the first boot, and exercises the complete temporary
  level-control round trip against the exact Astra QEMU backend. It also reads
  the file-backed 90x30 text plane independently, requires it to equal guest
  memory, and checks that the published cursor follows Left/Right/Backspace and
  rests after exactly one prompt separator rather than after an eraser cell.
  Enter must publish column zero of the next row before a launched program is
  loaded, proving slow dispatch visibly acknowledges the accepted line.
  The five-run boot gate measures terminal-ready at a 0.17 s median against the
  1.00 s budget.

The `Graphics.kit` bundle now carries separate `font.library` and
`graphics.library` ELF32/m68k images beneath `LIBS:`. Terminal uses the
public `OpenLibrary()`/`CloseLibrary()` contract and calls their typed export
tables; neither implementation is statically linked into Terminal. Axiom
shares immutable library frames across process address spaces, keeps writable
pages private, reclaims unused cache entries, and tears mappings down on
process death. The constrained loader accepts eager `R_68K_RELATIVE`
relocations and rejects PLTs and unsupported dynamic state. The complete Beast
QEMU display gate on 2026-08-16 passed with Terminal, keyboard, pointer,
move/resize/maximize/close, and all fences: 26 submissions, 19 batches, 775
commands, 371 fills, 166 blits, 42 glyph commands, 13,500 present cycles versus
250,000 budget, 13,316 pointer cycles versus 250,000, and 0.359 s boot. The
first integration boot exposed a custom-linker defect that placed
`_GLOBAL_OFFSET_TABLE_` at `.got.plt`; the linker script now fixes and asserts
the GOT base at `.got`, preventing PIC libraries from silently reading the
wrong entries.

The version-1 application, Kit, manifest, and `.aicon` contracts are locked in
`BUNDLE_FORMAT.md`. `Terminal.app` is installed beneath `APPS:`, launched by
resolving its manifest rather than a hard-coded executable path, and receives
a read-only `APP:` binding to its own root. The startup manifest launches the
desktop alone; a double-click on its factory Terminal pin asks the supervisor's
capability-gated application-launch port to start `APPS:Terminal.app`. The
resulting standard window renders the required 16x16 strike at the left of its
title, while the desktop uses the separately designed 64x64 strike. All three
16x16, 32x32, and 64x64 strikes are generated deterministically. The NDK owns
the shared bounded manifest and icon parser. The host `astra-bundle` tool
validates and atomically copies, moves, trashes, or permanently deletes whole
bundle directories; it refuses overwrites and refuses a permanent Kit deletion
that would leave a declared library dependency unsatisfied. The filesystem
remains authoritative: there is no registry or uninstall database, and any
future discovery index must be rebuildable from manifests. The final Beast
QEMU gate booted the clear desktop, double-clicked its Terminal pin, launched
the manifest-resolved application and Kit libraries, and exercised pointer,
keyboard, blink, maximize, restore, resize, and close with 1,031 completed
fences, 22 batches, 1,101 commands, 657 fills, 205 blits, 43 glyph commands,
13,537 present cycles and 13,056 pointer cycles against 250,000-cycle budgets
in 0.359 s. A regression also proves that clicking the deliberately inactive
desktop does not request an undamaged frame. Independent
extraction of the generated ext4 image passed `astra-bundle check` for
`Terminal.app`, `Graphics.kit`, and `Filesystem.kit`; the application contains
its 41,300-byte MC68030 executable and 5,484-byte multi-strike icon.
`graphics.library` ABI 1.1 owns the shared `.aicon` parser/accessors, so
applications do not duplicate icon decoding. There is no separate
`icon.library` until icon policy needs an independently versioned ABI.

The retained Arty terminal deployment is rooted at commit
`381d15306ff6b0077d8042fe975f426b7cf4f173` plus the current working-tree
changes and was built on Beast from `/tmp/astra68-terminal-20260809`. Its exact
artifacts are ARM QEMU
`6506f3a1dcf7336a084acd471b6be828b49624cbf3dea3a1708e69a23f0b5f7a`,
the hardware-stable cursor ROM
`6c68e95698163bd52b42b763281a92b6a0d143070611a8632bad290ef2b48ed6`,
prepared terminal storage image
`7f431e51004b5843cb52c7a28477329d390b14d25f81c3c18a6cda54d2159153`,
terminal ELF
`37fa27a45e6c37cdc8bf80ae19810708c1b7c95223fcc11cdc1ecc75aef8ffe4`,
and ARM text-plane renderer
`3da485327dfd4f6acf1363835f07d2f1139f57ba9daa0356fa997bb995fdbdf2`.
The active matched ROM/image pair is staged at
`/data/astra/deploy/terminal-ack-37fa27a45e6c`; the earlier bundles and
runtime-modified images remain as hash-named rollbacks. The image changes after
each guest boot; its first active-boot hash was
`a5b5832852a310e92948b4900640ad6693b9ca250aac0735ae4db00fc21a9643`.
At that checkpoint the existing storage image and every FPGA/boot artifact
remained unchanged.

A rebuilt ROM `98a86a124acc7bf47b55572b008e0b6ade0127e530cc6a7c97586104fca2bd58`
passed the exact two-boot host gate but is rejected: on the ARM board QEMU
reached stage 8 and then segfaulted, leaving a torn HDMI frame. Restoring the
hardware-stable ROM with the exact same fixed terminal image passed the gate,
reached stage 8 on Arty, and remained resident. The rejected pair is retained
at `/data/astra/deploy/terminal-cursorfix-98a86a124acc` as failed evidence, not
as a release candidate.

The retained storage hot-path work uses `STOR` v3: `HELLO` transfers one reply
endpoint for the session, command loading reads through one 16 KiB shared area,
and ext4 directory enumeration retains its backend cursor instead of reopening
the scan. Version 2 remains the per-request/inline rolling-update fallback.
Short-lived `which` and `events` clients now send `BYE` on every normal exit;
the terminal gate crosses the complete service-session table repeatedly and
still launches another command. SD-backed and tmpfs-backed command runs were
effectively identical, proving the SD card was not the active limit; the
Cortex-A9-hosted TCG/message round trips were. Against the old image on the
same board, representative Enter-to-prompt times fell from 7.022 to 3.228 s
for `status`, 11.894 to 4.584 s for `which status`, and 11.153 to 4.222 s for
`devices status`. The final six-command hardware gate reports p50 4.375 s and
max 4.584 s. The exact pre-boot optimized image was
`82870f9206d063c94b5c4e8a0bc37dd6644f310ce752e07dc44b0c4c7613af16`;
normal journal and filesystem writes change the active image after boot.
`astra_image.py` now has a real CLI entry point; earlier successful no-op
invocations were the reason several nominally rebuilt images retained old
binaries.

The filesystem now uses lwext4's existing coherent LRU for metadata and file
data with 1,024 4 KiB entries. Before the change, two consecutive terminal
`ls` commands each caused 10 physical block requests; the exact two-boot gate
now reports `cache: first ls 0 physical reads; repeated ls 0` and passes the
complete terminal suite. QEMU exposes read-only request/sector counters on the
machine object so the gate measures beneath VFS and lwext4 rather than caching
shell output. Mount, partition-window, full-volume, allocator, kernel, and ELF
acceptance tests pass.

The remaining latency was not storage. The O(1) ready-bitmap scheduler,
one-shot 5 ms quantum, fixed kernel object pools, and same-address-space MMU
switch path were already sound, but normal terminal boots kept performance and
IRQ-off MMIO timing enabled indefinitely. Storage, events, and the supervisor
lifecycle watcher also stayed runnable in yield/poll loops, forcing needless
cross-address-space switches, cache invalidations, and ATC flushes. Normal
stage-8 boots now freeze that instrumentation while K1 qualification retains
it; services block on `WAIT_ONE`/`WAIT_MULTIPLE`; and VFS waits for its reply
before receiving instead of first making a guaranteed-empty probe. No command
or `ls` response is cached.

On the exact prior ROM/image, ten cached `COMMANDS: ls` operations had a
263,542-cycle median. The gate measures five repeated operations below an
80,000-cycle budget; after the first scheduling fixes the median was 42,636
cycles, and `STOR` v4 directory batching now measures 28,426 cycles. One cached
listing uses four port sends and receives instead of roughly 22, while the
machine-level block counter remains at zero physical reads after warmup. This
is a 89 percent reduction from the original path without caching command
output.

The terminal now blocks indefinitely on its stream, child, and input IRQ
waitables. The IRQ record captures the input head's device sequence, making a
new-key race distinguishable from an undrained old key, and the terminal
acknowledges after draining but before interpreting a key because Enter may
re-enter the pump while a child runs. The former 10 ms input poll remains only
for an older manifest with no IRQ grant. The events service's non-waitable
trace maintenance sweep moved from 100 Hz to 1 Hz; its request and control
ports still wake immediately. A settled 250 ms idle sample fell from 125
syscalls, 52 VM flushes and 52 cache invalidations to 5, 2 and 2.

All 30 kernel host tests, all userspace host tests, the complete two-boot
terminal gate, the events gate, and both normal and K1 release builds pass.
Generated MC68030 code keeps input capture/completion as direct MMIO reads,
tests and branches, with no allocator, division, or helper-call path.

The prior Arty cache deployment was `/data/astra/deploy/cache-fc06ead125de`:
ARM QEMU `8fa55bf02526c24942eace556631bb8d13f8c2dc7a1e439e6eef23916389a3f3`,
ROM `3efc554b2733e758ef5369ee64d39c01ed666069529654dc9ede244a691f2847`,
and pre-boot image
`fc06ead125dea4584c6352a0b2e2fa73a2fe723728d1825fbb9902c3635d1684`.
The previous matched trio is retained in that directory by hash. The board
reached stage 8 and the text plane read through `nuc` showed a live `WORK:>`;
Linux reported 203,260 KiB available with the 128 MiB guest resident. The same
ROM/image also reached stage 8 under the exact 32 MiB guest profile.

The previous generalized-performance deployment was
`/data/astra/deploy/perf-5dad00109a58`: unchanged ARM QEMU
`8fa55bf02526c24942eace556631bb8d13f8c2dc7a1e439e6eef23916389a3f3`,
ROM `5dad00109a583526b99510201aeb35b89a77c3dc44b970213025495c5044e111`,
and preserved pre-boot image
`4f33857e6d56a8948297cc13dd57bb15fd8f9292338bb1b7b4263952f48c866b`.
The exact prior writable image is recoverable there as
`storage-before-perf.img` with hash
`ba432a94aa66cce1660ecba474e5f39fe57288299c64f1308320a4db77dc335d`.
The board reached stage 8 with the physical keyboard and renderer attached; the
shared plane showed a live `WORK:>` and `ACUR` at row 7, column 7. Linux
reported 203,604 KiB available with the 128 MiB guest resident.

The active directory-batch/input-IRQ deployment is
`/data/astra/deploy/perf-irq-batch-1b92d0114799`. It retains the same ARM QEMU
and 128 MiB guest, with normal ROM
`1b92d011479903873de6de104bf113f655db2ddf52fdb25327ddffb0e9890b54`
and pre-boot image
`1842880b2a4662d128324b6d0861b6489f155962ef6df54bc501b5da0ff76c57`.
The exact prior ROM and writable image are recoverable there as
`rom-before.bin` and `storage-before.img`, hashes
`5dad00109a583526b99510201aeb35b89a77c3dc44b970213025495c5044e111`
and `bcec71c83b5022d37744e31d247594ab4997db607754641e2d9384ecc25fc23a`.
After installation the board reached stage 8, rendered a clean `WORK:>`, and
published `ACUR` at row 7, column 7 with an even sequence. The first active
boot changed the writable image to
`f49558d3c3d10f7c9137b7a9d596cb292bf002d980731868e1088de87413e5db`.
At a settled prompt Linux was 97.5 percent idle, QEMU used 0.4 percent of the
dual-core host sample, and 202,876 KiB remained available. Physical typing is
the remaining manual check for this exact deployment; the same ROM/image
already passed QMP keyboard input, repeated directory enumeration, and the
complete two-boot gate on Beast.

The active protected GUI deployment is
`/data/astra/deploy/gui-border-ca9d0c36e98b`. Its exact QEMU source identity is
`8305703f2228ed6cfcc8af5582dc4bfda8f03c34fdec17c3bca12dacf5d4c269`;
the stripped non-LTO ARM binary is
`a7868b9e8956aed8549960e80cde0f30c6a96077290d3524db382542768e1d3c`,
the ROM is
`ba04d60d146518db459081c8163c2657d67e3afd0198c7f8623c8e7d4549b2eb`,
the active pre-boot image is
`db7ccdd8cf7a0725c0f91a2b7a2570424dc8b73b2a5852378cf068224845b4a5`,
and the physical renderer is
`f5c8ed386d780f84a7bf206a7144c57a135702ea15a7a9351b1e8bef01a90aa6`.
The display and desktop process images hash to
`ca9d0c36e98b4535e0799348d5c9d130983c1fdae6db0af0b3ede93cba4d84f7`
and
`75b7bedf078823e1cd4c70fbaa0b39f57c02ec91f9d7485180312524cf6484bb`.
All prior GUI deployments remain intact for rollback.

This profile boots storage, events, the protected display/window server, and
the desktop process without a keyboard. The desktop creates and paints a
900x500 shared RGB565 surface, transfers a read-only duplicate to the `GUI`
service, and receives window ID 1 and a presentation generation. The display
service composes that surface with desktop chrome into its private 1280x720
DMA scanout, submits fence 1, validates completion, and remains resident. The
window shell uses 11-pixel rounded corners; cards use 10-pixel corners; the
client/header seam is flat. The rounded black shell is the only window
separation; shadows and general alpha are deliberately deferred. The exact
host gate reports `ASTRA DISPLAY PASS` in 15,629 of 250,000 cycles. On the
physical Arty, QMP reports one submit and completion, generation 24, and cycles
105,586,084 / 106,608,067 / 106,699,026 for submit, completion, and collect:
1,112,942 cycles end-to-end, about 89.0 ms at 12.5 MHz. The captured
1,843,200-byte big-endian RGB565 payload hashes to
`3bdc87db6ee8b63995884da91923f665bd79051b0c5f491d54fe60b1f6735db6`
and contains the clean composed desktop/window frame. Stage 8 remains live
with QEMU, renderer, and launcher resident and no panic or halt.

The retained protected-display proof is
`/data/astra/deploy/display-dba48d8302bc`. QEMU source identity is
`dba48d8302bcad015866fd78f08f6db993e862cf461d4e5794e89218f4b7ba7f`;
the stripped non-LTO ARM binary is
`e49028f4d8433b3bbdd8ea6b3253dd3c040799ee2313bb5a0cd2a9c9c55c5f56`,
the ROM is
`e9bfdca4c6e6bb8c9dbe114d921a6bdad19786c91fd3952ad6ca57a1482bc028`,
the pre-boot image is
`d3b9ac5611b4e15c5b82faf9e6c306ccec689f2155ee63e8c343651e24704b2e`,
and the physical renderer is
`9987b796c596cab99de46f513e14ad4e9dd77a1e656c7a38b0fb812b9dbf1aac`.
The image was derived from the known-good active terminal image
`63f047921983103abaad7e44d0c25e938c2a3765ab55c3dadf5e228735d8d4ed`;
the previous deployment remains intact for rollback. The first successful
physical boot changed the writable image to
`9a1832ad76c47aeea546449317b79ae437d4ff650cc8ef6803ba4a08e7b1a5d3`.

That image boots protected storage, events, and display processes without a
keyboard. The display process owns the exclusive display lease and Astraea
completion IRQ, submits fence 1 for RGB565 `0x135d`, validates its completion,
and stays resident. The Linux renderer consumes the shared 4 KiB mailbox,
fills the physical 1280x720 framebuffer, commits a fenced scene, and disables
boot text. QMP reports exactly one submit and completion, generation 12, and
cycles 89,811,134 / 91,613,648 / 91,710,626 for submit, completion, and collect:
1,899,492 cycles end-to-end, about 152 ms at 12.5 MHz. The live 1,843,200-byte
graphics mapping hashes to
`390757c13cec9acb69c955af8b467fbeeba48e11508c4ed280d4fe9e64c4629b`,
exactly the expected repeated `13 5d` bytes. Stage 8 remains live with QEMU and
the renderer resident and no panic or halt. The exact host gate measures
14,738 / 250,000 cycles and now waits past stage 8 so an immediate service
cascade cannot pass.

Two failed attempts remain useful evidence. The first inherited stale
Mac-built m68k objects into a Beast build and mixed pointer-return ABIs; a clean
Beast rebuild removed that false fault. The second reached stage 8 and then
lost the events service because a display-only profile had no terminal holding
its control sender. The supervisor now owns that endpoint for the service
lifetime, so optional terminal and GUI clients cannot kill the event service
by disconnecting. The display service also relies on the supervisor's single
10-second startup deadline instead of racing it with a shorter private clock.

An LTO ARM QEMU candidate
`b9eeded89fd25f4373e0a2083746ed7c90e6f57fe5a67e721bc4625b22d98a03`
improved the median by another 16 percent in the controlled timing gate but is
rejected: a live renderer deployment ended in an initial-user-image panic. The
active QEMU is the non-LTO build recorded in the deployment above, and the
build script no longer enables LTO.

The torn display and later QEMU launcher failures had a separate Linux-host
cause. The deployed device tree described all 512 MiB as System RAM while the
renderer mapped and cleared the graphics arena at
`0x18000000..0x1fffffff`; the `no-map` node was reported but its reservation
failed, so the renderer zeroed live page-cache pages, including its own QEMU
libraries. `build_device_tree.sh` now also caps `/memory@0` at 384 MiB. Beast
built device tree
`422c7d48554512f313f19d2e750d19ed2a426b46c53befcbe3e3e4c80ed9cfc4`
and FIT
`c9a77be0f5085ce048860d12bd88ce7a246b813cf76c20339e8c18b7f9358944`
with `SOURCE_DATE_EPOCH=1786326984`; the prior FIT remains
`image.ub.rollback-e9ef016f059c`. After atomic installation and reboot, Linux
reports `0x00000000..0x17ffffff` as its only Normal zone, reports the graphics
arena separately as `no-map`, boots QEMU and the renderer to a clean `WORK:>`
prompt, and leaves the three ARM runtime libraries hash-stable while rendering.
No RTL, routed bitstream, production clock, or graphics resource changed.

On the physical Arty, the renderer maps the 90x30 logical plane across the full
1280x720 active image at `(0,0)` and draws a three-pixel DOS-style underline
cursor with an approximately 512 ms blink period. The live shared record was
`ACUR`, row 7, column 7, visible with an even sequence: the cursor follows the
six-character empty prompt `WORK:>` and its single separator space. When Enter
is accepted, the terminal flushes the cursor at column zero of the following
row before dispatching the command. The guest reaches stage 8, the
scene reaches generation 2 with zero commit errors or deferrals, and the first
2 MiB of the live graphics arena hashes to
`551b0388cb024889b2da8c61b4025b83b94d5fe3ed0392123b2065b7456c7510`
(not the all-zero hash). QMP-injected manual proof completed `cd`, `pwd`,
write, list, cat, delete, directory removal, and an external `status 7`
process. A full board reboot then read `reboot.txt` back as `persisted`, proving
the guest filesystem survives reboot.

The Arty now starts exactly one QEMU runtime independently of physical input.
`run-arty.sh` holds an exclusive `flock`, exposes QMP, and starts
`astra-input-hotplug.py`; that watcher adds and removes QEMU `input-linux`
objects as stable udev keyboard and pointer paths appear and disappear. It
connects to QMP only while reconciling a change, leaving the management socket
available. QEMU, the renderer, and the watcher have bounded shutdown, including
the renderer's previously observed blocked futex wait.

This replaces a stale deployed firstboot waiter discovered on 2026-08-11. The
validated GUI runtime had been started manually without a keyboard while that
waiter still watched for `*-event-kbd`; connecting the keyboard launched a
second default QEMU and renderer built from an older ROM. That incompatible
supervisor exited with status 8 (`ASTRA_STATUS_INVALID`), and the kernel
correctly panicked because its registered initial resident image had died. The
validated runtime itself never restarted. The default paths now contain ROM
`a4ccc0915f402d67f8ac36d5500a6967515910440e7d609631a88fa0cc58fad3`,
QEMU `72dbc394bb7e2d458be3d7a287b3526a50322d52035fbabd9d43225b5fe3aa42`,
and renderer
`79bfebbee40881a9fdc5cd3667c36864a3af002cacc087dfba18cda2f6d7026c`.
The retained rollback is
`/data/astra/deploy/input-hotplug-a4ccc0915f40/rollback`.

On the live board, a keyboard-class udev-link probe made QMP publish and
withdraw `astra-keyboard`. A stronger physical-path test then unbound and
rebound the attached Logitech HID interface: the real pointer evdev node and
`astra-pointer` object disappeared for ten consecutive samples and returned
for ten consecutive samples. QEMU PID 2691 retained Linux start time 3731266,
the guest stayed at stage 8, and the exclusive lock rejected a second
launcher. Beast passes `test-input-hotplug.py` and `test-run-arty.py`. No QEMU
source, guest kernel, RTL, routed bitstream, production clock, or graphics
resource changed.

An Apple Magic Keyboard with Numeric Keypad was previously qualified as
`usb-Apple_Inc._Magic_Keyboard_with_Numeric_Keypad_F0T827700C4HTCYAU-if01-event-kbd`
beside the Logitech trackball. Direct physical typing and human HDMI inspection
proved the full-screen terminal, `ls`, and directory navigation. Physical
create/read/delete/program-launch plus power-cycle persistence remain the final
manual release proof; QMP coverage of those operations does not replace it.

The next event slice is `events --process`; token-bucket and coalescing policy
still requires the measured workload named in the event design.

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

## Protected Astraea render-batch transport

The retained software boundary now carries native Astraea render batches from
a display-owner DMA buffer through the kernel and QEMU display mailbox to the
Arty Linux helper. `DISPLAY_SUBMIT` operation 3 is byte-bounded, requires the
advertised render capability, and packs the validated byte count into the
supervisor-only AstraHost request word. Mailbox version 1.2 carries that exact
range. The Linux helper validates the batch header, fixed ring placement,
resource generation, command headers, sequences, and completion statuses,
then copies the bytes unchanged into the graphics arena and submits them to
Astraea. Kernel host tests, the Linux host validator/self-test, the static ARM
cross-build, and the rebuilt QEMU machine all pass.

The production gallery path now records bounded client draw lists instead of
rasterizing RGB565 pixels on the MC68030. The display service resolves the
shared theme, window chrome, client commands, proportional AFNT text, clipping,
and z order into native `FILL`, geometry, masked `BLIT`, and `GLYPH_RUN`
commands. Alternating 1,843,200-byte scanout allocations prevent Astraea from
writing the protected ACTIVE framebuffer; the helper presents the completed
allocation only after its fence retires. CPU rendering remains only in the host
behavioral oracle and recovery path.

Exact QEMU gate `14ad4b79061f` passes four render batches with 413 commands,
210 fills, 10 blits, and 41 glyph runs in 13,329 of 250,000 MC68030 cycles. The
same artifacts are resident on the Arty at
`/data/astra/deploy/hw-render-overlap-14ad4b79061f`: stripped ARM QEMU
`6554e53ce22b00ce2c4c8cb855cf7907876ebff574efbbe30efb357f39b1d3c2`,
ROM `884987b6a707d00f3b652001d6d93376676db764603ffc6f504435c807e0bc63`,
storage image
`7b40153f620ca4cb0e72daada5d08d372d1709732a12711863e1f42615688757`,
and Linux display owner
`4b79f14e8ebe6275a586644703f890d7e9a8e87c4fc9335c71107bc1da2632c3`.
The board reaches supervisor stage 8 and remains resident. QMP reports four
submissions/completions, operation 3, four batches, the same command mix, and
ordered submit/completion/collect cycles. FPGA submitted/completed counters
advance together from `0x305` to `0x4a2`; the failed counter remains unchanged
at the development baseline `0x58`, last fault is zero, and backpressure,
timeout, and reset counters remain zero.

Theme generation 3 makes ACTIVE title chrome visibly brighter than INACTIVE.
The gallery overlaps all four window types in back-to-front creation order and
places the active Standard window last, proving existing compositing z order.
Pointer-driven raise/focus transitions remain future input-service work; no
parallel focus policy was added for the static acceptance gallery.

## Capability-owned window management checkpoint

The NDK now exposes query, set-frame, move, resize, raise, lower, activate,
deactivate, minimize, maximize, restore, title, and close operations. A
successful GUI v5 create transfers private control and event capabilities; the
display service waits on those endpoints beside the create endpoint, so control
authority does not come from a guessable window ID. It maintains a dense
back-to-front stack, one active visible window, saved restore geometry,
owner-death cleanup, per-window hardware caches, and one bounded union damage
rectangle for each alternating scanout. Movement and exposure use clipped
masked Astraea blits; rounded corners do not require MC68030 pixel repair.

The fourth control endpoint exposed the old qualification quota of four ports
per owner. Control requests are synchronous, so each control port now reserves
one message rather than four. The owner quota is five ports, and the exact
worst-case process handle proof rises from 31 to 33. The handle table uses two
32-bit bitmap words, not 64-bit arithmetic; the change costs 60 bytes per
process and 300 bytes across the five-process pool. Kernel handle, port, and
process rollback tests cover the new boundary.

The exact Beast QEMU gate passes 17/17 native batches with 884 commands, 412
fills, 72 blits, and 68 glyph runs. Its final present takes 13,608 of the
250,000-cycle simulation budget. The hardware deployment is resident at
`/data/astra/deploy/window-management-8f23825cc74d`: ROM
`8f23825cc74d9cae7c5a41e7813a3d3688e409838d9802398fe73920988edbf2`,
storage image
`ff15cbeb3620622d68146446fe655470828d286fe1b892f656defaeb61670dae`,
unchanged stripped ARM QEMU
`6554e53ce22b00ce2c4c8cb855cf7907876ebff574efbbe30efb357f39b1d3c2`,
and unchanged Linux display owner
`4b79f14e8ebe6275a586644703f890d7e9a8e87c4fc9335c71107bc1da2632c3`.
The Arty reaches stage 8 and remains resident. QMP reports 17 submissions and
completions with the same command mix. FPGA submitted/completed counters move
from `0x4a2` to `0x816`, exactly 884 commands; failed remains `0x58`, last fault
is zero, and backpressure, timeout, and reset counters remain zero. The
trackball is present; keyboard input and pointer-driven window policy remain
outside this checkpoint.

## Protected pointer routing checkpoint

The source tree now implements the complete protected path from Vesta input to
application messages. The supervisor launches a fifth protected service named
`input`, grants it the physical input lease and IRQ, and publishes
`INPUT_SERVICE`. The display service connects as the unique seat owner. It
owns screen position, rounded-window hit testing, focus, z order, left-button
capture, titlebar dragging, gadget hover/press state, and close requests.
Applications select bounded per-window event masks; pointer and wheel messages
carry both screen and client-relative coordinates.

The NDK also exposes `astra_pointer_observer_open` for an explicitly authorized
process without a window. It accepts motion/button/wheel masks and reports
screen coordinates only. The service refuses key, text, focus, and seat-owner
requests from observers. Each client has independent motion coalescing and
loss-reset state. Port lifetime revokes the subscription without a global
process ID or invisible window.

Sprite 0 is the hardware pointer. Display mailbox version 1.3 adds one cursor
request carrying clipped x/y and visibility; the Linux display owner installs
the 16x24 cursor in reserved graphics memory, updates only sprite descriptor 0,
and commits the scene. The MC68030 does not redraw the framebuffer to move the
pointer. Hover damage remains a separate native render batch.

The exact Beast QEMU candidate uses source identity
`994698f6c51a6d0c8c1742088acf441e6a5784cb4b8d16cf7abe08d400c6563a`.
Startup consumes 18 requests: 17 native window batches plus the initial cursor.
A QMP relative-X event then produces one hover-damage batch and one cursor
request, moves x from 640 to 646 under the integer acceleration policy, and
retires 20/20 fences. The last startup present costs 13,156 of 250,000 cycles;
the pointer request costs 13,025 of 250,000. All kernel suites, all userspace
suites, NDK functional/sanitizer checks, input sanitizer/analyzer checks,
MC68030 builds, Linux host self-tests, and the static ARM cross-build pass.

The five-service/four-window graph exposed an obsolete IPC capacity, not an
input-path defect: it reserves 18 ports and 64 fixed message records while the
qualified K7 rollback provided 16 and 32. The current source profile is 24
ports, 72 message records, six ports per owner, and 40 records per owner. The
24,864-byte port/message arrays leave six port objects and one complete
eight-message queue beyond the measured composition. Byte and authority limits
and all transactional semantics are unchanged.

The retained board deployment is
`/data/astra/deploy/protected-pointer-51475076aef0`: ROM
`51475076aef0e57b76f6cea43f3fa419b1d7c61410df7f13a6d1de0b5e27fb24`,
storage image
`fa12f9838cc1ac273ebd3820a00c46430bbf70c6ecdb8f7f316393adb50f6f3f`,
stripped ARM QEMU
`71e586aa14daedb5fe2ff9f84bc230d266eeee40d63429e5cc3a39bdadf46c07`,
and stripped Linux display owner
`52a2d35263a5c77b8d98eb56d447f551859f35154ae17534327297774033bb78`.
The Arty reaches stage 8 with the Logitech trackball attached through its stable
evdev path. QMP retires 18/18 startup requests, including one cursor update,
then a relative-X event retires 20/20, moves the cursor from 640 to 646, and
adds one hover batch. FPGA submitted/completed counters advance from `0x816`
to `0xb8a` for the 884-command startup and to `0xc10` for the 134-command hover
batch. Failed remains `0x58`; last fault, backpressure, timeout, and reset
remain zero. Two deliberately rejected intermediate scene commits are retained
in the commit-error counter; the final owner leaves it at `2` with zero commit
deferrals. The previous window-management deployment remains intact as the
rollback.

## Pointer latency and resized-cache repair checkpoint

The protected pointer path now drains the existing eight-record input queue
before rendering. Cursor state is submitted first, motion is coalesced to the
newest ordered record, and at most one damage render follows. No queue,
allocator, transport, or rendering abstraction was added. Exact Beast QEMU
source identity
`5a748c61ca128bf2bcc15de85eecca527392d31af92badc5636118486385a6d8`
retires 20/20 requests: the final present costs 13,266 of 250,000 cycles and
the pointer path costs 13,365 of 250,000. Cursor completion and collection are
now explicitly gated before repaint submission.

The workbench-edge corruption was present in the live framebuffer and cached
surface, so it was not an HDMI artifact. Desktop resized the standard client
from 550x280 to 580x300 while its draw list retained the original bounds; the
reused cache exposed uninitialized right and bottom strips. The shared cache
builder now clears the complete current client through Astraea before replay.
Direct DDR readback verifies a clean 580x300 cache. The provisional cursor
mask was also malformed; sprite 0 now contains a conventional 16x16 `left_ptr`
silhouette with a (3,1) hotspot in the existing 16x24 allocation.

The retained live deployment is
`/data/astra/deploy/pointer-fast-356415ce33cf`: ROM
`356415ce33cf0bf5a64bdce2334f94a134b41855f388eba0662827f8854004f2`,
storage image
`32ed722c959d2b527072eae8228f8a708a7eaccf85cc30631378f154c1acda3f`,
stripped ARM QEMU
`6b31816c8c6aab828238d6fb44864fe4a80de967d4efe993458c283a5da78d09`,
and stripped Linux display owner
`3605c5518fd5889eabbe9cf5365be65f82ebb925055670765b6952b4b3f709a1`.
The Arty reaches stage 8. A six-motion drag burst produces one cursor update
and one render; cursor submit/completion/collection cycles are
3,854,629,088/3,854,708,878/3,854,732,066 and repaint submission is
3,855,282,497. FPGA submitted/completed counters finish together at `0x192d`;
failed remains the retained `0x58`, last fault, backpressure, timeout, and
reset remain zero, and scene commit errors/deferrals remain `2`/`0`.

## Window-drag blitter performance checkpoint

Target hardware profiling found that drag input was already coalesced into one
29-command repaint; the remaining hotspot was the pixel-serial general path for
unscaled RGB565 window copies. A directed 64x16 identity-copy budget test failed
before the change at 9,878 cycles and passes after it at 5,822 cycles. The
retained direct-copy branch applies only to zero-flag, same-format,
same-dimension blits, leaving scaling, reflection, masks, keys, alpha, ROP,
conversion, and overlap handling on their existing paths.

The exact Beast production route passes all 152,192 timing endpoints with
setup/hold/pulse-width slack of `+0.001`/`+0.019`/`+0.538 ns` and all 74,818
nets routed. Active `BOOT.BIN` is
`ac4dea6b90b562edf753d18378b9d8e5521cc26e5544b176b4bba1ad5a79df10`;
the previous `9637e1035acb9d1bd6d2bd0eec2e3cf9ca5c13023560af8d2b4f27a546444504`
image is retained in `/data/astra/deploy/direct-copy-ac4dea6b`.

On the Arty, warm 29-command drag repaints now spend 25.1--26.1 ms in hardware,
down from the measured 29--40 ms range. Ten complete renderer runs and three
each of sprite and copper certification pass. FPGA manager is `operating`, FIT
hash remains `c9a77be0f5085ce048860d12bd88ce7a246b813cf76c20339e8c18b7f9358944`,
and Linux Normal RAM remains bounded at `0x17ffffff`.

## Interactive window terminal checkpoint

The terminal is now the first complete interactive windowed application. It
uses the same shell, editor, VFS, child-stream, and process-launch loop as the
recovery console through a small backend contract; there is no second shell or
GUI-only command path. Its standard resizable window receives focus, physical
key, decoded text, pointer, button, wheel, frame, reset, and close records
through the NDK's eight-record bounded event port. Content is rebuilt as a
shared draw list and rendered by Astraea; the MC68030 coordinates cells and
cursor placement but does not rasterize glyph pixels.

The display service owns rounded hit testing, active-window focus, z order,
title dragging, left-button capture, all eight resize regions, gadget
hover/pressed state, and minimize/maximize/restore/close policy. Maximizing
from the titlebar now toggles back to the saved frame. Geometry-changing
gadget actions recompute hover against the new frame, so a moved gadget cannot
retain a stale hover state. Cursor-only motion submits one hardware-cursor
fence and no render batch; damage-producing interactions still coalesce into
one native repaint.

The startup manifest now distinguishes a resident `service` from an
`application`. Both may be required to launch successfully, but only resident
services are watched as fatal dependencies. Closing the terminal therefore
retires an ordinary application instead of causing the initial supervisor to
exit and panic the kernel. The exact graph requires seven process slots:
supervisor, storage, events, input, display, terminal, and one foreground
command. The matching limits are 38 handles per process and seven shared-area
aliases; the handle bitmap remains two 32-bit words.

All 30 kernel host suites and the complete userspace functional, ASan/UBSan,
and GCC analyzer matrix pass on Beast. The exact QEMU interaction gate types
`pwd`, exercises hardware cursor motion, maximize/restore, southeast resize,
and close, then proves the closed window receives no further input. It retires
26 fences and 19 native batches containing 1,293 commands, 591 fills, 186
blits, and 180 glyph runs. Initial present and cursor service cost
13,712/250,000 and 13,202/250,000 simulated cycles respectively.

The retained Arty deployment is
`/data/astra/deploy/interactive-terminal-a4ccc0915f40`: ROM
`a4ccc0915f402d67f8ac36d5500a6967515910440e7d609631a88fa0cc58fad3`,
prepared storage image
`d1b988c758c158273f7d9d20922c7114d0a58966969ddd51c633f9837df6f286`,
unchanged stripped ARM QEMU
`72dbc394bb7e2d458be3d7a287b3526a50322d52035fbabd9d43225b5fe3aa42`,
and unchanged Linux display owner
`79bfebbee40881a9fdc5cd3667c36864a3af002cacc087dfba18cda2f6d7026c`.
The first boot changed the writable image to
`76009456a847bedc0976cf706df06815a7d9db6930f0a4b69ebf93837f0e2be9`.
The board reaches stage 8 with FPGA manager `operating`; the trackball is
attached through its stable evdev path. Hardware injection of `pwd`,
maximize/restore, and resize advances the settled display from two to nine
batches and from 12 to 77 glyph commands, ending at 15/15 completed requests
with generation 1,414. QEMU and the display owner remain resident, the terminal
remains open for physical use, and Linux reports 200,544 KiB available with
the 128 MiB guest. No RTL, routed bitstream, production clock, QEMU source, or
Linux renderer changed in this checkpoint.

## Arty HDMI audio and front-panel candidate

The working tree contains a stable Linux-to-HDMI audio boundary: one 48 kHz,
signed 24-bit stereo FIFO consumed by the HDMI packetizer. Linux is the mixer,
so PCM players, wavetable synthesis, speech synthesis, and later sources can be
added without changing RTL. The FPGA does not contain one channel per source.

The existing `PNL0` contract now has an Arty AXI wrapper for both board
switches and all four LEDs. LED 3 is the storage-activity light. QEMU can map
the physical panel through `/dev/mem`, exposes it at the existing guest address
`0xfff01000`, and pulses activity for admitted AstraHost requests. The NDK
front-panel API is unchanged.

Focused audio/front-panel tests, the complete graphics regression, both QEMU
memory profiles, and the Linux runtime-supervisor test pass. The exact complete
Route 13 connects every net but fails the 200 MHz domain at -0.960 ns with only
239 of 13,300 physical slices free. The rejected candidate was not flashed;
the active direct-copy hardware release and boot package remain unchanged.
Exact source, route history, artifacts, resources, and the copper-to-scheduler
limiting cone are recorded in
`fpga/arty/graphics/TIMING_CLOSURE.md`.

AMD-guided AXI cleanup has since replaced the three one-to-one HP0/HP2/HP3
SmartConnect blocks with direct AXI4-to-AXI3 protocol converters. Every feature
remains enabled and the complete graphics regression passes. Exact Route 14
removes 2,522 LUTs, 2,479 registers, and 160 unique control sets, but frees only
57 additional physical slices because LUTRAM/control-set packing still leaves
the device at 97.77% slice occupancy. The route has zero routing errors but
fails the 200 MHz domain at -1.064 ns, so it is recorded and not flashed. The
active hardware release remains unchanged.

Route 15 replaces the remaining three-client HP1 SmartConnect with a tested
full-throughput Astra read arbiter and a direct protocol converter. Independent
outstanding tile and sprite reads remain supported through rewritten AXI IDs.
The complete graphics regression passes. Exact routing removes another 2,789
LUTs, 3,708 registers, 433 occupied slices, and 200 control sets, leaving 729
of 13,300 slices free. All 68,686 routable nets connect without error, but the
200 MHz domain still fails at -0.967 ns; the worst cone is now in flood-renderer
address/control generation. The rejected image was not flashed and the active
hardware release remains unchanged.

Route 16 tested a synthesis attribute intended to suppress the flood
renderer DSP clock-enable extraction. Vivado produced the same DSP mapping,
implementation checksums, resources, and `-0.967 ns` routed setup result as
Route 15. The ineffective attribute was removed; the next retained change
uses the existing registered address-valid pipeline to drive each DSP stage
directly instead of relying on an ignored inference hint.

Route 17 retains that structural fix. The complete graphics regression passes,
the old flood DSP-enable critical path is gone, and exact routing frees another
75 physical slices while improving WNS from `-0.967` to `-0.904 ns`. Timing is
still not releasable; the new worst path crosses the HP1 response-slice RID
decode into tile 1 pattern BRAM enable. The image was not flashed.

Route 18 replaces that HP1-only SRL response FIFO with AMD's fully registered
mode. It removes 243 LUTs and the HP1 critical path; exact-route TNS improves
from `-2089.857` to `-528.551 ns` and failing endpoints fall from 9,317 to
4,044. WNS is `-0.859 ns`, now in copper execute/retire control, so the image
is still not releasable or flashed. Packing uses 88 more physical slices even
with fewer LUTs, leaving 716 of 13,300 slices free.

Routes 19-28 pipeline only measured production hot paths while retaining HDMI
audio, front-panel MMIO, and every graphics engine. Exact Beast routes improve
WNS as far as `-0.508 ns`, but none meets the 5.000 ns release constraint and
none was flashed. Route 29 moves published sprite-collision history from
LUTRAM to BRAM, but its unregistered BRAM-to-AXI-read path regresses WNS to
`-0.933 ns`; it is also rejected.

Route 30 restores that existing read boundary and
pipelines the collision-table address/read/commit path without reducing its
one-command-per-clock throughput. Functional sprite, exhaustive 64-way
collision, and integrated graphics tests pass unchanged. The routed sprite
checkpoint passes 200 MHz at setup/hold `+0.096/+0.027 ns`. The exact full
production route improves WNS to `-0.438 ns`, with 2,397 failing endpoints,
but still fails the 5.000 ns constraint and was not flashed. Route 31 targets
only the measured AXI-Lite completion, Copper runtime validation/frame-start,
renderer handshake, and geometry line-error paths. Timing closure remains the
active blocker, and the active board release remains the prior direct-copy
image until a full route passes and completes hardware qualification.

Route 31's exact synchronized graphics regression now passes. A registered
flush-ready experiment was removed after the command integration test proved
it could lose the final writer flush; the live handshake is restored. The
retained command-engine OOC route is `-0.206 ns`, down from the Route 30 full
design's `-0.438 ns`; sequential blitter encoding and an extra mask pipeline
were both measured and rejected as regressions. The exact complete Route 31
route is the next release gate. No Route 31 image has been flashed.

The exact complete Route 31 subsequently connected every net but failed
release timing at `-0.552 ns` setup (`-153.474 ns` TNS, 1,358 endpoints);
hold and pulse width pass at `+0.039` and `+0.538 ns`. Its new authority is the
writer barrier-to-geometry flush-ready path, followed by sprite validation and
Copper dispatch-completion boundaries. The rejected image was not flashed;
Route 32 targets those measured paths.

Route 32's exact Beast graphics regression now passes. Its retained changes
are a correct one-entry writer-flush request buffer, separate sprite
validation accept/reject states, and a registered Copper dispatch endpoint
boundary. These directly cut Route 31's three leading path groups without
removing features or changing the 200 MHz renderer clock. The exact complete
Route 32 production route is now the release gate; no Route 32 image has been
flashed.

The exact Route 32 production route subsequently connected every net and cut
setup debt to `-0.298 ns` WNS and `-64.437 ns` TNS across 877 endpoints; hold
passes at `+0.006 ns`. It remains non-releasable and was not flashed. Route 33
now targets the measured Copper event-FIFO full path, writer-error completion
path, sprite blend DSP input, and flood neighbor-row range path.

Route 33's complete regression passes and its exact full production route
connects all 69,743 routable nets. It removes Route 32's four leading cones and
reduces setup debt to `-0.271 ns` WNS and `-18.549 ns` TNS across 344
endpoints; hold passes at `+0.019 ns`. The image remains non-releasable and was
not flashed. The active measured paths are now graphics-control AXI response,
renderer flush-to-glyph state, and Copper validation/dispatch completion.

Route 34's complete regression also passes and its exact full production route
connects all 70,210 routable nets, but setup regresses to `-0.364/-24.525 ns`
WNS/TNS across 372 endpoints; hold passes at `+0.018 ns`. It is rejected and
was not flashed. The graphics-control response cone is gone; Route 35 now
targets the measured tile-map response validation cone at the head of the new
route.

MMIO-predecode gate 1/5 passes graphics-control behavior and exact control OOC
at `+0.136 ns`; all 3,474 nets route without error. The staged select has
fanout five and no timing path remains from `awaddr_q` to any of the 32 target
clock enables. The predecode is retained. Complete graphics regression is the
last prerequisite before gate 2 performs one exact full production route.

Route 35 removes that tile validation cone with another response stage, and
its complete regression passes, but the exact route regresses to
`-0.417/-60.621 ns` WNS/TNS across 667 endpoints. It is rejected and was not
flashed. The added stage is removed; the next experiment reuses the existing
capture boundary and deletes redundant result state instead.

Route 36 reuses the existing tile response capture boundary, removes the
redundant result state, and passes the complete regression at 1,179/4,444 tile
clocks. Its exact full route connects all 69,772 routable nets but regresses to
`-0.480/-385.768 ns` WNS/TNS across 3,370 endpoints; hold passes at `+0.016
ns`. It is rejected and was not flashed. The tile validation cone is gone, but
the measured route is now led by command-admission-to-AXI-enable, blitter blend,
graphics-control write-decode, and Copper register-write cones.

Route 37 removes reset initialization from the renderer pixel FIFO payload and
passes the focused and complete regressions. It cuts the exact route to 33,854
LUTs, 39,917 registers, and 12,570 slices, but Vivado assigns the shallow
16x64-bit data array a complete RAMB36E1. The full route connects all 68,221
nets and improves setup to `-0.396/-39.477 ns` across 477 endpoints; hold
passes at `+0.014 ns`. It remains rejected and was not flashed. The reset-free
RAM contract is retained; the next route directs only that data array to
distributed RAM before changing any measured geometry or flood logic.

Route 38 implements that data array as distributed RAM and returns the
accidental BRAM36. The exact full route connects all 68,387 nets and reaches
`-0.368/-84.862 ns` WNS/TNS across 1,015 endpoints with `+0.009 ns` hold. It
is rejected and was not flashed. Its leading measured cone is now glyph
range-multiply state into the last-row register enable; the next route removes
the final-step qualification from that enable without changing the computed
final value.

Route 39 removes that glyph last-row enable cone and passes the complete
regression. Its exact full route connects all 68,312 nets and reaches
`-0.383/-78.332 ns` WNS/TNS across 722 endpoints with `+0.009 ns` hold. It is
rejected and was not flashed. The active measured leaders are now glyph
source-sample classification into state, flood pixel-format selection, and
blitter direct-copy/cache qualification. The next route registers only the
glyph classifications at the existing source-decode boundary.

Route 40 registers those glyph classifications at the existing boundary and
passes the complete regression. Its exact full route connects all 68,546 nets
and improves to `-0.305/-45.555 ns` WNS/TNS with `+0.007 ns` hold. It remains
rejected and was not flashed. The active leader is now glyph range state into
step/multiplicand clock enables; the next route applies the existing local
no-enable extraction pattern only to those registers.

Route 41 removes those CE paths with synthesis attributes and passes the full
regression, but its exact route regresses to `-0.502/-16.127 ns` WNS/TNS with
`+0.006 ns` hold. It is rejected and was not flashed. The attributes are
removed; Route 40 remains the active structural baseline. The next route
removes the same enable function by free-running only the otherwise-dead glyph
range step and shifted multiplicand between their mandatory reloads.

Route 42 removes that enable function structurally and passes the complete
regression, but its exact route reaches only `-0.395/-32.417 ns` WNS/TNS across
531 endpoints with `+0.005 ns` hold. It connects all 68,367 routable nets but
is rejected and was not flashed. The free-running change is removed because it
is 90 ps worse than Route 40; Route 40 remains the active structural baseline.
The next measured experiment leaves glyph range mapping alone and targets the
independent flood/HP2 response boundary.

Route 43 preasserts the flood engine's AXI read-response ready signal and adds
a focused contract check. The complete regression passes and the prior
flood-state-to-HP2 response-slice cone disappears. Its exact route connects all
68,382 nets but remains short at `-0.319/-41.884 ns` WNS/TNS across 541
endpoints; hold passes at `+0.014 ns`. The image is rejected and was not
flashed. The one-line AXI simplification is retained; the next measured leader
is geometry state into emit classification.

Route 44 replaces that classification control mux with the direct one-cycle
valid pipeline it represents. Its fail-first focused assertion and complete
regression pass, and the measured geometry cone disappears. The exact route
connects all 68,253 nets and improves setup to `-0.211/-12.291 ns` WNS/TNS
across 233 endpoints; hold passes at `+0.008 ns`. It remains non-releasable and
was not flashed. The next measured leader is the glyph rounded divide-by-255
carry chain.

Route 45 pipelines that glyph rounding through three new 18-bit registers. The
functional tests pass and the original glyph critical path disappears, but the
exact route regresses to `-0.550/-95.201 ns` WNS/TNS across 1,131 endpoints;
hold passes at `+0.011 ns`. The added register bank is removed and the image was
not flashed. Route 44 remains the best measured baseline; the next experiment
reuses the glyph's existing divide pipeline without adding registers.

Route 46 reuses that pipeline and passes exhaustive arithmetic checking plus
the complete graphics regression, but its exact route reaches only
`-0.433/-97.492 ns` WNS/TNS across 1,076 endpoints; hold passes at `+0.028 ns`.
It is rejected and was not flashed. The change is removed and Route 44 remains
the best measured baseline at `-0.211 ns`. Forty-six routes have demonstrated
that isolated critical-cone edits now move failure among unrelated dense
subsystems; further full routes require an architectural clock/resource-margin
decision rather than another local timing experiment.

Post-route hierarchy, control-set, fanout, congestion, and QoR analysis of the
restored Route 44 checkpoint identifies packing rather than feature capacity as
the active blocker. Vivado finds no level-5 congestion window, but 2,590
LUTRAMs, 666 SRLs, and 886 control sets spread 33,956 LUTs and 40,010 registers
across 94.07% of the physical slices. Sprite storage owns 1,754 LUTRAMs and the
largest replicated control/address nets. The active campaign therefore removes
duplicated storage/control machinery before revisiting individual timing cones.
It has a hard ceiling of five exact full routes; functional and synthesis-only
gates reject weak candidates without consuming that budget.

The first two retained synthesis-only cuts preserve the complete feature set.
Host and Copper tile commits now share their off-hot-path validators. Tile span
and descriptor metadata now uses four RAMB18s instead of distributed RAM. The
complete graphics regression passes; worst-case tile build is 1,347 of 4,444
allowed cycles. Exact Beast Vivado 2024.2 synthesis is now 33,632 LUTs, 39,810
registers, 835 control sets, 129 BRAM36-equivalent tiles, and 83 DSPs. The
metadata cut alone removes 672 LUTs, including 576 LUTRAMs, plus 30 registers
and 16 control sets. Those synthesis checkpoints spent no route attempt.

Structural route attempt 1 routes every net and lowers physical use to
12,401/13,300 slices (93.24%), but fails setup at `-0.680/-141.162 ns` across
1,609 endpoints; hold passes at `+0.015 ns`. Its exact leader is the newly
inferred tile-1 span RAMB18 output through three source-coordinate LUTs. The
image is rejected and was not flashed.

The retained registered-prefetch correction passes the complete graphics
regression and exact synthesis at 33,624 LUTs, 40,124 registers, 837 control
sets, 129 BRAM36-equivalent tiles, and 83 DSPs. Netlist timing proves the span
RAMB18 now terminates directly in the prefetch FF bank with zero intervening
LUTs and +1.485 ns synthesized setup slack.

Structural route attempt 2 confirms the old BRAM leader is gone and connects
all 67,723 nets, but remains non-releasable at `-0.373/-35.002 ns` across 452
setup endpoints; hold passes at `+0.010 ns`. It uses 12,432/13,300 slices
(93.47%). The new leader is a five-LUT, routing-dominated HP2 response-error
path into the glyph fault-detail clock enable. The image is rejected and was
not flashed. A third route requires another measured packing/control cut.
Campaign route budget used: 2/5.

That cut is now measured. The sprite admission list is one 242-bit record per
entry and uses an AMD XPM simple-dual-port block RAM because Vivado rejected
the equivalent inferred 64x242 RAM shape and mapped it to 81 RAM64M instances.
The complete graphics regression passes with unchanged sprite cycle counts.
Exact Beast Vivado 2024.2 synthesis `full-synth-sprite-record-xpm-1` uses
33,262 LUTs, including 2,457 LUTRAMs, 40,106 registers, 836 control sets,
132.5 BRAM36-equivalent tiles, and 83 DSPs. Relative to the registered-prefetch
checkpoint it removes 362 LUTs and 135 LUTRAMs while leaving 7.5 BRAM tiles
free. This authorizes structural route attempt 3; campaign budget remains 2/5
until that route completes.

Structural route attempt 3 connects every net but is rejected at
`-0.390/-88.937 ns` across 779 setup endpoints; hold passes at `+0.012 ns`.
It uses 32,661 LUTs, 40,079 registers, 12,464/13,300 slices (93.71%), 851
control sets, 132.5 BRAM36-equivalent tiles, and 83 DSPs. Although LUTRAM use
falls to 2,348, the seven-half-BRAM admission record increases hard-memory
placement pressure and regresses timing from attempt 2. The image was not
flashed and will not be rerouted. Campaign route budget used: 3/5.

The retained replacement stores only the 28 admission-result bits that are
not already authoritative in the scene descriptor: sprite index, clipped
screen X, and span. Preparation rereads the descriptor instead of duplicating
its remaining 214 bits. Full regression passes, including every 1..128 sprite
dimension and 131,072 scaling pairs. Exact Beast Vivado 2024.2 synthesis
`full-synth-sprite-narrow-record-1` uses 33,309 LUTs, including 2,457 LUTRAMs,
40,072 registers, 837 control sets, 129.5 BRAM36-equivalent tiles, and 83
DSPs. The 64x28 XPM maps to one RAMB18, returning three BRAM36-equivalent tiles
relative to attempt 3. This is a synthesis checkpoint only; route budget
remains 3/5.

The exact route-4 candidate also places one 73-bit elastic register at the
existing render-command HP2 boundary. Glyph, flood, and blitter now consume
registered response ID/data/status/last metadata instead of raw shared HP2
signals; back-to-back transfers remain supported. Payload registers are not
reset because their valid bits are. Full regression passes. Exact synthesis
`full-synth-narrow-record-engine-ingress-1` uses 33,348 LUTs, 2,457 LUTRAMs,
40,157 registers, 838 control sets, 129.5 BRAM36-equivalent tiles, and 83
DSPs. This costs 39 LUTs, 85 registers, and one control set over checkpoint 5
while directly breaking the leader shared by route attempts 2 and 3. Route
budget remains 3/5 until the single exact route completes.

Structural route attempt 4 connects every net but is rejected at
`-0.639/-476.709 ns` across 3,108 setup endpoints; hold passes at `+0.012 ns`
and pulse width at `+0.538 ns`. Physical use is 32,672 LUTs, including 2,347
LUTRAMs, 40,002 registers, 12,329/13,300 slices (92.70%), 875 control sets,
129.5 BRAM36-equivalent tiles, and 83 DSPs. The registered HP2 boundary removes
the prior route leader. The new worst path is the narrow admission RAMB18
clock-to-output feeding the distributed active-descriptor address and scene
output registers in the same cycle. That missing boundary also dominates the
sprite endpoint population. The image was not flashed and will not be rerun.
Campaign route budget used: 4/5.

The complete graphics regression now passes after reusing the preparation FSM
to register the admission record's sprite index before issuing the scene-store
descriptor read. This breaks the measured RAMB18-to-LUTRAM cascade at the cost
of one preparation cycle per admitted sprite: worst-case build is 3,877 and
64-way collision is 3,935, both below the 4,300-cycle gate. Exact synthesis is
33,349 LUTs, 40,157 registers, 838 control sets, 129.5 BRAM36-equivalent tiles,
and 83 DSPs. All six admission-RAM-to-index paths terminate at the new register
with zero LUT levels and +1.485 ns slack; no admission-RAM-to-scene-register
path remains. Identical clone/activation descriptor and palette captures are
also consolidated in source, although synthesis proves Vivado had already
merged them and the net resource count is unchanged. The fifth and final exact
route is authorized; campaign use remains 4/5 until it completes.

Structural route attempt 5 connects every net but is rejected at
`-0.320/-53.449 ns` across 807 setup endpoints; hold passes at `+0.016 ns`.
It uses 32,657 LUTs, including 2,348 LUTRAMs, 40,049 registers, 864 control
sets, 129.5 BRAM36-equivalent tiles, and 83 DSPs. The admission-RAM cascade is
gone. Of the remaining failures, 476 terminate in render-command logic: 173
glyph, 125 command-core, 77 flood, 54 blitter, and 47 geometry. The leader is
a 5.031 ns path from replicated command-type state through blitter abort
selection to the shared writer flush-pending register; routing contributes
4.141 ns. The image was not flashed. Campaign use is 5/5, so no Route 6 or
seed rerun is authorized; the next work must structurally remove that shared
render control path and clear regression plus synthesis gates first.

That new campaign gate now passes. The four mutually exclusive render engines'
registered start, abort, and flush pulses are combined directly instead of
passing through the command-type priority mux. The complete graphics regression
passes. Exact synthesis is 33,391 LUTs, 40,140 registers, 838 control sets,
129.5 BRAM36-equivalent tiles, and 83 DSPs. There are zero command-type-to-
writer-flush timing paths; the new worst input path to that register has
+1.572 ns synthesized slack. One exact route is authorized with a fresh hard
ceiling of five; new campaign use is 0/5.

Writer-control route attempt 1 is rejected at `-0.325/-32.435 ns` across 409
setup endpoints; hold passes at `+0.011 ns` and every net routes. Although it
halves Route 5's failing endpoints, it adds 37 occupied slices and exposes new
sprite-start and blitter address/control leaders. The direct-OR experiment has
been removed and no additional route is authorized for it. Campaign use is
1/5, stopped early by the measured regression rather than consuming the cap.

A five-directive command-classifier `max_fanout=16` removal passed regression
and reduced exact synthesis by 60 LUTs, 51 registers, and one control set, but
its placement gate failed decisively: WNS/TNS regressed to
`-0.580/-299.333 ns` across 2,182 endpoints versus the retained source's
approximately `-0.337 ns` placement. It was restored without routing, so it
consumes 0/5 route attempts. Smaller synthesis alone is not acceptance
evidence.

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

## Active compositor-area campaign (2026-08-12)

The blanket control-set threshold is rejected before routing: it cut control
sets from 838 to 537 but added 1,106 LUTs and placed at `-0.492 ns` in 12,379
slices. Hierarchical analysis instead found 4,217 LUTs in the zero-DSP
compositor. Mapping every blend multiply to DSPs removed 3,380 full-design
LUTs and 1,025 placed slices, but exact placement/physopt stalled at
`-0.718/-0.716 ns`; it was not routed.

The first reset-only follow-up unintentionally retained 12 forced compositor
DSPs and was rejected at `-0.503 ns` placement. With those stale attributes
removed, the actual zero-DSP reset-only candidate placed at `-0.311 ns`, but
used 12,395 slices and still failed 1,721 endpoints. Its 0.026 ns WNS movement
is not convergence, so the reset rewrite is removed without routing. The stale
DSP attributes stay removed. Production route budget remains 0/5.

The current measured candidate moves only the eight 8x64 sprite-collision
published banks from RAMB36 to distributed RAM. Full graphics regression
passes. Sprite-line OOC recovers exactly eight BRAM tiles (32.5 to 24.5),
routes at `+0.078/+0.028 ns`, and costs 350 LUTs plus 161 slices. It is now at
the full synthesis/placement gate; no production route has been spent (0/5).
That gate rejected it at `-0.700/-394.231 ns` across 2,276 endpoints and
12,425 slices, with new 1,200--1,400-fanout commit nets. The banks are restored
to block RAM; no production route or physical-optimization run was spent.

The replacement collision-storage candidate kept the eight parallel
current-frame banks but serialized completed-frame publication into one 64x64
BRAM during the existing 320-cycle clear window. Full graphics regression and
OOC routing passed, saving seven BRAM tiles. Full placement rejected it at
`-1.285/-844.057 ns` across 3,296 setup endpoints and 12,420 slices; the worst
paths were routing-dominated blitter register-to-DSP connections displaced by
the changed BRAM anchors. The shared publication is removed. No production
route or physical-optimization run was spent; campaign use remains 0/5.

The active structural candidate removes the blitter's duplicated
round-to-255 DSP chain. One multiplier remains; two 17-bit carry additions
perform the reduction. Exact blitter OOC use falls by 24 LUTs, 17 registers,
and two DSPs and improves from `+0.010` to `+0.071 ns`. The previous
collision-bank restore was also corrected so both current and published banks
are genuinely synchronous BRAM rather than rejected LUTRAM hidden behind an
ignored attribute. Exact sprite-line OOC is back to 32.5 BRAM tiles at
`+0.117/+0.027 ns`. Full regression and the uncontaminated integrated
synthesis/placement gate must pass before any production route; use is 0/5.

## Active production convergence campaign (2026-08-13)

Production route attempt 1/5 routed every net and passed hold at `+0.043 ns`,
but failed setup at `-0.290/-9.653 ns` across 107 endpoints and was not
flashed. Structural gates since then removed the flood row-product DSP-enable
cone. Registering Copper WAIT produced `-0.289/-169.499 ns` placement but
failed the integrated raster contract by releasing a prepared line one cycle
early, so it was rejected before routing.

The new leader was local to the framebuffer line builder: its 11-bit remaining
pixel counter drove the FSM enable through five LUT levels. Registered
pixels-active and last-pixel flags now carry that control while preserving the
counter and all behavior. The directed framebuffer regression passes with
unchanged cycle counts, and its exact OOC route passes at `+0.244 ns`. A full
graphics regression and exact synthesis/placement gate precede route attempt
2. No feature has been removed; route budget remains 1/5.

Copper now exposes the current combinational WAIT result only to the line
scheduler, while internal execution consumes its registered copy and directly
retires fixed WAIT/SKIP actions. Focused and integrated raster tests pass, and
Copper OOC routes at `+0.864 ns` with 16 RAMB36s. The intervening direct path
to execution state was rejected at `-0.726 ns` placement without routing. The
scheduler/execution split is the active full-placement candidate.

That split placed at `-0.460/-0.102 ns` and was rejected without routing after
Vivado folded the flood row operand back into a conditionally enabled DSP
stage. The retained operand boundary now proves `AREG=0` and constant `CEA2`
on both flood row DSPs in OOC and full synthesis. Its exact full placement is
still rejected at `-0.510/-0.193 ns`; the measured leader moved to Copper's
program-bank BRAM output crossing a redundant hold-data mux. The next gate
loads the already-valid-qualified program result continuously. Focused tests
pass and Copper OOC routes at `+0.762 ns` with all 16 BRAMs. No additional
production route has run; campaign use remains 1/5.

That continuous-load candidate is resource-neutral and improves exact full
placement setup to `-0.299 ns`, removing the Copper program-read cone, but its
`-0.261 ns` hold result is not route-worthy. The measured successor was a
duplicated wide zero reduction over render command words 12--14. Validation
now registers and reuses seven word-presence facts; the complete 45-command
test passes and the targeted cone is absent from exact render-command OOC
timing. A fresh exact full placement is the next gate. Route use remains 1/5.

That placement rejects at `-0.504/-0.186 ns` without routing. Its measured
leader is the flood right-edge comparison crossing three carry levels into
state decode. A registered right-scan-exhausted fact now updates alongside
the active coordinate, preserving scan cadence. Focused flood and complete
command tests pass, and exact render-command OOC routing removes the targeted
path. Full placement is next; production route use remains 1/5.

The subsequent five-gate structural campaign is closed. A stale Beast copy of
the flood RTL invalidated one nominal flood measurement; correctly synced
evidence rejected flood at `-0.719/-0.281 ns`, glyph source-kind at
`-0.447/-0.120 ns`, geometry direction facts at `-0.500/-0.343 ns`, and the
final blitter command-fact candidate at `-0.698/-0.393 ns`. Gate 5's leader is
the centralized pixel dispatch FIFO, with 4.298 ns of its 5.024 ns data path
spent routing. None was production-routed or flashed; production route use is
still 1/5, and there will be no sixth placement in that campaign.

The active cut removes that redundant two-entry FIFO because the pixel writer
already supplies registered ingress, two pixel staging entries, and a 16-entry
write FIFO. The 45-command regression passes unchanged. Its first exact OOC
route removes the dispatch path and uses 10,257 LUTs, 12,014 registers, and 27
DSPs for the command subsystem, but rejects at `-0.285 ns` on the existing
blitter state-to-source-address path. It is not admitted to full placement.
New campaign use is 1/5 OOC gates and 0/5 full placements.

That dispatch campaign stopped at 4/5 after successive blitter-address,
glyph-format, and pixel-ingress cuts. Its best exact full placement was
`-0.404/-0.262 ns` at 32,711 LUTs, 38,971 registers, 129.5 BRAM tiles, and 81
DSPs; the leader moved to Copper validation directly selecting a BRAM output.
No fifth gate or route was spent.

The closed Copper-boundary campaign used the existing execution capture stage
for validation and split program readback into two registered selections. Both
direct BRAM mux leaders are absent. An exact intermediate placement reached
`-0.285/-0.126 ns`, but the full regression caught a missing post-capture
permission cycle before routing. The final source restores distinct pre- and
post-capture stages and passes direct Copper, AXI control, and integrated
pipeline tests. Its gate-5/5 Copper OOC route is `+0.838 ns` with 595 LUTs,
662 registers, and all 16 RAMB36s. No final-source full placement, production
route, bitstream, or flash exists. Production route use remains 1/5; the next
measured blocker is glyph state-to-DSP enable and its campaign starts at 0/5.

The glyph-boundary campaign is closed at 4/5. Glyph operand preservation and
direct engine `ARREADY` return remove the glyph DSP-enable and ownership
feedback cones. Exact full placement uses 32,056 LUTs, 38,701 registers,
129.5 BRAM tiles, and 81 DSPs, but rejects at `-0.605/-0.184 ns` on the
blitter's unregistered round-to-255 source path. Reusing its existing divide
register passes the blitter regression and removes that path in exact OOC
routing. OOC still rejects at `-0.227 ns` on engine completion/fault capture,
so no fifth full build was spent. No production route or flash followed;
route use remains 1/5. The next campaign targets only that completion-control
boundary.

The completion-boundary campaign is also closed at 4/5. Registered completion
capture and a flood stack-nonempty fact removed their targeted cones; disabling
glyph FSM reset extraction produced `+0.006 ns` render-command OOC setup. A
fourth experiment failed to map an added operand stage into the DSP and was
reverted. No full placement, route, bitstream, or flash followed.

Timing closure is now paused for a build-flow reset. Leaf OOC routes remain
diagnostic only because they lack final clock roots, Pblocks, contained
routing, and partition pins. The next gate qualifies the existing Vivado
incremental-checkpoint path and then pilots physically constrained reusable
checkpoints at three natural boundaries: the whole render command processor,
the sprite line builder, and Copper control/events. Stable blocks remain
closed while one active partition changes; one incremental full route is the
integration authority. Production route use remains 1/5.

The incremental-checkpoint path is now qualified and the five-invocation pilot
is closed. The build fails closed before bitstream generation unless routing,
setup, and hold all pass. A clean exact-current-source route is the new stable
reference (`-0.397/+0.010 ns`); its measured block footprints overlap, so no
bad placement was frozen into Pblocks. Registering the shared async FIFO full
state passes the complete behavioral suite and removes the prior audio FIFO
leader. The final incremental route reuses 98.71% of cells and 92.60% of nets,
is fully routed with zero errors and `+0.010 ns` hold, and improves setup to
`-0.283 ns`, but still fails across 525 endpoints. No bitstream exists and
nothing was flashed. The next campaign begins from the final DCP and targets
the measured glyph/blitter/Copper routing clusters with a zero-slack
timing-closure target; it must not use `RuntimeOptimized`'s inherited negative
reference WNS as the post-route optimization threshold.

The zero-slack campaign has consumed 1/5 implementation runs. Native Vivado
`TimingClosure` targeting is now active and the complete graphics regression
passes after localizing glyph destination decode and blitter response-valid
ownership. Run 1 fully routed with zero errors and `+0.010 ns` hold, but setup
rejected at `-0.582 ns`; it intentionally used plain `Performance_Explore`
and proved that strategy omits the required post-route physical-optimization
stage. No bitstream was written. Run 2 keeps the same RTL and restores the
pilot's exact `Performance_ExplorePostRoutePhysOpt` strategy.

Run 2/5 is also rejected. It fully routes all 66,517 nets with zero errors;
post-route physical optimization improves setup from `-0.671` to `-0.607 ns`,
while hold remains `+0.010 ns`. No bitstream is written. The measured leader
is command-kind decoding crossing engine response consumption into the HP2
response-slice payload enable; the next independent path is response metadata
into glyph error/state decode. Run 3 is gated on one shared response-boundary
repair and regression, not another implementation-strategy trial.

The run-3 boundary repair is regression-clean. Registered two-beat engine
response capacity removes HP2 ready/metadata feedback, and pixel-writer ready
now reflects registered ingress capacity rather than pending policy. Exact OOC
timing improves from `-0.303 ns` to `-0.080 ns`; the former response and flush
families are absent. OOC tuning stops here. Exact full integration run 3/5 is
the next authority; no bitstream has yet been generated or flashed.

Run 3/5 fully routes 66,760 nets with zero errors and `+0.010 ns` hold but is
rejected at `-0.500 ns` setup across 1,297 endpoints. No bitstream is written.
Its full-design census found an incomplete internal READY decoupling, per-pixel
glyph format classification, and queue-dependent command-address enables.
Boundary tests prove all three corrections; command-block OOC improves to
`-0.050 ns`. Full regression and exact run 4/5 are the next gates, using the
run-3 current-source DCP as reference.

Run 4/5 fully routes 66,804 nets with zero errors and `+0.010 ns` hold,
but is rejected at `-0.347 ns` setup across 886 endpoints. No bitstream is
written. Failures are distributed across command, glyph, flood, and blitter
control, with roughly 75--81% routing delay on the reported critical groups;
Vivado finds no level-5 congestion window. Its automatic QoR report instead
identifies 92% BRAM pressure and placement-time replication opportunities.
Run 5 is gated on applying that measured suggestion file to a clean,
non-incremental exact build rather than another RTL or seed experiment.

Run 5/5 applied that QoR file to synthesis and implementation. It reduced BRAM
pressure to 105 tiles (75.00%) and the exact clean full route completed all
69,178 nets with zero errors and `+0.017 ns` hold. Setup still rejects at
`-0.277 ns` across 153 endpoints, so no bitstream was written or flashed. The
closed campaign's residual is 99 sprite clear/copy/working-line endpoints and
54 other endpoints led by command deadline state. A new structural campaign
starts at 0/5 and must qualify that sprite boundary in simulation and exact OOC
before another full implementation. Run-5 artifacts were accidentally placed
in an ephemeral remote directory and removed after the fail-closed exit; the
next campaign must retain and hash evidence in a persistent remote path.

Sprite structural gate 1/5 uses a distinct copy address counter so completion
policy no longer resets and fans out the clear counter. Its exact 200 MHz OOC
route retains 37 BRAM primitives and removes the integrated clear/copy family,
but rejects by 4 ps setup (`+0.028 ns` hold) on render-slot-to-phase clock
enable. One local render-load state removes that measured decode; directed
tests pass and the eight-mode sprite regression is the next gate before OOC
gate 2. No full implementation, bitstream, or flash follows from gate 1.

The extra render-load cycle was rejected because it measured 3,927 cycles
against the 3,900-cycle worst-case budget. Idle payload preload removes the
same indexed-ready clock-enable cone without latency and measures 3,878 cycles.
Sprite gate 2/5 passes exact OOC setup at `+0.147 ns`, retains 37 BRAM
primitives, and the complete graphics regression passes through all 45 render
commands and exhaustive sprite dimensions. One clean exact full integration
with the qualified QoR file is now authorized; no bitstream or flash exists
yet.

Gate 3/5 stopped before synthesis because Beast's partial source mirror lacked
the HDMI-audio RTL directory. It is not timing or resource evidence and wrote
no bitstream. A complete Arty/shared-SoC source sync and fresh persistent gate-4
project are required; the failed invocation is not retried in place.

Gate 4/5 is a complete exact production route after the full source sync. All
69,166 routable nets complete with zero errors and hold passes at `+0.010 ns`;
setup improves to `-0.208 ns` (`TNS=-6.098 ns`, 109 endpoints). No bitstream is
written. The measured leader is a 2.614 ns routed blitter net from the
`dont_touch` `blend_divided_q` register into result accumulation. Gate 5 is
reserved for the single existing-pattern correction that permits local
register replication, after regressions; it is not another seed or strategy
trial. A flash and board release test are authorized only if that exact gate
passes setup, hold, route, methodology, and build-identity checks.

Gate 5/5 passes the complete graphics regression and routes all 69,287 nets
with zero errors. Hold is `+0.013 ns`; setup improves to `-0.191 ns` across
123 endpoints, so no bitstream is written and the campaign is closed. The
blitter correction is retained, but the new leader proves the qualified QoR
file's explicit distributed-RAM override is harmful for the eight 512x32
sprite working-line memories: their worst synchronous read spends 4.173 ns in
routing. A fresh campaign starts at 0/5 and first changes only the shared array
name to exempt those memories from the stale literal QoR target. Full
synthesis must prove block-RAM mapping and safe capacity before another route.

Working-memory gate 1/5 passes that full synthesis check. The complete graphics
regression passes, all eight 320x32 sprite working memories map to RAMB18E1,
and total BRAM use is 109/140 tiles (77.86%). One exact full production route
with the unchanged qualified QoR file and strategy is now authorized. No
bitstream or flash exists yet.

Working-memory gate 2/5 routes all 67,466 nets and passes hold at `+0.050 ns`,
but setup rejects at `-0.171 ns`; no bitstream is written. Exact resources are
34,303 LUTs, 40,065 registers, 109 BRAM tiles, and 81 DSPs. The repaired sprite
memory family is no longer the blocker. The measured leader is glyph start
decode driving 201 extracted clock-enable loads, so gate 3 is a regression and
OOC cone-removal check using the module's existing no-enable-extraction policy.

Working-memory gate 3/5 passes the complete graphics regression and removes
the targeted clip-enable family from exact routed render-command OOC. Its
remaining `-0.075 ns` reset path is the documented missing-context OOC artifact,
not the full-design gate-2 leader. Gate 4 is authorized as one clean exact full
route with no strategy, seed, QoR, or feature change.

Working-memory gate 4/5 routes all 67,525 nets and passes hold at `+0.033 ns`,
but setup rejects at `-0.150 ns` across 112 endpoints; no bitstream is written.
The residual is 64 glyph state-to-clock-enable endpoints followed by seven
Copper AXI-decode-to-dispatch-write-enable endpoints. Gate 5 is the bounded
structural cut of those two measured families, after targeted regression.

Working-memory gate 5/5 routes all 67,539 nets and passes hold at `+0.005 ns`,
but setup rejects at `-0.245 ns`; no bitstream is written and the campaign is
closed. Copper staging removed its measured family and is retained. Broad glyph
enable suppression is rejected because routing lost the positive placement
result. A new campaign starts at 0/5 with glyph changed from the lone sequential
render FSM to the one-hot policy already used by every other render engine;
exact render-command OOC is required before another full route.

Glyph-FSM gate 1/5 is rejected before full integration because Vivado did not
infer the glyph FSM and ignored the requested encoding. Targeted behavior still
passes. Gate 2 replaces the unenforced hint with explicit 55-bit one-hot RTL;
no full timing run is authorized until its netlist shape and OOC paths pass.

Glyph-FSM gate 2/5 is rejected before full integration. Explicit 55-bit
one-hot state preserves behavior but creates a `-1.841 ns` high-fanout glyph
state path, so that implementation is not retained. Gate 3 restores the
compact state register, removes the state write from the shared failure task,
and requires synthesis-log proof that Vivado infers and one-hot encodes the
FSM before a full route. Campaign use is 2/5; no bitstream was written.

Glyph-FSM gate 3/5 passes behavior but is rejected immediately after synthesis:
moving the failure transition out of its task still does not make Vivado infer
the glyph FSM. Placement was interrupted and no route or bitstream was spent.
Gate 4 removes only the state register's glyph-only `extract_reset="no"`
attribute and must prove inference, one-hot encoding, and OOC timing. Campaign
use is 3/5.

Glyph-FSM gate 4/5 proves `extract_reset="no"` was not the recognition blocker;
targeted behavior passes, but Vivado still omits glyph from its FSM report, so
the run is stopped before route. The same log omits flood and infers blitter and
command, isolating the shared outside-case abort transition. Gate 5 moves that
glyph priority transition under `case (state)` without changing its behavior.
Campaign use is 4/5; no bitstream was written.

Glyph-FSM gate 5/5 also fails recognition after targeted behavior passes. The
run is stopped before route, no bitstream is written, and the campaign closes
with all FSM experiments rejected. The source returns to the compact baseline.
A new engine-response locality campaign starts at 0/5 from the retained full
`-0.245 ns` path: gate 1 limits fanout on its registered response-valid source
and requires behavioral plus exact OOC locality evidence before a full route.

Engine-response gate 1/5 passes the complete graphics regression. Exact OOC
routes all 19,215 nets with zero errors and proves seven bounded-fanout valid
register copies; glyph loads use local replicas and the prior boundary family
is absent from the top 50. The isolated `-0.320 ns` command fault path is the
known OOC-context residual. Gate 2 is one clean exact full production route
with the unchanged clock, strategy, and qualified QoR file.

Engine-response gate 2/5 routes all 67,615 nets and passes hold at `+0.041 ns`,
but setup rejects at `-0.185 ns`; no bitstream is written. Exact resources are
34,294 LUTs, 40,168 registers, 109 BRAM tiles, and 81 DSPs. Response locality
is retained. The new leader is AXI write decode into the eight
`shadow_tile1_control` clock enables; gate 3 targets only that register and
requires graphics-control regression plus exact control OOC before another
full route.

Engine-response gate 3/5 passes graphics-control behavior and exact OOC at
`+0.331 ns`. All 3,462 nets route with zero errors, and every
`shadow_tile1_control` CE is tied to VCC, proving the measured family is gone.
Gate 4 is one clean exact full production route with no other RTL, seed,
strategy, clock, or QoR change.

Engine-response gate 4/5 routes all 67,641 nets and passes hold at `+0.050 ns`,
but setup regresses to `-0.265 ns` across 241 endpoints; no bitstream is
written. The target CE family is gone, but applying no-enable mapping to all 32
bits disrupts packing and makes 40 blitter product-to-divide endpoints lead.
That broad attribute is rejected. Gate 5 restores the gate-2 mapping and splits
out only the eight transparent-index bits that formed the original leader;
targeted behavior and OOC structural proof are required before the campaign's
final full route.

Engine-response gate 5/5 passes the complete graphics regression and exact
control OOC at `+0.018 ns` setup. All 3,477 OOC nets route with zero errors.
The eight transparent-index registers alone have CE tied to VCC; the remaining
24 tile-control bits retain their original shared enable. The campaign's one
final exact full production route is now authorized with no further source,
seed, strategy, clock, QoR, or feature change.

Engine-response gate 5/5 full integration routes all 67,228 nets and passes
hold at `+0.012 ns`, but setup rejects at `-0.294 ns` across 149 endpoints; no
bitstream is written. The target CE is gone, but its `dont_touch` boundary
globally harms placement, so the entire split-register experiment is rejected
and removed. Response-valid locality remains retained. The campaign closes
with gate 2 (`-0.185 ns`) as the authoritative source checkpoint. A new 0/5
campaign will predecode only the measured tile-control write select beside the
existing write pipeline; behavior and exact control OOC proof precede any full
route.

MMIO-predecode gate 1/5 passes graphics-control behavior and exact OOC at
`+0.136 ns`, proving the staged write select removes every AXI-address-to-tile-
control CE path. The complete graphics regression also passes. Gate 2/5 routes
all 67,080 production nets and passes hold at `+0.017 ns`; setup improves from
the retained `-0.185 ns` checkpoint to `-0.111 ns` across only 23 endpoints.
No bitstream is written. The predecode is retained, and gate 3 targets only the
new five-level full-width geometry-opcode selection before another full route.

MMIO-predecode gate 3/5 passes the complete graphics regression and exact
render-command OOC. The changed geometry endpoint improves to `+1.821 ns` with
two logic levels; only the three-bit geometry subcode reaches it. The OOC
block's unrelated blitter path remains `-0.239 ns`. Gate 4 is authorized as one
unchanged-strategy exact full production route; no bitstream exists yet.

MMIO-predecode gate 4/5 routes all 67,162 nets and passes hold at `+0.013 ns`,
but setup remains `-0.101 ns` across 15 endpoints; no bitstream is written. The
geometry family is gone. The new leader is Copper validation computing and
consuming an unregistered range-end sum despite an existing next-cycle range
state. Gate 5 moves that decision onto the registered boundary before the
campaign's final exact route.

MMIO-predecode gate 5/5 passes the complete graphics regression and exact
Copper OOC at `+0.762 ns`, retaining all 16 Copper RAMB36s. The prior
validation-count-to-range-result path count is zero; the remaining endpoint is
`+1.471 ns`. One unchanged-strategy exact full route is now the final campaign
and release gate. No bitstream has yet been written or flashed.

That final exact route connects all 67,130 nets and passes hold at `+0.007 ns`,
but setup rejects at `-0.230 ns` across 40 endpoints; no bitstream is written.
Exact resources are 34,152 LUTs, 40,177 registers, 109 BRAM tiles, and 81 DSPs.
The Copper target is gone. The new leader is response-error control into glyph
fault-detail clock enables at `-0.230 ns`; scheduler slot-tag enables follow at
`-0.131 ns`. The MMIO-predecode campaign is closed at 5/5. Its proved local
changes remain retained, gate 4 remains the best exact timing checkpoint at
`-0.101 ns`, and the next bounded campaign begins only with exact OOC analysis
of the measured response-error/glyph-enable family. Nothing is flashed yet.

A full-feature 166,666,672 Hz baseline now passes exact implementation: all
66,837 nets route with zero errors, setup is `+0.144 ns`, and hold is
`+0.019 ns`. Vivado successfully writes a 4,045,683-byte bitstream and an XSA
containing it. Exact use is 33,409 LUTs, 40,069 registers, 109 BRAM tiles, and
81 DSPs. The block design proves requested and actual FCLK1 match. A redundant
wrapper check after `close_design` caused the command to exit 2 after the valid
artifacts were created; that checker is corrected without rerouting. FSBL,
device-tree, flash, and repeated board qualification are now the active gate.
The 200 MHz timing target remains open separately.

The exact 166,666,672 Hz release is now installed on the Arty attached to
Beast. Cold boot verifies BOOT.BIN
`98228e7a73f665576cc7855fc0b771b1f7618b57cbce35f57cb71e1a5ed119cf`,
FIT `a39e9b1e3b9a59742d288063822ee57d0b7f598decf2a02e8369ffe15b80c833`,
FPGA-manager `operating`, graphics generation 1 with capabilities
`0x000003ff`, nonzero build identity `0x18EBE2E1`, MC68030/PMMU POST, 128 MiB
guest RAM, stage 8, and the terminal runtime. Linux sees only the lower 384 MiB
of DDR; 128 MiB is the no-map graphics arena and QEMU preallocates the 128 MiB
guest from Linux's cached RAM, leaving the intended 256 MiB host-services
budget before kernel and process overhead.

The Linux runtime tuner is active and tested rather than merely planned. It
uses QMP to identify the actual TCG vCPU thread, pins it alone to ARM CPU1 at
nice -10, and pins QEMU coordination/I/O threads to CPU0, where the board's
device IRQs already land. The restored production runtime showed three QEMU
host threads allowed only on CPU0 and the vCPU allowed only on CPU1 with
priority 10/nice -10. Beast passes the input/runtime-tuning and exclusive-
launcher regressions.

Five alternating cold-boot pairs on the physical board compared default Linux
scheduling with that production split using the same ROM, 128 MiB preallocated
guest, and copied filesystem. The CPU-heavy POST median fell from 0.494 s to
0.221 s, a 2.236x speedup. Relative to the retained approximate 30 MHz TCG
reference, this workload is about 67 MHz equivalent; report it as roughly
65--70 MHz equivalent because TCG has no literal emulated clock rate and later
boot stages contain fixed waits and storage/service latency. Median terminal-
ready time was unchanged at 11.838 versus 11.893 s, confirming that its
remaining delay is not CPU execution. CPU1 remains reserved for MC68030/PMMU
execution; future host-backed audio synthesis/mixing and fixed-math workers
belong on CPU0 and communicate through bounded preallocated queues.

The sprite compositor now retains 64 global descriptors while admitting at
most 16 topmost spans and 2,048 destination pixels per scanline. Overflow is
deterministic and visible through the existing status/bitmap. Complete graphics
regression passes; the 16-by-128 case completes in 1,562 renderer clocks versus
3,865 for the former unrestricted 64-sprite line.

A five-attempt exact 200 MHz campaign did not close timing. The best attempt
reached `-0.141 ns`; the final route connected all 66,393 nets with zero route
errors and passed hold at `+0.050 ns`, but setup failed at `-0.152 ns`. It used
32,289 LUTs, 38,942 registers, 129.5 BRAM tiles, and 81 DSPs. The remaining
leaders are distributed across glyph/DSP enable, surface validation, sprite
preparation, glyph range, and AXI/Copper dispatch paths; the old sprite
scanline-throughput bottleneck is no longer the active blocker. The campaign
is closed at 5/5 and produced no bitstream. The Arty remains on the qualified
166,666,672 Hz release with build identity `0x18EBE2E1`.

The complete 187,500,000 Hz release candidate now passes exact implementation
without a feature cut. Its final route connects all 66,520 nets with zero
errors and passes setup/hold at `+0.173/+0.009 ns`; use is 31,957 LUTs, 39,070
registers, 12,263 slices, 129.5 BRAM tiles, and 81 DSPs. Placement and routing
used a 200 MHz margin target, while the block design, FSBL, device tree, and
release reports all retain the exact 187.5 MHz runtime clock. A complete
BOOT.BIN/FIT package and host-tested Linux tools exist on Beast.

This is now the active hardware authority. Hash-verified atomic deployment
installed BOOT.BIN
`3010d5f5fe5b19c9ef094823e8fbedd0d87fb8105c9c848c8ff65f6cd64be555`
and FIT
`cbf7ce8615c49c6f9d959b48d57326a8de3d3ec4b721a4ca7c5b781a47123679`
while retaining rollback copies. Three consecutive boots pass FPGA-manager,
the exact 187.5 MHz device-tree clock, MC68030/PMMU and SDRAM POST, 128 MiB
guest RAM, filesystem round-trip, terminal display, and stage 8. Linux owns
only the lower 384 MiB and QEMU preallocates 128 MiB for Astra.

Ten consecutive renderer, Copper, sprite, and HDMI-audio certifications pass,
plus a final complete sweep after boot three. The sprite certificate was
corrected to validate the published 16-span/2,048-pixel policy through shared
NDK constants; the audio certificate was corrected so its 1 ms polling window
cannot outrun a shorter silence tail. These were software-test defects found
by physical qualification, not RTL failures. Evidence is retained under
`docs/evidence/astra-arty-187m5-20260814/`. Beast has no HDMI capture device;
HDMI evidence is the exact splash readback, terminal-ready state, and repeated
48 kHz audio certification.

## Active front-panel/reset release (2026-08-15)

The active Arty hardware authority is now the exact full-feature 187.5 MHz
front-panel/reset release. Its route connects all 66,515 nets with zero errors
and passes setup/hold/pulse width at `+0.250/+0.010/+0.538 ns`. It uses 31,904
LUTs, 39,006 registers, 12,203 slices, 129.5 BRAM tiles, and 81 DSPs. The
bitstream is
`3fca60ea1af0aca2110633fe21561154e40f48d5d5713c8981b8388ca3c52afc`;
active BOOT.BIN is
`545f0ccb259972bc7fc26c08f9080dc7033ef7627693ff1ff03085c98a9e3d9c`
and active FIT is
`74838cdca1f45205bd2d69e6fba51f59b5fae43c2de39fde3e8f9cdc4ed4eb2d`.

The PL exposes all Arty controls through the shared `PNL0` MMIO contract: four
mono LEDs, three RGB channels, four buttons, two switches, software ownership,
atomic output operations, and HDD activity on mono LED 3. Register-level
hardware tests pass; all four physical buttons and both physical switches
assert, release, debounce, and latch as the exact `0x030f` change mask.
Firmware POST reports `Front panel ....... OK`.

The final physical power cycle passes FPGA manager, exact 187.5 MHz clocks,
1 Gbit/s Ethernet, MC68030/PMMU and full-range SDRAM POST, 128 MiB Astra RAM,
filesystem round-trip, terminal startup, and initial-image stage 8. Ten
consecutive complete renderer/Copper/sprite/HDMI-audio sweeps pass, as does a
complete sweep after a live QMP reset. QEMU remains the same process across
QMP reset, Linux evdev pointer detach/reattach, and physical Apple-keyboard
hotplug; both keyboard and pointer QMP objects are present without another
POST or panic. The ARM QEMU hash is
`a534f8f7af75743c3cfd71ef5854a57dc75a4bdfafb6a1f5bedcb668ad768220`;
the reset-capable launcher hash is
`5a4e35f1929773d27e5d55ed3bbefdf5b9caa8d47a8819c83f355b3d9e1d9400`.

The cold-boot network fix is sequencing, not a PHY workaround: the shared chip
reset helper lets Linux attach `eth0` once before asserting the global FCLK1
reset, preventing the RTL8211F ALDPS timeout reproduced when reset ran first.
Linux software `reboot` currently halts instead of resetting the Zynq; power
cycle, JTAG reset, QMP guest reset, and runtime crash handling are qualified.
Exact artifact identities and experiments are in
`fpga/arty/graphics/TIMING_CLOSURE.md`.

## Active GUI mono-terminal release (2026-08-15)

The Arty is running the unchanged qualified front-panel/reset bitstream with a
new Astra ROM and display image; no RTL, synthesis, or flash artifact changed.
The active ROM is
`4c4a2b943239f1313415a71ebd43532898495f11ff21cc84f5621c02f3fe3808`.
The installed display image is
`5612be150351e8c198c2326ad7900459f89ae3fc0efbd1f4af6ef8757807f473`
and is retained with rollback artifacts at
`/data/astra/deploy/terminal-mono-4c4a2b943239`; the exact prior live ROM and
storage image are retained at
`/data/astra/deploy/pre-terminal-mono-448d10f35635`.

Window content now lives in persistent hardware-rendered surfaces. Applications
publish bounded content damage through `astra_window_present_region`; gadget
chrome rebuilds reuse that content instead of replaying every glyph. Cursor
position commits immediately even when hover changes require a later chrome
render. The terminal publishes only changed runs plus a 500 ms,
full-cell-width underline cursor. The former 13-pixel proportional Workbench
strike was laid out at the widest-glyph advance, which made narrow letters
appear widely spaced. It is replaced by the complete Spleen-derived Astra Mono
8x16 AFNT strike and its native 8-pixel cell. The system height and cell advance
are published by the NDK theme and the face retains the public
`ASTRA_FONT_ROLE_MONO` identity; raw AFNT data remains private to the
font/rendering implementation. The terminal records semantic mono-text draw
commands and no longer links a private glyph bank: its text image fell from
99,813 to 27,331 bytes. The display service owns the one complete glyph bank.

The supervisor no longer has a second, arbitrary 80 KiB executable ceiling.
Its service cache reuses the kernel's established 256 KiB
`ASTRA_USER_IMAGE_MAX_SIZE` acceptance policy. This admits the 89,858-byte
display service while retaining the same kernel ELF validation boundary.

On the physical board a typed-cell update fell from 78 commands, 113.1 ms of
render preparation/hardware work, and 16.2 ms present time to alternating
10/11-command cursor/cell batches with 4.7--5.5 ms render time; hardware work
inside those batches is about 1.1 ms. Gadget-hover batches are 13 commands and
about 5 ms instead of forcing the former roughly 119 ms full-window path. The
host interaction gate passes at 26/26 fences with 19 batches, 775 commands,
371 fills, 166 blits, and 42 glyph runs. NDK, graphics, and display contract
tests and their ASan/UBSan variants pass on Beast. The exact ROM/storage pair
also boots the physical Arty through MC68030/PMMU POST, filesystem recovery,
service loading, and initial-image stage 8; QEMU, the display helper, and the
input hot-plug worker remain resident. Normal non-profiled runtime is active.

## Shared Kit namespace candidate (2026-08-15)

The shared-library resolution contract is locked without choosing an unmeasured
MC68030 relocation format. Applications resolve an exact immutable identity
`(name, ABI major, version, architecture, digest)` from their bundle's `libs/`
first and `LIBS:` second. The system namespace owns `LIBS:` read/write;
ordinary applications receive it read-only, while an installer must be granted
`LIBS:rw` explicitly. Updates install beside old
versions; launch never substitutes a newer file, running mappings remain live,
and cache/collection are keyed by the complete identity and reference count.

The supervisor now binds `LIBS:` to `/libs`, both startup profiles grant it,
prepared images create it, and the shell passes it to launched programs. Adding
the namespace exposed duplicate grants for one VFS service. The shared kernel
launch path now installs an identical source handle and rights once while
preserving each namespace name/root in the child's startup table. All 30 kernel
host suites pass, including the duplicate-authority regression, and the
protected GUI display gate passes at 26/26 fences (`13501/250000` present cycles
and `13097/250000` pointer cycles). Candidate ROM SHA-256 is
`1a155c4b88978b888084f7b6d2e99d90a59d254de0df07347c26123d8655bb02`;
candidate display image SHA-256 is
`751c2e6bb3a735704270a4af3347da5827c7098ece229b30c7f3ca1b20d6adcf`.
Neither candidate is installed on the Arty; the active pair above is unchanged.

## Active shared-library terminal release (2026-08-16)

The Arty now boots the Graphics Kit terminal through versioned
`graphics.library` and `font.library`. The active ROM is
`5cf1994e3eec260a29a9003542699d559baa28d22a8f681f2af4f5e1f4cb595a`;
the cursor-motion update changed only the filesystem image and did not change
the qualified FPGA bitstream or ROM. Terminal SHA-256 is
`0989b74dbc3408d16bc5b2dc9a54da5fa6bdf34dcf89f8fa6997963cc0bf79e0`.

Terminal now subscribes only to lifecycle, keyboard, and text window events.
It no longer receives an unused continuous pointer-motion stream. That cleanup
was necessary but did not fix the physical symptom: the display server placed
input ahead of every window control handle, while the kernel's documented
`wait_multiple` contract selects the lowest ready index. Continuous input could
therefore starve every pending application present. The server now rotates the
wait order after every serviced source, giving GUI opens, input, and every
window control port bounded service without favoring one class.

The display arbitration contract and its ASan/UBSan variant pass on Beast. The
QEMU interaction gate sustains pointer motion across two blink periods and
passes at 1,036 fences, 21 render batches, 796 commands, 376 fills, 182 blits,
and 42 glyph runs. Active display-service SHA-256 is
`ca161ea26816aa8b909be5af7595fa98db4ae7838cd45c315a3f8a08058fb02d`;
the exact binary and Terminal hash above were read back from the live Arty
image after restart. The staged image was
`8038f5e556c07215499a7d811c08beff6ad9906af0c8bfc2333face33632a63d`;
the expected mounted-volume journal changes the live image hash after boot.
The Arty passes POST, filesystem recovery, service loading, and initial-image
stage 8 with QEMU and the input hot-plug worker resident. The exact prior live
image is retained at
`/data/astra/deploy/display-fair-ca161ea26816/rollback/storage-terminal.img`
with SHA-256
`5db4c9d94616a440910d5f962ff2ecefb4e59e6d471bb8dea805a91bca156f8e`.

## Filesystem Kit candidate (2026-08-16)

`filesystem.library` ABI 1.0 is now the shared, assign-aware client layer over
the existing VFS and storage service. It exposes high-level open/close,
sequential and explicit-offset I/O, 64-bit seek, file information, stat,
mkdir/unlink, path qualification, and bounded union-directory enumeration. Its
low-level export table exposes the existing VFS client, assign-resolution, and
union-open primitives; it does not create another filesystem implementation or
another service session. The process attachment accepts the existing bulk-read
entry point explicitly, avoiding invalid cross-library function-identity tests
and preserving the 16 KiB read path.

The image installer places it at
`LIBS:Filesystem.kit/libraries/filesystem.library/abi-1/1.0.0/m68k-68030/filesystem.library`
beside the Graphics Kit bundle. The MC68030 shared object is 6,768 bytes text,
296 bytes
data, no BSS, and SHA-256
`669b3f9d10d843f5912003becfedf481da78dd0889e02e4ea402a9d76c78c5f8`.
Metadata reports `filesystem.library 1.0.0 ABI 1.0`; all 35 dynamic relocations
are `R_68K_RELATIVE`, with no PLT or unresolved dynamic imports.

Terminal, the events command and service, and `which` now open one process
filesystem context and route filesystem operations through the library. The
loader stages library images in a temporary mapped area instead of reserving a
256 KiB buffer in every consumer; `which` is 14,920 bytes total. Direct client
calls remain only in the supervisor bootstrap that must load the library and
in VFS service/library implementation code.

The complete VFS functional, ASan/UBSan, GCC analyzer, runtime loader, and
m68k build gates pass on Beast. A protected GUI QEMU boot with all three Kit
libraries passes at 1,030 fences, 21 batches, 796 commands, 376 fills, 182
blits, 42 glyph commands, 13,533 present cycles, 13,348 pointer cycles, and
0.360 s boot. No FPGA source, synthesis, flash artifact, or active Arty image
changed; candidate ROM SHA-256 is
`de2a038b1a1cebe91990544ae8be8dbc51234848454d4c5e6279ea61c602496e`.

The native-to-POSIX mapping is fixed in `FILESYSTEM_KIT.md`: the future POSIX
layer owns descriptors, cwd/root presentation, `errno`, and libc behavior over
this ABI. The public ABI exposes protocol statuses and generic file/directory
objects, never lwext4 state or an on-disk format, so RAM drives and future VFS
services require no application ABI change.

## Active desktop bundle release (2026-08-16)

The Arty now boots to a clear desktop containing the installed Terminal bundle
icon. A left-button double-click launches `APPS:Terminal.app`; Terminal obtains
its filesystem, graphics, and font services through the installed Kits and
transfers the bundle's 16x16 `.aicon` strike with its window-open request. The
display server owns titlebar composition and draws that icon to the left of the
Terminal title. Startup no longer creates Terminal directly.

The active ROM SHA-256 is
`edcf1388eccf1690cccb7978671c71ff2c778c5004e999b3a46fde9e2131ab49`.
The staged storage image SHA-256 was
`66900ca7910f4081bb914fd901db8184aa34d8e4b2f09812cef673c466b56047`;
the mounted journal changes the live image hash after boot. The active ARM
display bridge SHA-256 is
`a54e74d5f304bd33794cb00defbb84ce680537ab0b0ac0bd32b37c347c69b63b`.
The active ARM QEMU SHA-256 is
`63750eb5012e5f59d0199afdee2a2633d13983f2c6b6291fb71156a634415467`.
The exact prior ROM, image, display bridge, and QEMU remain under
`/data/astra/deploy/desktop-edcf1388eccf/rollback`. No FPGA source, synthesis,
or flash artifact changed.

The physical release exposed a reset boundary absent from host QEMU: the FPGA
correctly protects the active scanout from render writes, but after a QMP guest
reset the first desktop frame could select the scanout left active by the prior
guest. The ARM bridge now detects a restarted display request-ID sequence and
presents the opposite buffer before submitting that first render batch. Error
logs identify the failed command, status, and hardware fault detail. The bridge
self-test covers request-sequence restart detection. QEMU polls the external
display completion mailbox on its virtual-realtime clock so a completed host
request is collected even when the emulated CPU sleeps awaiting that interrupt.

On the live Arty, desktop idle is 2/2 completed display submissions with one
57-command hardware batch. An injected icon double-click advances rendering to
three batches and 172 commands while increasing fills, blits, and glyph runs,
which proves bundle launch and Terminal composition. A subsequent QMP reset
passes POST, filesystem recovery, and initial-image stage 8 and leaves the
desktop clear with QEMU, the display bridge, and input hot-plug worker resident.
The complete host interaction gate also passes at 1,026 fences, 22 batches,
1,101 commands, 657 fills, 205 blits, and 43 glyph runs.

## Interface, Events, and Messaging Kits (2026-08-16)

The shared-library surface now includes `interface.library` ABI 1.1 for alerts
and complete window lifecycle management, `input.library` ABI 1.0 for pointer
observers, `events.library` ABI 1.0 for diagnostic emission/log/trace/catalog
access, and `messaging.library` ABI 1.0 for bounded ports, timed messaging, and
capability transfer. These reuse the existing NDK and runtime implementations;
they do not add parallel window, input, event, or IPC mechanisms. Pub/sub is
reserved for a future broker service rather than faked inside the client Kit.

`Interface.kit`, `Events.kit`, and `Messaging.kit` pass their metadata,
relocation, host-unit, and bundle checks on Beast. The full userspace target and
NDK checks pass, including ASan/UBSan; VFS functional and sanitizer checks also
remain green. Their MC68030 library file sizes are 82,428 bytes
(`interface.library`), 10,508 bytes (`input.library`), 26,248 bytes
(`events.library`), and 10,312 bytes (`messaging.library`). A generated Astra
volume contains all three bundles under `LIBS:` with those exact payload sizes.
No FPGA source, synthesis, flash artifact, or active Arty image changed.

## Active crash-report and CPU-benchmark release (2026-08-17)

The Arty now boots with a stable hosted-MC68030 throughput measurement rather
than displaying the 12.5 MHz timer/device contract as CPU performance. Firmware
warms the production kernel decompressor, performs five more verified decodes,
and reports the fastest uncontended sample together with its calibrated 68030
equivalent. Four physical-board control samples were 39,565--39,694 us; the
active cold boot measured 39,677 us and reports 29.941 MHz equivalent. The
kernel displays that value separately from the unchanged 12.5 MHz timer clock.
QEMU does not model literal MC68030 cycle timing, so this is explicitly a
repeatable workload-equivalent result. It replaces the whole-POST estimate,
which varied from 51 to 136 MHz as cold TCG translation, display/DMA waits, and
Linux startup scheduling moved through the timing window.

A kernel panic now publishes the existing 90x30 text plane to the ARM display
bridge before QEMU exits. The bridge rasterizes it into the hardware scanout,
hides the pointer, disables the GUI overlay, and leaves the fault screen
visible. The launcher captures the complete serial panic report at
`/data/astra/log/panic-latest.log`. A controlled panic-selftest on the physical
Arty recorded the deliberate reason, build identity, supervisor SR/PC, worker
and IRQ state, six trace records, and `SYSTEM HALTED`; the persisted report has
SHA-256 `5fc7aa465b4838488ceac032021eaf57ceaf8356a3d3f3a0c8f82fe4c37bde3b`.
The normal ROM was then restored and passed POST, filesystem recovery, initial
image stage 8, and the Terminal concurrent-launch/close/relaunch lifecycle.

The active ROM is
`b93c2564b537f2b7a74e9ec1d5741210e8b8f67120484ee5ba210acf0b72db43`.
The staged storage image is
`075b1916e2bbf6137613e0451583f8381b49fdb35cb532f5b076808f7024c342`;
its live hash changes after journal recovery. The matching stripped ARM QEMU is
`3b922c4a61248b2ba3c6082f1da378c1fdd111aae9d9ccc58d97311ebf047981`,
the display bridge is
`04db9b9a49d4cd2b46b45d34ad8211cdb356d6f4347e983b9092f4fde2622471`,
and the launcher is
`09b0e24a76a694332d3a488ff2dd44ce4ce13315535189285bef56c9814ff4b6`.
The exact prior stack and all test candidates remain under
`/data/astra/deploy/cpu-panic-v1`. No FPGA source, synthesis, or flash artifact
changed.

## Active process-isolation hardening release (2026-08-17)

The launch-pressure crash was a shared-area lifetime defect, not an
uncontainable kernel fault. When an area's creator exited, the kernel revoked
every mapping even if another process still held a transferred area
capability. Display consequently dereferenced a surface mapping the kernel had
removed, its resident-process death made Supervisor exit, and only then did
the kernel correctly halt because its initial image was gone. Area lifetime is
now reference-controlled: creator death revokes only that process's mappings;
the area remains live until the final transferred handle or child reference is
released. The area and ring regression suites cover survival and final
reclamation.

Supervisor now logs and removes a dead resident process from its wait set
instead of exiting itself, so a service failure leaves init alive. The final
panic framebuffer is also rebuilt from a bounded summary after the verbose
serial report, keeping the reason, build/hardware identity, fault coordinates,
six trace records, and retained-log path visible within 90x30 characters.

All kernel and Supervisor host tests pass on Beast, and the full MC68030 ROM
build passes. On the physical Arty, twelve repeated Terminal launch-pressure
cycles completed with matched display submissions/completions and no user
fault, Display death, Supervisor exit, or kernel panic. A controlled panic ROM
produced the readable 15-line framebuffer summary and retained the complete
report with SHA-256
`cd786de82f75a974a2509850fecd3c22234acf09ff48f2461c013637a8dce5ca`.
The normal hardened ROM was restored and passed POST, filesystem recovery, and
initial-image stage 8 at 29.571 MHz equivalent. Its active SHA-256 is
`614a0ca4b77dcc7221882fbcda0b930f2fb418520ff8239e71369429e3626d76`;
the rollback ROM is under
`/data/astra/deploy/kernel-hardening-614a0ca4b77d/rollback`. No FPGA source,
synthesis, or flash artifact changed.

## HDMI startup and warm-reload repair (2026-08-21)

The horizontal screen shift is pinned to a source-startup contract defect: the
HDMI-audio image entered HDMI mode without first processing HPD and EDID, while
HDMI 1.3a requires reset/new-sink operation to begin DVI-compatible and permits
HDMI packets only after the sink is identified as HDMI. Cold power cycling
cleared the receiver state, explaining why the same image recovered. The
candidate now remains correctly aligned through cold boot and cable hot-plug;
physical confirmation of the final corrected warm sequence remains open.

The retained candidate uses the Arty Z7-20's documented R19 HPDN and M17/M18
DDC pins, Zynq PS7 I2C0 through EMIO at 100 kHz, and active-low PS GPIO events.
RTL resets to DVI and changes HDMI mode only at vertical blank; 48 kHz 24-bit
stereo audio remains present. A synthesis preflight and RTL test now fail if
this startup contract, pin wiring, or warm-reset behavior is removed.

All graphics simulations, the exact 1,280-pixel offset regression, audio RTL,
EDID parser, ARM static analysis/build, and device-tree validation pass. The
first full 200 MHz over-target route connected all nets but was rejected at
`-0.185/+0.033 ns`; no bitstream was written and the board was not changed.
The exact production build now routes all 66,591 nets and passes the real
187.5 MHz release clock at `+0.055/+0.034/+0.538 ns` setup/hold/pulse-width.
Its optional 200 MHz implementation margin misses setup by `0.278 ns`; that
margin failure is recorded and is not shipped as the PS clock. The exact XSA
has produced a fresh FSBL, 100 kHz I2C/187.5 MHz device tree, byte-verified FIT,
BOOT.BIN, and strictly built/tested ARM HDMI manager. BOOT.BIN is
`67d03fb4f1205236bad2bf870083d289fee135f428831bcaf21a1b0553dcb3fc`
and FIT is
`3fee66d1d33080b238509169ce459bbcae853d895a192b20de89d4f25df85dea`.
Cold boot now passes HPD/E-EDID and reports HDMI status `3` (requested and
active). A controlled full FPGA-manager reload proved that reconfiguration
preserves the healthy Cadence I2C state (`CR=0x310e`, timeout `0xff`). The
subsequent `astra-chip-reset` helper was the warm-reload failure: it re-locked
the Zynq SLCR after pulsing FCLK1 reset. AMD UG585 says that lock blocks writes
to every SLCR register, while the exact Xilinx Linux kernel unlocks SLCR during
early boot and leaves it unlocked for its clock framework. Consequently Linux
reported I2C0 active but could not set `APER_CLK_CTRL[18]`; the controller read
all zeroes and E-EDID timed out.

The retained helper still performs the documented unlock and fabric-reset
pulse but no longer takes Linux's global SLCR ownership away. On the board,
the unchanged HDMI manager then resumed I2C0, read E-EDID, and restored HDMI
status `3`; the audio certifier passed 48,064 frames at 48 kHz with a 440 Hz
tone. The installed helper SHA-256 is
`6ad793945c05d5c49c720e5d185f9ca1f612c78e82c07c7fe667e9c4de258395`.
The existing mocked reset test now requires exactly unlock, reset assert, and
reset release, and the pre-synthesis HDMI source contract runs it before
Vivado. The complete Beast graphics suite, production-width 1,280-pixel
screen-offset gate, 1,280x644 overlap blit, Linux host tests, and ARM static
analysis pass. Final release blockers are visible warm-screen confirmation,
physical HDMI hot-plug after the fix, and audible-tone confirmation.

Those physical gates now pass. The operator confirmed correct alignment after
warm recovery and cold power cycles, HDMI unplug/replug remained aligned, and
the 440 Hz tone was audible with the playback path enabled.

A later narrow dark column at the left edge was not an Astra defect. The same
column followed the Cam Link capture device when its HDMI input was moved to an
unrelated C64 Ultimate. Resetting only the Cam Link USB connection cleared it.
After that reset, the current Astra image stayed clean in DVI mode, after a
manual switch to true HDMI with link status `3`, after the production manager's
E-EDID sequence, through an Arty cold power cycle, and through HDMI
unplug/replug. A speculative five-pixel source-coordinate compensation was
rejected and removed; its dedicated test was also removed because it encoded a
false diagnosis. The existing production-width screen-offset and full overlap
blit regressions remain the correct Astra gates and both pass on Beast. HDMI
hot-plug, DVI-safe startup, and audio remain unchanged.

The candidate package is installed atomically on the Arty SD card through NUC.
The prior qualified BOOT/FIT hashes remain in their rollback files, `/` is
read-only, and the exact production PL is active. The live HDMI manager SHA-256
is `f4c4ab81b9a90e95748bc0896ddcbeb14e81bbc450ecbec5c64de2b853b8a6f3`.
