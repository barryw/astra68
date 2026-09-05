# DE25-Nano Timing Closure

## 2026-09-02: qualified vendor baseline

- Host: `beast`
- Board: DE25-Nano USB serial `TRWJGOUZ`
- Device: `A5EB013BB23BE4SCS`
- Quartus: Pro 26.1.1 Build 130
- Vendor source: `DE25-Nano_GHRD_QP25.3.1.qar`, SHA-256
  `5da65306fbb1689fb12c9d4bd3721e6be683192e899315df55718eb5df6083e5`
- Required migration: Quartus-supported upgrade of every IP component from the
  25.3.1 archive to 26.1.1 before synthesis.
- Build: `fpga/de25/build_vendor_baseline.sh` restores into a staging
  directory, verifies the pinned source inputs, performs the supported IP
  upgrade, compiles, gates timing, hashes the products, verifies those hashes
  before and after atomic publication, and only then replaces the retained
  checkpoint.
- Result: fit, assembly, fully constrained multicorner timing, HPS programming
  file generation, JTAG programming, and HPS SD/UART boot all pass.
- Resources: 9,160 / 46,800 ALMs; 18,358 registers; 2,098,688 / 7,331,840
  block-memory bits; 131 / 358 RAM blocks; 0 DSPs; 2 / 11 PLLs.
- Timing: setup slack +1.662 ns; hold slack 0.000 ns with zero failing
  endpoints. Setup and hold are fully constrained.
- Output identities from the retained reproducible checkpoint:
  - `golden_top.sof`:
    `33bcb4d0337b79d00e3a6eb6d9185b1deac5a9ae252d822951a3a5b4dfe64d15`
  - `golden_top_hps.sof`:
    `0b00f0da071b2079edb512e1fd69080e788791d1bea99100d1c7fffedbf80b13`
  - Quartus programmer checksum: `0x06304EE6`
  - running JTAG design hash:
    `DA18469719F071C3A242520C2C19D4CF9F12B44D37136703A38AFE5273D4D554`
- Physical boot evidence: LPDDR4A calibration and 1,024 MiB size check pass;
  U-Boot loads the kernel and device tree from SD; Linux 6.12.11 brings up all
  four ARM64 cores, recovers and mounts the ext4 root, starts networking and
  SSH, and reaches the serial login prompt.

Failed checkpoints retained:

1. Direct 26.1.1 compilation of the untouched 25.3.1 archive failed because
   HPS, HPS EMIF, and on-chip-memory IP required upgrade.
2. The first upgraded fit failed because `quartus_agilex5e` was installed but
   the local Quartus license file was not supplied to the tool process.
3. Cleaning the vendor `output_files/` also removed its required HPS SPL hex.
   The build now verifies that input, moves it out of the generated-output
   directory, and only then cleans.
4. The first automated publication wrote staging-directory paths into its
   checksum manifest. Verification from the published directory rejected it.
   The producer now records relocatable paths and verifies them on both sides
   of the atomic rename.

The vendor baseline is an integration oracle, not the Astra platform. It uses
HPS LPDDR4A, leaves FPGA LPDDR4B disabled, and contains demo OCM/JTAG/peripheral
logic that the Astra shell will replace.

## 2026-09-02: AArch64 Astra runtime gate

- Physical HPS: Ubuntu 22.04, AArch64, glibc 2.35, four cores, 1 GiB LPDDR4A.
- Build host: Beast, using an Ubuntu 22.04 AArch64 sysroot rather than Beast's
  Ubuntu 24.04 cross-libc headers and libraries.
- QEMU 9.2.4 SHA-256:
  `52ed189975d15019d696f99635081016499d989f549f4d9b55804ea172c5744e`.
  Its newest required libc symbol is `GLIBC_2.34`.
- Astra ROM SHA-256:
  `308b170b14113c9c327776ad01f607b767e561567c6904869877390ba27d0614`.
- Prepared 64 MiB image SHA-256:
  `4050e73454f43e171fbce52f409946018dec2c94d474f03a5c5d38d0068c7337`.
- The Terminal executable extracted from that image matched the current build
  at SHA-256
  `d25f29d03073beb9a63e8a3c01cd92cc690f6af006643e6e223b5ae5016b4369`.
- Physical result: the focused performance gate and complete 70-command gate
  pass on the DE25 HPS. The complete run covered durable storage, VFS and
  namespace operations, POSIX TCP fork/exec, Lua, process reporting, events,
  redirects, configuration assigns, and host-synchronized year 2026. Measured
  Enter-to-ready command completion was 0.29--4.28 seconds.

Two failed checkpoints are retained because they changed the release gate:

1. The first AArch64 binary linked against Beast's glibc 2.39 development
   environment and required `GLIBC_2.38`; the board rejected it. The DE25
   profile now requires the Jammy sysroot, uses its headers and libraries, and
   keys its build directory by the build contract.
2. Output silence was previously treated as shell readiness. The Cortex-A55
   run proved that a command can print before its process is reaped, and an
   `events:` query can echo an old readiness string. The shell now emits a
   structured informational ready event after drawing each prompt, and the
   gate matches the exact live event record rather than text or elapsed time.

The active hardware blocker is now the Astra FPGA shell and LPDDR4B graphics
integration; the retained vendor route remains the board and HPS oracle.

## 2026-09-03: first Astra LPDDR4B shell fit

- Host/tool: `beast`, Quartus Pro 26.1.1 Build 130.
- Source: exact vendor GHRD and DE25 resource archive pinned in
  `fpga/de25/platform.json`; the build rejected hashes taken from a mutable
  exploratory extraction and now verifies immutable ZIP members.
- Architecture: HPS full AXI directly to a 1 GiB EMIF window at `0x40000000`,
  with the vendor calibration driver and no Astra data-path logic between the
  HPS and EMIF.
- Synthesis passed with zero errors and 33,669 pre-fit resources.
- Fit failed before placement with four illegal EMIF byte lanes. The selected
  `Qsys_emif_io96b_lpddr4_0.ip` is bank A despite its name; Terasic's own
  `Qsys.qsys` maps bank B to the confusingly named
  `Qsys_emif_lpddr4a_0.ip`. The failure is retained because it establishes the
  exact physical cause rather than a placement or timing problem.
- Disposition: select the bank-B component by the vendor system mapping and
  immutable member hash `e0e37791846ef287f91776dd8a781ec5729b6d3351cd57f9883f47b28145c2c0`,
  then rerun the unchanged production fit gate.

## 2026-09-03: routed Astra LPDDR4B shell

- Host/tool: `beast`, Quartus Pro 26.1.1 Build 130.
- Inputs: pinned vendor GHRD SHA-256
  `5da65306fbb1689fb12c9d4bd3721e6be683192e899315df55718eb5df6083e5`
  and pinned DE25 resource archive SHA-256
  `554af27603855b37840e930bd075c34141c85d2f753cfa45df2eb5114f0f4c73`.
- Correct bank-B component: immutable archive member SHA-256
  `e0e37791846ef287f91776dd8a781ec5729b6d3351cd57f9883f47b28145c2c0`.
- Result: synthesis, fit, route, signoff timing, assembly, HPS programming-file
  generation, staged checksum verification, and publication pass with zero
  errors.
- Resources: 11,940 / 46,800 ALMs; 24,709 registers; 2,107,072 / 7,331,840
  block-memory bits; 138 / 358 RAM blocks; 0 DSPs; 3 / 11 PLLs.
- Timing: setup slack +1.341 ns; hold slack 0.000 ns with zero failing
  endpoints. Setup and hold are fully constrained and timing requirements are
  met.
- Design Assistant: zero high-severity violations. The three medium findings
  are one generated interconnect reset-polarity conflict, the vendor system's
  multiple reset synchronizers in the 100 MHz domain, and a min-only delay in
  the generated LPDDR4B IP SDC. They remain visible and unwaived.
- Retained output identities:
  - `golden_top.sof`:
    `6254c0c0091662c58ae47a01a9ff0f995766c20fa50838c8a50af455d9843224`
  - `golden_top_hps.sof`:
    `df1d8cd90d9748f0708d565664a525d519daf290468acf5ec3351eb560608e1d`
- The generated `qsys_top.sopcinfo` and Quartus
  `sopc-create-header-files` output independently identify the HPS-visible
  LPDDR4B window as base `0x40000000`, span `0x40000000`, end `0x7fffffff`.
- This checkpoint proves implementation and address-map closure, not physical
  memory operation. The next release gate is software-readable calibration
  status followed by physical calibration and destructive full-window memory
  tests on the DE25.

The first status-register rebuild failed during elaboration because the patch
used the nested-subsystem PIO port suffix `external_connection_export` for a
directly exported top-level PIO. Platform Designer's
`get_interface_ports astra_lpddr4b_status` command returned the authoritative
port name `astra_lpddr4b_status_export`; the binding and its regression
contract now use that generated name.

## 2026-09-03: routed shell with software-readable calibration status

- Host/tool: `beast`, Quartus Pro 26.1.1 Build 130.
- Inputs and bank-B component are unchanged from the routed shell above.
- Result: synthesis, fit, route, signoff timing, assembly, HPS programming-file
  generation, staged checksum verification, and publication pass with zero
  errors.
- Resources: 12,254 / 46,800 ALMs; 25,205 registers; 2,107,072 / 7,331,840
  block-memory bits; 138 / 358 RAM blocks; 0 DSPs; 3 / 11 PLLs.
- Timing: setup slack +1.380 ns; hold slack 0.000 ns with zero failing
  endpoints. Setup and hold are fully constrained and timing requirements are
  met.
- Design Assistant: zero high-severity violations and the same three visible,
  unwaived medium findings as the prior route.
- The HPS lightweight bridge exposes a 3-bit read-only PIO at relative address
  `0x20000`: bit 0 is calibration passed, bit 1 is calibration failed and
  latched, and bit 2 is the EMIF data interface ready. The official generated
  headers give it span 16 and end `0x2000f`.
- The same generated headers retain the LPDDR4B data window at base
  `0x40000000`, span `0x40000000`, end `0x7fffffff`.
- Retained output identities:
  - `golden_top.sof`:
    `9b9a6f456d71d780073d925e7b7920f7b28d44a4fd225ba30cc746efedb88fd2`
  - `golden_top_hps.sof`:
    `e0b19e35ff5083b2672a08e63722b0f3f76032e003e2ae01f9f570c42c77948b`
- The remaining gate is physical: cold-boot the HPS, derive the lightweight
  bridge's physical base from the running board device tree, program this exact
  SOF, require calibration pass and data-ready, then destructively verify the
  full LPDDR4B window. No memory access is permitted before readiness.

The first hardware load used `golden_top_hps.sof` at JTAG device 2. Quartus
reported programming checksum `0x0641F945`, running design hash
`001F9CDE36D0A705DAB8F0FEA0FCAC61652A15BFD8A13C1F5F727DB7CD48631D`,
and successful configuration with zero errors. Loading the HPS-bearing image
stopped the already-running Linux host at `192.168.1.52`; it did not reboot the
HPS. The serial console remained silent. This is not a memory-test failure.
Disposition: cold-boot the existing SD host, load the fabric-only
`golden_top.sof`, verify that Linux remains reachable, then perform the gated
status and LPDDR4B tests. Do not use HPS reachability as an implicit result of
fabric programming.

That disposition was tested and rejected with direct evidence. Quartus PFG
converted the exact HPS-bearing SOF to `golden_top.rbf` at SHA-256
`b38b877bd1daf0e2f94db8658d50928a313f1165161aa3a4b039ad918ab127d7`.
Programming that RBF preserved the HPS/CoreSight chain and running Linux, but
the reconfiguration disabled the HPS-to-FPGA bridges and interrupted the HPS
Ethernet path. Restarting NetworkManager restored Ethernet; a root `/dev/mem`
read of the status PIO at physical address `0xff420000` then raised `SIGBUS`.
The address is the generated `0x20000` offset plus Agilex 5's documented
`0xff400000` lightweight-bridge base. The SD boot script independently shows
the required order: `bridge enable` runs before Linux starts. Therefore the
fault is a disabled bridge, not a calibration result, and post-boot JTAG
reconfiguration is not the production path. The retained boot contract must
load the Astra RBF before U-Boot enables the bridges.

The board also contained runtime residue from its previous Ultimate128 role.
The disabled `c128-input.service`, five `/usr/local/bin/c128-input*` binaries,
`/var/lib/ultimate128`, the Ultimate128 Avahi advertisement, and all matching
boot backups were removed. The active DTB was restored to its clean
pre-Ultimate SHA-256
`d9bd893fc94e45359fb442ca06741aa63c5881e9376473fb9397ebf6d6be4d13`,
and the boot script to SHA-256
`b38747a4b43abcc04d84b8f46ea514594e9cc59c8188633e4d24ba36da3728ef`.
The board is now named `astra68`. A warm Linux reboot stopped after the HPS
reset request and required reloading the verified vendor HPS SOF, checksum
`0x06304EE6`, to cold-start the HPS. The recovered boot used no `mem=` override,
reported only the vendor `svcbuffer@0` reserved-memory node, exposed 934 MiB to
Linux, started no retired service, brought up networking and SSH, and reached
the `astra68` serial login. The warm-reset limitation is separate from the
removed software and remains part of the production cold-boot gate.

## 2026-09-05: instrumented production route and first stalled capture

- Host/tool: `beast`, Quartus Pro 26.1.1 Build 130. The exact-mirror source
  build passed the complete DE25 contract suite, exhaustive graphics/audio
  simulation, synthesis, placement, route, signoff timing, assembly, staged
  checksum verification, and atomic publication.
- Architecture: Intel Performance Monitor 4.0.1 is a 128-bit AXI4
  pass-through between `subsys_hps.hps2fpga` and LPDDR4B, with 48-bit counters,
  advanced latency support, and its official System Console JTAG endpoint.
- Resources: 43,863 / 46,800 ALMs; 3,818,904 / 7,331,840 block-memory bits;
  290 / 358 RAM blocks; 58 / 376 DSP blocks; 5 / 11 PLLs.
- Timing: setup slack +0.743 ns; hold slack 0.000 ns; zero TNS and zero failing
  endpoints. Setup and hold are fully constrained and timing requirements are
  met.
- Design Assistant: zero high-severity violations in the final snapshot.
- Retained output identities:
  - SD core RBF:
    `4141732e603b4ed10f88333d2529e9015495e32c603deb97d74a2286d7ac25c0`
  - HPS JIC:
    `4189b10a5cf211299ba686b4629342e04ce681217d6617f20173b373afaf6144`
  - full SOF:
    `4e7601a94586488960ff818bbecf4fd30870293488378b637d161b7b0b082add`
  - boot script:
    `c3b2c12bca140b889058668fad454bf238b64199c62dd794efb780a75e2d50ca`
  - source-manifest file:
    `d33c2a09a89a8b52ef5dbfbe85054fc460f460dac9928262408fc3d6faa4c4eb`
- The board verified the complete boot-bundle manifest before the installer
  atomically replaced the RBF and boot script. A warm reboot loaded the new
  image, returned Linux to the serial login, and restored `192.168.1.52` with
  0.2--0.4 ms host latency. This is one successful warm reboot, not yet grounds
  to remove the earlier failed warm-reset observation.
- Astra autostart was disabled by removing the single identified
  `multi-user.target.wants/astra.service` symlink. This prevents the known
  workload from racing Linux recovery boots; the unit itself and its data were
  retained.
- Hardware workload: AArch64 probe SHA-256
  `7fde94a5ab304d3304f0c03ac1ec0cfd0b4c004436250d85dbc14cb341b77f42`
  copied and displayed one 1280x720x16-bit frame, then issued volatile 64-bit
  reads across 1,843,200 bytes. Before the HPS read, the framebuffer endpoint
  reported 80 accepted AXI bursts, 1,280 accepted response beats, and zero
  response-stall cycles. The HPS workload did not reach its first 64 KiB
  progress report and Linux stopped answering Ethernet and serial input.
- The official PMON diagnostic capture was frozen during the failure. It
  reported 357 AR transactions, 357 R transactions, and 357 expected R
  transactions; 115,229 AW, W, B, and expected W transactions; 4,997,468
  traffic cycles; and no counter overflow. Every transaction accepted at this
  monitored boundary completed. The long Linux stall therefore occurs with no
  missing accepted LPDDR4B response; an upstream request blocked before its
  handshake remains possible.
- The serial kernel log subsequently identified CPU 1 as stalled and reported
  starvation of the RCU grace-period thread plus a possible timer-softirq
  handling issue on CPU 2. Magic SysRq produced no additional backtrace while
  the machine was stalled.
- Disposition: on the next recovery boot, use PMON's read-only efficiency and
  backpressure configuration on the unchanged routed image, reproduce the same
  workload, and freeze the counters. Do not alter the memory data path until
  that capture distinguishes AR acceptance backpressure from response-side
  backpressure.

## 2026-09-05: out-of-order production route and physical qualification

- Root cause: Platform Designer's HPS and graphics master connections into
  LPDDR4B did not enable the interconnect's native out-of-order support. The
  retained change sets `qsys_mm.enableOutOfOrderSupport` on the PMON path and
  the framebuffer, scene, and render connections. No custom ordering adapter
  was added.
- Host/tool: exact-mirror source on `beast`, Quartus Pro 26.1.1 Build 130.
  The complete DE25 contract, graphics/audio simulation, synthesis, fit,
  route, signoff, assembly, Design Assistant, staged hashes, and atomic
  publication passed. `BUILD_SHA256SUMS` has SHA-256
  `3b94e21eef925df7e56260e8d3a4f3b1f1a06a846351254b998504ef0b1f8e42`.
- Resources: 41,860 / 46,800 ALMs; 3,818,968 / 7,331,840 block-memory bits;
  290 / 358 RAM blocks; 58 / 376 DSP blocks; 5 / 11 PLLs.
- Timing: setup +0.671 ns, hold 0.000 ns, recovery +2.661 ns, removal
  +0.024 ns, and minimum pulse width +0.220 ns. Every production clock is
  fully constrained and meets timing. Design Assistant reports zero
  high-severity violations.
- Retained identities:
  - SD RBF:
    `2486427470514318a8f669f61dbeb766b2f18501eb6569b0fa13562cda0ce8c4`
  - HPS JIC:
    `c61af281078482027fdbd0c0a94130bee83e11daba8acf1abe5814ae344c18df`
  - full SOF:
    `633a7bfa76139900cba5e9a1bc4c490fcef3170fe636558b0a5c8be18353d562`
  - HPS SOF:
    `c102e120e4278269d32f4666cf2f32bf02e25a9ba0b86b75b8a100a7c9a26417`
  - generated Platform Designer system:
    `1b0bcb19ed9d0fa2a608979ec138c6b95b48fced56ece4f684f454ee2aaced5e`
  - Quartus programmer checksum: `0x073F0338`
- A stale programmed shell was caught before testing: System Console reported
  physical design hash `06CFF481957C313BF2C4`, while the retained SOF was
  `B685A4711883D751E0A6`. `verify_running_shell.sh` now verifies the complete
  build manifest, loads the exact SOF metadata, discovers exactly one DE25,
  and requires `design_link` to the physical JTAG device. This gate passed
  after the retained RBF was installed and again after a physical power cycle.
- Physical contention gate: 30 alternating same-address/cross-address tests,
  one initial four-core test, and five more four-core rounds completed 54
  workloads and 99,532,800 bytes with zero hangs. PMON counters 0, 1, and 5
  each reported `0xbdd800`, exactly 12,441,600 response beats and exactly
  54 times the expected 230,400 beats. No counter overflow occurred.

The first production runtime then failed independently in the AArch64 Linux
terminal-display helper. A captured core showed `SIGBUS` at `__memset_zva64`,
PC `0x4143c0`, on `dc zva, x3` while clearing the `/dev/mem` framebuffer.
Device-mapped memory cannot use libc's cache-line zeroing path. The shared
graphics hardware library now owns device-memory fill and copy primitives;
all device-memory callers use them. AArch64 disassembly proves their aligned
loops contain explicit `str`/`ldr` instructions with no libc call and no
`DC ZVA`.

The exact fixed terminal display has SHA-256
`fe62b9c45b45aa493e6eb4f3fd0554d95f54f0c2b168ac1140238f7c7a78fb87`.
Immutable release
`d7b5b035315219fcdc47bd34fa1c2726685904c9663e68a06c09533f01204344`
verified before and after installation. It reaches `POST PASS`, keeps
`ASTRA_TERMINAL_DISPLAY READY` resident, recovers and verifies storage, starts
hostfs/network/NTP in order, and reaches stage 8. The complete 70-command gate
passes on the DE25 against this release.

Three consecutive physical hardware sweeps pass the full splash transfer and
CRC32 `611029ee`, all sprite phases, the complete renderer/blitter/geometry/
AFNT/flood/compositor suite, dual-bank copper, and 48-kHz HDMI audio. Every
sweep reports zero AXI errors and zero renderer backpressure. Remaining release
gates are a second cold boot of this installed runtime and direct physical HDMI
observation; the Arty remains the active rollback machine until both pass.

The next physical power cycle passed `design_link` against the same exact FPGA
shell and started release `d7b5b035...`, but correctly failed the wall-clock
release criterion. Linux began Astra at `2026-09-05T15:27:23Z`; its first NTP
synchronization occurred at `15:55:35Z`. `network-online.target` had completed,
but `time-sync.target` only established service ordering and did not represent
completed synchronization on this Ubuntu image.

The first proposed correction polled `timedatectl`'s `NTPSynchronized`
property. A physical negative test rejected it: disabling NTP left that
property at `yes`, so it is a historical synchronization state rather than a
reliable completed-sync barrier. No custom polling code was retained.

The DE25 unit now uses systemd's existing synchronization primitive instead:
it pulls in `systemd-time-wait-sync.service` and orders Astra after that service
and `time-sync.target`. The native service waits with
`TimeoutStartSec=infinity` for the kernel clock synchronization state. The
installed unit SHA-256 is
`f7fec4c9bdbd6d17364061a5a06a1f7f5f4f7822b462a4120ba8d71bd0d8a6bb`;
the byte-verified runtime remains
`d7b5b035315219fcdc47bd34fa1c2726685904c9663e68a06c09533f01204344`.
A service restart through the active native barrier started Astra at
`2026-09-05T16:04:54Z`, matching the synchronized host clock. The remaining
cold-boot gate must prove the same ordering from power-on.

The final physical cold boot passed that gate. The wait service began while
the board still reported its stale 2023 RTC, then systemd-timesyncd performed
its initial network synchronization at `2026-09-05T16:11:25Z`. The infinite
wait service completed and Astra started in the same second, proving there was
no pre-NTP guest execution. POST passed at `16:11:29Z`; Axiom reported wall
clock `2026-09-05T16:11:29Z`. Storage block round-trip, partition discovery,
journal recovery, write/read verification, hostfs, network, NTP, and stage 8
all passed. The terminal display and QEMU remained resident with no Linux
stall, AXI error, `SIGBUS`, or kernel warning.

The exact-shell `design_link` gate passed again after this power cycle. The
installed release and unit retained identities
`d7b5b035315219fcdc47bd34fa1c2726685904c9663e68a06c09533f01204344`
and `f7fec4c9bdbd6d17364061a5a06a1f7f5f4f7822b462a4120ba8d71bd0d8a6bb`.
The live display mailbox recorded a successful desktop render request and
completion at graphics generation 4. Live register reads reported device ID
`0x41535452`, version `0x00010006`, capabilities `0x000003ff`, graphics
generation 4, arena `0x40000000..0x7fffffff`, scanout base `0x40200000`, pitch
2560, size 1280x720, and framebuffer control 3. Commit errors, response-stall
cycles, renderer failures, and renderer backpressure were all zero.

The HDMI control path also passed directly on the physical shell. With the
link request asserted, status became 3: requested and transmitter-ready after
the vendor I2C configuration completed with acknowledgements. Clearing the
request returned status to zero. Combined with the exact-shell gate, complete
cold boot, live scanout, terminal residency, and three exhaustive hardware
sweeps, this closes the DE25 migration release gate without an external video
capture device. The DE25 is the active Astra machine; the Arty is the rollback
platform. The next independently measured project is the MC68040 migration.
