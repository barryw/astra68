# AstraVM verification results

Verified 2026-07-14 with Rust 1.97.0 on Apple Silicon macOS and x86-64 Linux
(`beast` and `nuc`).

## Real-ROM boot

| Check | Result |
| --- | --- |
| Embedded ROM SHA-256 | `29569e80544d7543401cb405108a62cd9a57fa01c3d2eb868f8e7a24eff086e7` |
| Reset vectors | SP `0x02000000`, PC `0xFFE00400` |
| Execution backend | vendored Musashi 68030 with Astra PMMU, no FPU |
| POST milestone | `POST PASS` / `READY FOR OS LOADER` (CPU continues) |
| 250K-timeslice ready snapshot | `7,750,039` virtual cycles |
| PC at ready snapshot | `0xFFE0089C` (CPU continues after the marker) |
| Screen output | identical across 250K-cycle and 50K-cycle host timeslices |
| UART output | identical after draining the bounded post-ready serial tail |
| macOS native window | pass, wgpu/Metal |
| Linux native-window smoke test | pass, wgpu under Xvfb |

The emulator runs the unchanged ROM. Its reset alias, ROM, BRAM, big-endian
SDRAM, Vesta identity/UART/BIST, Vega bootstrap text, and Astraea fill/copy are
implemented as software-visible hardware contracts. The display transcript is
therefore guest output, not a host-authored imitation.

## Automated suites

| Suite | Result |
| --- | --- |
| AstraVM Rust unit/integration tests | 13/13 on `beast` after audit fixes |
| Standalone Astra MC68030 PMMU groups | 15/15 |
| Musashi/MC68030 PMMU integration groups | 13/13 |
| Vendored Musashi instruction binaries | 79/79 |
| Shared CPU/PMMU fixtures | 5/5, including no-FPU and table-bus failure |
| Maintained Harte MC68030-compatible gate | 96,103/96,103 |
| Maintained Harte ordinary full-state diagnostic | 256,894/256,894 |
| Rust Clippy (`-D warnings`, all targets) | pass |
| Optimized workspace build | pass on macOS and `beast` |

## Deliberate model boundaries

- Musashi instruction timing is not a cycle-accurate TG68K.C bus model.
- The BIST exposes the hardware phase/progress/status contract and final
  complemented memory pattern, but completes on deterministic virtual timing
  rather than simulating the 75 MHz SDRAM controller transaction by transaction.
- The boot cache-coherence program behaves correctly, but AstraVM does not yet
  model the TG68K instruction/data caches or performance counters.
- Only hardware reached by the current ROM POST is implemented. Unmodeled MMIO
  reads return zero and writes are ignored until a device contract is added.
  In contrast, an unmapped physical PMMU table transaction explicitly fails
  and becomes a Motorola table-bus fault.
- The ROM does not enable address translation during POST. PMMU execution is
  covered by the independent and Musashi-integrated conformance suites above;
  kernel boot will become the end-to-end PMMU workload.
