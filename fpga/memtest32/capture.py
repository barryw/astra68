#!/usr/bin/env python3
"""Load and capture the repeating Astra SDRAM32 hardware-test report."""

from __future__ import annotations

import argparse
import re
import subprocess
import time


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("bit")
    parser.add_argument("--loader", default="openFPGALoader")
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    subprocess.run(
        [args.loader, "--board", "ulx3s", args.bit],
        check=True,
        stdout=subprocess.DEVNULL,
    )

    import serial

    pattern = re.compile(
        rb"S32 L=([123]) R=([PF]) OP=([0-9A-F]{2}) "
        rb"E=([0-9A-F]{8}) A=([0-9A-F]{8})"
    )
    deadline = time.monotonic() + args.timeout
    data = bytearray()
    port = None
    while time.monotonic() < deadline:
        try:
            port = serial.Serial(args.port, 115200, timeout=0.1)
            break
        except (OSError, serial.SerialException):
            time.sleep(0.1)
    if port is None:
        print(f"serial port did not become accessible: {args.port}")
        return 2

    with port:
        while time.monotonic() < deadline:
            data.extend(port.read(256))
            match = pattern.search(data)
            if match:
                line = match.group(0).decode("ascii")
                print(line)
                return 0 if match.group(2) == b"P" else 1
            if len(data) > 4096:
                del data[:-1024]

    print(data.decode("ascii", errors="replace"))
    print("SDRAM32 hardware-test capture timeout")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
