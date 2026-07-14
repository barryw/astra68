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

For initial board provisioning only, a validated package can be embedded in a
separate firmware build:

```sh
ASTRA_PROVISION_ROM=/path/to/astra68.rom ./build-docker.sh
```

That output is under `build-provision/`. It creates `/ASTRA68.ROM` only when the
file is absent, using `/ASTRA68.NEW` as a validated staging file. It never
overwrites the boot ROM or touches unrelated files. Normal builds contain no
provisioning payload or write path.

The package parser also has a native corruption/reset-vector test:

```sh
./test-host.sh
```
