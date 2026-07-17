#!/usr/bin/env python3
"""Reset the ULX3S ESP32 into normal boot and capture its maintenance log."""

import argparse
import sys
import time

import serial


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("device", nargs="?", default="/dev/ttyUSB0")
    parser.add_argument("--seconds", type=float, default=30.0)
    args = parser.parse_args()

    port = serial.Serial()
    port.port = args.device
    port.baudrate = 115200
    port.timeout = 0.1
    port.dtr = False
    port.rts = False
    port.open()

    try:
        port.dtr = False  # GPIO0 released: normal boot
        port.rts = True   # EN low
        time.sleep(0.1)
        port.rts = False  # EN high

        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            data = port.read(port.in_waiting or 1)
            if data:
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
    finally:
        port.dtr = False
        port.rts = False
        port.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
