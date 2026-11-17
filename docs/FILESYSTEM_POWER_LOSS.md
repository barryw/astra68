# Filesystem power-loss qualification

Status: Linux VM and physical DE25 durability gates passed; host-root recovery defect isolated, 2026-11-17.
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

Trial93 reached native desktop with zero restarts and the updated NTP service.
`arm93.log` records the actual file/parent acknowledgement while the file is
still open, before close; Beast flushes that evidence before the power signal.
The user confirms disconnecting/reconnecting the power supply. DE25 initially has
power LEDs but no display, UART or network. JTAG chip IDs remain correct;
CONFIG_STATUS has no configuration error and CONF_DONE/INIT_DONE set. This
initially locates investigation at boot/HPS. Following the requested full power
cycle, UART `if02` reveals U-Boot. Explicit boot captures Linux root journal
recovery failure and VFS panic with `EUCLEAN` (-117) on `/dev/mmcblk0p2`;
`uboot93-boot2303.log` is the evidence. This is the Linux host, not Astra's kernel.
Root has ext4 fast commits and pending recovery. Journal metadata is preserved;
its fast-commit log contains a zero-link inode and repeated file replacements.
A 64 MiB RAM-only ext4 reproducer on the exact DE25 Linux kernel now triggers
the same `JBD2: journal recovery failed`/`EUCLEAN` after fsync of an unlinked,
still-open file and a crash snapshot. Standard `e2fsck -p` recovers a copy
of that snapshot cleanly. Repeating the same snapshot/replay on an ext4 image
without fast commits mounts successfully on the same DE25 kernel
(`unlinked95-replay.log`, `unlinked96-control.log`). An
[upstream ext4 report](https://lists.openwall.net/linux-kernel/2026/08/24/987)
describes the same reproducer and error. Repaired root and matching control
confirm fast-commit replay as observed Linux host failure. Beast VM root lacked
`fast_commit`, so its successful cut matrix did not exercise this feature.

RAM-only netboot used the exact retained FPGA core, Linux kernel and DTB with
a private rescue initramfs. It set every SD device read-only, mounted none, and
provided public-key-only SSH on port 2222 with a UART-pinned host key. The
source-IP-restricted temporary TFTP server stopped after rescue access succeeded.
`rescue94/` holds verified 31,267,487,744-byte raw SD backup, SHA-256
`47acb9f84ba1e3b17dd9de7b2c933547645c346fbb3ad378bf754a1371bb0010`.
Bounded `astra-rescue94-backup.service` checked exact length and source/
destination SHA-256, fsynced and renamed after completion. Diagnose and test recovery on disposable
copies before original SD writes. Repaired clone passed `e2fsck -p` and a full
read-only fsck. Extracted actual post-cut trial image held both acknowledged
files byte-exact after its own journal replay. Only then was original Linux
root repaired with standard `e2fsck -p`; `tune2fs -O ^fast_commit` removed the
broken fast-commit feature while retaining the normal ext4 journal. Follow-up
fsck was clean. No data partition was formatted or replaced.

RAM-rescue `reboot -f` left HPS silent; RAM-only HPS SOF reload followed by
U-Boot `boot` restored normal startup. That reset/bootstrap behavior remains a
separate investigation. After RAM rescue, normal Linux/Astra reached stage 8.
A guarded private DE25
trial booted the actual post-cut image. Native `posix --durability-check`
confirmed both previous baseline and newly acknowledged file exactly. Native
`fssem volume-root mapped-usage unmap-errors` passed all three cases; guest
exited cleanly with status 88. Offline `e2fsck -p` returned clean and recorded
the changed Linux boot ID. The private trial restored all three normal
services; stage 8, zero restarts and selected release verified. Its 57-record
board evidence was copied and checksum-verified on Beast as
`board93-postcut.tar.gz`, SHA-256
`00cfe1a7b68d9f3fdfbac08bb73c6c9f5c83adf91242b982b4b3c4d76014dde2`.
Disposable trial state was removed from DE25; 8.5 GiB free. Beast's original
`board93/input.img` remains the pre-trial seed. The post-cut image is in the
verified archive and `rescue94/trial93-postcut.img`.
The focused development installer then installed only the qualified native
`/services/ntpd` file into the selected user image. It reported
`ASTRA_DE25_DEV_INSTALL PASS guest=1 host=0 removed=0`; normal stage 8, three
active services, zero restarts, only loop0 and 8.5 GiB free were rechecked.

Production Linux root policy: use ext4's standard journal without
`fast_commit`. For new DE25 root media, create ext4 with `-O ^fast_commit`;
for existing media, first preserve a raw backup, run `e2fsck -p` offline, then
`tune2fs -O ^fast_commit` and another offline `e2fsck -p`. Verify
`dumpe2fs -h` shows clean filesystem, journal start zero and no `fast_commit`.
This avoids a reproduced upstream replay defect; no Astra filesystem contract
changes. Keep this policy until a corrected Linux kernel passes the same
unlinked-file crash test on DE25.

VM archive: `/home/barry/astra-evidence/powerloss92-vm-20261116.tar.gz`,
958 sealed records, SHA-256
`5aacb7ab3a8ab11aea22bce2c936183f1be4b4682aeb0a13a7ab50cdeeea7c0f`.
