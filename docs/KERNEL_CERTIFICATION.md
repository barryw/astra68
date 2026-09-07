# Axiom MC68040 kernel certification

Status: **PASS**, 2026-09-07

This gate covers Axiom on the DE25-Nano's QEMU TCG MC68040. It does not certify
the remaining systemd/NTP/display cold-boot path or filesystem throughput.

## Certified artifacts

- Production release identity:
  `8786083b44a9b91d3e5a8123093cc855953497824ee1239ab318c9c0b898bf0f`
- Production ROM:
  `a8e34319e74ac9efc60f1056fc452cba5566a462a997b0c60e36a69a7784b685`
- Production kernel ELF:
  `b729e23f82da0a6477fed727bfa071db7c0c1f10dd89599a47572aa552bcca75`
- Production kernel binary:
  `6010c35f9a3c1e2e40c103369d7f1b3ad6810865020bc25741987b634f0cc199`
- Physical K1-K10 qualification ROM:
  `4f596cc90409a7e90bb8ac382e1191adb96d21a850f5e2fc36b51a17c71967a6`
- Physical soak ROM:
  `a56d7f35d59cf0f3966507d596a6aef9cffe0573a251915369e2127aa60f379b`
- Physical panic ROM:
  `698ac6b255517b5af36b5a78b2a359da9b63449f784bfe668c79166dc587d0ba`

Every target object was built for `-m68040`; QEMU rejects any other CPU model.
No MC68030 MMU, exception-frame, standalone core-test, TG68, or Musashi path
remains.

## Evidence

| Gate | Evidence | Result |
| --- | --- | --- |
| Host behavior | Complete 30-test kernel suite | PASS |
| Undefined behavior | Clang ASan + UBSan, including a repaired zero-length diagnostic-copy test oracle | PASS |
| Data flow | GCC 13 `-fanalyzer` | PASS |
| Static analysis | Clang 18 Static Analyzer and exhaustive cppcheck warning/performance/portability profile | PASS, zero findings |
| Coverage | 15,563 / 19,418 lines | 80.1% |
| Failure atomicity | Allocation-site injection, pool exhaustion, VM/process/thread/handle/port rollback, emergency reserve isolation and replenishment | PASS |
| Physical qualification | Three DE25 K1-K10 runs on Cortex-A76 CPU2 | PASS |
| Physical soak | 18,000 fault/create/reap cycles; 30,945 free pages at every checkpoint | PASS |
| Panic retention | Launcher report and retained panic log byte-identical | PASS |
| Production stack | 9 processes, 14 threads, 52,941 syscalls; maximum 4,268 / 8,192 bytes | PASS |
| Four-core host placement | input/log CPU0, display CPU1, vCPU CPU2, QEMU main/AIO/filesystem workers CPU3 | PASS |

The physical qualification logs are retained on Beast as
`/tmp/astra-kernel-cert-stackopt-20260907/qual-{1,2,3}.log`; their SHA-256
values are respectively
`0d241ef449d41a0d4a189923cbadcd79f9dbe9c6a3cde80995c88996e12a4e02`,
`115fc96cc12e3fbc34ca7e2894e919949ee648bba2b2d6121b3ff621b850122e`,
and `218236b96a416aef6292251a49eaa835ff473b3558dafd287151a10bbeb72db1`.
The soak log is `/tmp/astra-k1-040-soak-20260907.log`, SHA-256
`9e0c62cb381c0ed64cb83926bb90edc508dfad664ba968c022b3e726b1b452b0`.
The production GDB snapshot is `/tmp/astra-prod-kernel-stack-20260907.log`,
SHA-256
`fce2a82f581fd2a995d419b3dc959f6abd21613cfe39e81920d157743a7852c1`.

## Physical performance

The three deterministic physical runs reported 62.362, 61.714, and 62.411 MHz
effective. The first two measured the same kernel paths in guest timebase
cycles: syscall 3,382; timer 631; user fault 22,991; scheduler pick 34;
same-address-space switch 65; cross-address-space switch 70; wait block 74;
wake 111; port send 327; port receive 779. The third varied by one guest tick in
a few paths. These are comparable instruction-timebase measurements, not
literal Cortex cycles.

The production image measured 72.227 MHz effective during the retained manual
boot. It recovered and mounted the journaled volume, completed its block
round-trip and IRQ handshake, and launched storage, hostfs, network, and ntpd.

Generated MC68040 inspection found native `movec`, `pflusha`, `pflushan`, and
address-selective `pflush` in the MMU path, `bfffo` in ready selection, and the
shared assembly copies in user-copy paths. Moving serialized process-launch
scratch out of the common syscall stack reduced the dispatcher frame from
4,292 to 712 bytes in qualification and 716 bytes in production. Physical K1
used 3,244 / 8,192 bytes; the full production launch path used 4,268 / 8,192,
leaving 3,924 bytes.

## Disposition

The kernel gate is closed. The rejected `-O2` image is not part of this
certification because it failed to finish the qualification workload after the
deliberate user fault; its larger size was not the rejection reason.

The next release gate is a cold boot of the selected immutable release through
the native NTP synchronization barrier, stage 8, the real terminal display,
the command acceptance gate, and repeated boots. Filesystem performance work
then resumes against this certified kernel baseline.
