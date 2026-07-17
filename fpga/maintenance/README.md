# ULX3S ESP32 maintenance passthrough

This bitstream exists only to program or inspect the ULX3S ESP32 through the
board's FTDI interface. It is not part of Astra68 and must never be used as an
application transport. Normal ESP32-to-FPGA boot, storage, input, network, and
control traffic is SPI-only.

The Astra68 ULX3S is attached to `nuc` (`barry@192.168.1.2`). Although this
physical board identifies itself as v3.0.8, hardware reset probing established
that its ESP32 uses the v3.1 pin route: EN `J5`, GPIO0 `F1`, RX `K3`, and TX
`K4`. ESP32 GPIO2 and SD D0 share FPGA pin `J3`. Serial download requires that
net low, while SD operation requires it released. The maintenance logic drives
it low only while FTDI DTR requests download mode and otherwise leaves it and
the other SD nets in high impedance. Permanently driving it low makes the card
fail at `SEND_IF_COND`; permanently pulling it high makes ESP flash operations
fail after the loader connects. The checked-in LPF is specific to this proven
board wiring.

Build and load the passthrough into volatile FPGA SRAM on `nuc`:

```sh
OSS=/home/barry/oss-cad-suite ./fpga/maintenance/build_esp32_passthru.sh
openFPGALoader --board ulx3s fpga/maintenance/build/esp32_passthru.bit
```

Then use `python3 -m esptool` on `/dev/ttyUSB0`. The Ubuntu esptool package on
`nuc` was observed without its stub JSON; add `--no-stub` when that package
raises `FileNotFoundError` for `stub_flasher_32.json`. The hardware-proven
conservative programming rate is 115200 baud:

```sh
python3 -m esptool --chip esp32 --port /dev/ttyUSB0 -b 115200 \
  --before default_reset --after hard_reset --no-stub write_flash \
  --flash_mode dio --flash_size 4MB --flash_freq 40m \
  0x1000 bootloader.bin 0x8000 partition-table.bin \
  0xd000 ota_data_initial.bin 0x10000 astrahost.bin
```

Capture a complete normal boot, including SD provisioning evidence, with:

```sh
./fpga/maintenance/capture_esp32_log.py /dev/ttyUSB0
```

Validation and staging on the installed 256 GB exFAT card took about 22
seconds even when the existing ROM already matched. Use at least a 30-second
capture and do not reset the board merely because output pauses after the card
information. Opening the FTDI serial device changes DTR/RTS state, so the
capture tool deliberately performs a deterministic normal reset rather than
claiming it can attach without disturbing the ESP32.

Loading only to SRAM is deliberate: a maintenance image must not replace the
persistent Astra68 FPGA image. Restore the normal AstraHost ESP32 firmware
after one-shot SD provisioning, then load and validate the exact release FPGA
image before writing it to persistent flash.
