# Astra68 QEMU TCG backend

This directory contains the Astra-owned delta against QEMU 9.2.4.  It exists
to run the unchanged Astra boot ROM and Axiom K1-K10 kernel suite through
QEMU's native ARMv7 TCG backend on the Arty Z7-20.

A QEMU result is accepted only when the exact ROM reaches the required kernel
markers and the shared MC68030 PMMU/restart tests pass.

The initial overlay adds the physical machine map and device mechanisms.  The
MC68030 PMMU target changes will be kept here as patches rather than depending
on an untracked QEMU checkout on a build host.

The keyboard and pointer transport can be certified independently of the guest
boot path:

```sh
./emu/qemu/test-input.py "$(./emu/qemu/build.sh host)"
```

The certifier injects QEMU input events and verifies Astra's big-endian MMIO
records, ordering, independent device sequences, queue bounds, sticky overflow,
and input IRQ assertion/deassertion.

## AstraHost block service

The machine implements the Vesta block registers at `0x150..0x1b0` over a host
image, so `sw/kernel/block.c` and the block half of `sw/kernel/platform.c` can
run in emulation. Attach an image with `if=none`, which is the interface QEMU
lets a machine claim without a qdev device behind it:

```sh
qemu-system-m68k -M astra68 -m 128M -bios astra_boot.bin \
    -drive if=none,format=raw,file=/data/astra/storage/astra.img
```

The machine defaults to the Arty guest's 128 MiB profile. The Arty launcher
uses a preallocated 128 MiB memory backend, so guest RAM is committed when QEMU
starts instead of competing with Linux on demand.

Sectors are 512 bytes. The hosted backend reports a 128-sector (64 KiB)
maximum; physical transports report their own limit through the same register.
One asynchronous host transfer is active at a time and completes through the
normal interrupt path. A reset cancels an active transfer, raises a pending
state change, and publishes a new host generation, which the guest clears
through `BLOCK_STATE_ACK`.

**The block service exists only when an image is attached.** Without one,
`BLOCK_ID` reads zero and `SYS_ASTRA_HOST` stays clear in `SYS_STATUS`, so the
boot path is exactly what it was before this device existed. With an image the
kernel reports `AstraHost runtime ... OK, media present`.

Certify the transport independently of the guest:

```sh
./emu/qemu/test-block.py "$(./emu/qemu/build.sh host)"
```

The certifier covers identity and geometry, the reset state change and its
interrupt, deferred completion, read and write data paths verified against the
host image, flush, completion field and generation reporting, pop and interrupt
deassertion, the write-one-to-clear error register, single-transfer queue-full
rejection, and every submission rejection: bad opcode, zero and oversized
counts, a flush carrying sectors, unaligned and out-of-range buffers, LBA past
the media, a transfer crossing the end of the media, a zero request ID, and
unknown flags.

On the Arty, `run-arty.sh` starts QEMU once and holds an exclusive runtime
lock. `astra-input-hotplug.py` watches stable udev keyboard and pointer paths
and adds or removes QEMU `input-linux` objects through QMP. Linux autorepeat is
suppressed; the Astra input service owns repeat policy. Attaching or removing a
USB input device does not restart QEMU or the guest. The same reconciler pins
the single TCG vCPU to CPU1 at nice -10 and QEMU's host I/O threads to CPU0;
failure to apply this optional tuning leaves the guest running.

CPU1 is the MC68030/PMMU execution core. CPU0 owns Linux device IRQs, QEMU
coordination and I/O, and future host-backed audio and fixed-math workers.
Those workers use preallocated queues and complete asynchronously; expensive
mixing, synthesis, or math never executes synchronously on the vCPU thread.
