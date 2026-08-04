#!/usr/bin/env python3
"""Exercise the running Axiom kernel monitor over the ULX3S FTDI UART."""

from __future__ import annotations

import argparse
import re
import time

try:
    from .check_hardware import find_port, open_serial
except ImportError:
    from check_hardware import find_port, open_serial


MONITOR_COMMANDS = ("build", "irqs", "mem", "trace", "devices")


def normalize_build_id(value: str) -> bytes:
    build_id = value.removeprefix("0x").removeprefix("0X")
    if len(build_id) != 8 or any(
        character not in "0123456789abcdefABCDEF" for character in build_id
    ):
        raise ValueError("build ID must be eight hexadecimal digits")
    return build_id.upper().encode("ascii")


def validate_responses(
    responses: dict[str, bytes], expected_build: bytes, minimum_spi_commands: int
) -> tuple[int, int]:
    missing = [command for command in MONITOR_COMMANDS if command not in responses]
    if missing:
        raise ValueError(f"missing monitor responses: {', '.join(missing)}")

    for command, response in responses.items():
        if response.startswith(b"ERR "):
            raise ValueError(f"{command} failed: {response.decode('ascii', 'replace')}")

    build = re.fullmatch(
        rb"kernel=\S+ built=\S+ git=\S+ hw=([0-9A-Fa-f]{8})",
        responses["build"],
    )
    if build is None or build.group(1).upper() != expected_build:
        raise ValueError("build response does not match the expected FPGA identity")
    if re.fullmatch(
        rb"live=\d+ delivered=\d+ acked=\d+ dropped=\d+ storms=\d+ "
        rb"spurious=\d+ irqoff_max=\d+",
        responses["irqs"],
    ) is None:
        raise ValueError("malformed IRQ response")
    if re.fullmatch(
        rb"total=\d+ free=\d+ high_water=\d+ failures=\d+ owners=\d+",
        responses["mem"],
    ) is None:
        raise ValueError("malformed memory response")
    if re.fullmatch(
        rb"next=\d+ wraps=\d+ dropped=\d+(?: event=\d+ flags=0x[0-9A-Fa-f]{8} "
        rb"arg0=0x[0-9A-Fa-f]{8})?",
        responses["trace"],
    ) is None:
        raise ValueError("malformed trace response")

    devices = responses["devices"]
    if re.fullmatch(
        rb"system=0x[0-9A-Fa-f]{8} block=0x[0-9A-Fa-f]{8} input=[01] "
        rb"worker_max=\d+ mon_ftdi=\d+ mon_spi=\d+",
        devices,
    ) is None:
        raise ValueError("malformed device response")
    ftdi = re.search(rb"\bmon_ftdi=(\d+)\b", devices)
    spi = re.search(rb"\bmon_spi=(\d+)\b", devices)
    assert ftdi is not None and spi is not None
    ftdi_commands = int(ftdi.group(1))
    spi_commands = int(spi.group(1))
    # The devices renderer observes the four preceding FTDI commands. The
    # devices command itself is counted immediately after rendering.
    if ftdi_commands < len(MONITOR_COMMANDS) - 1:
        raise ValueError("FTDI monitor command count did not advance")
    if spi_commands < minimum_spi_commands:
        raise ValueError("AstraHost-SPI monitor command count is below the gate")
    return ftdi_commands, spi_commands


def query(port, command: str, timeout: float) -> bytes:
    port.write(command.encode("ascii") + b"\n")
    port.flush()
    deadline = time.monotonic() + timeout
    response = bytearray()
    while time.monotonic() < deadline:
        value = port.read(1)
        if not value:
            continue
        if value == b"\n":
            return bytes(response).rstrip(b"\r")
        response.extend(value)
        if len(response) > 128:
            raise RuntimeError(f"{command} response exceeded 128 bytes")
    raise RuntimeError(f"timed out waiting for {command} response")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--expect-build", required=True)
    parser.add_argument("--min-spi-commands", type=int, default=0)
    args = parser.parse_args()

    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.min_spi_commands < 0:
        parser.error("--min-spi-commands must be nonnegative")
    try:
        expected_build = normalize_build_id(args.expect_build)
    except ValueError as error:
        parser.error(str(error))

    port_name = find_port(args.port)
    port = open_serial(port_name, args.baud, time.monotonic() + 10.0)
    try:
        port.reset_input_buffer()
        responses = {
            command: query(port, command, args.timeout)
            for command in MONITOR_COMMANDS
        }
    finally:
        port.close()

    for command in MONITOR_COMMANDS:
        print(f"{command}: {responses[command].decode('ascii', 'replace')}")
    ftdi_commands, spi_commands = validate_responses(
        responses, expected_build, args.min_spi_commands
    )
    print(
        "ASTRA KERNEL MONITOR PASS "
        f"mon_ftdi={ftdi_commands} mon_spi={spi_commands}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
