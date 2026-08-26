# Astra 68 Address Map

This document is the authoritative registry of CPU-visible address ranges.
Per-device documents define registers inside each allocated aperture; they may
not allocate a new global range without updating this file.

The table describes the currently implemented production SoC, not a future
low-memory layout. All addresses are physical 68030 bus addresses. MMIO is
uncached and uses 32-bit, big-endian registers unless a device specification
explicitly says otherwise.

## Global map

| Start | End | Size | Owner | Access | State |
|---|---|---:|---|---|---|
| `0x00000000` | `0x00001FFF` | 8 KiB | Stage 0 reset alias | RO | Active while the reset overlay is selected |
| `0x00000000` | `0x0003FFFF` | 256 KiB | ULX3S system ROM low alias | RO | Active after the stage-0 handoff |
| `0x00000000` | `0x0007FFFF` | 512 KiB | Arty-hosted system ROM low alias | RO | Implemented in QEMU |
| `0x01FF8000` | `0x01FFFFFF` | 32 KiB | Bootstrap scratch BRAM | RWX | Implemented |
| `0x02000000` | `0x03FFFFFF` | 32 MiB | SDRAM CPU aperture, ULX3S profile | RWX | Implemented |
| `0x02000000` | `0x09FFFFFF` | 128 MiB | SDRAM CPU aperture, Arty-hosted profile | RWX | Implemented in QEMU |
| `0xFFE00000` | `0xFFE3FFFF` | 256 KiB | ULX3S system ROM aperture | RX | Alias of reserved SDRAM backing storage |
| `0xFFE00000` | `0xFFE7FFFF` | 512 KiB | Arty-hosted system ROM aperture | RX | QEMU host memory; no PL or guest-RAM cost |
| `0xFFF00000` | `0xFFF0FFFF` | 64 KiB | Vesta system and I/O | MMIO | Allocated |
| `0xFFF10000` | `0xFFF1FFFF` | 64 KiB | Astraea DMA/blitter/copper | MMIO | Allocated |
| `0xFFF20000` | `0xFFF2FFFF` | 64 KiB | Vega video | MMIO | Allocated |
| `0xFFF30000` | `0xFFF3FFFF` | 64 KiB | Lyra audio | MMIO | Reserved; not instantiated |
| `0xFFF40000` | `0xFFF40FFF` | 4 KiB | OHCI USB host | MMIO | Implemented |
| `0xFFFC0000` | `0xFFFC1FFF` | 8 KiB | Immutable stage 0 | RX | Implemented |

The ROM payload occupies SDRAM controller offsets
`0x01E00000..0x01E3FFFF`, visible through the normal SDRAM aperture at
`0x03E00000..0x03E3FFFF`. The OS must reserve that backing range while the ROM
aperture is in use.

## ROM budgets

The active Arty-hosted ROM is a 512 KiB QEMU memory region. It consumes ARM
host memory, not FPGA BRAM or guest SDRAM, and ends below the Vesta MMIO
aperture at `0xFFF00000`.

The legacy ULX3S ROM is a fixed 256 KiB window decoded in RTL — `boot_memory_map.sv` compares
`address[31:18]` for both the low alias and the `0xFFE00000` aperture, and the
SDRAM backing translation adds an 18-bit offset to `0x01E00000`. Enlarging it is
a bitstream change and therefore a timing-closure re-qualification, so the
budget is fixed and software fits inside it.

**What may live in ROM:** exactly the chain that runs before a filesystem
exists, plus enough to explain a failure when one never will — reset vectors and
firmware, the kernel, the initial user image, the block-device service, a
read-only reader for the boot volume, and the console font. Everything else is a
file on storage: filesystem stacks beyond the boot volume, terminal, shell,
fonts, desktop, applications, and diagnostics beyond POST.

lwext4 is the load-bearing example. It is 64–74 KiB of MC68030 text
(`docs/STORAGE_AND_VFS.md`) and it does **not** belong in ROM: `sw/stage0` reads
FAT in a 2,020-byte image, so the boot volume is reachable without it and the
full filesystem stack can be loaded as an ordinary file.

That reasoning is sound but it is not what actually stops it, and the
difference matters when planning. Measured on hardware: a supervisor with
lwext4 linked in is 98,732 bytes raw and 57,350 compressed, and the resulting
ROM is 243,304 of 262,144 — **it fits here**. Firmware rejects it anyway with
`POST FAIL: user image exceeds its reservation`, because the initial user image
is capped at `ASTRA_USER_IMAGE_MAX_SIZE` = 48 KiB by the RAM hole between
`ASTRA_USER_IMAGE_ADDRESS` (`0x02004000`) and `ASTRA_KERNEL_LOAD_ADDRESS`
(`0x02010000`), not by this budget. Raising it is a boot ABI change that moves
the kernel. The ROM budget is therefore not the reason to load the filesystem
from a file; the user image reservation is.

The current Arty/QEMU software image is close to this same fixed ceiling. The
2026-08-08 event-durability checkpoint is 249,000 of 262,144 bytes, leaving
13,144 bytes. Its supervisor image is 106,840 bytes on disk (95,903 text, 12
data, 377,382 BSS) and compresses to 65,906 bytes. The embedded copy is made
with `objcopy --strip-all`; the separate linked ELF retains symbols for
debugging. Keeping symbol and string tables in the ROM copy previously spent
about 20 KiB of raw payload on bytes the machine never loads.

Measured at ROM v0.3 with boot ABI 0.3 (`astra68.rom` reports this on every
build):

| Payload | Bytes | Form |
|---|---:|---|
| kernel image | 85,032 | LZ4 of 129,244 |
| splash asset | 86,654 | LZ4 of 350,720 |
| firmware code, rodata, vectors | ~15,000 | uncompressed; it is the decoder |
| initial user image | 1,913 | LZ4 of 6,468 |
| **used** | **189,064** | 72.1% |
| **free** | **73,080** | |

Both loadable images ship as LZ4-legacy streams decoded into their load
addresses by the same decoder the splash has always used, then verified by
CRC-32 against a build-time constant. That replaced a read-back comparison
which compression made impossible, and it preserves what that comparison
actually proved: the destination RAM holds the intended image. Stronger codecs
were measured and rejected — gzip and xz save a further 28 KiB and 46 KiB but
cost roughly 1.5–3 s of decode at 12.5 MHz, plus a larger decoder, to buy space
that is not currently scarce.

## Boot reservations

The boot ABI publishes these sorted physical ranges in `AstraBootInfo`. They
are fixed for boot ABI 0.x and must not be inferred from linker symbols by the
kernel.

| Start | End | Size | Type | Initial ownership |
|---|---|---:|---|---|
| `0x01FF8000` | `0x01FFFFFF` | 32 KiB | Firmware scratch, `BootInfo`, stack | Firmware until handoff data is copied |
| `0x02000000` | `0x02003FFF` | 16 KiB | Early ring log | Kernel diagnostics |
| `0x02004000` | `0x02043FFF` | 256 KiB | Initial user image, then usable RAM | Firmware for the pages the image fills; allocator for the rest |
| `0x02044000` | `0x020C3FFF` | 512 KiB | Kernel image, BSS, and guarded stacks | Kernel |
| `0x020C4000` | `0x020D3FFF` | 64 KiB | Retained kernel trace | Kernel diagnostics |
| `0x020D4000` | `0x02153FFF` | 512 KiB | Frame and ownership metadata for up to 128 MiB | Kernel |
| `0x02154000` | `0x03DFFFFF` | 28.672 MiB | Usable RAM | Physical-page allocator |
| `0x03E00000` | `0x03E3FFFF` | 256 KiB | System-ROM backing | Firmware/ROM mapping |
| `0x03E40000` | `0x03FFFFFF` | 1.75 MiB | Usable RAM | Physical-page allocator |
| `0x04000000` | `0x09FFFFFF` | 96 MiB | Hosted-profile extension | Physical-page allocator on the 128 MiB profile only |

`AstraBootInfo` itself begins at `0x01FF8000` and is 268 bytes as of boot ABI
0.3. The linker reserves the first 1 KiB of bootstrap BRAM for the handoff
structure and ABI growth. The canonical definitions are in
`sw/include/astra/boot.h`.

The 256 KiB below the kernel is split at boot. Firmware copies the one initial
user image to `0x02004000`, publishes `user_image_base`/`user_image_size` in
`AstraBootInfo`, and declares only the page-rounded span it fills as firmware
memory; the remainder of the 256 KiB is handed to the physical allocator as
usual. Firmware memory is the correct class because the kernel reads those
bytes long after it has taken ownership of the map, and the allocator must
never hand them out. The image is capped at `ASTRA_USER_IMAGE_MAX_SIZE`
(256 KiB) and validation rejects any description that escapes a firmware range.

The kernel is linked once for both profiles. Its large per-frame tables live
after the fixed trace ring, so crash tooling keeps the same address while the
32 MiB physical profile pays only the static reservation, not a second kernel
image. Firmware accepts only the exact 32 MiB ULX3S and 128 MiB Arty-hosted
sizes; arbitrary intermediate layouts are rejected.

Addresses not listed above are unallocated. In particular, the current SoC
does not yet expose general SDRAM at `0x00040000..0x01FF7FFF`; software must use
the high SDRAM aperture until a deliberate low-memory mapping is implemented.

## Vesta sub-apertures

| Start | End | Size | Block | Specification |
|---|---|---:|---|---|
| `0xFFF00000` | `0xFFF0014F` | 336 B | Identity, POST, diagnostics, AstraHost boot | [VESTA.md](VESTA.md) |
| `0xFFF00150` | `0xFFF001B3` | 100 B | AstraHost runtime block service | [VESTA.md](VESTA.md) |
| `0xFFF00200` | `0xFFF002FF` | 256 B | Retired region-MMU, reserved | [VESTA.md](VESTA.md) |
| `0xFFF00300` | `0xFFF003FF` | 256 B | Interrupt controller | [VESTA.md](VESTA.md) |
| `0xFFF00400` | `0xFFF0041F` | 32 B | Timers | [VESTA.md](VESTA.md) |
| `0xFFF00500` | `0xFFF0050F` | 16 B | Diagnostic UART | [VESTA.md](VESTA.md) |
| `0xFFF00600` | `0xFFF0060B` | 12 B | Direct-SD recovery SPI | [VESTA.md](VESTA.md) |
| `0xFFF00700` | `0xFFF00727` | 40 B | AstraHost input event queue | [VESTA.md](VESTA.md) |
| `0xFFF00800` | `0xFFF0081F` | 32 B | Physical bus-fault diagnostics | [VESTA.md](VESTA.md) |
| `0xFFF01000` | `0xFFF01FFF` | 4 KiB | Front-panel GPIO | [VESTA.md](VESTA.md) |

The front-panel block deliberately owns a complete 4 KiB page. A protected OS
can delegate that page without also exposing reset, boot, storage, UART, or
interrupt-controller registers. Normal multitasking applications use NDK
leases so the OS can arbitrate LED ownership; direct mapping is reserved for
bare-metal or explicitly privileged clients.

## USB aperture

`0xFFF40000..0xFFF40FFF` exposes the integrated OHCI-compatible host
controller. The standard register block begins at offset `0x000`; software
definitions live in `sw/include/ohci.h`. Controller and DMA faults assert Vesta
interrupt source 7. The page is privileged platform MMIO and is owned by the
kernel USB stack rather than mapped directly into applications.

## Allocation rules

1. Add every CPU-visible aperture here before adding its RTL decode.
2. Allocate MMIO in page-sized blocks when user processes may receive direct
   PMMU mappings.
3. Record aliases explicitly and identify their backing storage.
4. Never reuse a published range with different semantics. Deprecate it and
   allocate a new range.
5. Public NDK headers contain APIs and logical constants, not these addresses.
   Physical addresses remain private to the NDK hardware backend and firmware.
