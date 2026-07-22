#!/usr/bin/env python3
"""Optionally load a bitstream, then require a complete hardware POST over UART."""

from __future__ import annotations

import argparse
import glob
import queue
import re
import subprocess
import sys
import threading
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


def acceptance_reached(
    output: bytes | bytearray,
    expected_build: bytes | None,
    expect_kernel_entry: bool,
    expect_route_probe: bool = False,
    expected_rom_crc: bytes | None = None,
) -> bool:
    if expect_route_probe:
        match = re.search(
            rb"ASTRA ROUTE PROBE id=56535441 "
            rb"sys=[0-9A-Fa-f]{8} mem=[0-9A-Fa-f]{8} "
            rb"err=[0-9A-Fa-f]{8} host=[0-9A-Fa-f]{8} "
            rb"cycles=([0-9A-Fa-f]{8})\r?\n",
            output,
        )
        return match is not None and int(match.group(1), 16) != 0
    build_reached = expected_build is None
    if expected_build is not None:
        build_id = expected_build.rsplit(b"0x", 1)[-1]
        build_reached = re.search(
            rb"(?m)^BUILD:[ \t]+0x" + re.escape(build_id) + rb"\r?$",
            output,
        ) is not None
    rom_crc_reached = expected_rom_crc is None or re.search(
        rb"(?m)^ROM CRC32[ .\t]+0x" + re.escape(expected_rom_crc) + rb"\r?$",
        output,
    ) is not None
    return (
        b"POST PASS" in output
        and build_reached
        and rom_crc_reached
        and (not expect_kernel_entry or b"K0 ENTRY PASS" in output)
    )


def failure_reached(output: bytes | bytearray) -> bool:
    return b"POST FAILURE" in output or b"*** ASTRA KERNEL PANIC ***" in output


def capture_serial(
    port: str,
    baud: int,
    ready: threading.Event,
    stop: threading.Event,
    received: queue.Queue[bytes],
    open_port=None,
) -> None:
    if open_port is None:
        open_port = open_serial
    first_connection = True
    while not stop.is_set():
        try:
            serial_port = open_port(port, baud, time.monotonic() + 0.25)
        except RuntimeError:
            continue
        try:
            if first_connection:
                serial_port.reset_input_buffer()
                first_connection = False
                ready.set()
            while not stop.is_set():
                byte = serial_port.read(1)
                if byte:
                    received.put(byte)
        except OSError:
            # FPGA configuration resets FTDI and invalidates the old handle.
            # Reopen immediately so bytes sent before the loader exits survive.
            pass
        finally:
            try:
                serial_port.close()
            except OSError:
                pass


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bit", help="load this bitstream into SRAM before capture")
    parser.add_argument("--loader", default="openFPGALoader")
    parser.add_argument("--port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument("--expect-build", help="required eight-digit hardware build ID")
    parser.add_argument("--expect-rom-crc", help="required eight-digit system ROM CRC32")
    parser.add_argument(
        "--expect-kernel-entry",
        action="store_true",
        help="also require the kernel to report K0 ENTRY PASS",
    )
    parser.add_argument(
        "--expect-route-probe",
        action="store_true",
        help="require one complete diagnostic route-probe line instead of POST",
    )
    args = parser.parse_args()

    if args.expect_route_probe and (
        args.expect_build or args.expect_rom_crc or args.expect_kernel_entry
    ):
        parser.error(
            "--expect-route-probe cannot be combined with POST expectations"
        )

    expected_build = None
    if args.expect_build:
        build_id = args.expect_build.removeprefix("0x").removeprefix("0X")
        if len(build_id) != 8 or any(c not in "0123456789abcdefABCDEF" for c in build_id):
            parser.error("--expect-build must be an eight-digit hexadecimal ID")
        expected_build = f"BUILD: 0x{build_id.upper()}".encode("ascii")

    expected_rom_crc = None
    if args.expect_rom_crc:
        rom_crc = args.expect_rom_crc.removeprefix("0x").removeprefix("0X")
        if len(rom_crc) != 8 or any(c not in "0123456789abcdefABCDEF" for c in rom_crc):
            parser.error("--expect-rom-crc must be an eight-digit hexadecimal value")
        expected_rom_crc = rom_crc.upper().encode("ascii")

    port = find_port(args.port)
    received: queue.Queue[bytes] = queue.Queue()
    capture_ready = threading.Event()
    capture_stop = threading.Event()
    capture_thread = threading.Thread(
        target=capture_serial,
        args=(port, args.baud, capture_ready, capture_stop, received),
        daemon=True,
    )
    capture_thread.start()
    if not capture_ready.wait(10.0):
        capture_stop.set()
        capture_thread.join(1.0)
        raise RuntimeError(f"serial port did not appear: {port}")

    output = bytearray()
    events: list[tuple[float, str]] = []
    try:
        if args.bit:
            subprocess.run(
                [args.loader, "--board", "ulx3s", args.bit],
                check=True,
                stdout=subprocess.DEVNULL,
            )
    finally:
        capture_stop.set()
        capture_thread.join(1.0)

    # openFPGALoader owns the FTDI device on ULX3S and may prevent the capture
    # worker from reconnecting until the loader exits. Open a fresh handle here
    # as well; any bytes the worker did catch remain queued below.
    serial_port = open_serial(port, args.baud, time.monotonic() + 10.0)
    try:
        started = time.monotonic()
        deadline = started + args.timeout
        with serial_port:
            while time.monotonic() < deadline:
                try:
                    byte = received.get_nowait()
                except queue.Empty:
                    byte = serial_port.read(1)
                    if not byte:
                        continue
                output.extend(byte)
                if byte in b"WR].":
                    events.append((time.monotonic() - started, byte.decode("ascii")))
                if failure_reached(output):
                    break
                if acceptance_reached(
                    output,
                    expected_build,
                    args.expect_kernel_entry,
                    args.expect_route_probe,
                    expected_rom_crc,
                ):
                    break
    finally:
        if serial_port.is_open:
            serial_port.close()

    elapsed = time.monotonic() - started
    decoded_output = output.decode("ascii", errors="replace")
    sys.stdout.write(decoded_output)
    if output and output[-1:] != b"\n":
        sys.stdout.write("\n")
    capture_name = "route probe" if args.expect_route_probe else "POST"
    print(f"{capture_name} capture elapsed={elapsed:.3f}s events={events}")
    print_memory_rates(decoded_output)

    if acceptance_reached(
        output,
        expected_build,
        args.expect_kernel_entry,
        args.expect_route_probe,
        expected_rom_crc,
    ):
        return 0
    if failure_reached(output):
        return 1
    print(f"{capture_name} capture timeout", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
