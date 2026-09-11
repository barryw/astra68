# Astra 68 current engineering state

Status: active continuation map, 2026-09-10

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
setup, hold, recovery, removal, and minimum-pulse slack are +0.009 ns, 0.000 ns,
+2.479 ns, +0.032 ns, and +0.220 ns. Resource use is 43,611 / 46,800 ALMs,
4,183,520 / 7,331,840 block-memory bits, 340 / 358 RAM blocks, 59 / 376 DSPs,
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

Media RAM is a service-owned arena, not raw process memory. Applications send
validated draw lists through GUI IPC; only the display service holds the
exclusive display lease and chooses media offsets. Scanouts use two 4 MiB
slots, the render batch and scratch workspace occupy 8..16 MiB, window caches
occupy 16..32 MiB, and window content occupies 32..48 MiB. Compile-time layout
proofs and the shared render builder's per-slot capacity check prevent a
surface from crossing those boundaries. Temporary render surfaces begin after
the 256 KiB batch rather than at its base.

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
release. DE25 release creation now force-rebuilds this small renderer from the
current mirrored source, so a caller cannot package an older release's copy.

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
under ASan/UBSan, GCC analysis, and in the MC68040 target build. Dynamic
window/resource storage and a batched hardware scene descriptor remain the
next compositor work; the current four-window service table is not an accepted
product limit.

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
object; Terminal uses both for pointer-drag selection. Clipboard transfer,
scrollback, find, wide cells, and code/flow layout remain pending. The current
AFNT bitmap/Amiga importer remains operational; the
documented scalable TTF/OTF/WOFF pipeline and persistent synthetic-strike cache
are not yet implemented.

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
source derives four 4 MiB process DMA slots and their 4,096-page aggregate
budget from that requirement. The anonymous private window correspondingly
spans 496 MiB; allocation remains demand-paged and owner-charged.

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
`host.fs.open.calls`; the command completed in 0.27 seconds in that gate.
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
