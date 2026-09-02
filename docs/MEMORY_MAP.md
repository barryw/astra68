# Astra 68 Address Map

This document is the authoritative registry of CPU-visible address ranges.
Per-device documents define registers inside each allocated aperture; they may
not allocate a new global range without updating this file.

The table describes the active QEMU machine on the Arty. All addresses are
physical MC68030 bus addresses. MMIO is
uncached and uses 32-bit, big-endian registers unless a device specification
explicitly says otherwise.

## Global map

| Start | End | Size | Owner | Access | State |
|---|---|---:|---|---|---|
| `0x00000000` | `0x0007FFFF` | 512 KiB | Arty-hosted system ROM low alias | RO | Implemented in QEMU |
| `0x01FF8000` | `0x01FFFFFF` | 32 KiB | Bootstrap scratch BRAM | RWX | Implemented |
| `0x02000000` | `0x09FFFFFF` | 128 MiB | SDRAM CPU aperture, Arty-hosted profile | RWX | Implemented in QEMU |
| `0xFFE00000` | `0xFFE7FFFF` | 512 KiB | Arty-hosted system ROM aperture | RX | QEMU host memory; no PL or guest-RAM cost |
| `0xFFF00000` | `0xFFF0FFFF` | 64 KiB | Vesta system and I/O | MMIO | Allocated |
| `0xFFF10000` | `0xFFF1FFFF` | 64 KiB | Astraea DMA/blitter/copper | MMIO | Allocated |
| `0xFFF20000` | `0xFFF2FFFF` | 64 KiB | Vega video | MMIO | Allocated |
| `0xFFF30000` | `0xFFF3FFFF` | 64 KiB | Reserved device aperture | MMIO | Unimplemented |
| `0xFFF40000` | `0xFFF40FFF` | 4 KiB | OHCI USB host | MMIO | Implemented |

## ROM budgets

The active Arty-hosted ROM is a 512 KiB QEMU memory region. It consumes ARM
host memory, not FPGA BRAM or guest SDRAM, and ends below the Vesta MMIO
aperture at `0xFFF00000`.

**What may live in ROM:** exactly the chain that runs before a filesystem
exists, plus enough to explain a failure when one never will — reset vectors and
firmware, the kernel, the initial user image, the block-device service, a
read-only reader for the boot volume, and the console font. Everything else is a
file on storage: filesystem stacks beyond the boot volume, terminal, shell,
fonts, desktop, applications, and diagnostics beyond POST.


lwext4 is the load-bearing example. It is 64–74 KiB of MC68030 text
(`docs/STORAGE_AND_VFS.md`) and currently lives in the embedded Supervisor only
long enough to mount the boot volume and launch the storage service. The
initial-image reservation is 256 KiB at `0x02004000..0x02043fff`; firmware
reserves only the pages the image actually occupies and returns the rest to the
allocator. Later programs are files and use the transactional streaming loader,
so this firmware-only reservation is not an application-size ceiling.

The current image size and retained identity are recorded in
`CURRENT_STATE.md`. Loadable images are LZ4-compressed and CRC-32 verified after
decode.

## Boot reservations

The boot ABI publishes sorted physical ranges in `AstraBootInfo`; the kernel
must not infer them from linker symbols.

| Start | End | Size | Type | Initial ownership |
|---|---|---:|---|---|
| `0x01FF8000` | `0x01FFFFFF` | 32 KiB | Firmware scratch, `BootInfo`, stack | Firmware until handoff data is copied |
| `0x02000000` | `0x02003FFF` | 16 KiB | Early ring log | Kernel diagnostics |
| `0x02004000` | `0x02043FFF` | 256 KiB | Initial user image, then usable RAM | Firmware for the pages the image fills; allocator for the rest |
| `0x02044000` | `0x020C3FFF` | 512 KiB | Kernel image, BSS, and guarded stacks | Kernel |
| `0x020C4000` | `0x020D3FFF` | 64 KiB | Retained kernel trace | Kernel diagnostics |
| `0x020D4000` | `0x02153FFF` | 512 KiB | Frame and ownership metadata for up to 128 MiB | Kernel |
| `0x02154000` | `0x02353FFF` | 2 MiB | Kernel object and scheduler tables | Kernel |
| `0x02354000` | `0x03EFFFFF` | 27.668 MiB | Usable RAM | Physical-page allocator |
| `0x03F00000` | `0x03FFFFFF` | 1 MiB | OHCI DMA pool | Device-owned, uncached |
| `0x04000000` | `0x09FFFFFF` | 96 MiB | Usable RAM | Physical-page allocator |

`AstraBootInfo` itself begins at `0x01FF8000` and is 268 bytes as of boot ABI
0.6. The linker reserves the first 1 KiB of bootstrap BRAM for the handoff
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

The kernel's large per-frame tables live after the fixed trace ring, so crash
tooling keeps the same address. Firmware accepts a page-aligned RAM map
beginning at the early-log base
and extending through the fixed bootstrap reservations; Axiom sizes its frame
tables from the reported map. The production profile is 128 MiB Arty-hosted.

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
