# AstraHost

AstraHost is Astra 68's ESP32 firmware. It is an ESP-IDF/FreeRTOS application,
independent of NovaHost.

The ESP32 owns the ULX3S SD electrical interface and is the only SPI master.
The SD card and FPGA share SCK/MOSI/MISO with separate chip selects:

| Signal | ESP32 GPIO | ULX3S signal |
|---|---:|---|
| SCK | 14 | SD clock |
| MOSI | 15 | SD command |
| MISO | 2 | SD data 0 |
| SD CS | 13 | SD data 3 |
| FPGA CS | 4 | SD data 1 |

All communication between AstraHost and the FPGA is SPI mode 0. UART is not a
transport, fallback, control channel, or data path. ESP-IDF log output may use
ESP UART0 only through a dedicated FPGA maintenance passthrough while
programming; the production FPGA image exposes no ESP UART path.

Version 0.1 mounts the existing FAT12/16/32 or exFAT partition without
formatting, opens only `/ASTRA68.ROM` with `rb`, validates both package CRCs and
reset vectors, and streams the payload into FPGA-owned SDRAM. Existing files
are never scanned, renamed, deleted, or modified.

AstraHost remains active after handoff. It waits while the current FPGA
generation reports boot complete, then re-identifies the link and serves the
same validated ROM after an FPGA-only reset or reconfiguration.

Protocol 1.1 also provides queued raw-block and normalized input-event
services. Raw storage is exposed only when the card has exactly one valid GPT
partition with Astra type GUID `1A991104-9317-4CFD-B5EB-0402471570AC`.
The parser verifies GPT header and entry-array CRCs and never falls back to the
whole disk or the boot volume. A card without that partition still boots and
reports the runtime link with media absent.

Block transfers are limited to 16 512-byte sectors and use CRC-protected,
idempotent 256-byte SPI chunks. Every runtime reconnect advances the host
generation, allowing the FPGA to fail partial work explicitly before accepting
new requests. Keyboard, pointer, and gamepad producers submit normalized
records to a bounded FreeRTOS queue; their transport to Vesta is SPI-only.

ESP-IDF 5.5.4 ships the upstream FatFs exFAT implementation but disables it in
`ffconf.h`. The pinned Docker build enables that implementation before compiling
and fails if the expected upstream configuration has changed. AstraHost never
offers to format or repair a volume when mounting fails.

The direct FPGA FAT loader remains a separate recovery bitstream. Production
images use AstraHost and a smaller immutable stage 0.

## Build

The project is pinned to ESP-IDF 5.5.4 by the Docker build helper:

```sh
./build-docker.sh
```

The production FPGA link defaults to 20 MHz. Hardware characterization builds
can override it without changing source:

```sh
ASTRA_FPGA_SPI_HZ=10000000 ./build-docker.sh
```

The output is under `build/`. Flashing is intentionally separate from building
so an ordinary build cannot alter the board or SD card.

For physical AstraHost-SPI kernel-monitor qualification, build the isolated
diagnostic image with:

```sh
ASTRA_MONITOR_SELFTEST=1 ./build-docker.sh
```

Its output is under `build-monitor-selftest/`. After runtime `HELLO`, it sends
`build`, `irqs`, `mem`, and `trace` through the SPI monitor protocol, validates
each bounded reply, logs `ASTRAHOST MONITOR SELFTEST PASS`, and continues the
normal storage/input service. The bounded response deadline allows an early
request to wait for guarded-worker startup. A subsequent FTDI `devices` command
reports the observed `mon_spi` count. Normal builds do not contain or run this
test.

For initial board provisioning only, a validated package can be embedded in a
separate firmware build:

```sh
ASTRA_PROVISION_ROM=/path/to/astra68.rom ./build-docker.sh
```

That output is under `build-provision/`. It creates `/ASTRA68.ROM` only when the
file is absent, using `/ASTRA68.NEW` as a validated staging file. It never
overwrites the boot ROM or touches unrelated files. Normal builds contain no
provisioning payload or write path.

Each provisioning invocation regenerates the embedded ROM object even when
`build-provision/` already exists. Every bind-mounted payload appears at the
same container path, so relying only on Ninja's incremental timestamp check can
otherwise retain bytes from the previous payload.

An explicit maintenance build may replace an existing, valid boot ROM:

```sh
ASTRA_PROVISION_ROM=/path/to/astra68.rom ASTRA_PROVISION_REPLACE=1 \
    ./build-docker.sh
```

Replacement first validates `/ASTRA68.NEW`, renames the old image to
`/ASTRA68.OLD`, installs the new image, and removes the backup only after the
rename succeeds. A subsequent maintenance boot restores the backup if power
was lost between those renames. No path outside these three root files is
opened for writing. Normal production firmware remains read-only.

The native tests cover package corruption/reset vectors and GPT validation:

```sh
./test-host.sh
```
