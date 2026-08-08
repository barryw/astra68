# Axiom kernel status

Status date: 2026-08-04

This is the kernel-specific truth table. `CURRENT_STATE.md` remains the whole
machine continuation map. A row marked CURRENT has evidence; PLANNED or MISSING
must not be presented as working software.

## Unqualified driver-substrate candidate

- CURRENT SOURCE: fixed registry and exclusive generation-safe leases; 8
  devices, 8 leases, and 2 leases per process.
- CURRENT SOURCE: trusted grant, read/transfer/administer rights, and
  query/reset/revoke plus bounded physical-input reads at ABI `0x00010007`.
- CURRENT SOURCE: owner death revokes, quiesces, and resets before handle
  closure; failed recovery contains the target as `FAILED`.
- TESTED ON BEAST: device tests, process/syscall integration, malformed
  syscall coverage, and the freestanding MC68030 link.
- NOT YET QUALIFIED: full gates, emulator/Arty execution, performance,
  physical registrations, discovery, or userspace service protocols.

## Input transport candidate

- CURRENT SOURCE: stable 20-byte keyboard/pointer records, bounded Vesta FIFO,
  sticky overflow, independent device sequences, host generation, and input
  IRQ source 5.
- CURRENT SOURCE: QEMU accepts QMP or Linux evdev keyboard/pointer events and
  maps physical keys to USB HID Keyboard/Keypad Usage IDs.
- CURRENT SOURCE: Axiom conditionally registers the input controller in its
  sealed device registry and provides bounded quiesce, reset, drain, and event
  delivery through an exclusive lease. Reads are capped at 16 records, preserve
  a record across copy faults, and report plus acknowledge sticky overflow.
- TESTED ON BEAST AND ARTY ARM: `emu/qemu/test-input.py` passes ordering,
  encoding, queue-limit, overflow, and IRQ tests against the exact QEMU 9.2.4
  builds.
- CURRENT SOURCE: allocation-free input service core, replaceable keymap with a
  built-in US map, modifier/lock state, bounded repeat, integer pointer
  acceleration/clipping, focus routing, reset repair, and fixed-size
  application-port messages.
- TESTED ON BEAST: functional tests, ASan/UBSan, GCC `-fanalyzer`, MC68030
  cross-build, and the complete kernel host regression suite pass. Target size
  is 3,095 bytes of text and 368 bytes of caller-owned state.
- NOT YET QUALIFIED: physical keyboard/mouse evdev events, protected-process
  launcher/runtime integration, live application ports, non-US keymaps,
  composition/IME, or display-service focus authorization.

## Userspace runtime foundation

- CURRENT SOURCE: 64-byte versioned startup information and bounded 92-byte
  initial-capability records (`ASTRA_STARTUP_ABI_VERSION` 2) in
  `sw/include/astra/process.h`, each carrying a 64-byte mount-relative root a
  child's namespace binds at. `docs/ABI.md` and `docs/HANDOVER-union-assigns.md`
  are current; the rest of this entry predates process launch and a VFS.
- CURRENT SOURCE: separate MC68030 `crt0`, one C-callable `TRAP #15` veneer,
  typed query/yield/close/exit wrappers, startup validation, and freestanding
  memory/string primitives in `libastrart.a`.
- TESTED ON BEAST: warnings-as-errors host tests, ASan/UBSan, generated m68k
  veneer inspection, and the canonical `-m68030 -msoft-float` cross-build pass.
  Archive objects total 660 bytes of text; separate `crt0` is 46 bytes, with no
  data or BSS.
- NOT YET IMPLEMENTED: ELF acceptance/loading, supervisor executable,
  transactional process launch, heap/VM growth, stdio, VFS bindings, or a
  standards C library. The runtime is a foundation, not a running service.

## Userspace storage foundation

- CURRENT SOURCE: bounded synchronous block facade with 64-bit LBA validation,
  512-4096-byte sector geometry, media generations, read-only/presence state,
  deadlines, and per-operation call/failure/sector/total/max metrics.
- CURRENT SOURCE: caller-owned memory/image backend with deterministic failure
  injection and media removal/reinsertion. It is the reference backend for
  filesystem tests and emulator images, not an on-disk filesystem.
- TESTED ON BEAST: 100,000 deterministic randomized reads/writes/flushes verify
  847,243 sectors against an independent 2 MiB oracle. Warnings-as-errors,
  ASan/UBSan, GCC `-fanalyzer`, and MC68030 cross-build pass. Target text is
  1,492 bytes with zero data/BSS; final host averages are 84 ns read and 74 ns
  write facade overhead.
- NOT YET IMPLEMENTED: block-device userspace admission, Arty QEMU image
  backend, filesystem handler, VFS, protected storage service, or shell file
  commands. `STORAGE_AND_VFS.md` defines the retained route and release gates.

## Current source identity

- Branch: `main`.
- K10 device and observability is an unpromoted pre-route candidate in the
  current working tree. Its exact implementation identity will be frozen
  before routing. All 28 host suites, analyzers, sanitizers, MC68030
  cross-build, NDK, Rust, shared architecture, Harte smoke, focused
  USB/bus-fault RTL, and all 29 hardware-checker tests pass. Fresh Beast
  Verilator 5.047 snapshot `/tmp/astra68-k10-final-sim-20260727b` completes the
  full HDMI-enabled pin-level run in 2,051.123 seconds and exits zero after
  `ASTRAHOST KERNEL ENTRY PASS`. Its 154,905-byte durable transcript is
  `/tmp/k10-final-hostboot-20260727b.log`. It covers 32 MiB BIST, PMMU,
  scheduler/fault containment, K10 source mask `0x000003B0`,
  delivered/acknowledged `5/5`, one exact owner-death cleanup, and
  AstraHost-SPI monitor output. Removing only Verilator's `UART: ` transcript
  prefix makes that output pass `--expect-k10-device`; raw board serial does
  not contain the prefix. The exact committed rerun, no-waiver production
  route, two ULX3S boots, physical HDMI, and interactive FTDI/AstraHost-SPI
  monitor checks remain pending. K9 therefore remains the current
  hardware-qualified release.
- The current working tree also adds the native INDEX8 boot splash and Astraea
  MASK1 status text. It is based on
  `4579138eb3d30e26aef658252fba28b15dd33420`; the exact 25-file source
  manifest is
  `docs/evidence/astra68-splash-source-manifest-20260727.sha256`, SHA-256
  `c53e68b7f9be69917c07aa31a66a9b78552254013b822bf889974c52fbd026d1`.
  The earlier `320CAE59` source routed without a timing waiver but failed its
  first ULX3S scene presentation because a routine Vega baseline copy rejected
  shadow edits and the submit. That bitstream is rejected. The fixed Beast
  snapshot `/tmp/astra68-splash-hwglyph-vega-lock-20260727` arbitrates shared
  palette/descriptor RAM writes while leaving the independent shadow scene
  editable. Its forced-overlap Vega test, full directed graphics suite, and
  exact release-ROM TG68K/SDRAM/HDMI boot all pass; the latter uses the
  reproducible 223,004-byte, CRC32 `84E611A6` ROM and completes all 24 hardware
  glyph jobs and both framebuffer pixel checks. Exact fixed build `C53E68B7`
  now routes the complete production feature set on Beast with zero SCCs and
  no waiver: 67,295 TRELLIS_COMB, 26,024 TRELLIS_FF, 103 DP16KD, and 18
  MULT18X18D cells, with 14.127087 MHz CPU and 67.971725 MHz SDRAM achieved.
  Bitstream SHA-256 is
  `9c6a1f575596bf612fa9649940a3c3a65758aa7e55684cebf3b42ba62c576b46`.
  Repeated SRAM-only ULX3S boots and physical HDMI qualification remain.
- K9 memory-pressure hardening is hardware-qualified from exact implementation
  commit `03660014d7af6d3662504fc076700f04929117ab`, built reproducibly at
  `2026-07-26T02:43:12Z`. The immutable source archive SHA-256 is
  `db884481ef58f27ed4be2823c57a43089b24d140c160d1f512b89f89547151a5`
  and is extracted as `/tmp/astra68-k9-0366001` on Beast and NUC.
- K9 kernel: 90,132 bytes, SHA-256
  `6d9397b044e133bb9e04750d78cd46e3b32ea1e418d553e44c9e97f958b6d823`.
  The 124,028-byte ELF SHA-256 is
  `293261fef2378ae767d3f1f6edb4730679fd40ccec28a2dcc8ab944366d959b0`.
- K9 normal boot payload: 101,544 bytes, CRC32 `8E57D4DA`, SHA-256
  `996f2305477067b2793e678ec93a8a536aba10c677b343d747a05f22d445456d`.
  The 101,576-byte packaged ROM SHA-256 is
  `989e02cdea3722eb7a53037815d6d6bf6b67f97869f29c46cab354473e1bcddd`.
  K9 passes all 21 host suites, analyzers, sanitizers, NDK and MC68030 gates,
  generated HTML/PDF documentation, Rust gates, shared conformance, normal and
  performance Musashi, a clean full pin-level RTL run, an exact no-waiver
  production route, SD provisioning, AstraHost restoration, two independent
  ULX3S boots, and physical HDMI. K9 is current and K8 is its rollback.
- K8 shared areas and bounded bulk rings are hardware-qualified from exact
  implementation commit `56bd1770c834205a4dccc42efb61552a77647988`, built
  reproducibly at `2026-07-25T23:59:00Z`. The immutable source archive
  SHA-256 is
  `b0db820437d5526b9157815c5a280770e59e21930c39d48dddb6d21389d5cb49`
  and is extracted as `/tmp/astra68-k8-56bd177` on Beast and NUC.
- K8 kernel: 81,768 bytes, SHA-256
  `ea879e760c48342f535ee9aee65bf1bab97e855c2e576579c2ab80ef615ba55b`.
  The 112,736-byte ELF SHA-256 is
  `81a8b2a65ef411c7fe77f5493f485e764e7c4b5331756055e56aea4e7104a3f5`.
- K8 normal boot payload: 93,180 bytes, CRC32 `BE5F5D5D`, SHA-256
  `a70ae3323884ffb3eccaa70b4e4c6be34a2e0a4aa044cee99a632375b6faba2a`.
  The 93,212-byte packaged ROM SHA-256 is
  `ae6bab5ab9a249211ef0b7f1daccb7e00ddb9e683facbe7ecfd7b0d6307d17a8`.
  K8 passes all 20 host suites, analyzers, sanitizers, NDK and MC68030 gates,
  generated HTML/PDF documentation, normal and performance Musashi, a clean
  full pin-level RTL run, SD provisioning, AstraHost restoration, and two
  independent exact ULX3S boots. K8 is the qualified K9 rollback; K7 is its
  predecessor.
- K7 bounded message ports are the hardware-qualified rollback from base commit
  `1529496c168975cee0fc46c7955f98ab4a1b8d2b` plus implementation patch
  SHA-256
  `815347c8d094a1507b94ac5f8acb7636903d8eaf6fe8457e2f7a0d641763906e`.
  The ROM reports
  `1529496c168975cee0fc46c7955f98ab4a1b8d2b-dirty-815347c8d094` and was
  built reproducibly at `2026-07-25T20:20:47Z`. The exact source archive
  SHA-256 is
  `79965cebb6da2aefb435d655ee279cd37f8f98bb1fb1dc4ef7c5efa53ebe6e09`;
  it is extracted as `/tmp/astra68-k7-815347c8d094` on Beast and NUC.
- K7 kernel: 69,496 bytes, SHA-256
  `4c9d3807c95a701d8e2f16f52b1321fd97c0393798656ea589e6197c9dfccd4e`.
  The ELF SHA-256 is
  `12c39d37b17a2389940074462e56d48e408ad2f88f47a1ef4b5e24242d620c02`.
- K7 normal boot payload: 80,944 bytes, CRC32 `B124CB22`, SHA-256
  `16d22cf05570e94ee03571705d55934ed66060616935e9590724f40afba17e21`.
  The 80,976-byte packaged ROM SHA-256 is
  `4abbc4471f8a84eaec770cab3758f27cb98e1a0768c6ef85295033834b70fd81`.
  Its hash matches after transfer to Mac and NUC. K7 passes the exact host,
  Musashi, 1,000-iteration performance, fresh full pin-level RTL, SD
  provisioning, AstraHost restoration, and two independent ULX3S gates.
- K6 bounded wait-multiple is hardware-qualified from base commit
  `0208cb516801fe452bf59ef053d6daa0a118ee7e` plus implementation patch
  SHA-256
  `04524898314df4adebfe5091a183f2130e7c8bf98a3dd7a7d97e01b5b1fe1505`.
  The ROM reports
  `0208cb516801fe452bf59ef053d6daa0a118ee7e-dirty-04524898314d` and was
  built reproducibly at `2026-07-24T20:22:57Z`. The exact source archive
  SHA-256 is
  `bb8da75c784cfd5f5f696abb6d4eb5057c75c9c30593c8e277c7b897d8be30d6`;
  it is extracted as `/tmp/astra68-k6-04524898-final` on Beast and NUC.
- K6 kernel: 57,412 bytes, SHA-256
  `17476aa268db37dde0e066f4cc0799848bc0024ae12bba809ec8cffedf84f425`.
  The ELF SHA-256 is
  `367358d6c023ebdc4ebd6aa528690594dc16c646f3d475bf06c97e281fc889dd`.
- K6 normal boot payload: 68,860 bytes, CRC32 `80B0364C`, SHA-256
  `d1cd0888366f66f1a52e3309b02ee0a6b7b824bc163cad20a07bd30e06307693`.
  The 68,892-byte packaged ROM SHA-256 is
  `7073b6f5501f9821d32f2d317fd0adb709e03959b4091bc07ae38a14fc82a8d8`.
  Its hash matches after transfer to Mac and NUC. K6 passes every host,
  analyzer, sanitizer, NDK, Rust, shared-conformance, Musashi, Vesta,
  full pin-level RTL, and provisioning gate plus two independent ULX3S boots.
- K5 thread-lifecycle implementation is based on commit
  `0208cb516801fe452bf59ef053d6daa0a118ee7e` plus the exact qualified
  implementation patch SHA-256
  `1a234d1e5099e31f11e0288bc3e0e40e499fc31de1d6c1333a2138a25aa79e41`.
  The ROM reports
  `0208cb516801fe452bf59ef053d6daa0a118ee7e-dirty-1a234d1e5099` and was
  built reproducibly at `2026-07-24T22:47:32Z`. Exact source snapshots are
  `/tmp/astra68-k5-c481c115-p2` on Beast and NUC.
- K5 kernel: 48,420 bytes, SHA-256
  `260bbcf82fbf955cee42d5798054e6d6549daa8921462d7216a241a685095e03`.
- K5 normal boot payload: 59,804 bytes, CRC32 `11BE3620`, SHA-256
  `8dec0a9ae9ce03f19d5be8c5e8f016dc52684d53828e5acf849a4e76f992527c`.
  The 59,836-byte packaged ROM SHA-256 is
  `6f4c3376597884ac1ff1d544a62b3af97b2e8502731b751eae964fc598d6bdb9`.
  The package hash matches after transfer to Mac and NUC. K5 is qualified
  through host fault injection, exact Musashi normal/performance runs, a fresh
  pin-level RTL build, and two ULX3S boots on unchanged routed FPGA build
  `25D9CB8E`.
- Patch `020f9460a270...` and its passing transcripts are superseded evidence.
  Final audit found that it enabled interrupts across ready-queue publication;
  patch `1a234d1e5099...` makes publication an interrupt-masked commit and adds
  a deterministic timer-boundary regression.
- K4 handle-synchronization source commit:
  `662aa04ef807d6c74ea1a8d0c3a95b8eb78931e7`. The synchronization
  implementation landed in `4a878c9095213d9009e3ad6eeca85ebac3d7c936`; the
  later source delta adds exact K4 RTL and hardware acceptance only. The
  immutable qualification source archive has
  SHA-256
  `ee06971a5239d890ea9e157ae46638bdefa94d3eb86c13656e1aaeedf00d6fe3`
  and is extracted as `/tmp/astra68-k4-662aa04` on Beast and NUC.
- K4 kernel: 44,740 bytes, SHA-256
  `11c2ed31ca5caf07dcfbd87cf354f6ce7be3eb1873cef412b65a6821940fb91c`.
- K4 normal boot payload: 56,152 bytes, CRC32 `2F9B149C`, SHA-256
  `15f713f45e5e8b1eec1bf9820759e915186b70339d3704c0e0771d35df47e588`.
  The 56,184-byte packaged ROM SHA-256 is
  `14f4f980ebeb3fed099ac44d3035e6a1d6f1b2aa354b87bd208c468eb1b66c28`.
  K4 is qualified through exact pin-level RTL and two ULX3S boots on unchanged
  routed FPGA build `25D9CB8E` and remains the K5 rollback checkpoint.
- K3 one-shot/deadline baseline commit:
  `3787d820e1140f49ba31623ccc578bb274a631cc`. Its retained target artifacts
  report the exact development identity
  `8929c063cdd24c8f4f526be330549e2eb5038fc8-dirty` and remain a qualified
  historical rollback checkpoint.
- K3 kernel: 41,020 bytes, SHA-256
  `6ab38364d2ef5e67b6f5e8c7fb691cbf45291624562d7a0203f812c2e648e61d`.
- K3 normal boot payload: 52,444 bytes, CRC32 `BAEF4D0B`, SHA-256
  `33009b3eb09ae51d3ebcdbeac57ec7aff9d3aadee6ee34ab4ea550bc1e76e2c7`.
  The 52,476-byte packaged ROM SHA-256 is
  `b73964d87904994a570c3b5e2b931602f8eb7878f0b531c0ac7e775050919ab1`.
  This software-only image reuses exact routed FPGA build `25D9CB8E`.
- K1 functional source commit: `66d6094f9339469313fefb70b259d07a7c2272ce`.
- K1 lifecycle-soak source commit:
  `470bf123cf24bbadf3525f91307e3d9aebe92006`.
- PMMU processor-reset conformance source commit:
  `c599f921cb35dcc7e8d2988ba253769341311516`.
- Exact corrected qualification snapshot: commit
  `77b3cdc8fddb984850073a2c2cb5998bbbe1d857`, archive SHA-256
  `678f4bb31a8c652615675b871274c992fde08d648a0e6f0a2e135361d168dbb5`,
  extracted as `/tmp/astra68-k1-reset-77b3cdc` on NUC and Beast.
- Deferred-reclamation source commit:
  `bbb1616a1e65ef56619bffb11cb21e9ea1bc5202`, archive SHA-256
  `0e2a64c37871bfc70601e363da72437e821437e6efad3915ef180fe5cf1e9d50`,
  extracted as `/tmp/astra68-k1-bbb1616` on NUC. It changes ROM/kernel
  software and acceptance tooling only; production bitstream `77B3CDC8` is
  unchanged.
- Hardware-burn-in source commit:
  `853ae66e300232dcbdf5f69903747faa42521114`, archive SHA-256
  `c416c1bfb0720ac2bf9fb94898a99e66e1e7b6040f215706a661462ea493f7ad`,
  extracted as `/tmp/astra68-k1-853ae66` on NUC and Beast. It adds coherent
  64-bit elapsed-cycle reporting and acceptance checks only; production
  bitstream `77B3CDC8` remains unchanged.
- Master-mode interrupt correction commit:
  `9a977e13f560b4c85eafc7835d88aad437314491`. It fixes the exact
  format-1/format-0 chained `RTE` path and adds the Motorola-directed M=1
  dual-frame regression.
- Guarded deferred-worker commit:
  `42f4bb55ebd5ac47d057162322e293e4999a2661`. Exact committed release source
  `e108a3711befa08a309f068939dff226a21c869c` has archive SHA-256
  `52420b817dd77be3632640b34ea7a5e2136ededec5c59970c93f883c559ef395`
  and was extracted independently on NUC and Beast. Host, Musashi, focused and
  complete RTL, exact route, repeated ULX3S boot, bounded soak, and
  reset-from-flash gates pass.
- Immutable build snapshot: Beast `/tmp/astra68-k1-66d6094` from Git archive
  SHA-256 `ba4d91999cf829c33a345d895b7966a438b28b93871d8d34843d658a1d0c0039`.
- Immutable soak snapshot: NUC `/tmp/astra68-k1-soak-470bf12` and independent
  Beast `/tmp/astra68-k1-soak-beast` from Git archive SHA-256
  `b5db0e133ee04605fc1e18e4a159e1893893ca5c90c54df1c2ad8bcfc0c64fa5`.
- The qualified normal, direct-panic, and guard-panic ROMs report that complete
  Git identity and reproducible commit timestamp.

## Works now

| Mechanism | State | Evidence |
|---|---|---|
| BootInfo validation and separate kernel image | CURRENT | host, Musashi, full RTL |
| kernel VBR, guarded 8 KiB ISP, guarded 8 KiB worker MSP | CURRENT HW | host descriptors, Musashi, focused/full RTL, exact ULX3S boot and soak |
| frame allocator for all 8,192 4 KiB frames | CURRENT HW | host tests, target startup, 64-owner bounded ledger, exact release-visit accounting |
| one-way boot allocator retirement | CURRENT HW | boot self-test page returns before retirement; all later boot-only requests fail without state change on host, Musashi, full RTL, and two ULX3S boots |
| 32-page emergency reserve | CURRENT HW | ordinary allocation exclusion, exact 32-page exhaustion/return, owner release, zero-free cleanup, Musashi, full RTL, and two ULX3S boots |
| typed fixed-object caches | CURRENT HW | 11 typed caches, independent allocation bitmaps, next-fit claims, generation-preserving release, exhaustive host validation, Musashi, full RTL, and hardware ledger check |
| per-site allocation ledger and deterministic failure injection | CURRENT HW | stable IDs 1-23; all 22 injectable sites fail through global-Nth and site-Nth selectors and restore every resource baseline |
| allocation-free teardown at zero ordinary free pages | CURRENT HW | user-fault retirement, process/IPC/area cleanup, reserve return, and retained logging complete without allocation or drift |
| SRP/CRP 4 KiB two-level translation | CURRENT | host, focused RTL, Musashi, full RTL |
| PMMU enable and CRP switching | CURRENT HW | Musashi, full RTL, and three exact SRAM K1 boots |
| user null/code/stack guards | CURRENT | host mapping tests and target fault |
| supervisor stack guard | CURRENT HW | exact descriptor host test plus deliberate format-A vector-2 guard panic at `0x02028000` in full RTL and physical hardware |
| cross-CRP cache isolation | CURRENT SIM | distinct same-address code and stack markers on Musashi/full RTL |
| duplicate cached user-alias rejection | CURRENT HOST | one-bit/frame VM ledger and remap test |
| whole-address-space cache invalidation | CURRENT HOST | destruction invalidates before descriptor removal/frame reuse |
| format 0/1/2/9/A/B frame decode | CURRENT | byte-exact host tests |
| SFC/DFC copyin/copyout fault recovery | CURRENT | focused RTL, Musashi, full RTL |
| typed generation handle table | CURRENT | host tests; current 16 entries/process |
| process creation and owner teardown | CURRENT HW | host, Musashi, full RTL, and exact 100-cycle ULX3S fault/reap path |
| separate generation-safe process/thread objects | CURRENT HW | host, Musashi, full RTL, and two exact K2 ULX3S boots; 4 process and 16 thread slots |
| runtime thread create and caller-only exit | CURRENT HW | bounded transactional create, interruptible prepare plus masked no-allocation publish, timer-boundary race injection, fixed guarded stack, exact rollback, caller-only exit, and last-thread process promotion pass host, Musashi, pin-level RTL, and two ULX3S boots |
| waitable thread death | CURRENT HW | `WAIT_ONE` returns the exact 32-bit exit status, remains level-triggered, and arbitrates exit/timeout/cancel/close/process death exactly once; generation-safe reuse and stale rejection pass every backend |
| guarded per-thread supervisor stacks | CURRENT HW | 16 8 KiB stacks with 4 KiB guards; host mapping/canary tests, Musashi, full RTL, and two K5 ULX3S boots report a 680-byte maximum use |
| multi-thread process teardown | CURRENT HW | all siblings leave ready/wait queues atomically; deferred owner reap passes host, Musashi, full RTL, and ULX3S |
| deferred pinned-DMA reap | CURRENT SIM | guarded worker state machine on host; Musashi and full RTL lifecycle soaks |
| two isolated user processes | CURRENT HW | same ROM on Musashi, full RTL, and three exact SRAM boots |
| 5 ms one-shot fixed-priority scheduling | CURRENT HW | exact 62,500-cycle quantum, 32 queues and ready bitmap; highest priority first, FIFO round-robin among equals; K3 target boots pass |
| same-address-space thread switch | CURRENT HW | host, Musashi, full RTL, and ULX3S count this path separately without a CRP/ATC/cache switch |
| K3 atomic block/wake/deadline substrate | CURRENT HW | sequence-checked wait queues, 16-entry deadline heap, priority/FIFO wake, timeout, close wake-all, and immediate higher-priority handoff pass host, Musashi, full RTL, and ULX3S |
| handle-backed events and semaphores | CURRENT HW | generation-safe handles, explicit rights, absolute-nanosecond deadlines, cancellation, close, owner death, quotas, and exact-once arbitration pass host, exact Musashi, pin-level RTL, and two ULX3S boots |
| bounded wait-multiple | CURRENT HW | 1-16 events, semaphores, timers, thread/process death, ports, and K8 ring endpoints share fixed registrations and one deadline; complete prevalidation, deterministic input-order winner, duplicate members, every terminal race, and exact cleanup pass host, Musashi, full pin-level RTL, and two ULX3S boots |
| waitable one-shot timers | CURRENT HW | shared 32-object pool plus fixed deterministic timer heap; set/rearm/immediate expiry/cancel/close/owner-death and level readiness pass host, Musashi, full pin-level RTL, and two ULX3S boots |
| waitable process death | CURRENT HW | generation-safe process handles, normal exit detail, abnormal terminal result, self rejection, prestart-only bootstrap grant, close/reuse, and exact reference cleanup pass host, Musashi, full pin-level RTL, and two ULX3S boots |
| bounded IRQ endpoints and common Vesta dispatcher | CURRENT SIM | 16 generation-safe endpoints, four fixed records each, edge/level ordering, exact sequence acknowledgement, overflow/storm masking, waiter wake, stale generation rejection, and owner-death cleanup pass host tests and the corrected complete pin-level K10 boot |
| typed kernel MMIO accessors | CURRENT HOST/RTL | width/alignment/range/order/fence tests, MC68030 cross-build, and focused USB/control-path RTL pass; production kernel C uses the centralized accessors |
| physical bus watchdog and first-fault diagnostics | CURRENT RTL | sticky first-fault/lost accounting, unmapped and timeout BERR, split-cycle strobe suppression, USB target withdrawal, SDRAM-fatal classification, and framebuffer-guard tests pass focused host/RTL gates |
| retained allocation-free trace ring | CURRENT SIM | fixed 64 KiB/2,047-record section, commit-last records, wrap/torn-read checks, panic snapshot, and zero-ordinary-memory operation pass host and target qualification |
| bounded FTDI/AstraHost-SPI monitor core | CURRENT SIM | one fixed post-PMMU parser/command table, bounded lines/fragments, backpressure/truncation, guarded-worker dispatch, and zero-ordinary-memory host checks; the retained trace stages pre-PMMU events independently, and physical interactive transport checks remain pending |
| bounded message ports | CURRENT HW | 16 receiver-owned ports and 32 fixed message records enforce per-port and per-owner count/byte caps, nonblocking backpressure, absolute deadlines, cancellation, close, and peer death; host, Musashi, pin-level RTL, and two ULX3S boots pass |
| atomic handle transfer | CURRENT HW | up to eight move-only generation-safe handles reserve, validate, and commit exactly once; failed send and failed receive copyout preserve or release authority without leaks, stale reuse, or partial publication |
| reduced-right handle duplication | CURRENT HW | K8 non-destructive duplication requires `transfer`, accepts only a nonzero rights subset on explicitly retainable objects, publishes atomically, and preserves the source on every failure |
| shared areas | CURRENT HW | eight areas, 32 mappings, 128 committed pages, fixed cross-process logical addresses, transactional descriptor publication/rollback, coherent cache policy, revocation, and exact frame/accounting cleanup pass host, Musashi, pin-level RTL, and two ULX3S boots |
| bounded SPSC bulk rings | CURRENT HW | 16 area-backed rings use fixed big-endian headers, move-only producer/consumer endpoints, batched notify, fixed wait queues, corruption containment, peer/creator death, and no kernel payload allocation; every backend and two ULX3S boots pass |
| trap ABI query/progress/yield/process-exit/close/clock/sync/thread lifecycle/wait-multiple/timers/ports/areas/rings/IRQs | CURRENT SIM | ABI `0x00010005`; retained K1-K9 calls remain unchanged and the K10 IRQ operations pass host and complete pin-level target qualification; K9 ABI remains the hardware-qualified subset |
| offender-only user fault death | CURRENT HW | format-B fault reaps only the offender on Musashi, full RTL, and three exact SRAM boots |
| last-process supervisor idle transition | CURRENT HOST | process/dispatch tests; target assembly builds |
| panic to console and retained early log | CURRENT HW | exact direct and supervisor-guard panic paths pass full RTL plus physical HDMI/log qualification |
| kernel host analyzer/sanitizer gates | CURRENT | 28 suites, analyzer, ASan/UBSan/leak checks |
| kernel cycle-budget gate | CURRENT HW | twenty measured syscall/timer/fault/scheduler/wait/deadline/thread-lifecycle/wait-set/port/area/ring paths retain fixed K8 limits in K9 Musashi, full RTL, and two ULX3S boots; zero overruns |
| end-to-end Musashi performance gate | CURRENT | exact 1,000-iteration K9 workload is 603,007,142 virtual cycles against a 675,000,000-cycle cap with a stable 7,954-page baseline |
| deterministic lifecycle-soak harness | CURRENT HW | dual-host 500,000-cycle legacy Musashi, optimized Musashi, 13-cycle full RTL, routed five-minute candidate, and independent 30-minute release runs pass without drift |
| deferred user-fault reclamation | CURRENT HW | host proves no maintenance/owner release in fault dispatch; Musashi, full RTL, and ULX3S report bounded masked-fault cycles |
| shared CPU/PMMU framework | CURRENT | 90 tests, 30 adapter executions, Harte smoke |
| master-mode interrupt dual-frame return | CURRENT RTL | exact Motorola format-0/1 frames, MSP chain, multiword restart, complete strict inventory |
| fixed interruptible process-reap worker | CURRENT HW | guarded MSP/ISP, coalesced work bit, bounded retry, normal and soak target images, exact five-minute ULX3S run |
| CACR independent I/D commands | CURRENT RTL | Motorola-directed mixed CI/CD decoder test; strict inventory 141/115 clean |
| RESET preserves roots and ATC until explicit flush | CURRENT RTL/ROUTED | stale-ATC/reset/`PFLUSHA` regression; strict inventory 141/115 clean; prior exact full mapping has zero SCCs and passes all clocks |
| prior corrected K1 release ROM | CURRENT | build `77B3CDC8` remains a qualified rollback artifact with Musashi/full RTL and routed-hardware evidence |
| guarded-worker K1 release ROM | CURRENT HW | build `25D9CB8E` passes exact route, repeated SRAM boots, five-minute worker soak, and reset-from-flash |
| guarded-worker K1 hardware boot | CURRENT HW | exact identity, full POST/BIST, PMMU, guarded worker, 100 Hz preemption, offender-only fault containment, K1 entry, and physical HDMI pass |
| K3 one-shot/deadline hardware boot | CURRENT HW | two exact `25D9CB8E` reloads pass ROM `BAEF4D0B`, full 32 MiB POST/BIST, 5 ms quantum, timeout handoff, all K1/K2/K3 markers, and zero overruns |
| K4 handle-synchronization hardware boot | CURRENT HW | two exact `25D9CB8E` reloads pass ROM `2F9B149C`, full 32 MiB POST/BIST, all event/semaphore lifecycle counts, priority handoffs, K1-K4 markers, and zero overruns |
| K5 thread-lifecycle hardware boot | CURRENT HW | two exact `25D9CB8E` reloads pass ROM `11BE3620`, full 32 MiB POST/BIST, create/exit/death-wait/reap, all K1-K5 markers, and all twelve cycle budgets with zero overruns |
| K6 wait-multiple hardware boot | CURRENT HW | fresh Verilator full-SoC run and two independent exact `25D9CB8E` ULX3S loads pass PMMU isolation, exact K6 counters, all K1-K6 markers, all fourteen budgets, and zero overruns |
| K7 message-port hardware boot | CURRENT HW | fresh Verilator full-SoC run and two independent exact `25D9CB8E` ULX3S loads pass full POST/BIST, exact port/transfer counts, all K1-K7 markers, all sixteen budgets, and zero overruns |
| K8 shared-area/bulk-ring hardware boot | CURRENT HW | clean Verilator full-SoC run and two independent exact `25D9CB8E` ULX3S loads pass full POST/BIST, exact area/mapping/ring/duplicate counts, K1-K8, all twenty budgets, and zero overruns |
| K9 memory-pressure hardware boot | CURRENT HW | exact routed `7DDD9C03` passes two independent volatile ULX3S loads, full POST/BIST, 32/32 reserve, runtime allocation ledger, K1-K8, all twenty budgets, zero overruns, and physical HDMI |

## Hardware status

- The ULX3S attached to NUC retains exact persistent guarded-worker release
  `25D9CB8E`. K9 build `7DDD9C03` was loaded into volatile SRAM twice and is
  the current qualified image, but persistent FPGA flash was deliberately not
  rewritten. Prior `77B3CDC8` K1 and `6C0D0CA3` K0 images remain historical
  rollback artifacts.
- Exact K9 ROM CRC32 `8E57D4DA` is installed at `/ASTRA68.ROM`. Provisioning
  mounted the existing 244,016 MB game volume without formatting, changed only
  that file, and verified it independently. Exact read-only production
  AstraHost application SHA-256
  `1a822cd9bb08ce9c3dfb6292017e96967b1dada34de78b27200cea22c1227e6e`
  was restored and verified by flash readback.
- NUC enumerates no HDMI capture device, so the user supplied the physical
  second-boot HDMI image. It shows the exact K9 performance tail, zero
  overruns, every K1-K8 pass marker, and `KERNEL MULTITASKING`; retained image
  SHA-256 is
  `e7b853d62ab634b7ae1ef023769493f93560007c7520687f5f39604a8b6184eb`.
- Exact `7DDD9C03` maps 53,079 LUT4s, 25,532 FFs, 101 DP16KDs, and 18
  multipliers with zero SCCs. POR validation reports 25,536 GSR-enabled FFs.
  The no-waiver route packs 66,523 TRELLIS_COMB and 25,565 FFs and passes at
  15.058201 MHz CPU, 66.907532 MHz SDRAM, 79.693970 MHz USB, 53.267990 MHz
  pixel, and 289.771088 MHz HDMI shift. Bitstream SHA-256 is
  `cf1adbe78cb9f486b3d2fbae36ada91023fda36d8cb4b0ffec7df5828e3c6bf1`;
  routed-JSON SHA-256 is
  `1956c067536ce521b10cda7429ada2252af0b0e66f4d9e2debc6ac49caff9f18`.
- Routed SRAM candidate `F4DC1E18` proves the repaired PMMU core and K0 platform,
  not the staged K1 kernel.
- K-HW3 table-walk arbitration, K-HW4 timer/IACK changes, and K1 are integrated
  in exact build `77B3CDC8`, fully routed together, and exercised by three
  passing volatile SRAM boots plus an automatic reset-from-flash boot. Both
  physical panic paths and the bounded hardware burn-in pass.
- Exact build `66D6094F` completed a timing-clean strict route on Beast as
  useful diagnostic physical evidence. It predates the PMMU reset correction
  and cannot be a release image or be loaded onto the board.
- Exact corrected build `77B3CDC8` passes full-chip synthesis and seed-4
  critical-floorplan placement on NUC with zero SCCs, 53,073 LUT4s, 25,532
  GSR-enabled FFs, 101 DP16KDs, and 18 multipliers. Placement packs 66,513
  TRELLIS_COMB and 25,561 TRELLIS_FF cells and finishes normally with checksum
  `0x7c9a8594`. The uninterrupted no-waiver strict router1 route finishes
  normally with checksum `0x09264110`. Every constrained clock passes:
  14.179972 MHz CPU, 61.270760 MHz SDRAM, 77.760498 MHz USB, 58.227554 MHz
  pixel, and 294.290771 MHz HDMI shift. The physical-capacity gate passes at
  66,513/83,640 TRELLIS_COMB, 101/208 DP16KD, and 18/156 multipliers. Exact
  bitstream SHA-256 is
  `56f768b2d78801f6cc93a7c518643f1012e30f48241e0d77be8250f97c1c2755`;
  exact build-manifest SHA-256 is
  `0593ba251da7b467e413126539d1e863ca19ef00f63843ed5f0cc6d32913b74e`.
- The exact normal `77B3CDC8` ROM now passes the complete pin-level RTL/SDRAM
  model with full 32 MiB BIST at 115.06 MB/s, PMMU enable, two isolated
  processes, 100 Hz preemption, offender-only fault death, three context
  switches, and `K1 PROTECTED ENTRY PASS`. The same image passes Musashi.
- NUC mounted the existing 244,016 MB SD volume without formatting, atomically
  validated/replaced only `/ASTRA68.ROM`, and reported exact payload CRC32
  `EB1B381F`. Normal read-only AstraHost firmware was restored and remounted the
  card. Three independent SRAM reloads of exact bitstream
  `56f768b2d78801f6cc93a7c518643f1012e30f48241e0d77be8250f97c1c2755`
  pass the complete hardware gate in 2.127, 2.145, and 2.147 seconds. Physical
  HDMI shows the exact K1 identity and complete protected-entry result; retained
  screenshot SHA-256 is
  `8b6d0d57bf7f029aa63506c348976830079ccabcdeb6a3cf38cad51d3365b051`.
  NUC then programmed the identical bitstream persistently and reset it. The
  automatic flash boot passes the same gate in 2.132 seconds; retained log
  SHA-256 is
  `deeaba2d4acdb5fbc5115085b4f751796ce11079cc68ded319c43117d17b0e97`.
- The exact direct-panic image passes on physical hardware in 1.816 seconds
  after complete POST. HDMI displays the deliberate panic, exact provenance,
  and `SYSTEM HALTED`; physical screenshot SHA-256 is
  `639785017f2691b7e4cebc493289f0e0f15d89762aed34c4994c869bce17a8de`.
  The checker transcript SHA-256 is
  `e6297e0b7adb8e2cc0352fc1c6575d6c02dbc09312059ee66ca1206ad5b8114a`.
- The exact supervisor-guard image passes on physical hardware in 1.821
  seconds after complete POST. HDMI reports vector 2, format A, SSW `0x0105`,
  exact guard address `0x02028000`, provenance, and `SYSTEM HALTED`; screenshot
  SHA-256 is
  `d7289448fb1453fee1e6be617eaad00d458d267f68183416f83ebfa1a827dce1`.
  The checker transcript SHA-256 is
  `01aa5fd5d578ad94291a82f9f771df89395274c0ac7a9a42cf702784d9abc0d0`.
- Exact source `bbb1616a1e65ef56619bffb11cb21e9ea1bc5202` removes synchronous
  address-space destruction and owner release from IPL-7 fault dispatch. Its
  64-owner ledger makes release O(frames owned) and preserves all-or-nothing
  behavior when any frame remains pinned. Eleven host suites, GCC
  `-fanalyzer`, ASan/UBSan/leak checks, 15 Rust tests, rustfmt, Clippy, all 90
  framework tests, all 30 shared Musashi/RTL executions, and both Harte smoke
  adapters pass. Exact 100-cycle Musashi completes at virtual cycle 77,501,092
  in 0.814 seconds with a 4,482-cycle masked-fault maximum. The complete
  pin-level RTL/SDRAM run completes 13 teardown cycles in 355.123 seconds with
  an 8,866-cycle maximum and no baseline drift.
- On the routed ULX3S, the same software running under unchanged production
  bitstream `77B3CDC8` completes 100 cycles in 8.219 seconds. Cycle 100 reports
  205 switches, 636 delivered ticks, syscall count `0x689`, exactly 7,987 free
  pages, and a maximum masked user-fault dispatch of 8,834 cycles (706.72 us),
  well below the 125,000-cycle gate. Retained soak transcript
  `docs/evidence/k1-77b3cdc8-bbb1616-soak-100-hw.log` has SHA-256
  `928fdd5414aacb237c5818293a464c3860ffcd0c7cf6d0a48f2fbcf200f0fb5e`.
- The normal ROM is restored at payload CRC32 `C030B951`; the read-only
  AstraHost application and production bitstream are restored unchanged. A
  fresh normal boot passes complete POST and `K1 PROTECTED ENTRY PASS` in
  1.931 seconds. Retained transcript
  `docs/evidence/k1-77b3cdc8-bbb1616-normal-hw.log` has SHA-256
  `6197aeeeb3a55ea1d8366025a6c64f9d2a424b17791bb60d3ab72d8ec6916b86`.
  That checkpoint preceded the elapsed-counter release qualification below.
- Exact source `853ae66e300232dcbdf5f69903747faa42521114` passes both
  routed-hardware release gates under the unchanged production bitstream. The
  candidate run reaches cycle 5,000 after 317.246 host seconds and
  `0x00000000EAE8411F` FPGA CPU cycles, with 10,005 switches, 31,533 delivered
  ticks, syscall count `0x15288`, 7,987 free pages, and an 8,809-cycle maximum
  masked-fault interval. The independent release run reaches cycle 29,000
  after 1,830.658 host seconds and `0x000000055263857F` FPGA CPU cycles, with
  58,005 switches, 182,861 delivered ticks, syscall count `0x7AD6B`, the same
  7,987-page baseline, and the same latency maximum. Evidence SHA-256 values
  are `db9ad4900951e3cc61ae20d8078bd714a20089bcd1880f0f77dc58d34f64dbf6`
  for the five-minute candidate and
  `71d2c3a766bc1cd25a58f6e81ca9c904517b0df74322d2d3130279a0e1ffa489`
  for the 30-minute release run.
- Normal ROM package SHA-256
  `696afc6ecf9d5df31acc76966aeea0fe190b44479c4af61a2fbf16f8866f7d05`
  is restored at payload CRC32 `BBAB0AA1`; a second one-shot boot verifies the
  exact file match. Normal read-only AstraHost and the production bitstream are
  restored unchanged. The final boot reports full Git identity
  `853ae66e300232dcbdf5f69903747faa42521114`, completes POST/BIST, and reaches
  `K1 PROTECTED ENTRY PASS` in 1.955 seconds. Retained provisioning and boot
  transcript SHA-256 values are
  `5be77adb8627fbd0ca4a6ede6601c92d362d44bf0394dce3f31fad8f0c398929`
  and `14b69338b1c429def6fa0a13067bff6e00f087dae0dc7a05a3a463e7a107f09c`.
  The hardware burn-in gate is closed; the stable-kernel mechanisms below
  remain open.

## Guarded-worker release

Exact source `42f4bb55ebd5ac47d057162322e293e4999a2661` moves process
reclamation off syscall and idle paths into a fixed stackful worker. The worker
runs S=1/M=1 on an 8 KiB MSP with a 4 KiB unmapped guard; exception and IRQ
entry use the independent guarded ISP. Pending work is a one-bit process-reap
mask, retries are one bounded bit, and all maintenance executes with interrupts
enabled. Panic output includes worker state, counters, stack use, last
supervisor IRQ, and exact maintenance failure data.

Beast passes all 12 host suites normally, under GCC `-fanalyzer`, and under
ASan/UBSan/leak checks; canonical m68k build verification, 15 Rust tests,
rustfmt, Clippy, all 90 framework tests, all 30 shared executions, and both
Harte smoke adapters pass. The complete Questa inventory is 141 total and 115
clean with the unchanged 3/18/5 classified buckets. Musashi normal boot reaches
K1, and its 1,000-cycle lifecycle soak ends at virtual cycle 640,260,129 with
2,001 switches and the exact 7,987-page baseline. The complete pin-level
RTL/SDRAM model passes normal K1 in 130.017 seconds and the worker soak
checkpoint in 191.959 seconds at 115.03 MB/s BIST with no baseline drift.

Exact production build `25D9CB8E` routes without a timing waiver and passes
three independent normal SRAM boots on ULX3S. Its five-minute hardware soak
reaches 5,000 cycles, 10,003 switches, 30,057 delivered ticks, 48,709 syscalls,
and `0x00000000DFEAD7D7` elapsed CPU cycles while retaining exactly 7,987 free
pages and a 9,376-cycle masked-fault maximum. After normal ROM and read-only
AstraHost restoration, a fourth SRAM boot passes. FPGA flash now contains the
same exact bitstream, and reset-from-flash reaches K1 in 2.008 seconds. NUC has
no capture device, but the retained physical-HDMI image visibly confirms the
exact Git identity, guarded worker, PMMU, preemption, fault containment, and K1
entry. Screenshot SHA-256 is
`e6e654d6ad0c9f5dead16f9116ab622d7a5ba731fc2fafc1ff7ba324c08128a4`.

## K2 blocking/thread development checkpoint

The scheduler dispatches independent `KernelThread` objects rather than a
combined process/context record. Sixteen fixed 156-byte records carry
generation IDs, process-private handles, CPU contexts, guarded user and 8 KiB
supervisor stacks, base/effective priority, accounting, and intrusive ready and
wait links. Processes own the CRP/address space, resource account, handle table,
default priority 16, user ceiling 23, and aggregate death. Current handle-table
capacity makes the development per-process cap 15 threads; the stable target
remains 16 after the handle pool grows.

Thirty-two FIFO queues plus one bitmap select the highest effective priority in
bounded constant steps and rotate equal priorities round-robin. Same-process
switches retain CRP and avoid the VM switch/cache/ATC path. A 12-byte wait queue
uses a monotonic nonzero sequence to make condition-check plus block atomic;
wake-one selects the highest-priority waiter and preserves FIFO order among
equals, while wake-all is bounded by the 16-thread pool. The first 16-byte event
implements signaled, consumed, and closed states on that queue. Process death
withdraws every sibling from ready and wait queues before deferred destruction
and generation-safe record reuse.

All 16 host suites pass normally, under GCC `-fanalyzer`, and under
ASan/UBSan/leak checks. Canonical MC68030 build verification, 15 Rust tests,
rustfmt, and Clippy `-D warnings` pass. Shared out-of-line byte copy/clear
primitives replaced repeated force-inlined loops; exhaustive alignment/length
tests cover them and target disassembly uses aligned longword operations. The
exact 1,000-cycle Musashi workload fell from 895,512,627 to 645,759,523 virtual
cycles. Replacing duplicate DMA/block generation rollover with the existing
shared helpers removed 12 image bytes and another 436 cycles, leaving
645,759,087 cycles. A 675,000,000-cycle automated ceiling now makes that
end-to-end result a regression gate. The normal kernel is 36,960 bytes with
SHA-256
`1ee4232e1cc7c637f4c0ea06787191193fb23c64a19b603fc51d89539b425a29`.

The coherent full pin-level Verilator/SDRAM run uses the same ROM payload and
exact dirty source identity. Full 32 MiB BIST passes at 115.04 MB/s. It executes
seven context switches, two same-CRP switches, two blocks, one wake, one
higher-priority handoff, and reports a 388/8192-byte maximum supervisor-stack
use. Maximum measured cycles are 20,806 syscall dispatch, 14,344 timer dispatch,
21,896 user fault, 1,449 scheduler pick, 1,567 same-CRP switch, 2,540 cross-CRP
switch, 1,265 block, and 2,437 wake. Every fixed budget passes with zero
overruns, followed by K2 blocking/thread and retained K1 markers.

The HDMI-enabled boot payload is 48,384 bytes with CRC32 `E28408B4`; the
packaged ROM is 48,416 bytes. Two independent volatile reloads of exact routed
build `25D9CB8E` on the NUC-attached ULX3S complete the same gate in 2.333 and
2.338 seconds. Both report the same 388-byte stack high-water mark and zero
cycle budget overruns. Run-to-run maxima vary only slightly: syscall
29,705/29,685, timer 14,310/14,335, user fault 19,632/19,635, scheduler pick
1,444/1,444, same-CRP 1,575/1,569, cross-CRP 2,546/2,527, block 1,264/1,264,
and wake 2,436/2,428. FPGA flash was not rewritten; normal read-only AstraHost
firmware and the exact K2 ROM were restored while unrelated SD contents were
preserved.

Retained final evidence is
`evidence/k2-be27074-refactor-perf-rtl.log` (SHA-256
`7eb2ffa99c6d9d270acbb51de05ebe7807a16afc5f3a7696b0624c68b80d9047`),
`evidence/k2-25d9cb8e-be27074-refactor-perf-provision.log`
(`36383d6e3d1e421ca77718d05473136f5fda66bd284e79d610904ef36859f0ee`),
`evidence/k2-25d9cb8e-be27074-refactor-perf-hw-1.log`
(`09a76d063c7743e386f66100d53eeda75707dc5087b5364e3436ad1d03441cbf`),
and `evidence/k2-25d9cb8e-be27074-refactor-perf-hw-2.log`
(`2e75d86ee06d0efcc219e0cbd258bfb693f902842823dfdbd99798981fdd74bf`).
A mixed-source scratch simulation and an accidental `77B3CDC8` rollback load
were rejected before acceptance; neither is a kernel failure or evidence for
this checkpoint.

## K3 one-shot/deadline checkpoint

K3 replaces the periodic 10 ms scheduling tick with an exact 5 ms one-shot
quantum (62,500 cycles at 12.5 MHz). The timer is rearmed to the earlier of the
active quantum and earliest blocked-thread deadline. Context activation starts
a quantum; ordinary syscalls do not renew it. Higher-priority readiness from a
signal or timeout preempts immediately, while an all-blocked system retains the
deadline and idles through the guarded supervisor worker.

The deadline owner is one fixed 16-entry binary min-heap, one entry per global
thread slot. It stores absolute 64-bit cycle deadlines, orders equal deadlines
by slot, allocates nothing, and removes timeout/signal/close/process-death
waiters exactly once. The current timed wait remains an internal qualification
path; public handles, absolute monotonic nanoseconds, explicit cancellation,
and peer-death ABI remain the next object-lifecycle work.

All 17 kernel host suites pass normally, under GCC `-fanalyzer`, and under
ASan/UBSan/leak checks. Canonical m68k verification, all Rust format/Clippy/test
gates, all 90 framework tests, all 30 shared executions, both Harte smoke
adapters, and the directed Vesta timer/IACK race test pass. Exact normal
Musashi reaches K3 at 17,250,229 virtual cycles. The exact 1,000-cycle workload
finishes at 596,507,297 cycles, 49,251,790 cycles below K2 and 78,492,703 below
the enforced 675,000,000 ceiling, with 2,036 switches, 3,103 delivered timer
interrupts, syscall count `0x1F85`, 7,986 free pages, and zero overruns.

The complete pin-level RTL/SDRAM model uses an intentional 64 KiB simulated
BIST and reaches all K1/K2/K3 markers in 222.222 seconds. It measures deadline
expiry at 6,163/20,000 cycles with zero overruns. NUC mounted the existing
244,016 MB card without formatting, atomically replaced only `/ASTRA68.ROM`,
and independently verified the installed file. Provisioning evidence SHA-256
values are
`e541070f0a086e3a2c29177c52611709d27040f0302719bc80df8fc28004f0a9`
and
`58c58ac2f1c55990b17690b19fa921fa9c2c710061aea065c56836a260adca4e`.

Two independent volatile loads of exact production bitstream `25D9CB8E` then
pass ROM CRC32 `BAEF4D0B`, complete POST and real 32 MiB BIST, PMMU/user-copy
isolation, 20 context switches, one timeout and higher-priority handoff, all
K1/K2/K3 markers, and zero overruns. Both measure deadline expiry at
6,177/20,000 cycles. Hardware transcript SHA-256 values are
`f05c5f0a6b88ab38fb3557d6f412dfbd708011b5078112eaa905b515a0856709`
and
`f5f46ccd4230aca44a360a402dc57747e42aef2a8f56461f55960a3bd8ceaa55`.
Read-only AstraHost is restored. This is a software-only checkpoint: FPGA
flash, routed resources, and every constrained-clock result remain unchanged.

## K4 handle-synchronization checkpoint

K4 publishes auto-reset/manual-reset events and counting semaphores through
per-process generation-safe handles. Handles require explicit wait or signal
rights. Waits accept signed absolute monotonic nanosecond deadlines, with zero
as poll and `INT64_MAX` as no deadline. Signal, timeout, explicit cancellation,
last-handle close, and owner death remove and complete a waiter exactly once.
Wake-one is priority ordered and FIFO among equals; waking a higher-priority
thread requests an immediate handoff.

The implementation allocates 32 fixed 36-byte objects, limits each owner to
eight objects, and inherits the global 16-waiter ceiling from the thread pool.
It reuses each thread's existing wait/deadline linkage and allocates nothing in
the signal, wait, timeout, cancellation, or teardown paths. The syscall ABI now
provides monotonic time, event/semaphore creation, wait-one, signal, event
reset, and cancellation. Public NDK rights use the same canonical bit values as
the kernel ABI.

All 17 host suites pass normally, under GCC `-fanalyzer`, and under
ASan/UBSan/leak checks. NDK `check`, sanitizer, analyzer, m68k library/example,
and canonical m68k kernel/ROM verification pass. Exact committed normal Musashi
reaches all K1-K4 markers in 19,000,216 cycles, with 19 context switches, six
blocks, two wakes, three priority handoffs, one expiry, and
cancel/close/death counts of 1/1/1. The exact 1,000-cycle workload completes in
622,507,501 cycles under the 675,000,000 cap, retains exactly 7,986 free pages,
reports 2,055 switches, 3,204 timer ticks, syscall count `0x2377`, and has zero
performance overruns. Maximum masked user-fault time is 34,580 cycles.

The exact pin-level RTL/SDRAM model passes its intentional 64 KiB BIST, all
lifecycle counts, every K1-K4 marker, and all performance gates in 266.959
seconds. It reports 27 context switches, six same-CRP switches, a
6,164/20,000-cycle deadline maximum, and zero overruns. Retained transcript
SHA-256 is
`fa89ee4c9188866a20aed4ced11d90d7391a8455f0ef4fc1fd6614619ed661da`.

NUC mounted the existing 244,016 MB card without formatting, replaced only
`/ASTRA68.ROM`, and independently reported that the installed file already
matched. Provisioning transcript SHA-256 values are
`e3d0d21efbfae8d4a84efe5318ee495329d8d4fe8655da35799c16e8524c0be0`
and
`3d7aae0c20165175fdffdf51655c5ebd4fafae82fd9f2fa6371bb8952bb33e78`.
Known read-only AstraHost was then restored.

Two independent volatile loads of exact production bitstream `25D9CB8E` pass
ROM CRC32 `2F9B149C`, complete POST and physical 32 MiB BIST, PMMU/user-copy
isolation, 27 context switches, six blocks, two wakes, three priority
handoffs, one deadline expiry, cancel/close/death counts of 1/1/1, all K1-K4
markers, and zero overruns. Both report a 6,164/20,000-cycle deadline maximum.
Hardware transcript SHA-256 values are
`4dd8583781bca253229240e25f4169d70aaa1a5a224b22be24d4aef23d2c3135`
and
`b614522dd02fd9f110b56196297dd7afb221165c60e5383424abd3a7e1139de6`.
This is a software-only promotion: FPGA flash, routed resources, and every
constrained-clock result remain unchanged.

## K9 memory-pressure checkpoint

K9 is exact clean implementation commit
`03660014d7af6d3662504fc076700f04929117ab`, built with
`SOURCE_DATE_EPOCH=1785033792`. It adds 11 typed fixed-object caches, stable
allocation-site IDs 1-23, both deterministic failure selectors for all 22
external sites, a 32-page emergency reserve, one-way boot retirement,
per-subsystem accounting, and allocation-free zero-memory cleanup. It changes
no public syscall or handle ABI and does not add a general heap.

All 21 host suites, analyzers, sanitizers, NDK, Rust, shared-conformance, Harte,
Musashi, and full pin-level RTL gates pass. The 1,000-iteration Musashi run
finishes in 603,007,142/675,000,000 cycles at a stable 7,954-page baseline.
The first implementation was rejected at 66,897/50,000 syscall cycles; the
retained O(1) production validation path raises no budget and passes every K8
metric with zero overruns.

Exact route `7DDD9C03` retains the complete production feature set, zero SCCs,
66,523 TRELLIS_COMB, 25,565 FFs, 101 DP16KDs, and 18 multipliers. Every clock
passes, including 15.058201 MHz CPU and 66.907532 MHz SDRAM. Two independent
volatile ULX3S loads pass full 32 MiB POST/BIST, K9 allocator/reserve checks,
K1-K8, all 20 performance gates, and physical HDMI. The exact bitstream
SHA-256 is
`cf1adbe78cb9f486b3d2fbae36ada91023fda36d8cb4b0ffec7df5828e3c6bf1`.
Persistent FPGA flash remains exact `25D9CB8E`; K9 is current and K8 is its
qualified rollback.

## K8 shared-area and bounded-ring checkpoint

K8 is exact implementation commit
`56bd1770c834205a4dccc42efb61552a77647988`, built with
`SOURCE_DATE_EPOCH=1785023940` (`2026-07-25T23:59:00Z`). Immutable source
archive `/tmp/astra68-k8-56bd177.tar` has SHA-256
`b0db820437d5526b9157815c5a280770e59e21930c39d48dddb6d21389d5cb49`
and was extracted independently as `/tmp/astra68-k8-56bd177` on Beast and NUC.

K8 adds eight fixed shared areas, 32 mapping records, 16 SPSC rings, real
physical commit, reduced-right handle duplication, transactional map rollback,
one cache-safe logical address per area across CRPs, batched notifications,
wait-multiple integration, and terminal peer/creator-death cleanup. The profile
caps areas at 16 4 KiB pages, area commit at 128 pages system-wide and 64 per
creator, mappings at four per process, and rings at four per area and creator.
Ring payload lives entirely in its area; the kernel allocates no payload queue.

All 20 host suites pass normally, under GCC ASan/UBSan/leak checks, and under
GCC `-fanalyzer`. The NDK passes six normal and six sanitizer suites, its
analyzer gate, the MC68030 library and examples, and generated HTML plus a
106-page, 413,053-byte PDF. These tests cover every injected create/map
publication failure, duplicate/reduced rights, real K7 handle transfer,
cross-process aliases, lost-wakeup windows, corruption, peer/creator death, and
1,000 create/map/ring/unmap/lifecycle repetitions with exact baseline return.

Normal Musashi reaches every K1-K8 marker in 24,750,250 cycles. Its K8 area
create/map/unmap/ring-notify maxima are 25,298/43,908/43,248/14,694 cycles.
The exact 1,000-iteration workload completes in 576,508,485 cycles against the
unchanged 675,000,000 ceiling with 7,986 pages free, 2,027 context switches,
4,426 delivered ticks, syscall count `0x69F7`, and zero overruns. Its K8 maxima
are 25,328/43,938/43,278/14,724 cycles.

A clean, non-reused Beast Verilator 5.047 build completed in 25.228 seconds and
its pin-level SDRAM run completed in 377.592 seconds. It passes the intentional
64 KiB BIST at 115.03 MB/s, PMMU isolation, exact K8 lifecycle counts, every
K1-K8 marker, and all 20 cycle gates. Area create/map/unmap/ring-notify maxima
are 37,762/56,097/71,283/29,390 cycles with zero overruns.

Exact normal artifacts are:

- kernel: 81,768 bytes, SHA-256
  `ea879e760c48342f535ee9aee65bf1bab97e855c2e576579c2ab80ef615ba55b`;
- kernel ELF: 112,736 bytes, SHA-256
  `81a8b2a65ef411c7fe77f5493f485e764e7c4b5331756055e56aea4e7104a3f5`;
- boot payload: 93,180 bytes, CRC32 `BE5F5D5D`, SHA-256
  `a70ae3323884ffb3eccaa70b4e4c6be34a2e0a4aa044cee99a632375b6faba2a`;
  and
- packaged ROM: 93,212 bytes, SHA-256
  `ae6bab5ab9a249211ef0b7f1daccb7e00ddb9e683facbe7ecfd7b0d6307d17a8`.

NUC loaded maintenance passthrough bitstream SHA-256
`2b423314c35ef00fc16929aaf72f536906abba4b602bfd79ab537e4b78185471`
only into volatile SRAM, then flashed and verified one-shot provisioning
AstraHost application SHA-256
`6033a507f51470bdda78c1924308e37f5d5af603cfac54e348da62417379e3ba`.
The existing 244,016 MB card was mounted without formatting. Provisioning
atomically changed only `/sdcard/ASTRA68.ROM`; a second independent pass
reported the exact 93,180-byte payload and CRC32 `BE5F5D5D` already matched.
Exact read-only production AstraHost application SHA-256
`1a822cd9bb08ce9c3dfb6292017e96967b1dada34de78b27200cea22c1227e6e`
was then restored and verified.

Production bitstream SHA-256
`78cd218f12feb72ccbdcb6bb141d19908c961f3438b6b559bf99b60d1c9d6940`
was verified before each of two independent volatile loads. Both boots report
build `25D9CB8E`, exact commit and ROM CRC32 `BE5F5D5D`, complete physical
32 MiB POST/BIST, PMMU/user-copy isolation, exact K8 counts, every K1-K8
marker, and all 20 cycle gates with zero overruns. Run 1 completes in 4.158
host seconds and measures area create/map/unmap/ring-notify at
37,763/56,091/71,263/29,416 cycles. Run 2 completes in 3.876 seconds and
measures 37,742/56,106/71,263/29,416.

Key accepted evidence SHA-256 values are:

- `evidence/astra68-k8-56bd177-host-tests.log`:
  `45ac1cebae220e55207ddeae0b99bf46a0baf914a312155adc766b03d336f993`;
- `evidence/astra68-k8-56bd177-musashi-normal.log`:
  `6c3781d1d8d5d9c87a72792bac3da507c3afda229db1f63a7169d5033dc68365`;
- `evidence/astra68-k8-56bd177-musashi-performance.log`:
  `8517652d853c84f2e902b422171bfbf01c6f28c2e94d7cec4dd4f03f2811f428`;
- `evidence/astra68-k8-56bd177-rtl.log`:
  `6a2bdff9164fed71200e23dc0084eb48c1df1261cd6c4537085138d10c6477f5`;
- `evidence/astra68-k8-56bd177-esp-provision-flash.log`:
  `e382002231435f714a79091e50ec336b10c83aab8af0bee6657f92963599e1ff`;
- `evidence/astra68-k8-56bd177-provision.log`:
  `c7ae9301b48ceed2fe831f64175662a7520ef6c2af1a8971f189c7b1c346f735`;
- `evidence/astra68-k8-56bd177-provision-verify.log`:
  `83e8ebf14aa82fefaa2a31253898a3023f7a9af52e4ba5a0d898c95cb0fc1a57`;
- `evidence/astra68-k8-56bd177-esp-production-flash.log`:
  `df3e49a9b03a25ad617ff62b284db6507e3c5a0e4b0ea94485fd9a2b71463aba`;
- `evidence/astra68-k8-25d9cb8e-56bd177-hw-1.log`:
  `7bab18bcf3881cece216a17a718982cc779f3d52de180fc1fb0ffab9ee9ff66d`;
  and
- `evidence/astra68-k8-25d9cb8e-56bd177-hw-2.log`:
  `3dab5148851e869102d89d412b35aa7a2650ac7a97cc726d861ae9adde56dd82`.

Disposition: K8 implementation, immutable-source reproduction, software,
fresh pin-level RTL, SD provisioning, production AstraHost restoration, and
physical hardware promotion PASS. No production SoC synthesis, placement,
route, pack, or persistent FPGA-flash operation was performed. Mapped resources
remain 53,079 LUT4s, 25,536 FFs, 101 DP16KDs, and 18 multipliers; packed
resources remain 66,523 TRELLIS_COMB and 25,565 FFs. The unchanged route passes
at 15.058201 MHz CPU, 66.907532 MHz SDRAM, 79.693970 MHz USB, 53.267990 MHz
pixel, and 289.771088 MHz HDMI shift. K8 is the qualified K9 rollback; K7 is
its predecessor.

## K7 bounded message-port checkpoint

K7 adds bounded copied-message IPC on top of K6 wait-multiple. The development
configuration has 16 receiver-owned ports and 32 fixed message records. A
message is 24-280 bytes and may move at most eight handles. Each port admits
1-8 queued messages and 24-2,240 queued bytes. One owner may hold four ports,
16 queued messages, 4,480 queued bytes, and 128 detached authorities; the
system detached-authority pool has 256 entries. Queue and authority storage are
fixed, charged to a named owner, and never grow in response to user input.

Creation returns a transferable send endpoint and a nontransferable receive
endpoint. Send is a reserve, complete-validate, copy, authority-export, and
single commit transaction. Any failure before commit leaves every source
handle usable and returns every reservation to baseline. Receive reserves
hidden destination handle slots, copies the message to user memory, then
publishes the handles and dequeues exactly once. A copyout fault cancels the
hidden imports without consuming the message. Final receive close wakes
blocked senders and receivers with peer-dead; process teardown releases every
queued message and detached authority exactly once.

The three kernel syscalls are deliberately nonblocking. The NDK implements
poll, blocking, and absolute-monotonic-deadline operations through the existing
K6 wait contract without resetting the caller's deadline. A failed-send
sequence token covers both message and byte capacity so a receiver transition
cannot be lost between observing backpressure and blocking. The receive
endpoint is waitable for data and the send endpoint is waitable for capacity or
peer death.

All 18 exact frozen host suites pass. The same implementation passes GCC
`-fanalyzer`, ASan/UBSan/leak checks, NDK host and MC68030 builds, generated
HTML/PDF documentation, Rustfmt, Clippy `-D warnings`, all 15 AstraVM tests,
all 90 shared tests, all 30 adapter executions, and both Harte smoke adapters.
Normal Musashi reaches every K1-K7 marker in 22,750,224 virtual cycles. The
exact 1,000-iteration workload completes in 563,507,415 cycles, below the
675,000,000-cycle ceiling, with 7,986 free pages, 2,024 context switches, 3,624
timer ticks, syscall count `0x6C96`, and zero overruns.

Two profiling defects were rejected rather than normalized. The first soak
crossed 675,000,000 cycles because exhaustive detached-pool and object-graph
validators ran during ordinary maintenance and owner reap. The retained build
uses transition-maintained O(1) corruption latches and keeps exhaustive scans
for host, maintenance-diagnostic, and milestone checks. A later accepted-send
path still scanned all 256 detached entries to compute a maximum-live statistic;
transition-maintained live and high-water counters replace it. Neither fix
raises a budget or weakens the validators.

A clean Beast Verilator 5.047 build completed in 25.657 seconds and the fresh
pin-level SDRAM run completed in 323.885 seconds. It passes 64 KiB BIST at
115.04 MB/s, PMMU isolation, exact K7 counts of three sends, three receives,
one backpressure event, two committed transfers, and one maximum detached
authority, every K1-K7 marker, and all sixteen cycle gates. Port send and
receive maxima are 12,256/25,000 and 17,817/30,000 cycles with zero overruns.

NUC mounted the existing 244,016 MB SD volume without formatting, updated only
`/sdcard/ASTRA68.ROM` to the exact 80,944-byte payload with CRC32 `B124CB22`,
then independently reported that the installed file already matched. Exact
read-only production AstraHost SHA-256
`1a822cd9bb08ce9c3dfb6292017e96967b1dada34de78b27200cea22c1227e6e`
was restored. Two independent volatile loads of unchanged routed FPGA build
`25D9CB8E` pass full physical 32 MiB POST/BIST, PMMU/user-copy isolation, exact
K7 counters, every K1-K7 marker, all sixteen cycle budgets, and zero overruns.
Run 1 measures port send/receive at 12,256/17,883 cycles; run 2 measures
12,257/17,880 cycles. Both report the frozen source identity and exact ROM CRC.

Key accepted evidence SHA-256 values are:

- `evidence/astra68-k7-815347c8d094-host-tests.log`:
  `3e1c2127e6e9aa5f20acdbf2b9f9e1bda80bf19fb8469dcca00a46f5eed4f375`;
- `evidence/astra68-k7-815347c8d094-musashi-normal.log`:
  `0f91cd0ff8825d62a6b2c656edea186ba5c147b306178af12e0b8f7610346e22`;
- `evidence/astra68-k7-815347c8d094-musashi-performance.log`:
  `a2d2da52bc4dd8aea8452e5118222a1dfa49e7d885cd4b99c2b02dff11045e2c`;
- `evidence/astra68-k7-815347c8d094-rtl.log`:
  `6e5adafaf55a228ae90ca7315c776341eaf5b9075190222168d16a9cac450629`;
- `evidence/astra68-k7-815347c8d094-provision.log`:
  `a3c15fe80ccaf05b1f86b61efe6808e6c25c263d6a3eb2c08ea11bdb9889a5c9`;
- `evidence/astra68-k7-815347c8d094-provision-verify.log`:
  `0ec8310eea6c218ec247f2e062a21794d53ca10362708e1eeb0f85d4f3cef542`;
- `evidence/astra68-k7-815347c8d094-esp-production-flash.log`:
  `ab75830e96c3fe15532fb5941af6e750a7fbd912631f61e106e97b1d6ea3dfcd`;
- `evidence/astra68-k7-25d9cb8e-815347c8-hw-1.log`:
  `017ae37943f5c46d1ff8faa26f653ed1fbcbc0db7c02ce16babe2aecb1a04b91`;
  and
- `evidence/astra68-k7-25d9cb8e-815347c8-hw-2.log`:
  `85a40eea380e5dc25b88570db85f92d0fd4d6d6af64beff44bd28a05d97c12c5`.

## K6 bounded wait-multiple checkpoint

K6 extends the existing wait substrate with one bounded `WAIT_MULTIPLE`
operation over events, semaphores, one-shot timers, thread death, and process
death. A call accepts 1-16 descriptors, completely validates every handle,
right, type, duplicate, and output range before changing kernel state, then
uses the caller's fixed registration array and existing deadline linkage. It
allocates no memory and creates no helper thread. If more than one member is
ready, the lowest input index wins. Duplicate descriptors are legal and retain
that same input-order rule.

Every terminal path claims the wait exactly once. Signal, timer expiry, thread
exit, process exit, timeout, cancellation, final-handle close, and owner death
all remove every losing registration and the single deadline before publishing
the result. Process and thread death are level-triggered and return the retained
32-bit exit detail. Waiting on the calling process is rejected. Process handles
may be granted only during prestart bootstrap, so a process cannot acquire new
authority after it becomes runnable.

Timers use a shared 32-object fixed pool and a deterministic fixed-capacity
heap. `TIMER_SET` arms or rearms one absolute monotonic deadline; a past or
current deadline becomes immediately ready. `TIMER_CANCEL` disarms the timer
without manufacturing an expiry. Expiry remains level-triggered until rearm,
and close or owner death removes an armed timer exactly once. No timer, wait,
wake, cancellation, or teardown path allocates.

All 17 host suites pass normally, under GCC `-fanalyzer`, and under
ASan/UBSan/leak checks. NDK checks, analyzers, sanitizers, MC68030 builds,
examples, generated HTML/PDF documentation, Rustfmt, Clippy `-D warnings`, all
15 Rust tests, all 90 shared framework tests, all 30 adapter executions, both
Harte smoke adapters, and directed Vesta IRQ/timer tests pass. Normal Musashi
reaches every K1-K6 marker with exact wait/wake/timeout counts of 11/5/5,
wait-multiple counts of 7 calls, 4 blocks, and 4 wakes, a maximum two-member
set, one live and three maximum registrations, one timer create/arm/expiry,
and one nonblocking process-death wait.

The exact 1,000-iteration K6 Musashi workload completes in 579,507,730 virtual
cycles against the unchanged 675,000,000-cycle cap. It retains the 7,986-page
free baseline, performs 2,024 context switches and 3,730 timer ticks, issues
`0x6DA0` syscalls, and reports zero cycle-budget overruns. A fresh full-SoC
Verilator build and pin-level SDRAM run passes 64 KiB BIST at 115.04 MB/s,
PMMU isolation, every K1-K6 marker, the exact K6 counters, and all fourteen
cycle budgets. Its wait-set block/wake maxima are 6,259/50,000 and
10,049/50,000 cycles; all other retained maxima also remain within their fixed
limits.

An intermediate full-RTL checkpoint measured final-handle close at 62,069
cycles. Audit found an exhaustive all-process/all-handle validator in the
production hot path. The retained implementation replaces that scan with
constant-time corruption latches while preserving the exhaustive validator in
host, maintenance, and milestone checks. That optimization is semantic, not a
raised budget.

NUC mounted the existing 244,016 MB card without formatting and changed only
`/ASTRA68.ROM`; the provisioner verified 68,860 payload bytes and CRC32
`80B0364C`. The normal read-only AstraHost was then restored. Two independent
volatile loads of unchanged routed FPGA build `25D9CB8E` pass full 32 MiB
POST/BIST, PMMU/user-copy isolation, exact K6 counts, every K1-K6 marker, all
fourteen cycle budgets, and zero overruns. Run 1 measures wait-set block/wake
at 6,267/10,045 cycles; run 2 measures 6,254/10,041 cycles. Both report the
frozen source identity and exact ROM CRC.

Key accepted evidence SHA-256 values are:

- `evidence/astra68-k6-04524898-musashi-normal.log`:
  `bdd9700424646c59ab143f87a16c7901c0139d12117b587a8ce876e4261a2af5`;
- `evidence/astra68-k6-04524898-musashi-performance.log`:
  `5f550a791b28b69bb98bd2defaa31ee211b4cec255ec221fdb9f7d1742af3c8d`;
- `evidence/astra68-k6-04524898-rtl.log`:
  `73355c2eb5075df985a5aa4ec9b34ffbf5fb1d6e6fcb06f9a25d2f88bd080adc`;
- `evidence/astra68-k6-04524898-provision.log`:
  `cf4937dc014b943c2f9e59a25920f9763b374784a5530216f51c8c78e998df37`;
- `evidence/astra68-k6-04524898-esp-production-flash.log`:
  `dd92746edf357f7df851ecfa879f57955b76d0f3bb5609678f5313fcdf2b02d0`;
- `evidence/astra68-k6-04524898-hw-1.log`:
  `1fb84795ee08f96735ed2625aacfd81f1afe58d5a56c0d46706e888d05a5ac96`;
  and
- `evidence/astra68-k6-04524898-hw-2.log`:
  `77ce8067efabaf42c0541309eaa1fcf67264c1ee6dc1edb7ad10cf402e21f141`.

## K5 thread-lifecycle checkpoint

K5 exposes the existing scheduler object through `THREAD_CREATE`, caller-only
`THREAD_EXIT`, and thread-death `WAIT_ONE`. Creation validates the executable
entry, priority, argument, and rights before reserving one global thread slot,
one fixed supervisor-stack slot, one process stack slot, one zeroed 4 KiB user
frame, one mapping, one handle, and the corresponding exact quota charges.
Preparation may run with interrupts enabled. Publication occurs only after
every reservation succeeds and is a no-allocation commit with interrupts
masked: handle visibility, ready-queue insertion, live-thread accounting, and
quota accounting become visible together. Directed failure at each reservation
and publication boundary restores handle, mapping, frame, ready-queue,
stack-slot, and quota baselines exactly. A deterministic host regression
injects a supervisor timer at the final enable-to-disable boundary, expires a
second waiter, and proves that timer enqueue and thread publication both remain
linked in the ready queue.

Normal exit records one 32-bit status, wakes death waiters in priority/FIFO
order, and moves user-stack destruction to the guarded worker after execution
has left the exiting supervisor stack. A still-open handle retains only the
bounded thread record and fixed supervisor-stack slot. Repeated waits are
level-triggered and return the same status; timeout, cancellation, final-handle
close, process death, and normal exit arbitrate one terminal result. Closing a
live thread's final handle never kills it, and K5 intentionally provides no
asynchronous thread-kill operation.

All 17 host suites pass normally, under GCC `-fanalyzer`, and under
ASan/UBSan/leak checks. NDK host/sanitizer/analyzer, MC68030 library/example,
and generated-documentation checks pass. Rustfmt, Clippy `-D warnings`, all 15
Rust tests, all 90 shared framework tests, all 30 Musashi/RTL executions, and
both Harte smoke adapters pass. A forced clean MC68030 build on Beast
produces and verifies all three qualified artifacts. Exact normal Musashi
reaches every K1-K5 marker in 20,000,212 virtual cycles. Its 1,000-cycle
workload finishes at 600,506,981 cycles under the unchanged 675,000,000
ceiling, retains exactly 7,986 free pages, performs 2,062 context switches and
9,085 syscalls, and reports zero overruns. Musashi maxima for create, exit, and
deferred reap are respectively 89,379/150,000, 7,046/50,000, and
33,748/125,000 cycles.

The first fresh pin-level run measured thread creation at 162,250 cycles and
correctly failed the 150,000-cycle acceptance limit. The cause was duplicate
initialization: the generic allocator poisoned the new page before creation
cleared the complete stack. A shared zero-filled allocation path writes the
page once and is covered by the memory tests. That first corrected checkpoint
passed every backend, but final audit rejected it after finding that its
syscall enabled interrupts across non-atomic ready-queue publication. The
accepted split prepare/commit implementation adds the exact boundary regression
described above. Its clean Verilator 5.047 build, without simulator reuse,
passes 64 KiB BIST at 115.04 MB/s and every K1-K5 marker in 296.331 seconds. It
measures create at 137,193/150,000, exit at 14,804/50,000, and reap at
47,560/125,000 cycles, with zero overruns. Both rejected checkpoints remain in
evidence.

NUC mounted the existing 244,016 MB card without formatting and atomically
updated only `/sdcard/ASTRA68.ROM` to the exact 59,804-byte payload with CRC32
`11BE3620`. A fresh 268,320-byte production AstraHost built with both
provisioning options off was restored; its SHA-256 is
`22740476f66e1394f0a6363a4c24d7ce9aa5a5980c656d2edd52711ac1c1d381`.
Two independent volatile
loads of production bitstream `25D9CB8E` each report that exact CRC, complete
physical 32 MiB POST/BIST, PMMU and user-copy isolation, one thread exit, two
death waits, one deferred reap, every K1-K5 marker, and zero overruns. Run 1
measures create/exit/reap at 137,107/14,816/47,534 cycles; run 2 measures
137,101/14,812/47,534 cycles.

Superseded/rejected K5 evidence remains retained:

- `evidence/k5-020f9460-musashi-normal.log`:
  `3e86cd1410b2846a1aa479db481def1d2c1b8fdfe25898fe076851f1079319d0`;
- `evidence/k5-020f9460-musashi-performance.log`:
  `d8f8e71907b1c4442fb6ca28eadaa8e5fc46259740f290e900d4f6feba48c9f4`;
- `evidence/k5-020f9460-rtl-create-budget-rejected.log`:
  `6d7ecd4e853c2865b1dad472cb6cf138b239f5924944d4dd7baf4469640a8972`;
- `evidence/k5-020f9460-rtl.log`:
  `f609162ec62b0de53a1fe02e53f623e3cbd07195841c317cde98fcf2f5e9cf10`;
- `evidence/k5-25d9cb8e-020f9460-provision.log`:
  `21d68093ecf4e77b420b3cc128ca01a1bf2ca6baae1b718dfe172a6776f1043e`;
- `evidence/k5-25d9cb8e-020f9460-hw-1.log`:
  `c4ce877436276f4af55dfa983b789759b022b9fffbd3e26c43f4fb9cba68759f`;
  and
- `evidence/k5-25d9cb8e-020f9460-hw-2.log`:
  `472589e0cdb827a5d8ec3d40db22af123024b5764c0727d4fe1fff53cff9d24d`.

Accepted K5 evidence and SHA-256 values are:

- `evidence/k5-1a234d1e-musashi-normal.log`:
  `a8ca8dd640c4d10ca45d11b1957e798c193ff0a0a550af36a21f2bd0a9d20dc0`;
- `evidence/k5-1a234d1e-musashi-performance.log`:
  `6f09f9175f8ec39a7f825bac7e701ed8c22cc517162f001c1ec01f90cbe726b7`;
- `evidence/k5-1a234d1e-rtl.log`:
  `b26f7075305b23b1f445aeb2639d7d20bf22a7dfab8f5a5c8fa305a8253d66c1`;
- `evidence/k5-25d9cb8e-1a234d1e-provision.log`:
  `8a380195cf4cab6a912f09a7a358b5a5a825006c3967b5c6bd9e5128303a0aca`;
- `evidence/k5-25d9cb8e-1a234d1e-esp-production-flash.log`:
  `33f747b22720e471030cb568b09c7f66887a0c1a3301f91ae0ff2ca926e963b1`;
- `evidence/k5-25d9cb8e-1a234d1e-hw-1.log`:
  `d915b9629ab0c77d3c8750e2b593eca7032329b0278a2d4cff87a9872e316d38`;
  and
- `evidence/k5-25d9cb8e-1a234d1e-hw-2.log`:
  `c4a069f6cc2abccc09a86e815060ee66c314b518eaa91daf0420137e9914d753`.

This is a production-software-only promotion. The maintenance passthrough was
rebuilt and loaded into volatile SRAM solely to restore AstraHost. Production
SoC synthesis, placement, routing, resources, constrained clocks, bitstream
hash, and persistent FPGA flash remain the exact qualified `25D9CB8E` values.

## Closed K1 release requirements

| Requirement | State |
|---|---|
| move resource destruction out of hard IRQ and IPL-7 user fault | CURRENT HW; host asserts exclusion and ULX3S reports 8,834-cycle maximum |
| supervisor stack guard in SRP | CURRENT HW; exact format-A vector-2 fault at `0x02028000` passes full RTL and physical HDMI/log |
| exact cache synchronization/alias test for loaded user code | CURRENT SIM, hardware remains |
| committed nonzero ROM/Git identity | CURRENT HW; exact `bbb1616a...` normal and soak ROMs boot on ULX3S |
| full normal/direct-panic/guard-panic RTL rerun | CURRENT from exact `77B3CDC8`; direct panic and exact `0x02028000` guard panic both preserve retained logs |
| Motorola RESET/ATC preservation and boot-flush regression | CURRENT RTL/ROUTED/HW; automatic reset-from-flash K1 boot passes |
| exact 12.5 MHz CPU / 60 MHz SDRAM complete route | CURRENT; all clocks, LUT permutation, POR, font ROM, and `kernel_platform_v1` gates pass without waiver |
| repeated ULX3S POST, SDRAM, PMMU, timer, fault, HDMI | CURRENT; three exact SRAM boots, physical HDMI, and automatic reset-from-flash boot pass |
| masked user-fault latency | CURRENT HW; 8,834 cycles against 125,000-cycle gate |
| long context/syscall/fault/allocation soak | CURRENT HW; dual-host 500,000-cycle simulation, routed five-minute/5,000-cycle candidate, and independent 30-minute/29,000-cycle release run pass at baseline 7,987 with coherent FPGA elapsed-time proof |
| panic HDMI and retained-log check on physical board | CURRENT; exact direct-panic and supervisor-guard paths pass |
| guarded-worker source exact route and ULX3S promotion | CURRENT HW; no-waiver route, repeated SRAM boots, five-minute soak, restoration, reset-from-flash, and physical HDMI pass |

## Partial or transitional current code

- Four process slots, 16 global thread slots, 16 handles/process, and the
  resulting 15-thread/process cap are qualification limits, not the stable
  limits in `KERNEL_ARCHITECTURE.md`.
- The fixed-priority queue/bitmap scheduler, 5 ms one-shot quantum, and bounded
  absolute-cycle deadline heap are current. Runtime high-priority signal and
  timeout handoff are proven through the public K4 handle path on host and
  Musashi. Address-space affinity among equal priorities, inheritance,
  donation, wakeup boost, and real-time budgets are not built.
- K10 retains K8 runtime thread creation, caller-only exit, waitable
  thread/process death, timers, bounded wait-multiple, message ports, shared
  areas, and SPSC rings
  have a documented provisional ABI `0x00010005` handle/syscall contract. Stack
  size is fixed at 4 KiB, pool/handle/queue capacities remain development
  limits, and there is deliberately no asynchronous thread kill.
- Ordinary user threads have guarded user and supervisor stacks. Events,
  semaphores, timers, thread death, and process death are public waitable
  objects through wait-one and wait-multiple. Bounded message ports provide
  copied control IPC and atomic handle movement; shared areas and rings provide
  bulk IPC without kernel payload copies. Service discovery does not yet exist.
- `block.c` and `dma.c` qualify ownership, generation, pin, completion, and
  revocation. Filesystem/block policy still belongs in a user service.
- The fixed K1 worker now services process reap plus eight bounded K10 device
  classes. It is not yet a general scheduler-owned kernel-thread class and has
  no general wait object, priority inheritance, or independently scheduled
  per-device work queues.
- User-fault retirement is minimal and schedules the interruptible worker.
  Synchronization cancellation, wait-multiple cleanup, timer expiry,
  thread/process owner-death arbitration, port/ring peer-death cleanup, and
  area mapping revocation are current; service supervision and restart policy
  remain.
- The diagnostic UART mirror is emergency FTDI output only. ESP runtime
  communication is SPI.

## Not implemented

- bounded general heap, disposable-cache reclamation, user-service pressure
  notification, and service-level low-memory policy;
- demand-zero pages, stable-default 8 KiB page option, general process commit
  accounts beyond K8 area quotas, and cache-safe executable/file-page
  reclamation;
- priority inheritance and priority donation;
- service restart, privileged device-mapping objects, and independently
  scheduled user-service deferred handlers;
- physical interactive qualification of the implemented SPI/FTDI monitor;
- graphics-service command ring/mapping revocation and dedicated graphics RAM
  arena.

## Known invariants and tests

| Invariant | Test state |
|---|---|
| user cannot map kernel/page-table frames | host VM tests |
| null and W+X mappings rejected | host VM tests |
| stale handles cannot reach reused slot | host handle tests |
| stale thread IDs cannot reach reused slot, including slot 15 | host thread tests |
| failed thread creation restores every frame, mapping, handle, stack slot, ready link, and quota | host process fault-injection matrix |
| a zeroed thread stack is published without a redundant poison/clear pass | host memory/process tests plus pin-level create budget |
| wait-multiple completely prevalidates before registration and unwinds every losing member exactly once | host process/sync race matrix, Musashi, and pin-level RTL |
| simultaneous ready members choose the lowest input index, including duplicate descriptors | host process/sync directed races and target K6 markers |
| port message and byte capacities remain bounded and charged to the receiver owner under backpressure | host port quota/exhaustion matrix, Musashi, pin-level RTL, and two K7 ULX3S boots |
| handle movement is all-or-nothing across invalid input, detached-pool exhaustion, copy faults, peer death, and destination-slot exhaustion | host handle/port failure injection including the partial detached-pool case, Musashi, and pin-level RTL |
| failed receive copyout leaves the message queued and publishes no destination handle | host page-boundary/copy-fault matrix and target user-copy qualification |
| final receive close wakes blocked peers and releases every queued message and detached authority exactly once | host close/owner-death races, Musashi exact counters, and pin-level RTL |
| failed area create/map publication restores every frame, descriptor, leaf, mapping record, handle, reference, and charge | host allocation-failure matrix, Musashi, fresh pin-level RTL, and two K8 ULX3S boots |
| one shared area has one cache-policy-compatible logical address across every process and is revoked before frame reuse | host alias/revocation matrix, Musashi, fresh pin-level RTL, and two K8 ULX3S boots |
| ring full/empty/wrap, notification, wait, corruption, close, peer death, and creator death return every endpoint and waiter to baseline | host ring/lifecycle matrix, Musashi, fresh pin-level RTL, and two K8 ULX3S boots |
| cache allocation bitmaps, module states, ledger units/bytes, and physical frame site IDs agree after every transition | 21 host suites, analyzer, sanitizers, normal/performance Musashi, fresh pin-level RTL, and two K9 ULX3S boots |
| all 22 injectable allocation sites fail through both global-Nth and site-Nth selectors without publication or resource drift | exhaustive host allocation matrix and target K9 milestone checks |
| ordinary allocation cannot consume the 32 reserve pages; reserve exhaustion and owner release restore exactly 32 | host exhaustion/zero-free tests, Musashi, fresh pin-level RTL, and two K9 ULX3S boots |
| boot allocation retires once with zero live boot units and rejects every later boot-only request | host phase-transition tests, Musashi, fresh pin-level RTL, and two K9 ULX3S boots |
| user-fault and process teardown complete with zero ordinary free pages and retained logging remains writable | host zero-free cleanup test plus target allocation-free teardown invariants |
| fixed wait registrations return to baseline after signal, timeout, cancel, close, thread death, and process death | host accounting tests, Musashi exact counters, and pin-level RTL |
| timer heap ordering and equal-deadline behavior are deterministic without allocation | host timer/process tests, directed Vesta timer test, Musashi, and pin-level RTL |
| stale process handles cannot name a replacement process and self-wait is rejected | host process/handle tests plus target K6 process-death path |
| thread death, timeout, cancel, final close, and process death complete one wait exactly once | host thread/process/sync race tests, Musashi, pin-level RTL, and two K5 ULX3S boots |
| dead thread retains only its bounded record/supervisor charge until final close | host accounting/reap tests and target lifecycle marker |
| stale thread handle cannot name a replacement object after reap and reuse | host process/handle tests plus target stale-handle rejection |
| higher priority wins and equal priorities remain FIFO round-robin | host thread/process tests plus K3 target boot |
| same-process switch preserves CRP and bypasses VM switch accounting | host process test, Musashi, full RTL, and ULX3S K3 boots |
| stale wait sequence cannot block after a condition change | host thread/sync tests |
| wake-one is priority ordered and FIFO among equals | host thread test plus target priority-handoff path |
| event close wakes each waiter exactly once | host sync/process tests plus exact K4 Musashi |
| process death withdraws every sibling from ready/wait queues before deferred record reuse | host process test, Musashi soak, full RTL, and ULX3S K3 boots |
| each thread supervisor stack has an unmapped guard, valid canary, and bounded high-water | host VM/thread tests, Musashi, full RTL, and ULX3S K3 boots |
| every measured K8 hot path stays within its fixed cycle budget under K9 accounting | host profiler tests, Musashi, full RTL, and two K9 ULX3S boots |
| 1,000-iteration Musashi workload remains at or below 675,000,000 virtual cycles | automated emulator performance gate |
| ordinary syscalls do not renew the running thread's quantum | host process test, Musashi, full RTL, and two K3 ULX3S boots |
| timer always targets the earlier active quantum or wait deadline | host platform/process tests plus K3 target timeout handoff |
| equal deadlines are deterministic and deadline capacity equals thread capacity | host thread tests |
| timeout, signal, cancellation, close, and process death remove one deadline/waiter exactly once | host thread/sync/process tests plus exact K4 Musashi |
| higher-priority timeout wake preempts immediately | host process test, Musashi, full RTL, and two K3 ULX3S boots |
| killed owner releases mappings/handles/frames | host process test, target K1 |
| pinned DMA delays but does not lose teardown | host process/DMA/block tests |
| user-copy fault returns error, unrelated kernel fault panics | host and full target |
| nested timer on supervisor exception resumes handler | host dispatch and focused RTL |
| M=1 IRQ returns through format-1 ISP then format-0 MSP frame | Motorola-directed Questa plus full pin-level normal/soak |
| worker cannot lose signal at service or block boundary | host worker state machine plus target lifecycle soak |
| worker stack guard is invalid; canary/high-water remain readable | host VM/worker tests plus Musashi/full RTL boot |
| fault after repeated traps stacks on ISP | focused Questa wait/zero-wait |
| hard timer path performs no maintenance/destruction | host dispatch test |
| user-fault path performs no maintenance/owner release | host dispatch/process tests; bounded Musashi, full RTL, and ULX3S reports |
| owner release visits only that owner's frames | host memory test, visit counter |
| pinned owner release is atomic and owner slots are bounded/reusable | host memory tests |
| kernel stack guard descriptor is invalid and adjacent pages remain mapped | host VM test |
| kernel stack guard access reaches retained panic path | full RTL and physical hardware at exact address `0x02028000` |
| duplicate user cached alias is rejected and reusable after unmap | host VM test |
| cross-CRP same-address code/data cannot expose stale bytes | Musashi and full RTL K1 |
| IRQ readiness cannot be lost between test and blocking; close/death wakes every waiter exactly once | host IRQ/process race matrix and complete pin-level K10 boot |
| stale IRQ handles and acknowledgement sequences cannot affect a reused endpoint | host IRQ generation/reuse matrix and target K10 counters |
| hard IRQ, fault record, trace, endpoint revoke, and monitor-pressure paths allocate no memory | 28 host suites, zero-free injection, analyzer/sanitizer gates, and target K10 qualification |
| a matched SDRAM fabric timeout is system-fatal rather than offender-contained | dispatch host regression plus focused SDRAM BERR RTL |
| malformed syscall corpus never panics | MISSING |
| every allocation site unwinds exactly | CURRENT; all 22 injectable sites pass global-Nth and site-Nth failure matrices with exact resource baselines |

## Next actions

1. freeze the K10 source identity, pass the exact 12.5 MHz CPU / 60 MHz SDRAM
   production route, then complete two ULX3S boots and interactive FTDI/SPI
   monitor, IRQ stress, HDMI, and exact-identity checks;
2. add the malformed-syscall corpus before exposing device mappings or
   starting a protected device service;
3. grow development pools only from measured workload data, and implement and
   benchmark 8 KiB pages against the retained 4 KiB oracle before freezing the
   stable VM ABI.
