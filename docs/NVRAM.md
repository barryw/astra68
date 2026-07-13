# Astra 68 Persistent Settings (v0.1)

This is a small persistent configuration store, not a BIOS. The boot ROM owns
validation and defaults; the operating system owns the settings UI. Neither
firmware services nor executable option ROMs are stored here.

## Boot Contract

1. Read both storage slots.
2. Reject a slot with bad magic, unsupported format, invalid lengths, or CRC.
3. Select the valid slot with the newest generation number.
4. Parse known TLVs and ignore unknown TLVs.
5. Use documented defaults if neither slot is valid.
6. Report the selected generation/backend during POST.

Invalid NVRAM must never prevent diagnostics or recovery boot.

## Record Format

All multibyte fields are big-endian. Each slot begins with this 32-byte header:

| Offset | Size | Field | Value / meaning |
|---|---:|---|---|
| `0x00` | 4 | magic | `0x41434647` (`ACFG`) |
| `0x04` | 2 | format version | `1` |
| `0x06` | 2 | header bytes | `32` |
| `0x08` | 4 | generation | monotonically increasing, wrapping |
| `0x0C` | 4 | payload bytes | maximum 4096 |
| `0x10` | 4 | payload CRC32 | IEEE CRC-32 |
| `0x14` | 4 | header CRC32 | header with this field zeroed |
| `0x18` | 4 | flags | zero for v1 |
| `0x1C` | 4 | reserved | zero |

The payload is a sequence of 4-byte-aligned TLVs: 16-bit type, 16-bit value
length, value bytes, then zero padding. Initial types are boot-device order,
console policy, video mode, POST policy, locale, keyboard layout, and machine
name. CPU clock, RAM size, and instantiated hardware are authoritative hardware
properties and are not overridden by NVRAM.

## Atomic Updates

Updates always target the inactive slot:

1. Erase inactive slot if the backend requires it.
2. Write header with invalid magic, then payload.
3. Read back and verify payload.
4. Write the final CRC-protected header and magic last.
5. Keep the previous slot intact until the new slot validates.

Loss of power at any point leaves at least one valid generation. Generation
comparison uses signed modulo-32-bit subtraction so wraparound is defined.

## Backends

### ULX3S Prototype

The detected ISSI IS25LP128 provides 16 MiB of configuration SPI flash. Astra
bitstreams currently occupy roughly the first 2.3 MiB. The prototype may
reserve the final two 64 KiB erase sectors at `0x00FE0000` and `0x00FF0000`.
The reservation must also be enforced by every FPGA programming/package tool.

Fabric access to configuration SPI requires the ECP5 user-clock path and a
small flash controller. Early POST images must remain read-only until that path
has hardware tests for address bounds, erase isolation, interrupted writes, and
bitstream recovery.

### Product Hardware

A dedicated I2C FRAM is preferred. It avoids configuration-flash coupling,
sector erase latency, and wear management. The same dual-slot record format and
Vesta-facing interface apply; only the backend capability bits differ.

## Vesta Interface

`VESTA.NVRAM_CAPS == 0` means no backend is present and the ROM uses defaults.
The future controller will expose backend type, capacity, busy/error status,
slot metadata, a bounded shadow-data window, and explicit load/commit commands.
Raw erase/program commands are not exposed to user mode.
