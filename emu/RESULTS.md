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
| AstraVM Rust unit/integration tests | 15/15 on `beast` after K1 integration |
| Standalone Astra MC68030 PMMU groups | 15/15 |
| Musashi/MC68030 PMMU integration groups | 13/13 |
| Vendored Musashi instruction binaries | 79/79 |
| Shared CPU/PMMU fixtures | 5/5, including no-FPU and table-bus failure |
| Maintained Harte MC68030-compatible gate | 96,103/96,103 |
| Maintained Harte ordinary full-state diagnostic | 256,894/256,894 |
| Rust Clippy (`-D warnings`, all targets) | pass |
| Optimized workspace build | pass on macOS and `beast` |

## K1 translated-kernel boot

Verified 2026-07-22 on `beast` with Rust 1.97.0. The exact 35,260-byte boot
binary used by the full pin-level RTL run has SHA-256
`7fa58266c26a3b3d679254235c3be2ad079f5f8710174940e6246e52e820022b`.
AstraVM ran it unchanged through complete POST, PMMU enable, separate SRP/CRP
user-copy recovery, two isolated user processes, 100 Hz timer preemption,
trap-based syscalls, and a deliberate format-B user fault. The offender alone
was reaped and its owned address space, handles, and frames were released. The
final snapshot was cycle 12,500,134 with PC `0x02013DB2`, three context
switches, and kernel scratch status `K1OK`.

The same gate models the current front-panel aperture and Vesta periodic
timer/IRQ contract. A level interrupt is recomputed immediately after MMIO
writes so clearing `TMR_EXPIRED` deasserts IPL before `RTE`, matching the
physical controller rather than waiting for a host timeslice boundary.

## K1 lifecycle soak

Exact source `470bf123cf24bbadf3525f91307e3d9aebe92006` adds a deterministic
offender-fault, teardown, baseline-accounting, and relaunch loop without
changing the CPU/PMMU model. The exact 24,876-byte kernel has SHA-256
`5c540492ca7aaf8bf3d2270f17e2bf0a4f6f65e2a8bb4aec5c3ee019338c56c5`.

An optimized Musashi run completed 100 lifecycle cycles at virtual cycle
262,502,952 with 201 context switches, 411 timer ticks, 1,377 syscalls, and
7,987 free pages. The release-duration 500,000-cycle run passed its 50,000
checkpoint at virtual cycle 125,291,837,865 after 4,363.506 seconds and continues
from the same immutable NUC snapshot. A progress checkpoint is evidence that
all counters returned to baseline through that point, not a completed long-soak
claim.

The same committed kernel also passes the complete pin-level RTL/SDRAM model.
After `K1 PROTECTED ENTRY PASS`, RTL completes four lifecycle cycles with 11
context switches, 23 timer ticks, 96 syscalls, and the identical 7,987-free-page
baseline before reporting retained status `K1SK`.

## Deliberate model boundaries

- Musashi instruction timing is not a cycle-accurate TG68K.C bus model.
- The BIST exposes the hardware phase/progress/status contract and final
  complemented memory pattern, but completes on deterministic virtual timing
  rather than simulating the 60 MHz SDRAM controller transaction by transaction.
- The boot cache-coherence program behaves correctly, but AstraVM does not yet
  model the TG68K instruction/data caches or performance counters.
- Only hardware reached by the current ROM POST is implemented. Unmodeled MMIO
  reads return zero and writes are ignored until a device contract is added.
  In contrast, an unmapped physical PMMU table transaction explicitly fails
  and becomes a Motorola table-bus fault.
- POST does not enable address translation. The unchanged K1 kernel image now
  provides the end-to-end PMMU workload; independent and shared conformance
  suites remain the broader architectural oracle.
