#!/usr/bin/env python3
"""Optionally load a bitstream, then require a complete hardware POST over UART."""

from __future__ import annotations

import argparse
import glob
import re
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


def print_memory_rates(output: str) -> None:
    cpu_match = re.search(r"CPU:.* @ (\d+) Hz", output)
    payload_match = re.search(r"CPU memory cycles \((\d+) bytes payload\)", output)
    if not cpu_match or not payload_match:
        return
    cpu_hz = int(cpu_match.group(1))
    payload_bytes = int(payload_match.group(1))
    rows = re.findall(
        r"^\s+(8-bit|16-bit|32-bit)\s+write=(\d+)\s+read=(\d+)\s*$",
        output,
        flags=re.MULTILINE,
    )
    for width, write_cycles, read_cycles in rows:
        write_mbps = payload_bytes * cpu_hz / int(write_cycles) / 1_000_000
        read_mbps = payload_bytes * cpu_hz / int(read_cycles) / 1_000_000
        print(
            f"CPU memory {width}: write={write_mbps:.3f} MB/s "
            f"read={read_mbps:.3f} MB/s"
        )
    dma_match = re.search(
        r"Astraea DMA \((\d+) KiB\).*?^\s+fill=(\d+) copy=(\d+)\s*$",
        output,
        flags=re.MULTILINE | re.DOTALL,
    )
    if dma_match:
        dma_bytes = int(dma_match.group(1)) * 1024
        fill_mbps = dma_bytes * cpu_hz / int(dma_match.group(2)) / 1_000_000
        copy_mbps = dma_bytes * cpu_hz / int(dma_match.group(3)) / 1_000_000
        print(f"Astraea DMA: fill={fill_mbps:.3f} MB/s copy={copy_mbps:.3f} MB/s")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bit", help="load this bitstream into SRAM before capture")
    parser.add_argument("--loader", default="openFPGALoader")
    parser.add_argument("--port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument("--expect-build", help="required eight-digit hardware build ID")
    args = parser.parse_args()

    expected_build = None
    if args.expect_build:
        build_id = args.expect_build.removeprefix("0x").removeprefix("0X")
        if len(build_id) != 8 or any(c not in "0123456789abcdefABCDEF" for c in build_id):
            parser.error("--expect-build must be an eight-digit hexadecimal ID")
        expected_build = f"BUILD: 0x{build_id.upper()}".encode("ascii")

    if args.bit:
        subprocess.run(
            [args.loader, "--board", "ulx3s", args.bit],
            check=True,
            stdout=subprocess.DEVNULL,
        )

    started = time.monotonic()
    port = find_port(args.port)
    serial_port = open_serial(port, args.baud, started + 10.0)
    output = bytearray()
    events: list[tuple[float, str]] = []
    deadline = started + args.timeout

    with serial_port:
        while time.monotonic() < deadline:
            byte = serial_port.read(1)
            if not byte:
                continue
            output.extend(byte)
            if byte in b"WR].":
                events.append((time.monotonic() - started, byte.decode("ascii")))
            if b"POST FAILURE" in output:
                break
            if b"POST PASS" in output and (
                expected_build is None or expected_build in output
            ):
                break

    elapsed = time.monotonic() - started
    decoded_output = output.decode("ascii", errors="replace")
    sys.stdout.write(decoded_output)
    if output and output[-1:] != b"\n":
        sys.stdout.write("\n")
    print(f"POST capture elapsed={elapsed:.3f}s events={events}")
    print_memory_rates(decoded_output)

    if b"POST PASS" in output and (
        expected_build is None or expected_build in output
    ):
        return 0
    if b"POST FAILURE" in output:
        return 1
    print("POST capture timeout", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
