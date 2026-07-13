#!/usr/bin/env python3
"""Load a coretest bitstream and require a PASS result over UART."""

from __future__ import annotations

import argparse
import glob
import subprocess
import sys
import time


def find_port(explicit: str | None) -> str:
    if explicit:
        return explicit
    for pattern in ("/dev/ttyUSB*", "/dev/cu.usbserial*"):
        ports = sorted(glob.glob(pattern))
        if ports:
            return ports[0]
    return "/dev/ttyUSB0"


def open_serial(port: str, baud: int, deadline: float):
    import serial

    while time.monotonic() < deadline:
        try:
            return serial.Serial(port, baud, timeout=0.05)
        except (OSError, serial.SerialException):
            time.sleep(0.05)
    raise RuntimeError(f"serial port did not appear: {port}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bit", help="load this bitstream into SRAM before capture")
    parser.add_argument("--loader", default="openFPGALoader")
    parser.add_argument("--port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=180.0)
    args = parser.parse_args()

    if args.bit:
        subprocess.run(
            [args.loader, "--board", "ulx3s", args.bit],
            check=True,
            stdout=subprocess.DEVNULL,
        )

    started = time.monotonic()
    serial_port = open_serial(
        find_port(args.port), args.baud, started + min(args.timeout, 10.0)
    )
    output = bytearray()
    deadline = started + args.timeout

    with serial_port:
        while time.monotonic() < deadline:
            chunk = serial_port.read(256)
            if not chunk:
                continue
            output.extend(chunk)
            if b"CORETEST PASS" in output or b"CORETEST FAIL" in output:
                break

    elapsed = time.monotonic() - started
    decoded = output.decode("ascii", errors="replace")
    sys.stdout.write(decoded)
    if output and output[-1:] != b"\n":
        sys.stdout.write("\n")
    print(f"coretest capture elapsed={elapsed:.3f}s bytes={len(output)}")

    if b"CORETEST PASS" in output:
        return 0
    if b"CORETEST FAIL" in output:
        return 1
    print("coretest capture timeout", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
