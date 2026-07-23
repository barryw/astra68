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
    expect_video_probe: bool = False,
    expect_graphics: bool = False,
    expect_k1_entry: bool = False,
    expect_k1_soak_cycles: int | None = None,
    expect_kernel_panic: bool = False,
    expected_panic_fault: bytes | None = None,
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
    if expect_video_probe:
        return re.search(
            rb"ASTRA VIDEO PROBE id=56454741 caps=00000077 "
            rb"ctrl=00000000 before=000000(?:20|41) "
            rb"first=00000041 last=00000045\r?\n",
            output,
        ) is not None
    if expect_graphics:
        return re.search(rb"(?m)^GFX PASS\r?$", output) is not None
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
    if expect_kernel_panic:
        post_offset = output.find(b"POST PASS")
        panic_offset = output.find(
            b"*** ASTRA KERNEL PANIC ***", post_offset + 1
        )
        halt_offset = output.find(b"SYSTEM HALTED", panic_offset + 1)
        panic_reached = post_offset >= 0 and panic_offset >= 0 and halt_offset >= 0
        if panic_reached and expected_panic_fault is not None:
            panic_reached = re.search(
                rb"(?m)^Fault:[ \t]+0x"
                + re.escape(expected_panic_fault)
                + rb"\r?$",
                output[panic_offset:halt_offset],
            ) is not None
        kernel_entry_reached = panic_reached
    elif expect_k1_soak_cycles is not None:
        protected_entry_offset = output.find(b"K1 PROTECTED ENTRY PASS")
        soak_reached = False
        for match in re.finditer(
            rb"(?m)^K1 SOAK cycles=(\d+) switches=(\d+) "
            rb"ticks=(\d+) syscalls=0x([0-9A-Fa-f]{16}) free=(\d+)\r?$",
            output,
        ):
            if (
                protected_entry_offset >= 0
                and match.start() > protected_entry_offset
                and int(match.group(1)) >= expect_k1_soak_cycles
                and int(match.group(2)) != 0
                and int(match.group(3)) >= expect_k1_soak_cycles
                and int(match.group(4), 16) != 0
                and int(match.group(5)) != 0
            ):
                soak_reached = True
                break
        kernel_entry_reached = soak_reached
    elif expect_k1_entry:
        kernel_entry_reached = b"K1 PROTECTED ENTRY PASS" in output
    elif expect_kernel_entry:
        kernel_entry_reached = b"K0 ENTRY PASS" in output
    else:
        kernel_entry_reached = True
    return (
        b"POST PASS" in output
        and build_reached
        and rom_crc_reached
        and kernel_entry_reached
    )


def failure_reached(
    output: bytes | bytearray, expected_kernel_panic: bool = False
) -> bool:
    return (
        b"POST FAILURE" in output
        or (
            not expected_kernel_panic
            and b"*** ASTRA KERNEL PANIC ***" in output
        )
        or re.search(rb"(?m)^GFX (?:FAIL |F)[0-9A-F]{2}\r?$", output) is not None
    )


def loader_command(loader: str, bit: str, program_flash: bool) -> list[str]:
    command = [loader, "--board", "ulx3s"]
    if program_flash:
        command.extend(("-f", "-r"))
    command.append(bit)
    return command


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
    parser.add_argument("--bit", help="configure this bitstream before capture")
    parser.add_argument("--loader", default="openFPGALoader")
    parser.add_argument(
        "--program-flash",
        action="store_true",
        help="write --bit to persistent FPGA flash and reset before capture",
    )
    parser.add_argument("--port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument("--expect-build", help="required eight-digit hardware build ID")
    parser.add_argument("--expect-rom-crc", help="required eight-digit system ROM CRC32")
    kernel_entry_group = parser.add_mutually_exclusive_group()
    kernel_entry_group.add_argument(
        "--expect-kernel-entry",
        action="store_true",
        help="require the legacy kernel to report K0 ENTRY PASS",
    )
    kernel_entry_group.add_argument(
        "--expect-k1-entry",
        action="store_true",
        help="require the protected kernel to report K1 PROTECTED ENTRY PASS",
    )
    kernel_entry_group.add_argument(
        "--expect-k1-soak-cycles",
        type=int,
        metavar="COUNT",
        help="require a validated K1 soak checkpoint at or beyond COUNT cycles",
    )
    kernel_entry_group.add_argument(
        "--expect-kernel-panic",
        action="store_true",
        help="require a complete kernel panic and halt after POST",
    )
    parser.add_argument(
        "--expect-panic-fault",
        metavar="ADDRESS",
        help="require an eight-digit fault address in the expected panic",
    )
    parser.add_argument(
        "--expect-route-probe",
        action="store_true",
        help="require one complete diagnostic route-probe line instead of POST",
    )
    parser.add_argument(
        "--expect-video-probe",
        action="store_true",
        help="require Vega identity and retained text-aperture probe values",
    )
    parser.add_argument(
        "--expect-graphics",
        action="store_true",
        help="require the complete graphics diagnostic to report GFX PASS",
    )
    args = parser.parse_args()

    if args.program_flash and not args.bit:
        parser.error("--program-flash requires --bit")
    if args.expect_k1_soak_cycles is not None and args.expect_k1_soak_cycles <= 0:
        parser.error("--expect-k1-soak-cycles must be positive")
    if args.expect_panic_fault and not args.expect_kernel_panic:
        parser.error("--expect-panic-fault requires --expect-kernel-panic")

    diagnostic_modes = sum(
        (args.expect_route_probe, args.expect_video_probe, args.expect_graphics)
    )
    if diagnostic_modes and (
        args.expect_build
        or args.expect_rom_crc
        or args.expect_kernel_entry
        or args.expect_k1_entry
        or args.expect_k1_soak_cycles is not None
        or args.expect_kernel_panic
        or args.expect_panic_fault
    ):
        parser.error(
            "diagnostic expectations cannot be combined with POST expectations"
        )
    if diagnostic_modes > 1:
        parser.error("select only one diagnostic expectation")

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

    expected_panic_fault = None
    if args.expect_panic_fault:
        panic_fault = (
            args.expect_panic_fault.removeprefix("0x").removeprefix("0X")
        )
        if len(panic_fault) != 8 or any(
            c not in "0123456789abcdefABCDEF" for c in panic_fault
        ):
            parser.error(
                "--expect-panic-fault must be an eight-digit hexadecimal value"
            )
        expected_panic_fault = panic_fault.upper().encode("ascii")

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
                loader_command(args.loader, args.bit, args.program_flash),
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
                if acceptance_reached(
                    output,
                    expected_build,
                    args.expect_kernel_entry,
                    args.expect_route_probe,
                    expected_rom_crc,
                    args.expect_video_probe,
                    args.expect_graphics,
                    args.expect_k1_entry,
                    args.expect_k1_soak_cycles,
                    args.expect_kernel_panic,
                    expected_panic_fault,
                ):
                    break
                if failure_reached(output, args.expect_kernel_panic):
                    break
    finally:
        if serial_port.is_open:
            serial_port.close()

    elapsed = time.monotonic() - started
    decoded_output = output.decode("ascii", errors="replace")
    sys.stdout.write(decoded_output)
    if output and output[-1:] != b"\n":
        sys.stdout.write("\n")
    capture_name = (
        "route probe" if args.expect_route_probe else
        "video probe" if args.expect_video_probe else
        "graphics" if args.expect_graphics else
        "kernel panic" if args.expect_kernel_panic else
        "K1 soak" if args.expect_k1_soak_cycles is not None else
        "POST"
    )
    print(f"{capture_name} capture elapsed={elapsed:.3f}s events={events}")
    print_memory_rates(decoded_output)

    if acceptance_reached(
        output,
        expected_build,
        args.expect_kernel_entry,
        args.expect_route_probe,
        expected_rom_crc,
        args.expect_video_probe,
        args.expect_graphics,
        args.expect_k1_entry,
        args.expect_k1_soak_cycles,
        args.expect_kernel_panic,
        expected_panic_fault,
    ):
        return 0
    if failure_reached(output, args.expect_kernel_panic):
        return 1
    print(f"{capture_name} capture timeout", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
