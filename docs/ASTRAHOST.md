# AstraHost FPGA Transport (v1.0)

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

A read transaction is three bytes. The FPGA returns the token in byte 1. Token
`0x00` means that no response byte is available; token `0x01` means byte 2 is
valid response data. AstraHost retries empty reads until the operation deadline.
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
argument, `0x04` overflow, `0x05` CRC failure, and `0x06` size failure.

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
| Stage 0 BRAM budget | 4 KiB |

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

## 6. Storage policy and recovery

AstraHost mounts the existing FAT12/16/32 or exFAT boot partition without
formatting and opens only `/ASTRA68.ROM` for reading. The production boot path
does not modify other files on the card. Astra's native filesystem will use a
versioned raw multi-sector SPI service for a partition the ESP32 never mounts.

The direct FPGA FAT loader is a separate recovery and simulation backend built
with `ASTRA_HOST_ENABLE=0`. It is not the production storage architecture and
does not weaken the SPI-only ESP32-to-FPGA rule.

## 7. Required verification

Changes to this boundary must preserve all of the following:

- standalone command-engine simulation;
- SPI slave tests under Icarus and Verilator;
- full pin-level MMU2 boot through stage 2 and POST;
- zero combinational SCCs after synthesis;
- successful ECP5 place and route at the selected clock targets;
- hardware boot and repeated transfer tests at the configured SPI rate;
- FPGA-only reset followed by a second complete SPI boot without resetting the
  ESP32.

Future raw-block, Wi-Fi, input, update, and debug services extend the versioned
SPI protocol with bounded queues, explicit backpressure, timeouts, and reset
generations. They do not add UART communication.
