# AstraHost FPGA Transport (v1.1)

AstraHost is the ESP32 firmware and FPGA transport used by Astra 68. This
document is the authoritative boundary between the ESP32 and FPGA.

## 1. Non-negotiable transport rule

**LOCKED:** Every ESP32-to-FPGA control, status, boot, storage, network, input,
and future service message crosses the SPI link. UART is never an
ESP32-to-FPGA transport, fallback, side channel, or recovery path.

The FPGA's Vesta UART remains an independent FTDI diagnostic console for POST,
bring-up, and hardware test traffic. ESP-IDF may also emit logs on the ESP32's
own console. Neither UART connects the two processors, and AstraHost must not
depend on either one.

## 2. Physical link and ownership

The ESP32 is the only SPI master. It owns the ULX3S SD electrical interface and
shares SCK, MOSI, and MISO between the SD card and FPGA using separate chip
selects.

| Signal | ESP32 GPIO | ULX3S signal |
|---|---:|---|
| SCK | 14 | SD clock |
| MOSI | 15 | SD command |
| MISO | 2 | SD data 0 |
| SD chip select | 13 | SD data 3 |
| FPGA chip select | 4 | SD data 1 |

Both chip selects must be driven high before the shared SPI peripheral is
enabled. The unselected target must release MISO. The production FPGA build
tri-states its direct SD master and drives MISO only while FPGA chip select is
low.

The FPGA link is SPI mode 0, MSB first. AstraHost v0.1 uses a 20 MHz SCK. A
higher rate may be selected only after simulation and hardware stress tests
show that protocol correctness and timing margins are preserved.

## 3. Byte transport

Each chip-select assertion starts a transport transaction:

| First MOSI byte | Operation |
|---:|---|
| `0x57` (`W`) | Queue all following MOSI bytes into the FPGA receive stream |
| `0x52` (`R`) | Read one response token and optional response byte |

A read transaction returns repeated token/data slots after the operation byte.
Token `0x00` means that no response byte is available; token `0x01` means the
following byte is valid response data. AstraHost uses bounded 512-byte bursts
and retries empty reads until the operation deadline.
Before `IDENTIFY`, AstraHost drains responses until it observes two empty tokens
separated by a quiescence interval. This recovers from stale traffic when an
older SPI client was active while the FPGA image changed.

The FPGA crosses the asynchronous SCK and system-clock domains through separate
receive and transmit FIFOs. FIFO overflow and read underflow are sticky hardware
diagnostics; no UART retry path exists.

## 4. Boot protocol

All integers in command payloads are big-endian. Every command produces a
response whose first byte is a status code.

| Command | Code | Request payload | Response |
|---|---:|---|---|
| `IDENTIFY` | `0x01` | none | status, `A68H`, version, capabilities, state |
| `BOOT_STATUS` | `0x02` | none | status, flags, error, bytes received |
| `BOOT_BEGIN` | `0x10` | size, CRC32, native SDRAM offset | status |
| `BOOT_DATA` | `0x11` | count, then 1-256 bytes; count 0 means 256 | status |
| `BOOT_COMMIT` | `0x12` | none | status |
| `BOOT_ABORT` | `0x13` | none | status |

Status codes are `0x00` OK, `0x01` bad command, `0x02` bad state, `0x03` bad
argument, `0x04` overflow, `0x05` CRC failure, `0x06` size failure, `0x07`
busy, and `0x08` stale generation.

The immutable FPGA stage 0 sets `BOOT_REQUESTED` after SDRAM BIST. AstraHost
then validates `/ASTRA68.ROM`, sends `BOOT_BEGIN`, streams the payload in
256-byte chunks, and sends `BOOT_COMMIT`. The FPGA accepts commit only after all
bytes reached SDRAM and the calculated CRC32 matches. It captures the initial
SP and PC from the first eight payload bytes. Stage 0 independently checks the
size, progress, SP, and PC before switching the ROM overlay and transferring
control.

AstraHost is a persistent service rather than a one-shot loader. After a
successful commit it waits while the current FPGA generation reports
`BOOT_DONE`. An FPGA-only reset or reconfiguration clears that state; AstraHost
then re-identifies the SPI endpoint and serves the next boot request without an
ESP32 reset.

The v1 boot contract is:

| Item | Value |
|---|---:|
| Virtual ROM aperture | `0xFFE00000..0xFFE3FFFF` |
| SDRAM physical load address | `0x03E00000` |
| SDRAM controller offset | `0x01E00000` |
| Maximum payload | 256 KiB |
| Stage 0 BRAM budget | 8 KiB (2048 32-bit words) |

The package format and host-side validation are implemented by
`sw/boot/package_rom.py` and `esp32/astrahost/main/astra_rom.c`.

## 5. CPU-visible boot state

Stage 0 uses these Vesta registers. They report the SPI boot engine; they do not
provide a second transport.

| Offset | Name | Description |
|---:|---|---|
| `0x0130` | `HOST_CTRL` | boot-request state |
| `0x0134` | `HOST_STATUS` | request, busy, done, error, link-seen flags |
| `0x0138` | `HOST_ROM_SIZE` | declared payload bytes |
| `0x013C` | `HOST_ROM_CRC32` | declared and verified CRC32 |
| `0x0140` | `HOST_INITIAL_SP` | captured reset SP |
| `0x0144` | `HOST_INITIAL_PC` | captured reset PC |
| `0x0148` | `HOST_BYTES_RECEIVED` | committed payload progress |
| `0x014C` | `HOST_ERROR` | protocol status code |

Writing `SYS_CTRL.SYS_HOST_BOOT_REQUEST` requests a transfer. Writing
`SYS_CTRL.SYS_BOOT_SDRAM` enables the SDRAM-backed ROM overlay after validation.

## 6. Runtime framing and services

Commands `0x20` and above use a framed request: command byte, big-endian
16-bit payload length, then exactly that many payload bytes. Payloads larger
than 270 bytes are drained and rejected so a malformed transaction cannot
desynchronize the next command. The FPGA permits one active block transfer and
uses bounded asynchronous queues between the CPU and 60 MHz service domains.

| Command | Code | Request payload | Response |
|---|---:|---|---|
| `SERVICE_HELLO` | `0x20` | host generation, media generation, state flags, media sectors, max sectors | status |
| `BLOCK_POLL` | `0x21` | none | status, valid, ID, op, flags, sectors, LBA, buffer, media generation, host generation |
| `BLOCK_PUSH` | `0x22` | ID, byte offset, count, CRC32, data | status |
| `BLOCK_FETCH` | `0x23` | ID, byte offset, count | status, ID, offset, count, data, CRC32 |
| `BLOCK_COMPLETE` | `0x24` | ID, completion status, sectors, detail | status |
| `INPUT_EVENT` | `0x30` | host generation, header, value, timestamp, device/sequence | status |

Block sectors are 512 bytes. A CPU request contains at most 16 sectors; SPI
data moves in aligned 256-byte chunks. Every chunk has an IEEE CRC32. Push,
fetch, completion, and input operations are idempotent for exact retries; a
duplicate with different content is rejected. The FPGA invalidates its CPU
cache view after host writes and flushes pending CPU writes before host reads.
The FPGA validates operation, count, flags, DMA range, media state, generation,
and LBA bounds before enqueueing. The ESP independently revalidates operation,
flags, host/media generations, write permission, and partition-relative bounds
before any SD read, write, or flush; neither side relies on the other as its
only safety boundary.

Completion statuses are `0=OK`, `1=I/O error`, `2=CRC error`, `3=bad request`,
`4=timeout`, `5=host reset`, and `6=media changed`. The CPU must match the ID,
host generation, and media generation before accepting a completion.

`SERVICE_HELLO` is both negotiation and a generation barrier. The ESP assigns
a new nonzero host generation to every runtime link session. If an older
session left work active, the FPGA completes it as host-reset before publishing
the new state. A five-second command watchdog completes abandoned work, clears
link/media state, and raises the Vesta storage state IRQ. Runtime commands are
rejected until a new HELLO succeeds. Regular polling is the link heartbeat.

Input events use one ordered queue. Header layout is class `[31:24]`, kind
`[23:16]`, and flags `[15:0]`; the device/sequence word is device ID
`[31:16]` and nonzero sequence `[15:0]`. Classes 1, 2, and 3 are keyboard,
pointer, and gamepad. The transport accepts normalized events; physical HID
producers are separate AstraHost modules.

## 7. Storage policy and recovery

AstraHost mounts the existing FAT12/16/32 or exFAT boot partition without
formatting and opens only `/ASTRA68.ROM` for reading. The production boot path
does not modify other files on the card. AstraFS uses the runtime raw
multi-sector SPI service for a separate partition the ESP32 never mounts.

The required GPT type GUID is `1A991104-9317-4CFD-B5EB-0402471570AC`.
AstraHost validates the protective MBR, primary GPT header CRC, complete entry
array CRC, every populated entry's bounds, uniqueness of the Astra partition,
and absence of overlap between it and every non-Astra partition. It never falls
back to the whole disk or the FAT/exFAT partition. Until exactly one valid
Astra partition exists, the runtime link remains available but reports no
media.

The direct FPGA FAT loader is a separate recovery and simulation backend built
with `ASTRA_HOST_ENABLE=0`. It is not the production storage architecture and
does not weaken the SPI-only ESP32-to-FPGA rule.

## 8. Required verification

Changes to this boundary must preserve all of the following:

- standalone command-engine simulation;
- queued descriptor validation and clock-domain-crossing simulation;
- CRC retry, generation change, timeout, link-loss, and recovery simulation;
- SPI slave tests under Icarus and Verilator;
- full pin-level MMU2 boot through stage 2, POST, and kernel entry;
- zero combinational SCCs after synthesis;
- successful ECP5 place and route at the selected clock targets;
- hardware boot and repeated transfer tests at the configured SPI rate;
- FPGA-only reset followed by a second complete SPI boot without resetting the
  ESP32.

Future Wi-Fi, update, and debug services extend the same versioned SPI protocol
with bounded queues, explicit backpressure, timeouts, and reset generations.
They do not add UART communication.
