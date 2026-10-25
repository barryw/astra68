# Block formatters for Astra's Linux storage provider

Pinned upstream source archives and small patches keep FAT32 and exFAT format
operations on the provider's already verified, exclusively opened block-device
descriptor. No guest application sees these programs, Linux paths or file
descriptors. Native Disk Setup now reaches both formatters through its separate
administration grant and the provider's exact-source protocol.

| Upstream | Version | Source | License |
| --- | --- | --- | --- |
| [dosfstools](https://github.com/dosfstools/dosfstools/releases/tag/v4.2) | 4.2 | `dosfstools-4.2.tar.gz` | GPL-3.0-or-later; see `COPYING` inside archive |
| [exfatprogs](https://github.com/exfatprogs/exfatprogs/releases/tag/1.4.3) | 1.4.3 | `exfatprogs-1.4.3.tar.gz` | GPL-2.0-or-later; see `COPYING` and source headers inside archive |

Run `third_party/storage-format/build.sh host` or `... de25` on Beast. Script
verifies source hashes, applies patches, and writes binaries under
`build/storage-format/<target>/`. Those binaries are build products, not
installed provider dependencies yet. Run
`third_party/storage-format/tests/test_exclusive_fd.sh [host|de25]` on Beast
for disposable 128 MiB loop tests; `de25` runs AArch64 formatters with QEMU
user mode as the invoking non-root user, with a root-opened exclusive test FD,
and verifies output with host fsck builds.

The private `--astra-fd N` option accepts an inherited writable block-device
descriptor. Provider must open exact validated device with `O_RDWR|O_EXCL`,
retain it across worker execution, pass only that descriptor, and verify its
identity again before any write. Formatting must never fall back to device
paths or release the exclusive descriptor between validation and execution.
The shared storage worker executes either formatter as the non-root provider
user through an inherited descriptor. Discovery has one exact block-device
opener for both read probing and exclusive format admission. The host loop gate
exercises that worker and verifies FAT32/exFAT with fsck. Native Disk Setup and
provider control use this path; physical DE25 FAT32/exFAT format and automatic
remount gates passed on the disposable SanDisk Cruzer.
For partition tables, the provider-only worker uses stock `sfdisk` with fixed
arguments and `/proc/self/fd/3` while the descriptor remains exclusive. Its
typed MBR/GPT initialize/create/delete requests validate geometry and fields;
it suppresses the tool's immediate kernel reread. Provider must still fence
every sibling and holder and reread the table after the worker closes the
descriptor, before exposing the action to native clients.
An additional Beast gate dropped to the provider user before executing stock
`sfdisk` on `/proc/self/fd/3`; it failed to reopen the held block device with
`Permission denied`. The fixed-command worker therefore runs with the
provider's root privilege; native clients never supply a path or command.

Disposable Beast loop tests on 2026-10-23 proved FAT32/exFAT formatting on a
held `O_EXCL` descriptor with clean filesystem checks. Same test proved
`sfdisk` MBR/GPT initialize, create and delete through the actual worker,
followed by kernel reread on a `--partscan` loop. Both `host` and `de25`
variants pass; `de25` also runs the AArch64 FAT32/exFAT formatters under
QEMU user mode.
The AArch64 partition worker additionally passed the same MBR/GPT lifecycle
on a disposable DE25 `/dev/shm` loop with the board's installed `sfdisk`;
the reusable board gate is `tests/test_de25_partition_worker.sh` and its
Beast log is `/var/tmp/astra-partition-de25-worker-20261023.log`. It does not
edit the physical USB or qualify guest partition controls.
These tests do not qualify a native Disk Setup action or physical media.
