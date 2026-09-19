# Astra 68 current engineering state

Status: active continuation map, 2026-09-19

This file contains current facts only. Git history holds superseded board,
processor, benchmark, and milestone records. The platform is **Astra 68**, its
kernel is **Axiom**, and the user-facing system is **Astra OS**.

## Active machine

- The DE25-Nano attached to Beast is the only production target.
- Its HPS serial console is the stable by-id device
  `usb-TERASIC_DE25-Nano_TRWJGOUZ-if02-port0` (currently `/dev/ttyUSB0` on
  Beast). The adjacent `if03` port is not the console; scripts must use the
  by-id path rather than a `ttyUSB` number.
- The sole CPU is a big-endian MC68040 provided by the Astra QEMU 9.2.4 TCG
  backend on the DE25's AArch64 HPS. There is no FPGA CPU and no older-CPU
  compatibility path.
- CPU0/1 are Cortex-A55; CPU2/3 are Cortex-A76. Physical interrupt accounting
  places SD and Ethernet receive IRQs on CPU0 and neither on CPU1. The release
  launcher therefore pins input/log helpers to CPU0, the hardware display
  renderer to CPU1, the sole TCG vCPU to CPU2, and QEMU main/AIO/filesystem
  workers to CPU3. A live host-channel run proved that six workers created
  after launch inherited CPU3 while the vCPU remained on CPU2. The MC68040
  benchmark reaches about 72 MHz effective on CPU2; the rejected A55 placement
  reached about 36 MHz.
- Astra owns 512 MiB guest RAM preallocated from HPS LPDDR4A. Media RAM owns
  512 MiB of the separate LPDDR4B device at `0x40000000..0x5fffffff` for
  graphics and sound. Linux exposes 934 MiB of LPDDR4A as normal system RAM.
- The ROM aperture is 512 KiB at `0xffe00000`.

The production Agilex 5 shell routes with every clock constrained. Retained
setup, hold, recovery, removal, and minimum-pulse slack are +0.076 ns, 0.000 ns,
+2.518 ns, +0.006 ns, and +0.220 ns. Resource use is 45,080 / 46,800 ALMs,
4,189,680 / 7,331,840 block-memory bits, 342 / 358 RAM blocks, 60 / 376 DSPs,
and 5 / 11 PLLs. Exact build and deployment evidence belongs in
`fpga/de25/TIMING_CLOSURE.md`.

The active display checkpoint has a fixed 1920x1080x60 HDMI output at 148.500
MHz and a 165 MHz graphics-build domain. The FPGA performs nearest-neighbor
logical scaling, integer fit/letterboxing when possible, fractional fit/fill
otherwise, composed-line replay, and logical copper-beam translation. The
framebuffer, tiles, sprites, copper, and boot overlay scale together; the
double-buffered 32x32 ARGB hardware pointer is the native unscaled output
plane. The MC68040 supplies logical content and mode requests but never scales
pixels.

The framebuffer scanline manager uses the LPDDR4B controller's native 128-bit
port and writes two consecutive pixels per build clock into parity-banked line
stores. The production-shaped three-span row completes in 1,164 of the 2,444
available clocks; the prior narrowed, one-pixel path needed 2,234 clocks and
failed physically under Terminal. Replay requires explicit source-row
availability and cannot reinterpret a missed row as vertical scaling. The
installed shell `99c03a1487863bfe84b4f793a75bde2b8b6bfd3c1f3c3fd1e73023bec5204b62`
passed cold boot plus 1,382 captured active-Terminal frames with no black-band,
striped-line, or unbounded-replay event.

Media RAM is a service-owned arena, not raw process memory. Applications send
validated draw lists through GUI IPC; only the display service holds the
exclusive display lease and chooses media offsets. Scanouts use two 4 MiB
slots, and the render batch and scratch workspace occupy 8..16 MiB. Persistent
window content and chrome caches receive exact, 64-byte-aligned extents from
the remaining 16..512 MiB arena. The display service's page-backed window
metadata grows with demand; removing or resizing a window makes its old Media
RAM extents available for reuse. Admission is therefore governed by the real
Media RAM, kernel handle, port, wait-set, and render resources rather than a
display-specific window count or fixed per-window slot. Render records and
uploaded data grow upward while temporary surfaces grow downward through the
complete 8 MiB workspace, so they cannot overlap.

Visible process IDs are unsigned 16-bit values in the range 1..65535. PID 1 is
the initial supervisor. The allocator wraps, skips every live PID, and keeps
the generation-plus-slot owner token private, so PID reuse does not retarget
handles or an already-open `PROC:` node. Physical `ps` output on the current
release shows the supervisor as PID 1 and subsequent services and applications
as compact sequential IDs.

The owner ledger is sized from the process, library, and area capacities rather
than a 64-owner constant. A measured fifth-Terminal failure had 115,809 free
pages but all 64 old owner slots occupied; the derived 304-entry ledger removes
that false out-of-memory condition. A second measured ceiling was the port
pool's reservation of every port's advertised queue capacity. Five owners could
configure more than the 256-message physical pool even though peak live use was
only 13 messages. Port configuration is now limited per owner, while the fixed
global message pool is charged only when messages are actually queued.

The five-window physical gate also exposed an independent display-service
underflow. When an active client's event queue was temporarily backpressured,
wait source 1 represented the client's writable notification, but the dispatch
loop subtracted the three non-window sources and treated it as window index
`UINT32_MAX-1`. Display PID 10 faulted at `receive_command`, PC `0x00104ec8`,
address `0x3ffffe9e`. The dispatcher now validates the source before deriving a
window index; a blocked-input wake simply causes input readiness to be
re-evaluated. The regression failed before this fix and passes the normal,
ASan/UBSan, analyzer, and MC68040 display builds.

Immutable software release
`2d2932784cd0f4468a6cd8669228e66e6345a73d5ea8dc1086967344c2427aaf`
is selected, byte-verified, and running on the DE25. Its QEMU, ROM, storage
seed, host display, remote-desktop, and source-manifest SHA-256 values are
respectively
`3a13dc695833a277f3048de3835cfdedfcacc46adb9e31e8937942d4d410605e`,
`b60711fa19eda7da0d1d415b0dfd2e8e0f81f92427dc4be5601decb41b1cd5a4`,
`aba271bbf82afd0b20f09cb54017d75b2f8f0629e2a0c4fc9cc16388da314ac4`,
`9d86a327a113e2f5dd2ead97f49bab87b1aacf16f5037fa9c2991a7bc059d471`,
`6740ea01dcb16e19cb3af89d8efd5ee855d1d72cb45090aa1fc8094232e6df66`,
and `6e498c4ed3be3a06254a11c3ac86a208fdaffe9723b63c72ce51a93994cced97`.
It reaches stage 8; `astra.service` and
`astra-remote-desktop.service` are active with zero automatic restarts.

This release completed the library, CLI, storage, and remote-desktop audit.
The QEMU gate passes all 71 terminal commands. The `open` command returns 0
after confirmed startup without waiting for application exit, returns exactly
1 when startup fails, and `--wait` returns the application's exit status.
Positive and negative NDK, command, supervisor, QEMU, and physical-DE25 tests
cover those contracts. On the board, a missing application returned 1 while
`InterfaceGallery.app` returned 0 and remained alive. Launch origin is carried
in the canonical startup record and is available through
`astra_startup_launch_source()`; applications require no launch-origin switch.
The QEMU terminal gate rebuilds its workspace ROM before every run and has a
negative regression proving that it refuses to test when that refresh fails,
so a stale boot image cannot masquerade as current source.
The physical loopback RFB gate captured a 1920x1080 RGB frame, round-tripped
pointer position `(700, 500)`, and produced frame SHA-256
`59f1aaecaa1e0d1e0a2f0223f088095ce502c995ace9648312604a667ee23638`.
This release advances the GUI protocol to version 11. Every ordinary window
receives one complete state snapshot for active, inactive, minimized,
maximized, restored, and geometry transitions, plus a distinct resize event
only when its client extent changes. Terminal consumes that state directly:
its cursor blinks only while active, remains visible while inactive, and its
underline is aligned to the font box rather than the inter-line gap. The
desktop uses the 13-pixel title face for a measured, centered application label
and the Terminal bundle now generates the toolbar-free icon from the approved
desktop design. Physical captures 600 ms apart were byte-identical while the
Terminal was inactive
(`ca9d8919a34be268bbae5ec8fad5bd77f9e22565a35d48db7565725108f83290`)
and alternated between
`cd4e8278a46c1837fe27e8aba294d477131e4ef1ae321238af7c3b52044b1443`
and `6a71a7e7b7597acda363adf74b652d2dd772f6c362a64920b11b79e6de8fbffd`
after activating it. Maximizing Interface Gallery produced a full-screen,
correctly reflowed frame
(`eb93ccf209bf77a959a6983e4c59a1c23afe6201cf55e5a1e5e2fa2d480cd582`),
exercising both its state and resize notifications on the DE25.
The complete remote-desktop lifecycle also passes dynamic definition
create/enable/disable/delete, automatic and manual activation, restart,
broker loss/recovery, events, and shutdown. The storage allocator derives its
production arena from cache classes plus measured transient filesystem demand.
lwext4 rename atomically replaces compatible destinations, preserves both
operands on every refusal, rejects cross-mount moves and directory-descendant
cycles, and passes raw, partitioned, full-volume, power-cut, journal-failure,
ASan/UBSan, TSan, analyzer, and independent `e2fsck` gates. Kernel, NDK, tools,
every userspace suite, shared-library contracts, analyzers, and sanitizers pass
on Beast.

The library-loading audit is cleared in immutable DE25 release
`354a01b26f2458642761ad24658841b4525c8d31cb102cb965702e8ed14613e5`.
The screenshot showing `filesystem.library.2` and `config.library` load
failures was the board still executing stale release `62db2ee8...`, not the
fresh Beast/QEMU candidate. The production selector and live QEMU process now
both identify release `354a01b2...`; `astra.service` is active with zero
restarts, every boot service including `ntpd` reports status 0, and the initial
image reaches stage 8. A physical Terminal run reports `POSIX RAW PASS: /CWD`,
and bare `which status` resolves `/commands/status`. The release ROM, writable
storage seed, and QEMU executable SHA-256 values are respectively
`ad733d4d830c179f479971391b9ba42ace15718e0cf6aef35ccd2ddf8255c6c2`,
`8e38f96b5732d3305e522d49ccccd98b47fc8401f069e15b6c76a70e3f44bdc7`, and
`3553fca8f5d13f0cb6bb931ef5c301928cdbbf65a9710e2aa76bbce1e25f3e35`.
The launcher and physical-terminal probe now use the production
`/var/lib/astra` and `/run/astra/qmp.sock` defaults; positive assertions and
negative stale-path regressions cover both defaults. The probe also submits
each key's press and release atomically and rejects the interactive `posix`
command before opening QMP, preventing duplicate characters and repeated raw
mode failures from contaminating a live Terminal.

The release gate previously advanced the verified `current` selector without
restarting `astra.service`; the board therefore kept executing the preceding
release even though publication reported success. The deployer now restarts
the service and refuses success until `/proc` identifies the selected
release's exact QEMU executable and its QMP socket is live. Only then may it
prune old releases. Application bundle packaging likewise always asks each
producer makefile for its current binary, so an existing bundle cannot hide a
newer source or service image.

Immutable release
`6f59f47361d5e3d920b0798268dd77a6ed4f19a9c2c617d14c5935107ba9ea91`
is selected and byte-verified on the DE25. Its QEMU, ROM, storage, host display
helper, and launcher SHA-256 values are respectively
`dae7d602590fac78c50b4e2abead269c1b2ffdea135b3b96c896bcaa47961342`,
`3257fbefb898e4a992f6e8f9de73a61379a6789006959770fb734ef007c0e61d`,
`1e16134170631aea7e0b2772ee82163cf4b78b19d74a73f446f404de2f738c9e`,
`ca281d6d6d2d37fb566c02a415f1d54dc7e04192f2adaf492a4ee4ab817edf65`,
and `1e76f48511636d1cc9c479c3f7ae90412322691b9af6c3b33a026d454b7b6092`.
It reached stage 8 at 69.037 MHz and remained active with zero restarts. The
physical process table contained five Terminal processes at PIDs 12, 14, 16,
18, and 20 and five zsh processes at PIDs 13, 15, 17, 19, and 21. Port counters
reported configured capacity 261, peak occupancy 15, and zero allocation,
quota, discard, or handle-discard failures. The retained ring is
`/tmp/astra-five-after-fix.bin`, SHA-256
`209220945685a18641db988b7fbb735917de8df5762a38f0f47b3064872ae79a`.
The Cam Link proof is `/private/tmp/astra-five-terminals-fixed.png`, SHA-256
`f7b3d2538f77564678a3fdc8014cbe4724e321ee6e7187a45bbcfe7e3cad5174`.

The exact routed shell passed cold boot, JTAG design identity, native splash
readback/presentation, the complete renderer suite, live 320x200-to-1080p 5x
integer presentation, the complete 64-sprite variable-geometry sweep, copper,
48 kHz HDMI audio, and POST status updates. The physical sprite run reported
zero AXI and deadline errors, and Linux remained healthy. The prior fatal HPS
SError was eliminated at its root: recoverable scene validation failures now
increment the existing commit-error counter and return AXI `OKAY`, while true
transport faults retain AXI error responses. The normal immutable runtime is
restored and reaches stage 8 at 70.192 MHz effective with the correct wall
clock. Requested clock values, generated PLL clocks, Platform Designer
metadata, RTL timing constants, TimeQuest constraints, and physical behavior
must agree; none may substitute for another as release evidence.

Immutable release
`00cb50981461fabb6b10ddc6a99b6b38a9cdc1b94df400f5cd4117aa635e2095`
corrects the POST/panic console's stale 14-pixel character spacing. The host
renderer now consumes the NDK's 8-pixel Astra Mono cell advance and has
SHA-256
`8b03e3b59136d8cd389f39b0888aaf07490d6352a4295e321afebcaa228673aa`;
its ROM and storage inputs are byte-identical to the preceding accepted
release. A physical Cam Link specimen measured repeated `I` glyph starts at
x=616, 624, 632, and 640 and repeated `W` glyph starts at x=655, 663, 671,
and 679: exact 8-pixel advances. The capture is
`/private/tmp/astra-post-spacing-v1-proof.png`, SHA-256
`b4862be2f89ae28da4c7831708c35af0ee193a39f24d22c1c7501323fb82bfc2`.
The restored runtime reached stage 8 at 70.275 MHz effective; the board
service is active with zero restarts, and the live renderer hash matches the
release. The DE25 production publisher now owns all software inputs: it cleans
its producers, builds from the current mirrored source, records that source
manifest in the immutable release, and rejects any source mutation during the
build. It does not clean or rebuild the independently qualified FPGA route.
Caller-supplied binaries are confined to the lower-level diagnostic creator
and cannot enter the production publication path.

Retained interface-performance release
`1c12a8d59120f00181cdda7171330922febd623590bd24adb7f61db25505f0e1`
established the current flat-layout baseline. Its storage, ROM, and QEMU
SHA-256 values are respectively
`01c7a3ce8a99ccec41687eb401d780852f77047564382d6fd2ba1878bbffc3f4`,
`750be675b149b2355d6ddd1d5aa22264e63ee420ad44694b00ea307d8e64e2a6`,
and `4c5606a57402fff5c404da0d0033eed975ed3f6454b2244f9fe32de12ea3df5e`.
Its interface library and gallery executable hashes are
`c51cdb243fad5f2326c7c7da1296b22165b176320669ec7503862930b5af7405`
and `095bfd474644bfe6efc9b1c2fced252b995e2963600e683163a6d7a44ffbdeee`.
It launches Interface Gallery against `interface.library` ABI 1.4 with Label,
Button, Checkbox, Radio, Switch, Slider, and Progress controls. A physical Cam
Link capture shows the shared five-segment checkbox mark and outlined off-state
switch thumb. A second capture taken while the slider button was still held
shows its label changed from `62%` to `93%`; the update is not deferred until
release. The retained Beast captures are
`/tmp/astra-switch-off-clear-1c12.png` and
`/tmp/astra-slider-held-1c12.png`, with SHA-256 values
`c8e08a974787014d9f1a9683ecff0b93f2df5ff7216ac30ed7b39a1d848d62aa`
and `a5a84e3fa070f6f54940f73ac05c407e5f44abca8cb9fec1e86b0a9a4c80cdf3`.

The retained integer flex engine measured 7.126, 6.962, and
7.295 microseconds per control: 85.511 microseconds/reflow for 12 controls,
445.550 for 64, and 1.867 milliseconds for 256. The 10-microsecond/control
automated gate retains at least 27% headroom over this run. The retained Beast
trace is `/tmp/astra-interface-ring-6a512f28.bin`, SHA-256
`a6bdb60a1bf3f633823883d8705d64d478f974c1e216e4ea673f7e6458951813`.

Immutable release
`06f28a5787a70c38d032b04a66242349b0c4f975a99613670715e3bf9c544c61`
is the byte-verified runtime selected by `/var/lib/astra/current`. Its display
service, Interface Gallery, and storage image SHA-256 values are respectively
`5a4b6b04c71f4b9620d932dd28d481d1e543d8677132fa16f4cdb2ecb9c3aec9`,
`baab9ded2e39b4410e87cb3267386064c4e0b76f295131dcfe2cdba726467a0f`,
and `d13a6322d7bf3c988df5044f1b9b1df9c4e2a0d99c427b0f834a7b3b5e201d2e`.
The physical DE25 service is active with zero restarts. A Cam Link capture
shows Interface Gallery alive and completely rendered after launch, while the
kernel, supervisor, resident services, and desktop remain alive. The failure
that formerly killed the system was not a kernel fault: the manifest marked an
ordinary application `required`, its window-open render failed, and the
supervisor exited. Manifest parsing now rejects `required` on applications.

The shared render builder now treats a fully clipped primitive as a successful
no-op, while an exhausted command ring remains a distinct failure. This fixed
the Gallery window-open failure at its common rendering boundary and retains
strict errors for invalid destinations, descriptors, surfaces, and storage.
Focused graphics and display tests pass normally, under ASan/UBSan, and through
the MC68040 cross-build. The physical capture is retained locally as
`/private/tmp/astra-interface-fix-v6-resume.png`.

The builder no longer exposes a glyph-count or fixed descriptor-table ceiling.
Descriptors, glyph records, and source data share the workspace allocator;
text longer than Astraea's 4,096-glyph per-command encoding is split into
consecutive commands with its 26.6 pen position preserved. Callers may provide
any batch storage size from the protocol minimum through the physical 8 MiB
workspace, and allocation fails only when that supplied storage is exhausted.

Immutable DE25 release
`321a882c34ac27f7bf287bc3129eaf1c1fae59aa0384e9c5a8fa8aafde84ae60`
physically accepts Interface Kit 5.1 disclosure geometry and the nonblocking
input path. A collapsed disclosure points right and an expanded disclosure
points down; retained Cam Link frames are
`/private/tmp/astra-v52-disclosure-collapsed-clear.png` and
`/private/tmp/astra-v52-disclosure-expanded.png`, with SHA-256 values
`0631be6fb91731933d4c9dfede60c83c9c491535f953821b7eed4169bc90e362`
and `f5ab52090861331a89ea178af6970a6d5f2a2b8dd3ebdb92681afcc26cf83dc2`.
The input service no longer waits on a saturated client: its existing loss,
state-reset, and latest-motion recovery runs from writable client handles in
the normal wait set. The Gallery drains every already-queued event, preserves
ordered state transitions, accumulates damage, and presents once.

The deployed physical motion ceiling remains the active display blocker. A 60-sample,
one-second large-window drag produced 15 render batches during input and one
more after settling. The same splitter workload produced nine render batches
during input and one more after settling, versus three catch-up renders in the
preceding build. Event batching removes stale replay but cannot raise the
measured 15 fps window and 9 fps control presentation ceilings. The retained
window-scene architecture is now implemented, regression-tested, and routed in
the complete production shell. Clean deployment and physical measurement are
the active acceptance gate; further control or cursor micro-optimization is
not an acceptance path.
One profiled move frame completed 37 commands in 56.539 ms of hardware time:
one 951,808-pixel background fill, one 951,808-pixel desktop restore, one
915,200-pixel Gallery copy, and 34 negligible rounded-edge blits. Release
artifact SHA-256 values are `24b08c602529275991796c75b7826180f4dc350fc7f97ac89153c1ab92c1bd7d`
for storage, `abbc0720a0d9bd23c02260486e8f9f3db5838b59be55c4fd700e67d85655876d`
for the display helper, `f04adb18fe13b147f88e26e11a64070222c9bc9dc8b925868593a98ea6db2418`
for QEMU, and `174c4eee1cc20d28ca45ca3b4280b07dd5763e29079a0efd01d897faf10a5942`
for the ROM.

The NDK now owns the sole HID-to-key translation source used by Terminal and
the input service. Their prior independent implementations were consolidated
without changing terminal control-key or input text-event behavior. Terminal
and input builds also include dependency files only for their declared object
graphs, so a removed or renamed source can no longer remain build-authoritative
through a stale wildcard-imported `.d` file. Interface Kit packaging derives
its ABI and version payload paths from its manifest rather than repeating a
second version literal.

A physical southeast resize advanced renderer batches from 2 to 4 and
submissions/completions from 3/3 to 7/7. Both snapshots remained unchanged one
second later. The pre-fix build advanced by another 20 batches and 240 glyphs
in the same interval because every same-size frame notification redamaged the
UI; `interface.library` now reflows and damages only when the parent extent
actually changes, and a focused regression test retains that contract.

This release retains the corrected compositor layout. Across 463 captured
physical HDMI frames, the former corruption band at y=580..781 remained
constant while the cursor visibly blinked; after a window drag, another 300
frames had zero black-band frames. The pre-fix band was an exact software
overlap: the 3,855,360-byte desktop surface occupied a 2 MiB slot, so the
following Terminal slot overwrote precisely those rows.

Current source removes the MC68040's bounded visible-region
subtraction pass. The display service now emits every damaged window in
bottom-to-top order, clipped to the frame damage, and Astraea's ordered blits
resolve overlap. The former eight-region overflow fallback is gone. Display
tests prove the complete damaged stack is submitted, and pass normally,
under ASan/UBSan, GCC analysis, and in the MC68040 target build. The display
service now also uses page-backed dynamic window storage and exact Media RAM
allocations. Host tests grow the table beyond its first committed page,
compose five simultaneous windows, verify every live chrome/content extent is
disjoint, prove freed-range reuse, and reject allocation only after the
physical arena is exhausted. A validated batched hardware scene descriptor
remains the next compositor step.

Immutable release
`69c97c7381e7daffad96064ae41a1410b7d31bb01f1a469d3263bd6c5e355a7b`
advances Graphics, Font, and Interface Kits to
ABI 2.0. Interface controls now support caller-owned nested flex containers
with parent-before-child validation, reverse-order intrinsic measurement, and
iterative descendant reflow without heap allocation, recursion, or a child or
depth cap. Ancestor clipping governs hit testing, damage, and painting; draw
list ABI 1.1 carries the clip into Astraea's hardware command clip. Graphics
and Interface host, sanitizer, analyzer, and MC68040 gates pass. The physical
70.324 MHz MC68040 nested-layout measurements are 92.449 microseconds per
12-control reflow, 325.825 microseconds for 64 controls, and 1.480 milliseconds
for 256 controls: respectively 7.704, 5.091, and 5.783 microseconds per control,
all inside the 10-microsecond gate. Full-window frame damage now suppresses
per-control damage-union work that cannot enlarge the dirty region; compared
with the immediately preceding physical build this reduced the three workloads
by 28.4%, 35.2%, and 11.7% without changing layout or damage results.

The release's storage image, Graphics library, Interface library, Gallery, and
trace SHA-256 values are respectively
`472897d5732523e4c455e44d89729afb45b267ec1564781a8448761b7b317085`,
`739ed3a46abad7d81b33aae51d612b2112148d4a69a84d542459cbdd4b1d42dd`,
`7612e7460fd1d91d9e54e02760894c9e20adee2b29e40fb25a1a46a2c18a0b8c`,
`d72a1cb3eb63cda20554acb886440ef396139ad846263a4425e6adddaa8d239f`,
and `80957f5c117953b03db7b2dcb09635244e17ff3c1a7d3458427b4f2bb217061c`.
The trace is retained on Beast as
`/tmp/astra-interface-abi2-v3-ring.bin`. Cam Link captures show the Gallery
alive and Terminal launched above it after executing `echo terminal-ok`; their
local paths are `/private/tmp/astra-interface-abi2-v3-gallery.png` and
`/private/tmp/astra-interface-abi2-v3-terminal.png`, with SHA-256 values
`73014e5614e9eaf849d24f15e87fc872cbc21c520b9fb8ee1b86986c0b9ed61a`
and `4bc4930544dceedd546ba0c32f5da3f685bac6725beb2150ec9c1e53c99dff1b`.
The service remains active at stage 8 with zero restarts.

The launch failure in the preceding candidate was a stale
`graphics_shared_surface.o`: Graphics' explicit loadable-library recipes did
not generate header dependency files, so ABI-1 surface layout code was linked
into an ABI-2 library. Graphics and Interface library recipes now generate and
include dependency files, and their objects depend on their owning Makefiles
so the corrected recipes bootstrap themselves. An executable boundary test
retains both build-graph contracts.

Current source defines UTF-8 as Astra OS's sole text encoding and moves
validation, scalar encoding/decoding, and scalar-boundary traversal into the
NDK's single `astra/utf8.h` implementation. NDK calls, launch arguments and
environment values, config documents, VFS paths and returned names, display
IPC, window titles, and interface labels reject malformed complete spans;
terminal byte streams replace malformed input with U+FFFD. Editing-key
sentinels now live above U+10FFFF and cannot collide with Unicode characters.

AFNT 0.2 and `font.library` 2.1 now carry baseline-relative
ascent/descent/line-gap, cap-height, x-height, maximum advance, underline, and
strikeout metrics. The draw-list renderer synthesizes bold and italic from one
resident glyph source and positions underline/strikeout from those metrics;
glyph expansion remains hardware work. `interface.library` 2.2 adds the shared
TextSurface fixed-grid renderer, and Terminal now consumes it for styled runs,
logical colors, carets, and hardware-blit scrolling instead of owning a private
painter. The component now also performs grid hit testing and renders
normalized, half-open selections without storing selection state in its ABI
object; Terminal uses both for pointer-drag selection. Current source advances
this to `interface.library` 2.4: capacity-padded grid rows use an explicit
selection stride, typed multi-representation clipboard documents are immutable
shared areas owned by a protected clipboard service, and Terminal maps the
input service's normalized Meta-C/Meta-V to copy/paste without claiming
Ctrl-C. Immutable release
`07be64339b3bdb58eef906734ed3b857e701856d607f17e6804a4e26a6993ee8`
booted that service graph on the physical DE25, reached stage 8 at 70.092 MHz
effective, and remained active with zero service restarts. The end-to-end QEMU
gate then selected a known Terminal row through pointer input, copied it with
Meta-C, pasted it with Meta-V, executed it in zsh, and observed the exact
`CLIPBOARD-PASTE-PASS` result. Scrollback, find, wide cells, and code/flow
layout remain pending. The current
AFNT bitmap/Amiga importer remains operational; the
documented scalable TTF/OTF/WOFF pipeline and persistent synthetic-strike cache
are not yet implemented.

`interface.library` is now ABI 2.5 and supplies the shared per-document
undo/redo manager. It retains named, typed, serializable actions in a
caller-owned growable arena, applies groups transactionally with compensation,
tracks save-point dirty state, invalidates redo branches after a new edit, and
rejects callback reentrancy. Normal, sanitizer, analyzer, and MC68040 builds
pass. On the physical 69.874 MHz MC68040, 10,000 groups measured 7.546
microseconds per apply-and-record, 3.958 per undo, and 3.292 per redo; the
retained gate is 12.5 microseconds per phase.

The ABI 3.0 release retained the 2.6
allocation-free UTF-8 piece-table model and single-line Field control, and now
gives every animated control its own state and active-list membership. The
display service delivers a coalescing per-window vblank event; the window's UI
thread calls the shared Interface Kit once per pulse, and only active controls
are visited. Focused carets and indeterminate progress indicators therefore
need no application timer, polling loop, worker thread, or cross-thread UI
mutation. The retained text model owns scalar-boundary selection, an indexed
line map, exact capacity preflight, atomic replacement failure, arena
relocation and compaction, and full invariant validation. Field adds caret and
pointer/Shift selection, horizontal viewport tracking, UTF-8 editing,
read-only and fault states, semantic copy/cut/paste actions, and one shared
replacement boundary for typed, programmatic, and pasted text. Interface
Gallery and the NDK example consume the public API rather than private text
buffers. ABI 3.0 deliberately enlarges caller-owned `AstraControl` and
`AstraWindow` so controls directly own animation membership and phase and
windows directly own their coalescing vblank wait handle. The obsolete timer
tick and special vblank-window constructor were deleted rather than retained
as compatibility paths. GUI protocol version 8 returns the control and vblank
handles atomically, and every in-tree producer and consumer uses that single
contract.

Immutable physical DE25 release
`29b7d36f7687b4fdf577cc6e13a8fdf3064be2947905a190155edfef11d313ca`
boots Interface Kit ABI 3.0 and GUI protocol 8, reaches stage 8 at 69.865 MHz
effective, and remains active with zero service restarts. Its storage image,
ROM, Interface library, Gallery, and display service SHA-256 values are
respectively
`519a0817fec8a5dfdaea4ca9d8687d2aa4a22d3ec5a3ddce102628d896c4ece0`,
`2588923622c2a83adc586bf7629d95670307154bbb6bbe3713e39f1da40120e6`,
`a593564cfad3d962b65ada457eab79cf0597508c6f6bfe10668038ad0e02c34d`,
`86f43e9431c60b99dda1ccb5daf1e8d6e126510d214f6f0be7c95e47e20d25d4`,
and `4dccc638e287690de291f5ebd08fe7760d5334212db66460b67652501bb2d3ae`.
The extracted ext4 volume passes read-only `e2fsck` and contains only the 3.0
Interface Kit provider. Twelve independent Cam Link samples of the
indeterminate progress region produced ten distinct frames. The retained frame
is `/private/tmp/astra-interface-v3.png`, SHA-256
`e738c85fcd3f281b319f7e11daab32161dd0a928c9917b18683d60e691e24371`.

Immutable release
`e3d871bf579f732e58d5755414ca0f21c1445004ca4cadad480009bab18e15e7`
was the previous byte-verified physical DE25 runtime. It carried Interface Kit
ABI 3.1 and the Gallery's shared segmented selector, reached stage 8 at
21:48:54 on 2026-09-12, and remains active with zero service restarts. Its
clean storage image, ROM, Interface library, and Gallery SHA-256 values are
respectively
`e8ed59be274941a28a71e082d7695ded45b83a989d59754554047feb41fb676c`,
`4c019911b1c6c500a045fc3cf29bcab7225e9fee014d33bbeba78009b2cb9ee5`,
`8aca7e8064579dd911036c3e8b6753ac68d0cd713ed2a7a1e878be053dacc113`,
and
`6bccd169bfbb59a0228df44565fe416f12c92a5a9ccf6f9107b91f1a3335957c`.
The extracted ext4 volume passed read-only `e2fsck` and contains only the 3.1
provider. Segmented visual/input/timing acceptance is still outstanding.

Current source deliberately advances Interface Kit to ABI 4.0 for the numeric
stepper. The ABI break changes the caller-owned UI context and makes vblank
return the same semantic action used by ordinary input, so pointer-held repeat
does not introduce polling or a second callback route. The stepper supports
signed decimal entry, range snapping, Arrow/Home/End, immediate pointer steps,
a 400-millisecond hold delay, and muted bound affordances through shared range,
layout, focus, damage, and rendering code. Host, sanitizer, analyzer, MC68040,
library-contract, and SONAME checks pass. Diagnostic immutable DE25 release
`1543f41e976943999bfc5447e26a9aa7f62a8f49e7966acaa6540515c44b4bff`
reached stage 8 at 69.416 MHz with zero restarts and rendered the control
through the production draw-list and FPGA path. Ten thousand segmented and
stepper action transitions measured 7.003 and 8.926 microseconds each; both
now share a measured 12.5-microsecond regression gate. The Gallery capture is
`/private/tmp/astra-interface-v4-gallery.png`, SHA-256
`2da4732dc0c5b42ed5763ab8e11b8f9d8df0b361199b3002e8325595c1a438d0`.
Its image, Interface library, and Gallery SHA-256 values are respectively
`3233d5ab123633a06cf713b949650660134e5bc68b896f6f9bdb02bab948380c`,
`f3958b93f0dac80a35db19407e91930c28b0e72446d3e25ba7d222359282f199`,
and `9032d4788dcaa01ce16adcdcad5104972d4ac6dcdeaf3c46ef3ca8ede1a8cf54`.
Physical pointer and hold-repeat acceptance remains pending.

The same capture exposes a font integration gap rather than a UTF-8 decoder
failure: the Field preserves the `世界` scalars, but its embedded mono strike
lacks those glyphs and displays the replacement glyph twice. Interface Kit
must consume `font.library`'s resolved layout and fallback chain; it must not
grow a parallel embedded-font fallback implementation. `世界` remains the
target regression specimen.

Immutable release
`196ed3afe0a803db5760749fbd32a9ca32d6545453343b2056cd1be4efdaa8aa`
is the current normal-desktop DE25 runtime. It carries Interface Kit ABI 4.0,
reached stage 8 at 69.685 MHz on 2026-09-12, and remains active with zero
service restarts. Its clean image, ROM, QEMU, Interface library, Gallery, and
display-helper SHA-256 values are respectively
`bc6b6c86c6e199132ce5894ef5329ffb77c51fd8eefcafd718f4bb4adbcffd05`,
`4c019911b1c6c500a045fc3cf29bcab7225e9fee014d33bbeba78009b2cb9ee5`,
`036b25e47f4cdbbf44ee99d6d83eb61b02c61eec0f9738e51b6b262522607fec`,
`f3958b93f0dac80a35db19407e91930c28b0e72446d3e25ba7d222359282f199`,
`9032d4788dcaa01ce16adcdcad5104972d4ac6dcdeaf3c46ef3ca8ede1a8cf54`,
and `ca281d6d6d2d37fb566c02a415f1d54dc7e04192f2adaf492a4ee4ab817edf65`.
The extracted ext4 volume passed read-only `e2fsck` and contains only the ABI
4 provider.

Immutable release
`f01ed24223639e6b6881fed5372a66728388e8ea1f146a04de48277212e6c8ad`
is selected and running on the physical DE25 with Interface Kit ABI 4.1. The
Gallery's tabbed root is a column: the tab strip and active page now share the
same x-axis, and the page begins below the strip. The former right-edge
overflow was an obsolete `ASTRA_FLEX_BREAK_AFTER` retained on those root
children; in a column it correctly began another column. The constraints were
removed rather than weakening the shared flex semantics, and a geometry
regression retains the vertical relationship. The service is active with zero
restarts. Its clean image, ROM, QEMU, Interface library, Gallery, and display
helper SHA-256 values are respectively
`001a051a179f8f7e2d59471c3a5a368c3345b03808d3041a6dfab894d1c80652`,
`9fea35c45eff803e26ee66c2c9bf81d156c5d77a2df7fca550e0503b829d6530`,
`036b25e47f4cdbbf44ee99d6d83eb61b02c61eec0f9738e51b6b262522607fec`,
`67b7ed5ca577bd6e04f626a34bcbe172cb58fcd976526225a415ea4e7190a527`,
`e6b0c11bf40008c2151fdeed610eb451e89ef5709082af2883c77fc0fec77183`,
and `ca281d6d6d2d37fb566c02a415f1d54dc7e04192f2adaf492a4ee4ab817edf65`.
The extracted ext4 volume passed read-only `e2fsck`; its installed Gallery and
Interface-library bytes match the target build exactly. The Cam Link proof is
`/private/tmp/astra-gallery-tabs-fixed.png`, SHA-256
`591628e3b33320ee583d8b1e7ab4315e8631149df2cab6c97ea493db88f54fdf`.

Immutable release
`92aa26670bd04ed73c22dad072fb082d08fffccf5bfd8a185fd07df4aff33e43`
is selected and running on the physical DE25 with Interface Kit ABI 4.3.
ScrollModel, ScrollView, and Scrollbar pass host, sanitizer, analyzer, NDK,
and MC68040 gates; the release volume passes read-only `e2fsck`. A physical
wheel notch completed in one presentation and reduced the render batch from
the normal 153 commands to 53. Command zero copies retained pixels from
`(24,159)` to `(24,143)`, `260x104`, then only the exposed 16-pixel strip and
bound scrollbar repaint. The Cam Link specimen is
`/private/tmp/astra-gallery-retained-scroll.png`, SHA-256
`313ffb1e26c45b11c97b83c75ff3216da540e3a927f27899aad2d48af48787c7`;
the captured render mailbox is
`/private/tmp/astra-scroll-retained-mailbox.bin`, SHA-256
`56600bba31a6910b4ad78be24b79cdf0f26c0d59e20722fb0539d05a76efeafa`.

Interface Kit ABI 4.4 adds a splitter as the only new primitive required for
split views. The panes remain ordinary flex containers, so nesting, intrinsic
measurement, min/max constraints, damage, actions, and resizing retain one
implementation. The divider supports captured live pointer dragging and full
keyboard adjustment. Its validation is folded into the existing sibling
layout walk; interfaces without a splitter do not pay a second control-array
traversal. All host, sanitizer, analyzer, MC68040, NDK, and benchmark-parser
gates pass.

Physical acceptance release
`b83a032916fe6e0bf9fe6704fbc3855923f97e65fb12cb2143ef28e011456cce`
measured the complete three-control pane/divider/pane reflow at 26.953
microseconds per action across 10,000 live changes, inside its
30-microsecond gate. Its trace has SHA-256
`34bf6843d15a8f470f93dd66e65eabc832fcf5e4b28e399597ee3cb3c55e6e0d`.
The physical drag specimen is
`/private/tmp/astra-splitter-final-dragged.png`, SHA-256
`31ce41e37184a26c46090fd8b70706cf8c94b8803117cfa907a7fe9d9646eeb3`.
The normal desktop release
`3160fef3de25828a981d2cf9eaf133b0d6ccbb55cd3faef23fa1e4e9994337ff`
is selected and running at stage 8 with zero service restarts. Its clean ext4
image, Interface library, and Gallery SHA-256 values are respectively
`6ba4abc6f121379b6fa9051e69a1d8449c1c95c0332540ea3bc043bd5e2f27fe`,
`75409503f55837b9f29f6bbb18c50612cf1d999ae17d6341862600a68d3c6417`,
and `49c06d849e2d28d288a7c9d73e395a5547d45e6d24bb8a15547656152daff331`;
the extracted volume passed read-only `e2fsck`.

Current source advances the logical input service to protocol 2, the GUI
service to protocol 9/window-event ABI 4, and the public pointer observer to
event ABI 2. Normalized modifier state is captured on every logical input
event and survives input motion coalescing, display routing, window-local
delivery, and screen-space pointer observation. This is the shared prerequisite
for ScrollView Option-click/Shift-drag behavior, modified drag-and-drop, and
other pointer gestures; receivers reject unknown modifier bits at their trust
boundaries.

The preceding ABI 2.6 physical DE25 release
`ee84a8a336d68cc6469252690e3ac778790225bceda79ea7e7ecd1d425944ebc`
booted this slice at 70.038 MHz, reached stage 8 with the correct wall clock,
and remains active with zero service restarts. Its storage image, Graphics
library, Font library, Interface library, and Gallery SHA-256 values are
respectively
`51f0b89637c151b452feff64c1b8d5dcadfb58f005b990705cd033990f0efb05`,
`f1ed71716748d288bc1972b1cd10f5733ee413344cf4ad779effccbe25a1e345`,
`8381f37774653b834b7a83a12f627a411f5aa8b766399303ceb55d9742168859`,
`8ef9ab735ad969749c1230217b2d8df2b41ee3fe0cd9fd0b322f5c30058e13b9`,
and `851bf2c321e0b3321ab1db6d5ca35ebb33d0baa56096e8f14e941963e962ffed`.
The target measured 7.320 microseconds per contiguous append and 656.958
microseconds per deliberately maximally fragmented insertion across 4,096
edits; automated ceilings are 10 and 800 microseconds respectively. The
retained trace is `/tmp/astra-interface-ee84a8a3-ring.bin` on Beast, SHA-256
`1428bfb135d319866e9666301f06f22a23bb35b20f4a96c221251ea5d792635b`.
The Cam Link specimen is
`/private/tmp/astra-interface-field-ee84a8a3.png`, SHA-256
`6c9cf5236a4f3a20941b55d8727b3627990f75a11cb833a85a81ca160f73aafd`.

The canonical rescue font's encoded U+FFFD glyph was blank. The shared AFNT
importer now requires a visible replacement glyph and chooses visible U+FFFD,
`?`, or the source default in that order; surface and importer tests retain
that invariant. Unsupported scalars are therefore visible instead of silently
disappearing. The rescue face remains Spleen. The intended system stack is
Atkinson Hyperlegible Next for UI, JetBrains Mono for terminal and code, and
Noto fallback faces for installed-script coverage; these require the shared
A8 outline-import and font-service resource path rather than per-application
font copies. Current uncommitted work separates the Spleen rescue AFNT from the
normal desktop stack, imports the complete Atkinson Hyperlegible Next and
JetBrains Mono Unicode cmaps into A8 strikes, and extends both the hardware
draw-list lowering path and its RGB565 software oracle to consume A8 coverage.
The Fonts Settings contract now treats a multi-font drop as one durable catalog
transaction: format handlers report per-face progress, and any conversion,
validation, source, or storage failure publishes none of the batch.

The Astra OS and NDK version are one authoritative value,
`0.1.0-dev`, exported by `astra/version.h` and consumed by the kernel, boot
image, manuals, metadata, and package name. Beast certification produced the
deterministic relocatable archive
`astra68-ndk-0.1.0-dev.tar.xz` (2.8 MiB, SHA-256
`ef2ff330baf08f47b7c9c9742f26637ba0129d71a67994f9c83ff26dbb3e9a9e`).
Its self-check rejects unsafe or nondeterministic tar entries, validates its
internal checksums and shared version, extracts it, and links native C, POSIX
C, and POSIX C++ consumers using only archive contents plus the cross compiler.
The complete certification also runs every owned host, sanitizer, analyzer,
shared-library ABI, and link contract against the OS library implementations.
Every shipped public header must opt into the documentation gate; undocumented
public declarations, structures, members, callbacks, parameters, return
contracts, and unresolved references fail the build. The current strict
Doxygen warning log is empty, and both the HTML manual and 175-page, 574 KiB
PDF build successfully. Documentation artifacts are dependency-tracked so an
unchanged archive build does not regenerate them.

The shared-library architecture has been reduced to one policy path. Package
construction is the sole compatible-version selector and emits one canonical
provider record for every registered shared-library product. The VFS owns the
single exact provider resolver used by both supervisor bootstrap and the
process interpreter; the former Kit-directory fallback, its fixed scan tables,
and the kernel's unused "latest compatible" selection policy are gone. The
runtime loader alone owns `DT_NEEDED` traversal, symbols, relocations, TLS,
RELRO, and initializer order. The kernel accepts an exact library identity and
only validates, maps, shares, accounts, and reclaims pages. This boundary was
cross-checked against Haiku's user runtime-loader/kernel image split while
retaining Astra's narrower streamed-image cache because Astra VFS is a user
service rather than a kernel file cache. VFS, supervisor, kernel process, image
provider, and Kit provider-coverage tests pass on Beast; full image boot
validation remains the release gate for this uncommitted cut.

Commit `b36a784` is physically accepted for this fixed-grid cutover in immutable
DE25 release
`5b00754844ed0714b8ecce98fe8042a9d130031ba2014bfd1d49207818fecabb`.
The board completed POST, reached initial-image stage 8, reported an effective
70,196,000 Hz MC68040 and remained active with zero service restarts. Opening
Terminal and executing `echo textsurface-ok` produced the expected command,
output, and prompt on HDMI. The first output appeared in 108.041 ms and settled
in 205.463 ms through two display batches, two glyph commands, 189 renderer
commands, 29 fills, 158 blits, and two completed submissions. The retained
Cam Link frame is `/private/tmp/astra-textsurface-terminal-5b007548.png`,
SHA-256 `30f43a3f234208ad570ce0b22cfefc647b293a7f8f85c9a2cfea44d883babf23`.
Release artifact SHA-256 values are
`472897d5732523e4c455e44d89729afb45b267ec1564781a8448761b7b317085`
for `storage-terminal.img`,
`4c5606a57402fff5c404da0d0033eed975ed3f6454b2244f9fe32de12ea3df5e`
for QEMU, `a84456a3d4026e6fdca0d02fb0c949fa5e48040971b7781a7e2d75ddd86f47e9`
for the boot ROM, and
`c109cadab97c18500ee7bfb2bb4c4701542342b6effc48d6204832b0578f5bb1`
for the terminal display renderer.

A forced kernel rebuild exposed a stale 1080p contract: the advertised native
RGB565 frame is 4,147,200 bytes but a transfer slot held only 2 MiB. Current
source derives a transfer slot from Astraea's complete 8 MiB render workspace;
the old independent mapping constant and duplicate public DMA quotas are gone.
The anonymous private window correspondingly spans 480 MiB; allocation remains
demand-paged and owner-charged.

## MC68040 contract

Every Astra target build uses `-m68040`; user software reserves A4 for the
thread pointer and uses software floating point. Axiom uses the MC68040's native
SRP/URP registers, three-level `7/7/6/12` translation, 4 KiB pages, format-7
access-error frames, address-selective ATC invalidation, and explicit cache
maintenance.

Only MC68040 exception formats 0, 1, 2, 3, and 7 are accepted. The maximum
frame is 60 bytes. Formats 9, A, and B and their older SSW interpretation have
been removed. The old private QEMU MMU/frame implementation and the unreferenced
standalone core-test ROM have also been removed.

The architectural authority is Motorola's MC68040 manual. Current normative
contracts are `KERNEL_ARCHITECTURE.md`, `MEMORY_MAP_AND_PMMU.md`, `ABI.md`,
`LOCKING_AND_PREEMPTION.md`, and `RESOURCE_OWNERSHIP_AND_FAILURES.md`.

## Kernel certification

Axiom's MC68040 kernel certification gate is complete on the current source.
Filesystem performance work may resume after the remaining Astra OS cold-boot
release gate.

Completed evidence on the current source:

- clean GCC release and debug builds;
- the complete 30-test host kernel suite;
- Clang ASan plus UBSan;
- GCC 13 `-fanalyzer`;
- Clang 18 build/tests and Static Analyzer;
- exact cppcheck warning, performance, and portability pass with zero findings;
- 80.1% aggregate host-test line coverage (15,563 / 19,418 lines);
- generated MC68040 inspection of exception entry, interrupt entry, scheduling,
  address-space switching, user copies, syscall dispatch, and port operations;
- allocation-site injection, pool exhaustion, rollback, emergency-reserve, and
  zero-free cleanup tests;
- three physical DE25 executions through the complete K1-K10 workload;
- 18,000 consecutive physical fault/create/reap soak cycles with constant free
  pages and no panic;
- a retained allocation-free panic report reproduced byte for byte;
- a stopped production snapshot after every resident service launched.

The exception-family cutover was developed test-first. The old implementation
failed focused format, fault-classification, context, and user-copy tests; the
MC68040-only decoder and fixtures now pass the complete clean host suite.

The retained physical `-Os` qualification image has SHA-256
`4f596cc90409a7e90bb8ac382e1191adb96d21a850f5e2fc36b51a17c71967a6`,
a 178,576-byte kernel, and a 344,084-byte ROM. Three deterministic runs on CPU2
reported 62.362, 61.714, and 62.411 MHz effective. The first two produced exact
path measurements: syscall 3,382 cycles; timer 631; user fault 22,991;
scheduler pick 34; same-space switch 65; cross-space switch 70; wait block 74;
wake 111; port send 327; and port receive 779. The third differed only by one
guest tick in a few paths. The longest interrupts-disabled interval remained
inside its explicit 125,000-cycle gate.

These are guest timebase cycles, not literal physical CPU clocks. The board
run is physical throughput evidence; Beast measurements are development-only.
The 18,000-cycle physical soak used ROM SHA-256
`a56d7f35d59cf0f3966507d596a6aef9cffe0573a251915369e2127aa60f379b`.
All 21 checkpoints passed, free pages remained exactly 30,945, and 36,032
context switches had completed at cycle 18,000. The retained Beast copy of the
physical log is `/tmp/astra-k1-040-soak-20260907.log`, SHA-256
`9e0c62cb381c0ed64cb83926bb90edc508dfad664ba968c022b3e726b1b452b0`.

Removing process-launch scratch arrays from the common syscall stack reduced
the generated dispatcher frame from 4,292 to 712 bytes in qualification and
716 bytes in production. The corrected physical K1 poison scan reported a
3,244 / 8,192-byte high-water mark. A GDB poison scan of the uninstrumented
production kernel after storage, hostfs, network, and ntpd launched found the
worst thread at 4,268 / 8,192 bytes, leaving 3,924 bytes. That snapshot contains
9 live processes, 14 live threads, and 52,941 completed syscalls; its retained
log is `/tmp/astra-prod-kernel-stack-20260907.log` on Beast, SHA-256
`fce2a82f581fd2a995d419b3dc959f6abd21613cfe39e81920d157743a7852c1`.

The retained panic test used ROM SHA-256
`698ac6b255517b5af36b5a78b2a359da9b63449f784bfe668c79166dc587d0ba`.
The launcher's retained report and `panic-latest.log` are byte-identical,
3,377-byte records with SHA-256
`312e4b9e2f7418b138a3e3dfaad579b801a078d984f6a4bc07f1a866eaf1376`.

An `-O2` experiment expanded the kernel to 247,200 bytes and did not complete
the same physical qualification workload after the deliberate user fault. It
is rejected pending root-cause data; size is not the rejection reason.

The complete evidence matrix and disposition are in
`KERNEL_CERTIFICATION.md`. The remaining release work is outside the kernel
gate: cold-boot the selected immutable release through the native NTP barrier,
stage 8, the real display, the terminal command gate, and repeated boots.

`astra.service` is enabled and the board had cold-booted immutable release
`8786083b44a9b91d3e5a8123093cc855953497824ee1239ab318c9c0b898bf0f`
through native time synchronization, journal recovery, storage, hostfs,
network, ntpd, and the real display path. Immutable observability release
`9a8505ab47df89008a529437cedb56e507363050c23eca5d8f46bb444607e97e`
is now selected, byte-verified, and running. A physical console SysRq sequence
synced storage, remounted it read-only, and reset the board. The following cold
boot passed the native NTP barrier, reported the correct 2026 wall clock,
verified and recovered ext4, launched the metrics-enabled hostfs, network, and
ntpd services, and reached stage 8 through the real systemd/display path.

## Runtime and storage boundaries

Linux owns physical storage and networking. Axiom exposes mechanisms; protected
services own filesystems, paths, configuration, networking, graphics, input,
audio, and launch policy. VFS remains filesystem-agnostic.

The DE25 production storage image is immutable per release and copied to a
writable per-release state image. The host filesystem root must exist before
QEMU starts. Astra startup waits without a deadline for the native
`systemd-time-wait-sync.service`; a guest never starts with an invalid host
date.

The DE25 root filesystem boots with the explicit kernel option
`rootflags=commit=5`. A physical boot after `saveenv` verified the exact kernel
command line, synchronized NTP, and normal `astra.service` startup. The prior
`commit=1` policy is rejected: ext4 now uses its normal
five-second journal interval while ordered journaling, write barriers, and
explicit `fsync`/`fdatasync` durability remain intact. The live root is
`rw,relatime`; `findmnt` omits `commit=5` because it is the ext4 default.

The retained filesystem baseline release carries QEMU SHA-256
`c06b6fab7e4b88d4e0e921d906801316a2da7a8d6002e39be8dd0c3840c14316`,
production ROM `a8e34319e74ac9efc60f1056fc452cba5566a462a997b0c60e36a69a7784b685`,
and storage base
`3d05392f0cf20c09af186845620c1d11f8115311ad0fc253b8f2415b122d9c2f`.
The display and launcher hashes are respectively
`fe62b9c45b45aa493e6eb4f3fd0554d95f54f0c2b168ac1140238f7c7a78fb87`
and `1862251c1c8c6c18df55f17189c9daf4a1136c50f5c46919594136c2822909b6`.
A fresh physical host-channel run completed 901,136 commands with 2,048
requests in flight and peaked at 645,753 requests/s at depth 1,024. This proves
the cross-boundary worker machinery and four-core placement; it is not a VFS
or durable-filesystem throughput claim.

The retained Cortex-A76 QEMU experiment improved the physical no-poll baseline
from 1,110 to 1,183 operations/s in RAM and from 667 to 725 operations/s on
SD-backed ext4. The retained shared C completion window then reached 1,665
operations/s in RAM and 795 operations/s on ext4, with exact accounting and
clean independent `e2fsck`. An assembly version was rejected after paired
physical medians regressed from 1,618 to 1,288 operations/s by reaching the
sleep path sooner.

Operation-specific physical measurements locate the durable mutation cost:
reads reached 3,992 operations/s, renames 2,682, repeated syncs 4,543, and
open/truncate/write/close reached 701. Host timing attributed 98.508 of 133.2
aggregate seconds in that write run to open/truncate. After removing
`commit=1`, the existing native SD durability benchmark completed and verified
4,096 records: one `fdatasync` per record reached 183 operations/s, four
records per flush reached 835, and eight reached 1,685. This proves that the
remaining >=1,000-operations/s design needs safe group commit; disabling
barriers or acknowledging uncommitted data is not an acceptable route.

The complete 524,288-operation, eight-worker guest/VFS stress run on the
physical DE25 and SD-backed ext4 `commit=5` path passed exact accounting at
733 operations/s and 1,191,513 bytes/s, followed by a clean independent
`e2fsck`. It completed 1,341,528 host commands with 2,717,776,160,647 ns of
host execution and 80,284,785,733 ns of queue time. The retained log is
`/tmp/astra-fs-commit5-20260907.log` on Beast, SHA-256
`1bcee433a7cb906b3d6eaf13f65f8e53abd6c3fac9b6e6497af78eb6b3cd0791`.
This is the current durable end-to-end baseline; it does not yet meet the
1,000-operations/s target.

## Developer observability

`PROC:` is the supervisor-rendered, capability-gated process view used by `ps`.
Its source is Axiom's complete live-process registry, not the supervisor's
launch bookkeeping, so services, desktop applications, Terminal children,
forked processes, and `ps` itself follow one visibility rule. Only the initial
supervisor can take the complete fixed-slot snapshot; commands receive only the
read-only `PROC:` mount. Records include measured CPU runtime, elapsed lifetime,
resident memory, state, scheduling, syscall, handle, and fault information.
System and service counters remain separate through the read-only `METRICS:`
capability. Hostfs owns that view because it already owns the host-device
channel and its VFS service; neither underlying authority is delegated to
commands.

`METRICS:snapshot` contains versioned fixed records for block traffic and
durability transitions, host-channel submissions/completions/execution and
in-flight depth, hostfs VFS protocol/session/file/resource accounting, and
calls plus execution time for every filesystem operation. The `metrics`
command renders those records as `group.name value`. A current-source QEMU
boot passed the complete 71-command terminal gate, including ordinary-process
reads of `host.channel.commands`, `hostfs.vfs.requests`, and
`host.fs.open.calls`; the command completed in 0.27 seconds in that gate. The
same gate verified PID 1 as `ROM:supervisor`, the top-level `PROC:` layout,
resident library accounting, and installed library versions, ABIs, binary
sizes, and provider paths. Its ROM and clean storage-image SHA-256 values are
`f67e96036f3434ffbc31fe5a7cc43b400c6a1f21ee1798e858503def6f6d8119` and
`9ccb9456ac04e9dea8932022bf9800878469503061062696e8703efe68b1c887`;
the host QEMU SHA-256 is
`b7e5cf2ca611fe35fff492527051fe439f9bfde352476728dcb766d1793a3cb0`.
The installed release then passed the physical cold-boot gate described above.
A focused terminal driver running the production AArch64 QEMU directly on the
DE25 also launched `metrics`, found the three independent host-channel, VFS,
and filesystem-operation groups, and completed in 1.12 seconds while the
production instance remained active. Its retained Beast log is
`/tmp/astra-metrics-physical-20260907.log`, SHA-256
`68b78603a69181a5e4490550480de6fdb9ae556ac62d335b9be3791947b6cf6a`.
The complete 71-command gate used the
same ROM and storage content under the current-source host QEMU so that its QMP
driver could inject input and inspect the trace without granting physical-board
users control of the production QMP socket.

The last complete terminal gate passed 84 commands, including upstream
zsh arithmetic, PATH lookup, exit status, nested command substitutions,
external pipelines, stdin, stdout/stderr redirection, and Lua execution. The
intermittent `lua: wait failed` / `POSIX VFS:16` class was measured as
`ASTRA_SYSCALL_CANCELLED` interrupting a VFS reply wait after its request had
already been sent; treating that as peer death closed a healthy storage
session. Internal protocol and synchronization waits now share the runtime's
restart-after-signal contract, while application-facing POSIX waits remain
interruptible. A regression injects cancellation between request and reply.
That exact clean Beast QEMU gate used image SHA-256
`3bb30aac26ef298c794130b99950ee11008c9b485ebce76ba305cbddcaf98f60`,
ROM SHA-256
`6b55f9e5b786705031aa51fbc2d7b7452e9c2595b7b396ed85820a9b828c70b4`,
and QEMU SHA-256
`1302f467d8e63ff3af38d94a57b5cdb7d37e75fe764714fe40f484ea35d71999`.
The image's extracted ext4 volume passed read-only `e2fsck`. Physical DE25
cold-boot qualification of this source remains outstanding.

A subsequent clean focused gate proved the shared process view and zsh's
interactive contract on the MC68040 target. Its 14 scripting cases passed in
0.34--0.77 seconds each, then a second Terminal's `ps` listed both Terminal
processes, the live interactive `zsh`, and the observer `ps`. ZLE editing,
history replay, clean exit, and Terminal session recovery passed. The clean
image SHA-256 was
`b9cf7b7eb83e874623a9da423b033bd9159121b1004053c879fe785be55f0152`,
the ROM SHA-256 was
`769968541d13d860bf4b835aab4af23893dac3be23ae065b4e6d5a4088cb5e11`,
and QEMU SHA-256 remained
`1302f467d8e63ff3af38d94a57b5cdb7d37e75fe764714fe40f484ea35d71999`.
The extracted ext4 volume passed read-only `e2fsck`; physical DE25 cold-boot
qualification of this source remains outstanding.

Immutable 512 MiB release
`b05969f66fe1a4ce0b70d1fe595c4a0ea40dc32a8bbb0ad5aa079955649a11c4`
is selected, byte-verified, and running on the DE25. Its AArch64 QEMU, ROM,
clean storage base, display helper, and launcher SHA-256 values are
`09f177837ef3c0d0a2dc0c482bc98e9935e9c2919e1387269f782bf15bd604bd`,
`96dcfa24ca2f39ba163e1c36c7122d94e5f9d65e012b1e69dc9e8f311331f30f`,
`9bb24543fbf2f69c008b8c0d946eeb1173cbdead9224dd781c35e712406c2c42`,
`ce47c8af92de431758739cbc4c95fe0d7996ff232000330f849bdffcc18f9743`,
and `b413bf10b7ddb11990f510aa5ff2f5f099e7d09fa7115f87ba027c065d632e74`.
The extracted ext4 volume passed read-only `e2fsck`. A physical service start
reported 512 MiB, passed full-range POST, exposed 131,072 pages, recovered and
verified storage, launched every protected service, and reached stage 8 at
70.117 MHz effective. QEMU settled at 553,940 KiB RSS; the systemd unit was
active with zero restarts and the terminal display resident.

The 512 MiB Media RAM window passed stuck-address, random-value, XOR, subtract,
multiply, divide, OR, AND, sequential-increment, and all 64 solid-bit patterns
with zero mismatches. The following block-sequential phase was stopped at the
operator-selected acceptance boundary rather than represented as completed.
The exact shell also passed calibration/readiness, splash CRC32 `611029ee`, and
the 512 MiB arena identity check.

All window-content scrolling uses the shared draw-list copy operation, which
the renderer lowers to an overlap-safe FPGA BLIT. The boot console now uses the
same renderer and AFNT font, retains exact state for both scanouts, repairs any
coalesced changes around a measured-cost scroll candidate, and presents only
complete inactive scanouts at vblank. It no longer writes a live framebuffer.
Render batches carry their exact live high-water mark, and the host copies only
the protocol regions the hardware may read rather than reserved ring gaps.

Physical DE25 counters measured the resulting steady window-scroll frames at
102528 bytes, 37--38 commands, 0.28--0.34 ms of graphics-memory transfer,
1.08--1.15 ms in the FPGA renderer, and 2.30--2.49 ms total render handling.
Presentation then waits for the next 60 Hz vblank, so sustained visible
scrolling is refresh-limited rather than renderer-limited. Before the retained
transport change, equivalent frames copied 139264 bytes in 7.6--7.9 ms and
took 9.6--10.0 ms of render handling. Coalesced boot-console scrolling fell
from 103--110 ms hardware repaints to roughly 19 ms hardware batches, with
about 2.2 ms of transfer. A physical 121-line zsh scroll workload completed
without corruption or a service restart.

Immutable release
`7a7abd27067563523838288cb8aeb00fcb6acd56acd50d97666d8243bae2daea`
is the current byte-verified DE25 runtime. It reached stage 8 with zero service
restarts. Its predecessor
`2e05b83e3bb8feada63d017e473829ad011095b04ee94e4c52c356c3a71a25a8`
is the automatic rollback release; these are the only unpinned release and
state generations retained on the board. The root filesystem has 18 GiB free
and is 35% used.

The apparent recurrent zsh crash was an old capture predating the active
release. The actual zsh failure was nevertheless fixed at its shared process
boundary: ncurses reserves 65,552 bytes in `_nc_read_tic_entry`, exceeding the
former 60 KiB usable stack ceiling. User threads now receive an 8 MiB virtual
stack reservation with a guard page and demand-paged physical backing; stack
page accounting is wide enough for the reservation. The unchanged zsh binary
starts, executes `echo zsh-ok`, and returns to its prompt on the physical DE25.

Render-batch ABI 1.1 carries the hardware-pointer state with the scene, so a
window move submits and commits one scene-plus-pointer transaction at one
vblank. A 60-sample physical drag produced 30 submissions, 30 render batches,
and 29 completions while input was active, then 34/34/34 after settling; the
former path submitted a separate cursor request for the same input sample.
This removes split-frame state but does not yet meet the 60 Hz movement target.
Cycle measurements identify the remaining limit without inference: a moved
Terminal frame spends about 25--27 ms in three large hardware passes--a
434,928-pixel background fill, a 434,928-pixel desktop restore, and a
403,440-pixel Terminal blit. Hardware-managed window planes/z-order, or an
equivalently measured compositor design, is the next display architecture
work; cursor-path micro-optimization cannot solve this bottleneck.

Immutable release
`803e2052cb2a4904c3b68160e9fc6e41c20566c014cd4a5cb8f1ceb2568eaa3f`
is selected and running on the physical DE25. Its QEMU, ROM, clean storage,
display helper, and launcher SHA-256 values are respectively
`fc95b55c8f71762f7df9f0a6576329dc79f89db2b4fbeed14cb3d7827fb6e15a`,
`e3edf0a710f59f8007473f16b6f444623b8cd13b890ac74af7c4efb6f7e42ebd`,
`050ed6034110483de8f79e1285cee29b74b9823b18765fb7a524ce19a5662fe9`,
`ca281d6d6d2d37fb566c02a415f1d54dc7e04192f2adaf492a4ee4ab817edf65`,
and
`1e76f48511636d1cc9c479c3f7ae90412322691b9af6c3b33a026d454b7b6092`.
The extracted ext4 volume passed read-only `e2fsck`; the runtime reached stage
8 at 70.014 MHz effective with the renderer resident and display submissions
and completions at 2/2.

The preceding visible freeze was not a kernel deadlock. All 11 processes and
15 threads remained alive and blocked on their expected wait objects while the
MC68040 idled. The guest render builder allocated every record at four-byte
alignment, but Astraea requires 32-byte surface descriptors and 16-byte glyph
arrays. A second source descriptor at `0x0081a310` therefore reached hardware
misaligned, was rejected as `ASTRA_RENDER_STATUS_BAD_DESCRIPTOR`, and the host
renderer exited while the display service waited for a completion. The shared
builder now aligns each record to its protocol requirement, and the host
validator enforces the same contract before submission.

The launcher now supervises both host display and input helpers instead of
waiting only for QEMU. A dead helper is reaped and replaced while the guest
continues. A physical SIGKILL of renderer PID 19446 produced replacement PID
31193; QEMU and the Astra service retained their PIDs, systemd recorded zero
restarts, the renderer returned ready, and the last accepted display pair
remained complete at 2/2. The launcher regression also forces one renderer
exit with status 42 and proves that its replacement runs before QEMU exits.

Interface Kit ABI 4.5, GUI protocol 11, render-batch ABI 1.2, and display
mailbox 1.5 now carry semantic and custom pointer images end to end. The shared
NDK window API selects the built-in arrow, horizontal resize, vertical resize,
text I-beam, or wait image and copies application RGBA pixels plus hotspot into
an immutable window-server-owned area for a custom image. Interface Kit maps
field and splitter hover/capture state to the same identifiers; applications
do not access display MMIO or reimplement pointer policy. Its one update entry
point accepts `ASTRA_POINTER_SHAPE_AUTOMATIC` for those control semantics or an
explicit built-in/custom shape as a deliberate window-wide override;
automatic hover changes cannot replace a wait or custom override.

Custom replacement remains transactional until the hardware accepts the new
image, and image, hotspot, position, visibility, and scene commit at vblank.
All NDK, Interface Kit, display-service, graphics, kernel, MC68040, AArch64
renderer, and QEMU build gates pass. Temporary physical Gallery release
`ab8de7c48ef6059ec25f89711cbfb6e5e42ef5e3acff4fe5556a174d2cc30f02`
reached stage 8 with zero restarts and visibly produced the default arrow,
I-beam, horizontal resize, and vertical resize cursors. The normal desktop
release `c5238d4b0f76e5d4a15ae80494c52ede9993c3eb65bb996914b34706083f5abd`
is selected and byte-verified on the DE25. It reached stage 8 at 68.464 MHz
effective with the renderer resident and zero service restarts. Its QEMU, ROM,
clean storage image, display helper, Interface library, and Gallery SHA-256
values are respectively
`8a7b8be1707379b8b40c49a19fe4df73d7a1091c37dfdaecaa5e792066fc9540`,
`53c4b0a9c757d2d730f2bfce414e282611abedbb95ab9c22cd6b015ad177762c`,
`6fff5cfe0124777fd906a489e045c800f957a1b352b52353dcbb053894d295f7`,
`abbc0720a0d9bd23c02260486e8f9f3db5838b59be55c4fd700e67d85655876d`,
`9534e426200ea517d98d29f789f554806d0d59763b79fbfc8e5adb8b638ae958`, and
`b609dc01d9bd556e1923210a376de0d40b51f1a4fbe106dba3d5cd52ef2cc29b`.

Graphics Kit ABI 2.1 and Interface Kit ABI 4.6 add the shared clipped line
primitive and retained Dial control. Dial provides vertical drag, Alt fine
motion, bipolar zero detent, double-click reset, keyboard adjustment, and live
value actions. It draws its indicator through the one Graphics Kit line path;
software clipping and draw-list lowering share regression coverage. Consumers
now declare the exact append-only Graphics export-table extent they use, so a
new minor export does not force unrelated 2.0 clients to rebuild.

Temporary Gallery release
`cc87a710f8b003090d122c4bb7d374ebe1160dbd3423bf20c1a809b5c8623442`
reached stage 8 at 69.238 MHz with zero service restarts. A physical drag moved
the Dial from 0 to 100 and updated its value label, focus ring, and indicator
in the same frame. The MC68040 completed 10,000 alternating Dial key actions
in 50,315,360 ns: 5.032 microseconds/action, or approximately 198,746
actions/second. Its retained Beast trace is
`/tmp/astra-dial-ring.bin`, SHA-256
`3c84e7ad3332e07eb435497131873804339a767471a656053d0f8782d605ae90`.
Cam Link captures before and after the drag are
`/private/tmp/astra-interface-dial-v46-value.png` and
`/private/tmp/astra-interface-dial-v46-dragged.png`, SHA-256
`95419357a11c05eb1568b8a5baf649b08b148a09e5f0ab0e8cdbedee21f3ddcd`
and
`5d26004a9d74484f15283e38b9e477885605ddb6aa6f2148844cb5c5d1425665`.

Normal desktop release
`35ca43caa2bec604f267d67ca1138259b28ac19b9324f0034fc776c45185bab3`
is selected and byte-verified on the physical DE25. Its QEMU, ROM, clean
storage image, display helper, launcher, Graphics library, Interface library,
and Gallery SHA-256 values are respectively
`8a7b8be1707379b8b40c49a19fe4df73d7a1091c37dfdaecaa5e792066fc9540`,
`53c4b0a9c757d2d730f2bfce414e282611abedbb95ab9c22cd6b015ad177762c`,
`1e5e6414393c5a5f201ad8e38711e8c4b48d757b36ca9fbf0edbe64c6f1107cd`,
`abbc0720a0d9bd23c02260486e8f9f3db5838b59be55c4fd700e67d85655876d`,
`1e76f48511636d1cc9c479c3f7ae90412322691b9af6c3b33a026d454b7b6092`,
`53d5f184c4cb672e6561e5835caa84806a649d4c4bab68534d8bdaa6583d689d`,
`263c44454c61ef47d400f4466fea974f26fff53918df8695029fad031f8187e3`,
and
`3fe986523ac245be2727976bfacfdd8c54cb230f77c051f9790f6b2d85659292`.
Its extracted ext4 volume passed read-only `e2fsck`; the runtime reached stage
8 at 69.295 MHz effective with zero service restarts.

Immutable release
`80a5eb9d0899e24e17d2e629e16adde13f393520c930a05c3397c0d531177cb2`
is selected and byte-verified on the physical DE25. It was produced by the
canonical publisher after clean, all-core builds of every owned software
input. Its source manifest is part of the immutable release, and publication
would have failed if any non-ignored source changed during the build. The
release directory and files are read-only; `astra.service` is active with zero
restarts, and the live QEMU command names this release's QEMU, ROM, and
release-keyed writable storage explicitly. The release's QEMU, ROM, clean
storage image, display helper, launcher, and source-manifest SHA-256 values are
respectively
`a5b8e918d35eab433773f7268e76a06f5a825597d90bcb9699f8560ee56cd9ec`,
`0c040408756d3da305b0a273940dfc8e37915f61060e61a02e509be22a464a68`,
`8c0fe7cf9b1aece128c4246a46aa4c88dc61d24fb26c25da6cb7198c512aa4d0`,
`8196e83f575a90c3f1db9dd78a705859c55885169799019c4256303a5bd232b7`,
`1e76f48511636d1cc9c479c3f7ae90412322691b9af6c3b33a026d454b7b6092`,
and
`1a2eff8bc42a5bd0d61440104078c488e9f89ef53108afc5d9dfca4e381a66da`.

## Active remote-display capture

The routed DE25 shell now contains a demand-driven RGB888 capture writer on
the final physical pixel stream. Its input is after framebuffer/window, tile,
sprite, Copper, boot-overlay, scanline-replay, and native-pointer composition;
remote display therefore sees the same complete image sent to HDMI. Capture is
disabled unless armed. One exact 6,220,800-byte frame at the top of the 512 MiB
Media RAM arena is excluded from the display allocator; render records,
surfaces, and capture storage cannot overlap.

Immutable release
`48903a19e61b15bd2b0d09556e7cf57d9f2c94cdd25b3398b448b10b78d4bc39`
is selected, byte-verified, and running at stage 8 with zero service restarts.
The display helper now commits an empty scene before reusing Media RAM, so an
abruptly killed showcase cannot leave the FPGA scanning the render workspace
while its successor writes command records there. A fresh direct Cam Link
frame is `/private/tmp/astra-display-handoff-fixed.png`, SHA-256
`5093d751b893498d4b5eb0256c759b6f5feb9103c06c106aa0f1c700b157682b`.

The existing DE25 DesignWare AXI DMA controllers provide the no-RTL readback
path. The exact running Linux source is Altera commit
`d7d192a9ddd955fe206aa350afed8f82e368a74c`; enabling its upstream
`dw_axi_dmac_platform` module exposes eight physical channels. Verified
single-channel `dmatest` copied 1,601 aligned 65,504-byte blocks with zero
failures at 156,206 KiB/s. Four channels on one controller moved the real
6,220,800-byte captured frame in 15,081,176 ns during the probe and
14,570,053 ns through the final read-only mmap device.

`fpga/de25/linux/astra_display_capture.ko` is the production on-demand
boundary. An exclusive open allocates one page-rounded coherent frame and
claims up to the four physical channels on one controller; close returns every
resource. Each ioctl captures a complete physical frame, maps Media RAM only
for the subsequent DMA ownership interval, stripes the transfer, and returns
the hardware generation, capture cycles, channel count, and measured times.
Writable mappings are rejected. The final physical gate reported generation
7, 2,639,808 capture cycles, 28,842,070 ns acquisition wall time, and
14,570,053 ns DMA time. Its RGB payload is
`/private/tmp/astra-production-capture-fixed.rgb`, SHA-256
`38e690b4f69e57fead7bdce21142d9f5f1420c7e95ce1d8dcd0fd0c4791de476`;
the lossless PNG is
`/private/tmp/astra-production-capture-fixed.png`, SHA-256
`6636321c89dd50b0470bbfcdbde75ab29b115d6dbd53dfe0432ead9a4b9dff87`.
The RGB payload is byte-identical to direct uncached readback.

Load `dw_axi_dmac_platform` once per boot and retain it until shutdown. The
upstream 6.12.11 remove path omits `dma_async_device_unregister()`, so unloading
it leaves stale DMA class registrations and reinsertion fails with `EEXIST`.
The Astra capture module remains unload-safe while closed and consumes no DMA
channels or coherent frame while unopened.

Immutable release
`2e2ba48d9dc8d902481e660d2cc28472736697447839e6005030917df42a7070`
adds the standard RFB/VNC service over this device and QEMU's existing input
interface without changing RTL or the Astra guest ABI. LibVNCServer listens
only on `127.0.0.1:5900`; remote clients use an SSH tunnel for authentication
and encryption. A dedicated
`/run/astra/remote-desktop-qmp.sock` keeps input injection independent of the
diagnostic `/run/astra/qmp.sock`. The physical RFB gate received the exact
1920x1080 frame and native pointer position `[960, 540]`; its 6,220,800-byte
RGB payload SHA-256 is
`38e690b4f69e57fead7bdce21142d9f5f1420c7e95ce1d8dcd0fd0c4791de476`,
byte-identical to the production capture above. With no client connected, the
service used zero scheduler ticks over two seconds.

LibVNCServer's default software cursor path attempted to paint into the
capture device's intentionally read-only mapping and crashed on the first
frame. The service now disables that redundant software cursor with
`rfbSetCursor(screen, NULL)` because the final composed capture already
contains Astra's native pointer. The service is packaged in the immutable
release, installed atomically by the deployer, hardened to read only the
capture device, and remains disabled by default. Explicit physical start and
RFB input/frame tests passed with both `astra.service` and
`astra-remote-desktop.service` at zero restarts.

The matching `astra_display_capture.ko` and `dw-axi-dmac-platform.ko` are
installed under the running kernel's `extra/` module directory with SHA-256
values
`1cc0eece7badb0aebc8c7690cfd341841356e36fc0358f03d0882ff9881d56e4`
and
`d254cdea3f4b5f0b57ac99cf6a1cebde41c2f4996c38bc2f0f69627355e42300`.
`/etc/modules-load.d/astra-display-capture.conf` loads the capture module while
its soft dependency orders the DesignWare provider first. A controlled cold
boot loaded both automatically, created `/dev/astra-display-capture`, reached
stage 8, and left the opt-in remote service inactive until explicitly started.

## Active dynamic-linking work

The executable/startup ABI is version 5. A streamed process-load transaction
can now validate and map one `ET_EXEC` program plus its supervisor-resolved
`loader.library.1` interpreter atomically. The child starts at the relocated
interpreter entry while its startup block preserves the program entry and the
exact interpreter base, span, and entry. Direct execution of a dynamic program
without that interpreter is rejected. Focused kernel and runtime tests prove
both images' bytes, entry selection, source routing, release-once behavior,
rollback, and unchanged static launch behavior.

The source-tree conversion is complete for all 24 programs installed in
`COMMANDS:`. Every shipped artifact is an `ET_EXEC` dynamic executable with
`loader.library.1` as its interpreter, exact ordered direct dependencies,
immediate binding, GNU RELRO, and no text relocations. Independent validation
passes for both unstripped and shipped images. Small commands are 9,144..26,588
bytes; Vim and zsh are 4,351,768 and 1,637,232 bytes. A stable all-command
no-op takes 3.18 seconds with zero compile, configure, link, ABI-generation, or
audit commands; the stable zsh-only no-op takes 1.55 seconds. This source state
is published as physical DE25 release `354a01b2...` described above.

Shared libraries use one page-streamed kernel load transaction and a growable
resident cache packed into the dynamic arena; the old whole-image syscall,
fifteen-entry cache, fixed 16 MiB slots, and 4 MiB VFS staging ceiling are gone.
The eager process loader uses the same positioned immutable source contract for
every dependency, so a sparse library consumes pages for its mapped extent
rather than a second contiguous copy of its file.

Library publication is now a reversible map/relocate/commit transaction.
Aborting before commit unmaps every segment and reclaims idle cache pages.
Commit validates and seals the kernel-retained GNU RELRO range with an
all-or-nothing VM rights reduction before dropping rollback authority. Every
cached dependency attach uses the same transaction and cannot bypass RELRO or
rollback. The complete kernel, VM, ELF, runtime, streams, and VFS host gates
pass this contract.

`loader.library.1` owns exact dependency resolution, combined per-thread TLS,
eager versioned relocation, main-program finalization, constructor/destructor
order, and loader-channel teardown. The kernel validates and maps bytes;
filesystem and symbol policy remain in the one user loader. There is no second
mapper or fixed dependency count.

The versioned NDK archive is now dynamic by default and carries the dynamic
CRT, all 16 registry-owned versioned `.library` files, the closed
`loader.library.1` interpreter, static self-contained archives, and the
executable-contract checker. All library owners, including the interpreter,
pass their full ABI contracts before packaging. The NDK make fragments are the
sole executable ABI and dynamic-link policy owner for archive consumers,
source-tree ports, all 24 commands, and the generated Vim and zsh vendor
builds. The source-tree
POSIX kit now supplies locations only, and a regression rejects duplicated
ABI, sysroot, PIE, binding, RELRO, or garbage-collection policy in either
adapter or command build. The 10,601,784-byte `0.1.0-dev` archive passed its
relocatability, normalized-metadata, and checksum gate. Using only its
extracted contents, native C, POSIX C, and POSIX C++ smoke programs linked to
9,684, 10,288, and 23,808 bytes and passed their exact dynamic contracts. It
also builds static smoke images of 13,792, 841,632, and 2,328,660 bytes,
proving the explicit self-contained mode from the extracted NDK. Its SHA-256 is
`28a80605da706bd1bdb048e6c7c4b8fb7588cabde2317a5df18b747ea0c3905a`.
The NDK's HTML documentation and generated PDF also pass the
warnings-as-errors public-API documentation gate. Static linking remains an
explicitly named, self-contained developer choice; dynamic linking is the
default, and Astra's production tree selects static mode only for Supervisor
and Storage because they bootstrap the loader and system volume.

Runtime, shared-library-loader, VFS-library-record, and POSIX allocations now
share one process heap. The previous loader/VFS side reservations and fixed
record slabs are gone, so POSIX cannot consume the process address space while
leaving unusable free memory outside its heap. A fresh 128 MiB filesystem image
passed the complete QEMU zsh integration gate with cached library attachment;
the former `filesystem.library cached attach:11` failure did not recur.

An initial supervisor exit is now a contained userspace failure rather than a
kernel panic. Process teardown records one retained `PROCESS_EXIT` event with
the PID, normalized status, exit reason, and initial-image flag, revokes the
owner's resources through the normal path, and presents a degraded boot-control
diagnosis. A QEMU fault-injection boot with `/startup/system` removed reported
PID 1 status `2`, reason `1`, and initial flag `1`; all five IRQ leases and all
owned devices were then revoked/reset, QEMU remained alive, and no panic was
raised. Panic remains reserved for corrupted kernel state or an invariant whose
violation makes safe continuation impossible.

The boot tail now applies that same boundary before userspace starts. A missing
or rejected initial image enters the existing interrupt-driven kernel idle
state; an unavailable AstraHost block controller, storage link, or input
controller removes only that capability and records one retained
`SYSTEM_DEGRADED` event. None of those external failures calls panic. A normal
prepared-image zsh boot passed, and a current-ROM QEMU fault injection with
`/startup/system` removed reported the supervisor failure and remained alive.
The MC68040 kernel host suite and `all verify` pass; the current kernel payload
is 181,612 bytes.

The current simplification audit has two completed consolidations and two
explicit structural follow-ups. General runtime, POSIX, dynamic-loader, and
VFS record allocation now share one process heap instead of competing virtual
reservations and private slabs. Recoverable boot failures now share one report
and one idle path instead of several panic exits. The remaining high-value
complexity is (1) the coupled fixed process/thread/address-space tables and
(2) the supervisor loader's parallel per-process bookkeeping arrays. The
correct fixes are boot-sized protected kernel metadata and one tested
per-process supervisor record respectively; merely raising constants or
moving the same parallel state behind helpers would preserve the underlying
problem. The storage/ext4 fixed arena remains intentionally separate because
it provides precharged, deterministic allocation failure during recovery.

## Build and artifact rules

Build, QEMU, FPGA, and physical work run through Beast. Source transfer is an
exact checksum mirror that excludes every generated `build/` directory and
deletes removed remote sources. Finished artifacts move separately. Remote
process completion and artifact identity are both checked; a wrapper exit alone
is not evidence.

Generated products never enter source control. Immutable releases are selected
by verified content identity, and physical acceptance requires the exact ROM,
QEMU, service configuration, storage base, and FPGA shell intended for release.
Activation first validates the installed release and its `current` selector,
then rotates the previous valid `current` release into `previous` atomically.
Only after the newly selected `current` release verifies by content identity
does deployment prune release and writable-state generations unreachable from
`current`, `previous`, or explicit `by-boot` selectors. This is the permanent
retention policy; an unbounded release archive on the board is not permitted.

Linker export/version definitions are source and use the tracked `.exports`
suffix. Generated linker reports may use `.map`; the repository does not
globally ignore that suffix, because doing so previously allowed an essential
ABI input to exist only as an untracked stale file. Shared-library builds and
their documentation gates depend directly on the tracked export definition.
