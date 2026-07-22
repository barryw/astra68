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
| `0x00000000` | `0x0003FFFF` | 256 KiB | System ROM low alias | RO | Active after the stage-0 handoff |
| `0x01FF8000` | `0x01FFFFFF` | 32 KiB | Bootstrap scratch BRAM | RWX | Implemented |
| `0x02000000` | `0x03FFFFFF` | 32 MiB | SDRAM CPU aperture | RWX | Implemented |
| `0xFFE00000` | `0xFFE3FFFF` | 256 KiB | System ROM aperture | RX | Alias of reserved SDRAM backing storage |
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

## Boot reservations

The boot ABI publishes these sorted physical ranges in `AstraBootInfo`. They
are fixed for ABI 0.1 and must not be inferred from linker symbols by the
kernel.

| Start | End | Size | Type | Initial ownership |
|---|---|---:|---|---|
| `0x01FF8000` | `0x01FFFFFF` | 32 KiB | Firmware scratch, `BootInfo`, stack | Firmware until handoff data is copied |
| `0x02000000` | `0x02003FFF` | 16 KiB | Early ring log | Kernel diagnostics |
| `0x02004000` | `0x0200FFFF` | 48 KiB | Usable RAM | Physical-page allocator |
| `0x02010000` | `0x0208FFFF` | 512 KiB | Kernel image, BSS, stack | Kernel |
| `0x02090000` | `0x03DFFFFF` | 29.4375 MiB | Usable RAM | Physical-page allocator |
| `0x03E00000` | `0x03E3FFFF` | 256 KiB | System-ROM backing | Firmware/ROM mapping |
| `0x03E40000` | `0x03FFFFFF` | 1.75 MiB | Usable RAM | Physical-page allocator |

`AstraBootInfo` itself begins at `0x01FF8000` and is 256 bytes. The linker
reserves the first 1 KiB of bootstrap BRAM for the handoff structure and ABI
growth. The canonical definitions are in `sw/include/astra/boot.h`.

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
