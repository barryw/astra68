# Filesystem power-loss qualification

Status: Linux VM matrix passed; physical DE25 cut pending, 2026-11-16.
This closes the storage correctness gate, not broad performance qualification.

## What is cut

`emu/qemu/test-storage-power-cuts.py` runs the actual Astra MC68040 emulator,
confined Linux storage provider and native desktop inside a disposable Linux
KVM VM. The outer VM owns an ext4 system image and FAT32/exFAT USB disks.
The host kills that entire VM using its authenticated QMP peer PID and pidfd.
Linux page cache, provider state and native memory all disappear together.
Killing only the inner emulator cannot prove this contract.

Both emulators run unprivileged with all capability sets empty and
`NoNewPrivs=1`. Only the provider/mount setup inside the disposable VM runs
as root. The worker requires its private kernel boot marker and exact outer
volume label before touching devices or VM writeback settings. The controller
accepts regular-file inputs and generates fresh disks; no host physical block
device or production user image is used.

Outer drives use `cache=none`, preserving flush operations while avoiding an
extra host page cache. VM periodic dirty writeback is disabled before the
probe starts. Recovery uses a new Linux boot with the same generated disks.
This models loss of Linux memory, not every possible physical-drive firmware
failure. A separate DE25 electrical power cycle supplies hardware evidence.

## Oracle and boundaries

The existing native `posix` command writes through the normal POSIX adapter
and canonical Astra filesystem. Its private `GATE 2` handshake exposes:

| Boundary | Meaning | Required recovery |
|---|---|---|
| `READY` | No target mutation yet | Target absent; prior acknowledged baseline exact |
| `WRITTEN` | Write/stdio flush returned; native file sync has not run | Target unspecified; prior acknowledged baseline exact |
| `SYNCED` | File and retained parent-directory sync acknowledged | Target and prior acknowledged baseline exact |

The file remains open at `SYNCED`; neither native close nor graceful provider
shutdown can supply an accidental final barrier. A clean control exercises
all three writable formats before the nine boundary cuts. A deliberately
false early acknowledgement is cut before mutation; the same acknowledged-data
oracle must reject its missing target. Separate assert-based oracle tests
reject missing/prefix data, damaged baselines and invalid phases.

Each recovery permits standard journal replay/repair, boots native Astra,
checks bytes through native commands, performs clean native shutdown 88, and
requires read-only ext4/FAT32/exFAT fsck to return zero. Unacknowledged FAT/exFAT
writes do not acquire a journal or atomic-write promise from this test.
Existing `fssem durable-save` covers temporary-write, file sync, rename and
directory sync semantics; the cut matrix directly verifies the file/parent
durability acknowledgement on which that sequence depends.

Haiku reference: `src/add-ons/kernel/file_systems/bfs/Journal.cpp:875-902`
flushes the device cache before declaring a transaction complete. Astra keeps
its existing Linux-owned filesystem and native sync chain; qualification
tests that boundary rather than adding a second cache or journal.

## Reproduction and evidence

Run on Beast using the current x86-64 **host-profile** M68k emulator and paired
storage provider. The generic QEMU output directory may hold the AArch64 board
build; host ELF pairing is checked before any VM starts. The fixture copies
installed Codec worker dependencies, processed udev rules and the selected
Linux kernel modules. USB devices must identify as `ID_BUS=usb`; stable mount
names use their own private root-owned state directory. Build only current
`posix` and `ntpd`; existing image helpers install them into a disposable copy
of the coherent seed.

Pass `--help` for the explicit Linux kernel, outer QEMU/firmware, KVM group and
fresh evidence directory inputs. Run under one bounded systemd user unit
(`MemoryMax=2560M`, `CPUQuota=150%`, `TasksMax=128`); do not launch a replacement
while the previous process remains live. `inputs.json` hashes actual products,
and each case retains console, process identity and disks. `results.json` is
written after each completed case. A final PASS requires all eleven cases.

Evidence root: Beast `/home/barry/astra-evidence/powerloss-20261116/`.
Setup failures 84-91 remain recorded. Run87 exposed `ntpd` waiting forever for
network synchronization before publishing READY. Its existing port/deadline
loop now publishes the configured service first; offline desktop startup and
an explicit failed NTP request pass without disabling the service. See
[clock contract](TIME.md). Run92 contains the current complete VM matrix.
All eleven cases pass in 8m44s: clean baseline, nine boundary cuts and rejected
early acknowledgement. Every recovered baseline and every acknowledged target
matches exactly. Native shutdown 88 and all three read-only fsck checks pass
after every recovery. `provider92-v2.log` separately passes native atomic save,
post-ack sync-loss refusal, provider reset/death, exact readback and offline fsck
in 49 seconds. Its first launcher omitted the systemd working directory; the
corrected invocation and original failure remain in evidence.

Physical trial uses a verified copy of the selected release, a disposable
coherent image and private Config Kit storage. Production is cleanly closed
before the trial. The user receives the power-cut signal only after native
file/parent acknowledgement is captured on Beast. Runtime-only systemd
overrides disappear on reboot; production HOME and settings are not test inputs.
